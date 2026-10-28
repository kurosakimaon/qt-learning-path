// ============================================================================
// 第 5 讲 · 输入事件与焦点
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   鼠标、键盘、焦点是用户程序 90% 的输入面，也是 **Qt5 → Qt6 改动最集中**的一片。
//   本讲用「代码合成的输入事件」把这几件事一次讲清（全程不用鼠标键盘）：
//     1) QMouseEvent 的坐标体系：position() / scenePosition() / globalPosition()
//        都是 **QPointF**（Qt5 是 pos()/globalPos() 的 QPoint 时代，必考差异）；
//     2) buttons() 与 button() 的分工（「此刻按住哪些」vs「这一次是谁变了」）——
//        Qt5 的 mouseButtons()/keyboardModifiers() 在 Qt6 已改名，多处仍是弃用状态；
//     3) 双击的真实时序：Press → Release → MouseButtonDblClick，
//        以及 QApplication::doubleClickInterval() 从哪来；
//     4) 滚轮：angleDelta() 与 pixelDelta() 的区别、DefaultDeltasPerStep = 120、
//        phase/inverted 这些 Qt6 新增参数；
//     5) 键盘：key() / text() / modifiers() / isAutoRepeat()，以及 QKeyCombination；
//     6) 焦点：Qt::FocusPolicy 五档、tab 顺序由什么决定、setFocus/focusIn/focusOut、
//        QApplication::focusChanged 信号；
//     7) QShortcut + QKeySequence（含标准键 QKeySequence::Copy）。
//
// 对应构建目标名：events_05_mouse_keyboard_focus
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(events_05_mouse_keyboard_focus 05_mouse_keyboard_focus.cpp)
//   target_link_libraries(events_05_mouse_keyboard_focus PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)   # 本讲示例不需要 Q_OBJECT（重写 handler 只是虚函数继承，
//                           # 不依赖元对象表）——这一点正好和第 3 讲「过滤器要发信号」对照
//
// 运行前须知：
//   · 无鼠标键盘：全部事件由代码 new/栈构造后用 QCoreApplication::sendEvent() 投递。
//   · 本机当前无法编译链接 Qt 运行时，本示例**未在本机实测编译**；
//     签名对着 C:\msys64\ucrt64\include\qt6 的真实头文件逐个核对（注释给了文件与行号）。
//   · **焦点/快捷键有一类演示依赖「窗口是否活动」**：Windows 正常桌面能过；
//     在 -platform offscreen/minimal 下可能收不到 focusChanged、快捷键也可能不触发。
//     示例把这类计数都打出来，读到 0 别当成 bug，请按注释分辨「平台限制」与「代码写错」。
// ============================================================================

#include <QApplication>
#include <QWidget>
#include <QEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QShortcut>
#include <QKeySequence>
#include <QKeyCombination>
#include <QTimer>
#include <QDebug>
#include <QString>

// ============================================================================
// 一、核对签名（本讲全部依据，逐条列出来）
// ============================================================================
// QtGui/qevent.h
//   第 49 行   class QInputEvent : public QEvent
//   第 73 行   class QPointerEvent : public QInputEvent
//   第 109 行  class QSinglePointEvent : public QPointerEvent
//        · 第 117 行  inline Qt::MouseButton  button()    const   ← 哪一个按钮发生了变化
//        · 第 118 行  inline Qt::MouseButtons buttons()   const   ← 此刻按住着哪些
//        · 第 120~125 行 inline QPointF position() const / scenePosition() const / globalPosition() const
//   第 197 行  class QMouseEvent : public QSinglePointEvent
//        · 未弃用的构造（第 206 行起）：
//            QMouseEvent(Type, const QPointF &localPos, const QPointF &globalPos,
//                        Qt::MouseButton, Qt::MouseButtons, Qt::KeyboardModifiers,
//                        const QPointingDevice *device = QPointingDevice::primaryPointingDevice());
//            QMouseEvent(Type, const QPointF &localPos, const QPointF &scenePos, const QPointF &globalPos,
//                        Qt::MouseButton, Qt::MouseButtons, Qt::KeyboardModifiers, ...);
//        · 第 222 行左右  inline QPoint pos() const { return position().toPoint(); }
//            —— 注意它被 #ifndef QT_NO_INTEGER_EVENT_COORDINATES 包着：
//               定义了那个宏的构建里**没有 pos()/globalPos()/x()/y()**，只有 QPointF 版本。
//        · localPos()/windowPos()/screenPos()/globalPos()/x()/y() 全部
//          QT_DEPRECATED_VERSION_X_6_0（Qt6 里仍可用但会报警告，迁移请换成三个 position()）
//        · 第 244 行左右  Qt::MouseEventSource source() const; Qt::MouseEventFlags flags() const;
//   第 282 行  class QWheelEvent : public QSinglePointEvent
//        · 第 292 行 enum { DefaultDeltasPerStep = 120 };
//        · 第 294 行 QWheelEvent(const QPointF &pos, const QPointF &globalPos,
//                                 QPoint pixelDelta, QPoint angleDelta,
//                                 Qt::MouseButtons, Qt::KeyboardModifiers,
//                                 Qt::ScrollPhase phase, bool inverted,
//                                 Qt::MouseEventSource = Qt::MouseEventNotSynthesized,
//                                 const QPointingDevice * = QPointingDevice::primaryPointingDevice());
//          ← **phase 与 inverted 在 Qt6 是必填参数**，Qt5 那个版本带的是 Qt::Orientation。
//        · pixelDelta() / angleDelta() 返回 QPoint；hasPixelDelta() / inverted() / phase()
//   第 425 行  class QKeyEvent : public QInputEvent
//        · 第 429 行 QKeyEvent(Type type, int key, Qt::KeyboardModifiers modifiers,
//                              const QString &text = QString(), bool autorep = false, quint16 count = 1);
//        · int key() / QString text() / Qt::KeyboardModifiers modifiers() / bool isAutoRepeat()
//        · QKeyCombination keyCombination()   ← Qt6 新增：把 (key, modifiers) 合成一个可比较的值
//        · bool matches(QKeySequence::StandardKey) const（需 QT_CONFIG(shortcut)）
//   第 471 行  class QFocusEvent : public QEvent
//        · explicit QFocusEvent(Type type, Qt::FocusReason reason = Qt::OtherFocusReason);
//        · Qt::FocusReason reason() const / bool gotFocus() const
//   第 166 行  class QEnterEvent : public QSinglePointEvent
//        ← Qt5 里 QWidget::enterEvent(QEvent*)，**Qt6 改成 enterEvent(QEnterEvent*)**
//          （qwidget.h 第 672 行）：老代码搬过来会在这里编译失败，不是你的错。
// QtWidgets/qwidget.h（全在 protected 段，第 658 行 protected:）
//   661 mousePressEvent  662 mouseReleaseEvent  663 mouseDoubleClickEvent  664 mouseMoveEvent
//   666 wheelEvent  668 keyPressEvent  669 keyReleaseEvent
//   670 focusInEvent  671 focusOutEvent  672 enterEvent(QEnterEvent*)  673 leaveEvent
//   public 段：426 setFocus()  433 setFocus(Qt::FocusReason)  431 clearFocus()
//              434 focusPolicy()  435 setFocusPolicy(Qt::FocusPolicy)
//              436 hasFocus()  437 static setTabOrder(QWidget*, QWidget*)
//              439 setFocusProxy(QWidget*)  430 activateWindow()
//              336 setMouseTracking(bool)  450 grabKeyboard()  451 releaseKeyboard()
//              558 setAcceptDrops(bool)（第 7 讲要用）
// QtWidgets/qapplication.h
//   79  static QWidget *focusWidget()   81 static QWidget *activeWindow()
//   100 static int doubleClickInterval()   103 static int keyboardInputInterval()
//   107 static int wheelScrollLines()（QT_CONFIG(wheelevent)）
//   124 void focusChanged(QWidget *old, QWidget *now)   ← Q_SIGNALS（第 123 行）
// QtCore/qnamespace.h
//   68  enum MouseButton { NoButton=0, LeftButton=0x1, RightButton=0x2, MiddleButton=0x4, ... }
//   107 Q_DECLARE_FLAGS(MouseButtons, MouseButton)
//   118 enum FocusPolicy { NoFocus=0, TabFocus=0x1, ClickFocus=0x2,
//                          StrongFocus=TabFocus|ClickFocus|0x8, WheelFocus=StrongFocus|0x4 }
//   1077 enum KeyboardModifier { NoModifier, ShiftModifier, ControlModifier, AltModifier,
//                                MetaModifier, KeypadModifier, GroupSwitchModifier }
//   1322 enum ShortcutContext { WidgetShortcut, WindowShortcut, ApplicationShortcut,
//                               WidgetWithChildrenShortcut }
//   1369 enum FocusReason { MouseFocusReason, TabFocusReason, BacktabFocusReason,
//                           ActiveWindowFocusReason, PopupFocusReason, ShortcutFocusReason,
//                           MenuBarFocusReason, OtherFocusReason, NoFocusReason }
//   1729 enum ScrollPhase { NoScrollPhase, ScrollBegin, ScrollUpdate, ScrollEnd, ScrollMomentum }
//   1737 enum MouseEventSource { MouseEventNotSynthesized, MouseEventSynthesizedBySystem,
//                                MouseEventSynthesizedByQt, MouseEventSynthesizedByApplication }
//   1744 enum MouseEventFlag { NoMouseEventFlag, MouseEventCreatedDoubleClick = 0x01, ... }
//   687  Key_Tab = 0x01000001、536 Key_Space = 0x20
// QtGui/qshortcut.h（注意：QShortcut 在 **QtGui**，不在 QtWidgets）
//   19  class Q_GUI_EXPORT QShortcut : public QObject
//   28  explicit QShortcut(QObject *parent);
//   77  QShortcut(const QKeySequence &key, QObject *parent, ...,
//                 Qt::ShortcutContext context = Qt::WindowShortcut)      ← 推荐的两参用法
//   118 QShortcut(QKeySequence::StandardKey key, QObject *parent, ...,
//                 Qt::ShortcutContext context = Qt::WindowShortcut)
//   160 setKey / 161 key / 162 setKeys(StandardKey) / 166 setEnabled / 169 setContext
//   172 setAutoRepeat / 194 Q_SIGNAL void activated()
// QtGui/qkeysequence.h  第 40 行起 enum StandardKey { ..., Open(44), Save(46), Cut(49),
//                       Copy(50), Paste(51), Undo(52), Redo(53), Print(59), Find(63),
//                       Replace(66), SelectAll(67), ..., Quit(106) }  ← 标准键确实存在

// ============================================================================
// 二、一块会自报家门的输入面板
// ============================================================================
class InputPanel : public QWidget
{
public:
    int pressCount = 0, releaseCount = 0, moveCount = 0, dblCount = 0;
    int wheelCount = 0, keyCount = 0, focusInCount = 0, focusOutCount = 0;
    QString lastReport;

    explicit InputPanel(const QString &name)
    {
        setObjectName(name);
        setFixedSize(180, 80);
        setFocusPolicy(Qt::StrongFocus);   // 核对：qnamespace.h 118 行；qwidget.h 435 行
    }

    void log(const QString &line)
    {
        lastReport = line;
        qInfo().noquote() << QStringLiteral("    %1 :: %2").arg(objectName(), line);
    }

protected:
    // ── 鼠标 ────────────────────────────────────────────────────
    void mousePressEvent(QMouseEvent *e) override
    {
        ++pressCount;
        // Qt6：三个 position() 都是 QPointF；button() 是谁变了，buttons() 是现在的状态
        log(QStringLiteral("mousePressEvent  type=%1  position=(%2,%3) globalPosition=(%4,%5) "
                           "button=0x%6 buttons=0x%7 modifiers=0x%8 source=%9")
                .arg(int(e->type()))
                .arg(e->position().x(), 0, 'f', 1).arg(e->position().y(), 0, 'f', 1)
                .arg(e->globalPosition().x(), 0, 'f', 1).arg(e->globalPosition().y(), 0, 'f', 1)
                .arg(int(e->button()), 0, 16)
                .arg(int(e->buttons()), 0, 16)
                .arg(int(e->modifiers()), 0, 16)
                .arg(int(e->source())));
        QWidget::mousePressEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        ++releaseCount;
        log(QStringLiteral("mouseReleaseEvent button=0x%1 buttons=0x%2")
                .arg(int(e->button()), 0, 16).arg(int(e->buttons()), 0, 16));
        QWidget::mouseReleaseEvent(e);
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        ++moveCount;
        log(QStringLiteral("mouseMoveEvent position=(%1,%2) buttons=0x%3")
                .arg(e->position().x(), 0, 'f', 1).arg(e->position().y(), 0, 'f', 1)
                .arg(int(e->buttons()), 0, 16));
        QWidget::mouseMoveEvent(e);
    }

    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        ++dblCount;
        // 核对：qnamespace.h 1744 行 MouseEventFlag::MouseEventCreatedDoubleClick
        log(QStringLiteral("mouseDoubleClickEvent button=0x%1 flags=0x%2（%3）")
                .arg(int(e->button()), 0, 16)
                .arg(int(e->flags()), 0, 16)
                .arg(e->flags() & Qt::MouseEventCreatedDoubleClick
                         ? QStringLiteral("Qt 由两次 press 合成") : QStringLiteral("系统直接给的双击")));
        QWidget::mouseDoubleClickEvent(e);
    }

    void wheelEvent(QWheelEvent *e) override
    {
        ++wheelCount;
        // 核对：qevent.h 282 行起 —— angleDelta()/pixelDelta() 都是 QPoint
        log(QStringLiteral("wheelEvent angle=(%1,%2) pixel=(%3,%4) hasPixelDelta=%5 phase=%6 inverted=%7 buttons=0x%8")
                .arg(e->angleDelta().x()).arg(e->angleDelta().y())
                .arg(e->pixelDelta().x()).arg(e->pixelDelta().y())
                .arg(e->hasPixelDelta() ? QStringLiteral("true") : QStringLiteral("false"))
                .arg(int(e->phase()))
                .arg(e->inverted() ? QStringLiteral("true") : QStringLiteral("false"))
                .arg(int(e->buttons()), 0, 16));
        QWidget::wheelEvent(e);
    }

    // ── 键盘 ────────────────────────────────────────────────────
    void keyPressEvent(QKeyEvent *e) override
    {
        ++keyCount;
        // 说明：Qt6 另有 QKeyCombination keyCombination() const（qevent.h 内联实现，
        //   把 key 与 modifiers 合成一个可比较的值）。它的访问器本讲没核对，
        //   所以下面只用 key()/modifiers() 这两个已核对的字段。
        log(QStringLiteral("keyPressEvent key=0x%1 text=\"%2\" modifiers=0x%3 autoRepeat=%4 count=%5")
                .arg(e->key(), 0, 16)
                .arg(e->text().toHtmlEscaped())
                .arg(int(e->modifiers()), 0, 16)
                .arg(e->isAutoRepeat() ? QStringLiteral("true") : QStringLiteral("false"))
                .arg(int(e->count())));
        QWidget::keyPressEvent(e);
    }

    void keyReleaseEvent(QKeyEvent *e) override
    {
        log(QStringLiteral("keyReleaseEvent key=0x%1").arg(e->key(), 0, 16));
        QWidget::keyReleaseEvent(e);
    }

    // ── 焦点 ────────────────────────────────────────────────────
    void focusInEvent(QFocusEvent *e) override
    {
        ++focusInCount;
        // 核对：qevent.h 471 行 Qt::FocusReason reason() const
        log(QStringLiteral("focusInEvent reason=%1（%2）")
                .arg(int(e->reason()))
                .arg(e->reason() == Qt::TabFocusReason ? QStringLiteral("Tab")
                     : e->reason() == Qt::MouseFocusReason ? QStringLiteral("Mouse")
                     : e->reason() == Qt::ActiveWindowFocusReason ? QStringLiteral("ActiveWindow")
                     : QStringLiteral("Other")));
        QWidget::focusInEvent(e);
    }

    void focusOutEvent(QFocusEvent *e) override
    {
        ++focusOutCount;
        log(QStringLiteral("focusOutEvent reason=%1").arg(int(e->reason())));
        QWidget::focusOutEvent(e);
    }

    // 核对：Qt6 的签名是 QEnterEvent*（Qt5 是 QEvent*）——迁移必改点
    void enterEvent(QEnterEvent *e) override
    {
        log(QStringLiteral("enterEvent 位置=(%1,%2)").arg(e->position().x()).arg(e->position().y()));
        QWidget::enterEvent(e);
    }
};

// ============================================================================
// 三、合成事件的三个小工具（本模块的核心技巧：不用手也能演输入）
// ============================================================================
static void synthMouse(QWidget *w, QEvent::Type type, const QPointF &local,
                       Qt::MouseButton changed, Qt::MouseButtons held,
                       Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    // 全局坐标 = 控件局部坐标 + 控件左上角的全局坐标
    // 核对：qwidget.h 的 mapToGlobal 收 QPoint；而 QMouseEvent 要的是 QPointF，
    //       所以这里显式 qRound 之后再过整数（QPointF::toPoint() 是**截断**，不是四舍五入）
    const QPoint localInt(qRound(local.x()), qRound(local.y()));
    const QPointF global(w->mapToGlobal(localInt));

    QMouseEvent ev(type, local, global, changed, held, mods);
    QCoreApplication::sendEvent(w, &ev);            // 同步：返回时整条链已跑完
}

static void synthWheel(QWidget *w, const QPoint &angle, const QPoint &pixel)
{
    const QPointF local(40, 40);
    const QPointF global = QPointF(w->mapToGlobal(local.toPoint()));
    // Qt6 的 phase 与 inverted 是**必填**（核对 qevent.h 294 行）
    QWheelEvent ev(local, global, pixel, angle, Qt::NoButton, Qt::NoModifier,
                   Qt::ScrollUpdate, false);
    QCoreApplication::sendEvent(w, &ev);
}

static void synthKey(QWidget *w, int key, Qt::KeyboardModifiers mods, const QString &text = QString())
{
    // 核对：qevent.h 429 行 QKeyEvent(Type, int key, Qt::KeyboardModifiers,
    //                              const QString &text = ..., bool autorep = false, quint16 count = 1)
    QKeyEvent press(QEvent::KeyPress, key, mods, text);
    QKeyEvent release(QEvent::KeyRelease, key, mods, text);
    QCoreApplication::sendEvent(w, &press);
    QCoreApplication::sendEvent(w, &release);
}

// ============================================================================
// 四、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QWidget window;
    window.setObjectName(QStringLiteral("主窗"));
    window.setWindowTitle(QStringLiteral("第 5 讲 · 输入事件与焦点"));
    window.resize(560, 320);

    auto *p1 = new InputPanel(QStringLiteral("面板1"));
    p1->move(20, 20);
    auto *p2 = new InputPanel(QStringLiteral("面板2"));
    p2->move(220, 20);
    p2->setFocusPolicy(Qt::ClickFocus);             // 故意不同：只能用鼠标点进来

    window.show();
    p1->show();
    p2->show();

    // ── 记录全程序焦点变化（应用层信号，正是「事件→信号」的现成例子）
    int focusChanges = 0;
    QObject::connect(&app, &QApplication::focusChanged,
                     [&focusChanges](QWidget *oldW, QWidget *now) {
                         ++focusChanges;
                         qInfo() << "  信号 QApplication::focusChanged：旧 ="
                                 << (oldW ? oldW->objectName() : QStringLiteral("<无>"))
                                 << "新 =" << (now ? now->objectName() : QStringLiteral("<无>"));
                     });

    // ── 演示 ① 鼠标：按下→移动→松开，看清 button()/buttons()/position()
    QTimer::singleShot(500, [p1] {
        qInfo() << "\n=== 演示① 鼠标按下/移动/释放（合成，无鼠标）===";
        synthMouse(p1, QEvent::MouseButtonPress, QPointF(30, 20), Qt::LeftButton, Qt::LeftButton);
        synthMouse(p1, QEvent::MouseMove, QPointF(60, 40), Qt::NoButton, Qt::LeftButton);
        synthMouse(p1, QEvent::MouseButtonRelease, QPointF(60, 40), Qt::LeftButton, Qt::NoButton);
        qInfo() << "演示① 计数：press" << p1->pressCount << "move" << p1->moveCount
                << "release" << p1->releaseCount;
        // 【鼠标移动的前提】没按下时 Qt 只把移动事件发给开了 setMouseTracking(true)
        // 的控件（qwidget.h 336 行）。这里移动发生在按下之后，所以一定到；
        // 试着把 p1->setMouseTracking(false) 后单独投一次 Move，计数就不会涨。
    });

    // ── 演示 ② button() vs buttons()：多键并按
    QTimer::singleShot(900, [p1] {
        qInfo() << "\n=== 演示② button()（谁变了）vs buttons()（现在按着谁）===";
        synthMouse(p1, QEvent::MouseButtonPress, QPointF(30, 20), Qt::LeftButton, Qt::LeftButton);
        synthMouse(p1, QEvent::MouseButtonPress, QPointF(30, 20), Qt::RightButton,
                   Qt::LeftButton | Qt::RightButton);      // 左键仍按着
        synthMouse(p1, QEvent::MouseButtonRelease, QPointF(30, 20), Qt::LeftButton, Qt::RightButton);
        qInfo() << "演示② 结论：**button() 是本次事件里发生变化的那个键**，"
                << "**buttons() 是此刻仍然按住的所有键**。判断「拖拽中」请用 buttons() & Qt::LeftButton，"
                << "判断「右键点在这」请用 button() == Qt::RightButton。"
                << "（Qt5 的 mouseButtons()/keyboardModifiers() 在 Qt6 已改名 buttons()/modifiers()，"
                << "旧名在 QDropEvent 里仍留着但标了 QT_DEPRECATED_VERSION_X_6_0，qevent.h 13~16 行）";
    });

    // ── 演示 ③ 双击时序
    QTimer::singleShot(1300, [p1] {
        qInfo() << "\n=== 演示③ 双击的先后次序 ===";
        p1->pressCount = 0; p1->releaseCount = 0; p1->dblCount = 0;
        // 窗口系统实际送来的顺序（Windows）：Press → Release → **DoubleClick** → Release
        // Qt 把「第二次按下」直接改写成 MouseButtonDblClick，所以你**不会**看到两次 press。
        synthMouse(p1, QEvent::MouseButtonPress, QPointF(30, 20), Qt::LeftButton, Qt::LeftButton);
        synthMouse(p1, QEvent::MouseButtonRelease, QPointF(30, 20), Qt::LeftButton, Qt::NoButton);
        synthMouse(p1, QEvent::MouseButtonDblClick, QPointF(30, 20), Qt::LeftButton, Qt::LeftButton);
        synthMouse(p1, QEvent::MouseButtonRelease, QPointF(30, 20), Qt::LeftButton, Qt::NoButton);
        qInfo() << "演示③ 结果：press" << p1->pressCount << "次、dbl" << p1->dblCount
                << "次、release" << p1->releaseCount << "次 → 期望 1/1/2。"
                << "双击判定窗口由系统给出，Qt 侧的参考值是 QApplication::doubleClickInterval()"
                << "=" << QApplication::doubleClickInterval() << "ms（qapplication.h 100 行）。";
    });

    // ── 演示 ④ 滚轮：angleDelta vs pixelDelta
    QTimer::singleShot(1700, [p1] {
        qInfo() << "\n=== 演示④ 滚轮两种增量 ===";
        // 传统鼠标滚轮：angleDelta 一格 120（QWheelEvent::DefaultDeltasPerStep，qevent.h 292 行），
        // pixelDelta 在这条路径上是 (0,0)。
        synthWheel(p1, QPoint(0, 120), QPoint(0, 0));
        // 触摸板/惯性滚动：pixelDelta 给「按像素滚动多少」，angleDelta 可能同时有值。
        synthWheel(p1, QPoint(0, 30), QPoint(0, 14));
        qInfo() << "演示④ 结论：**angleDelta 是「格 × 120」的通用值，pixelDelta 是像素值**。"
                << "写通用滚动代码请两条路都走：pixelDelta 非零就用它，否则用 angleDelta().y() / 120 "
                << "* QApplication::wheelScrollLines()（qapplication.h 107 行）。"
                << "另外 Qt6 的 QWheelEvent 构造里 phase 与 inverted 是必填参数——"
                << "Qt5 那个带 Qt::Orientation 的构造搬过来编不过。";
    });

    // ── 演示 ⑤ 键盘：key / text / modifiers / 组合键
    QTimer::singleShot(2100, [p1] {
        qInfo() << "\n=== 演示⑤ 键盘事件字段 ===";
        p1->setFocus(Qt::MouseFocusReason);      // qwidget.h 433 行
        synthKey(p1, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
        synthKey(p1, Qt::Key_A, Qt::ControlModifier, QString());
        synthKey(p1, Qt::Key_Space, Qt::ShiftModifier, QStringLiteral(" "));
        // 核对：qevent.h 425 行起 int key()（键值，来自 Qt::Key_*）、QString text()（产出的字符）、
        //       Qt::KeyboardModifiers modifiers()（修饰键掩码）、QKeyCombination keyCombination()
        qInfo() << "演示⑤ 结论：**key() 是物理/逻辑键码（Qt::Key_*），text() 是经过输入法/shift"
                << "换算之后的字符**。处理「输入了什么」用 text()，处理「按了哪个键」用 key()；"
                << "两者都为空的按键（如纯修饰键）必须防住。"
                << "长任务/输入框一律先判 isAutoRepeat()（qevent.h 22 行附近），否则按住键会把你打爆。";
    });

    // ── 演示 ⑥ 焦点策略五档 + tab 顺序
    QTimer::singleShot(2500, [p1, p2, &window, &focusChanges] {
        qInfo() << "\n=== 演示⑥ FocusPolicy 五档与 tab 链 ===";
        // 核对：qnamespace.h 118 行
        //   NoFocus=0 / TabFocus=0x1 / ClickFocus=0x2 / StrongFocus=TabFocus|ClickFocus|0x8
        //   WheelFocus=StrongFocus|0x4   ← 其实是**五档**，不是四档
        struct Row { const char *name; Qt::FocusPolicy p; };
        const Row rows[] = {
            { "NoFocus", Qt::NoFocus }, { "TabFocus", Qt::TabFocus },
            { "ClickFocus", Qt::ClickFocus }, { "StrongFocus", Qt::StrongFocus },
            { "WheelFocus", Qt::WheelFocus }
        };
        for (const Row &r : rows) {
            p1->setFocusPolicy(r.p);
            p1->clearFocus();
            p1->setFocus(Qt::TabFocusReason);
            qInfo() << "   策略" << r.name << "→ setFocus 之后 hasFocus =" << p1->hasFocus()
                    << "（qwidget.h 436 行）";
        }

        // tab 顺序由什么决定：**先看你有没有 setTabOrder**；
        // 没设过就是「同一父控件下、能接受 tab 的控件，按创建/加入父子树的先后」排。
        if (QWidget *cur = QApplication::focusWidget())
            cur->clearFocus();              // qapplication.h 79 行 / qwidget.h 431 行
        QWidget::setTabOrder(p1, p2);           // qwidget.h 437 行，static
        p1->setFocusPolicy(Qt::StrongFocus);
        p2->setFocusPolicy(Qt::TabFocus);
        p1->setFocus(Qt::TabFocusReason);
        qInfo() << "   当前 focusWidget =" << (QApplication::focusWidget()
                    ? QApplication::focusWidget()->objectName() : QStringLiteral("<无>"))
                << "（qapplication.h 79 行）";
        // 用代码投 Tab 键：Qt 内部会走 focusNextPrevChild(true)（qwidget.h 725 行，protected）
        synthKey(&window, Qt::Key_Tab, Qt::NoModifier);
        qInfo() << "   投了一次 Tab：焦点现在在" << (QApplication::focusWidget()
                    ? QApplication::focusWidget()->objectName() : QStringLiteral("<无>（平台可能不配合）"))
                << "；再投 Shift+Tab 应回退（BacktabFocusReason）。";
        synthKey(&window, Qt::Key_Backtab, Qt::ShiftModifier);
        qInfo() << "   Shift+Tab 之后：" << (QApplication::focusWidget()
                    ? QApplication::focusWidget()->objectName() : QStringLiteral("<无>"));
        qInfo() << "   focusChanged 信号累计" << focusChanges << "次；面板1 focusIn"
                << p1->focusInCount << "/focusOut" << p1->focusOutCount;
    });

    // ── 演示 ⑦ QShortcut + QKeySequence（含标准键）
    QTimer::singleShot(2900, [&window, p1] {
        qInfo() << "\n=== 演示⑦ 快捷键：QShortcut 与标准键 ===";
        int fired = 0;

        // 写法 A：自定义组合键（核对 qshortcut.h 第 77 行的两参便利构造）
        auto *scCustom = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+J")), &window);
        scCustom->setContext(Qt::WindowShortcut);        // qshortcut.h 169 行；qnamespace.h 1322 行
        QObject::connect(scCustom, &QShortcut::activated, [&fired, p1] {
            ++fired;
            p1->log(QStringLiteral("QShortcut(Ctrl+Alt+J) activated"));
        });

        // 写法 B：标准键（qkeysequence.h 第 50 行 Copy 确实存在）
        auto *scCopy = new QShortcut(QKeySequence::Copy, &window);
        int copyFired = 0;
        QObject::connect(scCopy, &QShortcut::activated, [&copyFired] { ++copyFired; });

        qInfo() << "  自定义键的 QKeySequence 文本 =" << scCustom->key().toString()
                << "标准键 Copy 的文本 =" << scCopy->key().toString()
                << "（标准键在不同平台/风格下解析成不同实际按键，别硬写 \"Ctrl+C\"）";

        // 试着用合成按键触发快捷键
        synthKey(&window, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
        qInfo() << "  合成 Ctrl+Alt+J 之后 activated 计数 =" << fired
                << "→ 桌面平台一般能触发；**offscreen/minimal 下常常是 0**，"
                << "因为快捷键激活依赖「这是活动窗口」这一平台判定（qapplication.h 81 行 activeWindow()）。"
                << "这是平台限制，不是代码错误；要单测快捷键，请显式 activateWindow()+setFocus()，"
                << "或把「快捷键要做的事」抽成一个普通函数直接测那个函数（推荐）。";
        qInfo() << "  copyFired =" << copyFired << "（同上，别把 0 当 bug）";
        delete scCustom; delete scCopy;
    });

    // ── 结论
    QTimer::singleShot(3300, [p1, p2] {
        qInfo() << "\n=== 结论 ===";
        qInfo() << "面板1：press" << p1->pressCount << "release" << p1->releaseCount
                << "move" << p1->moveCount << "dbl" << p1->dblCount
                << "wheel" << p1->wheelCount << "key" << p1->keyCount;
        qInfo() << "面板2（ClickFocus）：focusIn" << p2->focusInCount << "focusOut" << p2->focusOutCount;
        qInfo() << "一句话：**坐标用 QPointF 的 position()/scenePosition()/globalPosition()，"
                << "按钮用 button()/buttons() 分清「变化」与「状态」，"
                << "输入字符看 text()、按键看 key()，焦点行为看 FocusPolicy 与 tab 链。**";
        QCoreApplication::quit();
    });

    return app.exec();
}

// ============================================================================
// 附：Qt5 → Qt6 输入事件对照表（本讲核对到的，逐条给你）
// ============================================================================
/*
 *  Qt5 写法                              Qt6 对应（本机 qevent.h 行号）             状态
 *  ------------------------------------------------------------------------------
 *  e->pos()          → QPoint            e->position().toPoint()（222 行左右）      pos() 仍在，
 *  e->globalPos()    → QPoint            e->globalPosition().toPoint()              被 QT_NO_INTEGER_EVENT_
 *  e->x() / e->y()                       从 position() 取整                         COORDINATES 宏屏蔽
 *  e->localPos()     → QPointF           e->position()               (6_0 弃用)      ⚠ 警告
 *  e->windowPos()    → QPointF           e->scenePosition()          (6_0 弃用)      ⚠ 警告
 *  e->screenPos()    → QPointF           e->globalPosition()         (6_0 弃用)      ⚠ 警告
 *  drop->mouseButtons()                  drop->buttons()             (6_0 弃用, 13 行) ⚠
 *  drop->keyboardModifiers()             drop->modifiers()           (6_0 弃用, 16 行) ⚠
 *  enterEvent(QEvent*)                   enterEvent(QEnterEvent*)    (qwidget.h 672)  ❌ 编译不过
 *  QWheelEvent(..., Qt::Orientation)     QWheelEvent(..., Qt::ScrollPhase, bool inverted) ❌
 *  QDragEnterEvent(QPoint, ...)          QDragEnterEvent(QPointF, ...)（6_16 弃用 QPoint 版）⚠
 *  QElapsedTimer::restart() → void       → qint64（qelapsedtimer.h 37 行）           ✔ 兼容
 *
 *  另外两条与「输入」间接相关、但很容易在迁移时挨的打：
 *   · Qt::DefaultTimer 在 Qt6.12 的 qnamespace.h 里**不存在**（三档 TimerType，1719 行起）。
 *   · QAbstractEventDispatcher::filterEvent()/installMessageFilter() 在 Qt6 已消失
 *     （本机 qabstracteventdispatcher.h 里查不到）；老的全局拦截代码请改用
 *     QCoreApplication::installNativeEventFilter（qcoreapplication.h 198 行），
 *     回调签名是 nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
 *     （qabstractnativeeventfilter.h 22 行）——注意最后那个 **qintptr**（Qt6 由 long 改来）。
 *
 * ——— 老师傅的话 ———
 *   徒弟，输入事件这一讲，代码量不大，但**规矩最多**。师傅给你归成三句：
 *
 *   第一句：**别信整数坐标。** position() 给的是 QPointF，因为高 DPI 下一个逻辑像素
 *   根本不是整数。你写 `int x = e->position().x()` 那一刻，就把缩放问题埋进了
 *   工程——100% 缩放下你看不见，150% 缩放下客户报「点击偏移一像素」。
 *   需要整数时才 toPoint()，而且要知道 toPoint() 是**截断**不是四舍五入。
 *
 *   第二句：**分清「谁变了」和「现在按着谁」。** button() 和 buttons() 是两个东西，
 *   写拖拽的、写右键菜单的、写「按住 Shift 多选」的，十个里有六个把它们混用过。
 *   混用的症状是「偶发」：只在同时按两键、或者鼠标在窗口外松开的时候出问题。
 *
 *   第三句：**焦点是平台给的，不是你算出来的。** setFocus() 之后有没有真的拿到焦点，
 *   取决于窗口是不是活动的、控件是不是 enabled/visible、策略让不让。所以别写
 *   「我 setFocus 了，所以我的 keyPressEvent 一定会收到」这种推理，
 *   要么判 hasFocus()，要么用 QApplication::focusChanged 这个**信号**去确认。
 *   这也是「事件 vs 信号」的又一次现场：焦点**变化**这个事实，Qt 用信号通知你，
 *   而焦点**过程**用事件（FocusIn/FocusOut）交代你。
 *
 * ——— 小练习 ———
 *   1. 把 p1 的 setMouseTracking 关掉，用 synthMouse 单独投一个 QEvent::MouseMove，
 *      验证 moveCount 不涨；再打开 setMouseTracking(true) 复现它涨。
 *      由此说明「悬停类交互（比如高亮当前行）为什么必须先开鼠标追踪」。
 *   2. 给 InputPanel 加 `void keyPressEvent` 的「按住不放」防护：打印 isAutoRepeat()
 *      与 count()，并写一个「只接受首次 press」的分支。然后用代码连投 5 次
 *      KeyPress(Qt::Key_A) 且 autorep=true，确认你的分支只放过第一次。
 *   3. 用 QWidget::setTabOrder 造一条 p1→p2→window 之外的链（自己再加一个面板），
 *      投三次 Tab 打印焦点走位；然后**删掉 setTabOrder**，只靠创建顺序，
 *      比较两次走位差别，亲手验证「tab 顺序 = 显式 setTabOrder 优先，否则按父子树顺序」。
 */
// ============================================================================
