// ============================================================================
// 第 1 讲 · 模型/视图五角色总览：Model / View / Delegate / Selection / Proxy
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   把「模型/视图」这套东西的**五个角色**一次性摆清楚，并把三条最反直觉的事实
//   用打印的方式钉死，不让你靠背诵去接受它：
//     1) Qt 为什么拒绝「把数据直接塞进控件」这种设计（老 VB/Delphi 的
//        List.Items[i].Caption 那条路）：控件是**画出来的**，不是**存出来的**。
//        本讲用「视图的 children 数量与行数无关」这条来证明：40 行 160 个格子，
//        QTableView 底下的子对象仍然只有表头 + 滚动条那几个，格子不是控件、
//        数据也不在视图里（QTableView 头文件通篇没有 item()/addItem()）。
//     2) QModelIndex 只是「**带内部指针的坐标**」：它不持有数据（把 sizeof 打出来
//        给你看）、会失效（removeRows 之后现场演示裸 index 与 QPersistentModelIndex
//        的两种下场）、**不能跨模型混用**（把源模型的 index 喂给代理模型就不认识）。
//     3) data(index, role) 是被视图**反复调用**的拉取式接口（pull-based）：
//        视图不问「变化」，它问「现在是什么」。本讲给现成的 QStandardItemModel
//        套一层计数器，把「滚一段距离里 data() 被调了几次、按角色怎么分布」打印出来。
//     4) 顺手把另外三个角色的入口认全：Delegate（画一格 + 造编辑器）、
//        Selection Model（选择状态独立于模型，view->selectionModel()）、
//        Proxy Model（同一份数据前再套一层坐标系）。
//   最后用 QStandardItemModel + QTableView 跑通**最小闭环**：造数据 → setModel →
//   show → 读回同一个格子。这一讲不写自己的模型（第 2 讲）、不碰编辑（第 3 讲）、
//   不碰委托（第 4 讲）。
//
// 对应构建目标名：model_view_01_architecture
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(model_view_01_architecture 01_architecture.cpp)
//   target_link_libraries(model_view_01_architecture PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)
//   本文件**不含 Q_OBJECT 类**：两个自定义类都只是覆写虚函数、不加新信号/槽，
//   所以末尾不需要 #include "01_architecture.moc"。讲架构总览不必先懂元对象。
//
// 运行前须知：
//   · 无人值守：QTimer::singleShot 排动作 → 打印证据 → QCoreApplication::quit()，
//     全程不要鼠标键盘。**没有 QDialog::exec()，也没有裸 QEventLoop::exec()**；
//     中途只用 QCoreApplication::processEvents()（qcoreapplication.h 第 110 行
//     `static void processEvents(QEventLoop::ProcessEventsFlags flags, int maxtime)`）
//     把已经排好队的重绘「催」出来，它不等用户、不套事件循环。
//   · 可以无头跑：`model_view_01_architecture -platform offscreen`（CTest 已设好环境变量）。
//   · 会真的 show 一个窗口（offscreen 下是不可见窗口），因为「视图调用 data()」这件事
//     只有在有视图、有布局、有重绘时才发生——这正是本讲的全部意义。
//   · 打印的**次数依赖机器/样式/窗口尺寸**，我只承诺关系：一次重绘的 data() 调用数是
//     「可见格子数 × 角色数」这个量级，而不是「总格子数」。
//   · 图解页面：../visuals/model_view.html（五角色数据流 + index 生命周期）。
//   · 未实测声明：本机当前无法链接 Qt 运行时（运行时库版本不匹配），
//     本示例**已通过 g++ -fsyntax-only 编译期核对（-std=c++17 -Wall -Wextra 零 error 零 warning），
//     未链接、未运行**；下面每一处「预期」后面写的是推理，不是实测数字。
// ============================================================================

#include <QApplication>             // QtWidgets/qapplication.h
#include <QTableView>               // QtWidgets/qtableview.h
#include <QAbstractItemView>        // QtWidgets/qabstractitemview.h（ScrollHint/State/selectionModel）
#include <QStandardItemModel>       // **QtGui**/qstandarditemmodel.h ← Qt6 里它在 QtGui，不在 QtWidgets
#include <QStandardItem>            // 同上
#include <QSortFilterProxyModel>    // **QtCore**/qsortfilterproxymodel.h ← 代理模型全家在 QtCore
#include <QItemSelectionModel>      // QtCore/qitemselectionmodel.h
#include <QAbstractItemModel>       // QtCore/qabstractitemmodel.h：QModelIndex 也住在这个头里
#include <QModelIndex>
#include <QPersistentModelIndex>    // 同一个头；讲「会失效」必须用它
#include <QVariant>                 // QtCore/qvariant.h
#include <QString>
#include <QStringList>
#include <QList>
#include <QHash>
#include <QSet>
#include <QMetaType>
#include <QPaintEvent>              // QtGui/qevent.h
#include <QTimer>
#include <QCoreApplication>
#include <QEventLoop>               // 只用它的 ProcessEventsFlag 枚举，不用 exec()
#include <QGuiApplication>
#include <QtGlobal>                 // QT_VERSION_STR
#include <QDebug>

// ============================================================================
// 一、核对签名（本机 C:\msys64\ucrt64\include\qt6，Qt 6.12.0；行号是我去头文件里看的）
// ============================================================================
// QtCore/qabstractitemmodel.h
//   第 127~174 行  class QModelIndex
//     ← **本机没有 QtCore/qmodelindex.h 这个文件**，QModelIndex 就住在 qabstractitemmodel.h 里；
//       include <QModelIndex> 走的是转发头 QtCore/QModelIndex
//     第 133/134 行 constexpr int row() const noexcept / constexpr int column() const noexcept
//     第 135 行     constexpr quintptr internalId() const noexcept
//     第 136 行     void *internalPointer() const noexcept   ← internalId 的「指针脸」
//     第 138 行     QModelIndex parent() const               ← 内联，转手调 model()->parent(*this)
//     第 140 行     QModelIndex siblingAtColumn(int) const / 141 siblingAtRow(int)
//     第 142 行     QVariant data(int role = Qt::DisplayRole) const  ← 便利：等于 model->data(index, role)
//     第 144 行     Qt::ItemFlags flags() const
//     第 145 行     constexpr const QAbstractItemModel *model() const noexcept
//     第 146 行     constexpr bool isValid() const noexcept { return (r >= 0) && (c >= 0) && (m != nullptr); }
//                    ← **isValid 的判据就这三条**，本讲逐条打印验证
//     第 171~173 行 私有成员只有 int r, c; quintptr i; totally_ordered_wrapper<const QAbstractItemModel*> m;
//                    ← 行、列、一个内部 id、模型指针。**没有任何数据字段**，所以它不持有数据
//     第 149~165 行 comparesEqual()/compareThreeWay()：四个字段全等才相等
//                    → 「同一个格子」在源模型和代理模型里取到的 index **一定不相等**
//   第 175 行 Q_DECLARE_TYPEINFO(QModelIndex, Q_RELOCATABLE_TYPE)   ← 拷贝很轻，可以值传
//   第 273 行 Q_INVOKABLE bool hasIndex(int row, int column,
//                              const QModelIndex &parent = QModelIndex()) const;
//   第 274/275 行 virtual QModelIndex index(int row, int column,
//                              const QModelIndex &parent = QModelIndex()) const = 0;   ← 纯虚
//   第 276 行 virtual QModelIndex parent(const QModelIndex &child) const = 0;           ← 纯虚
//   第 279/280 行 virtual int rowCount(const QModelIndex &parent = QModelIndex()) const = 0;
//                 virtual int columnCount(const QModelIndex &parent = QModelIndex()) const = 0;
//   第 281 行 virtual bool hasChildren(const QModelIndex &parent = QModelIndex()) const;
//   第 283 行 virtual QVariant data(const QModelIndex &index,
//                              int role = Qt::DisplayRole) const = 0;                    ← 纯虚
//   第 284 行 virtual bool setData(const QModelIndex &index, const QVariant &value,
//                              int role = Qt::EditRole);      ← 非纯虚：默认直接返回 false
//   第 286/287 行 virtual QVariant headerData(int section, Qt::Orientation orientation,
//                              int role = Qt::DisplayRole) const;   ← 非纯虚
//   第 324 行 virtual Qt::ItemFlags flags(const QModelIndex &index) const;
//   第 333 行 virtual QHash<int,QByteArray> roleNames() const;
//   第 356 行 virtual void multiData(const QModelIndex &index, QModelRoleDataSpan) const; ← 批量取角色
//   第 359/360 行 void dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight,
//                                  const QList<int> &roles = QList<int>());
//                    ← Qt6.12 这里是 **QList<int>**；Qt5 老代码写 QVector<int>
//   第 377/378 行 void modelAboutToBeReset(QPrivateSignal); void modelReset(QPrivateSignal);
//                    ← 带 QPrivateSignal：**外面能 connect，不能 emit**
//   第 304/306 行 virtual bool insertRows(int row, int count,
//                              const QModelIndex &parent = QModelIndex());    （Q_REVISION(6,4)）
//                 virtual bool removeRows(int row, int count,
//                              const QModelIndex &parent = QModelIndex());
//   第 396/397 行 inline QModelIndex createIndex(int row, int column,
//                              const void *data = nullptr) const;  / (int, int, quintptr) const
//   第 402~406/420/421 行 beginInsertRows/endInsertRows/beginRemoveRows/endRemoveRows/
//                 beginResetModel/endResetModel —— 全在 **protected:** 段里（只能子类自己调）
//   第 468~493 行 class Q_CORE_EXPORT QAbstractTableModel : public QAbstractItemModel
//     第 476 行 QModelIndex index(int, int, const QModelIndex &parent = QModelIndex()) const override;
//                    ← **给了默认实现，不再是纯虚**（这是它和 QAbstractItemModel 的头一条区别）
//     第 481 行 Qt::ItemFlags flags(const QModelIndex &) const override;   ← 也有默认实现
//     第 483 行 using QObject::parent;
//                    ← 关键：覆写 parent(const QModelIndex&) 会把 QObject::parent() 遮蔽掉，
//                      Qt 自己在这一行加 using 把无参版本捞回来
//     第 491 行 QModelIndex parent(const QModelIndex &child) const override;
//                    ← 它在 **private:** 段里！QAbstractTableModel 把 parent() 藏了；
//                      但虚函数按声明处看访问性，通过 QAbstractItemModel* 调用照样合法
// QtWidgets/qtableview.h
//   第 18 行 class Q_WIDGETS_EXPORT QTableView : public QAbstractItemView
//   第 33 行 void setModel(QAbstractItemModel *model) override;
//   第 38/39 行 QHeaderView *horizontalHeader() const; / QHeaderView *verticalHeader() const;
//   第 52 行 void setColumnWidth(int column, int width);      第 88 行 void selectRow(int row);
//   第 97 行 void resizeColumnsToContents();                  第 99 行 void setShowGrid(bool show);
//   ← 通篇**没有 item()/addItem()/setCellText()**：视图侧不提供任何「存数据」的口子
// QtWidgets/qabstractitemview.h
//   第 101 行 virtual void setModel(QAbstractItemModel *model);
//   第 104/105 行 virtual void setSelectionModel(QItemSelectionModel *);
//                 QItemSelectionModel *selectionModel() const;
//   第 110/113 行 setSelectionMode(SelectionMode) / setSelectionBehavior(SelectionBehavior)
//   第 119/120 行 void setEditTriggers(EditTriggers); EditTriggers editTriggers() const;
//   第 177 行 virtual void scrollTo(const QModelIndex &, ScrollHint = EnsureVisible) = 0;
//   第 178 行 virtual QModelIndex indexAt(const QPoint &) const = 0;
//   第 190~192 行 openPersistentEditor/closePersistentEditor/isPersistentEditorOpen
//   第 194/195 行 void setIndexWidget(const QModelIndex&, QWidget*); QWidget *indexWidget(...) const;
//                 ← 注意：**编辑器不是 index widget**，用 indexWidget() 拿不到编辑器
//   第 197/200 行 void setItemDelegateForRow(int, QAbstractItemDelegate*);
//                 void setItemDelegateForColumn(int, QAbstractItemDelegate*);   ← 第 4 讲的主战场
//   第 219 行 void edit(const QModelIndex &index);                 ← public 槽
//   第 245~251 行 signals: pressed/clicked/doubleClicked/activated/entered/viewportEntered
//   第 273 行 virtual bool edit(const QModelIndex &index, EditTrigger trigger, QEvent *event); ← protected
//   第 284~295 行 enum State { NoState, DraggingState, DragSelectingState, EditingState,
//                 ExpandingState, CollapsingState, AnimatingState };  State state() const;
//   第 79~87 行 enum EditTrigger { NoEditTriggers=0, CurrentChanged=1, DoubleClicked=2,
//                 SelectedClicked=4, EditKeyPressed=8, AnyKeyPressed=16, AllEditTriggers=31 };
//   第 71~76 行 enum ScrollHint { EnsureVisible, PositionAtTop, PositionAtBottom, PositionAtCenter };
//   第 55~68 行 enum SelectionMode { NoSelection, SingleSelection, MultiSelection,
//                 ExtendedSelection, ContiguousSelection }; enum SelectionBehavior {
//                 SelectItems, SelectRows, SelectColumns };
// QtGui/qstandarditemmodel.h（便利类，本讲用、第 2 讲起就不用）
//   第 24 行 class Q_GUI_EXPORT QStandardItem
//   第 33 行 virtual QVariant data(int role = Qt::UserRole + 1) const;
//   第 35 行 virtual void setData(const QVariant &value, int role = Qt::UserRole + 1);
//                    ← **默认角色是 UserRole+1，不是 DisplayRole**：`item->setData(42)` 之后
//                      item->text() 是空的。要么显式给角色，要么用 setText()/构造函数重载
//   第 216~222 行 inline setText → setData(text, Qt::DisplayRole)、
//                 setIcon → …Qt::DecorationRole、setToolTip → …Qt::ToolTipRole
//   第 285~287 行 QStandardItemModel(QObject*) / (int rows, int columns, QObject*) / ~QStandardItemModel()
//   第 290 行 QHash<int, QByteArray> roleNames() const override;
//   第 292~296 行 index/parent/rowCount/columnCount 全部 override（都给了实现）
//   第 299 行 QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
//   第 304 行 QVariant headerData(…) override;      第 311 行 bool removeRows(…) override;
//   第 314 行 Qt::ItemFlags flags(const QModelIndex &) const override;
//   第 320 行 void clear();
//   第 329~331 行 QStandardItem *item(int row, int column = 0) const;
//                 void setItem(int row, int column, QStandardItem *item);
//   第 339 行 void setHorizontalHeaderLabels(const QStringList &labels);
//   第 349/351 行 void insertRow(int row, const QList<QStandardItem*> &items);
//                 inline void insertRow(int row, QStandardItem *item);
// QtCore/qitemselectionmodel.h
//   第 89 行 class Q_CORE_EXPORT QItemSelectionModel : public QObject
//   第 107~118 行 enum SelectionFlag { NoUpdate, Select, Deselect, Toggle, Current, Rows,
//                    Columns, CommandLinkHint, EditCurrent, Clear, SelectCurrent,
//                    Rows=0x0020, ClearAndSelect = Clear | Select };
//   第 128/130/139/140 行 QModelIndex currentIndex() const; bool isSelected(…) const;
//                    QModelIndexList selectedIndexes() const;
//                    QModelIndexList selectedRows(int column = 0) const;
//   第 151/152 行 virtual void setCurrentIndex(const QModelIndex&, SelectionFlags);
//                 virtual void select(const QModelIndex&, SelectionFlags);
// QtCore/qabstractproxymodel.h
//   第 17 行 class Q_CORE_EXPORT QAbstractProxyModel : public QAbstractItemModel
//   第 27 行 virtual void setSourceModel(QAbstractItemModel *sourceModel);
//   第 31/32 行 virtual QModelIndex mapToSource(const QModelIndex &) const = 0;
//                 virtual QModelIndex mapFromSource(const QModelIndex &) const = 0;
// QtCore/qsortfilterproxymodel.h
//   第 55 行 void setSourceModel(QAbstractItemModel *) override;
//   第 57 行 QModelIndex mapToSource(const QModelIndex &) const override;
//   第 66/67 行 int filterKeyColumn() const; void setFilterKeyColumn(int column);
//   第 89/90 行 int sortRole() const; void setSortRole(int role);        ← 排序用哪个角色
//   第 93/94 行 int filterRole() const; void setFilterRole(int role);
//   第 86 行 void setDynamicSortFilter(bool enable);
//   第 124 行 void setFilterFixedString(const QString &pattern);
//   第 128 行 virtual bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const;
//   第 156~164 行 index/parent/rowCount/columnCount/data 全部 override
// QtCore/qnamespace.h
//   第 1517~1554 行 enum ItemDataRole { DisplayRole=0, DecorationRole=1, EditRole=2,
//                    ToolTipRole=3, StatusTipRole=4, WhatsThisRole=5, FontRole=6,
//                    TextAlignmentRole=7, BackgroundRole=8, ForegroundRole=9, CheckStateRole=10,
//                    AccessibleTextRole=11, AccessibleDescriptionRole=12, SizeHintRole=13,
//                    InitialSortOrderRole=14, …, UserRole = 0x0100 };
//                    ← **Qt6 里 Qt::SortRole 已经删掉了**（本机 qnamespace.h 搜不到 SortRole），
//                      代理模型的排序角色要显式 setSortRole(Qt::EditRole)
//   第 1556~1569 行 enum ItemFlag { NoItemFlags=0, ItemIsSelectable=1, ItemIsEditable=2,
//                    ItemIsDragEnabled=4, ItemIsDropEnabled=8, ItemIsUserCheckable=16,
//                    ItemIsEnabled=32, ItemIsAutoTristate=64, ItemNeverHasChildren=128,
//                    ItemIsUserTristate=256 }; Q_DECLARE_FLAGS(ItemFlags, ItemFlag)
// 本讲**没用**、也提醒你别用的：Qt::SortRole（Qt6 无）、indexWidget() 当「拿编辑器」（拿不到）、
//   QVector<int> 版 dataChanged（本机是 QList<int>）、QtCore/qmodelindex.h（这文件不存在）。

// ============================================================================
// 二、给现成模型套一层计数器：证明 data() 是「被反复问」的
// ============================================================================
// 只做一件事：data()/headerData() 每被调一次记一笔，然后把活交给 QStandardItemModel。
// **没有 Q_OBJECT**——不加新信号/槽、只覆写虚函数，元对象那一层不需要参与。
class CountingModel : public QStandardItemModel
{
public:
    explicit CountingModel(QObject *parent = nullptr) : QStandardItemModel(parent) {}

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        ++s_total;
        s_roleHits[role] += 1;
        if (role == Qt::DisplayRole) {
            ++s_display;
            if (index.isValid()) {                 // 视图也会拿无效 index 来问，这里分开记
                s_rows.insert(index.row());
                s_cols.insert(index.column());
            } else {
                ++s_invalidAsks;
            }
        }
        return QStandardItemModel::data(index, role);
    }

    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override
    {
        ++s_header;
        return QStandardItemModel::headerData(section, orientation, role);
    }

    // ── 读数窗口：计数属于「这一讲的取证」，不属于某个模型实例，所以放 static ──
    static void resetCounters()
    {
        s_total = 0;
        s_display = 0;
        s_header = 0;
        s_invalidAsks = 0;
        s_roleHits.clear();
        s_rows.clear();
        s_cols.clear();
    }
    static qint64 total() { return s_total; }
    static qint64 displayCalls() { return s_display; }
    static qint64 headerCalls() { return s_header; }
    static qint64 invalidAsks() { return s_invalidAsks; }
    static int queriedRowCount() { return s_rows.size(); }
    static int queriedColCount() { return s_cols.size(); }

    static QString roleHistogram()
    {
        struct Entry { int role; const char *name; };
        static const Entry names[] = {
            { Qt::DisplayRole,       "DisplayRole" },
            { Qt::DecorationRole,    "DecorationRole" },
            { Qt::EditRole,          "EditRole" },
            { Qt::ToolTipRole,       "ToolTipRole" },
            { Qt::FontRole,          "FontRole" },
            { Qt::TextAlignmentRole, "TextAlignmentRole" },
            { Qt::BackgroundRole,    "BackgroundRole" },
            { Qt::ForegroundRole,    "ForegroundRole" },
            { Qt::CheckStateRole,    "CheckStateRole" },
            { Qt::SizeHintRole,      "SizeHintRole" },
        };
        QStringList parts;
        for (const Entry &e : names) {
            const int hit = s_roleHits.value(e.role, 0);
            if (hit > 0)
                parts << QString::fromLatin1(e.name) + QLatin1Char(':') + QString::number(hit);
        }
        for (auto it = s_roleHits.cbegin(); it != s_roleHits.cend(); ++it) {
            bool known = false;
            for (const Entry &e : names) {
                if (e.role == it.key())
                    known = true;
            }
            if (!known)
                parts << QStringLiteral("role%1:%2").arg(it.key()).arg(it.value());
        }
        return parts.isEmpty() ? QStringLiteral("（一次都没被调用）") : parts.join(QStringLiteral("  "));
    }

private:
    inline static qint64 s_total = 0;
    inline static qint64 s_display = 0;
    inline static qint64 s_header = 0;
    inline static qint64 s_invalidAsks = 0;
    inline static QHash<int, int> s_roleHits;
    inline static QSet<int> s_rows;
    inline static QSet<int> s_cols;
};

// ============================================================================
// 三、给视图套一层计数器：重绘到底发生了多少次
// ============================================================================
class CountingTableView : public QTableView
{
public:
    using QTableView::QTableView;             // 继承构造函数；同样不需要 Q_OBJECT

    static qint64 paints() { return s_paints; }
    static void resetPaints() { s_paints = 0; }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        ++s_paints;
        QTableView::paintEvent(event);        // 该画还是要画——画的时候视图就去问模型
    }

private:
    inline static qint64 s_paints = 0;
};

// ============================================================================
// 四、小工具
// ============================================================================
// 把事件循环里已经排好队的活儿催出来（布局、重绘）。它不等用户输入、不嵌套 exec()。
static void flushEvents()
{
    QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
}

// 把「一个 index 到底是什么」打成一行
static QString describeIndex(const QModelIndex &idx)
{
    return QStringLiteral("row=%1 column=%2 internalId=0x%3 model=%4 isValid=%5")
        .arg(idx.row())
        .arg(idx.column())
        .arg(QString::number(idx.internalId(), 16))
        .arg(idx.model() ? QStringLiteral("非空") : QStringLiteral("nullptr"))
        .arg(idx.isValid() ? QStringLiteral("true") : QStringLiteral("false"));
}

// ============================================================================
// 五、证据①：最小闭环 —— QStandardItemModel + QTableView
// ============================================================================
static void evidenceMinimalLoop(CountingModel &model, CountingTableView &view)
{
    qInfo() << "【证据①】最小闭环：数据存在模型里，视图只负责画";

    model.setHorizontalHeaderLabels({ QStringLiteral("任务"), QStringLiteral("负责人"),
                                      QStringLiteral("进度"), QStringLiteral("阶段") });
    const QStringList owners { QStringLiteral("张工"), QStringLiteral("李工"), QStringLiteral("王工") };
    const QStringList stages { QStringLiteral("未开始"), QStringLiteral("进行中"), QStringLiteral("已完成") };

    for (int r = 0; r < 40; ++r) {
        auto *name = new QStandardItem(QStringLiteral("任务 %1").arg(r + 1));  // 走 setText 语义
        auto *owner = new QStandardItem(owners.at(r % owners.size()));
        auto *prog = new QStandardItem;                                        // 先建空 item
        prog->setData(r * 2 % 101, Qt::DisplayRole);   // 存整数：**显式**给角色（默认角色是 UserRole+1）
        prog->setData(r * 2 % 101, Qt::EditRole);      // EditRole 才是「机器读/编辑用的那个值」
        prog->setData(QStringLiteral("完成 %1%2，可编辑").arg(r * 2 % 101).arg(QLatin1Char('%')),
                      Qt::ToolTipRole);
        prog->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);             // → TextAlignmentRole
        auto *stage = new QStandardItem(stages.at(r % stages.size()));
        model.appendRow(QList<QStandardItem*>{ name, owner, prog, stage });
    }

    view.setModel(&model);
    qInfo() << "  setModel 之后：model->rowCount() =" << model.rowCount()
            << "columnCount() =" << model.columnCount()
            << "｜ view->model() 是否同一个对象 =" << (view.model() == &model);
    qInfo() << "  表头文字也是模型给的：headerData(2, Horizontal, DisplayRole) ="
            << model.headerData(2, Qt::Horizontal, Qt::DisplayRole).toString()
            << "（视图只是把它画在表头区；表头也走 delegate，第 4 讲提一句）";

    const QModelIndex viaModel = model.index(7, 2);
    qInfo() << "  坐标长这样：[" << describeIndex(viaModel) << "]";
    qInfo() << "  同一个格子两条读法：model->data(idx) ="
            << model.data(viaModel, Qt::DisplayRole).toString()
            << "｜ idx.data() =" << viaModel.data(Qt::DisplayRole).toString()
            << "（后者是 QModelIndex::data 的便利写法，内部就是调 model->data；**值一样，因为都是问模型**）";

    const QVariant editValue = viaModel.data(Qt::EditRole);
    qInfo() << "  那一格的 EditRole：typeId 是 int 吗 ="
            << (editValue.typeId() == QMetaType::Int)
            << "｜ toInt() =" << editValue.toInt()
            << "｜ toString() =" << editValue.toString()
            << "（QVariant 负责转换：存的是 int，显示时是字符串，**同一份数据两种读法**）";
    qInfo() << "  问一个没给过的角色：data(idx, FontRole).isValid() ="
            << viaModel.data(Qt::FontRole).isValid()
            << "← **模型对「没有的角色」必须返回无效 QVariant**，视图靠这个决定「要不要处理」";

    view.show();
    flushEvents();
    qInfo() << "  show() 并催一次事件之后：view 的子对象数 =" << view.children().size()
            << "｜格子总数 =" << model.rowCount() * model.columnCount();
    qInfo() << "     这些子对象是" << view.children().count() << "个（表头 2 + 滚动条 2 一类），"
            << "横向表头 =" << (view.horizontalHeader() != nullptr)
            << "纵向表头 =" << (view.verticalHeader() != nullptr)
            << "——**跟行数、列数完全无关**：40 行 160 格，视图侧一个「格子控件」都没有。";
    qInfo() << "     想把数据塞进控件的人第一个撞到的墙：**QTableView 头文件里没有 item()/addItem()**。"
               "想要 item()，只有便利类 QStandardItemModel 有（qstandarditemmodel.h 第 329 行），"
               "那是「模型」提供的，不是「视图」提供的。";
}

// ============================================================================
// 六、证据②：拉取式接口 —— 滚一段路，data() 被问了多少次
// ============================================================================
static void evidencePullBased(CountingModel &model, CountingTableView &view)
{
    qInfo() << "【证据②】data(index, role) 是拉取的：视图反复问，模型从不推";

    const qint64 cells = qint64(model.rowCount()) * model.columnCount();

    CountingModel::resetCounters();
    CountingTableView::resetPaints();
    view.viewport()->repaint();                 // 同步重绘一次：把「画一遍要问多少格」数清楚
    qInfo() << QString::asprintf("  甲：整幅重绘 1 次 → paintEvent %lld 回，data() 被调用 %lld 次"
                                 "（其中 DisplayRole %lld 次、headerData %lld 次）；全表共 %lld 格",
                                 static_cast<long long>(CountingTableView::paints()),
                                 static_cast<long long>(CountingModel::total()),
                                 static_cast<long long>(CountingModel::displayCalls()),
                                 static_cast<long long>(CountingModel::headerCalls()),
                                 static_cast<long long>(cells));
    qInfo() << "     被问到的行 =" << CountingModel::queriedRowCount() << "/" << model.rowCount()
            << "列 =" << CountingModel::queriedColCount() << "/" << model.columnCount()
            << "（列会被问全，行只问到看得见的几条 → 这就是「只画可见格子」的证据）";
    qInfo() << "     角色分布：" << CountingModel::roleHistogram();

    CountingModel::resetCounters();
    CountingTableView::resetPaints();
    for (const int target : { 12, 24, 39 }) {    // 假装用户滚了三次
        view.scrollTo(model.index(target, 0), QAbstractItemView::EnsureVisible);
        flushEvents();
        view.viewport()->repaint();
    }
    qInfo() << QString::asprintf("  乙：连续滚 3 次 → paintEvent %lld 回，data() 又被调用 %lld 次"
                                 "（DisplayRole %lld 次）",
                                 static_cast<long long>(CountingTableView::paints()),
                                 static_cast<long long>(CountingModel::total()),
                                 static_cast<long long>(CountingModel::displayCalls()));
    qInfo() << "     角色分布：" << CountingModel::roleHistogram();
    qInfo() << "     顺手记一笔：拿无效 index 来问的次数 =" << CountingModel::invalidAsks()
            << "（视图确实会用无效坐标问一次，比如「当前项」为空时；所以 data() 开头先判 isValid）";
    qInfo() << "  丙：把甲+乙的调用数加起来跟" << cells << "格比，你会发现它**只跟可见区有关**，"
               "跟总行数无关。这就是模型/视图能扛住十万行的根本原因。";
    qInfo() << "  → 三条工程含义：① data() 里不要加锁、读盘、造大对象、写日志；"
               "② 返回「没有」就用 QVariant()（无效），不要返回空 QString 蒙混；"
               "③ 需要一次拿多个角色时，Qt 给了批量入口 multiData(index, QModelRoleDataSpan)"
               "（qabstractitemmodel.h 第 356 行）与委托侧的 paintingRoles()"
               "（qabstractitemdelegate.h 第 78 行）——第 8 讲调优时再动它。";
}

// ============================================================================
// 七、证据③：QModelIndex 只是坐标 —— 不持有数据、会失效、不能跨模型
// ============================================================================
static void evidenceIndexIsCoordinate(CountingModel &model)
{
    qInfo() << "【证据③】QModelIndex = 带内部指针的坐标，不是数据";

    qInfo() << QString::asprintf("  sizeof(QModelIndex) = %zu 字节 ｜ sizeof(QVariant) = %zu 字节 ｜ "
                                 "sizeof(QPersistentModelIndex) = %zu 字节",
                                 sizeof(QModelIndex), sizeof(QVariant), sizeof(QPersistentModelIndex));
    qInfo() << "     QModelIndex 的全部成员就是 int r, int c, quintptr i, 模型指针"
               "（qabstractitemmodel.h 第 171~173 行）——**没有一格数据的位置**";

    const QModelIndex idx = model.index(3, 1);
    qInfo() << "  idx 拆开看：[" << describeIndex(idx) << "]  model 指针 == &model ="
            << (idx.model() == &model);
    qInfo() << "  idx.internalId() = 0x" + QString::number(idx.internalId(), 16)
            << "  internalPointer() =" << idx.internalPointer()
            << "← 这是**模型自己塞的私货**：便利类塞的是节点指针，"
               "QAbstractTableModel 的默认 index() 塞 0（第 2 讲自己塞、自己取）。"
               "**框架完全不解释它**，只有你的模型解释它，所以它不能当跨模型的身份。";

    const QModelIndex copy = idx;
    qInfo() << "  copy = idx 之后：两个 index 相等 =" << (copy == idx)
            << "取到的值也相等 ="
            << (copy.data(Qt::DisplayRole) == idx.data(Qt::DisplayRole))
            << "（拷贝的是那二十几字节的坐标，**不是那一格的数据**）";

    const QModelIndex invalid;
    qInfo() << "  默认构造的 index：[" << describeIndex(invalid) << "]"
            << "isValid() =" << invalid.isValid()
            << "——判据是 (row>=0 && column>=0 && model!=nullptr) 三条（第 146 行），"
               "默认构造 row/column 都是 -1，第一条就不满足";
    qInfo() << "  越界不崩溃，只给无效：hasIndex(999,0) =" << model.hasIndex(999, 0)
            << "｜ index(999,0).isValid() =" << model.index(999, 0).isValid()
            << "｜ 拿无效 index 问数据：isValid =" << invalid.data().isValid()
            << "text =" << ("[" + invalid.data().toString() + "]")
            << "← 所以**先判 isValid() 再用**是纪律，不是建议";

    const QModelIndex p = idx.parent();
    qInfo() << "  表格模型里 idx.parent()：[" << describeIndex(p) << "]  isValid =" << p.isValid();
    qInfo() << "     → 表格只有一层，行的「父亲」是那个看不见的**虚拟根**，Qt 用无效 index 表示它。"
               "所以 rowCount(QModelIndex()) / columnCount(QModelIndex()) 才表示「整表多少行/列」——"
               "参数是无效 index 就是在问根。树（QTreeView）才会让 parent() 返回真坐标。";
    qInfo() << "     同一条从模型侧看：model->rowCount(无效 index) =" << model.rowCount(QModelIndex())
            << "｜model->columnCount(无效 index) =" << model.columnCount(QModelIndex())
            << "｜model->rowCount(第 0 行的 index) =" << model.rowCount(model.index(0, 0))
            << "｜hasChildren(index(0,0)) =" << model.hasChildren(model.index(0, 0))
            << "（表格在真实格子下面没有子行，全是 0/无效——这就是「表格 = 只有一层的树」）";

    // index 会失效：裸 QModelIndex 只是三个数 + 一个指针，删行之后它「不知道自己过时了」
    const QModelIndex doomed = model.index(5, 0);
    const QPersistentModelIndex guard(doomed);
    qInfo() << "  删除前 → 裸 index：[" << describeIndex(doomed) << "] 文本 ="
            << doomed.data(Qt::DisplayRole).toString();
    qInfo() << "  删除前 → 护栏版（QPersistentModelIndex）：row =" << guard.row()
            << "isValid =" << guard.isValid() << "文本 =" << guard.data(Qt::DisplayRole).toString();
    const bool removed = model.removeRows(5, 1);   // 便利类内部会 begin/endRemoveRows
    qInfo() << "  removeRows(5,1) 返回 =" << removed;
    qInfo() << "  删除后 → 护栏版：isValid =" << guard.isValid()
            << "（模型把「这一行没了」通知给了它，**它自己作废**）";
    qInfo() << "  删除后 → 裸 index：row 仍是" << doomed.row() << "，文本变成了「"
            << doomed.data(Qt::DisplayRole).toString() << "」"
            << "—— 它**不报错，只是指到别的行上去了**。这就是「别把 QModelIndex 存起来跨操作用」的由来。";
    qInfo() << "  纪律：要跨「增删行/reset」持有位置 → QPersistentModelIndex；"
               "要跨「函数调用」持有位置 → 存 row/column 两个整数，用完立刻 model->index(r,c)；"
               "要跨「模型」（源↔代理）→ 必须先 mapToSource / mapFromSource。";

    // 补回一行，让后面的证据看到完整表格
    model.insertRow(5, new QStandardItem(QStringLiteral("（补回来的占位）")));
    qInfo() << "  补回占位行之后 rowCount =" << model.rowCount()
            << "（注意：插入这一行只给了第 0 列，第 1~3 列是空的，"
               "便利类容忍「洞」；手写模型时你可没有这个容忍度，第 2/3 讲自己管）";
}

// ============================================================================
// 八、证据④：第四个角色 —— Selection Model
// ============================================================================
static void evidenceSelectionModel(CountingModel &model, CountingTableView &view)
{
    qInfo() << "【证据④】选择状态不在模型里，在**第四个角色**里";

    QItemSelectionModel *sel = view.selectionModel();
    qInfo() << "  从没调过 setSelectionModel，view->selectionModel() 也非空 =" << (sel != nullptr)
            << "｜它是 setModel 时按这个模型建起来的：sel->model() == view.model() ="
            << (sel->model() == static_cast<const QAbstractItemModel *>(view.model()));
    qInfo() << "     → 关键点：**模型里没有「选中」这个角色**。选择是另一个 QObject，"
               "它的坐标空间跟模型一致，但管的是「哪些格子被选中、哪个是当前」，"
               "与「格子里是什么」彻底分开。所以同一份模型可以挂两个视图、两套选择。";

    const QModelIndex one = model.index(9, 0);
    sel->select(one, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    qInfo() << "  select(index(9,0), ClearAndSelect|Rows) 之后："
            << "selectedIndexes().size() =" << sel->selectedIndexes().size()
            << "（按行选 → 该行的 4 列全在集合里）｜ isSelected(9,1) ="
            << sel->isSelected(model.index(9, 1))
            << "｜ currentIndex =[" << describeIndex(sel->currentIndex()) << "]";
    const QModelIndexList rows = sel->selectedRows();
    qInfo() << "  selectedRows()（默认按第 0 列代表）size =" << rows.size()
            << "第一个 row =" << (rows.isEmpty() ? -1 : rows.first().row())
            << "｜selectedColumns() 也能问，size =" << sel->selectedColumns(0).size();

    view.selectRow(13);
    qInfo() << "  view.selectRow(13)（视图便利，转手就是 sel->select）之后 isSelected(13,2) ="
            << view.selectionModel()->isSelected(model.index(13, 2))
            << "｜selectionModel()->selectedIndexes().size() ="
            << view.selectionModel()->selectedIndexes().size();
    // 一个真实的小坑（本机头文件给的）：**QTableView::selectedIndexes() 是 protected**
    // （qtableview.h 第 128 行 `QModelIndexList selectedIndexes() const override;` 落在 protected 段），
    // 所以外面想拿选中格子，走 QAbstractItemView::selectedIndexes()（第 271 行，也是 protected）
    // 也不行——正路是 view.selectionModel()->selectedIndexes()。编译器替我抓到的，抄下来。
    qInfo() << "  删一行会发生什么（选择模型会跟着调整，这是裸 index 做不到的）：";
    const int beforeRows = view.selectionModel()->selectedRows().size();
    model.removeRows(13, 1);
    const int afterRows = view.selectionModel()->selectedRows().size();
    qInfo() << "     removeRows(13,1)：selectedRows 从" << beforeRows << "变成" << afterRows
            << "（第 13 行被摘掉；回路是「模型发行数变化信号 → 选择模型自己改状态」）";
    model.insertRow(13, new QStandardItem(QStringLiteral("（补回来的占位）")));

    qInfo() << "  第 6 讲正面讲这个角色：QItemSelection 的「区间」表示（它存的是 topLeft/bottomRight，"
               "不是一堆 index）、批量选择、跨视图同步、selectAll/clearSelection 的行为差异。";
}

// ============================================================================
// 九、证据⑤：第五个角色 —— Proxy Model（另开一套坐标系）
// ============================================================================
static void evidenceProxy(CountingModel &model)
{
    qInfo() << "【证据⑤】代理模型：同一份数据，另一套坐标";

    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&model);                // 不复制数据，只「转接」
    proxy.setFilterKeyColumn(0);                 // 用第 0 列做匹配
    proxy.setFilterRole(Qt::DisplayRole);        // **Qt6 没有 Qt::SortRole**，角色要显式说
    proxy.setSortRole(Qt::EditRole);             // 排序读 EditRole（本机第 90 行 setSortRole）
    proxy.setFilterFixedString(QStringLiteral("任务 1"));

    const int srcRows = model.rowCount();
    const int proxyRows = proxy.rowCount();
    qInfo() << QString::asprintf("  源模型 %d 行 → 代理模型 %d 行（过滤串「任务 1」，第 0 列）；"
                                 "列数 %d → %d",
                                 srcRows, proxyRows, model.columnCount(), proxy.columnCount());
    qInfo() << "  代理的 data() 内部还是去问源模型：proxy.index(0,0) 的文本 ="
            << proxy.index(0, 0).data(Qt::DisplayRole).toString();

    const QModelIndex pi = proxy.index(1, 0);
    const QModelIndex si = proxy.mapToSource(pi);
    qInfo() << "  代理坐标 index(1,0)：[" << describeIndex(pi) << "] 行号" << pi.row()
            << "→ mapToSource 之后：[" << describeIndex(si) << "] 行号" << si.row()
            << "｜两个 index 相等吗 =" << (pi == si)
            << "（相等要 row/column/internalId/model **四个字段全等**，模型不同直接不等）";

    const QModelIndex fromSource = model.index(0, 0);
    const QVariant proxySide = proxy.data(fromSource, Qt::DisplayRole);
    const int rowCountViaForeign = proxy.rowCount(fromSource);
    qInfo() << "  把**源模型的 index** 直接喂给代理模型：proxy.rowCount(源index) ="
            << rowCountViaForeign << "｜proxy.data(源index).isValid() =" << proxySide.isValid()
            << "← 不崩溃、不报错，**就是问不出东西**（代理不认识这套坐标）";
    qInfo() << "  正确姿势：先映射再问。mapToSource/mapFromSource（qabstractproxymodel.h 第 31/32 行）"
               "是代理模型的全部身家；视图 setModel(&proxy) 之后拿到的永远是**代理坐标**，"
               "把它们直接拿去给源模型用，轻则空值、重则越界踩内存。";
    qInfo() << "  代理家族（Qt6 全在 QtCore，本机 QtCore/q…proxymodel.h）："
               "QSortFilterProxyModel、QIdentityProxyModel（不改数据只改接口的壳）、"
               "QConcatenateTablesProxyModel（多表拼一张）、QTransposeProxyModel（行列倒过来）。"
               "第 5 讲专讲，包括自写代理必须实现的四个映射函数。";
}

// ============================================================================
// 十、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("model_view_01"));

    CountingModel model;                      // 栈上就够：本讲不测父子所有权
    CountingTableView view;

    QTimer::singleShot(0, [&model, &view]() {
        qInfo() << "================================================================";
        qInfo() << "model_view 第 1 讲 · 模型/视图五角色（平台 ="
                << QGuiApplication::platformName() << ", Qt =" << QT_VERSION_STR << "）";
        qInfo() << "================================================================";

        evidenceMinimalLoop(model, view);
        evidencePullBased(model, view);
        evidenceIndexIsCoordinate(model);
        evidenceSelectionModel(model, view);
        evidenceProxy(model);

        QTimer::singleShot(80, []() {
            qInfo() << "【收尾】五个角色认全了：Model（存 + 答）、View（画 + 问 + 滚动）、"
                       "Delegate（一格怎么画 + 编辑器怎么造）、Selection Model（谁被选中）、"
                       "Proxy（另一套坐标）。下一讲把「答」这一半自己写出来。";
            QCoreApplication::quit();
        });
    });

    return app.exec();
}

// ============================================================================
// 十一、速查表与纪律（背不住就回来查）
// ============================================================================
/*
 * 一、五个角色各持有什么、对外说什么话
 *   ┌──────────────────────┬───────────────────────┬──────────────────────────────┐
 *   │ 角色                 │ 持有什么              │ 主要接口                     │
 *   ├──────────────────────┼───────────────────────┼──────────────────────────────┤
 *   │ Model                │ 真正的数据（你自己的）│ index/parent/rowCount/       │
 *   │ QAbstractItemModel   │ + 每个角色的解释      │ columnCount/data + dataChanged│
 *   │ View                 │ 视口几何、滚动位置    │ setModel/scrollTo/indexAt/   │
 *   │ QAbstractItemView    │ **一格控件都没有**    │ selectionModel/state         │
 *   │ Delegate             │ **什么都不持有**      │ paint/sizeHint/createEditor/ │
 *   │ QAbstractItemDelegate│ （编辑器是临时工）    │ setEditorData/setModelData   │
 *   │ Selection Model      │ 选中集合 + 当前 index │ select/setCurrentIndex/      │
 *   │ QItemSelectionModel  │ （坐标跟着模型走）    │ selectedIndexes/isSelected   │
 *   │ Proxy Model          │ 映射关系 + 过滤条件   │ mapToSource/mapFromSource/   │
 *   │ QAbstractProxyModel  │ **不复制数据**        │ setSourceModel               │
 *   └──────────────────────┴───────────────────────┴──────────────────────────────┘
 *
 * 二、为什么 Qt 拒绝「数据塞进控件」
 *   ① 内存：20 万行 × 8 列 = 160 万个 QWidget（每个几十字节起步，还带事件、字体、
 *      调色板继承链）。模型/视图之下视图的子对象恒定为「表头 + 滚动条 + 当前编辑器」，
 *      本讲用 children().size() 打出来了。
 *   ② 时间：只需要把「可见那几格」格式化，其余的连 QString 都不该生成——
 *      拉取式接口把这个约束变成**结构性**的：视图只能问，不问就不算。
 *   ③ 复用：同一份模型可以同时喂 QTableView / QTreeView / QListView / 代理模型 /
 *      QML 的 ListView。数据一旦长在控件里，「视图无关」就没了。
 *   ④ 编辑：改了值要落回数据源。「控件里那份文本」和「数据源那份值」是两份，
 *      同步责任必须有人担；模型/视图把它拆成 setData + dataChanged，回路清楚（第 3 讲）。
 *
 * 三、QModelIndex 的四条脾气（本讲逐条打印过）
 *   ① 不持有数据：成员只有 r / c / i / model 指针，见证据③的 sizeof。
 *   ② 属于某个模型：model() 是身份的一部分；跨模型必须 mapToSource/mapFromSource。
 *   ③ 会失效：增删行、reset 之后裸 index 不报错、只给错的东西；
 *      QPersistentModelIndex 会被通知并自己作废（isValid() 变 false）。
 *   ④ 很轻（Q_RELOCATABLE_TYPE，第 175 行）：值传、拷一堆都无所谓，
 *      QModelIndexList 批量操作是正常用法，**但别存过久**。
 *
 * 四、data(index, role) 的三条工程含义
 *   ① 会被反复调用（证据②）：不要在里面加锁、读盘、new 大对象、打日志。
 *   ② 「这个格子没有这个角色」→ 返回 QVariant()（无效），不是空串：
 *      视图靠 isValid() 决定要不要走后面那条路（比如要不要画图标）。
 *   ③ 一次调用只答一个角色；要批量答，用 multiData()（第 356 行）与
 *      QAbstractItemDelegate::paintingRoles()（第 78 行）——第 8 讲调优。
 *
 * 五、Qt5 → Qt6 的本讲相关差异（都是本机头文件里看得见的）
 *   ① QStandardItemModel / QStandardItem 在 **QtGui**（本机 QtGui/qstandarditemmodel.h），
 *      Qt5 时代在 QtWidgets。include 写 <QStandardItemModel> 就别手写目录。
 *   ② 代理模型家族在 **QtCore**：qabstractproxymodel.h / qsortfilterproxymodel.h /
 *      qidentityproxymodel.h / qconcatenatetablesproxymodel.h / qtransposeproxymodel.h。
 *   ③ Qt::SortRole 在 Qt6 **已删除**（qnamespace.h 第 1517~1554 行那段里没有它），
 *      代理模型用 setSortRole(int) 显式指定，习惯上给 Qt::EditRole。
 *   ④ dataChanged 的第三参数是 const QList<int> &roles（第 360 行）。
 *   ⑤ QStandardItem::data/setData 的默认角色是 **Qt::UserRole + 1**（第 33/35 行），
 *      不是 DisplayRole：`item->setData(42)` 之后 `item->text()` 是空串。
 *      整数列尤其容易中这条——务必写清角色，或者第 2 讲自己实现 data()。
 *
 * ——— 老师傅的话 ———
 *   徒弟，这一讲我只要你记住一句口令：**「数据在模型里，视图只会问。」**
 *   你以前写控件是「往框里填字」，现在反过来是「框来问我字是什么」。方向一翻，
 *   以前想不通的事一下就通了：为什么改了数据不发信号界面不动（第 3 讲）、
 *   为什么 data() 里不许藏慢活（证据②）、为什么十万行的表格内存不炸（证据①那个
 *   children 数）、为什么代理模型不复制数据还能排序过滤（证据⑤）。
 *
 *   第二件事：**QModelIndex 是张纸片坐标，不是行李**。我见过有人把一屏 index 存进
 *   成员里，隔天用户删两行，程序拿到驴唇不对马嘴的数据——还不崩，「看起来一切正常」，
 *   这种 bug 最难查。证据③那个 doomed / guard 的对照你亲手跑一遍，
 *   以后就知道：跨增删用 QPersistentModelIndex，跨函数只存 row/column。
 *
 *   第三件事：五个角色里最容易被偷懒合并的是 Delegate 和 Model。
 *   「我就在这一格里画个进度条嘛，画的时候顺手算一下百分比不就完了」——
 *   于是画的时候算一遍、编辑的时候再算一遍、过滤的时候第三遍。记住：
 *   **格子的「值」属于模型，格子的「长相」属于委托**。百分比是值（存 0..100 的整数），
 *   把条子画出来才是长相。第 4 讲这条纪律要正面用上。
 *
 * ——— 小练习 ———
 *   1. 把证据②里 `view.viewport()->repaint()` 去掉，只留 scrollTo + flushEvents，
 *      看 data() 次数怎么变；再手动 `view.resize(300, 200)` 和 `view.resize(1200, 800)`
 *      各跑一次，把两次的「被问到行数」列成表，说明可见区如何直接决定调用次数。
 *   2. 证据⑤里把过滤列改成第 2 列（setFilterKeyColumn(2)）、过滤串填 "3"，
 *      看命中行为什么变了（整数被当字符串匹配）；再把 setFilterRole 改成
 *      Qt::EditRole、setDynamicSortFilter(false) 之后手动 sort，观察代理的两个开关。
 *   3. 把证据③的 removeRows 换成 model.clear()（reset 家族），对照
 *      guard.isValid() 与 view.selectionModel()->selectedRows() 的变化，
 *      回答：为什么 clear() 之后选择一定全空？（提示：modelReset 信号，
 *      qabstractitemmodel.h 第 378 行，还有第 3 讲会讲的 reset 丢滚动位置。）
 */
// ============================================================================

// 本文件没有 Q_OBJECT 类，因此不需要 #include "01_architecture.moc"。
