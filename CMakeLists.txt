# ============================================================================
# Qt 学习之路 · 顶层构建文件
# ----------------------------------------------------------------------------
# 这一份 CMakeLists 是整个资料库的「验证入口」。你的环境里只要跑三条命令：
#
#   cmake -G "MinGW Makefiles" -B build -S .
#   cmake --build build
#   ctest --test-dir build --output-on-failure
#
# 就能知道：每个示例能不能编过、每个示例能不能跑完、哪里跑出不正常的结果。
# 全部示例都注册成了 CTest 用例，并强制以 QT_QPA_PLATFORM=offscreen 运行，
# 所以不需要显示器、不需要鼠标点、也不需要有窗口管理器。
#
# 常用目标：
#   cmake --build build --target start_01_hello_window   # 只编一个示例
#   cmake --build build --target all_widgets             # 编某一阶段全部示例
#   cmake --build build --target all_demos               # 编全部示例（不含实战项目外的东西）
#
# 环境要求：Qt 6.x（本库按 Qt 6.12 编写）、CMake ≥ 3.21、支持 C++17 的编译器。
# MinGW / MSVC / Clang 都可以；Windows 上产物集中在 build/bin，方便 windeployqt 收依赖。
# ============================================================================

cmake_minimum_required(VERSION 3.21)
project(qt_learning_path LANGUAGES CXX VERSION 1.0.0)

# ── 全局设定 ────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt 工程的三件套开关。本库绝大多数示例用不到 .ui 文件（手写布局更利于理解
# 布局系统的原理），所以只开 AUTOMOC 和 AUTORCC。
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# 示例一律保持「控制台子系统」：qInfo() 打印的证据要能在终端里看见。
# 只在 Windows + MinGW/MSVC 上有意义：设为 OFF 可避免生成无控制台窗口的 GUI 程序。
set(CMAKE_WIN32_EXECUTABLE OFF)
set(CMAKE_MACOSX_BUNDLE OFF)

# ── 把「编得过」抬到「编得过且没有警告」────────────────────────────────────
#   为什么默认开：本仓库每一讲都靠打印出来的数字说话，一个 warning 就是一次
#   「这个数可能不是你以为的那个」。本机实测 GCC 13.1.0 + -Wall -Wextra：
#   全仓库 0 warning（曾经有 6 条 -Wunused-function，是六个写了却没调用的自检
#   助手函数，已删掉而不是用 [[maybe_unused]] 把噪声按住）。
#   换编译器/换版本被警告淹了时的退路：
#     cmake -B build -DQTLP_STRICT_WARNINGS=OFF
option(QTLP_STRICT_WARNINGS "给全部目标加 -Wall -Wextra（GCC/Clang）" ON)
if(QTLP_STRICT_WARNINGS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
elseif(QTLP_STRICT_WARNINGS AND MSVC)
    # MSVC 没有 -Wall 这个拼写，/W4 才是对应档；/Wv 不动，默认即可。
    add_compile_options(/W4)
endif()

enable_testing()
find_package(Threads REQUIRED)

# ── 找 Qt6 ──────────────────────────────────────────────────────────────────
# 这里用 QUIET：找不到 Qt 时不直接 fatal，而是打印一段「你该装什么」的提示后
# 提前结束。这样 `cmake -B build` 在没装 Qt 的机器上也能跑通，方便先读资料。
set(QT6_REQUIRED_COMPONENTS Widgets Gui Core Network Sql Concurrent Test Svg Xml PrintSupport)
find_package(Qt6 QUIET COMPONENTS ${QT6_REQUIRED_COMPONENTS})

if(NOT Qt6_FOUND)
    message(WARNING
        "\n================= Qt6 没有被找到 =================\n"
        "本库的所有示例都依赖 Qt 6 Widgets，找不到 Qt 时不会生成任何构建目标。\n"
        "请任选一种方式告诉 CMake 去哪里找 Qt：\n"
        "  1) cmake -G \"MinGW Makefiles\" -B build -S . -DCMAKE_PREFIX_PATH=D:/Qt/6.12.0/mingw_64\n"
        "     （本机实测就是这条；装了 Ninja 的机器把 -G 换成 Ninja 也行）\n"
        "  2) 用 Qt 自带的 qt-cmake 包装命令：qt-cmake -G \"MinGW Makefiles\" -B build -S .\n"
        "  3) MSYS2：pacman -S mingw-w64-ucrt-x86_64-qt6-base 之后直接 configure\n"
        "装好之后重新 configure 即可，build 目录不需要清空。\n"
        "==================================================")
    return()
endif()

message(STATUS "Found Qt ${Qt6_VERSION} at ${Qt6_DIR}")

# 某些发行版/精简安装会缺模块（例如没有 QtSvg）。缺了就跳过对应用例，
# 而不是让整个 configure 失败——这样一台机器上少装了一个包，
# 你依然能把 90% 的示例编出来跑出来。
function(qt_add_demo group target)
    foreach(lib ${QT_DEMO_LIBS})
        if(NOT TARGET ${lib})
            message(STATUS "跳过 ${target}：本机 Qt 缺少 ${lib}")
            return()
        endif()
    endforeach()

    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE ${QT_DEMO_LIBS} Threads::Threads)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    # 产物集中放 build/bin：ctest 的工作目录、windeployqt 的收集目录都在一处
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

    set_property(GLOBAL APPEND PROPERTY QT_DEMO_${group} ${target})
    set_property(GLOBAL APPEND PROPERTY QT_DEMO_ALL ${target})

    # 每个示例都是一个测试用例：无头运行，超时 120 秒判失败
    add_test(NAME ${target} COMMAND ${target})
    # QT_QPA_PLATFORM=offscreen     —— 没有显示器也能跑。
    # QT_FORCE_STDERR_LOGGING=1     —— ★Windows 上必须加这一条。
    #   Qt 的消息处理器在「检测到自己没有控制台」时，会把 qInfo()/qDebug() 改投
    #   OutputDebugString（只能被调试器看见），于是 ctest 的 stdout/stderr 全空，
    #   --output-on-failure 打出来是一片空白：用例挂了你也看不到它卡在哪一句证据上。
    #   实测（Qt 6.12 / MinGW / Windows）：不加这条，73 个用例的失败日志一条都收不到。
    #   注意 Windows 投出来的字节是按**本地代码页**（简体中文 = GBK）编的，不是 UTF-8；
    #   想要 UTF-8 就在跑之前 chcp 65001，或者按 _tools/run_all.sh 那样事后 iconv。
    # ★这里**不设** QT_LOGGING_RULES —— 原本设的是 default.warning=false，实测把三个用例判红了：
    #   「default」这个 context 覆盖的正是**没有类别的 qWarning()**，也就是示例自己打的证据行。
    #   后果有两个，都很难查：
    #     · quality_01 用 QTest::ignoreMessage 认领一条 qWarning → 消息被规则提前丢掉，
    #       报 "Not all expected messages were received"（单独跑却过，因为只有 ctest 带着这条规则）；
    #     · quality_05 用 qInstallMessageHandler 数泄漏行数 → 处理器根本没被调用，计数恒为 0。
    #   规矩：**日志规则要在消息处理器之前生效**，会被规则吃掉的消息永远进不了 handler。
    #   真要压 Qt 内部的刷屏，就按 context 点名（qt.core.* / qt.qpa.* 之类），
    #   别动 default —— 那是示例的证据通道。
    set_tests_properties(${target} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
        TIMEOUT 120
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endfunction()

# 每个阶段编完，给这一阶段做一个 all_<group> 聚合目标
function(qt_finish_group group)
    get_property(targets GLOBAL PROPERTY QT_DEMO_${group})
    if(targets)
        add_custom_target(all_${group} DEPENDS ${targets})
        list(LENGTH targets n)
        message(STATUS "  ${group}: ${n} 个示例")
    endif()
endfunction()

# 本组示例默认链接的 Qt 模块；个别示例需要更多模块时在下面单独覆盖
set(QT_DEMO_LIBS Qt6::Widgets)

# ============================================================================
# 第壹阶段 · 起步（start/）
#   一个窗口是怎么从 main() 走到屏幕上的；工程结构、QApplication、日志、无头运行
# ============================================================================
qt_add_demo(start start_01_hello_window          start/01_hello_window.cpp)
qt_add_demo(start start_02_project_anatomy       start/02_project_anatomy.cpp start/startwidget.cpp)
qt_add_demo(start start_03_application_object    start/03_application_object.cpp)
qt_add_demo(start start_04_first_interaction     start/04_first_interaction.cpp)
qt_add_demo(start start_05_logging               start/05_logging.cpp)
qt_add_demo(start start_06_headless_and_offscreen start/06_headless_and_offscreen.cpp)
qt_finish_group(start)

# ============================================================================
# 第肆阶段 · 元对象系统与信号槽（metaobject/）
#   Qt 与别家 GUI 库的根本区别：Q_OBJECT、moc、连接语义、生命周期、属性、反射
# ============================================================================
qt_add_demo(metaobject metaobject_01_signal_slot_basics      metaobject/01_signal_slot_basics.cpp)
qt_add_demo(metaobject metaobject_02_connect_variants        metaobject/02_connect_variants.cpp)
qt_add_demo(metaobject metaobject_03_disconnect_and_connection metaobject/03_disconnect_and_connection.cpp)
qt_add_demo(metaobject metaobject_04_lambda_and_lifetime     metaobject/04_lambda_and_lifetime.cpp)
qt_add_demo(metaobject metaobject_05_moc_anatomy             metaobject/05_moc_anatomy.cpp)
qt_add_demo(metaobject metaobject_06_property_system         metaobject/06_property_system.cpp)
qt_add_demo(metaobject metaobject_07_qobject_tree            metaobject/07_qobject_tree.cpp)
qt_add_demo(metaobject metaobject_08_introspection           metaobject/08_introspection.cpp)
qt_finish_group(metaobject)

# ============================================================================
# 第叁阶段 · 事件系统（events/）
#   事件循环、notify 三层派发、事件过滤器、定时器、输入与焦点、自定义事件、拖放
# ============================================================================
qt_add_demo(events events_01_event_loop              events/01_event_loop.cpp)
qt_add_demo(events events_02_event_dispatch          events/02_event_dispatch.cpp)
qt_add_demo(events events_03_event_filter            events/03_event_filter.cpp)
qt_add_demo(events events_04_timers                  events/04_timers.cpp)
qt_add_demo(events events_05_mouse_keyboard_focus    events/05_mouse_keyboard_focus.cpp)
qt_add_demo(events events_06_custom_events           events/06_custom_events.cpp)
qt_add_demo(events events_07_drag_and_drop           events/07_drag_and_drop.cpp)
qt_finish_group(events)

# ============================================================================
# 第贰阶段 · 控件、布局与外观（widgets/）
# ============================================================================
qt_add_demo(widgets widgets_01_buttons_and_labels      widgets/01_buttons_and_labels.cpp)
qt_add_demo(widgets widgets_02_text_widgets            widgets/02_text_widgets.cpp)
qt_add_demo(widgets widgets_03_containers              widgets/03_containers.cpp)
qt_add_demo(widgets widgets_04_convenience_itemviews   widgets/04_convenience_itemviews.cpp)
qt_add_demo(widgets widgets_05_dialogs                 widgets/05_dialogs.cpp)
qt_add_demo(widgets widgets_06_mainwindow              widgets/06_mainwindow.cpp)
qt_add_demo(widgets widgets_07_layout_basics           widgets/07_layout_basics.cpp)
qt_add_demo(widgets widgets_08_size_policies           widgets/08_size_policies.cpp)
qt_add_demo(widgets widgets_09_style_sheets            widgets/09_style_sheets.cpp)
qt_add_demo(widgets widgets_10_internationalization    widgets/10_internationalization.cpp)
# 第 10 讲第 ⑧ 步要加载一份**本仓库自带**的样本 catalog：
#   widgets/i18n/gauge_zh_CN.ts（填好译文的源文件，进仓库）──lrelease──▶ .qm（构建产物）
# 路径在编译期钉成绝对路径：ctest 把工作目录设成 build/bin，靠运行目录找文件就会
# 时灵时不灵，而这一讲的台账必须是不随环境变的硬结论。
#
# 【勘误·原来这里写的是「.qm 生成后提交进仓库，所以不需要 LinguistTools」】：
#   那句和自家 .gitignore 第 68 行的 `*.qm` 直接打架 —— .qm 永远进不了仓库，
#   而 .ts 当时也还没被 git add，于是**新克隆里 widgets/i18n/ 整个目录都不存在**。
#   本机验过：把 widgets/i18n/ 挪走再跑 widgets_10_internationalization.exe，
#   账1/账2 判错、退出码 1，也就是 ctest 在干净克隆上是红的。
#   现在的口径改成「.ts 进仓库、.qm 现场编」，和 build_deploy/qt_cmake_05_translations
#   一样用 find_package(... QUIET COMPONENTS LinguistTools)，找不到也只是不生成。
set(I18N_SRC_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/widgets/i18n")
set(I18N_BUILD_DIR "${CMAKE_BINARY_DIR}/i18n")
set(I18N_TS        "${I18N_SRC_DIR}/gauge_zh_CN.ts")
set(QTLP_I18N_DIR  "${I18N_SRC_DIR}")          # 兜底：手工 lrelease 到源码目录也能跑通
if(NOT EXISTS "${I18N_TS}")
    message(WARNING "缺 ${I18N_TS}：第 10 讲第 ⑧ 步的样本 catalog 没了，"
                    "账1/账2 会判错（这个 .ts 是仓库自带的源文件，别删、也别漏提交）")
elseif(TARGET widgets_10_internationalization)
    find_package(Qt6 QUIET COMPONENTS LinguistTools)
    if(Qt6LinguistTools_FOUND)
        file(MAKE_DIRECTORY "${I18N_BUILD_DIR}")
        add_custom_command(OUTPUT "${I18N_BUILD_DIR}/gauge_zh_CN.qm"
            COMMAND Qt6::lrelease "${I18N_TS}" -qm "${I18N_BUILD_DIR}/gauge_zh_CN.qm"
            DEPENDS "${I18N_TS}"
            COMMENT "lrelease：gauge_zh_CN.ts → build/i18n/gauge_zh_CN.qm（第 10 讲的样本 catalog）")
        add_custom_target(gauge_zh_CN_qm DEPENDS "${I18N_BUILD_DIR}/gauge_zh_CN.qm")
        set(QTLP_I18N_DIR "${I18N_BUILD_DIR}")
    else()
        message(WARNING "本机 Qt 没装 LinguistTools：第 10 讲第 ⑧ 步的 .qm 得自己生成 —— "
                        "lrelease \"${I18N_TS}\" -qm \"${I18N_SRC_DIR}/gauge_zh_CN.qm\"")
    endif()
endif()
if(TARGET widgets_10_internationalization)
    target_compile_definitions(widgets_10_internationalization PRIVATE
        QTLP_I18N_DIR="${QTLP_I18N_DIR}")
    if(TARGET gauge_zh_CN_qm)
        add_dependencies(widgets_10_internationalization gauge_zh_CN_qm)
    endif()
endif()
qt_finish_group(widgets)

# ============================================================================
# 第伍阶段 · 绘图与自定义控件（painting/）
#   每个示例都会把绘制结果存成 PNG，跑完在 build/bin 下能看到图
# ============================================================================
set(QT_DEMO_LIBS Qt6::Widgets Qt6::Svg)
qt_add_demo(painting painting_01_painter_basics           painting/01_painter_basics.cpp)
qt_add_demo(painting painting_02_coordinates_and_transforms painting/02_coordinates_and_transforms.cpp)
qt_add_demo(painting painting_03_pen_brush_gradients      painting/03_pen_brush_gradients.cpp)
qt_add_demo(painting painting_04_text_and_font_metrics    painting/04_text_and_font_metrics.cpp)
qt_add_demo(painting painting_05_images_and_pixmaps       painting/05_images_and_pixmaps.cpp)
qt_add_demo(painting painting_06_custom_widget            painting/06_custom_widget.cpp)
qt_add_demo(painting painting_07_painter_path_and_clipping painting/07_painter_path_and_clipping.cpp)
qt_add_demo(painting painting_08_graphicsview_basics      painting/08_graphicsview_basics.cpp)
qt_finish_group(painting)
set(QT_DEMO_LIBS Qt6::Widgets)

# ============================================================================
# 第陆阶段 · 模型/视图与委托（model_view/）
# ============================================================================
qt_add_demo(model_view model_view_01_architecture         model_view/01_architecture.cpp)
qt_add_demo(model_view model_view_02_custom_table_model   model_view/02_custom_table_model.cpp)
qt_add_demo(model_view model_view_03_editing_and_flags    model_view/03_editing_and_flags.cpp)
# 实测（Qt 6.12.0 / MinGW-w64 GCC 13.1 / Windows）：这一讲在默认 Release 的 -O3 下
# 稳定段错误，-O0/-O1/-O2 都正常；把 -O3 加 -fno-strict-aliasing 或
# -fno-tree-slp-vectorize 任一开关换掉也就好了 —— 典型的「优化器敏感的别名类 UB」，
# 崩点落在 insertRows 里一个临时 QString 的析构上，源码层面查不出越界。
# 学习材料不该让人撞这种玄学，所以单独把这一讲钉在 -O2（GCC 才会加，其它编译器不动）。
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(model_view_03_editing_and_flags PRIVATE -O2)
endif()
qt_add_demo(model_view model_view_04_delegate             model_view/04_delegate.cpp)
qt_add_demo(model_view model_view_05_proxy_model          model_view/05_proxy_model.cpp)
qt_add_demo(model_view model_view_06_selection_model      model_view/06_selection_model.cpp)
qt_add_demo(model_view model_view_07_item_models          model_view/07_item_models.cpp)
qt_add_demo(model_view model_view_08_view_tuning          model_view/08_view_tuning.cpp)
qt_finish_group(model_view)

# ============================================================================
# 第柒阶段 · 并发与线程（threads/）
# ============================================================================
set(QT_DEMO_LIBS Qt6::Widgets Qt6::Concurrent)
qt_add_demo(threads threads_01_thread_basics              threads/01_thread_basics.cpp)
qt_add_demo(threads threads_02_thread_affinity            threads/02_thread_affinity.cpp)
qt_add_demo(threads threads_03_cross_thread_signals       threads/03_cross_thread_signals.cpp)
qt_add_demo(threads threads_04_synchronization           threads/04_synchronization.cpp)
qt_add_demo(threads threads_05_qtconcurrent              threads/05_qtconcurrent.cpp)
qt_add_demo(threads threads_06_qrunnable_threadpool      threads/06_qrunnable_threadpool.cpp)
qt_add_demo(threads threads_07_cancel_and_shutdown       threads/07_cancel_and_shutdown.cpp)
# 第 8 讲讲进程间通信，用的是 QtNetwork 里的本地套接字（Windows 上是named pipe，
# Linux/macOS 上是本地 Unix socket），所以要额外链 Qt6::Network。
set(QT_DEMO_LIBS Qt6::Widgets Qt6::Concurrent Qt6::Network)
qt_add_demo(threads threads_08_local_ipc                 threads/08_local_ipc.cpp)
qt_finish_group(threads)
set(QT_DEMO_LIBS Qt6::Widgets)

# ============================================================================
# 第捌阶段 · 数据与持久化（storage/）
# ============================================================================
set(QT_DEMO_LIBS Qt6::Widgets Qt6::Sql Qt6::Xml)
qt_add_demo(storage storage_01_qsettings                  storage/01_qsettings.cpp)
qt_add_demo(storage storage_02_file_io                    storage/02_file_io.cpp)
qt_add_demo(storage storage_03_json                       storage/03_json.cpp)
# 第 4 讲除了资源系统本身，还用 QSvgRenderer 把 qrc 里的 SVG 画出来当证据，
# 所以要额外链 Qt6::Svg（只给这一个示例开，别让整个 storage 组都被它卡住）。
set(QT_DEMO_LIBS Qt6::Widgets Qt6::Sql Qt6::Xml Qt6::Svg)
qt_add_demo(storage storage_04_resource_system            storage/04_resource_system.cpp storage/resource_demo.qrc)
set(QT_DEMO_LIBS Qt6::Widgets Qt6::Sql Qt6::Xml)
qt_add_demo(storage storage_05_sql_database               storage/05_sql_database.cpp)
qt_add_demo(storage storage_06_sql_model_view             storage/06_sql_model_view.cpp)
qt_finish_group(storage)
set(QT_DEMO_LIBS Qt6::Widgets)

# ============================================================================
# 第玖阶段 · 网络（network/）
#   涉及真实网络的用例都连本地回环，不依赖外网；断网也能跑完
# ============================================================================
set(QT_DEMO_LIBS Qt6::Widgets Qt6::Network)
qt_add_demo(network network_01_network_access_manager     network/01_network_access_manager.cpp)
qt_add_demo(network network_02_rest_client                network/02_rest_client.cpp)
qt_add_demo(network network_03_tcp_client_server          network/03_tcp_client_server.cpp)
qt_add_demo(network network_04_download_and_progress      network/04_download_and_progress.cpp)
qt_add_demo(network network_05_dns_and_ssl                network/05_dns_and_ssl.cpp)
qt_finish_group(network)
set(QT_DEMO_LIBS Qt6::Widgets)

# ============================================================================
# 第拾阶段 · 工程质量与资深实践（quality/）
#   前两个是 QtTest 用例（正常退出码 0 = 通过），后面几个是「可跑的架构图」
# ============================================================================
set(QT_DEMO_LIBS Qt6::Widgets Qt6::Test Qt6::Concurrent)
qt_add_demo(quality quality_01_qtest_unit                 quality/01_qtest_unit.cpp)
qt_add_demo(quality quality_02_qtest_gui                  quality/02_qtest_gui.cpp)
qt_add_demo(quality quality_03_architecture_layers        quality/03_architecture_layers.cpp)
qt_add_demo(quality quality_04_ownership_and_lifetime     quality/04_ownership_and_lifetime.cpp)
qt_add_demo(quality quality_05_error_handling             quality/05_error_handling.cpp)
qt_add_demo(quality quality_06_performance_profiling      quality/06_performance_profiling.cpp)
qt_add_demo(quality quality_07_large_view_optimization    quality/07_large_view_optimization.cpp)
qt_finish_group(quality)
set(QT_DEMO_LIBS Qt6::Widgets)

# ============================================================================
# 第拾壹阶段 · 构建与部署（build_deploy/）
#   这里每个子目录都是一个**独立工程**（各自有自己的 CMakeLists.txt 和 project()），
#   所以要单独 configure，不能由顶层一次性 add_subdirectory：
#     cd build_deploy/qt_cmake_01_widgets
#     cmake -G "MinGW Makefiles" -B build -S . && cmake --build build
#   顶层只负责把它们的存在检查一下，真正的学习从模块首页进。
# ============================================================================
file(GLOB BUILD_DEMO_SUBDIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/build_deploy
     ${CMAKE_CURRENT_SOURCE_DIR}/build_deploy/qt_cmake_*)
foreach(sub ${BUILD_DEMO_SUBDIRS})
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/build_deploy/${sub}/CMakeLists.txt)
        message(STATUS "  build_deploy 独立工程：${sub}（进该目录单独 configure）")
    endif()
endforeach()

# ============================================================================
# 综合实战项目（projects/todo_desktop/）
# ============================================================================
option(BUILD_TODO_DESKTOP "构建综合实战项目 todo_desktop" ON)
if(BUILD_TODO_DESKTOP AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/projects/todo_desktop/CMakeLists.txt)
    add_subdirectory(projects/todo_desktop)
endif()

# ============================================================================
# 汇总目标与 configure 结尾报告
# ============================================================================
get_property(QT_ALL_DEMOS GLOBAL PROPERTY QT_DEMO_ALL)
if(QT_ALL_DEMOS)
    add_custom_target(all_demos DEPENDS ${QT_ALL_DEMOS})
    list(LENGTH QT_ALL_DEMOS TOTAL_DEMOS)
else()
    set(TOTAL_DEMOS 0)
endif()

message(STATUS "")
message(STATUS "已注册 ${TOTAL_DEMOS} 个示例（每个都同时是一个 ctest 用例）")
message(STATUS "  编译全部：cmake --build build --target all_demos")
message(STATUS "  跑通全部：ctest --test-dir build --output-on-failure")
message(STATUS "  只跑一个：ctest --test-dir build -R start_01 --output-on-failure")
message(STATUS "")
