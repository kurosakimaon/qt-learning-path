#!/usr/bin/env bash
# ============================================================================
# _tools/check_svg_overflow.sh —— 一次跑完 16 页图解的「文字捅出方框」体检
# ----------------------------------------------------------------------------
# 为什么需要它：
#   判据在 _tools/svg_overflow_check.js 里，但那个文件原本只能「打开一页 → F12 → 粘一遍」，
#   16 页就得手工点 16 次。README 里「剩下 13 页欢迎你自己跑」就是这么写的 ——
#   等于把回归测试交给了不会跑它的人。这个脚本把汇总页临时生成到 visuals/，
#   用同一个 runDeck() 把每一页的**每一步**都量一遍，结果写进 <pre id="result">，
#   再靠 headless --dump-dom 抓回来解析。
#
# 用法：
#   bash _tools/check_svg_overflow.sh                 # 全仓（自动列出图解页）
#   bash _tools/check_svg_overflow.sh thread_affinity # 只查某几页（不带 .html）
#   GAP=500 bash _tools/check_svg_overflow.sh         # 慢机器上把每步间隔放大
#   VB=400000 bash _tools/check_svg_overflow.sh       # 慢机器上把虚拟时间预算放大
#   CHROME=<可执行文件> bash _tools/check_svg_overflow.sh
#
# 三个坑（都是本机踩出来的）：
#   1. 汇总页必须**写在 visuals/ 里**：iframe 和 <script src> 都用相对路径，
#      放到别处就变成跨目录 file://，同源判定随时会变。文件名以 _ 开头，
#      跑完就删；check_pages_render.sh 也已经跳过 _ 开头的 HTML，不会被当成第 17 页。
#   2. 必须加 --allow-file-access-from-files，否则主页面读不到 iframe 的 window.STEPS，
#      每一页都报「拿不到文档」。（这是无头浏览器对 file:// 的默认隔离，不是页面坏了。）
#   3. 每步切换后要走真实导航按钮的 click()，别改回 location.hash ——
#      地址栏是 1 基的，从 0 数会把每页最后一步漏掉（svg_overflow_check.js 头注第 2 条）。
#
# 期望值：over=0 err=0。溢出条目给了容器 rect 的 x,y,w,h，照着改文案或改框；
#         别调 js 里的 TOLERANCE 让它闭嘴。
# ==========================================================================*/
set -u
TOOLS_DIR="$(cd "$(dirname "$0")" && pwd)"   # 先记绝对路径，再 cd（和 check_pages_render.sh 同一写法）
cd "$TOOLS_DIR/.." || exit 1
. "$TOOLS_DIR/find_browser.sh"

BROWSER="$(find_browser)" || {
    echo "找不到 Chrome / Edge / Chromium。用 CHROME=<可执行文件> 再跑一次。" >&2
    exit 2
}

# 图解页认 window.STEPS = [ 这个赋值（和 check_pages_render.sh 同一口径：
# index.html 正文里也出现了这个词，只 grep 名字会把它误当图解页）。
PAGES=()
if [ "${#}" -gt 0 ]; then
    for a in "$@"; do
        a="${a%.html}"
        [ -f "visuals/$a.html" ] || { echo "没有这一页：visuals/$a.html" >&2; exit 2; }
        PAGES+=("$a")
    done
else
    mapfile -t PAGES < <(grep -lE 'window\.STEPS[[:space:]]*=[[:space:]]*\[' visuals/*.html \
        | sed 's|.*/||; s|\.html$||' | sort)
fi
[ "${#PAGES[@]}" -gt 0 ] || { echo "visuals/ 里没找到图解页" >&2; exit 2; }

PROBE="visuals/_overflow_probe.html"
cleanup() { rm -f "$PROBE"; }
trap cleanup EXIT
[ -e "$PROBE" ] && { echo "$PROBE 已存在（上一次没扫干净？先删掉再跑）" >&2; exit 2; }

{
    echo '<!DOCTYPE html>'
    echo '<!-- 本文件由 _tools/check_svg_overflow.sh 生成，跑完即删，不要提交。 -->'
    echo '<html lang="zh-CN"><head><meta charset="utf-8"><title>svg overflow scan</title></head>'
    echo '<body><pre id="result">RUNNING</pre>'
    echo '<script src="../_tools/svg_overflow_check.js"></script>'
    echo '<script>'
    # 【坑】拼 JS 数组别用 "${PAGES[*]}"：[*] 用空格连接，16 页会拼成
    # ['a b c …'] —— 一个根本不存在的路径，整批只报一条错（单页测试时看不出来）。
    ITEMS="$(printf "'%s'," "${PAGES[@]}")"
    echo "const PAGES = [${ITEMS%,}];"
    cat <<'JS'
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const lines = [];
let stepsTotal = 0;
let done = 0;
// 每查完一页就把已有结果写回 #result：虚拟时间预算不够时 --dump-dom 抓到的就是
// 「跑到第几页」的现场，而不是干巴巴一个 RUNNING（16 页那轮就是这么看不见的）。
function report(final) {
  const over = lines.filter((l) => l.startsWith('OVER|')).length;
  const tight = lines.filter((l) => l.startsWith('TIGHT|')).length;
  const err = lines.filter((l) => l.startsWith('ERR|')).length;
  let head = 'SCAN|pages=' + PAGES.length + '|steps=' + stepsTotal + '|over=' + over
    + '|tight=' + tight + '|err=' + err;
  if (!final) head += '|partial=' + done;
  document.getElementById('result').textContent = head + (lines.length ? '\n' + lines.join('\n') : '');
}
async function run() {
  if (typeof SvgOverflow === 'undefined') {
    lines.push('ERR|probe|SvgOverflow 未定义：../_tools/svg_overflow_check.js 没加载成功');
    report(true); return;
  }
  for (const p of PAGES) {
    const f = document.createElement('iframe');
    f.style.cssText = 'width:1200px;height:900px';
    f.src = p + '.html';
    document.body.appendChild(f);
    await new Promise((res) => { f.onload = res; setTimeout(res, 8000); });
    const w = f.contentWindow;
    // 判据是「iframe 的文档能不能读」，**不是** w.SvgOverflow：
    // svg_overflow_check.js 只引进本汇总页，被量的那一页里没有这个全局，
    // 拿它当就绪判据会把每一页都判成「同源被挡」（上一版就是这么全红的）。
    if (!w || !w.document || !w.document.getElementById('steps')) {
      lines.push('ERR|' + p + '|拿不到 iframe 的文档（同源被挡？要加 --allow-file-access-from-files）');
      f.remove(); done++; report(false); continue;
    }
    try {
      const r = await SvgOverflow.runDeck(w);
      if (r.errors.length) r.errors.forEach((e) => lines.push('ERR|' + p + '|' + e));
      stepsTotal += r.steps;
      r.findings.forEach((x) => lines.push((x.kind === 'over' ? 'OVER|' : 'TIGHT|') + p + '|step' + x.step + '|[' + x.cls + ']'
        + ' 左' + x.oL + ' 上' + x.oT + ' 右' + x.oR + ' 下' + x.oB + ' rect=' + x.host + '|' + x.text));
    } catch (e) {
      lines.push('ERR|' + p + '|' + (e && e.message ? e.message : String(e)));
    }
    f.remove();
    done++;
    report(false);
  }
  if (done < PAGES.length) lines.push('ERR|probe|只跑完 ' + done + '/' + PAGES.length + ' 页（虚拟时间预算用完了？VB=更大 再跑）');
  report(true);
}
// run() 是异步的：没有这个 catch，任何意外都会让它停在 RUNNING，什么线索都不留。
run().catch((e) => {
  lines.push('ERR|probe|汇总页自己炸了：' + (e && e.stack ? e.stack.split('\n')[0] : String(e)));
  report(true);
});
JS
    echo '</script></body></html>'
} > "$PROBE" || { echo "生成 $PROBE 失败" >&2; exit 2; }

# 拼成标准形态 file:///C:/Users/.../visuals/_overflow_probe.html：
# cygpath -ma 给的是 C:/x/y（正斜杠 + 盘符），前面补三个斜杠就是浏览器认的写法。
WIN="$(cygpath -ma "$PROBE")"
case "$WIN" in /*) WIN="C:$WIN";; esac
# 用户名里带空格是常见的（C:\Users\My Name\...），不转 %20 的话路径里就断了
URL="file:///${WIN// /%20}"

OUT="$(mktemp)"
trap 'cleanup; rm -f "$OUT" "$OUT.parsed"' EXIT
echo "浏览器：$BROWSER"
echo "页面：${#PAGES[@]} 页（每页每一步都量）"
"$BROWSER" --headless=new --disable-gpu --no-sandbox --hide-scrollbars \
    --allow-file-access-from-files --virtual-time-budget="${VB:-240000}" \
    --dump-dom "$URL" > "$OUT" 2>/dev/null

# DOM 里 result 的内容会被 --dump-dom 原样打印；先把它整段取出来
perl -0777 -ne '
    s/\r\n/\n/g;
    if (/<pre id="result">(.*?)<\/pre>/s) { my $r = $1;
        $r =~ s/&lt;/</g; $r =~ s/&gt;/>/g; $r =~ s/&amp;/&/g; $r =~ s/&quot;/"/g;
        print $r; }
    else { print "NO_RESULT 浏览器没执行汇总页脚本（DOM 里没有 #result？）\n" }
' "$OUT" > "$OUT.parsed"

scan="$(head -1 "$OUT.parsed")"
if ! printf '%s' "$scan" | grep -q '^SCAN|'; then
    echo "✗ 没拿到结果：$scan" >&2
    echo '── 浏览器实际给的（前 12 行）──' >&2
    head -12 "$OUT.parsed" >&2
    exit 1
fi
# 【坑】明细在 #result 的第 2 行往后，必须从整个 $OUT.parsed 里 grep。
# 这里原来写的是 printf '%s\n' "$scan" | tail -n +2 —— $scan 只有第 1 行，
# tail 之后永远是空，于是「异常 1 条」却一条都不打印（改回去就又哑火，实测过）。
DETAIL="$OUT.parsed"
steps=$(printf '%s' "$scan" | sed 's/.*|steps=\([0-9]*\).*/\1/')
over=$(printf '%s' "$scan" | sed 's/.*|over=\([0-9]*\).*/\1/')
tight=$(printf '%s' "$scan" | sed 's/.*|tight=\([0-9]*\).*/\1/')
err=$(printf '%s' "$scan" | sed 's/.*|err=\([0-9]*\).*/\1/')

grep '^OVER|' "$DETAIL" | while IFS='|' read -r _ page step mid text; do
    printf '  ✗ 出界 %-18s %s %s\n          「%s」\n' "$page" "$step" "$mid" "$text"
done
grep '^TIGHT|' "$DETAIL" | while IFS='|' read -r _ page step mid text; do
    printf '  · 贴边 %-18s %s %s\n          「%s」\n' "$page" "$step" "$mid" "$text"
done
grep '^ERR|' "$DETAIL" | while IFS='|' read -r _ page msg; do
    printf '  ! %s：%s\n' "$page" "$msg"
done

echo
echo "共 ${#PAGES[@]} 页、$steps 步：出界 $over 处，贴边 $tight 处，异常 $err 条"
if [ "$over" -eq 0 ] && [ "$err" -eq 0 ]; then
    [ "$tight" -gt 0 ] && echo "贴边的那几处不出界，但换字体/换 DPI 就会出界 —— 有空顺手把 rect 放宽一点"
    echo "ALL CLEAN"
    exit 0
fi
echo "有文字捅出方框（或某页没量到）—— 改文案/改 rect，别调容差"
exit 1
