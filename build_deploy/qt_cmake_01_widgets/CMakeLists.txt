# ============================================================================
# 第 1 个独立工程 · 最小可用的 Qt Widgets 程序
# ----------------------------------------------------------------------------
# 这个工程故意小到不能再小：一个 main.cpp，一份 20 行出头的 CMakeLists。
# 它的用途不是「做个界面」，而是让你把 Qt 工程的**四行骨架**背下来：
#
#   cmake_minimum_required(VERSION 3.21)
#   project(hello_widgets LANGUAGES CXX)
#   find_package(Qt6 REQUIRED COMPONENTS Widgets)
#   qt_standard_project_setup()
#   qt_add_executable(hello_widgets main.cpp)
#   target_link_libraries(hello_widgets PRIVATE Qt6::Widgets)
#
# 怎么构建（在本目录下，不要在上层目录）：
#   cmake -G Ninja -B build -S .
#   cmake --build build
#   ./build/hello_widgets            # Windows: build\hello_widgets.exe
#
# 为什么每个工程都要自己 project()：
#   这五个目录是**独立工程**，各自能被单独 configure。它们不是被上层
#   add_subdirectory() 进来的子目录（上层只检查它们存在）。这样你才能在一个
#   干净的目录里，看清「一个 Qt 可执行文件最少需要什么」。
# ============================================================================

cmake_minimum_required(VERSION 3.21)

# project() 顺手把 CMAKE_PROJECT_NAME 设好，qt_add_executable 会在需要时用它。
project(hello_widgets LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 只找 Widgets 一个组件。Qt6::Widgets 会自动把 Qt6::Gui、Qt6::Core 带进来，
# 所以你不用写三遍 find_package。
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# 这一句（Qt 6.3 起提供）替你做下面几件事。6.12 的 qt6_standard_project_setup
# 实现（Qt6CoreMacros.cmake 第 3646 行起）实际做的是：
#   1) 按 REQUIRES 设置 Qt 的 CMake 策略组（默认 6.0.0），让 qt_add_* 用新布局
#   2) include(GNUInstallDirs)，并定好产物输出目录：
#      Windows 下没设过 CMAKE_RUNTIME_OUTPUT_DIRECTORY 就设成构建目录
#      （这样 exe 和 Qt 的 DLL 能放在一起）；
#      Linux/macOS 追加 $ORIGIN 到 install RPATH，装完还能找到库
#   3) 打开 **AUTOMOC 与 AUTOUIC**（如果它们此前没有被显式设置）
#      ★ 注意：**不包括 AUTORCC**。要用「把 .qrc 当源文件」的老路线，
#        得自己 set(CMAKE_AUTORCC ON)——第 3 个工程会演示
#   4) 整理 IDE 的文件夹结构与生成物分组，并定义 QT_I18N_SOURCE_LANGUAGE
#      （默认 en），给 qt_add_translations 用
qt_standard_project_setup()

# 用 qt_add_executable 而不是 add_executable：它会
#   · 在 Windows 上正确处理 WinMain 入口（链接 Qt6::EntryPoint）
#   · 对 iOS / Android 等平台做相应处理
#   · 自动把目标标记为「Qt 可执行文件」，供 qt_add_translations 等找到
qt_add_executable(hello_widgets main.cpp)

# PUBLIC 还是 PRIVATE？可执行文件没有「下游」，所以 PRIVATE 就够。
# 如果这是一个库，且头文件里出现了 Qt 类型，那些 Qt 组件就必须 PUBLIC。
target_link_libraries(hello_widgets PRIVATE Qt6::Widgets)

# 一条 install 规则，说明「产物该装到哪儿」。部署时 windeployqt 会读它。
install(TARGETS hello_widgets
        BUNDLE  DESTINATION .
        RUNTIME DESTINATION bin)
