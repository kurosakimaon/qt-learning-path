#!/usr/bin/env bash
# 语法与 API 校验门（只做 moc + 语义检查，不链接、不运行）
# 用法：
#   bash _tools/check_syntax.sh                      # 检查全部模块
#   bash _tools/check_syntax.sh threads model_view   # 检查指定模块
#   bash _tools/check_syntax.sh threads/01_thread_basics.cpp   # 检查单个文件
set -u

# ── Qt 头文件 / moc / g++ 在哪：三种摆法自动挑，别把路径写死在一台机器上 ──────
#   ① Qt 官方在线安装器（本机就是这套）：QTDIR=D:/Qt/6.12.0/mingw_64
#        头文件 $QTDIR/include/{QtCore,QtWidgets,…}，moc 在 $QTDIR/bin/moc.exe
#   ② MSYS2 的 mingw-w64-ucrt-x86_64-qt6-base：/c/msys64/ucrt64
#        头文件 $MSYS/include/qt6/…，moc 在 $MSYS/share/qt6/bin/moc.exe
#   ③ 自己指：QTDIR=/path/to/mingw_64 bash _tools/check_syntax.sh
#        （或 MSYS=/path/to/prefix 走 ② 那套目录结构）
#   ★ 原来这里只有 ② 一条路，写死 /c/msys64/ucrt64；本机 MSYS2 里**根本没装 Qt**
#     （没有 include/qt6，也没有 share/qt6/bin/moc.exe），所以这道门在本机一上来就
#     「找不到编译器或 Qt6 头文件」 exit 2 —— 现在是自动探测，两条路都能走。
MSYS="${MSYS:-/c/msys64/ucrt64}"
QTDIR="${QTDIR:-/d/Qt/6.12.0/mingw_64}"

if [ -d "$QTDIR/include/QtCore" ]; then          # ①（或 ③）
    QI="$QTDIR/include"
    MOC="$QTDIR/bin/moc.exe"
elif [ -d "$MSYS/include/qt6/QtCore" ]; then      # ②
    QI="$MSYS/include/qt6"
    MOC="$MSYS/share/qt6/bin/moc.exe"
else
    QI=""
    MOC=""
fi
# 编译器：优先用 PATH 上的 g++（本机构建用的就是 Qt 自带的 D:/Qt/Tools/mingw1310_64），
# 退而求其次才用 MSYS2 那个；两者版本不同，但这里只做 -fsyntax-only，不影响结论。
GXX="${GXX:-$(command -v g++ || true)}"
[ -x "$GXX" ] || GXX="$MSYS/bin/g++.exe"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 2

INCLUDES=(-I"$QI" -I"$QI/QtWidgets" -I"$QI/QtGui" -I"$QI/QtCore" -I"$QI/QtNetwork"
         -I"$QI/QtSql" -I"$QI/QtConcurrent" -I"$QI/QtTest" -I"$QI/QtSvg"
         -I"$QI/QtXml" -I"$QI/QtPrintSupport" -I"$ROOT")

# ── 模块宏：这些不是我们自己写的，是 Qt6 的 CMake 目标替我们定义的 ────────────
#   Qt6::Widgets 之类的 imported target 带 INTERFACE_COMPILE_DEFINITIONS，
#   CMake 建目标时会自动往命令行塞 -DQT_WIDGETS_LIB。Qt 的头文件靠这些宏
#   「条件编译出」一部分声明，最典型的就是 QtTest/qtestkeyboard.h：
#
#     #ifdef QT_WIDGETS_LIB          // 第 151 行
#     ... keyClick(QWidget *, ...)   // 第 274/280 行
#     #endif                         // 第 294 行
#
#   门里不定义它，就只剩 QWindow* 那套重载，于是 quality/02 会报
#     error: no matching function for call to 'keyClick(QLineEdit*, Qt::Key)'
#   而真实构建里这段是编得过的 —— 那是门自己造的假红，必须和构建对齐。
#   这里取的是全仓库 CMakeLists 用到的组件并集（Widgets/Svg/Concurrent/
#   Network/Sql/Xml/Test），比单个目标略宽；宽出来的部分只影响「是否多看见
#   几个声明」，不会让编不过的代码蒙混过关。
QT_DEFS=(-DQT_CORE_LIB -DQT_GUI_LIB -DQT_WIDGETS_LIB -DQT_SVG_LIB
         -DQT_CONCURRENT_LIB -DQT_NETWORK_LIB -DQT_SQL_LIB -DQT_XML_LIB
         -DQT_TESTLIB_LIB)

ALL_MODULES=(start metaobject events widgets painting model_view threads storage network quality)

FILES=()
if [ $# -eq 0 ]; then
  for mod in "${ALL_MODULES[@]}"; do
    for f in "$ROOT/$mod"/*.cpp; do [ -e "$f" ] && FILES+=("$f"); done
  done
else
  for arg in "$@"; do
    case "$arg" in
      *.cpp)  [ -e "$arg" ] && FILES+=("$arg") || echo "找不到文件：$arg" ;;
      *)      for f in "$ROOT/$arg"/*.cpp; do [ -e "$f" ] && FILES+=("$f"); done ;;
    esac
  done
fi

if [ ! -x "$GXX" ] || [ ! -d "$QI" ] || [ ! -x "$MOC" ]; then
  echo "自动探测失败：既没找到 Qt6 头文件+moc，也没找到 g++。"
  echo "  试过的两条路：QTDIR=$QTDIR（要 include/QtCore 和 bin/moc.exe）"
  echo "               MSYS=$MSYS（要 include/qt6/QtCore 和 share/qt6/bin/moc.exe）"
  echo "  你的 Qt 装在别处的话：QTDIR=/你的/mingw_64 路径 bash _tools/check_syntax.sh"
  command -v g++ >/dev/null || echo "  另外：PATH 上没有 g++（本机构建用的是 D:/Qt/Tools/mingw1310_64/bin）"
  exit 2
fi
echo "语法门用的是：QI=$QI｜MOC=$MOC｜GXX=$GXX"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

pass=0; fail=0; failed=()
for f in "${FILES[@]}"; do
  base=$(basename "$f" .cpp)
  mod=$(basename "$(dirname "$f")")
  out=""

  # ── 静态门 1：一个「讲」必须是一个建得出来的可执行文件 ────────────────────
  #   按仓库约定，编号文件（NN_*.cpp）各自对应一个 qt_add_demo 目标，所以必须有入口：
  #   要么自己写 int main，要么用 QTest 的 QTEST_*_MAIN（宏里替你生成了 main）。
  #   非编号的伴生源文件（例如 start/startwidget.cpp，和 02 拼成同一个目标）跳过。
  #   ★ 这条是踩过才知道要加的：network/02 只有类、没有 main，
  #     -fsyntax-only 一路绿灯，直到 CMake 去链接才炸 "undefined reference to `main'"。
  case "$base" in
    [0-9]*)
      # ★ 这个正则原来是 QTEST_[A-Z]*_MAIN\( ，它匹配不到裸的 QTEST_MAIN( ——
      #   因为 QTEST_ 后面直接跟 MAIN，没有额外的下划线。quality/02 一上手就被它误拦。
      if ! grep -qE '^[[:space:]]*(int|qint64)[[:space:]]+main[[:space:]]*\(|QTEST_[A-Z_]*MAIN\(' "$f"; then
        out="没有入口：编号讲义必须有 int main 或 QTEST_*_MAIN，否则 add_executable 在链接期就挂"
      fi
      ;;
  esac

  # ── 静态门 2：源码里的中文字面量不许走 Latin-1 通道 ──────────────────────
  #   本仓库的 .cpp 全是 UTF-8。QString::fromLatin1("中文") / QLatin1String("中文")
  #   把每个 UTF-8 字节当成一个 Latin-1 字符，编得过、跑得动，打出来是乱码 ——
  #   而且乱的是界面文字，截图里最显眼的那一行。要转中文一律 fromUtf8。
  if [ -z "$out" ]; then
    enc=$(LC_ALL=C grep -nE '(fromLatin1|QLatin1String)\("[^"]*[^ -~]' "$f")
    if [ -n "$enc" ]; then
      out="中文字面量走了 Latin-1 通道（运行期必乱码，改 fromUtf8 或 QStringLiteral）："
      out="$out"$'\n'"$enc"
    fi
  fi

  # 只在「真实代码里出现 Q_OBJECT」时跑 moc。
  # 旧写法 grep -q 'Q_OBJECT' 会把注释里提到的这个词也算进去（讲义里经常
  # 讨论 moc / Q_OBJECT，甚至有整讲不用 moc 的），于是给没有元对象的文件
  # 硬塞一次 moc，报 "Missing access specifier for slots"。
  # 这里的判据：Q_OBJECT 之前不能出现 '/'（注释行 // 或 /* 一定先命中 '/'）。
  if grep -Eq '^[^/]*Q_OBJECT' "$f"; then
    if ! mocout=$("$MOC" -I"$ROOT" -I"$ROOT/$mod" "$f" -o "$TMP/$base.moc" 2>&1); then
      out="MOC: $mocout"
    fi
  fi

  if [ -z "$out" ]; then
    gxxout=$("$GXX" -fsyntax-only -std=c++17 -fPIC -Wall -Wextra \
            "${QT_DEFS[@]}" "${INCLUDES[@]}" -I"$TMP" "$f" 2>&1)
    gxxrc=$?
    if [ "$gxxrc" -ne 0 ]; then
      out="$gxxout"
    elif [ -n "$gxxout" ]; then
      # 警告不影响 g++ 的退出码，但会骗过「只看退出码」的判断，
      # 让一个「OK」的行数里藏着 nodiscard、sign-compare 这类真问题。
      # 本仓库的标准是 0 error / 0 warning，所以警告一律算失败。
      out="$gxxout"
    fi
  fi

  if [ -z "$out" ]; then
    pass=$((pass+1)); printf '  OK   %s/%s.cpp\n' "$mod" "$base"
  else
    fail=$((fail+1)); failed+=("$mod/$base.cpp")
    echo "  FAIL $mod/$base.cpp"
    echo "$out" | head -14 | sed 's/^/        /'
  fi
done

echo
echo "校验完成：通过 $pass，失败 $fail"
if [ ${#failed[@]} -gt 0 ]; then printf '  待修：%s\n' "${failed[@]}"; fi
# ★ 门自己也得把失败带到进程外：永远 exit 0 的门，等于一个只会说「过了」的评审。
if [ "$fail" -gt 0 ]; then exit 1; fi
exit 0
