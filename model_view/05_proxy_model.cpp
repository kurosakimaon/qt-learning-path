// ============================================================================
// 第 5 讲 · QSortFilterProxyModel：在源模型前面再加一层坐标系
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   前四讲把「模型 → 视图」这条直线走通了。真实产品里中间几乎必然再插一层：
//   **排序 + 过滤 = 代理模型（proxy model）**。它不改你的数据，只改「视图看到几行、
//   第 0 行到底是哪一条」。这一层最咬人的不是 API 多少，而是**坐标系换了**：
//     1) 视图手里的 QModelIndex 从此属于代理，不再属于源。你拿代理的 index 去问源模型，
//        源模型不会报错，它只会给你**空值**——这就是「线上偶现空表格/删错行」的病根。
//        本讲把这条做成硬证据：同一个格子，mapToSource 前后问出来的值不一样，
//        不映射直接问源，打出来是 QVariant() 无效值 + rowCount()==0。
//     2) 过滤有两条路，搞混就会写出「筛不动」的代码：
//        · **静态匹配**（不用写代码）：setFilterFixedString / setFilterWildcard /
//          setFilterRegularExpression + filterKeyColumn + filterRole。它是「拿判据去
//          和某个 role 的字符串比」，判据是**一个模式串**。
//          ← 注意：Qt 里**没有 filterFixedColumn 这个函数**（我把本机
//            QtCore/qsortfilterproxymodel.h 通读并 grep 过，只有
//            setFilterFixedString(const QString&) 这个槽和 filterKeyColumn() 这个 getter）。
//            网上流传的「filterFixedColumn」是把两个名字焊一起了，别背。
//          · **重写 filterAcceptsRow / filterAcceptsColumn**（判据在外部、要查好几列、
//            要做集合运算时只有这条路）。签名必须逐字一致，本讲抄头文件第 128~129 行。
//     3) 外部判据改了，代理**不知道**：它会跟源模型的结构性信号（行插了/删了它自己会重算
//        映射），但你自己的 m_keyword 变了它无从得知 → 必须显式失效。
//        Qt6.12 的正规做法是 `invalidate()`（public slot，第 125 行）或子类里
//        `beginFilterChange()` / `endFilterChange()`（第 142~143 行，protected）。
//        老的 `invalidateFilter()/invalidateRowsFilter()/invalidateColumnsFilter()`
//        在本机头文件里**同时是 protected 且被标了废弃**（第 145~151 行原文：
//        `QT_DEPRECATED_VERSION_X_6_13("Use begin/endFilterChange() instead")`），
//        所以本讲不调它们——调了 -Wall 下就是一串 deprecation warning。
//     4) sort 与视图 setSortingEnabled 的关系：视图只负责「表头点一下 → 调 model->sort()」，
//        真正被 sort 的是**视图当前绑的那个模型**（这里就是代理）；代理再决定是
//        重排映射还是让源去排。dynamicSortFilter 这个开关管的是「源变了要不要自动重排/重筛」，
//        默认 false，很多「数据一多就不筛了」的 bug 就卡在这。
//   配套：QTableView 显示、QElapsedTimer 计时、data() 计数器（第 2 讲的思路）看代理
//   到底替视图问了多少次源模型。
//
// 对应构建目标名：model_view_05_proxy_model
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(model_view_05_proxy_model 05_proxy_model.cpp)
//   target_link_libraries(model_view_05_proxy_model PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)     # 本文件**有 Q_OBJECT 类**（StatusKeywordProxy），需要 moc
//   # 手工核对：moc 05_proxy_model.cpp -o 05_proxy_model.moc
//   #           文件末尾 #include "05_proxy_model.moc" 就是给 AUTOMOC 留的钩子
//
// 运行前须知：
//   · 无人值守：QTimer::singleShot 排一串动作 → qInfo() 打印证据 → QCoreApplication::quit()。
//     **没有 QDialog::exec()，没有裸 QEventLoop::exec()**；只用 processEvents() 催重绘。
//   · 可以无头跑：`model_view_05_proxy_model -platform offscreen`（CTest 里就是这么设的）。
//   · 数据全在内存现造：**源模型 1 万行 × 4 列**，够看出映射与过滤次数了。不读文件、不联网。
//   · 打印的**具体次数依赖 Qt 版本/样式/窗口尺寸**，我只承诺「关系」：
//     映射一定成立（代理第 i 行 ↔ 源某一行）、不映射问源一定拿到空、
//     改判据不失效则结果一定不变。
//   · 图解页面：../visuals/model_view.html（「视图 → 代理 → 源」三层索引流向那张）。
//   · 未实测声明：本机当前无法链接 Qt 运行时（运行时库版本不匹配），
//     本示例**已通过 g++ -fsyntax-only 编译期核对（-std=c++17 -Wall -Wextra 零 error 零 warning），
//     未链接、未运行**；下面每处「预期」后面写的是推理，不是实测数字。
// ============================================================================

#include <QApplication>              // QtWidgets/qapplication.h
#include <QAbstractTableModel>       // QtCore/qabstractitemmodel.h
#include <QSortFilterProxyModel>     // **QtCore**/qsortfilterproxymodel.h ← 代理全家在 QtCore，不在 QtWidgets
#include <QAbstractProxyModel>       // QtCore/qabstractproxymodel.h：QSortFilterProxyModel 的父类
#include <QModelIndex>               // 与 QAbstractItemModel 同一个头
#include <QPersistentModelIndex>     // 同一个头：讲映射与失效要用它
#include <QItemSelectionModel>       // QtCore/qitemselectionmodel.h（第 6 讲正片，这里只用它证明「选择也换坐标系」）
#include <QTableView>                // QtWidgets/qtableview.h
#include <QHeaderView>               // QtWidgets/qheaderview.h（setSortingEnabled / sortIndicator）
#include <QAbstractItemView>         // QtWidgets/qabstractitemview.h
#include <QVariant>                  // QtCore/qvariant.h
#include <QString>
#include <QStringList>
#include <QVector>
#include <QSet>                      // 状态判据用集合：这正是「静态匹配做不到」的那种条件
#include <QHash>
#include <QByteArray>
#include <QDebug>
#include <QTimer>
#include <QCoreApplication>
#include <QEventLoop>                // 只借它的 ProcessEventsFlags，不调 exec()
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QtGlobal>

#include <vector>
#include <string>

// ============================================================================
// 零、硬账：退出码 = 结论
// ============================================================================
namespace {

struct Ledger
{
    inline static int total = 0;
    inline static int passed = 0;
    static int exitCode() { return (total > 0 && passed == total) ? 0 : 1; }
};

void check(const char *what, qint64 got, qint64 want)
{
    ++Ledger::total;
    const bool ok = (got == want);
    if (ok)
        ++Ledger::passed;
    qInfo().noquote() << QStringLiteral("      %1 %2 → 期望 %3，实测 %4")
                             .arg(ok ? QStringLiteral("［对］") : QStringLiteral("［差］"))
                             .arg(QString::fromUtf8(what))
                             .arg(want).arg(got);
}

void checkBool(const char *what, bool got, bool want = true)
{
    check(what, got ? 1 : 0, want ? 1 : 0);
}

} // namespace

// ============================================================================
// 一、核对过的签名（本机 C:\msys64\ucrt64\include\qt6，QtCore 版本串 6.12.0）
//     行号是我去头文件里逐行看的，改 Qt 版本请重新核。
// ============================================================================
// QtCore/qsortfilterproxymodel.h
//   第 21 行   class Q_CORE_EXPORT QSortFilterProxyModel : public QAbstractProxyModel
//   第 53 行   explicit QSortFilterProxyModel(QObject *parent = nullptr);
//   第 55 行   void setSourceModel(QAbstractItemModel *sourceModel) override;   ← 父类的，代理自己实现
//   第 57 行   QModelIndex mapToSource(const QModelIndex &proxyIndex) const override;
//   第 58 行   QModelIndex mapFromSource(const QModelIndex &sourceIndex) const override;
//   第 60/61 行 QItemSelection mapSelectionToSource(const QItemSelection &) const override;
//              QItemSelection mapSelectionFromSource(const QItemSelection &) const override;
//   第 66/67 行 int filterKeyColumn() const;  void setFilterKeyColumn(int column);
//   第 71 行   void setFilterCaseSensitivity(Qt::CaseSensitivity cs);
//   第 82/83 行 int sortColumn() const;  Qt::SortOrder sortOrder() const;
//   第 85/86 行 bool dynamicSortFilter() const;  void setDynamicSortFilter(bool enable);
//   第 89/90 行 int sortRole() const;  void setSortRole(int role);
//   第 93/94 行 int filterRole() const;  void setFilterRole(int role);
//   第 105~109 行 enum class Direction { Rows = 0x01, Columns = 0x02, Both = Rows | Columns };
//   第 120~125 行 public Q_SLOTS:
//                void setFilterRegularExpression(const QString &pattern);
//                void setFilterRegularExpression(const QRegularExpression &regularExpression);
//                void setFilterWildcard(const QString &pattern);
//                void setFilterFixedString(const QString &pattern);
//                void invalidate();                    ← 唯一的 invalidate()，**没有重载**
//   第 127~129 行 protected:
//                virtual bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const;
//                virtual bool filterAcceptsColumn(int source_column, const QModelIndex &source_parent) const;
//   第 130 行   virtual bool lessThan(const QModelIndex &source_left, const QModelIndex &source_right) const;
//   第 142/143 行 void beginFilterChange();  void endFilterChange(Directions directions = Direction::Both);
//   第 145~151 行 #if QT_DEPRECATED_SINCE(6, 13) 里才是 invalidateFilter()/invalidateRowsFilter()/
//                invalidateColumnsFilter()，且都带 QT_DEPRECATED_VERSION_X_6_13，且都在 protected 段。
//   第 156~161 行 QModelIndex index(...)/parent(...)、int rowCount(...)/columnCount(...) 全 override；
//   第 164/165 行 QVariant data(...) const override / bool setData(...) override;   ← 代理默认转发给源
//   第 190 行   void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
//
// QtCore/qabstractproxymodel.h（父类，纯虚的是映射四件套）
//   mapToSource / mapFromSource / index / parent 是纯虚；rowCount/columnCount/data 有默认转发实现。
//
// QtWidgets/qtableview.h
//   第 61 行   void setSortingEnabled(bool enable);
//   第 78 行   void scrollTo(const QModelIndex &index, ScrollHint hint = EnsureVisible) override;
//   第 98 行   void sortByColumn(int column, Qt::SortOrder order);        ← public slot
// QtWidgets/qheaderview.h
//   第 110~115 行 setSortIndicatorShown(bool) / setSortIndicator(int, Qt::SortOrder) /
//                sortIndicatorSection() / sortIndicatorOrder()
//   第 165 行   void sortIndicatorChanged(int logicalIndex, Qt::SortOrder order);   ← 信号
// QtWidgets/qabstractitemview.h
//   第 215 行   public Q_SLOTS: virtual void reset();

// ============================================================================
// 二、源模型：1 万行 × 4 列的「工单表」
//     列：0 编号(int) / 1 标题(QString) / 2 状态(QString) / 3 优先级(int)
//     为什么还自己写一个而不用 QStandardItemModel：过滤/排序期间代理会**大量**问源模型，
//     这里要给 data() 装计数器，便利类没法干净地插计数器（第 7 讲专门讲它的代价）。
// ============================================================================

struct Issue {
    int id = 0;
    QString title;
    QString state;        // "OPEN" / "DOING" / "DONE" / "BLOCKED"
    int priority = 0;     // 1..5
};

class IssueSourceModel : public QAbstractTableModel
{
public:
    // 这里**故意不加 Q_OBJECT**：本类不新增信号/槽，只覆写基类虚函数。
    // 加了 Q_OBJECT 也不多错，但会让「谁需要 moc」这件事变得含糊——记住判据：
    // 只有新写信号/槽/Q_PROPERTY 的类才需要 Q_OBJECT（见 ../start/02_project_anatomy.cpp）。
    explicit IssueSourceModel(int rowCount, QObject *parent = nullptr)
        : QAbstractTableModel(parent)
    {
        m_rows.reserve(rowCount);
        // 造数据：状态按 4 种循环，优先级 1..5 循环，标题里塞上 id，方便用关键字过滤命中。
        static const char *kStates[] = { "OPEN", "DOING", "DONE", "BLOCKED" };
        for (int i = 0; i < rowCount; ++i) {
            Issue it;
            it.id = i;
            it.title = QStringLiteral("issue-%1").arg(i, 5, 10, QLatin1Char('0'));
            it.state = QString::fromLatin1(kStates[i % 4]);
            it.priority = (i % 5) + 1;
            m_rows.push_back(it);
        }
    }

    // ---- 必须实现的五个（继承 QAbstractTableModel 时 index/parent 有默认实现，
    //      但我们照样自己写，因为要把行号塞进 internalPointer，见 ../model_view/02）----
    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        ++m_callRowCount;
        if (parent.isValid())      // 表格模型：任何有效 parent 下面都没有行
            return 0;
        return int(m_rows.size());
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.isValid())
            return 0;
        return kColumnCount;
    }

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
    {
        ++m_callIndex;
        if (row < 0 || column < 0 || row >= int(m_rows.size()) || column >= kColumnCount)
            return QModelIndex();
        if (parent.isValid())      // 表格没有子层：parent 必须是无效索引
            return QModelIndex();
        // 第三个参数**不放指针**，只放行号：放指针就要操心 vector 扩容换地址（第 2 讲的坑）。
        return createIndex(row, column, quintptr(row));
    }

    QModelIndex parent(const QModelIndex &child) const override
    {
        Q_UNUSED(child);
        return QModelIndex();      // 表格模型：一切行的 parent 都是「根」= 无效索引
    }

    QVariant data(const QModelIndex &indexArg, int role = Qt::DisplayRole) const override
    {
        ++m_callData;
        // 取证用：记下「哪些行真被问过」，最后能打出被问行的区间宽度。
        if (role == Qt::DisplayRole && indexArg.isValid() && indexArg.model() == this)
            m_askedRows.insert(indexArg.row());

        if (!indexArg.isValid() || indexArg.model() != this)     // ← 这条守卫是本讲证据⑦的关键
            return QVariant();                                   //   外来 index：给**空**，不报错

        const int r = indexArg.row();
        if (r < 0 || r >= int(m_rows.size()))
            return QVariant();
        const Issue &it = m_rows[size_t(r)];

        switch (role) {
        case Qt::DisplayRole:
            switch (indexArg.column()) {
            case ColId:    return it.id;
            case ColTitle: return it.title;
            case ColState: return it.state;
            case ColPrio:  return it.priority;
            }
            break;
        case Qt::EditRole:                       // 排序默认读 EditRole 的语义靠它（见证据④）
            switch (indexArg.column()) {
            case ColId:    return it.id;
            case ColTitle: return it.title;
            case ColState: return it.state;
            case ColPrio:  return it.priority;
            }
            break;
        case Qt::UserRole:                        // 给代理子类当判据用（第 4 讲「值归模型」的纪律）
            return it.state;
        default:
            break;
        }
        return QVariant();
    }

    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override
    {
        if (role != Qt::DisplayRole)
            return QVariant();
        if (orientation == Qt::Horizontal) {
            switch (section) {
            case ColId:    return QStringLiteral("编号");
            case ColTitle: return QStringLiteral("标题");
            case ColState: return QStringLiteral("状态");
            case ColPrio:  return QStringLiteral("优先级");
            }
        } else if (section >= 0 && section < int(m_rows.size())) {
            return m_rows[size_t(section)].id;
        }
        return QVariant();
    }

    // 视图里能改吗？不能——基类 flags() 给的默认值含 ItemIsEditable，
    // 但本讲**不覆写 setData**，所以编辑提交会失败。编辑的正式讲法是 ../model_view/03。
    // 这里只开一个「后门」给证据③用：程序改一格并规规矩矩发 dataChanged。
    void setStateOfRow(int row, const QString &state)
    {
        if (row < 0 || row >= int(m_rows.size()) || m_rows[size_t(row)].state == state)
            return;
        m_rows[size_t(row)].state = state;
        const QModelIndex tl = index(row, ColState);
        emit dataChanged(tl, tl, QList<int>() << Qt::DisplayRole << Qt::EditRole << Qt::UserRole);
    }

    // 结构性变更：在中间插 1 行 / 删 1 行（begin→改容器→end 的铁律见 ../model_view/03）
    void insertRowAt(int row)
    {
        if (row < 0 || row > int(m_rows.size()))
            return;
        beginInsertRows(QModelIndex(), row, row);
        Issue it;
        it.id = -1000 - row;                       // 负号一眼认出是插入的测试行
        it.title = QStringLiteral("inserted-%1").arg(row);
        it.state = QStringLiteral("OPEN");
        it.priority = 1;
        m_rows.insert(m_rows.begin() + row, it);
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

    int idAtSourceRow(int row) const
    {
        if (row < 0 || row >= int(m_rows.size()))
            return -999999;
        return m_rows[size_t(row)].id;
    }

    // ---- 计数器（本讲的仪表）----
    void resetCounters() const
    {
        m_callData = 0; m_callIndex = 0; m_callRowCount = 0;
        m_askedRows.clear();
    }
    int dataCallCount() const { return m_callData; }
    QString counterReport() const
    {
        int lo = -1, hi = -1, n = 0;
        for (int r : std::as_const(m_askedRows)) {
            if (lo < 0 || r < lo) lo = r;
            if (r > hi) hi = r;
            ++n;
        }
        return QStringLiteral("data()=%1 次, index()=%2 次, rowCount()=%3 次, 被问到的**不同源行**=%4 个(区间 %5..%6)")
            .arg(m_callData).arg(m_callIndex).arg(m_callRowCount).arg(n).arg(lo).arg(hi);
    }

    enum Column { ColId = 0, ColTitle = 1, ColState = 2, ColPrio = 3 };
    static constexpr int kColumnCount = 4;

private:
    std::vector<Issue> m_rows;
    mutable int m_callData = 0;
    mutable int m_callIndex = 0;
    mutable int m_callRowCount = 0;
    mutable QSet<int> m_askedRows;
};

// ============================================================================
// 三、代理子类：判据来自外部 setter（关键字 + 状态集合 + 是否隐藏优先级列）
//     这就是「静态匹配不够用时」的标准写法。三个要点：
//       1) 覆写签名逐字照抄头文件 128~129 行（protected virtual const，参数是**源**的行号！）
//       2) 判据改变要显式失效：beginFilterChange()/endFilterChange() 包起来（本类内可行，
//          因为这两个函数是 protected，子类能调；外部只能调 public slot 的 invalidate()）
//       3) filterAcceptsRow 里拿数据要用 **sourceModel()->index(...) + data()**，
//          不是 self->index(...)——参数是源坐标，这是最常见的写错。
// ============================================================================

class StatusKeywordProxy : public QSortFilterProxyModel
{
    Q_OBJECT            // 有 public slots（给信号用的 setter），需要 moc

public:
    explicit StatusKeywordProxy(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent)
    {
        // 静态匹配这条路先关掉：-1 = 全部列都参与匹配（本机头文件第 66 行的 getter 会如实告诉我们）
        setFilterKeyColumn(-1);
    }

    // ---- 外部判据的三个 setter：写成**槽**，这样真做界面时可以直接
    //      `connect(lineEdit, &QLineEdit::textChanged, proxy, &StatusKeywordProxy::setKeyword)`。
    //      （也正因为有新槽，本类才真的需要 Q_OBJECT + moc。）
    //      改完必须「通知代理重筛」，否则结果纹丝不动。
    public slots:
    void setKeyword(const QString &kw)
    {
        if (m_keyword == kw)
            return;
        beginFilterChange();           // 第 142 行：告诉代理「接下来判据要变了」
        m_keyword = kw;
        endFilterChange();             // 第 143 行：这里才真正重跑 filterAcceptsRow/Column
        ++m_invalidateCount;
    }

    void setStateFilter(const QSet<QString> &states)
    {
        if (m_states == states)
            return;
        beginFilterChange();
        m_states = states;
        endFilterChange();
        ++m_invalidateCount;
    }

    // 「隐藏优先级列」演示 filterAcceptsColumn：列也是能过滤掉的
    void setShowPriorityColumn(bool show)
    {
        if (m_showPriority == show)
            return;
        beginFilterChange();
        m_showPriority = show;
        endFilterChange();
        ++m_invalidateCount;
    }

    // 故意留一个「改了判据但**不**失效」的路径，给证据⑤当反面教材（普通成员函数，不是槽）
    void setKeywordSilently(const QString &kw) { m_keyword = kw; }

    int acceptedRowCalls() const { return m_acceptRowCalls; }
    int acceptedColumnCalls() const { return m_acceptColumnCalls; }
    int filterChangeCount() const { return m_invalidateCount; }
    void resetFilterCounters() const
    {
        m_acceptRowCalls = 0;
        m_acceptColumnCalls = 0;
    }

    // ---- 逐字核对过的两个覆写（QtCore/qsortfilterproxymodel.h 第 128~129 行）----
    //      注意它们在基类是 **protected**，这里保持 protected 不动（改宽访问是合法的，
    //      但没必要，而且会让「谁是给外部调的、谁是给 Qt 调的」这件事糊掉）。
    protected:
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override
    {
        ++m_acceptRowCalls;
        QAbstractItemModel *src = sourceModel();
        if (!src)
            return false;
        // 注意：参数是**源坐标**，所以这里必须用 src->index(...)，不是 this->index(...)
        const QModelIndex stateIdx = src->index(source_row, IssueSourceModel::ColState, source_parent);
        const QString state = src->data(stateIdx, Qt::UserRole).toString();

        // 条件一：状态集合（这就是「静态匹配做不到」的那类条件：一个值属于一个集合）
        if (!m_states.isEmpty() && !m_states.contains(state))
            return false;

        // 条件二：关键字命中「编号」或「标题」任一列（多列 OR，静态匹配也做不到——
        //        filterKeyColumn 只能选**一列**或全部列，全列 OR 会把状态列也算进去）
        if (!m_keyword.isEmpty()) {
            const QModelIndex idIdx = src->index(source_row, IssueSourceModel::ColId, source_parent);
            const QModelIndex titleIdx = src->index(source_row, IssueSourceModel::ColTitle, source_parent);
            const QString idText = src->data(idIdx, Qt::DisplayRole).toString();
            const QString title = src->data(titleIdx, Qt::DisplayRole).toString();
            if (!idText.contains(m_keyword, Qt::CaseInsensitive)
                && !title.contains(m_keyword, Qt::CaseInsensitive))
                return false;
        }
        return true;
    }

    bool filterAcceptsColumn(int source_column, const QModelIndex &source_parent) const override
    {
        ++m_acceptColumnCalls;
        Q_UNUSED(source_parent);
        if (!m_showPriority && source_column == IssueSourceModel::ColPrio)
            return false;
        return true;
    }

    // ---- 排序判据：默认 lessThan 是「拿两个 index 的 data(sortRole) 用 QVariant 比」，
    //      字符串列按字典序、整数列按数值序都能忍；但状态列我想按**业务顺序**排，
    //      就得自己写。参数同样是**源坐标**。签名见头文件第 130 行。----
    bool lessThan(const QModelIndex &source_left, const QModelIndex &source_right) const override
    {
        if (!sourceModel())
            return false;
        if (source_left.column() == IssueSourceModel::ColState
            || source_right.column() == IssueSourceModel::ColState) {
            const QString a = sourceModel()->data(source_left, Qt::DisplayRole).toString();
            const QString b = sourceModel()->data(source_right, Qt::DisplayRole).toString();
            return stateRank(a) < stateRank(b);     // 业务顺序：OPEN < DOING < BLOCKED < DONE
        }
        return QSortFilterProxyModel::lessThan(source_left, source_right);   // 其余列交回基类
    }

    static int stateRank(const QString &s)
    {
        if (s == QLatin1String("OPEN"))    return 0;
        if (s == QLatin1String("DOING"))   return 1;
        if (s == QLatin1String("BLOCKED")) return 2;
        if (s == QLatin1String("DONE"))    return 3;
        return 9;
    }

private:
    QString m_keyword;
    QSet<QString> m_states;
    bool m_showPriority = true;
    mutable int m_acceptRowCalls = 0;
    mutable int m_acceptColumnCalls = 0;
    int m_invalidateCount = 0;
};

// ============================================================================
// 四、取证脚本：一条一条把结论打出来
//     用 QTimer::singleShot 排队，全走事件循环，不阻塞、不等鼠标。
// ============================================================================
static void dumpMapping(const char *tag,
                        const QAbstractProxyModel &proxy,
                        const IssueSourceModel &source,
                        int proxyRow)
{
    // tag 是源码里的中文字面量（UTF-8），所以必须 fromUtf8；
    // 用 fromLatin1 会把每个 UTF-8 字节当成一个 Latin-1 字符，打出来是一串问号样的乱码。
    const QModelIndex pIdx = proxy.index(proxyRow, 0);
    const QModelIndex sIdx = proxy.mapToSource(pIdx);
    qInfo().noquote() << QStringLiteral("  [%1] 代理第 %2 行 → mapToSource → 源第 %3 行；"
                                        "代理格 data=%4，源格 data=%5")
                             .arg(QString::fromUtf8(tag))
                             .arg(proxyRow)
                             .arg(sIdx.row())
                             .arg(pIdx.data(Qt::DisplayRole).toString())
                             .arg(source.data(sIdx, Qt::DisplayRole).toString());
}

class Evidence : public QObject          // 只是为了有地方放槽；Q_OBJECT 由文件末尾的 moc 提供
{
    Q_OBJECT

public:
    IssueSourceModel *source = nullptr;
    QSortFilterProxyModel *plain = nullptr;      // 只用静态匹配的裸代理
    StatusKeywordProxy *custom = nullptr;        // 重写 filterAcceptsRow/Column 的组合筛选代理
    QTableView *view = nullptr;

public slots:
    void step1_plainBasics()
    {
        qInfo().noquote() << QStringLiteral("① 代理的「默认长相」：判据字段当前值（没设过就该是文档默认）");
        qInfo().noquote() << QStringLiteral("  filterKeyColumn()=%1  (约定 -1 = 所有列都参与匹配)")
                                 .arg(plain->filterKeyColumn());
        qInfo().noquote() << QStringLiteral("  filterRole()=%1 (=Qt::DisplayRole, 静态匹配拿这个 role 的字符串比)")
                                 .arg(plain->filterRole());
        qInfo().noquote() << QStringLiteral("  sortColumn()=%1  dynamicSortFilter()=%2  (代理默认**不**动态)")
                                 .arg(plain->sortColumn()).arg(plain->dynamicSortFilter() ? "true" : "false");
        qInfo().noquote() << QStringLiteral("  源 rowCount=%1，代理 rowCount=%2，代理 columnCount=%3 —— 无判据时应当完全一致")
                                 .arg(source->rowCount()).arg(plain->rowCount()).arg(plain->columnCount());
        check("① filterKeyColumn 默认（本讲设的）为 -1", plain->filterKeyColumn(), -1);
        checkBool("① dynamicSortFilter 默认 false", plain->dynamicSortFilter(), false);
        check("① 无判据时代理行数 = 源行数", plain->rowCount(), source->rowCount());
        check("① 代理列数 = 源列数", plain->columnCount(), source->columnCount());
    }

    void step2_staticFilter()
    {
        qInfo().noquote() << QStringLiteral("② 静态匹配：setFilterFixedString(判据串) + filterKeyColumn(哪一列)");
        plain->setFilterKeyColumn(IssueSourceModel::ColState);
        plain->setFilterFixedString(QStringLiteral("BLOCKED"));
        qInfo().noquote() << QStringLiteral("  固定串 'BLOCKED' 只盯第 2 列 → 代理 rowCount=%1（源 %2 行的四分之一）")
                                 .arg(plain->rowCount()).arg(source->rowCount());
        check("② BLOCKED 恰好 2500 行（1/4）", plain->rowCount(), 2500);
        plain->setFilterFixedString(QStringLiteral("0"));
        plain->setFilterKeyColumn(-1);
        qInfo().noquote() << QStringLiteral("  换成固定串 '0' 且盯所有列 → 代理 rowCount=%1（整数/字符串不分，见下条警告）")
                                 .arg(plain->rowCount());
        plain->setFilterFixedString(QString());
        plain->setFilterKeyColumn(IssueSourceModel::ColState);
        qInfo().noquote() << QStringLiteral("  清空判据 → 代理 rowCount=%1 又回到全量").arg(plain->rowCount());
        check("② 清空判据后回到 10000 行", plain->rowCount(), 10000);
        qInfo().noquote() << QStringLiteral("  ⚠ 静态匹配是「字符串包含」：'0' 会命中编号 10、100、状态串里的 0。"
                                            "想要「值∈集合」或「两列 OR」→ 必须重写（见⑤）。");
    }

    void step3_dynamicSwitch()
    {
        qInfo().noquote() << QStringLiteral("③ dynamicSortFilter 这个开关到底管什么");
        plain->setFilterFixedString(QStringLiteral("BLOCKED"));
        const int before = plain->rowCount();
        source->setStateOfRow(0, QStringLiteral("BLOCKED"));   // 源第 0 行改成 BLOCKED（它本来状态是 OPEN）
        const int dynFalse = plain->rowCount();
        qInfo().noquote() << QStringLiteral("  dynamicSortFilter=false：改完源，代理 rowCount %1 → %2（**没动**，判据变了源也变了但它不重筛）")
                                 .arg(before).arg(dynFalse);
        plain->setDynamicSortFilter(true);
        source->setStateOfRow(1, QStringLiteral("BLOCKED"));
        const int dynTrue = plain->rowCount();
        qInfo().noquote() << QStringLiteral("  设成 true 之后再改源第 1 行：代理 rowCount=%1（这次自动重筛了）").arg(dynTrue);
        check("③ dynamic=false 时改源不重筛", dynFalse, before);
        check("③ dynamic=true 时改源立刻多出一行", dynTrue, dynFalse + 1);
        qInfo().noquote() << QStringLiteral("  注：setDynamicSortFilter(true) 不会立刻重跑历史账，它管的是「之后源发 dataChanged 时」跟不跟。");
        plain->setFilterFixedString(QString());
        plain->setDynamicSortFilter(false);
    }

    void step4_sorting()
    {
        qInfo().noquote() << QStringLiteral("④ sort() 与视图 setSortingEnabled 的分工");
        // 4a. 直接命令代理排
        plain->sort(IssueSourceModel::ColPrio, Qt::DescendingOrder);
        qInfo().noquote() << QStringLiteral("  直接 plain->sort(第3列, Descending) → sortColumn()=%1 sortOrder()=%2，"
                                            "代理第 0 行对应源第 %3 行（优先级=%4）")
                                 .arg(plain->sortColumn())
                                 .arg(plain->sortOrder() == Qt::AscendingOrder ? "Asc" : "Desc")
                                 .arg(plain->mapToSource(plain->index(0, 0)).row())
                                 .arg(plain->index(0, IssueSourceModel::ColPrio).data().toString());
        check("④a sortColumn 变成优先级列", plain->sortColumn(), IssueSourceModel::ColPrio);
        checkBool("④a sortOrder = Descending", plain->sortOrder() == Qt::DescendingOrder);
        check("④a 降序后第一行优先级 = 5", plain->index(0, IssueSourceModel::ColPrio).data().toInt(), 5);
        // 4b. 视图侧：setSortingEnabled(true) 只是把表头的 sortIndicatorChanged 接到 model->sort()
        view->setSortingEnabled(true);
        view->sortByColumn(IssueSourceModel::ColState, Qt::AscendingOrder);
        qInfo().noquote() << QStringLiteral("  view->setSortingEnabled(true) + sortByColumn(第2列, Asc) → "
                                            "表头指示器 section=%1 order=%2，被 sort 的是 view->model()（=代理），不是源")
                                 .arg(view->horizontalHeader()->sortIndicatorSection())
                                 .arg(view->horizontalHeader()->sortIndicatorOrder() == Qt::AscendingOrder
                                      ? "Asc" : "Desc");
        qInfo().noquote() << QStringLiteral("  源模型自己的第 0 行编号=%1（**没被重排**：代理只改映射，绝不碰你的容器）")
                                 .arg(source->idAtSourceRow(0));
        check("④b 表头指示器落在状态列", view->horizontalHeader()->sortIndicatorSection(),
              IssueSourceModel::ColState);
        check("④b 源模型第 0 行编号仍是 0", source->idAtSourceRow(0), 0);
        // 注意：此刻 view->model() 是**裸代理 plain**，它用的是基类的默认 lessThan（字符串字典序），
        // 所以状态列第一行是 BLOCKED 而不是业务上的 OPEN。想看「自定义 lessThan」得等⑤把
        // custom 挂上去之后（5b 那段），别把两个对象的账记混了。
        const QString firstStatePlain = view->model()->index(0, IssueSourceModel::ColState).data().toString();
        qInfo().noquote() << QStringLiteral("  plain 用基类默认 lessThan（字符串字典序）：状态列第一行=%1 → "
                                            "字典序 BLOCKED<DOING<DONE<OPEN；想要业务顺序得重写 lessThan（见⑤ 5b）")
                                 .arg(firstStatePlain);
        checkBool("④b 默认 lessThan 让 BLOCKED 排在最前", firstStatePlain == QStringLiteral("BLOCKED"));
        view->setSortingEnabled(false);
        plain->sort(0, Qt::AscendingOrder);
    }

    void step5_comboFilter()
    {
        qInfo().noquote() << QStringLiteral("⑤ 组合筛选（重写 filterAcceptsRow/Column）：关键字 + 状态集合 + 藏一列");
        custom->setSourceModel(source);
        const int allRows = custom->rowCount();
        custom->resetFilterCounters();
        custom->setStateFilter(QSet<QString>() << QStringLiteral("OPEN") << QStringLiteral("DOING"));
        qInfo().noquote() << QStringLiteral("  只留 {OPEN, DOING} → rowCount=%1（全量 %2），"
                                            "本次重筛跑了 filterAcceptsRow %3 次、filterAcceptsColumn %4 次")
                                 .arg(custom->rowCount()).arg(allRows)
                                 .arg(custom->acceptedRowCalls()).arg(custom->acceptedColumnCalls());
        const int rowsBeforeKeyword = custom->rowCount();
        qInfo().noquote() << QStringLiteral("  （注：③ 把源第 0、1 行改成了 BLOCKED，所以 {OPEN,DOING} 是 %1 而不是 5000）")
                                 .arg(rowsBeforeKeyword);
        check("⑤ {OPEN,DOING} 共 4998 行", rowsBeforeKeyword, 4998);
        custom->resetFilterCounters();
        custom->setKeyword(QStringLiteral("74"));
        qInfo().noquote() << QStringLiteral("  加关键字 '74'（走 begin/endFilterChange）→ rowCount %1 → %2，"
                                            "filterAcceptsRow 跑了 %3 次")
                                 .arg(rowsBeforeKeyword).arg(custom->rowCount()).arg(custom->acceptedRowCalls());
        check("⑤ 关键字 '74' 把结果缩到 110 行", custom->rowCount(), 110);
        bool allMatch74 = true;
        for (int r = 0; r < custom->rowCount(); ++r) {
            const QModelIndex srcIdx = custom->mapToSource(custom->index(r, 0));
            const QString id = source->data(source->index(srcIdx.row(), IssueSourceModel::ColId)).toString();
            const QString title = source->data(source->index(srcIdx.row(), IssueSourceModel::ColTitle)).toString();
            if (!id.contains(QStringLiteral("74")) && !title.contains(QStringLiteral("74")))
                allMatch74 = false;
        }
        checkBool("⑤ 过滤后每一行都真的含 '74'", allMatch74);

        const int rowsWith74 = custom->rowCount();
        custom->setKeywordSilently(QStringLiteral("75"));   // 反面教材：只改判据不失效
        qInfo().noquote() << QStringLiteral("  ⚠ setKeywordSilently('75') 不失效 → rowCount 仍是 %1（和关键字 '74' 时 %2 一样），"
                                            "证明「外部判据变了代理不知道」")
                                 .arg(custom->rowCount()).arg(rowsWith74);
        check("⑤ 不失效时行数一动不动", custom->rowCount(), rowsWith74);
        custom->setKeyword(QStringLiteral("750"));          // 正规路径：走 endFilterChange（判据从 75 → 750，不会被同值短路）
        qInfo().noquote() << QStringLiteral("  改用 setKeyword('750') → rowCount 变成 %1，filterChange 累计 %2 次")
                                 .arg(custom->rowCount()).arg(custom->filterChangeCount());
        check("⑤ 正规 setKeyword('750') 之后是 6 行", custom->rowCount(), 6);
        custom->setShowPriorityColumn(false);
        qInfo().noquote() << QStringLiteral("  setShowPriorityColumn(false) → 代理 columnCount=%1（源 %2），整列消失")
                                 .arg(custom->columnCount()).arg(source->columnCount());
        check("⑤ 藏掉优先级列后剩 3 列", custom->columnCount(), 3);
        custom->setKeyword(QString());

        // 5b. 自定义 lessThan：同一列，按业务顺序而不是字典序
        custom->sort(IssueSourceModel::ColState, Qt::AscendingOrder);
        const QString firstState = custom->index(0, IssueSourceModel::ColState).data().toString();
        qInfo().noquote() << QStringLiteral("  5b 自定义 lessThan 按业务顺序排状态列：第一行状态=%1"
                                            "（默认字典序会是 BLOCKED）").arg(firstState);
        checkBool("⑤ 自定义 lessThan 让 OPEN 排最前", firstState == QStringLiteral("OPEN"));
    }

    void step6_mapping()
    {
        qInfo().noquote() << QStringLiteral("⑥ 索引映射：为什么 mapToSource/mapFromSource 不是可选项");
        custom->setShowPriorityColumn(true);
        custom->setStateFilter(QSet<QString>() << QStringLiteral("DONE"));
        custom->setKeyword(QString());
        qInfo().noquote() << QStringLiteral("  当前判据：只看状态=DONE → 代理 %1 行，源 %2 行")
                                 .arg(custom->rowCount()).arg(source->rowCount());
        check("⑥ 只看 DONE → 2500 行", custom->rowCount(), 2500);
        dumpMapping("代理第0行", *custom, *source, 0);
        dumpMapping("代理第1行", *custom, *source, 1);
        dumpMapping("末行", *custom, *source, custom->rowCount() - 1);
        checkBool("⑥ 末行映射到合法源行",
                  custom->mapToSource(custom->index(custom->rowCount() - 1, 0)).isValid());
        // 反向：源第 3 行（4 取余是 3 → 状态 BLOCKED，被筛掉了）→ mapFromSource 应给**无效索引**
        const QModelIndex src3 = source->index(3, IssueSourceModel::ColState);
        const QModelIndex mapped = custom->mapFromSource(src3);
        qInfo().noquote() << QStringLiteral("  mapFromSource(源第3行, 状态=%1) → %2（被筛掉的行**没有**代理坐标）")
                                 .arg(source->data(src3).toString())
                                 .arg(mapped.isValid() ? QStringLiteral("代理第 %1 行").arg(mapped.row())
                                                       : QStringLiteral("无效索引 isValid=false"));
        checkBool("⑥ 被筛掉的源行 mapFromSource 得无效索引", mapped.isValid(), false);
        // 错误行为演示：拿代理 index 直接问源模型
        const QModelIndex p0 = custom->index(0, IssueSourceModel::ColId);
        const QVariant wrong = source->data(p0, Qt::DisplayRole);
        qInfo().noquote() << QStringLiteral("  ❌ source->data(代理 index) → isValid=%1 类型=%2 值=<%3>"
                                            "（不抛异常、不报错，就是空）")
                                 .arg(wrong.isValid() ? "true" : "false")
                                 .arg(wrong.typeId())
                                 .arg(wrong.isNull() ? QStringLiteral("null") : wrong.toString());
        checkBool("⑥ source->data(代理 index) 是无效 QVariant", wrong.isValid(), false);
        check("⑥ source->rowCount(代理 index) = 0", source->rowCount(p0), 0);
        qInfo().noquote() << QStringLiteral("  ❌ source->rowCount(代理 index)=%1，source->index(代理 row/col) 却**能**给出东西："
                                            "行号 0 在源里合法，但它指的是源的第 0 行（状态=%2），不是你要的那条 → 删错行就这么来的")
                                 .arg(source->rowCount(p0))
                                 .arg(source->data(source->index(p0.row(), IssueSourceModel::ColState)).toString());
        qInfo().noquote() << QStringLiteral("  ✅ mapToSource 之后：源第 %1 行，编号=%2，状态=%3 —— 与代理格显示值一致")
                                 .arg(custom->mapToSource(p0).row())
                                 .arg(source->idAtSourceRow(custom->mapToSource(p0).row()))
                                 .arg(source->data(custom->mapToSource(p0).sibling(
                                     custom->mapToSource(p0).row(), IssueSourceModel::ColState)).toString());
        checkBool("⑥ mapToSource 后取到的值与代理格显示值一致",
                  source->data(custom->mapToSource(p0), Qt::DisplayRole) == p0.data(Qt::DisplayRole));
        qInfo().noquote() << QStringLiteral("  顺带：index 自带户口 → p0.model()=%1，source=%2，同一个指针才是自家人")
                                 .arg(QString::number(qintptr(p0.model()), 16))
                                 .arg(QString::number(qintptr(source), 16));
        checkBool("⑥ 代理 index 的 model() 就是代理自己", p0.model() == custom);
        // 选择也换坐标系（第 6 讲正片）
        QItemSelectionModel sel(custom);
        sel.select(custom->index(0, 0), QItemSelectionModel::ClearAndSelect);
        const QItemSelection asSource = custom->mapSelectionToSource(sel.selection());
        qInfo().noquote() << QStringLiteral("  代理上选 1 格 → 选择集里的行=%1；mapSelectionToSource 之后源行=%2")
                                 .arg(sel.selectedIndexes().isEmpty() ? -1 : sel.selectedIndexes().first().row())
                                 .arg(asSource.indexes().isEmpty() ? -1 : asSource.indexes().first().row());
        checkBool("⑥ 选择集换算到源模型后非空", !asSource.indexes().isEmpty());
        check("⑥ 换算后的源行 = mapToSource(代理第0行)",
              asSource.indexes().isEmpty() ? -1 : asSource.indexes().first().row(),
              custom->mapToSource(custom->index(0, 0)).row());
    }

    void step7_structureAndInvalidate()
    {
        qInfo().noquote() << QStringLiteral("⑦ 结构性信号 vs invalidate()：谁负责跟");
        const int rowsBefore = custom->rowCount();
        const int srcBefore = source->rowCount();
        source->insertRowAt(0);                     // 源在第 0 行前插一行（状态 OPEN，会被 DONE 判据筛掉）
        qInfo().noquote() << QStringLiteral("  源 insertRows(0,1)：源行数 %1 → %2；代理行数 %3 → %4"
                                            "（插的行不符合判据 → 代理只跟着挪映射，不多放行）")
                                 .arg(srcBefore).arg(source->rowCount())
                                 .arg(rowsBefore).arg(custom->rowCount());
        check("⑦ 源插一行后 = 10001", source->rowCount(), srcBefore + 1);
        check("⑦ 插的 OPEN 行不符合 DONE 判据 → 代理行数不变", custom->rowCount(), rowsBefore);
        dumpMapping("插行之后", *custom, *source, 0);
        const int srcRows2 = source->rowCount();
        const int proxyRows2 = custom->rowCount();
        // 删掉「代理第 0 行」对应的那一**源**行：必须 mapToSource，否则删错
        const QModelIndex doomedProxy = custom->index(0, 0);
        const int doomedSourceRow = custom->mapToSource(doomedProxy).row();
        const int doomedId = source->idAtSourceRow(doomedSourceRow);
        source->removeRowAt(doomedSourceRow);
        qInfo().noquote() << QStringLiteral("  源 removeRows(映射后的第 %1 行)：源 %2 → %3，代理 %4 → %5；"
                                            "被删的是编号 %6 那条").arg(doomedSourceRow)
                                 .arg(srcRows2).arg(source->rowCount())
                                 .arg(proxyRows2).arg(custom->rowCount()).arg(doomedId);
        check("⑦ 删完后源 = 10000", source->rowCount(), srcRows2 - 1);
        check("⑦ 删的是 DONE 行 → 代理也少一行", custom->rowCount(), proxyRows2 - 1);
        // 反面：不映射直接拿代理行号去删源
        const int safeProxyRow = custom->index(0, 0).row();
        const int wrongTargetId = source->idAtSourceRow(safeProxyRow);
        const int rightTargetId = source->idAtSourceRow(custom->mapToSource(custom->index(0, 0)).row());
        qInfo().noquote() << QStringLiteral("  ❌ 若按代理行号 %1 直接删源：删掉的是编号 %2；"
                                            "✅ 映射后删的是编号 %3 —— 两个数字不同，就是「删错行」事故现场")
                                 .arg(safeProxyRow).arg(wrongTargetId).arg(rightTargetId);
        checkBool("⑦ 「按代理行号直接删」会删错行（两个编号不同）", wrongTargetId != rightTargetId);
        qInfo().noquote() << QStringLiteral("  结论：源发 rowsAboutToBeInserted/rowsInserted/… 时**代理自己会跟**"
                                            "（它内部连着这些信号）；你外部判据变了它**不会**跟 → 只能 invalidate()/begin+endFilterChange()。");
        const int r0 = custom->rowCount();
        custom->invalidate();                       // public slot，头文件第 125 行；无重载
        qInfo().noquote() << QStringLiteral("  判据没变的情况下调 invalidate()：代理 rowCount %1 → %2（重跑筛选，结果不变，"
                                            "代价是全表重筛——别把它当刷新按钮乱按）").arg(r0).arg(custom->rowCount());
        check("⑦ 判据没变时 invalidate 不改变行数", custom->rowCount(), r0);
    }

    void step8_viewAndCost()
    {
        qInfo().noquote() << QStringLiteral("⑧ 挂上视图，顺手量一下代理的账");
        view->setModel(custom);
        view->resize(900, 420);
        view->show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

        source->resetCounters();
        custom->resetFilterCounters();
        view->viewport()->repaint();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        qInfo().noquote() << QStringLiteral("  一次重绘的账（代理在中间转发）：%1").arg(source->counterReport());
        qInfo().noquote() << QStringLiteral("  代理侧 filterAcceptsRow 计 %1 次、filterAcceptsColumn 计 %2 次"
                                            "（重绘阶段是 0——筛选只在结构/判据变化时跑，不在画的时候跑）")
                                 .arg(custom->acceptedRowCalls()).arg(custom->acceptedColumnCalls());
        check("⑧ 一次重绘期间 filterAcceptsRow 不被调用", custom->acceptedRowCalls(), 0);
        checkBool("⑧ 重绘确实向源模型要过数据", source->dataCallCount() > 0);

        const int rowsForDone = custom->rowCount();
        view->scrollTo(custom->index(rowsForDone - 1, 0), QAbstractItemView::EnsureVisible);
        source->resetCounters();
        view->viewport()->repaint();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        qInfo().noquote() << QStringLiteral("  scrollTo 到代理末行（第 %1 行）再重绘：%2")
                                 .arg(rowsForDone - 1).arg(source->counterReport());
        qInfo().noquote() << QStringLiteral("  视图永远只问**可见区**那些格，只不过问的对象是代理，代理再挑源的行号去问。");
    }
};

// ============================================================================
// 五、main：把证据排进事件循环
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("model_view_05_proxy_model"));

    qInfo().noquote() << QStringLiteral("================ 第 5 讲 · QSortFilterProxyModel ================");
    qInfo().noquote() << QStringLiteral("Qt 版本串：%1（本机头文件 QtCore 版本同为 6.12.0）").arg(QT_VERSION_STR);

    Evidence ev;
    ev.source = new IssueSourceModel(10000);                 // 源模型：1 万行 × 4 列
    ev.plain = new QSortFilterProxyModel;                    // 只用静态匹配
    ev.plain->setSourceModel(ev.source);
    ev.custom = new StatusKeywordProxy;                      // 重写 filterAcceptsRow/Column
    ev.view = new QTableView;
    ev.view->setModel(ev.plain);                             // 一开始挂裸代理
    ev.view->resize(900, 420);
    ev.view->show();                                         // offscreen 下也是真窗口，视图会真去问模型
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    // 每一步之间隔 20ms，保证前一步的重绘/信号都处理完了再取证。
    QTimer::singleShot(40, &ev, [&ev] { ev.step1_plainBasics(); });
    QTimer::singleShot(80, &ev, [&ev] { ev.step2_staticFilter(); });
    QTimer::singleShot(120, &ev, [&ev] { ev.step3_dynamicSwitch(); });
    QTimer::singleShot(160, &ev, [&ev] { ev.step4_sorting(); });
    QTimer::singleShot(200, &ev, [&ev] { ev.step5_comboFilter(); });
    QTimer::singleShot(240, &ev, [&ev] { ev.step6_mapping(); });
    QTimer::singleShot(280, &ev, [&ev] { ev.step7_structureAndInvalidate(); });
    QTimer::singleShot(320, &ev, [&ev] { ev.step8_viewAndCost(); });
    QTimer::singleShot(400, &ev, [&ev] {
        qInfo().noquote() << QStringLiteral("⑨ 收尾自检：判据/映射/视图三方一致");
        qInfo().noquote() << QStringLiteral("  custom 仍挂着源：sourceModel()==source ? %1；代理行数=%2；视图 model()==custom ? %3")
                                 .arg(ev.custom->sourceModel() == ev.source ? "true" : "false")
                                 .arg(ev.custom->rowCount())
                                 .arg(ev.view->model() == ev.custom ? "true" : "false");
        QElapsedTimer t; t.start();
        const QModelIndex last = ev.custom->index(ev.custom->rowCount() - 1, 0);
        const QModelIndex lastSrc = ev.custom->mapToSource(last);
        qInfo().noquote() << QStringLiteral("  末行映射耗时 %1 ns（代理第 %2 行 ↔ 源第 %3 行）")
                                 .arg(t.nsecsElapsed()).arg(last.row()).arg(lastSrc.row());
        checkBool("⑨ custom 仍挂着源模型", ev.custom->sourceModel() == ev.source);
        checkBool("⑨ 视图挂的是 custom", ev.view->model() == ev.custom);
        checkBool("⑨ 末行能映射回合法源行", lastSrc.isValid());
        check("⑨ 收尾时代理行数 = 2499", ev.custom->rowCount(), 2499);
        qInfo().noquote() << QStringLiteral("\n【硬账】%1/%2 项核对通过 → 退出码 %3")
                                 .arg(Ledger::passed).arg(Ledger::total).arg(Ledger::exitCode());
        QCoreApplication::exit(Ledger::exitCode());
    });

    const int rc = app.exec();
    qInfo().noquote() << QStringLiteral("（事件循环正常退出，rc=%1）").arg(rc);
    return rc;
}

// ============================================================================
// 六、老师傅的话 + 小练习
// ============================================================================
/*
 * ——— 老师傅的话 ———
 *   徒弟，代理模型这层，新手栽的跟头九成不在「筛不出来」，而在「筛出来以后拿错了行」。
 *   你记住一句话就够用：**代理是一副新坐标系，不是新数据**。视图交给你的一切 index，
 *   都是「在这副新坐标系里的坐标」，进源模型之前必须换算，出来进视图之前也必须换算。
 *   换算函数就两个：mapToSource / mapFromSource，两个都要 const，别自己造轮子去
 *   「按 row 减一减」——过滤后的行号关系是**任意映射**，不是等差，你算不出来。
 *
 *   第二个要记住的是「谁负责跟」。源模型加了行、删了行、整个 reset，代理自己会跟，
 *   因为它内部连着源模型的结构性信号，这部分你不用管；但**你的判据是藏在你自己对象里的
 *   几个成员变量**，代理看不见它们，源模型也不会发信号。所以 setter 里那句失效不是客套，
 *   是必需品。证据⑤里我特意留了个 setKeywordSilently()，跑一遍你就看到「改了但没反应」
 *   那种诡异现象——线上「过滤框打字没反应」的 bug，八成就是这个。
 *
 *   第三，别把 invalidate() 当刷新键。它会把整表的判据重跑一遍，一万行就是至少一万次
 *   filterAcceptsRow，而且每次 filterAcceptsRow 里你还在向源模型要 index 和 data。
 *   Qt6.12 给了 beginFilterChange()/endFilterChange() 这条更细的路（头文件里那两个
 *   invalidate*Filter 被标废弃就是要把人往这条路赶），能只重筛行或只重筛列。数据大时
 *   这是能救命的差别——但先按证据⑧跑一遍，看看次数账，再决定要不要省。
 *
 *   第四，别在 filterAcceptsRow 里干重活。它被调用的次数是「行数 × 重筛次数」，
 *   你在这儿解析一次 JSON、算一次日期、new 一个 QString，乘以一万行就是一万份账单。
 *   判据要的值，早早存在模型里（本讲用 Qt::UserRole 存状态字符串就是这个意思），
 *   这里只做比较。第 2 讲那句「data() 要便宜」，到了代理这儿要升级成「filterAcceptsRow
 *   要更便宜」，因为它的调用频率比 paint 高得多。
 *
 *   最后说一句实在话：面试问「为什么视图里删一行会删错」，八成想听的就是映射这件事；
 *   线上出「过滤后双击编辑改到别的记录」，也是同一件事。这层的钱不是学 API 花的，
 *   是「什么时候必须换坐标」这个意识花的。想清楚这个，代理模型就不难。
 *
 * ——— 小练习 ———
 *   1. 把证据⑤里 setKeywordSilently() 的「不失效」毛病，改成用 public slot 的 invalidate()
 *      来补（在 setKeywordSilently 末尾加一次 invalidate()），对比 filterAcceptsRow 的
 *      次数账；再把它换成 beginFilterChange()/endFilterChange()，看次数有没有差。
 *   2. 给代理加一条「按编号区间过滤」的判据（setter 收两个 int），用 setStateFilter 那套
 *      失效写法接上；然后把 filterKeyColumn() 显式设成 0、setFilterFixedString("74")，
 *      观察「只盯一列」和「你写的多列 OR」命中行数为什么不同。
 *   3. 把证据④的 lessThan 业务顺序去掉，直接交回基类，按状态列排一次，看 DONE/DOING/BLOCKED
 *      变成字典序以后有多别扭；再把 view->setSortingEnabled(true) 打开，程序调
 *      horizontalHeader()->setSortIndicator(3, Qt::DescendingOrder)，验证「表头指示器一动，
 *      真正被 sort 的是 view->model() 指向的那个对象」这句话（打印 sortColumn() 与源的顺序）。
 */
// ============================================================================

// 本文件有 Q_OBJECT 类（StatusKeywordProxy / Evidence），AUTOMOC 生成后在这里包含。
#include "05_proxy_model.moc"
