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
#           —— ★路径由 FILES 的**相对路径**决定，不是文件名；要压平得加 BASE。见下面第二节。
#
# 本工程用**路线 B** 建 app_assets（不压平）与 app_assets_flat（BASE 压平），
# 再用**路线 A** 建 legacy_assets，三条都跑，运行时把同一个文件的几个路径都打印出来，
# 证明资源树长什么样完全由「PREFIX + alias」决定。
#
# 目录结构：
#   qt_cmake_03_resources/
#     CMakeLists.txt
#     assets/style.qss
#     assets/logo.svg
#     assets/legacy.qrc      ← 只给路线 A 用
#     src/main.cpp
#
# 怎么构建（三选一，取决于你机器上装了哪个 generator）：
#   cmake -G "MinGW Makefiles" -B build -S .            && cmake --build build
#   cmake -G "MinGW Makefiles" -B build -S . && cmake --build build   # ← 没装 Ninja 时用这条
#   cmake -B build -S .                     && cmake --build build     # 让 CMake 自己挑
#   ./build/resource_app        # Windows 下是 build\resource_app.exe
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
# ★★ 这里有一个很容易踩的坑，本机实测过（Qt 6.12.0 / MinGW / Windows），务必看清：
#    资源里的路径 = PREFIX + **文件在源码树里的相对路径**，不是「文件名」。
#    所以下面这两条 FILES 装进去的是 ":/assets/style.qss"、":/assets/logo.svg"，
#    而不是很多人以为的 ":/style.qss"。
#    依据：Qt6CoreMacros.cmake 的 _qt_internal_add_resources 逻辑（2522~2532 行）——
#    只有给了 BASE 才会把相对 BASE 的路径写进 QT_RESOURCE_ALIAS；没给 BASE 时
#    alias 保持原相对路径。生成的中间文件可以当场验证：
#      build/.qt/rcc/app_assets.qrc 里是
#      <file alias="assets/style.qss">…/assets/style.qss</file>
#
# 想控制路径，两条手段：
#   · BASE <目录>：以该目录为根算 alias —— FILES assets/style.qss 得到 ":/style.qss"
#   · 给单个文件设 QT_RESOURCE_ALIAS 源属性（qt_add_resources **没有** ALIAS 关键字：
#     Qt6CoreMacros.cmake 408 行把单值参数列成 PREFIX;LANG;BASE;OUTPUT_TARGETS;DESTINATION，
#     多值只有 FILES;OPTIONS，所以按文件改名只能走源属性这一条）：
#       set_source_files_properties(assets/logo.svg PROPERTIES QT_RESOURCE_ALIAS brand.svg)
qt_add_resources(resource_app "app_assets"
    PREFIX "/"
    FILES
        assets/style.qss
        assets/logo.svg
)

# ── 路线 B+：同一个宏，加 BASE，把路径压平 ────────────────────────────────
# 与上面唯一区别是多了一句 BASE assets，于是得到 ":/style.qss"、":/logo.svg"。
# 两段都留着，是因为「加不加 BASE」的差别只有并排跑一遍才看得清 ——
# main.cpp 会把两个路径都读一次，读不到的那条会打出 No such file or directory。
qt_add_resources(resource_app "app_assets_flat"
    PREFIX "/"
    BASE assets
    FILES
        assets/style.qss
        assets/logo.svg
)

# ── 路线 A：传统 .qrc 当源文件 ─────────────────────────────────────────────
# AUTORCC 会为它调用 rcc，生成 qrc_legacy.cpp 并编进本目标。
# 本工程的 legacy.qrc 里 prefix 是 "/legacy"，于是同一份 style.qss 会**同时**
# 出现在三个路径下：":/assets/style.qss"（路线 B）、":/style.qss"（路线 B+）、
# ":/legacy/style.qss"（路线 A）。
# 资源以内容为准，但字节是真编了三份——第 4 讲演示过「alias 改路径不改内容，
# 字节却各存一份」，这里就是构建期的同一个现象。main.cpp 会把三份都读出来、
# 逐字节比对，并打印 exe 里资源段的实际体积当证据。
target_sources(resource_app PRIVATE assets/legacy.qrc)

target_link_libraries(resource_app PRIVATE Qt6::Widgets)

install(TARGETS resource_app
        BUNDLE  DESTINATION .
        RUNTIME DESTINATION bin)
