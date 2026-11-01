# 桌面待办管理器 · todo_desktop

> 综合实战项目：把前面十一个阶段拧成一个能跑、能测、能交付的 Qt 6 桌面程序。

![Qt](https://img.shields.io/badge/Qt-6.12-41CD52?logo=qt)
![C%2B%2B](https://img.shields.io/badge/C%2B%2B-17-00599C)
![Build](https://img.shields.io/badge/build-CMake%20%7C%20Ninja-orange)

---

## 这是什么

不是一个「练手小 demo」，而是一个**结构完整的小工程**：业务/数据层是静态库，界面层只依赖它；正式程序自带无界面自检；单元测试测的就是正式实现里的那几个类。

功能不贪多，但每一处都能指出用的是哪一阶段的知识：

| 功能 | 用到的阶段 |
|------|-----------|
| `QTableView` + 手写 `QAbstractTableModel`（四列、自定义 role、精确 `dataChanged`） | 第陆阶段 |
| `QSortFilterProxyModel` 子类：搜索标题/备注、只看未完成、按优先级/日期排序 | 第陆阶段 |
| 自定义委托：优先级画成彩色胶囊，`sizeHint` 撑高行 | 第陆阶段 · 第伍阶段 |
| 选择模型：选中行 → 编辑/删除/切换完成按钮的启用状态 | 第陆阶段 |
| 新增/编辑对话框（`QFormLayout` + 日期 + 备注，空标题拦截） | 第贰阶段 |
| SQLite 落库：具名连接、建表、`INSERT OR REPLACE`、事务批量导入 | 第捌阶段 |
| JSON 导入导出：`QSaveFile` 原子写、宽松解析、坏文件拒收 | 第捌阶段 |
| 后台线程算统计（总数/完成/逾期/各优先级 + 耗时），结果走信号回主线程 | 第柒阶段 · 第肆阶段 |
| 拖一个 `.json` 到窗口即导入 | 第叁阶段（拖放） |
| `QSettings` 记住窗口大小与表头宽度，关闭时保存 | 第捌阶段 |
| `--self-check` 无界面自检 + QtTest 单元测试，都注册进 ctest | 第拾阶段 · 第拾壹阶段 |
| CMake：静态库 + 应用 + 测试三个目标，AUTOMOC、CTest、独立可 configure | 第拾壹阶段 |

---

## 两种跑法

### 1. 正常界面

```bash
./build/bin/todo_desktop
```

- 数据默认落在系统的应用数据目录（`QStandardPaths::AppDataLocation`）下的 `todo.db`
- 窗口大小/表头状态写在 `QSettings`（组织名 `QtLearningPath`，应用名 `TodoDesktop`）
- 第一次运行、库里没有任务时，会自动放 3 条样例（其中一条故意逾期，方便看红色与排序）
- 双击一行编辑；勾「完成」列切换状态；工具条支持新增/删除/导入/导出/清除已完成
- 把导出的 `.json` 直接拖到窗口里 = 追加导入（原有任务不覆盖）

> 这两个位置都是这个程序**自己**的目录，不会碰别的应用的配置。想彻底不碰本机，就用下面的自检模式。

### 2. 无界面自检

```bash
./build/bin/todo_desktop --self-check
echo $?        # 0 = 全部通过
```

自检在 `QTemporaryDir` 里建库、写文件，跑完即删；它做 **38 项断言**，覆盖：

- 模型插行/删行/改完成状态、`dataChanged` 与 `countsChanged` 是否真的发出
- 自定义 role 取回的是数值而不是显示文本；空标题被拒绝
- 代理的「只看未完成」「搜索标题与备注」、`mapToSource` 行号映射
- SQLite 写入/读回字段一致、按 id 删除、整批替换、清除已完成、连接关闭幂等
- JSON 往返（含引号、换行、空日期）、坏 JSON 与缺 `tasks` 数组被拒
- 同步统计的数字；后台线程统计在超时内返回且结果一致

任何一项 FAIL 都会让进程以非 0 退出——CI 不看日志，只看退出码。

---

## 构建

### 在仓库顶层一起构建（推荐）

```bash
cmake -G "MinGW Makefiles" -B build -S . -DCMAKE_PREFIX_PATH=D:/Qt/6.12.0/mingw_64
cmake --build build --target todo_desktop tst_taskmodel
ctest --test-dir build -R "todo_|tst_taskmodel" --output-on-failure
```

顶层 `CMakeLists.txt` 已经用 `option(BUILD_TODO_DESKTOP ...)` + `add_subdirectory` 接上了这个项目，关掉它用 `-DBUILD_TODO_DESKTOP=OFF`。

### 单独构建（验证「一个工程能不能独立 configure」）

```bash
cd projects/todo_desktop
cmake -G "MinGW Makefiles" -B build -S . -DCMAKE_PREFIX_PATH=D:/Qt/6.12.0/mingw_64
cmake --build build
ctest --test-dir build --output-on-failure
```

这一步本身就是第拾壹阶段的知识点：`find_package(Qt6 COMPONENTS Widgets Sql Concurrent)`、`AUTOMOC`、`RUNTIME_OUTPUT_DIRECTORY`、`add_test` 各司其职，放到哪个目录都能 configure。

### 部署（第拾壹阶段的收口）

```bash
mkdir deploy
cp build/bin/todo_desktop.exe deploy/
windeployqt --release --no-translations deploy/todo_desktop.exe
```

`windeployqt` 会把 Qt 运行库、平台插件（`platforms/qwindows.dll`）、SQL 驱动插件（`sqldrivers/qsqlite.dll`）一起拷进来。少任何一个，症状分别是「<code>0xC0000135</code>」和「no Qt platform plugin could be initialized」。具体排查见 [`../../build_deploy/qt_cmake_guide.html`](../../build_deploy/qt_cmake_guide.html) 与 [`../../visuals/deployment_tree.html`](../../visuals/deployment_tree.html)。

---

## 文件地图

```text
projects/todo_desktop/
├── CMakeLists.txt              # todo_core 静态库 + 应用 + 两个 ctest 用例
├── README.md                   # 本文件
├── tutorial.md                 # 学习指南：按什么顺序读、每步的重点与坑
│
├── src/
│   ├── main.cpp                # 两种模式的分叉；设置 org/app 名；数据目录
│   ├── core/                   # ← 不依赖 QWidget 的一层，测试只测这里
│   │   ├── task.h / .cpp       # Task、Priority、TaskStats、computeStats
│   │   ├── taskmodel.h / .cpp  # QAbstractTableModel：四列 + role + 编辑回路
│   │   ├── taskfilterproxy.*   # QSortFilterProxyModel：过滤 + 排序
│   │   ├── taskrepository.*    # SQLite 仓储：具名连接、事务、CRUD
│   │   ├── jsoncodec.*         # JSON 往返 + QSaveFile 原子写
│   │   └── statsservice.*      # QtConcurrent + QFutureWatcher 后台统计
│   ├── ui/
│   │   ├── mainwindow.*        # 工具栏、搜索、表格、状态栏、拖放、QSettings
│   │   ├── taskdialog.*        # 新增/编辑对话框
│   │   └── prioritydelegate.*  # 优先级彩色胶囊委托
│   └── app/
│       └── selfcheck.*         # --self-check：38 项断言，退出码即结论
│
└── tests/
    └── tst_taskmodel.cpp       # QtTest：模型/代理/JSON/统计（无需窗口）
```

约 2,500 行（含注释），按「领域 → 模型 → 代理 → 仓储 → 序列化 → 后台 → 界面 → 自检」的顺序读。

---

## 几个设计决定，以及为什么

**为什么手写模型，不用 `QStandardItemModel`？**
数据本来就在 `Task` 里，用便利类等于再维护一份 `QStandardItem` 镜像；而且这个项目只需要四列、需要精确控制 `dataChanged`。手写一遍五个虚函数，也正是第陆阶段的考核点。

**为什么过滤放在代理，而不是模型里？**
模型回答「全部数据是什么」，代理回答「视图现在愿意看哪一部分」。搜索每敲一个字就重建一次模型，会把选择、滚动位置、持久索引一起弄乱；代理只换映射。

**为什么统计要放后台线程？**
任务上千条时，一次全量统计虽不至于卡死，但会占住界面线程。这里顺便把「按值拷贝快照、结果走信号、忙时合并请求」这三条跨线程规矩落成代码。

**为什么导入是「追加」而不是「覆盖」？**
覆盖会让用户误操作后无法恢复。追加只增不删；真要替换，先清空再导入，或者直接用导出的文件重建库。

**为什么用 SQLite，而不是 QSettings 存任务？**
QSettings 适合「少量键值配置」（窗口大小、表头宽度），不适合「一组会增删的行」。任务的形状是表，表就该进库。

**为什么自检不建窗口？**
因为窗口没法在 CI 里断言。业务逻辑不依赖 `QWidget`，自检才能用退出码说话——这也是分层架构最实际的好处。

**为什么写库前要把 null 字符串归一成空串？**
`Task.notes`/`Task.title` 的默认值是 `QString()`（null），不是空串 `""`。QSQLITE 驱动绑参时把 null `QString` 认成 SQL `NULL`（`qsql_sqlite.cpp` 的 `exec()` → `QSqlResultPrivate::isVariantNull()` → `sqlite3_bind_null`），而 `notes`/`title` 是 `NOT NULL` 列。巧的是 `upsert()` 的 `INSERT OR REPLACE` 会用 REPLACE 冲突解决把 NULL 悄悄换成默认值 `''`，而 `replaceAll()` 的普通 `INSERT` 会当场报 `NOT NULL constraint failed`——同一份数据两条路径一隐一现，正是自检当初跑出来才暴露的 bug。仓储层用 `notNull()` 在边界把 null 归一成 `""`，两条路径行为才对齐。

---

## 数据与安全

- 正常模式的 `todo.db` 在应用数据目录；`QSettings` 在 `QtLearningPath/TodoDesktop`
- 自检模式**只在临时目录里操作**，退出时删掉，不产生任何用户级残留
- 程序不联网、不需要管理员权限、不写注册表的自定义键
- JSON 导入只接受本地文件；对畸形输入一律「拒收并说清原因」，不做静默修补

---

## 已知边界

- **本机已真实构建并运行过**（Qt 6.12.0 + MinGW 13.1.0，Windows，`QT_QPA_PLATFORM=offscreen`）：
  静态库 `todo_core`、可执行 `todo_desktop`、QtTest 目标 `tst_taskmodel` 全部编译链接通过；
  `todo_desktop --self-check` **38 项全 `[PASS]`、退出码 0**；`ctest` 2/2 通过
  （`todo_selfcheck` 0.13 s、`tst_taskmodel` 9 passed / 0 failed / 0 skipped）。
  两条路径都验过：① 顶层 `cmake -B build` 里以 `add_subdirectory` 构建；
  ② 在本目录单独 `cmake -G "MinGW Makefiles" -B build -S .` 也configure/build/ctest 全通。
  ★ ②这条以前是**通的假的**：这份 `CMakeLists.txt` 没有 `cmake_minimum_required()`/`project()`
  （它一直靠顶层提供），单独 configure 当场 `CMake Error: No cmake_minimum_required command is present`。
  现在用 `if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)` 守卫补齐，两种用法都实测过。
- 界面细节（行高、胶囊颜色、状态栏文字）在不同平台/主题下会有出入，这属于样式差异，不是缺陷。
  ⚠ 这一条**不在**上面的「已验证」范围内：offscreen 平台不往屏幕画东西，像素级的东西只有人眼在真显示器上说得准。
- 没有做撤销/重做、没有多用户、没有云同步——这些不是本项目的目标。

---

## 下一步可以自己加什么

1. **撤销栈**：`QUndoStack` + `QUndoCommand`，把「删除」「切换完成」做成可撤销
2. **标签/分组**：加第五列与代理的 `QSortFilterProxyModel` 组合过滤
3. **`QSqlTableModel` 版本**：用便利类重写仓储，对比两份代码的取舍（第捌阶段第 6 讲的战场）
4. **托盘与提醒**：`QSystemTrayIcon` + 到期日定时器，练 `QTimer` 与生命周期
5. **导出 CSV**：`QTextStream` 引号转义，练文件 IO 的边界

每加一项，都问自己三个问题：数据归谁、信号谁发、失败怎么告诉用户。答得上来，再加。
