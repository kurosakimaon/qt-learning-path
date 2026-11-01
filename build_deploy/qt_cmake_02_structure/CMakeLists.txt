# ============================================================================
# 第 2 个独立工程 · 一个库 + 一个界面：目录结构、可见性、AUTOMOC
# ----------------------------------------------------------------------------
# 第 1 个工程只有一份 main.cpp。真实项目不是这样：它至少有两层——
# 一层「不碰界面的业务/核心」（能单独测、能单独复用），一层「界面」。
# 这个工程把那两层立起来，并把三个 CMake 概念讲清：
#
#   1) add_library(... STATIC ...) 与 target_link_libraries 的 PUBLIC/PRIVATE
#   2) target_include_directories(... PUBLIC include)：让下游用 <greeting/greeting.h>
#   3) 手写 AUTOMOC（不用 qt_standard_project_setup），看清它到底替谁做了什么
#
# 目录结构（这就是仓库里推荐的布局）：
#
#   qt_cmake_02_structure/
#     CMakeLists.txt
#     include/greeting/greeting.h     ← 公共头：库对外的脸
#     src/greeting.cpp                ← 库的实现
#     src/main.cpp                    ← 界面层的入口
#
# 怎么构建：
#   cmake -G "MinGW Makefiles" -B build -S .
#   cmake --build build
#   ./build/greeting_app
#
# 对应构建目标名：greeting（静态库）、greeting_app（可执行文件）
# ============================================================================

cmake_minimum_required(VERSION 3.21)
project(greeting_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── 手动打开三个自动化开关 ──────────────────────────────────────────────────
# 这里故意**不用** qt_standard_project_setup()，让你看见它平时替你做的那几件事：
#   · AUTOMOC：对每个含 Q_OBJECT / Q_GADGET 的**头文件**生成 moc_xxx.cpp；
#     对写在 .cpp 里的 Q_OBJECT 类，需要你在文件末尾 #include "xxx.moc"
#   · AUTOUIC：把 .ui 文件转成 ui_xxx.h（本工程没有 .ui，开着也不碍事）
#   · AUTORCC：把 .qrc 交给 rcc（本工程没有 .qrc，第 3 个工程会用到）
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# ── 核心库：不碰 QWidget ────────────────────────────────────────────────────
# 注意源文件列表里**也放了头文件**：这不是编译需要，而是让 AUTOMOC 与 IDE
# 都能看到它。头文件里一旦有 Q_OBJECT，AUTOMOC 就会为它生成 moc_greeting.cpp
# 并自动编进本目标。
add_library(greeting STATIC
    src/greeting.cpp
    include/greeting/greeting.h
)

# PUBLIC 包含目录：下游（greeting_app）自动继承，可以写 <greeting/greeting.h>。
# 如果写成 PRIVATE，下游就得自己再写一遍路径——那正是「头文件装不出去」的起点。
target_include_directories(greeting PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 库的公开头里出现了 QObject / QString，所以 Qt6::Core 必须是 PUBLIC：
# 谁用这个库，谁就需要 Qt6::Core 的 include 路径。
target_link_libraries(greeting PUBLIC Qt6::Core)

# 一个只有本目标编译时可见的宏：用来区分「我在编库」和「我在用库」。
# 这种宏常用来控制导出符号（Windows 的 __declspec(dllexport/dllimport)）。
target_compile_definitions(greeting PRIVATE GREETING_LIBRARY=1)

# ── 界面层：只认识库，不认识库的实现 ───────────────────────────────────────
add_executable(greeting_app src/main.cpp)

# greeting_app 需要 Widgets；而 Qt6::Core 与 include 路径是从 greeting 的
# PUBLIC 属性继承来的——一行顶三行，这就是 PUBLIC/PRIVATE 的意义。
target_link_libraries(greeting_app PRIVATE greeting Qt6::Widgets)

install(TARGETS greeting_app
        BUNDLE  DESTINATION .
        RUNTIME DESTINATION bin)
