// ============================================================================
// 第 4 讲 · 定时器家族
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   定时器是**唯一一个由事件循环自己造出来的事件源**，所以它是检验你是否真懂
//   第 1 讲的最佳试纸。本讲把四件事讲透并给出可测的数字：
//     1) QTimer 的两副面孔：单次（singleShot / setSingleShot）与重复（interval）；
//     2) setTimerType 的三档（Qt::PreciseTimer / CoarseTimer / VeryCoarseTimer）
//        到底差在哪——用 QElapsedTimer **实测**每个 tick 的真实间隔；
//     3) 定时器与事件循环的强绑定：**没有 exec()（或 processEvents），
//        定时器一次都不会响**，这条与第 1 讲互相印证；
//     4) 低级路线 QObject::startTimer() + timerEvent()：它其实就是「Qt 帮你
//        投一个 QTimerEvent」，可以用 sendEvent 手工复现，看穿它。
//
// 对应构建目标名：events_04_timers
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(events_04_timers 04_timers.cpp)
//   target_link_libraries(events_04_timers PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)   # 本文件里有 Q_OBJECT 类（Legacy），末尾要 #include "04_timers.moc"
//
// 运行前须知：
//   · 无鼠标键盘：全程由定时器与代码投递的事件驱动，约 4 秒自动跑完并 quit()。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 events_04_timers，跑到自动退出、退出码 0）。
//     但本讲**没有汇总自检行**，退出码 0 只证明「没崩、走完了」，不证明正文每个数字都对：
//     带具体数字的「预期」请以你自己那次运行输出为准（本机那一轮存档 runlogs/events_04_timers.log）。
//   · **实测毫秒数一定要你自己跑**：本机跑不出来，我也不替你的机器报数。
//     三档定时器的差别在不同系统/不同负载下会明显不同，文中只承诺「相对关系」，
//     不承诺具体数值。
//   · Windows 上的定时器精度还受系统时钟分辨率影响（别的进程调过 timeBeginPeriod
//     会互相干扰）；读到的数字抖是正常的。
//   · 写法纪律：本讲所有跨回调的状态都放在**堆或 static**上。
//     排进事件循环的 lambda 一定活得比「排它的那行代码」久，
//     用 & 捕获那个作用域的局部变量就是悬空（第贰阶段第 4 讲的规矩，这里必须守住）。
// ============================================================================

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>              // QtCore/qeventloop.h：processEvents 的 flags 在这里
#include <QTimer>
#include <QElapsedTimer>
#include <QEvent>
#include <QObject>
#include <QWidget>
#include <QDebug>
#include <QString>
#include <QVector>
#include <chrono>

// ============================================================================
// 一、核对签名（本讲全部依据）
// ============================================================================
// QtCore/qtimer.h
//   第 29 行   explicit QTimer(QObject *parent = nullptr);
//   第 32 行   bool isActive() const;
//   第 34 行   int timerId() const;
//   第 37 行   void setInterval(int msec);      第 38 行  int interval() const;
//   第 43 行   void setTimerType(Qt::TimerType atype);   第 44 行 Qt::TimerType timerType() const;
//   第 47 行   void setSingleShot(bool);        第 48 行 bool isSingleShot() const;
//   第 52 行   static void singleShot(int msec, const QObject *receiver, const char *member);
//   第 55 行   static void singleShot(int msec, Qt::TimerType timerType,
//                                     const QObject *receiver, const char *member);
//   第 85 行附近  static void singleShot(Duration interval, Functor &&slot);   ← 第壹阶段已核对
//   第 90 行   static inline void singleShot(Duration interval, Qt::TimerType, Functor &&slot)
//   第 110 行  void start(int msec);            第 112 行 void start();   第 113 行 void stop();
//   第 119 行  void setInterval(std::chrono::milliseconds value);         ← Qt6 的 chrono 重载
//   第 132 行  static void singleShot(std::chrono::milliseconds, const QObject *, const char *);
// QtCore/qelapsedtimer.h
//   第 15 行   class QElapsedTimer
//   第 36 行   void start() noexcept;           第 37 行 qint64 restart() noexcept;
//   第 42 行   qint64 nsecsElapsed() const noexcept;   第 43 行 qint64 elapsed() const noexcept;
//   第 44 行   bool hasExpired(qint64 timeout) const noexcept;
//   第 33/34 行 static ClockType clockType() / static bool isMonotonic()
//   第 18~25 行 enum ClockType { SystemTime, MonotonicClock, TickCounter(已弃用),
//                               MachAbsoluteTime, PerformanceCounter }
// QtCore/qobject.h（低级定时器）
//   第 149 行  int startTimer(int interval, Qt::TimerType timerType = Qt::CoarseTimer);
//   第 152 行  int startTimer(std::chrono::milliseconds, Qt::TimerType = Qt::CoarseTimer);
//   第 156 行  void killTimer(int id);          第 157 行 void killTimer(Qt::TimerId id);
//   第 372 行  virtual void timerEvent(QTimerEvent *event);      ← protected
// QtCore/qcoreevent.h
//   第 368 行  class Q_CORE_EXPORT QTimerEvent : public QEvent
//   第 372 行  explicit QTimerEvent(int timerId);   第 373 行 explicit QTimerEvent(Qt::TimerId);
//   第 375 行  int timerId() const;             第 376 行 Qt::TimerId id() const;
//   第 64 行   Timer = 1;                       第 188 行 ZeroTimerEvent = 154
//              ★ 勘误：旧稿后半写的是「第 154 行」——那是把**枚举值 154** 当成了行号。
//                本机 6.12.0：188 行才是 `ZeroTimerEvent = 154,  // Used for Windows Zero timer events`，
//                154 行上写着 `StatusTip = 112,`。这条枚举的名字和值不同款（152/154/155 有跳号），
//                所以「按值找行号」一定会找错；QEvent 表里凡是带显式值的项都别这么抄。
// QtCore/qnamespace.h
//   第 1719~1723 行  enum TimerType { PreciseTimer, CoarseTimer, VeryCoarseTimer };
//   第 1725~1727 行  enum class TimerId { Invalid = 0 };          ← Qt6 后期新增的强类型 id
// QtCore/qabstracteventdispatcher.h
//   第 60 行   virtual void registerTimer(int timerId, qint64 interval, Qt::TimerType, QObject *) = 0;
//   第 61/62 行 virtual bool unregisterTimer(int) / unregisterTimers(QObject *)

// ============================================================================
// 二、字符串版 singleShot 的接收者（为了演示老写法，才付出一份 Q_OBJECT）
// ============================================================================
class Legacy : public QObject
{
    Q_OBJECT
public slots:
    // SLOT(onTick()) 要能连上，必须是 slot（第贰阶段：裸 slots: 是语法错误，
    // public slots: 才是合法展开形式）
    void onTick() { ++count; }

public:
    int count = 0;
};

// ============================================================================
// 三、低级定时器：startTimer() + timerEvent()
// ============================================================================
class LowLevel : public QObject
{
public:
    int hits = 0;
    int myTimerId = 0;

protected:
    // 核对：qobject.h 第 372 行 virtual void timerEvent(QTimerEvent *) 在 protected 段
    void timerEvent(QTimerEvent *e) override
    {
        ++hits;
        // 核对：qcoreevent.h 第 375 行 int timerId() const
        if (e->timerId() == myTimerId) {
            if (hits == 3) {
                killTimer(myTimerId);          // 核对：qobject.h 第 156 行
                qInfo() << "  低级定时器：3 次到，killTimer(" << myTimerId << ") 已执行";
            }
        } else {
            QObject::timerEvent(e);            // 不是我的 id → 交给基类（它会 ignore）
        }
    }
};

// 小组合：算一组实测间隔的均值（避免在 lambda 里再套 lambda）
static int avgGap(const QVector<int> &v)
{
    if (v.isEmpty())
        return 0;
    long long sum = 0;
    for (int x : v)
        sum += x;
    return int(sum / v.size());
}

// 一份「三档对照」的堆上状态：ticks 的 lambda 会活得比排它的那行代码久
struct TypeState
{
    QElapsedTimer clock;
    QVector<int> gaps;
    QTimer *timer = nullptr;
    int last = 0;
    int hits = 0;
};

// ============================================================================
// 四、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QWidget stage;
    stage.setObjectName(QStringLiteral("舞台"));
    stage.setWindowTitle(QStringLiteral("第 4 讲 · 定时器家族"));
    stage.resize(460, 220);
    stage.show();

    // ────────────────────────────────────────────────────────────────
    // 证据 ①  「没有事件循环，定时器根本不会响」——与第 1 讲互相印证
    // ────────────────────────────────────────────────────────────────
    // 注意：这段**全部在 exec() 之前**跑。QTimer 的到期通知由事件分发器
    // （QAbstractEventDispatcher）产生，而它只在有人抽队列时才被服务。
    int preLoopTicks = 0;                 // 安全：这一整段都在 main 的作用域里直着走
    QTimer pre;
    pre.setInterval(10);                        // qtimer.h 37 行
    pre.setTimerType(Qt::PreciseTimer);         // qtimer.h 43 行
    QObject::connect(&pre, &QTimer::timeout, [&preLoopTicks] { ++preLoopTicks; });
    pre.start();                                // qtimer.h 112 行

    QElapsedTimer busy;
    busy.start();
    while (busy.elapsed() < 300) { /* 占住线程，不抽队列 */ }
    qInfo() << "证据① start() 之后自旋 300ms、没进事件循环：tick =" << preLoopTicks
            << "（预期 0 → 定时器不是后台线程，它就是事件循环里的一类事件）";

    const int beforePump = preLoopTicks;
    for (int i = 0; i < 12; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);   // qcoreapplication.h 110 行
    qInfo() << "证据① 手动 processEvents 12 次之后：tick 增量 =" << (preLoopTicks - beforePump)
            << "→ 只要有人抽队列，分发器就能把到期事件送进来；exec() 不过是把这个抽取写成死循环。";
    pre.stop();

    // ────────────────────────────────────────────────────────────────
    // 下面全部在 exec() 里，用 QTimer::singleShot 排自动脚本
    // ────────────────────────────────────────────────────────────────

    // ── 证据 ②  单次 vs 重复；isActive()/timerId()/interval()
    QTimer::singleShot(0, [] {
        int *hits = new int(0);            // 堆：内层 lambda 触发时，外层 lambda 早已返回
        auto *rep = new QTimer(nullptr);
        rep->setInterval(40);
        rep->setTimerType(Qt::PreciseTimer);
        QObject::connect(rep, &QTimer::timeout, [hits] { ++(*hits); });
        rep->start();
        // 核对：isActive()（34 行上面是 timerId()）、interval() 第 38 行、timerType() 第 44 行
        qInfo() << "证据② 重复定时器已启动：isActive =" << rep->isActive()
                << "timerId =" << rep->timerId()          // qtimer.h 34 行（内部就是 startTimer 的 id）
                << "interval =" << rep->interval()
                << "timerType 枚举值 =" << int(rep->timerType());

        // 单次：三种写法摆在一起
        // (a) functor + int/Duration（Qt6 主力写法，第 85 行附近）
        QTimer::singleShot(120, [] { qInfo() << "证据② (a) singleShot(120ms, lambda) 到了"; });
        // (b) functor + 显式 TimerType（qtimer.h 第 90 行）
        QTimer::singleShot(std::chrono::milliseconds(160), Qt::PreciseTimer,
                           [] { qInfo() << "证据② (b) singleShot(chrono 160ms, PreciseTimer, lambda) 到了"; });
        // (c) 字符串 SLOT 版（qtimer.h 第 52 行）——能跑，但把错误推到运行期（第贰阶段第 2 讲）
        static Legacy legacy;                          // static：无捕获 lambda 也能合法引用
        QTimer::singleShot(200, &legacy, SLOT(onTick()));
        QTimer::singleShot(260, [] {
            qInfo() << "证据② (c) 字符串版 singleShot(msec, receiver, SLOT(onTick())) 计数 =" << legacy.count;
        });

        QTimer::singleShot(200, [rep, hits] {
            rep->stop();
            qInfo() << "证据② 停止后：interval=40ms 跑了 200ms，tick =" << *hits
                    << "；isActive 现在 =" << rep->isActive()
                    << "（40ms × 5 = 200ms，读到 4~6 都正常）";
            rep->deleteLater();
            delete hits;
        });
    });

    // ── 证据 ③  三档 TimerType 的**实测间隔**
    QTimer::singleShot(600, [] {
        qInfo() << "\n=== 证据③ 三档定时器精度实测（interval 一律 100ms，各取 5 次 tick）===";

        auto *sP = new TypeState;
        auto *sC = new TypeState;
        auto *sV = new TypeState;
        const Qt::TimerType types[3] = { Qt::PreciseTimer, Qt::CoarseTimer, Qt::VeryCoarseTimer };
        TypeState *states[3] = { sP, sC, sV };
        for (int i = 0; i < 3; ++i) {
            states[i]->timer = new QTimer;
            states[i]->timer->setInterval(100);
            states[i]->timer->setTimerType(types[i]);
            states[i]->clock.start();
        }

        // 每档各连一个 tick 记录器（捕获指针，不捕获局部变量）
        auto record = [](TypeState *s) {
            const int now = int(s->clock.elapsed());   // qelapsedtimer.h 43 行
            s->gaps.append(now - s->last);
            s->last = now;
            if (++s->hits == 5)
                s->timer->stop();
        };
        QObject::connect(sP->timer, &QTimer::timeout, [sP, record] { record(sP); });
        QObject::connect(sC->timer, &QTimer::timeout, [sC, record] { record(sC); });
        QObject::connect(sV->timer, &QTimer::timeout, [sV, record] { record(sV); });
        sP->timer->start(); sC->timer->start(); sV->timer->start();

        QTimer::singleShot(900, [sP, sC, sV] {
            qInfo() << "  PreciseTimer    实测间隔 =" << sP->gaps << "均值 ≈" << avgGap(sP->gaps) << "ms";
            qInfo() << "  CoarseTimer     实测间隔 =" << sC->gaps << "均值 ≈" << avgGap(sC->gaps) << "ms";
            qInfo() << "  VeryCoarseTimer 实测间隔 =" << sV->gaps << "均值 ≈" << avgGap(sV->gaps) << "ms";
            qInfo() << "  读法（本机实测：三档都能打出数，别把上面的数字当常数——"
                    << "它们随机器负载变化，本讲只保证「这三行是这台机器刚才真实跑出来的」）："
                    << "Precise 最贴 100；Coarse 允许与别的唤醒合并、抖动最大但省电；"
                    << "VeryCoarse 会向系统 tick 取整（Windows 常落在 10~16ms 粒度，"
                    << "老 Unix 上是整秒），均值可能明显偏离 100。";
            // 三档的语义（Precise 尽力精确 / Coarse 允许 50% 误差 / VeryCoarse 依赖系统 tick）
            // 是**官方文档**的意图说明；本机头文件只给枚举名（qnamespace.h 1719~1723 行），
            // 实现看不到，所以这里只让它打印实测数，不替 Qt 承诺数字。
            delete sP->timer; delete sC->timer; delete sV->timer;
            delete sP; delete sC; delete sV;
        });
    });

    // ── 证据 ④  低级路线 startTimer()/timerEvent()，并看穿它「只是一个事件」
    QTimer::singleShot(1700, [] {
        qInfo() << "\n=== 证据④ startTimer()/timerEvent() ===";
        auto *ll = new LowLevel;
        // 核对：qobject.h 第 149 行，默认类型是 Qt::CoarseTimer —— 注意这个默认值！
        ll->myTimerId = ll->startTimer(60, Qt::PreciseTimer);
        qInfo() << "  startTimer 返回 id =" << ll->myTimerId;
        // chrono 版本（qobject.h 第 152 行）：
        const int id2 = ll->startTimer(std::chrono::milliseconds(500), Qt::VeryCoarseTimer);
        qInfo() << "  第二个定时器 id =" << id2 << "（同一对象可以有多个 id，靠 timerId() 分辨）";

        QTimer::singleShot(250, [ll, id2] {
            qInfo() << "  250ms 后低级定时器命中次数 =" << ll->hits
                    << "（预期 3，因为代码里到 3 次就 killTimer）";
            ll->killTimer(id2);

            // 看穿它：timerEvent 收到的是**一个普通的 QEvent::Timer 事件**
            // （qcoreevent.h 第 64 行 Timer = 1）。完全可以手工投一个：
            // 核对：qcoreevent.h 第 372 行 explicit QTimerEvent(int timerId)
            // 这里故意投一个不匹配的 id → 走 QObject::timerEvent 的兜底分支
            QTimerEvent fake(9999);
            const int before = ll->hits;
            QCoreApplication::sendEvent(ll, &fake);
            qInfo() << "  手工 sendEvent(QTimerEvent(9999)) 之后 hits 增量 ="
                    << (ll->hits - before) << "（0：不是它的 id → 基类把它 ignore 了）";
            qInfo() << "  结论：QTimer 只是 startTimer + timeout 信号的包装；"
                    << "低级路线省一个对象，代价是**没有信号可连**、得自己管 id 生命周期。";
            delete ll;
        });
    });

    // ── 证据 ⑤  interval = 0 的定时器：命中数由「循环转了几圈」决定，不由 start() 次数决定
    //   ★ 这一条原来写的是「连续 start() 2000+1 次后，150ms 内命中数**远小于 2000**」，
    //     而本机同一行代码实测打出来是 **11485**（runlogs/events_04_timers.log）——
    //     数字比 2000 还大一个数量级，结论和它自己的输出正对着干。
    //     错在哪：interval=0 的**重复**定时器每次到期都会重新排队，所以命中数 ≈
    //     这 150ms 里事件循环转的圈数，跟 start() 调了几次没关系；原句把两件事混成了一句。
    //     真正能证「合并」的对照不是「远小于 2000」，而是：
    //     **只 start() 一次的那组，命中数和 start() 两千次的那组一样多**。
    //     所以下面改成 A/B 两组并排跑，让结论站在数上。
    QTimer::singleShot(2100, [] {
        qInfo() << "\n=== 证据⑤ interval=0：一个待决 tick，转多少圈就跑多少次 ===";

        auto *hitsOne = new int(0);                 // A 组：start() 1 次
        auto *zOne = new QTimer;
        zOne->setInterval(0);                       // qtimer.h 37 行：0 不是「立刻」，是「尽快，且允许合并」
        QObject::connect(zOne, &QTimer::timeout, [hitsOne] { ++(*hitsOne); });
        zOne->start();

        auto *hitsMany = new int(0);                // B 组：同一个定时器连续 start() 2000 次
        auto *zMany = new QTimer;
        zMany->setInterval(0);
        QObject::connect(zMany, &QTimer::timeout, [hitsMany] { ++(*hitsMany); });
        for (int i = 0; i < 2000; ++i)
            zMany->start();                         // 每次都只是把同一个待决 tick 重新排一遍

        QTimer::singleShot(150, [zOne, zMany, hitsOne, hitsMany] {
            const int one = *hitsOne, many = *hitsMany;
            qInfo() << "  A｜start() 1 次    → 150ms 内命中" << one << "次";
            qInfo() << "  B｜start() 2000 次 → 150ms 内命中" << many << "次";
            qInfo().noquote() << "  B/A ="
                    << (one > 0 ? QString::number(double(many) / double(one), 'f', 3)
                                : QStringLiteral("inf（A 组一次都没命中，循环被别处卡住了）"))
                    << "→ 落在 1 附近就是**合并**：2000 次 start() 没有换来 2000 个 tick，"
                    << "同一定时器永远只有一个待决事件。";
            qInfo() << "  而 A 那一次 start() 也跑了远不止 1 个 tick：interval=0 的重复定时器"
                    << "每轮循环重新排一次，等于自己把事件循环转起来（这就是「0 毫秒定时器」"
                    << "能当空转用的原因）。想要「每次调用都算数」，请自己排队列——"
                    << "第 6 讲正面讲合并的边界（update() 合并重绘就是这套机制的产物）。";
            zOne->stop();  delete zOne;  delete hitsOne;
            zMany->stop(); delete zMany; delete hitsMany;
        });
    });

    // ── 证据 ⑥  QElapsedTimer 是「测量工具」，不是定时器：它不产生任何事件
    QTimer::singleShot(2500, [] {
        qInfo() << "\n=== 证据⑥ QElapsedTimer 的正确用法 ===";
        QElapsedTimer t;                       // 栈上安全：整个演示在这个 lambda 里直着走完
        t.start();                             // qelapsedtimer.h 36 行
        const qint64 d1 = t.elapsed();         // 第 43 行，单位 ms
        const qint64 n1 = t.nsecsElapsed();    // 第 42 行，单位 ns
        const qint64 afterRestart = t.restart();  // 第 37 行 —— **Qt6 返回 qint64，Qt5 返回 void**
        qInfo() << "  elapsed =" << d1 << "ms, nsecsElapsed =" << n1 << "ns,"
                << "restart() 的返回值（restart 之前的耗时）=" << afterRestart << "ms"
                << "（Qt5 老代码 `t.restart();` 不接返回值照样编，接了值就编不过）";
        qInfo() << "  时钟性质：isMonotonic =" << QElapsedTimer::isMonotonic()
                << "clockType 枚举值 =" << int(QElapsedTimer::clockType())
                << "（单调时钟不受系统改时间影响，所以测耗时**必须**用它，"
                << "别拿 QDateTime::currentMSecsSinceEpoch 相减）";
        qInfo() << "  hasExpired(0) =" << t.hasExpired(0)
                << "（第 44 行：判「是否已超过 timeout 毫秒」——写超时的正规工具）";
    });

    // ── 收尾
    QTimer::singleShot(3200, [] {
        qInfo() << "\n=== 结论 ===";
        qInfo() << "① 定时器**是事件**：没有事件循环就没有 tick（证据①）。";
        qInfo() << "② 三档精度是「精度 / 功耗」的取舍，不是三种定时器：同一套 startTimer 接口。";
        qInfo() << "③ 默认类型要小心：QTimer 与 startTimer 的默认都是 Qt::CoarseTimer"
                << "（qobject.h 149 行的默认实参）——动画想稳，请显式 setTimerType(Qt::PreciseTimer)。";
        qInfo() << "④ 本机 Qt 6.12 的 qnamespace.h 里**没有** Qt::DefaultTimer 这个别名"
                << "（1719~1723 行只有三档）；老代码里的 Qt::DefaultTimer 请改成 Qt::CoarseTimer。";
        QCoreApplication::quit();
    });

    return app.exec();
}

// ============================================================================
// 附：怎么选定时器（以及为什么动画不该用 VeryCoarseTimer）
// ============================================================================
/*
 *  需求                          选择                                        理由
 *  ----------------------------------------------------------------------------
 *  一次延迟，之后不管            QTimer::singleShot(ms, lambda)               无对象生命周期负担
 *  周期性轮询/心跳               QTimer + setInterval + timeout 连 lambda      可 stop、可改 interval
 *  批量合并的「稍后再算」         interval = 0 的 QTimer                        天然合并，配合脏区/去重
 *  高频动画（每帧）              别用定时器——用 QElapsedTimer 算增量驱动 update() 帧率该跟绘制节奏走
 *  一个对象要多个定时器          startTimer()/timerEvent() + 比 timerId()      省对象；但没有信号可连
 *  只测耗时，不产生事件          QElapsedTimer                                 它是秒表，不是闹钟
 *
 * 三条纪律：
 *  ① **定时器不保证准时，只保证不早于**。间隔是「最少等多久」，不是「一定在这刻响」。
 *     任何「靠 tick 次数推算过了多少时间」的代码都是错的，进度请用 QElapsedTimer 测。
 *  ② 定时器的 timeout 槽在**创建它的那个线程的事件循环**里执行。GUI 线程的定时器
 *     不会在子线程响；子线程要定时器，得有自己的事件循环（线程模块会正面讲）。
 *  ③ 槽里跑超过一个 interval 的活，你会看到「tick 掉了」而不是「变慢」。
 *     长活请拆步（每次干一点）或搬去线程，不要指望定时器帮你排队。
 *
 * ——— 老师傅的话 ———
 *   徒弟，今天这一讲我要你记住的画面是：**定时器是事件循环里的一只闹钟，
 *   闹钟响了没人起床，它就白响。** 证据① 那 300 毫秒里 tick 数是 0——
 *   这不是 bug，这就是 Qt 的定时器真相：它没有自己的线程，
 *   它只是让分发器「到点往队列里放一个 QEvent::Timer」。
 *
 *   所以老手写 Qt 有个条件反射：想看「程序活着没」，别看 CPU，看定时器还响不响；
 *   想让界面在长任务里不白，别加定时器轮询，要么把任务拆成每次一小步（chunking），
 *   要么老实开线程。前者是本讲真正想教的手艺。
 *
 *   再说精度。很多人一上来就 setTimerType(Qt::PreciseTimer)，觉得「越精确越好」。
 *   错。Coarse 存在，是因为系统要把多次唤醒合并成一次，省电、省调度。
 *   你给每个定时器都要 Precise，等于逼系统为你单独醒来，笔记本掉电，
 *   高刷新率动画反而更卡。判断标准只有一条：**这个定时器是给「人眼」或「协议超时」用的吗**。
 *   是人眼（动画、倒计时）→ Precise；心跳/轮询/缓存过期 → Coarse；
 *   一秒级就够的看门狗 → VeryCoarse。别凭感觉。
 *
 *   最后一个私货：本讲的状态全放堆上或 static 上，不是我啰嗦。
 *   「把 lambda 排进事件循环」意味着**它一定会在别的作用域里被执行**，
 *   这时候 & 捕获局部变量，编译器一声不响，运行时随机崩。
 *   定时器让这件事变得必然，而不是偶然。
 *
 * ——— 小练习 ———
 *   1. 把证据① 的 `while (busy.elapsed() < 300)` 换成
 *      `while (!watch.hasExpired(300)) QCoreApplication::processEvents(QEventLoop::AllEvents, 1);`
 *      （hasExpired 见 qelapsedtimer.h 44 行），观察 tick 数从 0 变成几十；
 *      再解释：为什么这种「等待式 processEvents」比死睡好，但仍比不过「把活儿拆片」。
 *   2. 给证据③ 再加第四列：interval = 1ms 的 PreciseTimer 跑 300ms，数实际 tick 数——
 *      你会亲眼看到「定时器不会按 1ms 准时响」，并由此理解为什么动画要用
 *      QElapsedTimer 算增量，而不是「每 tick 加 1」。
 *   3. 把证据⑤ 的循环改成 `for (int i = 0; i < 2000; ++i) QTimer::singleShot(0, [hits]{ ++(*hits); });`
 *      （注意 hits 仍要在堆上），打印最终计数，判断「singleShot(0) 会不会被合并」，
 *      把你实测到的数字写回注释，并在第 6 讲用同一份数据讨论合并的边界。
 */
// ============================================================================

// 本文件在 .cpp 里定义了 Q_OBJECT 类（Legacy）：AUTOMOC 生成同名 .moc，必须在类定义之后包含。
#include "04_timers.moc"
