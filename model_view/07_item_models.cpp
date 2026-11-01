// ============================================================================
// 第 7 讲 · QStandardItemModel / QStandardItem：便利类什么时候用、什么时候必须自己写
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   前两讲（../model_view/02、../model_view/03）我们手写模型，回答了「数据本来就是自己的」
//   那类场景。但很多活是「手上有一坨临时数据，先让它出现在界面上再说」，
//   这时候 Qt 给的**便利模型（convenience item model）**——QStandardItemModel——
//   能把几天的活压成三十行。它好在哪、贵在哪、什么时候必须换掉，这一讲讲清：
//     1) 它的**卖点**是「你不用写那五个虚函数、也不用自己发 dataChanged」。
//        第 3 讲那条「改完记得 emit」的纪律，在这里由模型内部代劳：
//        头文件 QtGui/qstandarditemmodel.h 第 390 行有一个私有槽
//        `Q_PRIVATE_SLOT(d_func(), void _q_emitItemChanged(const QModelIndex &topLeft, ...))`，
//        格子一改，它自己把变化转成 dataChanged 发给视图。证据②把这两个信号计数打出来。
//     2) 它的**代价**不是「慢」这么轻描淡写，而是**每个格子是一个独立堆对象**：
//        一格 = 一个 QStandardItem（还牵一个私有 d-pointer）+ 每格 QString 的堆缓冲。
//        10 万行 × 4 列 = **40 万个对象**，构建耗时、常驻内存、逐个读格的开销都是
//        「自己写模型」的好几倍。证据⑤用 QElapsedTimer 把两份账并排打出来。
//        ⚠ 一个必须纠正的流行说法：网上（以及不少老讲义）说「每个 item 都是一个 QObject」。
//          在 **Qt6 上不成立**：本机 QtGui/qstandarditemmodel.h 第 24 行是
//              class Q_GUI_EXPORT QStandardItem        ← **没有基类**
//          （Qt5 时代它确实 `: public QObject`，Qt6 把 QObject 继承拿掉了）。
//          结论方向没变，但理由要换成对的：**一次堆分配 + 一个 d-pointer + 虚析构（带 vtable）**，
//          而不是「因为它是 QObject」。这种细节面试问到就是送分/送命的分水岭。
//     3) 「树和表格共用同一套 item 类」这个现象：QStandardItem 自己带 children，
//        所以同一个模型既能在 QTreeView 上展开、也能在 QTableView 上平铺（证据③）。
//     4) take* 系列的所有权陷阱：takeRow()/takeItem() 把对象交回给你，
//        不接住并 delete 就是泄漏（证据③）。
//     5) 三档判断（什么时候用哪个），以及和 ../widgets/04「便利控件」那条线的边界。
//   顺带把 QStringListModel 认一眼（一维字符串列表的便利模型，证据⑦）。
//
// 对应构建目标名：model_view_07_item_models
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(model_view_07_item_models 07_item_models.cpp)
//   target_link_libraries(model_view_07_item_models PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)
//   本文件**不含 Q_OBJECT 类**：两个自定义类都只覆写虚函数、不新增信号/槽，
//   取证连接全用 lambda + context 对象，所以末尾**不**include 任何 .moc。
//   （QStandardItemModel 自己带 Q_OBJECT，那是它的事，不代表你的子类也要加。）
//
// 运行前须知：
//   · 无人值守：QTimer::singleShot 排动作 → qInfo() 打印 → QCoreApplication::quit()。
//     **没有 QDialog::exec()、没有裸 QEventLoop::exec()**，只用 processEvents() 催重绘。
//   · 可以无头跑：`model_view_07_item_models -platform offscreen`。
//   · 本讲会真的造 **10 万行 × 4 列**（便利类那一份 = 40 万个 QStandardItem），这是有意的
//     「压一下看看」。机器慢就把 kBigRows 改成 20000，结论方向不变。不读文件、不联网。
//   · **耗时是量级不是绝对值**：机器/编译器不同会差几倍，我只承诺「比例关系」。
//     内存这一项本讲**不打印「实测 RSS」**：本机 Qt 头文件里没有跨平台读自身常驻内存的
//     公共接口（我 grep 过 QtCore/QtGui/QtWidgets，只有 qrhi.h 里那个 GPU 资源统计的
//     memoryUsage，不是进程 RSS）。所以这里给「可核对的下界估算 + 教你自己用 OS 工具测」，
//     绝不编一个不存在 API 的名字。
//   · 图解页面：../visuals/model_view.html（便利类 vs 自定义模型那一栏）。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 model_view_07_item_models，跑到自动退出、退出码 0）。
//     但本讲**没有汇总自检行**，退出码 0 只证明「没崩、走完了」，不证明正文每个数字都对：
//     带具体数字的「预期」请以你自己那次运行输出为准（本机那一轮存档 runlogs/model_view_07_item_models.log）。
// ============================================================================

#include <QApplication>              // QtWidgets/qapplication.h
#include <QAbstractTableModel>       // QtCore/qabstractitemmodel.h
#include <QStandardItemModel>        // **QtGui**/qstandarditemmodel.h ← Qt6 里这个头在 QtGui，不在 QtWidgets
#include <QStandardItem>             // 同一个头
#include <QStringListModel>          // **QtCore**/qstringlistmodel.h：一维字符串列表的便利模型
#include <QTreeView>                 // QtWidgets/qtreeview.h
#include <QTableView>                // QtWidgets/qtableview.h
#include <QAbstractItemView>         // QtWidgets/qabstractitemview.h
#include <QHeaderView>               // QtWidgets/qheaderview.h
#include <QItemSelectionModel>       // QtCore/qitemselectionmodel.h（证据④里顺手对照第 6 讲）
#include <QModelIndex>
#include <QPersistentModelIndex>
#include <QVariant>
#include <QString>
#include <QStringList>
#include <QList>
#include <QVector>
#include <QSet>
#include <QHash>
#include <QByteArray>
#include <QDebug>
#include <QTimer>
#include <QCoreApplication>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QtGlobal>

#include <vector>

// ============================================================================
// 一、核对过的签名（本机 D:\Qt\6.12.0\mingw_64\include，QtCore/QtGui 版本 6.12.0）
//     行号是我去头文件逐行看的，换 Qt 版本请重新核。
// ============================================================================
// QtGui/qstandarditemmodel.h
//   第 24 行   class Q_GUI_EXPORT QStandardItem              ← **没有基类**（Qt5 才是 : public QObject）
//   第 26~29 行 QStandardItem(); explicit QStandardItem(const QString &text);
//              QStandardItem(const QIcon &icon, const QString &text);
//              explicit QStandardItem(int rows, int columns = 1);
//   第 31 行   virtual ~QStandardItem();                     ← 虚析构 ⇒ 每个对象带 vtable
//   第 33/35 行 virtual QVariant data(int role = Qt::UserRole + 1) const;
//              virtual void setData(const QVariant &value, int role = Qt::UserRole + 1);
//              ↑ 注意默认 role 是 **Qt::UserRole + 1**（= 模型层的 Qt::DisplayRole，值 0 映射到 0x0101）
//   第 41 行   inline void setText(const QString &)          （inline 实现在第 216 行）
//   第 46/51/70/75/85/90/95 行 setIcon / setToolTip / setSizeHint / setFont /
//              setBackground / setForeground / setCheckState
//   第 107/108 行 Qt::ItemFlags flags() const;  void setFlags(Qt::ItemFlags);
//   第 111~138 行 enabled/editable/selectable/checkable/autoTristate/userTristate/dragEnabled/dropEnabled
//              的「读 = flags() & 位、写 = 置/清那位」糖（第 113/118/123/128… 行）
//   第 153/154 行 int row() const;  int column() const;      ← 相对**父 item** 的坐标，不是全局行号
//   第 156~161 行 rowCount()/setRowCount()/columnCount()/setColumnCount()
//   第 163~166 行 bool hasChildren() const; QStandardItem *child(int row, int column = 0) const;
//              void setChild(int row, int column, QStandardItem *item);
//   ⚠⚠ **QStandardItem 里没有 appendChild() 这个函数**（我把第 163~200 行通读并 grep 过）。
//      给一个 item 加子节点用的就是 **appendRow()**（第 179~183 行），名字里带 child 的只有
//      child()/setChild()/takeChild()/sortChildren() 四个；「取子节点的 index」是模型侧的
//      index(row, column, parent)，不叫 childIndex()。这个词是从 QTreeWidgetItem/QDomElement
//      那边串门过来的，写错了编译器当场拦（本讲初稿就踩了，改完记在这儿）。
//   第 168~177 行 insertRow/insertRows/insertColumn/removeRow/removeColumn/removeRows/removeColumns
//   第 179~183 行 appendRow(const QList<QStandardItem*> &)（第 262 行 inline：insertRow(rowCount(), items)）
//              appendRows / appendColumn / insertRow(int, QStandardItem*) / appendRow(QStandardItem*)
//   第 185~187 行 QStandardItem *takeChild(int row, int column = 0);
//              QList<QStandardItem*> takeRow(int row);  QList<QStandardItem*> takeColumn(int column);
//              （第三个是 QList<QStandardItem*>，我抄的是头文件原文第 187 行）
//   第 189 行   void sortChildren(int column, Qt::SortOrder order = Qt::AscendingOrder);
//   第 191 行   virtual QStandardItem *clone() const;         ← **复制 item 只能用它**
//   第 192~193 行 enum ItemType { Type = 0, UserType = 1000 }; virtual int type() const;
//   第 200 行   virtual bool operator<(const QStandardItem &other) const;   ← 排序比的就是它
//   第 202~206 行 **protected**：QStandardItem(const QStandardItem &)、operator=、
//              emitDataChanged()、QScopedPointer<QStandardItemPrivate> d_ptr
//              ↑ 拷贝构造在 protected 段 ⇒ `new QStandardItem(*it)` **编不过**；
//                要复制用 it->clone()。而 emitDataChanged() 是 protected，
//                等于告诉你：想自定义 item 子类并「改完自动刷新」，得自己调它。
//   第 211 行   Q_DECLARE_PRIVATE(QStandardItem)              ← d-pointer：对象本身还要再牵一个私有对象
//   第 279~281 行 class Q_GUI_EXPORT QStandardItemModel : public QAbstractItemModel { Q_OBJECT …
//   第 286/287 行 explicit QStandardItemModel(QObject *parent = nullptr);
//              QStandardItemModel(int rows, int columns, QObject *parent = nullptr);
//   第 51/52 行（模型段）QStandardItem *item(int row, int column = 0) const;
//              void setItem(int row, int column, QStandardItem *item);
//   第 67/69 行 void appendRow(const QList<QStandardItem*> &items);  void appendRow(QStandardItem *item);
//   第 78/79 行 QStandardItem *takeItem(int row, int column = 0);  QList<QStandardItem*> takeRow(int row);
//   第 42 行    void clear();        第 46 行 void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
//   第 48/49 行 QStandardItem *itemFromIndex(const QModelIndex &) const;  QModelIndex indexFromItem(const QStandardItem *) const;
//   第 61/62 行 setHorizontalHeaderLabels(const QStringList &) / setVerticalHeaderLabels(const QStringList &)
//   第 65 行    void setColumnCount(int columns);
//   第 88 行    QList<QStandardItem*> findItems(const QString &text, Qt::MatchFlags flags = Qt::MatchContains,
//                                              int column = 0) const;   ← 它是「找」，不是「过滤」
//   第 390 行   Q_PRIVATE_SLOT(d_func(), void _q_emitItemChanged(const QModelIndex &topLeft,
//                                                    const QModelIndex &bottomRight))
//              ↑ 证据②的根据：改 item 不需要你自己 emit dataChanged，模型内部替你发。
// QtCore/qstringlistmodel.h
//   第 15 行   class Q_CORE_EXPORT QStringListModel : public QAbstractListModel
//   第 19/20 行 QStringListModel(QObject *parent = nullptr); QStringListModel(const QStringList &, QObject *parent = nullptr);
//   第 22 行    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
//   ⚠ QAbstractListModel（qabstractitemmodel.h 第 519 行）把 `columnCount(const QModelIndex&)`
//     声明在 **private** 段，而且**没有默认参数** ⇒ 对 QStringListModel 直接写
//     slm.columnCount() 会同时撞上「private within this context」和「expects 1 argument」两条错误。
//     想问列数就升到 `QAbstractItemModel *p = &slm; p->columnCount(QModelIndex());`。
//     这条我是被编译器拦下来才发现的，不是我背的。
//   第 26 行    bool setData(const QModelIndex &, const QVariant &, int role = Qt::EditRole) override;
//   第 31/32 行 insertRows / removeRows
//   第 40/41 行 QStringList stringList() const;  void setStringList(const QStringList &);
//   ← 它是**一维**（1 列）模型，别当表格用；本讲只做一眼认知。
//
// 关于所有权：Qt 官方文档对 takeItem()/takeRow() 的说法是「the caller becomes the owner of
// the returned item」。本机头文件里只有签名（返回 QStandardItem* / QList<QStandardItem*>），
// 没有那行注释 ⇒ 所有权这条我以文档为准，本讲就按「接住并 delete」来写。
// 而 setItem()/appendRow() 的文档说法是「模型接管」（本讲按它来，不再自己 delete）。

// ============================================================================
// 二、对照组：同样 10 万行 × 4 列，自己写的模型（结构体数组，一次分配）
//     实现要点全部沿用 ../model_view/02：createIndex 塞行号、表格的 parent() 一律无效索引。
// ============================================================================
struct Row4 {
    QString a;
    QString b;
    QString c;
    QString d;
};

class FlatStore : public QAbstractTableModel
{
public:
    explicit FlatStore(int rows, QObject *parent = nullptr)
        : QAbstractTableModel(parent)
    {
        m_rows.resize(size_t(rows));
        for (int r = 0; r < rows; ++r) {
            Row4 &it = m_rows[size_t(r)];
            it.a = QStringLiteral("ID-%1").arg(r, 6, 10, QLatin1Char('0'));
            it.b = QStringLiteral("name-%1").arg(r);
            it.c = QStringLiteral("state-%1").arg(r % 4);
            it.d = QStringLiteral("note-%1").arg(r % 7);
        }
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        ++m_callRowCount;
        return parent.isValid() ? 0 : int(m_rows.size());
    }
    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : kCols;
    }
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.isValid() || row < 0 || column < 0
            || row >= int(m_rows.size()) || column >= kCols)
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
        ++m_callData;
        if (role == Qt::DisplayRole && idx.isValid() && idx.model() == this)
            m_asked.insert(idx.row());
        if (!idx.isValid() || idx.model() != this)
            return QVariant();
        if (role == Qt::DisplayRole) {
            const Row4 &it = m_rows[size_t(idx.row())];
            switch (idx.column()) {
            case 0: return it.a;
            case 1: return it.b;
            case 2: return it.c;
            case 3: return it.d;
            }
        } else if (role == Qt::TextAlignmentRole) {
            return QVariant();
        }
        return QVariant();
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override
    {
        if (role != Qt::DisplayRole)
            return QVariant();
        if (orientation != Qt::Horizontal)
            return section;
        static const char *k[] = { "编号", "名称", "状态", "备注" };
        return (section >= 0 && section < kCols) ? QString::fromUtf8(k[section]) : QVariant();
    }

    // 计数器（第 2 讲的思路）：用来和便利类那份做「画一屏」的对照
    void resetCounters() const { m_callData = 0; m_callRowCount = 0; m_asked.clear(); }
    QString counterReport() const
    {
        int lo = -1, hi = -1;
        for (int r : std::as_const(m_asked)) { if (lo < 0 || r < lo) lo = r; if (r > hi) hi = r; }
        return QStringLiteral("data()=%1 次，rowCount()=%2 次，被问到的不同行=%3 个（区间 %4..%5）")
            .arg(m_callData).arg(m_callRowCount).arg(m_asked.size()).arg(lo).arg(hi);
    }
    int storedRows() const { return int(m_rows.size()); }

    static constexpr int kCols = 4;

private:
    std::vector<Row4> m_rows;             // 一次分配；4 个 QString 挤在同一结构体里，连续存放
    mutable int m_callData = 0;
    mutable int m_callRowCount = 0;
    mutable QSet<int> m_asked;
};

// ============================================================================
// 三、取证上下文对象：继承 QObject 但**不加 Q_OBJECT**
//     它不定义任何信号/槽，只当 connect() 的 context 用（负责「我死了就自动拆连接」）。
//     判据记牢：只有**新增**信号/槽/Q_PROPERTY 才需要 Q_OBJECT；当 context 不需要。
//     （机制见 ../metaobject/04_lambda_and_lifetime.cpp。）
// ============================================================================
class Harness : public QObject
{
public:
    explicit Harness(QObject *parent = nullptr) : QObject(parent) {}

    // 把三个信号接到计数器上（QStandardItemModel 的 itemChanged 是它自己的信号，
    // dataChanged/rowsInserted/modelReset 来自 QAbstractItemModel）
    void attach(QStandardItemModel *m)
    {
        QObject::connect(m, &QStandardItemModel::itemChanged, this,
                         [this](QStandardItem *) { ++itemChangedHits; });
        QObject::connect(m, &QAbstractItemModel::dataChanged, this,
                         [this](const QModelIndex &, const QModelIndex &, const QList<int> &) { ++dataChangedHits; });
        QObject::connect(m, &QAbstractItemModel::rowsInserted, this,
                         [this](const QModelIndex &, int, int) { ++rowsInsertedHits; });
        QObject::connect(m, &QAbstractItemModel::modelReset, this,
                         [this]() { ++modelResetHits; });
    }
    void zero() { itemChangedHits = 0; dataChangedHits = 0; rowsInsertedHits = 0; modelResetHits = 0; }

    int itemChangedHits = 0;
    int dataChangedHits = 0;
    int rowsInsertedHits = 0;
    int modelResetHits = 0;
};

// 打印「一坨字节」的人话单位（只做算术，不去测进程内存）
static QString humanBytes(double bytes)
{
    static const char *kUnits[] = { "B", "KB", "MB", "GB" };
    int u = 0;
    while (bytes >= 1024.0 && u < 3) { bytes /= 1024.0; ++u; }
    return QStringLiteral("%1 %2").arg(bytes, 0, 'f', 1).arg(QString::fromLatin1(kUnits[u]));
}

// ============================================================================
// 四、main：证据一条条来
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("model_view_07_item_models"));

    qInfo().noquote() << QStringLiteral("========== 第 7 讲 · QStandardItemModel / 便利类三档判断 ==========");
    qInfo().noquote() << QStringLiteral("Qt 版本串：%1 —— 下面关于 QStandardItem 的结论按 6.x 的头文件说")
                             .arg(QT_VERSION_STR);

    Harness h;

    // ------------------------------------------------------------ 证据①：最小用法 / 所有权 / flags
    QTimer::singleShot(30, &app, [&h]() {
        Q_UNUSED(h);
        qInfo().noquote() << QStringLiteral("① 便利类最小用法：appendRow / setItem / item(r,c) / setData / setEditable");
        QStandardItemModel m(0, 4);                 // 先定 4 列、0 行
        m.setHorizontalHeaderLabels({ QStringLiteral("编号"), QStringLiteral("名称"),
                                      QStringLiteral("状态"), QStringLiteral("备注") });
        for (int r = 0; r < 3; ++r) {
            QList<QStandardItem*> row;
            row << new QStandardItem(QStringLiteral("ID-%1").arg(r));      // 所有权从这里开始归模型
            row << new QStandardItem(QStringLiteral("name-%1").arg(r));
            QStandardItem *state = new QStandardItem(QStringLiteral("OPEN"));
            state->setEditable(false);                                     // 第 118 行的糖 = 清 ItemIsEditable 位
            state->setData(QStringLiteral("raw-%1").arg(r), Qt::UserRole);
            row << state;
            row << new QStandardItem(QStringLiteral("note-%1").arg(r));
            m.appendRow(row);
        }
        m.setItem(3, 0, new QStandardItem(QStringLiteral("只填了第 0 列的第 4 行")));

        qInfo().noquote() << QStringLiteral("  行数=%1 列数=%2（第 4 行只给了一格，其余格模型会补空 item 或留空）")
                                 .arg(m.rowCount()).arg(m.columnCount());
        QStandardItem *cell = m.item(0, 2);
        qInfo().noquote() << QStringLiteral("  item(0,2)：text=%1，flags=0x%2，ItemIsEditable(%3) 位=%4，UserRole=%5")
                                 .arg(cell ? cell->text() : QStringLiteral("<null>"))
                                 .arg(cell ? int(cell->flags()) : 0, 2, 16, QLatin1Char('0'))
                                 .arg(int(Qt::ItemIsEditable))
                                 .arg(cell && (cell->flags() & Qt::ItemIsEditable) ? "有" : "无")
                                 .arg(cell ? cell->data(Qt::UserRole).toString() : QStringLiteral("<null>"));
        const QStandardItem fresh;
        qInfo().noquote() << QStringLiteral("  新建 item 的默认 flags=0x%1（对照位值：Enabled=%2、Editable=%3、"
                                            "Selectable=%4、DragEnabled=%5、DropEnabled=%6，见 QtCore/qnamespace.h）")
                                 .arg(int(fresh.flags()), 2, 16, QLatin1Char('0'))
                                 .arg(int(Qt::ItemIsEnabled)).arg(int(Qt::ItemIsEditable))
                                 .arg(int(Qt::ItemIsSelectable)).arg(int(Qt::ItemIsDragEnabled))
                                 .arg(int(Qt::ItemIsDropEnabled));
        qInfo().noquote() << QStringLiteral("  item 的坐标是**相对父 item** 的：item(0,2)->row()=%1 column()=%2；"
                                            "模型侧则用 indexFromItem → (%3,%4)")
                                 .arg(cell ? cell->row() : -1).arg(cell ? cell->column() : -1)
                                 .arg(m.indexFromItem(cell).row()).arg(m.indexFromItem(cell).column());
        qInfo().noquote() << QStringLiteral("  交叉引用 ../model_view/03：模型的 flags 与视图的 editTriggers 是两道门，"
                                            "setEditable(false) 只关模型那道。");
    });

    // ------------------------------------------------------------ 证据②：便利类替你发信号
    QTimer::singleShot(60, &app, [&h]() {
        qInfo().noquote() << QStringLiteral("② 「不用自己 emit dataChanged」到底是谁在做");
        QStandardItemModel m(2, 2);
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 2; ++c)
                m.setItem(r, c, new QStandardItem(QStringLiteral("%1-%2").arg(r).arg(c)));
        h.attach(&m);
        h.zero();

        m.item(0, 0)->setText(QStringLiteral("改过了"));        // ← 只改对象，不 emit 任何东西
        qInfo().noquote() << QStringLiteral("  只调 item->setText() 一次 → itemChanged 计 %1 次、dataChanged 计 %2 次"
                                            "（模型内部那个 _q_emitItemChanged 私有槽干的活，头文件第 390 行）")
                                 .arg(h.itemChangedHits).arg(h.dataChangedHits);

        m.appendRow(QList<QStandardItem*>()
                    << new QStandardItem(QStringLiteral("新行 A"))
                    << new QStandardItem(QStringLiteral("新行 B")));
        qInfo().noquote() << QStringLiteral("  appendRow 之后：rowsInserted 计 %1 次（视图因此能正确挪行，"
                                            "不用你管 begin/endInsertRows）").arg(h.rowsInsertedHits);

        m.clear();
        qInfo().noquote() << QStringLiteral("  clear() 之后：modelReset 计 %1 次（对照 ../model_view/03 证据⑤："
                                            "reset 会让视图丢选择与滚动位置）").arg(h.modelResetHits);
        qInfo().noquote() << QStringLiteral("  手写模型这边呢？../model_view/02/03 里这三个信号**全靠你自己发** —— "
                                            "这就是便利类的第一笔收益。");
    });

    // ------------------------------------------------------------ 证据③：树 + 表共用一套类 / take 所有权
    QTimer::singleShot(90, &app, [&h]() {
        qInfo().noquote() << QStringLiteral("③ 同一个 QStandardItemModel：树上能展开，表里能平铺");
        QStandardItemModel tree(0, 2);
        tree.setHorizontalHeaderLabels({ QStringLiteral("项目"), QStringLiteral("数量") });
        for (int g = 0; g < 3; ++g) {
            QStandardItem *group = new QStandardItem(QStringLiteral("分组 %1").arg(g));
            group->setEditable(false);
            for (int s = 0; s < 2; ++s) {
                QStandardItem *sub = new QStandardItem(QStringLiteral("  子类 %1-%2").arg(g).arg(s));
                for (int leaf = 0; leaf < 2; ++leaf)
                    sub->appendRow(new QStandardItem(QStringLiteral("    叶子 %1-%2-%3").arg(g).arg(s).arg(leaf)));
                group->appendRow(sub);
            }
            tree.appendRow(QList<QStandardItem*>() << group
                                                   << new QStandardItem(QStringLiteral("g%1").arg(g)));
        }
        QTreeView tv;
        tv.setModel(&tree);
        tv.resize(360, 300);
        tv.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

        const QModelIndex root0 = tree.index(0, 0);
        qInfo().noquote() << QStringLiteral("  顶层 rowCount=%1，第 0 项 hasChildren=%2，"
                                            "它的 rowCount=%3（孙子层 %4）")
                                 .arg(tree.rowCount()).arg(tree.hasChildren(root0) ? "true" : "false")
                                 .arg(tree.rowCount(root0))
                                 .arg(tree.index(0, 0, root0).isValid() ? tree.rowCount(tree.index(0, 0, root0)) : -1);
        QStandardItem *g0 = tree.item(0, 0);
        qInfo().noquote() << QStringLiteral("  item 侧：group 的 childCount=%1，child(0,0)->text=%2，"
                                            "它的 parent()->text=%3（父子指针都在，所以不需要另写模型类）")
                                 .arg(g0 ? g0->rowCount() : -1)
                                 .arg(g0 && g0->child(0, 0) ? g0->child(0, 0)->text() : QStringLiteral("<null>"))
                                 .arg(g0 && g0->child(0, 0) && g0->child(0, 0)->parent()
                                      ? g0->child(0, 0)->parent()->text() : QStringLiteral("<null>"));

        QTableView tableView;
        tableView.setModel(&tree);                  // ← 同一个模型换挂到表格
        tableView.resize(360, 200);
        tableView.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        qInfo().noquote() << QStringLiteral("  同一模型挂 QTableView：rowCount=%1（只看见顶层 %2 行，"
                                            "子层要展开才可见 —— 表格不会自己递归）")
                                 .arg(tableView.model()->rowCount()).arg(tree.rowCount());
        qInfo().noquote() << QStringLiteral("  现象解释：QStandardItem 自带 children，所以一个类同时满足"
                                            "「表」和「树」两种结构；代价是**每格都背着 children 容器**，"
                                            "纯表格场景这份开销白付（证据⑤量化）。");

        // take 系列的所有权
        const int before = tree.rowCount();
        QList<QStandardItem*> taken = tree.takeRow(0);
        qInfo().noquote() << QStringLiteral("  takeRow(0)：行数 %1 → %2，拿回 %3 个 item（模型不再管它们，"
                                            "也不再去 hasChildren）").arg(before).arg(tree.rowCount()).arg(taken.size());
        tree.removeRow(0);
        qInfo().noquote() << QStringLiteral("  removeRow(0)：行数 → %1（这条是模型自己 delete，你不用接）")
                                 .arg(tree.rowCount());
        int deleted = 0;
        for (QStandardItem *it : taken) { delete it; ++deleted; }   // ← 不写这行就是泄漏
        qInfo().noquote() << QStringLiteral("  手动 delete 了 %1 个 take 回来的 item（文档：take* 之后调用方成为所有者）")
                                 .arg(deleted);
        h.zero();
    });

    // ------------------------------------------------------------ 证据④：findItems ≠ 过滤
    QTimer::singleShot(120, &app, [&h]() {
        qInfo().noquote() << QStringLiteral("④ 便利类的「找」和代理的「滤」不是一回事");
        QStandardItemModel m(0, 2);
        for (int r = 0; r < 2000; ++r) {
            m.appendRow(QList<QStandardItem*>()
                        << new QStandardItem(QStringLiteral("order-%1").arg(r))
                        << new QStandardItem(QStringLiteral("state-%1").arg(r % 5)));
        }
        QElapsedTimer t; t.start();
        const QList<QStandardItem*> hits = m.findItems(QStringLiteral("order-19"), Qt::MatchContains, 0);
        qInfo().noquote() << QStringLiteral("  findItems('order-19', MatchContains, 第0列) → 命中 %1 个，用时 %2 ms；"
                                            "它只是给你一个**列表**，视图里那些行还都在")
                                 .arg(hits.size()).arg(t.elapsed());
        t.restart();
        QModelIndexList removedRows;
        for (QStandardItem *it : hits)
            removedRows << m.indexFromItem(it);
        qInfo().noquote() << QStringLiteral("  把命中转成 index 用了 %1 ms（%2 个）→ 想「删掉不想要的」是条歪路，"
                                            "正路是第 5 讲的 QSortFilterProxyModel").arg(t.elapsed()).arg(removedRows.size());
        qInfo().noquote() << QStringLiteral("  交叉引用 ../model_view/05：过滤要在**代理**层做，源数据一根汗毛都不动。");
        QTableView v;
        v.setModel(&m);
        v.resize(420, 260);
        v.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        if (!v.selectionModel()->selectedIndexes().isEmpty())
            qInfo().noquote() << QStringLiteral("  （顺手）新建视图的选择模型默认是空的，这里 %1 个选中格")
                                     .arg(v.selectionModel()->selectedIndexes().size());
        else
            qInfo().noquote() << QStringLiteral("  （顺手）新建视图默认 0 个选中格 → 选择属于 sm 对象，不属于视图"
                                                "（见 ../model_view/06 证据①）");
        h.zero();
    });

    // ------------------------------------------------------------ 证据⑤：性能陷阱（本讲主菜）
    QTimer::singleShot(150, &app, [&h]() {
        constexpr int kBigRows = 100000;               // 10 万行
        constexpr int kBigCols = 4;                    // × 4 列 = 40 万个 item
        qInfo().noquote() << QStringLiteral("⑤ 性能陷阱实测：%1 行 × %2 列，两份模型并排对账")
                                 .arg(kBigRows).arg(kBigCols);

        // —— A 份：便利类
        QElapsedTimer tA; tA.start();
        QStandardItemModel *itemModel = new QStandardItemModel(0, kBigCols);
        itemModel->setHorizontalHeaderLabels({ QStringLiteral("编号"), QStringLiteral("名称"),
                                               QStringLiteral("状态"), QStringLiteral("备注") });
        for (int r = 0; r < kBigRows; ++r) {
            QList<QStandardItem*> row;
            row.reserve(kBigCols);
            row << new QStandardItem(QStringLiteral("ID-%1").arg(r, 6, 10, QLatin1Char('0')));
            row << new QStandardItem(QStringLiteral("name-%1").arg(r));
            row << new QStandardItem(QStringLiteral("state-%1").arg(r % 4));
            row << new QStandardItem(QStringLiteral("note-%1").arg(r % 7));
            itemModel->appendRow(row);
        }
        const qint64 buildMsItem = tA.elapsed();
        const qint64 items = qint64(kBigRows) * kBigCols;

        // —— B 份：自定义
        QElapsedTimer tB; tB.start();
        FlatStore *flat = new FlatStore(kBigRows);
        const qint64 buildMsFlat = tB.elapsed();

        qInfo().noquote() << QStringLiteral("  构建耗时：QStandardItemModel=%1 ms，自定义 FlatStore=%2 ms，"
                                            "倍数≈%3.%4 倍（预期：便利类明显更慢，因为它 new 了 %5 个对象）")
                                 .arg(buildMsItem).arg(buildMsFlat)
                                 .arg(buildMsItem / qMax<qint64>(1, buildMsFlat))
                                 .arg(10 * (buildMsItem % qMax<qint64>(1, buildMsFlat)) / qMax<qint64>(1, buildMsFlat))
                                 .arg(items);

        // —— 对象规模的可核对算术（**不是**实测内存，是下界估算）
        qInfo().noquote() << QStringLiteral("  编译期可得的事实：sizeof(QStandardItem)=%1 字节，"
                                            "sizeof(Row4 结构体的一行)=%2 字节，sizeof(QString)=%3 字节")
                                 .arg(sizeof(QStandardItem)).arg(sizeof(Row4)).arg(sizeof(QString));
        const double itemBytes = double(items) * double(sizeof(QStandardItem));
        const double flatBytes = double(kBigRows) * double(sizeof(Row4));
        qInfo().noquote() << QStringLiteral("  下界估算：便利类 = %1 个对象 × sizeof = %2；"
                                            "自定义 = %3 行 × sizeof(Row4) = %4")
                                 .arg(items).arg(humanBytes(itemBytes))
                                 .arg(kBigRows).arg(humanBytes(flatBytes));
        qInfo().noquote() << QStringLiteral("  【注意】 这只是**下界**：每格还有 d-pointer 指向的私有对象（大小未公开，"
                                            "第 211 行 Q_DECLARE_PRIVATE）、每格 QString 的堆缓冲、"
                                            "以及模型自己那棵树状容器。真实常驻至少再翻 2~3 倍。");
        qInfo().noquote() << QStringLiteral("  要真数请用 OS 工具自己测（本示例不猜 API）：Windows 任务管理器/"
                                            "Process Explorer 看 Working Set；MSYS2 下 `/usr/bin/time -v ./程序`；"
                                            "或读 /proc/self/status 的 VmRSS（Linux）。"
                                            "Qt 侧我 grep 过：本机头文件里没有跨平台读自身 RSS 的公共类。");

        // —— 逐个读 1 万格：两种「拿数据」的路径长度差别
        QElapsedTimer tR; tR.start();
        qint64 sinkItem = 0;
        for (int r = 0; r < 10000; ++r)
            for (int c = 0; c < kBigCols; ++c)
                sinkItem += itemModel->item(r, c) ? itemModel->item(r, c)->text().size() : 0;
        const qint64 readMsItem = tR.elapsed();
        tR.restart();
        qint64 sinkFlat = 0;
        for (int r = 0; r < 10000; ++r)
            for (int c = 0; c < kBigCols; ++c)
                sinkFlat += flat->index(r, c).data().toString().size();
        const qint64 readMsFlat = tR.elapsed();
        qInfo().noquote() << QStringLiteral("  读同样的 %1 格：item(r,c) 直取对象 = %2 ms（累计字符 %3），"
                                            "index().data() 走完整模型协议 = %4 ms（累计字符 %5）")
                                 .arg(10000 * kBigCols).arg(readMsItem).arg(sinkItem)
                                 .arg(readMsFlat).arg(sinkFlat);
        qInfo().noquote() << QStringLiteral("  这条不是「谁快谁好」，是**两种用法**：便利类的爽点就在能直接抓着"
                                            "item 对象改；代价是这条路完全绕过了 index，也就绕不过 40 万个对象。");

        // —— 画一屏：两者在**绘制层面**都不该被总行数拖垮（第 2 讲的结论在便利类上也成立）
        //    两个视图放进这个小括号：**视图先亡，模型后 delete**，顺序干净，不留悬垂指针。
        qint64 paintMsItem = 0;
        qint64 paintMsFlat = 0;
        {
            QTableView vItem;
            vItem.setModel(itemModel);
            vItem.resize(900, 420);
            vItem.show();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

            QTableView vFlat;
            vFlat.setModel(flat);
            vFlat.resize(900, 420);
            vFlat.show();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

            tR.restart();
            vItem.viewport()->repaint();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            paintMsItem = tR.elapsed();

            flat->resetCounters();
            tR.restart();
            vFlat.viewport()->repaint();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            paintMsFlat = tR.elapsed();

            qInfo().noquote() << QStringLiteral("  重绘一屏（都是 10 万行）：便利类 %1 ms，自定义 %2 ms；"
                                                "自定义侧的账本：%3")
                                     .arg(paintMsItem).arg(paintMsFlat).arg(flat->counterReport());
        }
        qInfo().noquote() << QStringLiteral("  结论（可核对的那条）：**画一屏的开销与总行数无关**，"
                                            "只与可见格子数有关（第 2 讲证据④的结论在便利类上同样成立）；"
                                            "差别全在「构建 + 常驻 + 逐格访问」这三笔上。");

        // —— 三档判断，打印成一张随身卡
        qInfo().noquote() << QStringLiteral("  三档判断（背下来）：");
        qInfo().noquote() << QStringLiteral("    ① 几百~几千行、要频繁改格子、还要树/表混着显示 → QStandardItemModel，"
                                            "别硬写模型（第 1、2、3 讲那些纪律它都替你办了）");
        qInfo().noquote() << QStringLiteral("    ② 数据本来在别处（std::vector/QSqlQuery/缓存）、行数千级以上 → "
                                            "自定义 QAbstractTableModel（../model_view/02），只在 setData 里回写");
        qInfo().noquote() << QStringLiteral("    ③ 旧代码/临时面板要「先出界面」→ QStandardItemModel 当**胶水**，"
                                            "但把行数当红线：一旦上量（比如本讲的 10 万行）就换 ②，"
                                            "换的时候视图与委托一行都不用动 —— 这就是模型/视图的隔离红利");
        qInfo().noquote() << QStringLiteral("    （另一条线：只是显示字符串列表用 QStringListModel，"
                                            "只是要个列表框/树框/表格框的现成控件走 ../widgets/04 的 QListWidget 那族）");
        // 两份大模型留着，收尾再 delete（顺便验证 delete 不崩）
        delete itemModel;
        delete flat;
    });

    // ------------------------------------------------------------ 证据⑥：增删与排序（便利类的另一半爽点）
    QTimer::singleShot(180, &app, [&h]() {
        qInfo().noquote() << QStringLiteral("⑥ insertRows / sort / takeItem：结构性操作的日常写法");
        QStandardItemModel m(0, 2);
        for (int r = 0; r < 5; ++r)
            m.appendRow(QList<QStandardItem*>()
                        << new QStandardItem(QStringLiteral("第 %1 行").arg(r))
                        << new QStandardItem(QStringLiteral("%1").arg((r * 37) % 100, 3, 10, QLatin1Char('0'))));
        h.attach(&m);
        h.zero();
        m.insertRow(2, QList<QStandardItem*>() << new QStandardItem(QStringLiteral("插进来的第 2 行"))
                                               << new QStandardItem(QStringLiteral("000")));
        qInfo().noquote() << QStringLiteral("  insertRow(2) 后行数=%1，rowsInserted 计 %2 次，"
                                            "第 2 行文字=%3").arg(m.rowCount()).arg(h.rowsInsertedHits)
                                 .arg(m.item(2, 0) ? m.item(2, 0)->text() : QStringLiteral("<null>"));
        QTableView v;
        v.setModel(&m);
        m.sort(1, Qt::AscendingOrder);              // 命令**模型**排（QStandardItemModel::sort，第 46 行）
        v.sortByColumn(1, Qt::DescendingOrder);     // 视图侧的入口：等价于点表头（qtableview.h 第 98 行）
        qInfo().noquote() << QStringLiteral("  m.sort(第1列, Asc) 之后第 0 行的数字列=%1，"
                                            "对照模型 sortRole()=%2（便利类自带 sortRole 属性，头文件第 92 行）")
                                 .arg(m.item(0, 1) ? m.item(0, 1)->text() : QStringLiteral("<null>"))
                                 .arg(m.sortRole());
        QStandardItem *takeOne = m.takeItem(0, 0);
        qInfo().noquote() << QStringLiteral("  takeItem(0,0) 拿到 %1，模型该格现在=%2（**空指针，不是空 item**）")
                                 .arg(takeOne ? takeOne->text() : QStringLiteral("<null>"))
                                 .arg(m.item(0, 0) ? m.item(0, 0)->text() : QStringLiteral("<null>"));
        delete takeOne;                               // take 的所有权在你手上
        m.removeRow(0);
        qInfo().noquote() << QStringLiteral("  removeRow(0) 之后行数=%1，rowsInserted=%2 / itemChanged=%3 / dataChanged=%4")
                                 .arg(m.rowCount()).arg(h.rowsInsertedHits).arg(h.itemChangedHits).arg(h.dataChangedHits);
        qInfo().noquote() << QStringLiteral("  【注意】 别在 setItem() 里塞同一个 QStandardItem 两次：一个 item 只有一个爹，"
                                            "第二次塞会把它从第一次的位置摘走（这是便利类最常见的自伤写法）。");
    });

    // ------------------------------------------------------------ 证据⑦：QStringListModel 一眼
    QTimer::singleShot(210, &app, [&h]() {
        qInfo().noquote() << QStringLiteral("⑦ QStringListModel：一维字符串的便利模型（别当表格用）");
        QStringListModel slm(QStringList{ QStringLiteral("苹果"), QStringLiteral("香蕉"), QStringLiteral("橙子") });
        // QAbstractListModel 把 columnCount(const QModelIndex&) 声明在 **private** 段
        // （qabstractitemmodel.h 第 519 行），所以 `slm.columnCount(...)` 直接编不过：
        // 「列表只有一个列」是它的语义，Qt 故意不让你在外面问。要问就升到基类接口上问。
        QAbstractItemModel *asBase = &slm;
        qInfo().noquote() << QStringLiteral("  rowCount=%1（3 个字符串），经 QAbstractItemModel* 问 columnCount=%2"
                                            "（**只有 1 列，这就是它的形状**）")
                                 .arg(slm.rowCount()).arg(asBase->columnCount(QModelIndex()));
        qInfo().noquote() << QStringLiteral("  data(第1行)=%1；stringList().size()=%2；flags 含 ItemIsEditable=%3")
                                 .arg(slm.index(1, 0).data().toString())
                                 .arg(slm.stringList().size())
                                 .arg((slm.flags(slm.index(0, 0)) & Qt::ItemIsEditable) ? "是" : "否");
        slm.setData(slm.index(0, 0), QStringLiteral("西瓜"), Qt::EditRole);
        qInfo().noquote() << QStringLiteral("  setData(EditRole) 之后第 0 行=%1 → 它支持编辑，但只有文字一列；"
                                            "要多列就回到 QStandardItemModel 或自定义（../widgets/04 那条控件线也有它的用法）")
                                 .arg(slm.index(0, 0).data().toString());
        h.zero();
    });

    // ------------------------------------------------------------ 收尾自检
    QTimer::singleShot(240, &app, [&h]() {
        qInfo().noquote() << QStringLiteral("⑧ 收尾自检：三件本讲想钉死的事实");
        QStandardItemModel probe(1, 1);
        probe.setItem(0, 0, new QStandardItem(QStringLiteral("x")));
        const QStandardItem *it = probe.item(0, 0);
        qInfo().noquote() << QStringLiteral("  1) QStandardItem 不是 QObject：它没有 metaObject()，"
                                            "sizeof=%1 字节（Qt5 时代它是 QObject，6.x 去掉了继承）")
                                 .arg(sizeof(QStandardItem));
        qInfo().noquote() << QStringLiteral("     验证方式：本讲只调 item 的普通成员函数，不调任何 QObject 函数 —— "
                                            "如果它是 QObject 派生，这段照样编得过；真正的证据是头文件第 24 行的类声明。");
        qInfo().noquote() << QStringLiteral("  2) 便利模型的 itemFromIndex 与 indexFromItem 互逆："
                                            "indexFromItem(item(0,0)) = (%1,%2)，再 itemFromIndex 回来 = %3")
                                 .arg(probe.indexFromItem(it).row()).arg(probe.indexFromItem(it).column())
                                 .arg(probe.itemFromIndex(probe.index(0, 0)) == it ? "同一个指针" : "不是同一个");
        qInfo().noquote() << QStringLiteral("  3) 全程信号计数：itemChanged=%1 dataChanged=%2 rowsInserted=%3 modelReset=%4"
                                            "（便利类把这些都替你发了，这正是它值钱的地方）")
                                 .arg(h.itemChangedHits).arg(h.dataChangedHits)
                                 .arg(h.rowsInsertedHits).arg(h.modelResetHits);
        QCoreApplication::quit();
    });

    const int rc = app.exec();
    qInfo().noquote() << QStringLiteral("（事件循环正常退出，rc=%1）").arg(rc);
    return rc;
}

// ============================================================================
// 五、老师傅的话 + 小练习
// ============================================================================
/*
 * ——— 老师傅的话 ———
 *   徒弟，便利类这东西，我用了很多年，结论是一句大白话：**它不是「新手用」或「旧代码用」的
 *   标记，它是「数据形态临时、行数不大、要频繁改格子」这套场景的正确答案**。你别听人吹
 *   「资深都手写模型」就硬扛，也别因为它三十行就出界面就到处用。分水岭在**规模**和**所有权**。
 *
 *   规模这条最容易骗自己。「我才十万行而已」——十万行四列是四十万个 QStandardItem，
 *   每个对象一次堆分配、还牵一个私有 d-pointer、还带 vtable（第 24/31/211 行我都抄给你了）。
 *   构建慢几倍是小事，用户感受是「打开就卡一下」；麻烦的是三件事叠一起：
 *     · 常驻内存上去了，低端机器直接开始换页；
 *     · 每格都背着 children 容器，纯表格白付；
 *     · 排序/过滤一旦再套一层代理，代理问源模型问的就是这四十万个对象。
 *   证据⑤那两份耗时你亲手跑一遍，红线自己就有了，比我念十遍都强。
 *
 *   所有权这条更阴。便利类的所有权规则很简单：**你 new 出来交给模型的那些，别再自己 delete；
 *   take* 还给你的那些，必须你 delete**。就这两句。可日常出事的是中间那类：
 *   有人图省事把同一个 QStandardItem 塞进两个格子，运行时不报错，但那个对象只有一个爹，
 *   第二次 setItem 会把它从第一次的位置摘走 —— 界面上就变成「A 列好好的，B 列一空，A 也空了」。
 *   还有人在 setItem(row, col, nullptr) 之后继续 item(row,col)->text()，直接空指针崩。
 *   本讲第⑥条把这两个坑都点了，你写的时候手边放一下。
 *
 *   第三句给你换模型的路径。「先胶水后重写」是正当的工程手段，前提是你从第一天就把
 *   视图/委托/选择这三样绑在**接口**而不是绑在 QStandardItemModel 上：视图只认 QAbstractItemModel*，
 *   委托只认 index+role，选择只认 selectionModel。这样证据⑤里那张「三档判断」的③→②
 *   就是一次替换 setModel 的事，一天搞定。反过来，你代码里到处 item(r,c)->setText(...)
 *   这种硬抓对象的写法，将来换自定义模型时要一格一格改回去 —— 那才叫债。
 *   所以我的偏好是：**便利类可以用，但业务代码里尽量走 index + setData 这条路，
 *   把 item 对象留给初始化那几行。**
 *
 *   最后一个提醒：别把「不用自己发 dataChanged」当成「模型的信号我不用学」。
 *   便利类替你发，你就不用理解了吗？第 3 讲那条回路（提交→setData→改存储→emit→视图重画）
 *   你还是得会画，面试会问、线上出「改完界面不刷新」的 bug 也会找上你——只不过那时候
 *   凶手多半是「item 被复制了一份」或者「你改了个脱离模型的 item」。
 *
 * ——— 小练习 ———
 *   1. 把 kBigRows 从 100000 改成 20000、50000、100000 各跑一遍，把两份模型的构建耗时和
 *      你在 OS 任务管理器里看到的 Working Set 记成表；找出「你的机器上」两者耗时差距
 *      开始变得不可接受的那一行，写下判断依据。
 *   2. 故意写一次「同一个 QStandardItem* 塞两格」（setItem(0,1,it); setItem(1,1,it);），
 *      然后打印两格的 text 与 it->row()/column()，把「对象只有一个爹」这件事亲眼验证出来；
 *      想复制一格请用 `it->clone()`——注意 `new QStandardItem(*it)` 是**编不过**的，
        因为拷贝构造在 protected 段（头文件第 202~206 行），只有 clone() 是公开的复制入口。
 *   3. 把证据①里 setEditable(false) 换成 setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable)，
 *      再把视图的 editTriggers 设成 QAbstractItemView::DoubleClicked，程序调用 view->edit(index)
 *      观察能不能开编辑器；然后回答：便利类里「不让改」有哪三种写法（flags / setData 里拒绝 /
 *      只给 DisplayRole 不给 EditRole），各自在 ../model_view/03 的回路里挡在哪一步。
 */
// ============================================================================

// 本文件没有 Q_OBJECT 类（FlatStore 只覆写虚函数，Harness 只当 connect 的 context），
// 所以末尾不需要 #include "07_item_models.moc"。
