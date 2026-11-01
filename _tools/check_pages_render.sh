#!/usr/bin/env bash
# ============================================================================
# _tools/check_pages_render.sh —— 用无头浏览器逐页、逐步把 HTML 打开一遍
# ----------------------------------------------------------------------------
# 为什么需要它（这一条是踩过坑才加的）：
#   visuals/<页>.html 的每一页把内容写成一个 JS 数组 window.STEPS = [...]。
#   只要这段脚本有**一处**语法错误 —— 多一个 `},`、少一个逗号、反引号没闭合 ——
#   整个数组就变成 undefined，visual.js 第一行 `Array.isArray(...) || []`
#   拿到空数组直接 return：**页面全白，但没有文件、没有编译、没有 ctest 会红**。
#   本仓一次批量替换就是这样把 7 个图解页静默打坏了 —— 只有真把页面打开才知道。
#
# 它查三件事（图解页的每一步都查）：
#   1. #counter 里出现「N / M」—— 说明 visual.js 真的跑起来了、这一帧有内容；
#   2. 渲染出来的 DOM 里有 <svg> —— 说明这一帧的画面画得出来；
#   3. 控制台没有 Uncaught（SyntaxError / TypeError / ReferenceError 都在这一类）。
#      注意 ①：--dump-dom 的输出**不包含**控制台，必须再加 --enable-logging=stderr，
#      Edge/Chrome 才会把 `Uncaught SyntaxError: Unexpected token '}'` 打到 stderr。
#      注意 ②：静态 HTML 骨架里本来就有 <svg> 装饰和空的 <div id="counter">，
#      所以「DOM 里有 svg」不足以证明渲染成功 —— 判据是 counter 里有数字，以及有无 Uncaught。
#
# 用法：
#   bash _tools/check_pages_render.sh                    # 全仓 HTML
#   bash _tools/check_pages_render.sh visuals            # 只查图解页（逐帧）
#   ⚠ 路径参数**只给一个**：这个脚本不像 check_svg_overflow.sh 那样收多个页面名，
#     多给一个它就直接退出 2（静默丢掉第二个会变成假绿）。
#   bash _tools/check_pages_render.sh events/index.html  # 只查某个文件
#   CHROME="/c/Program Files/Google/Chrome/Application/chrome.exe" \
#       bash _tools/check_pages_render.sh                # 指定浏览器可执行文件
#   VB=6000 bash _tools/check_pages_render.sh visuals    # 慢机器上多给点虚拟时间预算
#
# 依赖：本机任一 Chromium 系浏览器（Chrome / Edge / Chromium），脚本会自己找。
# 代价：图解页是「每步开一次浏览器」，16 页 103 步 ≈ 两三分钟，属正常。
# 期望值：最后一行 ALL CLEAN。任何一步画不出来就去改那一页的数据 ——
#         别放宽这里的判定，它红了就等于读者打开是一张空白页。
# ==========================================================================*/
set -u

# 【勘误·参数】这道门一次只吃**一个**路径，多的过去会被静默丢掉：
#   `check_pages_render.sh visuals/ painting/` 实际只查了 visuals/，末了照样打
#   「ALL CLEAN」—— 那是「以为查过了」的假绿，比报错更坏。
#   注意隔壁 check_svg_overflow.sh **是**吃多个页面名的（它自己组 PAGES 数组，
#   见那支脚本的 for a in "$@"），两个同族脚本口径不一样，所以这里宁可当场退出。
#   放在 set -u 之后、找浏览器之前：参数就不该走到「先花三秒找浏览器再拒绝」。
if [ "$#" -gt 1 ]; then
    echo "只能给一个路径参数（给了 $*）。多给会被忽略，而忽略 = 谎报「查过了」。" >&2
    echo "要查多个页面：传它们的共同目录，或者分几次跑。" >&2
    exit 2
fi

TOOLS_DIR="$(cd "$(dirname "$0")" && pwd)"   # 先记绝对路径，再 cd（见下面 source 的注释）
cd "$TOOLS_DIR/.." || exit 1

# find_browser 在 find_browser.sh：这个脚本和 check_svg_overflow.sh 都要开无头浏览器。
# 在 _tools/ 里直接 `bash check_pages_render.sh` 时 $0 是相对的，cd 之后再取 dirname
# 就指到仓库根、source 会失败 —— 所以顺序是「先算出 TOOLS_DIR，再 cd」。
. "$TOOLS_DIR/find_browser.sh"

BROWSER="$(find_browser)" || {
    echo "找不到 Chrome / Edge / Chromium。用 CHROME=<可执行文件> 再跑一次。" >&2
    exit 2
}
echo "浏览器：$BROWSER"

DOM_FILE="$(mktemp)"
trap 'rm -f "$DOM_FILE"' EXIT

# dump <url>：DOM 写进 $DOM_FILE，控制台 Uncaught 写进全局 CONSOLE_ERR
CONSOLE_ERR=""
dump_dom() {
    local cinf
    cinf="$(mktemp)"
    "$BROWSER" --headless=new --disable-gpu --no-sandbox --hide-scrollbars \
        --virtual-time-budget="${VB:-3000}" --enable-logging=stderr --v=0 \
        --dump-dom "$1" >"$DOM_FILE" 2>"$cinf"
    CONSOLE_ERR="$(grep -o 'Uncaught [^"]*' "$cinf" | head -1 | sed 's/^Uncaught //')"
    rm -f "$cinf"
}

to_url() {
    # -a 必须有：传相对路径（visuals/x.html）时 cygpath 原样返回，拼出来就是
    # file:///visuals/x.html —— 浏览器打不开，DOM 全空，于是整批页面假红（踩过）。
    local win
    win="$(cygpath -ma "$1")"                  # 绝对 + 正斜杠：C:/x/y.html
    case "$win" in /*) win="C:$win";; esac
    echo "file:///${win#/}"
}

target="${1:-.}"
FILES=()
if [ -f "$target" ]; then
    FILES=("$target")
elif [ -d "$target" ]; then
    # 【勘误·旧口径是 -not -path '*build*'】那条把**源码目录** build_deploy/ 也一起排掉了：
    #   本机数过 —— 仓库里作者写的 HTML 一共 30 个，旧写法只捞到 28，
    #   build_deploy/index.html 和 build_deploy/qt_cmake_guide.html 从没被这道门查过，
    #   而 README 写着「30 个页面已实测」—— 数字对不上就是这么来的。
    #   判据换成「目录里有没有 CMakeCache.txt」：有 = CMake 构建输出，跳过；没有 = 源码，查。
    # -not -name '_*'：以 _ 开头的 HTML 是工具临时生成的汇总页
    #   （check_svg_overflow.sh 会在 visuals/ 里落一个 _overflow_probe.html，跑完即删），
    #   别让它混进「页面总数」—— 上一版就是把它数成了第 17 页。
    PARGS=()
    while IFS= read -r d; do
        [ -n "$d" ] && PARGS+=(-e "$d/")
    done < <(find "$target" -type d 2>/dev/null | while IFS= read -r x; do
                 [ -f "$x/CMakeCache.txt" ] && printf '%s\n' "$x"; done)
    mapfile -t FILES < <(find "$target" -name '*.html' -not -name '_*' \
        | { if [ "${#PARGS[@]}" -gt 0 ]; then grep -vF "${PARGS[@]}"; else cat; fi; } \
        | sort)
else
    echo "路径不存在：$target" >&2; exit 2
fi
[ "${#FILES[@]}" -gt 0 ] || { echo "没找到 HTML 文件：$target" >&2; exit 2; }

fail=0
pages=0
steps=0

# 「这一页是不是图解页」要认 `window.STEPS = [` 这个**赋值**：
# visuals/index.html 的正文里也出现了 window.STEPS 这个词（讲解用），只 grep 名字会把它
# 误判成图解页，然后「数不出 step」假红一条（踩过）。
is_deck() { grep -qE 'window\.STEPS[[:space:]]*=[[:space:]]*\[' "$1"; }

# ── 一、图解页：逐帧打开 ────────────────────────────────────────────────────
for page in "${FILES[@]}"; do
    is_deck "$page" || continue
    pages=$((pages + 1))
    n=$(grep -c '^    title: ' "$page")
    [ "$n" -ge 1 ] || { echo "  ✗ $page：数不出 step"; fail=1; continue; }
    bad=0
    detail=""
    for i in $(seq 1 "$n"); do
        dump_dom "$(to_url "$page")#step-$i"
        counter="$(perl -0777 -ne 'print $1 if /id="counter"[^>]*>\s*([^<]*?)\s*</' "$DOM_FILE")"
        counter="$(printf '%s' "$counter" | tr -s ' ')"   # 把多余空白压成一个，便于精确比
        if [ -z "$CONSOLE_ERR" ] && [ "$counter" = "$i / $n" ]; then
            continue
        fi
        bad=$((bad + 1))
        note=""
        [ -s "$DOM_FILE" ] || note=" DOM 是空的（URL 或浏览器有问题，不是页面有问题）"
        detail="${detail}      step $i：counter='${counter:-空}'（期望 $i / $n）${CONSOLE_ERR:+ Uncaught='${CONSOLE_ERR}'}${note}"$'\n'
    done
    steps=$((steps + n))
    if [ "$bad" -gt 0 ]; then
        echo "  ✗ $page（$n 步里有 $bad 步没渲染）"
        printf '%s' "$detail" | head -4
        [ "$bad" -gt 4 ] && echo "      …（另有 $((bad - 4)) 步同类）"
        fail=1
    else
        printf '  ✓ %-36s %d 步全部渲染\n' "$(basename "$page")" "$n"
    fi
done

# ── 二、其余页面：能打开、没报错、目录页要有卡片 ────────────────────────────
for page in "${FILES[@]}"; do
    is_deck "$page" && continue
    pages=$((pages + 1))
    dump_dom "$(to_url "$page")"
    # 【勘误】原先是 grep -o 'class="card'（没有右引号），于是 card-grid / card-icon /
    # card-title 全被算进「卡片数」：本机实测 metaobject/index.html 明明只有 8 张卡，
    # 旧写法报 49；而 visuals/index.html 用的是 class="hub-card"，旧写法报 0（其实 16 张）。
    # 现在只认锚点上那几个**整词**类名，并且目录页/路线图页一张卡都没有就直接判失败。
    # 三个类名不是随手凑的：本机 grep 过 —— 阶段目录页用 card、图解专区用 hub-card、
    # 学习路线图用 vcard（16 张图）+ stage（11 个阶段卡）。旧写法给路线图页报的是 0，
    # 等于「这一页压根没查」，而 README 却写着它验过 11 个阶段卡、16 张图卡片。
    cards="$(grep -oE 'class="(card|hub-card|vcard|stage)"' "$DOM_FILE" | wc -l)"
    title="$(perl -ne 'print $1 and exit if /<title>(.*?)<\/title>/' "$DOM_FILE")"
    if [ ! -s "$DOM_FILE" ]; then
        echo "  ✗ $page：DOM 是空的（打不开？）"; fail=1; continue
    fi
    if [ -n "$CONSOLE_ERR" ]; then
        echo "  ✗ $page：控制台 Uncaught ${CONSOLE_ERR}"; fail=1; continue
    fi
    if [ -z "$title" ]; then
        echo "  ✗ $page：没有 <title>"; fail=1; continue
    fi
    # 「目录性质的页面」一张卡都没有 = 卡片链接写坏了。这两类页面都算：
    # 各阶段的 index.html，以及根上那张 学习路线图.html。
    base="$(basename "$page")"
    if { [ "$base" = "index.html" ] || [ "$base" = "学习路线图.html" ]; } && [ "$cards" -eq 0 ]; then
        echo "  ✗ $page：目录页一张卡片都没有（卡片链接写坏了？）"; fail=1; continue
    fi
    printf '  ✓ %-36s <title>=%s，卡片 %d 张\n' "$(basename "$page")" "$title" "$cards"
done

echo
echo "共 $pages 个页面、$steps 个图解步骤"
if [ "$fail" -eq 0 ]; then
    echo "ALL CLEAN"
    exit 0
fi
echo "有页面没渲染出来 —— 见上面打叉的条目"
exit 1
