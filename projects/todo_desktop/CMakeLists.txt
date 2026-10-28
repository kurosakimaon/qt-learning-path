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
#      cmake -G Ninja -B build -S . -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64
#      cmake --build build
#      ctest --test-dir build --output-on-failure
# ============================================================================

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
