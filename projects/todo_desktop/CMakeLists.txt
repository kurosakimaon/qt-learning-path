# ============================================================================
#  综合实战 · 桌面待办管理器（todo_desktop）
# ----------------------------------------------------------------------------
#  这是一份「正常的小工程」，不是讲义示例：
#    - 业务/数据层做成静态库 todo_core，界面层只依赖它
#    - 应用与测试共用同一个 todo_core，测试测的就是正式实现
#    - 自带两个 ctest 用例：无界面自检（--self-check）与 QtTest 单元测试
#
#  被顶层 CMakeLists.txt 以 add_subdirectory 引入；
#  也可以单独构建（验证「工程能独立 configure」这件事）：
#      cd projects/todo_desktop
#      cmake -G "MinGW Makefiles" -B build -S . -DCMAKE_PREFIX_PATH=D:/Qt/6.12.0/mingw_64
#      cmake --build build
#      ctest --test-dir build --output-on-failure
# ============================================================================

# ── 一份 CMakeLists，两种用法：靠这个守卫区分「我是不是顶层」───────────────
#   ① 顶层 add_subdirectory 进来：cmake_minimum_required() / project() 已经由顶层
#      做过。这里**不能**再叫一次 project() —— 它会新开一个作用域，把
#      CMAKE_SOURCE_DIR / CMAKE_BINARY_DIR 这套变量换成本目录，于是上面注释里
#      「产物在 ${CMAKE_BINARY_DIR}/bin」的约定就跟着变了（bin 会跑到本目录下）。
#   ② 单独进这个目录 configure（文件头教的、README 写的就是这条）：
#      ★ 本机实测：加这个守卫**之前**，单独 configure 直接
#        `CMake Error: No cmake_minimum_required command is present` →
#        "Configuring incomplete, errors occurred!"，一条命令都走不通。
#        也就是说文件头那段「也可以单独构建」当时只是打算，没验证过。
#      判据用 CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR：
#      被 add_subdirectory 时两者不同，自己就是顶层时相同。
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.21)
    project(todo_desktop LANGUAGES CXX VERSION 1.0.0)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_AUTOMOC ON)
    # --self-check 的证据是 qInfo() 打的字：别让程序变成无控制台的 GUI 子系统。
    set(CMAKE_WIN32_EXECUTABLE OFF)
    set(CMAKE_MACOSX_BUNDLE OFF)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Concurrent)
find_package(Qt6 QUIET COMPONENTS Test)

# 顶层已经开过；单独 configure 这份目录时也要开，否则 Q_OBJECT 类会在链接期
# 报 "vtable for TaskModel 未定义"。这就是第拾壹阶段讲的那个最常见误诊。
if(NOT CMAKE_AUTOMOC)
    set(CMAKE_AUTOMOC ON)
endif()

set(TODO_CORE_SOURCES
    src/core/task.h
    src/core/task.cpp
    src/core/taskmodel.h
    src/core/taskmodel.cpp
    src/core/taskfilterproxy.h
    src/core/taskfilterproxy.cpp
    src/core/taskrepository.h
    src/core/taskrepository.cpp
    src/core/jsoncodec.h
    src/core/jsoncodec.cpp
    src/core/statsservice.h
    src/core/statsservice.cpp
)

# ── 业务与数据层：不依赖 QWidget，所以测试可以不建窗口 ────────────────────
add_library(todo_core STATIC ${TODO_CORE_SOURCES})

target_include_directories(todo_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# PUBLIC：用的是静态库，依赖必须继续传给最终链接到它的可执行文件，
# 否则 app 会缺 Qt6::Sql / Qt6::Concurrent 的符号。
target_link_libraries(todo_core PUBLIC
    Qt6::Core
    Qt6::Gui        # QColor / QFont（模型与委托里的颜色）在 Gui 里
    Qt6::Sql
    Qt6::Concurrent
)

# ── 应用 ────────────────────────────────────────────────────────────────────
qt_add_executable(todo_desktop
    src/main.cpp
    src/app/selfcheck.h
    src/app/selfcheck.cpp
    src/ui/mainwindow.h
    src/ui/mainwindow.cpp
    src/ui/taskdialog.h
    src/ui/taskdialog.cpp
    src/ui/prioritydelegate.h
    src/ui/prioritydelegate.cpp
)

target_link_libraries(todo_desktop PRIVATE
    todo_core
    Qt6::Widgets
)

set_target_properties(todo_desktop PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ── 测试 ────────────────────────────────────────────────────────────────────
enable_testing()

add_test(NAME todo_selfcheck COMMAND todo_desktop --self-check)
set_tests_properties(todo_selfcheck PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

if(TARGET Qt6::Test)
    qt_add_executable(tst_taskmodel
        tests/tst_taskmodel.cpp
    )
    target_link_libraries(tst_taskmodel PRIVATE
        todo_core
        Qt6::Test
    )
    set_target_properties(tst_taskmodel PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    add_test(NAME tst_taskmodel COMMAND tst_taskmodel)
    set_tests_properties(tst_taskmodel PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 60
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
else()
    message(STATUS "未找到 Qt6::Test，跳过 tst_taskmodel（应用与自检仍会构建）")
endif()
