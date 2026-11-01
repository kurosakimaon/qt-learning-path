#!/usr/bin/env bash
# ============================================================================
# 一道命令跑完「不动 Qt 运行库、不改仓库文件」的全部源码门。
#
# 为什么要它：README 把门列成一张表，但表要人一条条敲。真正常见的事故不是
# 「某道门是红的」，而是「只跑了 check_syntax.sh，以为全绿，其实页面全没渲染」——
# 八条命令里漏跑哪条，没人看得出来。一条命令 + 一行一条判词，漏跑这件事就没了。
#
# 刻意**不放**进来的两样，各自的原因：
#   1) run_all.sh —— 它会重写 runlogs/*.log。校验命令不该有副作用：你跑一次
#      「检查」回来发现日志被覆盖，就再也不知道那份日志是哪一轮的了。要跑它请明说。
#   2) ctest —— 它要 build/ 已经配好、Qt 运行库在 PATH 上；本机这一轮 README 第二节
#      已经给了逐字命令，塞进这里只会让「没装 Qt 的人打开就一片红」，那红是噪声。
#
# 退出码：**任何一道真门变红就是 1**。浏览器找不到时那两道门记 SKIP（不是 0，
# 也不是红）——把「没跑」伪装成「跑过且过了」是这类脚本最容易犯的谎。
# ============================================================================
set -u

TOOLS_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$TOOLS_DIR/.." || exit 2
. "$TOOLS_DIR/find_browser.sh"

pass=0; fail=0; skip=0
FAILED=()

# 跑一道门：名字 / 命令。判词只留输出末尾两行（每个工具自己把结论打在最后），
# 完整输出落在 /tmp，变红时把路径印出来，省得你再手动复现。
run_gate() {
    local name="$1"; shift
    local log="/tmp/qtlp_gate_${name// /_}.txt"
    if "$@" >"$log" 2>&1; then
        local rc=0
    else
        local rc=1
    fi
    if [ "$rc" -eq 0 ]; then
        printf '  ✓ %-22s %s\n' "$name" "$(tail -1 "$log" | tr -d '\r')"
        pass=$((pass + 1))
    else
        printf '  ✗ %-22s %s\n' "$name" "$(tail -1 "$log" | tr -d '\r')"
        printf '      完整输出：%s\n' "$log"
        fail=$((fail + 1)); FAILED+=("$name")
    fi
}

echo "== 源码与讲义（不需要 Qt 运行库）=="
run_gate "JS 语法"        perl _tools/check_html_js.pl
run_gate "图解步骤一致"   perl _tools/check_visual_steps.pl
run_gate "控制台字形"     perl _tools/check_console_glyphs.pl
run_gate "字形门自测"     perl _tools/fix_console_glyphs.pl --selftest

# check_syntax.sh 要 moc 与 g++：缺了就 SKIP，因为这台上本来可能没装 Qt。
if command -v moc >/dev/null 2>&1; then
    run_gate "moc+语法"   bash _tools/check_syntax.sh
else
    echo "  · check_syntax.sh 跳过（PATH 上没有 moc）——它查的是「编不编得过」，别当已过"
    skip=$((skip + 1)); FAILED+=("moc+语法(未跑)")
fi

# 引用门要本机能找到 Qt 的头文件/源码；找不到时它自己会报「文件缺失」并变红，
# 这里不替它找借口，红就红——讲义里的行号对不上本机 Qt，本来就是要人看见的事。
run_gate "引用行号"       perl _tools/audit_citations.pl

echo "== 浏览器（无头）=="
BROWSER="$(find_browser)"
if [ -n "$BROWSER" ]; then
    echo "浏览器：$BROWSER"
    run_gate "页面渲染"   bash _tools/check_pages_render.sh
    run_gate "SVG 文字越界" bash _tools/check_svg_overflow.sh
else
    echo "  · 两道浏览器门跳过：没找到 Chrome/Edge/Chromium（设 CHROME=/path/to/browser 再跑）"
    skip=$((skip + 2)); FAILED+=("页面渲染(未跑)" "SVG文字越界(未跑)")
fi

echo
echo "共 $((pass + fail + skip)) 道门：过 $pass，红 $fail，跳 $skip"
if [ "$fail" -gt 0 ]; then
    echo "失败/未跑：${FAILED[*]}"
    exit 1
fi
# skip>0 时不算失败，但也不说「全绿」——话要说准。
if [ "$skip" -gt 0 ]; then
    echo "跑过的都过了；另有 $skip 道没跑（上面标了）。别把这条读成 ALL CLEAN。"
else
    echo "ALL CLEAN"
fi
