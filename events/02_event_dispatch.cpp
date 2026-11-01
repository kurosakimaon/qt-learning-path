// ============================================================================
// 第 2 讲 · 一次事件的三层传递
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   把一个事件从「Qt 收到」到「你的函数被调到」的完整链条拆开，并且**逐层打印**
//   进入顺序，让三层从背诵变成看见：
//     层 1  QApplication::notify(receiver, event)      —— 全体事件的总入口（一个程序只有一个）
//     层 2  receiver->event(QEvent*)                   —— 这个对象的事件闸门
//     层 3  mousePressEvent / keyPressEvent / paintEvent … —— 具体 handler
//   然后再演示两件新手最容易翻车的事：
//     · 重写 event() 拦截某类事件（做「统一改行为」的入口）
//     · **不调用基类实现**会发生什么：paintEvent 不再被叫到、事件不再往父控件抛、
//       样式/焦点/父子协作行为悄悄丢失。
//
// 对应构建目标名：events_02_event_dispatch
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(events_02_event_dispatch 02_event_dispatch.cpp)
//   target_link_libraries(events_02_event_dispatch PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)   # 本讲示例不定义 Q_OBJECT 类（原因见下方「日志里的类名」）
//
// 运行前须知：
//   · 无鼠标键盘：所有输入事件都是代码构造的 QMouseEvent / QKeyEvent，
//     再用 QCoreApplication::sendEvent() 投给控件（第 1 讲的手法）。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 events_02_event_dispatch，跑到自动退出、退出码 0）。
//     但本讲**没有汇总自检行**，退出码 0 只证明「没崩、走完了」，不证明正文每个数字都对：
//     带具体数字的「预期」请以你自己那次运行输出为准（本机那一轮存档 runlogs/events_02_event_dispatch.log）。
//   · 本示例无人值守跑完：约 4 秒自动演示 + qInfo() 打结论 + QCoreApplication::quit()。
//   · 重绘事件（paintEvent）依赖平台插件真派发：在 -platform offscreen 下同样会收到，
//     但**次数**与窗口管理器和脏区合并有关，以你打印的数字为准。
// ============================================================================

#include <QApplication>
#include <QCoreApplication>       // sendEvent / postEvent / notify
#include <QWidget>
#include <QEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QTimer>
#include <QDebug>
#include <QString>

// ============================================================================
// 一、层 1：重写 QApplication::notify —— 整个程序的事件总闸
// ============================================================================
// 【为什么它是总入口】不管事件来自窗口系统、定时器，还是你自己 sendEvent/postEvent，
// 最终都要经过某一个 QCoreApplication 实例的 notify()。它是**唯一**的收口点，
// 所以「记录一切事件」「全局拦截一切事件」只有在这里做才真正无遗漏
// （第 3 讲的全局事件过滤器其实也是在 notify 内部被调用的，覆盖面稍窄一点）。
//
// 核对签名（本机 Qt 6.12）：
//   QtCore/qcoreapplication.h 第 65 行 public:  → 第 170 行 private:
//   该头第 120 行   virtual bool notify(QObject *, QEvent *);
//   → 也就是说在 **Qt 6.12 的头文件里 notify 处于 public 段**。
//   QtWidgets/qapplication.h  第 48 行 public: → 第 119 行   bool notify(QObject *, QEvent *) override;
//   诚实标注：Qt5 时代文档把 QCoreApplication::notify 标为 protected（本讲不据此断言
//   Qt5 头文件的具体访问段，因为本机只有 Qt6 头）。**重写（override）本身与访问级别无关**，
//   派生类里 `QApplication::notify(...)` 调基类实现，public / protected 都合法，
//   所以下面这个写法在 Qt5/Qt6 都能用；变的只是「能不能在 main 里直接调它」。
class TracerApp : public QApplication
{
public:
    // 第 1 讲里的心跳流水号，这里另起一个，专给「层序」编号
    int notifyCount = 0;

    TracerApp(int &argc, char **argv) : QApplication(argc, argv) {}

    bool notify(QObject *receiver, QEvent *event) override
    {
        ++notifyCount;

        // 「日志里的类名」小知识：下面 className() 打出来可能是 "QWidget" 而不是 "Opaque"——
        // 因为我们的演示控件**故意没写 Q_OBJECT**，metaObject() 返回的是最近一个
        // 带 Q_OBJECT 的祖先（QWidget）的表（第贰阶段第 8 讲讲过这件事）。
        // 所以真正可靠的标识是 objectName()，它在构造函数里就能设，与元对象表无关。
        const QString who = receiver ? receiver->objectName() : QStringLiteral("<null>");
        const char *cls = receiver ? receiver->metaObject()->className() : "nullptr";

        // 核对：qcoreevent.h 第 307 行 type()、第 308 行 spontaneous()、第 311 行 isAccepted()
        qInfo().noquote() << QStringLiteral("层1 notify → 接收者=%1(元对象类=%2) type=%3 spontaneous=%4 isAccepted=%5")
                                 .arg(who, QLatin1String(cls))
                                 .arg(int(event->type()))
                                 .arg(event->spontaneous() ? "true(来自窗口系统)" : "false(程序投递)")
                                 .arg(event->isAccepted() ? "true" : "false");

        const bool handled = QApplication::notify(receiver, event);   // ← 交给基类：层 2/3 在这里发生

        qInfo().noquote() << QStringLiteral("层1 notify ← 返回 %1（%2）｜同一个事件此刻 isAccepted=%3")
                                 .arg(handled ? "true" : "false")
                                 .arg(handled ? "Qt 认为这个事件被处理了" : "没人要这个事件")
                                 .arg(event->isAccepted() ? "true" : "false");
        return handled;
    }
};

// ============================================================================
// 二、层 2：重写 receiver->event()，看见闸门与分派
// ============================================================================
// 核对：QtWidgets/qwidget.h 第 658 行 protected: → 第 660 行 bool event(QEvent *event) override;
//       QtCore/qobject.h  第 113 行 public: → 第 117 行 virtual bool event(QEvent *event);
// 也就是说：QWidget 把 event() 放进了 protected 段，QObject 放的是 public。
// 所以在 widget 子类里你通常写 `protected: bool event(QEvent *e) override;`
// （写成 public 也合法——C++ 允许派生类**放宽**访问级别，但不许收紧）。
//
// event() 的职责：按 type 把事件**分派**给具体 handler（层 3），然后返回
// 「我处理了吗」。绝大多数时候你只要老老实实 `return QWidget::event(e);`。
class Gate : public QWidget
{
public:
    int gateEntries = 0;      // 层 2 进了多少次
    int paintCount = 0;       // 层 3 paintEvent 被叫到多少次
    int mouseCount = 0;       // 层 3 mousePressEvent 被叫到多少次

    explicit Gate(const QString &name)
    {
        setObjectName(name);
        setFixedSize(160, 90);
        // 核对：qwidget.h 第 291 行 setFixedSize(const QSize &)、第 503 行 resize(int,int)
    }

protected:
    bool event(QEvent *e) override
    {
        ++gateEntries;
        qInfo().noquote() << QStringLiteral("  层2 %1::event() 第 %2 次进入，type=%3")
                                 .arg(objectName()).arg(gateEntries).arg(int(e->type()));
        return QWidget::event(e);        // ← 交给基类，才能走到层 3
    }

    // 核对：qwidget.h 层 3 的这批 virtual 全在 protected 段（658 行起）
    //   661 mousePressEvent(QMouseEvent*)   662 mouseReleaseEvent   663 mouseDoubleClickEvent
    //   664 mouseMoveEvent  666 wheelEvent  668 keyPressEvent  669 keyReleaseEvent
    //   670 focusInEvent    671 focusOutEvent  672 enterEvent(QEnterEvent*)  674 paintEvent
    //   689 dragEnterEvent  692 dropEvent      695 showEvent      696 hideEvent
    void mousePressEvent(QMouseEvent *e) override
    {
        ++mouseCount;
        qInfo().noquote() << QStringLiteral("    层3 %1::mousePressEvent 被调用").arg(objectName());
        QWidget::mousePressEvent(e);      // ← 正常做法：留给基类收尾（并允许继续往父控件抛）
    }

    void paintEvent(QPaintEvent *e) override
    {
        ++paintCount;
        // 核对：QtGui/qevent.h 第 487 行 class QPaintEvent；其内 8/9 行处
        //   inline const QRect &rect() const / inline const QRegion &region() const
        qInfo().noquote() << QStringLiteral("    层3 %1::paintEvent 被调用，脏区 rect=%2")
                                 .arg(objectName()).arg(e->rect().width())
                          << "x" << e->rect().height();
        QPainter p(this);
        p.setPen(Qt::black);
        p.drawText(rect(), Qt::AlignCenter, objectName());   // 画个名字，肉眼能分辨三块面板
    }
};

// ============================================================================
// 三、反面教材 A：event() 里不传给基类
// ============================================================================
class GateKeeps : public Gate
{
public:
    explicit GateKeeps(const QString &name) : Gate(name) {}

protected:
    bool event(QEvent *e) override
    {
        ++gateEntries;
        if (e->type() == QEvent::Paint) {
            // 拦住重绘：直接返回 true，**不调 QWidget::event(e)**
            qInfo() << "  层2" << objectName() << "::event() 截下 Paint，没交给基类 → 层3 不会发生";
            return true;
        }
        return QWidget::event(e);
    }
};

// ============================================================================
// 四、反面教材 B：具体 handler 里不传给基类（父控件收不到事件）
// ============================================================================
// 【被吞掉的那一步】文档语义：QWidget::mousePressEvent 的**默认实现**会把事件
// 标记为 ignore（顶层窗口等例外），而 Qt 在派发链末尾会把「未被接受」的鼠标事件
// 沿 parent 链往上抛。这两段逻辑都在 .cpp 实现里，**头文件层面看不到**，
// 本讲用计数器把结果测给你看，而不是让你背我。
class Opaque : public Gate
{
public:
    explicit Opaque(const QString &name) : Gate(name) {}

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        ++mouseCount;
        qInfo() << "    层3" << objectName() << "::mousePressEvent 自定义处理，**不调基类**";
        Q_UNUSED(e);                            // e 在本函数里就是没用途（这正是演示点），消掉 -Wunused-parameter
        // 故意不写 QWidget::mousePressEvent(e);  → 事件保持 accepted → 不再往父控件走
    }
};

class PassThrough : public Gate
{
public:
    explicit PassThrough(const QString &name) : Gate(name) {}

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        ++mouseCount;
        qInfo() << "    层3" << objectName() << "::mousePressEvent 自定义处理后**调基类**";
        QWidget::mousePressEvent(e);      // → 基类把它 ignore → 有机会被父控件接住
    }
};

// 「父控件」：接住子控件不想要的事件
class Tray : public QWidget
{
public:
    int mouseFromChild = 0;
    explicit Tray(const QString &name) { setObjectName(name); }

protected:
    void mousePressEvent(QMouseEvent *) override
    {
        ++mouseFromChild;
        qInfo().noquote() << QStringLiteral("      -> 父控件 %1 竟然收到了鼠标事件（子控件没要它）第 %2 次")
                                .arg(objectName()).arg(mouseFromChild);
    }
};

// ============================================================================
// 五、辅助：造一个「按下左键」的合成事件
// ============================================================================
// 核对：QtGui/qevent.h 第 197 行 class QMouseEvent，可用构造（未弃用的那几个）：
//   QMouseEvent(Type type, const QPointF &localPos, const QPointF &globalPos,
//               Qt::MouseButton button, Qt::MouseButtons buttons,
//               Qt::KeyboardModifiers modifiers,
//               const QPointingDevice *device = QPointingDevice::primaryPointingDevice());
//   → 第 206 行起（本机核到的行号）。带 scenePos 的四坐标版本也在（210/215 行）。
//   Qt5 时代那个 `(type, localPosF, button, buttons, modifiers, device)` 重载
//   在本机 6.12 已被标 QT_DEPRECATED_VERSION_X_6_4（201 行附近），别再用。
static void pressOn(QWidget *w, double x = 20, double y = 20)
{
    const QPointF local(x, y);
    const QPointF global = w->mapToGlobal(local.toPoint());
    QMouseEvent ev(QEvent::MouseButtonPress, local, global,
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    // 核对：Qt::MouseButton 见 qnamespace.h 第 68 行（LeftButton=0x1…），
    //       Qt::MouseButtons 是它的 flags（第 107 行 Q_DECLARE_FLAGS(MouseButtons, MouseButton)）
    QCoreApplication::sendEvent(w, &ev);       // 同步：返回时整条链已经跑完
}

// ============================================================================
// 六、main
// ============================================================================
int main(int argc, char *argv[])
{
    TracerApp app(argc, argv);      // ← 我们的 notify 从这里开始接管全部事件

    QWidget window;
    window.setObjectName(QStringLiteral("主窗"));
    window.setWindowTitle(QStringLiteral("第 2 讲 · 一次事件的三层传递"));
    window.resize(560, 300);
    window.show();

    // 三块面板：正常闸门 / 拦 Paint 的闸门 / 吞掉事件的 handler
    Gate normalGate(QStringLiteral("正常Gate"));
    GateKeeps keeper(QStringLiteral("截Paint"));
    Opaque opaque(QStringLiteral("不调基类"));

    // 父子演示：Tray 里放一个 PassThrough 和一个 Opaque
    Tray tray(QStringLiteral("Tray父"));
    tray.resize(200, 200);
    PassThrough *childPass = new PassThrough(QStringLiteral("调基类子"));
    childPass->setParent(&tray);
    Opaque *childOpaque = new Opaque(QStringLiteral("吞事件子"));
    childOpaque->setParent(&tray);
    childOpaque->move(0, 100);

    // ── 演示 ①：一次完整的三层链
    QTimer::singleShot(600, [&normalGate] {
        qInfo() << "\n=== 演示① 正常路径：notify → event() → mousePressEvent ===";
        normalGate.mouseCount = 0;
        pressOn(&normalGate);
        qInfo() << "演示① 结果：层2 进入" << normalGate.gateEntries
                << "次，层3 mousePressEvent" << normalGate.mouseCount << "次";
    });

    // ── 演示 ②：不传基类，paintEvent 从此消失
    QTimer::singleShot(1200, [&keeper, &normalGate] {
        qInfo() << "\n=== 演示② 在 event() 里截下 Paint，不交给基类 ===";
        keeper.show();
        normalGate.update();        // 核对：qwidget.h 第 470 行 update()（异步，排队等重绘）
        keeper.update();
        qInfo() << "演示② 说明：update() 只是投一次重绘请求；真正调用层3 要等事件循环派发。";
    });

    QTimer::singleShot(1600, [&keeper, &normalGate] {
        qInfo().noquote() << QStringLiteral("演示② 结果（此刻）：正常Gate 已重绘 %1 次，截Paint 已重绘 %2 次")
                                 .arg(normalGate.paintCount).arg(keeper.paintCount)
                          << "→ 后者明显少（预期为 0），因为 Paint 在层2 就被截了。";
    });

    // ── 演示 ③：handler 不传基类 → 父控件收不到；传基类 → 父控件收得到
    QTimer::singleShot(2000, [childPass, childOpaque, &tray] {
        qInfo() << "\n=== 演示③ 子控件是否调基类，决定父控件能不能接住事件 ===";
        tray.mouseFromChild = 0;

        pressOn(childOpaque);        // 吞掉：不调基类
        qInfo() << "给「吞事件子」按下之后：父控件收到" << tray.mouseFromChild << "次";

        pressOn(childPass);           // 传递：调基类
        qInfo() << "给「调基类子」按下之后：父控件累计收到" << tray.mouseFromChild << "次";
        qInfo() << "演示③ 结果：期望 tray 计数 =1（只有 pass 那次上抛）。"
                << "若你机器上是别的数，请把 notify 日志翻回去看 isAccepted 那一列——"
                << "「上抛」的唯一条件是事件仍处于 ignored 状态。";
    });

    // ── 演示 ④：键盘事件，看清 key()/text()/modifiers()
    QTimer::singleShot(2400, [&window, &normalGate] {
        qInfo() << "\n=== 演示④ 键盘事件穿过同一套三层链 ===";
        // 核对：QtGui/qevent.h 第 425 行 class QKeyEvent
        //   QKeyEvent(Type type, int key, Qt::KeyboardModifiers modifiers,
        //             const QString &text = QString(), bool autorep = false, quint16 count = 1);
        // Qt::Key_A / Qt::Key_Tab 等键值：qnamespace.h（Key_Tab 在第 687 行）
        // Qt::ControlModifier 等修饰键：qnamespace.h 第 1077~1089 行
        QKeyEvent ev(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier, QStringLiteral("a"));
        // 核对：QKeyEvent 成员 —— int key()（第 429 行附近）、QString text()、
        //       Qt::KeyboardModifiers modifiers()、bool isAutoRepeat()、
        //       QKeyCombination keyCombination()（Qt6 新增，把 key+modifiers 合成一个值）
        qInfo() << "合成键盘事件：key =" << ev.key()
                << "text =" << ev.text()
                << "modifiers =" << int(ev.modifiers())
                << "isAutoRepeat =" << ev.isAutoRepeat();

        QCoreApplication::sendEvent(&normalGate, &ev);
        qInfo() << "演示④ 结果：层2/层3 日志见上（Gate 没重写 keyPressEvent，"
                << "基类 QWidget::keyPressEvent 会把它 ignore，于是 Qt 交给父控件/快捷键系统）。";
    });

    // ── 结论与退出
    QTimer::singleShot(3200, [&app, &normalGate, &keeper, &tray] {
        qInfo() << "\n=== 结论 ===";
        qInfo() << "整场经过 notify 的事件总数 =" << app.notifyCount;
        qInfo() << "正常Gate：层2" << normalGate.gateEntries << "次 / 层3 paint"
                << normalGate.paintCount << "次 / 层3 mouse" << normalGate.mouseCount << "次";
        qInfo() << "截Paint ：层2" << keeper.gateEntries << "次 / 层3 paint"
                << keeper.paintCount << "次（预期 0）";
        qInfo() << "父 Tray 从子控件接到" << tray.mouseFromChild << "次";
        qInfo() << "一句话：**notify 是总闸，event() 是闸门，handler 是房间；"
                << "闸门不往基类传，房间就永远不会开。**";
        QCoreApplication::quit();
    });

    return app.exec();
}

// ============================================================================
// 附一：三层各自该干什么（照这个分工，你的事件代码就不会写歪）
// ============================================================================
/*
 * 层 1  QApplication::notify(receiver, event)
 *   · 全程序唯一收口点，看不到「哪个 handler」，只看得到 receiver 和 type。
 *   · 适合：全局事件审计/统计、调试日志、极少数框架级拦截、把事件转成信号往外广播。
 *   · 代价：**每个事件都要过你**，一慢全慢；而且它是 QApplication 的重写点，
 *     一个进程只能有一个应用对象，所以这是「独占式」的挂钩。
 *   · Qt 自己在 notify 内部还会先跑「事件过滤器」——这就是第 3 讲。
 *
 * 层 2  receiver->event(QEvent *e)
 *   · 这个对象的事件入口。默认实现就是一张 type → handler 的分派表。
 *   · 适合：一个类里统一处理很多种事件（比如所有事件都记一条流水），
 *     以及「不想为每种事件写一个 override」的场合。
 *   · 规矩：**除非明确要吞掉某类事件，否则一定 return QWidget::event(e);**
 *     你截下来就意味着 Qt 的默认行为（分派给层 3、脏区管理、焦点/父子协作、
 *     样式 polish……）全部不发生。演示② 就是这个后果的最小可复现。
 *
 * 层 3  mousePressEvent / keyPressEvent / paintEvent / …
 *   · 只关心一类事件时写这里，语义最清晰，也不用碰其他类型。
 *   · accept/ignore 是这一层的「返回值之外的第二个信号」：
 *       e->accept() / e->ignore() / e->setAccepted(bool)（qcoreevent.h 310~314 行）
 *     鼠标类事件「ignore」才有机会上抛给父控件；paint 的 accept/ignore 没有传播意义。
 *
 * ——— 老师傅的话 ———
 *   徒弟，本讲最值钱的一句话是：**事件的分层不是为了优雅，是为了「控制权交接」**。
 *   notify 把控制权交给某个对象；event() 把控制权交给某个 handler；handler 用
 *   accept/ignore 决定「这事儿我管了，还是往上传」。你每一次不调基类，都是在
 *   单方面宣布接管——而接管是有后果的：那些你根本没打算管的默认行为，一起没了。
 *
 *   所以师傅的规矩是三条：
 *   ① 重写 event() 只用来「观察」或「精确定向拦截」，收尾永远交给基类；
 *   ② 要改一个控件的行为，优先考虑第 3 讲的事件过滤器（不继承、可拆卸、可多处复用）；
 *   ③ 只有在做 QApplication 级别的框架（全局审计、埋点、兼容层）时才动 notify，
 *      而且要接受「一个进程只有这一份」的事实。
 *
 *   还有个小陷阱值得单独念叨：日志里 className() 打出来是 "QWidget" 而不是 "Opaque"。
 *   不是代码错了，是你的类没写 Q_OBJECT——元对象表是从最近一个带 Q_OBJECT 的祖先那儿
 *   继承来的（第贰阶段第 8 讲）。要靠名字识别对象，请设 objectName()。
 *   会区分「编译期类型」和「元对象类型」的人，才叫真的懂了 Qt 的对象模型。
 *
 * ——— 小练习 ———
 *   1. 给 Gate 加一个 keyPressEvent override，**不调基类**，然后重跑演示④：
 *      观察父控件（这里 window 没重写 handler，所以你得再套一层 Tray 才能看见差别）
 *      与「快捷键系统」的行为差别，并解释为什么按钮的 Tab 键会因此失灵。
 *   2. 把演示③ 的 `pressOn(childOpaque)` 之前先加一行
 *      `childOpaque->setAttribute(Qt::WA_TransparentForMouseEvents)`
 *      （qnamespace.h 里的 WidgetAttribute），再按一次：父控件的计数变成几？
 *      这条能帮你把「属性层」和「handler 层」两种拦截分清。
 *   3. 把 TracerApp::notify 的日志按事件 type 分类计数（QHash<int,int>），
 *      结束时打印一张「本次运行各类事件出现次数」的表；再和 QEvent::Type 的
 *      枚举名（qcoreevent.h 第 56~302 行）对一对，认识一下 Paint/Timer/Show/Polish
 *      这些你从没主动发过却一直存在的事件。
 */
// ============================================================================
