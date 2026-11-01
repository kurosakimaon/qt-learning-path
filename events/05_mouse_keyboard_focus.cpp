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
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；-Wall -Wextra 零警告）。
//   · 本讲**有汇总自检行**（结尾 自检 N PASS / M FAIL / K SKIP，失败时退出码 1）：
//     依赖「窗口是否活动」的那几条在窗口没激活时记 SKIP 而不是 FAIL，
//     因为焦点投递与快捷键激活都要看活动窗口判定（qwidget.cpp 6581、qapplication.h 81 行）。
//   · **一条最容易踩的坑先说**：焦点策略只约束「Qt 自己决定焦点」的三条路
//     （Tab 链 / 鼠标点 / 滚轮），它**不约束**你手写的 setFocus()；
//     而且合成的鼠标事件不是 spontaneous 的，压根走不到「按策略给焦点」那段代码。
//     所以「设了 ClickFocus 但代码点不出来焦点」既不是 bug，也不代表策略写错了。
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
#include <QStyleHints>
#include <QGuiApplication>
#include <QTimer>
#include <QDebug>
#include <QString>

// 自检账本（文件作用域：各个 singleShot 回调都要往里写，跨回调状态一律不上栈）
struct Ledger { int pass = 0, fail = 0, skip = 0; };
static Ledger gLedger;

static void check(const char *what, bool ok, bool applicable = true)
{
    const QString line = QString::fromUtf8(what);
    if (!applicable) {
        ++gLedger.skip;
        qInfo().noquote() << QStringLiteral("  [SKIP] %1（这条依赖活动窗口，本机这次不成立就跳过）").arg(line);
        return;
    }
    if (ok) { ++gLedger.pass; qInfo().noquote() << QStringLiteral("  [PASS] %1").arg(line); }
    else    { ++gLedger.fail; qInfo().noquote() << QStringLiteral("  [FAIL] %1").arg(line); }
}

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
//        · 未弃用的构造（第 207 行起，207~218 是三个重载）：
//            QMouseEvent(Type, const QPointF &localPos, const QPointF &globalPos,
//                        Qt::MouseButton, Qt::MouseButtons, Qt::KeyboardModifiers,
//                        const QPointingDevice *device = QPointingDevice::primaryPointingDevice());
//            QMouseEvent(Type, const QPointF &localPos, const QPointF &scenePos, const QPointF &globalPos,
//                        Qt::MouseButton, Qt::MouseButtons, Qt::KeyboardModifiers, ...);
//        · 第 221 行  inline QPoint pos() const { return position().toPoint(); }
//            —— 注意它被 #ifndef QT_NO_INTEGER_EVENT_COORDINATES 包着：
//               定义了那个宏的构建里**没有 pos()/globalPos()/x()/y()**，只有 QPointF 版本。
//        · localPos()/windowPos()/screenPos()/globalPos()/x()/y() 全部
//          QT_DEPRECATED_VERSION_X_6_0（Qt6 里仍可用但会报警告，迁移请换成三个 position()）
//        · 第 243~244 行  Qt::MouseEventSource source() const; Qt::MouseEventFlags flags() const;
//   第 282 行  class QWheelEvent : public QSinglePointEvent
//        · 第 293 行 enum { DefaultDeltasPerStep = 120 };
//        · 第 295~298 行 QWheelEvent(const QPointF &pos, const QPointF &globalPos,
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
//        · 第 436 行 int key() / 440 modifiers() / 445 text() / 446 isAutoRepeat() / 447 count()
//        · 第 441~444 行 QKeyCombination keyCombination()（就地内联：QKeyCombination(modifiers(), Qt::Key(m_key))）
//        · 第 438 行 bool matches(QKeySequence::StandardKey) const（被 QT_CONFIG(shortcut) 包着）
//   QtCore/qnamespace.h 第 1926~1980 行 class QKeyCombination
//        ← **它不在 qkeycombination.h**（QtGui/QKeyCombination 只是转发头，实现在 QtCore 的 qnamespace.h）
//        · 1935/1939 两个构造（(Modifiers,Key) 与 (KeyboardModifiers,Key)）、
//          1943 keyboardModifiers()（**不叫 modifiers()**）、1948 key()、
//          1953 fromCombined() / 1960 toCombined()、
//          1965~1971 operator int() 已 QT_DEPRECATED_VERSION_X(6,0)、1972 operator< 被 = delete
//          → Qt6 里比较组合键请用 QKeyCombination 的 ==，别再用裸 int 位或
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

    // 最近一次事件的字段快照：自检行要拿它们和「你传进去的值」对比，
    // 你在 GUI 里跑的时候也可以直接在调试器里看这几个成员。
    QPointF lastPressLocal, lastPressGlobal;
    Qt::MouseButton lastButton = Qt::NoButton;
    Qt::MouseButtons lastButtons = Qt::NoButton;
    QPoint lastAngle, lastPixel;
    bool lastHasPixelDelta = false;
    int lastKey = 0;
    QString lastText;
    Qt::KeyboardModifiers lastMods = Qt::NoModifier;
    bool lastAutoRepeat = false;

    explicit InputPanel(const QString &name, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(name);
        setFixedSize(180, 80);
        setFocusPolicy(Qt::StrongFocus);   // 核对：qnamespace.h 118 行；qwidget.h 435 行
    }

    // focusNextPrevChild 在 qwidget.h 725 行是 **protected**，这里开一个公开壳，
    // 好在没有真键盘、也没活动窗口的环境里单独验证 Tab 链算法本身。
    bool tryNextChild(bool next = true) { return focusNextPrevChild(next); }

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
        lastPressLocal = e->position();
        lastPressGlobal = e->globalPosition();
        lastButton = e->button();
        lastButtons = e->buttons();
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
        lastAngle = e->angleDelta();
        lastPixel = e->pixelDelta();
        lastHasPixelDelta = e->hasPixelDelta();
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
        lastKey = e->key();
        lastText = e->text();
        lastMods = e->modifiers();
        lastAutoRepeat = e->isAutoRepeat();
        // Qt6 的 keyCombination() 把 (modifiers, key) 合成一个**可直接比较**的值：
        //   访问器在 qevent.h 441~444（就地内联返回 QKeyCombination(modifiers(), Qt::Key(m_key))），
        //   类本体在 **QtCore/qnamespace.h 1926~1980**（#include <QKeyCombination> 只是 QtGui 的转发头）。
        //   ⚠ 取修饰键的方法叫 **keyboardModifiers()**（1943 行），不叫 modifiers()；
        //      key() 在 1948 行；和 int 互换的那条 operator int() 已经 QT_DEPRECATED_VERSION_X(6,0)（1966 行）。
        const QKeyCombination kc = e->keyCombination();
        log(QStringLiteral("keyPressEvent key=0x%1 text=\"%2\" modifiers=0x%3 autoRepeat=%4 count=%5"
                           " | keyCombination: key=0x%6 mods=0x%7 toCombined=0x%8 等于 Ctrl+A? %9")
                .arg(e->key(), 0, 16)
                .arg(e->text().toHtmlEscaped())
                .arg(int(e->modifiers()), 0, 16)
                .arg(e->isAutoRepeat() ? QStringLiteral("true") : QStringLiteral("false"))
                .arg(int(e->count()))
                .arg(int(kc.key()), 0, 16)
                .arg(int(kc.keyboardModifiers()), 0, 16)
                .arg(kc.toCombined(), 0, 16)
                .arg(kc == QKeyCombination(Qt::ControlModifier, Qt::Key_A)
                         ? QStringLiteral("是") : QStringLiteral("否")));
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
        // 核对：qevent.h 480 行 Qt::FocusReason reason() const（471 行是 class QFocusEvent 那一行）
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
    // Qt6 的 phase 与 inverted 是**必填**（核对 qevent.h 295~298 行）
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

    // 两只面板都挂在 window 下面：上一版它们是**顶层窗口**，于是演示⑥里
    // QWidget::setTabOrder 直接打印「'first' and 'second' must be in the same window」并拒绝建立顺序，
    // Tab 链演示整个失效。同窗口是 setTabOrder / focusNextPrevChild 的前提（qwidget.cpp 7032~7033 也在这条路上做校验）。
    auto *p1 = new InputPanel(QStringLiteral("面板1"), &window);
    p1->move(20, 20);
    auto *p2 = new InputPanel(QStringLiteral("面板2"), &window);
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
        const int b0 = p1->pressCount, m0 = p1->moveCount, r0 = p1->releaseCount;
        synthMouse(p1, QEvent::MouseButtonPress, QPointF(30, 20), Qt::LeftButton, Qt::LeftButton);
        synthMouse(p1, QEvent::MouseMove, QPointF(60, 40), Qt::NoButton, Qt::LeftButton);
        synthMouse(p1, QEvent::MouseButtonRelease, QPointF(60, 40), Qt::LeftButton, Qt::NoButton);
        qInfo() << "演示① 计数：press" << p1->pressCount << "move" << p1->moveCount
                << "release" << p1->releaseCount;
        // 【鼠标移动的前提】没按下时 Qt 只把移动事件发给开了 setMouseTracking(true)
        // 的控件（qwidget.h 336 行）。这里移动发生在按下之后，所以一定到；
        // 试着把 p1->setMouseTracking(false) 后单独投一次 Move，计数就不会涨。
        check("演示① 一发按下+移动+松开 → 三个 handler 各进 1 次",
              p1->pressCount - b0 == 1 && p1->moveCount - m0 == 1 && p1->releaseCount - r0 == 1);
        // 坐标契约：position() 是你传的**局部**坐标；globalPosition() 必须等于 mapToGlobal(局部)
        const QPointF expectGlobal = QPointF(p1->mapToGlobal(QPoint(30, 20)));
        qInfo().noquote() << QStringLiteral("  坐标契约：position()=(%1,%2) globalPosition()=(%3,%4) "
                                           "mapToGlobal(30,20)=(%5,%6)")
            .arg(p1->lastPressLocal.x()).arg(p1->lastPressLocal.y())
            .arg(p1->lastPressGlobal.x()).arg(p1->lastPressGlobal.y())
            .arg(expectGlobal.x()).arg(expectGlobal.y());
        check("演示① position() 原样是局部坐标，globalPosition() == mapToGlobal(局部)",
              p1->lastPressLocal == QPointF(30, 20) && p1->lastPressGlobal == expectGlobal);
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
                << "旧名在 QDropEvent 里仍留着但标了 QT_DEPRECATED_VERSION_X_6_0，qevent.h 723~725 行）";
        // 第二发按下：变化的是右键，但左右都还按着 → button=0x2、buttons=0x3
        qInfo().noquote() << QStringLiteral("  最后那发按下实测：button=0x%1 buttons=0x%2")
            .arg(int(p1->lastButton), 0, 16).arg(int(p1->lastButtons), 0, 16);
        check("演示② 左+右并按时 button()=右键(0x2)、buttons()=0x3（变化 vs 状态）",
              p1->lastButton == Qt::RightButton && p1->lastButtons == (Qt::LeftButton | Qt::RightButton));
    });

    // ── 演示 ③ 双击时序
    QTimer::singleShot(1300, [p1] {
        qInfo() << "\n=== 演示③ 双击的先后次序 ===";
        p1->pressCount = 0; p1->releaseCount = 0; p1->dblCount = 0;
        // 窗口系统实际送来的顺序（Windows）：Press → Release → **DoubleClick** → Release
        // Qt 把「第二次按下」直接改写成 MouseButtonDblClick，所以**系统层**只给了一个 Press。
        synthMouse(p1, QEvent::MouseButtonPress, QPointF(30, 20), Qt::LeftButton, Qt::LeftButton);
        synthMouse(p1, QEvent::MouseButtonRelease, QPointF(30, 20), Qt::LeftButton, Qt::NoButton);
        synthMouse(p1, QEvent::MouseButtonDblClick, QPointF(30, 20), Qt::LeftButton, Qt::LeftButton);
        synthMouse(p1, QEvent::MouseButtonRelease, QPointF(30, 20), Qt::LeftButton, Qt::NoButton);
        // 本机实测 press = 2，不是 1。原因不在事件分发（qwidget.cpp 8985~8995 把
        // MouseButtonPress 和 MouseButtonDblClick 分成两个 case，各调各的 handler），
        // 而在**默认实现**：QWidget::mouseDoubleClickEvent 的函数体只有一行
        //     mousePressEvent(event);            （qwidget.cpp 9573~9576）
        // 本面板的 mouseDoubleClickEvent 里数完 dblCount 之后调了 QWidget::mouseDoubleClickEvent(e)，
        // 于是**同一个 type=4 的事件又进了一次 mousePressEvent** → pressCount 变成 2。
        // 日志里那条「mousePressEvent type=4」就是证据：进来的根本不是 Press(2)，是 DblClick(4)。
        qInfo() << "演示③ 结果：press" << p1->pressCount << "次、dbl" << p1->dblCount
                << "次、release" << p1->releaseCount
                << "次 → 实测 2/1/2，不是 1/1/2。"
                << "**只要你在 mouseDoubleClickEvent 里调了基类实现，mousePressEvent 就会被再走一遍**；"
                << "这条默认实现的意义是「没重写双击的控件，双击时仍会像按下一样响应」。"
                << "所以不调基类的代价，是让双击不再等价于一次按下（多数控件正是靠这条拿到按压反馈）。"
                << "双击判定窗口由系统给出，Qt 侧的参考值是 QApplication::doubleClickInterval()"
                << "=" << QApplication::doubleClickInterval() << "ms（qapplication.h 100 行）。";
        check("演示③ 四发合成事件后 press/dbl/release = 2/1/2"
              "（基类 mouseDoubleClickEvent 会把事件再喂给 mousePressEvent）",
              p1->pressCount == 2 && p1->dblCount == 1 && p1->releaseCount == 2);
    });

    // ── 演示 ④ 滚轮：angleDelta vs pixelDelta
    QTimer::singleShot(1700, [p1] {
        qInfo() << "\n=== 演示④ 滚轮两种增量 ===";
        // 传统鼠标滚轮：angleDelta 一格 120（QWheelEvent::DefaultDeltasPerStep，qevent.h 293 行），
        // pixelDelta 在这条路径上是 (0,0)。
        synthWheel(p1, QPoint(0, 120), QPoint(0, 0));
        const QPoint a1 = p1->lastAngle, px1 = p1->lastPixel;
        const bool hp1 = p1->lastHasPixelDelta;
        // 触摸板/惯性滚动：pixelDelta 给「按像素滚动多少」，angleDelta 可能同时有值。
        synthWheel(p1, QPoint(0, 30), QPoint(0, 14));
        qInfo() << "演示④ 结论：**angleDelta 是「格 × 120」的通用值，pixelDelta 是像素值**。"
                << "写通用滚动代码请两条路都走：pixelDelta 非零就用它，否则用 angleDelta().y() / 120 "
                << "* QApplication::wheelScrollLines()（qapplication.h 107 行）。"
                << "另外 Qt6 的 QWheelEvent 构造里 phase 与 inverted 是必填参数——"
                << "Qt5 那个带 Qt::Orientation 的构造搬过来编不过。";
        qInfo().noquote() << QStringLiteral("  实测：第一发 angle=(%1,%2) pixel=(%3,%4) hasPixelDelta=%5")
            .arg(a1.x()).arg(a1.y()).arg(px1.x()).arg(px1.y())
            .arg(hp1 ? QStringLiteral("true") : QStringLiteral("false"));
        qInfo().noquote() << QStringLiteral("        第二发 angle=(%1,%2) pixel=(%3,%4) hasPixelDelta=%5")
            .arg(p1->lastAngle.x()).arg(p1->lastAngle.y())
            .arg(p1->lastPixel.x()).arg(p1->lastPixel.y())
            .arg(p1->lastHasPixelDelta ? QStringLiteral("true") : QStringLiteral("false"));
        check("演示④ 传统滚轮那一发：angle=(0,120) 且 hasPixelDelta()=false",
              a1 == QPoint(0, 120) && px1.isNull() && !hp1
              && QWheelEvent::DefaultDeltasPerStep == 120);
        check("演示④ 触摸板那一发：pixelDelta 有值时 hasPixelDelta()=true",
              p1->lastPixel == QPoint(0, 14) && p1->lastHasPixelDelta);
    });

    // ── 演示 ⑤ 键盘：key / text / modifiers / 组合键
    QTimer::singleShot(2100, [p1] {
        qInfo() << "\n=== 演示⑤ 键盘事件字段 ===";
        p1->setFocus(Qt::MouseFocusReason);      // qwidget.h 433 行
        synthKey(p1, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
        const int kA = p1->lastKey; const QString tA = p1->lastText;
        synthKey(p1, Qt::Key_A, Qt::ControlModifier, QString());
        const int kCtrlA = p1->lastKey; const int mCtrlA = int(p1->lastMods);
        const QString tCtrlA = p1->lastText; const bool arCtrlA = p1->lastAutoRepeat;
        synthKey(p1, Qt::Key_Space, Qt::ShiftModifier, QStringLiteral(" "));
        // 核对：qevent.h 425 行起 class QKeyEvent；436 key()（键值，来自 Qt::Key_*）、
        //       440 modifiers()（修饰键掩码）、441~444 keyCombination()、445 text()（产出的字符）、
        //       446 isAutoRepeat()、447 count()、438 matches()（要 QT_CONFIG(shortcut)）
        qInfo() << "演示⑤ 结论：**key() 是物理/逻辑键码（Qt::Key_*），text() 是经过输入法/shift"
                << "换算之后的字符**。处理「输入了什么」用 text()，处理「按了哪个键」用 key()；"
                << "两者都为空的按键（如纯修饰键）必须防住。"
                << "长任务/输入框一律先判 isAutoRepeat()（qevent.h 446 行），否则按住键会把你打爆。";
        // Qt6 新路子：把 (modifiers, key) 合成一个可比较的 QKeyCombination，
        // 或者干脆让 Qt 判断「这是不是标准键 Copy」
        QKeyEvent cCopy(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier, QStringLiteral("C"));
        const bool isCopy = cCopy.matches(QKeySequence::Copy);
        // 与上面投给面板的那一发完全同参数的一个本地对象，用来看 keyCombination() 的算法
        const QKeyEvent evCtrlA(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier, QString());
        qInfo() << "  QKeyEvent::matches(QKeySequence::Copy)（qevent.h 438 行，受 QT_CONFIG(shortcut) 管）="
                << isCopy;
        qInfo().noquote() << QStringLiteral("  keyCombination() 拆开看：key=0x%1 mods=0x%2 toCombined=0x%3"
                                           "（无修饰的 A = 0x%4，所以 Ctrl+A ≠ A）")
            .arg(int(evCtrlA.keyCombination().key()), 0, 16)
            .arg(int(evCtrlA.keyCombination().keyboardModifiers()), 0, 16)
            .arg(evCtrlA.keyCombination().toCombined(), 0, 16)
            .arg(int(QKeyCombination(Qt::Key_A).toCombined()), 0, 16);
        check("演示⑤ Ctrl+A 那一发：key() 仍是 Key_A(0x41)、modifiers() 是 ControlModifier、"
              "text() 空（修饰键不产出字符）、isAutoRepeat()=false",
              kA == Qt::Key_A && tA == QStringLiteral("a")
              && kCtrlA == Qt::Key_A && mCtrlA == int(Qt::ControlModifier)
              && tCtrlA.isEmpty() && !arCtrlA);
        check("演示⑤ QKeyCombination（qnamespace.h 1926~1980）：能把 (Ctrl, A) 拆回 key()/keyboardModifiers()，"
              "toCombined() 可逆，且 Ctrl+A ≠ A",
              evCtrlA.keyCombination().key() == Qt::Key_A
              && evCtrlA.keyCombination().keyboardModifiers() == Qt::ControlModifier
              && QKeyCombination::fromCombined(evCtrlA.keyCombination().toCombined())
                     == evCtrlA.keyCombination()
              && evCtrlA.keyCombination() != QKeyCombination(Qt::Key_A));
        check("演示⑤ matches() 认出标准键 Copy", isCopy);
    });

    // ── 演示 ⑥ 焦点策略五档 + tab 顺序
    QTimer::singleShot(2500, [p1, p2, &window, &focusChanges] {
        qInfo() << "\n=== 演示⑥ FocusPolicy 五档与 tab 链 ===";
        // 核对：qnamespace.h 118~124 行
        //   NoFocus=0 / TabFocus=0x1 / ClickFocus=0x2
        //   StrongFocus = TabFocus|ClickFocus|0x8 = 0xb
        //   WheelFocus  = StrongFocus|0x4        = 0xf   ← 其实是**五档**，不是四档
        //
        // 策略到底在哪儿被读（本机从源码核到的两处，都是「**需要的位必须全中**」）：
        //   A. 鼠标/滚轮给焦点：QApplicationPrivate::giveFocusAccordingToFocusPolicy
        //      （qapplication.cpp 3620~3649）——点击要求 ClickFocus(0x2)，
        //      滚轮要求 WheelFocus(0xf) 全位；判定在 shouldSetFocus（同文件 3676~3687）。
        //      ⚠ 鼠标那条**只在 e->spontaneous() 时才调用**（qapplication.cpp 2719~2721），
        //        所以本讲用 sendEvent 合成的点击**永远不会**触发 ClickFocus——
        //        这不是 bug，是「合成事件不算系统送来的」的直接后果（QEvent::spontaneous()）。
        //        下面「p2 被合成点击」那一步就是这条的证据。
        //   B. Tab 链：QApplicationPrivate::focusNextPrevChild_helper（qapplication.cpp 1982 行）
        //          focus_flag = qt_tab_all_widgets() ? Qt::TabFocus : Qt::StrongFocus
        //      而 qt_tab_all_widgets() 就是
        //      styleHints()->tabFocusBehavior() == Qt::TabFocusAllControls（qapplication.cpp 345~348）。
        //      2004~2005 再按「全位包含」筛。→ **门槛是平台给的，别背死答案**：
        //      门槛 = TabFocus(0x1) 时，TabFocus/StrongFocus/WheelFocus 三档可达；
        //      门槛 = StrongFocus(0xb) 时，只有 StrongFocus/WheelFocus 可达
        //      ——单看 TabFocus(0x1) 缺 0x8 那位，反而进不去。
        //      本机实测的 tabFocusBehavior 值与两种门槛的对照，见下面「平台前提」和「反证」两段。
        struct Row { const char *name; Qt::FocusPolicy p; };
        const Row rows[] = {
            { "NoFocus", Qt::NoFocus }, { "TabFocus", Qt::TabFocus },
            { "ClickFocus", Qt::ClickFocus }, { "StrongFocus", Qt::StrongFocus },
            { "WheelFocus", Qt::WheelFocus }
        };
        // 平台前提一：hasFocus 只有在**窗口是活动的**时才可能为 true。
        // QWidget::setFocus 里那一行 if (f->isActiveWindow())（qwidget.cpp 6581）不成立就整段跳过；
        // 官方文档原话在 qwidget.cpp 6710~6715（"…if this widget or one of its parents is the active window"）。
        window.activateWindow();                  // qwidget.h 430 行
        // 平台前提二：Tab 链用的门槛
        const Qt::TabFocusBehavior beh = QGuiApplication::styleHints()->tabFocusBehavior(); // qstylehints.h 102
        const Qt::FocusPolicy tabGate = (beh == Qt::TabFocusAllControls) ? Qt::TabFocus : Qt::StrongFocus;
        bool noFocusTookFocus = false;                    // 下面循环里填，交给自检
        qInfo().noquote() << QStringLiteral("  平台前提：window.isActiveWindow()=%1"
                                           " | QApplication::activeWindow()=%2"
                                           " | tabFocusBehavior=0x%3 → Tab 门槛=0x%4")
                .arg(window.isActiveWindow() ? QStringLiteral("true") : QStringLiteral("false"))
                .arg(QApplication::activeWindow()
                        ? QApplication::activeWindow()->objectName() : QStringLiteral("<无>"))
                .arg(int(beh), 0, 16).arg(int(tabGate), 0, 16);
        qInfo() << "      （枚举见 qnamespace.h 126~131 行。上一版两只面板是**各自顶层的窗口**，"
                << "到演示⑥时早就不是活动窗口，所以那一版 hasFocus 五档全 false——"
                << "那是「演示搭错了」，不是「五档没区别」。现在面板挂在 window 下、并显式 activateWindow()）";
        for (const Row &r : rows) {
            p1->setFocusPolicy(r.p);
            p1->clearFocus();
            p1->setFocus(Qt::TabFocusReason);
            const Qt::FocusPolicy back = p1->focusPolicy();   // qwidget.h 434 行：读回来证明设进去了
            if (r.p == Qt::NoFocus)
                noFocusTookFocus = p1->hasFocus();            // 交给下面的自检
            qInfo().noquote() << QStringLiteral("   %1  读回=0x%2  Tab 链可达=%3  点击给焦点=%4  滚轮给焦点=%5  hasFocus=%6")
                .arg(QString::fromLatin1(r.name).leftJustified(11))
                .arg(int(back), 0, 16)
                .arg((back & tabGate) == tabGate ? QStringLiteral("是") : QStringLiteral("否"))
                .arg((back & Qt::ClickFocus) == Qt::ClickFocus ? QStringLiteral("是") : QStringLiteral("否"))
                .arg((back & Qt::WheelFocus) == Qt::WheelFocus ? QStringLiteral("是") : QStringLiteral("否"))
                .arg(p1->hasFocus() ? QStringLiteral("true") : QStringLiteral("false"));
        }
        qInfo().noquote() << QStringLiteral("  读法①：Tab 那一列是拿「枚举位 + 平台门槛 0x%1」现算的，不是背下来的。%2")
            .arg(int(tabGate), 0, 16)
            .arg(tabGate == Qt::TabFocus
                     ? QStringLiteral("本机门槛是 TabFocus(0x1)——offscreen 报的是 TabFocusAllControls，"
                                      "所以 TabFocus/StrongFocus/WheelFocus 三档可达。")
                     : QStringLiteral("本机门槛是 StrongFocus(0xb)，所以只有 StrongFocus/WheelFocus 可达"
                                      "（TabFocus 缺 0x8 那位）。"));
        qInfo() << "  读法①b：点击那一列只看 ClickFocus 位、滚轮那一列要 WheelFocus 全位——"
                << "这两条与平台无关。读法②：hasFocus 这一列本机**五档全 true（实测）**，包括 NoFocus——"
                << "因为 QWidget::setFocus 从头到尾没查过 focusPolicy：它只查 isEnabled()（qwidget.cpp 6546）"
                << "和 isActiveWindow()（6581）。这正是 Qt::NoFocus 文档里那句"
                << "「除了程序调用之外不接受焦点」的意思：**策略拦的是用户操作，拦不住你手写 setFocus()**。"
                << "想验证策略本身，别用 hasFocus 当尺子，要么真点一次鼠标（spontaneous），要么查那两位。";

        // 反证：门槛是平台提示给的，那就当场把它换一档，看可达性跟着变。
        // setTabFocusBehavior 是公共 API（qstylehints.h 103 行），改完记得复原。
        QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusTextControls);
        const Qt::FocusPolicy otherGate =
            (QGuiApplication::styleHints()->tabFocusBehavior() == Qt::TabFocusAllControls)
                ? Qt::TabFocus : Qt::StrongFocus;
        QString table;
        for (const Row &r : rows)
            table += QStringLiteral(" %1=%2")
                .arg(QString::fromLatin1(r.name))
                .arg(((r.p & otherGate) == otherGate) ? QStringLiteral("可达") : QStringLiteral("不可达"));
        qInfo().noquote() << QStringLiteral("  反证：tabFocusBehavior 改成 TabFocusTextControls(0x01)"
                                           "→ 门槛变 0x%1 →%2")
            .arg(int(otherGate), 0, 16).arg(table);
        QGuiApplication::styleHints()->setTabFocusBehavior(beh);    // 复原
        qInfo() << "  → 同一份判定代码，两种平台提示给出两种「能不能被 Tab 走到」。"
                << "写无障碍/自动化测试时这一点必须显式设，别假设默认值。";

        // 证据 A 的落地实验：给「ClickFocus 的面板2」投一次**合成**点击。
        p2->setFocusPolicy(Qt::ClickFocus);
        const int inBefore = p2->focusInCount;
        if (QWidget *cur = QApplication::focusWidget())
            cur->clearFocus();
        synthMouse(p2, QEvent::MouseButtonPress, QPointF(40, 40), Qt::LeftButton, Qt::LeftButton);
        const int clickFocusDelta = p2->focusInCount - inBefore;
        qInfo() << "   合成点击 ClickFocus 的面板2：focusIn 增量 =" << clickFocusDelta
                << "（本机实测 0）→ 不是策略写错了，是 giveFocusAccordingToFocusPolicy"
                << "只在 e->spontaneous() 的分支里被调用（qapplication.cpp 2719~2721）。"
                << "同样代码在真桌面上会 +1。";

        // tab 顺序由什么决定：**先看你有没有 setTabOrder**；
        // 没设过就是「同一父控件下、能接受 tab 的控件，按创建/加入父子树的先后」排。
        if (QWidget *cur = QApplication::focusWidget())
            cur->clearFocus();              // qapplication.h 79 行 / qwidget.h 431 行
        QWidget::setTabOrder(p1, p2);           // qwidget.h 437 行，static
        p1->setFocusPolicy(Qt::StrongFocus);
        p2->setFocusPolicy(Qt::StrongFocus);    // 上一版这里给 p2 设 TabFocus(0x1)。
                                                // 本机默认门槛恰好也是 TabFocus，所以那一版“看起来能 Tab 过去”；
                                                // 可一旦平台门槛换成 StrongFocus（上面反证那一档），TabFocus 就掉链子了。
                                                // 结论：**要能被 Tab 走到就写 StrongFocus**，别赌平台提示。
        p1->setFocus(Qt::TabFocusReason);
        qInfo() << "   当前 focusWidget =" << (QApplication::focusWidget()
                    ? QApplication::focusWidget()->objectName() : QStringLiteral("<无>"))
                << "（qapplication.h 79 行）";
        // 直接走 Tab 链的函数本体（qwidget.h 725 行，protected；面板里包了个同名公开壳）
        const bool moved = p1->tryNextChild(true);
        qInfo() << "   focusNextPrevChild(true) 返回" << moved
                << "→ 焦点现在在" << (QApplication::focusWidget()
                        ? QApplication::focusWidget()->objectName() : QStringLiteral("<无>"));
        // 用代码投 Tab 键：Qt 内部同样走 focusNextPrevChild(true)。
        // 键盘这条**不看 spontaneous**（qapplication.cpp 2661~2666 只按键值判定
        // qt_in_tab_key_event），所以合成按键一样能把焦点挪走——和上面鼠标那条正好相反。
        synthKey(&window, Qt::Key_Tab, Qt::NoModifier);
        const bool tabToP1 = QApplication::focusWidget() == p1;
        qInfo() << "   投了一次 Tab：焦点现在在" << (QApplication::focusWidget()
                    ? QApplication::focusWidget()->objectName() : QStringLiteral("<无>"))
                << "；再投 Shift+Tab 应回退（BacktabFocusReason）。";
        synthKey(&window, Qt::Key_Backtab, Qt::ShiftModifier);
        const bool backTabToP2 = QApplication::focusWidget() == p2;
        qInfo() << "   Shift+Tab 之后：" << (QApplication::focusWidget()
                    ? QApplication::focusWidget()->objectName() : QStringLiteral("<无>"));
        qInfo() << "   focusChanged 信号累计" << focusChanges << "次；面板1 focusIn"
                << p1->focusInCount << "/focusOut" << p1->focusOutCount
                << "（本机日志开头那两条的 reason=3 = Qt::ActiveWindowFocusReason，"
                << "即「窗口激活时 Qt 自动把焦点给了面板1」，qnamespace.h 1369~1373 行）。";

        // ── 本演示的自检：位判定与平台无关；焦点投递那两条要等活动窗口成立
        check("FocusPolicy 的位：StrongFocus=0xb、WheelFocus=0xf（五档，不是四档）",
              int(Qt::StrongFocus) == 0xb && int(Qt::WheelFocus) == 0xf
              && int(Qt::TabFocus) == 0x1 && int(Qt::ClickFocus) == 0x2
              && int(Qt::NoFocus) == 0x0);
        // 这条用的是**上面反证时强制设出来的那档门槛**（StrongFocus 全位），
        // 所以在任何平台上都成立；本机默认提示是 TabFocusAllControls，直接拿 tabGate 判会 SKIP。
        check("门槛=StrongFocus(0xb) 时的可达集合只有 StrongFocus/WheelFocus"
              "（qapplication.cpp 1982 + 2004~2005 的全位判定）",
              otherGate == Qt::StrongFocus
              && ((Qt::StrongFocus & otherGate) == otherGate)
              && ((Qt::WheelFocus & otherGate) == otherGate)
              && ((Qt::TabFocus & otherGate) != otherGate)
              && ((Qt::ClickFocus & otherGate) != otherGate)
              && ((Qt::NoFocus & otherGate) != otherGate));
        check("合成的（非 spontaneous）鼠标按下不会按 ClickFocus 给焦点",
              clickFocusDelta == 0);
        check("同窗口 + StrongFocus 的控件之间，focusNextPrevChild(true) 能把焦点挪到下一个",
              moved && QApplication::focusWidget() == p2, window.isActiveWindow());
        check("策略拦的是用户操作：Qt::NoFocus 的控件被手写 setFocus() 依然能拿到焦点",
              noFocusTookFocus, window.isActiveWindow());
        check("Tab/Backtab 键在合成（非 spontaneous）按键下照样走 Tab 链",
              tabToP1 && backTabToP2, window.isActiveWindow());
    });

    // ── 演示 ⑦ QShortcut + QKeySequence（含标准键）
    QTimer::singleShot(2900, [&window, p1] {
        qInfo() << "\n=== 演示⑦ 快捷键：QShortcut 与标准键 ===";
        static int fired = 0;
        static int copyFired = 0;

        // 写法 A：自定义组合键（核对 qshortcut.h 第 77 行的两参便利构造）
        auto *scCustom = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+J")), &window);
        scCustom->setContext(Qt::WindowShortcut);        // qshortcut.h 169 行；qnamespace.h 1322 行
        QObject::connect(scCustom, &QShortcut::activated, [p1] {
            ++fired;
            p1->log(QStringLiteral("QShortcut(Ctrl+Alt+J) activated"));
        });

        // 写法 B：标准键（qkeysequence.h 第 50 行 Copy 确实存在）
        auto *scCopy = new QShortcut(QKeySequence::Copy, &window);
        QObject::connect(scCopy, &QShortcut::activated, [] { ++copyFired; });

        qInfo() << "  自定义键的 QKeySequence 文本 =" << scCustom->key().toString()
                << "标准键 Copy 的文本 =" << scCopy->key().toString()
                << "（标准键在不同平台/风格下解析成不同实际按键，别硬写 \"Ctrl+C\"）";

        // 用合成按键触发快捷键。⚠ 上一版这里是 0，而且把 0 归因成「offscreen 不配合」——
        // 真正的原因是**那时没人激活过窗口**：演示⑥补上 activateWindow() 之后同一份代码变成 1。
        // 另外 Qt 对非 spontaneous 的 KeyPress 会**先补发一条 ShortcutOverride**
        // （qapplication.cpp 2651~2659，注释原话是「enables emulation/playback of recorded
        // events to still have the same effect」）→ 合成按键本来就是能打通快捷键的。
        synthKey(&window, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
        qInfo() << "  合成 Ctrl+Alt+J 之后 activated 计数 =" << fired
                << "| 活动窗口 =" << (QApplication::activeWindow()
                        ? QApplication::activeWindow()->objectName() : QStringLiteral("<无>"));
        // 标准键那条也真发一次按键（上一版只打印 copyFired，却没投过 Ctrl+C，0 是必然的）
        synthKey(&window, Qt::Key_C, Qt::ControlModifier);
        qInfo() << "  合成 Ctrl+C（标准键 Copy 解析出来的组合）之后 copyFired =" << copyFired
                << "→ 快捷键要单测，就照这段：activateWindow() + 投对应的按键。"
                << "窗口没被激活时它确实是 0（WindowShortcut 要求「本窗口是活动窗口」，"
                << "qapplication.h 81 行），那种 0 是前提没满足，不是代码错。";
        check("窗口激活后合成 Ctrl+Alt+J 触发自定义 QShortcut", fired == 1, window.isActiveWindow());
        check("标准键 QKeySequence::Copy 合成 Ctrl+C 触发", copyFired == 1, window.isActiveWindow());
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
                << "输入字符看 text()、按键看 key()，焦点行为看 FocusPolicy 与 tab 链——"
                << "但记住策略只管「Qt 替你决定焦点」的三条路，合成事件与手写 setFocus 都不在它管辖内。**";
        qInfo() << "自检：" << gLedger.pass << "PASS /" << gLedger.fail << "FAIL /"
                << gLedger.skip << "SKIP（SKIP 只可能是「窗口没被激活」那几条前提）";
        // 失败把退出码带成 1，ctest 才会真的拦住「结论和输出对不上」
        QCoreApplication::exit(gLedger.fail == 0 ? 0 : 1);
    });

    return app.exec();
}

// ============================================================================
// 附：Qt5 → Qt6 输入事件对照表（本讲核对到的，逐条给你）
// ============================================================================
/*
 *  Qt5 写法                              Qt6 对应（本机 qevent.h 行号）             状态
 *  ------------------------------------------------------------------------------
 *  e->pos()          → QPoint            e->position().toPoint()（221 行）      pos() 仍在，
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
