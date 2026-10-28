// ============================================================================
// 第 2 讲 · 线程亲和性：一个 QObject 到底「归」哪条线程
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   第 1 讲你已经会把活丢进子线程了。这一讲回到 Qt 多线程的**第一原理**：
//   每个 QObject 心里都揣着一条线程（内部那份 threadData），这叫**线程亲和性**
//   （thread affinity）。Qt 的几乎所有「本机偶发崩溃」都能追溯到「你以为对象归 A，
//   其实它归 B」。本讲把这条原理拆成六个可打印的事实：
//     1) **谁创建对象，对象就归谁**：在主线程 new 出来的 QObject / QTimer，
//        它的 thread() 就是主线程 —— 跟你打算把它交给哪条线程干活**毫无关系**。
//        这是新手第二大坑（第一大坑是第 1 讲那个「run() 里没有事件循环」）。
//     2) `QObject::thread()` 读的就是这份归属；`moveToThread()` 改它。
//        核对到的硬信息：本机 qobject.h 第 143 行 `QThread *thread() const;`、
//        第 145 行的**旧版 void 返回**被包在 `#if QT_CORE_REMOVED_SINCE(6, 7)` 里，
//        第 147 行才是本机 6.12 真正用的 **`bool moveToThread(QThread *)`** ——
//        返回值直接把「挪没挪成功」告诉你，别再靠猜。
//     3) 挪的前提与后果：对象**不能有 parent**（有 parent 就挪不动 —— 归属要跟父亲一致，
//        这是 Qt 的对象树模型定的）；挪完还得看「有没有事件循环在跑」，
//        用 `QThread::loopLevel()`（qthread.h 第 78 行）当场验证。
//     4) **QTimer 必须由它所属线程来 start()**：本讲的正例是把定时器 `new` 在 worker 的槽里
//        （槽跑在 worker 线程 → 定时器天然归属 worker 线程），tick 就一路走；
//        反面教材是在主线程 new 一个定时器再 moveToThread 到子线程、然后在**主线程**调 start()。
//     5) **对象在别的线程被 delete 的代价**：析构函数在调用 delete 的那条线程上跑，
//        而它要放开的资源、它要摘的 parent、它发的 destroyed 信号全都按「对象归属线程」的
//        假设写的 —— 于是变成随机崩。正路是 `deleteLater()`（qobject.h 第 364 行的
//        public slot），它投递一个**队列事件**给对象所属线程，让那条线程自己收尸。
//     6) **GUI widget 只能在主线程碰**：本讲用 QCoreApplication 一个 widget 都不建，
//        但把「为什么崩溃常来自这里」讲透（见第一节最后一段与「老师傅的话」）。
//   演示主线：把 worker moveToThread 之后，让 worker **在自己的线程里**建并启动 QTimer，
//   每次 tick 通过信号把「当时 QThread::currentThread() 的句柄」与「worker->thread() 的句柄」
//   搬到主线程打印，逐条证明事件确实跑在正确的线程上；再把 deleteLater 跨线程调用一次，
//   看析构函数**打印出自己跑在哪条线程**。全程 QTimer::singleShot 串步骤，末尾 qInfo()
//   给可核对的计数与耗时，然后 QCoreApplication::quit()。
//
// 对应构建目标名：threads_02_thread_affinity
//   find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)   # 仓库根 CMakeLists 就这么给
//   add_executable(threads_02_thread_affinity threads/02_thread_affinity.cpp)
//   target_link_libraries(threads_02_thread_affinity PRIVATE Qt6::Widgets Qt6::Concurrent)
//   set(CMAKE_AUTOMOC ON)                    # 本文件**有 Q_OBJECT 类**，必须要 moc
//   # 手工核对：moc 02_thread_affinity.cpp -o 02_thread_affinity.moc
//   #           文件末尾那句 #include "02_thread_affinity.moc" 就是 AUTOMOC 的钩子
//
// 运行前须知：
//   · 只建 **QCoreApplication**：本讲不碰任何 widget（正是要用「无 GUI」的干净环境
//     把归属问题讲纯；GUI 那一条只在注释里讲）。全程 QTimer::singleShot 串步骤，
//     **没有任何阻塞对话框**，跑完自己 QCoreApplication::exit(Ledger::exitCode())。
//   · 【4】那一组是**故意**踩一次「从别的线程 start 一个归属子线程的定时器」：
//     按 Qt6 的实现，这里预期得到**一条运行时 qWarning**（文案里含 "another thread"），
//     程序不会崩；本讲只把 isActive()/loopLevel()/tick 数原样打出来，
//     **不对结果做排序式承诺**（不同小版本措辞与行为略有差异，见该处「待复核」注释）。
//   · 不读文件、不联网；耗时全靠 QThread::msleep 与定时器间隔造出来。
//   · 可核对的数：正例预算 12 次 tick（间隔 20ms）→ `ownTicks` 应为 12；
//     每次 tick 的「currentThread 句柄 == worker->thread() 句柄」判定应**全为真**。
//     这些结论不只在屏幕上打一行：每一项都登记进 Ledger，末尾以退出码 0/1 表态，
//     ctest 只看退出码。12 次 tick 有约 500 ms 余量（第 12 次落在 ~400 ms，汇总在 900 ms）。
//   · 本文件未在本机实测编译运行：只过了 `moc + g++ -fsyntax-only`（-std=c++17
//     -Wall -Wextra，0 error 0 warning）；**未链接、未运行**，文中「预期」皆为推理。
// ============================================================================

#include <QCoreApplication>      // QtCore/qcoreapplication.h：quit() 是 public slot
#include <QObject>               // QtCore/qobject.h —— 本讲主角：thread() / moveToThread()
#include <QThread>               // QtCore/qthread.h：currentThread / currentThreadId / loopLevel
#include <QTimer>                // QtCore/qtimer.h：start(int) / stop() / isActive() / singleShot
#include <QElapsedTimer>
#include <QDeadlineTimer>        // wait(QDeadlineTimer) —— 第 1 讲核对过
#include <QString>
#include <QStringList>
#include <QDebug>

// ============================================================================
// 零、硬账：退出码 = 结论
// ----------------------------------------------------------------------------
//   归属判断、moveToThread 的返回值、tick 是否全落在 worker 线程、宿主是否停干净，
//   这些都登记进 Ledger。末尾 exit(Ledger::exitCode())：全过 0，有一项不对 1。
//   规矩同 threads/06：一项都没核（total == 0）也算失败。
// ============================================================================
namespace {

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

} // namespace

// ============================================================================
// 一、核对签名（本机 C:\msys64\ucrt64\include\qt6，Qt 6.12.0）
// ============================================================================
// ── qobject.h ──────────────────────────────────────────────────────────────
//   第 143 行  QThread *thread() const;
//          ← 读的是「亲和性」，不是「现在谁在执行我的代码」。这两件事**可以不一致**，
//            不一致的那一刻就是 bug（本讲【2】把两个口径同时打出来）。
//            它是 const、无锁地读一个内部指针：从别的线程调它本身不崩，但你读到的值
//            可能正被 moveToThread 改 —— 所以**只在时序上安全的地方读**（本讲所有跨线程
//            读数都靠信号搬，不在主线程直接读子线程对象的成员）。
//   第 144~146 行  #if QT_CORE_REMOVED_SINCE(6, 7) / void moveToThread(QThread *thread); / #endif
//   第 147 行  bool moveToThread(QThread *thread QT6_DECL_NEW_OVERLOAD_TAIL);
//          ← qtversionchecks.h 第 69 行：那个宏 = `QT6_ONLY(, QT6_DECL_NEW_OVERLOAD)`，
//            而第 68 行 QT6_DECL_NEW_OVERLOAD = `Qt::Disambiguated_t = Qt::Disambiguated`，
//            即**带默认值的尾参** → 照旧只传一个实参就能调，且**返回 bool**。
//   第 363~364 行  public Q_SLOTS: void deleteLater();
//          ← 是**槽**：可以在别的线程通过队列调用（本讲【5】正是这么用的对手戏）。
//   第 134 行  inline bool isWidgetType() const { return d_ptr->isWidget; }
//          ← 本讲用它当「这东西是不是 widget」的现场指标（我们的对象全是 false，
//            而 false 恰恰说明：这里崩不了，但换成 QWidget 就是另一回事了）。
//   第 149 行  int startTimer(int interval, Qt::TimerType timerType = Qt::CoarseTimer);
//   第 156 行  void killTimer(int id);   ← 裸定时器 id，QTimer 就是靠它实现的；
//            本讲不直接用（用 QTimer 类），但记住「定时器是登记在**所属线程**的分发器上的」。
// ── qthread.h（第 1 讲已通读，这里只拎本讲要用的）─────────────────────────
//   第 32 行 static Qt::HANDLE currentThreadId() noexcept;  第 33 行 static QThread *currentThread();
//   第 34 行 static bool isMainThread() noexcept;           第 35 行 static int idealThreadCount();
//   第 78 行 int loopLevel() const;   ← 「这条线程有没有事件循环在跑」的硬指标（0 = 没有）
//   第 80 行 bool isCurrentThread() const noexcept;  ← 比 currentThread()==this 干净，用它
//   第 92 行 void quit();  第 95 行 bool wait(QDeadlineTimer = Forever);
//   第 108~110 行 信号 started(QPrivateSignal) / finished(QPrivateSignal)
//   关于 currentThreadId() 的返回值：同一个头文件第 147~159 行的注释**明写**
//   「application code is not supposed to use that value for anything」，Qt 自己只拿它
//   判「是不是同一条线程」。所以本讲打印它只为**比相等**，不做算术、不排序、不当 key 存。
// ── qtimer.h ──────────────────────────────────────────────────────────────
//   第 110 行 void start(int msec);   第 112 行 void start();   第 113 行 void stop();
//   第 32  行 bool isActive() const;  第 116 行 timeout(QPrivateSignal) 信号
//   第 60/66 行 singleShot(Duration, const QObject *context, Functor&&)  ← 带 context：
//            slot 在**context 所属线程**执行（本讲用它把「开工令」送进 worker 线程）。
// ── 关于 parent 与归属（**文档级铁律**，头文件里看不到断言文案）────────────
//   · 有 parent 的对象**不能** moveToThread：对象树的归属必须自洽，Qt 在这里会拦你
//     （不同 Qt 版本是断言还是返回 false，措辞不一 —— **待复核**，本讲一律用「无 parent」
//     的 worker 绕开这条，并且把这条写成纪律而不是靠试）。
//   · 反过来：`child->setParent(workerInOtherThread)` 也是跨线程归属错配，同样别做。
//   · moveToThread 会**把对象已有的一堆东西一起改户口**（定时器、待发的事件），
//     所以「挪」这件事只应在对象**空闲**时做：第 1 讲那条「先连好线再挪、挪完再开工」的顺序
//     就是为这个。
// ── GUI widget 为什么只能在主线程碰 ────────────────────────────────────────
//   QWidget 是 QObject，于是它也有归属；而 QtWidgets 的约定是**整个 widget 树归属主线程**
//   （创建 QApplication 的那条线程）。它的实现里到处是「当前线程 == 对象的 GUI 线程」这类
//   断言/检查（不同版本宏名不一：ASSERT_MAIN_THREAD_WIDGET / senderThread()==thread() 之类，
//   具体文案**待复核**），触发时要么当场 abort、要么静默画错。
//   更实际的一条：你在 worker 里 `label->setText(...)`，它做的第一件事是标脏 + 投递重绘事件，
//   而**只有主线程的事件循环会去画**；于是「改了没反应」和「主线程正好在别的地方读到半个
//   状态」两种后果随机出现。正路永远是：worker 发信号 → 主线程的槽碰 widget。
// ============================================================================

static QString ptrText(const void *p)
{
    return QStringLiteral("0x%1").arg(QString::number(quintptr(p), 16));
}

// 句柄统一转成 qint64 才好用信号搬（qint64 是 Qt 内建元类型，跨线程零成本）
static qint64 hnd(const void *p)
{
    return qint64(quintptr(p));
}

// 三个口径一起给：QThread 对象指针 + HANDLE + objectName（照第 1 讲的取证姿势）
static QString who(const QString &tag)
{
    QThread *t = QThread::currentThread();
    return QStringLiteral("%1 thread对象=%2(%3) HANDLE=%4")
        .arg(tag, ptrText(t),
             t->objectName().isEmpty() ? QStringLiteral("(无名)") : t->objectName(),
             ptrText(QThread::currentThreadId()));
}

// 一行讲清一个对象的归属：它是谁、它归谁、现在谁在执行
static QString affinityText(const QString &label, const QObject *obj)
{
    const QThread *own = obj->thread();
    return QStringLiteral("      %1 类型=%2 isWidgetType=%3 parent=%4 归属线程=%5(%6)｜"
                          "归属==当前线程? %7")
        .arg(label)
        .arg(QString::fromLatin1(obj->metaObject()->className()))
        .arg(obj->isWidgetType() ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(obj->parent() ? ptrText(obj->parent()) : QStringLiteral("(无 parent)"),
             ptrText(own),
             own && !own->objectName().isEmpty() ? own->objectName() : QStringLiteral("(无名)"))
        .arg(own == QThread::currentThread() ? QStringLiteral("是") : QStringLiteral("否"));
}

// ============================================================================
// 二、会自己喊「我在哪条线程被析构」的小对象
// ============================================================================
class DtorProbe : public QObject
{
    Q_OBJECT

public:
    explicit DtorProbe(const QString &name) : m_name(name)
    {
        setObjectName(name);
    }

    ~DtorProbe() override
    {
        // 这一行就是本讲【5】的证据：**析构函数跑在哪条线程**，取决于
        // 「谁触发了这次销毁」，而正路是让「对象所属线程」自己销毁它。
        qInfo() << QStringLiteral("      DtorProbe(%1) 析构：本线程=%2 我的归属线程=%3 一致? %4")
                       .arg(m_name, who(QStringLiteral("        ")),
                            ptrText(thread()),
                            thread() == QThread::currentThread() ? QStringLiteral("是")
                                                                 : QStringLiteral("否"));
    }

private:
    QString m_name;
};

// ============================================================================
// 三、worker：所有「碰自己」的动作都放在**槽里**，于是天然落在正确的线程上
// ============================================================================
class AffinityProbe : public QObject
{
    Q_OBJECT

public:
    explicit AffinityProbe(const QString &name) : m_name(name)
    {
        setObjectName(name);
    }

    // 取证读数（**只由 worker 线程写**，主线程只通过信号读，不直接访问成员）
    int ownTicks = 0;             // 正例：在自己线程里建的定时器 tick 数
    bool everyTickOnOwnThread = true;
    int childBorn = 0;            // 在 worker 线程里 new 出来的子对象个数

    QString bornThreadText;       // 「我是谁 new 的」这件事的现场快照

Q_SIGNALS:
    // 参数全是内建元类型（int / qint64 / bool / QString），跨线程队列连接零门槛
    void ticked(int seq, qint64 curThreadHandle, qint64 ownThreadHandle,
                bool sameAsOwnThread, qint64 threadObjAddr);
    void note(const QString &text);

public Q_SLOTS:
    // ── 正例：在**这个槽所属的线程**里 new QTimer 并 start ─────────────────
    void startOwnTimer(int intervalMs, int budget)
    {
        m_budget = budget;
        qInfo() << QStringLiteral("【2】startOwnTimer() 槽开跑：%1").arg(who("      "));
        qInfo() << affinityText(QStringLiteral("执行这个槽的线程上，worker 自己是："), this);

        // 关键一句：**在这里 new**，所以这个 QTimer 的归属线程 = 当前线程 = worker 的线程。
        // 「谁创建归谁」不是口号：它 new 的那一刻读的是当前线程的 threadData。
        m_timer = new QTimer(this);
        qInfo() << affinityText(QStringLiteral("槽里 new 的 QTimer："), m_timer);
        qInfo() << QStringLiteral("      定时器 parent==worker ? %1｜parent 同线程 → 合法"
                                  "（对照：把 parent 设成**别的线程**上的对象是另一类错配，别做）")
                       .arg(m_timer->parent() == this ? QStringLiteral("是") : QStringLiteral("否"));

        QObject::connect(m_timer, &QTimer::timeout, this, [this]() { onTick(); });
        m_timer->start(intervalMs);        // start 与归属线程一致 → 一路走
        qInfo() << QStringLiteral("      start(%1) 后：isActive=%2｜worker->thread()->loopLevel=%3"
                                  "（>0 才有人给它送 timeout 事件）")
                       .arg(intervalMs)
                       .arg(m_timer->isActive() ? QStringLiteral("true") : QStringLiteral("false"))
                       .arg(thread()->loopLevel());
        Q_EMIT note(QStringLiteral("正例定时器已在 worker 线程里 start（间隔 %1 ms，预算 %2 次）")
                        .arg(intervalMs).arg(budget));
    }

    // ── 反面教材：主线程**直接调**这个方法（不是信号、不是队列）────────────
    void directCallFromMain(const QString &stage)
    {
        // 这就是「对象归 host、代码却在 main 的栈上跑」的那一刻。
        // 它没崩，是因为本函数只读只打印；一旦这里换成碰 m_timer、碰 widget、碰 socket，
        // 你就是在两条线程上同时动同一份状态。
        qInfo() << QStringLiteral("%1 直接调用（**跨线程裸调**）：当前线程=%2 我的归属=%3 一致? %4")
                       .arg(stage, who(QStringLiteral("      ")), ptrText(thread()))
                       .arg(QThread::currentThread() == thread() ? QStringLiteral("否 → 是「是」才对")
                                                                 : QStringLiteral("是"));
        ++m_directCalls;
    }

    // ── 在 worker 线程里造一个「以后要跨线程收尸」的对象 ───────────────────
    void adoptChildren(int count)
    {
        for (int i = 0; i < count; ++i) {
            // 在**这条线程**里 new → 归属这条线程；parent = this（同线程，合法）
            auto *p = new DtorProbe(QStringLiteral("child-%1").arg(childBorn + i));
            p->setParent(this);
        }
        childBorn += count;
        qInfo() << QStringLiteral("      在 worker 线程 new 了 %1 个 DtorProbe，"
                                  "它们现在的归属线程=%2（==worker 的归属，所以将来也得由"
                                  "这条线程收尸）").arg(count).arg(ptrText(thread()));
    }

private:
    void onTick()
    {
        ++ownTicks;
        const bool same = QThread::currentThread() == thread();
        if (!same)
            everyTickOnOwnThread = false;

        if (ownTicks == 1 || ownTicks == 6 || ownTicks == 12 || ownTicks >= m_budget)
            qInfo() << QStringLiteral("      tick #%1 在 worker 侧看的现场：%2")
                           .arg(ownTicks).arg(who(QStringLiteral("        ")));

        if (ownTicks >= m_budget && m_timer) {
            m_timer->stop();
            qInfo() << QStringLiteral("      tick 预算用满 %1 次 → 在**自己线程**里 stop()："
                                      "isActive=%2｜然后请宿主 quit")
                           .arg(m_budget)
                           .arg(m_timer->isActive() ? QStringLiteral("true") : QStringLiteral("false"));
            Q_EMIT finishedWork(ownTicks);
        }

        // 把「worker 侧读到的两个句柄」搬回主线程打印 —— 这就是跨线程取证的唯一正路
        Q_EMIT ticked(ownTicks, hnd(QThread::currentThread()), hnd(thread()), same,
                      hnd(QThread::currentThread()));
    }

Q_SIGNALS:
    void finishedWork(int totalTicks);

private:
    QString m_name;
    QTimer *m_timer = nullptr;
    int m_budget = 0;
    int m_directCalls = 0;
};

// ============================================================================
// 四、主线程侧的记账本（所有数字只能「通过信号」流进来）
// ============================================================================
class Collector : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    int mainSeenTicks = 0;
    int mainAllSame = 0;
    int mismatch = 0;
    int badTicks = 0;             // 【4】那组「从别的线程 start」的 tick 计数
    QString lastTickLine;

    void onTick(int seq, qint64 curHandle, qint64 ownHandle, bool same, qint64 threadObjAddr)
    {
        ++mainSeenTicks;
        if (same)
            ++mainAllSame;
        else
            ++mismatch;
        lastTickLine = QStringLiteral("tick #%1：worker 侧 currentThread=%2｜worker->thread()=%3"
                                      "｜相等? %4｜QThread 对象=%5")
                           .arg(seq).arg(ptrText(reinterpret_cast<void *>(curHandle)))
                           .arg(ptrText(reinterpret_cast<void *>(ownHandle)))
                           .arg(same ? QStringLiteral("是") : QStringLiteral("否"))
                           .arg(ptrText(reinterpret_cast<void *>(threadObjAddr)));
        // 主线程打印：注意这里打印的是**信号里搬来的值**，不是我去子线程读的
        qInfo() << QStringLiteral("  [主线程 %1] 收到 → %2")
                       .arg(who(QStringLiteral(""))).arg(lastTickLine);
    }
};

// ============================================================================
// 五、main：QTimer::singleShot 串起来的自动演示
// ============================================================================
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("threads_02_thread_affinity"));
    QThread *mainThread = QThread::currentThread();
    mainThread->setObjectName(QStringLiteral("main"));

    Collector col;
    QElapsedTimer wall;
    wall.start();

    qInfo() << "================================================================";
    qInfo() << "threads 第 2 讲 · 线程亲和性（Qt =" << QT_VERSION_STR << "）";
    qInfo() << "================================================================";
    qInfo() << who(QStringLiteral("【0】主线程："));
    qInfo() << QStringLiteral("      isMainThread=%1 idealThreadCount()=%2"
                              "｜本讲只建 QCoreApplication，一个 widget 都不碰")
                   .arg(QThread::isMainThread() ? QStringLiteral("true") : QStringLiteral("false"))
                   .arg(QThread::idealThreadCount());

    // 宿主线程 + worker（**都**在主线程 new —— 于是**都**先归主线程，这正是本讲的第一课）
    auto *host = new QThread;
    host->setObjectName(QStringLiteral("host"));
    auto *probe = new AffinityProbe(QStringLiteral("probe"));

    // 【4】反面组：在主线程 new 的定时器（归属=主线程），后面 moveToThread 到 host
    auto *badTimer = new QTimer;
    badTimer->setObjectName(QStringLiteral("bad-timer"));

    // 连线：probe 的信号 → 主线程的 Collector（context=&col → 槽在主线程跑）
    QObject::connect(probe, &AffinityProbe::ticked, &col,
                     [&col](int seq, qint64 cur, qint64 own, bool same, qint64 objAddr) {
                         col.onTick(seq, cur, own, same, objAddr);
                     });
    QObject::connect(probe, &AffinityProbe::note, &col, [&col](const QString &s) {
        qInfo() << QStringLiteral("  [主线程] 记事：%1").arg(s);
    });
    QObject::connect(probe, &AffinityProbe::finishedWork, &col, [host](int total) {
        qInfo() << QStringLiteral("  [主线程] worker 报完成 %1 次 tick → host->quit()").arg(total);
        host->quit();
    });

    // ── 【1】创建即归属 ────────────────────────────────────────────────────
    QTimer::singleShot(30, [&col, host, probe, badTimer, mainThread]() {
        qInfo() << QStringLiteral("【1】谁创建对象，对象就归谁（此刻还没 moveToThread）");
        qInfo() << affinityText(QStringLiteral("worker(probe)："), probe);
        qInfo() << affinityText(QStringLiteral("宿主 QThread 对象："), host);
        qInfo() << affinityText(QStringLiteral("主线程 new 的 QTimer："), badTimer);
        qInfo() << QStringLiteral("      → 三个对象的归属线程都==主线程? %1｜"
                                  "**注意宿主 QThread 对象也归主线程**：它的 run() 在新线程跑，"
                                  "但对象户口在主线程（第 1 讲第三节那个对照）")
                       .arg(probe->thread() == mainThread && host->thread() == mainThread
                                && badTimer->thread() == mainThread
                            ? QStringLiteral("是") : QStringLiteral("否"));
        checkBool("probe 创建后归主线程", probe->thread() == mainThread);
        checkBool("宿主 QThread 对象也归主线程", host->thread() == mainThread);
        checkBool("主线程 new 的 QTimer 归主线程", badTimer->thread() == mainThread);
        Q_UNUSED(col)
    });

    // ── 【2】moveToThread + 起宿主 + 让 worker 自己建定时器 ─────────────────
    QTimer::singleShot(120, [host, probe]() {
        qInfo() << QStringLiteral("【2】moveToThread：改户口，然后起宿主");
        const bool moved = probe->moveToThread(host);   // qobject.h 第 147 行：返回 bool
        qInfo() << QStringLiteral("      moveToThread(host) 返回 %1｜挪后 probe->thread()==host ? %2"
                                  "｜host->loopLevel()=%3（**还没 start，预期 0**）")
                       .arg(moved ? QStringLiteral("true") : QStringLiteral("false"))
                       .arg(probe->thread() == host ? QStringLiteral("是") : QStringLiteral("否"))
                       .arg(host->loopLevel());
        qInfo() << QStringLiteral("      probe 有 parent 吗 = %1（**有 parent 就挪不动**，见第一节）")
                       .arg(probe->parent() ? QStringLiteral("有") : QStringLiteral("没有"));
        checkBool("moveToThread(host) 返回 true", moved);
        checkBool("挪完 probe->thread() == host", probe->thread() == host);
        checkBool("probe 确实没有 parent", probe->parent() == nullptr);
        check("还没 start 时 host->loopLevel() = 0", host->loopLevel(), 0);

        host->start();
        qInfo() << QStringLiteral("      host->start() 已调；此刻 host->isCurrentThread()=%1"
                                  "（**主线程当然不是它**）")
                       .arg(host->isCurrentThread() ? QStringLiteral("true") : QStringLiteral("false"));

        // 开工令用**带 context 的 singleShot**送进 host 线程 → 槽在 host 里跑，
        // 于是槽里 new 的定时器天然归属 host —— 「谁创建归谁」的正确用法。
        QTimer::singleShot(40, probe, [probe]() { probe->startOwnTimer(20, 12); });

        // 反面教材：主线程**裸调** worker 的方法（不走信号、不走队列）
        probe->directCallFromMain(QStringLiteral("【2】主线程裸调 probe->directCallFromMain()："));
        QTimer::singleShot(60, probe, [probe]() {
            probe->adoptChildren(2);
        });
    });

    // ── 【3】挪不动的那一个：证明「主线程 new 的定时器不会被 moveToThread 改归属之外的事救回来」
    QTimer::singleShot(300, [&col, host, probe, badTimer]() {
        qInfo() << QStringLiteral("【3】同一个定时器，两条路的差别就在「谁 new 的」");
        qInfo() << affinityText(QStringLiteral("bad-timer（主线程 new）："), badTimer);
        qInfo() << affinityText(QStringLiteral("对比：worker 槽里 new 的那个："), probe);
        qInfo() << QStringLiteral("      worker 侧目前 ownTicks 只通过信号搬运，主线程**不读**它的成员；"
                                  "本讲主线程只认 Collector 的 %1 次到达")
                       .arg(col.mainSeenTicks);
        Q_UNUSED(host)
    });

    // ── 【4】故意踩一次：把归属改成 host 之后，仍在**主线程**调 start() ───────
    // 「待复核」：Qt6 的实现预期在这里打出一条 qWarning（文案含 another thread），不崩；
    // 本组只把 isActive()/loopLevel()/tick 数原样打出，**不对数值下结论**，
    // 不同小版本的行为差异不写进结论 —— 结论只有一条：**别这么写**。
    QTimer::singleShot(360, [&col, host, badTimer]() {
        qInfo() << QStringLiteral("【4】反面组：moveToThread(host) 后，仍在主线程调 start()");
        const bool m2 = badTimer->moveToThread(host);
        qInfo() << QStringLiteral("      moveToThread(host) 返回 %1｜badTimer->thread()==host ? %2"
                                  "｜badTimer->parent()=%3（**没有 parent**，所以挪得动）")
                       .arg(m2 ? QStringLiteral("true") : QStringLiteral("false"))
                       .arg(badTimer->thread() == host ? QStringLiteral("是") : QStringLiteral("否"))
                       .arg(badTimer->parent() ? QStringLiteral("有") : QStringLiteral("(无)"));
        checkBool("badTimer->moveToThread(host) 返回 true", m2);
        checkBool("挪完 badTimer->thread() == host", badTimer->thread() == host);
        checkBool("badTimer 没有 parent", badTimer->parent() == nullptr);
        QObject::connect(badTimer, &QTimer::timeout, &col, [&col]() { ++col.badTicks; });
        qInfo() << QStringLiteral("      现在从**主线程**调用 badTimer->start(25) —— 预期一条 Qt 警告：");
        badTimer->start(25);
        qInfo() << affinityText(QStringLiteral("start() 之后的 bad-timer："), badTimer);
        qInfo() << QStringLiteral("      isActive=%1｜host->loopLevel()=%2｜主线程 loopLevel=%3"
                                  "｜已 tick=%4 → 这一组**只用来说明「别这么写」**")
                       .arg(badTimer->isActive() ? QStringLiteral("true") : QStringLiteral("false"))
                       .arg(host->loopLevel())
                       .arg(QThread::currentThread()->loopLevel())
                       .arg(col.badTicks);
    });

    // ── 【5】跨线程收尸：deleteLater（正路）────────────────────────────────
    QTimer::singleShot(520, [probe]() {
        qInfo() << QStringLiteral("【5】对象归子线程时，主线程怎么「删」它：deleteLater()");
        const QList<QObject *> kids = probe->children();
        if (kids.isEmpty()) {
            qInfo() << QStringLiteral("      （这一轮子对象还没建好，跳过）");
            return;
        }
        QObject *victim = kids.first();
        qInfo() << affinityText(QStringLiteral("要收尸的对象："), victim);
        qInfo() << QStringLiteral("      主线程调 victim->deleteLater() —— **不是 delete**："
                                  "它只往**对象所属线程**的事件队列投一个 DeferredDelete 事件");
        victim->deleteLater();
        qInfo() << QStringLiteral("      调用之后**立刻**看：children 还剩 %1 个（说明此刻**还没析构**，"
                                  "析构要等 host 的事件循环取到那个事件）").arg(probe->children().size());
        qInfo() << QStringLiteral("      反面：在主线程写 `delete victim;` —— 析构函数就在主线程跑，"
                                  "而它按「我归 host」的假设去摘 parent、发 destroyed、放开资源；"
                                  "**这是本讲唯一一处「代码里绝不能出现」的写法**，所以本文件一次都没写。");
    });

    // ── 【6】收尾：quit → finished → 宿主停了才回收 ────────────────────────
    QTimer::singleShot(760, [host, probe, badTimer, &col]() {
        qInfo() << QStringLiteral("【6】收尾");
        badTimer->stop();
        delete badTimer;              // badTimer 归属=**主线程**（主线程 new 的），此刻主线程删它合法
        qInfo() << QStringLiteral("      bad-timer 已在主线程删掉（它归主线程，所以能这么删）；"
                                  "tick 计数=%1").arg(col.badTicks);
        qInfo() << QStringLiteral("      host->quit() 已由 finishedWork 触发；probe 归属=%1"
                                  "（==host → 它只能由 host 那条线程收尸）").arg(ptrText(probe->thread()));
        const bool waited = host->wait(QDeadlineTimer(500));
        qInfo() << QStringLiteral("      host->wait(QDeadlineTimer(500)) = %1｜host->loopLevel()=%2"
                                  " isRunning=%3").arg(waited ? QStringLiteral("true") : QStringLiteral("false"))
                       .arg(host->loopLevel())
                       .arg(host->isRunning() ? QStringLiteral("true") : QStringLiteral("false"));
        checkBool("host->wait(QDeadlineTimer(500)) 在超时前返回", waited);
        checkBool("wait 之后 host 不再运行", host->isRunning(), false);
        Q_UNUSED(probe)
    });

    // ── 【7】总汇总并退出 ───────────────────────────────────────────────────
    QTimer::singleShot(900, [&col, host, probe, &wall]() {
        qInfo() << QStringLiteral("【7】总汇总（可核对的证据）");
        qInfo() << QStringLiteral("      主线程收到 tick 信号 %1 次，其中「worker 侧"
                                  " currentThread==worker->thread()」判定为真的 %2 次，"
                                  "为假的 %3 次 → **全部为真即事件跑在正确的线程上**")
                       .arg(col.mainSeenTicks).arg(col.mainAllSame).arg(col.mismatch);
        qInfo() << QStringLiteral("      最后一行取证：%1").arg(col.lastTickLine);
        qInfo() << QStringLiteral("      反面组（从别的线程 start）tick=%1（**不作为结论**，见【4】注释）")
                       .arg(col.badTicks);
        qInfo() << QStringLiteral("      probe 在 worker 线程里 new 的子对象数=%1"
                                  "｜host 线程对象=%2｜主线程=%3")
                       .arg(probe->childBorn)
                       .arg(ptrText(static_cast<QThread *>(host) ? reinterpret_cast<void *>(host) : nullptr),
                            ptrText(QCoreApplication::instance()->thread()));
        qInfo() << QStringLiteral("      墙钟耗时：%1 ms").arg(wall.elapsed());
        qInfo() << QStringLiteral("      一句话：**对象的归属线程 = 创建它的那条线程；"
                                  "改归属只有 moveToThread，而且它管不了「现在谁在执行代码」** —— "
                                  "所以「在别的线程调它的方法」永远是错的。");
        check("12 次 tick 全部到达主线程", col.mainSeenTicks, 12);
        check("没有一次 tick 被判成跑错线程", col.mismatch, 0);
        check("worker 线程里造了 2 个子对象", probe->childBorn, 2);

        // 顺序纪律：宿主线程停了之后，probe 只能交给它自己的线程收尸
        QObject::connect(host, &QThread::finished, probe, &QObject::deleteLater);
        if (!host->isRunning())
            qInfo() << QStringLiteral("      （host 已结束；probe 的 deleteLater 会在其线程收尾时被处理）");
        delete host;                  // QThread 对象归**主线程**（主线程 new 的）→ 主线程删它合法
        qInfo().noquote() << QStringLiteral("\n【硬账】%1/%2 项核对通过 → 退出码 %3")
                                 .arg(Ledger::passed).arg(Ledger::total).arg(Ledger::exitCode());
        QCoreApplication::exit(Ledger::exitCode());
    });

    return app.exec();
}

// 本文件有 Q_OBJECT 类（DtorProbe / AffinityProbe / Collector），AUTOMOC 会生成并在这里包含。
#include "02_thread_affinity.moc"

// ——— 老师傅的话 ———
/*
 * 徒弟，第 1 讲教你「怎么把活丢到子线程」，这一讲教你**为什么丢了活之后东西会莫名其妙坏掉**。
 * 你只需要记住一句话：**Qt 的对象有一本户口，户口跟着「谁 new 的」走，不跟着「谁在用」走。**
 *
 * 于是三个推论，一个比一个实用：
 *
 * 推论一：**在槽里 new，才是在对的线程上 new**。本讲【2】那个定时器是 worker 的槽里
 * `new QTimer(this)` 出来的 —— 槽跑在 host 线程，所以定时器归 host 线程，所以 start() 一路走。
 * 你要是图省事在主线程 `new QTimer(workerInHost)`，户口的「创建者线程」这一栏就写上了 main，
 * 而 Qt 的定时器是登记在**所属线程的事件分发器**上的（qobject.h 第 149 行 startTimer 那句），
 * 于是你得到一个「isActive() 返回 true、但永远不响」的定时器。它不报错。这是最贵的一种坏。
 *
 * 推论二：**moveToThread 只改户口，不改「此刻谁在执行」**。
 * 本讲【2】里那句「主线程裸调 probe->directCallFromMain()」没崩 —— 因为那个函数只读只打印。
 * 但那一刻的现场是「对象归 host、代码在 main 的栈上跑」，你在这条路上多写一行
 * （碰 m_timer、碰 socket、碰 widget），就从「读日志」变成「查崩溃」。
 * 所以我的纪律是：**跨线程只能走信号槽**（或带 context 的 singleShot / invokeMethod，第 3 讲）。
 * 编译器不会拦你 —— 它是合法的方法调用，只有你自己拦得住自己。
 *
 * 推论三：**收尸要交给它自己的线程**。deleteLater() 之所以是个**槽**（qobject.h 第 364 行），
 * 就是为了让你能从别的线程「下命令」而不是「动手」。本讲【5】把那条命令投出去之后立刻
 * 数了一遍 children —— 没少。这就是队列：析构发生在 host 的事件循环取到那个事件的那一刻。
 * 而 `delete victim;` 从主线程下去，析构函数就在主线程跑，它摘 parent、发 destroyed、
 * 释放资源的全套动作都跑错了线程。这种 bug 现场不留痕迹，只在客户机器上出现。
 *
 * 还有一条是我拿过手的经验：**有 parent 就别想挪**。对象树的归属必须自洽，Qt 在这儿拦得死。
 * 所以 worker-object 姿势的**第一个零件**就是「new 的时候不要传 parent」，
 * 收尸改由 finished → deleteLater 这条线负责（第 1 讲就是这么写的）。
 * 你把「归属」和「生命周期」分开管，代码一下就清楚了：归属靠 moveToThread，生命周期靠 parent
 * 或 deleteLater —— 一个对象只能有一套，别混。
 *
 * 最后说 GUI。你以后写桌面程序，99% 的「随机闪退」工单都是这一类：worker 里顺手
 * 改了一个 QLabel、主线程正在画它。记住本讲【1】打的那个 isWidgetType=false ——
 * **我们这些对象不是 widget，所以随便打印都不崩；换成 widget，同一个动作就是断言或者崩。**
 * 整个 QtWidgets 的隐含契约是「widget 树归主线程」。你要更新界面，就把数据用信号搬回主线程，
 * 在主线程的槽里碰控件 —— 多写一个信号，少加一次班，这笔账我算过很多遍了。
 */

// ——— 小练习 ———
/*
 *   1. 把【2】里 `QTimer::singleShot(40, probe, [probe](){ probe->startOwnTimer(20, 12); })`
 *      改成不带 context 的两参版本 `QTimer::singleShot(40, [probe](){ ... })`，
 *      再把 `probe->startOwnTimer(20, 12)` 改成**直接调用**（不是信号）。
 *      跑一遍，看【2】打印的「槽里 new 的 QTimer」那行的归属线程从 host 变成 main，
 *      并解释 ownTicks 为什么当场归零（提示：isActive() 还是 true）。
 *   2. 给 AffinityProbe 的构造函数加一个 `QObject *parent` 参数并传入主线程上的某个对象，
 *      然后把 moveToThread 调上 —— 观察 Qt 怎么拦你（断言还是返回值，记下你这版本的实际表现），
 *      再用一句话写出「worker-object 为什么必须无 parent」的完整理由（含收尸方案）。
 *   3. 把【5】改成「不 quit，直接让 host 继续跑 3 秒」，观察 deleteLater 投出去的三个对象
 *      分别在什么时候析构（析构行打印的线程名），并把「主线程 delete」这一种**真的**试一次，
 *      在沙箱里看它的现场（崩溃点、调用栈），然后回到正路。
 *   4. 本讲用 QCoreApplication 所以碰不到 widget。把它换成 QApplication，
 *      建一个 QLabel（**不要** show），然后加一个 worker 槽，在里面调 label->setText()，
 *      再用「把文字通过信号搬回主线程、在主线程 setText」的正确版本对照，
 *      把你观察到的运行时提示/断言原文抄进注释。
 */
// ============================================================================
