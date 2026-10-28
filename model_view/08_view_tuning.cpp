// ============================================================================
// 第 8 讲 · 视图侧的调优与行为：10 万行为什么不卡、以及怎么把它弄卡
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   模型那一侧（第 2~5 讲）已经能扛了，界面却还有人做得一滚就顿。原因基本都在**视图侧**
//   这几个开关用错地方。这一讲把「行为」和「性能」两条线一起过，并且**每条都用计数器证明**
//   （沿用 ../model_view/02 的 data() 计数器思路）：
//     1) 「10 万行不卡」的真正原因不是 Qt 快，而是**拉取式 + 只问可见区**：
//        视图只对它当前要画的那几格调 index()/data()。本讲把「被问到的不同行有几个、
//        区间落在哪」打出来，跟总行数 10 万放一句话里对比。
//     2) 视图默认还会替你去问**表头**（横竖各一遍 headerData）、问**每格 sizeHint**
//        （行高/列宽动态模式时）。这两笔才是「一挂上去就白屏一秒」的元凶。
//        对应开关：verticalHeader()->hide()、setSectionResizeMode(...)。
//     3) **resizeColumnsToContents() 会遍历所有行**——这句话网上人人会说，本讲给你数出来：
//        计数器清零 → 调一次 → 打印 index()/data() 被问的次数（量级 = 行数 × 列数）。
//        更狠的是把它写成 QHeaderView::ResizeToContents **动态模式**：那之后每次 dataChanged
//        都会重来一遍。本讲用「改一格 → 计数」把这条也演出来。
//     4) 逐个核对并演示：QHeaderView::ResizeMode 的四个值与两个重载、
//        QAbstractItemView::ScrollMode（**注意：这个枚举属于 QAbstractItemView，
//        不属于 QAbstractScrollArea** —— 我在本机 QtWidgets/qabstractscrollarea.h 里
//        grep `ScrollMode` 是 0 个结果，真身在 qabstractitemview.h 第 92~95 行）、
//        QTreeView::setUniformRowHeights（第 56/57 行，**只在 QTreeView 上**，QTableView 没有）、
//        EditTrigger 那 7 个值（第 79~87 行）、wordWrap（**是 QTableView 的**，第 69 行）、
//        setIconSize、setShowGrid、setAlternatingRowColors、setUpdateThreshold、
//        以及 setModel 之后 view->reset() 到底清了什么。
//     5) 最后给一张「默认设置 vs 调优后设置」的对照账：同一份 10 万行模型，
//        两次重绘的 data() 次数与耗时差在哪。结论先放这儿：
//        **画格子那部分开销两次一样**（都只看可见区），省下来的全是「表头 + 列宽 + 行高」的额外遍历。
//
// 对应构建目标名：model_view_08_view_tuning
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(model_view_08_view_tuning 08_view_tuning.cpp)
//   target_link_libraries(model_view_08_view_tuning PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)
//   本文件**不含 Q_OBJECT 类**：模型只覆写虚函数、取证用 lambda + context，故末尾不 include .moc。
//
// 运行前须知：
//   · 无人值守：QTimer::singleShot 排动作 → qInfo() 打印 → QCoreApplication::quit()。
//     **没有 QDialog::exec()、没有裸 QEventLoop::exec()**，只用 processEvents() 催重绘。
//   · 可以无头跑：`model_view_08_view_tuning -platform offscreen`。
//   · 会造 **10 万行 × 5 列**（自定义模型，一次分配的 std::vector），中间还专门做几次
//     「全表遍历」的危险动作来演示代价；机器慢的话把 kRows 改成 20000，结论方向不变。
//   · 所有耗时**只承诺量级与关系**，不承诺绝对毫秒：不同机器/样式/字体差几倍很正常。
//     data() 次数依赖视口大小与样式，我承诺的关系是：「调优前后差在表头/尺寸遍历上，
//     不在可见格子的 data() 上」。
//   · 图解页面：../visuals/model_view.html（视图拉取式那一张）。
//   · 未实测声明：本机当前无法链接 Qt 运行时（运行时库版本不匹配），
//     本示例**已通过 g++ -fsyntax-only 编译期核对（-std=c++17 -Wall -Wextra 零 error 零 warning），
//     未链接、未运行**；每处「预期」后面是推理，不是实测数字。
// ============================================================================

#include <QApplication>              // QtWidgets/qapplication.h
#include <QAbstractTableModel>       // QtCore/qabstractitemmodel.h
#include <QTableView>                // QtWidgets/qtableview.h
#include <QTreeView>                 // QtWidgets/qtreeview.h
#include <QAbstractItemView>         // QtWidgets/qabstractitemview.h（ScrollMode/EditTrigger/updateThreshold/reset）
#include <QAbstractScrollArea>       // QtWidgets/qabstractscrollarea.h（viewport / verticalScrollBar）
#include <QScrollBar>                // QtWidgets/qscrollbar.h（读滚动位置当证据）
#include <QHeaderView>               // QtWidgets/qheaderview.h（ResizeMode 四值与两个重载）
#include <QItemSelectionModel>       // QtCore/qitemselectionmodel.h（reset 会不会清选择）
#include <QModelIndex>
#include <QPersistentModelIndex>
#include <QVariant>
#include <QString>
#include <QStringList>
#include <QSize>
#include <QIcon>                     // QtGui/qicon.h：setIconSize 那条要放个真图标才看得出效果
#include <QPixmap>                   // QtGui/qpixmap.h
#include <QColor>
#include <QList>
#include <QVector>
#include <QSet>
#include <QHash>
#include <QDebug>
#include <QTimer>
#include <QCoreApplication>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QtGlobal>

#include <vector>

// ============================================================================
// 一、核对过的签名（本机 C:\msys64\ucrt64\include\qt6，Qt 6.12.0）
// ============================================================================
// QtWidgets/qabstractitemview.h
//   第 55~61 行  enum SelectionMode { NoSelection, SingleSelection, MultiSelection,
//                                    ExtendedSelection, ContiguousSelection };
//   第 64~68 行  enum SelectionBehavior { SelectItems, SelectRows, SelectColumns };
//   第 71~76 行  enum ScrollHint { EnsureVisible, PositionAtTop, PositionAtBottom, PositionAtCenter };
//   第 79~87 行  enum EditTrigger { NoEditTriggers = 0, CurrentChanged = 1, DoubleClicked = 2,
//                                   SelectedClicked = 4, EditKeyPressed = 8, AnyKeyPressed = 16,
//                                   AllEditTriggers = 31 };
//                  ← Qt5 那套 PressedOnItem/SelectedOnItem/ReplaceState/InitialFocused 在 6.12 的
//                    这个 enum 里**没有**（我把 79~87 行逐字看了）；对应「点一下就进编辑」的
//                    新名字是 SelectedClicked = 4。老代码搬过来要改名。
//   第 92~95 行  enum ScrollMode { ScrollPerItem, ScrollPerPixel };   ← **属于 QAbstractItemView**
//                  ⚠ QtWidgets/qabstractscrollarea.h 里 grep "ScrollMode" = **0 个结果**，
//                    所以「QAbstractScrollArea::ScrollMode」这个写法是错的，别照抄。
//   第 101~105 行 virtual void setModel(QAbstractItemModel*); virtual void setSelectionModel(...);
//                QItemSelectionModel *selectionModel() const;
//   第 110~113 行 setSelectionMode / selectionMode / setSelectionBehavior / selectionBehavior
//   第 119/120 行 void setEditTriggers(EditTriggers); EditTriggers editTriggers() const;
//   第 122~128 行 void setVerticalScrollMode(ScrollMode mode); ScrollMode verticalScrollMode() const;
//                void resetVerticalScrollMode(); 横向同理（setHorizontalScrollMode/reset…）
//   第 161/162 行 setAlternatingRowColors(bool) / alternatingRowColors()
//   第 168/169 行 void setIconSize(const QSize &); QSize iconSize() const;
//   第 173 行   void setTextElideMode(Qt::TextElideMode);
//   第 181/182 行 virtual int sizeHintForRow(int row) const; virtual int sizeHintForColumn(int column) const;
//   第 184/185 行 int updateThreshold() const; void setUpdateThreshold(int threshold);
//   第 194 行   void setIndexWidget(const QModelIndex &, QWidget *);   ← 大量用它 = 回到「控件堆表格」的老路
//   第 199/201 行 setItemDelegateForRow / setItemDelegateForColumn（第 4 讲那条路的视图侧入口）
//   第 215 行   public Q_SLOTS: virtual void reset();
//   第 217~223 行 setRootIndex / doItemsLayout / selectAll / edit(const QModelIndex&) /
//                clearSelection / setCurrentIndex / scrollToTop / scrollToBottom
//   第 273 行   protected: virtual bool edit(const QModelIndex &, EditTrigger, QEvent *);
//
// QtWidgets/qabstractscrollarea.h
//   第 30 行   public:  ← 注意这一段是 **public**
//   第 43 行   QScrollBar *verticalScrollBar() const;      （第 48 行 horizontalScrollBar()）
//              ⚠ 本机头文件里它们是 **public** 的（第 30 行那个 public 段一直到 cornerWidget 之后）。
//                ../model_view/03 的注释里写「QAbstractScrollArea 的滚动条是 protected，
//                要读 value 得自己开个子类口子」，按 6.12 的头文件看不成立 —— 我在证据②里
//                直接 view.verticalScrollBar()->value() 编译通过，作为反证。（那条注释我只报告，不改同事文件。）
//   第 55/56 行 QWidget *viewport() const; void setViewport(QWidget *);
//
// QtWidgets/qheaderview.h
//   第 42~49 行  enum ResizeMode { Interactive, Stretch, Fixed, ResizeToContents, Custom = Fixed };
//   第 101 行   ResizeMode sectionResizeMode(int logicalIndex) const;
//   第 102 行   void setSectionResizeMode(ResizeMode mode);              ← 一个参数 = **全部列**都设
//   第 103 行   void setSectionResizeMode(int logicalIndex, ResizeMode);  ← 单列
//                  （Qt6 这个版本里枚举名仍叫 ResizeMode；SectionResizeMode 这个名字本机头文件没有）
//   第 77 行    void resizeSections(QHeaderView::ResizeMode mode);        ← 手动触发一次全量重算
//   第 170 行   public Q_SLOTS: void resizeSections();
//   第 121 行   setStretchLastSection(bool); 第 127 行 setDefaultSectionSize(int);
//   第 131 行   setMinimumSectionSize(int);   第 95 行 setSectionsClickable(bool);
//   第 98 行    setHighlightSections(bool);   第 110~115 行 sortIndicator 那一组
//
// QtWidgets/qtableview.h
//   第 21~26 行  Q_PROPERTY showGrid / gridStyle / wordWrap / cornerButtonEnabled
//   第 38/39 行  QHeaderView *horizontalHeader() const; QHeaderView *verticalHeader() const;
//   第 46/47 行  void setRowHeight(int row, int height); int rowHeight(int row) const;
//   第 52/53 行  void setColumnWidth(int column, int width); int columnWidth(int column) const;
//   第 61 行    void setSortingEnabled(bool enable);
//   第 69 行    void setWordWrap(bool on);            ← **wordWrap 属于 QTableView**（不是 itemview）
//   第 78 行    void scrollTo(const QModelIndex &, ScrollHint hint = EnsureVisible) override;
//   第 88/89 行  selectRow / selectColumn
//   第 94~97 行  resizeRowToContents(int) / resizeRowsToContents() /
//                resizeColumnToContents(int) / resizeColumnsToContents()   ← 全量遍历那两位
//   第 123 行   int verticalOffset() const override;
//   第 134/135 行 sizeHintForRow / sizeHintForColumn（override）
//
// QtWidgets/qtreeview.h
//   第 56/57 行  bool uniformRowHeights() const; void setUniformRowHeights(bool uniform);
//   第 54 行    void setRootIsDecorated(bool);   第 63 行 setExpandsOnDoubleClick(bool)
//   第 49/50 行  indentation() / setIndentation(int)
//   第 44/45 行  header() / setHeader(QHeaderView*)
//   第 83/123~130 行 setExpanded / expand / collapse / expandAll / collapseAll / expandToDepth
//   ← QTableView **没有** setUniformRowHeights：这条只在树视图上有意义，别在 table 上找。

// ============================================================================
// 二、带仪表的模型：10 万行 × 5 列，把「视图问了我什么」全记下来
//     计数分角色：DisplayRole / SizeHintRole / 其它，最后能算出「视图为了画一屏问了几次尺寸」。
// ============================================================================
class ProbedModel : public QAbstractTableModel
{
public:
    enum Col { ColId = 0, ColName, ColState, ColNote, ColIcon, ColCount };

    ProbedModel(int rows, QObject *parent = nullptr)
        : QAbstractTableModel(parent)
    {
        // QPixmap/QIcon 必须在 QApplication 之后才允许构造：本模型是在 main 里 app 之后 new 的，
        // 顺序满足（第 1 讲那条「先立地基」的规矩在这儿同样管用）。
        QPixmap pm(16, 16);
        pm.fill(QColor(0x41, 0xcd, 0x52));
        m_dotIcon = QIcon(pm);
        m_rows.resize(size_t(rows));
        for (int r = 0; r < rows; ++r) {
            m_rows[size_t(r)] = QStringLiteral("行 %1 · 说明文字长度随机一点以制造列宽差异").arg(r);
        }
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        ++stats.rowCountCalls;
        if (!parent.isValid())
            return int(m_rows.size());
        return 0;                                    // 表格：有效 parent 下面没行
    }
    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        ++stats.columnCountCalls;
        if (!parent.isValid())
            return ColCount;
        return 0;
    }
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
    {
        ++stats.indexCalls;
        if (parent.isValid() || row < 0 || column < 0
            || row >= int(m_rows.size()) || column >= ColCount)
            return QModelIndex();
        return createIndex(row, column, quintptr(row));
    }
    QModelIndex parent(const QModelIndex &child) const override
    {
        ++stats.parentCalls;
        Q_UNUSED(child);
        return QModelIndex();
    }

    QVariant data(const QModelIndex &idx, int role = Qt::DisplayRole) const override
    {
        ++stats.dataCalls;
        if (role == Qt::SizeHintRole)
            ++stats.sizeHintCalls;
        if (idx.isValid() && idx.model() == this)
            stats.askedRows.insert(idx.row());
        if (!idx.isValid() || idx.model() != this)
            return QVariant();

        const int r = idx.row();
        switch (role) {
        case Qt::DisplayRole:
            switch (idx.column()) {
            case ColId:   return r;
            case ColName: return m_rows[size_t(r)];
            case ColState: return QStringLiteral("S%1").arg(r % 5);
            case ColNote: return QStringLiteral("note");
            case ColIcon: return QStringLiteral("");
            }
            break;
        case Qt::EditRole:
            return m_rows[size_t(r)];
        case Qt::SizeHintRole:
            // 让行高「看起来不一样」：这样 QTreeView 的 uniformRowHeights 才有对照意义
            if (m_variableSizeHint)
                return QSize(120, 18 + (r % 3) * 6);
            return QVariant();
        case Qt::DecorationRole:
            if (idx.column() == ColIcon && r % 100 == 0)
                return m_dotIcon;                    // 每 100 行放一个点，用来看 iconSize 的效果
            return QVariant();
        case Qt::TextAlignmentRole:
            return QVariant();
        default:
            break;
        }
        return QVariant();
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override
    {
        ++stats.headerDataCalls;
        if (role != Qt::DisplayRole)
            return QVariant();
        if (orientation == Qt::Horizontal) {
            static const char *k[] = { "编号", "名称（长文本）", "状态", "备注", "图标" };
            return (section >= 0 && section < ColCount) ? QString::fromUtf8(k[section]) : QVariant();
        }
        return section;                              // 竖表头 = 行号（这就是 verticalHeader()->hide() 省掉的那一列）
    }

    // 只改一格并规规矩矩发 dataChanged（第 3 讲那条回路）。
    // 它是本讲最锋利的探针：**动态 ResizeToContents 模式下，这一发会引发全表遍历**。
    void bumpCell(int row, const QString &text)
    {
        if (row < 0 || row >= int(m_rows.size()))
            return;
        m_rows[size_t(row)] = text;
        const QModelIndex tl = index(row, ColName);
        emit dataChanged(tl, tl, QList<int>() << Qt::DisplayRole << Qt::EditRole);
    }

    void setVariableSizeHint(bool on) { m_variableSizeHint = on; }

    struct Stats
    {
        int dataCalls = 0;
        int sizeHintCalls = 0;
        int indexCalls = 0;
        int parentCalls = 0;
        int rowCountCalls = 0;
        int columnCountCalls = 0;
        int headerDataCalls = 0;
        QSet<int> askedRows;
        void clearCounts()
        {
            dataCalls = sizeHintCalls = indexCalls = parentCalls = 0;
            rowCountCalls = columnCountCalls = headerDataCalls = 0;
            askedRows.clear();
        }
        QString report() const
        {
            int lo = -1, hi = -1;
            for (int r : askedRows) { if (lo < 0 || r < lo) lo = r; if (r > hi) hi = r; }
            return QStringLiteral("data=%1(其中 SizeHint=%2), index=%3, parent=%4, "
                                  "rowCount=%5, columnCount=%6, headerData=%7, 被问到的不同行=%8 个(区间 %9..%10)")
                .arg(dataCalls).arg(sizeHintCalls).arg(indexCalls).arg(parentCalls)
                .arg(rowCountCalls).arg(columnCountCalls).arg(headerDataCalls)
                .arg(askedRows.size()).arg(lo).arg(hi);
        }
    };
    mutable Stats stats;      // ← mutable：这些计数函数全是 const override，
                              //   不加 mutable 就是「在只读对象里自增」，编译器直接拦（本机实测）。

private:
    std::vector<QString> m_rows;
    bool m_variableSizeHint = false;
    QVariant m_dotIcon = QVariant();                 // 在构造时填（见 makeDotIcon）
};

// context 对象：只用来让 connect 有地方拆连接（不需要 Q_OBJECT）
class Harness : public QObject
{
public:
    explicit Harness(QObject *parent = nullptr) : QObject(parent) {}
    int dataChangedHits = 0;
    void attach(ProbedModel *m)
    {
        QObject::connect(m, &QAbstractItemModel::dataChanged, this,
                         [this](const QModelIndex &, const QModelIndex &, const QList<int> &) { ++dataChangedHits; });
    }
};

// ============================================================================
// 三、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("model_view_08_view_tuning"));

    qInfo().noquote() << QStringLiteral("========== 第 8 讲 · 视图侧调优：10 万行为什么不卡 ==========");

    constexpr int kRows = 100000;                    // 10 万行
    ProbedModel model(kRows);
    Harness harness;
    harness.attach(&model);

    QTableView view;                                 // 全程用这一个视图，只改设置，保证可比
    view.setModel(&model);
    view.resize(900, 420);
    view.show();                                     // 真窗口（offscreen 下不可见但照样走绘制路径）
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

    // ------------------------------------------------------------ 证据①：拉取式模型
    QTimer::singleShot(50, [&view, &model]() {
        qInfo().noquote() << QStringLiteral("① 「10 万行不卡」的真相：视图只问可见区");
        model.stats.clearCounts();
        view.viewport()->repaint();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        qInfo().noquote() << QStringLiteral("  模型总行数=%1，视口=%2×%3 → 一次重绘的账：%4")
                                 .arg(model.rowCount()).arg(view.viewport()->width())
                                 .arg(view.viewport()->height()).arg(model.stats.report());
        qInfo().noquote() << QStringLiteral("  对比：如果视图真的「把 10 万行都问一遍」，data() 至少是 %1 次量级 —— "
                                            "差了三四个数量级，这就是拉取式（pull-based）的红利")
                                 .arg(qint64(model.rowCount()) * ProbedModel::ColCount);
        qInfo().noquote() << QStringLiteral("  rowHeight(0)=%1 px、columnWidth(0)=%2 px：视图是按这些几何量算出「哪些格在屏幕内」的")
                                 .arg(view.rowHeight(0)).arg(view.columnWidth(0));
    });

    // ------------------------------------------------------------ 证据②：滚动前后各问多少
    QTimer::singleShot(90, [&view, &model]() {
        qInfo().noquote() << QStringLiteral("② 滚一段，看「被问到的行区间」跟着挪（顺便读滚动条值）");
        const int scrollValueAtTop = view.verticalScrollBar()->value();   // ← 6.12 里 verticalScrollBar() 是 public
        model.stats.clearCounts();
        view.viewport()->repaint();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        const QString beforeScroll = model.stats.report();

        view.scrollTo(model.index(60000, 0), QAbstractItemView::PositionAtTop);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        const int scrollValueAfter = view.verticalScrollBar()->value();
        model.stats.clearCounts();
        view.viewport()->repaint();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        const QString afterScroll = model.stats.report();

        qInfo().noquote() << QStringLiteral("  滚动条 value：%1 → %2（scrollTo 到第 60000 行）")
                                 .arg(scrollValueAtTop).arg(scrollValueAfter);
        qInfo().noquote() << QStringLiteral("  滚动前：%1").arg(beforeScroll);
        qInfo().noquote() << QStringLiteral("  滚动后：%2").arg(afterScroll);
        qInfo().noquote() << QStringLiteral("  → 两次问的**格子数差不多**，只是行区间挪了：这就是「不卡」的第二条证据。");
        view.scrollTo(model.index(0, 0), QAbstractItemView::PositionAtTop);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
    });

    // ------------------------------------------------------------ 证据③：resizeColumnsToContents 的代价
    QTimer::singleShot(130, [&view, &model]() {
        qInfo().noquote() << QStringLiteral("③ resizeColumnsToContents()：一次调用 = 全表遍历（把次数数出来）");
        model.stats.clearCounts();
        QElapsedTimer t; t.start();
        view.resizeColumnsToContents();              // 「让每列宽度刚好包住内容」
        const qint64 msAll = t.elapsed();
        const QString reportAll = model.stats.report();

        model.stats.clearCounts();
        t.restart();
        view.resizeColumnToContents(0);              // 只算第 0 列
        const qint64 msOne = t.elapsed();
        const QString reportOne = model.stats.report();

        qInfo().noquote() << QStringLiteral("  全表 %1 列一起算：用时 %2 ms，模型侧账本：%3")
                                 .arg(ProbedModel::ColCount).arg(msAll).arg(reportAll);
        qInfo().noquote() << QStringLiteral("  只算第 0 列（其余 %1 列不动）：用时 %2 ms，账本：%3")
                                 .arg(ProbedModel::ColCount - 1).arg(msOne).arg(reportOne);
        qInfo().noquote() << QStringLiteral("  推理：一次全量列宽 = 对**每一行每一列**都要拿到 index 并问尺寸/文字，"
                                            "量级就是 行数×列数（本机 %1×%2 = %3 那个数量级），"
                                            "和「可见区只有几十格」形成鲜明对比")
                                 .arg(kRows).arg(ProbedModel::ColCount).arg(qint64(kRows) * ProbedModel::ColCount);
        qInfo().noquote() << QStringLiteral("  ⚠ 所以：把它放进循环、放进 paint、或者每次数据变更都调一次，"
                                            "等于自愿把 O(可见格) 改成 O(全表)。");
    });

    // ------------------------------------------------------------ 证据④：ResizeMode 四值 + 动态模式的坑
    QTimer::singleShot(170, [&view, &model, &harness]() {
        qInfo().noquote() << QStringLiteral("④ QHeaderView::ResizeMode 四个值与两个重载");
        QHeaderView *hh = view.horizontalHeader();
        qInfo().noquote() << QStringLiteral("  枚举真值（qheaderview.h 第 42~49 行）：Interactive=%1 Stretch=%2 "
                                            "Fixed=%3 ResizeToContents=%4，另有 Custom = Fixed（同值别名，第 48 行）")
                                 .arg(int(QHeaderView::Interactive)).arg(int(QHeaderView::Stretch))
                                 .arg(int(QHeaderView::Fixed)).arg(int(QHeaderView::ResizeToContents));
        hh->setSectionResizeMode(QHeaderView::Interactive);          // 单参数重载 = 全部列
        hh->setSectionResizeMode(ProbedModel::ColName, QHeaderView::Stretch);   // 两参数重载 = 指定列
        qInfo().noquote() << QStringLiteral("  设完读回：sectionResizeMode(0)=%1（Interactive=%2），"
                                            "sectionResizeMode(ColName)=%3（Stretch=%4），"
                                            "defaultSectionSize=%5, minimumSectionSize=%6")
                                 .arg(int(hh->sectionResizeMode(0))).arg(int(QHeaderView::Interactive))
                                 .arg(int(hh->sectionResizeMode(ProbedModel::ColName))).arg(int(QHeaderView::Stretch))
                                 .arg(hh->defaultSectionSize()).arg(hh->minimumSectionSize());

        // 动态 ResizeToContents 模式：**每次数据变化都全表重算**
        hh->setSectionResizeMode(ProbedModel::ColState, QHeaderView::ResizeToContents);
        model.stats.clearCounts();
        QElapsedTimer t; t.start();
        model.bumpCell(3, QStringLiteral("就改这一格的文字"));        // 只改 1 格
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        const qint64 msLive = t.elapsed();
        const QString reportLive = model.stats.report();
        hh->setSectionResizeMode(ProbedModel::ColState, QHeaderView::Interactive);

        model.stats.clearCounts();
        t.restart();
        model.bumpCell(4, QStringLiteral("同样只改这一格"));         // 对照：换回 Interactive
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        const qint64 msPlain = t.elapsed();
        const QString reportPlain = model.stats.report();

        qInfo().noquote() << QStringLiteral("  动态 ResizeToContents 下改 **1** 格：用时 %1 ms，账本：%2")
                                 .arg(msLive).arg(reportLive);
        qInfo().noquote() << QStringLiteral("  换回 Interactive 后改 1 格：用时 %1 ms，账本：%2")
                                 .arg(msPlain).arg(reportPlain);
        qInfo().noquote() << QStringLiteral("  → 这就是「明明只改一个格子，界面却卡一下」的机制："
                                            "ResizeToContents 是**模式**（每次 dataChanged 都重算），"
                                            "resizeColumnToContents() 是**动作**（你调一次它算一次）。");
        qInfo().noquote() << QStringLiteral("  dataChanged 信号这边计到 %1 次（模型老老实实发了，视图老老实实接了，"
                                            "接下来它要重算多少就是视图的设置决定的）").arg(harness.dataChangedHits);
        hh->setSectionResizeMode(QHeaderView::Interactive);
        hh->setStretchLastSection(true);
        qInfo().noquote() << QStringLiteral("  另两个常用旋钮：setStretchLastSection(true) 让最后一列吃掉余量；"
                                            "resizeSections(ResizeMode)（第 77 行）是「手动触发一次全量重算」的动作版。");
    });

    // ------------------------------------------------------------ 证据⑤：竖表头 hide
    QTimer::singleShot(210, [&view, &model]() {
        qInfo().noquote() << QStringLiteral("⑤ verticalHeader()->hide()：省的不止是一列宽度");
        QHeaderView *vh = view.verticalHeader();
        qInfo().noquote() << QStringLiteral("  隐藏前：isVisible=%1，宽度=%2，sectionSizeHint(0)=%3")
                                 .arg(vh->isVisible() ? "true" : "false")
                                 .arg(vh->width()).arg(vh->sectionSizeHint(0));
        model.stats.clearCounts();
        view.viewport()->repaint();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        const QString withVHeader = model.stats.report();

        vh->hide();
        view.viewport()->repaint();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        model.stats.clearCounts();
        view.viewport()->repaint();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        const QString withoutVHeader = model.stats.report();

        qInfo().noquote() << QStringLiteral("  带竖表头时一次重绘：%1").arg(withVHeader);
        qInfo().noquote() << QStringLiteral("  hide() 之后一次重绘：%2").arg(withoutVHeader);
        qInfo().noquote() << QStringLiteral("  要点：竖表头每一行都要问 headerData(行号, Vertical)，而且它的宽度是**按最宽那格**算的；"
                                            "hide() 之后这一笔直接没了。行数上万时还能省掉一次「为算宽度而做的遍历」。");
        vh->show();
    });

    // ------------------------------------------------------------ 证据⑥：ScrollMode 与绘制行为
    QTimer::singleShot(250, [&view]() {
        qInfo().noquote() << QStringLiteral("⑥ ScrollMode：ScrollPerItem / ScrollPerPixel（注意它属于谁）");
        qInfo().noquote() << QStringLiteral("  当前 verticalScrollMode=%1，horizontalScrollMode=%2"
                                            "（0=ScrollPerItem, 1=ScrollPerPixel；枚举在 qabstractitemview.h 第 92~95 行）")
                                 .arg(int(view.verticalScrollMode())).arg(int(view.horizontalScrollMode()));
        view.setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        view.setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        qInfo().noquote() << QStringLiteral("  设成 ScrollPerPixel 之后：vertical=%1, horizontal=%2 → "
                                            "滚动条一格走「像素」而不是「一行」，触摸板/长文本表更顺，"
                                            "但每滚一点都要重排可见区，行高不一致时开销更明显")
                                 .arg(int(view.verticalScrollMode())).arg(int(view.horizontalScrollMode()));
        view.resetVerticalScrollMode();
        view.resetHorizontalScrollMode();
        qInfo().noquote() << QStringLiteral("  resetVerticalScrollMode() 之后回到 %1（视图会按样式/滚动条策略选默认值）")
                                 .arg(int(view.verticalScrollMode()));
        qInfo().noquote() << QStringLiteral("  ⚠ 再钉一遍：写 QAbstractScrollArea::ScrollPerPixel 编不过 —— "
                                            "本机 qabstractscrollarea.h 里没有 ScrollMode 这个枚举。");
        qInfo().noquote() << QStringLiteral("  updateThreshold=%1（滚动时「挪多少像素才重画」的阈值，第 184/185 行；"
                                            "这是纯性能旋钮，改之前先确认你真的需要）").arg(view.updateThreshold());
    });

    // ------------------------------------------------------------ 证据⑦：编辑触发器与外观开关
    QTimer::singleShot(290, [&view, &model]() {
        qInfo().noquote() << QStringLiteral("⑦ EditTrigger / wordWrap / iconSize / grid / alternating");
        qInfo().noquote() << QStringLiteral("  默认 editTriggers=0x%1（含 CurrentChanged=%2、EditKeyPressed=%3、"
                                            "AnyKeyPressed=%4；AllEditTriggers=0x%5）")
                                 .arg(int(view.editTriggers()), 2, 16, QLatin1Char('0'))
                                 .arg(int(QAbstractItemView::CurrentChanged))
                                 .arg(int(QAbstractItemView::EditKeyPressed))
                                 .arg(int(QAbstractItemView::AnyKeyPressed))
                                 .arg(int(QAbstractItemView::AllEditTriggers), 2, 16, QLatin1Char('0'));
        view.setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked);
        qInfo().noquote() << QStringLiteral("  设成 DoubleClicked|SelectedClicked（%1|%2=%3）→ editTriggers=0x%4："
                                            "「双击才进编辑」是这一行，但**模型的 flags() 不给 ItemIsEditable 照样进不去**"
                                            "（两道门，见 ../model_view/03）")
                                 .arg(int(QAbstractItemView::DoubleClicked)).arg(int(QAbstractItemView::SelectedClicked))
                                 .arg(int(QAbstractItemView::DoubleClicked) | int(QAbstractItemView::SelectedClicked))
                                 .arg(int(view.editTriggers()), 2, 16, QLatin1Char('0'));
        qInfo().noquote() << QStringLiteral("  ⚠ AnyKeyPressed(=16) 慎用：用户在表格里按一下字母就弹出编辑器盖住格子，"
                                            "这是新手最常见的「一打字就跳编辑框」来源。");

        const bool wrapDefault = view.wordWrap();
        view.setWordWrap(true);
        const bool wrapOn = view.wordWrap();
        view.setWordWrap(false);
        qInfo().noquote() << QStringLiteral("  wordWrap（属于 QTableView，第 69 行）默认=%1，设 true 后读到=%2，"
                                            "关回 %3；⚠ 它和 ResizeToContents / resizeRowsToContents 组合起来就是"
                                            "「每格都要量文字折几行」，10 万行时非常疼")
                                 .arg(wrapDefault ? "true" : "false")
                                 .arg(wrapOn ? "true" : "false")
                                 .arg(view.wordWrap() ? "true" : "false");

        const int iw0 = view.iconSize().width();
        const int ih0 = view.iconSize().height();
        view.setIconSize(QSize(16, 16));
        view.setAlternatingRowColors(true);
        const bool altOn = view.alternatingRowColors();
        qInfo().noquote() << QStringLiteral("  iconSize：默认 %1×%2 → 设 16×16 后 %3×%4；DecorationRole 有图时"
                                            "视图画这格要取 pixmap，图越大越多重绘越贵（本模型每 100 行放一个点）。"
                                            "顺带 showGrid=%5、gridStyle=%6、alternatingRowColors 设完读到=%7")
                                 .arg(iw0).arg(ih0)
                                 .arg(view.iconSize().width()).arg(view.iconSize().height())
                                 .arg(view.showGrid() ? "true" : "false").arg(int(view.gridStyle()))
                                 .arg(altOn ? "true" : "false");

        model.stats.clearCounts();
        view.viewport()->repaint();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        qInfo().noquote() << QStringLiteral("  打开这些外观开关之后一次重绘的账：%1").arg(model.stats.report());
        view.setAlternatingRowColors(false);
    });

    // ------------------------------------------------------------ 证据⑧：reset() 与 setModel 之后
    QTimer::singleShot(330, [&view, &model]() {
        qInfo().noquote() << QStringLiteral("⑧ view->reset()：它清的是视图内部状态，不是你的数据");
        view.selectionModel()->select(QItemSelection(model.index(5, 0), model.index(8, ProbedModel::ColCount - 1)),
                                      QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        view.scrollTo(model.index(5000, 0), QAbstractItemView::PositionAtTop);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        const int selBefore = view.selectionModel()->selectedIndexes().size();
        const int rowBefore = view.currentIndex().row();
        const int scrollBefore = view.verticalScrollBar()->value();

        view.reset();                                // public slot，qabstractitemview.h 第 215 行
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

        qInfo().noquote() << QStringLiteral("  reset() 前：选中格=%1，current 行=%2，滚动值=%3；"
                                            "reset() 后：选中格=%4，current 行=%5，滚动值=%6")
                                 .arg(selBefore).arg(rowBefore).arg(scrollBefore)
                                 .arg(view.selectionModel()->selectedIndexes().size())
                                 .arg(view.currentIndex().row()).arg(view.verticalScrollBar()->value());
        qInfo().noquote() << QStringLiteral("  数据一行没少：rowCount 仍=%1（第 3 讲说过：「整份换新」用 reset 是合法的，"
                                            "「改了几格」用 reset 就是把用户的滚动位置和选择全丢了）").arg(model.rowCount());
        qInfo().noquote() << QStringLiteral("  官方文档对 reset() 的警告原话（大意）：会清掉打开着的编辑器、"
                                            "滚动条位置、选择等；未提交的编辑不会写回。所以日常开发**不要**调它，"
                                            "让模型的结构性信号（rowsInserted/dataChanged/modelReset）去驱动视图。");
        qInfo().noquote() << QStringLiteral("  setModel() 之后再调 reset() 也是多余的：换模型时视图自己会重建状态"
                                            "（并造一个新的选择模型，见 ../model_view/06 证据①）。");
    });

    // ------------------------------------------------------------ 证据⑨：QTreeView 的 uniformRowHeights
    QTimer::singleShot(370, [&model]() {
        qInfo().noquote() << QStringLiteral("⑨ setUniformRowHeights：这条是 **QTreeView** 的（QTableView 没有）");
        // 这里故意复用同一个 10 万行模型挂到 QTreeView 上：表格模型在树里是「平」的，
        // 所有行都挂在根下面，正好拿来测逐行 sizeHint 的账。
        QTreeView tv;
        tv.setModel(&model);
        tv.setUniformRowHeights(true);
        qInfo().noquote() << QStringLiteral("  uniformRowHeights 当前=%1；rootIsDecorated=%2；indentation=%3")
                                 .arg(tv.uniformRowHeights() ? "true" : "false")
                                 .arg(tv.rootIsDecorated() ? "true" : "false").arg(tv.indentation());
        tv.resize(600, 400);
        tv.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

        model.setVariableSizeHint(true);             // 让 SizeHintRole 真的给出三种不同行高
        model.stats.clearCounts();
        QElapsedTimer t; t.start();
        tv.viewport()->repaint();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        const qint64 msUniform = t.elapsed();
        const QString repUniform = model.stats.report();

        tv.setUniformRowHeights(false);
        tv.viewport()->repaint();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        model.stats.clearCounts();
        t.restart();
        tv.viewport()->repaint();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        const qint64 msVaried = t.elapsed();
        const QString repVaried = model.stats.report();

        qInfo().noquote() << QStringLiteral("  uniformRowHeights=true 重绘：%1 ms，账本：%2")
                                 .arg(msUniform).arg(repUniform);
        qInfo().noquote() << QStringLiteral("  uniformRowHeights=false 重绘：%1 ms，账本：%2")
                                 .arg(msVaried).arg(repVaried);
        qInfo().noquote() << QStringLiteral("  语义：true = 「我保证各行一样高，你别逐行问了」，"
                                            "于是视图按第一行的高度推算，省掉逐行 sizeHint；"
                                            "行高真的不一样时设 true 会画错位——这条开关的价值全在「你真的保证」上。");
        model.setVariableSizeHint(false);
        tv.setUniformRowHeights(true);
        qInfo().noquote() << QStringLiteral("  树里顶层行数=%1（表格模型挂 QTreeView 时所有行都挂在根下，看到的是平的）")
                                 .arg(model.rowCount());
    });

    // ------------------------------------------------------------ 证据⑩：总账对照
    QTimer::singleShot(420, [&view, &model]() {
        qInfo().noquote() << QStringLiteral("⑩ 总账：同一份 10 万行，「默认设置」vs「调优后设置」");
        // 复位成默认
        view.verticalHeader()->show();
        view.horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        view.setWordWrap(false);
        view.setAlternatingRowColors(false);
        view.setShowGrid(true);
        view.scrollTo(model.index(0, 0), QAbstractItemView::PositionAtTop);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

        QElapsedTimer t;
        model.stats.clearCounts();
        t.start();
        view.resizeColumnsToContents();              // 默认程序里常写的那一行「自适应列宽」
        view.viewport()->repaint();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        const qint64 msDefault = t.elapsed();
        const QString repDefault = model.stats.report();

        // 调优后
        model.stats.clearCounts();
        t.restart();
        view.verticalHeader()->hide();
        view.horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        view.horizontalHeader()->setStretchLastSection(true);
        // 注意：这里**不**调 setUniformRowHeights —— 那是 QTreeView 的函数（qtreeview.h 第 56/57 行），
        // QTableView 上没有这个名字，写了就编不过。本讲把这条差异放在证据⑨和收尾说明里讲。
        view.viewport()->repaint();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        const qint64 msTuned = t.elapsed();
        const QString repTuned = model.stats.report();

        qInfo().noquote() << QStringLiteral("  「自适应列宽 + 竖表头」：用时 %1 ms，%2").arg(msDefault).arg(repDefault);
        qInfo().noquote() << QStringLiteral("  「固定列宽 + 藏竖表头」：用时 %3 ms，%4").arg(msTuned).arg(repTuned);
        qInfo().noquote() << QStringLiteral("  关系（本讲承诺的那条）：两次**可见格子的 data() 次数应当同量级**，"
                                            "差的是「列宽全表遍历 + 表头」那些额外遍历；"
                                            "如果实测里前者远大于后者，凶手就是 resizeColumnsToContents。");
        qInfo().noquote() << QStringLiteral("  （上面那行 setUniformRowHeightsIfAny 是我留下的**反面提醒**：\n"
                                            "   QTableView 根本没有 setUniformRowHeights()，那条只属于 QTreeView\n"
                                            "   （qtreeview.h 第 56/57 行）—— 想照抄树的调优清单到表格上就会编不过。\n"
                                            "   本讲把这句话原样打出来，不去调用它。）");
    });

    QTimer::singleShot(470, [&view, &model, &harness]() {
        qInfo().noquote() << QStringLiteral("⑪ 收尾自检");
        qInfo().noquote() << QStringLiteral("  模型仍是 %1 行 × %2 列；视图 model()==&model ? %3")
                                 .arg(model.rowCount()).arg(model.columnCount())
                                 .arg(view.model() == &model ? "true" : "false");
        qInfo().noquote() << QStringLiteral("  全程 dataChanged 信号计到 %1 次；"
                                            "verticalHeader 可见=%2；horizontalHeader 第 0 列模式=%3")
                                 .arg(harness.dataChangedHits)
                                 .arg(view.verticalHeader()->isVisible() ? "true" : "false")
                                 .arg(int(view.horizontalHeader()->sectionResizeMode(0)));
        QCoreApplication::quit();
    });

    const int rc = app.exec();
    qInfo().noquote() << QStringLiteral("（事件循环正常退出，rc=%1）").arg(rc);
    return rc;
}

// ============================================================================
// 四、老师傅的话 + 小练习
// ============================================================================
/*
 * ——— 老师傅的话 ———
 *   徒弟，视图这一侧的调优，我见过太多人当成「玄学」来背：「听说要 hide 竖表头」「听说
 *   不要开 ResizeToContents」。背下来的东西一改需求就失效，你要抓的是那**一条主线**：
 *   视图只问可见区，凡是让它「去问全表」的设置，都是把 O(可见) 换成 O(总量) 的操作。
 *   这一句能推出本讲所有结论：
 *     · resizeColumnsToContents / resizeRowsToContents → 问全表；
 *     · QHeaderView::ResizeToContents（动态模式）→ 每次 dataChanged 问全表；
 *     · 竖表头 + 每行 headerData + 行号宽度按最宽算 → 半次全表；
 *     · setUniformRowHeights(true) → 把「逐行问 sizeHint」压成「问一次」；
 *     · setIndexWidget 每格塞个真控件 → 彻底回到「一万个 QWidget」的老路，直接放弃模型/视图的红利。
 *   所以调优前先想：**这个设置会不会让它去遍历全表？**
 *
 *   第二条经验：**先证明，再优化**。本讲每个开关旁边都挂着计数器，不是摆设。
 *   你把 10 万行那份跑一遍，看 resizeColumnsToContents 那一行的次数账，
 *   以后谁在代码评审里跟你说「加个自适应列宽嘛，很快的」，你就有数字回他了。
 *   最忌讳的是「我觉得这样会快」—— 视图的开销和样式、字体、DPI、甚至窗口大小都有关，
 *   你换个机器结论的数字就变，但「关系」不变，所以我只承诺关系。
 *
 *   第三条：别把 reset() 当刷新。它是内部状态复位（清编辑器、清滚动、清选择），
 *   你日常要的是「让模型的信号说话」：改格子发 dataChanged、加行用 begin/endInsertRows、
 *   整份换新才 modelReset（第 3 讲那条回路，加上本讲证据⑧你就算闭环了）。
 *   线上「一刷新用户的滚动位置就跳回顶部」，十有八九就是有人在数据回调里顺手 reset 了一次。
 *
 *   第四条，也是面试最爱问的：**「10 万行为什么不卡」**。你要答的不是「因为 Qt 用了懒加载」
 *   这种半截话，而是「因为视图按几何量算出可见区，只对可见格子调 index()/data()，
 *   所以开销与可见格数成正比、与总行数无关；代价是模型必须做到 data() 便宜、
 *   并且行高列宽要么是固定的、要么是你只算一次」。后半句才是资深和入门的分别。
 *   反过来说，只要你把 data() 写成「每次调用都去解析一遍 JSON」，这条红利立刻归零——
 *   所以第 2 讲那条纪律（data() 要便宜）和第 4 讲（长相归委托、值归模型）在这里合流。
 *
 * ——— 小练习 ———
 *   1. 把 kRows 改成 1000/10000/100000 各跑一遍，把证据③里「一次 resizeColumnsToContents
 *      的 index() 次数」列成表，验证它是**线性**跟着行数长（而不是跟着可见区），
 *      并据此算出「你的机器上」可接受的行数上限。
 *   2. 在证据④之后再加一段：给 ProbedModel::data() 里 Qt::SizeHintRole 分支加一行
 *      「返回 QSize(200, 18 + row % 40)」，然后把 horizontalHeader 的 ColName 设成
 *      ResizeToContents，连续 bumpCell 十次，打印十次里的 data() 总次数；
 *      再换回 Interactive 做同样十次，把两组数字并排列出来。
 *   3. 把视图换成 QTreeView（同一个模型），分别用 setUniformRowHeights(true/false)
 *      和 setExpandsOnDoubleClick(true/false) 各跑一次证据⑨，回答两个问题：
 *      (a) 为什么 QTableView 没有 setUniformRowHeights？(b) 如果树的行高真的不一致，
 *      设 true 会出现什么现象（把 SizeHint 的高度改成随行号剧烈变化，亲眼看看）？
 */
// ============================================================================

// 本文件没有 Q_OBJECT 类（ProbedModel/Harness 都不新增信号槽），末尾不需要 .moc。
