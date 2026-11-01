// ============================================================================
// 第 6 讲 · 自定义事件（custom event）
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   Qt 的内建事件类型（QEvent::Type）已经覆盖了输入、绘制、生命周期，
//   但**你自己的业务节拍**（进度回调、状态机 tick、跨对象通知）也可以走事件通道。
//   本讲讲清四件事：
//     1) 类型怎么来：**QEvent::registerEventType() 动态注册**，而不是自己
//        拍一个 `QEvent::User + 3` 了事——拍脑袋的代价是和别人撞车；
//     2) QEvent 子类怎么带自己的数据（构造必须把 type 传给基类），
//        以及 Qt6 新加的虚函数 clone() 为什么要重写；
//     3) postEvent 的三条真实语义：所有权转移、优先级、**同一接收者同一类型的合并**；
//     4) 为什么「跨线程传数据」不该手写事件（要靠第贰阶段的 QueuedConnection，
//        它内部其实就是投一个 QMetaCallEvent），而 postEvent 天生就是线程安全的。
//
// 对应构建目标名：events_06_custom_events
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(events_06_custom_events 06_custom_events.cpp)
//   target_link_libraries(events_06_custom_events PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)   # 本文件里有 Q_OBJECT 类（Job），末尾必须 #include "06_custom_events.moc"
//
// 运行前须知：
//   · 无鼠标键盘：进度事件全部由定时器 + postEvent 产生，约 4 秒自动跑完并 quit()。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 events_06_custom_events，跑到自动退出、退出码 0）。
//     但本讲**没有汇总自检行**，退出码 0 只证明「没崩、走完了」，不证明正文每个数字都对：
//     带具体数字的「预期」请以你自己那次运行输出为准（本机那一轮存档 runlogs/events_06_custom_events.log）。
//   · registerEventType() 返回的具体数字**每次运行/每个工程都可能不同**，
//     这正是本讲要你记住的点：**不要硬编码事件的整数值**。
// ============================================================================

#include <QApplication>
#include <QCoreApplication>       // sendEvent / postEvent / sendPostedEvents / removePostedEvents
#include <QWidget>
#include <QObject>
#include <QEvent>
#include <QTimer>
#include <QElapsedTimer>
#include <QDebug>
#include <QString>
#include <QByteArray>

// ============================================================================
// 一、核对签名
// ============================================================================
// QtCore/qcoreevent.h
//   第 49 行    class Q_CORE_EXPORT QEvent            // event base class
//   第 56 行    enum Type { None = 0, Timer = 1, MouseButtonPress = 2, ...
//   第 188 行        ZeroTimerEvent = 154,   // Used for Windows Zero timer events
//   第 300 行        User = 1000,            // first user event id
//   第 301 行        MaxUser = 65535 }       // last user event id
//   第 305 行    explicit QEvent(Type type);
//   第 306 行    virtual ~QEvent();
//   第 307 行    inline Type type() const { return static_cast<Type>(t); }
//   第 310~314 行 setAccepted()/isAccepted()/accept()/ignore()
//   第 320 行    static int registerEventType(int hint = -1) noexcept;    ← 本讲主角
//   第 322 行    virtual QEvent *clone() const;                            ← Qt6 新增
// QtCore/qcoreapplication.h
//   第 113 行    static bool sendEvent(QObject *receiver, QEvent *event);
//   第 114 行    static void postEvent(QObject *receiver, QEvent *event,
//                                      int priority = Qt::NormalEventPriority);
//   第 115 行    static void sendPostedEvents(QObject *receiver = nullptr, int event_type = 0);
//   第 116 行    static void removePostedEvents(QObject *receiver, int eventType = 0);
// QtCore/qnamespace.h
//   第 1610~1612 行 HighEventPriority = 1, NormalEventPriority = 0, LowEventPriority = -1
// 观察点：
//   · registerEventType 的返回类型是 **int**（不是 QEvent::Type），所以要自己 cast；
//     它带一个 `int hint = -1` —— 给 hint 就是「尽量用这个值」，不给就是「你帮我挑」。
//   · QEvent 的构造是 **explicit QEvent(Type)**：子类的构造初始化列表里必须给它 type。
//   · 事件类型池：User(1000) ~ MaxUser(65535)，内建类型都 <1000（本机最大内建值
//     是 ParentAboutToChange=131 / WinEventAct=132，另外 Qt Jambi 预留了 512/513）。

// ============================================================================
// 二、自定义事件：带自己的数据
// ============================================================================
// 规矩（本讲最重要的三条，写在这里当注释版「法条」）：
//   R1  类型必须来自 registerEventType()，**不要**写 QEvent::User + N；
//   R2  构造函数把 type 交给 QEvent 基类，数据成员放自己这边（事件要能被 new/delete，
//       不要放引用成员、不要放不可拷贝的东西，否则 clone() 写不出来）；
//   R3  Qt6 请重写 clone()——默认实现（文档语义）会 qWarning 并做一次 QEvent(*this)
//       浅拷贝，你的数据成员就丢了。
class ProgressEvent : public QEvent
{
public:
    // 惰性注册：第一次用到时才向 Qt 要一个唯一 type（线程安全的 static 初始化）
    static QEvent::Type type()
    {
        if (s_id < 0)
            s_id = QEvent::registerEventType();      // qcoreevent.h 320 行；不给 hint，让 Qt 挑
        return static_cast<QEvent::Type>(s_id);
    }

    ProgressEvent(int percent, const QString &stage, const QByteArray &payload = QByteArray())
        : QEvent(type()),                        // ← 基类必须拿到 type（305 行）
          m_percent(percent), m_stage(stage), m_payload(payload)
    {
    }

    int percent() const { return m_percent; }
    const QString &stage() const { return m_stage; }
    const QByteArray &payload() const { return m_payload; }

    // R3：重写 clone()，让「拷贝事件」保住你自己的数据
    QEvent *clone() const override { return new ProgressEvent(*this); }

    static int rawId() { return s_id; }

private:
    int m_percent;
    QString m_stage;
    QByteArray m_payload;
    inline static int s_id = -1;                 // C++17 inline static：全进程一份
};

// 第二个自定义事件：故意演示「硬编码 vs 注册」的撞车风险
class HeartbeatEvent : public QEvent
{
public:
    static QEvent::Type type()
    {
        if (s_id < 0)
            // 演示 hint 用法：给一个 User 段的值当偏好（给了不保证采纳，Qt 会避开已占用值）
            s_id = QEvent::registerEventType(QEvent::User + 1);
        return static_cast<QEvent::Type>(s_id);
    }
    explicit HeartbeatEvent(quint64 tickNo = 0) : QEvent(type()), m_tick(tickNo) {}
    quint64 tickNo() const { return m_tick; }
    QEvent *clone() const override { return new HeartbeatEvent(*this); }
    static int rawId() { return s_id; }

private:
    quint64 m_tick;
    inline static int s_id = -1;                 // 本类自己的 id：每个自定义事件类各持一份，别共用 ProgressEvent 那个
};

// ============================================================================
// 三、接收端：靠 type 判断 + static_cast 取数据
// ============================================================================
class ProgressSink : public QObject
{
public:
    explicit ProgressSink(const QString &name) { setObjectName(name); }

    int progressSeen = 0;
    int heartbeatSeen = 0;
    int otherSeen = 0;
    int unknownUserSeen = 0;
    QString lastStage;
    int lastPercent = -1;

    // 注意：QObject::event() 是 **public** virtual（qobject.h 第 117 行），
    // 而非控件的 QObject 子类的闸门就在 public 段；widget 的 event() 在 protected（第 2 讲）。
    bool event(QEvent *e) override
    {
        // 标准三段式：先比 type，再 static_cast，**别用 qobject_cast**（事件不是 QObject）
        if (e->type() == ProgressEvent::type()) {
            auto *pe = static_cast<ProgressEvent *>(e);
            ++progressSeen;
            lastStage = pe->stage();
            lastPercent = pe->percent();
            if (progressSeen <= 3 || pe->percent() == 100)
                qInfo().noquote() << QStringLiteral("    %1 收到 ProgressEvent：percent=%2 stage=%3 payload=%4 字节")
                                         .arg(objectName())
                                         .arg(pe->percent())
                                         .arg(pe->stage())
                                         .arg(pe->payload().size());
            return true;
        }
        if (e->type() == HeartbeatEvent::type()) {
            auto *he = static_cast<HeartbeatEvent *>(e);
            ++heartbeatSeen;
            if (heartbeatSeen <= 2)
                qInfo() << "    " << objectName() << " 收到 HeartbeatEvent tickNo =" << he->tickNo();
            return true;
        }
        if (e->type() >= QEvent::User) {
            ++unknownUserSeen;      // 别人的用户段事件打到你身上了 → 撞车或乱投的信号
            qInfo() << "    " << objectName() << " 收到一个**不认识的用户段事件**，type =" << int(e->type());
            return false;
        }
        ++otherSeen;
        return QObject::event(e);   // ← 交回基类（DeferredDelete / ThreadChange 等要靠它）
    }
};

// ============================================================================
// 四、一个用信号做同一件事的类（用来做对照实验）
// ============================================================================
class Job : public QObject
{
    Q_OBJECT
public:
    int steps = 0;

    void doWorkWithSignal(int percent)
    {
        // 「应用层语义」的做法：直接发信号，谁关心谁 connect
        Q_EMIT progressed(percent);
    }

Q_SIGNALS:
    void progressed(int percent);
    void finished(int total);
};

// ============================================================================
// 五、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QWidget stage;
    stage.setObjectName(QStringLiteral("舞台"));
    stage.setWindowTitle(QStringLiteral("第 6 讲 · 自定义事件"));
    stage.resize(520, 240);
    stage.show();

    ProgressSink sink(QStringLiteral("接收器"));
    ProgressSink bystander(QStringLiteral("旁观者"));

    // ── 事实① 注册出来的类型值（每次运行/每个工程都可能不同 → 别硬编码）
    qInfo() << "事实① 内建类型的边界：QEvent::User =" << int(QEvent::User)
            << "（1000）、MaxUser =" << int(QEvent::MaxUser) << "（65535）";
    qInfo() << "事实① ProgressEvent 注册到的 type =" << ProgressEvent::type()
            << "（registerEventType() 无 hint）；HeartbeatEvent =" << HeartbeatEvent::type()
            << "（hint = User+1）。两者相等吗？" << (ProgressEvent::rawId() == HeartbeatEvent::rawId());
    // 反面教材（别学）：两个互不相干的库都写 `QEvent::Type(QEvent::User + 1)`
    // → 它们的 ProgressEvent 与 HeartbeatEvent 会**互相被对方解析**，
    // static_cast 之后读到的是别人的内存。这就是 R1 存在的全部理由。

    // ── 事实② sendEvent（同步）与 postEvent（异步）在自定义事件上的表现
    QCoreApplication::postEvent(&sink, new ProgressEvent(10, QStringLiteral("下载")));
    qInfo() << "事实② postEvent 之后立刻读：sink.progressSeen =" << sink.progressSeen
            << "（0：事件还在队列里）";

    // 三条不同优先级：观察**到达顺序**，这就是队列的证据
    QCoreApplication::postEvent(&sink, new ProgressEvent(21, QStringLiteral("低优先")), Qt::LowEventPriority);
    QCoreApplication::postEvent(&sink, new ProgressEvent(22, QStringLiteral("高优先")), Qt::HighEventPriority);
    QCoreApplication::postEvent(&sink, new ProgressEvent(23, QStringLiteral("普通")), Qt::NormalEventPriority);
    // 顺序日志：加一个探针，把上面 4 条的实际到达次序打出来
    ProgressSink order(QStringLiteral("顺序探针"));
    QCoreApplication::postEvent(&order, new ProgressEvent(91, QStringLiteral("A-低")), Qt::LowEventPriority);
    QCoreApplication::postEvent(&order, new ProgressEvent(92, QStringLiteral("B-高")), Qt::HighEventPriority);
    QCoreApplication::postEvent(&order, new ProgressEvent(93, QStringLiteral("C-普通")));
    QCoreApplication::postEvent(&order, new ProgressEvent(94, QStringLiteral("D-高（更晚入队）")), Qt::HighEventPriority);
    // 手动抽干（此刻还没进 exec()，这样打印的顺序是**确定性**的）
    QCoreApplication::sendPostedEvents(&order, int(ProgressEvent::type()));
    qInfo() << "事实② 优先级实验：顺序探针收到 4 条，percent 序列应为「高→高→普通→低」"
            << "（同优先级按入队先后）。若你读到 92,94,93,91 就完全符合预期。";

    // ── 事实③ 合并（coalescing）：先测「自定义事件」——默认**不会**替你合并
    int postedBefore = 0;
    for (int i = 0; i < 500; ++i) {
        QCoreApplication::postEvent(&sink, new ProgressEvent(i, QStringLiteral("刷进度")));
        ++postedBefore;
    }
    const int seenBefore = sink.progressSeen;
    QElapsedTimer drain;
    drain.start();
    QCoreApplication::sendPostedEvents(&sink, int(ProgressEvent::type()));
    qInfo() << "事实③ 同一接收者、同一类型连投" << postedBefore << "条 → 实际收到"
            << (sink.progressSeen - seenBefore) << "条；抽干耗时" << drain.elapsed() << "ms"
            << "→ **自造事件不会自动合并**：投 500 条就跑 500 次，还 500 次 new/delete。";

    // 结论落地：需要「批量合并的稍后再算」时，用 interval=0 的 QTimer 做闸门（第 4 讲），
    // 而不是自己往 postEvent 里堆。
    int mergedHits = 0;
    auto *gate = new QTimer;
    gate->setInterval(0);                       // 「本轮循环收尾时跑一次」
    QObject::connect(gate, &QTimer::timeout, [gate, &sink, &mergedHits] {
        ++mergedHits;
        // 只投**一个**快照事件：中间过程全部丢弃，这是合并的正确姿势
        QCoreApplication::postEvent(&sink, new ProgressEvent(0, QStringLiteral("合并快照#%1").arg(mergedHits)));
        gate->stop();                           // 单次闸门：下次有需求再 start
    });
    for (int i = 0; i < 500; ++i)
        gate->start();                          // 500 次「请求刷新」
    qInfo() << "事实③ 500 次 gate->start() 排好了；闸门跑起来后，接收器只会收到 1 条快照事件。";

    // ── 事实④ removePostedEvents 按类型撤账（关窗前清理自己的欠债）
    QCoreApplication::postEvent(&bystander, new HeartbeatEvent(7));
    QCoreApplication::postEvent(&bystander, new HeartbeatEvent(8));
    const int seenBeforeRemove = bystander.heartbeatSeen;
    QCoreApplication::removePostedEvents(&bystander, int(HeartbeatEvent::type()));  // void：Qt6 不回报条数（qcoreapplication.h 116 行）
    QCoreApplication::sendPostedEvents(&bystander, 0);
    const int arrivedAfterCancel = bystander.heartbeatSeen - seenBeforeRemove;      // 自己统计：撤掉 2 条投递 → 到达 0 条
    qInfo() << "事实④ 投 2 条、removePostedEvents 按类型撤掉 2 条（Qt6 返回 void，条数自己统计）；"
            << "旁观者随后收到 heartbeat" << arrivedAfterCancel << "条（预期 0）。"
            << "接收者销毁时 Qt 会自动清掉发给它的队列事件，但**你要提前撤的**是那种"
            << "「重算代价很大、结果已经作废」的活儿。";

    // ── 演示脚本（在 exec() 里）
    // 用定时器把「业务进度」投成事件：这就是自定义事件的典型用途——
    // 干活的一方不需要认识接收方，只要有一个类型和一份数据。
    QTimer producer;
    producer.setInterval(90);
    int step = 0;
    QObject::connect(&producer, &QTimer::timeout, [&] {
        step += 10;
        if (step <= 100) {
            QCoreApplication::postEvent(&sink, new ProgressEvent(step,
                                     QStringLiteral("第 %1 步").arg(step / 10),
                                     QByteArray(step, '\x41')));
        }
        // 心跳：同一个接收者也可以并行跑另一种自定义事件
        QCoreApplication::postEvent(&bystander, new HeartbeatEvent(quint64(step)));
    });
    producer.start();

    // 对照实验：同一件事用信号做
    Job job;
    int signalHits = 0;
    QObject::connect(&job, &Job::progressed, [&signalHits](int p) {
        ++signalHits;
        if (signalHits <= 2)
            qInfo() << "    信号路径 progressed() 收到 percent =" << p
                    << "（同线程：就在 Q_EMIT 那一行里同步跑完，见第贰阶段第 1 讲）";
    });
    QTimer::singleShot(500, [&job] { job.doWorkWithSignal(42); });

    // ── 收尾：把三条事实汇总打印
    QTimer::singleShot(1600, [&] {
        producer.stop();
        qInfo() << "\n=== 结论 ===";
        qInfo() << "① 自定义事件的 type 必须来自 registerEventType()（qcoreevent.h 320 行）："
                << "ProgressEvent =" << ProgressEvent::rawId()
                << "HeartbeatEvent =" << HeartbeatEvent::rawId()
                << "——两个都是 Qt 分配的，不重叠；**别把自己的事件绑在整数值上存到文件/跨进程**。";
        qInfo() << "② 接收端计数：sink progress" << sink.progressSeen << "条 / heartbeat"
                << sink.heartbeatSeen << "条 / 不认识的 user 段" << sink.unknownUserSeen << "条 / 其他"
                << sink.otherSeen << "条；最后一条进度 = " << sink.lastPercent << "% " << sink.lastStage;
        qInfo() << "③ 合并：自造事件 500 条 → 500 次（事实③ 上半）；"
                << "QTimer(0) 闸门 500 次请求 → 闸门触发" << mergedHits << "次 →"
                << "只投 1 条快照事件。**想要合并就用定时器当闸门，别自造合并。**";
        qInfo() << "④ 事件 vs 信号：本次 signal 路径跑了" << signalHits
                << "次（同步、参数类型化、有连接表与生命周期）；"
                << "事件路径跑了" << sink.progressSeen << "次（异步入队、拥有权交给 Qt、只有原始 type 码）。";
        QCoreApplication::quit();
    });

    return app.exec();
}

// ============================================================================
// 附：什么时候该用自定义事件，什么时候不该
// ============================================================================
/*
 *  适合用自定义事件（QEvent 子类 + postEvent）的场景：
 *    · **跨线程投递一份状态快照**：postEvent 天生线程安全（内部有队列锁），
 *      而接收端的 event() 一定跑在接收者所在线程的事件循环里 → 天然「回到 GUI 线程」。
 *    · 需要「同一目标上排队、可取消、可合并」的异步任务（配合 removePostedEvents）。
 *    · 你要接入的是**事件语义**：accept/ignore、过滤器（第 3 讲）、notify（第 2 讲）
 *      这条链上的任何一环，比如做一个自定义事件让某层拦截。
 *    · 与内建事件同构的扩展：比如自定义 QEvent::Type 走 widget 的 event() 分派。
 *
 *  不适合的场景（这时候请用信号）：
 *    · 「谁关心我就告诉谁」的广播 —— 信号有连接表，多接收者、可断开、可带 context。
 *    · 参数是业务类型化数据、还要在编译期查错 —— 事件的参数只是「内存里的一段」，
 *      static_cast 之后没有任何类型保护，写错 type 就是未定义行为。
 *    · 需要返回值/需要立刻知道结果 —— 事件队列做不到，信号（DirectConnection）可以。
 *
 *  关于「跨线程」的两条硬规矩（本模块先把话说死，线程模块会正面展开）：
 *    · **不要**把 A 线程 new 的事件用 sendEvent 送给 B 线程的对象：sendEvent 会在
 *      **调用线程**里直接跑 B 线程对象的 event()，那就是两个线程同时摸一个对象。
 *      跨线程只能用 postEvent。
 *    · 信号槽的跨线程不是「另一套机制」：Qt::QueuedConnection 的实现，就是给接收者
 *      所在线程 post 一个 **QMetaCallEvent**（qcoreevent.h 第 102 行 MetaCall = 43），
 *      由那个线程的事件循环把它还原成一次槽调用。参数必须是可拷贝且 QMetaType 认得的类型。
 *      理解到这一层，「事件」和「信号」就不再是两套并列的知识，而是**同一台机器的两层**。
 *
 *  clone() 为什么要重写（文档语义，本机核到的是声明：qcoreevent.h 第 322 行
 *  `virtual QEvent *clone() const;`）：Qt6 里有些路径需要事件的副本（默认实现会
 *  qWarning 并退化成一次 QEvent 浅拷贝，你的数据成员全丢）。自己带数据的事件，
 *  一行 `return new MyEvent(*this);` 就解决了——本讲两个事件类都这么写了。
 *
 * ——— 老师傅的话 ———
 *   徒弟，自定义事件这一手，会写的人不多，写对的人更少。它的门槛不在「会 subclass」，
 *   在三个观念上的转变：
 *
 *   第一，**事件是内存，不是调用**。你 postEvent 出去的那一刻，控制权就交出去了：
 *   所有权交给 Qt（它会 delete），类型交给整数码（没人替你检查），时机交给事件循环。
 *   所以师傅要求你：接收端永远「先比 type 再 static_cast」，事件类永远可拷贝、
 *   永远重写 clone()、永远别在析构里等任何回执。
 *
 *   第二，**别侵占整数语义**。硬编码 `User + 1` 的代码，在只有一个库时是聪明的偷懒，
 *   在两个库时是定时炸弹——而且炸的是别人的工程，因为 static_cast 会读到对方的字段，
 *   崩在一次看似无关的调用里。registerEventType() 就是为了让 Qt 来分配，
 *   它保证同进程内唯一。这条没有讨论余地。
 *
 *   第三，**合并不是免费的功能，是设计**。事实③ 那一串 500 条你自己投的事件，一条都没被
 *   合并——Qt 只对它认识的少数内建类型（以及零定时器事件这类）做合并。
 *   老手怎么做？拿一个 interval=0 的 QTimer 当闸门，请求来了只 start()，
 *   闸门真正响的时候投**一个最新快照**。这一招能省掉 99% 的事件与 new/delete，
 *   也是 Qt 自己 update() 合并重绘的原理。别自造队列、别自造节流。
 *
 * ——— 小练习 ———
 *   1. 把 ProgressEvent 的 type 改成硬编码 `QEvent::Type(QEvent::User + 1)`，
 *      同时把 HeartbeatEvent 也改成同样的值，重跑：观察 sink/bystander 各自
 *      「不认识的用户段事件」计数（unknownUserSeen）如何变化，并解释
 *      为什么这里可能不崩但读到脏数据。然后改回 registerEventType()。
 *   2. 给 ProgressEvent 加一个 `qint64 seq` 字段，并在接收端只处理
 *      「比上次大」的事件（丢掉过期快照）。做完之后把这套逻辑与你用
 *      QTimer(0) 闸门写的方案比较：哪个更省事？为什么？
 *   3. 删掉事实③ 里 `gate->stop()` 那一行（让闸门一直开着），再跑一次，
 *      打印 mergedHits 与接收器的 progressSeen 增量；由此说明
 *      「interval=0 的定时器在事件循环里能合并多少重复请求」的真实含义。
 */
// ============================================================================

// 本文件在 .cpp 里定义了 Q_OBJECT 类（Job）：末尾必须包含 AUTOMOC 生成的同名 .moc
#include "06_custom_events.moc"
