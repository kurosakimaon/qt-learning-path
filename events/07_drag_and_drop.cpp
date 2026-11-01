// ============================================================================
// 第 7 讲 · 拖放（drag and drop）
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   拖放是事件系统里唯一「要跨进程说话」的一段，也是最容易写成「在我机器上好好的」的
//   一段。本讲把六件事讲清，并且**全程不用鼠标**：
//     1) QMimeData 是唯一的载体：setText/setUrls/setData + hasText/hasFormat/formats/data；
//     2) dragEnter/drop 的「表态」到底是哪几个字段：isAccepted 与 dropAction
//        **是两件事**，而 accept() 与 acceptProposedAction() 在一个刚造出来的事件上
//        **给出完全相同的结果**（本机实测，见演示③）——真正的杠杆是 setDropAction()；
//     3) 事件是怎么**沿父链重定向**的：没开 acceptDrops 的控件收不到，开了的祖先替它收；
//        Drop 事件更狠，它压根不听你 sendEvent 给谁，只发给 QDragManager 记下的那个目标；
//     4) dropEvent 里怎么把数据接住（含「自定义 MIME 类型」这一条内部拖放特权）；
//     5) 真正的拖动用 QDrag::exec()，启动阈值是
//        **mouseMoveEvent 里 manhattanLength() >= QApplication::startDragDistance()**；
//        本讲把这条阈值用合成鼠标事件**真跑了一遍**（见演示⑦，含 exec 的返回值）；
//     6) 内部拖放 vs 跨程序拖放的真实差别（序列化、可辨认的 MIME 名、光标/来源判定）。
//
// 对应构建目标名：events_07_drag_and_drop
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(events_07_drag_and_drop 07_drag_and_drop.cpp)
//   target_link_libraries(events_07_drag_and_drop PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)   # 本讲示例不需要 Q_OBJECT（拖放 handler 全是虚函数继承）
//
// 运行前须知：
//   · 无人值守的诀窍：**在进程内直接构造合成事件投递**——
//     QMimeData 摆在栈上，QDragEnterEvent / QDropEvent 用 qevent.h 里核对过的构造
//     手工做出来，再 QApplication::sendEvent() 投给目标控件，
//     于是「没有鼠标也能把数据接住」这件事变成可打印的证据。
//   · 真实拖动（QDrag::exec）在本机**能自动跑**，原因很实在：离屏平台把拖放实现
//     写成了一个桩——QOffscreenDrag::drag() 的函数体就一行
//     `return Qt::IgnoreAction;`（qoffscreencommon.h 58~61 行）。
//     所以 exec() 不阻塞、不挂起，立刻返回 0x0；阈值判定、mimeData 装配、
//     defaultAction 换算这些**我们自己的代码**全都真的跑到了，可以断言。
//     换到 windowsplatform（Windows 原生 OLE 拖放）就不是这个行为：
//     文档明确「On Windows, the Qt event loop is blocked during the operation」
//     （qdrag.cpp 223~229 行；Linux/macOS 那段说「不阻塞循环」在 199~203 行）。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 events_07_drag_and_drop）。
//     文件末尾有**汇总自检台账**：本机那一轮是 26 PASS / 0 FAIL / 0 SKIP，
//     任何一条 FAIL 都会把进程退出码变成非 0，ctest 直接变红。
//     本机那一轮的完整输出存档在 runlogs/events_07_drag_and_drop.log。
// ============================================================================

#include <QApplication>
#include <QCoreApplication>
#include <QWidget>
#include <QEvent>
#include <QMimeData>              // QtCore/qmimedata.h：数据载体在 **QtCore**，不在 QtGui
#include <QUrl>                   // QtCore/qurl.h
#include <QDrag>                  // QtGui/qdrag.h
#include <QDragEnterEvent>        // QtGui/qevent.h
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QPainter>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QList>
#include <QDebug>

// ============================================================================
// 一、核对签名（本讲全部依据）
// ============================================================================
// QtCore/qmimedata.h（第 16 行起，class QMimeData : public QObject）
//   20  QMimeData();                 21  ~QMimeData();
//   23  QList<QUrl> urls() const;    24  void setUrls(const QList<QUrl> &urls);  25  bool hasUrls() const;
//   27  QString text() const;        28  void setText(const QString &text);      29  bool hasText() const;
//   31  QString html() const;        32  void setHtml(const QString &html);      33  bool hasHtml() const;
//   35  QVariant imageData() const;  36  void setImageData(const QVariant &);    37  bool hasImage() const;
//   43  QByteArray data(const QString &mimetype) const;
//   44  void setData(const QString &mimetype, const QByteArray &data);
//   45  void removeFormat(const QString &mimetype);
//   47  virtual bool hasFormat(const QString &mimetype) const;
//   48  virtual QStringList formats() const;
//   50  void clear();
//   53  【protected】virtual QVariant retrieveData(const QString &mimetype,
//                                                  **QMetaType** preferredType) const;
//        ← Qt5 这个函数第二个参数是 QVariant::Type；Qt6 换成 QMetaType。重写它的老代码必改。
//        ← 另外 Q_DISABLE_COPY（第 56 行）：**不可拷贝**，只能传指针、谈所有权。
//
// QtGui/qevent.h —— QDropEvent 一家（第 710 行起）
//   710 class Q_GUI_EXPORT QDropEvent : public QEvent
//   714 QDropEvent(const QPointF &pos, Qt::DropActions actions, const QMimeData *data,
//                  Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers,
//                  Type type = Drop);
//   717~726 弃用块（QT_DEPRECATED_SINCE(6, 0)）：pos()/posF()/mouseButtons()/keyboardModifiers()
//   728 position() → QPointF        729 buttons()      730 modifiers()
//   732 possibleActions()  —— 拖动方说「我支持哪些动作」
//   733 proposedAction()   —— 「对方这一发默认想干什么」（就是 m_defaultAction）
//   734 acceptProposedAction()：**内联实现 { m_dropAction = m_defaultAction; accept(); }**
//   736 dropAction()       737 setDropAction(Qt::DropAction)
//   739 source()           740 mimeData()
//   742 protected: / 743 `friend class QApplication;`
//        ← 所以 **source() 只能由 Qt 自己填**，手工合成的事件里它是 **nullptr**，必须判空。
//   ★ 构造函数干了什么（qevent.cpp 3198~3207 行，这是本讲最重要的一段）：
//        m_defaultAction = m_dropAction =
//            platformIntegration()->drag()->defaultAction(m_actions, modifiers);
//        ignore();
//     ——也就是说 **m_dropAction 从造出来那一刻就已经是「提议的动作」了**，
//     构造函数只额外做了一件负面事：ignore()。
//     于是 accept() 与 acceptProposedAction() 的差别只剩「再写一次 m_dropAction = m_defaultAction」，
//     对一个没人 setDropAction() 过的新鲜事件而言，这一句是**空操作**。演示③ 把它打出来了。
//   754 class QDragMoveEvent : public QDropEvent
//        766 answerRect()（**不是** reply()，本讲旧稿写错了）、768 accept()/769 ignore()、
//        771 accept(const QRect &)/772 ignore(const QRect &) —— 多出来的只有「回一个矩形」的能力
//   779 class QDragEnterEvent : public QDragMoveEvent
//        788 QDragEnterEvent(QPointF pos, Qt::DropActions, const QMimeData*,
//                            Qt::MouseButtons, Qt::KeyboardModifiers)   ← 用这个
//        785 收 QPoint 的那位在 6.12 已标 QT_DEPRECATED_VERSION_X_6_16（783 行的 #if）
//          本机实测：直接传 QPoint 会出
//            warning: 'QDragEnterEvent::QDragEnterEvent(const QPoint&, ...)' is deprecated:
//            Use the constructor taking QPointF [-Wdeprecated-declarations]
//            note: declared here  → qevent.h:785
//          ★ 但别把这条警告当成「迁移保护」：把重载藏起来
//            （-DQT_DISABLE_DEPRECATED_BEFORE=0x061000）之后同一行代码**照样编得过、零警告**，
//            因为 qpoint.h 235 行 `constexpr QPointF(const QPoint &p)` 是**非 explicit** 的，
//            QPoint → QPointF 会静默隐式转换。「没警告」不等于「没问题」，这一条要记住。
//   793 class QDragLeaveEvent : public QEvent（无数据）
//   QtCore/qcoreevent.h：311 isAccepted()  313 accept()  314 ignore()
//        ← 注意 accept/ignore 动的只有 m_accept 这一个布尔位，**跟 dropAction 无关**。
//
// QtGui/qdrag.h（第 22 行起）
//   27  explicit QDrag(QObject *dragSource);     ← **Qt6 收 QObject\***；Qt5 是 QWidget*
//   30  void setMimeData(QMimeData *data);       ← 所有权交给 QDrag（qdrag.cpp 35 行文档原话）
//   31  QMimeData *mimeData() const;
//   33  void setPixmap(const QPixmap &);         36  void setHotSpot(const QPoint &);
//   39  QObject *source() const;                 40  QObject *target() const;
//   42  Qt::DropAction exec(Qt::DropActions supportedActions = Qt::MoveAction);
//   43  Qt::DropAction exec(Qt::DropActions supportedActions, Qt::DropAction defaultAction);
//   45  void setDragCursor(const QPixmap &, Qt::DropAction);
//   48  Qt::DropActions supportedActions() const; 49  Qt::DropAction defaultAction() const;
//   51  static void cancel();
//   54  Q_SIGNAL void actionChanged(Qt::DropAction);  55  Q_SIGNAL void targetChanged(QObject *);
//   exec() 的默认动作换算（qdrag.cpp 232~260 行，实测在演示⑦）：
//     · 没 setMimeData 就 exec → qWarning("QDrag: No mimedata set before starting the drag")，直接返回（235~238）
//     · 第二个参数给 IgnoreAction 时，按 **Move > Copy > Link** 的顺序挑一个当默认（241~248）
//     · 结尾 QPointer<QDrag> self = this；若平台实现把 drag 对象当场删了，exec 返回 IgnoreAction（254~258）
//   谁删 QDrag：QPlatformDrag::ownsDragObject() 基类返回 **false**（qplatformdrag.cpp 184~187），
//     于是 QDragManager::drag() 在 exec 返回前替你 `m_object->deleteLater()`（qdnd.cpp 83~84）。
//     → 起拖之后**别再 delete drag**；再补一次 deleteLater() 是安全的（qobject.cpp 2523~2534
//       的注释原话「De-bounce QDeferredDeleteEvents」，重复调用会去重）。
//
// 「提议的动作」是怎么算出来的（qplatformdrag.cpp 75~118 行，演示④ 逐条实测）：
//     起点：有 currentDrag() 就用 drag->defaultAction()，否则 IgnoreAction → 兜底成 **CopyAction**
//     然后按修饰键覆盖：Ctrl+Shift → Link；Ctrl → Copy；Shift → Move；Alt → Link
//     最后做合法性检查：不在 possibleActions 里就按 **Copy → Move → Link → IgnoreAction** 回退
//   这条链是纯函数（platformIntegration()->drag()->defaultAction(actions, modifiers)），
//   所以**合成事件也能把它打出来**，不需要真的拖一次。
//
// 谁决定光标长什么样（qsimpledrag.cpp 277~321 行 + 437 行，源码读出来、本机不可见）：
//     437: setCanDrop(qt_response.isAccepted());
//     285: Qt::CursorShape cursorShape = Qt::ForbiddenCursor;   ← 默认就是禁止
//     286: if (canDrop()) switch (action) { Copy→DragCopyCursor  Link→DragLinkCursor
//                                          default→DragMoveCursor }
//     而 qt_response 来自 QGuiApplicationPrivate::processDrag()（qguiapplication.cpp 3557~3602）：
//     3599~3600: lastAcceptedDropAction = me.isAccepted() ? me.dropAction() : Qt::IgnoreAction
//   → **「一直是禁止光标」的根因是 isAccepted 为假（或者动作不在 supportedActions 里），
//      跟 accept() / acceptProposedAction() 二选一没有关系。**旧稿那一句话是错的，本讲已改。
//   → 离屏平台没有可见光标，这段只能读源码，不作断言。
//
// 拖放事件在 widgets 层是怎么派发的（qapplication.cpp 2984~3048 行，演示②⑤ 把它跑成证据）：
//   case QEvent::DragEnter（2985~3010）：
//     2996~3008 从你 sendEvent 的那个控件开始**沿父链往上走**，
//       2997: `if (w->isEnabled() && w->acceptDrops())` 才真正调 notify_helper(w, e)
//       2999~3001: 一旦被接受 → QDragManager::self()->setCurrentTarget(w)
//       3006: `dragEvent->m_pos = w->mapToParent(dragEvent->m_pos)` ← **每上一层，坐标重写一次**
//   case QEvent::DragMove / Drop / DragLeave（3011~3048）：
//     3021: `w = qobject_cast<QWidget*>(QDragManager::self()->currentTarget())`
//       ← **你投递给谁没用**，它只发给「Enter 时被接受过的那个控件」
//     3023~3031: 没有 currentTarget 时，DragMove/Drop 直接 break（**事件凭空消失**），
//       只有 DragLeave 会退回 receiver（3028~3029）
//     3033~3040: 再次重写 m_pos，把「receiver 本地坐标」换算到真正目标上
//     3042~3047: 处理完 Drop 就把 currentTarget **清空** → 每一轮「进→落」都必须先有一次被接受的 Enter
//   本讲旧稿说「没开 acceptDrops 是『我的 dropEvent 怎么不进』的第一因」——这条**依然成立**，
//   但真正的原因不是「Qt 不给你事件」，而是上面这段父链重定向；
//   QWidget::event() 自己（qwidget.cpp 9176~9184）对 DragEnter/Drop 是**无条件**调 handler 的，
//   门槛全在 QApplication::notify 里。
//
// QtWidgets/qapplication.h
//   111 static int startDragTime();      113 static int startDragDistance();
//   （Qt6 里**查不到** autoStartDragDistance / autoStartDragTime 这两个 Qt5 名字）
//   本机实测：startDragDistance() = 10 px，startDragTime() = 500 ms
// QtCore/qnamespace.h
//   1500~1507 enum DropAction { CopyAction=0x1, MoveAction=0x2, LinkAction=0x4, ActionMask=0xff,
//                               TargetMoveAction=0x8002, IgnoreAction=0x0 }
//   1508 Q_DECLARE_FLAGS(DropActions, DropAction)
//   68   enum MouseButton（LeftButton=0x1…）；107 MouseButtons 为它的 flags
//   1077 enum KeyboardModifier（合成事件里要填 modifiers，演示④ 就靠它换算 proposedAction）
// QtWidgets/qwidget.h
//   557 acceptDrops() / 558 setAcceptDrops(bool)          336 setMouseTracking(bool)
//   689 dragEnterEvent(QDragEnterEvent*)   690 dragMoveEvent(QDragMoveEvent*)
//   691 dragLeaveEvent(QDragLeaveEvent*)   692 dropEvent(QDropEvent*)
//   acceptDrops 就是 Qt::WA_AcceptDrops 属性（qwidget.cpp 3449~3451 / 3456 行）

// ============================================================================
// 二、自检台账（本机跑不出来的结论一律 SKIP，跑出来的每一条都要断言）
// ============================================================================
struct Ledger { int pass = 0, fail = 0, skip = 0; };
static Ledger gLedger;

static void check(const char *what, bool ok, bool applicable = true)
{
    const QString line = QString::fromUtf8(what);
    if (!applicable) {
        ++gLedger.skip;
        qInfo().noquote() << QStringLiteral("  [SKIP] %1").arg(line);
        return;
    }
    if (ok) {
        ++gLedger.pass;
        qInfo().noquote() << QStringLiteral("  [PASS] %1").arg(line);
    } else {
        ++gLedger.fail;
        qInfo().noquote() << QStringLiteral("  [FAIL] %1").arg(line);
    }
}

static QString hex(int v)
{
    return QStringLiteral("0x") + QString::number(v, 16);
}

// ============================================================================
// 三、接住拖放的控件
// ============================================================================
class DropBox : public QWidget
{
public:
    // 表态方式：用来把「accept() / acceptProposedAction() / setDropAction()+accept()」
    // 三种写法在**同一条派发链**上的结果并排打出来
    enum class Stance { ProposedAction, PlainAccept, ForceMove };

    Stance stance = Stance::ProposedAction;
    int enterCount = 0;
    int moveCount = 0;
    int leaveCount = 0;
    int dropCount = 0;
    int rejectedCount = 0;
    QString lastText;
    QStringList lastFormats;
    QString lastSource = QStringLiteral("<无>");
    int lastDropAction = -1;
    QPointF lastEnterPos{ -1, -1 };
    QPointF lastMovePos{ -1, -1 };
    QPointF lastDropPos{ -1, -1 };
    QRect lastAnswerRect;

    explicit DropBox(const QString &name, QWidget *parent = nullptr, bool acceptDrops = true)
        : QWidget(parent)
    {
        setObjectName(name);
        setFixedSize(200, 110);
        setAcceptDrops(acceptDrops);           // qwidget.h 558 行 —— 门槛在 QApplication::notify 2997 行
    }

protected:
    // 第 1 步：进入。这里唯一的任务是「表态」：这次拖放我接不接受、按哪个动作接。
    void dragEnterEvent(QDragEnterEvent *e) override
    {
        ++enterCount;
        lastEnterPos = e->position();          // ← 注意：这是**派发链换算之后**的坐标，见 3006 行
        lastFormats = e->mimeData() ? e->mimeData()->formats() : QStringList();
        qInfo().noquote() << QStringLiteral("    %1 dragEnter：位置=(%2,%3) possibleActions=%4 "
                                            "proposedAction=%5 formats=%6")
                                 .arg(objectName())
                                 .arg(e->position().x()).arg(e->position().y())
                                 .arg(hex(int(e->possibleActions())))
                                 .arg(hex(int(e->proposedAction())))
                                 .arg(e->mimeData() ? e->mimeData()->formats().join(QStringLiteral(", "))
                                                    : QStringLiteral("<null>"));

        const bool canTake = e->mimeData() && (e->mimeData()->hasText()
                            || e->mimeData()->hasFormat(QStringLiteral("application/x-qt-learning-item")));
        if (!canTake) {
            ++rejectedCount;
            e->ignore();                       // qcoreevent.h 314 行：只动 m_accept
            qInfo().noquote() << QStringLiteral("      → 数据格式不认识，ignore()");
            return;
        }
        switch (stance) {
        case Stance::ProposedAction:
            e->acceptProposedAction();
            break;
        case Stance::PlainAccept:
            e->accept();                       // ← 只置 isAccepted，dropAction 用构造函数预置的值
            break;
        case Stance::ForceMove:
            e->setDropAction(Qt::MoveAction);  // qevent.h 737 行：真正能改动作的是这一位
            e->accept();
            break;
        }
        qInfo().noquote() << QStringLiteral("      → 表态完：isAccepted=%1 dropAction=%2")
                                 .arg(e->isAccepted()).arg(hex(int(e->dropAction())));
    }

    void dragMoveEvent(QDragMoveEvent *e) override
    {
        ++moveCount;
        lastMovePos = e->position();
        // 拖动过程中位置在变，可能要重新表态（比如从「可放区」移到「不可放区」）
        if (e->mimeData() && e->mimeData()->hasText()) {
            // QDragMoveEvent 比 QDropEvent 多的就是这一位：accept(矩形)/ignore(矩形)
            // （qevent.h 768~772 行；读回来用 answerRect()，766 行）。
            // 按演示③ 的结论，对新鲜事件 accept() 与 acceptProposedAction() 等价，
            // 这里选带矩形的重载，是为了把「放置位置」也回给平台（插入光标/高亮框）。
            e->accept(QRect(4, 4, 100, 20));
            lastAnswerRect = e->answerRect();
        } else {
            e->ignore();
            lastAnswerRect = QRect();
        }
        qInfo().noquote() << QStringLiteral("      %1 dragMove：位置=(%2,%3) isAccepted=%4 "
                                            "dropAction=%5 answerRect=(%6,%7,%8,%9)")
                                 .arg(objectName())
                                 .arg(e->position().x()).arg(e->position().y())
                                 .arg(e->isAccepted()).arg(hex(int(e->dropAction())))
                                 .arg(lastAnswerRect.x()).arg(lastAnswerRect.y())
                                 .arg(lastAnswerRect.width()).arg(lastAnswerRect.height());
    }

    void dragLeaveEvent(QDragLeaveEvent *) override
    {
        ++leaveCount;
        qInfo().noquote() << QStringLiteral("    %1 dragLeave（把高亮/插入线撤掉的地方）").arg(objectName());
    }

    // 第 2 步：真正取数据
    void dropEvent(QDropEvent *e) override
    {
        ++dropCount;
        lastDropPos = e->position();
        lastDropAction = int(e->dropAction());
        const QMimeData *md = e->mimeData();
        if (md && md->hasText())
            lastText = md->text();             // qmimedata.h 27 行

        if (md && md->hasFormat(QStringLiteral("application/x-qt-learning-item"))) {
            const QByteArray raw = md->data(QStringLiteral("application/x-qt-learning-item"));  // 43 行
            qInfo().noquote() << QStringLiteral("    %1 取到自定义 MIME 数据，长度=%2 内容=%3")
                                     .arg(objectName()).arg(raw.size()).arg(QString::fromUtf8(raw));
        }
        if (md && md->hasUrls()) {
            const QList<QUrl> us = md->urls();                 // 23 行
            QStringList strs;
            for (const QUrl &u : us)
                strs << (u.isLocalFile() ? u.toLocalFile() : u.toString());
            qInfo().noquote() << QStringLiteral("    %1 取到 %2 个 URL：%3")
                                     .arg(objectName()).arg(us.size()).arg(strs.join(QStringLiteral(", ")));
        }
        // source() 由 Qt 内部填（qevent.h 739 行 + 743 行 friend class QApplication），
        // 合成的事件里它是 nullptr —— 判空是**义务**，不是可选项。
        QObject *src = e->source();
        lastSource = src ? src->objectName()
                         : QStringLiteral("<nullptr：合成事件/跨程序拖动都可能是空>");
        qInfo().noquote() << QStringLiteral("    %1 dropEvent：text=「%2」 dropAction=%3 位置=(%4,%5) source=%6")
                                 .arg(objectName()).arg(lastText).arg(hex(lastDropAction))
                                 .arg(e->position().x()).arg(e->position().y()).arg(lastSource);

        e->acceptProposedAction();
        update();                              // 重绘请求，别在 drop 里直接画
    }

    // 顺手把「接住了什么」画出来，肉眼可核（绘制永远在 paintEvent 里，第 1 讲的规矩）
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setPen(Qt::black);
        p.drawRect(rect().adjusted(1, 1, -2, -2));
        const QString label = lastText.isEmpty()
                ? QStringLiteral("%1\n(acceptDrops=%2)").arg(objectName())
                        .arg(acceptDrops() ? QStringLiteral("开") : QStringLiteral("关"))
                : QStringLiteral("%1\n收到: %2").arg(objectName(), lastText);
        p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, label);
    }
};

// ============================================================================
// 四、发起拖动的控件（本讲的阈值演示**真的会走到 exec()**）
// ============================================================================
class DragSource : public QWidget
{
public:
    Qt::DropAction lastExecResult = Qt::IgnoreAction;
    int lastExecDefaultAction = -1;      // exec() 之后回读 drag->defaultAction()
    int lastExecSupportedActions = -1;
    int dragStarted = 0;

    explicit DragSource(const QString &name, QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName(name);
        setFixedSize(200, 110);
    }

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            m_pressPos = e->position().toPoint();
            m_dragFired = false;             // 一次按下只允许起一次拖
        }
        QWidget::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!(e->buttons() & Qt::LeftButton))
            return;                          // 没有按键按着：这是「悬停移动」
        if (m_dragFired)
            return;
        // ★ 拖动阈值：Qt6 用 QPoint::manhattanLength()，阈值来自 QApplication::startDragDistance()
        //   陷阱：别用「m_pressPos 是不是 QPoint()」当「按下过没有」的标志——QPoint() 就是 (0,0)，
        //         那是一个**合法**的按下位置。要另开一个 bool。
        const QPoint delta = e->position().toPoint() - m_pressPos;
        if (delta.manhattanLength() < QApplication::startDragDistance()) {
            qInfo().noquote() << QStringLiteral("    %1 移动 manhattanLength=%2 < 阈值 %3 → 不起拖")
                                     .arg(objectName()).arg(delta.manhattanLength())
                                     .arg(QApplication::startDragDistance());
            return;
        }
        m_dragFired = true;
        ++dragStarted;
        startDrag(delta.manhattanLength());
    }

private:
    QPoint m_pressPos;
    bool m_dragFired = false;

    void startDrag(int moved)
    {
        auto *md = new QMimeData;                       // 所有权交给 QDrag（qdrag.h 30 行）
        md->setText(QStringLiteral("从 %1 拖出来的文本").arg(objectName()));
        md->setData(QStringLiteral("application/x-qt-learning-item"),
                    QByteArrayLiteral("payload-from-DragSource"));
        md->setUrls({QUrl::fromLocalFile(QStringLiteral("C:/Temp/示例文件.txt")),
                     QUrl(QStringLiteral("https://doc.qt.io/qt-6/drag-and-drop.html"))});

        QPixmap pm(80, 40);
        pm.fill(Qt::darkGreen);
        {
            QPainter p(&pm);
            p.setPen(Qt::white);
            p.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("拖动我"));
        }

        auto *drag = new QDrag(this);                   // **Qt6：QDrag(QObject*)**
        drag->setMimeData(md);
        drag->setPixmap(pm);
        drag->setHotSpot(QPoint(40, 20));               // 指针在 pixmap 上的落点

        const Qt::DropActions supported = Qt::CopyAction | Qt::MoveAction;
        // 第二个参数给 IgnoreAction → 走 qdrag.cpp 241~248 的「Move > Copy > Link」换算
        lastExecResult = drag->exec(supported, Qt::IgnoreAction);
        lastExecDefaultAction = int(drag->defaultAction());
        lastExecSupportedActions = int(drag->supportedActions());
        qInfo().noquote() << QStringLiteral("    %1 起拖：移动 %2 px，exec() 返回动作=%3，"
                                            "exec 之后 defaultAction=%4 supportedActions=%5")
                                 .arg(objectName()).arg(moved).arg(hex(int(lastExecResult)))
                                 .arg(hex(lastExecDefaultAction)).arg(hex(lastExecSupportedActions));
        // ⚠ 不要 delete drag：exec 里 Qt 已经替你 deleteLater（qdnd.cpp 83~84）。
        //   这里再补一次是安全的（deleteLater 自带去重，qobject.cpp 2523~2534）。
        drag->deleteLater();
    }
};

// ============================================================================
// 五、合成一次「进入 + 落下」
// ============================================================================
static void synthDrop(QWidget *target, const QPointF &where, QMimeData *md,
                      Qt::DropActions possible = Qt::CopyAction | Qt::MoveAction)
{
    // 第 1 击：DragEnter（核对 qevent.h 788 行；第一个参数是 **QPointF**）
    QDragEnterEvent enter(where, possible, md, Qt::LeftButton, Qt::NoModifier);
    qInfo().noquote() << QStringLiteral("  投递 QDragEnterEvent（位置=(%1,%2)）→")
                             .arg(where.x()).arg(where.y());
    QCoreApplication::sendEvent(target, &enter);       // 同步：返回时整条链已跑完
    qInfo().noquote() << QStringLiteral("    enter 之后 isAccepted=%1 dropAction=%2")
                             .arg(enter.isAccepted()).arg(hex(int(enter.dropAction())));

    // 第 1.5 击：DragMove（真实拖动一定会先经过它；qevent.h 763 行的 QPointF 构造）
    QDragMoveEvent move(where, possible, md, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(target, &move);
    qInfo().noquote() << QStringLiteral("    move 之后 isAccepted=%1 dropAction=%2")
                             .arg(move.isAccepted()).arg(hex(int(move.dropAction())));

    // 第 2 击：Drop（核对 qevent.h 714 行：
    //   QDropEvent(const QPointF&, Qt::DropActions, const QMimeData*,
    //              Qt::MouseButtons, Qt::KeyboardModifiers, Type type = Drop)）
    //   ⚠ 没有「只带 buttons/modifiers 的 QPoint 版」：QDropEvent 只有 QPointF 这一位，
    //     传 QPoint 会**静默隐式转换**（qpoint.h 235 行），编得过不代表写法对。
    QDropEvent drop(where, possible, md, Qt::LeftButton, Qt::NoModifier, QEvent::Drop);
    qInfo().noquote() << QStringLiteral("  投递 QDropEvent（位置=(%1,%2)，"
                                        "构造后未表态：isAccepted=%3 dropAction=%4）→")
                             .arg(where.x()).arg(where.y())
                             .arg(drop.isAccepted()).arg(hex(int(drop.dropAction())));
    QCoreApplication::sendEvent(target, &drop);
    qInfo().noquote() << QStringLiteral("    drop 之后 isAccepted=%1 dropAction=%2")
                             .arg(drop.isAccepted()).arg(hex(int(drop.dropAction())));
}

// 只投一颗鼠标事件给控件（演示⑦ 的阈值实验用）
static void synthMouse(QWidget *w, QEvent::Type type, const QPointF &local,
                       Qt::MouseButton changed, Qt::MouseButtons held)
{
    const QPoint localInt(qRound(local.x()), qRound(local.y()));
    const QPointF global(w->mapToGlobal(localInt));
    QMouseEvent ev(type, local, global, changed, held, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &ev);
}

// ============================================================================
// 六、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // ★ 所有控件必须**挂在同一个父窗口下**：本讲的演示②⑤ 全靠「沿父链重定向」这条规则，
    //   控件各自成为顶层窗口的话，2997 行那个循环第一圈就 `if (w->isWindow()) break` 了。
    QWidget window;
    window.setObjectName(QStringLiteral("主窗"));
    window.setWindowTitle(QStringLiteral("第 7 讲 · 拖放"));
    window.resize(660, 320);

    auto *box = new DropBox(QStringLiteral("接收盒"), &window);
    box->move(20, 20);
    auto *boxNoAccept = new DropBox(QStringLiteral("没开acceptDrops"), &window, false);
    boxNoAccept->move(240, 20);
    auto *dropSite = new DropBox(QStringLiteral("父·投放点"), &window);   // 开 acceptDrops
    dropSite->move(460, 20);
    auto *childNoFlag = new DropBox(QStringLiteral("子·没开关"), dropSite, false);
    childNoFlag->setFixedSize(80, 40);
    childNoFlag->move(10, 10);
    auto *boxOrphan = new DropBox(QStringLiteral("只等Drop的盒"), &window);
    boxOrphan->move(20, 150);
    auto *source = new DragSource(QStringLiteral("拖动源"), &window);
    source->move(240, 150);
    window.show();

    // ── 事实① 阈值与系统的建议值
    qInfo().noquote() << QStringLiteral(
        "事实① 拖动阈值：QApplication::startDragDistance()=%1 px，startDragTime()=%2 ms"
        "（qapplication.h 111/113 行）→ 这两个值来自系统/样式，**别在代码里写死 10 像素**，用 API 取。")
        .arg(QApplication::startDragDistance()).arg(QApplication::startDragTime());
    check("事实① startDragDistance() 是正数（阈值为 0 就成了「按下即起拖」，鼠标抖一下东西就飞出去）",
          QApplication::startDragDistance() > 0);

    // ── 事实② QMimeData 能力自检（数据格式列表是拖放双方的「合同」）
    QMimeData probe;
    probe.setText(QStringLiteral("hello"));
    probe.setHtml(QStringLiteral("<b>hello</b>"));
    probe.setData(QStringLiteral("application/x-qt-learning-item"), QByteArrayLiteral("k=1"));
    probe.setUrls({QUrl::fromLocalFile(QStringLiteral("C:/Temp/a.txt"))});
    qInfo().noquote() << QStringLiteral(
        "事实② 一个 QMimeData 可以同时装多种格式：formats()=[%1] hasText=%2 hasUrls=%3 hasHtml=%4 "
        "hasFormat(自定义)=%5 自定义 data 长度=%6")
        .arg(probe.formats().join(QStringLiteral(", ")))
        .arg(probe.hasText()).arg(probe.hasUrls()).arg(probe.hasHtml())
        .arg(probe.hasFormat(QStringLiteral("application/x-qt-learning-item")))
        .arg(probe.data(QStringLiteral("application/x-qt-learning-item")).size());
    // 注意：setText 之后 formats() 里出现的是 text/plain（以及 Qt 顺手给的
    // text/html、text/uri-list 等衍生格式）；**具体列表与顺序由 Qt 决定，本讲不背它**，
    // 让程序自己打印才是可靠的做法。
    check("事实② 同一份数据同时以 4 种格式对外（自定义 MIME 不会挤掉标准格式）",
          probe.formats().size() == 4 && probe.hasText() && probe.hasUrls()
          && probe.hasHtml() && probe.hasFormat(QStringLiteral("application/x-qt-learning-item")));

    // ── 演示① 合成拖放：数据被接住（这是本讲「无人值守」的核心）
    QTimer::singleShot(400, [&] {
        qInfo() << "";
        qInfo().noquote() << QStringLiteral("=== 演示① 进程内合成一次拖放（没有鼠标）===");
        QMimeData md;
        md.setText(QStringLiteral("第 7 讲的载荷"));
        md.setData(QStringLiteral("application/x-qt-learning-item"),
                   QByteArrayLiteral("内部拖放特权：随意的 MIME 名与任意字节"));
        md.setUrls({QUrl::fromLocalFile(QStringLiteral("C:/Temp/示例文件.txt")),
                    QUrl(QStringLiteral("https://doc.qt.io"))});

        const int beforeDrop = box->dropCount;
        const int beforeMove = box->moveCount;
        synthDrop(box, QPointF(30, 30), &md);
        qInfo().noquote() << QStringLiteral("演示① 结果：接收盒 enter 累计=%1、本轮 move 增量=%2、"
                                            "本轮 drop 增量=%3；接到文本=「%4」 来源=%5")
                                 .arg(box->enterCount).arg(box->moveCount - beforeMove)
                                 .arg(box->dropCount - beforeDrop)
                                 .arg(box->lastText).arg(box->lastSource);
        // ⚠ 生命周期：sendEvent 不接管事件、也不接管 mimeData（QDropEvent 只存指针 m_data）。
        //    md 在这里是**栈对象**，必须活得比 sendEvent 这次调用久——上面正是这样写的。
        //    若换成 postEvent，就必须把 md 也放到堆上并自己安排好删除时机。
        check("演示① 一轮 enter+move+drop 之后接收盒真的收到 1 次 drop，文本与 dropAction 都对",
              box->dropCount - beforeDrop == 1 && box->lastText == QStringLiteral("第 7 讲的载荷")
              && box->lastDropAction == int(Qt::CopyAction));
        check("演示① dragMove 也进了（Enter 之后 currentTarget 已登记，Move 才会被改投给它），"
              "并且 answerRect 回读得到 (4,4,100,20)",
              box->moveCount - beforeMove == 1
              && box->lastAnswerRect == QRect(4, 4, 100, 20));
        check("演示① source() 是 nullptr（qevent.h 743 行 friend class QApplication：合成事件没人填它）",
              box->lastSource.startsWith(QStringLiteral("<nullptr")));
        check("演示① 投递给 box 的坐标 (30,30) 原样到达（box 自己就开了 acceptDrops，不用上溯父链）",
              box->lastEnterPos == QPointF(30, 30) && box->lastDropPos == QPointF(30, 30));
    });

    // ── 演示② 门槛到底在哪：acceptDrops + 父链重定向
    QTimer::singleShot(800, [&] {
        qInfo() << "";
        qInfo().noquote() << QStringLiteral("=== 演示② 没开关的控件 vs 开了开关的祖先 ===");
        QMimeData md;
        md.setText(QStringLiteral("投给没开开关的盒子"));
        const int beforeNoFlag = boxNoAccept->enterCount;
        synthDrop(boxNoAccept, QPointF(30, 30), &md);
        qInfo().noquote() << QStringLiteral("演示②a 结果：「没开acceptDrops」的 dragEnter 增量=%1、"
                                           "drop 累计=%2（父窗口也没开 → 一路上溯没人接，事件蒸发）")
                                 .arg(boxNoAccept->enterCount - beforeNoFlag).arg(boxNoAccept->dropCount);
        check("演示②a 自己和父窗口都没开 acceptDrops → 处理器一次都不进（qapplication.cpp 2997 行的门槛）",
              boxNoAccept->enterCount - beforeNoFlag == 0 && boxNoAccept->dropCount == 0);

        // 换一个「父开子关」的对照组：子控件没开关，事件却会**被父接走**，坐标还会被重写
        QMimeData md2;
        md2.setText(QStringLiteral("投给子控件，父来接手"));
        const int parentBefore = dropSite->enterCount;
        QDragEnterEvent enter(QPointF(10, 10), Qt::CopyAction | Qt::MoveAction, &md2,
                              Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(childNoFlag, &enter);   // ← 投给**没开关的子控件**
        qInfo().noquote() << QStringLiteral("演示②b 结果：投给「子·没开关」，"
                                            "「父·投放点」的 dragEnter 增量=%1，父看到的位置=(%2,%3)"
                                            "（我投的是子局部 (10,10)）")
                                 .arg(dropSite->enterCount - parentBefore)
                                 .arg(dropSite->lastEnterPos.x()).arg(dropSite->lastEnterPos.y());
        qInfo().noquote() << QStringLiteral("    → 规则（qapplication.cpp 2996~3008 行）：DragEnter 从接收者"
                                            "开始**沿父链往上走**，第一个 isEnabled() && acceptDrops() 的祖先接手；"
                                            "每上一层 m_pos 就 mapToParent 一次（3006 行），"
                                            "所以 handler 里读到的 position() 是**接手者**的局部坐标。");
        check("演示②b 子控件没开关，事件被开了开关的父控件接走（不是「吞掉」，是「上移一级」）",
              dropSite->enterCount - parentBefore == 1 && childNoFlag->enterCount == 0);
        check("演示②b 交接手时坐标被重算：子局部 (10,10) 变成父局部 (20,20)（子本身在父的 (10,10)）",
              dropSite->lastEnterPos == QPointF(20, 20));
        qInfo().noquote() << QStringLiteral("    【注意】 顺手记一笔**状态**：这发 Enter 被父控件接住了，"
                                            "于是 QDragManager 把 currentTarget **登记成了父·投放点**"
                                            "（2999~3001 行），而我这一轮没有投 Drop 去清空它。"
                                            "演示⑤ 一开头就要先处理这个**残留目标**——"
                                            "真实程序里「上一次拖到 A 没松手／A 只接了 Enter」之后"
                                            "的下一发 Drop 落进 A，就是这个残留。");
    });

    // ── 演示③ accept() / acceptProposedAction() / setDropAction() 的真值表
    QTimer::singleShot(1200, [&] {
        qInfo() << "";
        qInfo().noquote() << QStringLiteral("=== 演示③ 表态三写法的真值表（别再背错公式）===");
        QMimeData md;
        md.setText(QStringLiteral("表态实验"));
        const Qt::DropActions possible = Qt::CopyAction | Qt::MoveAction;

        auto fresh = [&] {
            return new QDragEnterEvent(QPointF(20, 20), possible, &md, Qt::LeftButton, Qt::NoModifier);
        };
        QScopedPointer<QDragEnterEvent> a(fresh());   // 一行一个写法，避免相互污染
        QScopedPointer<QDragEnterEvent> b(fresh());
        QScopedPointer<QDragEnterEvent> c(fresh());
        QScopedPointer<QDragEnterEvent> d(fresh());
        QScopedPointer<QDragEnterEvent> e(fresh());
        QScopedPointer<QDragEnterEvent> f(fresh());

        // A：完全不表态
        b->accept();                                            // B：只 accept()
        c->acceptProposedAction();                              // C：只 acceptProposedAction()
        d->setDropAction(Qt::MoveAction); d->accept();          // D：换动作 + accept
        e->setDropAction(Qt::MoveAction); e->acceptProposedAction();  // E：看它会不会覆盖
        f->setDropAction(Qt::MoveAction);                       // F：换动作但不表态

        struct Row { const char *name; QDropEvent *ev; };
        const QList<Row> rows = {
            { "A 造出来没表态          ", a.data() },
            { "B 只 accept()           ", b.data() },
            { "C acceptProposedAction()", c.data() },
            { "D setDropAction+accept  ", d.data() },
            { "E setDropAction+Proposed", e.data() },
            { "F 只 setDropAction      ", f.data() },
        };
        for (const Row &r : rows) {
            qInfo().noquote() << QStringLiteral("  %1 isAccepted=%2 dropAction=%3 proposedAction=%4")
                                     .arg(QString::fromUtf8(r.name))
                                     .arg(r.ev->isAccepted()).arg(hex(int(r.ev->dropAction())))
                                     .arg(hex(int(r.ev->proposedAction())));
        }
        qInfo().noquote() << QStringLiteral("  读法：构造函数（qevent.cpp 3198~3207）已经把 "
                                            "m_dropAction 和 m_defaultAction **同时**设成默认动作，"
                                            "只多干了一件坏事：ignore()。"
                                            "所以 B 与 C 一模一样（qevent.h 734 行那句 "
                                            "m_dropAction = m_defaultAction 在这里是空操作），"
                                            "真正能换动作的是 D 那一行的 setDropAction()；"
                                            "E 行说明 acceptProposedAction() 会**覆盖**你刚 set 的动作。");
        check("演示③ A：新鲜事件的 dropAction 已经是 0x1（构造时预置），isAccepted 是 false",
              !a->isAccepted() && int(a->dropAction()) == int(Qt::CopyAction));
        check("演示③ B 与 C 完全等价：isAccepted 与 dropAction 两个字段一模一样",
              b->isAccepted() == c->isAccepted() && b->dropAction() == c->dropAction());
        check("演示③ D：setDropAction(Move)+accept() 才真的把动作换成 0x2",
              d->isAccepted() && int(d->dropAction()) == int(Qt::MoveAction));
        check("演示③ E：acceptProposedAction() 把刚 set 的 Move 又覆盖回默认的 0x1",
              e->isAccepted() && int(e->dropAction()) == int(Qt::CopyAction));
        check("演示③ F：只 setDropAction 不 accept → 动作改了但 isAccepted 仍为 false（路由看的是后者）",
              !f->isAccepted() && int(f->dropAction()) == int(Qt::MoveAction));
    });

    // ── 演示④ proposedAction() 是怎么算出来的：修饰键换算表 + 回退顺序
    QTimer::singleShot(1600, [&] {
        qInfo() << "";
        qInfo().noquote() << QStringLiteral("=== 演示④ 修饰键 → proposedAction（qplatformdrag.cpp 75~118）===");
        QMimeData md;
        md.setText(QStringLiteral("换算表"));
        const Qt::DropActions all = Qt::CopyAction | Qt::MoveAction | Qt::LinkAction;

        struct Case { const char *name; Qt::KeyboardModifiers mods; Qt::DropActions possible; };
        const QList<Case> cases = {
            { "无修饰键 (Copy|Move|Link)      ", Qt::NoModifier,      all },
            { "Ctrl   (Copy|Move|Link)        ", Qt::ControlModifier, all },
            { "Shift  (Copy|Move|Link)        ", Qt::ShiftModifier,   all },
            { "Ctrl+Shift (Copy|Move|Link)    ", Qt::ControlModifier | Qt::ShiftModifier, all },
            { "Alt    (Copy|Move|Link)        ", Qt::AltModifier,     all },
            { "Ctrl   (只有 Move，Copy 不合法) ", Qt::ControlModifier, Qt::MoveAction },
        };
        QList<int> got;
        for (const Case &c : cases) {
            QDropEvent ev(QPointF(0, 0), c.possible, &md, Qt::LeftButton, c.mods, QEvent::Drop);
            got.append(int(ev.proposedAction()));
            qInfo().noquote() << QStringLiteral("  %1 possibleActions=%2 → proposedAction=%3 "
                                                "（dropAction 初值同 %3）")
                                     .arg(QString::fromUtf8(c.name))
                                     .arg(hex(int(c.possible))).arg(hex(int(ev.proposedAction())));
        }
        qInfo().noquote() << QStringLiteral("  读法：起点是 drag->defaultAction()（此刻没有真 drag → "
                                            "IgnoreAction → 兜底 CopyAction），再按修饰键覆盖 "
                                            "Ctrl→Copy / Shift→Move / Ctrl+Shift→Link / Alt→Link；"
                                            "最后一步「不在 possibleActions 里就按 Copy→Move→Link→Ignore 回退」"
                                            "就是最后一行从 Copy 变成 Move 的原因。");
        check("演示④ 无修饰键与 Ctrl 都提议 Copy(0x1)；Shift 提议 Move(0x2)",
              got.value(0) == int(Qt::CopyAction) && got.value(1) == int(Qt::CopyAction)
              && got.value(2) == int(Qt::MoveAction));
        check("演示④ Ctrl+Shift 与 Alt 都提议 Link(0x4)（前提是 possibleActions 里有 Link）",
              got.value(3) == int(Qt::LinkAction) && got.value(4) == int(Qt::LinkAction));
        check("演示④ possibleActions 只有 Move 时，Ctrl 想要的 Copy 被回退成 Move(0x2)（107~116 行的回退链）",
              got.value(5) == int(Qt::MoveAction));
    });

    // ── 演示⑤ Drop 不听你的：currentTarget 重定向 + 落一次就清空
    QTimer::singleShot(2000, [&] {
        qInfo() << "";
        qInfo().noquote() << QStringLiteral("=== 演示⑤ Drop 发给谁，不由你说了算 ===");
        qInfo().noquote() << QStringLiteral("  先把坐标换算的底数打出来：主窗 mapToGlobal(0,0)=(%1,%2)"
                                            "（离屏平台给顶层窗口摆的位置），"
                                            "孤儿盒在窗内的 pos=(%3,%4)。")
                                 .arg(window.mapToGlobal(QPoint(0, 0)).x())
                                 .arg(window.mapToGlobal(QPoint(0, 0)).y())
                                 .arg(boxOrphan->pos().x()).arg(boxOrphan->pos().y());
        QMimeData md;
        md.setText(QStringLiteral("孤儿 Drop"));

        // ① 演示②b 留下的**残留目标**：那发 Enter 被父控件接走，却没人投 Drop 去清空它
        const int orphanBefore = boxOrphan->dropCount;
        const int parentBefore = dropSite->dropCount;
        QDropEvent orphan(QPointF(5, 5), Qt::CopyAction | Qt::MoveAction, &md,
                          Qt::LeftButton, Qt::NoModifier, QEvent::Drop);
        QCoreApplication::sendEvent(boxOrphan, &orphan);
        qInfo().noquote() << QStringLiteral("  ① 【残留目标】只投 Drop（投给孤儿盒）："
                                            "孤儿盒增量=%1、父·投放点增量=%2、isAccepted=%3")
                                 .arg(boxOrphan->dropCount - orphanBefore)
                                 .arg(dropSite->dropCount - parentBefore).arg(orphan.isAccepted());
        qInfo().noquote() << QStringLiteral("    → qapplication.cpp 3021 行把 w 换成 "
                                            "QDragManager::self()->currentTarget()，"
                                            "而 currentTarget 还是演示②b 里那个**上一轮的接手者**。"
                                            "所以这一发 Drop 落进了父·投放点：孤儿盒一次没进。"
                                            "真实程序里「上一发拖到 A 结果 A 只回了 Enter／没松手」"
                                            "之后的下一发 Drop 就是这么凭空进错地方的。");
        check("演示⑤① 残留的 currentTarget 会**跨轮留任**：Drop 投给孤儿盒，却落进上一轮接手 Enter 的父控件",
              boxOrphan->dropCount - orphanBefore == 0 && dropSite->dropCount - parentBefore == 1
              && orphan.isAccepted());

        // ② 清场：完整一轮 Enter+Drop，把 currentTarget 消耗掉（3042~3047 落完就清空）
        QDragEnterEvent cleanEnter(QPointF(8, 8), Qt::CopyAction | Qt::MoveAction, &md,
                                   Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(box, &cleanEnter);
        QDropEvent cleanDrop(QPointF(8, 8), Qt::CopyAction | Qt::MoveAction, &md,
                             Qt::LeftButton, Qt::NoModifier, QEvent::Drop);
        QCoreApplication::sendEvent(box, &cleanDrop);
        qInfo().noquote() << QStringLiteral("  ② 【清场】给接收盒投完整一轮 enter+drop："
                                            "接收盒 drop 累计=%1（Drop 处理完就把 currentTarget 清空）")
                                 .arg(box->dropCount);

        // ③ 现在真的是「没有目标」了：再投一发孤儿 Drop，看它蒸发
        const int orphanBefore2 = boxOrphan->dropCount;
        QDropEvent orphan2(QPointF(5, 5), Qt::CopyAction | Qt::MoveAction, &md,
                           Qt::LeftButton, Qt::NoModifier, QEvent::Drop);
        QCoreApplication::sendEvent(boxOrphan, &orphan2);
        qInfo().noquote() << QStringLiteral("  ③ 【真空目标】再投一发孤儿 Drop：handler 增量=%1、"
                                            "isAccepted=%2（连控件都没到达）")
                                 .arg(boxOrphan->dropCount - orphanBefore2).arg(orphan2.isAccepted());
        qInfo().noquote() << QStringLiteral("    → 没有 currentTarget 时 3023~3031 行直接 break，"
                                            "DragMove/Drop 就这样被丢掉；只有 DragLeave 有退回 receiver 的待遇"
                                            "（3028~3029 行）。这就是「断点在 dropEvent 里却不进」的一种成因。");
        check("演示⑤③ 清场之后再没有登记的 Enter，孤儿 Drop 事件**凭空蒸发**（handler 不进、isAccepted 保持假）",
              boxOrphan->dropCount - orphanBefore2 == 0 && !orphan2.isAccepted());

        // ④ 同轮改投 + 坐标重写：Enter 给 box，Drop 投给**别的**控件
        QDragEnterEvent enter(QPointF(12, 12), Qt::CopyAction | Qt::MoveAction, &md,
                              Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(box, &enter);
        const int boxBefore = box->dropCount;
        const int orphanBefore3 = boxOrphan->dropCount;
        QDropEvent drop(QPointF(5, 5), Qt::CopyAction | Qt::MoveAction, &md,
                        Qt::LeftButton, Qt::NoModifier, QEvent::Drop);
        QCoreApplication::sendEvent(boxOrphan, &drop);
        qInfo().noquote() << QStringLiteral("  ④ 【同轮改投】Enter 投给接收盒并接住，Drop 投给孤儿盒："
                                            "接收盒增量=%1（它看到的位置=(%2,%3)），孤儿盒增量=%4")
                                 .arg(box->dropCount - boxBefore)
                                 .arg(box->lastDropPos.x()).arg(box->lastDropPos.y())
                                 .arg(boxOrphan->dropCount - orphanBefore3);
        qInfo().noquote() << QStringLiteral("    → Drop 被**改投**给 currentTarget（接收盒），"
                                            "而且 m_pos 被 3036~3039 行的循环换算过：我投的是孤儿盒局部 (5,5)，"
                                            "接收盒读到的是 (%1,%2) = 孤儿盒窗口坐标 (25,155) 再加上"
                                            "主窗的全局偏移。处理完这发 Drop，3042~3047 行又把 "
                                            "currentTarget 清空 → **每一轮「进→落」都必须重新过一次 Enter**。")
                                 .arg(box->lastDropPos.x()).arg(box->lastDropPos.y());
        check("演示⑤④ Drop 落到 Enter 时被登记的那个控件（不是你 sendEvent 的对象）",
              box->dropCount - boxBefore == 1 && boxOrphan->dropCount - orphanBefore3 == 0);
        check("演示⑤④ 转发时坐标被重写：投 (5,5) 给孤儿盒，接收盒看到的是换算后的值",
              box->lastDropPos != QPointF(5, 5));
    });

    // ── 演示⑥ 数据格式不对 → 走 ignore 分支（接收端要有失败路径）
    QTimer::singleShot(2400, [&] {
        qInfo() << "";
        qInfo().noquote() << QStringLiteral("=== 演示⑥ 只给一个我们不认识的数据类型 ===");
        QMimeData md;
        md.setData(QStringLiteral("application/vnd.something-else"), QByteArrayLiteral("x"));
        const int rejBefore = box->rejectedCount;
        QDragEnterEvent enter(QPointF(30, 30), Qt::CopyAction, &md, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(box, &enter);
        qInfo().noquote() << QStringLiteral("  接收盒拒绝次数增量=%1，enter.isAccepted=%2，"
                                            "dropAction=%3（没人改过它，所以还是 0x1——但路由看的是 isAccepted）")
                                 .arg(box->rejectedCount - rejBefore).arg(enter.isAccepted())
                                 .arg(hex(int(enter.dropAction())));
        qInfo().noquote() << QStringLiteral("    → 之后用户真落下也不会有 dropEvent（拖动方拿到 IgnoreAction）。"
                                            "结论：**dragEnter 里的判断要尽量便宜**，别在这里做 I/O 或解析大对象，"
                                            "因为拖动过程中它会反复被调（dragMove 更频繁）。");
        check("演示⑥ 不认识的格式走 ignore()：拒绝计数 +1，isAccepted 为假",
              box->rejectedCount - rejBefore == 1 && !enter.isAccepted());
    });

    // ── 演示⑦ 真起拖：阈值判定 + exec() 的返回值（离屏平台是桩，立刻返回）
    QTimer::singleShot(2800, [&] {
        qInfo() << "";
        qInfo().noquote() << QStringLiteral("=== 演示⑦ 合成鼠标事件驱动真实起拖路径 ===");
        const int threshold = QApplication::startDragDistance();
        const int before = source->dragStarted;
        synthMouse(source, QEvent::MouseButtonPress, QPointF(2, 2), Qt::LeftButton, Qt::LeftButton);
        synthMouse(source, QEvent::MouseMove, QPointF(2.0 + threshold - 1, 2), Qt::NoButton, Qt::LeftButton);
        qInfo().noquote() << QStringLiteral("  按下 (2,2) 后移动 %1 px（阈值 %2）：起拖次数增量=%3"
                                            "（期望还没起拖）")
                                 .arg(threshold - 1).arg(threshold).arg(source->dragStarted - before);
        const int afterShort = source->dragStarted - before;
        synthMouse(source, QEvent::MouseMove, QPointF(2.0 + threshold + 8, 2), Qt::NoButton, Qt::LeftButton);
        qInfo().noquote() << QStringLiteral("  继续移动到累计 %1 px（≥ 阈值）：这一跳起拖次数=%2，"
                                            "exec() 返回=%3")
                                 .arg(threshold + 8).arg(source->dragStarted - before - afterShort)
                                 .arg(hex(int(source->lastExecResult)));
        synthMouse(source, QEvent::MouseMove, QPointF(2.0 + threshold + 40, 2), Qt::NoButton, Qt::LeftButton);
        qInfo().noquote() << QStringLiteral("  按住继续乱动（同一次按下）：起拖累计=%1 → 应当**不再增加**")
                                 .arg(source->dragStarted - before);
        qInfo().noquote() << QStringLiteral("    → exec() 返回 0x0 是**本机离屏平台的桩**"
                                            "（QOffscreenDrag::drag 直接 return IgnoreAction，"
                                            "qoffscreencommon.h 58~61），不是「拖放失败」的业务含义；"
                                            "换 windowsplatform 才会真跑 OLE 拖放。");
        check("演示⑦ 移动不足阈值 → 不起拖（阈值取的是 startDragDistance()，不是硬编码）",
              afterShort == 0);
        check("演示⑦ 越过阈值 → 恰好起拖一次，且 exec() 真的跑到了（本机返回 IgnoreAction）",
              source->dragStarted - before - afterShort == 1
              && source->lastExecResult == Qt::IgnoreAction);
        check("演示⑦ 同一次按下里继续移动不再起第二次拖（m_dragFired 守卫；"
              "别拿 QPoint()==(0,0) 当「按下过」的标志）",
              source->dragStarted - before == 1);
        check("演示⑦ exec(Copy|Move, IgnoreAction) 之后 defaultAction 被换算成 Move(0x2)——"
              "Move > Copy > Link（qdrag.cpp 241~248 行）",
              source->lastExecDefaultAction == int(Qt::MoveAction)
              && source->lastExecSupportedActions == int(Qt::CopyAction | Qt::MoveAction));
    });

    // ── 事实⑧ 内部拖放 vs 跨程序拖放（写在日志里，方便对着代码回想）
    QTimer::singleShot(3200, [] {
        qInfo() << "";
        qInfo().noquote() << QStringLiteral("=== 事实⑧ 两种拖放的差别 ===");
        qInfo().noquote() << QStringLiteral("  内部（同进程）：QMimeData **对象指针**直接递过去，数据不序列化，"
                                            "自定义 MIME 名随便用，source() 是真实的 QObject*，"
                                            "还能传只在本进程有意义的复杂对象；拖动过程可以很快（没有编解码）。");
        qInfo().noquote() << QStringLiteral("  跨程序：数据要经 QPA 走平台的机制（Windows 是 OLE 拖放，"
                                            "X11/Wayland 是各自的 DnD 协议），于是——"
                                            "① MIME 名必须是对方认得的（text/plain、text/uri-list 这类标准名）；"
                                            "② 数据要能变成 QByteArray，即 retrieveData(mimetype, QMetaType) 得给得出"
                                            "（qmimedata.h 53 行；Qt5 这里是 QVariant::Type，重写它的老代码必改）；"
                                            "③ 对方是另一个进程，你无法在它 drop 之前问它要不要，"
                                            "所以**在 dragEnter 就把 supportedActions 收得越窄越好**，"
                                            "别把「移动」这种破坏性动作开放给外部数据；"
                                            "④ 大对象请用 URL/文件路径传，别把整个字节流塞进拖放（会卡）。");
        qInfo().noquote() << QStringLiteral("  一句话：**内部拖放比的是对象指针，跨进程拖放比的是 MIME 名。**");
    });

    // ── 结论
    QTimer::singleShot(3600, [&] {
        qInfo() << "";
        qInfo().noquote() << QStringLiteral("=== 结论 ===");
        qInfo().noquote() << QStringLiteral("接收盒：enter=%1 move=%2 leave=%3 drop=%4 拒绝=%5 "
                                            "最后一次 dropAction=%6")
                                 .arg(box->enterCount).arg(box->moveCount).arg(box->leaveCount)
                                 .arg(box->dropCount).arg(box->rejectedCount).arg(hex(box->lastDropAction));
        qInfo().noquote() << QStringLiteral("没开开关的盒子：enter=%1 drop=%2；父·投放点：enter=%3；"
                                            "子·没开关：enter=%4")
                                 .arg(boxNoAccept->enterCount).arg(boxNoAccept->dropCount)
                                 .arg(dropSite->enterCount).arg(childNoFlag->enterCount);
        qInfo().noquote() << QStringLiteral("孤儿盒：enter=%1 drop=%2；拖动源：起拖次数=%3")
                                 .arg(boxOrphan->enterCount).arg(boxOrphan->dropCount)
                                 .arg(source->dragStarted);
        qInfo().noquote() << QStringLiteral("一句话：**QMimeData 谈格式、isAccepted 谈态度、"
                                            "dropAction 谈动作、currentTarget 谈收件人、"
                                            "startDragDistance 谈时机。**");
        qInfo().noquote() << QStringLiteral("=== 自检台账：PASS=%1 FAIL=%2 SKIP=%3 ===")
                                 .arg(gLedger.pass).arg(gLedger.fail).arg(gLedger.skip);
        QCoreApplication::exit(gLedger.fail == 0 ? 0 : 1);
    });

    return app.exec();
}

// ============================================================================
// 附：拖放的最小正确骨架（照抄这个，别自己发明）
// ============================================================================
/*
 *  接收端（target）
 *    setAcceptDrops(true);                                    // 忘了它，一切免谈（门槛在 notify 2997 行）
 *    void dragEnterEvent(QDragEnterEvent *e) {
 *        if (e->mimeData()->hasFormat(我的类型)) e->acceptProposedAction();   // 表态
 *        else e->ignore();
 *    }
 *    void dragMoveEvent(QDragMoveEvent *e)   { 同上，判断可以更快 }
 *    void dropEvent(QDropEvent *e) {
 *        const QMimeData *md = e->mimeData();
 *        ... 取数据 ...
 *        e->setDropAction(Qt::MoveAction);   // **要换动作就得显式写这一行**
 *        e->accept();
 *    }
 *
 *  发起端（source）
 *    void mousePressEvent(QMouseEvent *e) {
 *        if (e->button() == Qt::LeftButton) { m_pos = e->position().toPoint(); m_fired = false; }
 *    }
 *    void mouseMoveEvent(QMouseEvent *e) {
 *        if (!(e->buttons() & Qt::LeftButton) || m_fired) return;
 *        if ((e->position().toPoint() - m_pos).manhattanLength() < QApplication::startDragDistance()) return;
 *        m_fired = true;
 *        auto *drag = new QDrag(this);                       // Qt6：this 可以是任意 QObject
 *        auto *md = new QMimeData; md->setText(...); md->setData(我的类型, 字节);
 *        drag->setMimeData(md);                              // 所有权交给 drag
 *        drag->setPixmap(缩略图); drag->setHotSpot(指针在图上的位置);
 *        if (drag->exec(Qt::CopyAction|Qt::MoveAction, Qt::CopyAction) == Qt::MoveAction) 删掉原数据;
 *        drag->deleteLater();                                // **别 delete**：exec 里 Qt 已 deleteLater
 *    }
 *
 * 五条纪律：
 *  ① **isAccepted 与 dropAction 是两个字段**。构造函数已经把 dropAction 预置成提议的动作，
 *     所以对本机 6.12 的新鲜事件来说 accept() 和 acceptProposedAction() **结果相同**（演示③ 打出来了）。
 *     习惯上仍然写 acceptProposedAction()：它是「我接受这次拖放，并按你提议的动作来」的**声明**，
 *     而且在事件被复用的场合（qguiapplication.cpp 3593~3596 会把上一轮接受的动作先 set 进新的
 *     DragMove 事件）它能保证 dropAction 回到「对方此刻提议的值」。
 *     需要**换动作**时必须 setDropAction()；先 set 再 acceptProposedAction() 会被覆盖（演示③ E 行）。
 *  ② 禁止光标的根因是 **isAccepted 为假**（qsimpledrag.cpp 285 行默认 ForbiddenCursor，
 *     437 行 setCanDrop(isAccepted)），或者你要的动作不在拖动方的 supportedActions 里；
 *     不是「少调了一次 acceptProposedAction」。查「放不下」先查这两处，再查 acceptDrops。
 *  ③ dragEnter/dragMove 每次移动都会跑，里面**只做便宜判断**；解析、读文件、
 *     申请内存这些活全留到 dropEvent。
 *  ④ 自定义 MIME 名是内部拖放的福利、跨程序的雷：起名叫 `应用名/语义`
 *     （本讲用 application/x-qt-learning-item），并且永远同时准备一份标准格式
 *     （text/plain）作为对外兼容。
 *  ⑤ **exec() 里嵌着平台拖放会话**：Windows 会阻塞 Qt 事件循环（qdrag.cpp 223~229 行），
 *     Linux/macOS 不阻塞（199~203 行）。跨平台的重入清单因此**按最坏的那一代写**：
 *     别在起拖之前改状态又指望它保持不变。
 *
 * ——— 老师傅的话 ———
 *   徒弟，拖放这个功能，代码量小，坑全是「看不见」的那一类。为什么？
 *   因为你平时测试是**用鼠标拖的**，鼠标一拖，进-移-落三步全走通，你只看到了「成功的那条路」。
 *   而用户的失败发生在另外三条路上：格式不对、开关没开、光标没反馈。
 *
 *   所以师傅把这个仓库里旧稿的一句话给你改了。旧稿说「只 accept() 不 acceptProposedAction()
 *   会变成禁止光标」，听起来很有道理，背下来也很顺手——但构造函数早就把 dropAction 填好了，
 *   这一对在新鲜事件上根本等不出差别。**能打印的东西不要背**：本讲演示③ 六行真值表，
 *   一眼就知道该写哪一行。
 *
 *   再说「我的 dropEvent 怎么不进」这条老问题。旧稿给你列了三个原因，本讲把它换成一条规则：
 *   **拖放事件的收件人不是你能指定的**。Enter 沿父链上溯找开关（2996~3008），
 *   Move/Drop 一律改投 QDragManager 记下的那个目标（3021），落完一次就清空（3042~3047）。
 *   你 sendEvent 投给谁、投在什么坐标，Qt 都有权改。想不明白这条，
 *   「明明断点在 dropEvent 里却不进」就能查你一晚上。
 *
 *   最后说跨进程那件事。你在自己程序里传个 QByteArray 叫「自定义类型」，
 *   出了你的进程它就叫「没人认的数据」。真正的资深做法是：**同时给多份表示**，
 *   文本给 text/plain、文件给 text/uri-list、自己的结构体给自定义类型，
 *   让对方按标准名去挑。这么写一次，你的程序就能和整个桌面环境互通；
 *   偷懒只给自定义类型，程序就只能跟自己说话。
 *
 * ——— 小练习 ———
 *   1. 给 DropBox 再加一个格式判断：只接受「urls 里全部是 .txt」的拖放，
 *      其他一律 ignore()。然后用 synthDrop 分别投「1 个 txt」「1 个 exe」「txt+exe 混合」
 *      三种 QMimeData，把三次的 enter/drop 计数与 dropAction 打表。
 *   2. 把演示②b 的父控件也改成 setAcceptDrops(false)，重跑一次，
 *      看「父·投放点」的 enter 计数变成几；再解释：为什么这一次事件彻底消失了，
 *      而演示⑤① 的 Drop 也消失，两者消失的位置**不是同一处**。（提示：2997 与 3021~3031。）
 *   3. 把演示③ 的 D 行改成 `setDropAction(Qt::TargetMoveAction); accept();`
 *      （qnamespace.h 1505 行，0x8002），对照 possibleActions 的检查（qplatformdrag.cpp 107~116），
 *      说出 proposedAction() 为什么不会给你 TargetMoveAction。
 *   4. 手动路径（本机离屏跑不出真会话，只能手动）：把 DragSource::startDrag() 的 exec
 *      换成 `exec(Qt::CopyAction)`，在**带桌面的运行**里从「拖动源」拖到「接收盒」，
 *      观察光标形状与打印出的 exec 返回值；再拖到窗口**外面**松手，看返回什么动作。
 *      最后回答：为什么这条演示无法用合成事件代替？（提示：指针 grab 与平台 DnD 会话。）
 */
// ============================================================================
