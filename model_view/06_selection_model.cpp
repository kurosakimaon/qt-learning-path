// ============================================================================
// 第 6 讲 · QItemSelectionModel：「选中了哪些」这件事不归视图管
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   模型/视图五个角色里，Selection 这一层最容易被当成「视图的附属品」。
//   真相是：**选择状态是模型坐标系里的一份数据**，住在 QItemSelectionModel 这个
//   QObject 里，视图只是它的读写面板。抓住这一条，下面这些现象才解释得通：
//     1) 为什么表格换成树、或者同一份模型开两个窗口，选中行还在（甚至两个视图
//        会同时高亮）——因为它们共用同一个 selection model；反之，你在
//        view->setModel() 之后重建了选择模型，选择就没了（本讲把两种情况的
//        selectedIndexes().size() 都打出来）。
//     2) 为什么「选中 3 行 6 列」在内存里不是 18 个 index，而是**矩形范围**
//        QItemSelectionRange（一段 topLeft..bottomRight）；QItemSelection 就是
//        QList<QItemSelectionRange>（头文件第 179 行原文）。范围的两个角还是
//        **QPersistentModelIndex**（第 39/40 行），所以选择天生扛得住增删行。
//     3) select() 的第二参数 SelectionFlags 到底有哪些名字（本讲逐条打印值），
//        特别两个要澄清的点：
//        · **Qt6.12 的 SelectionFlag 里没有 CommandMask**。Qt4/Qt5 时代它有
//          （= 0x00FF，用来把「命令位」从「方式位」里掩出来），本机头文件
//          QtCore/qitemselectionmodel.h 第 107~118 行通读 + grep 过，没有这个名字，
//          所以本讲不写它——你在老书/老代码里看到别慌，直接改成显式写 Toggle 就行。
//        · selectionChanged 的信号参数**不带命令**（第 161 行只有 selected/deselected
//          两个 QItemSelection）。想知道「这次是 Ctrl 加选还是点了一下」，
//          得看视图侧：protected virtual selectionCommand(index, event)
//          （qabstractitemview.h 第 275 行）才是把键盘鼠标状态翻译成 flags 的地方。
//     4) QModelIndex 与 QPersistentModelIndex 的取舍：前者是「一次性快照」，
//        后者是「在模型里登记的活引用」。本讲给对照证据：插一行以后，裸 index 的
//        row() 还是老数字（数据已经换人了），持久 index 的 row() 自己 +1（数据没换）。
//        代价也打出来：构造/析构要登记注销、结构性变更时模型得更新全部登记项。
//   全篇数据都在内存现造，不读文件不联网。
//
// 对应构建目标名：model_view_06_selection_model
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(model_view_06_selection_model 06_selection_model.cpp)
//   target_link_libraries(model_view_06_selection_model PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)
//   本文件**不含 Q_OBJECT 类**：模型只覆写虚函数、不新增信号/槽；取证全部用 lambda 连，
//   lambda 作槽不需要接收方带 Q_OBJECT（要求的是**信号方**是 QObject，
//   QItemSelectionModel 当然是）。所以末尾**不**include 任何 .moc。
//
// 运行前须知：
//   · 无人值守：QTimer::singleShot 排动作 → qInfo() 打印 → QCoreApplication::quit()。
//     **没有 QDialog::exec()、没有裸 QEventLoop::exec()**，只用 processEvents() 催重绘。
//   · 可以无头跑：`model_view_06_selection_model -platform offscreen`。
//   · 模型规模故意做小（50 行 × 6 列），因为「Ctrl 加一列」的账要能手算对得上；
//     行数大了只是数字变大，关系不变。
//   · 打印的**范围个数**依赖 Qt 怎么归并矩形（不同版本可能不同），
//     我只承诺「格子数」这类可手算的量。
//   · 图解页面：../visuals/model_view.html（Selection 那一格 + 索引映射）。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 model_view_06_selection_model，跑到自动退出、退出码 0）。
//     但本讲**没有汇总自检行**，退出码 0 只证明「没崩、走完了」，不证明正文每个数字都对：
//     带具体数字的「预期」请以你自己那次运行输出为准（本机那一轮存档 runlogs/model_view_06_selection_model.log）。
// ============================================================================

#include <QApplication>              // QtWidgets/qapplication.h
#include <QAbstractTableModel>       // QtCore/qabstractitemmodel.h
#include <QItemSelectionModel>       // **QtCore**/qitemselectionmodel.h ← 注意不在 QtWidgets
#include <QItemSelectionRange>       // 同一个头；Qt6 没有独立的转发头可依赖，就按这个头来
#include <QModelIndex>
#include <QPersistentModelIndex>     // 同一个头（qabstractitemmodel.h）
#include <QTableView>                // QtWidgets/qtableview.h
#include <QTreeView>                 // QtWidgets/qtreeview.h（讲「换视图选择还在」要用第二个视图）
#include <QAbstractItemView>         // QtWidgets/qabstractitemview.h（SelectionMode/Behavior/selectionCommand）
#include <QHeaderView>               // QtWidgets/qheaderview.h
#include <QVariant>
#include <QString>
#include <QStringList>
#include <QList>
#include <QVector>
#include <QSet>
#include <QDebug>
#include <QTimer>
#include <QCoreApplication>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QtGlobal>

#include <vector>

// ============================================================================
// 一、核对过的签名（本机 D:\Qt\6.12.0\mingw_64\include，QtCore 6.12.0）
// ============================================================================
// QtCore/qitemselectionmodel.h
//   第 18 行    class Q_CORE_EXPORT QItemSelectionRange
//   第 21~23 行 QItemSelectionRange() = default;
//               QItemSelectionRange(const QModelIndex &topL, const QModelIndex &bottomR);
//               explicit QItemSelectionRange(const QModelIndex &index);
//   第 31~37 行 int top()/left()/bottom()/right()/width()/height()   （全部 inline const）
//   第 39/40 行 inline const QPersistentModelIndex &topLeft() const;  …bottomRight() const;
//               ↑↑ 返回类型是**持久索引的 const 引用**，这就是「选择扛得住增删」的根
//   第 44~49 行 bool contains(const QModelIndex &) const / bool contains(int,int,const QModelIndex&) const
//   第 65 行    bool isValid() const
//   第 73 行    QModelIndexList indexes() const;
//   第 89 行    class Q_CORE_EXPORT QItemSelectionModel : public QObject
//   第 107~118 行 enum SelectionFlag {
//                 NoUpdate = 0x0000, Clear = 0x0001, Select = 0x0002, Deselect = 0x0004,
//                 Toggle = 0x0008, Current = 0x0010, Rows = 0x0020, Columns = 0x0040,
//                 SelectCurrent = Select | Current, ToggleCurrent = Toggle | Current,
//                 ClearAndSelect = Clear | Select }
//   第 120 行    Q_DECLARE_FLAGS(SelectionFlags, SelectionFlag)   ← 所以能用 operator|
//   第 121 行    Q_FLAG(SelectionFlags)
//   第 131~136 行 bool isSelected(const QModelIndex&) const / isRowSelected(int row, const QModelIndex &parent = QModelIndex()) const
//                isColumnSelected(...) / rowIntersectsSelection(...) / columnIntersectsSelection(...)
//   第 139~142 行 QModelIndexList selectedIndexes() const / selectedRows(int column = 0) const /
//                selectedColumns(int row = 0) const / const QItemSelection selection() const
//   第 144/145 行 const QAbstractItemModel *model() const;  QAbstractItemModel *model();
//   第 150~157 行 public Q_SLOTS: virtual void setCurrentIndex(const QModelIndex&, SelectionFlags);
//                virtual void select(const QModelIndex&, SelectionFlags);
//                virtual void select(const QItemSelection&, SelectionFlags);
//                virtual void clear(); virtual void reset(); void clearSelection();
//                virtual void clearCurrentIndex();
//   第 160~165 行 Q_SIGNALS: selectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
//                currentChanged(const QModelIndex &current, const QModelIndex &previous);
//                currentRowChanged / currentColumnChanged / modelChanged(QAbstractItemModel*)
//   第 167~169 行 protected: emitSelectionChanged(const QItemSelection&, const QItemSelection&)
//   第 179 行    class QItemSelection : public QList<QItemSelectionRange>
//   第 183 行    QItemSelection(const QModelIndex &topLeft, const QModelIndex &bottomRight);
//   第 187~192 行 void select(const QModelIndex &topLeft, const QModelIndex &bottomRight);
//                bool contains(const QModelIndex &) const;  QModelIndexList indexes() const;
//                void merge(const QItemSelection&, SelectionFlags);  static void split(...)
//   ← **grep 结果：CommandMask 在本文件里不存在**（Qt5 才有）。别写，写了编不过。
//
// QtWidgets/qabstractitemview.h
//   第 55~61 行  enum SelectionMode { NoSelection, SingleSelection, MultiSelection,
//                                     ExtendedSelection, ContiguousSelection };
//   第 64~68 行  enum SelectionBehavior { SelectItems, SelectRows, SelectColumns };
//   第 104/105 行 virtual void setSelectionModel(QItemSelectionModel*); QItemSelectionModel *selectionModel() const;
//   第 220 行    public Q_SLOTS: void clearSelection(); void selectAll();(第 218 行)
//   第 275 行    protected: virtual QItemSelectionModel::SelectionFlags selectionCommand(
//                            const QModelIndex &index, const QEvent *event = nullptr) const;
// QtWidgets/qtableview.h
//   第 88/89 行  void selectRow(int row);  void selectColumn(int column);   ← public slot
// QtCore/qabstractitemmodel.h
//   第 4~56 行   class QPersistentModelIndex：ctor(const QModelIndex&)、row()/column()/data()
//                /isValid()/parent()/sibling() 全 const；operator QModelIndex() 可转回去

// ============================================================================
// 二、模型：50 行 × 6 列，格子值就写成 "r%1c%2"，一眼能认出「哪格」
//     另外提供 insertRows/removeRows 的口子——持久索引的证据必须靠增删行才能演出来。
// ============================================================================
class GridModel : public QAbstractTableModel
{
public:
    enum Col { ColName = 0, ColState, ColQty, ColOwner, ColTag, ColNote, ColCount };

    explicit GridModel(int rows, QObject *parent = nullptr)
        : QAbstractTableModel(parent)
    {
        m_rows.reserve(rows);
        for (int r = 0; r < rows; ++r)
            m_rows.push_back(QStringLiteral("R%1").arg(r));
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : int(m_rows.size());
    }
    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : ColCount;
    }
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.isValid() || row < 0 || column < 0
            || row >= int(m_rows.size()) || column >= ColCount)
            return QModelIndex();
        return createIndex(row, column, quintptr(row));
    }
    QModelIndex parent(const QModelIndex &child) const override
    {
        Q_UNUSED(child);
        return QModelIndex();
    }
    QVariant data(const QModelIndex &idx, int role = Qt::DisplayRole) const override
    {
        if (role != Qt::DisplayRole || !idx.isValid() || idx.model() != this)
            return QVariant();
        return QStringLiteral("%1C%2").arg(m_rows[size_t(idx.row())]).arg(idx.column());
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override
    {
        if (role != Qt::DisplayRole)
            return QVariant();
        if (orientation == Qt::Horizontal) {
            static const char *k[] = { "名称", "状态", "数量", "负责人", "标签", "备注" };
            if (section >= 0 && section < ColCount)
                return QString::fromUtf8(k[section]);   // ★ 中文表头：必须 fromUtf8，fromLatin1 会乱码
        }
        return section;
    }

    // 增删行（正规三步：begin → 改容器 → end）
    void insertRowAt(int row)
    {
        if (row < 0 || row > int(m_rows.size()))
            return;
        beginInsertRows(QModelIndex(), row, row);
        m_rows.insert(m_rows.begin() + row, QStringLiteral("NEW%1").arg(row));
        endInsertRows();
    }
    void removeRowAt(int row)
    {
        if (row < 0 || row >= int(m_rows.size()))
            return;
        beginRemoveRows(QModelIndex(), row, row);
        m_rows.erase(m_rows.begin() + row);
        endRemoveRows();
    }
    QString labelAt(int row) const
    {
        if (row < 0 || row >= int(m_rows.size()))
            return QStringLiteral("<越界>");
        return m_rows[size_t(row)];
    }

private:
    std::vector<QString> m_rows;     // 每行一个「名字」，格子的显示值是 "R%1C%2" 现拼
};

// ============================================================================
// 三、取证用的记录器：把 selectionChanged / currentChanged 收到的东西原样打出来
//     它继承 QObject 但**不加 Q_OBJECT**：它不定义任何信号/槽，只是当 connect 的
//     context（上下文对象）用——context 只负责「它一死就把连接拆掉」这件事，
//     不需要元对象表。判据请记牢：**有新信号/新槽/新 Q_PROPERTY 才要 Q_OBJECT**。
//     （拆连接的机制在 ../metaobject/04_lambda_and_lifetime.cpp 里专门讲过。）
// ============================================================================
struct SignalLog : QObject
{
    int selectionChangedHits = 0;
    int currentChangedHits = 0;
    int lastSelectedRanges = -1;
    int lastDeselectedRanges = -1;
    int lastSelectedCells = -1;
    QStringList history;          // 每次 selectionChanged 的摘要

    void attachTo(QItemSelectionModel *sm)
    {
        // 信号参数只有 (selected, deselected)：**没有命令**。命令在调用方/视图里，不在信号里。
        QObject::connect(sm, &QItemSelectionModel::selectionChanged, this,
                         [this](const QItemSelection &selected, const QItemSelection &deselected) {
                             ++selectionChangedHits;
                             lastSelectedRanges = selected.size();
                             lastDeselectedRanges = deselected.size();
                             lastSelectedCells = selected.indexes().size();
                             history << QStringLiteral("  #%1 selected 范围数=%2(格=%3), deselected 范围数=%4")
                                            .arg(selectionChangedHits)
                                            .arg(selected.size())
                                            .arg(selected.indexes().size())
                                            .arg(deselected.size());
                         });
        QObject::connect(sm, &QItemSelectionModel::currentChanged, this,
                         [this](const QModelIndex &current, const QModelIndex &previous) {
                             ++currentChangedHits;
                             history << QStringLiteral("  currentChanged: (%1,%2) ← (%3,%4)，valid=%5/%6")
                                            .arg(current.row()).arg(current.column())
                                            .arg(previous.row()).arg(previous.column())
                                            .arg(current.isValid() ? "Y" : "N")
                                            .arg(previous.isValid() ? "Y" : "N");
                         });
    }

    static QString describeRanges(const QItemSelection &sel, int limit = 4)
    {
        QStringList parts;
        int n = 0;
        for (const QItemSelectionRange &r : sel) {
            if (n >= limit) { parts << QStringLiteral("…"); break; }
            parts << QStringLiteral("[%1,%2 → %3,%4]")
                         .arg(r.topLeft().row()).arg(r.topLeft().column())
                         .arg(r.bottomRight().row()).arg(r.bottomRight().column());
            ++n;
        }
        return parts.join(QStringLiteral(" "));
    }
};

// ============================================================================
// 四、main：证据一条条来
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("model_view_06_selection_model"));

    qInfo().noquote() << QStringLiteral("================ 第 6 讲 · QItemSelectionModel / QItemSelection ================");

    GridModel model(50);
    // 自己造选择模型，并把所有权留在自己手里（parent = &app）。
    //   Qt 官方文档对 setSelectionModel 有一句：「if you call setModel() after this
    //   function, the given selectionModel will be replaced by one created by the view」；
    //   对 setModel 有：「This function will create and set a new selection model …
    //   However, the old selection model will not be deleted」。
    //   两处「谁负责 delete」文档没写死到让我能复述，本例就不猜：sm 由 app 收养、活到最后，
    //   视图只是共用它。真要换模型/换视图，请看证据①里那三步指针比对。
    QItemSelectionModel *sm = new QItemSelectionModel(&model, &app);
    SignalLog mainLog; mainLog.attachTo(sm);

    QTableView viewA;
    QTreeView viewB;                       // 第二个视图：证明「选择不在视图里」
    QTableView viewC;                      // 第三个视图：证明「新视图自己造的那个 sm 是空的」
    viewA.setModel(&model);
    viewA.setSelectionBehavior(QAbstractItemView::SelectItems);   // 先按格子选，别让它自己扩成整行
    viewA.setSelectionMode(QAbstractItemView::ExtendedSelection);
    viewA.setSelectionModel(sm);           // ← 把视图自带的那个换成我们自己的
    viewB.setModel(&model);                // QTreeView 拿同一个模型：它自己造了一个**新的**选择模型
    QItemSelectionModel *smB = viewB.selectionModel();
    SignalLog logB; logB.attachTo(smB);

    viewA.resize(760, 420);
    viewB.resize(420, 420);
    viewA.show();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    // ------------------------------------------------------------------ 证据①
    QTimer::singleShot(40, &app, [&]() {
        qInfo().noquote() << QStringLiteral("① 选择是**模型侧**的状态，不是视图的附属品");
        const QItemSelection rect(model.index(2, 0), model.index(4, model.columnCount() - 1));
        sm->select(rect, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        qInfo().noquote() << QStringLiteral("  对 sm 选 2..4 行之后：selectedIndexes=%1（手算 3 行 × %2 列 = %3）")
                                 .arg(sm->selectedIndexes().size())
                                 .arg(model.columnCount())
                                 .arg(3 * model.columnCount());
        qInfo().noquote() << QStringLiteral("  viewA.selectionModel() 就是 sm ? %1（视图只是它的读写面板）")
                                 .arg(viewA.selectionModel() == sm ? "true" : "false");
        qInfo().noquote() << QStringLiteral("  此刻 viewB 自己造的那个 smB：selectedIndexes=%1（**空的**，另一个对象）、"
                                            "指针 %2 vs %3")
                                 .arg(smB->selectedIndexes().size())
                                 .arg(QString::number(qintptr(smB), 16))
                                 .arg(QString::number(qintptr(sm), 16));
        // 关键一步：把 A 的选择模型塞给 B —— 这就是「同一份选择、两个视图」的做法
        viewB.setSelectionModel(sm);
        qInfo().noquote() << QStringLiteral("  viewB->setSelectionModel(sm) 之后：viewB=%1 格、viewA=%2 格 → "
                                            "两边同步（切换视图选择还在，就是因为共用同一个对象）")
                                 .arg(viewB.selectionModel()->selectedIndexes().size())
                                 .arg(viewA.selectionModel()->selectedIndexes().size());
        // 再挂一个新视图：它 setModel 时自己造 sm，选择是空的
        viewC.setModel(&model);
        const int cBefore = viewC.selectionModel()->selectedIndexes().size();
        const bool cSameObject = (viewC.selectionModel() == sm);
        viewC.setSelectionModel(sm);
        qInfo().noquote() << QStringLiteral("  新视图 viewC.setModel(&model) → 它的 selectionModel 是%1对象，"
                                            "selectedIndexes=%2；把 sm 也给它 → %3")
                                 .arg(cSameObject ? "同一个" : "另一个")
                                 .arg(cBefore)
                                 .arg(viewC.selectionModel()->selectedIndexes().size());
        qInfo().noquote() << QStringLiteral("  smB（被 viewB 换掉的那个）**没被 delete**：%1（读得出来就证明它还活着）")
                                 .arg(smB->model() == &model ? "model() 仍指向 &model" : "model() 变了");
        qInfo().noquote() << QStringLiteral("  文档原话提醒：先 setSelectionModel 再 setModel，视图会换成它自己造的"
                                            "新选择模型 → 这就是「重挂模型后选择没了」的机制（不是数据丢了）。");
    });

    // ------------------------------------------------------------------ 证据②
    QTimer::singleShot(80, &app, [&]() {
        qInfo().noquote() << QStringLiteral("② SelectionFlag 逐个打印真值（抄自 qitemselectionmodel.h 第 107~118 行）");
        struct { const char *name; int v; } kFlags[] = {
            { "NoUpdate",       int(QItemSelectionModel::NoUpdate) },
            { "Clear",          int(QItemSelectionModel::Clear) },
            { "Select",         int(QItemSelectionModel::Select) },
            { "Deselect",       int(QItemSelectionModel::Deselect) },
            { "Toggle",         int(QItemSelectionModel::Toggle) },
            { "Current",        int(QItemSelectionModel::Current) },
            { "Rows",           int(QItemSelectionModel::Rows) },
            { "Columns",        int(QItemSelectionModel::Columns) },
            { "SelectCurrent",  int(QItemSelectionModel::SelectCurrent) },
            { "ToggleCurrent",  int(QItemSelectionModel::ToggleCurrent) },
            { "ClearAndSelect", int(QItemSelectionModel::ClearAndSelect) },
        };
        for (const auto &f : kFlags)
            qInfo().noquote() << QStringLiteral("  %1 = 0x%2").arg(QString::fromLatin1(f.name))
                                     .arg(f.v, 4, 16, QLatin1Char('0'));
        qInfo().noquote() << QStringLiteral("  组合值核对：ClearAndSelect == Clear|Select ? %1，"
                                            "SelectCurrent == Select|Current ? %2")
                                 .arg(QItemSelectionModel::ClearAndSelect == QItemSelectionModel::SelectionFlags(
                                          QItemSelectionModel::Clear | QItemSelectionModel::Select) ? "true" : "false")
                                 .arg(QItemSelectionModel::SelectCurrent == QItemSelectionModel::SelectionFlags(
                                          QItemSelectionModel::Select | QItemSelectionModel::Current) ? "true" : "false");
        qInfo().noquote() << QStringLiteral("  【注意】 本机 6.12 的头文件里**没有 CommandMask**（Qt5 有，值 0x00FF）。"
                                            "要「命令语义」就显式写 Select/Deselect/Toggle，别去抄老代码。");
        qInfo().noquote() << QStringLiteral("  【注意】 Rows/Columns 是「方式位」，Select/Toggle/Clear 是「命令位」，"
                                            "它们用 | 拼成一个 SelectionFlags 传进 select()。");
    });

    // ------------------------------------------------------------------ 证据③
    QTimer::singleShot(120, &app, [&]() {
        qInfo().noquote() << QStringLiteral("③ 程序化写出「鼠标选了 2..4 行，再 Ctrl 点第 3 列」的等价操作");
        SignalLog log; log.attachTo(sm);
        const int cellCount = model.columnCount();

        // 第一步：ClearAndSelect + Rows → 清掉旧选择，把 2..4 行整行选中
        sm->select(QItemSelection(model.index(2, 0), model.index(4, cellCount - 1)),
                  QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        const int afterStep1 = sm->selectedIndexes().size();
        qInfo().noquote() << QStringLiteral("  步骤1 ClearAndSelect|Rows(2..4) → 格子数=%1（推理 3×%2=%3），"
                                            "本次 selectionChanged hits=%4，selected 范围数=%5")
                                 .arg(afterStep1).arg(cellCount).arg(3 * cellCount)
                                 .arg(log.selectionChangedHits).arg(log.lastSelectedRanges);

        // 第二步：Toggle + Columns → 相当于 Ctrl 点第 ColQty 列：
        //         原来选中的（2..4 行那几格）被取消，其余行的该列被加上
        const int col = GridModel::ColQty;
        sm->select(QItemSelection(model.index(0, col), model.index(model.rowCount() - 1, col)),
                  QItemSelectionModel::Toggle | QItemSelectionModel::Columns);
        const int afterStep2 = sm->selectedIndexes().size();
        const int expected = afterStep1 - 3 + (model.rowCount() - 3);
        qInfo().noquote() << QStringLiteral("  步骤2 Toggle|Columns(第 %1 列整列) → 格子数=%2，"
                                            "手算应为 %3 - 3 + (%4 - 3) = %5")
                                 .arg(col).arg(afterStep2).arg(afterStep1).arg(model.rowCount())
                                 .arg(expected);
        qInfo().noquote() << QStringLiteral("  范围列表（QItemSelection 就是 QList<QItemSelectionRange>）：%1")
                                 .arg(SignalLog::describeRanges(sm->selection()));
        qInfo().noquote() << QStringLiteral("  记录器：selectionChanged 共 %1 次，最后一次 selected 范围=%2 / "
                                            "deselected 范围=%3 / selected 格子=%4")
                                 .arg(log.selectionChangedHits).arg(log.lastSelectedRanges)
                                 .arg(log.lastDeselectedRanges).arg(log.lastSelectedCells);
        // 对照：视图自带的 selectRow 走的是 selectionBehavior/Mode，而不是你手写的 flags
        viewA.selectRow(7);
        qInfo().noquote() << QStringLiteral("  对照 viewA->selectRow(7)（ExtendedSelection 下按「整行 + 不清旧选择?」"
                                            "由视图内部算 flags）→ 格子数=%1，第 7 行整行选中=%2")
                                 .arg(sm->selectedIndexes().size())
                                 .arg(sm->isRowSelected(7) ? "true" : "false");
        qInfo().noquote() << QStringLiteral("  第 %1 列是否与选择集相交=%2，第 2 行相交=%3，"
                                            "第 2 行是否**全部**选中=%4")
                                 .arg(col).arg(sm->columnIntersectsSelection(col) ? "true" : "false")
                                 .arg(sm->rowIntersectsSelection(2) ? "true" : "false")
                                 .arg(sm->isRowSelected(2) ? "true" : "false");
        qInfo().noquote() << QStringLiteral("  selectedRows(0).size()=%1，selectedColumns(ColName).size()=%2"
                                            "（这两个只给「一个代表格」，别拿它们当全量遍历）")
                                 .arg(sm->selectedRows(0).size()).arg(sm->selectedColumns(GridModel::ColName).size());
    });

    // ------------------------------------------------------------------ 证据④
    QTimer::singleShot(160, &app, [&]() {
        qInfo().noquote() << QStringLiteral("④ QItemSelectionRange：矩形 + 两个角是**持久**索引");
        sm->select(QItemSelection(model.index(10, 1), model.index(12, 3)),
                  QItemSelectionModel::ClearAndSelect);
        const QItemSelection sel = sm->selection();
        qInfo().noquote() << QStringLiteral("  范围数=%1，摘要=%2").arg(sel.size())
                                 .arg(SignalLog::describeRanges(sel));
        const QItemSelectionRange r = sel.isEmpty() ? QItemSelectionRange() : sel.first();
        qInfo().noquote() << QStringLiteral("  topLeft=(%1,%2) bottomRight=(%3,%4)，width=%5 height=%6，"
                                            "面积=%7，indexes().size()=%8")
                                 .arg(r.topLeft().row()).arg(r.topLeft().column())
                                 .arg(r.bottomRight().row()).arg(r.bottomRight().column())
                                 .arg(r.width()).arg(r.height())
                                 .arg(r.width() * r.height()).arg(r.indexes().size());
        qInfo().noquote() << QStringLiteral("  contains(11,2)=%1，contains(20,2)=%2（按矩形判断，不是逐格比对）")
                                 .arg(r.contains(model.index(11, 2)) ? "true" : "false")
                                 .arg(r.contains(model.index(20, 2)) ? "true" : "false");
        // 选择扛得住增删：插一行到最前面，看 topLeft 的 row 自己变
        const int rowBefore = r.topLeft().row();
        const QString cellBefore = r.topLeft().data().toString();
        model.insertRowAt(0);
        const QItemSelection sel2 = sm->selection();
        const int rowAfter = sel2.isEmpty() ? -1 : sel2.first().topLeft().row();
        qInfo().noquote() << QStringLiteral("  model.insertRows(0,1) 之后：范围左上角行号 %1 → %2（**+1 自动跟随**），"
                                            "该格值 %3 → %4（值没换，说明跟着数据走）")
                                 .arg(rowBefore).arg(rowAfter).arg(cellBefore)
                                 .arg(sel2.isEmpty() ? QStringLiteral("<空>") : sel2.first().topLeft().data().toString());
        qInfo().noquote() << QStringLiteral("  原因就在头文件第 39/40 行：topLeft()/bottomRight() 的返回类型是 "
                                            "const QPersistentModelIndex & —— 范围天生是活的引用。");
    });

    // ------------------------------------------------------------------ 证据⑤
    QTimer::singleShot(200, &app, [&]() {
        qInfo().noquote() << QStringLiteral("⑤ QModelIndex vs QPersistentModelIndex：什么时候必须用后者");
        sm->clear();                                  // 先清干净，免得干扰
        const QModelIndex plain = model.index(5, GridModel::ColName);
        const QPersistentModelIndex guard(plain);      // 登记一个活引用
        const QString valueBefore = plain.data().toString();
        qInfo().noquote() << QStringLiteral("  建立两个快照：plain=(row %1,col %2) 显示=%3；guard=(row %4) 显示=%5")
                                 .arg(plain.row()).arg(plain.column()).arg(valueBefore)
                                 .arg(guard.row()).arg(guard.data().toString());
        model.insertRowAt(0);                         // 在第 0 行前面插一行 → 原来的第 5 行现在是第 6 行
        qInfo().noquote() << QStringLiteral("  insertRows(0,1) 之后：");
        qInfo().noquote() << QStringLiteral("    plain.row()=%1（**没变，还是老坐标**），plain.data()=%2 "
                                            "→ 已经指到新插入的那一行，数据换人了")
                                 .arg(plain.row()).arg(plain.data().toString());
        qInfo().noquote() << QStringLiteral("    guard.row()=%1（自己 +1 了），guard.data()=%2 → 仍指向同一份数据")
                                 .arg(guard.row()).arg(guard.data().toString());
        qInfo().noquote() << QStringLiteral("    模型侧标签对照：第5行=%1，第6行=%2")
                                 .arg(model.labelAt(5)).arg(model.labelAt(6));
        qInfo().noquote() << QStringLiteral("  sizeof(QModelIndex)=%1 字节，sizeof(QPersistentModelIndex)=%2 字节"
                                            "（前者是坐标快照，后者还要在模型里登记）")
                                 .arg(sizeof(QModelIndex)).arg(sizeof(QPersistentModelIndex));
        model.removeRowAt(6);                          // 把 guard 指的那一行删掉
        qInfo().noquote() << QStringLiteral("  删掉 guard 指向的那一行之后：guard.isValid()=%1，plain.isValid()=%2"
                                            "（plain 一直「有效」，只是指向别的东西——这才是最阴的）")
                                 .arg(guard.isValid() ? "true" : "false")
                                 .arg(plain.isValid() ? "true" : "false");
        // 代价：结构性变更时模型要把所有登记的持久索引更新一遍 → 数量大了 insertRows 变贵
        const int probeRows = model.rowCount();
        QElapsedTimer t1; t1.start();
        for (int i = 0; i < probeRows; ++i) {
            QModelIndex p = model.index(i, 0);         // 只造裸坐标（局部即用即弃）
            Q_UNUSED(p);
        }
        const qint64 costPlain = t1.elapsed();
        QElapsedTimer t2; t2.start();
        {
            QVector<QPersistentModelIndex> alive;
            alive.reserve(probeRows);
            for (int i = 0; i < probeRows; ++i)
                alive.append(QPersistentModelIndex(model.index(i, 0)));
            const qint64 costGuard = t2.elapsed();
            QElapsedTimer t3; t3.start();
            model.insertRowAt(0);                      // 有 probeRows 个活登记时插入一行
            qInfo().noquote() << QStringLiteral("  代价对照：造 %1 个 QModelIndex 用时 %2 ms；"
                                                "造 %1 个 QPersistentModelIndex 用时 %3 ms；"
                                                "带着 %1 个活登记再做一次 insertRows 用时 %4 ms")
                                     .arg(probeRows).arg(costPlain).arg(costGuard).arg(t3.elapsed());
            int stillValid = 0;
            for (const QPersistentModelIndex &g : std::as_const(alive))
                if (g.isValid()) ++stillValid;
            qInfo().noquote() << QStringLiteral("  插入后仍有效的登记项=%1/%2（插入不会让索引失效，只是行号整体后移）")
                                     .arg(stillValid).arg(alive.size());
        }
        qInfo().noquote() << QStringLiteral("  经验：一次调用之内用 QModelIndex 就好；要跨增删/跨事件"
                                            "（异步回调里「用户当时选的那一行」）才升级为 QPersistentModelIndex。");
    });

    // ------------------------------------------------------------------ 证据⑥
    QTimer::singleShot(240, &app, [&]() {
        qInfo().noquote() << QStringLiteral("⑥ 两个槽的分工与「命令从哪来」");
        SignalLog log; log.attachTo(sm);
        sm->clear();
        const int afterClear = sm->selectedIndexes().size();
        const QModelIndex curAfterClear = sm->currentIndex();
        qInfo().noquote() << QStringLiteral("  clear() → 格子数=%1，currentIndex.isValid()=%2（clear 把选择和当前都清）")
                                 .arg(afterClear).arg(curAfterClear.isValid() ? "true" : "false");
        sm->select(QItemSelection(model.index(1, 1), model.index(1, 2)), QItemSelectionModel::ClearAndSelect);
        sm->setCurrentIndex(model.index(9, 3), QItemSelectionModel::NoUpdate);
        qInfo().noquote() << QStringLiteral("  setCurrentIndex(9,3, NoUpdate) → 格子数=%1（选择**没动**，"
                                            "NoUpdate=0x0000 就是「只改当前、不碰选择」），current=(%2,%3)")
                                 .arg(sm->selectedIndexes().size())
                                 .arg(sm->currentIndex().row()).arg(sm->currentIndex().column());
        sm->setCurrentIndex(model.index(20, 0), QItemSelectionModel::SelectCurrent);
        qInfo().noquote() << QStringLiteral("  setCurrentIndex(20,0, SelectCurrent) → 格子数=%1，"
                                            "第 20 行第 0 列 isSelected=%2")
                                 .arg(sm->selectedIndexes().size())
                                 .arg(sm->isSelected(model.index(20, 0)) ? "true" : "false");
        qInfo().noquote() << QStringLiteral("  期间记录器收到的事件（注意每条都**没有**命令参数）：");
        for (const QString &line : std::as_const(log.history))
            qInfo().noquote() << line;
        qInfo().noquote() << QStringLiteral("  → 想知道「这次是 Ctrl 还是 Shift」，信号里没有；"
                                            "视图是用 protected virtual selectionCommand(index, event)"
                                            "（qabstractitemview.h 第 275 行）把按键状态翻译成 flags 的。");
        sm->clearSelection();
        qInfo().noquote() << QStringLiteral("  clearSelection() → 格子数=%1，current 仍有效=%2（它不动当前索引）")
                                 .arg(sm->selectedIndexes().size())
                                 .arg(sm->currentIndex().isValid() ? "true" : "false");
        sm->clearCurrentIndex();
        qInfo().noquote() << QStringLiteral("  clearCurrentIndex() → current.isValid()=%1，格子数=%2（反向：只动当前）")
                                 .arg(sm->currentIndex().isValid() ? "true" : "false")
                                 .arg(sm->selectedIndexes().size());
        qInfo().noquote() << QStringLiteral("  累计：本讲对 sm 的 selectionChanged 一共记到 %1 次，"
                                            "currentChanged %2 次").arg(mainLog.selectionChangedHits)
                                 .arg(mainLog.currentChangedHits);
    });

    // ------------------------------------------------------------------ 证据⑦
    QTimer::singleShot(280, &app, [&]() {
        qInfo().noquote() << QStringLiteral("⑦ 挂上视图：选择如何影响绘制（以及第 5 讲的坐标系问题）");
        sm->select(QItemSelection(model.index(3, 0), model.index(6, model.columnCount() - 1)),
                  QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        viewA.scrollTo(model.index(3, 0), QAbstractItemView::EnsureVisible);
        viewA.viewport()->repaint();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        qInfo().noquote() << QStringLiteral("  选中 3..6 行 → selectedRows(0).size()=%1，selectedIndexes=%2，"
                                            "hasSelection=%3，viewB 看到的格子数=%4")
                                 .arg(sm->selectedRows(0).size()).arg(sm->selectedIndexes().size())
                                 .arg(sm->hasSelection() ? "true" : "false")
                                 .arg(viewB.selectionModel()->selectedIndexes().size());
        qInfo().noquote() << QStringLiteral("  视图画每一格时是回到同一个 sm->isSelected(index) 问「该不该涂选中色」，"
                                            "所以共用 sm 的两个视图高亮必然一致。");
        qInfo().noquote() << QStringLiteral("  （提醒）视图挂**代理**时，selection 里存的是代理坐标，"
                                            "落到源数据必须 mapSelectionToSource —— 见 ../model_view/05 证据⑥。");
    });

    QTimer::singleShot(340, &app, [&]() {
        qInfo().noquote() << QStringLiteral("⑧ 收尾自检：三个「一定能复现」的事实");
        qInfo().noquote() << QStringLiteral("  1) viewA.selectionModel() == viewB.selectionModel() ? %1"
                                            "（同一个对象才叫共用）")
                                 .arg(viewA.selectionModel() == viewB.selectionModel() ? "true" : "false");
        qInfo().noquote() << QStringLiteral("  2) sm->model() == &model ? %1 → 选择模型认的是**模型**，不是视图")
                                 .arg(sm->model() == &model ? "true" : "false");
        const int beforeAll = sm->selectedIndexes().size();
        viewA.selectAll();
        const int rowsAfterAll = sm->selectedRows(0).size();
        sm->clear();
        const int afterClear = sm->selectedIndexes().size();
        qInfo().noquote() << QStringLiteral("  3) 起始格子数=%1；selectAll → selectedRows=%2（应等于行数 %3）；"
                                            "clear → 格子数=%4")
                                 .arg(beforeAll).arg(rowsAfterAll).arg(model.rowCount()).arg(afterClear);
        qInfo().noquote() << QStringLiteral("  全程 sm 上记录到的事件：selectionChanged %1 次 / currentChanged %2 次")
                                 .arg(mainLog.selectionChangedHits).arg(mainLog.currentChangedHits);
        QCoreApplication::quit();
    });

    const int rc = app.exec();
    qInfo().noquote() << QStringLiteral("（事件循环正常退出，rc=%1）").arg(rc);
    return rc;
}

// ============================================================================
// 七、老师傅的话 + 小练习
// ============================================================================
/*
 * ——— 老师傅的话 ———
 *   徒弟，选择这一层，我给你把一句话钉在墙上：**选中了什么，是数据，不是界面**。
 *   它在 QItemSelectionModel 这个 QObject 里，跟着模型走。所以：
 *     · 同一份模型开三个窗口，想同步高亮就 setSelectionModel 共用一个 sm，不用自己广播；
 *     · 你调 view->setModel() 重挂一次，选择就没了——因为你换的是**另一个 sm 对象**，
 *       不是「视图把选择擦了」；
 *     · 后台线程算完一批结果，想「把命中的行标出来」，直接对 sm 下手，跟界面开没开无关。
 *   很多人写「先取 selectedIndexes()，再弹个确认框，确认后按原来的 index 处理」，
 *   中间那段时间里模型加了行——就错位了。证据⑤那个 plain/guard 对照就是这个事故：
 *   裸 index 的 row() 一直是 5，看起来「有效」，但指的东西已经换了，它不会给你报错。
 *   要么当场把要用的值抄出来（拷贝 row/column/data 的整数和字符串），要么升级为持久索引。
 *
 *   第二件要说的是 flags。你别背，你只要抓住三层：
 *     · 命令位：Clear（先清空）/ Select（加）/ Deselect（减）/ Toggle（反）；
 *     · 是否顺带动当前项：带 Current 的那几个组合（SelectCurrent/ToggleCurrent）；
 *     · 范围怎么解释：Rows/Columns 告诉它「我给的这几个 index 代表整行/整列」。
 *   鼠标键盘那套 Ctrl/Shift 语义，是视图在 selectionCommand() 里现场算 flags 的，
 *   你程序化操作时就照这三层自己拼。Qt5 书里那种 CommandMask 的写法在 6.12 里编不过，
 *   别硬试。
 *
 *   第三件：QItemSelection 是矩形范围**列表**，不是一堆格子。你手工构造的时候给两个角，
 *   它内部就这么存；selectedIndexes() 才是展开的格子。行数列数大时这两个量的差距非常大——
 *   所以别拿 selectedIndexes() 的 size 去和「你点了几个格子」对账，也别拿它当遍历工具
 *   （几百万格子的选择集，展开就是一次几百万个 index 的构造）。要遍历就用 selection() 的范围，
 *   一行一行处理，别把 indexes() 存下来反复用。
 *
 *   最后：selectionChanged 的参数只有新旧两个选择集，**没有命令**、没有「谁触发的」。
 *   你在槽里想知道是用户点的还是你自己代码改的，只能自己加标记（本讲就是这么在调用点
 *   打印的）。别去信号里找，找不到。
 *
 * ——— 小练习 ———
 *   1. 把证据③的第二步分别改成 Deselect|Columns 和 Select|Columns，把三次打印的
 *      「格子数」列成表，验证 Toggle/Select/Deselect 在「已经选中一部分」的选择集上的差别。
 *   2. 给 viewA 子类化，覆写 protected virtual selectionCommand(const QModelIndex &, const QEvent *)，
 *      让它永远返回 ClearAndSelect（即「禁用 Ctrl 加选」），再程序化 select() 一次，
 *      回答：为什么程序化 select() 不受你的覆写影响？（提示：覆写只在视图处理鼠标事件时被调用。）
 *   3. 造一个 1 万行的模型，把 viewA 的选择集存成 QItemSelection 并持有（别展开），
 *      然后 insertRows 一次；分别计时「存 QItemSelection 后用 selectedIndexes() 展开一次」
 *      和「一开始就展开并存成 QModelIndexList」两种做法的耗时，体会范围与展开的代价差。
 */
// ============================================================================

// 本文件没有 Q_OBJECT 类（GridModel 只覆写虚函数；SignalLog 是普通结构体），
// 所以末尾不需要 #include "06_selection_model.moc"。
