#!/usr/bin/env bash
# ============================================================================
# 逐个跑构建产物里的示例，捕获退出码与输出，并把 GBK 输出转成 UTF-8 存档。
#
# 用法：
#   bash _tools/run_all.sh [过滤正则]
#   RUN_ALL_BIN=build_net/bin bash _tools/run_all.sh '^network'   # 换构建目录 / 只跑一段
#
# 为什么需要它：ctest 只告诉你「过 / 没过」，这个脚本把**每个示例自己打的证据行**
# 存成 runlogs/<target>.log，勘误时要引用的就是这些行。退出码非 0 才算异常，
# 「跑完但自检项没过」的示例也是非 0（见各文件的 Ledger / QTest Totals）。
#
# 三个坑（都是本机踩过的，别再改回去）：
#   1. Windows 上不设 QT_FORCE_STDERR_LOGGING=1，Qt 把 qInfo() 灌进 OutputDebugString，
#      重定向到文件就是空的——你会以为示例什么都没打。
#   2. 控制台默认代码页是 GBK，日志必须过一遍 iconv（-c 是为了跳过个别非法字节，
#      否则整份转换直接失败退回原始字节）。
#   3. 不要设 QT_LOGGING_RULES="default.warning=false"：无类别的 qWarning() 走的 context
#      正是 "default"，压掉它 = 压掉示例自己打的证据行、QTest::ignoreMessage 认领的消息、
#      以及 qInstallMessageHandler 的计数（本仓 quality_01 / quality_05 就是这么被冤枉的）。
# ============================================================================
set -u
cd "$(dirname "$0")/.." || exit 1
BIN="${RUN_ALL_BIN:-build/bin}"
OUT=runlogs
FILTER="${1:-.*}"
PER_TIMEOUT="${PER_TIMEOUT:-30}"          # 单个示例最多跑这么久；超了就记 SKIP，不拖累整轮

if [ ! -d "$BIN" ]; then
    echo "找不到构建目录 $BIN（先 cmake --build，或用 RUN_ALL_BIN=你的目录 指过来）" >&2
    exit 2
fi
mkdir -p "$OUT"

export QT_QPA_PLATFORM=offscreen
export QT_FORCE_STDERR_LOGGING=1
export QT_LOGGING_RULES="${QT_LOGGING_RULES:-}"

# 少数示例的默认形态是「给人看的窗口」，不自带自退；它们另有 --self-check 无人值守入口。
declare -A EXTRA_ARGS=(
    [todo_desktop]="--self-check"
)

pass=0; fail=0; skip=0
: > "$OUT/summary.txt"
for exe in "$BIN"/*.exe; do
    name=$(basename "$exe" .exe)
    [[ "$name" =~ $FILTER ]] || continue
    # timeout 的退出码：124 = 到期限被杀，137 = SIGKILL（-k 之后）
    timeout -k 5 "$PER_TIMEOUT" "$exe" ${EXTRA_ARGS[$name]:-} > "$OUT/$name.raw" 2>&1
    code=$?
    iconv -f GBK -t UTF-8 -c "$OUT/$name.raw" > "$OUT/$name.log" 2>/dev/null \
        || cp "$OUT/$name.raw" "$OUT/$name.log"
    if [ "$code" -eq 0 ]; then
        pass=$((pass+1)); status=PASS
    elif [ "$code" -eq 124 ] || [ "$code" -eq 137 ]; then
        skip=$((skip+1)); status=SKIP
        printf '%s\ttimeout=%ss\n' "$name" "$PER_TIMEOUT" >> "$OUT/summary.txt"
    else
        fail=$((fail+1)); status=FAIL
        printf '%s\texit=%s\n' "$name" "$code" >> "$OUT/summary.txt"
    fi
    printf '%-46s %s (exit %s)\n' "$name" "$status" "$code"
done
echo "----- pass=$pass fail=$fail skip=$skip  （日志在 $OUT/，失败清单在 $OUT/summary.txt）-----"
[ "$fail" -eq 0 ]
