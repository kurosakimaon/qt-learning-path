// ============================================================================
// 第 7 讲 · 取消与收尾：让一条已经在跑的活自己走出来
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   第 6 讲结尾留了一句「取消必须任务自己配合」，这一讲把那句话拆成可执行的规则。
//   你手里此刻有三种「停」的念头，而其中两种是错的：
//     · 「能不能把已经在跑的任务撤下来？」—— 不能。没有任何 API 做得到。
//     · 「能不能把线程强行杀了？」—— 能（QThread::terminate，qthread.h 90 行），
//       但你拿不到池里的线程，而且 terminate 杀掉的是整条线程：它持有的锁、
//       正在写的半截数据结构、还没返回的栈，全都留在原地。这是「停下来了」和
//       「停下来且还能继续用」的分界线。
//     · 「那正确的做法是什么？」—— 让任务自己在**检查点**上看一眼标志，然后 return。
//       这是本讲全部八节都在反复刻画的那一个形状。
//
//   八节地图（每一节都有可打印的结论，硬账进退出码）：
//     1) 协作式取消的最小正确形状：一个原子标志 + 循环体里的一句 if。
//        我用「置位前后各做了多少块」把它变成能核对的数。★ 关键在于：我把
//        「置位时还剩块没做」这个**前提本身**也记进账本 —— 前提没成立时整节作废
//        （stalled），而不是硬拗出一个绿。
//     2) QThread::requestInterruption()（68/69 行）管的是**线程**，不是任务。
//        它在你自己 QThread 子类里好使；在线程池里没用 —— 因为 QThreadPool 的
//        头文件里没有任何接口能把它的工人线程列出来（第【2】节用编译期断言证明）。
//     3) QFuture::cancel()（qfuture.h 65 行）对三种局面做三件不同的事：
//        还在队列里、正在跑、已经跑完。只有前两种能立硬账，第三种我标 Obs 并说明为什么。
//     4) 一个零调度的取消：`QtConcurrent::run(空池, f)` 会在**调用者线程上**
//        走完 cancel + finish + delete（qtconcurrentrunbase.h 62~68 行）。
//        这一节不需要任何线程，所以它可以当「取消语义」的对照组。
//     5) promise-hint 那一族 run<T>（qtconcurrentrun.h 70~85 行）要求函数**返回 void**，
//        结果只能走 promise.addResult()（qtconcurrentstoredfunctioncall.h 31~32 行
//        那句 static_assert 就是原话）。我第一版写错，编译器当场顶回来，记在正文里。
//     6) QPromise 的析构函数（qpromise.h 37~46 行）会替你 cancelAndFinish。
//        于是「谁负责收尾」这件事不靠约定，靠语言规则 —— 这是本讲唯一一条
//        单线程、可判定的收尾证明。
//     7) 收尾状态机：QThreadPool **没有 finished 信号**（编译期证明），
//        所以你只能自己轮询 `waitForDone(0)`（无期限 = 问一句就走），
//        配一条期限。第 6 讲说「池的析构到底等不等」证明不了，这一讲把它做成
//        一次带期限的实测，并且给出正确的收尾顺序。
//     8) aboutToQuit（qcoreapplication.h 209 行）：程序退出前最后一道闸门。
//        这里只许关闸、不许等待 —— 并且顺手把退出码的算法改成「只在 main 的
//        return 上算一次」，修掉第 6 讲那种「exit(code) 之后还有账要记」的漏洞。
//
//   ★ 运行前须知（和前面几讲一样，先说清楚哪里我不试）：
//     · 本讲一次都不调用 QThread::terminate()。它确实存在（qthread.h 90 行，
//       第【三】节的编译期断言会打出「它是有的」），但它杀掉线程之后的进程状态
//       不可判、不可复现，写进一份要进 CI 的教材就是在教人赌命。要知道它坏在
//       哪里，读它的文档，然后记住一句话就够：**terminate 是给「我要退出了、
//       什么都不管了」准备的，不是给「取消一个任务」准备的。**
//     · 本讲不去测「池的析构在有条任务卡住时到底会不会挂住」。那种测法要么
//       超时、要么让总闸跳，两者都会把一节实验变成噪音。第【7】节量的是
//       **确定已经空了之后** delete 池花了几毫秒 —— 那是个能读的数。
//     · 所有会分叉的结果（续跑有没有被执行、取消一个已完成的 future 改不改状态位）
//       一律标 Obs，并把两种可能都写下来。**并发材料里最大的作假方式，就是把
//       「两种都合理」的说成「一定是这样」。**
// ============================================================================

// ============================================================================
// 一、头文件地图：这一讲引用的每一个数，都指到具体哪一行
// ----------------------------------------------------------------------------
// QtCore/qpromise.h
//   第 31 行 QPromise() = default;                 ← 默认构造的 promise 不算已启动
//   第 37~46 行 ~QPromise() 的函数体：★★ 本讲【6】的靶子
//              第 41 行 if (d.d && !(loadState() & State::Finished))
//              第 42 行     d.cancelAndFinish();      ← 取消
//              第 43 行     d.runContinuation();      ← 并且把下游叫醒
//              第 45 行 d.cleanContinuation();
//              ← 这三行是**头文件里看得见的真实源码**，不是文档措辞。
//                所以「忘记 finish 会怎样」这一题有确定答案：被取消，不会永远挂着。
//   第 49 行 QFuture<T> future() const
//   第 60~64 行 addResult(U&&, int index = -1)
//   第 75/76 行 start() / finish()                    ← reportStarted / reportFinished
//   第 80 行 bool isCanceled() const                  ← ★ 任务自查取消的唯一合法入口
//   第 83~88 行 setProgressRange / setProgressValue / setProgressValueAndText
//              ← 进度是「任务 → 外界」的合法通道，本讲【5】用它当观察窗
// QtCore/qfuture.h
//   第 65/66/67 行 void cancel() / bool isCanceled() / void cancelChain()
//   第 95~97 行 isStarted() / isFinished() / isRunning()
//   第 99 行 int resultCount() const
//   第 100 行 int progressValue() const
//   第 104 行 void waitForFinished()                  ← ★★ **没有带期限的重载**。
//              本讲第【6】节用编译期断言把这件事钉住：想等 future 又想要期限，
//              这个 API 不给你 —— 所以界面代码只能靠信号（QFutureWatcherBase 52 行）。
//   第 133/136/139/142 行 then(...) 四个重载：无策略 / Launch 策略 / 指定池 / 指定 context
//   第 147/151 行 onFailed(...)（仅在 @cond 非 QT_NO_EXCEPTIONS 时）
//   第 155/158 行 onCanceled(...)                     ← ★ 要求处理器返回 T（见 154 行的
//              std::is_invocable_r_v<T, Function>），因为整条链的类型不变。
// QtCore/qfuture_impl.h
//   第 38 行 enum class Launch { Sync, Async, Inherit };   ← 由 qfuture.h 第 14 行带进来
//   第 346~355 行 CompactContinuation::runImpl()：Sync 就在「谁 finish 就在谁身上」跑，
//              Async 才往池里投（353 行 pool->start(runObj)）
//   第 316/319 行 runObj->setAutoDelete(false) 与 ~CompactContinuation(){ delete runObj; }
// QtCore/qfutureinterface.h
//   第 57~68 行 enum State { NoState, Running, Started, Finished, Canceled, ... }
//              ← 这一串位就是「状态机」的全部字母表
//   第 92/93 行 setRunnable(QRunnable*) / setThreadPool(QThreadPool*)
//              ← ★ QFuture 之所以能撤下一条还在排队的任务，靠的就是它记着这两样
//   第 131/132/133 行 cancel() / cancelAndFinish()（内联，转手 cancel(CancelAndFinish)）
//                      / cancelChain()
//   第 154 行 operator== 比的是 d 指针 —— 所以「同一个 future」是可判定的
//   第 163 行 bool isChainCanceled() const             ← **只在接口上，不在 QFuture 上**
//   第 205~211 行 enum ContinuationType { Unknown, Then, OnFailed, OnCanceled }
//   第 218~221 行 setContinuation(...)（没有 add 版本）← 一个节点只有一个续跑槽
//   第 230~232 行 enum CancelMode { CancelOnly, CancelAndFinish } 与两个私有 cancel
// QtCore/qfuturewatcher.h
//   第 48 行 bool event(QEvent *event) override;        ← 它是靠事件收通知的
//   第 50~65 行 真正的信号：started / finished / canceled / resultsReadyAt / progress…
//   第 68 行 void cancel()                             ← 一个槽，直接转给 future
//   第 86/87 行 connectNotify / disconnectNotify        ← 「有人连我才去订阅」的开关
//   第 119~167 行 一整段 `#ifdef Q_QDOC`：★★ 这是**给文档生成器看的**，编译时不存在。
//              真正的声明在基类（50~72 行）。这一段是读头文件的功课，不是 bug：
//              我一开始以为「写 &QFutureWatcher<int>::finished 会编译不过」，
//              本机 -fsyntax-only 实测两种写法都过（名字在基类里找到了）。
//              所以这一条我改成教「怎么认出 Q_QDOC 段」，而不是教你一个不存在的错误。
//   第 176~184 行 setFuture 的函数体：同一条 future 直接早退（178~179 行），
//              否则先 disconnectOutputInterface(true) 再接上。
// QtCore/qthreadpool.h
//   第 77~79 行 waitForDone(int) / waitForDone(QDeadlineTimer = Forever)
//              ← ★ 第【7】节的全部戏法都在这：传 0 = 只问一句、立刻回来
//   第 81 行 void clear()                             ← 只管队列，管不了在跑的
//   第 83 行 bool contains(const QThread *thread) const ← 能「认出」，不能「列出」
//   第 85 行 [[nodiscard]] bool tryTake(QRunnable *)   ← 撤回还没开跑的那条
//   ★ 头文件里**没有** cancel(QRunnable*)，**没有** threads()，**没有** finished 信号。
//     这三条「没有」在第【2】节用编译期断言钉死 —— 比我在注释里说十遍都硬。
// QtConcurrent/qtconcurrentrunbase.h
//   第 40~44 行 struct TaskStartParameters { QThreadPool *threadPool = globalInstance(); ... }
//   第 47 行 class RunFunctionTaskBase : public QRunnable
//   第 55~70 行 start(parameters)：57 setThreadPool / 58 setRunnable / 59 reportStarted /
//              60 拷贝出 theFuture / 63 有池就投 / **64~68 没池就 cancel+finish+delete this**
//              ← ★★ 第【4】节的逐行依据。注意 59 行比 62 行的判空还早：
//                所以「空池」那条 future 是 **started 且 canceled 且 finished** 同时成立。
//   第 75~93 行 run()：77~80 **只在入口查一次取消**，然后 84 行进 runFunctor，
//              92 行**无条件** reportFinished()。
//              ← ★★ 这一条决定了第【3】节的全部结论：取消对「已经在 runFunctor 里」的
//                函数没有任何作用，因为没有任何一行代码在那个位置替你查标志。
// QtConcurrent/qtconcurrentrun.h
//   第 39~40 行 QTCONCURRENT_RUN_NODISCARD：要结果才用它，不要结果请用 pool->start
//   第 42~50 行 run(QThreadPool*, f, args...)   ← 池在前，**类型是 QThreadPool\***
//   第 61~67 行 run(f, args...)                 ← 没有池 = 用 globalInstance（65 行）
//   第 70~85 行 promise-hint 版 run<PromiseType>(...)
//              ← 本讲【5】实测：这一族要的是「返回 void、第一个参数是 QPromise<T>&」
//                （qtconcurrentstoredfunctioncall.h 25~32 行与 190~193 行两句 static_assert）
// QtConcurrent/qtconcurrentstoredfunctioncall.h
//   第 29~32 行 两句 static_assert：可调用 + **必须返回 void**
//   第 138~169 行 StoredFunctionCallWithPromise：167 行有个 QPromise<PromiseType> 成员
// QtCore/qatomic.h / qbasicatomic.h
//   第 37~40 行 loadRelaxed / loadAcquire / storeRelaxed / storeRelease
//   qbasicatomic.h 第 144~147 行：前置 ++ 返回新值，后置 ++ 返回旧值
// QtCore/qthread.h
//   第 68/69 行 requestInterruption() / isInterruptionRequested()  ← 管线程的
//   第 90 行 void terminate()                                    ← 本讲只用它做「存在性」对照
//   第 95~101 行 wait(QDeadlineTimer) / wait(unsigned long)（后者转发，ULONG_MAX ⇒ Forever）
//   第 104 行 static void msleep(unsigned long)
// QtCore/qdeadlinetimer.h
//   第 26 行 static constexpr ForeverConstant Forever
//   第 38~40 行 isForever() / hasExpired()
// QtCore/qcoreapplication.h
//   第 209 行 void aboutToQuit(QPrivateSignal);        ← ★ 第【8】节
//
// 与本讲相关的其它模块（跨讲引用，别在这里重复证明）：
//   · threads 第 3 讲：queued 连接怎么把一次 emit 变成对方线程队列里的一条事件。
//   · threads 第 4 讲：Hard / Soft / Obs 三档口径的来历。
//   · threads 第 6 讲：QThreadPool 的 start/tryTake/clear/waitForDone 细节；
//     本讲【7】接着它那句「池的析构到底等多久，证明不了」往下做。
//   · metaobject 第 4 讲：connect 的 context 参数（本讲【8】关闸时用它保命）。
// ============================================================================

// ============================================================================
// 二、口径与账本：三档结论，只有第一档进退出码
// ----------------------------------------------------------------------------
// 第 4 讲立的规矩，第 6 讲原样用过，这里再原样用一次：
//   Hard —— API 语义保证（含「我自己的代码里这两条分支穷尽了」这种保证）。
//           不对就是代码错，进账、影响退出码。
//   Soft —— 「应当」发生但取决于调度器。没命中不算失败，但必须打印。
//   Obs  —— 只报实测，不做承诺（毫秒数、续跑有没有被执行）。
// ★ 这一讲 Hard 特别多，原因不是我的代码更可靠，而是我把每个实验都设计成两种形状之一：
//   (a) 零调度 / 单线程 —— 结论与调度器无关，比如【4】【6】；
//   (b) 两条分支都覆盖 —— 不管实现走哪条，结论都一样，比如【3a】。
//   反过来，凡是落到「要么这样、要么那样，都合理」的地方，一律 Obs（【3c】【6B】）。
// ============================================================================

#include <QCoreApplication>
#include <QThread>            // qthread.h：requestInterruption(68) / isInterruptionRequested(69) / wait(95)
#include <QThreadPool>        // qthreadpool.h：waitForDone(0) 的轮询、clear(81)、contains(83)
#include <QPromise>           // qpromise.h：37~46 的析构、80 的 isCanceled
#include <QFuture>            // qfuture.h：65 cancel、104 waitForFinished（没有期限）
#include <QFutureWatcher>     // qfuturewatcher.h：52 finished 信号；119~167 是 Q_QDOC 段
#include <QDeadlineTimer>     // qdeadlinetimer.h：Forever(26) / isForever(38)
#include <QAtomicInt>
#include <QAtomicPointer>     // 【2】把 worker 线程指针带回来
#include <QElapsedTimer>
#include <QTimer>
#include <QDebug>
#include <QString>

#include <QtConcurrent/QtConcurrentRun>   // qtconcurrentrun.h：run 的两族重载

#include <memory>             // shared_ptr
#include <functional>         // std::function
#include <type_traits>        // 那一大串 static_assert
#include <utility>

enum Kind { Hard, Soft, Obs };

struct Ledger {
    int total = 0;        // 做过的硬核对次数
    int failed = 0;       // 其中没对上的次数
    int stalled = 0;      // 前提没建立起来、于是整节作废的节数
    bool watchdogFired = false;

    int exitCode() const
    {
        // 三项里任何一项命中都要留痕迹：不符、整节作废、账是空的。
        // 「total == 0 也算失败」是这个仓库的规矩 —— 什么都没测却说测过了，
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

// ★ 为什么不能把「实测值」直接当 bool 传给 verdict：
//   verdict 的第 3 个参数是「这一项过了」，而本讲一半以上的结论是「某个计数 == 期望值」，
//   期望值经常是 0。把 0 当 false 传进去 → 一个假红；把 5 当 true 传进去 → 一个假绿。
//   第 6 讲就是这么踩的（那一讲正文里写了）。所以判据只能在函数里算。
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

static void checkAtLeast(const QString &label, qint64 got, qint64 floor)
{
    verdict(Hard, label, got >= floor, QStringLiteral("%1（至少 %2）").arg(got).arg(floor));
}

static void checkAtMost(const QString &label, qint64 got, qint64 ceiling)
{
    verdict(Hard, label, got <= ceiling, QStringLiteral("%1（至多 %2）").arg(got).arg(ceiling));
}

// ★ 「前提」不是结论，但它是结论的地基。地基塌了，整节作废（stalled），
//   而不是硬拗出一个绿。一次 false 的代价是 2：一条 Hard 不符 + 一节作废。
//   这是故意的 —— 前提塌掉比结论错更值得你抬头看一眼。
static bool premise(const QString &label, bool ok, const QString &detail)
{
    verdict(Hard, QStringLiteral("前提：") + label, ok, detail);
    if (!ok) {
        ++gLedger.stalled;
        qWarning().noquote()
            << QStringLiteral("  ……… 本节前提没成立，余下的核对全部跳过（这一节记作作废）");
    }
    return ok;
}

static void stalled(const QString &why)
{
    ++gLedger.stalled;
    qWarning().noquote() << QStringLiteral("  [作废] ") + why;
}

// ============================================================================
// 三、缺席证明：把「没有暴力取消」写成编译期断言
// ----------------------------------------------------------------------------
// ★ 教你怎么证「一个成员函数不存在」。
//   嘴上说「QThreadPool 没有 cancel」是没用的 —— 读到的人只能信你。
//   编译期能证：用「这个调用表达式合不合法」当判据（std::void_t + decltype）。
//   合法 → 继承 std::true_type；不合法 → 走主模板 std::false_type。
//   一个模板参数都不用在运行期求值，所以它**跑一万次一万次都对**。
//
// ★ 这里我踩过一个坑，值得记下来：
//   Qt 在默认配置下把 `forever` 定义成宏（qforeach.h 第 79 行 → 第 71 行 `for(;;)`），
//   `foreach` 也是（第 75 行）。我给一个局部变量起名 forever，编译器报的是
//   「expected primary-expression before 'for'」—— 看起来像语法错，其实是宏展开。
//   所以本文件里那个变量叫 isForever。要躲开这一族宏，编译时加 -DQT_NO_KEYWORDS。
// ============================================================================
template <typename C, typename = void> struct HasCancel : std::false_type {};
template <typename C>
struct HasCancel<C, std::void_t<decltype(std::declval<C &>().cancel())>> : std::true_type {};

template <typename C, typename = void> struct HasTerminate : std::false_type {};
template <typename C>
struct HasTerminate<C, std::void_t<decltype(std::declval<C &>().terminate())>> : std::true_type {};

template <typename C, typename = void> struct HasEnumerateThreads : std::false_type {};
template <typename C>
struct HasEnumerateThreads<C, std::void_t<decltype(std::declval<C &>().threads())>> : std::true_type {};

template <typename C, typename = void> struct HasInterruptionRequest : std::false_type {};
template <typename C>
struct HasInterruptionRequest<C, std::void_t<decltype(std::declval<C &>().requestInterruption())>>
    : std::true_type {};

template <typename C, typename = void> struct HasFinishedSignal : std::false_type {};
template <typename C>
struct HasFinishedSignal<C, std::void_t<decltype(&C::finished)>> : std::true_type {};
// ★ 上一版我写的是 `declval<C&>().finished()`（无参调用），结果 QThread 那条断言红了。
//   原因：QThread::finished 的签名是 `void finished(QPrivateSignal)`（qthread.h 110 行），
//   它**不带**默认参数，所以「无参调用」这个探针根本够不着它。
//   探针的形状必须和被测对象一致 —— 换成「能不能取到成员地址」，信号/普通函数/槽一视同仁。
//   这是本讲第二次栽在「口径和主语不匹配」上（第一次是【2】那个 waitForValue）。
static_assert(HasFinishedSignal<QThread>::value, "QThread 有 finished 信号，且能取地址");

template <typename C, typename = void> struct HasWaitForDoneWithDeadline : std::false_type {};
template <typename C>
struct HasWaitForDoneWithDeadline<
    C, std::void_t<decltype(std::declval<C &>().waitForDone(QDeadlineTimer(1)))>> : std::true_type {};

template <typename C, typename = void> struct HasWaitForFinishedWithDeadline : std::false_type {};
template <typename C>
struct HasWaitForFinishedWithDeadline<
    C, std::void_t<decltype(std::declval<C &>().waitForFinished(QDeadlineTimer(1)))>> : std::true_type {};

// —— 这一组断言是「结论」，不是「注释」：编不过就是我说错了 ——
static_assert(!HasCancel<QThreadPool>::value,
              "QThreadPool 没有 cancel()：池里没有「撤下一条已经开跑的活」这个动作");
static_assert(!HasCancel<QRunnable>::value,
              "QRunnable 没有 cancel()：任务自己也没有被外部叫停的入口，只有 run()");
static_assert(HasCancel<QFuture<int>>::value,
              "取消在 QFuture 上（qfuture.h 65 行），不在池上、也不在任务上");
static_assert(!HasTerminate<QRunnable>::value, "QRunnable 连 terminate 都没有");
static_assert(HasTerminate<QThread>::value,
              "唯一能硬停的是 QThread::terminate（qthread.h 90 行），而它杀的是整条线程");
static_assert(!HasEnumerateThreads<QThreadPool>::value,
              "QThreadPool 不给你列工人线程的接口 —— 所以你没法对池里的线程 requestInterruption");
static_assert(HasInterruptionRequest<QThread>::value, "requestInterruption 在线程上");
static_assert(!HasInterruptionRequest<QRunnable>::value, "它不在任务上");
static_assert(!HasFinishedSignal<QThreadPool>::value,
              "池没有 finished 信号 —— 想「等池空了再说下一步」，只能自己轮询");
static_assert(HasFinishedSignal<QThread>::value, "线程有 finished 信号（qthread.h 110 行）");
static_assert(HasWaitForDoneWithDeadline<QThreadPool>::value,
              "池的等待可以带期限（qthreadpool.h 79 行）");
static_assert(!HasWaitForFinishedWithDeadline<QFuture<int>>::value,
              "★ QFuture::waitForFinished() 没有带期限的重载（qfuture.h 104 行）—— "
              "所以「等一个 future」在界面线程里是个不许做的动作");
static_assert(!HasWaitForFinishedWithDeadline<QFutureWatcherBase>::value,
              "watcher 的 waitForFinished() 同样没有期限（qfuturewatcher.h 44 行）—— "
              "它的正确用法是连信号（52 行），不是等");

static void phase0_absenceProof()
{
    section(QStringLiteral("【0】缺席证明：编译期就定下的 12 条，跑多少次都一样"));

    checkBool(QStringLiteral("池上没有 cancel()（头文件 32~85 行整段扫完也没有）"),
              HasCancel<QThreadPool>::value, false);
    checkBool(QStringLiteral("任务上没有 cancel()"), HasCancel<QRunnable>::value, false);
    checkBool(QStringLiteral("取消这个动词长在 QFuture 上（qfuture.h 65 行）"),
              HasCancel<QFuture<int>>::value, true);
    checkBool(QStringLiteral("唯一能硬停邻居的是 QThread::terminate（qthread.h 90 行）"),
              HasTerminate<QThread>::value, true);
    checkBool(QStringLiteral("池不给你列工人线程（所以「给池里的线程发中断」这条路不存在）"),
              HasEnumerateThreads<QThreadPool>::value, false);
    checkBool(QStringLiteral("池没有 finished 信号"), HasFinishedSignal<QThreadPool>::value, false);
    checkBool(QStringLiteral("线程有 finished 信号"), HasFinishedSignal<QThread>::value, true);
    checkBool(QStringLiteral("池的等待能带期限"), HasWaitForDoneWithDeadline<QThreadPool>::value, true);
    checkBool(QStringLiteral("future 的等待**不能**带期限（qfuture.h 104 行只有一个签名）"),
              HasWaitForFinishedWithDeadline<QFuture<int>>::value, false);
    checkBool(QStringLiteral("watcher 的等待也不能带期限"),
              HasWaitForFinishedWithDeadline<QFutureWatcherBase>::value, false);
    checkBool(QStringLiteral("requestInterruption 在线程上"),
              HasInterruptionRequest<QThread>::value, true);
    checkBool(QStringLiteral("requestInterruption 不在任务上"),
              HasInterruptionRequest<QRunnable>::value, false);

    qInfo().noquote() << QStringLiteral(
        "  这 12 条一条都不是「测出来的」—— 它们是编译器替我证的："
        "\n     一个调用表达式编得过 ⇒ 有这个成员；编不过 ⇒ 没有。运行期不参与。");
}

// ============================================================================
// 四、探针：所有计数都是原子的，而且每节各自清零基线
// ----------------------------------------------------------------------------
// ★ 第 6 讲留下的教训在这里继续用：**计数器的口径要和结论的主语一致**。
//   所以这里不是一个大 gRunCount 到处用，而是「谁做的块」「谁进的场」「谁被拒了」
//   各自一个计数器。看起来啰嗦，但一条结论配一个计数器，是这一讲还能立硬账的原因。
// ============================================================================
static QAtomicInt gJobCtor = 0;        // ChunkRunnable 构造
static QAtomicInt gJobDtor = 0;        // ChunkRunnable 析构（谁删的都算）
static QAtomicInt gEnter = 0;          // 任何任务 run() 进场
static QAtomicInt gChunks = 0;         // 【1】做过的块
static QAtomicInt gFirstChunk = 0;     // 【1】第一块已落地的信号
static QAtomicInt gStoppedAt = 0;      // 【1】取消发生在第几块；== total 表示跑完了

static QAtomicInt gSpinIters = 0;      // 【2】线程子类转了多少圈
static QAtomicInt gSpinExit = 0;       // 【2】1 = 因中断请求退出，0 = 别的路
static QAtomicInt gSpinEnter = 0;      // 【2】run() 进场
static QAtomicPointer<QThread> gSeenPoolThread;  // 【2】池任务看到的 currentThread()

static QAtomicInt gKeptRuns = 0;       // 【3a】对照组（没取消的那条）
static QAtomicInt gCancelRuns = 0;     // 【3a】被撤下的那条
static QAtomicInt gBlindChunks = 0;    // 【3b】一个不查取消的函数做了多少块

static QAtomicInt gNullPoolRuns = 0;   // 【4】空池那条的函数体

static QAtomicInt gPromiseChunks = 0;  // 【5】promise 版任务做了多少块
static QAtomicInt gPromiseResults = 0; // 【5】addResult 调用次数
static QAtomicInt gPromiseExit = 0;    // 【5】1 = 走的是取消退出那条路

static QAtomicInt gThenRuns = 0;       // 【6】then 的续跑调用次数
static QAtomicInt gOnCancelRuns = 0;   // 【6】onCanceled 的处理器调用次数

static QAtomicInt gSubmitted = 0;      // 【7】submit 受理了几次
static QAtomicInt gRefused = 0;        // 【7】停闸之后被拒了几次
static QAtomicInt gLongEnter = 0;      // 【7】长期任务进场
static QAtomicInt gLongExit = 0;       // 【7】长期任务离场（靠自己看标志）
static QAtomicInt gShortEnter = 0;     // 【7】被撤下的短任务**本体**进场次数（应为 0）
static QAtomicInt gStopRequests = 0;   // 【7】requestStop 被调用几次
static QAtomicInt gTimedOut = 0;       // 【7】超时几次（自己的收尾契约被破了）
static QAtomicInt gDrainMs = 0;        // 【7】从关闸到池空，毫秒（Obs）

static QAtomicInt gAboutHits = 0;      // 【8】aboutToQuit 触发次数
static QAtomicInt gAfterQuitRuns = 0;  // 【8】退出闸门之后还跑起来的任务数（应为 0）

// 有期限地等一个原子量变成 want。不许裸 while 死等 —— 本讲每一处等待都带预算。
static bool waitForValue(QAtomicInt &v, int want, int budgetMs)
{
    QElapsedTimer t;
    t.start();
    while (v.loadAcquire() != want) {
        if (t.elapsed() > budgetMs)
            return false;
        QThread::msleep(1);
    }
    return true;
}

// ★ 为什么还要一个「至少」版：【2】那个计数器是**一直往上涨**的，等它「正好等于 5」
//   是在赌没有第二个 tick 落在两次读之间 —— 一等一个准。等「不小于 5」才对得上
//   结论的主语（「它已经开始转了」），也才不会白等 800ms。这是第 6 讲那条
//   「计数器的口径要和结论的主语一致」的又一次应用：**「到了没有」和「到了正好多少」是两个问题。**
static bool waitAtLeast(QAtomicInt &v, int floor, int budgetMs)
{
    QElapsedTimer t;
    t.start();
    while (v.loadAcquire() < floor) {
        if (t.elapsed() > budgetMs)
            return false;
        QThread::msleep(1);
    }
    return true;
}

// ============================================================================
// 五、【1】协作式取消的最小正确形状：一个标志 + 循环体里的一句 if
// ----------------------------------------------------------------------------
// 先说结论：**「取消」在 Qt 里不是一条命令，是一次协商。**
//   池没有 cancel()（第【0】节已证），任务也没有被外部叫停的入口。
//   唯一存在的通道是：外面把某个标志置上，里面在检查点上看一眼，然后 return。
//
//   所以「你的任务可以被取消」这件事，真正的实现是**你自己写的那三行**：
//       if (m_flag->loadAcquire()) return;
//   没有这三行，外面做什么都没用 —— 第【1B】节就是拿掉这三行，让同一段循环
//   不理标志、照跑 40 块，用来量「检查点值多少钱」。
//
// ★ 检查点的密度 = 取消延迟的上限。
//   每块 2ms，那最坏 2ms 后才会让路；每块 40ms，就是 40ms 后才让路。
//   这不是精度问题，是**产品问题**：用户点「取消」多久之后界面才敢说「已取消」。
// ============================================================================
class ChunkRunnable : public QRunnable
{
public:
    ChunkRunnable(int total, int chunkMs, QAtomicInt *flag, bool checkFlag)
        : m_total(total), m_chunkMs(chunkMs), m_flag(flag), m_check(checkFlag)
    {
        ++gJobCtor;
    }
    ~ChunkRunnable() override { ++gJobDtor; }

    void run() override
    {
        ++gEnter;
        for (int i = 0; i < m_total; ++i) {
            // ★ 检查点。m_check 为 false 时就是【1B】那次「拿掉这三行」的实验。
            if (m_check && m_flag->loadAcquire()) {
                gStoppedAt.storeRelease(i);
                return;
            }
            ++gChunks;
            if (i == 0)
                gFirstChunk.storeRelease(1);   // 让主线程知道「真的开始做块了」
            if (m_chunkMs > 0)
                QThread::msleep(unsigned(m_chunkMs));
        }
        gStoppedAt.storeRelease(m_total);      // 跑完了，没被取消
    }

private:
    int m_total;
    int m_chunkMs;
    QAtomicInt *m_flag;
    bool m_check;
};

static void phase1_cooperativeCancel()
{
    section(QStringLiteral("【1】协作式取消：标志 + 检查点（拿掉检查点会怎样，这里也演一次）"));

    const int total = 40;
    const int chunkMs = 2;

    // ★ 声明顺序就是析构顺序的反序：flag 必须先于 pool 声明，才能晚于 pool 析构。
    //   池里那些任务握着 &flag；如果 flag 先死、池后死，池的析构去等一个正在读
    //   已销毁对象的任务 —— 那就是第 6 讲【8】那个「谁先死」的坑的另一种写法。
    //   本讲每一处提前 waitForDone 都是为了这个顺序能成立；顺序写在声明里，是最后一道保险。
    QAtomicInt flag = 0;

    QThreadPool pool;                       // 私有池：不动全局（第 5/6 讲立的规矩）
    pool.setMaxThreadCount(1);

    // ── 【1A】有检查点 ──────────────────────────────────────────────────
    gStoppedAt.storeRelease(-1);
    gFirstChunk.storeRelease(0);
    const int enter0 = gEnter.loadRelaxed();
    const int ctor0 = gJobCtor.loadRelaxed();
    const int dtor0 = gJobDtor.loadRelaxed();

    pool.start(new ChunkRunnable(total, chunkMs, &flag, true));

    const bool startedOk = waitForValue(gFirstChunk, 1, 800);
    if (!premise(QStringLiteral("【1A】置位之前，任务真的开始做块了"),
                 startedOk, QStringLiteral("第一块%s落地")
                                .arg(startedOk ? QStringLiteral("已") : QStringLiteral("没"))))
        return;

    const int chunksAtCancel = gChunks.loadRelaxed();
    const bool roomLeft = chunksAtCancel < total - 1;
    if (!premise(QStringLiteral("【1A】置位时还剩不止一块没做（否则「提前退出」无从谈起）"),
                 roomLeft, QStringLiteral("置位时已完成 %1 块 / 共 %2 块")
                               .arg(chunksAtCancel).arg(total)))
        return;

    QElapsedTimer clock;
    clock.start();
    flag.storeRelease(1);                                 // ★← 这就是「取消」的全部动作
    // ★ 计数器必须在**置位之后**读，这一行决定了下面那条 checkAtMost 能不能算硬账。
    //   如果在置位前读，主线程在「读」和「置位」之间被操作系统调度走 2ms，worker 就能
    //   多做完一块 —— 那条「至多 1 块」就成了概率。置位之后读，逻辑就封死了：
    //   之后任何一次 ++gChunks 只能来自「已经通过检查点的那一轮」，一轮之后再查必然退出。
    const int chunksAtStore = gChunks.loadRelaxed();
    const bool done = pool.waitForDone(QDeadlineTimer(2000));
    const qint64 cancelToExit = clock.elapsed();

    checkBool(QStringLiteral("【1A】waitForDone(2000) 收到 true（置位后任务在一两块内退出）"),
              done, true);
    checkNum(QStringLiteral("【1A】run() 进场次数"), gEnter.loadRelaxed() - enter0, 1);
    checkBool(QStringLiteral("【1A】走的是取消分支（gStoppedAt < total）"),
              gStoppedAt.loadRelaxed() < total, true);
    checkAtMost(QStringLiteral("【1A】置位之后又落地的块数（最多是在飞的那一块）"),
                (gChunks.loadRelaxed() - chunksAtStore), 1);
    checkAtLeast(QStringLiteral("【1A】置位前确实做了块（块数不会倒退）"),
                 chunksAtCancel, 1);
    checkNum(QStringLiteral("【1A】构造 1 次"), gJobCtor.loadRelaxed() - ctor0, 1);
    checkNum(QStringLiteral("【1A】析构 1 次（autoDelete 默认 true，池替我删）"),
             gJobDtor.loadRelaxed() - dtor0, 1);
    verdict(Obs, QStringLiteral("【1A】从置位到池空"),
            true, QStringLiteral("%1 ms").arg(cancelToExit));

    // ── 【1B】同一段循环，拿掉检查点 ───────────────────────────────────
    flag.storeRelease(0);
    gStoppedAt.storeRelease(-1);
    gFirstChunk.storeRelease(0);
    const int chunksB0 = gChunks.loadRelaxed();

    pool.start(new ChunkRunnable(total, chunkMs, &flag, false));
    const bool startedB = waitForValue(gFirstChunk, 1, 800);
    if (!premise(QStringLiteral("【1B】置位之前，任务真的开始做块了"), startedB,
                 QStringLiteral("第一块%s落地")
                     .arg(startedB ? QStringLiteral("已") : QStringLiteral("没"))))
        return;

    QElapsedTimer clockB;
    clockB.start();
    flag.storeRelease(1);                    // 同一个标志、同样的置位动作
    const bool doneB = pool.waitForDone(QDeadlineTimer(2000));
    const qint64 fullRunMs = clockB.elapsed();

    checkBool(QStringLiteral("【1B】waitForDone(2000) 收到 true（它总会结束，只是不看标志）"),
              doneB, true);
    checkNum(QStringLiteral("【1B】块数：一块都没少（没人查标志 ⇒ 取消对它零效果）"),
             gChunks.loadRelaxed() - chunksB0, total);
    checkBool(QStringLiteral("【1B】它走的是自然完成那条路（gStoppedAt == total）"),
              gStoppedAt.loadRelaxed() == total, true);
    verdict(Obs, QStringLiteral("【1B】置位之后它又干了多久"),
            true, QStringLiteral("%1 ms —— 这就是「没有检查点」的价格").arg(fullRunMs));

    verdict(Obs, QStringLiteral("【1】两投的差别只在循环体里那三行"),
            true, QStringLiteral("① 有检查点：第 %1 块就停了；② 没检查点：%2 块全做完")
                      .arg(chunksAtCancel).arg(total));
}

// ============================================================================
// 六、【2】requestInterruption：它管的是线程，不是任务
// ----------------------------------------------------------------------------
// QThread::requestInterruption()（68 行）其实就是一个布尔标志，
// isInterruptionRequested()（69 行）就是读它。**它自己不杀线程、不排队、不发信号。**
// 你要它有用，就得有一条线程在自己的循环里反复读它 —— 和【1】一模一样。
//
//   那它和【1】那个自备标志差在哪？差在**归属**：
//     · 线程的标志归 QThread 对象，谁拿着这个对象谁能置（外部可发起）；
//     · 任务的自备标志归你，得你自己把指针传进去。
//
// ★ 所以这一节真正的结论是负面的：**这套机制在线程池里用不上。**
//   池不给你工人线程的指针（第【0】节已证 HasEnumerateThreads == false），
//   你没法对它们 requestInterruption。池里的活只能用【1】那种「任务持有的标志」。
//
// ★ 这一节我故意用 QThread 子类重写 run()，不挂事件循环。
//   因为我要证的是「中断标志本身管用」，把「事件循环怎么排队」这个变量拿掉 ——
//   那个变量在第 2/3 讲已经证过了。一次只动一个变量，是实验设计的基本功。
// ============================================================================
class InterruptibleThread : public QThread
{
public:
    void run() override
    {
        ++gSpinEnter;
        while (!isInterruptionRequested()) {          // ★ 检查点，读的是自己的标志
            ++gSpinIters;
            msleep(1);
            if (gSpinIters.loadRelaxed() > 20000)     // 保险丝：谁也不许把片子跑死
                break;
        }
        gSpinExit.storeRelease(isInterruptionRequested() ? 1 : 0);
    }
};

static void phase2_interruptionIsPerThread()
{
    section(QStringLiteral("【2】requestInterruption：它管线程，不管任务（池里用不上）"));

    // ── 【2A】在线程自己身上，它是好使的 ──────────────────────────────
    const int enter0 = gSpinEnter.loadRelaxed();
    gSpinIters.storeRelease(0);
    gSpinExit.storeRelease(-1);

    InterruptibleThread th;
    th.start();

    const bool spinning = waitAtLeast(gSpinIters, 5, 800);
    if (!premise(QStringLiteral("【2A】线程已经在转了（否则「请求中断」无从谈起）"),
                 spinning, QStringLiteral("已转 %1 圈").arg(gSpinIters.loadRelaxed()))) {
        // ★ 前提没成立也必须先把线程停掉：QThread 在「线程还在跑」时被析构，
        //   Qt 会直接 abort 整个进程（它没有安全的方式替你等）。
        //   「这一节作废」是一条账；「进程被杀」是一条尸检报告 —— 两者差着一个量级。
        th.requestInterruption();
        th.wait(QDeadlineTimer(1000));
        return;
    }

    QElapsedTimer clock;
    clock.start();
    th.requestInterruption();                 // ★← 全部动作就这一句
    const bool stopped = th.wait(QDeadlineTimer(1000));
    const qint64 exitMs = clock.elapsed();

    checkBool(QStringLiteral("【2A】请求中断后线程在期限内结束"), stopped, true);
    checkNum(QStringLiteral("【2A】run() 进场次数"), gSpinEnter.loadRelaxed() - enter0, 1);
    checkBool(QStringLiteral("【2A】退出的原因是中断请求（不是撞了保险丝）"),
              gSpinExit.loadRelaxed() == 1, true);
    checkBool(QStringLiteral("【2A】保险丝没被触发（转的圈数远小于 20000）"),
              gSpinIters.loadRelaxed() < 20000, true);
    verdict(Obs, QStringLiteral("【2A】从请求到线程结束"),
            true, QStringLiteral("%1 ms（检查点间隔 1ms，所以这个数应当很小）").arg(exitMs));

    // ── 【2B】池里的线程：能认出，不能列出 ────────────────────────────
    QThreadPool pool;
    pool.setMaxThreadCount(1);
    pool.start([]() { gSeenPoolThread.storeRelease(QThread::currentThread()); });
    const bool done = pool.waitForDone(QDeadlineTimer(2000));
    QThread *seen = gSeenPoolThread.loadAcquire();

    if (!premise(QStringLiteral("【2B】池任务跑完了，而且它报回了自己所在的线程"), done && seen,
                 QStringLiteral("waitForDone=%1，线程指针=%2")
                     .arg(done ? QStringLiteral("true") : QStringLiteral("false"),
                          seen ? QStringLiteral("有") : QStringLiteral("空"))))
        return;

    checkBool(QStringLiteral("【2B】池能认出「这条线程是我的工人」（contains，83 行）"),
              pool.contains(seen), true);
    checkBool(QStringLiteral("【2B】池认不出「主线程是我的工人」"),
              pool.contains(QThread::currentThread()), false);
    checkBool(QStringLiteral("【2B】可是它拿不出一个列表 —— 所以你对这些线程发不了中断"),
              HasEnumerateThreads<QThreadPool>::value, false);
    verdict(Obs, QStringLiteral("【2B】那个工人线程此刻的中断标志"),
            true, QStringLiteral("%1（没人置它；我也不会去置一个不属于我的线程）")
                      .arg(seen->isInterruptionRequested() ? QStringLiteral("true")
                                                           : QStringLiteral("false")));

    // ★ 上面这一行是我这一讲的「别做」：seen 是池的线程对象，池随时可以回收它
    //   （expiryTimeout，默认 30000ms）。我只是在池还活着、而且刚刚跑完的时刻读了一眼，
    //   绝不把它存起来、更不对它 requestInterruption。
    //   把指针存起来跨池的生命周期用，是第 6 讲【8】那个「接收者先死」的另一种写法。
}

// ============================================================================
// 七、【3】QFuture::cancel()：三种局面，只有两种能立硬账
// ----------------------------------------------------------------------------
// 局面 A：任务还在队列里（没被 worker 取走）
//   两条实现路径都通向同一个结果，所以这一条是硬账：
//     · 若实现走「从队列里 tryTake 回来」—— qfutureinterface.h 92/93 行给了它工具；
//     · 若实现走「留在队列里、等 worker 取到它」—— qtconcurrentrunbase.h 77~80 行
//       在 runFunctor 之前就查了 isCanceled()，直接 reportFinished 返回。
//   两条都不调用你的函数体。**这叫「两条分支都覆盖」的证明法**：我不猜实现，
//   我只证明「不管它走哪条，我的结论都成立」。
//
// 局面 B：任务正在跑
//   77~80 行那句检查**只在入口执行一次**，之后没有任何一行代码替你查标志。
//   所以「取消对已经在跑的函数无效」不是玄学，是那 4 行代码的直接推论：
//   你不在自己的循环里查 isCanceled()，就没有人替你查。
//
// 局面 C：任务已经跑完
//   我不知道 cancel() 会不会早退 —— 它的函数体在 qfutureinterface.cpp 里，
//   本机只有头文件。所以整条标 Obs，并且把两种可能都写下来。
//   **一个负责任的材料，宁可写「这我量不出来」，也不写「一定是这样」。**
// ============================================================================
static void phase3_futureCancel()
{
    section(QStringLiteral("【3】QFuture::cancel()：还在队列 / 正在跑 / 已经跑完"));

    QThreadPool pool;
    pool.setMaxThreadCount(1);                // ★ 只要一个工人，队列才排得起来

    // ── 【3A】还在队列里 ─────────────────────────────────────────────
    QElapsedTimer holdClock;
    holdClock.start();
    QFuture<void> hold = QtConcurrent::run(&pool, []() { QThread::msleep(200); });

    const int kept0 = gKeptRuns.loadRelaxed();
    const int cancel0 = gCancelRuns.loadRelaxed();
    QFuture<int> kept = QtConcurrent::run(&pool, []() { ++gKeptRuns; return 1; });
    QFuture<int> doomed = QtConcurrent::run(&pool, []() { ++gCancelRuns; return 2; });
    const qint64 msSinceHold = holdClock.elapsed();

    if (!premise(QStringLiteral("【3A】撤消的那一刻，两条都还在队列里（工人还被 hold 占着）"),
                 msSinceHold < 190,
                 QStringLiteral("从投出 hold 到现在 %1 ms（它的 msleep 是 200ms）").arg(msSinceHold)))
        return;

    doomed.cancel();
    const bool canceledNow = doomed.isCanceled();   // 不等 worker，当场就能读

    checkBool(QStringLiteral("【3A】cancel() 之后 isCanceled() 立刻为 true（不用等工人）"),
              canceledNow, true);

    const bool drained = pool.waitForDone(QDeadlineTimer(3000));
    checkBool(QStringLiteral("【3A】waitForDone(3000) 把队列收干净"), drained, true);
    checkNum(QStringLiteral("【3A】对照组（没撤的那条）跑了 1 次 —— 池和队列都是好的"),
             gKeptRuns.loadRelaxed() - kept0, 1);
    checkNum(QStringLiteral("【3A】被撤下的那条：函数体一次都没进"),
             gCancelRuns.loadRelaxed() - cancel0, 0);
    // ★ 这一条我标 Obs 而不是 Hard：若实现走「tryTake 撤回」，它必须顺手把 future
    //   finish 掉（不然等它的人永远等），但那一步在 qfutureinterface.cpp 里，我看不到。
    //   两条路径我都在注释里写了，可「它一定 finished」这一步只有一条路径给得出来 ——
    //   所以只报实测。这是本讲「能被源码撑住的才写硬账」那条规矩的又一次执行。
    verdict(Obs, QStringLiteral("【3A】被撤下的那条 isFinished()"),
            true, doomed.isFinished() ? QStringLiteral("true") : QStringLiteral("false"));
    verdict(Obs, QStringLiteral("【3A】kept 的结果"), true,
            QStringLiteral("%1").arg(kept.result()));
    hold.waitForFinished();

    // ── 【3B】正在跑：取消改的是状态位，停不下函数 ────────────────────
    const int blind0 = gBlindChunks.loadRelaxed();
    const int busyTotal = 25;
    QFuture<int> busy = QtConcurrent::run(&pool, [busyTotal]() {
        for (int i = 0; i < busyTotal; ++i) {     // ★ 循环体里故意一次都不查取消
            ++gBlindChunks;
            QThread::msleep(4);
        }
        return busyTotal;
    });

    QElapsedTimer spinClock;
    spinClock.start();
    while (gBlindChunks.loadRelaxed() - blind0 < 3 && spinClock.elapsed() < 1500)
        QThread::msleep(1);
    const int atCancel = gBlindChunks.loadRelaxed();
    if (!premise(QStringLiteral("【3B】取消时它才做了不到一半"),
                 atCancel - blind0 >= 3 && atCancel - blind0 < busyTotal,
                 QStringLiteral("取消时已完成 %1 块 / 共 %2 块")
                     .arg(atCancel - blind0).arg(busyTotal)))
        return;

    busy.cancel();
    const bool busyCanceled = busy.isCanceled();
    const bool busyDrained = pool.waitForDone(QDeadlineTimer(3000));
    const int after = gBlindChunks.loadRelaxed();

    checkBool(QStringLiteral("【3B】isCanceled() 也是立刻 true"), busyCanceled, true);
    checkBool(QStringLiteral("【3B】waitForDone 收干净"), busyDrained, true);
    // ★ 这里只留一条真正有内容的硬账：**总块数一块不少**。
    //   我第一版写的是「取消之后又做了 (total - 已做) 块」，写完就发现它是个恒等式：
    //   两边用的都是同一个 atCancel，代数上自动相等，等于什么都没证。
    //   「一个恒真的断言」比「一个错的断言」更危险 —— 它会在账本里冒充一项硬账。
    checkNum(QStringLiteral("【3B】总块数一块不少（入口那次检查之后再没人查过取消）"),
             after - blind0, busyTotal);
    verdict(Obs, QStringLiteral("【3B】取消那一刻已经做了多少块"),
            true, QStringLiteral("%1 / %2（剩 %3 块照做）")
                      .arg(atCancel - blind0).arg(busyTotal)
                      .arg(busyTotal - (atCancel - blind0)));

    qInfo().noquote() << QStringLiteral(
        "  → 「取消」在这里只做了一件事：把一个状态位翻成 Canceled。"
        "\n     函数体没有看这个位，所以它做完最后一个块才走。"
        "\n     这就是必须在循环体里写检查点的全部理由。");

    // ── 【3C】已经跑完：只报实测 ─────────────────────────────────────
    busy.cancel();                              // 再来一次，此时它已经 finished
    verdict(Obs, QStringLiteral("【3C】对已完成的 future 再 cancel 之后，isCanceled()"),
            true, busy.isCanceled() ? QStringLiteral("true") : QStringLiteral("false"));
    verdict(Obs, QStringLiteral("【3C】对已完成的 future 再 cancel 之后，resultCount()"),
            true, QString::number(busy.resultCount()));
    qInfo().noquote() << QStringLiteral(
        "  ↑ 这两条我不立硬账：cancel() 的函数体在 qfutureinterface.cpp 里，本机只有头文件。"
        "\n     「finished 之后早退」和「照样把位置上」两种实现都说得通 ——"
        "\n     所以这里只报这台机器这一次的读数，不替 Qt 承诺。");
}

// ============================================================================
// 八、【4】零调度的取消：QtConcurrent::run(空池, f)
// ----------------------------------------------------------------------------
// 这是本讲唯一一节「一行线程都不起」的取消实验，所以它最适合当语义的对照组。
//
// 逐行看 qtconcurrentrunbase.h 55~70 行会发生什么（这四段是头文件里的真实源码）：
//   57 行  promise.setThreadPool(parameters.threadPool);   ← 池指针（这里是 nullptr）
//   58 行  promise.setRunnable(this);                      ← 任务指针（存档，为了能撤回）
//   59 行  promise.reportStarted();                        ← ★ 先报「已启动」
//   60 行  QFuture<T> theFuture = promise.future();        ← 先把 future 抄一份出来
//   62 行  if (parameters.threadPool)  …                   ← 空指针 → 走 else
//   65 行      promise.reportCanceled();
//   66 行      promise.reportFinished();
//   67 行      delete this;                                ← 任务对象就地注销
//   69 行  return theFuture;                               ← 返回的是**已经终态**的 future
//
// ★ 三个值得记住的推论：
//   ① 59 行在 62 行的判空之前 —— 所以这条 future 同时是 started、canceled、finished。
//      「已启动」不是「工人开始跑了」的意思，它只是「有人认领了这次投递」。
//   ② 函数体一次都没被调用过（run() 根本没发生）—— 这条硬账是 67 行的直接后果。
//   ③ 67 行把任务对象删了，而 69 行返回的 future 还好用 —— 说明**状态不跟着任务死**。
//      QFuture 自己拿着引用计数（qfutureinterface.h 154 行那个 operator== 比的是 d 指针，
//      就是同一个共享体的证据）。这一条是后面【6】和【7】能成立的地基。
//
// ★ 顺带一个实测出来的编译期事实（我第一版写错了，记在这里）：
//   `QtConcurrent::run(nullptr, f)` **编不过**。因为 qtconcurrentrun.h 61~67 行那族
//   `run(Function&&, Args&&...)` 会把 `nullptr` 当成「要执行的函数」，去解它的 operator()。
//   必须先给它一个**类型是 QThreadPool\*** 的变量，或者 static_cast。
//   编译器当时的原话是：'operator()' is not a member of 'std::decay<std::nullptr_t>::type'。
// ============================================================================
static void phase4_nullPoolCancel()
{
    section(QStringLiteral("【4】零调度的取消：run(空池, f)（逐行对照 62~68 行）"));

    QThreadPool *noPool = nullptr;            // ★ 类型必须是 QThreadPool*，不能写裸 nullptr
    const int runsBefore = gNullPoolRuns.loadRelaxed();

    QFuture<int> f = QtConcurrent::run(noPool, []() { ++gNullPoolRuns; return 99; });

    checkNum(QStringLiteral("【4】函数体进场次数（run() 从没发生过，67 行就 delete 了）"),
             gNullPoolRuns.loadRelaxed() - runsBefore, 0);
    checkBool(QStringLiteral("【4】isStarted()：59 行在判空之前就报了"), f.isStarted(), true);
    checkBool(QStringLiteral("【4】isCanceled()：65 行报的"), f.isCanceled(), true);
    checkBool(QStringLiteral("【4】isFinished()：66 行报的"), f.isFinished(), true);
    checkNum(QStringLiteral("【4】resultCount()（函数没跑，一个结果都没有）"),
             f.resultCount(), 0);
    checkBool(QStringLiteral("【4】状态活过了任务对象：67 行 delete this 之后，这三行读的还是对的"),
              f.isFinished() && f.isCanceled() && f.isStarted(), true);

    verdict(Obs, QStringLiteral("【4】isRunning()"),
            true, f.isRunning() ? QStringLiteral("true") : QStringLiteral("false"));
    qInfo().noquote() << QStringLiteral(
        "  ↑ isRunning 这条我标 Obs：它的判据在 .cpp 里。可能是"
        "\n     「Started && !Finished」（那这里就是 false），也可能是「有工人在跑」。"
        "\n     前三条我有 59/65/66 三行源码撑着，这一条没有。");
}

// ============================================================================
// 九、【5】promise-hint 那族 run<T>：返回值必须是 void，结果走 addResult
// ----------------------------------------------------------------------------
// Qt 6 给「要自己上报进度、自己查取消」的任务准备了一族重载：
//   QtConcurrent::run<PromiseType>(pool, f, args...)     （qtconcurrentrun.h 70~85 行）
// 它对 f 的要求写在两句 static_assert 里（qtconcurrentstoredfunctioncall.h 25~32 行）：
//   ① 第一个参数得是 QPromise<PromiseType>&（否则 190~193 行那句会响）；
//   ② **返回值必须是 void**（31~32 行："The function must return void type."）。
//
// ★ 我第一版写成了「返回 int」，编译器当场把第 ② 条顶到我脸上。原话：
//     static assertion failed: The function must return void type.
//   这一族的答案不走返回值，走 promise.addResult()。
//   把这件事和「普通 run 用返回值当结果」放在一起看，你就明白为什么很多人
//   在这两族之间来回撞墙：它们的**结果通道**根本不是同一个。
//
// ★ promise 还带来一个额外的好处：p.isCanceled()（qpromise.h 80 行）是任务
//   「自查取消」的唯一合法入口，不用你自己传一个标志进去。而 p.setProgressValue()
//   （83~88 行）是任务把进度报出去的正路 —— 主线程既能看见进度，又不用伸手进去。
// ============================================================================
static void phase5_promiseCancel()
{
    section(QStringLiteral("【5】run<T> 那族：返回 void、结果走 addResult、任务自查 isCanceled"));

    const int total = 40;
    QThreadPool pool;
    pool.setMaxThreadCount(1);

    auto body = [](QPromise<int> &p, int n) {          // ← 返回 void，是这一族的硬要求
        // ★ 进度先划范围，再报值。默认进度区间是 0..0，不划范围就 setProgressValue(i+1)，
        //   值会被夹在 0 上 —— 主线程看到 progressValue() 永远是 0。
        //   这不是 Qt 的怪癖：进度本来就是一个「在某个区间里的位置」，没有区间就没有位置。
        p.setProgressRange(0, n);
        for (int i = 0; i < n; ++i) {
            if (p.isCanceled()) {                      // ★ 任务自查（qpromise.h 80 行）
                gPromiseExit.storeRelease(1);
                return;
            }
            ++gPromiseChunks;
            p.setProgressValue(i + 1);                 // 进度走合法通道，不共享裸变量
            QThread::msleep(2);
        }
        p.addResult(n);                                // 结果走 addResult，不走返回值
        ++gPromiseResults;
    };

    // ★ 这条断言是【5】的地基。把 body 的返回类型改成 int，这一句当场编不过 ——
    //   那正是我第一版踩的那一脚（qtconcurrentstoredfunctioncall.h 31~32 行的原话：
    //   "The function must return void type."）。放在这里而不是写在注释里，
    //   是为了让「改错了」变成一个编译错误，而不是一句需要人记得的约定。
    static_assert(std::is_void_v<std::invoke_result_t<decltype(body), QPromise<int> &, int>>,
                  "run<T> 这一族要求函数返回 void（qtconcurrentstoredfunctioncall.h 31~32 行）");

    // ── 对照组：不取消，它应当正常产出一个结果 ────────────────────────
    const int chunksCtrl0 = gPromiseChunks.loadRelaxed();
    const int resultsCtrl0 = gPromiseResults.loadRelaxed();
    QFuture<int> ctrl = QtConcurrent::run<int>(&pool, body, total);
    const bool ctrlDone = pool.waitForDone(QDeadlineTimer(4000));

    checkBool(QStringLiteral("【5】对照组收干净"), ctrlDone, true);
    checkNum(QStringLiteral("【5】对照组块数"), gPromiseChunks.loadRelaxed() - chunksCtrl0, total);
    checkNum(QStringLiteral("【5】对照组产出 1 个结果（走 addResult）"),
             gPromiseResults.loadRelaxed() - resultsCtrl0, 1);
    checkNum(QStringLiteral("【5】对照组结果个数"), ctrl.resultCount(), 1);
    if (ctrl.isFinished() && ctrl.resultCount() == 1)
        verdict(Obs, QStringLiteral("【5】对照组结果值"), true, QString::number(ctrl.result()));
    checkBool(QStringLiteral("【5】对照组没被取消"), ctrl.isCanceled(), false);

    // ── 实验组：跑到一半取消 ──────────────────────────────────────────
    gPromiseExit.storeRelease(-1);
    const int chunks0 = gPromiseChunks.loadRelaxed();
    const int results0 = gPromiseResults.loadRelaxed();

    QFuture<int> f = QtConcurrent::run<int>(&pool, body, total);

    QElapsedTimer clock;
    clock.start();
    while (f.progressValue() < 3 && clock.elapsed() < 1500)
        QThread::msleep(1);
    const int progressAtCancel = f.progressValue();
    if (!premise(QStringLiteral("【5】取消时它已经推进了几块（进度通道可见）"),
                 progressAtCancel >= 3 && progressAtCancel < total,
                 QStringLiteral("progressValue = %1 / %2").arg(progressAtCancel).arg(total)))
        return;

    f.cancel();
    const bool finished = pool.waitForDone(QDeadlineTimer(4000));

    checkBool(QStringLiteral("【5】waitForDone 收到 true"), finished, true);
    checkBool(QStringLiteral("【5】任务走的是 promise 自查那条出口"),
              gPromiseExit.loadRelaxed() == 1, true);
    checkAtMost(QStringLiteral("【5】取消之后它没有把 40 块做完（差值 ≤ 39）"),
                gPromiseChunks.loadRelaxed() - chunks0, total - 1);
    checkNum(QStringLiteral("【5】产出结果个数（取消那条路 return 了，不走 addResult）"),
             gPromiseResults.loadRelaxed() - results0, 0);
    checkNum(QStringLiteral("【5】f.resultCount()"), f.resultCount(), 0);
    checkBool(QStringLiteral("【5】f.isCanceled()"), f.isCanceled(), true);
    checkBool(QStringLiteral("【5】f.isFinished()（92 行无条件 reportFinished）"),
              f.isFinished(), true);
    verdict(Obs, QStringLiteral("【5】取消时 progressValue"),
            true, QString::number(progressAtCancel));
}

// ============================================================================
// 十、【6】忘记 finish 的 QPromise：析构替你收尾
// ----------------------------------------------------------------------------
// qpromise.h 41~44 行是本讲最值钱的四行代码：
//     if (d.d && !(d.loadState() & QFutureInterfaceBase::State::Finished)) {
//         d.cancelAndFinish();   // 取消
//         d.runContinuation();   // 并且把下游叫醒
//     }
// 它意味着：**「谁负责 finish」这件事不靠约定，靠语言规则。**
// 你 new 一个 QPromise、忘了 finish、甚至半路 return —— 只要它离开作用域，
// 上游一定落到「已取消 + 已结束」，下游也不会永远挂在 Pending 上。
//
// ★ 为什么这条能立硬账：它是**头文件里看得见的函数体**，不是文档措辞；
//   而且整节单线程、零调度 —— 没有第二个人能插进来改变时序。
//
// ★ 同时要记住它的代价：**下游拿到的是「取消」，不是「正常完成」。**
//   如果你的界面代码把「取消」和「没有数据」当成同一件事，它就会安静地显示一个空列表，
//   而不是报错。第【6】节最后那句 onCanceled 的签名约束，就是让你把这两种情况分开。
// ============================================================================
static void phase6_promiseDestructor()
{
    section(QStringLiteral("【6】忘记 finish：析构函数替你 cancelAndFinish（qpromise.h 37~46 行）"));

    // ── 【6A】全程单线程：连一个工人都不起 ────────────────────────────
    QFuture<int> upstream;
    QFuture<int> downstream;
    const int then0 = gThenRuns.loadRelaxed();
    {
        QPromise<int> p;                       // 故意：既不 addResult，也不 finish
        upstream = p.future();
        downstream = upstream.then(QtFuture::Launch::Sync,
                                   [](int v) { ++gThenRuns; return v + 100; });
        // 离开这个作用域 → ~QPromise() → 41~44 行那四句
    }

    checkBool(QStringLiteral("【6A】上游被取消（42 行 cancelAndFinish 的直接后果）"),
              upstream.isCanceled(), true);
    checkBool(QStringLiteral("【6A】上游已结束 —— 不是「只置了取消位却还挂着」"),
              upstream.isFinished(), true);
    checkNum(QStringLiteral("【6A】上游 resultCount（从来没有 addResult 过）"),
             upstream.resultCount(), 0);
    checkBool(QStringLiteral("【6A】下游不是 Pending：runContinuation（43 行）把它推到了终态"),
              downstream.isFinished(), true);

    verdict(Obs, QStringLiteral("【6A】Sync 续跑被调用了几次"),
            true, QString::number(gThenRuns.loadRelaxed() - then0));
    verdict(Obs, QStringLiteral("【6A】下游 isCanceled()"),
            true, downstream.isCanceled() ? QStringLiteral("true") : QStringLiteral("false"));
    qInfo().noquote() << QStringLiteral(
        "  ↑ 续跑到底跑不跑、下游到底是不是 canceled，我标 Obs。"
        "\n     因为「父 future 被取消时 then 的处理器要不要执行」这个策略在 .cpp 里，"
        "\n     本机头文件只给到 346~349 行「Sync 在原线程上跑」这一层。"
        "\n     凡是两种实现都说得通的地方，我不替 Qt 承诺。");

    // ── 【6B】onCanceled 的处理器必须返回 T ──────────────────────────
    // 154 行：std::enable_if_t<std::is_invocable_r_v<T, Function>>。
    // 意思是「处理器返回的东西要能当 T 用」—— 因为整条链的类型不变。
    auto returnsInt = []() { return -1; };
    auto returnsVoid = []() {};
    static_assert(std::is_invocable_r_v<int, decltype(returnsInt)>,
                  "onCanceled 的处理器必须能产出 T");
    static_assert(!std::is_invocable_r_v<int, decltype(returnsVoid)>,
                  "返回 void 的处理器过不了 154 行那道 enable_if");

    checkBool(QStringLiteral("【6B】返回 int 的处理器满足 onCanceled 的约束"),
              std::is_invocable_r_v<int, decltype(returnsInt)>, true);
    checkBool(QStringLiteral("【6B】返回 void 的处理器不满足（想「只是记一下」得用 then）"),
              std::is_invocable_r_v<int, decltype(returnsVoid)>, false);

    const int onCancel0 = gOnCancelRuns.loadRelaxed();
    // ★ 处理器不捕获任何局部变量：它可能在**很久以后**、在别的线程上被调用。
    //   捕获一个栈变量的引用，就是给自己埋一个「回调时对象已经没了」的坑
    //   —— 这和 metaobject 第 4 讲那个 context 参数要解决的是同一个问题。
    QFuture<int> handled = upstream.onCanceled([]() {
        ++gOnCancelRuns;
        return -1;
    });
    checkBool(QStringLiteral("【6B】onCanceled 的返回值是一条新 future（链的类型不变）"),
              handled.isValid(), true);
    verdict(Obs, QStringLiteral("【6B】上游早已是终态，此时才挂 onCanceled 有没有被回调"),
            true, QString::number(gOnCancelRuns.loadRelaxed() - onCancel0));
    qInfo().noquote() << QStringLiteral(
        "  ↑ 这条 Obs 顺带告诉你一条实践规则：**要在取消发生之前就把处理器挂上。**"
        "\n     挂在终态之后，回不回调取决于实现（本机不承诺）。");

    // ── 【6C】QFuture 没有 cancelChain 之外的「查链」入口 ──────────────
    static_assert(HasCancel<QFuture<int>>::value, "cancel 在 QFuture 上");
    // isChainCanceled() 在 QFutureInterfaceBase 上（163 行），QFuture 没有转发它 ——
    // 所以普通使用者查不到「链上游是不是被取消了」，只能靠自己记。
    QFuture<int> chainProbe = upstream;
    chainProbe.cancelChain();
    checkBool(QStringLiteral("【6C】cancelChain() 是 QFuture 上的公开动作（67 行）"),
              chainProbe.isCanceled(), true);
}

// ============================================================================
// 十一、【7】收尾状态机：在事件循环线程里停下一切
// ----------------------------------------------------------------------------
// 问题：你在界面线程里想「把活停掉再退出」，但
//   · 池没有 finished 信号（第【0】节已证）→ 没法 await；
//   · waitForDone() 不带参数就是 Forever（qthreadpool.h 79 行）→ 界面一睡，鼠标点不动；
//   · 池的析构按文档会等在跑的活——但「等多久」你不该在主线程上赌。
//
// 正确形状：把等待拆成一串**有期限的轮询**，交给事件循环去做。
//   池里有没有空，一次就能问出来：`waitForDone(0)` —— 期限是 0，问一句立刻回来。
//   于是整个收尾就变成：关闸 → 撤队列 → 置标志 → 每 20ms 问一句 → 空了就通知。
//
// ★ 闸门要装两道，这是本节的第二个要点：
//   ① submit() 入口查一次（不再接新活）；
//   ② 任务包装器在真正开跑之前**再查一次**（挡住「刚被 worker 取走、还没进你函数」的那条）。
//   只有 ①，就有一条缝：worker 已经把它取出队列，你的 clear() 撤不回来。
//   两道加起来，「停闸之后不会有新的函数体被调用」才成立。
//
// ★ 收尾顺序（写死在 dtor 里，不写在注释里）：
//      先关闸 → 再撤队列 → 再等在场的人离场 → 最后才 delete 池。
//   反过来的写法（先 delete 池）在文档上也许也能跑，但它把「谁来等」交给了 Qt 的
//   默认行为 —— 而默认行为下面还压着那些任务攥着的裸指针。第 6 讲【8】已经演过
//   一遍那个坑，这里不再重复。
// ============================================================================
class ShutdownRunner : public QObject
{
    Q_OBJECT
public:
    explicit ShutdownRunner(int workers, QObject *parent = nullptr)
        : QObject(parent)
        , m_stop(std::make_shared<QAtomicInt>(0))
        , m_pool(new QThreadPool)          // ★ 私有池，故意不挂 parent（见 dtor 注释）
        , m_budgetMs(1500)
    {
        m_pool->setMaxThreadCount(workers);
        m_clock.start();
        m_poll.setInterval(20);
        connect(&m_poll, &QTimer::timeout, this, &ShutdownRunner::pollOnce);
    }

    ~ShutdownRunner() override
    {
        // ★ 走到这里时，一定是 drained 或 timedOut 之后（main 里按这个顺序删）。
        //   如果没走到那个状态就被删，说明我的收尾契约破了 —— 那也要留痕，不能沉默。
        m_poll.stop();
        delete m_pool;       // 「这次 delete 等了多久」由调用方自己掐表（见 main），不写全局
    }

    // body 拿到的是这个 runner 的闸门标志，用来在自己内部配合退出。
    using Body = std::function<void(const std::shared_ptr<QAtomicInt> &)>;

    bool submit(const Body &body)
    {
        if (m_stop->loadAcquire()) {       // 闸门 ①：受理口
            ++gRefused;
            return false;
        }
        const auto flag = m_stop;
        m_pool->start([flag, body]() {
            if (flag->loadAcquire())        // 闸门 ②：开跑前
                return;
            body(flag);
        });
        ++gSubmitted;
        return true;
    }

    // 【8】专用的最小动作：只关闸，不撤队列、不轮询、不记【7】那些账。
    //   为什么单独给一个方法：aboutToQuit 里只允许做「不再接活」这一件事，
    //   连 clear() 都不该在那儿做（它会让主线程去碰池的内部状态）。关闸就够了。
    void closeGate() { m_stop->storeRelease(1); }

    void requestStop()
    {
        ++gStopRequests;
        m_stop->storeRelease(1);           // 先关闸
        m_pool->clear();                   // 再撤掉还在队列里的
        m_stopAtMs = m_clock.elapsed();
        m_poll.start();                    // 然后交给事件循环，每 20ms 问一句
    }

Q_SIGNALS:
    void drained();
    void timedOut();

private:
    void pollOnce()
    {
        if (!m_pool->waitForDone(0)) {     // 期限 0：问一句，立刻回来
            if (m_clock.elapsed() - m_stopAtMs > m_budgetMs) {
                m_poll.stop();
                ++gTimedOut;
                Q_EMIT timedOut();
            }
            return;
        }
        m_poll.stop();
        gDrainMs.storeRelease(int(m_clock.elapsed() - m_stopAtMs));
        Q_EMIT drained();
    }

    std::shared_ptr<QAtomicInt> m_stop;
    QThreadPool *m_pool;
    QTimer m_poll;
    QElapsedTimer m_clock;
    qint64 m_stopAtMs = 0;
    const qint64 m_budgetMs;
};

static void phase7_startShutdown(ShutdownRunner *runner)
{
    section(QStringLiteral("【7】收尾状态机：关闸 → 撤队列 → 等离场 → 才 delete 池"));

    // 一枚「长期任务」占住唯一的工人：它只做一件事 —— 等闸门打开然后离场。
    // 它必须能自己走出来，否则这一节就是在测「池会不会挂住」，那没有意义。
    const auto longJob = [](const std::shared_ptr<QAtomicInt> &flag) {
        ++gLongEnter;
        QElapsedTimer t;
        t.start();
        while (!flag->loadAcquire() && t.elapsed() < 1000)   // 自己带期限，绝不无限等
            QThread::msleep(1);
        ++gLongExit;
    };
    const auto shortJob = [](const std::shared_ptr<QAtomicInt> &) {
        ++gShortEnter;                     // ★ 这一条**不该**被执行（它会被 clear 撤下）
    };

    const bool accepted1 = runner->submit(longJob);
    checkBool(QStringLiteral("【7】闸门开着时 submit 受理长期任务"), accepted1, true);

    if (!premise(QStringLiteral("【7】长期任务已经占住唯一的工人（之后投的都是排队）"),
                 waitForValue(gLongEnter, 1, 1000),
                 QStringLiteral("gLongEnter = %1").arg(gLongEnter.loadRelaxed()))) {
        // ★ 前提没成立也要关闸：否则状态机永远不会发 drained，整个进程只能等总闸。
        //   「结论错了」和「进程挂住」是两件不同的错，前者已经记进账了，后者不该附带发生。
        runner->requestStop();
        return;
    }

    for (int i = 0; i < 5; ++i)            // 这 5 条一定在队列里（工人被占着）
        runner->submit(shortJob);
    checkNum(QStringLiteral("【7】一共受理 6 条（1 条在跑 + 5 条排队）"),
             gSubmitted.loadRelaxed(), 6);

    runner->requestStop();                 // 关闸 + clear + 开始轮询

    checkNum(QStringLiteral("【7】停闸之后再 submit 被拒 1 次"), gRefused.loadRelaxed(), 1);
    const bool rejected = runner->submit(shortJob);
    checkBool(QStringLiteral("【7】被拒的这一次返回 false"), rejected, false);
    checkNum(QStringLiteral("【7】所以被拒总数变成 2"), gRefused.loadRelaxed(), 2);
    checkNum(QStringLiteral("【7】被撤下的 5 条短任务：本体一次都没进"),
             gShortEnter.loadRelaxed(), 0);
}

static void phase7_checkShutdown()
{
    // 由 drained / timedOut 之后调用 —— 见 main。
    checkNum(QStringLiteral("【7】超时 0 次（收尾在预算之内完成了）"),
             gTimedOut.loadRelaxed(), 0);
    checkNum(QStringLiteral("【7】长期任务靠自己看标志离场（gLongExit）"),
             gLongExit.loadRelaxed(), 1);
    checkNum(QStringLiteral("【7】requestStop 只被调用 1 次"), gStopRequests.loadRelaxed(), 1);
    verdict(Obs, QStringLiteral("【7】从关闸到池空"), true,
            QStringLiteral("%1 ms（轮询间隔 20ms，所以这个数不会小于 20）")
                .arg(gDrainMs.loadRelaxed()));
    qInfo().noquote() << QStringLiteral(
        "  → 这一段是第 6 讲留下的那道题：delete 一个私有池要花多少毫秒，"
        "\n     下面这一行是实测（Obs）。注意我量的是「已经空了之后」的 delete，"
        "\n     不是「有条活卡住时」的 delete —— 后者要么等、要么超时，量出来没有意义。");
}

// ============================================================================
// 十二、【8】aboutToQuit：程序退出前最后一道闸门
// ----------------------------------------------------------------------------
// QCoreApplication::aboutToQuit（qcoreapplication.h 209 行）在事件循环**退出之后、
// exec() 返回之前**发出。它是你最后一次能确定「主线程还活着、但活该停了」的时刻。
//
// 三条规则：
//   ① 只许关闸，不许等待。这里 waitForDone(Forever) 一下，用户看到的就是一个
//      关不掉的窗口（点叉没反应、任务管理器里才看得见）。
//   ② 关闸之后要**拒绝**新任务 —— 因为有些退出路径（比如最后一个窗口关闭）
//      之后还会有人提交活。
//   ③ 这里的核对，必须进退出码 —— 所以本讲的退出码改成「只在 main 的 return 上算一次」。
//      第 6 讲用的是 QCoreApplication::exit(gLedger.exitCode())，在 exit() 那一刻
//      算出来的码，看不见之后 aboutToQuit 里补的账。**这是这一讲对前面写法的一次修正。**
// ============================================================================
static void phase8_installQuitGuard(QCoreApplication &app, ShutdownRunner *guard)
{
    // ★ context 用 &app，不用 guard：aboutToQuit 是 app 发出来的，接收者必须活得比 guard 久。
    //   第一版我把 context 写成了 runner（也就是【7】那个），结果它在 drained 时就被删了 ——
    //   连接随之断开，这一节永远不会触发。**context 选错，代码不报错，只是那段逻辑静默消失。**
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [guard]() {
        section(QStringLiteral("【8】aboutToQuit：只关闸，不等待"));
        ++gAboutHits;
        checkNum(QStringLiteral("【8】此刻 aboutToQuit 恰好触发 1 次"), gAboutHits.loadRelaxed(), 1);

        // guard 在启动时就已经关闸了（见 main），所以这里必然被拒。
        const bool accepted = guard->submit([](const std::shared_ptr<QAtomicInt> &) {
            ++gAfterQuitRuns;
        });
        checkBool(QStringLiteral("【8】退出闸门之后再提交，必须被拒"), accepted, false);
        checkNum(QStringLiteral("【8】被拒的那具 lambda 一次都没跑"),
                 gAfterQuitRuns.loadRelaxed(), 0);
        qInfo().noquote() << QStringLiteral(
            "  → 这里一句 waitForDone 都没有。要等的人请到【7】那个状态机里去等，"
            "\n     它带期限、且在事件循环里跑。aboutToQuit 里只做「不再接活」这一件事。");
    });
}

// ============================================================================
// 十三、合账：把这一讲的全部硬账收成一行
// ============================================================================
static void reconcile()
{
    section(QStringLiteral("合账"));
    const int broken = gLedger.failed + gLedger.stalled;
    qInfo().noquote() << QStringLiteral("  硬账 %1 项：对上 %2，不符 %3；整节作废 %4；总闸%s")
                             .arg(gLedger.total)
                             .arg(gLedger.total - gLedger.failed - gLedger.stalled)
                             .arg(gLedger.failed)
                             .arg(gLedger.stalled)
                             .arg(gLedger.watchdogFired ? QStringLiteral("跳过总闸")
                                                        : QStringLiteral("没跳"));
    qInfo().noquote() << QStringLiteral("  退出码 = %1（%2）")
                             .arg(gLedger.exitCode())
                             .arg(gLedger.exitCode() == 0
                                      ? QStringLiteral("没有硬账不符、没有整节作废")
                                      : (broken > 0
                                             ? QStringLiteral("有硬账不符或整节作废")
                                             : QStringLiteral("一项都没核过")));
    if (gLedger.exitCode() != 0)
        qWarning().noquote() << QStringLiteral("  ★ 请按上面的 [!! ] / [作废] / 前提 行往下查。");
}

// ============================================================================
// 十四、main：时间轴只有一条，所有异步的东西都在【7】【8】
// ============================================================================
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("threads_07_cancel_and_shutdown"));

    qInfo() << "================================================================";
    qInfo() << "threads 第 7 讲 · 取消与收尾（Qt =" << QString::fromLatin1(QT_VERSION_STR) << "）";
    qInfo() << "================================================================";

    QThreadPool *global = QThreadPool::globalInstance();
    const int globalMaxAtStart = global->maxThreadCount();
    qInfo().noquote() << QStringLiteral("  全局池账面（本讲只用来看一眼，全程不改它）："
                                       "maxThreadCount=%1，idealThreadCount=%2")
                             .arg(globalMaxAtStart).arg(QThread::idealThreadCount());

    // ★ 这个 runner **不挂 parent**：它内部攥着一个池，池里的任务攥着它给出去的标志。
    //   挂 parent 就等于把「谁先死」交给子对象列表的顺序 —— 第 6 讲【8】那条教训。
    //   这里由 main 在 drained 之后显式 delete，顺序写在代码里。
    auto *runner = new ShutdownRunner(1);

    // 【8】需要另一个闸门对象，而且它必须活到 aboutToQuit 之后 ——
    //   不能被【7】那条「drained 就删」的路径带走。它在启动时就关闸（模拟
    //   「程序已经在退出流程里」），然后一直活到 main 结尾。
    auto *quitGuard = new ShutdownRunner(1);
    quitGuard->closeGate();

    QTimer::singleShot(0, [runner]() {
        // 【0】~【6】全是同步的：每一节自己等完自己的池才返回。
        phase0_absenceProof();
        phase1_cooperativeCancel();
        phase2_interruptionIsPerThread();
        phase3_futureCancel();
        phase4_nullPoolCancel();
        phase5_promiseCancel();
        phase6_promiseDestructor();

        // 【7】起来头就返回，把轮询交给事件循环。
        phase7_startShutdown(runner);
    });

    // 【7】的结果：drained 或 timedOut，两者都进账，然后收账 + 关程序。
    //   注意顺序：先核对 → 再 delete runner（顺手量它花了多少毫秒）→ 再 quit。
    QObject::connect(runner, &ShutdownRunner::drained, &app, [runner]() {
        phase7_checkShutdown();

        // 「delete 一个已经空了的私有池要多久」—— 第 6 讲留下的那道题，在这里量。
        // 由调用方掐表，而不是让 dtor 往全局写：**测量属于测量的人**。
        QElapsedTimer dtorClock;
        dtorClock.start();
        delete runner;
        const qint64 dtorMs = dtorClock.elapsed();
        verdict(Obs, QStringLiteral("【7】delete 那个已经空了的私有池（第 6 讲留下的问题）"),
                true, QStringLiteral("%1 ms").arg(dtorMs));

        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    });
    QObject::connect(runner, &ShutdownRunner::timedOut, &app, [runner]() {
        // 收尾契约被破了：这一条会进 failed（gTimedOut 让 phase7_checkShutdown 的期望落空），
        // 但绝不能就这么挂死 —— 打印、收账、退出，一个不少。
        qWarning().noquote() << QStringLiteral("【7】收尾超预算（1500 ms 还没空），强制继续");
        phase7_checkShutdown();
        delete runner;
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    });

    // 【8】的闸门装在这里。它会在 quit 之后、exec 返回之前跑一次。
    phase8_installQuitGuard(app, quitGuard);

    // 总闸：正常路径走不到（按每节的等待期限加出来的预算约 11 秒，这里是它的 1.4 倍）。
    // ★ 必须用不带 context 的那一族（qtimer.h 83 行起，85~88 行把 receiver 写死 nullptr）：
    //   带 context 的版本 receiver 一死就不发了，而总闸恰恰要在「事情不对劲」时还能响。
    QTimer::singleShot(15000, []() {
        gLedger.watchdogFired = true;
        qWarning().noquote() << QStringLiteral("【总闸】15 秒到点：强制收尾");
        qInfo().noquote() << QStringLiteral("      到点时硬账已到 %1 项").arg(gLedger.total);
        QCoreApplication::quit();
    });

    app.exec();

    // ★★ 退出码只在这里算一次。
    //   为什么不学第 6 讲在 exit() 里传码：因为本讲【8】的核对发生在 aboutToQuit 里，
    //   那已经晚于任何一次 exit() 调用。把「算账」和「退出」解耦，是这一讲唯一
    //   一个跨讲的动作 —— 也是这一整套教材里所有「最后还要补几笔账」的场合该用的形状。
    // 【8】那个闸门活到这里才死：它的池是空的（启动时就关了闸，一条活都没进去过），
    //   所以这一句 delete 不会等任何人。
    delete quitGuard;

    reconcile();
    const int rc = gLedger.exitCode();
    qInfo().noquote() << QStringLiteral("进程退出码 = %1").arg(rc);
    return rc;
}

// ============================================================================
// 十五、速查：背不住就回来查
// ============================================================================
/*
 * 「停一条活」的五条路，只有一条是正解：
 *   池上 cancel()                 ✗ 不存在（编译期已证）
 *   任务上 cancel()/terminate()   ✗ 不存在
 *   强行 QThread::terminate()     ✗ qthread.h 90 行确实有；杀的是整条线程，锁和半截数据全留着
 *   池给你线程列表去发中断       ✗ 不存在（没有 threads()），所以 requestInterruption 在池里没用
 *   任务自己在检查点看标志        ✓ 唯一正解。标志归谁：自备 QAtomicInt，或用 QPromise::isCanceled()
 *
 * 三种「取消」的主语，别混：
 *   QThread::isInterruptionRequested()   线程级，作者：你自己 QThread 子类
 *   QPromise::isCanceled()               任务级，作者：QtConcurrent + QPromise（qpromise.h 80 行）
 *   QFuture::cancel()                    句柄级，作者：调用方（qfuture.h 65 行）
 *
 * cancel() 对三种局面的效果（本讲实测过的）：
 *   还在队列里   函数体不会执行（两条实现路径都通向这个结论）
 *   正在跑       只翻状态位；函数体不看就不停（77~80 行只在入口查一次）
 *   已经跑完     本机量不出来 → Obs，别写成结论
 *
 * 收尾的正确顺序（照抄【7】）：
 *   关闸（submit 拒绝新活）
 *   → clear()（撤队列）
 *   → 置标志（让在跑的自己走出来）
 *   → waitForDone(0) 轮询 + 期限（交给事件循环，不许在主线程死等）
 *   → delete 池
 *
 * 两个「不许」：
 *   · 有事件循环的线程里，不许裸 waitForDone()（默认 Forever，qthreadpool.h 79 行）
 *   · 不许 waitForFinished() 等 future（qfuture.h 104 行没有带期限的重载）
 *
 * aboutToQuit 里只做两件事：关闸、拒绝新活。不许等。
 */

// ============================================================================
// 十六、老师傅的几句收尾话
// ============================================================================
/*
 * 这一讲我最想让你带走的，不是「怎么取消」，而是**为什么取消必须是协作式的**。
 *
 * 设想你真的有一条能随时掐断任意函数的 API。它掐下去的时候，那个函数可能正持有一把锁、
 * 可能刚把半个节点挂进链表、可能正在往一个缓冲区写到第 3 个字节。你掐断它，谁去解那把锁？
 * 谁去把链表的半个节点摘掉？没人。于是下一次别人拿锁就死，下一次遍历链表就崩 ——
 * 而且是**在离你掐断它很远的地方**崩，栈里全是不相干的人。
 * 这就是为什么 QThread::terminate() 在文档里被写成「危险」，而不是「快速」。
 *
 * 协作式取消把这件事的成本说清楚了：**只有任务自己知道现在是不是一个安全点。**
 * 它知道「这一块算完了、结果是对的、可以走」；它也知道「我刚拿到锁，等我放下」。
 * 所以把那一句 if 放在哪儿，是任务作者的责任，也是他能提供的唯一保证。
 *
 * 第二件事，关于「我怎么知道它真的取消了」。
 * 你在这一讲里看到的每一个硬账，形状都一样：**先证明前提，再证明结论。**
 * 「置位时还剩 39 块没做」（前提）+「置位后只多做了 1 块」（结论）→「取消生效了」。
 * 少任何一半，那句话都可以被一个更慢的机器推翻。这就是我为什么在每一节都先打一行
 * 「前提：…」—— 它们是这份材料的承重墙，不是客套话。
 *
 * 第三件事，关于「量不出来就直说」。
 * 【3C】和【6A/B】里那些 Obs 不是我懒。cancel() 对已完成的 future 到底早不早退，
 * 那行代码在 .cpp 里，我看不到；「父 future 被取消时 then 的处理器跑不跑」同理。
 * 一个材料如果在这种地方硬给一个「一定是」，它后面所有的「一定是」你都得打个折。
 * 所以我的规矩是：**能被头文件里的源码或两句 static_assert 撑住的，才写成硬账；
 * 剩下的写成 Obs，并且把两种可能都摆出来。** 你以后写设计文档、写评审意见，
 * 也该守这条线 —— 它比任何一条 API 细节都值钱。
 *
 * 最后一句，关于退出码。
 * 这一讲把「算账」挪到了 main 的 return 上，只为了一个理由：aboutToQuit 里的账，
 * 任何一次 exit() 都看不见。**一个程序的失败必须有一个唯一的、最后被计算的地方。**
 * 散在三个 exit() 里的退出码，早晚会有一个地方忘了加。
 */

// ——— 小练习 ———
/*
 * 1. 把【1】的检查点密度从 2ms 改成 40ms，重跑。
 *    记录「从置位到池空」的毫秒数（那一行已经是 Obs），画成两条线。
 *    回答：为什么「取消延迟」这件事在产品上要写成「最坏 N 毫秒」而不是「很快」？
 *    ★ 把你测到的 N 和每块耗时对上：N ≈ 一块的耗时。这就是「密度 = 上限」的实测版。
 *
 * 2. 给【1】的 ChunkRunnable 加一个「已做完的块数」结果，在取消后从外面读回来
 *    （提示：QPromise<int> + addResult，参考【5】）。要求：
 *      · 取消时把「做到第几块」交出去，而不是丢掉（这正是断点续传的形状）；
 *      · 把这一条从 Obs 升成 Hard：构造一个「取消后读到的值 == gStoppedAt」的核对。
 *    说明：为什么「取消时交出进度」比「取消时什么都不交」贵，但值得？
 *
 * 3. 把【3B】那个不看标志的函数改对：循环里加 p.isCanceled() 检查。
 *    重跑，确认「取消后它不再把剩下的块做完」。然后回答：
 *    「同样的 cancel() 调用，为什么在【3B】里没用、在【5】里有用？」
 *    （提示：答案不在 cancel() 里，在函数体里。）
 *
 * 4. 把【7】的 ShutdownRunner 改成**两个** runner（各 2 个工人），退出时同时收。
 *    要求：等两个都 drained 才 quit；任何一个 timedOut 都要让退出码非 0；
 *    并且打印「谁先空、谁后空」。回答：为什么不该用 waitForDone 串行地等两个？
 *
 * 5. 故意把【7】的 m_budgetMs 改成 5（比轮询间隔 20ms 还小），重跑。
 *    观察：timedOut 这条路径会不会被执行？退出码是什么？总闸有没有跳？
 *    然后回答：**一个「超时会变成失败」的收尾契约，为什么比「超时就静默继续」好？**
 *    （提示：用户看到的「窗口关不掉 3 秒」和你日志里的一行 [!! ]，哪个能被修好？）
 *
 * 6. 资深题：写一个 CancelScope，对外只有 requestCancel() 和 isCancelRequested()，
 *    内部是一个 QAtomicInt。要求：
 *      · 能同时喂给 N 个【1】那种任务（shared_ptr 共享，别用裸指针）；
 *      · 析构时若还有任务在跑，打印一行警告（说明「作用域结束了但活没结束」）；
 *      · 用它改造【7】的闸门，让 submit 的那个 lambda 捕获 shared_ptr 而不是 this。
 *    做完把【7】里那句「闸门 ②」的注释删掉，看看你的测试还能不能过 ——
 *    如果还能过，说明你的闸门 ① 太强、② 根本没被用到；如果不能过，说明 ② 是必需的。
 *    两种结果都有信息，把结论写下来。
 *
 * 7. 找一段你项目里真实的「取消」代码，用本讲的五个问题审它一遍：
 *      (a) 状态位是不是原子的？(b) 检查点在哪、密度多大？
 *      (c) 取消之后，已经算出来的结果怎么处理（丢？交出去？）
 *      (d) 谁负责收尾 —— 有没有一条路是「忘了 finish 也没事」的？
 *      (e) 退出时是 waitForDone(Forever) 还是轮询 + 期限？
 *    把这五个答案写成一段评审意见。这一题不写代码，但它比前面六题都用得上。
 */
// ============================================================================

// 本文件有 1 个 Q_OBJECT 类（ShutdownRunner），AUTOMOC 会生成 .moc 并在这里包含。
#include "07_cancel_and_shutdown.moc"
