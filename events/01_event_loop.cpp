// ============================================================================
// 第 1 讲 · 事件循环解剖
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   把 app.exec() 这个「一行代码」拆开，看清 Qt 程序的心脏到底怎么跳：
//     1) exec() 里到底发生了什么（取事件 → 派发 → 再取，直到有人叫停）；
//     2) QEventLoop 是**可以嵌套**的——为什么说「在槽里再开一个模态循环」
//        既是卡顿的根源，也是重入（re-entrancy）的根源；
//     3) processEvents() 什么时候能用来「救急」，什么时候是往火里浇油；
//     4) quit() / exit(int) / 从槽里 return 三者完全不同的后果；
//     5) sendEvent（同步）与 postEvent（异步入队）的时序差别——用两条路径
//        的**实际到达顺序**当证据，而不是靠背结论。
//
// 对应构建目标名：events_01_event_loop
//   CMake 里大致是：
//     find_package(Qt6 REQUIRED COMPONENTS Widgets)
//     add_executable(events_01_event_loop 01_event_loop.cpp)
//     target_link_libraries(events_01_event_loop PRIVATE Qt6::Widgets)
//     set(CMAKE_AUTOMOC ON)   # 本讲示例没有 Q_OBJECT 类，但工程里一直保持开着
//
// 运行前须知：
//   · 依赖 Qt6::Widgets（QApplication + 一个 QWidget 当演示舞台）。
//   · 无鼠标键盘参与：所有事件都是代码**手工构造**后用
//     QCoreApplication::sendEvent() / postEvent() 投递的——这是本模块的核心技巧。
//   · 本机当前无法编译链接 Qt 运行时，本示例**未在本机实测编译**；
//     文中每一个类、函数、枚举都对着 C:\msys64\ucrt64\include\qt6 里的真实头文件核对过。
//   · 本示例无人值守跑完：约 3.4 秒自动演示 → qInfo() 打印关键结论 → exit(硬账退出码)。
//     到达次数、重入深度、flag 过滤这些「本该如此」的数字都登记进 Ledger，
//     全过才是进程退出码 0；有一条对不上就返回 1，ctest 直接判红。
//   · 打印出来的**毫秒数、tick 次数**会因机器而异，请以你自己终端上的数字为准。
// ============================================================================

#include <QApplication>          // QtWidgets/qapplication.h：QApplication::exec()
#include <QCoreApplication>      // QtCore/qcoreapplication.h：sendEvent/postEvent/processEvents
#include <QEventLoop>            // QtCore/qeventloop.h：嵌套循环与 ProcessEventsFlag
#include <QTimer>                // QtCore/qtimer.h：自动演示脚本 + 心跳定时器
#include <QElapsedTimer>         // QtCore/qelapsedtimer.h：自旋占住循环用（不用 QThread::sleep）
#include <QThread>               // QtCore/qthread.h：证据⑧ 用 loopLevel() 证明外层循环还在
#include <QWidget>               // QtWidgets/qwidget.h：演示舞台
#include <QEvent>                // QtCore/qcoreevent.h：QEvent 本体与 Type 枚举
#include <QKeyEvent>             // QtGui/qevent.h：构造一个「用户输入事件」来验证 flag
#include <QDebug>
#include <QString>

// ============================================================================
// 一、一个「能自己报告到达顺序」的探针对象
// ============================================================================
// 为什么不用现成的 widget？因为我们要**精确看见**每个事件在什么时刻、
// 第几号到达哪个对象。用 QObject 直接重写 event()，比挂各种 handler 更直白。
//
// 核对签名：
//   QtCore/qobject.h 第 117 行   virtual bool event(QEvent *event);      ← public
//   QtCore/qobject.h 第 118 行   virtual bool eventFilter(QObject*, QEvent*); ← public
//   （注意：QWidget 里那个 event() 在 **protected** 段，qwidget.h 第 658 行
//    protected: → 第 660 行 bool event(QEvent *event) override;）
namespace {

// ── 硬账：退出码 = 结论 ──────────────────────────────────────────────────
//   到达次数、重入深度、flag 过滤结果这些「本该如此」的数字，全部登记进 Ledger，
//   末尾 exit(Ledger::exitCode())：全过返回 0，有一条对不上返回 1（ctest 判红）。
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

int g_seq = 0;                                    // 全局事件流水号，用来看「谁先到」

// 自定义事件类型：先借用 User 段，第 6 讲会改用 registerEventType() 正式注册
// 核对：QtCore/qcoreevent.h 第 300~301 行  User = 1000, MaxUser = 65535
constexpr QEvent::Type Ping = QEvent::Type(QEvent::User + 1);
constexpr QEvent::Type Pong = QEvent::Type(QEvent::User + 2);

class Probe : public QObject
{
public:
    explicit Probe(const QString &name) : m_name(name) {}

    int delivered() const { return m_delivered; }   // 一共收到几次事件
    int maxDepth() const { return m_maxDepth; }     // 最大重入深度
    void setReenter(bool on) { m_reenter = on; }    // 收到 Ping 时是否再抽一次队列

    // QObject::event() 是 public virtual，所以这里可以直接 public 重写
    bool event(QEvent *e) override
    {
        ++m_delivered;
        ++m_depth;
        m_maxDepth = qMax(m_maxDepth, m_depth);

        // 核对：qcoreevent.h 第 307 行  inline Type type() const
        //       第 311 行  bool isAccepted() const
        //       第 313/314 行  void accept() / void ignore()
        qInfo().noquote() << QStringLiteral("    [%1] 第 %2 号事件到达，type=%3，isAccepted=%4，当前深度=%5")
                                 .arg(m_name)
                                 .arg(++g_seq)
                                 .arg(int(e->type()))
                                 .arg(e->isAccepted() ? "true" : "false")
                                 .arg(m_depth);

        // 「在事件处理里再抽一次队列」= 重入。第 3 号证据会用到它。
        if (m_reenter && e->type() == Ping)
            QCoreApplication::processEvents(QEventLoop::AllEvents);   // qcoreapplication.h 109 行

        --m_depth;
        return true;      // 已处理（等价于 accept 的语义，见第 2 讲 notify 的返回值）
    }

private:
    QString m_name;
    int m_delivered = 0;
    int m_depth = 0;
    int m_maxDepth = 0;
    bool m_reenter = false;
};

} // namespace

// ============================================================================
// 二、main：先演「没有循环时事件在哪」，再演「循环里事件怎么走」
// ============================================================================
int main(int argc, char *argv[])
{
    // 核对：QtWidgets/qapplication.h 第 52 行  QApplication(int &argc, char **argv, int = ApplicationFlags);
    QApplication app(argc, argv);

    QWidget stage;
    stage.setWindowTitle(QStringLiteral("第 1 讲 · 事件循环解剖"));
    stage.resize(480, 260);
    stage.show();          // 记住第壹阶段那句：show() 只是挂号，画出来得等 exec()

    Probe a(QStringLiteral("A"));
    Probe b(QStringLiteral("B"));

    // ────────────────────────────────────────────────────────────────────
    // 证据 ①  exec() 之前：postEvent 的事件「躺在队列里」，一个都不会到
    // ────────────────────────────────────────────────────────────────────
    // 这是理解整门课的零点：**事件不是「调用」，是「待办事项」**。
    // 核对：QtCore/qcoreapplication.h
    //   第 108 行  static int exec();
    //   第 113 行  static bool sendEvent(QObject *receiver, QEvent *event);
    //   第 114 行  static void postEvent(QObject *receiver, QEvent *event,
    //                                    int priority = Qt::NormalEventPriority);
    //   第 115 行  static void sendPostedEvents(QObject *receiver = nullptr, int event_type = 0);
    //   第 116 行  static void removePostedEvents(QObject *receiver, int eventType = 0);
    // 优先级常量：QtCore/qnamespace.h 第 1610~1612 行
    //   HighEventPriority = 1, NormalEventPriority = 0, LowEventPriority = -1
    QCoreApplication::postEvent(&a, new QEvent(Ping));            // 堆上！postEvent 接管所有权
    QCoreApplication::postEvent(&b, new QEvent(Pong), Qt::LowEventPriority);
    qInfo() << "证据① postEvent 两次之后（还没进 exec()）：A 收到" << a.delivered()
            << "B 收到" << b.delivered();
    check("① exec 之前 A 一个都没收到", a.delivered(), 0);
    check("① exec 之前 B 也一个都没收到", b.delivered(), 0);
    // 期望：0 0。所有权提醒：postEvent 的 event **必须**是 new 出来的，
    // Qt 在派发后（或接收者销毁时）负责 delete；传栈上对象 = 悬空指针事故。

    // ────────────────────────────────────────────────────────────────────
    // 证据 ②  sendEvent 是同步的：函数返回时，接收者的 event() 早就跑完了
    // ────────────────────────────────────────────────────────────────────
    // 惯例：sendEvent 的事件**由调用方**负责销毁，所以写成栈对象最安全
    // （文档原话大意：unless a subscriber takes ownership，caller 必须 delete）。
    QEvent syncPing(Ping);                     // qcoreevent.h 305 行  explicit QEvent(Type type)
    const bool handled = QCoreApplication::sendEvent(&b, &syncPing);
    qInfo() << "证据② sendEvent 返回："
            << "handled =" << handled
            << "| B 已收到" << b.delivered() << "个（同步，就在这一行里发生的）"
            << "| A 仍是" << a.delivered() << "个（异步入队，还没轮到）";
    // 期望：B=1、A=0 —— 这一条就是「send 插队、post 排队」的物证。
    checkBool("② sendEvent 返回 true（事件被处理了）", handled);
    check("② sendEvent 当场送到 B（B=1）", b.delivered(), 1);
    check("② 此时 A 仍为 0（post 还在排队）", a.delivered(), 0);

    // ────────────────────────────────────────────────────────────────────
    // 证据 ③  手动抽干队列 & 取消队列里的事件
    // ────────────────────────────────────────────────────────────────────
    QCoreApplication::sendPostedEvents(&a, 0);   // receiver=&a, event_type=0(全部)
    qInfo() << "证据③ sendPostedEvents(&a) 之后：A 收到" << a.delivered()
            << "（证明事件此前一直待在队列里，是**这次调用**把它们派发的）";
    check("③ 手动抽队列后 A 收到 1 个", a.delivered(), 1);

    QCoreApplication::postEvent(&a, new QEvent(Ping));
    const int deliveredBeforeRemove = a.delivered();
    QCoreApplication::removePostedEvents(&a, int(Ping));   // void：Qt6 不回报条数（116 行）
    QCoreApplication::sendPostedEvents(&a, 0);
    const int arrivedAfterRemove = a.delivered() - deliveredBeforeRemove;  // 自己统计：撤掉 1 条投递 → 到达 0 条
    qInfo() << "证据③ 投递 1 条、removePostedEvents 撤掉 1 条（Qt6 返回 void，条数自己统计：再抽队列后实际到达"
            << arrivedAfterRemove << "条）；A 的计数 =" << a.delivered()
            << "（撤掉的那条永远不会到）";
    check("③ 被撤掉的那条确实没到（到达 0 条）", arrivedAfterRemove, 0);
    // 这条在多线程/关窗时很有用：接收者快死了，先把它在队列里的欠账清掉。

    // ────────────────────────────────────────────────────────────────────
    // 下面全部放进 exec() 里，用 QTimer::singleShot 排成自动演示脚本
    // ────────────────────────────────────────────────────────────────────

    // 心跳：50ms 一次的重复定时器。它是「事件循环有没有在转」的**指针表**。
    // 核对：QtCore/qtimer.h 第 37 行 setInterval(int msec)、第 112 行 start()、第 113 行 stop()
    int heartbeats = 0;
    QTimer heart;
    heart.setInterval(50);
    QObject::connect(&heart, &QTimer::timeout, [&heartbeats] { ++heartbeats; });
    heart.start();

    // ── 证据 ④  槽里自旋 400ms（= 拿一段「耗时活」冒充业务）：心跳停摆
    // 核对：qtimer.h 第 85 行附近  static void singleShot(Duration interval, Functor &&slot)
    QTimer::singleShot(400, [&heartbeats] {
        const int before = heartbeats;
        qInfo() << "证据④ 起点：心跳已累计" << before << "次（说明 exec() 正在派发定时器事件）";

        QElapsedTimer busy;                     // qelapsedtimer.h 第 36 行 start()、第 43 行 elapsed()
        busy.start();
        while (busy.elapsed() < 400) { /* 故意占住 GUI 线程不放 */ }

        qInfo() << "证据④ 自旋 400ms 之后：心跳 =" << heartbeats
                << "增量 =" << (heartbeats - before)
                << "→ 0 或极小，就证明了「事件循环被一个槽独占时，所有登记都响应不了」";
        checkBool("④ 进槽之前循环确实在转（心跳 ≥ 1）", before >= 1);
        check("④ 自旋 400ms 期间心跳增量为 0", heartbeats - before, 0);
    });

    // ── 证据 ⑤  嵌套事件循环（QEventLoop）：心跳继续跳，但重入风险已经埋下
    QTimer::singleShot(900, [&heartbeats] {
        qInfo() << "证据⑤ 进入嵌套 QEventLoop 前：心跳 =" << heartbeats;

        // 「在槽里开一个模态循环」——QDialog::exec()、QMessageBox::exec()、
        // 以及所有「同步等一会儿」的写法，内部都是这一套。
        // 核对：QtCore/qeventloop.h
        //   第 23 行  explicit QEventLoop(QObject *parent = nullptr);
        //   第 43 行  int exec(ProcessEventsFlags flags = AllEvents);
        //   第 44 行  bool isRunning() const;
        //   第 46 行  void wakeUp();
        //   第 51 行  void exit(int returnCode = 0);     ← public Q_SLOTS（第 50 行）
        //   第 52 行  void quit();                        ← public Q_SLOTS
        QEventLoop nested;
        QTimer::singleShot(400, [&nested] { nested.quit(); });   // 也可以用 &nested + SLOT(quit())
        const int before = heartbeats;
        const int rc = nested.exec();          // ← 阻塞在这儿，但**这个 nested 在派发事件**

        qInfo() << "证据⑤ 嵌套循环里心跳增量 =" << (heartbeats - before)
                << "→ 界面没卡（循环还在转）；代价是：这期间**任何**事件都可能进来，"
                  "包括再次触发你这段代码本身（重入）。退出码 rc =" << rc;
        check("⑤ nested.exec() 被 quit() 正常结束，rc = 0", rc, 0);
        checkBool("⑤ 嵌套循环里心跳还在跳（增量 ≥ 1）", heartbeats - before >= 1);
        checkBool("⑤ exec() 返回后 isRunning() = false", nested.isRunning(), false);
    });

    // ── 证据 ⑥  重入的真实代价：在事件处理里调 processEvents
    QTimer::singleShot(1500, [] {
        Probe deep(QStringLiteral("R"));
        deep.setReenter(true);

        // 一次 send：R::event() 在深度 1 里又调 processEvents →
        // 队列里那条 Ping 被抽出来，在**同一个函数栈**里第二次进入 event() → 深度 2。
        QCoreApplication::postEvent(&deep, new QEvent(Ping));
        QCoreApplication::postEvent(&deep, new QEvent(Ping));
        QEvent first(Ping);
        QCoreApplication::sendEvent(&deep, &first);

        qInfo() << "证据⑥ R 收到事件数 =" << deep.delivered()
                << "最大重入深度 =" << deep.maxDepth()
                << "→ 深度 >1 就意味着：同一个槽/处理函数在栈里叠了两层，"
                  "你在里面改的成员变量、开的新对象、释放的指针，全都可能被上层再摸一次。";
        check("⑥ 1 次 send + 2 条队列全部到达（3 个事件）", deep.delivered(), 3);
        check("⑥ 最大重入深度 = 3（send 一层 + 队列抽了两层）", deep.maxDepth(), 3);
        // 规矩：真要用 processEvents，就写 `if (m_inside) return; m_inside = true; ... m_inside = false;`
        // 或者用 QCoreApplication::postEvent 把活儿推到下一轮，而不是原地抽队列。
    });

    // ── 证据 ⑦  ProcessEventsFlag 的过滤能力（枚举名核对：qeventloop.h 第 26~35 行）
    //   AllEvents = 0x00, ExcludeUserInputEvents = 0x01, ExcludeSocketNotifiers = 0x02,
    //   WaitForMoreEvents = 0x04, X11ExcludeTimers = 0x08, EventLoopExec = 0x20,
    //   DialogExec = 0x40, ApplicationExec = 0x80
    // 注意 AllEvents 的值是 **0**：这套 flag 的语义是「排除项」，不写任何排除就是全收。
    QTimer::singleShot(2000, [] {
        Probe k(QStringLiteral("K"));

        // 构造一个「用户输入事件」压进队列
        // 核对：QtGui/qevent.h 第 425 行起  class QKeyEvent
        //   QKeyEvent(Type type, int key, Qt::KeyboardModifiers modifiers,
        //             const QString &text = QString(), bool autorep = false, quint16 count = 1);
        // Qt::Key_Tab 等键值：qnamespace.h 第 687 行；Qt::NoModifier：第 1078 行
        QCoreApplication::postEvent(&k, new QKeyEvent(QEvent::KeyPress, Qt::Key_Tab,
                                                      Qt::NoModifier, QStringLiteral("\t")));
        const int before = k.delivered();

        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        const int afterExclude = k.delivered();

        QCoreApplication::processEvents(QEventLoop::AllEvents);
        const int afterAll = k.delivered();

        qInfo() << "证据⑦ 键盘事件：初始" << before
                << "| ExcludeUserInputEvents 之后" << afterExclude
                << "| AllEvents 之后" << afterAll
                << "→ 预期中段不涨、末段 +1；以你打印的数字为准。";
        check("⑦ 初始收到 0 个", before, 0);
        check("⑦ 排除用户输入时不派发（仍为 0）", afterExclude, 0);
        check("⑦ AllEvents 之后 +1", afterAll, 1);
        // Qt5 老代码里 QEventLoop::exec 的参数类型叫 ExecFlags（ExecOption 枚举），
        // Qt6 这里统一是 QEventLoop::ProcessEventsFlags（qeventloop.h 36 行）。
        // 本机只有 Qt6 头，Qt5 那份枚举的数值我核不到，所以不复述——迁移时这里是常见编译错误点。
    });

    // ── 证据 ⑧  quit() / exit(int) / return 的区别
    QTimer::singleShot(2600, [] {
        // (a) return：只是从当前槽函数里出去，事件循环照转。
        // (b) QCoreApplication::quit()（qcoreapplication.h 第 205 行，static 槽）
        //     = 等价于 exit(0)：请求**最外层** exec() 结束，返回 0。
        // (c) QCoreApplication::exit(int retcode = 0)（第 206 行）
        //     给最外层 exec() 一个退出码；这个返回值常被 main 直接 return，
        //     于是变成**进程的退出码**（CI 里就是靠它判成败）。
        // (d) QEventLoop::exit(int)（qeventloop.h 第 51 行）只结束**自己这一层**，
        //     外层循环继续转——这就是模态对话框关掉后主循环没死的原因。
        QEventLoop inner;
        QTimer::singleShot(30, [&inner] { inner.exit(7); });   // 只停 inner
        const int rc = inner.exec();
        qInfo() << "证据⑧ inner.exit(7) → inner.exec() 返回" << rc
                << "；而外层 app.exec() 完全没受影响（程序还活着），"
                << "直到有人调 QCoreApplication::quit()/exit()。rc 用来表示「这个循环里发生了什么」，"
                << "进程退出码要用 exit(int) 才对。";
        check("⑧ inner.exit(7) 只让内层返回 7", rc, 7);
        check("⑧ 内层退出后本线程还在最外层循环里（loopLevel = 1）",
              QThread::currentThread()->loopLevel(), 1);
    });

    // ── 收尾：自动退出，无人值守
    QTimer::singleShot(3400, [&heartbeats] {
        qInfo() << "结论：本次运行事件循环共派发心跳" << heartbeats
                << "次（interval=50ms，约 3.4 秒 → 理论上限 ~68，实际少于此是"
                << "因为自旋那段把循环占住了）。";
        qInfo().noquote() << QStringLiteral("\n【硬账】%1/%2 项核对通过 → 退出码 %3")
                                 .arg(Ledger::passed).arg(Ledger::total).arg(Ledger::exitCode());
        QCoreApplication::exit(Ledger::exitCode());      // → exec() 返回同样的码 → main return rc
    });

    // 核对：QtWidgets/qapplication.h 第 118 行  static int exec();
    const int rc = app.exec();
    qInfo() << "exec() 返回" << rc << "（若用 QCoreApplication::exit(n) 就是 n）";
    return rc;
}

// ============================================================================
// 附：把 exec() 用伪代码写一遍
// ============================================================================
/*
 * QCoreApplication::exec() 里在跑的东西（高度简化，够用来建立心智模型）：
 *
 *   while (!runningWasCalled) {                 // 没人 quit()/exit() 就一直转
 *       if (事件队列里有待办) {                 // QPostEventList
 *           取出优先级最高、最早入队的一条;
 *           notify(接收者, 事件);               // ← 第 2 讲的主角
 *       } else {
 *           向事件分发器（QAbstractEventDispatcher）等一次原生事件;
 *           // Windows 上就是 GetMessage/PeekMessage 那一套
 *           // 定时器到期、socket 可读、窗口消息，都从这里进来
 *       }
 *   }
 *   return 退出码;
 *
 * 三条推论，本讲全部有实测代码撑着：
 *   · 队列没人抽 → postEvent 的事件永远不到（证据①③）。
 *   · 循环被一个槽独占 → 什么都进不来（证据④，界面「未响应」就是它）。
 *   · exec() 可以嵌套：每一层都有自己的退出码，内层 quit()/exit() 杀不掉外层（证据⑤⑧）。
 *
 * 顺便记两个「安全工具」：
 *   · QEventLoopLocker（qeventloop.h 第 57 行起）：栈对象，析构时若它是最后一个
 *     「留」在循环里的人，会调用 QCoreApplication::quit()——用于「窗口全关就退程序」
 *     这类策略（配合 Qt::WA_QuitOnClose，别在 exec 之前构造它）。
 *   · QCoreApplication::startingUp()/closingDown()（第 122/123 行）：判断当下处于
 *     生命周期的哪一段，写全局析构里的兜底代码时很有用。
 *
 * ——— 老师傅的话 ———
 *   徒弟，很多人学 Qt 学了一年，exec() 在他眼里还是「一行必须写的咒语」。今天
 *   我要你把它换成一句话：**exec() 是一台只会做两件事的机器——从队列里取事件、
 *   把事件交出去。** 其余全是它的家务事。
 *
 *   一旦你这么看世界，三类玄学问题会当场消失：
 *   ①「我 postEvent 了怎么没反应？」——因为没人抽队列，或者你抽的方式不对；
 *   ②「为什么一点按钮整个界面就白了？」——因为一个槽独占着那台机器；
 *   ③「为什么我改了状态界面不刷新？」——刷新也得排队，你没给循环机会。
 *
 *   然后是这门手艺的分水岭：**嵌套循环是双刃剑**。它能让你在同步等待里「界面还
 *   活着」，代价是同一份代码可以在栈里叠两层。你写「同步等待」的那一行，永远不
 *   知道它会重入谁。所以规矩是：能不嵌套就不嵌套；非要嵌套，就把这段代码写成
 *   可重入安全的（进不来第二次、或者第二次进来也不坏）。QDialog::exec() 是官方
 *   给你的合法嵌套，别自己再造第四个。
 *
 *   最后夸一句本讲的写法：全程没碰一次鼠标键盘，全是自己 new 事件再 send/post。
 *   这不是花活——它意味着**事件流可以被程序化地复现和断言**。CI、回归测试、
 *   给客户演示「你看它确实收到了」，靠的都是这个。记住：能自动跑的东西才是资产。
 *
 * ——— 小练习 ———
 *   1. 把证据④里那段 `while (busy.elapsed() < 400)` 改成
 *      `QElapsedTimer` + `QCoreApplication::processEvents()` 轮询（每 10ms 抽一次
 *      队列），观察心跳增量从 ~0 变成 ~8，并解释：这时界面为什么「活了」，但
 *      证据⑥的重入风险为什么反而变高了。
 *   2. 把最后的 `QCoreApplication::exit(Ledger::exitCode())` 临时换成 `exit(3)`，
 *      在终端里用 `echo $?`（或 Windows 的 `echo %errorlevel%`）确认进程退出码
 *      真的是 3；再把它换成从槽里直接 `return`，看程序为什么不退出。
 *      （改回来时记住：这个文件现在靠退出码表态，别留成永久 3。）
 *   3. 往证据⑦里再 post 两条不同优先级的同一类型事件
 *      （HighEventPriority 与 LowEventPriority，qnamespace.h 1610~1612 行），
 *      给 Probe::event() 里加一行打印 `++g_seq`，验证「优先级高的先到、
 *      同优先级按入队先后到」。然后试着 post 三条同样的 Ping，
 *      数一数 A 到底收到几条——这条留到第 6 讲讲合并时揭晓。
 */
// ============================================================================
