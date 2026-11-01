# ============================================================================
# 第 4 个独立工程 · 库 + 应用 + 测试：让 CTest 替你验收
# ----------------------------------------------------------------------------
# 前三个工程都在回答「怎么构建」。这个工程回答「构建完之后，怎么知道它还对不对」。
#
#   目标一 calc      静态库，纯逻辑，不碰界面（能在任何进程里跑）
#   目标二 calc_app  命令行小程序，调用库并打印结果
#   目标三 tst_calc  QtTest 用例，QTEST_GUILESS_MAIN（不需要平台插件）
#
# 三个 CMake 要点：
#   1) enable_testing() 必须在**顶层**目录调用（本工程自己就是顶层）
#   2) find_package(Qt6 COMPONENTS Test) 才拿得到 Qt6::Test
#   3) add_test(NAME ... COMMAND ...)：把可执行文件注册成 CTest 用例；
#      用例的「通过」判据是**退出码为 0**（QtTest 自己负责把它变成 0/1）
#
# 怎么构建与验证：
#   cmake -G "MinGW Makefiles" -B build -S .
#   cmake --build build
#   ctest --test-dir build --output-on-failure
#   ./build/calc_app
#
# 对应构建目标名：calc、calc_app、tst_calc
# ============================================================================

cmake_minimum_required(VERSION 3.21)
project(calc_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Test 这个组件只在「要写测试」时才需要。它比 Widgets 轻得多（不需要 GUI）。
find_package(Qt6 REQUIRED COMPONENTS Core Test)

qt_standard_project_setup()

# ── 被测的库：纯逻辑，零依赖（只依赖 QtCore 的值类型） ─────────────────────
add_library(calc STATIC
    src/calc.cpp
    include/calc/calc.h
)
target_include_directories(calc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(calc PUBLIC Qt6::Core)

# ── 命令行小程序：演示「库能被非 UI 程序复用」 ─────────────────────────────
add_executable(calc_app src/main.cpp)
target_link_libraries(calc_app PRIVATE calc)

# ── 测试目标 ───────────────────────────────────────────────────────────────
enable_testing()

add_executable(tst_calc tests/tst_calc.cpp)
target_link_libraries(tst_calc PRIVATE calc Qt6::Test)

# 这一步把目标变成「一个用例」。不做这件事，ctest 一个用例都看不到。
add_test(NAME tst_calc COMMAND tst_calc)

# 用例需要的工作目录 / 环境变量可以按用例设。QTEST_GUILESS_MAIN 本身不用平台
# 插件，这里仍然写上 offscreen：将来你若把它改成 QTEST_MAIN，也不会突然在
# 没有显示器的 CI 上失败。
set_tests_properties(tst_calc PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 30
)

# 也可以直接用 Qt 的 CTest 集成：qt_add_test / qt_add_test_case 会替你处理
# 平台插件、环境与依赖。本工程用手写 add_test，是为了让你看清「一行 add_test
# 到底做了什么」——集成宏最终也是落到这一步。
