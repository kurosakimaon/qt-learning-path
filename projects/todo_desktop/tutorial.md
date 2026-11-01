# 综合实战 · 桌面待办管理器 todo_desktop

> 把十一个阶段拧成一个工程：从 `main()` 进来，到部署出去，中间每一层都指得出出处。

## 一、项目简介

徒弟，前面十一个阶段，你是「一个知识点一个示例」地学。示例的好处是干净，坏处也是干净——每一讲的代码都只为一件事服务，没有人把模型、代理、委托、数据库、线程、拖放、设置、测试塞进同一个窗口里让你收拾。

这个项目就是来收拾的。

它不大：四列表格、增删改查、搜索过滤、SQLite 落库、JSON 导入导出、后台统计、拖放导入、记住窗口大小。总共两千四百多行，但分层是认真的：

- **`todo_core` 静态库**：领域模型、表模型、过滤代理、SQLite 仓储、JSON 编解码、后台统计。**一行 `QWidget` 都没有**。
- **`todo_desktop` 应用**：主窗口、对话框、委托、入口。
- **`tst_taskmodel` 单元测试**：只链 `todo_core`，不需要窗口。

这样分不是为了好看。它带来三个实打实的好处：

1. **能测**：业务逻辑不依赖界面，QtTest 和 `--self-check` 才能在无头环境里断言。
2. **能换**：今天用 `QTableView`，明天换成 `QListView` 或命令行工具，`todo_core` 不用动。
3. **能讲清**：面试官问「你的模型和数据库怎么分工」，你可以直接指文件，而不是说「都在 MainWindow 里」。

> 实测标注（本机 Qt 6.12.0 + MinGW GCC 13.1.0，Windows，`QT_QPA_PLATFORM=offscreen`）：这套代码**已在本机完整编译、链接、运行**。`-Wall -Wextra` 下 0 error / 0 warning；`todo_desktop --self-check` 38 项全 `[PASS]`、退出码 0；`ctest` 2/2 通过（`tst_taskmodel` 9 passed / 0 failed / 0 skipped）。下面凡是「会发生什么」，都已对着本机运行输出核对过；只有文末那张**人眼界面表**仍然是未验证项 —— offscreen 不往屏幕画东西，像素级的事只有真显示器上看得准。

---

## 二、学完能做到什么

读完并动手改过这个项目，你应该能：

1. 说清一个真实 Qt 工程为什么按「core / ui / app / tests」分层，以及**哪一层不许出现 `QWidget`**
2. 手写一个四列表模型：`data` / `setData` / `flags` / `headerData` / `insertRows` / `removeRows` 六个入口各管什么，`dataChanged` 的 `roles` 参数为什么值得传
3. 在代理层同时做「只看未完成」和「标题/备注搜索」，并解释为什么筛选状态变了要 `beginFilterChange()` / `endFilterChange()`
4. 拿到视图的 `currentIndex()` 时先问一句「它属于谁」，跨层取数据一律 `mapToSource`
5. 用具名连接写一个 SQLite 仓储，知道 `removeDatabase()` 之前为什么必须先让所有 `QSqlDatabase` 副本析构
6. 用 `QSaveFile` 做原子导出，用宽松策略做导入，并说清「追加导入」与「覆盖导入」的取舍
7. 把一次全量统计丢进线程池，按值传快照、用 `QFutureWatcher` 收结果、忙时只保留最后一次请求
8. 给 `QTableView` 的一列换上自定义委托，且不破坏选中高亮
9. 把业务自检做成 `--self-check`，让 CI 只看退出码；再写一组 QtTest 用例测同一批类
10. 独立 configure 这个工程、用 `ctest` 跑它、用 `windeployqt` 把它搬到别的电脑上

---

## 三、读代码的顺序（别从 MainWindow 开始）

很多人一进项目就打开 `mainwindow.cpp`，然后被三百行信号槽淹没。师傅给你换一条路：**先读数据长什么样，再读它怎么被搬来搬去，最后才读谁把它画出来。**

```text
  ① src/core/task.h / .cpp            ← 一条任务是什么；统计怎么算
  ② src/core/taskmodel.h / .cpp       ← 一组任务怎么变成四列表格
  ③ src/core/taskfilterproxy.*        ← 视图为什么只看见一部分
  ④ src/core/taskrepository.*         ← 数据怎么落进 SQLite
  ⑤ src/core/jsoncodec.*              ← 数据怎么跟外界交换
  ⑥ src/core/statsservice.*           ← 重活怎么挪到后台
  ⑦ src/ui/prioritydelegate.*         ← 一格怎么画成胶囊
  ⑧ src/ui/taskdialog.*               ← 一条任务怎么被编辑
  ⑨ src/ui/mainwindow.*               ← 上面这些怎么被接起来
  ⑩ src/app/selfcheck.cpp             ← 怎么证明它们真的对
  ⑪ tests/tst_taskmodel.cpp           ← 怎么用 QtTest 再证一遍
  ⑫ CMakeLists.txt                    ← 怎么把三层编成三个目标
```

### ① `task.h` / `task.cpp` —— 先把「一条任务」钉死

**为什么先读它**：后面所有层都在搬运 `Task`。它的字段、默认值、`id = 0` 的含义，决定了模型和仓储的边界。

**重点**：

- `enum class Priority { Low, Normal, High }`：枚举不存字符串。数据库存 `int`、界面显示中文、排序按数值，三件事各自独立。
- `QDate due` 用**无效日期**表示「没有截止日期」，不另设 `bool hasDue`。这是 Qt 的惯例：`QDate()` 就是空值，`isValid()` 一问便知。
- `isOverdue(task, today)` 和 `computeStats(tasks, today)` 都把 `today` 做成**可注入参数**（默认 `QDate::currentDate()`）。测试才能传固定日期，不然「昨天/明天」的断言会在跨零点时随机翻车。
- 已完成的任**不算逾期**——`computeStats` 里先判 `done` 再判日期。

**坑**：

- `enum class` 不能隐式转 `int`。落库要 `static_cast<int>(task.priority)`，读库统一走 `priorityFromInt()`；那个函数对越界值回落到 `Normal`，不让一条脏数据把整个导入搞崩。
- `Task` 故意不是 `QObject`：它能按值拷贝、能塞进 `QVector`、能被 lambda 捕获。别给它加信号。

### ② `taskmodel.*` —— 手写模型的六个入口

**为什么第二读它**：它是整个项目的腰。上面接着界面，下面接着 `QVector<Task>`，左边接着代理，右边接着数据库。

**重点**：

- 四个列由 `enum Column { TitleColumn, PriorityColumn, DueColumn, DoneColumn, ColumnCount }` 定死。列号是常量，不是魔法数字。
- 自定义 role 从 `Qt::UserRole + 1` 开始：`IdRole`、`DoneRole`、`PriorityRole`、`DueRole`、`NotesRole`。**委托按 `PriorityRole` 取数值决定颜色，不解析显示文本**——显示文本是给人看的，随时可能改。
- `data()` 里 `DisplayRole` / `EditRole` / `CheckStateRole` / `TextAlignmentRole` / `ForegroundRole` / `ToolTipRole` 各司其职；逾期行前景色 `#b91c1c`，已完成行 `#94a3b8`。
- `setData()` 只认三列：标题（trim 后非空才收）、优先级、截止日期（无效日期 = 清空）。改完发 `dataChanged(index, index, {role})`，并把完成状态的变化通过 `countsChanged(total, done)` 报给状态栏。
- `setDone()` 改的是**整行**的完成态，所以 `dataChanged` 的右边界是 `index(row, ColumnCount - 1)`，不是单格。
- `removeCompleted()` **从后往前删**，每删一行都走 `beginRemoveRows` / `endRemoveRows`。正着删会让后面的行号整体前移，删一半漏一半。
- `insertRows()` 给新行一个默认标题「新任务 N」，并分配递增 id。

**坑**：

- 模型里**不许**直接 `m_tasks.removeAt()` 而不包 begin/end。视图会拿着旧行号来问新数据，轻则错位，重则越界。
- `data()` 是 `const` 热路径。这个项目里它只做「取字段 + 小包装」，没有查库、没有正则、没有大对象构造。
- 改完不发 `dataChanged`，屏幕不会自己更新——这是第陆阶段反复讲的那条「一改就错、一滚就对」。

> 想再看一遍拉取式与 `dataChanged` 的关系：[第陆阶段讲义](../../model_view/tutorial.md) · [图解 · 模型/视图数据流](../../visuals/model_view.html)。

### ③ `taskfilterproxy.*` —— 第二套坐标系

**为什么第三读它**：模型回答「全部数据是什么」，代理回答「视图现在愿意看哪一部分」。这两件事混在一起，搜索每敲一个字就得重建模型，选择、滚动位置、持久索引全乱。

**重点**：

- 两个外部判据：`m_onlyOpen` 与 `m_query`。改它们时用 `beginFilterChange()` / `endFilterChange(Direction::Rows)` 包住——Qt 6.10 起 `invalidateFilter()` 已标废弃，而且它不告诉视图「变的是行还是列」。
- `filterAcceptsRow()` 同时看 `title` 和 `notes`，所以不能只用 `setFilterFixedString()`（它只盯一列）。
- `lessThan()` 自己定排序：优先级按数值、日期按先后（**无效日期排最后**）、完成状态、标题用 `localeAwareCompare` 照顾中文。
- 构造函数里 `setDynamicSortFilter(true)`：源数据变了，代理要自动重排/重筛。

**坑**：

- 视图手里的 `QModelIndex` 属于**代理**。拿去问源模型不会报错，只会给你空值。这个项目里所有「选中行 → 源行号」都走 `selectedSourceRow()`，里面就一句 `m_proxy->mapToSource(proxyIndex)`。
- 只改成员变量、不通知代理，界面还是老样子。`beginFilterChange()` 不是仪式，是让视图知道映射要重算。

### ④ `taskrepository.*` —— SQLite 与具名连接

**为什么第四读它**：到这里你才看到数据真正落地。它故意**不是 `QObject`**：不需要信号，也不需要事件循环，上层决定什么时候读、什么时候写。

**重点**：

- 连接名 `"todo_desktop_main"` 是构造参数。`open()` 里 `addDatabase("QSQLITE", name)`，之后全进程靠名字取连接。
- 建表语句在 `ensureSchema()`：`id INTEGER PRIMARY KEY`、`title TEXT NOT NULL`、`done INTEGER`、`priority INTEGER`、`due TEXT`、`notes TEXT NOT NULL DEFAULT ''`。注意 `title` 与 `notes` 都是 `NOT NULL`：备注「可以为空」的意思是**空串 `""` 允许、NULL 不允许**。
- `upsert()` 用 `INSERT OR REPLACE`；`due` 无效时绑 `NULL`（这一列可空，OK），读回时 `QDate::fromString(..., Qt::ISODate)` 再判 `isValid()`。
- `replaceAll()` 是「清空 + 批量插入」，**整段包在一个事务里**，任何一步失败就 `rollback()`。
- `close()` 幂等：先判连接在不在，再在花括号里让 `QSqlDatabase` 副本析构，最后才 `removeDatabase()`。

**坑**：

- `removeDatabase()` 之前只要还有一个 `QSqlDatabase` 或 `QSqlQuery` 副本活着，Qt 就会打印 `connection is still in use`，而且**真的不删**。这个文件里每个局部变量都被花括号关住了，不是风格，是纪律。
- **null QString ≠ 空串：NOT NULL 文本列的第一号坑。** `Task` 的 `title/notes` 默认成员初始化是 `QString()`（null），不是 `QStringLiteral("")`（空串）。当你 `bindValue(col, 一个 null QString)`，QSQLITE 驱动在 `qsql_sqlite.cpp` 的 `exec()` 里对每个值先过 `QSqlResultPrivate::isVariantNull()`——而它判 QString 的标准是「里面那个 QString 自己 isNull()」（`qsqlresult.cpp` 604~605 行），于是 null QString 被绑成 **SQL NULL**，直接撞 `NOT NULL`。
  最阴的地方在于它**只在一条路径上暴露**：`upsert()` 是 `INSERT OR REPLACE`，SQLite 的 REPLACE 冲突解决会把违反 `NOT NULL` 的 NULL 悄悄换成列默认值 `''`，「看起来成功」；`replaceAll()` 是普通 `INSERT`，没有这层兜底，当场报 `NOT NULL constraint failed: tasks.notes`。**同一条数据，一个成功一个炸** = 靠副作用续命。正解是在边界上把 null 归一成空串（`notNull()` 小函数），让两条路径行为一致，不再依赖 REPLACE 的隐性转换。
- 错误不靠异常，靠 `QString* error` 出参。上层（`MainWindow`）负责把它显示到状态栏，底层不弹窗。
- 数据库打不开时，窗口仍然要能显示——`MainWindow` 构造函数里先建界面再开库，失败只影响「能不能落盘」。

### ⑤ `jsoncodec.*` —— 对外交换格式

**为什么第五读它**：数据库是「自己存」，JSON 是「交给别人」。两种读写的容错策略完全相反。

**重点**：

- 根对象：`format`（`"todo_desktop.tasks"`）、`version`（1）、`exportedAt`、`tasks` 数组。前三个是给未来的自己留的识别位。
- `save()` 用 `QSaveFile`：写临时文件 → `commit()` 才原子替换。写到一半断电，旧文件还在，不会留下半截 JSON。
- `fromJson()` 故意**宽松**：不是对象的条目跳过、标题缺失补「未命名任务」、优先级越界回落 `Normal`；只有「整体不是合法 JSON」或「缺 `tasks` 数组」才判失败。
- 对外交换的第一原则：**能读进来的尽量读进来，读不进来的说清哪一条**。

**坑**：

- `QSaveFile` / `QFile` 别加 `QIODevice::Text`。Windows 上它会把 `\n` 翻成 `\r\n`，写出去的字节数和 `write()` 返回值对不上，还破坏 JSON 里字符串的原始内容。
- 导入不是「恢复备份」：本项目导入一律 `task.id = 0` 再 `addTask()`，分配新 id，**只增不覆盖**。

### ⑥ `statsservice.*` —— 把重活挪走

**为什么第六读它**：这是第柒阶段在本项目里唯一的落点，也是「跨线程只走信号」的最小完整例子。

**重点**：

- `request(snapshot)` 按**值**收一份 `QVector<Task>`，lambda 按值捕获。工作线程永远不碰 `m_model` 的内部容器。
- `QtConcurrent::run(QThreadPool::globalInstance(), lambda)` 返回 `QFuture<Result>`，交给 `QFutureWatcher<Result>`；`finished` 在**创建 watcher 的线程**（主线程）发出。
- `Result` 里带 `stats` 和 `elapsedMs`，状态栏能显示「统计 31 ms（后台线程）」。
- 忙时来的请求只保留**最后一次**（`m_pending` + `m_hasPending`）。统计的是「当前状态」，中间态没人要看。
- 工作函数里故意 `QThread::msleep(30)`：任务少时统计太快，加一点工作量才能观察到「它确实不在界面线程上」。

**坑**：

- 别在工作线程里 `emit` 完就以为万事大吉——如果接收者住在主线程，Qt 会自动排队；但**参数必须可拷贝**。这里传的是 `TaskStats` POD 和 `qint64`，安全。
- `StatsService` 被销毁时如果 future 还在跑，`QFutureWatcher` 会断开；别在 `finished` 回调里访问已经析构的 `this`。本项目把它挂在 `MainWindow` 下，生命周期与窗口一致。
- 合并请求不是「丢弃」：算完当前这次，会立刻补算最后一份快照。

### ⑦ `prioritydelegate.*` —— 一格怎么画

**为什么第七读它**：模型、代理、仓储都读完了，终于可以看「像素」了。

**重点**：

- `paint()` 先 `initStyleOption(&opt, index)`，然后 `opt.text.clear()`，再 `style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget)`——**先让样式画底**（选中高亮、焦点框、交替行色都在这一下），再画胶囊。顺序反了，胶囊会被底盖住，或者选中态消失。
- 颜色按 `PriorityRole` 取，不解析文字。Low 灰、Normal 绿、High 红。
- `sizeHint()` 把高度撑到至少 30、宽度至少 76，否则胶囊会被压扁。

**坑**：

- 不要在 `paint()` 里 `new` 大对象或查库。它会被调成千上万次。
- 想让复选框仍然可点，就别把整格都自己画死；本项目只换优先级列的委托，完成列仍交给默认委托。

> 时序细节：[图解 · 委托生命周期](../../visuals/delegate_lifecycle.html)。

### ⑧ `taskdialog.*` —— 只收集输入，不碰模型

**为什么第八读它**：它是界面层里最干净的一块，适合先建立「对话框只负责收集」的观念。

**重点**：

- `QFormLayout` 排标题、优先级、截止日期、备注；`QDialogButtonBox` 给确定/取消。
- 优先级下拉用 `addItem(priorityName(p), int(p))`，`userData` 存枚举值，取的时候 `currentData().toInt()`。
- 截止日期用「复选框 + `QDateEdit`」：没勾就是无效日期，勾了才启用日期框。
- `setTask()` 记住 `id` 与 `done`，`task()` 原样带回——**对话框不改这两项**。
- 标题为空时 `accept()` 被拦截，光标回到标题框。

**坑**：

- 对话框里不要直接写数据库。数据库写失败要显示在哪儿、要不要重试，是主窗口的决定。
- `QDateEdit` 的显示格式设成 `yyyy-MM-dd`，和 JSON 里的 `Qt::ISODate` 对齐，少一层转换就少一类 bug。

### ⑨ `mainwindow.*` —— 接线员

**为什么最后读它**：它是把所有零件接起来的地方。先懂零件，再看接线，才不会把「接线」误当成「业务」。

**重点**：

- 构造顺序：`TaskModel` → `TaskFilterProxy`（`setSourceModel`）→ `StatsService` → `buildUi()` → `buildActions()` → `repository.open()` → `loadFromDatabase()` → `refreshStats()`。先建界面再开库，库坏了窗口也能开。
- `m_view->setModel(m_proxy)`：视图绑的是**代理**，不是模型。
- 表头：标题列 `Stretch`，其余 `ResizeToContents`；隐藏垂直表头；按截止日期升序。
- 搜索框 `textChanged` → `proxy->setQueryText()`；过滤下拉 → `proxy->setOnlyOpen()`。两者都只动代理。
- `selectedSourceRow()` 是全场唯一的「代理 → 源」换算点。编辑、删除、切换完成都先过它。
- 导入：`JsonCodec::load()` → 每条 `id = 0` → `addTask()` → `persistTask()`，状态栏说清「追加模式」。
- 导出：`QFileDialog::getSaveFileName()` 默认落在「文档」下的 `todo-tasks.json`。
- 拖放：`setAcceptDrops(true)` + `dragEnterEvent` 只收 `.json` + `dropEvent` 调同一个 `importFromFile()`。
- `QSettings` 在 `buildUi()` 末尾恢复 `window/geometry` 与 `window/header`，`closeEvent()` 里保存。
- `updateActions()` 根据「有没有选中源行」启用/禁用编辑、删除、切换完成；`countsChanged` 顺手控制「清除已完成」。

**坑**：

- 删除确认框用 `QMessageBox::question(..., Yes | No, No)`，默认按钮是 No——误按回车不会删数据。
- 每一处写库都走 `persistTask()`，它先判 `m_repository.isOpen()`；库没开时改动只留在内存，状态栏会提示。
- 拖放只接受本地文件 URL；`toLocalFile()` 对远程 URL 返回空串，天然过滤。

### ⑩ `selfcheck.cpp` —— 用退出码说话

**为什么读它**：它是这个项目「能交付」的证据。CI 不看日志，只看退出码。

**重点**：

- `--self-check` 在 `QTemporaryDir` 里建库、写 JSON，跑完即删，不产生用户级残留。
- `check(ok, what)` 打印 `[PASS]` / `[FAIL]`，失败累加 `g_failed`；`run()` 最后返回 `g_failed == 0 ? 0 : 1`。
- 覆盖五组：模型（插行/改完成/角色/信号/空标题）、代理（过滤/搜索/`mapToSource`）、SQLite（写读/删除/整批替换/清除/关闭幂等）、JSON（往返/坏输入/缺 `tasks`）、统计（同步数字 + 后台线程 3 秒内返回）。
- 异步统计用 `QEventLoop` + `QTimer::singleShot(3000, &loop, &QEventLoop::quit)` 等结果；超时就让断言失败，绝不无限等。

**坑**：

- 自检里不要弹任何对话框。无头环境没有用户点「确定」。
- 失败信息要具体到「哪一项」，别只返回一个 1。日志是给人排查的，退出码是给机器判的。

### ⑪ `tests/tst_taskmodel.cpp` —— QtTest 再证一遍

**重点**：

- 七个槽：id 递增、`setDone` 发 `dataChanged` + `countsChanged`、空标题拒绝、代理按完成过滤、代理按文本过滤且 `mapToSource` 行号正确、JSON 往返（含引号/换行/空日期）、统计跳过已完成逾期。
- `QSignalSpy` 数信号次数；`QTemporaryDir` 隔离文件；统计用固定日期 `2026-10-28`。
- 末尾 `#include "tst_taskmodel.moc"` + `QTEST_GUILESS_MAIN`：没有窗口也能跑，不需要 `QT_QPA_PLATFORM`。

**坑**：

- `QCOMPARE` 对 `enum class` 不友好，比较时统一 `static_cast<int>`。
- 测试只链 `todo_core`。如果哪天它开始 `#include <QWidget>`，说明分层已经漏了。

### ⑫ `CMakeLists.txt` —— 三个目标

**重点**：

- `add_library(todo_core STATIC ...)` + `target_include_directories(todo_core PUBLIC src)`：应用和测试都能 `#include "core/task.h"`。
- `todo_core` 只链 `Qt6::Core Gui Sql Concurrent`，**不链 Widgets**；`todo_desktop` 才链 `Qt6::Widgets`。
- `qt_add_executable` + `RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin`，产物和顶层示例放一起。
- `add_test(NAME todo_selfcheck COMMAND todo_desktop --self-check)`，`ENVIRONMENT QT_QPA_PLATFORM=offscreen`，`TIMEOUT 60`。
- `tst_taskmodel` 只在 `Qt6::Test` 存在时才建，缺模块不至于让整个 configure 失败。

---

## 四、四条数据流，画在脑子里

### 1. 启动：从 `main()` 到第一屏

```text
 main()
   │ QApplication
   │ setOrganizationName / setApplicationName   ← 必须在任何 QSettings 之前
   │ --self-check ? ──是──▶ selfcheck::run() ──▶ 退出码
   ▼ 否
 AppDataLocation/todo.db
   ▼
 MainWindow 构造
   ├─ TaskModel ──▶ TaskFilterProxy ──▶ QTableView
   ├─ StatsService
   ├─ buildUi() / buildActions()
   ├─ repository.open()          ← 失败只提示，窗口照开
   ├─ loadFromDatabase()
   │     └─ 空库 ? ──是──▶ 放 3 条样例（含一条逾期）
   └─ refreshStats() ──▶ 线程池算统计 ──▶ statsReady ──▶ 状态栏
```

### 2. 编辑：一次改动的完整回路

```text
 双击一行
   ▼
 selectedSourceRow()
   └─ m_proxy->mapToSource(currentIndex())   ← 代理坐标 → 源坐标
   ▼
 TaskDialog::setTask() ──▶ 用户改 ──▶ task()
   ▼
 model->updateTask(row, task)                ← 保留 id 与 done
   ├─ emit dataChanged(...) ──▶ 视图标脏 ──▶ 重绘
   └─ emit countsChanged(...) ──▶ 状态栏
   ▼
 persistTask() ──▶ repository.upsert() ──▶ INSERT OR REPLACE
   ▼
 refreshStats() ──▶ 后台统计
```

### 3. 搜索：只动代理，不动模型

```text
 QLineEdit::textChanged
   ▼
 proxy->setQueryText(text)
   ├─ beginFilterChange()
   ├─ m_query = trimmed
   └─ endFilterChange(Direction::Rows)
        ▼
   代理重算映射 ──▶ 视图行数变化（源模型一行没动）
        ▼
   状态栏：「已显示 N 条（共 M 条）」
```

### 4. 导入导出：一个文件进，一个文件出

```text
 导入（菜单 / 拖放）
   JsonCodec::load(path)
     ├─ 整体不是 JSON → 报错，什么都不写
     ├─ 缺 tasks 数组  → 报错
     └─ 逐条宽松解析
          ▼
   每条 id = 0 → model->addTask() → repository.upsert()
          ▼
   状态栏：「追加模式，原有任务保留」

 导出
   model->tasks() → JsonCodec::save(path)
     ├─ QSaveFile 写临时文件
     ├─ 写失败 → cancelWriting()，旧文件不动
     └─ commit() 原子替换
```

---

## 五、动手练习（按难度排）

### 练习 1 · 加一列「备注摘要」

在 `TaskModel` 里加 `NotesColumn`，`data()` 返回备注前 20 个字符（超出加省略号），表头叫「备注」。

**验收**：不改 `MainWindow` 的列宽设置也能正常显示；`ColumnCount` 变成 5 后，`setDone()` 的 `dataChanged` 右边界仍然覆盖整行；自检里加一条断言。

### 练习 2 · 把「只看未完成」换成三态过滤

下拉加「只看已完成」，代理的 `m_onlyOpen` 换成 `enum class DoneFilter { All, Open, Done }`。

**验收**：三种状态切换后 `rowCount()` 都对；搜索与过滤能叠加；`beginFilterChange` / `endFilterChange` 仍然成对。

### 练习 3 · 导出 CSV

写 `CsvCodec::save()`，字段里出现逗号、引号、换行时按 RFC 4180 转义。

**验收**：用 Excel/LibreOffice 打开不乱列；导出再导入（写个宽松的 CSV 读入）能还原；给含 `"` 和 `\n` 的备注写一条测试。

### 练习 4 · 加撤销栈

用 `QUndoStack` + `QUndoCommand` 把「删除」「切换完成」「清除已完成」做成可撤销。

**验收**：撤销后模型、SQLite、状态栏统计三者一致；重做不重复插 id；栈空时撤销动作禁用。

### 练习 5 · 加到期提醒

`QSystemTrayIcon` + 定时器，每天检查一次逾期任务，弹一条通知。

**验收**：只在有逾期时提醒；关掉窗口后托盘仍在；定时器随程序退出而停，不留后台进程。

### 练习 6 · 换掉仓储，对比取舍

用 `QSqlTableModel` 重写一份仓储，和现在的手写 SQL 版并排放。回答：哪种更适合这个项目？如果任务涨到十万条，哪种更快？如果要做「按标签组合过滤」，哪种更省事？

**验收**：能说出至少三条具体差异，而不是「各有优缺点」。

---

## 六、常见误区 → 正解

| 误区 | 正解 |
|------|------|
| 把 `m_view->currentIndex()` 直接当源模型行号 | 先 `m_proxy->mapToSource()`；代理坐标和源坐标是两套 |
| 在 `TaskModel` 里做搜索过滤 | 模型回答「全部是什么」，代理回答「看哪部分」；过滤放 `TaskFilterProxy` |
| 导入时保留原 id | 一律 `id = 0` 重新分配，追加模式只增不覆盖 |
| 改完 `m_tasks` 不发 `dataChanged` | 视图不轮询；不发信号，屏幕就是旧的 |
| 删除多行时正序删 | 从后往前删，或用 `beginRemoveRows` 一次删一段 |
| `removeDatabase()` 前还留着 `QSqlDatabase` 副本 | 用花括号把副本关进作用域，否则连接删不掉 |
| 先建 `QSettings` 再设组织名/应用名 | 必须在任何 `QSettings` 之前设好，否则写到默认位置 |
| 工作线程直接读 `m_model->tasks()` | 按值传快照进 lambda，结果走信号回来 |
| `QSaveFile` 加 `QIODevice::Text` | Windows 会做 CRLF 转换，破坏字节数与 JSON 内容 |
| 自检里弹 `QMessageBox` | 无头环境没人点按钮；自检只打印 + 退出码 |
| 把 `enum class Priority` 直接 `toInt()` | 先 `static_cast<int>()`；读回统一走 `priorityFromInt()` |
| `bindValue` 把 null `QString()` 塞进 `NOT NULL` 文本列 | null≠空串；`INSERT OR REPLACE` 会悄悄兜默认值、普通 `INSERT` 当场炸。进 NOT NULL 列前把 null 归一成 `""` |
| 在 `paint()` 里查库/建大对象 | `paint()` 是热路径，只做绘制 |
| 统计请求每次都排队 | 忙时只保留最后一次快照，算完补算 |
| 搜索只调 `setFilterFixedString()` | 它只盯一列；要同时搜标题和备注就重写 `filterAcceptsRow()` |

---

## 七、怎么验，验到什么程度

### 1. 编译期门槛

```bash
# 仓库顶层
cmake -G "MinGW Makefiles" -B build -S . -DCMAKE_PREFIX_PATH=D:/Qt/6.12.0/mingw_64
cmake --build build --target todo_desktop tst_taskmodel
```

预期：零 error。`-Wall -Wextra` 下如果有 warning，先修干净再往下走。

### 2. 无界面自检

```bash
./build/bin/todo_desktop --self-check
echo $?
```

实测（本机）：38 项 `[PASS]`，退出码 0。任何一项 `[FAIL]` 都会让退出码非 0。

### 3. 单元测试

```bash
ctest --test-dir build -R "todo_|tst_taskmodel" --output-on-failure
```

实测（本机）：`todo_selfcheck`（0.13 s）与 `tst_taskmodel`（9 passed / 0 failed / 0 skipped）全绿。

### 3.5 单独构建这一层（第拾壹阶段那条「工程要能独立 configure」的验收）

```bash
cd projects/todo_desktop
cmake -G "MinGW Makefiles" -B build -S . -DCMAKE_PREFIX_PATH=D:/Qt/6.12.0/mingw_64
cmake --build build
ctest --test-dir build --output-on-failure     # 2/2 Passed
```

★ 这一步在本机**曾经过不去**：这份 `CMakeLists.txt` 是从顶层 `add_subdirectory` 长出来的，
自己不带 `cmake_minimum_required()` / `project()`，单独 configure 时 CMake 直接报
`CMake Error: No cmake_minimum_required command is present` → `Configuring incomplete`。
现在文件里用 `if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)` 补上了这一段，
上面三条命令实测全通。为什么不能无条件写 `project()`：那会重开作用域，
`CMAKE_BINARY_DIR` 跟着变成本目录，顶层约定的 `build/bin/` 产物位置就漂移了。

### 4. 手工界面清单（需要真实 Qt 运行环境）

| 操作 | 预期 |
|------|------|
| 首次启动 | 出现 3 条样例，其中一条红色（逾期） |
| 搜索「线程」 | 只剩 1 条，状态栏显示「已显示 1 条（共 3 条）」 |
| 切「只看未完成」 | 已完成的行消失，源模型行数不变 |
| 双击一行改标题 | 表格立刻更新；重启后仍是新标题 |
| 勾选完成 | 整行变灰；状态栏完成数 +1 |
| 删除一行 | 弹确认框，默认按钮是「否」；确认后行消失，重启不回来 |
| 导出 JSON，再拖回窗口 | 任务条数翻倍（追加），原有任务仍在 |
| 拖一个非 `.json` 文件 | 窗口不接受 |
| 改窗口大小后关闭再开 | 窗口大小与表头宽度恢复 |
| 关掉数据库文件权限再启动 | 窗口仍能打开，状态栏提示数据库打不开 |

> 这张表**程序本身在本机跑得起来**（`--self-check` 与 `ctest` 全绿就是证据），但它一条都验不了：`offscreen` 平台不往显示器画任何东西，行高、胶囊颜色、状态栏文字、确认框的默认按钮焦点，全都是**像素与焦点顺序**层面的事，只有真显示器上的人眼说得准。请在自己的机器上逐条过。界面细节（行高、胶囊颜色、状态栏文字）随平台主题有差异，不算缺陷。

### 5. 验收标准：能当场讲清这十个问题

1. 为什么 `todo_core` 不链 `Qt6::Widgets`？
2. `TaskModel::data()` 一共认几种 role？委托靠哪个 role 画颜色？
3. 视图的 `currentIndex()` 属于谁？不映射会得到什么？
4. 搜索时源模型为什么一行都没变？
5. `beginFilterChange()` / `endFilterChange()` 不写会怎样？
6. `removeDatabase()` 之前为什么要让副本析构？
7. 导入为什么把 `id` 清零？覆盖导入的风险是什么？
8. `QSaveFile` 比 `QFile` 多做了什么？
9. 后台统计为什么按值传快照？忙时为什么只留最后一次？
10. `--self-check` 和 `tst_taskmodel` 各自证明什么？为什么两个都要？

十题都能不看代码讲明白，这个项目才算你的。

---

## 八、老师傅的话

**第一句：分层不是为了好看，是为了「能测」。**
这个项目最值钱的地方不是界面，是 `todo_core` 不依赖 `QWidget`。正因为如此，38 项自检和 7 个 QtTest 用例才能在无头环境里跑。你以后接的每一个项目，先问一句「业务逻辑能不能脱离界面编译」，答案决定它好不好维护。

**第二句：坐标系的事，宁可多写一行映射。**
代理一上，就有两套坐标。这个项目把换算收在 `selectedSourceRow()` 一个函数里，别的地方一律不直接碰。真实项目里代理会一层套一层，写规范了是幂等的，省了就是偶现 bug。

**第三句：能交给别人的东西，才叫交付。**
源码能编、自检能过、测试能跑、`windeployqt` 能搬走——四件事齐了，才敢说「做完了」。本机前四件都齐了（编译链接通过、38 项自检全绿、ctest 2/2、`windeployqt` 实测能搬），**唯独「看起来对不对」这一件没有**：无头环境跑不出像素，所以上面那张人眼清单依然只能交给你。写讲义时吃过一次亏：「本机跑不了 Qt」这句话在文件里躺了整整一个阶段，其实只是一时缺 DLL，环境一好，那段免责和它盖住的假绿一起露了馅。诚实标注不是给自己留台阶，是给下一个读的人标出**哪一格还没验过**。

**第四句：改任何一处，先问三个问题。**
数据归谁？信号谁发？失败怎么告诉用户？这三个问题答不上来，就先别写代码。它们分别对应分层、通知链、错误处理——也是这个项目里每一处设计决定背后的同一把尺子。

---

> 下一站：把这个项目搬到你自己机器上，按第七节逐条验；验完再挑第五节里的一道练习动手改。改坏了不要紧，`git diff` 和自检会告诉你哪儿错了。
>
> 回看路线：[学习路线图](../../学习路线图.html) · [第拾阶段 · 工程质量](../../quality/tutorial.md) · [第拾壹阶段 · 构建与部署](../../build_deploy/tutorial.md) · [部署图解](../../visuals/deployment_tree.html)。
