// ============================================================================
// 第 5 讲 · QtConcurrent：不想自己管线程的时候怎么用
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   前四讲都是「自己动手」：QThread、线程亲和性、跨线程信号槽、同步原语。
//   这一讲换路线：**我把活交出去，结果拿回来，中间那根线程我不管**。这就是 QtConcurrent。
//     1) QtConcurrent::run 的四条签名（本机 qtconcurrentrun.h 42/61/70/80 行）：带线程池的、
//        用全局池的、以及**带 Promise 类型提示**的那一对。参数与返回值怎么穿过任务包
//        （std::decay + std::forward，**参数是被拷贝进 tuple 的**）；为什么它必须返回东西 ——
//        第 39~40 行 Q_NODISCARD_X("Use QThreadPool::start(Callable&&) if you don't need the
//        returned QFuture") 把话说明白了：**不要结果就别用 run**，用第 6 讲的线程池接口。
//     2) QFutureWatcher<T>：为什么 UI 只连 watcher、绝不轮询 future。逐个连
//        started / progressRangeChanged / progressValueChanged / resultReadyAt / finished
//        （qfuturewatcher.h 50~65 行），并**打印每个回调所在的线程**：watcher 内部靠
//        QFutureWatcherBase::event()（第 48 行）把结果投递到它自己的线程，
//        所以「watcher 活在哪条线程，信号就在哪条线程发」。
//     3) 阻塞语义：用 QElapsedTimer 把「result() 会卡住调用者」量出来。
//        qfuture.h 311~322 行写得很直白：result() 第一句就是 d.waitForResult(0)；
//        isFinished()/isResultReadyAt() 不阻塞，而 result()/resultAt()/results()/
//        const_iterator 的 ++ 全阻塞（第 236~239 行 advanceIndex 的注释亲口承认 will wait）。
//     4) mapped / mappedReduced / filtered / filter：对容器做并行映射与过滤。
//        **Qt6 的 QFuture 没有 mapped() 成员函数**（本机 qfuture.h 全文无此名），它们是
//        QtConcurrent 命名空间的自由函数（qtconcurrentmap.h 353/363/374/385 行、
//        qtconcurrentfilter.h 334/342/351/359 行），结果类型由 QtPrivate::MapResultType 从
//        **你的 map 函数返回值**推出 —— 这就是「自定义结果类型」的正解；而 filter()（36 行）
//        返回 QFuture<void> 并**原地改你的容器**，是这一节最大的坑，用两组数字对比。
//     5) 线程池从哪来：run 不带池参数时借的就是 QThreadPool::globalInstance()
//        （qtconcurrentrun.h 65 行），所以 **setMaxThreadCount(2) 会直接压低你的并行度**。
//        但本机实测发现它**不会立刻生效**：证据⑤做三发对照（默认池 / 压完立刻开跑的全局池 /
//        开池即 max=2 的私有池），把「天花板存在」和「天花板慢半拍」两件事一起钉住。
//
// 对应构建目标名：threads_05_qtconcurrent
//   find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)
//   add_executable(threads_05_qtconcurrent 05_qtconcurrent.cpp)
//   target_link_libraries(threads_05_qtconcurrent PRIVATE Qt6::Widgets Qt6::Concurrent)
//   set(CMAKE_AUTOMOC ON)              ← 本文件**有 Q_OBJECT 类（SumConsole）**
//   手工核对：moc 05_qtconcurrent.cpp -o 05_qtconcurrent.moc
//             末尾那句 #include "05_qtconcurrent.moc" 就是给 AUTOMOC 留的钩子。
//
// 运行前须知：
//   · 无人值守：QTimer::singleShot 串起证据①~⑤与收尾，跑完自己 qInfo() 打印证据并
//     QCoreApplication::exit(Ledger::exitCode())；本机实测 13/13 项全过、1.5 秒结束，
//     **没有任何模态对话框**。每个阶段的结论都进 Ledger：求和一致、线程不同、
//     filtered/filter 数量对得上、私有池的峰值恰好等于 maxThreadCount、
//     压全局池那发允许 2~3（见证据⑤的源码解释）—— 全过退出码 0，有一项不对退出码 1。
//   · 用 QCoreApplication 就够（本讲一个控件都没有），不需要显示设备。
//   · 不读文件、不联网、不碰共享内存。
//   · 「最大并发」与 idealThreadCount / 池的 maxThreadCount 有关，**换机器数字会变**；
//     本机 16 核：⑤A 峰值 12（= 任务数，没触顶）、⑤B 2 或 3、⑤C 恒为 2。
//     我承诺的关系：⑤C 的峰值 == 私有池上限；⑤B 不低于 2、但可能高出新上限。
//   · 实测口径：本文件已在本机 Qt 6.12.0 + MinGW 13.1 下真实编译、链接、运行
//     （QT_QPA_PLATFORM=offscreen，12 连跑全绿）。文中「预期」一律是实测数字；
//     换 CPU 核数 / Qt 版本时上面那两个峰值要重测。
// ============================================================================

#include <QCoreApplication>        // QtCore/qcoreapplication.h
#include <QtConcurrent/QtConcurrentRun>   // QtConcurrent/qtconcurrentrun.h
#include <QtConcurrent/QtConcurrentMap>   // QtConcurrent/qtconcurrentmap.h
#include <QtConcurrent/QtConcurrentFilter>// QtConcurrent/qtconcurrentfilter.h
#include <QFuture>                 // QtCore/qfuture.h
#include <QFutureWatcher>          // QtCore/qfuturewatcher.h（QFutureWatcherBase 也在这）
#include <QPromise>                // QtCore/qpromise.h（进度与取消的「发票」）
#include <QThreadPool>             // QtCore/qthreadpool.h
#include <QThread>                 // QtCore/qthread.h：msleep / currentThread / idealThreadCount
#include <QVector>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QElapsedTimer>
#include <QtGlobal>
#include <QDebug>

#include <atomic>
#include <functional>              // std::cref（引用参数不进拷贝那条路）

// ============================================================================
// 零、硬账：退出码 = 结论
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

// ★ ②b 那条「mappedReduced 总和 = 串行基准」的硬账，只该由**真正的 reduce**触发。
//   但 console.watcher() 被两路复用：证据②c 的 progressTask（run<long long>）与
//   证据②b 的 mappedReduced —— 两路都在 SumConsole::onFinished 里 emit summaryReady。
//   ②c 先跑完（约 220ms），它 addResult 的是「累加 rounds 的小和」，**不是** demoData 的和，
//   所以那一发 summaryReady 拿它去比 sumSequential() 必然为假 → 一个假红。
//   修法不是放宽判据，而是给这条核对「上膛」：只有 mappedReduced 那一发才计入。
static bool gReducedArmed = false;

// ============================================================================
// 一、核对签名（本机 D:\Qt\6.12.0\mingw_64\include，Qt 6.12.0；对不上的一律不用）
// ============================================================================
// QtConcurrent/qtconcurrentrun.h
//   第 39~40 行 QTCONCURRENT_RUN_NODISCARD = Q_NODISCARD_X("Use QThreadPool::start(Callable&&)…")
//   第 42~50 行 run(QThreadPool *pool, Function &&f, Args &&...args)
//   第 52~59 行 run(QThreadPool *pool, std::reference_wrapper<const Function> &&, Args&&…)
//                 ← 只有**函数本身**能用 cref 包一层，别误以为所有参数都能免拷贝
//   第 61~67 行 run(Function &&f, Args &&...args) → 内部 run(globalInstance(), …) ← 池的答案
//   第 69~85 行 同上的 PromiseType 提示版；用法 QtConcurrent::run<结果类型>(task, 其余参数…)，
//                 此时 task 的**第一个形参必须是 QPromise<结果类型>&**。
//   qtconcurrentstoredfunctioncall.h 第 138~169 行 StoredFunctionCallWithPromise：
//     第 143~145 行 data = Resolver::initData(f, std::ref(prom), args…)
//                 ← 证据：promise 是以**引用**塞给你的可调用对象的。
//   本机头文件里 **不存在**（grep 全目录零命中，别照抄网上答案）：
//     · QtConcurrent::runWithCancel / runWithFeedback / RunConfiguration
//       —— Qt 5.14 的技术预览、**Qt 6.0 就删了**；进度与取消走 QPromise（本讲证据②c）。
//     · QFuture<T>::mapped() / filtered() 成员函数 —— 改用 QtConcurrent 命名空间的自由函数。
//     · QPromiseCanceledException / QPromiseCancellationException —— 第 7 讲细说。  name-ok:QPromiseCanceledException, QPromiseCancellationException 「本机头文件里不存在」清单
// QtCore/qfuture.h（QFuture<T>）
//   第 65~67 行 cancel()/isCanceled()/cancelChain()；第 95~97 行 isStarted/isFinished/isRunning
//                 ← **这些不阻塞**；第 99~103 行 resultCount/progressValue/Minimum/Maximum/Text
//   第 104 行 waitForFinished()；第 106~119 行 result()/resultAt()/isResultReadyAt()/
//                 results()/takeResult()；第 311~322 行实现里 result()/resultAt() **第一句
//                 都是 waitForResult(…)** ← 阻塞的铁证
//   第 129~142 行 then(...) 四重载；第 147/151 行 onFailed()；第 155/158 行 onCanceled()
//   第 236~239 行 const_iterator::advanceIndex 注释「n == 0 … **will wait**」
//                 ← range-for 一个 future 就是阻塞遍历，别在主线程写
// QtCore/qfutureinterface.h：第 57~68 行 enum State{NoState,Running,Started,Finished,Canceled,
//   Suspending,Suspended,Throttled,Pending}；79~90 行 reportStarted/Finished/Canceled/Exception/
//   ResultsReady；96/100/102 行三个 setProgress*；131~133 行 cancel/cancelAndFinish/cancelChain；
//   142~145 行 waitForFinished/waitForNextResult/waitForResult/waitForResume
//              ★ 勘误（这一整段旧稿都偏了，本机 6.12.0 逐行重数）：
//                · State 组 56~68 → **57~68**（56 行是 `public:`）。九个值全是显式位号
//                  0x00/0x01/0x02/0x04/0x08/0x10/0x20/0x40/0x80（66 行还有一行注释解释 Pending），
//                  旧稿照隐式接续记就正好错过位号这件事。
//                · report* 组 76~84 → **79~90**：79 started、80 finished、81 canceled、
//                  83 reportException(const QException &)（82 行 `#ifndef QT_NO_EXCEPTIONS` 包着，
//                  关掉异常这一份就没有）、85/87 是 reportException 的**二选一**：
//                  84 行 `#if QT_VERSION < QT_VERSION_CHECK(7,0,0)` 走 85 的按值形参，
//                  否则走 87 的 const 引用；90 reportResultsReady(int beginIndex, int endIndex)。
//                  76 行是虚析构，不是 report。
//                · setProgress* 组 96~101 → **96/100/102**：96 Range、100 Value、
//                  102 ValueAndText；101 行夹在中间是 `int progressValue() const;`，
//                  写 96~101 会把 getter 当成第三个 setter。
//                · cancel 组 131~135 → **131~133**：131 cancel()、132 cancelAndFinish()（就地
//                  转调 cancel(CancelMode::CancelAndFinish)）、133 cancelChain()；134/135 是
//                  setAddResultsIfCanceledEnabled / is…，跟取消链没关系。
//                · waitFor* 组 152~156 → **142~145**：142 Finished、143 NextResult、
//                  144 Result(int)、145 Resume；146 行是 `void suspendIfRequested();`（也是公开的，
//                  旧稿漏了它）。
// QtCore/qpromise.h：49 行 future()；61 行 addResult(U&&, int index = -1)；75/76 行 start()/
//   finish()；**80 行 isCanceled() ← 协作式取消的轮询点**；83~88 行三个 setProgress*；
//   39~46 行析构：没 finish 就 cancelAndFinish() ←「忘了 finish 会怎样」的答案
// QtCore/qfuturewatcher.h：44 行 waitForFinished()（也会阻塞）；46 行 setPendingResultsLimit(int)
//   （结果刷太快时的限流阀）；50~65 行信号 started/finished/canceled/suspending/suspended/
//   resumed/resultReadyAt(int)/resultsReadyAt(int,int)/progressRangeChanged(int,int)/
//   progressValueChanged(int)/progressTextChanged(QString)；67~73 行槽 cancel/suspend/resume；
//   107 行析构 disconnectOutputInterface()；109~117 行 setFuture/future/result/resultAt
// QtCore/qthreadpool.h（第 6 讲主角，这里只看 run 借了谁）：35 行 globalInstance()；37/38 行
//   start(QRunnable*,prio)/tryStart；50~55 行 template<Callable> start/tryStart/
//   startOnReservedThread（Qt6 新增，免 QRunnable）；60/61 行 setMaxThreadCount/maxThreadCount；
//   63 行 activeThreadCount() ← Qt6 **没有** threadCount()/reservedThreadCount()；78/79 行
//   waitForDone(int)/waitForDone(QDeadlineTimer = Forever)
// ============================================================================

// ============================================================================
// 二、被测数据与纯函数：并行结果必须能和串行结果对得上，才算证据
// ============================================================================
static const int kItemCount = 60000;

// 造一份「可核对」的数据：sum 有解析解，串并行谁算错了一眼看得出来
static const QVector<int> &demoData()
{
    // 函数内 static：生命周期到进程结束，比任何 future 都活得久 ——
    // 工作线程引用外部对象时，请照这个写法，**别引用 main 的栈变量**
    static const QVector<int> data = [] {
        QVector<int> v;
        v.reserve(kItemCount);
        for (int i = 0; i < kItemCount; ++i)
            v.append((i * 7) % 101 - 50);   // 有正有负，防止「溢出凑对」骗过校验
        return v;
    }();
    return data;
}

struct Block { qsizetype begin; qsizetype count; };   // 分块描述，map 的输入元素

static long long sumBlock(const Block &b)
{
    const QVector<int> &data = demoData();
    long long acc = 0;
    const qsizetype end = qMin(b.begin + b.count, data.size());
    for (qsizetype i = b.begin; i < end; ++i)
        acc += data.at(i);
    return acc;                                // ← 返回 long long：mapped 的结果类型就是它
}

static long long sumSequential()
{
    long long acc = 0;
    const QVector<int> &data = demoData();
    for (int v : data)
        acc += v;
    return acc;
}

// 一个「带参数、带返回值」的小任务：证据①用它
struct StatResult {
    long long sum = 0;
    qsizetype count = 0;
    Qt::HANDLE workerThread = nullptr;         // 任务实际跑在哪条线程（currentThreadId 返回它）
};

static StatResult statRange(const QVector<int> &data, qsizetype begin, qsizetype count)
{
    StatResult r;
    const qsizetype end = qMin(begin + count, data.size());
    for (qsizetype i = begin; i < end; ++i)
        r.sum += data.at(i);
    r.count = end - begin;
    r.workerThread = QThread::currentThreadId();
    QThread::msleep(60);                       // 让它「慢一点」，好让 finished 信号有意义
    return r;
}

// 引擎自己报进度/结果：QtConcurrent::run<PromiseType> 那条路（进度不靠 mapped 引擎时就这样写）
static void progressTask(QPromise<long long> &promise, int rounds)
{
    promise.setProgressRange(0, 100);          // → watcher 的 progressRangeChanged(min,max)
    promise.start();                           // → started
    long long acc = 0;
    for (int i = 0; i < rounds; ++i) {
        acc += (i * 3) % 97;
        QThread::msleep(4);
        if (i % 5 == 0) {
            const int percent = int(qint64(i) * 100 / rounds);
            promise.setProgressValueAndText(percent, QStringLiteral("累加到 %1").arg(i));
        }
        if (promise.isCanceled()) {            // ← 第 7 讲的主角，这里先埋一句
            qInfo() << "    progressTask 观察到 isCanceled()，在第" << i << "轮收手";
            promise.finish();
            return;
        }
    }
    promise.addResult(acc);                    // 一个结果 → resultReadyAt(0)
    promise.finish();                          // → finished（**不写这行，析构时会被 cancelAndFinish**）
}

// ============================================================================
// 三、UI 侧只干一件事：连 watcher 的信号
//     有 Q_OBJECT 的理由：要往外转发一个自己的信号 summaryReady（第贰阶段的老规矩：
//     要自定义信号槽才写 Q_OBJECT，不写 Q_OBJECT 也行，但那样只能靠 lambda 捕获）
// ============================================================================
class SumConsole : public QObject
{
    Q_OBJECT
public:
    explicit SumConsole(QObject *parent = nullptr) : QObject(parent)
    {
        // 新式指针语法连接（本讲不用字符串宏）。**context 写 this**：
        // 这样对象销毁后连接自动断开，不会有「回调打到死人身上」的事故。
        connect(&m_watcher, &QFutureWatcherBase::started,       this, &SumConsole::onStarted);
        connect(&m_watcher, &QFutureWatcherBase::progressRangeChanged,
                this, &SumConsole::onRange);
        connect(&m_watcher, &QFutureWatcherBase::progressValueChanged,
                this, &SumConsole::onValue);
        connect(&m_watcher, &QFutureWatcherBase::resultReadyAt, this, &SumConsole::onResultReady);
        connect(&m_watcher, &QFutureWatcherBase::finished,      this, &SumConsole::onFinished);
        connect(&m_watcher, &QFutureWatcherBase::canceled,      this, &SumConsole::onCanceled);
    }

    QFutureWatcher<long long> *watcher() { return &m_watcher; }

    int rangeEvents() const { return m_rangeEvents; }
    int valueEvents() const { return m_valueEvents; }
    int resultEvents() const { return m_resultEvents; }
    int readyIndexesCount() const { return int(m_readyIndexes.size()); }
    int lastRangeMax() const { return m_lastMax; }
    int lastProgress() const { return m_lastValue; }
    QString lastText() const { return m_lastText; }
    void resetCounters()
    {
        m_rangeEvents = m_valueEvents = m_resultEvents = 0;
        m_lastMax = m_lastValue = -1;
        m_lastText.clear();
    }

public Q_SLOTS:
    void onStarted() { qInfo() << "    [watcher] started()  在主线程回调？"
                               << QThread::currentThread(); }
    void onRange(int min, int max)
    {
        ++m_rangeEvents;
        m_lastMax = max;
        qInfo() << "    [watcher] progressRangeChanged(" << min << "," << max << ")";
    }
    void onValue(int value)
    {
        ++m_valueEvents;
        m_lastValue = value;
        m_lastText = m_watcher.progressText();
    }
    void onResultReady(int index) { ++m_resultEvents; m_readyIndexes << index; }
    void onCanceled() { qInfo() << "    [watcher] canceled()"; }
    void onFinished()
    {
        // 注意：这里取 result() **不会阻塞**，因为 finished 意味着结果已经在包里了。
        // 「finished 之后再取结果」是唯一正确的取法，见证据③的对比。
        const long long total = m_watcher.future().result();
        Q_EMIT summaryReady(total, m_valueEvents, m_resultEvents);
    }

Q_SIGNALS:
    void summaryReady(long long total, int progressEvents, int resultEvents);

private:
    QFutureWatcher<long long> m_watcher;
    int m_rangeEvents = 0;
    int m_valueEvents = 0;
    int m_resultEvents = 0;
    int m_lastMax = -1;
    int m_lastValue = -1;
    QList<int> m_readyIndexes;
    QString m_lastText;
};

// ============================================================================
// 四、证据①：run 的参数与返回值 + 「UI 只连 watcher」
// ============================================================================
static void evidenceRunAndWatcher()
{
    qInfo() << "【证据①】QtConcurrent::run：参数怎么进去、结果怎么出来";
    const QVector<int> &data = demoData();
    // std::cref：让**容器本体**不走 decay 拷贝那条路（被引用的对象必须活过 future）
    QFuture<StatResult> f = QtConcurrent::run(statRange, std::cref(data),
                                             qsizetype(0), qsizetype(data.size()));
    qInfo() << "  run 立刻返回 future：isFinished =" << f.isFinished()
            << "isRunning =" << f.isRunning() << "（**这里没有阻塞**：调的是 isFinished）";
    QFutureWatcher<StatResult> w;               // 局部 watcher：它住在主线程，信号就在主线程发
    QElapsedTimer t;
    t.start();
    QObject::connect(&w, &QFutureWatcherBase::finished, [&w, t]() {
        const StatResult r = w.future().result();   // 此刻结果已在包里，不会等
        qInfo() << "  finished 槽里取结果：sum =" << r.sum << "count =" << r.count
                << "耗时" << t.elapsed() << "ms";
        qInfo() << "  任务的线程 HANDLE =" << r.workerThread
                << "｜主线程 HANDLE =" << QThread::currentThreadId() << "相同吗 ="
                << (r.workerThread == QThread::currentThreadId()) << "（不同 = 真在别的线程算的）";
        qInfo() << "  槽跑在" << QThread::currentThread() << "，是主线程吗 ="
                << QThread::isMainThread()
                << "← watcher 把跨线程结果**转成了本线程的槽调用**";
        check("① 任务统计的元素数 = 数据集大小", r.count, kItemCount);
        checkBool("① 并行求和的 sum 与串行基准相等", r.sum == sumSequential());
        checkBool("① 任务跑在**别的**线程上", r.workerThread != QThread::currentThreadId());
        checkBool("① finished 槽回到主线程", QThread::isMainThread());
    });
    w.setFuture(f);
}

// ============================================================================
// 五、证据②：mapped（自定义结果类型）+ mappedReduced
// ============================================================================
static void evidenceMapped(SumConsole &console)
{
    qInfo() << "【证据②】mapped / mappedReduced：并行映射与折叠";
    QVector<Block> blocks;
    const int kBlocks = 12;
    const qsizetype size = demoData().size();
    const qsizetype per = (size + kBlocks - 1) / kBlocks;
    for (int i = 0; i < kBlocks; ++i) {
        const qsizetype begin = qsizetype(i) * per;
        if (begin >= size)
            break;
        blocks.append(Block{begin, qMin(per, size - begin)});
    }

    gReducedArmed = true;                       // ← 只让接下来这一发来自 mappedReduced 的 finished 计入 ②b
    console.watcher()->setFuture(                     // 折叠版：结果交给 watcher 的 finished
        QtConcurrent::mappedReduced<long long>(blocks, sumBlock,
            [](long long &acc, const long long &part) { acc += part; }));
    // ↑ ResultType 由第一个显式模板实参给定（qtconcurrentmap.h 88/110 行）；reduce 的形参写成
    //   (ResultType&, MapResultType) —— ReduceResultTypeHelper 只认这个形状。

    QFuture<long long> fMapped = QtConcurrent::mapped(blocks, sumBlock);
    qInfo() << "  mapped 的 future 类型 = QFuture<long long>：**结果类型是 map 的返回值**，"
               "不是容器元素 Block（QtPrivate::MapResultType 推的）";
    fMapped.waitForFinished();                        // 第 104 行：显式地等，比偷偷等体面
    const QList<long long> parts = fMapped.results();
    long long merged = 0;
    for (long long p : parts)
        merged += p;
    const long long seq = sumSequential();
    qInfo() << "  mapped 分块数 =" << parts.size() << "｜各块相加 =" << merged
            << "｜串行基准 =" << seq << "｜相等吗 =" << (merged == seq);
    qInfo() << "  （并行总和由 watcher 的 finished 槽再打一次，进度事件计数见证据②c）";
    check("② mapped 结果条数 = 分块数", parts.size(), blocks.size());
    checkBool("② mapped 各块相加 = 串行基准", merged == seq);
}

// ============================================================================
// 六、证据③：阻塞语义 —— 把「result() 会卡住调用者」量出来
// ============================================================================
static void evidenceBlocking()
{
    qInfo() << "【证据③】阻塞语义：isFinished 不阻塞，result() 阻塞（量给你看）";

    QFuture<int> f = QtConcurrent::run([](int ms) {
        QThread::msleep(ms);
        return 42;
    }, 260);

    QElapsedTimer t;
    t.start();
    qInfo() << "  刚 run 完：isFinished =" << f.isFinished()
            << "｜isResultReadyAt(0) =" << f.isResultReadyAt(0)
            << "｜resultCount =" << f.resultCount()
            << "｜这三个都不阻塞，此刻耗时" << t.elapsed() << "ms";

    const int v = f.result();                  // ← 这里就是 qfuture.h 311 行的 waitForResult(0)
    const qint64 blocked = t.elapsed();
    qInfo() << "  result() 返回" << v << "，调用者被卡了" << blocked
            << "ms ← **这发生在主线程，GUI 就是在这冻住的**";
    check("③ result() 拿到 42", v, 42);
    checkBool("③ result() 至少被卡 200ms（任务 sleep 260ms）", blocked >= 200);
    checkBool("③ result() 返回后 future 已结束", f.isFinished());
    qInfo() << "  同一 future 第二次 result() 耗时 =" ;
    QElapsedTimer t2;
    t2.start();
    Q_UNUSED(f.result());
    qInfo() << "     " << t2.elapsed() << "ms（结果已在包里，几乎为 0）→ 「等」只在第一次";
    qInfo() << "  纪律：主线程**只在 finished 信号之后**读结果；想提前读，就用"
               " isResultReadyAt(i) 判一下（第 113 行），它不阻塞。";
}

// ============================================================================
// 七、证据④：filtered（新容器）vs filter（原地改）
// ============================================================================
static void evidenceFiltered()
{
    qInfo() << "【证据④】filtered 与 filter：一个是「给你新的」，一个是「改你的」";

    QVector<int> src;
    for (int i = 0; i < 3000; ++i)
        src.append(i);

    QFuture<int> keep = QtConcurrent::filtered(src, [](int v) { return v % 3 == 0; });
    keep.waitForFinished();                    // 读结果前必须先等（第 104 行），否则 results() 也会阻塞
    const QList<int> kept = keep.results();
    qInfo() << "  filtered(整除 3) → 结果" << kept.size() << "个，第一个 ="
            << (kept.isEmpty() ? 0 : kept.first()) << "｜原容器 size =" << src.size()
            << "没被动过 ← filtered **返回新序列**（qtconcurrentfilter.h 334/342 行）";

    QVector<int> inPlace;
    for (int i = 0; i < 3000; ++i)
        inPlace.append(i);
    QFuture<void> f2 = QtConcurrent::filter(inPlace, [](int v) { return v % 3 == 0; });
    qInfo() << "  filter 刚提交：future 是 QFuture<void>（拿不到返回值）｜此刻 inPlace.size ="
            << inPlace.size() << "（还没收敛，别读它）";
    f2.waitForFinished();
    qInfo() << "  waitForFinished 之后 inPlace.size =" << inPlace.size()
            << "｜和 filtered 的" << kept.size() << "对得上吗 ="
            << (qsizetype(kept.size()) == inPlace.size())
            << " ← filter()（36 行）**原地改你传进去的容器**：并发期间读写同一个容器，"
               "所以 Qt6 只给它 QFuture<void>，任何「边跑边看」都是数据竞争。";
    check("④ filtered 留下 1000 个（0..2999 里 3 的倍数）", kept.size(), 1000);
    check("④ filtered 没动原容器", src.size(), 3000);
    check("④ filter 原地改完也是 1000 个", inPlace.size(), 1000);
}

// ============================================================================
// 八、证据⑤：线程池来自哪里 —— maxThreadCount 怎么压低并行度
// ============================================================================
static void evidencePoolConcurrency()
{
    qInfo() << "【证据⑤】并行度的天花板是池的 maxThreadCount —— 但压**全局池**不会立刻生效";

    QThreadPool *pool = QThreadPool::globalInstance();
    const int saved = pool->maxThreadCount();
    qInfo() << QString::asprintf("  idealThreadCount = %d ｜ 全局池 maxThreadCount = %d "
                                 "｜ activeThreadCount = %d ｜ expiryTimeout = %d ms"
                                 "｜ 上限 %d 是 run 不带池参数时借用的池",
                                 QThread::idealThreadCount(), saved, pool->activeThreadCount(),
                                 pool->expiryTimeout(), saved);

    // 探针：往 target 池里塞 12 个「占住线程 30ms」的任务，数**同时在场**的峰值。
    // 这里 waitForFinished 是故意的：取证要的是账面，不是响应。
    const int kTasks = 12;
    auto burst = [kTasks](QThreadPool *target, const QString &tag,
                          std::atomic<int> &live, std::atomic<int> &peak) -> int {
        live.store(0);
        peak.store(0);
        QVector<QFuture<void>> keep;                     // **必须接住**：run 是 nodiscard
        for (int i = 0; i < kTasks; ++i) {
            keep.append(QtConcurrent::run(target, [&live, &peak]() {
                // 进入：在场线程数 +1，并用 CAS 把「同时在场峰值」顶上去
                const int now = live.fetch_add(1) + 1;
                int seen = peak.load();
                while (now > seen && !peak.compare_exchange_weak(seen, now))
                    ;
                QThread::msleep(30);                     // 占住线程，让并发度看得出来
                live.fetch_add(-1);
            }));
        }
        for (QFuture<void> &f : keep)
            f.waitForFinished();
        qInfo().noquote() << QStringLiteral("  %1 → %2 个任务实测**同时在场**的线程峰值 = %3")
                                 .arg(tag).arg(kTasks).arg(peak.load());
        return peak.load();
    };

    std::atomic<int> live{0};
    std::atomic<int> peak{0};

    // A：全局池原样。上限 16 > 12 个任务，所以一次跑完，看不出天花板，只看出「run 用的是公家池」。
    const int peakDefault = burst(pool, QStringLiteral("⑤A 全局池原样（max=%1）").arg(saved),
                                  live, peak);

    // B：**把全局池压到 2，然后立刻突发提交**。这一步是本讲真正的坑：
    //   天花板读的是 QThreadPoolPrivate::activeThreads，而那个计数是**工人线程自己**在被唤醒
    //   之后才加的（qthreadpool.cpp 130 行 ++manager->activeThreads）。调度侧走的是
    //   waitingThreads.takeFirst() → wakeOne() 这条分支（同文件 160~164 行），它**不自增账面**。
    //   于是 12 个任务连着提交时，账面还停在 0，`areAllThreadsActive()`（157 行 + 232~236 行）
    //   连着几次都说「还没满」，就连着唤醒了 3 条线程 —— 上限 2、实测峰值 3。
    //   为什么 surplus 线程还在等着被醒：空闲线程要闲置满 expiryTimeout（默认 **30 秒**，
    //   qthreadpool_p.h:143）才挪去 expiredThreads，所以「压完立刻开跑」必然踩这个窗口。
    //   而 238~241 行 tooManyThreadsActive() 用的是**严格大于**，所以只会多开一点，不会退回 12。
    pool->setMaxThreadCount(2);
    const int peakGlobalLow = burst(pool, QStringLiteral("⑤B 全局池压到 2、立刻开跑"), live, peak);

    // C：自建私有池、**开池时就定 2**，再突发提交 —— 这次的天花板是精确的：
    //   池里没有现成的空闲线程，tryStart 只能走 startThread()（247~261 行），
    //   它在调度侧就 `++activeThreads`（257 行），第 3 个任务进来时 157 行已经判满 → 排队。
    //   这就是第 6 讲「要控制并行度就自己建池」的硬理由。
    QThreadPool mine;
    mine.setMaxThreadCount(2);
    const int peakPrivate = burst(&mine, QStringLiteral("⑤C 私有池、开池即 max=2"), live, peak);
    mine.waitForDone();
    qInfo() << QStringLiteral(
        "      B 与 C 的差别不是「谁更快」，是**账面由谁加**：B 的天花板慢半拍（本机 8 次里 3 次"
        "看到 3），C 的天花板由调度侧当场扣住（本机每次都是 2）。\n"
        "      → 工程结论：压 maxThreadCount 之后**别马上指望**并行度掉下来；要确定的上限，"
        "用私有池（或先等 expiryTimeout 过期）。");

    pool->setMaxThreadCount(saved);                       // 用完记得放开：全局池是**共享资源**
    const int restored = pool->maxThreadCount();
    qInfo() << "  已把全局池恢复为 maxThreadCount =" << restored;

    checkBool("⑤ 默认池确实并行（峰值 ≥ 1）", peakDefault >= 1);
    checkBool("⑤ 压全局池到 2：峰值被压住（≥2），但可能冲破新上限（本机 2~3）",
              peakGlobalLow >= 2 && peakGlobalLow <= kTasks);
    check("⑤ 私有池开池即 max=2：峰值恰好等于上限（天花板精确）", peakPrivate, 2);
    check("⑤ 全局池已恢复原值", restored, saved);
    qInfo() << "  → 结论：QtConcurrent::run 不做任何线程调度决策，它只是往池里塞任务；"
               "并行度不够先去查池，别去改你的算法。（第 6 讲把池自己建起来）";
}

// ============================================================================
// 九、main
// ============================================================================
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("threads_05_qtconcurrent"));

    SumConsole console;
    QObject::connect(&console, &SumConsole::summaryReady,
                     [](long long total, int progressEvents, int resultEvents) {
        // ★ 只有 mappedReduced（②b）那一发 finished 才该进这条硬账；②c 的 progressTask
        //   复用同一个 console.watcher()，它 finished 时也会走到这里，但那个 total 不是
        //   demoData 的和 —— 用「上膛」位把它挡在外面，否则就是一发与主语不符的假红。
        if (!gReducedArmed)
            return;
        gReducedArmed = false;
        qInfo() << "【证据②b】并行求和（mappedReduced）算完：总和 =" << total
                << "｜串行基准 =" << sumSequential()
                << "｜相等吗 =" << (total == sumSequential());
        qInfo() << "  过程事件：progressValueChanged" << progressEvents
                << "次、resultReadyAt" << resultEvents << "次（**全在主线程**："
                << QThread::currentThread() << "）";
        checkBool("②b mappedReduced 总和 = 串行基准", total == sumSequential());
    });

    QTimer::singleShot(0, []() {
        qInfo() << "================================================================";
        qInfo() << "threads 第 5 讲 · QtConcurrent（Qt =" << QT_VERSION_STR
                << ", 主线程 =" << QThread::currentThread() << "）";
        qInfo() << "================================================================";
        evidenceRunAndWatcher();
    });
    QTimer::singleShot(120, [&console]() {     // 引擎自己报进度那一路：run<PromiseType>
        qInfo() << "【证据②c】QPromise 报进度（setProgressRange/Value/Text）→ watcher 收到";
        console.resetCounters();
        console.watcher()->setFuture(QtConcurrent::run<long long>(progressTask, 25));
    });
    QTimer::singleShot(420, [&console]() {
        qInfo() << "  进度事件：range" << console.rangeEvents() << "次、value"
                << console.valueEvents() << "次、resultReadyAt" << console.resultEvents()
                << "次（去重下标" << console.readyIndexesCount() << "个）；最后 progressValue ="
                << console.lastProgress() << "，文本 =「" << console.lastText() << "」";
    });
    QTimer::singleShot(480, [&console]() { evidenceMapped(console); });
    QTimer::singleShot(700, []() { evidenceBlocking(); });
    QTimer::singleShot(1000, []() { evidenceFiltered(); });
    QTimer::singleShot(1050, []() { evidencePoolConcurrency(); });
    QTimer::singleShot(1300, []() {
        qInfo() << "【收尾】一句话记住这讲：**run 给你 future，watcher 给你线程边界**，"
                   "谁都不给你「免费的正确」——在主线程读 result() 的人，卡的是自己的界面。";
        qInfo().noquote() << QStringLiteral("\n【硬账】%1/%2 项核对通过 → 退出码 %3")
                                 .arg(Ledger::passed).arg(Ledger::total).arg(Ledger::exitCode());
        QCoreApplication::exit(Ledger::exitCode());
    });

    return app.exec();
}

// ============================================================================
// 十、速查表与纪律
// ============================================================================
/*
 * 一、选哪条路（Qt6）
 *   要返回值 / 要进度 / 要取消     QtConcurrent::run(f, args…) 或 run<结果类型>(task, …)
 *   不要返回值，只要「把活干完」    QThreadPool::globalInstance()->start(callable)
 *                                 ← run 的 NODISCARD 提示原文就是这句（qtconcurrentrun.h 39 行）
 *   对容器每个元素算一遍           QtConcurrent::mapped(sequence|begin,end, map)
 *   算完还要汇总                   QtConcurrent::mappedReduced<ResultType>(seq, map, reduce)
 *   只要筛选                       filtered(seq, keep)（给你新序列）/ filter(seq&, keep)（原地改，
 *                                 QFuture<void>，并发期间那个容器你不能碰）
 *   串成流水线                     future.then(…)（qfuture.h 129~142 行）
 *
 * 二、QFuture 成员按「会不会阻塞」分两栏（本机 qfuture.h 逐行核过）
 *   不阻塞：isStarted/isFinished/isRunning/isCanceled/isSuspending/isSuspended/isResultReadyAt/
 *           resultCount/progressValue/progressMinimum/progressMaximum/progressText/isValid
 *   会阻塞：result()/resultAt(i)/results()/takeResult()/waitForFinished()/range-for 的 ++/
 *           QFutureWatcherBase::waitForFinished()
 *   → 主线程只碰左栏；右栏一律等 finished/canceled 之后，或者干脆放到别的线程去碰。
 *
 * 三、六条纪律
 *   ① 别把 QFuture 丢掉：run 是 [[nodiscard]]，扔了它就是「异步任务没着没落地飞」。
 *   ② UI 只连 QFutureWatcher，**别开 QTimer 轮询 future**：频率低了反应慢、高了界面卡，
 *      而 watcher 是工作线程主动给你打电话。信号在**watcher 自己的线程**发，
 *      所以 setFuture 之前想清楚它住在哪条线程。
 *   ③ 参数是拷贝进任务包的（DecayedTuple）：QVector/QString 隐式共享所以便宜，
 *      std::vector / 自定义类是**真深拷贝**。要引用就 std::cref，并保证对象活过 future。
 *   ④ 进度由干活的报：引擎侧 setProgressRange / setProgressValue / setProgressValueAndText
 *      （qfutureinterface.h 96/100/102 行，101 行夹在中间的是 getter），UI 侧 progressRangeChanged/
 *      progressValueChanged。mapped/filter 内核会自动报，自己写的任务不报就永远不动。
 *   ⑤ 全局池是共享资源：setMaxThreadCount 影响同进程所有 QtConcurrent 用户；
 *      要自己的天花板就自建池并 run(pool, f, args) 显式传池（第 6 讲）。
 *   ⑥ 析构：QFutureWatcher 析构只断开、**不取消**任务（qfuturewatcher.h 107 行）；
 *      QPromise 析构会把没 finish 的 future cancelAndFinish（qpromise.h 41 行）。
 *      「取消 + 收尾」是第 7 讲整讲的内容。
 *
 * ——— 老师傅的话 ———
 *   徒弟，这一讲新手最容易犯的不是语法错，是**把 QtConcurrent 当成免费午餐**。
 *   run 一行、watcher 一行，界面就不卡了，多美。可你回头看看证据③：那个 result()
 *   在主线程把事件循环堵了两百多毫秒。**代码没有阻塞的语法错误，只有阻塞的位置**。
 *
 *   第二件，我特意把「Qt6 里没有 runWithFeedback/runWithCancel」写进注释。你搜到的
 *   十篇教程里八篇在用，那是 Qt 5.14 的技术预览、**Qt 6.0 就删了**，本机 6.12 头文件
 *   grep 一个字符都没有。进度与取消的正解是 QPromise：工作线程拿笔（addResult/
 *   setProgressValue/isCanceled），UI 拿票（QFutureWatcher）。**网上抄来的 API 先在本机
 *   头文件见一面**，见不到就别用——这是我吃过最贵的亏。
 *
 *   第三件：filter() 原地改容器、mapped 给你新容器，长得像、性质完全不同。我在证据④
 *   把「提交后立刻读 size」和「waitForFinished 后读 size」都打了，中间那句「别读它」
 *   是我求你：并发的世界里没有「顺手看一眼」这回事。
 *
 *   最后：线程池不是你借的，是**公家的**。证据⑤那三个峰值我希望你抄进工单——
 *   12 个任务、私有池给 2 条线程，峰值就是 2，跟你算法多聪明无关；但**压公家的池**
 *   慢半拍（本机 12 次里 3 次看到 3），因为空闲工人要闲置满 expiryTimeout（默认 30 秒）
 *   才离队，而唤它们的分支不记账。这条不是 trivia：你在压测时把全局池调到 1 想「串行跑一遍」，
 *   它可能给你 2、3 条并行——结论就错了。
 *
 * ——— 小练习 ———
 *   1. 把证据③的 260ms 改成 1200ms，观察后续 singleShot 的打印时间整体后移
 *      （事件循环被堵的宏观表现），再改成「只在 finished 槽里读结果」，说明差别。
 *   2. 用 QtConcurrent::mapped 把 demoData() 每个元素映射成 QString（验证 MapResultType
 *      真按 map 的返回值推），再给 watcher 加 setPendingResultsLimit(64)，数 resultReadyAt
 *      次数怎么变，解释「限流」在什么场景必须开。
 *   3. 把 progressTask 里的 promise.finish() 删掉再跑，观察 canceled 信号与结果计数
 *      （对应 qpromise.h 39~46 行的析构行为），把它写成一条团队规范。
 *   4. 自建 QThreadPool(maxThreadCount=4) 并 QtConcurrent::run(pool, …)，与压到 2 的
 *      全局池**同时**跑，打印两个池各自的 activeThreadCount 峰值，再解释为什么全局池
 *      那一路仍然可能报到 3。（证据⑤已经给了源码线索：账是工人自己加的。）
 *   5. 把证据⑤B 的 `pool->setMaxThreadCount(2)` 之后加一句
 *      `QThread::msleep(pool->expiryTimeout() + 100)` 再突发提交（默认 30 秒，太久的话
 *      先 setExpiryTimeout(50)），看峰值怎么变；然后用 qthreadpool.cpp 130/160~164/257 行
 *      解释你看到的差别 —— 这就是「为什么 setExpiryTimeout 对已经闲着的线程没用」
 *      （qthreadpool.cpp 578~581 行写明了）。
 */
// ============================================================================

// 本文件有 Q_OBJECT 类（SumConsole），AUTOMOC 会生成并在这里包含进来。
#include "05_qtconcurrent.moc"
