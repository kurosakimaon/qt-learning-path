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
#     translations_app_en.ts      ← 提交进仓库的那份，里面**填好了英文译文**
#     translations_app_zh_CN.ts   ← 不是翻译，是 lupdate 的 plurals 模板（见下）
#     （.qm 是构建产物，出现在 build/ 里，不提交）
#   ★ 上面那句「提交进仓库」差点是假的：仓库根 .gitignore 有一条 `build_*/`，
#     它本意是挡构建输出目录，却把整个 build_deploy/ 也吞了 —— 已跟踪的文件不受影响，
#     但这两个 .ts 属于**新增**，`git status` 里一直是 ??，永远进不了提交
#     （本机 `git check-ignore -v` 实测命中的就是那一条）。现在 .gitignore 加了
#     `!build_deploy/` + `!build_deploy/**` 反选，并把 build_deploy/**/build/ 重新压回忽略。
#
# ★ 一个第一次用一定会踩的坑（本工程的前一版就踩了，而且踩得很隐蔽）：
#   qt_add_translations **建 .ts 的时候是空的**。它只写 XML 头 + 一句
#       "Run the update_translations CMake target to populate the source strings in this file."
#   抽取源串是 lupdate 的活，而 lupdate **不在默认构建里跑**（它归 update_translations /
#   translations_app_lupdate 这两个手动 target 管）。所以照下面的顺序构建，
#   .qm 会编出来、能 load() 成功、installTranslator() 也成功，但里面 0 条译文，
#   界面还是中文 —— 而程序当时的判据只看 load()，就会打印「翻译是否生效： true」。
#   完整顺序（本机实测跑通）：
#       cmake -G "MinGW Makefiles" -B build -S .
#       cmake --build build --target update_translations   # lupdate：把源串抽进 .ts
#       # 填译文：Qt Linguist 打开 .ts，或直接改 XML（把 type="unfinished" 那格写上字）
#       cmake --build build                                # lrelease：.ts → .qm → :/i18n/
#   本机跑 update_translations 的原文输出：
#       Updating '../translations_app_en.ts'...    Found 2 source text(s) (2 new and 0 already existing)
#       Updating '../translations_app_zh_CN.ts'... Found 2 source text(s) (2 new and 0 already existing)
#
# 那个 _zh_CN.ts 为什么也在？它是 plurals 模板，不是要你翻译的 catalog。
#   Qt6LinguistTools/Qt6LinguistToolsMacros.cmake 749~757 行：当 QT_I18N_SOURCE_LANGUAGE
#   （这里 zh_CN）不在 QT_I18N_TRANSLATED_LANGUAGES 里时，宏自动生成「基名_源语言.ts」
#   当 arg_PLURALS_TS_FILE，用来记录源语言自己有几个复数形态；886 行把它一起放进
#   all_ts_files。于是 build/ 里会多一个 translations_app_zh_CN.qm，0 条译文，程序也不加载它。
#   不想要：给 qt_add_translations 传 NO_GENERATE_PLURALS_TS_FILE（同文件 240 行那个选项）。
#
# 怎么构建：
#   cmake -G "MinGW Makefiles" -B build -S .
#   cmake --build build
#   ./build/translations_app
#   （仓库里那份 .ts 已经填好英文，所以照这三条建完就能看到译文 —— 前提是那两个 .ts
#     真的进了仓库；上面 ★ 记的就是它差点不成立的原因。）
#
#   ★ 关于「重跑 update_translations 会不会把我填的译文冲掉」——本机实测：**不会**。
#     lupdate 是**合并**而不是重写：源文没变的条目连译文一起留着，只把 <location> 的
#     行号刷新。第二次跑它打的是 "Found 3 source text(s) (1 new and 2 already existing)"。
#     真正要防的是另一件事，同一次运行里它就顺手干了：
#         Same-text heuristic provided 1 translation(s)
#     ——代码里新冒出一个**不同上下文、相同源文**的 translate()，lupdate 会把别的
#     上下文里那条译文**抄过来**（标成 type="unfinished"）。而 unfinished 只要填了字
#     就照样进 .qm、照样命中（本机探针量过）。两个凑一起 = 幽灵翻译：
#     上下文拼错了却仍有译文。src/main.cpp 的账8 就是把这件事跑成一行判卷。
#     不想让它抄：填译文前跑 lupdate，或者用 -no-obsolete / Linguist 里逐条确认。
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
