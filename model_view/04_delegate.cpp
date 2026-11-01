// ============================================================================
// 第 4 讲 · 自定义委托：视图与数据之间「画一格 + 管编辑器」的那一层
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   前三讲把「答话」（data）与「接活」（setData）都写完了，但屏幕上那一格
//   「长什么样、用什么控件改」还全是默认答案。这一讲把委托（Delegate）拆开：
//     1) 十个回调各自的**签名与时机**，逐行对着本机头文件核（见第一节），
//        包括两个最容易被背错的点：createEditor/setEditorData/setModelData/
//        updateEditorGeometry 全带 **const**，而 editorEvent **不带 const**
//        （qabstractitemdelegate.h 第 66/69 行）；paint/sizeHint 是**纯虚**（第 41~46 行）。
//     2) 为什么 paint 必须借 QStyleOptionViewItem 的手：选中/焦点/悬停/RTL 全在
//        option.state 里（qstyleoption.h 第 60 行 `QStyle::State state;`），
//        自己 new 一个 option 重画 = 丢掉全部状态；正确姿势是**拷一份**，
//        在副本上改 text/backgroundBrush/decorationPosition，再用
//        QStyle::drawControl(CE_ItemViewItem, &副本, painter, widget)（qstyle.h 第 230/239 行）复用样式。
//     3) 编辑器的**生命周期与所有权**：createEditor 造出来的 widget 挂谁给的
//        parent（视图的 viewport）、谁负责删（closeEditor 信号 → 视图调 destroyEditor）。
//     4) 「提交」与「不提交直接关」的区别——⚠ 勘误：流传的
//        `QAbstractItemDelegate::CommitData{RevertBefore, Keep}` 这个枚举**在本机 Qt 6.12
//        的全部公开头文件里 grep 为 0 命中，不存在**。真机制是委托的两个信号
//        commitData(editor)/closeEditor(editor, EndEditHint)（第 81/82 行）连到视图的两个
//        protected 槽（qabstractitemview.h 第 240/241 行）：走 commitData 才会调 setModelData 写模型，
//        只走 closeEditor = 编辑器销毁、模型一个字没变。EndEditHint 真值五个（第 29~35 行）。
//     5) 异步/延迟提交的守卫：编辑器还开着、模型那边删了行怎么办？用
//        QPersistentModelIndex（本机在 qabstractitemmodel.h 第 186 行起，**没有**独立的
//        小写 qpersistentmodelindex.h，用 <QPersistentModelIndex>）+ QPointer 双守卫，
//        本讲把它做成可核对的证据：删行后延迟提交被跳过，模型没被写脏。
//     6) displayText 的正用（qstyleditemdelegate.h 第 53 行）：模型 DisplayRole 返回**裸 int**，
//        「拼百分号」这种事后格式化挪到委托里——第 2 讲证据④欠的债在这儿还。
//   注意版本坑：Qt 6.12 本机 QStyleOptionViewItem 的显示文本成员叫 **text**（第 443 行），
//   新版文档里的 QStyleOptionDisplayItem/displayText 成员在 6.12 里查无此名，照抄必编不过。  name-ok:QStyleOptionDisplayItem 新版文档里的类，本行正在说 6.12 查无此名
//
// 对应构建目标名：model_view_04_delegate
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(model_view_04_delegate 04_delegate.cpp)
//   target_link_libraries(model_view_04_delegate PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)                       # 本文件**有 Q_OBJECT 类**，需要 moc
//   # 手工核对时：moc 04_delegate.cpp -o 04_delegate.moc
//   #             文件末尾那句 #include "04_delegate.moc" 就是给 AUTOMOC 的钩子
//
// 运行前须知：
//   · 无人值守：show → 强制重绘取证 → view.edit(index) 开编辑器 → 对编辑器 setValue/setText
//     → 提交/删行/不提交直接关，全程 QTimer::singleShot 串步骤，**没有** exec() 阻塞对话框。
//   · 可以无头跑：`model_view_04_delegate -platform offscreen`。
//   · 数据全部在内存里现造（8 行 × 3 列），不读文件、不联网。
//   · paint/sizeHint 的**次数**依赖样式与可见区；本讲承诺的是关系：
//     「改过的值没走 commitData 就不会进模型」「跳过计数 == 被删行时挂着的编辑器数」。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 model_view_04_delegate，跑到自动退出、退出码 0）。
//     但本讲**没有汇总自检行**，退出码 0 只证明「没崩、走完了」，不证明正文每个数字都对：
//     带具体数字的「预期」请以你自己那次运行输出为准（本机那一轮存档 runlogs/model_view_04_delegate.log）。
//   · 图解页面：../visuals/delegate_lifecycle.html（这一讲的主线图）。
// ============================================================================

#include <QApplication>                 // QtWidgets/qapplication.h：QApplication::style()
#include <QStyledItemDelegate>          // QtWidgets/qstyleditemdelegate.h
#include <QAbstractItemDelegate>        // QtWidgets/qabstractitemdelegate.h
#include <QAbstractTableModel>          // QtCore/qabstractitemmodel.h
#include <QPersistentModelIndex>        // QtCore/…（类定义在 qabstractitemmodel.h 第 186 行起）
#include <QTableView>                   // QtWidgets/qtableview.h：visualRect 第 77 行
#include <QHeaderView>                  // QtWidgets/qheaderview.h
#include <QAbstractItemView>            // QtWidgets/qabstractitemview.h
#include <QStyle>                       // QtWidgets/qstyle.h
#include <QStyleOptionViewItem>         // QtWidgets/qstyleoption.h 第 405~453 行
#include <QLineEdit>                    // QtWidgets/qlineedit.h
#include <QSpinBox>                     // QtWidgets/qspinbox.h
#include <QPainter>                     // QtGui/qpainter.h
#include <QPalette>                     // QtGui/qpalette.h
#include <QColor>
#include <QEvent>                       // QtCore/qcoreevent.h：第 305 行 `explicit QEvent(Type)`
#include <QPointer>                     // QtCore/qpointer.h：观测编辑器生死
#include <QTimer>
#include <QCoreApplication>
#include <QEventLoop>                   // 只用来 processEvents 催一轮重绘（**不 exec**）
#include <QGuiApplication>              // platformName()
#include <QVariant>
#include <QMetaType>                    // QMetaType::Int（displayText 里判类型用）
#include <QLocale>                      // displayText 的第二个参数
#include <QString>
#include <QStringList>
#include <QRect>
#include <QSize>
#include <QFontMetrics>                 // QtGui/qfontmetrics.h：第 54 行 horizontalAdvance（Qt6 已无 width()）
#include <QDebug>

#include <vector>

// ============================================================================
// 一、签名核对清单（本机 D:\Qt\6.12.0\mingw_64\include，Qt 6.12.0）
// ============================================================================
// QtWidgets/qabstractitemdelegate.h（委托的「契约」本体）
//   第 29~35 行  enum EndEditHint { NoHint, EditNextItem, EditPreviousItem,
//                    SubmitModelCache, RevertModelCache };   ← closeEditor 的第二参数
//   第 41~43 行  virtual void paint(QPainter*, const QStyleOptionViewItem&,
//                    const QModelIndex&) const = 0;          ← 纯虚，**const**
//   第 45~46 行  virtual QSize sizeHint(const QStyleOptionViewItem&,
//                    const QModelIndex&) const = 0;          ← 纯虚，**const**
//   第 49~51 行  virtual QWidget *createEditor(QWidget *parent,
//                    const QStyleOptionViewItem&, const QModelIndex&) const;
//                    ← 默认实现返回空指针 =「这一格没有编辑器」
//   第 53 行     virtual void destroyEditor(QWidget *editor,
//                    const QModelIndex&) const;              ← 收尾；默认实现负责处置编辑器，
//                    你**别自己 delete**，视图 closeEditor 之后这条链会走到它
//   第 55 行     virtual void setEditorData(QWidget*, const QModelIndex&) const; ← 模型→编辑器（灌初值）
//   第 57~59 行  virtual void setModelData(QWidget*, QAbstractItemModel*,
//                    const QModelIndex&) const;              ← 编辑器→模型（写回），方向与上面相反
//   第 61~63 行  virtual void updateEditorGeometry(QWidget*,
//                    const QStyleOptionViewItem&, const QModelIndex&) const; ← 摆位置
//   第 66~69 行  virtual bool editorEvent(QEvent*, QAbstractItemModel*,
//                    const QStyleOptionViewItem&, const QModelIndex&);  ← **非 const**，public；
//                    给「非控件编辑器」（画出来的勾选框之类）处理鼠标/键事件用
//   第 71 行     bool handleEditorEvent(QObject*, QEvent*);   ← 非虚，视图转发达人用
//   第 73~76 行  virtual bool helpEvent(QHelpEvent*, QAbstractItemView*,
//                    const QStyleOptionViewItem&, const QModelIndex&); ← ToolTip/What'sThis 事件
//   第 78 行     virtual QList<int> paintingRoles() const;    ← 声明「paint 会读哪些角色」，
//                    视图据此决定 dataChanged 带哪些角色时要不要重画这一格
//   第 81 行     void commitData(QWidget *editor);            ← **信号**：委托喊「该提交了」
//   第 82 行     void closeEditor(QWidget *editor,
//                    QAbstractItemDelegate::EndEditHint hint = NoHint);  ← **信号**
//   第 83 行     void sizeHintChanged(const QModelIndex &);   ← 信号：这格尺寸变了（动态行高用）
// QtWidgets/qstyleditemdelegate.h（「按样式画 + 控件编辑器」的现成实现，本讲的基类）
//   第 30~31 行 paint / 第 32~33 行 sizeHint / 第 36~38 行 createEditor /
//   第 40 行 setEditorData / 第 41~43 行 setModelData / 第 45~47 行 updateEditorGeometry（全部 override）
//   第 53 行  virtual QString displayText(const QVariant&, const QLocale&) const;
//   第 56~57 行 protected: virtual void initStyleOption(QStyleOptionViewItem*,
//                    const QModelIndex&) const; ← 把 text/icon/checkState 灌进 option
//   第 59~61 行 protected: eventFilter / editorEvent ← **在 QStyledItemDelegate 里降为 protected**，
//                    但视图都通过 QAbstractItemDelegate*（public）调它，本讲为了演示包了个 public 口
// QtWidgets/qabstractitemview.h（视图侧的接线柱）
//   第 79~87 行 enum EditTrigger { NoEditTriggers=0, CurrentChanged=1, DoubleClicked=2,
//                    SelectedClicked=4, **EditKeyPressed=8**, AnyKeyPressed=16, AllEditTriggers=31 };
//                    ← 真名是 EditKeyPressed；别处看到的「EditedClicked」是讹传，头文件里没有
//   第 107 行  void setItemDelegate(QAbstractItemDelegate*);
//   第 200 行  void setItemDelegateForColumn(int, QAbstractItemDelegate*);   ← 按列挂不同委托
//   第 203~207 行 itemDelegate(const QModelIndex&) **Qt 6.0 起废弃** → 用第 208 行 itemDelegateForIndex
//   第 219 行  public slot: void edit(const QModelIndex&);   ← 程序化进入编辑（本讲用它免鼠标）
//   第 221 行  public slot: void setCurrentIndex(const QModelIndex&);
//   第 240/241/242 行 protected slots: closeEditor(editor, hint) / commitData(editor) /
//                    editorDestroyed(QObject*) ← 视图把委托那两个信号接到这里，本讲全部用计数旁观
// QtWidgets/qstyle.h
//   第 230 行  ControlElement 里 **CE_ItemViewItem**（第 237 行 Q_ENUM）
//   第 239~240 行 virtual void drawControl(ControlElement, const QStyleOption*, QPainter*,
//                    const QWidget *w = nullptr) const = 0;   ← 第四个参数可空
//   第 76/81/83 行 State_HasFocus=0x100 / State_MouseOver=0x2000 / State_Selected=0x8000
// QtWidgets/qstyleoption.h
//   第 39~73 行  QStyleOption 基类：第 60 行 `QStyle::State state;`、62 rect、63 fontMetrics、64 palette
//   第 405~453 行 QStyleOptionViewItem：411 enum Position{Left,Right,Top,Bottom}、
//   416 decorationPosition、433 features（ViewItemFeature：None=0x00 WrapText=0x01 Alternate=0x02
//   HasCheckIndicator=0x04 HasDisplay=0x08 HasDecoration=0x10 …421~430）、435 locale、
//   436 `const QWidget *widget;`、440 index、441 checkState、442 icon、**443 text**、445 backgroundBrush
// QtCore/qabstractitemmodel.h
//   第 186~221 行 QPersistentModelIndex：190 从 QModelIndex 构造、209 隐式转回 QModelIndex、
//                    210 row()、211 column()、217 data()、221 isValid()
//   第 284 行 setData / 304 insertRows / 306 removeRows / 359 dataChanged（信号，带 roles）
// QtGui/qpainter.h：153 setPen(QColor) / 201~202 save/restore / 268 drawRect(QRect) /
//   375 drawText(QRect, int flags, QString, QRect* = nullptr) / 392 fillRect(QRect, QBrush) /
//   396 fillRect(QRect, QColor)
// QtWidgets/qspinbox.h：34 value() / 53 setRange / 70 setValue / 73 信号 valueChanged(int)
//   （QSpinBox 只有一个 valueChanged(int)，&QSpinBox::valueChanged 不用 qOverload；double 版是 QDoubleSpinBox 的）
// QtWidgets/qlineedit.h：62 text() / 149 setText / 168 信号 textChanged(QString) / 172 editingFinished()
// ———— 勘误存档 ————
//   grep -r "RevertBefore\|NoCommit\b\|CommitData\b" QtWidgets/*.h → **0 命中**。
//   「QAbstractItemDelegate::CommitData{RevertBefore, Keep}」不存在，别往代码里写。
//   「提交 vs 不提交」= commitData 信号发不发（发 → 视图槽调 setModelData；不发只 closeEditor → 只销毁编辑器）。
// ============================================================================

// ============================================================================
// 二、演示数据：小而完整的自定义表模型（第 2/3 讲的做法，这里一次带全）
// ============================================================================
struct TaskRow {
    QString name;
    int     progress = 0;   // 0..100，**裸整数**，格式化是委托的事
    QString owner;
};

enum TaskColumn { ColName = 0, ColProgress, ColOwner, ColCount };

class TaskTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit TaskTableModel(QObject *parent = nullptr) : QAbstractTableModel(parent)
    {
        const char *names[] = { "巡检", "组塔", "架线", "变电站", "通信", "基础", "消缺", "验收" };
        const char *owners[] = { "张工", "李工", "王工", "赵工" };
        for (int i = 0; i < 8; ++i) {
            TaskRow r;
            r.name = QString::fromUtf8(names[i]) + QStringLiteral("任务");
            r.progress = 10 + i * 10;                 // 10,20,...,80
            r.owner = QString::fromUtf8(owners[i % 4]);
            m_rows.push_back(r);
        }
    }

    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.isValid() || !hasIndex(row, column, parent))
            return QModelIndex();
        return createIndex(row, column, nullptr);     // 表格用行号定位就够，别塞 vector 元素地址（第 2 讲证据③）
    }
    QModelIndex parent(const QModelIndex &child) const override { Q_UNUSED(child) return QModelIndex(); }
    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    { return parent.isValid() ? 0 : int(m_rows.size()); }
    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    { Q_UNUSED(parent) return ColCount; }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() >= int(m_rows.size()))
            return QVariant();
        const TaskRow &r = m_rows.at(size_t(index.row()));
        if (role == Qt::DisplayRole || role == Qt::EditRole) {
            switch (index.column()) {
            case ColName:     return r.name;
            case ColProgress: return r.progress;      // ★ 两个角色都给**原始 int**，「%」交给委托 displayText
            case ColOwner:    return r.owner;
            }
            return QVariant();
        }
        if (role == Qt::TextAlignmentRole && index.column() == ColProgress)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        if (role == Qt::ToolTipRole && index.column() == ColProgress)
            return QStringLiteral("完成 %1%（点一下 +10，见 editorEvent）").arg(r.progress);
        return QVariant();
    }

    // 「接活」这一半（第 3 讲）：验 → 比 → 写 → **必须 emit dataChanged**
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override
    {
        ++s_setDataCalls;
        if (!index.isValid() || role != Qt::EditRole || index.row() >= int(m_rows.size()))
            return false;
        TaskRow &r = m_rows[size_t(index.row())];
        bool changed = false;
        if (index.column() == ColName && r.name != value.toString()) { r.name = value.toString(); changed = true; }
        if (index.column() == ColProgress && r.progress != value.toInt()) { r.progress = value.toInt(); changed = true; }
        if (!changed)
            return false;                              // 没变就别发信号，视图白重绘
        ++s_dataChangedEmits;
        emit dataChanged(index, index, QList<int>{ Qt::DisplayRole, Qt::EditRole });
        return true;
    }

    // 两道门之一（另一道是视图 editTriggers）：负责人列**不给可编辑位**，
    // 于是视图根本不会为它调 createEditor —— 这本身就是委托链的一条证据。
    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        Qt::ItemFlags f = QAbstractTableModel::flags(index);
        if (index.isValid() && index.column() != ColOwner)
            f |= Qt::ItemIsEditable;
        return f;
    }

    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override
    {
        if (row < 0 || row + count > int(m_rows.size()))
            return false;
        beginRemoveRows(parent, row, row + count - 1);  // 顺序：先喊、再改、后收尾（第 3 讲纪律）
        m_rows.erase(m_rows.begin() + row, m_rows.begin() + row + count);
        endRemoveRows();
        return true;
    }

    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override
    {
        if (role != Qt::DisplayRole)
            return QVariant();
        if (orientation == Qt::Horizontal) {
            static const char *hs[] = { "任务", "进度（委托画成进度条）", "负责人（不可编辑）" };
            return QString::fromUtf8(hs[section]);
        }
        return section + 1;
    }

    static void resetCounters() { s_setDataCalls = 0; s_dataChangedEmits = 0; }
    static qint64 setDataCalls() { return s_setDataCalls; }
    static qint64 dataChangedEmits() { return s_dataChangedEmits; }

private:
    std::vector<TaskRow> m_rows;
    inline static qint64 s_setDataCalls = 0;
    inline static qint64 s_dataChangedEmits = 0;
};

// ============================================================================
// 三、自定义委托：进度列画进度条 + 两种提交路径 + 全套取证计数
// ============================================================================
// 取证成员全是 **mutable**：被覆写的 paint/setModelData/createEditor 按契约都是 const，
// 而计数必须记得下来——「证据」与「const 承诺」不冲突，因为不改委托的对外行为。
class ProgressDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit ProgressDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent)
    {
        m_deferTimer = new QTimer(this);
        m_deferTimer->setSingleShot(true);
        // ctor 里 this 是非 const，lambda 里 emit 信号没问题（信号函数非 const，见下面 createEditor 的注）
        connect(m_deferTimer, &QTimer::timeout, this, [this]() { flushDeferredCommit(); });
    }

    // ── 画：只在进度列亲自动手，其余列**交还基类**（别吞掉默认行为） ──
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        ++m_paintTotal;
        if (index.column() == ColProgress && ++m_paintProgressCells == 1)
            m_firstProgressRect = option.rect;         // 抓「视图给我的那一格」的 rect 当证据
        if (index.column() != ColProgress) {
            QStyledItemDelegate::paint(painter, option, index);   // 基类里已经做了：
            return;                                              // initStyleOption + displayText + drawControl
        }
        // ① **副本**纪律：传入的 option 是视图的（const 引用），要改就拷一份再改。
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);                  // ② 灌 text/icon/checkState（第 56/57 行，protected 工具）
        ++m_initStyleOptionTotal;
        const int percent = index.data(Qt::EditRole).toInt();
        opt.text = QString();                          // 文字我自己画，基类只画格子底
        if (percent >= 100)
            opt.backgroundBrush = QColor(233, 247, 236);   // ③ 改副本，不改本体
        opt.decorationPosition = QStyleOptionViewItem::Left; // 演示 Position 枚举（第 411/416 行）
        // ④ 复用样式画格子：选中/焦点/悬停全在 option.state 里带着，自己另起炉灶必丢。
        QStyle *st = opt.widget ? opt.widget->style() : QApplication::style();
        st->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);   // qstyle.h 239/240 行
        // ⑤ 进度条（用 state 判断高亮态，别写死颜色体系）
        const bool selected = option.state & QStyle::State_Selected;      // 0x8000（qstyle.h 83 行）
        const bool hot = option.state & QStyle::State_MouseOver;         // 0x2000（81 行）
        const QRect bar(opt.rect.left() + 6, opt.rect.center().y() - 4,
                        qMax(10, opt.rect.width() - 74), 8);
        painter->save();                                               // qpainter.h 201 行
        painter->setPen(Qt::NoPen);
        painter->fillRect(bar, QColor(214, 218, 222));
        const int filled = int(bar.width() * percent / 100.0 + 0.5);
        painter->fillRect(QRect(bar.left(), bar.top(), filled, bar.height()),
                          selected ? QColor(30, 110, 60) : (hot ? QColor(52, 170, 96) : QColor(65, 205, 82)));
        m_lastBarFilledWidth = filled;
        const QString txt = QStringLiteral("%1%").arg(percent);
        const QRect textRect(opt.rect.right() - 62, opt.rect.top(), 54, opt.rect.height());
        painter->setPen(selected ? option.palette.color(QPalette::HighlightedText)
                                 : QColor(40, 44, 48));
        if (option.fontMetrics.horizontalAdvance(txt) <= textRect.width())  // 54 行（Qt6 已删 width()）
            painter->drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, txt);   // 375 行
        painter->restore();
    }

    // ── 尺寸：视图只在表头是 ResizeToContents 时来问；进度列加高 10px ──
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        ++m_sizeHintTotal;
        const QSize base = QStyledItemDelegate::sizeHint(option, index);
        if (index.column() == ColProgress)
            return QSize(qMax(base.width(), 96), base.height() + 10);
        return base;
    }

    // ── 编辑链①：造控件。parent 是视图传进来的 viewport——**谁给 parent 谁负责删** ──
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        ++m_createEditorTotal;
        Q_UNUSED(option)
        // 信号的成员函数**非 const**，而本函数按契约是 const：const_cast 是 Qt 源码里
        // 委托自己同款写法，为了把 this 交给延迟/立即提交的 lambda 用。
        ProgressDelegate *self = const_cast<ProgressDelegate *>(this);
        QWidget *editor = nullptr;
        switch (index.column()) {
        case ColName: {                                 // 路径 A：**防抖提交**（150ms 内不重复提交）
            QLineEdit *le = new QLineEdit(parent);
            le->setPlaceholderText(QStringLiteral("改名后停 150ms 自动提交"));
            // 在 createEditor 这一刻就把坐标「挂号」成持久索引（第 190 行 ctor 只吃 QModelIndex）
            connect(le, &QLineEdit::textChanged, le,
                    [self, le, persistent = QPersistentModelIndex(index)](const QString &) {
                        self->armDeferredCommit(le, persistent);
                    });
            editor = le;
            break;
        }
        case ColProgress: {                             // 路径 B：**改一下即提交**（valueChanged 直发 commitData）
            QSpinBox *spin = new QSpinBox(parent);
            spin->setRange(0, 100);                     // qspinbox.h 53 行
            spin->setSingleStep(1);
            spin->setSuffix(QStringLiteral(" %"));
            connect(spin, &QSpinBox::valueChanged, spin, [self, spin](int) {
                ++self->m_commitSignalEmitted;
                emit self->commitData(spin);            // ← 委托发信号，**视图**的 commitData 槽（241 行）
            });                                         //   才会回调 setModelData → model->setData
            editor = spin;
            break;
        }
        default:
            return nullptr;   // 返回空指针 = 合法的「我没有编辑器」；负责人列 flags 已不给 Editable，走不到这
        }
        m_lastEditor = editor;
        m_editorParentIsViewport = (editor->parent() != nullptr);
        return editor;
    }

    // ── 编辑链②：模型 → 编辑器（灌初值；方向是「读」） ──
    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        ++m_setEditorDataTotal;
        if (QLineEdit *le = qobject_cast<QLineEdit *>(editor)) {
            le->setText(index.data(Qt::EditRole).toString());
        } else if (QSpinBox *spin = qobject_cast<QSpinBox *>(editor)) {
            spin->setValue(index.data(Qt::EditRole).toInt());
        }
    }

    // ── 编辑链③：编辑器 → 模型（写回；方向是「写」，与 setEditorData 正好相反） ──
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        ++m_setModelDataTotal;
        const QVariant oldValue = index.data(Qt::EditRole);
        QVariant newValue;
        if (const QLineEdit *le = qobject_cast<QLineEdit *>(editor))
            newValue = le->text();
        else if (const QSpinBox *spin = qobject_cast<QSpinBox *>(editor))
            newValue = spin->value();
        const bool ok = model->setData(index, newValue, Qt::EditRole);
        m_writeLog << QStringLiteral("行%1 列%2 ｜旧=%3 新=%4 ｜setData=%5 ｜写回后模型答 DisplayRole=%6")
                          .arg(index.row()).arg(index.column())
                          .arg(oldValue.toString(), newValue.toString())
                          .arg(ok ? "true " : "false")
                          .arg(model->data(index, Qt::DisplayRole).toString());
    }

    // ── 编辑链④：摆位置。option.rect 就是这一格（视图已算好），直接吃下来 ──
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const override
    {
        ++m_updateEditorGeometryTotal;
        Q_UNUSED(index)
        editor->setGeometry(option.rect);
        m_lastEditorRect = option.rect;
    }

    // ── 编辑链⑤：收尾。计数后**交还基类**处置编辑器（别自己 delete，视图也留了后手 editorDestroyed） ──
    void destroyEditor(QWidget *editor, const QModelIndex &index) const override
    {
        ++m_destroyEditorTotal;
        QStyledItemDelegate::destroyEditor(editor, index);   // 继承来的默认实现（第 53 行那份）
    }

    // ── 格式化：模型给裸 int，这里拼「%」（第 2 讲证据④说的 displayText 就在这儿） ──
    QString displayText(const QVariant &value, const QLocale &locale) const override
    {
        ++m_displayTextTotal;
        if (value.typeId() == QMetaType::Int)           // qvariant.h 573 行 typeId()
            return QStringLiteral("%1%").arg(value.toInt());
        return QStyledItemDelegate::displayText(value, locale);
    }

    // ── 非控件编辑器的事件入口。注意签名**没有 const**（第 66~69 行）。 ──
    // 平时由视图在鼠标/键盘事件落在格子上时调用（本讲为了确定性在 main 里直接调它一次）。
    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                     const QStyleOptionViewItem &option, const QModelIndex &index) override
    {
        ++m_editorEventTotal;
        if (event->type() == QEvent::MouseButtonRelease && index.column() == ColProgress) {
            const int bumped = qMin(100, index.data(Qt::EditRole).toInt() + 10);
            model->setData(index, bumped, Qt::EditRole);   // 「点一下 +10」：勾选框式交互就是这么写的
            return true;                                    // true = 事件我吃了，视图别再默认处理
        }
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }

    // initStyleOption 是 protected（第 56 行），给 main 包一个 public 口，
    // 用来在直接调 editorEvent 前把 option 灌成视图会给的样子（text/state 都是它填的）。
    void primeOption(QStyleOptionViewItem *opt, const QModelIndex &index) const
    {
        initStyleOption(opt, index);
    }
    // 「不提交直接关」的程序化入口：只发 closeEditor、不发 commitData（第 81/82 行两个信号的区别）。
    void closeWithoutCommit(QWidget *editor)
    {
        ++m_closeSignalEmitted;
        emit closeEditor(editor, QAbstractItemDelegate::NoHint);   // hint 真值：第 29~35 行五个
    }

    // ── 取证读数 ──
    QPointer<QWidget> lastEditor() const { return m_lastEditor; }
    QString writeLog() const { return m_writeLog.isEmpty() ? QStringLiteral("（还没有写回记录）")
                                                            : m_writeLog.join(QStringLiteral("\n     ")); }
    int paintTotal() const { return m_paintTotal; }
    int paintProgressCells() const { return m_paintProgressCells; }
    QRect firstProgressRect() const { return m_firstProgressRect; }
    int lastBarFilledWidth() const { return m_lastBarFilledWidth; }
    int sizeHintTotal() const { return m_sizeHintTotal; }
    int createEditorTotal() const { return m_createEditorTotal; }
    int setEditorDataTotal() const { return m_setEditorDataTotal; }
    int setModelDataTotal() const { return m_setModelDataTotal; }
    int updateEditorGeomTotal() const { return m_updateEditorGeometryTotal; }
    int destroyEditorTotal() const { return m_destroyEditorTotal; }
    int editorEventTotal() const { return m_editorEventTotal; }
    int displayTextTotal() const { return m_displayTextTotal; }
    int initStyleOptionTotal() const { return m_initStyleOptionTotal; }
    int commitEmitted() const { return m_commitSignalEmitted; }
    int closeEmitted() const { return m_closeSignalEmitted; }
    int deferredArmed() const { return m_deferredArmed; }
    int deferredSkipped() const { return m_deferredSkipped; }
    int deferredCommitted() const { return m_deferredCommitted; }
    bool editorHadParent() const { return m_editorParentIsViewport; }
    QRect lastEditorRect() const { return m_lastEditorRect; }

private:
    // 防抖提交：把「编辑器 + 持久坐标」存下来，150ms 后才真发 commitData。
    void armDeferredCommit(QWidget *editor, const QPersistentModelIndex &idx)
    {
        m_pendingEditor = editor;
        m_pendingIndex = idx;
        ++m_deferredArmed;
        m_deferTimer->start(150);
    }
    // ★ 异步编辑的双守卫：编辑器可能已被 closeEditor 销毁（QPointer 归零），
    //   那一行也可能已经被删掉（持久索引 isValid() 变 false）。两个查一个都不能少。
    void flushDeferredCommit()
    {
        if (!m_pendingEditor || !m_pendingIndex.isValid() || m_pendingEditor->isHidden()) {
            ++m_deferredSkipped;                       // 跳过 = 什么都没写进模型，脏写事故到此为止
            return;
        }
        QWidget *editor = m_pendingEditor.data();
        m_pendingEditor = nullptr;
        ++m_deferredCommitted;
        emit commitData(editor);
    }

    QTimer *m_deferTimer = nullptr;
    mutable QPointer<QWidget> m_lastEditor;
    mutable QPointer<QWidget> m_pendingEditor;         // mutable：createEditor 是 const
    mutable QPersistentModelIndex m_pendingIndex;
    mutable int m_paintTotal = 0;
    mutable int m_paintProgressCells = 0;
    mutable int m_sizeHintTotal = 0;
    mutable int m_createEditorTotal = 0;
    mutable int m_setEditorDataTotal = 0;
    mutable int m_setModelDataTotal = 0;
    mutable int m_updateEditorGeometryTotal = 0;
    mutable int m_destroyEditorTotal = 0;
    mutable int m_editorEventTotal = 0;
    mutable int m_displayTextTotal = 0;
    mutable int m_initStyleOptionTotal = 0;
    mutable int m_commitSignalEmitted = 0;
    int m_closeSignalEmitted = 0;
    int m_deferredArmed = 0;
    int m_deferredSkipped = 0;
    int m_deferredCommitted = 0;
    mutable QRect m_firstProgressRect;
    mutable int m_lastBarFilledWidth = -1;
    mutable bool m_editorParentIsViewport = false;
    mutable QRect m_lastEditorRect;
    mutable QStringList m_writeLog;
};

// ============================================================================
// 四、main：singleShot 串起来的自动取证
// ============================================================================
static void flushEvents() { QCoreApplication::processEvents(QEventLoop::AllEvents, 30); }

static int s_commitSeenByBystander = 0;   // 旁观计数：证明「commitData 是信号」可被任意方接收
static int s_closeSeenByBystander = 0;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("model_view_04_delegate"));

    TaskTableModel model;
    QTableView view;
    ProgressDelegate delegate;

    view.setItemDelegate(&delegate);                    // qabstractitemview.h 107 行
    // 两道门之二：编辑触发方式。程序化入口是第 219 行的 edit(index) 槽，本讲全程不用鼠标。
    view.setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked
                         | QAbstractItemView::EditKeyPressed);
    view.verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); // ← 让视图真的去问 sizeHint
    view.horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    QObject::connect(&delegate, &QAbstractItemDelegate::commitData, &app,
                     [](QWidget *) { ++s_commitSeenByBystander; });
    QObject::connect(&delegate, &QAbstractItemDelegate::closeEditor, &app,
                     [](QWidget *) { ++s_closeSeenByBystander; });

    QTimer::singleShot(0, [&]() {
        qInfo() << "================================================================";
        qInfo() << "model_view 第 4 讲 · 自定义委托（平台 =" << QGuiApplication::platformName()
                << ", Qt =" << QT_VERSION_STR << "）";
        qInfo() << "================================================================";
        view.setModel(&model);
        view.resize(760, 400);
        view.show();
    });

    // ── 证据①：paint / sizeHint / displayText / initStyleOption 的「一问一答」 ──
    QTimer::singleShot(120, [&]() {
        flushEvents();
        view.viewport()->repaint();                     // 同步画一轮，数得干净
        TaskTableModel::resetCounters();
        qInfo() << "【证据①】绘制链：视图把格子交给委托的方式";
        qInfo() << "  paint 被调" << delegate.paintTotal() << "次，其中进度列" 
                << delegate.paintProgressCells() << "格；"
                << "第一张进度格的 option.rect =" << delegate.firstProgressRect()
                << "（宽" << delegate.firstProgressRect().width() << "×高"
                << delegate.firstProgressRect().height() << "，**视图算好递进来的**）";
        qInfo() << "  initStyleOption" << delegate.initStyleOptionTotal()
                << "次 ｜displayText" << delegate.displayTextTotal()
                << "次（模型 DisplayRole 给的是裸 int，「42%」这三个字符是委托拼的——第 2 讲的债在这还）";
        qInfo() << "  sizeHint 被视图问了" << delegate.sizeHintTotal()
                << "次（竖表头开了 ResizeToContents 才会来问）｜rowHeight(0) =" << view.rowHeight(0)
                << "vs 默认 22+进度列加高 10 → 应该更大";
        qInfo() << "  进度条填充宽（最后一次 paint） =" << delegate.lastBarFilledWidth()
                << "px；对照 option.rect 宽 =" << delegate.firstProgressRect().width()
                << "（比例 = 行0进度10% → 约 0.1×(rect宽-74)）";
        qInfo() << "  时机口诀：**sizeHint 定行高（布局期）→ paint 画格子（重绘期）**，都在**视口的循环里**被调，不是你主动调。";

        // 下一步：程序化打开**进度格**编辑器（行 1 列 1）
        const QModelIndex cell = model.index(1, ColProgress);
        view.setCurrentIndex(cell);
        view.edit(cell);                                 // 219 行 public slot，免鼠标
    });

    // ── 证据②：编辑器诞生 —— createEditor / setEditorData / updateEditorGeometry ──
    QTimer::singleShot(160, [&]() {
        qInfo() << "【证据②】打开进度格（行1列1）编辑器：创建链";
        QWidget *editor = delegate.lastEditor();
        qInfo() << "  createEditor" << delegate.createEditorTotal() << "次 ｜setEditorData"
                << delegate.setEditorDataTotal() << "次 ｜updateEditorGeometry"
                << delegate.updateEditorGeomTotal() << "次";
        qInfo() << "  编辑器类型 =" << (qobject_cast<QSpinBox *>(editor) ? "QSpinBox" : "??")
                << "｜有 parent =" << delegate.editorHadParent()
                << "（parent 是视图给的 viewport——**不是你 new 了就归你 delete**）";
        qInfo() << "  编辑器几何 =" << delegate.lastEditorRect()
                << "｜visualRect(行1列1) =" << view.visualRect(model.index(1, ColProgress))
                << "（updateEditorGeometry 就是吃 option.rect 摆位置）";
        QSpinBox *spin = qobject_cast<QSpinBox *>(editor);
        qInfo() << "  setEditorData 灌进来的初值 value() =" << (spin ? spin->value() : -999)
                << "（= 模型 EditRole 的裸 int，**模型→编辑器**方向）";
        if (spin)
            spin->setValue(87);                          // 用户打字 → valueChanged → 委托发 commitData
    });

    // ── 证据③：commitData 信号 → 视图槽 → setModelData → setData → dataChanged 全回路 ──
    QTimer::singleShot(160, [&]() {
        qInfo() << "【证据③】提交回路（setValue(87) 之后）";
        qInfo() << "  委托发 commitData" << delegate.commitEmitted() << "次 ｜旁观方收到"
                << s_commitSeenByBystander << "次（信号→视图槽是 Qt 内部接的，第 240/241 行）";
        qInfo() << "  于是 setModelData 被调" << delegate.setModelDataTotal() << "次 ｜"
                << "model->setData 计数 =" << TaskTableModel::setDataCalls()
                << "｜dataChanged 发射 =" << TaskTableModel::dataChangedEmits();
        qInfo() << "  写回账本：\n     " << delegate.writeLog();
        qInfo() << "  复核：模型现在答 (1,1) EditRole ="
                << model.data(model.index(1, ColProgress), Qt::EditRole).toInt()
                << "｜进度条填充宽变成" << delegate.lastBarFilledWidth() << "px（dataChanged→标脏→重画）";
        qInfo() << "  → closeEditor/commitData 必须走**信号**的原因：视图的 editorState 状态机"
                   "靠这两个槽收尾；委托不发，视图就一直认为「还在编辑」，其它格子全点不动。";

        // 下一步：名字列（行5）开编辑器，改一半把行删掉——双守卫上场
        QModelIndex cell = model.index(5, ColName);
        view.setCurrentIndex(cell);
        view.edit(cell);
    });

    // ── 证据④：异步守卫。改名字 → 防抖未到期 → 删行 → 提交被跳过 ──
    QTimer::singleShot(140, [&]() {
        QLineEdit *le = qobject_cast<QLineEdit *>(delegate.lastEditor());
        qInfo() << "【证据④前奏】行5 名字编辑器 opened =" << (le != nullptr)
                << "，setText(\"改到一半\") 触发**防抖**（150ms 后才提交）";
        if (le)
            le->setText(QStringLiteral("改到一半"));
        const QModelIndex stale = model.index(5, ColName);   // 故意留一份**裸 index** 当对照组
        qInfo() << "  此刻删掉行 5（begin/endRemoveRows 走正规流程）…";
        model.removeRows(5, 1);
        view.close();                                      // 先收起窗口：close() 会销毁挂在 viewport 上的编辑器
        QTimer::singleShot(80, [&, stale]() {
            qInfo() << "  编辑器 QPointer 还活着吗 =" << !delegate.lastEditor().isNull()
                    << "｜destroyEditor 计数 =" << delegate.destroyEditorTotal();
            qInfo() << "  对照：裸 index(5,0).data() 现在给的是 ="
                    << stale.data(Qt::DisplayRole).toString()
                    << "← **它不报错，它只是指到了别的行上**（旧行5删了，行5现在是旧行6）";
        });
        QTimer::singleShot(300, [&]() {
            qInfo() << "【证据④】防抖提交到期时的双守卫";
            qInfo() << "  挂过号" << delegate.deferredArmed() << "次 ｜到期" 
                    << (delegate.deferredArmed()) << "次 ｜**被守卫拦下**"
                    << delegate.deferredSkipped() << "次 ｜真提交" << delegate.deferredCommitted() << "次";
            qInfo() << "  模型有没有被脏写？setModelData 总次数 = " << delegate.setModelDataTotal()
                    << "（应与证据③持平）｜删行后 setData/dataChanged 计数 ="
                    << TaskTableModel::setDataCalls() << "/" << TaskTableModel::dataChangedEmits()
                    << "（removeRows 不发 dataChanged，走的是 rowsRemoved 那条线）";
            // 下一幕：一切正常路径——行2 改名，150ms 后防抖到期，双守卫放行
            const QModelIndex cell = model.index(2, ColName);
            view.show();
            view.setCurrentIndex(cell);
            view.edit(cell);
        });
    });

    // ── 证据⑤：正路防抖提交 ──
    QTimer::singleShot(180, [&]() {
        QLineEdit *le = qobject_cast<QLineEdit *>(delegate.lastEditor());
        if (le)
            le->setText(QStringLiteral("变电站任务·改名成功"));
        qInfo() << "【证据⑤前奏】行2 名字编辑器 setText(\"…改名成功\")，等 150ms 防抖到期";
    });
    QTimer::singleShot(320, [&]() {
        qInfo() << "  守卫放行次数 deferredCommitted =" << delegate.deferredCommitted()
                << "｜commitData 信号累计 =" << delegate.commitEmitted()
                << "｜setModelData 累计 =" << delegate.setModelDataTotal();
        qInfo() << "  写回账本最新一条：\n     " << delegate.writeLog();
        qInfo() << "  模型现在答 (2,0) DisplayRole ="
                << model.data(model.index(2, ColName), Qt::DisplayRole).toString();
    });

    // ── 证据⑥：editorEvent（非控件交互）+ 不提交直接关 ──
    QTimer::singleShot(140, [&]() {
        qInfo() << "【证据⑥a】editorEvent：点一下进度格 +10（平时由视图调，这里为确定性直接调）";
        const QModelIndex cell = model.index(0, ColProgress);
        const int before = cell.data(Qt::EditRole).toInt();
        QStyleOptionViewItem opt;
        opt.initFrom(view.viewport());                   // qstyleoption.h 71 行：rect/state 之外先给个壳
        delegate.primeOption(&opt, cell);                // protected initStyleOption 的 public 包装
        QEvent click(QEvent::MouseButtonRelease);        // qcoreevent.h 305 行 explicit QEvent(Type)
        const bool handled = delegate.editorEvent(&click, &model, opt, cell);
        qInfo() << "  返回 =" << handled << "（true=事件被委托吃掉）｜行0 进度" << before << "→"
                << model.data(cell, Qt::DisplayRole).toInt()
                << "｜editorEvent 计数 =" << delegate.editorEventTotal()
                << "（签名注意：**非 const**，第 66~69 行）";
        qInfo() << "  helpEvent（第 73~76 行）同款入口只管 QHelpEvent（ToolTip/WhatsThis），演示无鼠标悬停，不演。";

        qInfo() << "【证据⑥b】不提交直接关：改行3 名字 → 防抖没到期就 closeEditor(NoHint)";
        const QModelIndex cell3 = model.index(3, ColName);
        view.setCurrentIndex(cell3);
        view.edit(cell3);
    });
    QTimer::singleShot(140, [&]() {
        QLineEdit *le = qobject_cast<QLineEdit *>(delegate.lastEditor());
        if (le) {
            le->setText(QStringLiteral("这句不该落进模型"));
            delegate.closeWithoutCommit(le);             // 只发 closeEditor（82 行信号），不发 commitData（81 行）
        }
        qInfo() << "  closeEditor 信号发出 ｜委托计数 closeEmitted =" << delegate.closeEmitted()
                << "｜旁观收到 =" << s_closeSeenByBystander;
    });
    QTimer::singleShot(260, [&]() {
        qInfo() << "【证据⑥结论】「提交(CommitData)」与「不提交直接关(NoCommit 语义)」的区别：";
        qInfo() << "  setModelData 总次数 =" << delegate.setModelDataTotal()
                << "（这次关闭**没**让它 +1）｜deferredSkipped 现在是"
                << delegate.deferredSkipped() << "（比证据④后多 1 = 防抖被 QPointer/hidden 守卫拦下）";
        qInfo() << "  行3 名字模型侧仍是 =" << model.data(model.index(3, ColName), Qt::DisplayRole).toString()
                << "（编辑器的改动随编辑器销毁一起没了——这就是 Esc 取消编辑的机制）";
        qInfo() << "  【注意】 流传的 QAbstractItemDelegate::CommitData{RevertBefore,Keep} 枚举在本机 Qt 6.12 公开头文件里**不存在**"
                   "（grep 0 命中）；真机制就是上面这两条信号路径 + EndEditHint 五个真值。";
    });

    // ── 总账 + 退出 ──
    QTimer::singleShot(160, [&]() {
        qInfo() << "【总账】各回调被调次数（paint 计到最后一轮重绘为止）";
        qInfo() << QString::asprintf("  paint=%d(sizeHint=%d createEditor=%d setEditorData=%d\n"
                                     "  updateEditorGeometry=%d setModelData=%d destroyEditor=%d\n"
                                     "  displayText=%d initStyleOption=%d editorEvent=%d)\n"
                                     "  信号: commitData=%d closeEditor=%d ｜旁观: %d/%d\n"
                                     "  防抖: 挂号=%d 真提交=%d 拦截=%d",
                                     delegate.paintTotal(), delegate.sizeHintTotal(),
                                     delegate.createEditorTotal(), delegate.setEditorDataTotal(),
                                     delegate.updateEditorGeomTotal(), delegate.setModelDataTotal(),
                                     delegate.destroyEditorTotal(), delegate.displayTextTotal(),
                                     delegate.initStyleOptionTotal(), delegate.editorEventTotal(),
                                     delegate.commitEmitted(), delegate.closeEmitted(),
                                     s_commitSeenByBystander, s_closeSeenByBystander,
                                     delegate.deferredArmed(), delegate.deferredCommitted(),
                                     delegate.deferredSkipped());
        qInfo() << "  一句话验收：改过并提交的 → 模型可见新值；只改没提交的 → 模型纹丝不动；删行的 → 双守卫拦截。";
        qInfo() << "【收尾】委托的分工背这张表：绘制期 paint/sizeHint，进入编辑才 createEditor→setEditorData，"
                   "提交期 commitData 信号→setModelData→setData→dataChanged，收尾期 closeEditor→destroyEditor；"
                   "非控件交互 editorEvent/helpEvent；格式化 displayText。方向感（灌值 vs 写回）别丢。";
        QCoreApplication::quit();
    });

    return app.exec();
}

// ============================================================================
// 五、速查表与纪律（背不住就回来查）
// ============================================================================
/*
 * 一、每个回调的「时机 + 参数从哪来」
 *   ┌──────────────────────┬───────────────────────┬──────────────────────────────┐
 *   │ 回调                 │ 什么时候             │ 参数谁给的                   │
 *   ├──────────────────────┼───────────────────────┼──────────────────────────────┤
 *   │ sizeHint             │ 布局/行高计算期，视图 │ option=视图拼的半成品，      │
 *   │                      │ 问了才问（表头模式！）│ index=坐标                   │
 *   │ paint                │ 每一格每次重绘        │ painter=视图的，option 带    │
 *   │                      │                       │ rect/state/palette/font      │
 *   │ initStyleOption      │ QStyledItemDelegate:: │ 你把 text/icon/checkState 灌 │
 *   │                      │ paint 内部（本讲手动  │ 进 option（protected 工具）  │
 *   │                      │ 调了一次做演示）      │                              │
 *   │ displayText          │ paint 内，DisplayRole │ 值+locale，拼成人看的字      │
 *   │                      │ 变字符串那一刻        │                              │
 *   │ createEditor         │ **进入编辑那一次**    │ parent=viewport；此后视图不  │
 *   │                      │（不是每帧！）         │ 再要新控件                   │
 *   │ setEditorData        │ createEditor 之后立即 │ 模型→编辑器（读 EditRole）   │
 *   │ updateEditorGeometry │ 编辑器出现/格子挪位   │ 编辑器 setGeometry(option    │
 *   │                      │ 时（滚动会再调）      │ .rect)                       │
 *   │ setModelData         │ 视图 commitData 槽里  │ 编辑器→模型 setData          │
 *   │ destroyEditor        │ 视图 closeEditor 槽里 │ 默认实现处置编辑器；你别 delete│
 *   │ editorEvent          │ 事件落在格子且不在编辑│ 带 model/option/index，**非  │
 *   │                      │ 态（勾选框/按钮式）   │ const**；返回 true=吃掉      │
 *   │ helpEvent            │ QHelpEvent（ToolTip） │ 可以 QToolTip::showText      │
 *   └──────────────────────┴───────────────────────┴──────────────────────────────┘
 * 二、所有权与两条信号（本讲全部数过）
 *   · createEditor 的 parent 是视图给的 viewport：编辑器是视图的子 widget，
 *     **closeEditor 信号 → 视图槽 → destroyEditor** 这条链负责销毁；
 *     你自己 delete = 视图手里悬垂（视图还留了 editorDestroyed 槽兜底，242 行）。
 *   · commitData(editor)：委托喊「提交」；视图槽里调 delegate->setModelData → model->setData。
 *   · closeEditor(editor, hint)：委托喊「关门」；hint=EndEditHint 五值之一
 *     （NoHint/EditNextItem/EditPreviousItem/SubmitModelCache/RevertModelCache）。
 *   · 两条信号都不发 = 视图卡在编辑态（其它格子无法再编辑）——**这就是「为什么必须走
 *     信号/基类实现」**：视图的状态机只认这两个槽，你绕开它就是「一改就卡」。
 *   · 覆写时的「交还基类」：不感兴趣的列 paint/editorEvent 一律回 QStyledItemDelegate::
 *     …，否则默认格子变成白板。
 * 三、paint 的三条纪律
 *   ① 用副本：QStyleOptionViewItem opt = option（拷贝构造在 448 行），改 opt.text/
 *      backgroundBrush/decorationPosition，**不改本体**（本体是 const 引用，改了也越界）。
 *   ② 状态从 option.state 来：State_Selected(0x8000)/State_HasFocus(0x100)/
 *      State_MouseOver(0x2000)，选中配色、焦点虚线都靠它；自己 new option = 状态全丢。
 *   ③ 格子底用 drawControl(CE_ItemViewItem, &opt, painter, opt.widget) 复用样式，
 *      你只画增量（本讲：进度条 + 百分比字）。
 * 四、版本坑清单（本讲实测过的头文件核对）
 *   · QStyleOptionViewItem 显示文本成员在 Qt 6.12 叫 **text**（443 行）；新文档的
 *     QStyleOptionDisplayItem/displayText 成员 6.12 查无此名。  name-ok:QStyleOptionDisplayItem 同上
 *   · QAbstractItemView 编辑触发枚举第 8 位真名 **EditKeyPressed**，没有 EditedClicked。
 *   · 「CommitData{RevertBefore,Keep}」不存在（grep 0 命中）。
 *   · itemDelegate(index) 已废弃（6.0），用 itemDelegateForIndex（208 行）。
 *   · QFontMetrics 用 horizontalAdvance（54 行），width() 已删。
 *   · QPersistentModelIndex 没有小写独立头文件，include <QPersistentModelIndex>。
 *
 * ——— 老师傅的话 ———
 *   徒弟，委托这层考的是**方向感和时机感**，不是 API 量。它的接口就十个，
 *   但这十个分属三种时空：布局期问 sizeHint，重绘期调 paint（每帧、每可见格），
 *   编辑期才 createEditor（一次）然后灌值、摆位、写回、收尾。
 *   把 setEditorData 和 setModelData 搞反，症状就是你听过最玄的「一编辑就丢值」——
 *   一个读 EditRole 往编辑器里灌，一个从编辑器里掏往 setData 里塞，箭头相反。
 *
 *   画那一半，我的规矩就一条：**状态是视图给的，别自作主张**。option 里的 state、
 *   palette、fontMetrics 是样式和 DPI 的全部真相；你在 paint 里手 new 一个 option、
 *   写死一个背景色，选中态、暗色主题、高分屏就全找你算账。要改，改副本。
 *
 *   还有两条保命符：编辑器生命周期别碰（parent 是谁给的就让谁收，delete 是视图的权力）；
 *   异步提交前查 QPersistentModelIndex::isValid() 和 QPointer——证据④里那个「改一半
 *   行没了」的场景，真实项目天天发生（后台刷新把表重排的那一刻，用户正在打字）。
 *   裸 index 不报错，它只是安静地指到别的行上，把你写错的东西落进别人家数据。
 *
 *   最后一层意思：这一讲的计数器和「不存在的 API」勘误不是抬杠，是让你养成
 *   **查头文件再下笔**的手感。CommitData 那种枚举，网上教程写得越顺溜越要查。
 *
 * ——— 小练习 ———
 *   1. 给负责人列做「下拉编辑」：flags() 里把 ColOwner 也加上 Qt::ItemIsEditable，
 *      createEditor 返回 QComboBox（addItems 四个人名），setModelData 写回选中文本；
 *      跑一遍看 createEditor 从 2 个变成 3 个，并解释为什么不用改视图一行代码。
 *   2. 把进度列的提交策略从「valueChanged 立即提交」改成「editingFinished 再提交」
 *      （对照 QSpinBox 没有 editingFinished，换成 QLineEdit+QIntValidator 做），
 *      观察证据③的 setModelData 次数与 commitData 次数的关系变化。
 *   3. 在 paint 里故意**不用副本**：直接 const_cast 改传入 option 的 text，再画
 *      相邻列——观察相邻列显示被污染的顺序 bug（这就是「副本纪律」的存在意义）。
 *   4. 把 sizeHint 的 +10 撤掉、竖表头改回 Fixed，比较 sizeHintTotal 计数变化；
 *      再用 data(行0, Qt::SizeHintRole) 返回 QSize(…, 40)（角色 13），搞清楚
 *      「模型的 SizeHintRole」「委托的 sizeHint()」「视图的默认行高」三者谁压过谁。
 */
// ============================================================================

// 本文件有 Q_OBJECT 类（TaskTableModel / ProgressDelegate），AUTOMOC 产出的
// moc 文件在这里被包含——它必须放在**所有类定义之后**。
#include "04_delegate.moc"
