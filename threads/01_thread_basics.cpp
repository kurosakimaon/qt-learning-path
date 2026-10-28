// ============================================================================
// 第 1 讲 · Qt 线程的两条路：继承 QThread vs worker-object + moveToThread
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   「我要在后台干活」这句话在 Qt 里有两种写法：被用滥的**继承 QThread 重写 run()**，
//   和官方推荐的 **worker-object + moveToThread**。这一讲两条都跑一遍，把差别变成
//   可打印的事实：
//     1) run() 里**没有事件循环**。同一个 QTimer：run() 里不 exec() 就一个 tick 都不走；
//        自己 exec() 之后同一套代码就开始走。两个数字本讲都打出来——这是新手第一大坑
//        （「我在子线程建的定时器怎么不响」的根因）。
//     2) `QThread::currentThread()` 到底是谁：run() 执行在**新线程**里，且此刻
//        currentThread() == this（QThread 对象自己）；主线程里看 this->thread() 又是主线程。
//        三个身份一次打清。
//     3) worker-object 姿势的四个零件：QThread 只当**宿主**、业务对象是普通 QObject、
//        moveToThread 挂过去、跨线程**只靠信号槽**下命令、finished 之后收尸。
//     4) 中止：requestInterruption() / isInterruptionRequested() 是**协作式**置标志，
//        干活的自己轮询；terminate() 在头文件里有，但它粗暴到本讲一次都不调（下面解释）。
//     5) QThread::create()：Qt 6.3 起的第三条路。核对到的三条硬信息：它是**静态模板**、
//        返回**堆上的 QThread***（你得自己删）、并且标了 **[[nodiscard]]**。
//     6) idealThreadCount()：硬件线程数，用来解释「别开几百个 QThread」。
//   演示主线：子线程做一段**可计数**的活（每步 msleep(1) 并把下标累加进校验和 → 总和可核对），
//   主线程用 QTimer 轮询（进度只通过信号流回主线程，不共享内存），一轮跑完由 finished 信号
//   回主线程汇总，最后 qInfo() 打印步数、校验和、各线程 id 与墙钟耗时。
//
// 对应构建目标名：threads_01_thread_basics
//   find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)   # 仓库根 CMakeLists 就这么给
//   add_executable(threads_01_thread_basics threads/01_thread_basics.cpp)
//   target_link_libraries(threads_01_thread_basics PRIVATE Qt6::Widgets Qt6::Concurrent)
//   set(CMAKE_AUTOMOC ON)                    # 本文件**有 Q_OBJECT 类**，必须要 moc
//   # 手工核对：moc 01_thread_basics.cpp -o 01_thread_basics.moc
//   #           文件里那句 #include "01_thread_basics.moc" 就是 AUTOMOC 的钩子
//
// 运行前须知：
//   · 只建 **QCoreApplication**：本讲一个 widget 都不碰，不需要 GUI 平台插件；
//     命令行起来就是完整演示，全程 QTimer::singleShot 串步骤，**没有任何阻塞对话框**，
//     跑完自己 QCoreApplication::quit()。
//   · 不读文件、不联网；耗时全靠 QThread::msleep 造出来。
//   · 可核对的数：A 轮 60 步 → 校验和必须是 0+1+…+59 = **1770**；create 轮算 1+…+100 = **5050**；
//     B 轮请求 200 步、~70ms 时被 requestInterruption 拦下 → 实到步数 < 200（**设计如此**）。
//   · 本文件未在本机实测编译运行：只过了 `moc + g++ -fsyntax-only`（-std=c++17
//     -Wall -Wextra，0 error 0 warning）；**未链接、未运行**，文中「预期」皆为推理。
// ============================================================================

#include <QCoreApplication>      // QtCore/qcoreapplication.h：quit() 是它的 public slot
#include <QThread>               // QtCore/qthread.h —— 本讲主角，签名全对着它抄
#include <QObject>               // QtCore/qobject.h：thread() / moveToThread()
#include <QTimer>                // QtCore/qtimer.h：singleShot（带 context 那版）/ start / timeout
#include <QElapsedTimer>         // QtCore/qelapsedtimer.h
#include <QDeadlineTimer>        // QtCore/qdeadlinetimer.h：wait() 的新参数类型
#include <QList>
#include <QString>
#include <QStringList>
#include <QDebug>                // qInfo()

// ============================================================================
// 一、核对签名（本机 C:\msys64\ucrt64\include\qt6，Qt 6.12.0，qthread.h 全文读过）
// ============================================================================
// QThread : public QObject（第 28 行）——**它自己就是个 QObject**，这一条决定了后面所有事。
// 静态成员（第 32~36 行）：
//   第 32 行 Q_DECL_PURE_FUNCTION static Qt::HANDLE currentThreadId() noexcept;
//   第 33 行 static QThread *currentThread();
//   第 34 行 static bool isMainThread() noexcept;
//   第 35 行 static int idealThreadCount() noexcept;
//   第 36 行 static void yieldCurrentThread();
//          ← currentThreadId() 返回 Qt::HANDLE（= void*）。同一个头文件第 147~159 行的注释
//            **明写**「application code is not supposed to use that value for anything」，
//            Qt 自己只拿它判「是不是同一个线程」。所以本讲只用它比相等，并同时打印
//            QThread 对象指针与 objectName —— 三个口径互相印证，不做任何算术/排序。
// 实例成员：
//   第 38 行 explicit QThread(QObject *parent = nullptr);   第 39 行 ~QThread();
//   第 65 行 bool isFinished() const;                        第 66 行 bool isRunning() const;
//   第 68 行 void requestInterruption();                     第 69 行 bool isInterruptionRequested() const;
//   第 78 行 int loopLevel() const; ←「这条线程有没有事件循环在跑」的硬指标（没有 = 0）
//   第 80 行 bool isCurrentThread() const noexcept;
//   第 62/63 行 setPriority(Priority)/priority()（第 41~53 行 enum Priority，默认 InheritPriority）；
//   第 82/83 行 setServiceLevel(QualityOfService)（第 55~59 行 enum class：Auto/High/Eco）
// 槽（第 88~92 行，注意它们就在 `public Q_SLOTS:` 段里）：
//   void start(Priority = InheritPriority);  void terminate();  void exit(int retcode = 0);  void quit();
//          ← terminate() 头文件里确实有，但它是**平台级强杀**（类 pthread_cancel），
//            锁正持有、堆分配到一半被杀掉就是未定义行为。本讲**一次都不调**。
//            要停只有两条正路：quit()（事件循环自己退）/ requestInterruption()（活儿自己收）。
// 等待（第 95~101 行）：
//   bool wait(QDeadlineTimer deadline = QDeadlineTimer(QDeadlineTimer::Forever));
//   bool wait(unsigned long time)  ← Qt 6.12 这个重载还在（内联转调上面那个，
//            并对 time == ULONG_MAX 特判成 Forever），老代码 wait(300) 照编。
//            **GUI/主线程上永远别用无限 wait()**：它一堵，事件循环就停，而子线程那个
//            「要回主线程才能跑完」的槽反而永远跑不到 → 自己把自己堵死。
// 睡眠（第 103~106 行，**全是 static**）：sleep(unsigned long 秒)/msleep/usleep/sleep(std::chrono::nanoseconds)
// 信号（第 108~111 行）：void started(QPrivateSignal);  void finished(QPrivateSignal);
//          ← 都带 QPrivateSignal（外部发不出去），但**接的时候 lambda 不写参数就行**。
// 可覆写点（第 112~114 行，protected 段）：virtual void run();   int exec();
//          ← exec() 是 **protected**：只有 QThread 的子类在自己的 run() 里调得到，
//            主线程写 `thread->exec()` 编不过。这个事实第 2 讲还要用一次。
// create（第 85~86 行声明 + 第 132~145 行实现）：
//   template <typename Function, typename... Args>
//   [[nodiscard]] static QThread *create(Function &&f, Args &&... args);
//          ← ① **静态**；② 返回 **QThread***（堆上对象，**你自己负责删**）；
//            ③ [[nodiscard]] —— 不接返回值就是「ignoring return value」警告。
//            实现体是 std::async(std::launch::deferred, …) + createThreadImpl(…)，
//            语义 =「一个 run() 里只调用你这个函数的 QThread」，**同样没有事件循环**。
// moveToThread 的真签名（qobject.h 第 143~147 行）：
//   第 143 行 QThread *thread() const;
//   第 145 行 void moveToThread(QThread *thread);                                ← 6.7 之前
//   第 147 行 bool moveToThread(QThread *thread QT6_DECL_NEW_OVERLOAD_TAIL);      ← 本机 6.12
//          ← qtversionchecks.h 第 69 行：那个宏在 Qt6 里展开成**带默认值的尾参**，
//            所以照旧只传一个实参就能调；**返回 bool** 就把「挪没挪成功」直接告诉你。
// QTimer::singleShot 带 context 的那版（qtimer.h 第 65~71 行）：
//   template <typename Duration, typename Functor>
//   static inline void singleShot(Duration interval,
//       const typename QtPrivate::ContextTypeForFunctor<Functor>::ContextType *receiver,
//       Functor &&slot);
//          ← 第二个参数是 **context**：slot 会在**这个对象所属的那条线程**里被执行。
//            本讲就用它把「让 worker 在它自己的线程里开工」写成一行。
// ============================================================================

static QString ptrText(const void *p)
{
    return QStringLiteral("0x%1").arg(QString::number(quintptr(p), 16));
}

// 三个口径一起给：QThread 对象指针 + HANDLE + objectName
static QString who(const QString &tag)
{
    QThread *t = QThread::currentThread();
    return QStringLiteral("%1 thread对象=%2(%3) HANDLE=%4")
        .arg(tag, ptrText(t),
             t->objectName().isEmpty() ? QStringLiteral("(无名)") : t->objectName(),
             ptrText(QThread::currentThreadId()));
}

// ============================================================================
// 二、第一条路：继承 QThread 重写 run()
// ============================================================================
// 用它的三个前提（想明白再用，否则一律走第三节）：
//   ① 这条线程**从头到尾只做这一件事**，不需要事件循环给你派活；
//   ② 你不打算在这条线程上用 QTimer / 队列槽 / deleteLater / 任何 socket；
//   ③ 你接受「run() 一返回这个对象就废了，不能再次 start()」。
class BareThread : public QThread
{
    Q_OBJECT

public:
    BareThread(const QString &name, int loopBudgetMs, QObject *parent = nullptr)
        : QThread(parent), m_name(name), m_loopBudgetMs(loopBudgetMs)
    {
        setObjectName(name);                  // QObject::setObjectName：日志靠它认人
    }

    // 取证读数：run() 结束**之后**由主线程读（本讲时序保证了这一点）
    int noLoopTicks = 0;        // 证据①A：没有事件循环时的 tick
    int loopTicks = 0;          // 证据①B：exec() 之后的 tick
    int execReturn = -1;
    int loopLevelInRun = -1;
    bool timerWasActive = false;
    int interruptBreakAt = -1;

protected:
    void run() override                        // 第 113 行 virtual void run(); 一字不差
    {
        qInfo() << who(QStringLiteral("  [%1] run() 入口").arg(m_name));
        qInfo() << QStringLiteral("      currentThread()==this ? %1"
                                  "（QThread 对象在这段代码里也归这条线程）")
                       .arg(QThread::currentThread() == this ? QStringLiteral("是")
                                                             : QStringLiteral("否"));

        // ── 证据①A：不 exec()，定时器一个 tick 都不走 ──────────────────────
        // timeout 是**事件**；事件只有事件循环会送。start() 只是把定时登记进本线程的
        // 事件分发器，没人 poll 就永远躺在队列里。
        loopLevelInRun = loopLevel();           // 第 78 行
        auto *t1 = new QTimer(this);            // 在 run() 里 new → 归属**这条线程**（第 2 讲）
        QObject::connect(t1, &QTimer::timeout, this, [this]() { ++noLoopTicks; });
        t1->start(1);
        timerWasActive = t1->isActive();        // 「起来了」是真的起来了
        QThread::msleep(60);                    // 睡 60ms ≈ 本该有几十次 tick
        qInfo() << QStringLiteral("      A｜没 exec() 的 60ms：loopLevel=%1 timer.isActive=%2"
                                  " → tick=%3（**预期 0**：定时器活着 ≠ 有人给它送事件）")
                       .arg(loopLevelInRun)
                       .arg(timerWasActive ? QStringLiteral("true") : QStringLiteral("false"))
                       .arg(noLoopTicks);
        t1->stop();
        delete t1;                              // 同线程删除，安全

        // ── 证据①B：自己 exec()，同一套代码就开始走 ────────────────────────
        auto *t2 = new QTimer(this);
        QElapsedTimer et;
        QObject::connect(t2, &QTimer::timeout, this, [this, &et]() {
            ++loopTicks;
            if (et.elapsed() >= m_loopBudgetMs)
                quit();                        // 第 92 行：让 exec() 返回
        });
        t2->start(1);
        et.start();
        execReturn = exec();                     // 第 114 行 protected：只有子类调得到
        qInfo() << QStringLiteral("      B｜exec() 跑了 %1ms：tick=%2（**预期 > 0**）"
                                  "｜exec() 返回值=%3（quit() 等价 exit(0)）")
                       .arg(et.elapsed()).arg(loopTicks).arg(execReturn);
        delete t2;

        // ── 证据④：协作式中止 —— 干活的人自己看标志 ────────────────────────
        for (int i = 0; i < 1000; ++i) {
            if (isInterruptionRequested()) {     // 第 69 行 const
                interruptBreakAt = i;
                break;
            }
            QThread::msleep(1);
        }
        qInfo() << QStringLiteral("      C｜中止轮询：标志=%1，第 %2 步跳出（不设标志会跑满 1000）")
                       .arg(isInterruptionRequested() ? QStringLiteral("true") : QStringLiteral("false"))
                       .arg(interruptBreakAt);
        // run() 返回 → Qt 发 finished()，线程真结束
    }

private:
    QString m_name;
    int m_loopBudgetMs = 40;
};

// ============================================================================
// 三、第二条路（推荐）：worker-object + moveToThread
// ============================================================================
// 分工一句话背下来：
//   · QThread = **宿主**，只负责「这里有一条会跑事件循环的线程」这一件事；
//   · Worker  = **干活的人**，普通 QObject 派生、**不继承 QThread**、不知道自己在哪条线程。
//     正因为不知道，同一份 Worker 代码才能：主线程直调 / 挪进线程 / 丢进线程池（第 6 讲）。
//   · 跨线程**不许**直接调它的方法；只能通过信号槽，或带 context 的 singleShot/invokeMethod。
class Worker : public QObject
{
    Q_OBJECT

public:
    explicit Worker(const QString &name) : m_name(name) {}

public Q_SLOTS:
    // 可计数的耗时活：每步 msleep(sleepMs) 并把下标累加进校验和 → 总和是个**可核对**的数
    void work(int totalSteps, int sleepMs)
    {
        QElapsedTimer t;
        t.start();
        qInfo() << who(QStringLiteral("      [%1] work() 里").arg(m_name));
        qInfo() << QStringLiteral("          currentThread()==this->thread() ? %1"
                                  "（**必须是「是」**，否则这个槽就跑错了线程）")
                       .arg(QThread::currentThread() == thread() ? QStringLiteral("是")
                                                                : QStringLiteral("否"));
        int sum = 0;
        int done = 0;
        bool early = false;
        for (int i = 0; i < totalSteps; ++i) {
            // thread() 就是我的宿主 QThread；它被 requestInterruption() 之后这里立刻收工
            if (thread()->isInterruptionRequested()) {
                early = true;
                qInfo() << QStringLiteral("          [%1] 中断标志已置，第 %2 步收工")
                               .arg(m_name).arg(i);
                break;
            }
            sum += i;
            QThread::msleep(unsigned(sleepMs));
            done = i + 1;
            if (done % 20 == 0)
                Q_EMIT progress(done, totalSteps);      // 跨线程回主线程（Auto → 队列）
        }
        const qint64 el = t.elapsed();
        qInfo() << QStringLiteral("          [%1] work() 收尾：%2/%3 步、校验和 %4、耗时 %5 ms"
                                  "、提前收工=%6")
                       .arg(m_name).arg(done).arg(totalSteps).arg(sum).arg(el)
                       .arg(early ? QStringLiteral("true") : QStringLiteral("false"));
        Q_EMIT report(m_name, done, sum, el, early);    // 主线程据此汇总 + 让宿主 quit
    }

Q_SIGNALS:
    void progress(int done, int total);
    void report(const QString &tag, int steps, int sum, qint64 elapsedMs, bool stoppedEarly);

private:
    QString m_name;
};

// ============================================================================
// 四、主线程侧的记账本：所有数字只能「通过信号」流进来
// ============================================================================
// 为什么要有这个类，而不是主线程直接读 worker->某个成员？因为那就是**共享内存**：
// 一个线程写、另一个线程读、中间没有同步——那是第 4 讲的战场。
// Qt 的纪律：**跨线程传状态走信号，别用成员变量互读**。
class Collector : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    struct Rec { QString tag; int steps = 0; int sum = 0; qint64 ms = 0; bool early = false; };
    QList<Rec> recs;
    int progressHits = 0;
    int pollTicks = 0;
    int lastDone = 0;
    int lastTotal = 0;
    int createSum = 0;                        // create 轮的落地位置（活到 main 结束）

    void onProgress(int done, int total) { ++progressHits; lastDone = done; lastTotal = total; }
    void add(const Rec &r) { recs.append(r); }

    QString recText() const
    {
        QStringList parts;
        for (const Rec &r : recs)
            parts << QStringLiteral("%1:%2步/和%3/%4ms%5")
                         .arg(r.tag).arg(r.steps).arg(r.sum).arg(r.ms)
                         .arg(r.early ? QStringLiteral("(提前)") : QString());
        return parts.isEmpty() ? QStringLiteral("（没记到数）") : parts.join(QStringLiteral("  "));
    }
};

// ============================================================================
// 五、main：QTimer::singleShot 串起来的自动演示
// ============================================================================
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("threads_01_thread_basics"));
    QThread::currentThread()->setObjectName(QStringLiteral("main"));

    Collector col;
    QElapsedTimer wall;
    wall.start();

    qInfo() << "================================================================";
    qInfo() << "threads 第 1 讲 · Qt 线程的两条路（Qt =" << QT_VERSION_STR << "）";
    qInfo() << "================================================================";
    qInfo() << who(QStringLiteral("【0】主线程："));
    qInfo() << QStringLiteral("      isMainThread=%1 idealThreadCount()=%2"
                              "（硬件线程数；开超过它的 QThread 只增加切换成本，不会更快）")
                   .arg(QThread::isMainThread() ? QStringLiteral("true") : QStringLiteral("false"))
                   .arg(QThread::idealThreadCount());

    // ── 主线程轮询器：只读**主线程侧**的记账，不碰子线程任何状态 ────────────
    auto *poll = new QTimer(&col);
    poll->setInterval(30);
    QObject::connect(poll, &QTimer::timeout, &col, [&col]() { ++col.pollTicks; });
    poll->start();

    // ── 【1】第一条路：继承 QThread ─────────────────────────────────────────
    auto *bare = new BareThread(QStringLiteral("bare-A"), 40);
    QTimer::singleShot(10, [bare, &col]() {
        qInfo() << QStringLiteral("【1】第一条路：BareThread（继承 QThread 重写 run()）");
        QObject::connect(bare, &QThread::finished, &col, [bare]() {
            qInfo() << QStringLiteral("      finished 落回主线程：%1").arg(who("      "));
            qInfo() << QStringLiteral("      此刻 isRunning=%1 isFinished=%2"
                                      "（finished 发射时线程正在收尾）")
                           .arg(bare->isRunning() ? QStringLiteral("true") : QStringLiteral("false"))
                           .arg(bare->isFinished() ? QStringLiteral("true") : QStringLiteral("false"));
        });
        bare->start();                        // 第 89 行的槽，从主线程调合法
        qInfo() << QStringLiteral("      start() 后：isRunning=%1 isFinished=%2"
                                  "｜bare->thread()（**对象归属**）== 主线程 ? %3")
                       .arg(bare->isRunning() ? QStringLiteral("true") : QStringLiteral("false"))
                       .arg(bare->isFinished() ? QStringLiteral("true") : QStringLiteral("false"))
                       .arg(bare->thread() == QThread::currentThread() ? QStringLiteral("是")
                                                                       : QStringLiteral("否"));
        QTimer::singleShot(25, [bare]() {
            qInfo() << QStringLiteral("      主线程 → requestInterruption()："
                                      "**只是置标志**，不动栈、不抢锁 —— 这就是它和 terminate() 的区别");
            bare->requestInterruption();
        });
    });

    // ── 【2】第二条路 A 轮（不中断，数字必须精确）+ 回收 bare ───────────────
    QTimer::singleShot(600, [bare, &col]() {
        qInfo() << QStringLiteral("【2】第二条路：worker-object + moveToThread（A 轮）");
        qInfo() << QStringLiteral("      bare 的取证：tick(无循环)=%1 tick(有循环)=%2 loopLevel=%3"
                                  " exec()返回=%4 中断跳出=%5")
                       .arg(bare->noLoopTicks).arg(bare->loopTicks)
                       .arg(bare->loopLevelInRun).arg(bare->execReturn).arg(bare->interruptBreakAt);
        const bool waited = bare->wait(QDeadlineTimer(200));   // 已 finished → 预期立刻 true
        qInfo() << QStringLiteral("      wait(QDeadlineTimer(200)) = %1（false = 超时没等到；"
                                  "**GUI 线程别用无限 wait**）")
                       .arg(waited ? QStringLiteral("true") : QStringLiteral("false"));
        delete bare;                                             // 线程结束后删宿主，安全

        auto *hostA = new QThread;
        hostA->setObjectName(QStringLiteral("host-A"));
        auto *workerA = new Worker(QStringLiteral("worker-A"));

        // ① 先把管道连好：worker 的两个信号 → 主线程的 Collector（context = &col）
        QObject::connect(workerA, &Worker::progress, &col, &Collector::onProgress);
        QObject::connect(workerA, &Worker::report, &col,
                         [&col, hostA](const QString &, int steps, int sum, qint64 ms, bool early) {
                             qInfo() << QStringLiteral("      [主线程] report 到达：%1 步、校验和 %2、"
                                                       "耗时 %3 ms｜%4")
                                            .arg(steps).arg(sum).arg(ms).arg(who("      "));
                             col.add(Collector::Rec{QStringLiteral("A"), steps, sum, ms, early});
                             hostA->quit();                       // 让宿主的 exec() 收尾
                         });
        // ② 收尸：Qt 的标准写法是 finished → deleteLater（两个对象都这样），
        //    因为 finished 发射时线程还在收尾，**当场 delete** 就是在收尾中的线程上删对象。
        QObject::connect(hostA, &QThread::finished, hostA, &QObject::deleteLater);
        QObject::connect(hostA, &QThread::finished, workerA, &QObject::deleteLater);
        QObject::connect(hostA, &QThread::finished, &col, [hostA, &col]() {
            qInfo() << QStringLiteral("      host-A finished：%1｜本函数**不再读 workerA 的成员**，"
                                      "数字全从信号里拿（那是跨线程状态）")
                           .arg(who("      "));
            Q_UNUSED(hostA)
            Q_UNUSED(col)
        });

        // ③ 挪过去（前提：workerA **没有 parent** —— 第 2 讲正面讲这条铁律）
        const bool moved = workerA->moveToThread(hostA);          // qobject.h 第 147 行，返回 bool
        qInfo() << QStringLiteral("      moveToThread(hostA) 返回 %1｜挪后 worker->thread()==hostA ? %2"
                                  "（**归属已改，但还没有循环在跑**）")
                       .arg(moved ? QStringLiteral("true") : QStringLiteral("false"))
                       .arg(workerA->thread() == hostA ? QStringLiteral("是") : QStringLiteral("否"));
        hostA->start();                       // ④ 先起宿主，再下命令（反了也行：命令会排队等循环）
        QTimer::singleShot(0, workerA, [workerA]() {            // context=workerA → 在 A 线程执行
            workerA->work(60, 1);
        });
    });

    // ── 【3】B 轮：同一套 worker，这次中途 requestInterruption ──────────────
    QTimer::singleShot(1100, [&col]() {
        qInfo() << QStringLiteral("【3】B 轮：requestInterruption 拦下一轮活");
        auto *hostB = new QThread;
        hostB->setObjectName(QStringLiteral("host-B"));
        auto *workerB = new Worker(QStringLiteral("worker-B"));

        QObject::connect(workerB, &Worker::progress, &col, &Collector::onProgress);
        QObject::connect(workerB, &Worker::report, &col,
                         [&col, hostB](const QString &, int steps, int sum, qint64 ms, bool early) {
                             qInfo() << QStringLiteral("      [主线程] B 轮 report：%1/200 步、和 %2、"
                                                       "%3 ms、提前=%4")
                                            .arg(steps).arg(sum).arg(ms)
                                            .arg(early ? QStringLiteral("true") : QStringLiteral("false"));
                             col.add(Collector::Rec{QStringLiteral("B"), steps, sum, ms, early});
                             hostB->quit();
                         });
        QObject::connect(hostB, &QThread::finished, hostB, &QObject::deleteLater);
        QObject::connect(hostB, &QThread::finished, workerB, &QObject::deleteLater);
        QObject::connect(hostB, &QThread::finished, &col, [hostB, &col]() {
            qInfo() << QStringLiteral("      host-B finished：%1｜轮询器共 tick %2 次、"
                                      "收到 progress 信号 %3 次、最后一次看到 %4/%5")
                           .arg(who("      "))
                           .arg(col.pollTicks).arg(col.progressHits)
                           .arg(col.lastDone).arg(col.lastTotal);
            Q_UNUSED(hostB)
        });

        workerB->moveToThread(hostB);
        hostB->start();
        QTimer::singleShot(0, workerB, [workerB]() { workerB->work(200, 1); });
        QTimer::singleShot(70, [hostB]() {
            qInfo() << QStringLiteral("      70ms 到 → hostB->requestInterruption()"
                                      "（标志在**宿主**上，看标志的是**工人**）");
            hostB->requestInterruption();
        });
    });

    // ── 【4】QThread::create：第三条路（可调用对象，无事件循环）─────────────
    QTimer::singleShot(1600, [&col]() {
        qInfo() << QStringLiteral("【4】第三条路：QThread::create（第 85 行：static + [[nodiscard]]）");
        // 累加值**必须**放在活到最后的对象里（col.createSum）：把 main 里某个 lambda 的局部变量
        // 按引用带进子线程，是教科书级的悬垂引用 —— 这里就故意用 col 避开。
        QThread *th = QThread::create([&col]() {
            int s = 0;
            for (int i = 1; i <= 100; ++i)
                s += i;
            col.createSum = s;                 // 1..100 = 5050（可核对）
            qInfo() << QStringLiteral("      create 的 lambda 跑在：%1").arg(who("      "));
        });
        QObject::connect(th, &QThread::finished, &col, [th, &col]() {
            qInfo() << QStringLiteral("      create 线程结束：sum=%1（**预期 5050**）isFinished=%2"
                                      " → 现在可以由**创建它的线程**删掉")
                           .arg(col.createSum)
                           .arg(th->isFinished() ? QStringLiteral("true") : QStringLiteral("false"));
            col.add(Collector::Rec{QStringLiteral("create"), 100, col.createSum, 0, false});
            delete th;                          // create() 给的裸指针：谁接住谁负责
        });
        th->start();
    });

    // ── 【5】总汇总并退出 ───────────────────────────────────────────────────
    QTimer::singleShot(2100, [&col, poll, &wall]() {
        qInfo() << QStringLiteral("【5】总汇总");
        poll->stop();
        qInfo() << QStringLiteral("      各轮结果：%1").arg(col.recText());
        qInfo() << QStringLiteral("      核对：A 轮应 60 步/和 1770；create 轮 sum 应 5050；"
                                  "B 轮步数 < 200（被中断）");
        qInfo() << QStringLiteral("      主线程记账：progress 信号 %1 次、轮询 tick %2 次、"
                                  "最后进度 %3/%4")
                       .arg(col.progressHits).arg(col.pollTicks)
                       .arg(col.lastDone).arg(col.lastTotal);
        qInfo() << QStringLiteral("      %1").arg(who("      主线程："));
        qInfo() << QStringLiteral("      墙钟耗时：%1 ms").arg(wall.elapsed());
        qInfo() << QStringLiteral("      一句话：run() 那条路只适合「一条线程一件死事」；"
                                  "worker+moveToThread 才让你能用 QTimer、队列槽、并复用代码。");
        QCoreApplication::quit();
    });

    return app.exec();
}

// 本文件有 Q_OBJECT 类（BareThread / Worker / Collector），AUTOMOC 会生成并在这里包含。
#include "01_thread_basics.moc"

// ——— 老师傅的话 ———
/*
 * 徒弟，「继承 QThread 然后重写 run()」这个写法在中文教程里被讲成了 Qt 线程的**标准姿势**，
 * 这是这些年流传最广的一个误会。你把它当成标准姿势，就会掉进本讲证据①那个坑：
 *
 *   run() 里没有事件循环。
 *
 * QThread 的默认 run() 实现就一句 exec()（第一节第 113/114 行那一对照）。你一覆写，
 * 就把 Qt 唯一那段「给你把事件循环跑起来」的代码顶掉了。后果不是「不方便」，是**成片的功能失效**：
 * QTimer 一次不响（本讲 A 组 tick=0，B 组 tick>0，同一份代码，差别只在 exec()）、
 * 队列连接的槽永远不执行、deleteLater() 的对象永远不释放、QAbstractSocket 的 readyRead 不来。
 * 而且它**不报错**——这是最阴的地方，编译过、跑得起、只是什么都不发生。
 *
 * 所以我给你判据，别背「哪种更地道」：
 *   · 你的线程体是**一段有始有终的函数**（跑一次就算完）→ 用 QThread::create()，
 *     或者直接 std::thread + 结果用信号回主线程（第 5 讲 QtConcurrent 更省事）。
 *   · 你的线程要**长期存在、随时接活**（轮询设备、监听 socket、跑状态机）→
 *     worker-object + moveToThread，因为你必须要那个事件循环。
 *   · 只有「你要完全掌控线程入口、且明确不需要事件循环」时，才去继承 QThread 重写 run()。
 *
 * 第二条要拎清的是**「谁在哪个线程」和「谁属于哪个线程」是两件事**。
 * QThread 对象本身属于创建它的线程（主线程），但它的 run() 在新线程里执行，
 * 那一刻 QThread::currentThread() 就是它自己。本讲把这两个身份分别打了出来。
 * 你把「执行线程」和「对象归属线程」混起来，就会在子线程里给主线程的对象直接调方法 ——
 * 那就是崩溃的种子，第 2 讲专门讲这件事。
 *
 * 第三条：中止只有**协作式**这一条正路。requestInterruption() 只是把一个标志置起来，
 * 干活的人每轮开头看一眼（本讲 C 组：标志为真时第 N 步跳出）。terminate() 是平台级强杀，
 * 锁可能正握着、堆可能正分配到一半——它是历史包袱，不是给你的方案。
 * wait() 也不是中止手段，它是**回收**手段；而且带超时是底线——GUI 线程上一次无限 wait()，
 * 就是一个「本机偶发无响应」的工单。
 *
 * 最后一条是我最想让你带走的：**进度别用共享变量**。本讲 worker 一个字都没写进 Collector，
 * 全靠 progress/report 两个信号把数字搬回主线程。你写成「主线程读 worker->m_done」，
 * 那就是两个线程无同步地读写同一个 int —— 第 4 讲的整个主题就是从这一行开始的。
 * Qt 给你的正确工具，就是信号槽。
 */

// ——— 小练习 ———
/*
 *   1. 把证据①A 的 `QThread::msleep(60)` 换成「循环 60 次每次 msleep(1)」，tick 仍应为 0；
 *      再在循环里手动加一句 `QCoreApplication::processEvents()`，看 tick 变了没有，
 *      用一句话解释「loopLevel()==0 的线程上，timeout 事件到底卡在哪」。
 *   2. 把 A 轮的 `QTimer::singleShot(0, workerA, ...)` 换成不带 context 的写法
 *      （`QTimer::singleShot(0, [workerA]{ workerA->work(60,1); })`），观察打印出的线程名
 *      从 host-A 变回 main，并解释 work() 里 `currentThread()==thread()` 为什么变成「否」。
 *   3. 给 Worker::work() 的 progress 加限流（用 QElapsedTimer，每 25ms 最多发一次），
 *      把 A 轮改成 1000 步，对比 progressHits 的量级；再说明「进度发太密」为什么卡主线程。
 *   4. 把 B 轮的 requestInterruption 时间改成 5ms 与 300ms，观察 steps/early 的变化；
 *      再把 msleep(1) 改成 msleep(50)，解释「中断最快也要一个循环周期」。
 */
// ============================================================================
