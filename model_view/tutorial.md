# 第陆阶段 · 模型/视图与委托（Qt 从入门到资深）

> 老师傅带你把「数据」和「画出来的那一格」彻底分开 · 8 讲，四道坎，一本性能账

## 一、模块简介

徒弟，走到这一关，前面那种「一个控件一份状态」的日子就到头了。这里是**第陆阶段「模型/视图与委托」**，Qt 里第一道真正的工程分水岭。

之所以说是分水岭，因为前面学 `QPushButton`、`QLineEdit`，你拼的是「会不会摆」；从这里开始拼的是**你想清楚数据住在哪儿没有**。Qt 的列表、表格、树走的是 **Model / View / Delegate / Selection / Proxy** 五角色分工：

- **Model（模型）**：唯一持有数据的地方。它不画任何东西，只回答四类问题——有几行几列（`rowCount` / `columnCount`）、格子坐标怎么造（`index`）、这一格是什么（`data(index, role)`），外加「接住改动」（`setData`）。
- **View（视图）**：只负责「要画哪几格、把坐标交给模型去问、把自己那套状态（当前项、滚动位置）维护好」。格子**不是子控件**，视图里也没有 `item()` / `addItem()`。
- **Delegate（委托）**：一格的「怎么画」和「用什么编辑器编辑」都归它，`paint` 与 `createEditor`/`setEditorData`/`setModelData` 是它的两条线。
- **Selection（选择模型，`QItemSelectionModel`）**：「选中了哪些」是一份**住在模型坐标系里的数据**，不属于视图。
- **Proxy（代理模型，`QSortFilterProxyModel`）**：不改你的数据，只在源模型前面再加一层**坐标系**，决定视图看到几行、第 0 行是哪一条。

这套东西是 MVC 在 GUI 里最落地的一次实现，本模块一共 **8 讲**，从五角色总览一路讲到「10 万行为什么不卡」。每一讲都是完整可编译的示例，而且都遵守全课那条硬规矩：**无人值守能自己跑完、自己吐证据**（`QTimer::singleShot` 排动作 → 计数器/计时取证 → `qInfo()` 打印 → `QCoreApplication::quit()`）。

> 环境事实：Qt 6.12.0、Windows、MinGW-w64（MSYS2 ucrt64，gcc 15.2/16.2）、CMake ≥ 3.21 + Ninja、C++17。构建走 `find_package(Qt6 COMPONENTS Widgets)` + `Qt6::Widgets` + `CMAKE_AUTOMOC=ON`，8 个目标名（`model_view_01_architecture` … `model_view_08_view_tuning`）已在顶层 `CMakeLists.txt` 的「第陆阶段 · 模型/视图与委托（model_view/）」一节登记，也写在每个文件顶部注释块里。
>
> 诚实标注：本机当前**无法编译链接** Qt 程序（运行时库正在升级）。本模块示例**未链接、未运行**，但每个 .cpp 都过了 `g++ -fsyntax-only -std=c++17 -Wall -Wextra` 的编译期核对（零 error、零 warning），每个类、函数、重载、枚举值都对着本机 `C:\msys64\ucrt64\include\qt6` 真实头文件逐字核对；文中所有「预期」后面是推理，不是实测数字。

## 二、学习目标

学完本模块，你要能做到：

1. 说清「为什么格子里的数据不存在的视图里」，并能用**证据**（视图子对象数与行数无关、`data()` 被调次数只跟可见区成比例）证明它，而不是背它
2. 手写一个 `QAbstractTableModel`：五个纯虚函数的签名一个 `const` 都不差，`createIndex` 的 `internalPointer` 存什么、它的**生命周期**风险在哪，都讲得明白
3. 把**编辑回路**完整复述一遍：`delegate.setModelData` → `model.setData` → `emit dataChanged` → 视图标脏 → 重绘再问 `data()`；并知道 `flags()` 和视图 `editTriggers` 是**两道门**，缺一不可
4. 用委托（`QAbstractItemDelegate`）改一格的画法与编辑器：`paint` / `sizeHint` / `createEditor` / `setEditorData` / `setModelData` / `updateEditorGeometry` / `editorEvent` 各自在什么时机被调
5. 套上 `QSortFilterProxyModel` 之后**不换坐标系就敢动手**：知道视图给的 index 属于代理，取数据前先 `mapToSource`；分清静态匹配与重写 `filterAcceptsRow`/`filterAcceptsColumn`/`lessThan` 两条路；外部判据变了知道要 `invalidate()` 或 `beginFilterChange()`/`endFilterChange()`
6. 讲明白选择状态为什么是模型侧的：`QItemSelection` 是**矩形范围列表**、范围两角是 `QPersistentModelIndex`、`SelectionFlags` 有哪些名字、而「这次是 Ctrl 加选还是点了一下」是视图侧 `selectionCommand()` 翻译的，不在 `selectionChanged` 参数里
7. 对便利类做一次**算账**：什么时候 `QStandardItemModel` 三十行最划算、什么时候必须自己写模型，`take*` 系列的所有权归谁
8. 拿到一张视图就知道它为什么会卡：`resizeColumnsToContents()` 的全表遍历、`ResizeToContents` 动态模式的连锁重算、表头 `headerData` 与每格 `sizeHint` 这两笔隐藏开销，以及对应的开关（`verticalHeader()->hide()`、`ScrollMode`、`setUniformRowHeights`、`updateThreshold`）

## 三、文件列表与学习重点

一共 8 个示例，按顺序来，别跳：

```text
  01_architecture.cpp          ← 五角色总览（第一道坎：观念）
    重点：Qt 为什么拒绝「把数据塞进控件」——格子是画出来的不是存出来的
          （视图子对象数量与行数无关，QTableView 头文件里没有 item()/addItem()）；
          QModelIndex 只是「带内部指针的坐标」：不持有数据、会失效、不能跨模型混用；
          data(index, role) 是拉取式（pull-based）接口，套计数器把「一次重绘问了几次」打出来；
          另外三个角色的入口认全（Delegate / SelectionModel / Proxy）；
          最后用 QStandardItemModel + QTableView 跑通最小闭环

  02_custom_table_model.cpp    ← 手写 QAbstractTableModel（第二道坎：模型）
    重点：必须实现的成员逐个核签名——index / parent / rowCount / columnCount / data
          （QAbstractTableModel 已给 index()/parent() 默认实现，但默认实现不含 internalPointer）；
          createIndex(row, column, pointer) 存什么、表格的 parent() 为什么返回 QModelIndex()；
          加分项 headerData / roleNames；角色枚举真名真值（Qt::DisplayRole=0 … Qt::UserRole=0x0100，
          Qt6 已无 Qt::SortRole）；std::vector 扩容换内存 → 旧 index 里的指针当场悬垂（只比地址不解引用）；
          5000 行 × 4 列 + data() 计数器 + QSet 记「哪些行真被问过」

  03_editing_and_flags.cpp     ← 可编辑模型：flags / setData / dataChanged
    重点：回路视图提交 → setModelData → setData → 改内部存储 → emit dataChanged → 视图标脏 → 重画；
          两个模型实例 + 两个视图对照「改完不发信号」：模型自测都是对的，
          但漏发那个的 dataChanged 槽计数是 0，强制重绘才露馅（「一改就错、一滚就对」的病根）；
          flags()：ItemIsEditable / ItemIsUserCheckable / ItemIsEnabled / ItemIsSelectable 的真名真值，
          以及「模型 flags 与视图 editTriggers 是两道门」；
          setData 的正确姿势（验 index → 判 role → 校验 → 比有没有真变 → 写 + emit）；
          beginInsertRows/endInsertRows 的顺序；reset 与结构性信号的取舍（选择集与滚动位置会丢）

  04_delegate.cpp              ← 自定义委托（第三道坎：画与编辑）
    重点：Delegate 这一层「一格怎么画 + 用什么编辑器」的完整生命周期：
          paint(QPainter*, const QStyleOptionViewItem&, const QModelIndex&) 画；
          sizeHint 定尺寸；createEditor 造控件、setEditorData 把模型的值灌进编辑器、
          setModelData 把编辑器的值写回模型（这两个方向相反，是最容易搞反的一对）、
          updateEditorGeometry 摆位置、editorEvent 处理点击、destroyEditor 收尾；
          与第 3 讲那条回路接上（提交那一跳就发生在 setModelData 里）
          ⚠ 本文件由同事撰写；截至本指南生成时 04_delegate.cpp 尚未落到本目录，
             以上描述按本机 QtWidgets/qabstractitemdelegate.h 第 41/45/53/55/57/61/66 行的
             公开虚函数契约给出，文件到位后请据实际内容复核

  05_proxy_model.cpp           ← QSortFilterProxyModel（第四道坎：两套坐标系）
    重点：源模型 1 万行 × 4 列，代理只露过滤后的行；rowCount 两层各打一次；
          mapToSource / mapFromSource 与 mapSelectionToSource 做成硬证据——
          同一个格子映射前后问出来的值不同，不映射直接问源拿到的是空 QVariant；
          两条过滤路：静态匹配（setFilterFixedString / setFilterWildcard /
          setFilterRegularExpression + filterKeyColumn + filterRole；注意 Qt 里没有
          filterFixedColumn 这个函数）vs 重写 filterAcceptsRow / filterAcceptsColumn / lessThan；
          外部判据变了必须显式失效：invalidate()（public slot）或子类里
          beginFilterChange()/endFilterChange()（老 API invalidateFilter 等在本机已 protected 且标废弃）；
          sort 与视图 setSortingEnabled 的分工、dynamicSortFilter 默认 false 的坑

  06_selection_model.cpp       ← QItemSelectionModel：选择不归视图管
    重点：三个视图共用一个选择模型 → 会同时高亮；换/重建选择模型 → 选择就没了
          （selectedIndexes().size() 两种情况都打出来）；
          QItemSelection 是 QList<QItemSelectionRange>（矩形范围列表，不是逐格索引），
          范围的 topLeft/bottomRight 本身就是 QPersistentModelIndex，所以扛得住增删行；
          逐条打印 SelectionFlags 真值（Qt 6.12 的 SelectionFlag 里没有 CommandMask）；
          selectionChanged 参数不带命令，flags 来自视图侧 protected virtual selectionCommand()；
          裸 QModelIndex vs QPersistentModelIndex 插一行后的两种下场 + 登记的代价；
          clear / clearSelection / clearCurrentIndex 与 NoUpdate、SelectCurrent 的区别

  07_item_models.cpp           ← QStandardItemModel / QStandardItem：便利类的账
    重点：卖点——不写五个虚函数、也不用自己 emit dataChanged（私有槽 _q_emitItemChanged 代劳，
          用 itemChanged / dataChanged 计数证明）；
          代价——每格是一个独立堆对象（+ 私有 d-pointer + 带 vtable）；
          ⚠ 纠正流行说法：Qt6 里 QStandardItem 已不继承 QObject（本机头文件第 24 行），
             性能理由不是「因为它是 QObject」；
          树和表共用同一套 item（appendRow 加子节点，Qt 里没有 appendChild）；
          takeRow / takeItem 的所有权交回给你，不 delete 就是泄漏；
          findItems ≠ 过滤（过滤请用 ../model_view/05）；
          10 万行 × 4 列两份模型并排（构建耗时 / 读一格 / 重绘一屏）+ 三档判断；
          顺带认一眼 QStringListModel（注意 QAbstractListModel::columnCount 是 private）

  08_view_tuning.cpp           ← 视图侧调优与行为（性能账收尾）
    重点：10 万行不卡的真正原因是拉取式 + 只问可见区（打印被问到的行区间）；
          两笔隐藏开销是表头 headerData（横竖各一遍）与每格 sizeHint，
          对应开关 verticalHeader()->hide()、setSectionResizeMode(...)；
          resizeColumnsToContents() 会遍历所有行（把 index()/data() 次数数出来），
          比 resizeColumnToContents(int) 贵一整圈；
          ResizeMode 四值与两个重载；ResizeToContents 动态模式下改一格会引发全表重算；
          QAbstractItemView::ScrollMode（ScrollPerItem / ScrollPerPixel；这个枚举属于
          QAbstractItemView，不属于 QAbstractScrollArea）、updateThreshold、
          QTreeView::setUniformRowHeights（只在 QTreeView 上，QTableView 没有）；
          EditTrigger 七个值（AnyKeyPressed 的危险）、wordWrap / iconSize / showGrid /
          alternatingRowColors；setModel 之后 view->reset() 到底清了什么；
          最后「默认 vs 调优」总账：省下来的全是表头与尺寸遍历，画可见格子那部分一样
```

> 为什么第 4 讲单列一坎：模型管「是什么」、视图管「要哪格」，而**「这一格长什么样、用什么控件改」全在委托手里**。它的接口不多，但时机很碎——画的时候调 `paint`/`sizeHint`，进入编辑才 `createEditor`，灌值 `setEditorData`、回写 `setModelData` 是两个相反方向。第 3 讲那条回路里「提交」那一跳，真身就是 `setModelData`。

## 四、学习顺序建议

师傅给你划这条主线，和四道坎。

### 主线：为什么是这个顺序

- **第 1 → 第 2**：先用现成的 `QStandardItemModel` 跑通闭环、把「数据不在控件里」这条钉死；然后立刻把便利类扔掉——因为真实项目里数据本来就是你自己 `std::vector` 里的，只有亲手实现一遍五个虚函数，「拉取式」才从口号变成手感。
- **第 2 → 第 3**：能「答话」了，还得能「接活」。第 3 讲把 `flags` / `setData` / `dataChanged` 补上，顺便把整条**回路**画完整——这是后面委托、代理、选择三讲共同的地基。
- **第 3 → 第 4**：回路里「提交」那一跳发生在委托手里。第 4 讲把画与编辑这两条线（`paint`/`sizeHint` 与 `createEditor`→`setEditorData`→`setModelData`）单独讲透。
- **第 4 → 第 5**：单一直线走通了，就该加中间层了。代理模型是「同一份数据前的第二套坐标系」，它是本模块**事故率最高**的一层，也是第 6 讲「选择映射」的前提。
- **第 5 → 第 6**：数据能看能筛了，还要能选。选择模型单独成立一层，是因为它证明了一件很反直觉的事——**选择状态住在模型侧**，所以多视图会同时高亮、范围扛得住增删行。
- **第 6 → 第 7**：自己写过模型、也用过代理和选择，这才有了评价便利类的资格。第 7 讲把「三十行出一屏」和「40 万个堆对象」这两笔一起算给你看。
- **第 7 → 第 8**：模型那一侧能扛了，界面却还可能一滚就顿。第 8 讲收尾在视图侧，把每一笔开销**数出来**，并给你一张「默认 vs 调优」的总账。

### 硬坎：四道，一道都别绕

1. **第 1 步 · 观念转变坎**：接受「控件是画出来的，不是存出来的」，接受 `QModelIndex` 只是带内部指针的坐标。这条不转，后面全是背 API，一遇 `data()` 性能问题就没办法。
2. **第 2 步 · 手写模型坎**：五个虚函数签名（`const`、参数表、返回类型）差一个字就是「你没覆写到」；`internalPointer` 存裸指针，容器一扩容旧索引就悬垂。小练习会让你亲手把两种错都撞一遍。
3. **第 4 步 · 委托坎**：`setEditorData`（模型 → 编辑器）与 `setModelData`（编辑器 → 模型）方向相反，`createEditor` 只在进入编辑那一次调。搞混的现场症状是「一改就丢值」「编辑器出来是空的」。
4. **第 5 步 · 索引映射坎**：套了代理，视图手里的 index 就属于**代理**。拿它直接问源模型不会报错，只会给你空值——「偶现空表格」「过滤后删错行」这类线上事故，病根几乎都在这。

### 学习方法

每个文件都要：先读顶部注释块（「本讲解决什么问题」+「运行前须知」）→ 按注释里的目标名构建 → `-platform offscreen` 跑一遍看自动吐出来的证据 → 改代码做末尾三条小练习 → 改出问题了回头自己修。**光看不练，等于白看**；本模块尤其如此，因为它的每一条结论都是「被数出来的」。

> 🎬 想看「视图 → 代理 → 源模型」三层索引怎么映射、`data(index, role)` 什么时候被反复调用：[图解 · 模型/视图数据流](../visuals/model_view.html)。
> 🎬 想看一格从「被画」到「进入编辑再写回模型」的完整时序：[图解 · 委托生命周期](../visuals/delegate_lifecycle.html)。

## 五、核心知识点梳理

本模块所有细节都挂在两根柱子上：**索引流向**和**拉取式取数**。把这两张图画进脑子，八讲的内容就都有了位置。

### 1. 索引流向：视图 → 代理 → 源模型（每往下一层，坐标都要换算）

```text
  ┌──────────────────────── QTableView / QTreeView（View）────────────────────────┐
  │ 只知道自己那一套坐标：可见区域、当前项、鼠标点到的格子                          │
  │ 手里的 QModelIndex 属于「它绑定的那个模型」                                     │
  └───────────────┬───────────────────────────────────────────────────────────────┘
                  │ index(row, column) 属于代理
                  ▼
  ┌──────── QSortFilterProxyModel（Proxy，住在 QtCore）────────┐
  │ 第二套坐标系：只暴露「过滤后 + 排序后」的行/列序             │
  │   proxyIndex ──mapToSource()──▶ sourceIndex                │
  │   sourceIndex ──mapFromSource()─▶ proxyIndex               │
  │   QItemSelection ──mapSelectionToSource()──▶ 源坐标系的选择  │
  │ 重写点：filterAcceptsRow / filterAcceptsColumn / lessThan    │
  │ 外部判据变了：invalidate() 或 beginFilterChange()/endFilterChange()
  └───────────────┬────────────────────────────────────────────┘
                  │ 代理把源坐标系的 index 交下去
                  ▼
  ┌──────── QAbstractItemModel 子类（Model，数据的唯一住处）────────┐
  │ rowCount / columnCount / index / parent / data / setData        │
  │ index 里揣着 createIndex(..., internalPointer) 的裸指针 ← 生命周期风险
  └───────────────┬────────────────────────────────────────────┘
                  │ 一格的「长什么样」交给委托
                  ▼
  ┌──────── QAbstractItemDelegate（Delegate）────────┐
  │ paint() / sizeHint()                             │
  │ createEditor() → setEditorData() →（用户操作）→  │
  │ setModelData() → model->setData() → emit dataChanged()
  │ updateEditorGeometry() / editorEvent() / destroyEditor()
  └──────────────────────────────────────────────────┘

  另有一条独立的「状态轴」：
  QItemSelectionModel（选择）+ 当前索引（current）——它按【模型坐标系】存状态，
  所以同一份模型上的多个视图会一起高亮；换掉选择模型，选择就没了。
```

三条推论，句句要在代码里兑现：

- **视图的 index 属于「它 setModel 的那个模型」**。绑的是代理，index 就是代理坐标；要拿数据回源模型，必须先 `mapToSource`。不映射直接问源，源模型的守卫（`idx.model() != this`）会返回**空 `QVariant`**——不崩、不报错，只给空值，这是最难查的一类 bug。
- **行列数也是两层的**：`sourceModel->rowCount()` 与 `proxy->rowCount()` 是两个不同的数；`proxy->index(0, 0)` 对应源里第几行，完全由过滤与排序决定。
- **选择同样要换算**：代理上选中的格子映射回源坐标系才是「真正要删的那几行」。

> 🎬 这三层关系的分步动画在 [图解 · 模型/视图数据流](../visuals/model_view.html)。

### 2. `data(index, role)` 是拉取式（pull-based）：视图不问「变了吗」，只问「现在是什么」

```cpp
QVariant MyModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();          // ① 先验有效性
    if (index.model() != this) return QVariant();     // ② 防跨模型混用（否则代理 index 直接掉进来）
    if (role == Qt::DisplayRole || role == Qt::EditRole)
        return m_rows[index.row()].title;             // ③ 只是从自己的容器里取一个值
    if (role == Qt::BackgroundRole && /* 某条件 */)
        return QColor(255, 240, 200);
    return QVariant();                                // ④ 不关心的角色一律给无效值
}
```

关于这个函数，本模块用计数器钉死了四件事：

1. **它会被反复调用**：每次重绘、每个可见格子、每个视图关心的角色，都会调一次。一次重绘的调用量级是「可见格子数 × 角色数」，**不是**总格子数——这正是 10 万行不卡的原因（第 1、2、8 讲各数了一遍）。
2. **它只被问可见区**：视图只对它当前要画的那几格调 `index()`/`data()`。把「被问到的行」用 `QSet<int>` 记下来，打印出的是一段连续区间，不是 `0..99999`。
3. **它是 `const`，而且是热路径**：里面**不许**做重活、不许分配大块内存、不许查库、不许算正则。要做「按角色变化的显示」，就把成本挪到数据变更那一刻预先算好存起来。
4. **改完数据必须让视图知道**：拉取式意味着「视图不会自己来问你有没有变」，你必须 `emit dataChanged(topLeft, bottomRight, roles)`。忘了发，视图手里还是旧像素，直到某次别的重绘才「顺带」画对——就是「一改就错、一滚就对」。

### 3. 回路（写回）与结构性信号（增删）——两条不同的通知链

```text
 用户编辑 → delegate.setEditorData(灌初值) → 用户改 → delegate.setModelData
        → model.setData(index, value, Qt::EditRole) → 改内部存储
        → emit dataChanged(topLeft, bottomRight, {Qt::EditRole, Qt::DisplayRole})
        → view 的 dataChanged 槽 → 标脏区 → 下次重绘再调 data()
```

结构性变更走的是 **begin/end 成对**这一条：

```cpp
beginInsertRows(parent, first, last);   // ① 先说「接下来要插入」
m_rows.insert(...);                     // ② 再真的改容器
endInsertRows();                        // ③ 收尾并发 rowsInserted
```

顺序反了（先改容器再 begin），视图拿着**旧行号**去问新数据，越界是崩溃的经典配方。同理 `removeRows`、`moveRows`。整份数据换新才用 `beginResetModel()` / `endResetModel()`——注意 reset 会让视图丢掉**选择集与滚动位置**（第 3 讲用滚动条的 `value()` 打出来过）。

### 4. 三个数据结构的分工（本模块最容易背错）

| 类型 | 英文原名 | 一句话职责 | 关键事实 |
|---|---|---|---|
| `QModelIndex` | model index | 一次性坐标快照 | 不持数据；结构性变更后可能失效或漂移；不可跨模型混用 |
| `QPersistentModelIndex` | persistent model index | 登记在册的活引用 | 增删行会自己挪到正确行；代价是构造/析构要登记、模型变更要更新全部登记项 |
| `QItemSelection` | item selection | `QList<QItemSelectionRange>` | 存的是**矩形范围**（不是逐格索引）；范围的 `topLeft`/`bottomRight` 返回 `const QPersistentModelIndex &` |

> 术语中英对照记一遍：model / view / delegate（item delegate）/ selection model / proxy model / role / index / persistent index / pull-based / signal-slot。

## 六、常见坑与注意事项

### 1. 拿代理的索引去问源模型（最常见的线上事故）

`QSortFilterProxyModel` 之后，视图给的 `QModelIndex` **属于代理**。直接 `sourceModel->data(proxyIndex)`，源模型的守卫会认不出它，于是你拿到空 `QVariant`、`rowCount()` 为 0、甚至 `index.data().toString()` 一片空白；但它**不报错、不崩溃**。症状就是「偶现空表格」「过滤之后删错行」「取到空值以为是数据问题」。
**规矩**：跨层必须先换算——`const QModelIndex src = proxy->mapToSource(proxyIndex);` 再问源；反向（比如外部只有一个源行号）用 `mapFromSource()`。选择集用 `mapSelectionToSource()` / `mapSelectionFromSource()`。第 5 讲把这条做成了硬证据：同一个格子，映射前后问出来的值不一样。

### 2. 改完数据不发 `dataChanged`

模型是拉取式的，视图不轮询「你变了没」。你自己绕过 `setData` 直接改容器（或者 `setData` 里改完忘记 `emit`），数据其实变了、`data()` 也能给新值，**但屏幕上是旧的**，直到下一次别的原因触发重绘才「自己对上了」——这就是最耗人的「一改就错、一滚就对」。第 3 讲用两个模型实例 + 两个视图对照，漏发那个的 `dataChanged` 槽被调次数是 **0**。
**规矩**：改格子的唯一入口是 `setData`（或它等价的内部函数），改完必须 `emit dataChanged(topLeft, bottomRight, roles)`；只改显示角色时把 `roles` 带上，省得视图做无谓的整套刷新。

### 3. 用 `QStandardItemModel` 撑大数据量

便利类很好，但它每个格子是一个**独立的 `QStandardItem` 堆对象**（还牵一个私有 d-pointer、带 vtable），外加每格 `QString` 的堆缓冲。10 万行 × 4 列就是 **40 万个对象**：构建慢、常驻内存大、读一格也贵，`resizeColumnsToContents()` 这种全表遍历更是雪上加霜。第 7 讲把两份模型（便利类 vs 自定义）并排计时给你看。
顺带纠正一个流行说法：网上（甚至不少老讲义）说「每个 item 都是一个 QObject」。**Qt6 上不成立**——本机 `QtGui/qstandarditemmodel.h` 第 24 行是 `class Q_GUI_EXPORT QStandardItem`，没有基类（Qt5 时代它确实继承 `QObject`）。结论方向不变，但理由要用对的：**一次堆分配 + d-pointer + 虚析构**，不是「因为它是 QObject」。
**规矩**：三档判断——数据本来就是自己的容器/数据库缓存 → 自己写模型；临时的小数据、要树表混排 → 便利类；只有一维字符串 → `QStringListModel`。

### 4. 把 `resizeColumnsToContents()` 放进循环或高频路径里

这一句网上人人会说「它会遍历所有行」，但很少有人真的数给你看：第 8 讲计数器清零 → 调一次 → 打印 `index()`/`data()` 被问的次数，量级是**行数 × 列数**。放循环里、放 `dataChanged` 槽里、放定时器里，等于每来一次变化就把整表重新量一遍，10 万行直接白屏。单列只需要 `resizeColumnToContents(int)`，代价小一整圈。
更狠的变种：把 `QHeaderView::setSectionResizeMode(...)` 设成 **`ResizeToContents`（动态模式）**——那之后**每次数据变化都会重来一遍**。第 8 讲用「改一格 → 看计数」把这条演出来了；对照 `Interactive` / `Fixed` / `Stretch` 三种模式，动态那一栏的计数会炸。
**规矩**：初始布局用一次 `resizeColumnsToContents()`（数据量大就只量 `headerData` 那几列或干脆给固定宽），之后一律改 `Interactive` 或 `Stretch`。

### 5. 在 `data()` 里做重活或分配内存

`data()` 是 `const` 的热路径，一次重绘会被调几十上百次，滚动时每秒上百次。在里面 `new`、构造大 `QImage`、跑正则、查数据库、拼 `QString` 链——都会被放大成「一滚就顿」。第 8 讲的对照账显示：调优前后差别**全在表头与尺寸遍历这些额外调用上**，而画可见格子那部分开销一样；也就是说，你在 `data()` 里加的成本是**逐格逐次**收的，谁也省不掉。
**规矩**：`data()` 只做「从已有存储里取一个值 + 必要的小包装」。图标预加载（`QIcon`/`QPixmap` 造好在构造阶段放进容器，注意 `QPixmap` 必须在 `QApplication` 之后创建），格式化好的显示串预先算或懒算后缓存，颜色条件用查表。

### 6. 把 `QModelIndex` 存起来长期使用

`QModelIndex` 是一次性快照：插行/删行之后它的 `row()` 还是老数字（**数据已经换人了**），容器换内存之后 `internalPointer()` 甚至悬垂。第 6 讲给了现场对照：插一行后裸 index 的 `row()` 不变，`QPersistentModelIndex` 的 `row()` 自己 +1。而范围类 `QItemSelectionRange` 的两个角本来就是 `QPersistentModelIndex`——选择天生扛得住增删，这不是巧合，是设计。
**规矩**：跨一次事件循环、跨结构性变更还要引用某个格子，就用 `QPersistentModelIndex` 并在用前判 `isValid()`；但要知道它是**登记在册**的开销（第 6 讲把活登记下 `insertRows` 的计时也打了出来），别当数组用。

### 7. 忘记「模型 `flags()` 与视图 `editTriggers` 是两道门」

`setData` 写得再对，只要 `flags()` 没给 `Qt::ItemIsEditable`，或者视图 `editTriggers` 里没有匹配的触发方式，编辑器根本不会出现。反过来，视图设了 `QAbstractItemView::AnyKeyPressed` 而模型允许编辑，用户敲任何一个键都会把当前格拉进编辑态——输入体验像坏掉。两道门，缺一不可，也别多开。

### 8. `begin/end` 忘了、写反了，或者拿 reset 当万能

`beginInsertRows` → 改容器 → `endInsertRows` 的顺序反了，视图会拿旧行号越界问你（崩溃的经典配方）。而 `beginResetModel()` 虽然一把梭，但它会让视图丢掉**选择集与滚动位置**（第 3 讲把 `verticalScrollBar()->value()` 在 reset 前后打出来对比）。
**另外记一条**：`ScrollMode` 这个枚举**属于 `QAbstractItemView`**（本机 `qabstractitemview.h` 第 92~95 行），不属于 `QAbstractScrollArea`——`qabstractscrollarea.h` 里 grep `ScrollMode` 是 0 个结果。写错命名空间是编不过的。同理 `QTreeView::setUniformRowHeights` 只在 `QTreeView` 上，`QTableView` 没这个函数。

### 9. 外部判据变了却没让代理失效 / `dynamicSortFilter` 没开

代理会跟源模型的结构性信号自己重算映射，但你**自己的** `m_keyword`、集合过滤条件变了它无从得知，结果就是「界面还是老样子」。正规做法是 `invalidate()`（public slot）或在子类里 `beginFilterChange()` / `endFilterChange()`（protected）。注意老的 `invalidateFilter()` / `invalidateRowsFilter()` / `invalidateColumnsFilter()` 在本机 Qt 6.12 头文件里同时是 **protected** 且被标了废弃（`QT_DEPRECATED_VERSION_X_6_13("Use begin/endFilterChange() instead")`），`-Wall` 下会一串 warning。
另一半坑是 `dynamicSortFilter`：默认 `false`，意思是「源数据改了不自动重排/重筛」。很多「数据一多就不筛了 / 排序不跟手」的 bug 就卡在这一个开关上。

### 10. 在视图上 `setModel` 之后以为选择、编辑器、滚动位置都还在

`setModel()` 会换掉（重建）视图的选择模型，视图自建的那一个你手里原来的指针就不再生效了；进入编辑状态时换模型还会牵扯编辑器。第 6 讲把「三视图共用一个选择模型」与「重建选择模型后 `selectedIndexes().size()` 归零」两种情况都打印过。`view->reset()` 则会把编辑器、滚动位置、选择一起清掉——第 8 讲把它前后 `current`/滚动值的变化打了出来。
**规矩**：要自己掌握选择模型就显式持有它并按 Qt 文档的规则设置（先 `setModel` 再 `setSelectionModel`）；否则就承认「视图自管」，别拿旧的指针乱使。

## 七、老师傅的经验之谈

### 1. 心智模型先于 API：数据只住一处，别的全是问出来的

别急着记 `QAbstractItemModel` 那二十几个函数。先把这句话刻进脑子：**数据只住在模型里；视图和委托都是「现场问、现场画」的**。这个模型一通，「为什么格子不是控件」「为什么要发 dataChanged」「为什么 10 万行不卡」三个问题会同时消解。反过来，凡是你想把值「存进视图」的瞬间（「我拿 `QTableWidgetItem` 记一下」这种），先停一下——那正是本模块要治的毛病。

### 2. 一切结论要能被数出来

本模块每个示例都装了计数器（`data()`、`index()`、`headerData`、`sizeHint`）和 `QElapsedTimer`，还把「被问到的行区间」用 `QSet<int>` 打出来。为什么这么较真？因为**「视图只问可见区」「resizeColumnsToContents 遍历全表」这种说法，只有被数出来才算你的知识**，否则只是复述。面试问「10 万行为什么不卡」，你答得出「量级 = 可见格子数 × 角色数，我数过」，这就是资深和入门的区别。

### 3. 坐标系的事，宁可多写一行映射

跨层取数据一律 `mapToSource`，别信「这里只有一个模型」。真实项目里代理会一层套一层（先按状态过滤、再按权限过滤），映射写规范了是**幂等**的，省了则是偶现 bug。同理，选择集要回源就用 `mapSelectionToSource()`。写 Qt 的老手都有这个肌肉记忆：**先问这个 index 属于谁。**

### 4. 便利类不是「低配」，是要算账的选择

很多同行把 `QStandardItemModel` 当「初学者用的」，这不对。三十行出一屏、树表共用、自带 `itemChanged`，这些活儿它干得漂亮。真正错的是**拿它撑十万行**。「你现在的数据量、角色控制权、性能预算，允许不允许便利类」——会用这三个问题问自己，就是资深度。

### 5. 别背二手 API，本机头文件才是准

这一阶段师傅拆掉了好几条「网上人人都在背」的说法：`filterFixedColumn` 根本不存在（真名是 `setFilterFixedString` + `filterKeyColumn`）；Qt 6.12 的 `SelectionFlag` 里没有 `CommandMask`；`selectionChanged` 的参数不带命令（翻译发生在 protected virtual `selectionCommand()`）；`ScrollMode` 属于 `QAbstractItemView`；Qt6 的 `QStandardItem` 已不继承 `QObject`；Qt 里没有 `appendChild`（加子节点用 `appendRow`）。**查不到签名的名字一律不写进代码**——包括我说的，你也去头文件里复核一遍。

### 6. 这一阶段的验收标准：能撑住面试，也能撑住线上

把 8 讲啃下来的标志不是「都看过」，而是这几件事你能当场讲清：五角色各管什么、`data()` 的调用量级、回路里 `dataChanged` 为什么必须发、代理两层坐标系怎么换算、选择为什么在模型侧、`resizeColumnsToContents()` 为什么危险。**这些恰好是面试最爱问、线上事故最溯源的六条。**

> 模型/视图与委托就到这儿。这 8 个文件把「数据」和「画出来」彻底分开了，往后你写任何列表、表格、树、任何自定义控件的数据侧，用的都是这副骨架。下一站「并发与线程」，我们会把「`data()` 里不许干重活」这条纪律的出口补上——重活该挪去哪儿。加油，徒弟！
