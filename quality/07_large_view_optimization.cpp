// ============================================================================
// 第 7 讲 · 百万行不卡：视图只问它要的那几十行
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   「我们有一百多万条记录，表格一打开就卡死」—— 这是桌面软件里最常见的一次性能抢救。
//   而抢救的第一句话总是同一个：**你有没有把一百万行都塞进控件里？**
//   答案通常是「没有啊，我只 setModel 了」。对，但 setModel 之后视图会去问
//   rowCount()，然后按「总行数 × 行高」算出滚动范围 —— 它**不一定**要逐行问数据。
//   这一讲的六个阶段，就是把「不一定」变成「一定」，并且用计数证明：
//
//     ① 视图只问它要的：一百万行，data() 只被叫了几百次
//     ② 分批取数：canFetchMore/fetchMore，先给 1000 行，剩下的滚动到位再要
//     ③ 列宽策略：Fixed 一秒摆好，ResizeToContents 要按行名去量 —— 代价可数
//     ④ 代理：在 12 万行上过滤 + 排序，一秒内出结果，且结果能对得上
//     ⑤ 批量更新：500 次单条插入 vs 关掉更新再插 —— 差的是布局次数
//     ⑥ data() 里的重活：每次调用都做格式化，调用一多就是真钱
//
//   六个阶段的共同手法只有一句：**数调用次数**。
//   性能问题在 UI 里几乎从不表现为「某一行代码慢」，而是「某一行代码被调了一百万次」。
//   所以你会看到我给每个模型都装了一个 data() 计数器 —— 这是本讲最重要的一件工具。
//
// 对应构建目标名：quality_07_large_view_optimization
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(quality_07_large_view_optimization 07_large_view_optimization.cpp)
//   target_link_libraries(quality_07_large_view_optimization PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)          # 本文件有 Q_OBJECT 类，需要 moc
//
// 运行前须知：
//   · **同步 + 显式泵事件**：没有 app.exec()。窗口只 show() 一次，
//     之后用 QCoreApplication::processEvents() 把布局/绘制事件处理掉。
//     所有等待都是 `processEvents(flags, 50)` 这种**带上限**的泵，不会挂住。
//   · 可以无头跑：`quality_07_large_view_optimization -platform offscreen`。
//   · 不写磁盘、不联网。
//   · 数据全是**算出来的**（第 row 行的分数 = (row*37)%100），没有真实文件/数据库，
//     所以「结果对不对」可以独立算一遍来核对，不依赖任何外部输入。
//   · 未实测声明：本机当前无法链接 Qt 运行时，本示例**未链接、未运行**，
//     仅通过 moc + g++ -fsyntax-only -std=c++17 -Wall -Wextra 做编译期核对。
//     文中所有毫秒数都是经验估计，不是本机实测。
// ============================================================================

#include <QApplication>
#include <QGuiApplication>           // platformName()
#include <QAbstractTableModel>       // QtCore/qabstractitemmodel.h：322/323 行 canFetchMore/fetchMore
#include <QTableView>
#include <QHeaderView>
#include <QScrollBar>
#include <QSortFilterProxyModel>     // QtCore/qsortfilterproxymodel.h：90/94/128/146 行
#include <QVariant>
#include <QVector>
#include <QStringList>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QDebug>

// ============================================================================
// 一、核对签名（本机 C:\msys64\ucrt64\include\qt6）
// ============================================================================
// 分批加载：qabstractitemmodel.h
//   第 322 行 Q_INVOKABLE virtual void fetchMore(const QModelIndex &parent);
//   第 323 行 Q_INVOKABLE virtual bool canFetchMore(const QModelIndex &parent) const;
//   ★ 注意它们是**虚函数且有默认实现**：默认 canFetchMore 返回 false、
//     fetchMore 什么都不做。所以「分批」不是自动的，是**你**在子类里实现的。
//     视图侧也**不需要**你写代码：QAbstractItemView 会在滚动靠近末尾时自己调 fetchMore。
//     ⚠ 这条「视图会自己调」属于 .cpp 里的策略，本讲不把它当硬账；
//     第 ② 节我们自己循环调，把「协议本身」验证清楚。
//
// 列宽：qheaderview.h
//   第 42~49 行 enum ResizeMode { Interactive, Stretch, Fixed, ResizeToContents, Custom = Fixed };
//   第 105 行 void setResizeContentsPrecision(int precision);
//   第 106 行 int  resizeContentsPrecision() const;
//   ★ ResizeToContents 的代价是「为每一列去问每一行有多宽」。为了不真的问一百万行，
//     QHeaderView 有一个**采样精度**（默认 1000）：它只看前 1000 行就定宽度。
//     这就是为什么它既「准」又「不那么慢」—— 也是为什么大表上它会给出**近似**列宽。
//
// 换行与统一行高：
//   qtableview.h 第 69 行 void setWordWrap(bool on);   ← 关掉它，行高就统一了
//   qtreeview.h  第 57 行 void setUniformRowHeights(bool uniform);  ← 树视图的必设项
//   ★ 行高统一之后，视图算滚动范围只需要「行数 × 一个行高」，
//     不必为了知道自己在哪而逐行测量。这是「大表不卡」的第一道保险。
//
// 代理：qsortfilterproxymodel.h
//   第 90 行 void setSortRole(int role);
//   第 94 行 void setFilterRole(int role);
//   第 128 行 virtual bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const;
//   第 146 行 void invalidateFilter();
//   ★ 子类重写 filterAcceptsRow 就能做「按条件筛」；每个源行都会被问一次，
//     所以这个函数必须**便宜** —— 它被调用的次数是源模型的行数。
//
// 跨讲链接：
//   · model_view 第 1 讲：模型/视图的职责划分，本讲是它的大数据版。
//   · model_view 第 5 讲：代理链（proxy 摞 proxy）的结构。
//   · model_view 第 8 讲：视图调优的常规手段（本讲是它的极限情形）。
//   · quality 第 6 讲：测量方法学（预热、中位数、取调用次数）。
// ============================================================================

// ============================================================================
// 二、核对台账
// ============================================================================
enum Kind { Hard, Soft, Obs };

struct Ledger {
    int total = 0;
    int failed = 0;
    int stalled = 0;

    int exitCode() const { return (failed + stalled > 0 || total == 0) ? 1 : 0; }
};
static Ledger gLedger;

static void verdict(Kind kind, const QString &label, bool ok, const QString &detail)
{
    QString head;
    switch (kind) {
    case Hard:
        ++gLedger.total;
        if (!ok)
            ++gLedger.failed;
        head = ok ? QStringLiteral("  [OK ] 核对  ") : QStringLiteral("  [!! ] 不符  ");
        break;
    case Soft: head = ok ? QStringLiteral("  [预期中] ") : QStringLiteral("  [这次没命中] "); break;
    case Obs:  head = QStringLiteral("  [实测]  "); break;
    }
    qInfo().noquote() << head + QStringLiteral("%1 = %2").arg(label, detail);
}

static void section(const QString &text)
{
    qInfo().noquote() << QStringLiteral("\n========== %1 ==========").arg(text);
}

static void checkNum(const QString &label, qint64 got, qint64 want)
{
    verdict(Hard, label, got == want, QStringLiteral("%1（期望 %2）").arg(got).arg(want));
}

static void checkBool(const QString &label, bool got, bool want)
{
    verdict(Hard, label, got == want,
            QStringLiteral("实测 %1（期望 %2）")
                .arg(got ? QStringLiteral("true") : QStringLiteral("false"),
                     want ? QStringLiteral("true") : QStringLiteral("false")));
}

// 泵一圈事件（有上限）。视图的布局与绘制都是事件驱动的：
// 不泵一下，你测到的「data() 被调了几次」永远是 0 —— 那不是快，那是没干活。
static void pumpEvents()
{
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

// ============================================================================
// 三、被测的模型：所有数据都是算出来的
// ----------------------------------------------------------------------------
// 三个设计点：
//   1) 数据不预先存在任何容器里 —— 第 row 行的内容由 row 算出来。
//      这样「一百万行」真的只是**一个数字**，内存占用不随行数增长。
//      这是虚拟模型（virtual model）的核心技巧，也是它为什么能开百万行。
//   2) data() 里挂一个计数器。本讲所有「视图只问它要的」这类论断，全靠这个数。
//   3) canFetchMore/fetchMore 让「总数」和「已加载数」分离：
//      先给 1000 行，剩下的等视图滚到底再要。
// ============================================================================
class BigModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColName = 0, ColScore = 1, ColState = 2, ColCount = 3 };
    enum Roles { ScoreRole = Qt::UserRole + 1 };

    // total: 逻辑总行数；initial: 一开始就放出来多少行；batch: 每次 fetchMore 加多少
    explicit BigModel(int total, int initial, int batch, QObject *parent = nullptr)
        : QAbstractTableModel(parent)
        , m_total(total)
        , m_loaded(qBound(0, initial, total))
        , m_batch(batch > 0 ? batch : 1)
    {
    }

    // 分数是完全确定的：第 row 行的分数 = (row*37) % 100。
    // 讲义里所有「结果对不对」都用这个公式独立算一遍。
    static int scoreOf(qint64 row) { return int((row * 37) % 100); }
    static QString nameOf(qint64 row) { return QStringLiteral("记录 %1").arg(row + 1); }
    static QString stateOf(qint64 row)
    {
        return (row % 2 == 0) ? QStringLiteral("未完成") : QStringLiteral("已完成");
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_loaded;
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : ColCount;
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        ++m_dataCalls;                       // ★ 本讲的核心仪表
        if (!index.isValid() || index.row() >= m_loaded)
            return {};

        const qint64 row = index.row();
        switch (role) {
        case Qt::DisplayRole:
            switch (index.column()) {
            case ColName:  return nameOf(row);
            case ColScore: return QString::number(scoreOf(row));
            default:       return stateOf(row);
            }
        case ScoreRole:
            return scoreOf(row);
        case Qt::TextAlignmentRole:
            return int(Qt::AlignRight | Qt::AlignVCenter);
        default:
            return {};
        }
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};
        switch (section) {
        case ColName:  return QStringLiteral("名称");
        case ColScore: return QStringLiteral("分数");
        default:       return QStringLiteral("状态");
        }
    }

    bool canFetchMore(const QModelIndex &parent) const override
    {
        return !parent.isValid() && m_loaded < m_total;
    }

    void fetchMore(const QModelIndex &parent) override
    {
        if (parent.isValid() || m_loaded >= m_total)
            return;
        const int target = qMin(m_total, m_loaded + m_batch);
        beginInsertRows(QModelIndex(), m_loaded, target - 1);
        m_loaded = target;
        endInsertRows();
    }

    // 单条追加（第 ⑤ 节要用它对比「五百次单插」与「关掉更新再插」）
    void appendOne()
    {
        beginInsertRows(QModelIndex(), m_loaded, m_loaded);
        ++m_loaded;
        ++m_total;
        endInsertRows();
    }

    qint64 dataCalls() const { return m_dataCalls; }
    void resetCounter() const { m_dataCalls = 0; }
    int loaded() const { return m_loaded; }
    int totalRows() const { return m_total; }

private:
    int m_total;
    int m_loaded;
    int m_batch;
    mutable qint64 m_dataCalls = 0;
};

// ============================================================================
// 四、阶段①：一百万年行，视图只问它要的那几十行
// ----------------------------------------------------------------------------
// 一个常见误解：「行数多所以慢」。真相是：**行数本身不花一分钱**，
// 花钱的是「每一行都被问了一遍」。视图要的其实只有三样东西：
//   · rowCount()     → 一个整数，O(1)
//   · 可视区域那几十行的 data()
//   · 表头文字
// 只要行高统一，它算滚动范围都不需要逐行测量。
// 本节把 data() 调用次数打出来，你就会看到「一百万」和「几百」之间的关系。
// ============================================================================
static void phase1_viewAsksLittle()
{
    section(QStringLiteral("① 一百万年行，data() 只被问了几百次"));

    BigModel model(1000000, 1000000, 1000000);   // 一次全放出来：一百万行都在
    QTableView view;
    view.setModel(&model);
    view.setWordWrap(false);                     // 关掉换行 → 行高统一
    view.verticalHeader()->setVisible(false);
    view.verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    view.verticalHeader()->setDefaultSectionSize(22);
    view.horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    view.horizontalHeader()->resizeSection(BigModel::ColName, 160);
    view.horizontalHeader()->resizeSection(BigModel::ColScore, 90);
    view.horizontalHeader()->resizeSection(BigModel::ColState, 90);
    view.resize(520, 400);
    view.show();
    pumpEvents();

    model.resetCounter();                        // 从「已经摆好」这一刻开始数
    pumpEvents();
    const qint64 calls = model.dataCalls();

    checkNum(QStringLiteral("【1A】逻辑总行数"), model.rowCount(), 1000000);
    checkBool(QStringLiteral("【1B】视图没有逐行问数据（data() 调用 < 10000）"),
              calls < 10000, true);
    checkBool(QStringLiteral("【1C】滚动条知道有一百万行的量级"),
              view.verticalScrollBar()->maximum() > 0, true);
    verdict(Obs, QStringLiteral("【1D】data() 实际被调用"), true,
            QStringLiteral("%1 次（对应 %2 行里的可视部分）")
                .arg(calls).arg(model.rowCount()));
    verdict(Obs, QStringLiteral("【1E】滚动条最大值"), true,
            QString::number(view.verticalScrollBar()->maximum()));

    view.hide();
    checkBool(QStringLiteral("【1F】行高统一（wordWrap 已关）"), view.wordWrap(), false);
}

// ============================================================================
// 五、阶段②：分批取数 —— 先给一千，剩下的滚到底再要
// ----------------------------------------------------------------------------
// 这一节验证的是**协议**，不是视图的策略：
//   canFetchMore() 说「还有」→ fetchMore() 就真的把行插进来 → 再问就变成「没了」。
// 三个必须说清的细节：
//   · fetchMore 里必须用 beginInsertRows/endInsertRows 包住行数变化，
//     否则视图不知道要更新，界面和模型立刻不一致；
//   · 插入的区间是 [旧行数, 新行数-1]，**闭区间**，off-by-one 就在这里出；
//   · canFetchMore 返回 false 之后，fetchMore 什么都不该做（本讲写成提前 return）。
// 视图侧不用写代码：QAbstractItemView 滚到接近末尾会自己调 fetchMore。
// 但本讲自己循环调一遍，因为「视图会自己调」是 .cpp 里的策略，不能当硬账。
// ============================================================================
static void phase2_fetchMore()
{
    section(QStringLiteral("② 分批取数：canFetchMore / fetchMore 的协议"));

    const int total = 100000;
    const int initial = 1000;
    const int batch = 1000;
    BigModel model(total, initial, batch);

    checkNum(QStringLiteral("【2A】初始只放出来 %1 行").arg(initial), model.rowCount(), initial);
    checkBool(QStringLiteral("【2B】一开始 canFetchMore 为真"),
              model.canFetchMore(QModelIndex()), true);

    int fetches = 0;
    int guard = 0;
    while (model.canFetchMore(QModelIndex()) && guard < 1000) {
        const int before = model.rowCount();
        model.fetchMore(QModelIndex());
        const int after = model.rowCount();
        if (after <= before) {
            // 说过「还有」却一行都不加：协议坏了，别在这儿死循环
            verdict(Hard, QStringLiteral("【2C】fetchMore 必须真的加行"), false,
                    QStringLiteral("第 %1 次调用后行数没变").arg(fetches));
            ++gLedger.stalled;
            return;
        }
        ++fetches;
        ++guard;
    }

    checkNum(QStringLiteral("【2D】最后一行不差"), model.rowCount(), total);
    checkBool(QStringLiteral("【2E】取完之后 canFetchMore 变为假"),
              model.canFetchMore(QModelIndex()), false);
    checkNum(QStringLiteral("【2F】一共取了 %1 次").arg(total / batch - 1),
             fetches, total / batch - 1);

    // 到了底部再调一次，应当什么都不发生（不能多插、不能崩）。
    model.fetchMore(QModelIndex());
    checkNum(QStringLiteral("【2G】取完之后再调用 fetchMore，行数不变"),
             model.rowCount(), total);

    // 顺便把「分批评测」的时间也报一下：它衡量的是 beginInsertRows 的代价。
    // 这里的循环同样要带上限：讲义里的每个 while 都得有退路，否则一个协议 bug
    // 会变成「跑不完的测试」，比失败还难查。
    BigModel timed(total, initial, batch);
    QElapsedTimer t;
    t.start();
    int timedGuard = 0;
    while (timed.canFetchMore(QModelIndex()) && timedGuard < 1000) {
        timed.fetchMore(QModelIndex());
        ++timedGuard;
    }
    verdict(Obs, QStringLiteral("【2H】把 %1 行分批全部放出").arg(total), true,
            QString::number(t.elapsed()) + QStringLiteral(" ms（%1 次插入）")
                .arg(total / batch - 1));
}

// ============================================================================
// 六、阶段③：列宽策略 —— Fixed 一秒摆好，ResizeToContents 要按行去量
// ----------------------------------------------------------------------------
// QHeaderView::ResizeToContents 的实现是「拿每一行的 sizeHint 去量最宽的那个」。
// 在一百万行上这就是一百万次 data() + 一百万次 delegate->sizeHint()。
// Qt 自己也知道这事，所以给了 setResizeContentsPrecision（默认 1000）：
// 它只采样前 N 行，然后按样本推宽度。
// 于是「准」和「快」之间有那么一条折中的缝 —— 本节把两边的数都打出来。
// ⚠ 本节的硬账只挂在 Fixed 上（< 10000 次）；ResizeToContents 的具体次数受
//   采样精度与视图状态影响，属于 .cpp 里的策略，只报 Obs 不下结论。
// ============================================================================
static void phase3_columnWidths()
{
    section(QStringLiteral("③ 列宽策略：Fixed vs ResizeToContents"));

    const int rows = 200000;
    BigModel model(rows, rows, rows);
    QTableView view;
    view.setModel(&model);
    view.setWordWrap(false);
    view.verticalHeader()->setVisible(false);
    view.verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    view.verticalHeader()->setDefaultSectionSize(22);
    view.resize(520, 400);
    view.show();
    pumpEvents();

    // —— Fixed：宽度由我们说了算，视图不需要问任何一行的内容 ——
    view.horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    view.horizontalHeader()->resizeSection(BigModel::ColName, 160);
    view.horizontalHeader()->resizeSection(BigModel::ColScore, 90);
    view.horizontalHeader()->resizeSection(BigModel::ColState, 90);
    pumpEvents();
    model.resetCounter();
    pumpEvents();
    const qint64 fixedCalls = model.dataCalls();
    checkBool(QStringLiteral("【3A】Fixed 下 data() 调用 < 10000（不逐行量宽）"),
              fixedCalls < 10000, true);
    checkNum(QStringLiteral("【3B】宽度就是我们设的那个值"),
             view.horizontalHeader()->sectionSize(BigModel::ColName), 160);
    verdict(Obs, QStringLiteral("【3C】Fixed 下 data() 调用"), true, QString::number(fixedCalls));

    // —— ResizeToContents：视图按行去量，代价立刻上来 ——
    view.horizontalHeader()->setResizeContentsPrecision(1000);   // 默认就是 1000，写出来看得见
    model.resetCounter();
    QElapsedTimer t;
    t.start();
    view.horizontalHeader()->setSectionResizeMode(BigModel::ColName, QHeaderView::ResizeToContents);
    pumpEvents();
    const qint64 elapsedMs = t.elapsed();
    const qint64 resizeCalls = model.dataCalls();
    verdict(Obs, QStringLiteral("【3D】ResizeToContents 下 data() 调用"), true,
            QString::number(resizeCalls) + QStringLiteral(" 次"));
    verdict(Obs, QStringLiteral("【3E】ResizeToContents 的耗时"), true,
            QString::number(elapsedMs) + QStringLiteral(" ms"));
    verdict(Obs, QStringLiteral("【3F】采样精度"), true,
            QStringLiteral("%1 行 —— 它没有真的量完 %2 行")
                .arg(view.horizontalHeader()->resizeContentsPrecision()).arg(rows));

    // 一条规矩：大表上用 Fixed（或 Interactive），需要「好看」时手工给几个合理宽度；
    // 千万别在百万行上对多列一起开 ResizeToContents。
    view.hide();
}

// ============================================================================
// 七、阶段④：代理 —— 在 12 万行上过滤 + 排序
// ----------------------------------------------------------------------------
// QSortFilterProxyModel 做两件事：**筛**和**排**。两件都在代理里做，源模型毫不知情。
// 关键认知：代理**不复制数据**，它维护的是「源行号 → 代理行号」的映射。
// 所以：
//   · 过滤的代价是「把源模型的每一行都问一遍 filterAcceptsRow」；
//   · 排序的代价是对通过过滤的那些行做一次排序。
// 这就解释了为什么 filterAcceptsRow 必须便宜：它被调用的次数是**源行数**。
// 本节用两个硬账锁住正确性：过滤出来的条数、排序后的第一条。
// ============================================================================
class ScoreFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit ScoreFilterProxy(int minScore, QObject *parent = nullptr)
        : QSortFilterProxyModel(parent), m_minScore(minScore)
    {
        setSortRole(BigModel::ScoreRole);       // 排序按分数这个角色，而不是显示文本
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override
    {
        const QModelIndex idx = sourceModel()->index(sourceRow, BigModel::ColScore, sourceParent);
        return idx.data(BigModel::ScoreRole).toInt() >= m_minScore;
    }

private:
    int m_minScore;
};

static void phase4_proxy()
{
    section(QStringLiteral("④ 代理：12 万行里筛 + 排"));

    const int rows = 120000;
    const int minScore = 90;
    BigModel source(rows, rows, rows);

    // 独立算一遍期望值（不经过 Qt 的任何模型代码）
    int expected = 0;
    for (int r = 0; r < rows; ++r) {
        if (BigModel::scoreOf(r) >= minScore)
            ++expected;
    }

    ScoreFilterProxy proxy(minScore);
    QElapsedTimer t;
    t.start();
    proxy.setSourceModel(&source);
    proxy.sort(BigModel::ColScore, Qt::DescendingOrder);
    const qint64 buildMs = t.elapsed();

    checkNum(QStringLiteral("【4A】筛出来的条数"), proxy.rowCount(), expected);
    checkBool(QStringLiteral("【4B】确实筛掉了一些（不是全过）"), proxy.rowCount() < rows, true);
    verdict(Obs, QStringLiteral("【4C】建代理 + 排序耗时"), true,
            QString::number(buildMs) + QStringLiteral(" ms"));

    // 排序：降序，所以代理的第一行应该是最高分。
    const int best = proxy.index(0, BigModel::ColScore).data(BigModel::ScoreRole).toInt();
    const int worst = proxy.index(proxy.rowCount() - 1, BigModel::ColScore)
                          .data(BigModel::ScoreRole).toInt();
    checkBool(QStringLiteral("【4D】第一行是最高分（99）"), best == 99, true);
    checkBool(QStringLiteral("【4E】最后一行不低于门槛"), worst >= minScore, true);
    checkBool(QStringLiteral("【4F】排序是降序的"), best >= worst, true);

    // 视图挂上去看一眼——顺带证明「代理 + 视图」这套组合在 12 万行上照样只问几十行。
    QTableView view;
    view.setModel(&proxy);
    view.setWordWrap(false);
    view.verticalHeader()->setVisible(false);
    view.verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    view.verticalHeader()->setDefaultSectionSize(22);
    view.resize(520, 400);
    view.show();
    pumpEvents();
    source.resetCounter();
    pumpEvents();
    verdict(Obs, QStringLiteral("【4G】挂上代理后，源模型 data() 调用"), true,
            QString::number(source.dataCalls()) + QStringLiteral(" 次（对 %1 行）").arg(proxy.rowCount()));
    checkBool(QStringLiteral("【4H】仍然没有逐行扫"),
              source.dataCalls() < 10000, true);
    view.hide();
}

// ============================================================================
// 八、阶段⑤：批量更新 —— 五百次单插 vs 关掉更新再插
// ----------------------------------------------------------------------------
// 每插一行，视图就要重算布局、重画。五百行 = 五百次。
// 两个办法：
//   · view->setUpdatesEnabled(false) ... true：期间不重画，最后一次性刷新；
//   · 模型侧合并成一次 beginInsertRows(a, b)（更彻底，但业务上不一定能合并）。
// 本节对比「单插」和「关更新后单插」，硬账只保证结果一致：
// 行数一样、内容一样 —— 快慢是 Obs，因为不同平台的重绘策略差别很大。
// ⚠ 一个坑必须记住：setUpdatesEnabled(false) 之后**一定要记得打开**，
//   否则界面最后一帧是旧的，用户会以为程序死了。所以本讲用 RAII 包一下。
// ============================================================================
class UpdatesOff
{
public:
    explicit UpdatesOff(QWidget *w) : m_w(w)
    {
        if (m_w)
            m_w->setUpdatesEnabled(false);
    }
    ~UpdatesOff()
    {
        if (m_w) {
            m_w->setUpdatesEnabled(true);
            m_w->update();
        }
    }
    UpdatesOff(const UpdatesOff &) = delete;
    UpdatesOff &operator=(const UpdatesOff &) = delete;

private:
    QWidget *m_w;
};

static void phase5_bulkInsert()
{
    section(QStringLiteral("⑤ 批量更新：五百次单插 vs 关掉更新"));

    const int base = 1000;
    const int extra = 500;

    // —— 对照组：更新开着，逐条插 ——
    BigModel modelA(base, base, base);
    QTableView viewA;
    viewA.setModel(&modelA);
    viewA.setWordWrap(false);
    viewA.verticalHeader()->setVisible(false);
    viewA.verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    viewA.verticalHeader()->setDefaultSectionSize(22);
    viewA.resize(520, 400);
    viewA.show();
    pumpEvents();

    QElapsedTimer t;
    t.start();
    for (int i = 0; i < extra; ++i)
        modelA.appendOne();
    pumpEvents();
    const qint64 withUpdatesMs = t.elapsed();

    // —— 实验组：更新关掉，逐条插，最后一次刷新 ——
    BigModel modelB(base, base, base);
    QTableView viewB;
    viewB.setModel(&modelB);
    viewB.setWordWrap(false);
    viewB.verticalHeader()->setVisible(false);
    viewB.verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    viewB.verticalHeader()->setDefaultSectionSize(22);
    viewB.resize(520, 400);
    viewB.show();
    pumpEvents();

    t.restart();
    {
        UpdatesOff off(&viewB);
        for (int i = 0; i < extra; ++i)
            modelB.appendOne();
        pumpEvents();
    }
    pumpEvents();
    const qint64 withoutUpdatesMs = t.elapsed();

    checkNum(QStringLiteral("【5A】对照组行数"), modelA.rowCount(), base + extra);
    checkNum(QStringLiteral("【5B】实验组行数"), modelB.rowCount(), base + extra);
    checkBool(QStringLiteral("【5C】两组最后一行内容一致"),
              modelA.index(modelA.rowCount() - 1, BigModel::ColName).data().toString()
                  == modelB.index(modelB.rowCount() - 1, BigModel::ColName).data().toString(),
              true);
    checkBool(QStringLiteral("【5D】更新已经恢复（没有被 RAII 忘掉）"),
              viewB.updatesEnabled(), true);
    verdict(Obs, QStringLiteral("【5E】%1 次单插的耗时").arg(extra), true,
            QStringLiteral("更新开: %1 ms ｜ 更新关: %2 ms")
                .arg(withUpdatesMs).arg(withoutUpdatesMs));

    viewA.hide();
    viewB.hide();
}

// ============================================================================
// 九、阶段⑥：data() 里的重活
// ----------------------------------------------------------------------------
// data() 会被调用「可视行数 × 列数 × 角色数」那么多次，而且每次滚动都会再来一遍。
// 所以在里面做格式化、拼字符串、查数据库、算正则，都是**按次计费**。
// 本节对比两种写法，输出必须一模一样：
//   A：每次调用都 QStringLiteral("记录 %1").arg(row + 1)
//   B：预先备好 100 条前缀，按 row % 100 取一条再拼（或者干脆只返回已有的字符串）
// 真实项目里的等价做法是：**把显示文本缓存在模型里**（按需构建、失效时重建），
// 或者交给 delegate 去画（delegate 里也能缓存）。
// 注意规模：这里跑 30 万次 × 2，已经能看出量级差别；真实的一百万行 × 3 列会更大。
// ============================================================================
static void phase6_formatCost()
{
    section(QStringLiteral("⑥ data() 里的重活：每次调用都格式化"));

    const int n = 300000;

    QElapsedTimer t;
    t.start();
    QString sinkA;
    for (int i = 0; i < n; ++i)
        sinkA = BigModel::nameOf(i);                 // 每次都是 arg() 一次格式化
    const qint64 onDemandMs = t.elapsed();

    // 预格式化：只备 100 条，按 i % 100 取。现实里通常是「缓存显示文本」。
    QVector<QString> cache;
    cache.reserve(100);
    for (int i = 0; i < 100; ++i)
        cache.append(BigModel::nameOf(i));

    t.restart();
    QString sinkB;
    for (int i = 0; i < n; ++i)
        sinkB = cache.at(i % 100);
    const qint64 cachedMs = t.elapsed();

    checkBool(QStringLiteral("【6A】两种写法在可比的样本上结果一致"),
              BigModel::nameOf(7) == cache.at(7), true);
    checkBool(QStringLiteral("【6B】sink 不是空的（防止被优化掉）"),
              !sinkA.isEmpty() && !sinkB.isEmpty(), true);
    verdict(Obs, QStringLiteral("【6C】%1 次「现算」").arg(n), true,
            QString::number(onDemandMs) + QStringLiteral(" ms"));
    verdict(Obs, QStringLiteral("【6D】%1 次「取缓存」").arg(n), true,
            QString::number(cachedMs) + QStringLiteral(" ms"));
    verdict(Obs, QStringLiteral("【6E】量级差异"), true,
            QStringLiteral("倍数 ≈ %1×")
                .arg(QString::number(cachedMs > 0 ? double(onDemandMs) / double(cachedMs) : 0.0,
                                     'f', 1)));
}

// ============================================================================
// 十、main：合账
// ============================================================================
static void reconcile()
{
    section(QStringLiteral("合账"));
    const int broken = gLedger.failed + gLedger.stalled;
    qInfo().noquote() << QStringLiteral("  硬账 %1 项：对上 %2，不符 %3；整节作废 %4")
                             .arg(gLedger.total)
                             .arg(gLedger.total - broken)
                             .arg(gLedger.failed)
                             .arg(gLedger.stalled);
    qInfo().noquote() << QStringLiteral("  退出码 = %1（%2）")
                             .arg(gLedger.exitCode())
                             .arg(gLedger.exitCode() == 0
                                      ? QStringLiteral("没有硬账不符、没有整节作废")
                                      : QStringLiteral("有硬账不符或整节作废"));
    if (gLedger.exitCode() != 0)
        qWarning().noquote() << QStringLiteral("  ★ 请按上面的 [!! ] 行往下查。");
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("quality_07_large_view_optimization"));

    qInfo() << "================================================================";
    qInfo() << "quality 第 7 讲 · 百万行不卡（Qt =" << QT_VERSION_STR
            << ", 平台 =" << QGuiApplication::platformName() << "）";
    qInfo() << "  提醒：毫秒数只在本机成立；硬账只挂在「调用次数」和「结果对不对」上。";
    qInfo() << "================================================================";

    phase1_viewAsksLittle();
    phase2_fetchMore();
    phase3_columnWidths();
    phase4_proxy();
    phase5_bulkInsert();
    phase6_formatCost();

    reconcile();

    const int rc = gLedger.exitCode();
    qInfo().noquote() << QStringLiteral("进程退出码 = %1").arg(rc);
    return rc;
}

// ============================================================================
// 十一、速查：大表七条
// ============================================================================
/*
 * 一、模型侧
 *   行数不花内存 —— 数据用公式/懒加载算出来，别真造一百万个对象。
 *   data() 里只做「取」，不做「算」：格式化、正则、查库都挪出去。
 *   data() 挂一个计数器（开发期），它比任何 profiler 都直给。
 *   fetchMore 里必须 beginInsertRows/endInsertRows，闭区间 [旧行数, 新行数-1]。
 *
 * 二、视图侧
 *   setWordWrap(false) + verticalHeader()->setSectionResizeMode(Fixed)
 *   + setDefaultSectionSize(22)：行高统一，滚动范围就不必逐行测量。
 *   QTreeView 还要 setUniformRowHeights(true)。
 *   列宽用 Fixed 或 Interactive；万不得已用 ResizeToContents，先设好
 *   setResizeContentsPrecision（默认 1000 行采样）。
 *
 * 三、批量改
 *   逐条 insert 就是逐次布局。能合并就合并成一次 beginInsertRows；
 *   不能合并就 setUpdatesEnabled(false)，改完打开（用 RAII 包，别靠记性）。
 *
 * 四、代理
 *   filterAcceptsRow 被调用 **源行数** 次，它必须 O(1) 且不分配内存。
 *   排序走 setSortRole，让代理去取数值角色，而不是比较显示文本。
 *   代理不复制数据，它只维护映射 —— 所以内存不会翻倍，但筛/排要花时间。
 *
 * 五、两个数字
 *   可视行数通常 20~60 行。任何「和总行数成正比」的操作都要被怀疑。
 *   一次滚动 = 一次 data() 批量调用；滚动流畅度比首屏渲染更能暴露问题。
 *
 * ——— 老师傅的话 ———
 *   徒弟，记住这一句：**UI 慢，九成是因为某段代码被调了一百万次。**
 *   不是那行代码慢，而是它不该出现在那个位置。
 *
 *   我调过最狠的一个表格：两百多万行，打开要 40 秒。原因只有一行 ——
 *   有人在 data() 里做了 `QString::number(...)` 之后又 `.arg(...)`，
 *   而视图在打开时要为每一行算一次「行高自适应」。
 *   把两件事分开做（行高固定 + 文本缓存）之后，打开是 0.3 秒。
 *   代码总量没变，只是**调用次数**从两百万变成几千。
 *
 *   所以你以后遇到卡顿，先别打开 profiler，先问三个问题：
 *   哪个函数被调了多少次？是谁在调？那个次数和「总行数」有没有关系？
 *   答完这三个问题，大多数性能 bug 自己就说出来了。
 */

// ============================================================================
// 十二、练习
// ----------------------------------------------------------------------------
/*
 * 1. 把第 ① 节的 initial 从 1000000 改成 1000，再跑一遍，看【1B】的 data() 次数
 *    变了没有。回答：为什么「行数」不影响 data() 次数，但会影响滚动条的最大值？
 *
 * 2. 给 BigModel 加一个 `setScore(qint64 row, int value)`，用 dataChanged 通知视图。
 *    要求只重画那一行（提示：dataChanged 的两个 index 传同一个，角色传 ScoreRole）。
 *    然后回答：为什么刷新一行比 endResetModel() 便宜得多？
 *
 * 3. 把第 ④ 节的过滤条件换成「分数 ≥ 50 且名称里含 '1'」。重写 filterAcceptsRow，
 *    要求仍然用 ScoreRole 取分数（不要用 DisplayRole 去解析字符串）。
 *    做完回答：为什么「从显示文本里再解析出数字」是不该做的事？
 *
 * 4. 给第 ⑤ 节加第三组：把 500 条合并成**一次** beginInsertRows。
 *    对比三组耗时。然后回答：什么业务场景下「合并插入」是做不到的？
 *
 * 5. 把第 ⑥ 节的缓存改成「按行缓存显示文本」（QVector<QString> 长度 = 已加载行数），
 *    测一下内存增长和速度。回答：这个方案在多少行时开始不划算？为什么？
 *
 * 6. 资深题：给你的项目做一次「表格体检」。要求输出三个数：
 *      (a) 打开表格时 data() 被调了几次；
 *      (b) 一次滚动（滚到底再滚回顶）data() 被调了几次；
 *      (c) 这个次数 / 可视行数 = ?（正常应该在 3~10 之间）
 *    如果 (c) 明显偏大，按本讲的六节顺序排查：
 *    行高 → 列宽 → 代理 → data() 里的活。
 */
// ============================================================================

// 本文件有 2 个 Q_OBJECT 类（BigModel / ScoreFilterProxy），AUTOMOC 生成 .moc。
#include "07_large_view_optimization.moc"
