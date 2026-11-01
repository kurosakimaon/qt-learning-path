# ============================================================================
# _tools/find_browser.sh —— 给两个无头体检脚本共用：找出本机任一 Chromium 系浏览器
# ----------------------------------------------------------------------------
# 用法：
#   . "$(dirname "$0")/find_browser.sh"
#   BROWSER="$(find_browser)" || exit 2
#   设了环境变量 CHROME=<可执行文件> 就优先用它（别的机器上只有 Chrome 时用）。
# 为什么单独一个文件：check_pages_render.sh 和 check_svg_overflow.sh 都要开无头浏览器，
#   这 16 行的查找顺序抄两遍迟早改漏一边。
# ==========================================================================*/
find_browser() {
    local c=("$HOME/AppData/Local/Google/Chrome/Application/chrome.exe")
    [ -n "${CHROME:-}" ] && c=("$CHROME" "${c[@]}")
    c+=(
        "/c/Program Files/Google/Chrome/Application/chrome.exe"
        "/c/Program Files (x86)/Google/Chrome/Application/chrome.exe"
        "/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"
        "/c/Program Files/Microsoft/Edge/Application/msedge.exe"
        "/c/Program Files/Chromium/Application/chrome.exe"
    )
    for b in "${c[@]}"; do [ -x "$b" ] && { echo "$b"; return 0; }; done
    for cmd in chromium chromium-browser google-chrome msedge; do
        command -v "$cmd" >/dev/null 2>&1 && { command -v "$cmd"; return 0; }
    done
    return 1
}
