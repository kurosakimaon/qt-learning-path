# ============================================================================
# 第 3 个独立工程 · 资源：把 qss 与 svg 编进可执行文件
# ----------------------------------------------------------------------------
# 第 4 讲（storage/04_resource_system.cpp）讲的是运行时怎么读资源；这里讲的是
# **构建期怎么把资源放进去**。两条路，本工程都写给你看：
#
#   路线 A：CMAKE_AUTORCC=ON + 把 .qrc 当源文件加进目标
#           add_executable(app main.cpp assets.qrc)
#           —— 老写法，最省事，rcc 在构建时自动跑
#
#   路线 B：qt_add_resources(目标 "资源名" PREFIX "/" FILES ...)
#           —— 推荐写法，不用自己写 .qrc 文件；能配合 target 的属性做增量构建
#
# 本工程用**路线 B** 建 app_assets，同时用**路线 A** 建 legacy_assets，
# 两条都跑，运行时把同一个文件的两种路径都打印出来，证明它们进的是同一个资源树。
#
# 目录结构：
#   qt_cmake_03_resources/
#     CMakeLists.txt
#     assets/style.qss
#     assets/logo.svg
#     assets/legacy.qrc      ← 只给路线 A 用
#     src/main.cpp
#
# 怎么构建：
#   cmake -G Ninja -B build -S .
#   cmake --build build
#   ./build/resource_app
#
# 对应构建目标名：resource_app
# ============================================================================

cmake_minimum_required(VERSION 3.21)
project(resource_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Qt 6.12 的标准工程设置：打开 AUTOMOC 与 AUTOUIC，设置策略组与输出目录。
# ★ 它**不会**打开 AUTORCC（Qt6CoreMacros.cmake 里只对 MOC、UIC 两个变量赋 TRUE），
# 所以下面路线 A 想「把 .qrc 当源文件加进去」，必须自己补这一句：
set(CMAKE_AUTORCC ON)

qt_standard_project_setup()

qt_add_executable(resource_app src/main.cpp)

# ── 路线 B：直接从文件列表造资源，不需要 .qrc ──────────────────────────────
# 参数顺序：  目标        资源名        PREFIX           文件...
# 生成后路径：  ":" + PREFIX + 文件名 → ":/style.qss"、":/logo.svg"
#
# 想给文件起别名（改路径不改内容），用 BASE + FILES 的相对关系，或者直接写 .qrc。
qt_add_resources(resource_app "app_assets"
    PREFIX "/"
    FILES
        assets/style.qss
        assets/logo.svg
)

# ── 路线 A：传统 .qrc 当源文件 ─────────────────────────────────────────────
# AUTORCC 会为它调用 rcc，生成 qrc_legacy.cpp 并编进本目标。
# 本工程的 legacy.qrc 里 prefix 是 "/legacy"，于是同一份 style.qss 会**同时**
# 出现在两个路径下：路线 B 的 ":/style.qss"、路线 A 的 ":/legacy/style.qss"。
# 资源以内容为准，同一份字节被编了两次——第 4 讲演示过「alias 改路径不改内容，
# 但字节真存了两份」，这里就是构建期的同一个现象。
target_sources(resource_app PRIVATE assets/legacy.qrc)

target_link_libraries(resource_app PRIVATE Qt6::Widgets)

install(TARGETS resource_app
        BUNDLE  DESTINATION .
        RUNTIME DESTINATION bin)
