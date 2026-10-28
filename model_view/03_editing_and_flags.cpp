// ============================================================================
// 第 3 讲 · 可编辑模型：flags / setData / dataChanged 与增删行
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   第 2 讲的模型是「只答不接」的：视图问 data()，它给；想改？没门（基类 setData
//   直接 return false）。这一讲把「接」这一半补上，并且把**回路**讲透：
//     视图提交 → delegate.setModelData → model.setData → 改内部存储
//              → emit dataChanged(topLeft, bottomRight, roles)
//              → view 的 dataChanged 槽 → 标记脏区 → 重画时再调 data()
//   这条回路里最容易出事的是中间那一跳「emit」。本讲用**两个模型实例 + 两个视图**
//   做对照：一个改完不发信号，一个规规矩矩发。取证点是三层：
//     · 模型自测（data()）——两个都是「对的」，因为数据确实改了；
//     · 视图的 dataChanged 槽被调用次数——漏发的那个是 **0**，视图根本不知道；
//     · 强制 repaint 之后漏发的那个立刻「露馅」：画出来的是新值。
//       这就是现场「一改就错、一滚就对」这种玄学 bug 的病根，本讲把它数出来。
//   另外把编辑相关的四件套一次讲完：
//     1) flags()：Qt::ItemIsEditable / ItemIsUserCheckable / ItemIsEnabled /
//        ItemIsSelectable / NoItemFlags 的真名与真值，以及「视图的 editTriggers
//        和模型的 flags 是两道门，缺一不可」。
//     2) setData() 的正确姿势：先验 index、**先判 role**（EditRole / CheckStateRole
//        分两条路）、再校验数据、再比「有没有真的变」、最后写存储 + emit dataChanged。
//     3) insertRows/removeRows：beginInsertRows(parent, first, last) → 改容器 →
//        endInsertRows()，顺序反了视图就会拿旧行号越界问你（这是崩溃的经典配方）。
//     4) beginResetModel/endResetModel 与 begin/endInsertRows 的**取舍**：
//        reset 会让视图丢掉选择集与滚动位置（本讲用 scrollbar 的 value 打出来），
//        但它适合「整份数据换新」的场景。
//
// 对应构建目标名：model_view_03_editing_and_flags
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(model_view_03_editing_and_flags 03_editing_and_flags.cpp)
//   target_link_libraries(model_view_03_editing_and_flags PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)         # 本文件**有 Q_OBJECT 类**，需要 moc
//
// 运行前须知：
//   · 无人值守：全程 QTimer::singleShot + 直接调模型的槽函数驱动，**没有任何
//     QDialog::exec()、没有裸 QEventLoop::exec()**；只用了 processEvents() 催重绘。
//     编辑动作是程序直接调 model.setData(...) 触发的（第 4 讲才让编辑器去提交）。
//   · 可以无头跑：`model_view_03_editing_and_flags -platform offscreen`。
//   · 数据现造（不读文件、不联网），两个模型实例各 200 行，够演示了。
//   · 图解页面：../visuals/model_view.html（第 1 讲的五角色图；本讲看「回路」那一段）。
//   · 未实测声明：本机当前无法链接 Qt 运行时（运行时库版本不匹配），
//     本示例**已通过 g++ -fsyntax-only 编译期核对（-std=c++17 -Wall -Wextra 零 error 零 warning），
//     未链接、未运行**；文中所有「预期」后面是推理，不是实测。
// ============================================================================

#include <QApplication>              // QtWidgets/qapplication.h
#include <QAbstractTableModel>       // QtCore/qabstractitemmodel.h
#include <QTableView>                // QtWidgets/qtableview.h
#include <QAbstractItemView>         // EditTriggers / State
#include <QItemSelectionModel>       // QtCore/qitemselectionmodel.h
#include <QScrollBar>                // QtWidgets/qscrollbar.h（QAbstractScrollArea 的滚动条是 protected，
                                     //   要读 value 得自己开个子类口子，见 ListeningView）
#include <QModelIndex>
#include <QPersistentModelIndex>
#include <QVariant>
#include <QString>
#include <QStringList>
#include <QList>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QCoreApplication>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QtGlobal>
#include <QDebug>

#include <vector>

// ============================================================================
// 一、核对签名（本机 C:\msys64\ucrt64\include\qt6，Qt 6.12.0）
// ============================================================================
// QtCore/qabstractitemmodel.h
//   第 283 行 virtual QVariant data(const QModelIndex &index,
//                              int role = Qt::DisplayRole) const = 0;
//   第 284 行 virtual bool setData(const QModelIndex &index, const QVariant &value,
//                              int role = Qt::EditRole);
//                    ← **默认实现直接 return false**，所以不覆写就是「不可编辑」；
//                      默认角色是 **Qt::EditRole**，视图提交时传的也是 EditRole
//   第 286~289 行 QVariant headerData(int section, Qt::Orientation, int role = Qt::DisplayRole) const;
//                 bool setHeaderData(int section, Qt::Orientation orientation,
//                                    const QVariant &value, int role = Qt::EditRole);
//   第 304 行 virtual bool insertRows(int row, int count,
//                              const QModelIndex &parent = QModelIndex());   ← Q_REVISION(6,4)
//   第 306 行 virtual bool removeRows(int row, int count,
//                              const QModelIndex &parent = QModelIndex());
//   第 324 行 virtual Qt::ItemFlags flags(const QModelIndex &index) const;
//   第 387/388 行 virtual bool submit(); virtual void revert();   ← 带内部缓存的编辑路径，本讲不用
//   第 359/360 行 void dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight,
//                                  const QList<int> &roles = QList<int>());
//                    ← **是 QList<int> 不是 QVector<int>**；roles 为空表示「不知道哪些角色变了」，
//                      视图就当全变了重画（代价你自己付）
//   第 361 行 void headerDataChanged(Qt::Orientation orientation, int first, int last);
//   第 365~369 行 rowsAboutToBeInserted / rowsInserted / rowsAboutToBeRemoved / rowsRemoved
//                 —— 参数尾巴上都有 **QPrivateSignal**：外面能 connect，**不能 emit**
//   第 377/378 行 modelAboutToBeReset(QPrivateSignal) / modelReset(QPrivateSignal)
//   第 402~406 行（**protected**）beginInsertRows(parent, first, last) / endInsertRows()
//                 beginRemoveRows(parent, first, last) / endRemoveRows()
//   第 411~415 行（protected）beginInsertColumns/endInsertColumns/beginRemoveColumns/endRemoveColumns
//   第 420/421 行（**protected**）beginResetModel() / endResetModel()
//   第 273 行（public）bool hasIndex(int row, int column, const QModelIndex &parent = QModelIndex()) const;
// QtCore/qnamespace.h 第 1556~1569 行  enum ItemFlag {
//     NoItemFlags = 0, ItemIsSelectable = 1, ItemIsEditable = 2, ItemIsDragEnabled = 4,
//     ItemIsDropEnabled = 8, ItemIsUserCheckable = 16, ItemIsEnabled = 32,
//     ItemIsAutoTristate = 64, ItemNeverHasChildren = 128, ItemIsUserTristate = 256 };
//     Q_DECLARE_FLAGS(ItemFlags, ItemFlag) / Q_DECLARE_OPERATORS_FOR_FLAGS(ItemFlags)
//   第 1517~1554 行 enum ItemDataRole { DisplayRole=0, DecorationRole=1, EditRole=2, …,
//     CheckStateRole=10, …, UserRole = 0x0100 };   ← **Qt6 无 Qt::SortRole**
// QtWidgets/qabstractitemview.h
//   第 79~87 行  enum EditTrigger { NoEditTriggers=0, CurrentChanged=1, DoubleClicked=2,
//                    SelectedClicked=4, EditKeyPressed=8, AnyKeyPressed=16, AllEditTriggers=31 };
//   第 119/120 行 void setEditTriggers(EditTriggers); EditTriggers editTriggers() const;
//   第 231/232 行（**protected Q_SLOTS**）
//                 virtual void dataChanged(const QModelIndex &topLeft,
//                                          const QModelIndex &bottomRight,
//                                          const QList<int> &roles = QList<int>());
//                    ← 本讲的取证就靠**覆写它**：视图到底听见了几次，一清二楚
//   第 235 行（protected 槽）virtual void rowsInserted(const QModelIndex&, int start, int end);
//   第 236 行（protected 槽）virtual void rowsAboutToBeRemoved(const QModelIndex&, int, int);
//   第 240/241 行（protected 槽）closeEditor(QWidget*, EndEditHint) / commitData(QWidget*)
// QtWidgets/qabstractscrollarea.h（ListeningView 里要用）：
//   QScrollBar *verticalScrollBar() const / horizontalScrollBar() const 都在 **protected** 段，
//   所以外部拿不到滚动条 —— 想读滚动位置必须自己继承一个类转出来（本讲就这么做的）
// ============================================================================

// ============================================================================
// 二、数据源
// ============================================================================
struct TaskRow {
    QString title;
    int     progress = 0;      // 0..100，越界就是本讲的「校验」靶子
    QString owner;
    bool    done = false;      // 用 CheckStateRole 表达
};

enum TaskColumn {
    ColTitle = 0,
    ColProgress,
    ColOwner,
    ColDone,
    ColCount
};

// ============================================================================
// 三、取证用的调用轨迹（谁在什么时候被调了）
// ============================================================================
namespace Trace {
inline QStringList lines;
inline void add(const QString &s) { lines << s; }
inline void clear() { lines.clear(); }
inline QString join() { return lines.isEmpty() ? QStringLiteral("（空）") : lines.join(QStringLiteral(" → ")); }
}

// ============================================================================
// 四、可编辑的表格模型
// ============================================================================
class TaskModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit TaskModel(QObject *parent = nullptr) : QAbstractTableModel(parent) {}

    // ── 五个基本成员（第 2 讲讲过，这里只留必要注释）─────────────────
    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.isValid() || !hasIndex(row, column, parent))
            return QModelIndex();
        return createIndex(row, column, nullptr);   // 表格：不塞指针，用 row 定位（第 2 讲证据③的结论）
    }
    QModelIndex parent(const QModelIndex &child) const override
    {
        Q_UNUSED(child)
        return QModelIndex();
    }
    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : int(m_rows.size());
    }
    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        Q_UNUSED(parent)
        return ColCount;
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        ++s_dataCalls;
        if (!index.isValid() || index.row() >= int(m_rows.size()))
            return QVariant();                     // 无效坐标 → **无效 QVariant**，不是空串
        const TaskRow &r = m_rows.at(size_t(index.row()));
        switch (role) {
        case Qt::DisplayRole:
            switch (index.column()) {
            case ColTitle:    return r.title;
            case ColProgress: return QStringLiteral("%1%").arg(r.progress);
            case ColOwner:    return r.owner;
            case ColDone:     return r.done ? QStringLiteral("已完成") : QStringLiteral("未完成");
            }
            return QVariant();
        case Qt::EditRole:                          // 编辑器/排序读这份**原始值**
            switch (index.column()) {
            case ColTitle:    return r.title;
            case ColProgress: return r.progress;
            case ColOwner:    return r.owner;
            case ColDone:     return r.done;
            }
            return QVariant();
        case Qt::CheckStateRole:                   // 只给 Done 列；返回 int（Qt::CheckState 的数值）
            if (index.column() == ColDone)
                return int(r.done ? Qt::Checked : Qt::Unchecked);
            return QVariant();
        case Qt::ToolTipRole:
            return QStringLiteral("%1｜进度 %2%%3")
                       .arg(r.title)
                       .arg(r.progress)
                       .arg(r.done ? QStringLiteral("（已勾选）") : QString());
        case Qt::TextAlignmentRole:
            return index.column() == ColProgress ? int(Qt::AlignRight | Qt::AlignVCenter)
                                                 : int(Qt::AlignLeft | Qt::AlignVCenter);
        default:
            return QVariant();
        }
    }

    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override
    {
        if (role != Qt::DisplayRole)
            return QVariant();
        if (orientation == Qt::Vertical)
            return section + 1;
        switch (section) {
        case ColTitle:    return QStringLiteral("任务");
        case ColProgress: return QStringLiteral("进度");
        case ColOwner:    return QStringLiteral("负责人（只读）");
        case ColDone:     return QStringLiteral("完成");
        }
        return QVariant();
    }

    // ── ①：flags()——「这一格允许干什么」────────────────────────────
    // 基类 QAbstractTableModel::flags 给的是 ItemIsSelectable|ItemIsEnabled（有有效 index 时）。
    // 常见错误是**把基类那两位丢了**（直接 return Qt::ItemIsEditable），那样格子既不能选也不能点。
    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        if (!index.isValid())
            return Qt::NoItemFlags;                 // 数值 0：无效坐标什么都别许诺

        Qt::ItemFlags f = QAbstractTableModel::flags(index);   // ← 继承基类的两位，别自己凭空造

        switch (index.column()) {
        case ColTitle:
        case ColProgress:
            f |= Qt::ItemIsEditable;                // ← 想被编辑：必须显式加这一位
            break;
        case ColDone:
            f |= Qt::ItemIsUserCheckable;           // ← 想打勾：这一位 + CheckStateRole 成对出现
            break;
        case ColOwner:
        default:
            break;                                   // 只读列：什么都不加，视图按 EditTrigger 也进不来
        }
        // ItemIsDropEnabled/ItemIsDragEnabled 等拖放的事第 8 讲之外再说，本讲不给，
        // 免得「能拖出去」这种副作用干扰取证。
        return f;
    }

    // ── ②：setData()——回路的心脏 ───────────────────────────────────
    // 签名逐字核对第 284 行：bool setData(const QModelIndex&, const QVariant&, int role = Qt::EditRole)
    // 顺序（**每一步都不能省**）：
    //   1) 验 index；2) 验 role；3) 验 flags 可编辑；4) 校验/换算数据；
    //   5) 「值没变就 return true 但不发信号」；6) 写内部存储；7) emit dataChanged。
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override
    {
        if (!index.isValid() || index.row() >= int(m_rows.size()))
            return false;

        TaskRow &r = m_rows[size_t(index.row())];   // 注意：data() 是 const，这里不是

        // 第 2 步：role 分流。视图默认走 EditRole；勾选框走 CheckStateRole
        if (role == Qt::EditRole) {
            if (!flags(index).testFlag(Qt::ItemIsEditable)) {
                ++s_setDataRejected;
                Trace::add(QStringLiteral("setData 被拒（第 %1 列不可编辑）").arg(index.column()));
                return false;
            }
            switch (index.column()) {
            case ColTitle: {
                const QString t = value.toString().trimmed();
                if (t.isEmpty()) {                  // 校验：空标题不收
                    ++s_setDataRejected;
                    Trace::add(QStringLiteral("setData 被拒（标题为空）"));
                    return false;
                }
                if (r.title == t) {                 // 值没变：**不发信号**，直接返回 true
                    ++s_setDataNoop;
                    return true;
                }
                r.title = t;
                break;
            }
            case ColProgress: {
                bool ok = false;
                const int v = value.toInt(&ok);
                if (!ok || v < 0 || v > 100) {      // 校验：必须是 0..100 的整数
                    ++s_setDataRejected;
                    Trace::add(QStringLiteral("setData 被拒（进度 %1 越界/非法）").arg(value.toString()));
                    return false;
                }
                if (r.progress == v) {
                    ++s_setDataNoop;
                    return true;
                }
                r.progress = v;
                break;
            }
            default:
                return false;                       // 只有这两列接 EditRole
            }
        } else if (role == Qt::CheckStateRole) {
            if (index.column() != ColDone || !flags(index).testFlag(Qt::ItemIsUserCheckable)) {
                ++s_setDataRejected;
                return false;
            }
            // 视图给的可能是 Qt::Checked / Qt::Unchecked（int），也可能是 bool
            const bool nowChecked = value.toBool();
            if (r.done == nowChecked) {
                ++s_setDataNoop;
                return true;
            }
            r.done = nowChecked;
        } else {
            return false;                            // 其它角色一律不接：**不要**顺手接 DisplayRole
        }

        // 第 7 步：把「变了」这件事广播出去。roles 列表要**只写真的变了的角色**
        ++s_setDataOk;
        const QList<int> changed{ Qt::DisplayRole, Qt::EditRole, Qt::ToolTipRole };
        const int roleForTrace = role;
        emit dataChanged(index, index, changed);
        Trace::add(QStringLiteral("emit dataChanged(row=%1,col=%2,role=%3)")
                       .arg(index.row()).arg(index.column()).arg(roleForTrace));
        return true;
    }

    // 表头也能改（把回路完整走一遍：视图 → setHeaderData → headerDataChanged）
    bool setHeaderData(int section, Qt::Orientation orientation, const QVariant &value,
                       int role = Qt::EditRole) override
    {
        if (orientation != Qt::Horizontal || role != Qt::EditRole
            || section < 0 || section >= ColCount)
            return false;
        if (value.toString().trimmed().isEmpty())
            return false;
        m_headers[section] = value.toString();
        emit headerDataChanged(orientation, section, section);   // 第 361 行：(orientation, first, last)
        return true;
    }
    QVariant headerDataFor(int section) const { return m_headers.at(section); }

    // ── ③：增删行——begin/end 的顺序就是「视图看到的行数」与「模型真实的行数」对齐的窗口 ──
    // 正确次序：beginInsertRows(parent, first, last) → **改容器** → endInsertRows()
    // 反过来的话，begin 与 end 之间视图一旦重画，它按「新行数」去问 index，
    // 而你的容器还是旧的 → 越界 / 拿到隔壁行的数据。
    bool insertRows(int row, int count, const QModelIndex &parent = QModelIndex()) override
    {
        if (parent.isValid() || count <= 0 || row < 0 || row > int(m_rows.size()))
            return false;
        beginInsertRows(QModelIndex(), row, row + count - 1);
        for (int k = 0; k < count; ++k) {
            TaskRow r;
            r.title = QStringLiteral("插入的任务 %1").arg(row + k + 1);
            r.progress = 0;
            r.owner = QStringLiteral("（新）");
            r.done = false;
            m_rows.insert(m_rows.begin() + row, r);   // 注意：vector 插入会搬地址（第 2 讲证据③）
        }
        endInsertRows();
        Trace::add(QStringLiteral("insertRows(%1,%2) 走完 begin/end").arg(row).arg(count));
        return true;
    }

    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override
    {
        if (parent.isValid() || count <= 0 || row < 0 || row + count > int(m_rows.size()))
            return false;
        beginRemoveRows(QModelIndex(), row, row + count - 1);
        m_rows.erase(m_rows.begin() + row, m_rows.begin() + row + count);
        endRemoveRows();
        Trace::add(QStringLiteral("removeRows(%1,%2) 走完 begin/end").arg(row).arg(count));
        return true;
    }

    // ── 反面教材：**只改数据、不发信号**（现场最常见的「界面不动」bug 制造机）──
    void pokeProgress(int row, int value)
    {
        if (row < 0 || row >= int(m_rows.size()))
            return;
        m_rows[size_t(row)].progress = value;         // 数据真的改了，data() 自测会给新值……
        // ……但这里**没有 emit dataChanged**，视图完全不知情。故意的，别看漏了。
        ++s_pokes;
        Trace::add(QStringLiteral("pokeProgress：改了存储，**没发 dataChanged**"));
    }

    // 正面对照：同一个改动，走规矩的路
    void setProgressProperly(int row, int value)
    {
        const QModelIndex idx = index(row, ColProgress);
        setData(idx, value, Qt::EditRole);            // 里面会 emit
    }

    // ── ④：reset 家族——整份换新时用，代价是选择与滚动位置 ──
    void reloadEverything(int count)
    {
        beginResetModel();                            // 从这里起：**所有 index/QPersistentModelIndex 作废**
        m_rows.clear();
        m_rows.reserve(size_t(count));
        for (int i = 0; i < count; ++i) {
            TaskRow r;
            r.title = QStringLiteral("任务 %1").arg(i + 1);
            r.progress = (i * 3) % 101;
            r.owner = QStringLiteral("张工");
            r.done = (i % 7 == 0);
            m_rows.push_back(r);
        }
        endResetModel();                              // 内部发 modelReset（第 378 行，带 QPrivateSignal）
        Trace::add(QStringLiteral("reloadEverything：beginResetModel/endResetModel"));
    }

    void seed(int count)
    {
        m_headers = { QStringLiteral("任务"), QStringLiteral("进度"),
                      QStringLiteral("负责人（只读）"), QStringLiteral("完成") };
        reloadEverything(count);
        Trace::clear();                               // 建数据不算剧情，轨迹从干净开始
    }

    // ── 取证读数 ──
    static void resetCounters()
    {
        s_dataCalls = 0;
        s_setDataOk = 0;
        s_setDataNoop = 0;
        s_setDataRejected = 0;
        s_pokes = 0;
    }
    static qint64 dataCalls() { return s_dataCalls; }
    static qint64 setDataOk() { return s_setDataOk; }
    static qint64 setDataNoop() { return s_setDataNoop; }
    static qint64 setDataRejected() { return s_setDataRejected; }
    static qint64 pokes() { return s_pokes; }
    static void resetDataCalls() { s_dataCalls = 0; }

private:
    std::vector<TaskRow> m_rows;
    QStringList m_headers;

    inline static qint64 s_dataCalls = 0;
    inline static qint64 s_setDataOk = 0;
    inline static qint64 s_setDataNoop = 0;
    inline static qint64 s_setDataRejected = 0;
    inline static qint64 s_pokes = 0;
};

// ============================================================================
// 五、监听型视图：把「视图听见了几次」数出来
// ============================================================================
// 覆写 QAbstractItemView 的 **protected 槽** dataChanged(...)（第 231/232 行）
// —— 这一条比在模型侧数 emit 更有说服力：它证明的是「视图到底听没听见」。
class ListeningView : public QTableView
{
    Q_OBJECT

public:
    explicit ListeningView(QWidget *parent = nullptr) : QTableView(parent) {}

    static qint64 viewDataChangedHits() { return s_viewDataChanged; }
    static qint64 viewRowsInsertedHits() { return s_viewRowsInserted; }
    static qint64 paints() { return s_paints; }
    static QString lastRoles() { return s_lastRoles; }
    static QString lastRange() { return s_lastRange; }
    static void resetHits()
    {
        s_viewDataChanged = 0;
        s_viewRowsInserted = 0;
        s_paints = 0;
        s_lastRoles.clear();
        s_lastRange.clear();
    }

    // QAbstractScrollArea 的 verticalScrollBar() 是 protected，这里转成 public 供取证
    int scrollValue() const { return verticalScrollBar()->value(); }

protected:
    void dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight,
                     const QList<int> &roles = QList<int>()) override
    {
        ++s_viewDataChanged;
        s_lastRange = QStringLiteral("(%1,%2)~(%3,%4)")
                          .arg(topLeft.row()).arg(topLeft.column())
                          .arg(bottomRight.row()).arg(bottomRight.column());
        QStringList rs;
        for (const int r : roles)
            rs << QString::number(r);
        s_lastRoles = rs.isEmpty() ? QStringLiteral("（roles 为空 = 视图当全变了）") : rs.join(QLatin1Char(','));
        QTableView::dataChanged(topLeft, bottomRight, roles);   // **别忘转给基类**，否则界面真的不更新
    }

    void rowsInserted(const QModelIndex &parent, int start, int end) override
    {
        ++s_viewRowsInserted;
        QTableView::rowsInserted(parent, start, end);
    }

    void paintEvent(QPaintEvent *event) override
    {
        ++s_paints;
        QTableView::paintEvent(event);
    }

private:
    inline static qint64 s_viewDataChanged = 0;
    inline static qint64 s_viewRowsInserted = 0;
    inline static qint64 s_paints = 0;
    inline static QString s_lastRoles;
    inline static QString s_lastRange;
};

// ============================================================================
// 六、小工具
// ============================================================================
static void flushEvents()
{
    QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
}

static QString flagName(Qt::ItemFlags f, Qt::ItemFlag bit, const char *label)
{
    return QStringLiteral("%1=%2").arg(QLatin1String(label)).arg(f.testFlag(bit) ? 1 : 0);
}

// ============================================================================
// 七、证据①：flags() 把「能不能编辑」说清楚
// ============================================================================
static void evidenceFlags(TaskModel &model, ListeningView &view)
{
    qInfo() << "【证据①】flags()：四道门的第一道（另一道是视图的 editTriggers）";

    for (int col = 0; col < ColCount; ++col) {
        const QModelIndex idx = model.index(0, col);
        const Qt::ItemFlags f = model.flags(idx);
        qInfo() << QStringLiteral("  第 %1 列「%2」→ [%3 %4 %5 %6 %7]  数值=0x%8")
                       .arg(col)
                       .arg(model.headerData(col, Qt::Horizontal, Qt::DisplayRole).toString())
                       .arg(flagName(f, Qt::ItemIsEnabled, "Enabled"))
                       .arg(flagName(f, Qt::ItemIsSelectable, "Selectable"))
                       .arg(flagName(f, Qt::ItemIsEditable, "Editable"))
                       .arg(flagName(f, Qt::ItemIsUserCheckable, "Checkable"))
                       .arg(flagName(f, Qt::ItemIsDropEnabled, "DropEnabled"))
                       .arg(int(f), 0, 16);
    }
    qInfo() << "  对照基类：QAbstractTableModel::flags(有效 index) 给的是 Selectable|Enabled，"
            << "数值 =" << int(model.flags(model.index(0, ColOwner)))
            << "（1|32=33）；无效 index 本模型给 NoItemFlags（数值 ="
            << int(model.flags(QModelIndex())) << "）";
    qInfo() << "  QModelIndex 上也有便利入口：index(0,ColProgress).flags().testFlag(ItemIsEditable) ="
            << model.index(0, ColProgress).flags().testFlag(Qt::ItemIsEditable)
            << "（它内部就是 model->flags(index)，第 144 行）";
    qInfo() << "  第二道门在视图手里：view.editTriggers() 现在是"
            << int(view.editTriggers()) << "（NoEditTriggers=0 / DoubleClicked=2 / EditKeyPressed=8 /"
               " SelectedClicked=4 / CurrentChanged=1 / AnyKeyPressed=16 / AllEditTriggers=31）";
    view.setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    qInfo() << QString::asprintf("  设成 DoubleClicked|EditKeyPressed = %d 之后，"
                                 "**两道门是 AND 关系**：flags 给 Editable 且触发方式命中才会开编辑器；"
                                 "只读列（负责人）永远进不来，哪怕你把触发方式设成 AllEditTriggers。",
                                 int(view.editTriggers()));
    qInfo() << "  顺手看一眼勾选列：flags(index(0,ColDone)).testFlag(ItemIsUserCheckable) ="
            << model.flags(model.index(0, ColDone)).testFlag(Qt::ItemIsUserCheckable)
            << "且 CheckStateRole 有值 =" << model.data(model.index(0, ColDone), Qt::CheckStateRole).isValid()
            << "（**成对**：只给位不给值，或者只给值不给位，勾选框都不出现）";
}

// ============================================================================
// 八、证据②：回路 —— setData → dataChanged → 视图槽 → 重画 → 又问 data()
// ============================================================================
static void evidenceCommitLoop(TaskModel &model, ListeningView &view)
{
    qInfo() << "【证据②】把回路走通一次，并打印 dataChanged 携带的范围与角色";

    ListeningView::resetHits();
    TaskModel::resetCounters();
    Trace::clear();

    // ① 在模型侧接住 dataChanged：用两种参数个数的 lambda，看 Qt 的「槽可以少接参数」
    //    （这几个变量**不能是 static**：块级 static 变量带静态存储期，C++ 禁止被 lambda 捕获）
    int hitsFull = 0;
    int hitsShort = 0;
    QString captured;
    hitsFull = 0;
    hitsShort = 0;
    captured.clear();
    QObject::connect(&model, &QAbstractItemModel::dataChanged,
                     [&hitsFull, &captured](const QModelIndex &tl, const QModelIndex &br,
                                            const QList<int> &roles) {
                         ++hitsFull;
                         captured = QStringLiteral("topLeft(%1,%2) bottomRight(%3,%4) roles=[%5]")
                                        .arg(tl.row()).arg(tl.column())
                                        .arg(br.row()).arg(br.column())
                                        .arg([&roles]() {
                                            QStringList l;
                                            for (int r : roles)
                                                l << QString::number(r);
                                            return l.join(QLatin1Char(','));
                                        }());
                     });
    QObject::connect(&model, &QAbstractItemModel::dataChanged,
                     [&hitsShort](const QModelIndex &tl, const QModelIndex &br) {
                         ++hitsShort;                          // 少写第三个参数：**照样接得住**
                         Q_UNUSED(br)
                         Q_UNUSED(tl)
                     });

    const QModelIndex cell = model.index(3, ColProgress);
    const int before = model.data(cell, Qt::EditRole).toInt();
    qInfo() << QString::asprintf("  改之前：data(index(3,1), EditRole) = %d；视图的 dataChanged 槽命中 %lld 次",
                                 before, static_cast<long long>(ListeningView::viewDataChangedHits()));

    TaskModel::resetDataCalls();                                 // 只看这一跳之后 data() 被重问了几次
    const bool ok = model.setData(cell, 77, Qt::EditRole);
    flushEvents();
    view.viewport()->repaint();

    const int after = model.data(cell, Qt::EditRole).toInt();
    qInfo() << QString::asprintf("  setData(77) 返回 %s → 模型自测：EditRole = %d ｜ DisplayRole = %s",
                                 ok ? "true" : "false", after,
                                 model.data(cell, Qt::DisplayRole).toString().toUtf8().constData());
    qInfo() << "  模型侧 dataChanged 被接住：三参数版" << hitsFull << "次、两参数版" << hitsShort
            << "次（**同一个信号，槽少写一个参数也能连上**，Qt 帮你把多余参数丢掉）";
    qInfo() << "     携带的范围与角色：" << captured;
    qInfo() << "  视图侧（覆写 protected 槽）命中 =" << ListeningView::viewDataChangedHits()
            << "次｜视图记下的范围 =" << ListeningView::lastRange()
            << "角色 =" << ListeningView::lastRoles();
    qInfo() << QString::asprintf("  回路闭合的最后一环：这次改动之后 data() 又被问了 %lld 次"
                                 "（视图重画时才把「77」取出来画上去）",
                                 static_cast<long long>(TaskModel::dataCalls()));
    qInfo() << "  轨迹：" << Trace::join();

    // ② 校验被拒 + 无效改动不发信号
    Trace::clear();
    const qint64 okBefore = TaskModel::setDataOk();
    const qint64 rejBefore = TaskModel::setDataRejected();
    const qint64 noopBefore = TaskModel::setDataNoop();
    model.setData(cell, 999, Qt::EditRole);                     // 越界 → 拒
    model.setData(model.index(3, ColOwner), QStringLiteral("李工")); // 只读列 → 拒
    model.setData(cell, 77, Qt::EditRole);                       // 值没变 → 不发信号
    qInfo() << QString::asprintf("  三种情形分别计到：成功 %lld→%lld、被拒 %lld→%lld、无变化 %lld→%lld",
                                 static_cast<long long>(okBefore), static_cast<long long>(TaskModel::setDataOk()),
                                 static_cast<long long>(rejBefore), static_cast<long long>(TaskModel::setDataRejected()),
                                 static_cast<long long>(noopBefore), static_cast<long long>(TaskModel::setDataNoop()));
    qInfo() << "  → 「值没变就别 emit」不是洁癖：编辑器每次提交都可能带同一个值回来，"
               "你照发不误的话视图就白重画一片（更糟的是撤销栈会被空操作灌爆）。";
    qInfo() << "  轨迹：" << Trace::join();

    // ③ 表头回路
    if (model.setHeaderData(ColOwner, Qt::Horizontal, QStringLiteral("负责人（可改的表头）")))
        qInfo() << "  setHeaderData 之后 headerData(2,Horizontal) ="
                << model.headerData(2, Qt::Horizontal, Qt::DisplayRole).toString()
                << "（同时 emit 了 headerDataChanged(Qt::Horizontal, 2, 2)，视图的表头会重画那一节）";
}

// ============================================================================
// 九、证据③：漏发 dataChanged —— 两个模型实例的对照
// ============================================================================
static void evidenceMissedSignal(TaskModel &modelBad, TaskModel &modelGood,
                                 ListeningView &viewBad, ListeningView &viewGood)
{
    qInfo() << "【证据③】漏发信号的现场：模型自测对、视图不知道、一滚就露馅";

    // 视图 A 接**坏模型**，视图 B 接**好模型**，各画一次打底
    viewBad.setModel(&modelBad);
    viewGood.setModel(&modelGood);
    viewBad.resize(360, 240);
    viewGood.resize(360, 240);
    viewBad.show();
    viewGood.show();
    flushEvents();
    ListeningView::resetHits();
    TaskModel::resetCounters();

    const int row = 5;
    const QModelIndex cellBad = modelBad.index(row, ColProgress);
    const QModelIndex cellGood = modelGood.index(row, ColProgress);
    qInfo() << QString::asprintf("  起点：两个模型的第 %d 行进度都是 %d",
                                 row, modelBad.data(cellBad, Qt::EditRole).toInt());

    // 甲：改存储、不发信号
    modelBad.pokeProgress(row, 88);
    // 乙：规矩地走 setData（内部 emit）
    modelGood.setProgressProperly(row, 88);
    ListeningView::resetHits();
    flushEvents();

    qInfo() << "  甲（pokeProgress，不发 dataChanged）："
            << "模型自测 data() =" << modelBad.data(cellBad, Qt::DisplayRole).toString()
            << "（**已经是 88% 了，数据确实改了**）"
            << "｜视图的 dataChanged 槽命中 =" << ListeningView::viewDataChangedHits() << "次";
    const qint64 badHits = ListeningView::viewDataChangedHits();
    const qint64 badPaintsA = ListeningView::paints();

    // 对照：乙
    ListeningView::resetHits();
    flushEvents();
    qInfo() << "  乙（setProgressProperly，emit 了）："
            << "模型自测 data() =" << modelGood.data(cellGood, Qt::DisplayRole).toString()
            << "｜视图的 dataChanged 槽命中 =" << ListeningView::viewDataChangedHits() << "次";
    const qint64 goodHits = ListeningView::viewDataChangedHits();
    qInfo() << QString::asprintf("  → 一句话结论：**模型侧 data() 两个都对（%d/%d），"
                                 "但视图只听见了乙（甲 %lld 次、乙 %lld 次）**。"
                                 "用户看到的就是「程序改完数据，屏幕不动」。",
                                 modelBad.data(cellBad, Qt::EditRole).toInt(),
                                 modelGood.data(cellGood, Qt::EditRole).toInt(),
                                 static_cast<long long>(badHits), static_cast<long long>(goodHits));

    // 决定性一击：强制重画，坏模型那边立刻「露馅」——因为 data() 是拉取的
    ListeningView::resetHits();
    viewBad.viewport()->repaint();
    qInfo() << QStringLiteral("  对甲的视图**强制 repaint** 之后：paintEvent %1→%2 次、"
                              "dataChanged 槽命中仍是 %3 次（视图压根不知道要更新），"
                              "但屏幕上的格子会画成 88%——"
                              "因为重画时视图重新去问 data()，拿到的就是新值。")
                       .arg(static_cast<long long>(badPaintsA))
                       .arg(static_cast<long long>(ListeningView::paints()))
                       .arg(static_cast<long long>(ListeningView::viewDataChangedHits()));
    qInfo() << "  → 现场表现：**「一滚动/一 resize 就自己变对了」的 bug，八成就是漏发 dataChanged**。"
               "它不崩、不报错，只在用户眼皮底下错，最难查。";
    qInfo() << "  两个模型的 setData 统计：成功" << TaskModel::setDataOk()
            << "次、poke 类「只改存储」" << TaskModel::pokes() << "次（后者是反面计数）";
    qInfo() << "  补一句 roles 参数的意义：本讲 emit 时给了 {0,2,3}（Display/Edit/ToolTip），"
               "如果 roles 留空（默认 QList<int>()），视图会把这一格**当全角色重画**——"
               "正确但更贵；如果 roles 写了但值其实没变，视图会白重画。第 8 讲讲这个调优。";
    qInfo() << "  轨迹（甲乙合流）：" << Trace::join();
}

// ============================================================================
// 十、证据④：insertRows / removeRows 与 begin/end 的配对
// ============================================================================
static void evidenceRowStructure(TaskModel &model, ListeningView &view)
{
    qInfo() << "【证据④】insertRows/removeRows：begin/end 之间**不能重入视图**，顺序不能反";

    view.setModel(&model);
    flushEvents();
    ListeningView::resetHits();

    int insertedSeen = 0;
    int aboutSeen = 0;
    int removedSeen = 0;
    // 带 QPrivateSignal 的信号：能 connect（少写尾部参数即可），**不能 emit**（第 365~369 行）
    QObject::connect(&model, &QAbstractItemModel::rowsInserted,
                     [&insertedSeen](const QModelIndex &p, int first, int last) {
                         ++insertedSeen;
                         Q_UNUSED(p) Q_UNUSED(first) Q_UNUSED(last)
                     });
    QObject::connect(&model, &QAbstractItemModel::rowsAboutToBeInserted,
                     [&aboutSeen](const QModelIndex &p, int first, int last) {
                         ++aboutSeen;
                         Q_UNUSED(p) Q_UNUSED(first) Q_UNUSED(last)
                     });
    QObject::connect(&model, &QAbstractItemModel::rowsRemoved,
                     [&removedSeen](const QModelIndex &p, int first, int last) {
                         ++removedSeen;
                         Q_UNUSED(p) Q_UNUSED(first) Q_UNUSED(last)
                     });

    const int rowsBefore = model.rowCount();
    const QModelIndex anchor = model.index(rowsBefore - 1, 0);   // 攥着最后一行的坐标
    const QString anchorTextBefore = anchor.data(Qt::DisplayRole).toString();
    const QPersistentModelIndex guard(anchor);
    const int guardRowBefore = guard.row();
    const bool ok = model.insertRows(0, 3);
    flushEvents();
    qInfo() << QString::asprintf("  insertRows(0,3) 返回 %s：行数 %d → %d ｜ "
                                 "rowsAboutToBeInserted %d 次、rowsInserted %d 次、"
                                 "视图的 rowsInserted 槽命中 %lld 次",
                                 ok ? "true" : "false", rowsBefore, model.rowCount(),
                                 aboutSeen, insertedSeen,
                                 static_cast<long long>(ListeningView::viewRowsInsertedHits()));
    qInfo() << "  插入对旧坐标的影响：anchor 之前指「" << anchorTextBefore << "」，"
            << "现在同一个坐标指「" << anchor.data(Qt::DisplayRole).toString() << "」"
            << "（在 0 处插 3 行 → 老行号全体后移 3，**裸 index 不会告诉你这件事**）；"
            << "护栏版 guard：row " << guardRowBefore << " → " << guard.row()
            << "、isValid =" << guard.isValid()
            << "（QPersistentModelIndex 会跟着调整，第 1 讲证据③的另一半）";
    qInfo() << "  删掉刚插的三行：removeRows(0,3) =" << model.removeRows(0, 3)
            << "｜行数回到" << model.rowCount() << "｜rowsRemoved 命中" << removedSeen << "次";
    qInfo() << "  越界的调用一律被拒：insertRows(-1,1) =" << model.insertRows(-1, 1)
            << "｜removeRows(99999,1) =" << model.removeRows(99999, 1)
            << "（**先检查再 begin**：一旦 begin 了就必须 end，半途 return 会把视图卡在「等通知」的状态）";
    qInfo() << "  纪律：begin/end 必须**严格配对、不许提前 return**；begin 之后到 end 之前，"
               "容器必须已经改到位、并且**别在这段时间里碰视图**（emit 别的信号、开编辑器都不行）。";
}

// ============================================================================
// 十一、证据⑤：reset 与 insertRows 的取舍 —— 选择集和滚动位置
// ============================================================================
static void evidenceResetVsInsert(TaskModel &model, ListeningView &view)
{
    qInfo() << "【证据⑤】beginResetModel/endResetModel 的代价：视图失忆";

    view.setModel(&model);
    view.show();
    flushEvents();

    // 先把视图摆到一个「有位置、有选择」的状态
    const int rows = model.rowCount();
    const QModelIndex target = model.index(rows - 1, 0);
    view.scrollTo(target, QAbstractItemView::PositionAtBottom);
    flushEvents();
    const QModelIndex picked = model.index(rows - 5, 0);
    view.selectionModel()->select(picked,
                                  QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    flushEvents();
    const QPersistentModelIndex guard(picked);        // 攥住「被选中的那一行」的护栏坐标
    const int scrollBefore = view.scrollValue();
    const int selBefore = view.selectionModel()->selectedRows().size();
    const int guardRowBefore = guard.row();           // 后面甲、乙两段都要对照它
    qInfo() << QString::asprintf("  操作前：滚动条 value = %d（共 %d 行，已滚到底）、选中 %d 行、"
                                 "护栏 guard.row() = %d isValid = %d",
                                 scrollBefore, rows, selBefore, guard.row(), guard.isValid() ? 1 : 0);

    // 甲：走 begin/endInsertRows（增量）
    QElapsedTimer t;
    t.start();
    model.insertRows(rows, 20);
    const qint64 usInsert = t.elapsed();
    flushEvents();
    qInfo() << QString::asprintf("  甲 insertRows(末尾 +20 行) 耗时 %lld us：滚动位置 %d → %d、"
                                 "选中行数 %d → %d、护栏 row %d → %d"
                                 "（**都保住了**：视图只是把尾巴接上，前面什么都没动）",
                                 static_cast<long long>(usInsert), scrollBefore, view.scrollValue(),
                                 int(selBefore), int(view.selectionModel()->selectedRows().size()),
                                 guardRowBefore, guard.row());

    // 乙：走 begin/endResetModel（整份换新）
    const int scrollBeforeReset = view.scrollValue();
    const int selBeforeReset = view.selectionModel()->selectedRows().size();
    const bool guardValidBefore = guard.isValid();
    t.restart();
    model.reloadEverything(200);
    const qint64 usReset = t.elapsed();
    flushEvents();
    qInfo() << QString::asprintf("  乙 reloadEverything(200 行，reset 家族) 耗时 %lld us："
                                 "滚动位置 %d → %d、选中行数 %d → %d、"
                                 "护栏 row/isValid %d/%d → %d/%d",
                                 static_cast<long long>(usReset),
                                 scrollBeforeReset, view.scrollValue(),
                                 int(selBeforeReset), int(view.selectionModel()->selectedRows().size()),
                                 guardRowBefore, guardValidBefore ? 1 : 0,
                                 guard.row(), guard.isValid() ? 1 : 0);
    qInfo() << "     ← 这一行就是「reset 让所有 index 作废」的实测口径（第 1 讲证据③的另一面）："
               "QPersistentModelIndex 变成无效，裸坐标更是一个都不能信。";
    qInfo() << "  → 取舍：**数据「换了一批」用 reset**（配置重载、服务器整表拉回、SQL 重新查询）；"
               "**只是在头/中/尾加了几个，就用 insertRows/removeRows**。";
    qInfo() << "     能用增量就别 reset：视图的选中集、当前项、滚动位置、展开状态、"
               "以及**所有** QPersistentModelIndex 都会没，用户只觉得「界面自己动了」。";
    qInfo() << "  另：reset 期间绝对不要碰视图、也不要 emit 别的信号；"
               "begin 之前视图看到的还是老数据，end 之前你说什么它都不认。";
}

// ============================================================================
// 十二、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("model_view_03"));

    TaskModel modelMain;
    TaskModel modelBad;
    TaskModel modelGood;
    ListeningView viewMain;
    ListeningView viewBad;
    ListeningView viewGood;

    QTimer::singleShot(0, [&]() {
        qInfo() << "================================================================";
        qInfo() << "model_view 第 3 讲 · 编辑、flags 与 dataChanged 回路（平台 ="
                << QGuiApplication::platformName() << ", Qt =" << QT_VERSION_STR << "）";
        qInfo() << "================================================================";

        modelMain.seed(60);
        modelBad.seed(40);
        modelGood.seed(40);
        viewMain.setEditTriggers(QAbstractItemView::AllEditTriggers);

        evidenceFlags(modelMain, viewMain);
        evidenceCommitLoop(modelMain, viewMain);
        evidenceMissedSignal(modelBad, modelGood, viewBad, viewGood);
        evidenceRowStructure(modelMain, viewMain);
        evidenceResetVsInsert(modelMain, viewMain);

        QTimer::singleShot(60, []() {
            qInfo() << "【收尾】回路背一遍：**flags 给权限 → 视图触发编辑 → 委托 setModelData → "
                       "model.setData（验 index/验 role/校验/比变化）→ emit dataChanged → "
                       "视图槽 → 重画时再问 data()**。中间断哪一环，界面就在哪一环骗你。";
            QCoreApplication::quit();
        });
    });

    return app.exec();
}

// ============================================================================
// 十三、速查表与纪律（背不住就回来查）
// ============================================================================
/*
 * 一、setData 的七步（顺序就是纪律）
 *   ① !index.isValid() / 行越界 → return false；
 *   ② role != 你接的那个（EditRole / CheckStateRole / 自定义）→ return false；
 *      **不要顺手接 Qt::DisplayRole**：那是给人看的文字，编辑器不该往它写，
 *      接了就会出现「用户输入的字符串被当显示文本存下来，原始 int 永远改不掉」。
 *   ③ !flags(index).testFlag(Qt::ItemIsEditable) → return false（**权限在 flags 里，别忘查**）；
 *   ④ 校验 + 换算（value.toInt(&ok) 一定要接 ok；范围/格式/业务规则在这里挡）；
 *   ⑤ 新旧相同 → return true，**但不 emit**（防无谓重画、防撤销栈灌空操作）；
 *   ⑥ 改内部存储（这一步之后模型自测就已经是「对的」了）；
 *   ⑦ emit dataChanged(index, index, {真的变了的角色})。
 *   返回值语义：true = 「我接受并处理了这个值」，不是「屏幕已经画好了」。
 *
 * 二、四套「通知」的分工（别混着用）
 *   改一格的数据   → dataChanged(topLeft, bottomRight, roles)         （第 359 行）
 *   改表头         → headerDataChanged(orientation, first, last)      （第 361 行）
 *   增/删行（列）  → beginInsertRows/endInsertRows、beginRemoveRows/endRemoveRows
 *                    （第 402~406 行；视图侧对应 rowsInserted / rowsAboutToBeRemoved 槽，第 235/236 行）
 *   整份换新       → beginResetModel/endResetModel                     （第 420/421 行）
 *                    代价：选择集、当前项、滚动位置、展开状态、所有 index
 *                    与 QPersistentModelIndex **全部作废**。
 *   换结构（列的增减/重新解释） → layoutAboutToBeChanged / layoutChanged
 *                    （第 362/363 行；本讲没动它，第 5 讲代理模型里再遇）
 *
 * 三、QPrivateSignal 是什么、为什么你必须知道
 *   rowsInserted / rowsRemoved / modelReset / columnsInserted … 尾巴上都挂着一个 QPrivateSignal
 *   （第 365~378 行）。它是 Qt 用来「禁止外部 emit」的私有类型标记：
 *     · connect 时**少写最后那个参数**就行（本讲三个 lambda 都是 3 参数版接 4 参数信号）；
 *     · 你自己 emit rowsInserted(...) 编不过 —— 也就提醒你：**行数变化只有模型自己能宣布**。
 *   而 dataChanged / headerDataChanged 没有 QPrivateSignal，正是给你在 setData 里发的。
 *
 * 四、两个视图并排测「漏发信号」的设计（本讲证据③的机关）
 *   为什么不用一个视图测两次？因为视图有重画缓存与脏区合并，前一次的更新会污染后一次的计数。
 *   两个模型 + 两个视图 + 每轮 resetHits()，才能把「命中 0 次」和「命中 1 次」干净地摆在一起。
 *   取证分三层，缺一条都不算证明：
 *     模型层：data() 给的是新值（说明「数据确实改了」）；
 *     信号层：视图的 dataChanged 槽命中次数（说明「视图听没听见」）；
 *     像素层：强制 repaint 后屏幕变对（说明「不是画不出来，是没人叫它画」）。
 *
 * 五、现场最容易中的五条
 *   ① flags 覆写时凭空 return Qt::ItemIsEditable —— 把 Selectable|Enabled 丢了，
 *      结果整格不能选、不能点，还以为是视图的 bug。正解：以 QAbstractTableModel::flags(index) 打底再 |=。
 *   ② setData 里改完存储忘了 emit —— 一滚就对、不动就错，最难查（证据③）。
 *   ③ emit dataChanged 后**没有让视图的槽转到基类实现**（覆写视图槽时忘了调 base）——
 *      本讲 ListeningView::dataChanged 最后一行必须 QTableView::dataChanged(...)。
 *   ④ begin/end 中间 return —— 视图卡在等待状态，轻则行数不对，重则断言。
 *   ⑤ 在 begin/endInsertRows 里 emit dataChanged 或调 view->update(index) —— 重入，
 *      行数还没对齐就问数据，越界就发生在这一瞬间。
 *
 * ——— 老师傅的话 ———
 *   徒弟，「改了数据界面不动」这句话，你以后要能条件反射地拆成三种病：
 *   一是**没改存储**（data() 还是旧值，去查 setData 的校验是不是把你挡了）；
 *   二是**改了存储没发信号**（data() 是新值、视图槽命中 0，就是证据③那个样子）；
 *   三是**发了信号但范围/角色不对**（视图只重画了隔壁格子，或者 roles 写空把整行都刷了）。
 *   三种病的药完全不同。你别一上来就加 repaint()「顶一下」，那是把第二种病当第三种治，
 *   顶得了一时顶不了一世。
 *
 *   第二件事：**flags 是权限，editTriggers 是习惯**。用户能不能编辑，取决于这两位的乘积。
 *   我在证据①把四列的 flags 数值都打出来了，你以后排查「为什么这一列不能编辑」，
 *   第一件事是把 flags 的 0x 值念出来（1=Selectable, 2=Editable, 16=UserCheckable, 32=Enabled），
 *   第二件事是念一遍 view->editTriggers()。别猜。
 *
 *   第三件事：reset 很爽，但它是**大锤**。整表重拉的时候用它没错；
 *   可有人每来一条新消息就 reset 一次——用户刚点中的行没了、滚到一半弹回顶部、
 *   展开的树全合上，这种产品体验就是这一行代码造成的。证据⑤把滚动 value 与选中行数
 *   的前后对照打出来了，就是这个目的。增量能用 insertRows/removeRows，别偷闲抡大锤。
 *
 * ——— 小练习 ———
 *   1. 给 ColProgress 加一条「只许每次 ±10 以内」的业务校验，让 setData 拒绝越界的提交，
 *      然后把 setDataRejected 的计数打出来；再故意把第 ⑤ 步「值没变就 return true 不 emit」
 *      去掉，用同一格重复 setData(77) 十次，对比视图 dataChanged 槽命中次数与 paintEvent 次数。
 *   2. 把 dataChanged 的第三个参数换成 QList<int>()（空），观察视图重画范围的变化：
 *      在 ListeningView 里把 roles 打印出来，并在 fill 时用 Qt::SizeHintRole 给一个
 *      自定义行高，看「空 roles」会不会把行高也重新算一遍（体会「roles 写得准，视图省得多」）。
 *   3. 用 reset 与增量两条路各做一次「删除前 10 行」，把三组数字列成表：
 *      删除前后 view.scrollValue()、selectedRows().size()、
 *      以及你自己造的 QPersistentModelIndex 的 isValid()。
 *      再回答：为什么增量删除时，被删行**之后**的选中行会整体前移而不是消失？
 */
// ============================================================================

// 本文件有两个 Q_OBJECT 类（TaskModel / ListeningView），AUTOMOC 生成后在这里包含。
#include "03_editing_and_flags.moc"
