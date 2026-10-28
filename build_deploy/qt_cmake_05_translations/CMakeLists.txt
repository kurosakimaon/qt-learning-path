# ============================================================================
# 第 5 个独立工程 · 翻译：从 tr() 到 :/i18n/xxx.qm
# ----------------------------------------------------------------------------
# 前四个工程把「界面、结构、资源、测试」立住了。这个工程补上最后一块——
# **界面文字怎么跟着语言走**。它把一条完整的链子串起来：
#
#   tr("...")  ──lupdate──>  xxx_<lang>.ts  ──lrelease──>  xxx_<lang>.qm
#                                                              │
#                                         qt_add_translations 自动把它编进资源
#                                                              ▼
#                                                     ":/i18n/xxx_<lang>.qm"
#   QTranslator::load(":/i18n/xxx_<lang>.qm") + QCoreApplication::installTranslator()
#
# 关键 CMake 事实（Qt 6.12）：
#   · qt_standard_project_setup(I18N_SOURCE_LANGUAGE zh_CN
#                               I18N_TRANSLATED_LANGUAGES en)
#     声明「源码语言是中文，要翻成英文」。这两个变量会被 qt_add_translations 读取。
#   · qt_add_translations(<target> TS_FILE_BASE <名字>) 会创建 lupdate / lrelease
#     目标，并把 .qm 以 RESOURCE_PREFIX（默认 "/i18n"）编进目标。
#   · 它需要 LinguistTools 组件；本工程用 QUIET 查找，找不到就退化成一个
#     「只显示源码语言」的工程，而不是让 configure 直接失败。
#
# 目录结构：
#   qt_cmake_05_translations/
#     CMakeLists.txt
#     src/main.cpp
#     （首次构建后，会自动出现 translations_app_en.ts / .qm，.ts 可以提交到仓库）
#
# 怎么构建：
#   cmake -G Ninja -B build -S .
#   cmake --build build
#   ./build/translations_app
#
# 对应构建目标名：translations_app
# ============================================================================

cmake_minimum_required(VERSION 3.21)
project(translations_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# LinguistTools 提供 lupdate / lrelease 两个工具与 qt_add_translations。
# 用 QUIET：精简安装的 Qt 可能没带它，此时工程仍应能编出来（只是不翻译）。
find_package(Qt6 QUIET COMPONENTS LinguistTools)

# 源码语言 + 目标语言。注意：这里的语言代码用的是 QLocale 的名字（zh_CN / en）。
qt_standard_project_setup(
    I18N_SOURCE_LANGUAGE zh_CN
    I18N_TRANSLATED_LANGUAGES en
)

qt_add_executable(translations_app src/main.cpp)

if(Qt6LinguistTools_FOUND)
    # TS_FILE_BASE 决定生成文件的基名：
    #   translations_app_en.ts → translations_app_en.qm → ":/i18n/translations_app_en.qm"
    qt_add_translations(translations_app
        TS_FILE_BASE translations_app
    )
    message(STATUS "translations_app：已启用翻译（en），qm 将嵌入 :/i18n/")
else()
    message(STATUS "translations_app：本机 Qt 未提供 LinguistTools，跳过翻译")
endif()

target_link_libraries(translations_app PRIVATE Qt6::Widgets)

install(TARGETS translations_app
        BUNDLE  DESTINATION .
        RUNTIME DESTINATION bin)
