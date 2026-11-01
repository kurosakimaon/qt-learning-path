// ============================================================================
// 第 2 讲 · 手写 QAbstractTableModel：把「自己的数据」挂上视图
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   第 1 讲用现成的 QStandardItemModel 跑通了闭环，这一讲把便利类扔掉：
//   **数据本来就是自己的**（一个 std::vector 结构体），要让它出现在 QTableView 里，
//   就得继承 QAbstractTableModel，把「答话」这一半自己写出来。
//     1) 必须实现的成员逐个核过签名（const、参数表、返回类型一个都不能差）：
//        index / parent / rowCount / columnCount / data ——前四个是 QAbstractItemModel
//        的纯虚函数，data 也是纯虚；**QAbstractTableModel 已经把 index() 与 parent()
//        给了默认实现**（qabstractitemmodel.h 第 476/491 行），所以「继承它最少要写
//        哪三个」和「继承 QAbstractItemModel 最少要写哪五个」是两道不同的题，
//        本讲两个都讲，并且**全都自己实现一遍**，因为默认实现不含 internalPointer。
//     2) 加分项：headerData（横竖表头的文字都从这儿来）、roleNames（把角色名交给 QML/委托）、
//        以及角色枚举本身的真名与真值（Qt::DisplayRole=0 … Qt::UserRole=0x0100=256，
//        **Qt6 已经没有 Qt::SortRole**）。
//     3) index() 为什么必须用 createIndex、表格的 parent() 为什么返回 QModelIndex()、
//        internalPointer 到底存什么、以及它最咬人的一条：**生命周期**。
//        std::vector 扩容会换掉整块内存，之前发出去的每个 index 里揣着的指针当场变悬垂——
//        本讲把两次指针值打出来给你看（只比地址，**绝不解引用**悬垂指针）。
//     4) 最有说服力的一条证据：挂 **5000 行 × 4 列**，把 data() 装个计数器，
//        打印「视图到底问了多少格、问的是哪些行」，证明视图只问可见的那几个格子。
//
// 对应构建目标名：model_view_02_custom_table_model
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(model_view_02_custom_table_model 02_custom_table_model.cpp)
//   target_link_libraries(model_view_02_custom_table_model PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)                       # 本文件**有 Q_OBJECT 类**，需要 moc
//   # 手工核对时：moc 02_custom_table_model.cpp -o 02_custom_table_model.moc
//   #             文件末尾那句 #include "02_custom_table_model.moc" 就是给 AUTOMOC 的钩子
//
// 运行前须知：
//   · 无人值守：show → 滚两下 → 打印 → QCoreApplication::quit()，全程无鼠标键盘；
//     **没有 QDialog::exec()、没有裸 QEventLoop::exec()**，只用 processEvents() 催重绘。
//   · 可以无头跑：`model_view_02_custom_table_model -platform offscreen`。
//   · 数据全部在内存里现造（5000 行结构体），**不读任何文件、不联网**。
//   · data() 调用次数依赖窗口尺寸与样式；我只承诺数量级关系：
//     「问到的行数」是可见行那一段，不是 0..4999 全区间。
//   · 图解页面：../visuals/model_view.html（第 1 讲那张五角色图，这讲看 Model 那一格）。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 model_view_02_custom_table_model，跑到自动退出、退出码 0）。
//     但本讲**没有汇总自检行**，退出码 0 只证明「没崩、走完了」，不证明正文每个数字都对：
//     带具体数字的「预期」请以你自己那次运行输出为准（本机那一轮存档 runlogs/model_view_02_custom_table_model.log）。
// ============================================================================

#include <QApplication>              // QtWidgets/qapplication.h
#include <QAbstractTableModel>       // QtCore/qabstractitemmodel.h（转发头 QtCore/QAbstractTableModel）
#include <QTableView>                // QtWidgets/qtableview.h
#include <QHeaderView>               // QtWidgets/qheaderview.h（讲表头怎么问模型）
#include <QModelIndex>
#include <QVariant>                  // QtCore/qvariant.h
#include <QString>
#include <QStringList>
#include <QHash>                     // roleNames 的返回类型
#include <QSet>                      // 取证用：记下「哪些行真被问过」
#include <QByteArray>
#include <QColor>                    // QtGui/qcolor.h：BackgroundRole 用它
#include <QRect>
#include <QSize>
#include <QMetaType>                 // QMetaType::Int / Float 之类，打印 typeId 用
#include <QElapsedTimer>
#include <QTimer>
#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QtGlobal>
#include <QDebug>

#include <vector>                    // 数据源就是它：真实项目里它可能是你的业务容器/数据库缓存
#include <cstdint>

// ============================================================================
// 一、核对签名（本机 D:\Qt\6.12.0\mingw_64\include，Qt 6.12.0）
// ============================================================================
// 必须覆写的五个（QAbstractItemModel 里的纯虚函数，qabstractitemmodel.h）：
//   第 274/275 行 virtual QModelIndex index(int row, int column,
//                              const QModelIndex &parent = QModelIndex()) const = 0;
//   第 276 行     virtual QModelIndex parent(const QModelIndex &child) const = 0;
//   第 279 行     virtual int rowCount(const QModelIndex &parent = QModelIndex()) const = 0;
//   第 280 行     virtual int columnCount(const QModelIndex &parent = QModelIndex()) const = 0;
//   第 283 行     virtual QVariant data(const QModelIndex &index,
//                              int role = Qt::DisplayRole) const = 0;
//                    ← 注意第三行的 **int role**，不是 QAbstractItemModel::ItemDataRole role，
//                      也不要写成 Qt::ItemDataRole（那样签名不同、override 会直接编不过）
// 常一起覆写的：
//   第 284 行 virtual bool setData(const QModelIndex &index, const QVariant &value,
//                              int role = Qt::EditRole);                       ← 第 3 讲
//   第 286/287 行 virtual QVariant headerData(int section, Qt::Orientation orientation,
//                              int role = Qt::DisplayRole) const;
//   第 324 行 virtual Qt::ItemFlags flags(const QModelIndex &index) const;      ← 第 3 讲
//   第 333 行 virtual QHash<int,QByteArray> roleNames() const;
//   第 281 行 virtual bool hasChildren(const QModelIndex &parent = QModelIndex()) const;
// 框架给你的工具（**都在 protected 段**，只能子类自己调用）：
//   第 396 行 inline QModelIndex createIndex(int row, int column,
//                              const void *data = nullptr) const;
//   第 397 行 inline QModelIndex createIndex(int row, int column, quintptr id) const;
//                    ← 两个重载都能吃「0 类型」的东西，所以传**裸 0** 要在 const void* 与
//                      quintptr 之间犯歧义；要传空就写 nullptr，要传 id 就写 quintptr(...)
//   第 273 行 Q_INVOKABLE bool hasIndex(int row, int column,
//                              const QModelIndex &parent = QModelIndex()) const;  ← public
//   第 402~406 / 420 / 421 行 begin/endInsertRows、begin/endRemoveRows、begin/endResetModel
// QAbstractTableModel 已经替你做了的事（第 468~493 行）：
//   第 476 行 QModelIndex index(int, int, const QModelIndex &parent = QModelIndex()) const override;
//   第 491 行 QModelIndex parent(const QModelIndex &child) const override;   ← 在 **private:** 段里
//   第 481 行 Qt::ItemFlags flags(const QModelIndex &) const override;
//   第 483 行 using QObject::parent;      ← 不加这句，无参的 QObject::parent() 会被遮蔽
//                    → 你自己覆写 parent(const QModelIndex&) 之后，**派生类对象上 model->parent()
//                      （无参）会因名字遮蔽而编不过**；要么加 using，要么写 QObject::parent()，
//                      要么老老实实通过 QAbstractItemModel* 指针调（本讲只用后者与 QModelIndex::parent()）
// 角色的真名与真值（QtCore/qnamespace.h 第 1517~1554 行，enum ItemDataRole）：
//   DisplayRole=0  DecorationRole=1  EditRole=2  ToolTipRole=3  StatusTipRole=4
//   WhatsThisRole=5  FontRole=6  TextAlignmentRole=7  BackgroundRole=8  ForegroundRole=9
//   CheckStateRole=10  AccessibleTextRole=11  AccessibleDescriptionRole=12  SizeHintRole=13
//   InitialSortOrderRole=14 … **UserRole = 0x0100（= 256）**
//   ← 自定义角色从 Qt::UserRole + 1 起编号（本讲用 257/258/259）；
//     别学老代码用「32 往上」，那是 Qt4 时代的账本；
//     也**别用 Qt::SortRole**：Qt6 的 ItemDataRole 里没有这一项，排序统一走 EditRole。
// 视图侧相关（QtWidgets/qabstractitemview.h / qtableview.h）：
//   第 177 行 virtual void scrollTo(const QModelIndex &, ScrollHint = EnsureVisible) = 0;
//   第 231/232 行（protected 槽）virtual void dataChanged(const QModelIndex&, const QModelIndex&,
//                              const QList<int> &roles = QList<int>());   ← 第 3 讲的取证点
//   qtableview.h 第 38/39 行 horizontalHeader()/verticalHeader()；第 102/103 行
//   qheaderview.h 的 setSectionResizeMode（本讲调一下，让 5000 行别把列宽算爆）
// QtGui/qstandarditemmodel.h 第 33/35 行（便利类的默认角色，本讲不用它、但别忘了它）：
//   virtual QVariant data(int role = Qt::UserRole + 1) const;
//   virtual void setData(const QVariant &value, int role = Qt::UserRole + 1);
// ============================================================================

// ============================================================================
// 二、数据源：真实项目里它就是你的业务容器
// ============================================================================
struct DeviceRow {
    QString name;        // 设备名
    int     progress = 0; // 巡检完成度 0..100
    QString owner;        // 负责人
    int     priority = 0; // 优先级 1..5
};

// 列号自己定义成枚举，**别让散落的 0/1/2 出现在 data() 的 switch 里**
enum DeviceColumn {
    ColName = 0,
    ColProgress,
    ColOwner,
    ColPriority,
    ColCount                 // 列数 = 4
};

// 自定义角色：从 Qt::UserRole + 1 起（Qt::UserRole == 0x0100 == 256）
enum DeviceRole {
    RoleProgressValue = Qt::UserRole + 1,   // 257：给图表/委托用的原始 int
    RoleOwnerName     = Qt::UserRole + 2,   // 258
    RolePriorityStars = Qt::UserRole + 3    // 259
};

// ============================================================================
// 三、手写表格模型
// ============================================================================
// 有 Q_OBJECT：本讲虽然不发自己的信号，但保留元对象是正经写法——
// 一旦你将来加自定义信号/槽/Q_PROPERTY（比如 modelVersion 属性），不用返工。
class DeviceTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit DeviceTableModel(QObject *parent = nullptr) : QAbstractTableModel(parent) {}

    // ── 纯虚①：index(row, column, parent) ────────────────────────────
    // 「第 row 行第 column 列那一格的**坐标**是什么」。表格的解读：
    //   · parent 必须是无效 index（根），否则这一格不存在；
    //   · 越界一律返回 QModelIndex()，**不要断言、不要崩**；
    //   · createIndex 的第三个参数塞什么由你自己决定，框架只负责原样带回来。
    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override
    {
        // 表格：任何真实格子下面都没有子行，所以 parent 有效就意味着「没有子项」
        if (parent.isValid())
            return QModelIndex();
        if (!hasIndex(row, column, parent))       // 越界检查交给框架（第 273 行）
            return QModelIndex();
        // 把行结构体的地址塞进 internalId —— 第 4 小节会演示它的代价
        return createIndex(row, column, &m_rows.at(size_t(row)));
    }

    // ── 纯虚②：parent(child) ────────────────────────────────────────
    // 表格只有一层：除了根（无效 index）没有别的父亲，所以**恒返回 QModelIndex()**。
    // 这条不是偷懒：视图拿它去走「展开/折叠」那条逻辑，返回无效 = 「上面就是根」。
    // 签名核对：第 276 行 `virtual QModelIndex parent(const QModelIndex &child) const = 0;`
    // 注意它和 QObject::parent()（无参）同名不同签名 —— 见第一节第 483 行的 using 说明。
    QModelIndex parent(const QModelIndex &child) const override
    {
        Q_UNUSED(child)
        return QModelIndex();
    }

    // ── 纯虚③：rowCount(parent) ─────────────────────────────────────
    // 视图会拿两种参数来问：**无效 index（= 根，问整表行数）**和某个真实格子
    // （= 问这一格下面有几行子行）。表格的答案分别是「全部行数」和「0」。
    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.isValid())
            return 0;                             // 格子里不再套行
        return int(m_rows.size());
    }

    // ── 纯虚④：columnCount(parent) ──────────────────────────────────
    // **别在这里加 parent.isValid() 的分支**：列数是全局属性，
    // 视图问「表头有几节」「这一行有几列」时传的可能是真实 index，
    // 你返回 0 的话表头就少一节、行就少画一格（这是自写模型最常见的错之一）。
    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        Q_UNUSED(parent)
        return ColCount;
    }

    // ── 纯虚⑤：data(index, role) ───────────────────────────────────
    // 全部证据都从这里流出去，所以计数器装在这。
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        ++s_dataCalls;
        s_roleHits[role] += 1;
        if (index.isValid()) {
            s_queriedRows.insert(index.row());
            if (index.row() < s_minRow) s_minRow = index.row();
            if (index.row() > s_maxRow) s_maxRow = index.row();
        } else {
            ++s_dataCallsInvalidAsked;
        }
        if (!index.isValid())
            return QVariant();                    // 无效坐标：返回**无效 QVariant**，不是空串

        const DeviceRow *row = rowOf(index);      // 见下面 internalPointer 那段
        if (!row)
            return QVariant();

        switch (role) {
        case Qt::DisplayRole:                      // 给人看的文字
            switch (index.column()) {
            case ColName:     return row->name;
            case ColProgress: ++s_formatCalls;     // ← 每问一次就拼一个 QString，代价见证据④
                                return QStringLiteral("%1%").arg(row->progress);
            case ColOwner:    return row->owner;
            case ColPriority: ++s_formatCalls;
                                return QStringLiteral("P%1").arg(row->priority);
            }
            return QVariant();
        case Qt::EditRole:                         // 给编辑器/排序/过滤用的**原始值**
            switch (index.column()) {
            case ColName:     return row->name;
            case ColProgress: return row->progress;
            case ColOwner:    return row->owner;
            case ColPriority: return row->priority;
            }
            return QVariant();
        case Qt::ToolTipRole:
            return QStringLiteral("%1｜完成 %2%%3").arg(row->name)
                       .arg(row->progress)
                       .arg(row->priority >= 4 ? QStringLiteral("（高优先级）") : QString());
        case Qt::TextAlignmentRole:                // 数字右对齐、文字左对齐
            if (index.column() == ColProgress || index.column() == ColPriority)
                return int(Qt::AlignRight | Qt::AlignVCenter);
            return int(Qt::AlignLeft | Qt::AlignVCenter);
            // ← 该角色要返回**能转成 Qt::Alignment 的 int**，视图内部 qvariant_cast<int>
        case Qt::DecorationRole:                   // 图标/颜色：进度满 100 就给个色块
            if (index.column() == ColProgress && row->progress >= 100)
                return QColor(60, 170, 90);
            return QVariant();
        case Qt::BackgroundRole:
            if (row->priority >= 4 && index.column() == ColName)
                return QColor(255, 236, 236);
            return QVariant();
        case Qt::UserRole + 1:                     // 自定义角色 257
            if (index.column() == ColProgress)
                return row->progress;
            return QVariant();
        case Qt::UserRole + 2:                     // 258
            if (index.column() == ColOwner)
                return row->owner;
            return QVariant();
        case Qt::UserRole + 3:                     // 259
            if (index.column() == ColPriority)
                return row->priority;
            return QVariant();
        default:
            return QVariant();                     // **没实现的角色一律返回无效 QVariant**
        }
    }

    // ── 加分①：headerData(section, orientation, role) ────────────────
    // 横表头 = 列名，竖表头 = 行号。视图的表头也是一个「画格子」的东西，
    // 它同样来问模型，**不是**在 QTableView 上写死。
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override
    {
        ++s_headerCalls;
        if (role == Qt::DisplayRole) {
            if (orientation == Qt::Horizontal) {
                switch (section) {
                case ColName:     return QStringLiteral("设备");
                case ColProgress: return QStringLiteral("巡检进度");
                case ColOwner:    return QStringLiteral("负责人");
                case ColPriority: return QStringLiteral("优先级");
                }
                return QVariant();
            }
            return section + 1;                    // 竖表头给行号（int 也行，QVariant 会转）
        }
        if (role == Qt::TextAlignmentRole)
            return int(orientation == Qt::Horizontal ? Qt::AlignCenter : Qt::AlignRight);
        if (role == Qt::ToolTipRole && orientation == Qt::Horizontal)
            return QStringLiteral("第 %1 列").arg(section);
        return QVariant();
    }

    // ── 加分②：roleNames() ─────────────────────────────────────────
    // 把角色号翻译成**名字**。给谁用？
    //   · QML：ListView 里 model.device / model.progressValue 就是靠它；
    //   · QStyledItemDelegate 的某些路径、以及调试输出；
    //   · 视图本身画格子**不看它**，视图看的是 data(index, role)。
    // 键是 int（角色号），值是 QByteArray（ASCII 名字，别放中文，它是给代码读的标识符）。
    QHash<int, QByteArray> roleNames() const override
    {
        ++s_roleNamesCalls;
        return {
            { Qt::DisplayRole,   "display" },
            { Qt::EditRole,      "edit" },
            { Qt::ToolTipRole,   "toolTip" },
            { Qt::DecorationRole,"decoration" },
            { Qt::BackgroundRole,"background" },
            { RoleProgressValue, "progressValue" },
            { RoleOwnerName,     "ownerName" },
            { RolePriorityStars, "priorityStars" },
        };
    }

    // ── 造数据 / 追加数据（第 3 讲正面讲 begin/end 那套，这里先用起来）──
    void fill(int count)
    {
        beginResetModel();                         // 一次性灌数据：reset 是合理的（还没有视图在看）
        m_rows.clear();
        m_rows.reserve(size_t(count));
        const QStringList owners { QStringLiteral("张工"), QStringLiteral("李工"),
                                   QStringLiteral("王工"), QStringLiteral("赵工"),
                                   QStringLiteral("陈工") };
        for (int i = 0; i < count; ++i) {
            DeviceRow r;
            r.name = QStringLiteral("设备 %1").arg(i + 1);
            r.progress = (i * 7) % 101;
            r.owner = owners.at(size_t(i % owners.size()));
            r.priority = 1 + (i % 5);
            m_rows.push_back(r);
        }
        endResetModel();
    }

    // 走正规流程追加一段行（**必须先 beginInsertRows、后 endInsertRows**）
    void appendRows(int count)
    {
        const int first = int(m_rows.size());
        const int last = first + count - 1;
        beginInsertRows(QModelIndex(), first, last);
        for (int k = 0; k < count; ++k) {
            DeviceRow r;
            r.name = QStringLiteral("设备 %1").arg(first + k + 1);
            r.progress = 0;
            r.owner = QStringLiteral("新来的");
            r.priority = 3;
            m_rows.push_back(r);
        }
        endInsertRows();
    }

    // 只给「不通知视图」的内部改法（第 3 讲要用它做反面教材，本讲只用它取地址）
    std::vector<DeviceRow> &rawRows() { return m_rows; }
    const std::vector<DeviceRow> &rawRows() const { return m_rows; }

    // ── internalPointer 的正解：地址可能已经过期，所以先看它、再用行号兜底 ──
    // 这条函数是本讲的技术核心：**永远不要把 internalPointer 当成「一定有效」**。
    const DeviceRow *rowOf(const QModelIndex &index) const
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= int(m_rows.size()))
            return nullptr;
        // 我们塞的是 &m_rows[row]，正常情况下一致；不一致说明这个 index 是扩容前的旧坐标，
        // 此时**以行号为准**（否则就是在解引用一个悬垂指针）。
        const DeviceRow *p = static_cast<const DeviceRow *>(index.constInternalPointer());
        const DeviceRow *byRow = &m_rows.at(size_t(index.row()));
        ++s_pointerChecks;
        if (p == byRow)
            ++s_pointerMatched;
        else
            ++s_pointerStale;
        return byRow;                              // 稳妥做法：按行号取
    }

    // ── 取证读数 ──
    static void resetCounters()
    {
        s_dataCalls = 0;
        s_dataCallsInvalidAsked = 0;
        s_headerCalls = 0;
        s_roleNamesCalls = 0;
        s_formatCalls = 0;
        s_pointerChecks = 0;
        s_pointerMatched = 0;
        s_pointerStale = 0;
        s_roleHits.clear();
        s_queriedRows.clear();
        s_minRow = -1;
        s_maxRow = -1;
    }
    static qint64 dataCalls() { return s_dataCalls; }
    static qint64 headerCalls() { return s_headerCalls; }
    static qint64 roleNamesCalls() { return s_roleNamesCalls; }
    static qint64 formatCalls() { return s_formatCalls; }
    static qint64 pointerChecks() { return s_pointerChecks; }
    static qint64 pointerMatched() { return s_pointerMatched; }
    static qint64 pointerStale() { return s_pointerStale; }
    static int queriedRowCount() { return s_queriedRows.size(); }
    static int minQueriedRow() { return s_minRow; }
    static int maxQueriedRow() { return s_maxRow; }
    static QString roleHistogram()
    {
        static const struct { int role; const char *name; } names[] = {
            { Qt::DisplayRole,       "DisplayRole(0)" },
            { Qt::DecorationRole,    "DecorationRole(1)" },
            { Qt::EditRole,          "EditRole(2)" },
            { Qt::ToolTipRole,       "ToolTipRole(3)" },
            { Qt::FontRole,          "FontRole(6)" },
            { Qt::TextAlignmentRole, "TextAlignmentRole(7)" },
            { Qt::BackgroundRole,    "BackgroundRole(8)" },
            { Qt::ForegroundRole,    "ForegroundRole(9)" },
            { Qt::CheckStateRole,    "CheckStateRole(10)" },
            { Qt::SizeHintRole,      "SizeHintRole(13)" },
        };
        QStringList parts;
        for (const auto &e : names) {
            const int hit = s_roleHits.value(e.role, 0);
            if (hit > 0)
                parts << QString::fromLatin1(e.name) + QLatin1Char(':') + QString::number(hit);
        }
        for (auto it = s_roleHits.cbegin(); it != s_roleHits.cend(); ++it) {
            bool known = false;
            for (const auto &e : names) {
                if (e.role == it.key())
                    known = true;
            }
            if (!known)
                parts << QStringLiteral("role%1:%2").arg(it.key()).arg(it.value());
        }
        return parts.isEmpty() ? QStringLiteral("（没人来问）") : parts.join(QStringLiteral("  "));
    }

private:
    std::vector<DeviceRow> m_rows;

    inline static qint64 s_dataCalls = 0;
    inline static qint64 s_dataCallsInvalidAsked = 0;
    inline static qint64 s_headerCalls = 0;
    inline static qint64 s_roleNamesCalls = 0;
    inline static qint64 s_formatCalls = 0;
    inline static qint64 s_pointerChecks = 0;
    inline static qint64 s_pointerMatched = 0;
    inline static qint64 s_pointerStale = 0;
    inline static QHash<int, int> s_roleHits;      // 角色号 → 被问次数
    inline static QSet<int> s_queriedRows;         // 哪些行真被问过
    inline static int s_minRow = -1;
    inline static int s_maxRow = -1;
};

// ============================================================================
// 四、小工具
// ============================================================================
static void flushEvents()
{
    QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
}

static QString describe(const QModelIndex &idx)
{
    return QStringLiteral("row=%1 col=%2 internalId=0x%3 isValid=%4")
        .arg(idx.row())
        .arg(idx.column())
        .arg(QString::number(idx.internalId(), 16))
        .arg(idx.isValid() ? QStringLiteral("true") : QStringLiteral("false"));
}

// ============================================================================
// 五、证据①：五个纯虚函数各答一次，把「谁问谁答」摆出来
// ============================================================================
static void evidenceFiveVirtuals(DeviceTableModel &model)
{
    qInfo() << "【证据①】五个必须自己实现的成员：签名与语义";

    qInfo() << QString::asprintf("  index(row,column,parent) const → QModelIndex   纯虚（第 274 行）\n"
                                 "  parent(child) const            → QModelIndex   纯虚（第 276 行）\n"
                                 "  rowCount(parent) const         → int         纯虚（第 279 行）\n"
                                 "  columnCount(parent) const      → int         纯虚（第 280 行）\n"
                                 "  data(index,role) const         → QVariant    纯虚（第 283 行）");
    qInfo() << "  跑一遍：rowCount(根) =" << model.rowCount(QModelIndex())
            << "columnCount(根) =" << model.columnCount(QModelIndex())
            << "｜columnCount(某真实格) =" << model.columnCount(model.index(0, 0))
            << "（列数**不随 parent 变 0**，见代码注释④：表头也拿真实 index 来问）";

    const QModelIndex cell = model.index(2, 1);
    qInfo() << "  model.index(2,1) →[" << describe(cell) << "]"
            << "｜internalPointer 是否 == &m_rows[2] ="
            << (cell.constInternalPointer()
                == static_cast<const void *>(&model.rawRows()[2]))
            << "（第三个参数是我们自己塞的，框架原样带回来）";
    qInfo() << "  model.index(2,1, 某个有效 parent) →[" << describe(model.index(2, 1, cell)) << "]"
            << "← 表格里「格子里的格子」不存在，返回无效 index（这是 parent/index 的分工）";
    qInfo() << "  cell.parent() →[" << describe(cell.parent()) << "] isValid ="
            << cell.parent().isValid() << "（表格恒无效：上面就是根）";
    qInfo() << "  越界不崩：index(999999,0) →[" << describe(model.index(999999, 0)) << "]"
            << "｜data(越界) 的 isValid =" << model.data(model.index(999999, 0)).isValid();
    qInfo() << "  便利函数：cell.siblingAtColumn(3).column() =" << cell.siblingAtColumn(3).column()
            << "｜sibling(5,0).row() =" << cell.sibling(5, 0).row()
            << "（同一行换列 / 换行换列，都还是问 index()，不是自己造）";
    qInfo() << "  data 按角色答（同一格 row=2,col=1）：DisplayRole ="
            << model.data(cell, Qt::DisplayRole).toString()
            << "｜EditRole =" << model.data(cell, Qt::EditRole).toInt()
            << "｜UserRole+1(257) =" << model.data(cell, Qt::UserRole + 1).toInt()
            << "｜没实现的 FontRole(6).isValid =" << model.data(cell, Qt::FontRole).isValid()
            << "← 默认分支必须 return QVariant()，视图才知道「这个角色我没有」";
    qInfo() << "  headerData(1, Horizontal, Display) ="
            << model.headerData(1, Qt::Horizontal, Qt::DisplayRole).toString()
            << "｜headerData(2, Vertical, Display) ="
            << model.headerData(2, Qt::Vertical, Qt::DisplayRole).toInt()
            << "（竖表头的行号也是**模型给的**，QTableView 自己不存）";
    qInfo() << "  roleNames() 给了" << model.roleNames().size() << "个角色名，前几个："
            << QStringLiteral("display / edit / toolTip / progressValue(257)");
    qInfo() << "  默认 flags 里有没有可编辑位：flags(cell).testFlag(Qt::ItemIsEditable) ="
            << model.flags(cell).testFlag(Qt::ItemIsEditable)
            << "｜有 Selectable 吗 =" << model.flags(cell).testFlag(Qt::ItemIsSelectable)
            << "｜有 Enabled 吗 =" << model.flags(cell).testFlag(Qt::ItemIsEnabled)
            << "（基类 QAbstractTableModel::flags 给的就是这两位，**想编辑得覆写 flags()+setData()，"
               "那是第 3 讲**）";
}

// ============================================================================
// 六、证据②：5000 行挂上视图 —— 视图只问可见的那几个格子
// ============================================================================
static void evidenceOnlyVisibleCells(DeviceTableModel &model, QTableView &view)
{
    qInfo() << "【证据②】5000 行 × 4 列：data() 的真实请求量";

    const int rows = model.rowCount();
    const qint64 allCells = qint64(rows) * model.columnCount();
    qInfo() << QString::asprintf("  规模：%d 行 × %d 列 = %lld 个格子；如果一格一个控件，"
                                 "光控件对象就够把你压死。现在看视图实际问了多少。",
                                 rows, model.columnCount(), static_cast<long long>(allCells));

    DeviceTableModel::resetCounters();
    view.setModel(&model);                        // setModel 会立刻问 rowCount/columnCount + 表头
    view.resize(820, 420);
    qInfo() << "  setModel 之后（还没 show）：data() 被调用" << DeviceTableModel::dataCalls()
            << "次、headerData" << DeviceTableModel::headerCalls()
            << "次、roleNames" << DeviceTableModel::roleNamesCalls() << "次";
    qInfo() << "     ← **没画的时候几乎不问 data()**：它只在要画、要算尺寸、要表头的时候问。"
               "（roleNames 只被问一次就够，它是「翻译表」不是「数据」）";

    view.show();
    flushEvents();
    view.viewport()->repaint();                   // 强制同步画一次，数得干净
    const qint64 firstPaint = DeviceTableModel::dataCalls();
    qInfo() << QString::asprintf("  show + 画一轮之后：data() 累计 %lld 次；被问到的行有 %d 个，"
                                 "区间 [%d, %d]；格子总数却是 %lld",
                                 static_cast<long long>(firstPaint),
                                 DeviceTableModel::queriedRowCount(),
                                 DeviceTableModel::minQueriedRow(),
                                 DeviceTableModel::maxQueriedRow(),
                                 static_cast<long long>(allCells));
    qInfo() << "     角色分布：" << DeviceTableModel::roleHistogram();
    qInfo() << "  → 这就是「视图只问可见的那几个格子」：4977 行**一个字节都没被格式化**。"
               "你的模型敢在 data() 里读数据库，读到的也只是看得见的那一小撮——"
               "只不过一帧一小撮，滚动起来就是每帧一小撮。";

    DeviceTableModel::resetCounters();
    for (const int target : { 800, 2500, 4900 }) {
        view.scrollTo(model.index(target, 0), QAbstractItemView::EnsureVisible);
        flushEvents();
        view.viewport()->repaint();
    }
    qInfo() << QString::asprintf("  滚 3 大跳之后：data() %lld 次，被问到的行 %d 个，区间 [%d, %d]"
                                 "（区间跟着滚动条走，永远只有一小条）",
                                 static_cast<long long>(DeviceTableModel::dataCalls()),
                                 DeviceTableModel::queriedRowCount(),
                                 DeviceTableModel::minQueriedRow(),
                                 DeviceTableModel::maxQueriedRow());
    qInfo() << "     角色分布：" << DeviceTableModel::roleHistogram();

    // 建 5000 行这件事本身的代价：让读者知道「慢不在模型/视图架构」
    QElapsedTimer t;
    t.start();
    model.appendRows(1000);
    const qint64 us = t.elapsed();
    qInfo() << QString::asprintf("  appendRows(1000)（走 begin/endInsertRows）耗时 %lld ms，"
                                 "现在 %d 行；视图只画得下 %d 行 → 剩下的 4000 多行**没被碰过**。",
                                 static_cast<long long>(us), model.rowCount(),
                                 DeviceTableModel::queriedRowCount());
    qInfo() << "     再滚一次看看：";
    DeviceTableModel::resetCounters();
    view.scrollTo(model.index(model.rowCount() - 1, 0), QAbstractItemView::PositionAtBottom);
    flushEvents();
    view.viewport()->repaint();
    qInfo() << QString::asprintf("       data() %lld 次，问到行区间 [%d, %d]，角色分布：%s",
                                 static_cast<long long>(DeviceTableModel::dataCalls()),
                                 DeviceTableModel::minQueriedRow(),
                                 DeviceTableModel::maxQueriedRow(),
                                 DeviceTableModel::roleHistogram().toUtf8().constData());
}

// ============================================================================
// 七、证据③：internalPointer 的用法与生命周期风险
// ============================================================================
static void evidenceInternalPointerLifetime(DeviceTableModel &model)
{
    qInfo() << "【证据③】internalPointer：塞什么、什么时候咬人";

    const QModelIndex early = model.index(0, 0);
    const void *pEarly = early.constInternalPointer();
    const void *storageBefore = static_cast<const void *>(model.rawRows().data());
    qInfo() << "  造 early = index(0,0)：internalPointer =" << pEarly
            << "｜当时 vector 的内存块首地址 =" << storageBefore
            << "｜两者相等 =" << (pEarly == storageBefore)
            << "（因为我们塞的就是 &m_rows[0]，它当然是内存块首地址）";

    // reserve 到很大 → 一定换内存块（只比地址，**绝不解引用旧指针**）
    model.rawRows().reserve(200000);
    const void *storageAfter = static_cast<const void *>(model.rawRows().data());
    const QModelIndex fresh = model.index(0, 0);
    qInfo() << "  做了 reserve(200000)（强制搬一次家）之后：";
    qInfo() << "     vector 新首地址 =" << storageAfter
            << "｜和旧的" << storageBefore << "相等吗 =" << (storageBefore == storageAfter);
    qInfo() << "     新造的 index(0,0).internalPointer =" << fresh.constInternalPointer()
            << "｜旧 index 里揣着的还是" << pEarly
            << "→ **旧 index 的 internalPointer 现在已经指向一块被 vector 释放掉的内存**。"
               "本讲只打印地址，一次都没有解引用它，这就是「生命周期风险」四个字的分量。";

    // 用模型的 rowOf() 说明「按行号兜底」为什么安全
    const DeviceRow *safe = model.rowOf(fresh);
    qInfo() << "  本讲 rowOf() 的取舍：先看 internalPointer，不一致就**按 index.row() 取**："
            << "fresh 取到的行首地址 =" << static_cast<const void *>(safe)
            << "｜新内存块首地址一致 =" << (safe == model.rawRows().data())
            << "｜期间检查次数" << DeviceTableModel::pointerChecks()
            << "命中" << DeviceTableModel::pointerMatched()
            << "过期" << DeviceTableModel::pointerStale();
    qInfo() << "  → 三条纪律：";
    qInfo() << "     ① 表格模型里 internalPointer **不是必需品**：index.row() 就够定位了，"
               "图省事就塞 0（写 nullptr，别写裸 0——两个 createIndex 重载都会犯歧义）。";
    qInfo() << "     ② 真要塞指针，只能塞「地址在整个模型生命周期里稳定」的东西："
               "堆上 new 出来的节点、或者 std::deque/自己管理的对象池。"
               "**vector 的元素地址、map 的迭代器、临时对象的地址都不行**。";
    qInfo() << "     ③ 树模型才真的离不开它（一格的孩子是谁只能靠指针/索引表回答），"
               "第 7 讲 item 模型与树模型会正面用一次。";

    // 顺手把 index/parent/sibling 的「坐标只是三个数」再钉一遍
    const QModelIndex other = model.index(1, 2);
    qInfo() << "  两个 index 的比较走的是四个字段全等（qabstractitemmodel.h 第 149~165 行）："
            << "index(0,0) == index(0,0) =" << (model.index(0, 0) == model.index(0, 0))
            << "｜== index(0,1) =" << (model.index(0, 0) == other)
            << "｜换个模型就不等了（第 1 讲证据⑤已经打过）";
}

// ============================================================================
// 八、证据④：data() 里格式化字符串的代价（本讲的性能账单）
// ============================================================================
static void evidenceFormatCost(DeviceTableModel &model, QTableView &view)
{
    qInfo() << "【证据④】data() 里每问一次就拼一个 QString，拼了多少个？";
    Q_UNUSED(model)                    // 计数器是 static 的，这里只需要 view 去触发重绘

    DeviceTableModel::resetCounters();
    view.viewport()->repaint();
    qInfo() << QStringLiteral("  画一轮：data() %1 次，其中**为了 DisplayRole 现拼的 QString %2 个**"
                              "（进度列拼「42%」、优先级列拼「P3」）")
                   .arg(DeviceTableModel::dataCalls())
                   .arg(DeviceTableModel::formatCalls());
    qInfo() << "  → 三条改法（第 4 讲会用上第一条）：";
    qInfo() << "     ① DisplayRole 直接返回 int，把「拼成人看的文字」交给委托的 displayText()"
               "（qstyleditemdelegate.h 第 53 行 `virtual QString displayText(const QVariant&, const QLocale&) const;`），"
               "只在真要显示的时候拼；";
    qInfo() << "     ② 需要格式化就缓存：把拼好的字符串**存在数据源里**（结构体加一个字段），"
               "改数据时更新一次，data() 只读；";
    qInfo() << "     ③ 别在 data() 里做 locale/数字分组以外的活（读文件、加锁、算正则）。";
    qInfo() << "     顺带一条：EditRole 返回原始 int、DisplayRole 返回给人看的文字，"
               "**排序和过滤读的是 EditRole**（Qt6 没有 SortRole，代理模型默认 sortRole 就是 EditRole），"
               "所以别把 EditRole 也拼成字符串，否则「10」会排在「9」前面。";
}

// ============================================================================
// 九、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("model_view_02"));

    DeviceTableModel model;
    QTableView view;

    QTimer::singleShot(0, [&model, &view]() {
        qInfo() << "================================================================";
        qInfo() << "model_view 第 2 讲 · 手写 QAbstractTableModel（平台 ="
                << QGuiApplication::platformName() << ", Qt =" << QT_VERSION_STR << "）";
        qInfo() << "================================================================";

        model.fill(5000);
        qInfo() << "  5000 行结构体已就位：model.rowCount() =" << model.rowCount()
                << "columnCount() =" << model.columnCount();

        view.setCornerButtonEnabled(false);
        view.horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        view.verticalHeader()->setDefaultSectionSize(22);
        view.setColumnWidth(ColName, 180);
        view.setColumnWidth(ColProgress, 110);
        view.setColumnWidth(ColOwner, 110);
        view.setColumnWidth(ColPriority, 90);

        evidenceFiveVirtuals(model);
        evidenceOnlyVisibleCells(model, view);
        evidenceInternalPointerLifetime(model);
        evidenceFormatCost(model, view);

        QTimer::singleShot(60, []() {
            qInfo() << "【收尾】这一讲的取舍记一句话：**视图不认识你的 vector，它只认识 QModelIndex 和 QVariant**。"
                       "你把「行/列 → 角色 → QVariant」这条翻译表写好，五万个数据照样只画看得见的那十几行。";
            QCoreApplication::quit();
        });
    });

    return app.exec();
}

// ============================================================================
// 十、速查表与纪律（背不住就回来查）
// ============================================================================
/*
 * 一、继承谁、最少写哪几个（本机头文件核过）
 *   ┌──────────────────────────┬────────────────────────────────────────────┐
 *   │ 基类                     │ 必须实现的（= 纯虚，不写编不过）           │
 *   ├──────────────────────────┼────────────────────────────────────────────┤
 *   │ QAbstractItemModel       │ index / parent / rowCount / columnCount /  │
 *   │                          │ data                       （五个，第 274~283 行）│
 *   │ QAbstractTableModel      │ rowCount / columnCount / data              │
 *   │                          │ ← index()/parent()/flags() 已有默认实现    │
 *   │                            （第 476/491/481 行），但默认实现**不带    │
 *   │                             internalPointer**，要存东西还是得自己写   │
 *   │ QAbstractListModel       │ rowCount / data（columnCount/index/parent  │
 *   │                          │ 都给了，见第 495~521 行）                  │
 *   └──────────────────────────┴────────────────────────────────────────────┘
 *   签名三条铁律（-Wall 不会替你把「角色类型写错」抓出来，但 override 会）：
 *   ① 全部 **const**：data/rowCount/columnCount/index/parent/headerData/flags/roleNames 都是 const；
 *      忘了 const + override = 编译期直接报错「no matching function」，这是好事。
 *   ② 角色参数是 **int role**，不是 Qt::ItemDataRole role：写成枚举类型是**另一个签名**，
 *      你的覆写会变成重载，基类那份纯虚没人实现 → 类变抽象，报「cannot instantiate abstract class」。
 *   ③ 默认参数只在**声明**处写一次；父类写了 = QModelIndex() 你就照写，
 *      别在实现处重复写默认值（重复写合法，但两处不一致会坑调用方）。
 *
 * 二、createIndex / internalPointer 决策表
 *   场景                          塞什么                        风险
 *   纯表格，行号能定位           createIndex(r, c, nullptr)      没有；最省
 *   表格里想少一次 vector 下标    &m_rows[r]                     vector 扩容搬家 → 悬垂
 *   树 / 需要父→子遍历           自定义节点指针（堆上、稳定）    节点被删时必须先 endRemoveRows
 *   跨进程/序列化                 quintptr(id) 存整数 id         id 要能在模型里查回行
 *   补充：createIndex 在 **protected** 段（第 396/397 行），外面调不到；
 *        它不检查越界，**越界检查是你的事**（用 hasIndex，第 273 行）。
 *
 * 三、角色怎么分（本讲用到的）
 *   Qt::DisplayRole (0)   给人看的文字 → 视图画的就是它（或者委托换的形态）
 *   Qt::DecorationRole(1) 图标/颜色 → 一格一格的「小图案」
 *   Qt::EditRole (2)      **原始值**：编辑器读写它、排序读它、过滤读它
 *   Qt::ToolTipRole (3)   悬停提示
 *   Qt::TextAlignmentRole(7) 返回 int（Qt::Alignment 的数值），别返回 QString
 *   Qt::CheckStateRole (10)   返回 Qt::CheckState（Unchecked/PartiallyChecked/Checked）
 *   Qt::BackgroundRole (8) / ForegroundRole(9) 返回 QBrush/QColor
 *   Qt::SizeHintRole (13)    返回 QSize —— 想要「这一格高一点」在这里给（第 4 讲 sizeHint 更常用）
 *   Qt::UserRole (0x0100 = 256) 起点：UserRole+1 起编号自定义
 *   （Qt6 **没有** Qt::SortRole；别照抄 Qt5 代码）
 *
 * 四、headerData 的三个冷知识
 *   ① 横表头问的是 orientation == Qt::Horizontal，section = 列号；竖表头 section = 行号。
 *   ② 竖表头默认显示行号这件事不是视图写死的，是你的 headerData 返回 section+1 的结果；
 *      不想显示就返回无效 QVariant，或者 view.verticalHeader()->setVisible(false)。
 *   ③ setHorizontalHeaderLabels() 之类的便利只在便利类里有；手写模型请覆写 headerData。
 *
 * 五、自写模型最容易踩的六个坑
 *   ① columnCount 里加了 parent.isValid() 分支 → 表头少一节、行少一格。
 *   ② data 的 default 分支返回 QString() 而不是 QVariant() → 视图以为「有这个角色的数据」，
 *      画出一个空白还占一次格式化的钱。
 *   ③ index() 里忘了 parent.isValid() 就返回 QModelIndex() → 树/表混合视图会画出幽灵子行。
 *   ④ 改完 m_rows 不发 begin/end 系列信号（第 3 讲）→ 视图行数与模型不一致，
 *      轻则显示旧行，重则视图拿旧行号越界问你。
 *   ⑤ 用 &vector[i] 当 internalPointer 又去 push_back → 悬垂（本讲证据③打印过两次地址）。
 *   ⑥ 在 data() 里 emit 信号或改模型状态 → 重入，视图当场问你一遍又一遍，栈都能打穿。
 *
 * ——— 老师傅的话 ———
 *   徒弟，这一讲的功夫不在「会写那五个函数」，而在**你会不会拒绝把逻辑塞进 data()**。
 *   新手写完 data 觉得挺美：显示要拼字符串、颜色要按业务算、还要读一次配置。
 *   证据②给你看了：一帧就要问几十上百次，滚动起来翻好几倍。你那些「顺手」全在帧里跑。
 *   我的规矩是：**data() 只做「取一个字段 + 必要时套一层 QVariant」**，
 *   要算的在改数据时算好存下来，要画的交给委托（第 4 讲）。
 *
 *   第二件事：internalPointer 是把刀。它便宜，因为你少一次下标；它危险，因为
 *   框架对它**一无所知**——你塞进去的地址它不检查、不持有、不负责。
 *   我在证据③里把两次地址打出来，就是让你亲眼看见「旧 index 里的指针已经不是你的行了」。
 *   表格模型我的建议：**别塞指针，用 index.row() 定位**。省那一次下标换不来半夜被叫起来查崩溃的钱。
 *
 *   第三件事：签名要抄头文件，不要抄记忆。我这文件的签名是逐行对着
 *   qabstractitemmodel.h 第 274~283 行写的，并且用 -fsyntax-only 让编译器替我核过一遍。
 *   你把 role 写成 Qt::ItemDataRole、把 const 漏一个，编译器都会当场拦住——
 *   前提是你写了 **override**。所以本讲所有覆写都带 override，一个「隐式虚函数」都不留。
 *
 * ——— 小练习 ———
 *   1. 把 index() 里 createIndex 的第三个参数改成 nullptr，再把 rowOf() 改成只用
 *      index.row()；重跑证据③，看 pointerStale 从多少变成 0，并解释为什么「不塞指针」
 *      反而让代码更耐改（提示：扩容/删除元素都不用管旧地址）。
 *   2. 给模型加第五列「上次巡检时间」（用 QDateTime 存进结构体），
 *      DisplayRole 返回「今天/昨天/3 天前」这类人话，EditRole 返回原始 QDateTime；
 *      然后打开视图的 setSortingEnabled(true)，观察排序读的是哪个角色（把
 *      headerData 的 DisplayRole 换成中文，看排序是否还正确 —— 这就是 EditRole 的意义）。
 *   3. 把 headerData 的竖表头从「行号」改成「负责人姓名首字」，再让
 *      view.verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents)，
 *      观察 5000 行时 setModel 到出画的时间变化（QElapsedTimer 自己加一次），
 *      体会「模型答得越贵，视图问得越疼」。
 */
// ============================================================================

// 本文件有 Q_OBJECT 类（DeviceTableModel），AUTOMOC 会生成并在这里包含进来。
#include "02_custom_table_model.moc"
