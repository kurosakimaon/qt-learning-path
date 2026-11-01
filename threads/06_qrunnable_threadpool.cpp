// ============================================================================
// 第 6 讲 · QRunnable + QThreadPool：把「一次任务」写成一个不带身份证的包裹
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   第 1 讲你已经会把 worker-object `moveToThread` 过去，第 5 讲你用了 QtConcurrent。
//   这一讲补中间那一格：**线程池**。它是 QtConcurrent 的底座（qtconcurrentrun.h 65 行
//   那句 `QtConcurrent::run(QThreadPool::globalInstance(), ...)`），也是你自己
//   「有一批互不相干的活、不想一条一条管线程」时的正解。它的接口只有几个动词，
//   但每一条都埋着一个坑。本讲拆成八节，每节都有可打印的结论：
//     1) **QRunnable 不是 QObject**。它没有元对象、没有线程亲和性、没有父子树，
//        只有一个纯虚 `run()`（qrunnable.h 24 行）。所以它**不能 emit 信号**，
//        也不能被 connect —— 想把结果送回去，必须手里另外攥着一个真 QObject。
//        这一条用三个 `static_assert` 在编译期钉死（第【1】节），不靠我嘴上说。
//     2) `autoDelete` **默认是 true**（qrunnable.h 20 行 `bool m_autoDelete = true;`）。
//        于是「谁 delete 这个 runnable」变成一件必须说清的事：默认情况池替你删，
//        你手写 `setAutoDelete(false)` 就轮到你删。本讲用**析构计数器**把这件事
//        变成能核对的数（第【1】【5】【6】节）。
//        ★ Qt5 那个 `setAutoDeleteOnce()` 在本机头文件里**不存在**（全文只有
//          autoDelete/setAutoDelete，38/39 行），别照着旧教程写。
//     3) `start(QRunnable*, int priority = 0)`（qthreadpool.h 37 行）—— 那个 priority
//        是**裸 int**，只用来给队列排序；它跟 `QThread::Priority` 那个枚举
//        （qthread.h 41 行起，IdlePriority…HighestPriority）**不是一回事**，后者是
//        交给操作系统的线程优先级，走 `setThreadPriority()`（68 行）。
//        第【4】节把 maxThreadCount 压到 1，让队列真的排起来，然后核对出场顺序。
//     4) `tryStart()`（38 行）**不排队**：没有空闲线程就当场返回 false。
//        而它有两个长得很不一样的重载：拿 `QRunnable*` 的那族（38 行）失败时
//        **对象归你**，你得自己删；拿 lambda 的那族（50~55 行那一族模板声明，
//        函数体在 94~102 行）第 100 行 `delete runnable;` 替你删干净。第【5】节各演一次，
//        并用一个「探针对象的析构」证明那句源码真的执行了。
//     5) `reserveThread()`/`releaseThread()`（71/72 行）+ `startOnReservedThread()`（45 行）：
//        预留出来的线程不受 expiryTimeout 回收。这一族的危险性在「没预留就调
//        startOnReservedThread」—— 文档说那会直接把进程干掉。本机头文件只给签名，
//        证明不了这条，所以**本讲绝不在未预留时调用它**，练习也别试（详见运行前须知）。
//        第【7】节另用 `waitForDone(200)` 打出一个「任务还在跑，等不到」的 false，
//        再打一个 `waitForDone(2000)` 的 true —— 一假一真，「限时等待」这句话才算落地。
//     6) `clear()`（81 行）与 `tryTake()`（85 行，注意它带 `[[nodiscard]]`）管的是
//        **还在队列里**的任务；已经在跑的任务谁都撤不下来。这就是第 7 讲
//        （取消与收尾）整讲的题目：取消必须任务自己配合。
//     7) `waitForDone()`（78/79 行）**默认是 Forever**：一个参数都不传，就是永远等。
//        在有事件循环的线程里这么干，如果那些任务里有任何一个要往这条线程投递事件，
//        就是自己等自己。第【8】节把这一环画出来，并说明为什么本讲前面那些
//        waitForDone 是**故意的例外**（那些任务一个都不碰主线程）。
//     8) 结果怎么回主线程：QRunnable 不能 emit，所以「手里攥一个 QObject，
//        让它 emit，连接时挂 context」是唯一的正规路子。第【8】节把四条结果
//        收回主线程，**逐条打印回调所在线程 id**，并现场演示一次
//        「接收者提前死掉 → 那条结果永久失联」。
//
// 对应构建目标名：threads_06_qrunnable_threadpool
//   find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)
//   add_executable(threads_06_qrunnable_threadpool 06_qrunnable_threadpool.cpp)
//   target_link_libraries(threads_06_qrunnable_threadpool PRIVATE Qt6::Widgets Qt6::Concurrent)
//   set(CMAKE_AUTOMOC ON)         # 本文件有 1 个 Q_OBJECT 类（Sink），需要 moc
//   手工核对：moc 06_qrunnable_threadpool.cpp -o 06_qrunnable_threadpool.moc
//             末尾那句 #include "06_qrunnable_threadpool.moc" 就是给 AUTOMOC 的钩子
//
// 运行前须知：
//   · 无人值守：阶段推进由一次 0ms 的 QTimer 触发，前七节在里面同步跑完，
//     第八节交给事件循环 + 一颗 600ms 的核对定时器。
//     正常路径约 3 秒，最坏路径（会合全部走满 300ms 期限）约 5 秒 ——
//     正常路径这一条已实测：本机 Qt 6.12.0 + MinGW 13.1.0、offscreen 下一次 3.24 s
//     （最坏路径是**按每节等待期限加出来的预算**，没人为制造过全部会合超时）。
//   · 总闸 15 秒：约为最坏预算的三倍，到点强制收尾并把退出码置 1（watchdogFired = true）。
//   · ★ **退出码 = 硬账**：任何一项 Hard 没对上、或者一项都没核过（Ledger::total == 0）、
//     或者整节作废过（stalled > 0）、或者总闸跳过一次 → 返回 1。根目录 CMakeLists.txt 的
//     qt_add_demo() 给每个示例都 `add_test()` 并设了 `TIMEOUT 120` —— 也就是说
//     **每个讲义本身就是一个 ctest 用例**。
//     那用 quit() 收尾（退出码永远 0）的写法，屏幕上的红字 CI 一个都看不见。
//   · ★ 本讲**故意**在主线程用了几处 `waitForDone(有限期限)` 和 `QThread::msleep`，
//     为的是把「同步等待」这件事演清楚。真实界面代码里一处都不该有：
//     主线程一睡，界面就停，而且一旦任务要往主线程投事件就是第【8】节那张死锁环。
//     每一处例外旁边都写了「为什么这一处可以睡」。
//   · 不读文件、不联网、不碰注册表、不碰共享内存；任务算的是纯算术，
//     所以任何一台机器上「串行复算的和」都是同一个数，能拿来当硬结论。
//   · 并发峰值/命中次数这类**取决于调度器**的数，一律按第 4 讲那三档口径标注：
//     Hard 才进账，Soft/Obs 只打印。换机器看到的 Soft 没命中，不代表代码错。
//   · `startOnReservedThread()` 在未预留线程时的行为**不做实验**：
//     那是「进程当场没了、日志一行都不留」的失败形状，不值得用一次崩溃去换一句确认。
//   · 可以无头跑：`threads_06_qrunnable_threadpool` 用的是 QCoreApplication，
//     不需要显示设备（CMake 里给 ctest 设的 QT_QPA_PLATFORM=offscreen 对它无影响）。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 threads_06_qrunnable_threadpool）。本机这一轮：硬账：核对 36 项，不符 0 项。
//     这只证明「编得过、跑得起来、上面这行为绿」——换 Qt 版本 / 换平台 / 换 CPU 核数
//     之后，正文里带具体数字的「预期」请以你自己那次运行输出为准
//     （重跑：bash _tools/run_all.sh）。
// ============================================================================

#include <QCoreApplication>
#include <QRunnable>             // QtCore/qrunnable.h
#include <QThreadPool>           // QtCore/qthreadpool.h
#include <QThread>               // QtCore/qthread.h：idealThreadCount(35) / Priority(41~) / msleep
#include <QTimer>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>          // 转发头；真实体 qmutex.h 241~242 行（是个模板，用 CTAD）
#include <QWaitCondition>        // qwaitcondition.h 24~26 行两个 wait 重载
#include <QAtomicInt>            // qbasicatomic.h：operator++ 在 144~147 行
#include <QDebug>
#include <QVector>
#include <QSet>
#include <QString>
#include <QStringList>

#include <type_traits>           // static_assert 那三条要用
#include <functional>

// ============================================================================
// 一、对着本机头文件核过的签名（D:\Qt\6.12.0\mingw_64\include，Qt 6.12.0）
// ============================================================================
// QtCore/qrunnable.h
//   第 18 行 class Q_CORE_EXPORT QRunnable            ← **不是** QObject 的子类，没有 Q_OBJECT
//   第 20 行 bool m_autoDelete = true;                ← 默认值就在这，别猜
//   第 22 行 Q_DISABLE_COPY(QRunnable)
//              宏的展开在 qtclasshelpermacros.h 24~26 行，是 `= delete` 两个：
//              拷贝构造 + 赋值。所以「拷一个 runnable」是编译错误，不是运行时惊喜。
//   第 24 行 virtual void run() = 0;                  ← 全文唯一的纯虚，也是全部的入口
//   第 26 行 constexpr QRunnable() noexcept = default;
//   第 27 行 virtual ~QRunnable();                    ← **必须是虚析构**：线程池只拿 QRunnable*
//   第 29 行 static QRunnable *create(std::function<void()>);
//              ← 包在 `#if QT_CORE_REMOVED_SINCE(6, 6)`（28 行）里，是**待移除**的老口子；
//                新代码走第 34~35 行那个模板版 create。
//   第 32 行 template <typename Callable> using if_callable =
//              std::enable_if_t<std::is_invocable_r_v<void, Callable>, bool>;
//              ← 「能当任务的东西」的判据：可调用、返回值能当 void。返回 int 的函数照样合格。
//   第 36 行 static QRunnable *create(std::nullptr_t) = delete;
//              ← 传 nullptr 是**编译错误**。这一条 delete 就是给「拿返回值判空」的人准备的。
//   第 38/39 行 bool autoDelete() const / void setAutoDelete(bool)
//              ← 全文到此为止。Qt5 的 setAutoDeleteOnce() 在 6.12 里**没有**。
//   第 46~98 行 class QRunnable::QGenericRunnable（create() 真正 new 出来的类型）：
//              第 66~86 行的 Helper<Callable> 把 callable 存进 CompactStorage，
//              第 70~78 行那个 impl(Op,…) 只有两个动作 —— Op::Run 调它，Op::Destroy 删自己。
//              ★ 读这段源码的价值：第 48 行注释自己写着 "Type erasure, to only instantiate
//                a non-virtual class per Callable" —— 每个 lambda 类型只实例化一份，
//                这就是为什么 `pool->start([…]{})` 不用你先写一个类。
//
// QtCore/qthreadpool.h
//   第 20 行 class QThreadPool : public QObject        ← 池**是** QObject（能挂 parent、能收信号）
//   第 24~28 行 五个 Q_PROPERTY（expiryTimeout / maxThreadCount / activeThreadCount /
//              stackSize / threadPriority）
//   第 32 行 QThreadPool(QObject *parent = nullptr);
//   第 33 行 ~QThreadPool();
//              ← 文档说析构会**等到所有活动线程干完**再删队列。这一条我从头文件证明不了，
//                所以本讲只在【合账】里把「池的析构用了多少毫秒」打成 Obs，不当硬结论。
//   第 35 行 static QThreadPool *globalInstance();
//   第 37 行 void start(QRunnable *runnable, int priority = 0);   ← 排队，priority 是裸 int
//   第 38 行 bool tryStart(QRunnable *runnable);                   ← **不排队**，失败对象归你
//   第 40~43 行 两个 std::function 版重载，包在 `#if QT_CORE_REMOVED_SINCE(6, 6)` 里
//   第 45 行 void startOnReservedThread(QRunnable *runnable);
//   第 46~48 行 同上的 std::function 版（也是待移除）
//   第 50~55 行 三个模板重载：start(Callable&&, int) / tryStart(Callable&&) /
//              startOnReservedThread(Callable&&)，判据都是 QRunnable::if_callable
//   第 88~92 行 start(Callable&&) 的函数体：转手叫 `start(QRunnable::create(...), priority)`
//   第 94~102 行 tryStart(Callable&&) 的函数体 —— ★ **第 100 行 `delete runnable;`**
//              失败时 Qt 自己把刚 new 出来的那个删掉。这一行是本讲【5】的靶子。
//   第 57/58 行 expiryTimeout() / setExpiryTimeout(int)
//              ← 「空闲工人活多久被回收」。默认值本机头文件里看不到赋值（只有 Q_PROPERTY），
//                文档那句 30000ms 是文档的；【7】把它打成实测，你在自己机器上看屏幕就行。
//   第 60/61 行 maxThreadCount() / setMaxThreadCount(int)
//   第 63 行 int activeThreadCount() const               ← 「现在有几条工人在场」
//   第 65/66 行 setStackSize(uint) / stackSize()
//   第 68/69 行 setThreadPriority(QThread::Priority) / threadPriority()  ← 交给 OS 的那一档
//   第 71/72 行 reserveThread() / releaseThread()
//   第 74/75 行 setServiceLevel(QThread::QualityOfService) / serviceLevel()
//   第 77~79 行 QT_CORE_INLINE_SINCE(6, 8) + bool waitForDone(int msecs);
//              bool waitForDone(QDeadlineTimer deadline = QDeadlineTimer::Forever);
//              ← **默认值是 Forever**，一个都不传就是死等。第 111~114 行那个 int 版
//                只是转手叫 QDeadlineTimer(msecs)，两句话是同一件事。
//                ★ 77 行那个宏不是装饰：int 版是 6.8 才内联进头文件的。你要兼容旧 Qt
//                  就写 `waitForDone(QDeadlineTimer(1000))`，它从 6.0 起就在。
//   第 81 行 void clear();                               ← 只管队列，管不了已开跑的
//   第 83 行 bool contains(const QThread *thread) const;
//   第 85 行 [[nodiscard]] bool tryTake(QRunnable *runnable);
//              ← 带 nodiscard：不接返回值编译就警告。本仓库标准是 0 warning，所以
//                「试一下能不能取回来」这种写法在这里活不过编译（第【6】节正面写）
// QtCore/qthread.h
//   第 35 行 static int idealThreadCount() noexcept      ← 「这台机器大概能开几条」的线索。
//              文档把池的默认上限跟它绑在一起，但本机头文件里找不到那行赋值，
//              所以【2】把两个数并排打印，不当硬结论用。
//   第 41~53 行 enum Priority { … }，**一个显式值都没写**，所以从 0 往下顺排：
//              42 IdlePriority=0、44 LowestPriority=1、45 LowPriority=2、46 NormalPriority=3、
//              47 HighPriority=4、48 HighestPriority=5、50 TimeCriticalPriority=6、
//              52 InheritPriority=7（最后一个是「继承」语义，不是更高的优先级）
//              ★ 记这两格反直觉的：带 Idle 的那档是**最闲**（0），不是「暂停/空闲着等活」；
//                末尾的 InheritPriority 是「跟随父线程」，不是「比 Highest 还高」。
//   第 62 行 void setPriority(Priority);
//
// 与本讲相关的其它模块（跨讲引用，别在这里重复证明）：
//   · threads 第 3 讲：queued 连接怎么把一次 emit 变成主线程队列里的一条事件；
//     本讲【8】只**用**它，并且把每条回调的线程 id 打出来当收据。
//   · threads 第 4 讲：Hard / Soft / Obs 三档口径，和「计数只在真的判定完之后才走」。
//   · metaobject 第 4 讲：connect 的 context 参数 —— 本讲【8】用它演示一次「结果永久失联」。
//   · network 第 2 讲：同一个手法在异步网络上的版本（那边是 reply::finished，这边是任务返回）。
// ============================================================================

// ============================================================================
// 二、口径与账本：三档结论，只有第一档进退出码
// ----------------------------------------------------------------------------
// 第 4 讲立的规矩，这里一行都不改：
//   Hard —— API 语义保证。不对就是代码错，进账、影响退出码。
//   Soft —— 「应当」发生但取决于调度器（并发峰值是不是正好 4、rendezvous 命中几次）。
//           没命中不算失败，但**必须打印**，因为你换台机器第一次跑就会看见它没命中。
//   Obs  —— 只报实测，不做承诺（毫秒数、activeThreadCount 此刻是多少）。
// 把三档混成一档，是并发材料最常见的作假方式：拿概率性现象冒充确定性结论。
// ============================================================================
enum Kind { Hard, Soft, Obs };

struct Ledger {
    int total = 0;        // 做过的硬核对次数
    int failed = 0;       // 其中没对上的次数
    int stalled = 0;      // 前提没建立起来、于是整节作废的节数
    bool watchdogFired = false;

    int exitCode() const
    {
        // 三项里任何一项命中都要留痕迹：不符、整节作废、账是空的。
        // 「checksTotal == 0 也算失败」这条是本仓库的规矩 —— 什么都没测却说测过了，
        // 比测错更坏：CI 看见的是一个绿色的 0。
        const int broken = failed + stalled;
        return (watchdogFired || broken > 0 || total == 0) ? 1 : 0;
    }
};
static Ledger gLedger;

static void verdict(Kind kind, const QString &label, bool ok, const QString &detail)
{
    QString head;
    switch (kind) {
    case Hard:
        ++gLedger.total;
        if (!ok)
            ++gLedger.failed;
        head = ok ? QStringLiteral("  [OK ] 核对  ") : QStringLiteral("  [!! ] 不符  ");
        break;
    case Soft: head = ok ? QStringLiteral("  [预期中] ") : QStringLiteral("  [这次没命中] "); break;
    case Obs:  head = QStringLiteral("  [实测]  "); break;
    }
    qInfo().noquote() << head + QStringLiteral("%1 = %2").arg(label, detail);
}

static void section(const QString &text)
{
    qInfo().noquote() << QStringLiteral("\n========== %1 ==========").arg(text);
}

// ★ 为什么还要再包一层 checkNum / checkBool，不直接叫 verdict：
//   verdict 的第 3 个参数是 bool「这一项过了」。本讲一半以上的结论形如
//   「某个计数 == 期望值」，而**期望值经常是 0**。把 `got` 直接当 ok 传进去，
//   `got == 0` 会被读成「没过」，`got == 5` 会被读成「过了」—— 一个假红 + 一个假绿，
//   同一行代码里两个方向都错，而且编译期一声不响。这是我写这一讲时**真的踩到的**
//   （第一版有 9 处这么写），所以把它固化成两个函数：判据在函数里算，调用处只能给
//   「实测值 + 期望值」两个数，想少写一个都过不去。
//   network 第 2/3 讲用的就是这个形状（checkNum/checkStr/checkKind），本仓库不另立口径。
static void checkNum(const QString &label, qint64 got, qint64 want)
{
    verdict(Hard, label, got == want, QStringLiteral("%1（期望 %2）").arg(got).arg(want));
}

static void checkBool(const QString &label, bool got, bool want)
{
    verdict(Hard, label, got == want,
            QStringLiteral("实测 %1（期望 %2）")
                .arg(got ? QStringLiteral("true") : QStringLiteral("false"),
                     want ? QStringLiteral("true") : QStringLiteral("false")));
}

// ============================================================================
// 三、探针：把「谁构造 / 谁运行 / 谁析构」变成三个能对账的数
// ----------------------------------------------------------------------------
// ★ 为什么非要计数：QRunnable 的默认 autoDelete=true 意味着「对象什么时候没的」
//   这件事在屏幕上完全看不见。看不见就没法核对，没法核对就只能信 —— 本讲不信。
//   做法：每个任务自己带一个「存在证」编号，构造/运行/析构三处各记一笔。
//   计数用 QAtomicInt：三条 worker 同时写一个 int，裸 `++` 是第 4 讲【1】那道题，
//   这里直接上原子（qbasicatomic.h 144~147 行：前置 ++ 返回新值，后置 ++ 返回旧值）。
// ============================================================================
static QAtomicInt gCtorCount = 0;      // 任务对象被构造了几次
static QAtomicInt gDtorCount = 0;      // 被析构了几次（**谁删的都算**，这才是关键）
static QAtomicInt gRunCount = 0;       // run() 真的进去过几次（四种任务全算）
static QAtomicInt gQsum = 0;           // 各任务算出来的部分和（取模压进 int）累加

// ★ 为什么还要一个「只数 SumRunnable」的计数器，不嫌重复：
//   【5】【6】【7】的开场都先投一个 BlockerRunnable 把工人占住。而工人线程是
//   **先创建、后进 run()** 的 —— 我轮询到 activeThreadCount()>0 的那一刻，
//   blocker 的那笔 ++gRunCount 可能还没落。于是「本节进场次数」的期望值会莫名其妙多 1，
//   而且是**低概率多 1**：CI 上一次绿、下一次红，查三小时。
//   解法不是「多睡一会儿」，是把判定用的计数换成与 blocker 无关的那一类：
//   SumRunnable 的进场只可能来自我这一节投的任务，账才是闭合的。
//   （这是本讲最容易写错的一处，值得记住：**计数器的口径要和结论的主语一致**。）
static QAtomicInt gSumRuns = 0;        // 只有 SumRunnable::run() 会加它
static QAtomicInt gProbeDtor = 0;      // 【5B】塞进 lambda 捕获的小探针析构了几次
static QAtomicInt gProbeRun = 0;       // 同一个探针的 lambda 体执行了几次

// 串行复算：qint64，绝不并发，所以它是「标准答案」。
static qint64 checksumOf(int lo, int hi)
{
    qint64 s = 0;
    for (int i = lo; i < hi; ++i)
        s += (qint64(i) * 31) % 1000;
    return s;
}

// ============================================================================
// 四、Sink：任务手里攥着的那个「真 QObject」
// ----------------------------------------------------------------------------
// QRunnable 不能 emit（它连 Q_OBJECT 都没有），所以回主线程这条路一定要一个 QObject 当发信人。
// 本讲用**外部对象 + 公开方法包一层 emit**：
//   · 包一层的原因：signal 在 Qt 里其实是 public（Q_SIGNAL 就是 public），
//     外面写 `emit sink->partialReady(...)` 编译得过 —— 但那是坏规矩：
//     发信号是对象的私有事件，别的类不该替它发。方法名 publishPartial() 就是这道墙。
//   · 生命周期：sink 挂在 app 上，本讲保证「所有任务收尸完之后再让它死」。
//     真项目做不到这个保证时（界面随时可能被关），就别让任务持有裸指针 ——
//     用第 3 讲那套 context/QueuedConnection 手法，或者干脆把结果放进线程安全队列，
//     由主线程自己取。**「裸指针 + 我保证它活着」这句话只在有收尸顺序的时候成立。**
// ============================================================================
class Sink : public QObject
{
    Q_OBJECT

public:
    explicit Sink(QObject *parent = nullptr) : QObject(parent) {}

    void publishPartial(int id, int value)
    {
        emit partialReady(id, value);
    }

Q_SIGNALS:
    void partialReady(int id, int value);
};

// ============================================================================
// 五、三种任务形状（同一段活，三种写法，各自的本事不一样）
// ============================================================================
// A. 经典继承：要带状态、要回调发信人，就写这个
class SumRunnable : public QRunnable
{
public:
    SumRunnable(int lo, int hi, Sink *sink, const char *stage)
        : m_lo(lo), m_hi(hi), m_sink(sink), m_stage(QString::fromLatin1(stage))
        // ↑ 这里的 stage 是纯 ASCII 的节号（"p1"/"p8"），走 fromLatin1 名正言顺；
        //   中文一律 QStringLiteral，别学旧教程用 fromLatin1 —— storage 第 3 讲算过这笔账。
    {
        ++gCtorCount;
    }

    ~SumRunnable() override { ++gDtorCount; }

    void run() override
    {
        ++gRunCount;
        ++gSumRuns;
        const qint64 v = checksumOf(m_lo, m_hi);
        gQsum.fetchAndAddOrdered(int(v % 100000));
        if (m_sink)
            m_sink->publishPartial(m_lo, int(v % 100000));   // 跨线程 emit，第【8】节核对落点
    }

private:
    int m_lo = 0;
    int m_hi = 0;
    Sink *m_sink = nullptr;
    QString m_stage;
};

// B. 占位的「忙碌任务」：不干活，只占着工人，让队列真的排起来
class BlockerRunnable : public QRunnable
{
public:
    explicit BlockerRunnable(int ms) : m_ms(ms) { ++gCtorCount; }
    ~BlockerRunnable() override { ++gDtorCount; }

    void run() override
    {
        ++gRunCount;
        QThread::msleep(m_ms);            // 占住这条工人线程
    }

private:
    int m_ms = 50;
};

// C. 记录出场顺序的任务：第【4】节（priority）用
class OrderRunnable : public QRunnable
{
public:
    OrderRunnable(int tag, int slot) : m_tag(tag), m_slot(slot) { ++gCtorCount; }
    ~OrderRunnable() override { ++gDtorCount; }

    void run() override
    {
        ++gRunCount;
        const int pos = gOrder.fetchAndAddOrdered(1);   // 我是第几个进场的（返回旧值）
        if (pos < kOrderCap)
            gOrderLog[pos] = m_tag;
        Q_UNUSED(m_slot)
    }

    static constexpr int kOrderCap = 8;
    static QAtomicInt gOrder;             // ★ 必须是原子：三条工人同时 ++ 一个裸 int，
                                          //   编译器不拦、优化照做，第 4 讲【1】那道题又回来了。
                                          //   定义在类外（C++17 也能用 inline static，这里选老写法便于查）。
    static int gOrderLog[kOrderCap];      // ← 这个**不是**原子：下标由 gOrder 发号，一格只写一次

private:
    int m_tag = 0;
    int m_slot = 0;
};
QAtomicInt OrderRunnable::gOrder;
int OrderRunnable::gOrderLog[OrderRunnable::kOrderCap] = { 0 };

// D. 并发在场探针：rendezvous（「凑齐 N 条再一起走」）
//    ★ 这是本讲**唯一**能证明「池真的同时开了 N 条线程」的手段。
//      只看 activeThreadCount() 不够：那个数是池自己记的账，而你怀疑的正是它。
//   先声明再用：任务持有会合点的指针，而会合点的定义在下面。
//   （写成 `class Rendezvous *rv` 那种「参数表里顺手声明」也能编过，但读的人要找半天 ——
//     本仓库一律用显式前向声明。）
class Rendezvous;

class PartyRunnable : public QRunnable
{
public:
    PartyRunnable(int lo, int hi, Rendezvous *rv)
        : m_lo(lo), m_hi(hi), m_rv(rv) { ++gCtorCount; }
    ~PartyRunnable() override { ++gDtorCount; }

    void run() override;

private:
    int m_lo = 0;
    int m_hi = 0;
    Rendezvous *m_rv = nullptr;
};

// 凑齐 N 条的会合点。全部状态都在锁的保护下，所以普通 int 就够（第 4 讲【2】的结论）。
class Rendezvous
{
public:
    explicit Rendezvous(int want) : m_want(want) {}

    // 返回「这一节里我是不是把大家凑齐了」。等待有期限：凑不齐也得放人走，
    // 否则【3】那种「故意让峰值小于 want」的节会把工人线程永久钉在 wait 上。
    bool arrive()
    {
        bool mine = false;
        {
            QMutexLocker lk(&m_mutex);
            ++m_arrived;
            if (m_arrived >= m_want) {
                m_allHereHit += 1;              // 凑齐过一次（这是 Soft 的证据）
                mine = true;
                m_cond.wakeAll();
            } else {
                // 醒来只说明「要么齐了，要么 300ms 期限到了」。两种情况都不需要再判一次：
                // 调用方只看 mine，而 mine 的含义就是「我是把大家凑齐的那一个吗」。
                // 第一版在这里写过 `if (mine && m_arrived >= m_want) ;` —— 一个空语句，
                // 读起来像有分支、其实什么都没做，编译器一声不响。删了。
                mine = m_cond.wait(&m_mutex, 300UL);   // 最多等 300ms：qwaitcondition.h 26 行
            }
            m_peakSeen = qMax(m_peakSeen, m_arrived);
            --m_arrived;                            // 我离开了这个「在场」区间
        }
        return mine;
    }

    int allHereHits() const { return m_allHereHit; }
    int peakSeen() const { return m_peakSeen; }
    int want() const { return m_want; }      // 【2】【3】拿它做一次「前提自检」：
                                             //   会合点的 want 和池的 maxThreadCount 必须是同一个数，
                                             //   否则这一节的结论讲的不是同一件事

private:
    QMutex m_mutex;
    QWaitCondition m_cond;
    int m_want = 0;
    int m_arrived = 0;
    int m_allHereHit = 0;
    int m_peakSeen = 0;
};

void PartyRunnable::run()
{
    ++gRunCount;
    const bool wasLast = m_rv->arrive();
    Q_UNUSED(wasLast)
    const qint64 v = checksumOf(m_lo, m_hi);
    gQsum.fetchAndAddOrdered(int(v % 100000));
    QThread::msleep(20);                     // 让「在场」区间长一点，好让会合真的发生
}

// ============================================================================
// 六、编译期就把「QRunnable 不是 QObject」钉死
// ----------------------------------------------------------------------------
// ★ 为什么用 static_assert 而不是打印一句：
//   打印只能证明「这次跑起来是这样」，static_assert 是**只要编译过了就永远成立**。
//   本讲三条：
//     ① QRunnable 不是 QObject 的子类 → 所以它没有元对象、不能 emit、没有亲和性；
//     ② QRunnable 不可拷贝 → 想把 runnable 值传进容器再分发，编译期就拦住你
//        （Q_DISABLE_COPY 的展开是 = delete，qtclasshelpermacros.h 24~26 行）；
//     ③ QThreadPool 是 QObject 的子类 → 所以它可以挂 parent、可以被 connect，
//        而它管理的 QRunnable 恰恰相反。这一组对照就是这一讲的骨架。
//   第②条尤其实用：你迟早会写 `QVector<QRunnable>`（少了 *），那是编译错误；
//   而 `QVector<QRunnable*>` 能过编译，然后就掉进【2】要讲的「谁删」问题。
// ============================================================================
static_assert(!std::is_base_of<QObject, QRunnable>::value,
              "QRunnable 不是 QObject：没有元对象，不能 emit，也没有线程亲和性");
static_assert(!std::is_copy_constructible<QRunnable>::value,
              "QRunnable 不可拷贝（Q_DISABLE_COPY → = delete），只能传指针");
static_assert(std::is_base_of<QObject, QThreadPool>::value,
              "QThreadPool 是 QObject：可以挂 parent、可以 connect");
static_assert(std::is_abstract<QRunnable>::value,
              "QRunnable 是抽象类：唯一的纯虚就是 run()（qrunnable.h 24 行）");

// ============================================================================
// 七、辅助：等线程池里「至少有一条工人已经在场」
// ----------------------------------------------------------------------------
// 【5】【6】的前提是「唯一那条工人正忙，所以后面排得进来的都只能在队列里」。
// 这个前提不能靠「我猜它已经起来了」，得**看见**：activeThreadCount() 变非零。
// 这里在主线程 msleep 轮询 —— 又是一处「故意例外」，理由写在这一节的注释里。
// 期限 400ms；到点还没建立，就返回 false，调用方把这一节记成 stalled（作废），
// 而不是硬拿一个不成立的前提去下一个结论。
// ============================================================================
static bool waitWorkerAlive(QThreadPool &pool, int maxMs = 400)
{
    QElapsedTimer t;
    t.start();
    while (pool.activeThreadCount() == 0) {
        if (t.elapsed() > maxMs)
            return false;
        QThread::msleep(1);
    }
    return true;
}

static QString intListToString(const int *arr, int n)
{
    QStringList parts;
    for (int i = 0; i < n; ++i)
        parts << QString::number(arr[i]);
    return parts.join(QLatin1Char(','));
}

// ============================================================================
// 八、七个同步阶段（第【8】节才交给事件循环）
// ============================================================================
static void phase1_shapeAndAutoDelete()
{
    section(QStringLiteral("【1】QRunnable 的形状 + autoDelete 默认值：到底谁删的"));

    QThreadPool pool;
    pool.setMaxThreadCount(2);

    // ① 默认值直接从对象身上读，不引用文档：这就是「本机头文件 20 行说 true」那句的实测收据。
    auto *probe = new SumRunnable(0, 10, nullptr, "p1");
    checkBool(QStringLiteral("新任务 autoDelete() 默认值（头文件 20 行说 true）"), probe->autoDelete(), true);

    // ② 没交给池，所以「归我删」。删一次 → 析构计数恰好动一次；多删少删都当场露出来。
    const int dtorBeforeManual = gDtorCount.loadRelaxed();
    delete probe;
    checkNum(QStringLiteral("没交给池时：我 delete 一次，析构计数动几次"),
             gDtorCount.loadRelaxed() - dtorBeforeManual, 1);

    // ③ 交给池，且**不**改 autoDelete → 期望池在 run() 之后自己删。
    const int dtorBefore = gDtorCount.loadRelaxed();
    const int runBefore = gSumRuns.loadRelaxed();
    pool.start(new SumRunnable(0, 20, nullptr, "p1"));
    const bool done = pool.waitForDone(1000);
    checkBool(QStringLiteral("waitForDone(1000) 该返回 true（任务只算 20 个数）"), done, true);
    if (done) {
        checkNum(QStringLiteral("autoDelete=true：run() 之后没人手工 delete，析构新增几次"),
                 gDtorCount.loadRelaxed() - dtorBefore, 1);
        checkNum(QStringLiteral("这一节的 SumRunnable 进场次数"),
                 gSumRuns.loadRelaxed() - runBefore, 1);
    } else {
        ++gLedger.stalled;                    // 前提（收干净）没成立 → 下面的差值全都不作数
    }
    qInfo().noquote() << QStringLiteral("      说明：上面几笔「新增」一律用差值而不是绝对值 ——\n"
                                        "      全局计数会被后面各节继续加，只有差值是这一节自己的账。\n"
                                        "      ★ 而差值的**基线**必须打在 new 之前、且这一节内不许再借道别人：\n"
                                        "        第一版我把 ② 的基线取成了「构造计数」，拿构造数去核析构次数，\n"
                                        "        数字看着挺像、其实对不上 —— 那是个假结论，编译器不管。");

    // ④ setAutoDelete(false) → 池不再负责，归你。这里演示完自己删掉，不泄漏。
    auto *keep = new SumRunnable(0, 5, nullptr, "p1");
    keep->setAutoDelete(false);
    const int dtorBefore2 = gDtorCount.loadRelaxed();
    pool.start(keep);                          // ★ 同一个 keep 的指针：池跑完它还在
    const bool done2 = pool.waitForDone(1000);
    checkBool(QStringLiteral("【1】setAutoDelete(false) 这一投也收干净"), done2, true);
    if (done2) {
        checkNum(QStringLiteral("setAutoDelete(false) 后，池跑完**不**删（析构新增）"),
                 gDtorCount.loadRelaxed() - dtorBefore2, 0);
        delete keep;                           // 归我；这一行的位置就是「所有权」三个字的含义
        checkNum(QStringLiteral("setAutoDelete(false) 之后我 delete，析构才动（累计新增）"),
                 gDtorCount.loadRelaxed() - dtorBefore2, 1);
    } else {
        ++gLedger.stalled;
    }
    qInfo().noquote() << QStringLiteral("      ★ 这一对（跑完不删 / 一删才动）就是「谁删」的全部答案：\n"
                                        "        autoDelete 决定所有权归属，而**默认值 true 很容易被忽略**，\n"
                                        "        于是最常见的两个事故是：① 又写 setAutoDelete(false) 又忘了删（泄漏），\n"
                                        "        ② 拿着 start() 之后的指针再 delete 一次（二次释放，随机崩）。\n"
                                        "      这两条都不是「读读代码」能防住的，所以要计数。");
}

static void phase2_concurrency(Rendezvous &rv4)
{
    section(QStringLiteral("【2】并发度：池真能同时开几条？先看默认，再压到 4"));

    QThreadPool pool;
    const int want = 4;                 // 会合要点亮的在场数 —— **必须**和 main 里 rv4(want) 同值
    constexpr int kTasks = 8;

    // 前提自检：会合点要求的条数和我设的上限是不是一个数？
    // ★ 这条看着多余，其实它是本节的**地基**：want 与 maxThreadCount 一旦不同值，
    //   「命中次数 > 0」这个 Soft 结论和「peak <= max」那个 Hard 结论讲的是两回事。
    //   参数对不上而结论照抄，是技术材料里最常见的传染性错误 —— 所以先核参数，再核结论。
    checkNum(QStringLiteral("【2】会合点要求的在场数（必须与 maxThreadCount 同值）"), rv4.want(), want);

    // 先把**刚建出来的私有池**默认值打出来，再压到 want：
    // 本机头文件没写默认值等于几（qthreadpool.h 里只有 setter/getter），所以这一笔只是实测，
    // 不当结论用 —— 但把默认值摆在屏幕上，比让读者记住一句「大概是核数」有用。
    qInfo().noquote() << QStringLiteral("      私有池默认 maxThreadCount=%1、expiryTimeout=%2ms；"
                                       "本机 QThread::idealThreadCount()=%3；本节把上限压到 %4")
                             .arg(pool.maxThreadCount()).arg(pool.expiryTimeout())
                             .arg(QThread::idealThreadCount()).arg(want);

    // 8 个任务，每个都要求「凑齐 4 条在场才放行」。池给到 4 条以上，会合就能发生。
    const int runBefore = gRunCount.loadRelaxed();
    const qint64 sumBefore = qint64(gQsum.loadRelaxed());
    pool.setMaxThreadCount(want);
    for (int i = 0; i < kTasks; ++i)
        pool.start(new PartyRunnable(i * 10, (i + 1) * 10, &rv4));
    const bool done = pool.waitForDone(3000);
    checkBool(QStringLiteral("【2】waitForDone(3000) 把 8 个任务收干净"), done, true);
    if (!done) {
        ++gLedger.stalled;
        return;
    }

    checkNum(QStringLiteral("【2】run() 进场次数"),
             gRunCount.loadRelaxed() - runBefore, kTasks);
    // ★ 硬核对：并发算出来的总和必须等于串行复算。这是「并行没算错」的最低要求，
    //   也是本讲唯一一处「数字对不上就是代码错」的算术结论。
    const qint64 wantSum = checksumOf(0, kTasks * 10) % 100000;
    const qint64 gotSum = (qint64(gQsum.loadRelaxed()) - sumBefore) % 100000;
    checkNum(QStringLiteral("【2】并发部分和 == 串行复算（%1 段拼接，串行值）").arg(kTasks), gotSum, wantSum);

    verdict(Soft, QStringLiteral("【2】会合命中次数（4 条线程同时在场上过）"),
            rv4.allHereHits() > 0, QStringLiteral("命中 %1 次（只要 >0 就算「并发是真的」）")
                                       .arg(rv4.allHereHits()));
    verdict(Obs, QStringLiteral("【2】旁观到的最大在场数"), true,
            QStringLiteral("%1（本池 maxThreadCount=%2）").arg(rv4.peakSeen()).arg(pool.maxThreadCount()));
    // 这条是 Hard：**在场数不可能超过 maxThreadCount**。超了就是我的探针写错了。
    verdict(Hard, QStringLiteral("【2】在场数上限不破（peak <= maxThreadCount）"),
            rv4.peakSeen() <= pool.maxThreadCount(),
            QStringLiteral("peak=%1 max=%2").arg(rv4.peakSeen()).arg(pool.maxThreadCount()));

    qInfo().noquote() << QStringLiteral("      对照：QThread::idealThreadCount() = %1（qthread.h 35 行），"
                                        "上面那行「私有池默认 maxThreadCount」就是拿它跟这个数比的。\n"
                                        "      ★ 我不在这里断言「默认一定等于核数」：本机头文件里只有\n"
                                        "        setMaxThreadCount/maxThreadCount 这一对（60/61 行），没写默认值。\n"
                                        "        文档那句话是文档的，屏幕这个数是这台机器的 —— 两条分开记。")
                             .arg(QThread::idealThreadCount());
    qInfo().noquote() << QStringLiteral("      注意 activeThreadCount()=%1 只是**此刻**的池内账，"
                                        "它不是「同时在场几条」的证据 —— 证据是上面那个会合。")
                             .arg(pool.activeThreadCount());
}

static void phase3_maxIsARealCeiling(Rendezvous &rv6)
{
    section(QStringLiteral("【3】把上限压到 2：要求「凑齐 6 条」就永远凑不齐 —— 这正是证据"));

    QThreadPool pool;
    pool.setMaxThreadCount(2);
    const int runBefore = gRunCount.loadRelaxed();

    // 6 个任务，每个都要等「6 条同时在场」。池最多 2 条 → 数学上不可能齐 →
    // 每个任务最多等 300ms 就自己放行。所以这一节的硬结论是**负的**：命中次数必须是 0。
    for (int i = 0; i < 6; ++i)
        pool.start(new PartyRunnable(100 + i * 5, 100 + (i + 1) * 5, &rv6));
    const bool done = pool.waitForDone(6000);
    checkBool(QStringLiteral("【3】waitForDone(6000) 收到 true（每个任务最多滞留 300ms，绝不会挂）"), done, true);
    checkNum(QStringLiteral("【3】6 个任务全部进场"), gRunCount.loadRelaxed() - runBefore, 6);
    // ★ 这一条是整节的结论，而且它是**负的**：凑齐 6 条这件事一次都没发生。
    checkNum(QStringLiteral("【3】在场数被 maxThreadCount 钉死（「凑齐 6」命中次数）"),
             rv6.allHereHits(), 0);
    verdict(Obs, QStringLiteral("【3】实际旁观到的在场峰值"), true,
            QStringLiteral("%1（max=%2）").arg(rv6.peakSeen()).arg(pool.maxThreadCount()));
    qInfo().noquote() << QStringLiteral("      ★ 这一节教的是「怎么证明一个上限是真的」：\n"
                                        "        不去数「跑了几次」，而是造一个只有越过上限才会成功的事件，\n"
                                        "        然后核对它**一次都没成功**。它比「峰值等于 2」结实得多 ——\n"
                                        "        等于几取决于调度，一次都没越过去才是承诺。\n"
                                        "      反过来，【2】那条「命中过 4 次」只敢标 Soft：\n"
                                        "        机器很忙的时候会合也可能错过，那不是代码错。");
}

static void phase4_priorityIsAnInt()
{
    section(QStringLiteral("【4】priority 是裸 int：它只给队列排序，不是 QThread::Priority"));

    QThreadPool pool;
    pool.setMaxThreadCount(1);                 // ★ 只留一条工人，队列才真的排起来

    // 先用一个 blocker 把唯一的工人占住（睡 250ms），期间投三个不同优先级的任务。
    pool.start(new BlockerRunnable(250));
    if (!waitWorkerAlive(pool)) {
        ++gLedger.stalled;
        qWarning().noquote() << QStringLiteral("      【4】前提没建立（工人一直没到场）→ 这一节作废，记 stalled");
        pool.waitForDone(2000);
        return;
    }
    OrderRunnable::gOrder.storeRelaxed(0);       // ★ 用 storeRelaxed，不写 `= 0`：
                                                 //   这一句在主线程，此刻没有工人（还没投任务），
                                                 //   但把「我怎么改这个原子」写明白，比省两个字符值。
    for (int i = 0; i < OrderRunnable::kOrderCap; ++i)
        OrderRunnable::gOrderLog[i] = 0;

    // 投的顺序故意从低到高，让「投的顺序」和「出的顺序」不可能长得一样。
    pool.start(new OrderRunnable(1, 0), 1);
    pool.start(new OrderRunnable(5, 1), 5);
    pool.start(new OrderRunnable(9, 2), 9);

    const bool done = pool.waitForDone(3000);
    if (!done) {
        ++gLedger.stalled;
        qWarning().noquote() << QStringLiteral("      【4】waitForDone(3000) 没等到收干净 → 记 stalled");
        return;
    }
    const int n = 3;
    const QString gotOrder = intListToString(OrderRunnable::gOrderLog, n);
    verdict(Hard, QStringLiteral("【4】出场顺序（只有一个工人时，入场顺序 == 出队顺序）"),
            OrderRunnable::gOrderLog[0] == 9 && OrderRunnable::gOrderLog[1] == 5
                && OrderRunnable::gOrderLog[2] == 1,
            QStringLiteral("期望 9,5,1 实测 %1").arg(gotOrder));
    qInfo().noquote() << QStringLiteral("      为什么这条敢当硬结论：maxThreadCount==1 把执行压成了\n"
                                        "        严格串行，于是「先进场的一定是队列里优先级最高的那个」是可判定的。\n"
                                        "      ★ 一旦 maxThreadCount>1，多个任务会同时在跑，出场顺序就退化成\n"
                                        "        Soft —— 抄这段代码去核对顺序之前，先看你把上限设成了几。\n"
                                        "      另外两条别记混：start(runnable, int) 这个 int 只管排队（头文件 37 行），\n"
                                        "      交给操作系统的线程优先级走 setThreadPriority(QThread::Priority)（68 行，\n"
                                        "      那个才是 qthread.h 41~53 行那个枚举，见第一节那八个值）。");
}

// 【5B】的探针：一个只会在析构时「举手」的小对象。
// 它唯一的作用是被塞进 lambda 的捕获里 —— 捕获随 QGenericRunnable 一起被销毁时，
// gProbeDtor 加一，于是「Qt 到底删没删我那个任务」这件事第一次变得可以数。
struct Probe
{
    ~Probe() { ++gProbeDtor; }
};

static void phase5_tryStartDoesNotQueue()
{
    section(QStringLiteral("【5】tryStart 不排队：失败了，对象归谁？两族重载差一行源码"));

    QThreadPool pool;
    pool.setMaxThreadCount(1);
    pool.start(new BlockerRunnable(300));
    if (!waitWorkerAlive(pool)) {
        ++gLedger.stalled;
        qWarning().noquote() << QStringLiteral("      【5】前提没建立 → 这一节作废");
        pool.waitForDone(2000);
        return;
    }
    qInfo().noquote() << QStringLiteral("      前提已看见：activeThreadCount=%1，maxThreadCount=%2 → "
                                        "现在没有空闲工人，唯一那条还要忙 300ms")
                             .arg(pool.activeThreadCount()).arg(pool.maxThreadCount());

    // A. 老式：tryStart(QRunnable*)（头文件 38 行）。返回 false 时**对象还在你手里**。
    const int runBefore = gSumRuns.loadRelaxed();      // ★ 用「只数 SumRunnable」的那个，见第三节末尾
    const int dtorBefore = gDtorCount.loadRelaxed();
    auto *rejected = new SumRunnable(0, 8, nullptr, "p5");   // 默认 autoDelete=true
    const bool startedA = pool.tryStart(rejected);
    checkBool(QStringLiteral("【5A】没有空闲工人时 tryStart(QRunnable*) 返回 false"), startedA, false);
    if (!startedA) {
        delete rejected;                       // ★ 归我删：不删就是泄漏
        checkNum(QStringLiteral("【5A】被拒的任务没执行（SumRunnable 进场新增）"),
                 gSumRuns.loadRelaxed() - runBefore, 0);
        checkNum(QStringLiteral("【5A】我删一次之后析构新增"),
                 gDtorCount.loadRelaxed() - dtorBefore, 1);
    } else {
        ++gLedger.stalled;                     // 前提没成立（居然有空闲工人）→ 这一节作废
    }

    // B. 新式：tryStart(Callable&&)（50~55 行声明，函数体 94~102 行）。
    //    第 97 行 new 出 QGenericRunnable，第 98 行试投，第 100 行 **delete runnable** ——
    //    源码替你收尾。探针：往 lambda 的捕获里塞一个小对象，它析构时把原子 +1。
    //    ★ 为什么不用 shared_ptr 计数：那样算的是「捕获有没有被销毁」，
    //      而销毁捕获的人是 ~QGenericRunnable —— 要证的正是「那个对象被 delete 了」，
    //      用一个自定义类型的析构当证据，指的就是它自己，没有绕。
    gProbeDtor.storeRelaxed(0);
    gProbeRun.storeRelaxed(0);
    const bool startedB = pool.tryStart([probe = Probe()]() {
        ++gProbeRun;
        Q_UNUSED(probe)
    });
    checkBool(QStringLiteral("【5B】同一前提下 tryStart(lambda) 也返回 false"), startedB, false);
    if (!startedB) {
        // ★★ 实测是 2 次析构，不是 1 次 —— 而且这**不是** Qt 替你 double-free，是两个不同对象各删一次：
        //   ① 你在这一行写的临时 lambda 对象本身（调用方栈上的那个右值），它的捕获 probe 在
        //      **本条语句结束（分号）时**随临时对象析构 → 第 1 次 ~Probe；
        //   ② QRunnable::create 里 `new QGenericRunnable(std::forward<Callable>(fn))` 把你的 lambda
        //      **搬进** CompactStorage（qrunnable.h 92 行 / qfunctionaltools_impl.h 的 StorageByValue）。
        //      Probe 因为**用户声明了析构函数**，其隐式移动构造函数被抑制（C++ 规则：有用户声明的
        //      dtor ⇒ 不生成隐式 move ctor），只剩拷贝构造。于是这次「移动」实际是**拷贝**——
        //      存储里另有一份 Probe。tryStart(QRunnable*) 返回 false 后，源码第 100 行
        //      `delete runnable;`（qthreadpool.h 94~102）删掉 QGenericRunnable → 存储里的 lambda 析构
        //      → 第 2 次 ~Probe。
        //   所以：若 Qt **不**执行第 100 行那句 delete，就只有①那一次析构（计数 1）；正因为它删了，
        //   我们才看到第 2 次 → 这个「2」恰恰是「Qt 替你 delete 了那个 QRunnable」的铁证，
        //   而多出来的另一次只是调用方临时 lambda 的正常收尾。二次释放会让进程当场崩，这里没有崩，
        //   两个对象地址也不同 —— 是「两份拷贝各自析构」，不是「同一份删两次」。
        QThread::msleep(1);                    // 等一拍：万一被接了，让 run 有时间进场
        checkNum(QStringLiteral("【5B】失败时探针析构次数（2 = 调用方临时 lambda 1 次 + Qt 第 100 行 delete 那份拷贝 1 次）"),
                 gProbeDtor.loadRelaxed(), 2);
        checkNum(QStringLiteral("【5B】失败时 lambda 体执行次数"), gProbeRun.loadRelaxed(), 0);
        qInfo().noquote() << QStringLiteral("      ★ 这就是「两族长得很像的重载，所有权规矩不一样」：\n"
                                            "        A 族给你裸指针，失败归你删；B 族自己 new 自己删（源码 94~102 行）。\n"
                                            "        抄错族的后果一边是泄漏、一边是二次释放，编译器都不帮你 ——\n"
                                            "        所以这一节把两族都投一遍，各数一遍析构。\n"
                                            "      ★★ 为什么这一节是 2 不是 1（本讲第一版就栽在这儿）：\n"
                                            "        探针被【拷贝】进了 QGenericRunnable，而不是【移动】——\n"
                                            "        因为 Probe 自带析构函数，按 C++ 规则它就没了隐式移动构造，\n"
                                            "        只能走拷贝。于是世上同时有两份 Probe：调用方那枚临时 lambda 里一份、\n"
                                            "        存进池任务里一份。临时 lambda 在语句末尾自己析构（第 1 次），\n"
                                            "        存进任务的那份被第 100 行 delete 掉（第 2 次）。\n"
                                            "        要证的「Qt 删了任务」= 那份拷贝的析构，它确实在；\n"
                                            "        多出来的一次是调用方临时对象该做的收尾，不是重复释放。\n"
                                            "      反面教训：**别把「析构次数 == 1」当成「只 new 了一次」的证据**，\n"
                                            "        值语义类型（拷贝 vs 移动）会改变对象份数。想只数 Qt 那一次，\n"
                                            "        就给 Probe 补一个 `Probe(Probe&&) noexcept = default;`（连同析构，\n"
                                            "        见 Rule of Five），移动就不产生第二份了。");
    } else {
        ++gLedger.stalled;
        pool.waitForDone(2000);                // 被接了的话让池把这条收干净，别把尸体留到下一节
    }
}

static void phase6_tryTakeAndClear()
{
    section(QStringLiteral("【6】tryTake 撤一条、clear 撤一摞：都只管还在排队的"));

    QThreadPool pool;
    pool.setMaxThreadCount(1);
    pool.start(new BlockerRunnable(300));
    if (!waitWorkerAlive(pool)) {
        ++gLedger.stalled;
        qWarning().noquote() << QStringLiteral("      【6】前提没建立 → 这一节作废");
        pool.waitForDone(2000);
        return;
    }

    const int runBefore = gSumRuns.loadRelaxed();
    const int dtorBefore = gDtorCount.loadRelaxed();

    // 排 3 条：t1 待取回，t2/t3 本想「留着让 waitForDone 收尾」—— 这个念头马上会被
    // clear() 打掉，而那正是本节要演示的第二件事。
    auto *t1 = new SumRunnable(200, 210, nullptr, "p6");
    auto *t2 = new SumRunnable(210, 220, nullptr, "p6");
    auto *t3 = new SumRunnable(220, 230, nullptr, "p6");
    pool.start(t1);
    pool.start(t2);
    pool.start(t3);

    const bool took = pool.tryTake(t1);        // ← [[nodiscard]]：接住返回值才编得过（头文件 85 行）
    checkBool(QStringLiteral("【6】tryTake(还在队列里的那条) 返回 true"), took, true);

    // ★ 取回来之后**本讲不 delete**，理由值得单独记：
    //   tryTake 的返回值能证明「它不在队列里了」，但「谁负责这块内存」这件事
    //   本机头文件只给了一个签名（85 行），证明不了。取回后自己删，万一 Qt
    //   也删一次，就是二次释放 —— 那是**当场崩、日志一行都不留**的失败形状；
    //   不删，最坏是泄一条几十字节的对象，而且析构计数会告诉我实情。
    //   拿不准所有权时宁可泄漏不要乱删：**泄漏能被计数发现，崩溃不能被日志解释。**
    //   （练习 4 让你把这条补成「读文档 + 计数两边都对得上」的确定写法。）
    if (!took)
        ++gLedger.stalled;

    checkNum(QStringLiteral("【6】被取回的那条没有执行（进场新增）"),
             gSumRuns.loadRelaxed() - runBefore, 0);

    // 再排 4 条，然后 clear() 把整摞撤掉 —— 注意是**整摞**：
    // 此刻队列里是 [t2, t3, 新4条]，clear() 不会只撤「我刚新加的那 4 条」。
    // 第一版我以为会留下 t2、t3，于是把期望写成「进场 2 次」；
    // 按头文件 81 行 clear() 的语义（撤掉**所有**还在排队的），正确答案是 0 次。
    // 这类差一个数量级的错，只有把「提交了几条 / 进场了几条」两个数都写死才对得上。
    for (int i = 0; i < 4; ++i)
        pool.start(new SumRunnable(300 + i * 10, 300 + (i + 1) * 10, nullptr, "p6"));
    pool.clear();
    const bool done = pool.waitForDone(3000);
    checkBool(QStringLiteral("【6】clear() 之后 waitForDone 能收干净（队列已空，场上只剩 blocker）"), done, true);
    // 提交 7 条 SumRunnable：1 条被 take、6 条被 clear → **一条都不该进场**
    checkNum(QStringLiteral("【6】被 take 的 1 条 + 被 clear 的 6 条，进场几次（本节点 SumRunnable）"),
             gSumRuns.loadRelaxed() - runBefore, 0);
    // ★ 这一笔只能是 Obs，而且值得盯着看：它有两个都对得上文档的答案。
    //   blocker 一定 +1（池跑完自己删，autoDelete=true，见【1】）。
    //   clear() 撤下的那 6 条呢？头文件 81 行只说「把排队的都撤掉」，
    //   撤掉之后**删不删**——本机头文件给不出证据。于是：
    //     · 实测 +7 → 说明 clear() 遵守 autoDelete（撤下即删）；
    //     · 实测 +1 → 说明 clear() 只撤不删，那 6 条归 Qt 的队列记账或直接泄漏。
    //   两种都跟「撤不下已经在跑的任务」这句话兼容，所以**这里不下结论**，只把数打出来。
    //   （练习 4 就是把这一条升成 Hard：先查文档定所有权，再让计数两边闭合。）
    verdict(Obs, QStringLiteral("【6】本节点析构计数新增（blocker 必 +1，clear 撤的 6 条是问题）"), true,
            QStringLiteral("%1 次").arg(gDtorCount.loadRelaxed() - dtorBefore));
    qInfo().noquote() << QStringLiteral("      ★ 看清边界：clear() 撤得下**队列**，撤不下**已经在跑的任务**。\n"
                                        "        所以「用户点了取消，界面就该立刻停下来」这句话，\n"
                                        "        线程池这一层只能做到一半 —— 剩下那一半是第 7 讲：\n"
                                        "        任务自己每干一小段就回头查一次标志位（QAtomicInt / QPromise::isCanceled）。\n"
                                        "        这是本讲和第 7 讲的分界线，别在第 6 讲里找取消的 API：没有。");
}

static void phase7_reserveAndWaitForDone()
{
    section(QStringLiteral("【7】reserveThread / startOnReservedThread / waitForDone 的一假一真"));

    QThreadPool pool;
    pool.setMaxThreadCount(4);

    // ① 限时等待的形状：先投一个一定会超时的任务，再等足时间收它。
    pool.start(new BlockerRunnable(600));
    const bool quick = pool.waitForDone(200);            // 一定还没完
    const bool sure = quick ? true : pool.waitForDone(2000);
    checkBool(QStringLiteral("【7】waitForDone(200) 在任务还要 600ms 时必须返回 false"), quick, false);
    checkBool(QStringLiteral("【7】再 waitForDone(2000) 应当收到 true"), sure, true);
    qInfo().noquote() << QStringLiteral("      ★ 为什么允许在这里睡主线程：BlockerRunnable 只 msleep，\n"
                                        "        一个事件都不往主线程投，所以「等的人」和「被等的人」没有环。\n"
                                        "        把任务换成【8】那种 emit 给主线程的，这一觉就可能变成死锁 ——\n"
                                        "        那张环图画在【8】的说明里，两种写法的差别就这一句。\n"
                                        "      另外：waitForDone() **不传参数 = Forever**（头文件 79 行默认实参），\n"
                                        "        本仓库要求编号讲义里一处都不许出现裸 waitForDone()：\n"
                                        "        ctest 那 120 秒超时会替你收尸，但 CI 里一条无日志的红比崩更难查。");

    // ② 预留一条线程，并用 startOnReservedThread 把任务丢到那条预留线程上。
    const int sumBefore2 = gSumRuns.loadRelaxed();
    pool.reserveThread();                                // 头文件 71 行
    pool.startOnReservedThread(new SumRunnable(400, 420, nullptr, "p7"));
    const bool done2 = pool.waitForDone(2000);
    checkBool(QStringLiteral("【7】预留线程之后 waitForDone(2000) 收到 true"), done2, true);
    checkNum(QStringLiteral("【7】startOnReservedThread 的任务确实进场了几次"),
             gSumRuns.loadRelaxed() - sumBefore2, 1);
    pool.releaseThread();                                // ★ 配对：借了就还
    verdict(Obs, QStringLiteral("【7】此刻池内账面"), true,
            QStringLiteral("activeThreadCount=%1、maxThreadCount=%2、expiryTimeout=%3ms")
                .arg(pool.activeThreadCount()).arg(pool.maxThreadCount()).arg(pool.expiryTimeout()));
    qInfo().noquote() << QStringLiteral("      ★ 本讲**不**演示两件事，都是故意的：\n"
                                        "        ① 未 reserve 就调 startOnReservedThread：文档说这会要命\n"
                                        "           （进程直接没了）。本机头文件 45 行只给签名，证明不了，\n"
                                        "           而一次崩溃换不来一句可靠的结论，所以只写警告、不做实验。\n"
                                        "        ② 忘了 releaseThread 会不会让进程退不出去：这要靠「挂着不放\n"
                                        "           然后看它挂多久」来证明，而那是把整个演示钉死。第 7 讲用\n"
                                        "           一个有期限的复现版本讲清楚，这里只留一句话：\n"
                                        "           **reserve/release 是一对引用计数，借了不还，池就当你还要用。**");
}

// ============================================================================
// 九、第【8】节：结果回主线程（本讲唯一需要事件循环的一节）
// ----------------------------------------------------------------------------
//                    worker 线程                     主线程（有事件循环）
//   ┌──────────────────────────────┐   emit   ┌──────────────────────────────┐
//   │ SumRunnable::run()           │          │ Sink 的亲和性 = 主线程        │
//   │   sink->publishPartial(...)  │─────────▶│ 队列里多出一条 QMetaCallEvent │
//   │   （立刻返回，不等主线程）   │          │ exec() 取出 → 槽在主线程执行 │
//   └──────────────────────────────┘          └──────────────────────────────┘
//      ▲                                                    │
//      │  ★ 环在这里：如果主线程此时正卡在 waitForDone() 里，
//      │              那条 QMetaCallEvent 就永远排在队列里没人取；
//      │    任务本身不依赖主线程 → 它还会跑完，于是**看起来**没事。
//      │    但只要任务改成「等主线程给个回执再返回」，两边互等 = 死锁。
//      └────────────────────────────────────────────────────┘
// ============================================================================
struct Phase8State
{
    // ★ 这一整个结构体**只用普通类型**，和第三节的 QAtomicInt 探针是刻意的对照：
    //   下面每一个字段都只在「接收者在主线程、连接是 queued」这条路上被写，
    //   写的人和读的人（600ms 那档定时器）在同一个线程里排队，不存在并发。
    //   反过来，只要有一个字段可能被 worker 线程直接写（比如你在 run() 里 ++received），
    //   它就必须换成 QAtomicInt —— 第 4 讲【1】讲的正是「编译器不拦你，内核也不拦你」。
    int received = 0;                 // 主线程收到几条结果
    int wrongThread = 0;              // 有几条**不在**主线程上被处理（必须 0）
    QSet<int> ids;                    // 收到的 id 集合
    qint64 sum = 0;                   // 收到的值累加
    int ghostCallbacks = 0;           // 挂 ghost 作 context 的那条连接被调用几次（期望 0）
    QString mainThreadId;
};
static Phase8State gP8;

static void phase8_startResults(Sink *sink, QThreadPool *pool)
{
    section(QStringLiteral("【8】结果回主线程：4 条 emit + 一条「接收者提前死掉」的连接"));

    gP8.mainThreadId = QString::number(qint64(QThread::currentThreadId()), 16);

    // ① 正常的那条：context 写 sink（本讲让它活到最后，所以写不写等价；
    //    写上是为了把规矩摆在明处 —— 机制见 metaobject 第 4 讲，价格见 network 第 2 讲⑦）。
    //    ★ 必须**先 connect，再 start**。queued 连接是在 emit 那一刻按「当时存在的连接」
    //      逐条生成 QMetaCallEvent 投进接收者所在线程的队列。emit 时连接还不存在，
    //      就一个事件都不会投 —— 结果不是「晚一点到」，是**永久丢失**。
    //      第一版我把这条 connect 写在 main 里、start 之后，账面「收到 4 条」于是变成赌局：
    //      赌的是「worker 线程比主线程接下来那两行代码慢」。这种赌局在回环上十赌九赢，
    //      在满载的 CI 机器上输一次就够你查一整天。
    QObject::connect(sink, &Sink::partialReady, sink, [](int id, int value) {
        ++gP8.received;
        gP8.ids.insert(id);
        gP8.sum += value;
        const QString tid = QString::number(qint64(QThread::currentThreadId()), 16);
        if (tid != gP8.mainThreadId)
            ++gP8.wrongThread;              // 期望 0：落点是**接收者的线程**，不是发射者的
        qInfo().noquote() << QStringLiteral("      [主线程收到] id=%1 value=%2 线程=%3")
                                 .arg(id).arg(value).arg(tid);
    });

    // ② 另一条：context 是一个马上就死的 QObject（模拟「界面已经关了，后台还在算」）。
    auto *ghost = new QObject;
    ghost->setObjectName(QStringLiteral("界面-已关闭"));
    QObject::connect(sink, &Sink::partialReady, ghost, [](int id, int value) {
        ++gP8.ghostCallbacks;
        qInfo().noquote() << QStringLiteral("      【不该出现】ghost 还收到了 id=%1 value=%2")
                                 .arg(id).arg(value);
    });

    QThreadPool &p = *pool;
    p.setMaxThreadCount(4);
    for (int i = 0; i < 4; ++i)
        p.start(new SumRunnable(500 + i * 20, 500 + (i + 1) * 20, sink, "p8"));

    // ③ 同步 delete，**不用定时器**。
    //    此刻主线程还在同一个回调里，事件循环一圈都没转 → 上面那 4 条 emit 产生的
    //    QMetaCallEvent（如果已产生）全躺在队列里。~QObject 做两件事：
    //      · 把以它为 sender 或 receiver 的连接全部断开；
    //      · 把它名下**已经排在队列里的事件**一并清掉（否则事件投递时接收者早没了，
    //        那是必崩的事，所以 Qt 一定要做这一步）。
    //    于是 ghostCallbacks 恒为 0 —— 这条敢标 Hard。
    //    ★ 第一版这里写的是 QTimer::singleShot(30, ghost, …)：看着更「真实」，其实是赌。
    //      SumRunnable 只算 20 个数，emit 在 start 后一两毫秒内就发生完了，
    //      30ms 之后才 delete —— 而事件是 **emit 那一刻**生成的，不是连接断开那一刻，
    //      delete 只是把它们从队列里摘掉；时序一变（30ms 内没 emit 完 vs emit 完了）
    //      结论就正反翻转。同步 delete 把这件事从「赌时序」变成「赌机制」，
    //      机制是有保证的，时序没有。
    qInfo().noquote() << QStringLiteral("      现在同步 delete「%1」：它一死，以它为 context 的"
                                        "那条连接断开，队列里指向它的事件也一起清掉")
                             .arg(ghost->objectName());
    delete ghost;

    // ★ 这一节**不** waitForDone：立刻返回，把主线程交还给事件循环，
    //   否则上面那四条 QMetaCallEvent 没人取，【8】的账一条都收不到。
    //   （前面七节全部用 waitForDone 同步收尸，只有这一节必须异步 —— 分界就画在「任务有没有
    //     往主线程投事件」上。把【7】那套「主线程睡等」搬到这里，就是那张环图的死锁。）
}

static void phase8_check()
{
    const QString nowThread = QString::number(qint64(QThread::currentThreadId()), 16);
    checkNum(QStringLiteral("【8】4 条任务 → 主线程收到几条结果"), gP8.received, 4);
    checkNum(QStringLiteral("【8】id 集合的大小（期望 4 个不同的 id：500,520,540,560）"), gP8.ids.size(), 4);
    checkNum(QStringLiteral("【8】不在主线程上处理的条数（连接是 queued，落点由**接收者亲和性**定）"),
             gP8.wrongThread, 0);
    const qint64 want = checksumOf(500, 580) % 100000;
    checkNum(QStringLiteral("【8】收到的值合起来（串行复算值）"), gP8.sum, want);
    checkNum(QStringLiteral("【8】context 死了的那条连接：回调进来几次"), gP8.ghostCallbacks, 0);
    verdict(Obs, QStringLiteral("【8】核对时所在的线程"), true,
            QStringLiteral("核对线程 id=%1，与主线程%2").arg(nowThread)
                .arg(nowThread == gP8.mainThreadId ? QStringLiteral("相同") : QStringLiteral("不同")));
    qInfo().noquote() << QStringLiteral("      ★ 三句话把这一节钉住：\n"
                                        "        ① QRunnable 自己不能 emit，所以结果必须由一个 QObject 代发；\n"
                                        "        ② 落点线程 = **接收者（Sink）的亲和性**，不是发射者的；\n"
                                        "        ③ 主线程必须回来转事件循环，否则结果只堆在队列里 ——\n"
                                        "          这就是前面七节能同步、这节必须异步的分界。\n"
                                        "      常见错误写法：在 run() 里直接改界面控件（QLabel::setText）。"
                                        "编译不报错，\n"
                                        "      界面不更新，运气不好就崩 —— 第 2 讲的亲和性 + 第 3 讲的连接类型"
                                        "共同解释了为什么。");
}

// ============================================================================
// 十、合账
// ============================================================================
static void reconcile(QThreadPool *globalPoolHint)
{
    section(QStringLiteral("【合账】全局的三笔流水 + 退出码"));
    Q_UNUSED(globalPoolHint)

    verdict(Obs, QStringLiteral("任务对象构造总数"), true, QString::number(gCtorCount.loadRelaxed()));
    verdict(Obs, QStringLiteral("任务对象析构总数"), true, QString::number(gDtorCount.loadRelaxed()));
    verdict(Obs, QStringLiteral("run() 进场总数"), true, QString::number(gRunCount.loadRelaxed()));
    verdict(Obs, QStringLiteral("全局池此刻"), true,
            QStringLiteral("maxThreadCount=%1、activeThreadCount=%2、expiryTimeout=%3ms")
                .arg(globalPoolHint->maxThreadCount()).arg(globalPoolHint->activeThreadCount())
                .arg(globalPoolHint->expiryTimeout()));
    qInfo().noquote() << QStringLiteral("      ★ 前两笔（构造 vs 析构）**故意不给硬结论**：\n"
                                        "        【6】有一条 tryTake 回来的对象故意留着不删，另有 6 条的去向\n"
                                        "        本机头文件证明不了（clear 撤下即删？还是只撤？），所以\n"
                                        "        「构造 == 析构」这一场是**明知说不清**的。把它标成实测，\n"
                                        "        是把话说清楚；真要在你自己项目里核这一对，\n"
                                        "        得先把每条任务的所有权规矩都定下来，再谈平账。\n"
                                        "      「进场总数」比「提交总数」小，差的就是 tryTake/clear 撤掉的那几条 ——\n"
                                        "      这一句【6】里已经按节核对过（投 7 条、进场 0 条），这里只给个总览。");

    const int code = gLedger.exitCode();
    qInfo().noquote() << QStringLiteral("\n硬账：核对 %1 项，不符 %2 项，整节作废 %3 项，总闸%4 → 退出码 %5")
                             .arg(gLedger.total).arg(gLedger.failed).arg(gLedger.stalled)
                             .arg(gLedger.watchdogFired ? QStringLiteral("跳过了一次") : QStringLiteral("没跳过"))
                             .arg(code);
    if (gLedger.total == 0)
        qWarning().noquote() << QStringLiteral("      一项硬核对都没做，退出码判 1：空账不等于通过。");
    QCoreApplication::exit(code);
}

// ============================================================================
// 十一、main
// ============================================================================
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("threads_06_qrunnable_threadpool"));

    qInfo() << "================================================================";
    qInfo() << "threads 第 6 讲 · QRunnable + QThreadPool（Qt ="
            << QString::fromLatin1(QT_VERSION_STR) << "）";
    qInfo() << "================================================================";

    auto *sink = new Sink(&app);                       // 主线程，亲和性 = 主线程
    // ★ 这个池**故意不挂 parent**，改由收尾处显式 delete（见下面 600ms 那一档）：
    //   它管理的任务里攥着 sink 的裸指针，而 QObject 的孩子是按挂上的顺序删的 ——
    //   sink 先挂，就会先死，池后死；池的析构要等所有 run() 返回，那几条任务一进场
    //   就摸到一个已经没了的 sink。挂 parent 看着「自动收尾」，其实把析构顺序交给了
    //   子对象列表的顺序 —— 这种依赖代码里看不见。所以：**谁被别人引用，谁最后死**，
    //   顺序要写在代码里，不能写在 Qt 的默认行为里。
    auto *poolForResults = new QThreadPool;
    QThreadPool *global = QThreadPool::globalInstance();

    // ★ 故意不动全局池的 maxThreadCount：第 5 讲已经证明「压全局池 = 压低全应用并行度」，
    //   这里再压一次会把那一讲的结论搞混。本讲所有并发实验都用**私有池**。
    qInfo().noquote() << QStringLiteral("  全局池（只用它的账面做对照）：maxThreadCount=%1，"
                                       "idealThreadCount=%2")
                             .arg(global->maxThreadCount()).arg(QThread::idealThreadCount());

    // 主时间轴：一次触发，前七节同步跑完，第八节起步后交给事件循环。
    QTimer::singleShot(0, [sink, poolForResults, global]() {
        Rendezvous rv4(4);                 // 【2】要凑齐 4 条
        Rendezvous rv6(6);                 // 【3】要凑齐 6 条（**故意让它凑不齐**）

        phase1_shapeAndAutoDelete();
        phase2_concurrency(rv4);
        phase3_maxIsARealCeiling(rv6);
        phase4_priorityIsAnInt();
        phase5_tryStartDoesNotQueue();
        phase6_tryTakeAndClear();
        phase7_reserveAndWaitForDone();

        // 【8】起头：两条连接 + 投任务 + 同步 delete ghost，然后**立刻返回**让事件循环转起来。
        //        连接全部写在 phase8_startResults 里面，不在这里补 —— 原因见那一节 ① 的注释。
        phase8_startResults(sink, poolForResults);

        // 600ms 后收账：4 条 queued 事件早就投递完了（任务只算 20 个数）。
        // 这个「等多久」是 Soft 的部分，所以本讲的硬结论没有一条依赖它变小；
        // 唯一依赖它的是「别太短」—— 短到事件还没投递，四项核对会一起变红，
        // 那种红一眼就知道是收账收早了，不是代码错。
        QTimer::singleShot(600, [sink, poolForResults, global]() {
            phase8_check();
            // ★ 先收池，再合账，最后才让 sink 有可能死。顺序：任务 → 池 → 发信人。
            //   这一行 delete 按文档会等在场任务跑完（本讲不测「它到底等多久」，
            //   测它得先把进程钉死；那条留给第 7 讲的有期限复现版本）。
            //   如果 15 秒总闸先跳了，这个池就留在这儿不删 —— 故意的：
            //   异常路径上不确定的对象**不要**再去碰它，进程马上退出，泄漏一条比二次释放安静。
            delete poolForResults;
            reconcile(global);
            Q_UNUSED(sink)
        });
    });

    // 总闸：正常路径走不到（最坏预算约 5 秒，这里是它的三倍）。
    // ★ 它必须用**不带 context** 的那一族（qtimer.h 83 行那句注释起，85~88 行把 receiver 写死 nullptr）：
    //   带 context 的版本 receiver 一死就不发了，而总闸恰恰要在「事情不对劲」的时候还能响。
    QTimer::singleShot(15000, []() {
        gLedger.watchdogFired = true;
        qWarning().noquote() << QStringLiteral("【总闸】15 秒到点：强制收尾（并发卡住绝不让进程挂死）");
        qInfo().noquote() << QStringLiteral("      到点时硬账已到 %1 项，进场流水 %2 次")
                                 .arg(gLedger.total).arg(gRunCount.loadRelaxed());
        QCoreApplication::exit(gLedger.exitCode());
    });

    return app.exec();
}

// ============================================================================
// 十二、速查（背不住就回来查）
// ============================================================================
/*
 * 两条投任务的路，先想清楚你要哪一条：
 *   pool->start(new MyRunnable(...))    ← 要带状态、要发信人、要跨很多参数：写类
 *   pool->start([…]{ … })               ← 一段闭包就够：用模板版（头文件 50~51 行），
 *                                          Qt 内部用 QGenericRunnable 打包（46~98 行）
 *   QtConcurrent::run(pool, fn)          ← **要结果**（QFuture）才用它；不要结果时
 *                                          qtconcurrentrun.h 39~40 行自己写了 nodiscard 劝退
 *
 * 所有权（本讲最容易出事的一格）：
 *   autoDelete 默认 **true**（qrunnable.h 20 行）→ start() 之后别自己 delete
 *   setAutoDelete(false) → 跑完池不删，**归你删**，而且只能在确定它不再被排队的时点删
 *   start(QRunnable*)     失败不会发生（它会排队）
 *   tryStart(QRunnable*)  返回 false → 对象还在你手里，**你删**（38 行那一族）
 *   tryStart(Callable&&)  返回 false → 源码第 100 行替你删（94~102 行），你什么都没拿到
 *   tryTake(ptr)          返回 true → 它不在队列里了；内存归谁**头文件没说**，
 *                                          本讲不删（宁可泄一条也不要二次释放）
 *   clear()               把队列里剩下的都撤掉；已在跑的一条都撤不下
 *
 * 「谁删的」怎么查：给每个任务带构造/析构计数（本讲第三节），只看差值不看绝对值。
 *
 * 并发度的三种说法，只有一种能当承诺：
 *   「峰值正好是 N」        → Soft，取决于调度器，别写进硬账
 *   「峰值不超过 N」        → Hard，是上限
 *   「要越过去才会成功的事，一次都没成功」 → 最结实的 Hard（本讲【3】：want=6 而 max=2）
 *
 * 等待：
 *   waitForDone(int msecs) / waitForDone(QDeadlineTimer)（77~79 行），**默认 Forever**
 *   主线程用它的唯一前提：被等的任务里没有一个要往主线程投事件
 *   有事件循环要转 → 一律改成「投完就返回，用信号/QTimer 收尾」（本讲【8】）
 *
 * 预留：
 *   reserveThread()（71）/ releaseThread()（72）是引用计数的一对
 *   startOnReservedThread(QRunnable*)（45）/ (Callable&&)（54~55）
 *     ★ **未预留时不要调**：文档说这会终止进程；头文件只给签名，本讲不做实验
 *   预留出来的线程不受 expiryTimeout（57/58，默认 30000ms）回收
 *
 * 优先级（三个不同的东西，别再记混）：
 *   start(runnable, int priority)      → 队列排序，裸 int，越大越先出队（37 行）
 *   setThreadPriority(QThread::Priority) → OS 线程优先级，枚举（68 行 + qthread.h 41~53 行）
 *   setServiceLevel(QThread::QualityOfService) → 服务质量档（74 行）
 *
 * 取消（本讲给不了的东西，明说）：
 *   QThreadPool **没有** cancel API。能做的只有「撤队列里的」（tryTake/clear），
 *   跑着的必须任务自己配合 → 第 7 讲（QAtomicInt 标志 / QPromise::isCanceled / 分片干活）。
 */

// 本文件有 1 个 Q_OBJECT 类（Sink），AUTOMOC 会生成 .moc 并在这里包含。
#include "06_qrunnable_threadpool.moc"

// ——— 老师傅的话 ———
/*
 * 徒弟，线程池这四个字里，「池」是最好懂的部分，「线程」是最容易出事故的部分。
 * 我先把这一讲的骨架给你：**QRunnable 是一个没有身份证的包裹**。
 * 它没有 QObject 身份，所以没有元对象、不能 emit、不认信号槽、没有亲和性；
 * 它连拷贝都不允许（qrunnable.h 22 行那个 Q_DISABLE_COPY 展开成 `= delete`）。
 * 很多人第一次撞上这个墙，是在 run() 里写 `emit progressReady(v)` —— 编译器直接告诉你
 * 「没有这个成员」，然后他给 QRunnable 加个 Q_OBJECT 想「补上」，moc 会说
 * 「QRunnable 不是 QObject 派生」。到这一步就该换设计，不是换写法：
 * **要么手里攥一个 QObject 让它代发（本讲【8】），要么别用线程池、用 worker-object（第 1 讲）。**
 *
 * 第二件我想让你记住的，是【3】那一节的形状。
 * 你要证明「并发不超过 2」，最自然的写法是「跑起来数一下峰值」——那是 Soft，
 * 它只在机器不忙的时候对。我教你的写法是：造一个**只有越过上限才会成功**的事件
 * （六条线程凑齐才放行），然后核对它一次都没发生。这条是 Hard，因为它不依赖调度器，
 * 依赖的是「池不可能开出比 maxThreadCount 更多的工人」这个不变量。
 * 这一手不只用在测试线程池。你以后要证明「连接池没溢出」「缓存没超过上限」「并发下载
 * 没超过 N」，全用这个形状：**别去数它到了哪儿，去证明它过不去。**
 *
 * 第三件，关于「谁删的」。
 * 【5】那两族长得很像的重载，一个失败要你删，一个失败替你删；【6】里 tryTake 成功之后
 * 归谁，本机头文件压根没说。这种地方我不敢猜，就写了一条规矩：**拿不准所有权时，
 * 宁可泄漏一条对象，也不要二次释放。** 泄一条 100 字节的对象，析构计数会告诉我、
 * 内存分析工具会告诉我，进程还活着、日志还在；二次释放是当场崩、栈未必有、
 * 复现未必稳 —— 这两种「错」的代价不是一个量级。所以【6】那段注释你照着抄进项目，
 * 然后按练习 4 把它补成确定的写法：先查文档、再用计数两边对齐，最后才敢 delete。
 *
 * 最后一件，是【7】和【8】之间那条线。
 * 我在【7】里让主线程睡在两三个 waitForDone 上，那是**故意破例**，而且每一处都写了
 * 「为什么这一处可以睡」：那些任务只 msleep，一个事件都不往主线程投，环闭不上。
 * 你在界面代码里敢不敢这么写？不敢。主线程一睡，界面就停，鼠标点不动，
 * 而 QThreadPool 里那些任务只要有一个是「算完 emit 给界面」的，那条 QMetaCallEvent
 * 就永远排在没人取的队列里 —— 更糟的是它**看起来没事**（任务本身不依赖你，它会跑完），
 * 只有等你哪天把任务改成「要界面回个话」，程序才当场钉死。
 * 这种「今天能用、明天改成需求就死」的代码，比一眼看得见的 bug 贵得多。
 * 所以：**有事件循环的线程，等线程池就靠信号；没有事件循环的线程（构建脚本、CI 冒烟），
 * 才允许 waitForDone，而且必须带期限。** 裸 waitForDone() 默认是 Forever，
 * 这一讲一处都不写，你也不要写。
 */

// ——— 小练习 ———
/*
 * 1. 把【8】那套「Sink 代发」改成**线程安全队列**：任务把结果塞进一个
 *    QMutex 保护的 QQueue，主线程每 50ms 取一批（QTimer + drainAll()）。
 *    核对同样的四项硬账（收到几条、值对不对、id 齐不齐、ghost 那几条 0 次），
 *    并回答：这两种写法在「界面卡顿」这件事上有什么差别？
 *    （提示：一种在结果多时每条结果都触发一次槽；另一种把 N 条合成一次。
 *      这就是 model_view 第 8 讲「批量更新」的另一半。）
 * 2. 把【2】那个「凑齐 N 条才放行」的 Rendezvous 抽成独立类，加上**超时计数**：
 *    wait 返回 false 的次数也要记。然后在 maxThreadCount = 1/2/4/8 四档下各跑一遍
 *    want=4 的实验，把「命中次数、超时次数、在场峰值」列成一张表。
 *    回答：哪几列是 Hard、哪几列只能是 Soft？为什么「超时次数」永远不能当硬结论？
 * 3. 给 SumRunnable 加一个**每 4096 个数查一次**的取消标志（QAtomicInt），
 *    主线程 30ms 后把它置 1。核对两件事：① 任务确实在查了之后就返回（进场数不变、
 *    完成计数变小）；② **已经在跑的那条撤不下来**这件事被你的日志证实了。
 *    做完这一条，你就站到第 7 讲的门口了。
 * 4. 【6】里那条被 tryTake 回来的对象本讲故意没删。现在把它补成确定写法：
 *    先写下你查到的结论（文档怎么说所有权），再把这条对象改成
 *    `setAutoDelete(false)` + 由你删，然后把「构造 == 析构」这一对从 Obs 升成 Hard。
 *    ★ 关键一步：要让它**可判定**，你得先给每条任务一个「我删过」的位标志，
 *      谁删的只允许一个。做完说明：为什么这一对能当硬账，而【合账】那一屏当时不能。
 * 5. 把【4】的 maxThreadCount 改成 2，重跑一次优先级实验。
 *    你多半会发现顺序不再是 9,5,1 —— 回答：**这是 Qt 骗了你，还是你的前提没了？**
 *    然后把这条核对从 Hard 降级成 Soft，并在代码注释里写清「降级的那一句为什么」。
 *    （这一题练的是：技术材料里改一个参数，就得重算一遍哪些结论还站得住。）
 * 6. 用 `pool->start([...]{})`（模板版，头文件 50~51 行）重写【2】的六个任务，
 *    一个 QRunnable 子类都不写。对照源码 46~98 行说明：
 *    为什么这个 lambda 只实例化一份 QGenericRunnable<你的 lambda 类型>，
 *    而不是变成 std::function（提示：48 行那句注释、以及 40~43 行为什么被标成待移除）。
 * 7. 资深题：写一个 `BoundedRunner`，对外只给两个动作 ——
 *    `submit(job)` 和 `requestStop()`，内部用**私有** QThreadPool（不许碰 globalInstance，
 *    理由见第 5 讲：压全局池 = 压低全应用并行度），要求：
 *      · submit 满了不排队、直接返回 false（对应本讲【5A】，并说清对象归谁）；
 *      · requestStop 之后：队列里的撤掉（clear）、跑着的在下一个检查点退出（练习 3）；
 *      · 析构顺序固定：**先确认所有工人都离场了，再销毁那个被 emit 的 Sink**；
 *        析构函数里不许出现裸 waitForDone()，必须带期限并把超时打印出来。
 *    最后给这个类补一组第【8】节那种「接收者提前死掉」的测试：
 *    证明它既不崩、也不泄漏第二条连接。这一题做完，【7】里那句
 *    「借了不还，池就当你还要用」就不再是听说的了。
 */
// ============================================================================
