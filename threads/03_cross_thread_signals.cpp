// ============================================================================
// 第 3 讲 · 跨线程信号：连接类型决定「槽跑在哪条线程」
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   第 1 讲你会了「把活丢进子线程」，第 2 讲你会了「对象归谁」。这两条线在本讲交汇：
//   **信号发射的那一刻，Qt 按连接类型决定这一趟是「当场函数调用」还是「投递一条事件」。**
//   这一个决定，就是 Qt 多线程 90% 的行为差异和 90% 的随机崩溃的来源。
//   本讲把它拆成八个可打印的事实：
//     1) `Qt::AutoConnection` 不是「自动挑一种更好的连接」，它只是**一条规则**：
//        发射那一刻 `QThread::currentThread() == 接收者->thread()` 就按 Direct 办，
//        否则按 Queued 办。所以同一个 connect()，裁决结果可以每次都不一样（【1】现场证明）。
//     2) `Qt::DirectConnection` 从子线程发：槽**就在子线程的调用栈上执行**。
//        这不是快，这是「把 UI 对象交到别的线程去碰」（【2】把两边的线程句柄同时打出来）。
//     3) `Qt::QueuedConnection`：发射方**立刻返回**，槽要等接收者那条线程的事件循环取到
//        事件才跑。「返回时槽还没跑」这一条本讲用两个原子计数当场称给你看（【3】）。
//     4) 参数是**按值拷贝进事件**的：connect 那一刻就把参数的 QMetaType 记下了
//        （本机 qobject.h 260~262 行：只有 Queued/BlockingQueued 才会去算 `types`）。
//        于是「自定义类型」要能跨线程搬运，得先过元类型这一关。Qt6 在这关上有个反直觉
//        的地方，本讲把它拆成两条都在本机验证过的事实：
//          · `QMetaType::fromType<T>()`（qmetatype.h 2680~2685 行）对**任何**完整又合适的
//            自定义类型都能编译过，哪怕你**没写** Q_DECLARE_METATYPE —— 因为 2643 行那个
//            `qMetaTypeInterfaceForType<T>()` 是**编译期**造接口，注释里明写
//            "don't check the type is suitable here"。所以拦你的不是编译器（本机实测）。
//          · 真正决定「能不能排队搬运」的是**运行期**才有的那个编号：
//            `isValid()`（2686~2689 行）只看 `d_ptr` 非空；`isRegistered()`（2691~2694 行）
//            看的是 `d_ptr->typeId.loadRelaxed()` —— 登记过才非零。队列连接要的是后者。
//            本机 `C:\msys64\ucrt64\bin\Qt6Core.dll` 里 grep 到两行原文（不是抄博客）：
//              "QObject::connect: Cannot queue arguments of type '%s'"
//              "(Make sure '%s' is registered using qRegisterMetaType().)"
//        → 一句话记法：**编译期给接口，运行期给编号；队列搬运要的是编号。**
//     5) `Qt::BlockingQueuedConnection`：发射方阻塞到槽在**接收者线程**跑完为止。
//        它是「跨线程拿返回值」的正解之一，也是「同线程用它 = 自己等自己」的死锁机器。
//        本讲只跑安全版，死锁那条留在注释里并说清为什么不敢跑（【5】）。
//     6) `QMetaObject::invokeMethod` 的两个世界：字符串版（Q_ARG 类型必须精确，
//        metaobject 模块第 8 讲已称过账）和 functor 版（qobjectdefs.h 441~452 行那一组
//        模板声明 + 454 行起的真实实现）。跨线程 + 要返回值时，返回值只能靠 BlockingQueued 或 future 拿。
//     7) **生命周期竞态**：接收者先死、事件还在路上 → Qt 在对象析构时摘掉连接，
//        那条排队事件到了也没人接（【7】用计数证明「发了但没收到」，而不是猜）。
//        lambda 版必须带 context 对象，否则捕获的 this 就是悬空指针。
//     8) 三个「查是谁叫我」的入口在跨线程时的可用性差别：`sender()`（qobject.h 367 行）、
//        `senderSignalIndex()`（368 行）、以及 functor 版里根本不该用它们（【8】）。
//   演示主线：一条 worker 线程（QThread + moveToThread，第 2 讲的规矩），一个主线程的
//   Hub，四类连接各发一轮，每轮都打印「发射时 currentThread」「槽里 currentThread」
//   「接收者->thread()」三个句柄和序号，最后给可核对的计数。全程 QTimer::singleShot
//   串步骤，末尾 QCoreApplication::quit()，无人值守能跑完。
//
// 对应构建目标名：threads_03_cross_thread_signals
//   find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)   # 仓库根 CMakeLists 就这么给
//   add_executable(threads_03_cross_thread_signals threads/03_cross_thread_signals.cpp)
//   target_link_libraries(threads_03_cross_thread_signals PRIVATE Qt6::Widgets Qt6::Concurrent)
//   set(CMAKE_AUTOMOC ON)                    # 本文件**有 Q_OBJECT 类**，必须要 moc
//   # 手工核对：moc 03_cross_thread_signals.cpp -o 03_cross_thread_signals.moc
//   #           文件末尾那句 #include "03_cross_thread_signals.moc" 就是 AUTOMOC 的钩子
//
// 运行前须知：
//   · 只建 **QCoreApplication**：本讲刻意不建任何 widget。理由和第 2 讲一样——
//     要把「线程裁决」讲纯，不能被 GUI 线程亲和那套规则叠上去干扰；GUI 那一条只在
//     【2】的注释里讲（真做 UI 的时候，【2】那种写法就是崩溃现场）。
//   · 全程无阻塞、无交互：QTimer::singleShot 串八个步骤，跑完 QCoreApplication::quit()；
//     worker 线程收尾用 requestInterruption + quit + wait 三件套（第 1 讲的规矩），
//     wait() 带 QDeadlineTimer，**绝不无期限等**。
//   · 【5】的 BlockingQueued 只在「跨线程」方向跑；「同线程用它自锁」那一条写在注释里，
//     因为一旦跑起来这个演示程序就永远停不下来，那不是证据，那是事故。
//   · 【4】的 Bare（没登记的那个对照物）会**真发一条**：预期在控制台看到上面那两行原文，
//     且 onBare 的计数停在 0。你要是嫌刷屏，把 pumpBare() 里那行 emit 注释掉就行 ——
//     警告跟着消失，而计数还是 0，这就是「登记与否」唯一的可见差别。
//   · 不读文件、不联网、不写注册表。
//   · 可核对的数（批大小 kBatch = 5，生命周期组 3 条）：
//     Direct 组槽执行数 == 5，且槽里的线程句柄 == worker 线程句柄；
//     Queued 组「发射方在 emit 返回那一刻读到的送达数」必须**小于**已发数（【3】逐条打）；
//     Pack 组 5 条全到，且槽里 payload 是**发射那一刻**的旧值（证明按值拷贝）；
//     Bare 组「发了 1 条、收到 0 条」；
//     【7】那一组两条连接各发 3 条：接收者还活着的收到 3 条，
//     接收者在同一槽里被 delete 的收到 0 条 —— 差值就是「事件还在路上，接收者先没了」。
//   · 本文件的校验状态（照实说）：moc + `g++ -fsyntax-only -std=c++17 -Wall -Wextra`
//     通过（0 error / 0 warning）；**未链接、未运行**（本机 Qt 运行时缺符号，起来就
//     0xC0000139）。所以文中所有「预期」都是推理，等你在能编译的机器上复核。
//     但下面这些**是实测**：本机头文件逐行核对（行号都是 6.12.0 的）；第三节那两条
//     「未登记类型的编译期/运行期分工」，编译部分是我用同一套 g++ 单独跑出来的，
//     警告文案是从本机 Qt6Core.dll 里 strings + grep 抠出来的原文。
//   · 本讲所有 API 都对着本机真实头文件核对过
//     （C:\msys64\ucrt64\include\qt6，Qt 6.12.0）：
//       QtCore/qnamespace.h      —— `enum ConnectionType` 1313~1320 行：
//                                    AutoConnection(=0) 1314 / DirectConnection 1315 /
//                                    QueuedConnection 1316 / BlockingQueuedConnection 1317 /
//                                    UniqueConnection = 0x80 1318 / SingleShotConnection = 0x100 1319。
//                                  ★ 注意**这里没有 Qt::ThreadedConnection**：我在整个
//                                    QtCore 头文件目录里 grep "ThreadedConnection" 零命中。
//                                    Qt5 博客里抄来的那一位，在本机这版已经不参与了。
//       QtCore/qobject.h         —— connect 三个「老式」重载 212 / 215 / 219 行；
//                                    函数指针版三个重载在 222~228 行是 Q_QDOC 文档声明，
//                                    真实模板实现从 231 行起（232 行那行就是带 context 的）；
//                                    253~258 行两个 static_assert：
//                                      "No Q_OBJECT in the class with the signal" /
//                                      "The slot requires more arguments than the signal provides."
//                                    260~262 行：只有 Queued / BlockingQueued 才去取
//                                      `QtPrivate::ConnectionTypes<...>::types()`（参数类型编号）；
//                                    disconnect 静态版 289~307 行（299 行收 const Connection&、
//                                      301 行收 Connection&、302 行收 &&）；
//                                    信号 destroyed 352 行、objectNameChanged 353 行；
//                                    slot deleteLater 364 行；
//                                    sender() 367 行、senderSignalIndex() 368 行。
//       QtCore/qobjectdefs.h     —— `class Connection;` 前向声明 236 行；
//                                    Q_ARG 宏 64 行、Q_RETURN_ARG 宏 65 行（都是
//                                      QtPrivate::Invoke::argument / returnArgument 的糖）；
//                                    qReturnArg<T>(T&) 229 行，而 228 行那条
//                                      `qReturnArg(const T &&) = delete` 明写：
//                                      返回值出口必须是个**左值**，`qReturnArg(std::move(x))`
//                                      当场编译不过 —— 这条是【6】里唯一编译器会拦你的地方。
//                                    invokeMethod 字符串版四个重载 324 / 338 / 355 / 372 行；
//                                    invokeMethod functor 版：439~452 行是 `#ifdef Q_QDOC`
//                                      的文档声明（441 行那个「type + ret 指针」的写法、
//                                      446/448 行那对 QTemplatedMetaMethodReturnArgument、
//                                      450/452 行那对「只传 functor + 参数」），
//                                      **真实实现在 453 行 #else 之后**，用 enable_if 按
//                                      「第二个实参能不能当 const char*」分流；
//                                    enum Call 567~576 行（IndexOfMethod /
//                                      RegisterMethodArgumentMetaType 等，metaobject 模块第 5 讲拆过）；
//                                    qobject_cast_helper 那对 static_assert 753~759 行。
//       QtCore/qmetatype.h       —— Q_DECLARE_METATYPE 1503 行；
//                                    qRegisterMetaType 三个版本：模板带名字版 1412~1421 行、
//                                      `inline constexpr int qRegisterMetaType()` 1436~1441 行、
//                                      收 QMetaType 的 1443~1446 行；qMetaTypeId<T>() 1423~1434 行。
//                                    ★ 把 1436~1441 行读一遍再下结论：那个无参模板版**只是
//                                      return qMetaTypeId<T>()**，也就是「把这类型的 id 摸出来」，
//                                      并不是往某张全局表里塞东西。Qt6 的登记时机在
//                                      QMetaType::fromType<T>() 那一侧。带 typeName 的 1412 行版
//                                      才会真的按**名字**登记（按名字跨线程/跨模块才需要它）。
//                                    【4】还要读六处（都是本机行号）：
//                                      378  行 checkTypeIsSuitableForMetaType<T>() ——
//                                             「这类型配不配当元类型」的编译期判据；
//                                      1503 行 Q_DECLARE_METATYPE 只是转发，正文在 1504 行的
//                                             Q_DECLARE_METATYPE_IMPL，它给 QMetaTypeId<TYPE>
//                                             填的正好是两样东西：1509 行 `enum { Defined = 1 }`
//                                             （编译期开关）+ 1511~1516 行 qt_metatype_id() 里
//                                             那个 `static QBasicAtomicInt metatype_id`
//                                             （**运行期**才申请的编号）。这两样就是「登记」的
//                                             全部含义，也是【4】那个对照实验的分界线。
//                                      2643 行 qMetaTypeInterfaceForType<T>()（注释原话
//                                             "don't check the type is suitable here"）、
//                                      2680~2685 行 fromType<T>()、
//                                      2686~2689 行 isValid()（只看 d_ptr 非空）、
//                                      2691~2694 行 isRegistered()（看 typeId 非零）。
//       （运行期文案核对）        —— C:\msys64\ucrt64\bin\Qt6Core.dll：strings + grep 命中两行
//                                    "QObject::connect: Cannot queue arguments of type '%s'" 与
//                                    "(Make sure '%s' is registered using qRegisterMetaType().)"
//                                    —— 它们**确实还在这版 Qt 里**。所以【4】的 Bare 那一组预期
//                                    就是这两行；文案照 DLL 原文抄，一个字没改。
//       QtCore/qatomic.h         —— QAtomicInteger<T> 17~18 行（继承 QBasicAtomicInteger）、
//                                    拷贝构造 24~28 行、拷贝赋值 30~34 行（两处体都是
//                                    storeRelease(other.loadAcquire())，即「快照拷贝」，
//                                    而且 21 行的注释明写 Non-atomic API）、
//                                    QAtomicInt 113 行、constexpr 构造 119 行。
//       QtCore/qbasicatomic.h    —— 24 行 QBasicAtomicInteger、51 loadAcquire、
//                                    52 storeRelease、53 `operator T()`、54 `operator=`、
//                                    72/81 testAndSetOrdered、93 fetchAndStoreOrdered、
//                                    105 fetchAndAddOrdered、145/147 两个 ++，
//                                    以及 167~169 **基类的拷贝是被 delete 的**
//                                    （能拷贝的是上层 QAtomicInteger 那个「非原子 API」）。
//       QtCore/qthread.h         —— currentThreadId() 32 行 / currentThread() 33 行 /
//                                    isMainThread() 34 行 / idealThreadCount() 35 行 /
//                                    loopLevel() 78 行 / isCurrentThread() 80 行 /
//                                    信号 started 109 行、finished 110 行。
//       QtCore/qcoreapplication.h—— exec() 108 行、postEvent() 114 行、quit() 205 行。
//       QtCore/6.12.0/QtCore/private/qobject_p.h
//                              —— 307~309 行和 qobject.h 260~262 行同一套判断（私有头，
//                                 只是让你看清「types 只在队列类连接里取」不是巧合）。
// ============================================================================

#include <QCoreApplication>
#include <QAtomicInt>            // 注意：Qt6 里它**不是** std::atomic 的别名，见第二节括号里那段
#include <QDebug>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QMetaMethod>           // mo->method(idx).name() 要用它（qobjectdefs_impl.h 23 行只是前向声明）
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QVariant>

// ============================================================================
// 一、先把「句柄怎么打印」这件事定了（不然后面每一步都在拼字符串）
// ----------------------------------------------------------------------------
// QThread::currentThreadId() 返回 Qt::HANDLE（qthread.h 32 行），它是个 void*。
// 打印它只有两条诚实的路：转成 quintptr 打**数字**，或者打上「主线程 / worker」这种
// 我自己贴的标签。本讲**两个都打**，因为「标签是我给的，数字是 Qt 给的」——
// 只有数字才能证明两趟执行真的不在同一条线程上。
// ============================================================================
static QString tagOf(const QThread *t)
{
    if (t == nullptr)
        return QStringLiteral("(null)");
    QString name = t->objectName().isEmpty() ? QStringLiteral("<无名线程>") : t->objectName();
    if (t == QThread::currentThread())
        name += QStringLiteral("·当前");          // 「就是我」的标记，和数字互相印证
    return name;
}

static QString handleText()          // 当前线程：标签 + 数字，一次打全
{
    return QStringLiteral("%1[%2]")
            .arg(tagOf(QThread::currentThread()))
            .arg(quintptr(QThread::currentThreadId()), 0, 16);
}

// ============================================================================
// 二、六组「必须原子」的计数：因为它们要被**另一条线程**读
// ----------------------------------------------------------------------------
// 本讲的证据全都是「一条线程写、另一条线程读」的计数器，所以先把工具用对，
// 才谈得上把结论说硬：
//   · Hub 里那些普通 int（directHits() 等）**只在两条线程都停下来之后**才读
//     （见 main 末尾的总结），那时不存在竞态，用 int 是诚实且够用的；
//   · 但下面这几个是「worker 正在跑的当口，主线程就要读」或反过来 ——
//     这就是第 4 讲要展开的 data race 的最小形状。这里先用对工具，第 4 讲再讲
//     「为什么用错了会怎样」。
// （QAtomicInt 在 Qt6 里**不是** std::atomic 的别名，别按 Qt5 博客的说法记：本机
//   qatomic.h 113 行 `class QAtomicInt : public QAtomicInteger<int>`，
//   而 QAtomicInteger（qatomic.h 17~18 行）又继承 QBasicAtomicInteger（qbasicatomic.h
//   24 行）—— 51 行 loadAcquire、105 行 fetchAndAddOrdered、145 行起的 ++ 都是那一层
//   给的。它同时**有拷贝构造和拷贝赋值**（qatomic.h 24~34 行，体是
//   `storeRelease(other.loadAcquire())`），注释里写着「Non-atomic API」：
//   拷进去的是**一份快照**，不是同一个计数。这一点第 4 讲专门辟一节讲，先记住
//   「跨线程共享一律传指针/引用，别按值」。）
// 为什么这一节放在类定义**之前**：Worker 的槽和 Hub 的槽要往同一组计数里写，
// C++ 只看声明顺序 —— 顺序错了就是「未声明的标识符」，编译器当场教你做人。
// ============================================================================
static QAtomicInt gQueuedDelivered;      // 【3】Queued 组的槽真的跑了几次（发射方要实时读）
static QAtomicInt gEmitReturnedFirst;    // 【3】「emit 返回时槽还没跑」命中了几次
static QAtomicInt gBlockingDelivered;    // 【5】BlockingQueued 的槽跑了几次（发射方前后对比）
static QAtomicInt gBareDelivered;        // 【4】未登记类型：预期恒为 0
static QAtomicInt gDoomedDelivered;      // 【7】接收者被提前 delete 的那一条
static QAtomicInt gSurvivorDelivered;    // 【7】对照组：同样三条，接收者没被 delete

// ============================================================================
// 三、要跨线程搬运的自定义类型：登记 + 一个「故意不登记」的对照物
// ----------------------------------------------------------------------------
// Qt6 的规则（对着 qmetatype.h 读出来的，不是背来的）：
//   · 类型要能进 QVariant / 能按值拷贝进事件，得先有 QMetaType；
//   · `Q_DECLARE_METATYPE(T)`（1503 行转发到 1504 行的 IMPL）就是把 T 挂进
//     QMetaTypeId 那套 traits：1509 行的 `Defined = 1` 是编译期开关，
//     1511~1516 行那个 `static QBasicAtomicInt metatype_id` 是**运行期**才申请的编号；
//   · 需要**按名字**被别的地方（QML、QtDBus、字符串版 invokeMethod 的 Q_ARG）认出来时，
//     才用带 typeName 的 qRegisterMetaType<T>("名字")（1412 行）。
// 下面 Pack 是正例；Bare 是对照物：**没有** Q_DECLARE_METATYPE。
// 本机实测（同一套 g++ -fsyntax-only）两件反直觉的事，都通过了：
//   ① 把 Bare 当参数 connect 起来（哪怕显式 Qt::QueuedConnection）编译**完全通过**，
//      一个警告都没有；
//   ② 连 `QMetaType::fromType<Bare>()`、`qMetaTypeId<Bare>()` 都**照样编译通过**
//      —— 因为 2643 行那个 `qMetaTypeInterfaceForType<T>()` 对任何完整又合适的类型
//      都能在编译期造出接口（注释原话 "don't check the type is suitable here"）。
// → 结论要记牢：**编译器一句都不吭声**。拦你的是运行期，而且拦的方式是「打一条
//   qWarning 然后把这次投递丢掉」，不是崩。本讲【4】把 Bare 真发一条，让你看见那两行
//   原文（文案是从本机 Qt6Core.dll 里 grep 的，不是抄博客）：
//     QObject::connect: Cannot queue arguments of type 'Bare'
//     (Make sure 'Bare' is registered using qRegisterMetaType().)
//   同时把 isValid() / isRegistered() 两个口径对打出来 —— 一个是编译期给的，
//   一个是运行期给的，这条分界线就是本讲最想让你带走的东西。
// ============================================================================
struct Pack
{
    int seq = 0;
    QString payload;
};
Q_DECLARE_METATYPE(Pack)             // qmetatype.h 1503 行

struct Bare                            // 故意不登记：对照物
{
    int seq = 0;
};

// ============================================================================
// 四、worker：住在子线程里的那个「干活的人」
// ----------------------------------------------------------------------------
// 第 2 讲的规矩全部沿用：对象在**主线程** new，随后 moveToThread(workerThread)，
// 于是它的 thread() 变成 worker 线程；它的**槽**都在 worker 线程的事件循环里执行，
// 而它**发信号**的那一刻，currentThread() 就是谁在调这个 emit —— 不一定是 worker。
// 本讲关心的不是它干什么活，而是它发信号那一刻，连接把槽送到了哪条线程上。
// ============================================================================
class Worker : public QObject
{
    Q_OBJECT
public:
    explicit Worker(QObject *parent = nullptr) : QObject(parent) {}

    // ★ 信号按「这一组要演示哪种连接类型」拆开，一个信号只干一件事。
    //   这不是啰嗦，是实验设计的**控制变量**：如果把 Direct / Queued / BlockingQueued
    //   三种连接挂在**同一个**信号上，一次 emit 就同时走三条，其中 BlockingQueued 那条
    //   会把发射线程卡住到主线程跑完槽为止 —— 【3】要测的「emit 返回时槽还没跑」
    //   当场被污染，计数器全乱了还怎么当证据。老代码里那种「一个信号什么都能连」的
    //   写法，出事的时候你连是哪条连接干的都不知道。
    Q_SIGNAL void sigDirect(int n);       // 【2】只挂 Direct
    Q_SIGNAL void sigQueued(int n);       // 【3】只挂 Queued（「先返回」实验的干净台面）
    Q_SIGNAL void sigBlocking(int n);     // 【5】只挂 BlockingQueued
    Q_SIGNAL void sigAuto(int n);         // 【1】+【8】只挂 Auto：同一条连接，两种裁决
    Q_SIGNAL void sigPack(const Pack &p); // 【4】挂 Queued，验「按值拷贝」
    Q_SIGNAL void sigBare(const Bare &b); // 【4】挂 Queued，未登记的对照物
    Q_SIGNAL void sigDoomed(int n);       // 【7】挂两条 Queued：接收者一个活着、一个被提前 delete
    Q_SIGNAL void acked(int total);       // 反方向（worker → 主线程）的回执

    // 一个「需要返回值」的活：主线程想要结果，但对象住在另一条线程。
    // 这条就是【5】BlockingQueued 和【6】invokeMethod 返回值那条路的题目。
    Q_INVOKABLE int compute(int seed) const { return seed * 3 + 1; }
    // 故意留一个**没标 Q_INVOKABLE** 的同签名方法给【6】当反面教材：
    // 字符串版 invokeMethod 查不到它 → 预期返回 false（外加一条 qWarning）。
    // metaobject 模块第 8 讲称过的账在这里再称一次：moc 只登记 Q_INVOKABLE / 槽，普通成员函数不在表上。
    int uninvokable(int seed) const { return seed * 3 + 1; }

    void setDoomedTarget(QObject *o) { m_doomed = o; }   // 【7】把要提前拆掉的接收者交给它

public Q_SLOTS:
    // ── 发动机组 ────────────────────────────────────────────────────────────
    // 这些槽全部跑在 worker 线程（第 2 讲的 moveToThread 规矩）。
    // 于是「它们里面每一次 Q_EMIT 时，currentThread() 是 worker」这件事是**结构性**的，
    // 不需要靠运气 —— 这就是本讲所有观察的发射侧。
    void pumpDirect(int n)
    {
        qInfo().noquote() << QStringLiteral("    ▶ worker 侧发 %1 条 sigDirect｜发射线程 = %2")
                                 .arg(n).arg(handleText());
        for (int i = 1; i <= n; ++i) {
            Q_EMIT sigDirect(i);
            // 走到这一行时，槽**已经**在本线程的调用栈上跑完了 —— 日志里
            // 「[槽 onDirect] n=i」一定出现在这行**之前**，顺序就是证据。
            qInfo().noquote() << QStringLiteral("      emit sigDirect(%1) 返回（槽早跑完了）").arg(i);
        }
    }
    void pumpQueued(int n)
    {
        qInfo().noquote() << QStringLiteral("    ▶ worker 侧发 %1 条 sigQueued｜连接是 Qt::QueuedConnection")
                                 .arg(n);
        for (int i = 1; i <= n; ++i) {
            Q_EMIT sigQueued(i);
            const int delivered = gQueuedDelivered.loadAcquire();
            if (delivered < i)
                gEmitReturnedFirst.fetchAndAddOrdered(1);
            qInfo().noquote() << QStringLiteral("      emit sigQueued(%1) 刚返回：已发 %2 条，"
                                                "可 Hub 报上来的送达只有 %3 条 → %4")
                                     .arg(i).arg(i).arg(delivered)
                                     .arg(delivered < i
                                              ? QStringLiteral("槽还没跑，参数正躺在主线程的事件队列里")
                                              : QStringLiteral("槽抢在这之前跑了（主线程正好被唤醒）"));
        }
        qInfo().noquote() << QStringLiteral("    ▶ 本轮「emit 先返回」命中 %1 次 / 共 %2 条")
                                 .arg(gEmitReturnedFirst.loadAcquire()).arg(n);
    }
    void pumpBlocking(int n)
    {
        const int before = gBlockingDelivered.loadAcquire();
        qInfo().noquote() << QStringLiteral("    ▶ 发射前 gBlockingDelivered = %1｜发射线程 = %2")
                                 .arg(before).arg(handleText());
        Q_EMIT sigBlocking(n);        // ← 这一行**卡住**，直到主线程把槽跑完才返回
        qInfo().noquote() << QStringLiteral("      emit sigBlocking(%1) 现在才返回："
                                            "gBlockingDelivered 从 %2 变成 %3 → 槽一定跑完了")
                                 .arg(n).arg(before).arg(gBlockingDelivered.loadAcquire());
    }
    void pumpAuto(int n)
    {
        qInfo().noquote() << QStringLiteral("    ▶ 同一条 Auto 连接，这次在 %1 发")
                                 .arg(handleText());
        Q_EMIT sigAuto(n);
    }
    void pumpPack(int n)
    {
        for (int i = 1; i <= n; ++i) {
            Pack p;
            p.seq = i;
            p.payload = QStringLiteral("发射时写的第%1份").arg(i);
            qInfo().noquote() << QStringLiteral("      发射前：本地 Pack 地址=%1 payload=[%2]")
                                     .arg(quintptr(&p), 0, 16).arg(p.payload);
            Q_EMIT sigPack(p);
            p.payload = QStringLiteral("★发射之后我在本地把它改了");   // ← 关键动作
            qInfo().noquote() << QStringLiteral("      发射后：同一个地址 %1 上现在是 [%2]")
                                     .arg(quintptr(&p), 0, 16).arg(p.payload);
        }
    }
    void pumpBare()
    {
        Bare b;
        b.seq = 907;
        qInfo().noquote() << QStringLiteral("      发一条 sigBare（参数类型 Bare **没登记**）｜线程 %1")
                                 .arg(handleText());
        Q_EMIT sigBare(b);
        // 预期：控制台出现第三节抄来的那两行原文，且 gBareDelivered 一直是 0。
    }
    void runLifecycleProbe()
    {
        for (int i = 1; i <= 3; ++i)
            Q_EMIT sigDoomed(i);
        qInfo().noquote() << QStringLiteral("    ▶ 发完 3 条：此刻 doomed 送达 %1、survivor 送达 %2"
                                            "（都还是 0 —— 两条都还排在 worker 线程的事件队列里）")
                                 .arg(gDoomedDelivered.loadAcquire())
                                 .arg(gSurvivorDelivered.loadAcquire());
        if (m_doomed != nullptr) {
            qInfo() << "    ▶ 现在在事件被处理**之前**把 doomed 这个接收者 delete 掉"
                       "（本线程 delete，符合第 2 讲；这里要的是「提前没」这一件事）";
            delete m_doomed;                 // ← Qt 摘掉它的全部连接 + 丢弃已排到它身上的事件
            m_doomed = nullptr;
        }
        qInfo().noquote() << QStringLiteral("    ▶ delete 之后立刻读：doomed 送达 %1")
                                 .arg(gDoomedDelivered.loadAcquire());
    }

    // ── 反方向：主线程发过来的活 ─────────────────────────────────────────────
    void handleFromHub(int n)
    {
        const int total = m_hubHits.fetchAndAddOrdered(1) + 1;
        QObject *s = sender();                        // 【8】：成员槽里才拿得到（367 行是 protected）
        qInfo().noquote() << QStringLiteral("      [槽 handleFromHub] 在 %1 执行  n=%2  累计=%3"
                                            "  sender()=%4")
                                 .arg(handleText()).arg(n).arg(total)
                                 .arg(s == nullptr
                                          ? QStringLiteral("空")
                                          : QStringLiteral("有，类名=%1")
                                                .arg(QString::fromLatin1(
                                                      s->metaObject()->className())));
        // ★ 这里对 sender() 只做一件事：读它的 metaObject() 那张**静态表**（虚函数返回的是
        //   编译期常量指针）。跨线程敢做的只有这种读法；要读它的**成员数据**，就是第 2 讲
        //   骂过的那种 bug。这条界线请刻在手上。
        Q_EMIT acked(total);
    }
    int  hubHits() const { return m_hubHits.loadAcquire(); }

private:
    QAtomicInt m_hubHits;              // 反方向计数的载体（本讲不是讲原子性，够用就行）
    QObject *m_doomed = nullptr;       // 【7】那个会被提前拆掉的接收者
};

// ============================================================================
// 五、Hub：住在主线程的接收者
// ----------------------------------------------------------------------------
// 它的每个槽都做同一件事：把「我此刻在哪条线程」打进日志。
// 于是「谁在什么线程执行了我的代码」这件事，从猜测变成一行可打印的证据。
// ============================================================================
class Hub : public QObject
{
    Q_OBJECT
public:
    explicit Hub(QObject *parent = nullptr) : QObject(parent) {}

    int directHits() const { return m_direct; }
    int queuedHits() const { return m_queued; }
    int blockingHits() const { return m_blocking; }
    int packHits() const { return m_pack; }
    int seqSum() const { return m_seqSum; }
    quintptr lastThreadHandle() const { return m_lastHandle; }

    // 让主线程这边也能主动发信号给 worker（反方向）
    Q_SIGNAL void pokeWorker(int n);

public Q_SLOTS:
    // —— Direct 组的槽 ——
    void onDirect(int n)
    {
        ++m_direct;
        m_seqSum += n;
        m_lastHandle = quintptr(QThread::currentThreadId());
        qInfo().noquote() << QStringLiteral("      [槽 onDirect ] 在 %1 执行  n=%2"
                                            "  此刻 Hub->thread()=%3")
                                 .arg(handleText())
                                 .arg(n)
                                 .arg(tagOf(this->thread()));
    }
    // —— Queued 组的槽 ——
    void onQueued(int n)
    {
        ++m_queued;
        m_seqSum += n;
        gQueuedDelivered.fetchAndAddOrdered(1);      // 发射方【3】就是靠这个实时读数做对比
        qInfo().noquote() << QStringLiteral("      [槽 onQueued ] 在 %1 执行  n=%2  累计送达=%3")
                                 .arg(handleText()).arg(n).arg(gQueuedDelivered.loadAcquire());
    }
    // —— BlockingQueued 组的槽（跑在 Hub 所属线程，即主线程）——
    void onBlocking(int n)
    {
        ++m_blocking;
        m_seqSum += n;
        gBlockingDelivered.fetchAndAddOrdered(1);    // 发射方【5】在它返回后才能看到这个 +1
        qInfo().noquote() << QStringLiteral("      [槽 onBlocking] 在 %1 执行  n=%2"
                                            "  ← 发射方此刻正卡在 emit 那一行")
                                 .arg(handleText()).arg(n);
    }
    // —— 自定义类型组：参数是 Pack（已登记），验证「按值拷贝」 ——
    void onPack(const Pack &p)
    {
        ++m_pack;
        qInfo().noquote() << QStringLiteral("      [槽 onPack   ] 在 %1 执行  seq=%2 payload=[%3]"
                                            "  收到的对象地址=%4")
                                 .arg(handleText()).arg(p.seq).arg(p.payload)
                                 .arg(quintptr(&p), 0, 16);
        // 要看的两件事：地址**不等于**发射方那个地址（事件里另有一份），
        // 而且 payload 还是「发射时写的那份」—— 发射方后面改成什么都不关它的事。
    }
    // —— 【4】的对照组：参数类型 Bare **没登记** ——
    void onBare(const Bare &b)
    {
        ++m_bare;
        gBareDelivered.fetchAndAddOrdered(1);
        qInfo().noquote() << QStringLiteral("      [槽 onBare   ] 在 %1 执行  seq=%2"
                                            "  ← 这条**预期永远不出现**")
                                 .arg(handleText()).arg(b.seq);
    }
    int  bareHits() const { return m_bare; }

    // —— 反方向的回执（worker 的 acked → 这里，走 Auto：跨线程 → 排队）——
    void onAcked(int total)
    {
        ++m_acked;
        qInfo().noquote() << QStringLiteral("      [槽 onAcked  ] 在 %1 执行  worker 累计干了 %2 趟")
                                 .arg(handleText()).arg(total);
    }
    int ackedHits() const { return m_acked; }

    // —— 【8】「是谁叫我」组 ——
    // 为什么这一组写成**成员槽**而不是 lambda：sender() 和 senderSignalIndex() 在
    // qobject.h 里是 **protected**（本机 367 / 368 行），只有 QObject 的子类的成员函数
    // 才拿得到它们；lambda 里没有 this，就进不了这个门径。
    // 「想知道是谁叫我」的诚实做法是回到成员槽 —— 而不是在 lambda 里绕路抓 this，
    // 那一绕正好撞上【7】讲的悬空接收者。
    void onProbe(int n)
    {
        ++m_probe;
        QObject *s = sender();                        // qobject.h 367 行
        const int idx = senderSignalIndex();          //        368 行
        const QMetaObject *mo = (s != nullptr) ? s->metaObject() : nullptr;
        QString resolution = QStringLiteral("(拿不到发送者)");
        if (mo != nullptr) {
            // 判越界再取值 —— metaobject 模块第 8 讲的规矩在这儿同样生效：查不到给 -1，
            // 而 method(int) **不做越界检查**。
            resolution = QStringLiteral("method(%1)=[%2]  methodOffset=%3  methodCount=%4")
                             .arg(idx)
                             .arg(idx >= 0 && idx < mo->methodCount()
                                      ? QString::fromLatin1(mo->method(idx).name())
                                      : QStringLiteral("(越界，不取)"))
                             .arg(mo->methodOffset())
                             .arg(mo->methodCount());
        }
        qInfo().noquote() << QStringLiteral("      [槽 onProbe   ] 在 %1 执行  n=%2"
                                            "  sender()=%3  senderSignalIndex()=%4")
                                 .arg(handleText()).arg(n)
                                 .arg(s == nullptr
                                          ? QStringLiteral("空")
                                          : QStringLiteral("有（就是 worker 吗：%1）")
                                                .arg(s == m_expectedSender
                                                         ? QStringLiteral("是")
                                                         : QStringLiteral("否")))
                                 .arg(idx);
        qInfo().noquote() << QStringLiteral("                          按绝对索引解：%1")
                                 .arg(resolution);
        // ★ 这个索引到底是「本地号」还是「绝对号」，本机跑不了 Qt，我不替你猜。
        //   你上手跑一次，拿 methodOffset 一比就知道：若 idx >= methodOffset 且 method(idx)
        //   打出的正是 sigAuto，那是**绝对**口径；若 idx 是个个位数且 method(idx+methodOffset)
        //   才对上，那是**本地**口径。两种都得判越界 —— 这一句才是本段真正要练的肌肉。
    }
    void setExpectedSender(QObject *o) { m_expectedSender = o; }
    int  probeHits() const { return m_probe; }

private:
    int m_direct = 0;
    int m_queued = 0;
    int m_blocking = 0;
    int m_pack = 0;
    int m_bare = 0;
    int m_acked = 0;
    int m_probe = 0;
    int m_seqSum = 0;
    QObject *m_expectedSender = nullptr;
    quintptr m_lastHandle = 0;
};

// ============================================================================
// 六、【7】专用的接收者：形状极简，只为把「接收者先没」这一件事孤立出来
// ----------------------------------------------------------------------------
// 两条一模一样的连接（同一个信号、同一种 Qt::QueuedConnection、同一个槽），
// 唯一的区别是其中一条的接收者**在事件还在路上的时候被 delete 了**。
// 于是「送达数」的差值就是结论，不用猜，也不用靠崩溃来证明。
//
// 为什么它要住在 worker 线程里：只有「发射 → 立刻 delete」发生在**同一条线程的
// 同一个槽调用里**，才能保证 delete 一定夹在「事件已投递」和「事件被处理」中间。
// 换到主线程去做反而不确定 —— 主线程一回到事件循环就先处理队列，顺序拼不过去。
// 而这里的 delete 由**对象所属线程自己**执行，正好是第 2 讲教的正路：本讲要演示的
// 是「事件在路上、接收者没了」，不是「跨线程 delete」，别把两件事搅在一起。
// ============================================================================
class Sink : public QObject
{
    Q_OBJECT
public:
    Sink(const QString &label, QAtomicInt *counter, QObject *parent = nullptr)
        : QObject(parent), m_label(label), m_counter(counter)
    {
    }

public Q_SLOTS:
    void got(int n)
    {
        const int total = m_counter->fetchAndAddOrdered(1) + 1;
        qInfo().noquote() << QStringLiteral("        · Sink[%1] 收到 n=%2  累计=%3  在 %4 执行")
                                 .arg(m_label).arg(n).arg(total).arg(handleText());
    }

private:
    QString m_label;
    QAtomicInt *m_counter = nullptr;     // 指向上面第二节那两个原子计数
};

// ============================================================================
// ── 【5】为什么「同线程 BlockingQueued = 自己等自己」只写在注释里 ──────────
// ----------------------------------------------------------------------------
//   // 主线程发，接收者**也**在主线程，可连接被硬写成 BlockingQueued：
//   connect(&someObj, &Some::sig, &hub, &Hub::onBlocking, Qt::BlockingQueuedConnection);
//   Q_EMIT someObj.sig(1);              // ← 程序停在这一行，永远
//   形状是这样：投递事件、然后**等接收者那条线程的事件循环把它取走**。可发射方就是
//   主线程，而主线程卡在 emit 里就回不到事件循环 —— 等的人正是被等的人。
//   它不崩、不报错、CPU 占用 0%，只能靠调试器进去看，是典型的「本机偶发无响应」工单。
//   我不在「跑完自己退出」的教学示例里放这段：读者一改顺序就先怀疑自己的机器。
//   正路两条：①同线程要返回值就**直接调用**那个函数（连信号槽都别绕）；
//             ②确实跨线程、且接收者线程**在跑事件循环**（loopLevel()>0，第 1 讲那个坑），
//               才轮到 BlockingQueued。本讲【5】跑的就是②这个唯一安全的形状。
// ============================================================================

// ============================================================================
// 七、main：把八个观察排成一串 QTimer::singleShot
// ----------------------------------------------------------------------------
// 为什么用定时器把步骤排队，而不是「一路写下来 + msleep」：
//   ① 本讲观察的对象**就是事件循环**。你用 msleep 把主线程睡住，所有排队给主线程的
//      槽只能等你睡醒 —— 测出来的是 sleep 的形状，不是连接的形状。
//   ② 全程无人值守：跑完自己 quit()，不弹任何框（这个仓库的规矩）。
//   ③ 步骤之间留 120ms，只是给「上一步排出去的事件」留出被处理的时间。
//      这个间隔不是结论的一部分；结论靠每一步打印里的**顺序**和**计数**。
// ============================================================================
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);        // 本讲一个 widget 都不建，见「运行前须知」

    QElapsedTimer clock;
    clock.start();

    // ── 给两条线程都起个名字，日志才读得懂（句柄数字负责证明，名字负责让你对号）──
    QThread *mainThread = QThread::currentThread();
    mainThread->setObjectName(QStringLiteral("main"));
    const quintptr mainHandle = quintptr(QThread::currentThreadId());

    QThread workerThread;
    workerThread.setObjectName(QStringLiteral("worker"));

    Worker worker;                     // 在**主线程**创建 → 一开始归属主线程（第 2 讲第一课）
    worker.setObjectName(QStringLiteral("worker-object"));
    Hub hub;
    hub.setObjectName(QStringLiteral("hub"));

    const bool moved = worker.moveToThread(&workerThread);   // 6.12 这版返回 bool（147 行）
    qInfo().noquote() << QStringLiteral("═══ 第 3 讲 · 跨线程信号 ═══");
    qInfo().noquote() << QStringLiteral("  moveToThread 返回 = %1 ｜ worker->thread() = %2"
                                        " ｜ hub->thread() = %3")
                             .arg(moved ? QStringLiteral("true（挪成了）")
                                        : QStringLiteral("false（挪不动！回去查有没有 parent）"))
                             .arg(tagOf(worker.thread())).arg(tagOf(hub.thread()));

    // ── 元类型：编译期给的 vs 运行期给的（第三节那两条判据落地成一次对打）──────
    {
        const QMetaType mtPack = QMetaType::fromType<Pack>();   // 已 Q_DECLARE_METATYPE
        const QMetaType mtBare = QMetaType::fromType<Bare>();   // **没**登记：这行照样编译过
        qInfo().noquote() << QStringLiteral("  元类型对照（fromType<T>() 两个都编译得过）：");
        qInfo().noquote() << QStringLiteral("    Pack → isValid=%1 isRegistered=%2 sizeOf=%3 名=%4")
                                 .arg(mtPack.isValid()).arg(mtPack.isRegistered())
                                 .arg(mtPack.sizeOf())
                                 .arg(QString::fromLatin1(mtPack.name()));
        qInfo().noquote() << QStringLiteral("    Bare → isValid=%1 isRegistered=%2 sizeOf=%3 名=%4")
                                 .arg(mtBare.isValid()).arg(mtBare.isRegistered())
                                 .arg(mtBare.sizeOf())
                                 .arg(QString::fromLatin1(mtBare.name()));
        const int namedId = qRegisterMetaType<Pack>("Pack");    // 1412 行：按**名字**登记
        qInfo().noquote() << QStringLiteral("  qRegisterMetaType<Pack>(\"Pack\") 返回 %1"
                                            "｜此后 fromType<Pack>().isRegistered()=%2")
                                 .arg(namedId)
                                 .arg(QMetaType::fromType<Pack>().isRegistered());
        qInfo().noquote() << QStringLiteral("  ★ 预期：两行 isValid 都是 true（编译期给的接口），"
                                            "差别只出现在 isRegistered 上（运行期给的编号）。"
                                            "id() 本讲**不印**，因为调用它本身可能触发惰性登记，"
                                            "拿它当判据就不干净了 —— 判据只用 2691 行那个 typeId。");
    }

    // ── 连接：每组一个信号、一种类型，控制变量（理由见 Worker 的注释）──────────
    hub.setExpectedSender(&worker);
    QObject::connect(&worker, &Worker::sigDirect, &hub, &Hub::onDirect, Qt::DirectConnection);
    QObject::connect(&worker, &Worker::sigQueued, &hub, &Hub::onQueued, Qt::QueuedConnection);
    QObject::connect(&worker, &Worker::sigBlocking, &hub, &Hub::onBlocking,
                     Qt::BlockingQueuedConnection);
    QObject::connect(&worker, &Worker::sigAuto, &hub, &Hub::onProbe, Qt::AutoConnection);
    QObject::connect(&worker, &Worker::sigPack, &hub, &Hub::onPack, Qt::QueuedConnection);
    QObject::connect(&worker, &Worker::sigBare, &hub, &Hub::onBare, Qt::QueuedConnection);
    // 反方向：主线程 → worker 的槽；以及 worker 的回执 → 主线程
    QObject::connect(&hub, &Hub::pokeWorker, &worker, &Worker::handleFromHub);
    QObject::connect(&worker, &Worker::acked, &hub, &Hub::onAcked);

    // ── 【7】那两条一模一样的连接，只差接收者的死活 ───────────────────────────
    // 它们**故意住在 worker 线程**：这样「发射 → 立刻 delete」能发生在同一个槽调用里，
    // 顺序才是确定的（见第六节）。既然发射线程 == 接收者线程，Auto 会判成 Direct，
    // 所以这里必须**显式写 Queued** —— 这也顺手证明：Queued 是强制的，同线程照样排队。
    Sink *survivor = new Sink(QStringLiteral("survivor"), &gSurvivorDelivered);
    Sink *doomed = new Sink(QStringLiteral("doomed  "), &gDoomedDelivered);
    survivor->moveToThread(&workerThread);
    doomed->moveToThread(&workerThread);
    worker.setDoomedTarget(doomed);
    QObject::connect(&worker, &Worker::sigDoomed, doomed, &Sink::got, Qt::QueuedConnection);
    QObject::connect(&worker, &Worker::sigDoomed, survivor, &Sink::got, Qt::QueuedConnection);

    workerThread.start();

    const int kBatch = 5;              // 每组的条数（「可核对的数」就锚在这）
    const int kStep = 120;             // 步骤间隔，纯留余量，不参与任何结论

    // ── 第 0 步：舞台自检 ──────────────────────────────────────────────────────
    QTimer::singleShot(0, [&]() {
        qInfo().noquote() << QStringLiteral("\n【0】自检：谁在跑、谁在听、队列有没有人在处理");
        qInfo().noquote() << QStringLiteral("  主线程 = %1 句柄=%2")
                                 .arg(tagOf(mainThread))
                                 .arg(mainHandle, 0, 16);
        qInfo().noquote() << QStringLiteral("  workerThread.isRunning()=%1 loopLevel()=%2")
                                 .arg(workerThread.isRunning()).arg(workerThread.loopLevel());
        qInfo().noquote() << QStringLiteral("  ★ loopLevel() 这一行是**主线程读另一条线程**的 int，"
                                            "严格说就是竞态（第 4 讲的靶子）。这里放在两条线程刚起步、"
                                            "没人改它的当口，且只是打印；工程里请让线程自己报（第 1 讲）。");
        qInfo().noquote() << QStringLiteral("  idealThreadCount()=%1（只是「机器有几核」，跟本讲无关）")
                                 .arg(QThread::idealThreadCount());
        // 让 worker 自己报一句，作为后面所有「worker 侧」日志的对齐基准
        QMetaObject::invokeMethod(&worker, []() {
            qInfo().noquote() << QStringLiteral("  worker 侧自报：我此刻在 ") + handleText();
        }, Qt::QueuedConnection);
    });

    // ── 第 1 步：AutoConnection —— 同一条连接，两种裁决 ────────────────────────
    QTimer::singleShot(kStep * 1, [&]() {
        qInfo().noquote() << QStringLiteral("\n【1】Auto：裁决只看「emit 那一刻谁在跑」，"
                                            "不看对象归谁");
        qInfo().noquote() << QStringLiteral("  1-a 在**主线程**直接调 worker.sigAuto(101)"
                                            "（只为观察裁决，不是推荐用法：跨线程直接调别人线程上的对象，"
                                            "正是第 2 讲骂过的形状）");
        Q_EMIT worker.sigAuto(101);
        qInfo().noquote() << QStringLiteral("      回到这一行时 hub.probeHits()=%1"
                                            " ← 已经 +1，说明槽是**当场**跑完的（这边判成了 Direct）")
                                 .arg(hub.probeHits());
        qInfo().noquote() << QStringLiteral("  1-b 同一个信号、同一条连接，改到 worker 线程里发：");
        const bool ok = QMetaObject::invokeMethod(&worker, &Worker::pumpAuto,
                                                  Qt::QueuedConnection, 102);
        qInfo().noquote() << QStringLiteral("      invokeMethod(functor, Queued) 返回=%1"
                                            "；这一行**先**打印，onProbe 的日志在后 → 那边判成了 Queued")
                                 .arg(ok);
    });

    // ── 第 2 步：Direct —— 槽就在发射方的调用栈上 ─────────────────────────────
    QTimer::singleShot(kStep * 2, [&]() {
        qInfo().noquote() << QStringLiteral("\n【2】Direct：别的线程发 → 槽在**那条线程**执行"
                                            "（换成 QWidget 这就是崩溃现场）");
        QMetaObject::invokeMethod(&worker, &Worker::pumpDirect, Qt::QueuedConnection, kBatch);
        qInfo().noquote() << QStringLiteral("      主线程这行先返回，日志里那 %1 行 [槽 onDirect]"
                                            "全是在 worker 栈上打的：kBatch=%1 条，一条不少")
                                 .arg(kBatch);
    });

    // ── 第 3 步：Queued + 「emit 先返回」实测 ─────────────────────────────────
    QTimer::singleShot(kStep * 3, [&]() {
        qInfo().noquote() << QStringLiteral("\n【3】Queued：投递完就走，槽等接收者线程的事件循环");
        QMetaObject::invokeMethod(&worker, &Worker::pumpQueued, Qt::QueuedConnection, kBatch);
        qInfo().noquote() << QStringLiteral("      主线程此刻 hub.queuedHits()=%1"
                                            "（这个读数是主线程**自己的**，不是竞态）")
                                 .arg(hub.queuedHits());
    });

    // ── 第 4 步：按值拷贝 + 未登记类型的对照 ──────────────────────────────────
    QTimer::singleShot(kStep * 4, [&]() {
        qInfo().noquote() << QStringLiteral("\n【4】参数按值拷贝进事件；没登记的类型编译器不管");
        QMetaObject::invokeMethod(&worker, &Worker::pumpPack, Qt::QueuedConnection, kBatch);
        QMetaObject::invokeMethod(&worker, &Worker::pumpBare, Qt::QueuedConnection);
        qInfo().noquote() << QStringLiteral("      预期紧跟着出现 DLL 里那两行原文（Bare 未登记）；"
                                            "onBare 的日志**不该**出现，hub.bareHits()=%1")
                                 .arg(hub.bareHits());
    });

    // ── 第 5 步：BlockingQueued —— 只跑安全方向 ───────────────────────────────
    QTimer::singleShot(kStep * 5, [&]() {
        qInfo().noquote() << QStringLiteral("\n【5】BlockingQueued：发射方卡在 emit，"
                                            "直到槽在接收者线程跑完（死锁那条见注释）");
        QMetaObject::invokeMethod(&worker, &Worker::pumpBlocking, Qt::QueuedConnection, 501);
        qInfo().noquote() << QStringLiteral("      主线程只是**排了个活**给 worker，没等它；"
                                            "等是 worker 等主线程，方向对了才不死锁");
    });

    // ── 第 6 步：invokeMethod 的两个世界 ─────────────────────────────────────
    QTimer::singleShot(kStep * 6, [&]() {
        qInfo().noquote() << QStringLiteral("\n【6】invokeMethod：functor 版与字符串版，"
                                            "跨线程拿返回值只有两条路");
        // (a) functor 版 + Queued，不要返回值：立刻返回，活在那条线程的队列里
        const bool okA = QMetaObject::invokeMethod(
                &worker,
                [&worker]() {
                    qInfo().noquote() << QStringLiteral("      (a) functor 版在 %1 执行"
                                                        "  compute(7)=%2")
                                             .arg(handleText()).arg(worker.compute(7));
                },
                Qt::QueuedConnection);
        qInfo().noquote() << QStringLiteral("  (a) 投递返回=%1（此刻它还没跑）").arg(okA);

        // (b) functor 版 + BlockingQueued + 返回值出口：这就是「跨线程要结果」的正解之一
        int retB = -1;
        const bool okB = QMetaObject::invokeMethod(&worker, &Worker::compute,
                                                   Qt::BlockingQueuedConnection,
                                                   qReturnArg(retB), 7);
        qInfo().noquote() << QStringLiteral("  (b) functor+BlockingQueued 返回=%1  retB=%2"
                                            "（qReturnArg 的出口必须是左值：228 行把右值版 = delete 了）")
                                 .arg(okB).arg(retB);

        // (c) 字符串版 + Q_RETURN_ARG/Q_ARG：类型名写错就返回 false，编译器不替你检查
        int retC = -1;
        const bool okC = QMetaObject::invokeMethod(&worker, "compute",
                                                   Qt::BlockingQueuedConnection,
                                                   Q_RETURN_ARG(int, retC), Q_ARG(int, 7));
        qInfo().noquote() << QStringLiteral("  (c) 字符串版 Q_INVOKABLE \"compute\" 返回=%1  retC=%2")
                                 .arg(okC).arg(retC);

        // (d) 字符串版查一个**没标 Q_INVOKABLE** 的同签名函数：预期 false
        int retD = -1;
        const bool okD = QMetaObject::invokeMethod(&worker, "uninvokable",
                                                   Qt::BlockingQueuedConnection,
                                                   Q_RETURN_ARG(int, retD), Q_ARG(int, 7));
        qInfo().noquote() << QStringLiteral("  (d) 字符串版 \"uninvokable\"（moc 表上没有）返回=%1"
                                            "  retD=%2 ← 预期 false / -1，另有一条 qWarning")
                                 .arg(okD).arg(retD);
        // (e) 最省事那条：同线程就**直接调用**，别绕信号槽。
        //     跨线程直接调（worker.compute(7) 写在主线程）语法上通、纯函数上也死不了人，
        //     但它把「谁在哪个线程」这件事从日志里彻底抹掉了 —— 那是第 4 讲的靶子，本讲不跑。
    });

    // ── 第 7 步：接收者先没，事件还在路上 ────────────────────────────────────
    QTimer::singleShot(kStep * 7, [&]() {
        qInfo().noquote() << QStringLiteral("\n【7】生命周期：同一个信号、同一种 Queued，"
                                            "只差接收者活没活着");
        QMetaObject::invokeMethod(&worker, &Worker::runLifecycleProbe, Qt::QueuedConnection);
        qInfo().noquote() << QStringLiteral("      主线程只是把这条**排给了 worker**；"
                                            "真正的 3 发 + delete 都在 worker 那个槽里发生");
    });

    // ── 第 8 步：sender() 的能与不能 ────────────────────────────────────────
    QTimer::singleShot(kStep * 8, [&]() {
        qInfo().noquote() << QStringLiteral("\n【8】是谁叫我：成员槽里才拿得到，"
                                            "手动调用时是空的");
        QMetaObject::invokeMethod(&worker, &Worker::pumpAuto, Qt::QueuedConnection, 802);
        qInfo().noquote() << QStringLiteral("  ▶ 下面这行是**直接调槽**（不是通过信号）→"
                                            " 预期 sender() 打「空」");
        hub.onProbe(999);
        qInfo().noquote() << QStringLiteral("  ▶ 反方向：主线程发 pokeWorker，槽在 worker 那边跑");
        Q_EMIT hub.pokeWorker(701);
        Q_EMIT hub.pokeWorker(702);
        qInfo().noquote() << QStringLiteral("      这两行返回时 handleFromHub 一次都没跑"
                                            "（hubHits=%1）—— Auto 在这里判成了 Queued")
                                 .arg(worker.hubHits());
    });

    // ── 第 9 步：等所有在途事件落地，再把账合上 ───────────────────────────────
    QTimer::singleShot(kStep * 9, [&]() {
        qInfo().noquote() << QStringLiteral("\n【9】合账（两条线程都不忙了，这些读数才有意义）");
        qInfo().noquote() << QStringLiteral("  Direct   ：槽 %1 次（预期 %2）｜最后一次执行线程句柄=%3"
                                            "  主线程句柄=%4  → 不同 = 槽真跑在 worker 上")
                                 .arg(hub.directHits()).arg(kBatch)
                                 .arg(hub.lastThreadHandle(), 0, 16).arg(mainHandle, 0, 16);
        qInfo().noquote() << QStringLiteral("  Queued   ：槽 %1 次（预期 %2）｜发射方「emit 先返回」命中 %3 次")
                                 .arg(hub.queuedHits()).arg(kBatch).arg(gEmitReturnedFirst.loadAcquire());
        qInfo().noquote() << QStringLiteral("  Blocking ：槽 %1 次（预期 1）｜Pack：%2 次（预期 %3）"
                                            "｜Bare：%4 次（预期 0）")
                                 .arg(hub.blockingHits()).arg(hub.packHits()).arg(kBatch)
                                 .arg(hub.bareHits());
        qInfo().noquote() << QStringLiteral("  Auto     ：onProbe %1 次（预期 4：1-a + 1-b + 802 + 手动调用）"
                                            "｜反方向 handleFromHub %2 次（预期 2）"
                                            "｜回执 onAcked %3 次（预期 2）")
                                 .arg(hub.probeHits()).arg(worker.hubHits()).arg(hub.ackedHits());
        qInfo().noquote() << QStringLiteral("  生命周期 ：survivor 送达 %1（预期 3）"
                                            "｜doomed 送达 %2（预期 0）← 差值就是「接收者先没」")
                                 .arg(gSurvivorDelivered.loadAcquire())
                                 .arg(gDoomedDelivered.loadAcquire());
        qInfo().noquote() << QStringLiteral("  seqSum   ：%1（预期 %2 = (1+..+%3)×2 + 501）")
                                 .arg(hub.seqSum()).arg((kBatch * (kBatch + 1)) + 501).arg(kBatch);
    });

    // ── 第 10 步：收摊（第 1、2 讲的规矩在这里全部要用上）──────────────────────
    QTimer::singleShot(kStep * 10, [&]() {
        qInfo().noquote() << QStringLiteral("\n【10】收尾：requestInterruption → quit → wait(带截止)");
        workerThread.requestInterruption();          // 协作式；本讲的 worker 不看它，纯表诚意
        workerThread.quit();                         // 让那条线程的事件循环退出
        const bool stopped = workerThread.wait(QDeadlineTimer(3000));
        qInfo().noquote() << QStringLiteral("  wait 返回=%1（false 就是「还有人在等别人」，"
                                            "八成是【5】那种方向搞反了）").arg(stopped);
        // ★ survivor 还归属 worker 线程。线程已经没了，对象还活着 —— 直接在这条线程 delete
        //   就是第 2 讲那个「析构跑在不是自己家的线程上」。先挪回主线程，再收。
        survivor->moveToThread(mainThread);
        delete survivor;
        // ★ worker 是**主线程栈上的对象**，同理必须先挪回来；不挪的话它退出 main 时的析构
        //   就发生在一个「已经不是任何运行线程」的归属上 —— 第 2 讲的 deleteLater 那条
        //   正路是给堆上对象用的（connect(&workerThread, &QThread::finished, worker,
        //   &QObject::deleteLater)），栈对象只能自己把它挪回正.thread()。
        worker.moveToThread(mainThread);
        qInfo().noquote() << QStringLiteral("  总耗时 %1 ms（kStep=%2 × 10 步的余量，不是结论）")
                                 .arg(clock.elapsed()).arg(kStep);
        qInfo().noquote() << QStringLiteral("═══ 跑完了 ═══");
        QCoreApplication::quit();
    });

    return app.exec();
}

#include "03_cross_thread_signals.moc"

// ——— 老师傅的话 ———
/*
 * 先给你那句能带走一辈子的话：**连接类型不是性能选项，是「这一趟怎么搬」的合同，
 * 而裁决发生在 emit 那一刻，不在 connect 那一刻。**
 * 本讲【1】故意把同一条 Auto 连接跑了两遍：主线程发 → 槽当场跑完（我打印的
 * probeHits 已经 +1 了）；worker 线程发 → 主线程这行日志先出来，槽的日志跟在后面。
 * 一条 connect，两副面孔，差别只有一个东西：谁在执行那行 emit。你要是把这句背成
 * 「Auto 会自动挑一种更好的连接」，那你迟早会在评审里说错话 —— 它不挑「更好」，
 * 它只挑「接收者这条线程现在是不是我这条」。
 *
 * 第二条：**Qt::DirectConnection 你几乎永远不该手写。**
 * 它唯一的正当形状是「接收者本来就在这条线程，你要零延迟同步」—— 可那时候 Auto
 * 已经判成 Direct 了，你写它是多余的。所以你手写的 Direct 只有两种来源：要么在
 * 跨线程（那就是把 UI 对象交到别人手上，【2】那五行日志换成 QWidget 就是崩溃报告），
 * 要么在赌「反正接收者跟我一条线程」（那 Auto 会自己判，你不用赌）。
 * 我在别人代码里看到显式 Direct，第一反应是问「这个接收者归谁」，第二反应是让他删掉。
 * 反过来，**显式 Queued 有一个正当用途**，而且很常用：同线程也要强制排队。
 * 典型场景是「我在处理一个事件的过程中想改数据，但不想在当前调用栈上重入」。
 * 【7】里那两条就是这个用法：发射方和接收者都在 worker，Auto 会判 Direct，
 * 我硬写 Queued 让它落到队列里 —— 于是「先投递、后 delete」的顺序才成立。
 *
 * 第三条：BlockingQueued 是「跨线程要返回值」的工具，不是「同步一点」的偏好。
 * 用之前把三条前提念一遍：① 接收者确实在**别的**线程（否则自己等自己，见那段注释）；
 * ② 接收者那条线程**在跑事件循环**（loopLevel() 大于 0，就是第 1 讲那个坑）；
 * ③ 发射方此刻**没持有**接收者那个槽要拿的锁。
 * 第三条是资深区最贵的一对组合，我见过不止一次：主线程拿着数据锁，觉得「同步一点好」，
 * 发一个 BlockingQueued；worker 里的槽要同一把锁 —— 一个等事件被处理，一个等锁被放开，
 * 双向死锁，现场跟【5】那条同线程自锁一模一样：不崩、不报错、CPU 0%、只能挂调试器进去看。
 * 真要同步语义，先看第 4 讲（QMutex / QReadWriteLock）和第 5 讲（QtConcurrent 的 future
 * 天生就是「要返回值」那条路），别拿连接类型当锁用。
 *
 * 第四条：Queued 的参数是**按值拷贝进事件**的，这一句要拆成三句用。
 * ① 别把指向可变对象的裸指针跨线程发。槽拿到的那个指针，等你回头看日志时，
 *    它指向的东西可能早被发射方改了或者析构了 ——【4】里我在 emit 之后立刻改了
 *    本地那份 payload，槽里打出来的还是发射那一刻的值，这就是「按值」的保险作用。
 * ② 大对象要掂量。一次 emit 排给 N 个接收者，就是 N 份拷贝，每份都是真 malloc。
 *    进度信号每秒几百次 × 一个 QString，你熟悉的「UI 卡」有一半其实是**拷贝卡**。
 *    省法是发序号、让槽自己去查数据 —— 但注意，那一查又把人家的数据跨线程读了一遍，
 *    必须配合不可变快照（第 4 讲正面讲）。别只学「发指针」那一半。
 * ③ 自定义类型要能拷贝、要有元类型。而 Qt6 在这里对外行极不友好：
 *    本讲第三节实测了 `fromType<未登记类型>()` 都编译得过，运行期只是打两行 qWarning
 *    然后把这次投递丢掉。所以我的判据很硬：**跨线程连接一沾自定义类型，
 *    第一件事写 Q_DECLARE_METATYPE，第二件事跑一次，看控制台有没有那两行。**
 *    「没警告」比「编译过了」值钱得多，因为编译器那句根本不表示什么。
 *
 * 第五条：接收者先没，Qt 会把那条在途事件丢掉 —— 这个「安全」是有边界的。
 * Qt 替你处理的是**连接**和**接收者 QObject**；没替你处理的是**你捕获的裸指针**。
 * 所以三条纪律：lambda 永远带 context 对象（connect 的第四个参数不是装饰，
 * 它就是那条连接的命）；跨线程收尸一律 deleteLater（第 2 讲）；
 * 至于「我确认发送者比接收者活得久」这句话 —— 你说不出口就别依赖它，
 * 代码改一轮，谁活得更久就不是你能肉眼看出来的了。
 *
 * 第六条：sender() 三条硬事实。① 它和 senderSignalIndex() 都是 protected
 * （本机 qobject.h 367/368 行），所以「在 lambda 里问是谁叫我」这条路本来就不存在；
 * ② 你**手动调槽**（【8】里那行 hub.onProbe(999)）它给你空 —— 它认的是信号那次调用；
 * ③ 跨线程时 sender() 给你的指向一个**别的线程的对象**，只敢比指针、只敢读
 *    metaObject() 那张静态表，读它的成员就是第 2 讲的 bug。
 * 还有一条我更想让你记住：如果你非得靠 sender() 才认得出「是谁叫我」，
 * 多半是该把那个「谁」当成参数传进信号里。显式传参的连接，重构时活得久得多。
 *
 * 最后一条，也是这一讲真正的看家本事：**日志里的顺序就是证据。**
 * 本讲没有任何 assert，全部结论靠「谁先打印、谁后打印、计数差几」。
 * 养成这个习惯 —— 每个跨线程的槽第一行就打「我此刻在哪条线程（名字 + 句柄数字）」，
 * 它比任何调试器都快，也比任何「我觉得它应该跑在主线程」都硬。
 * 第 4 讲会把这条纪律升级：同一份数据被两条线程改的时候，你怎么把它拍下来。
 */

// ——— 小练习 ———
/*
 *   1. 把 sigAuto→onProbe 那条连接显式改成 Qt::QueuedConnection，重跑【1】的 1-a：
 *      「回到这一行时 probeHits 已经 +1」那句话会变成什么？用一句话说清
 *      同线程时 Auto 与 Queued 的分岔，以及为什么 Direct 那组的日志顺序不会变。
 *   2. 把 sigBlocking 的连接从 BlockingQueued 换成 Queued，重跑【5】：
 *      先预判 pumpBlocking 那三行打印（发射前 / emit 返回 / 槽执行）会重排成什么样，
 *      再跑，跟你对一遍。gBlockingDelivered 在「emit 返回」那一行会是几？
 *   3. 【危险实验，必须在**另存的一份副本**里做，别改仓库里这个文件】
 *      把 hub.pokeWorker→worker.handleFromHub 改成 BlockingQueued：跑一下，正常。
 *      再把发射方与接收者换成同一条线程上的两个对象（主线程发、hub 收）：程序停在
 *      emit 那一行不动了。写出这两次的差别，以及为什么这个例子里我只把第二种留在注释里。
 *   4. 给 Pack 加一个 QByteArray 成员并填 200KB，用 QElapsedTimer 单独夹住【4】那
 *      kBatch 条 emit（只夹发射方自己），再分别在 kBatch=5 与 500 两处各量一次；
 *      然后把 sigPack 的连接改成 Direct 再量一次。回答：「按值拷贝」这笔钱在什么时候
 *      贵到必须换成「发序号 + 不可变快照」。
 *   5. 给 Bare 补一行 Q_DECLARE_METATYPE(Bare)，重跑【4】：那两行 qWarning 消失、
 *      bareHits 从 0 变 1。这是本讲唯一一条「加一行就改变行为」的练习 ——
 *      做完你就记住元类型这道门了。
 *   6. 把【6】(c) 的 Q_ARG(int, 7) 写成 Q_ARG(double, 7.0)：返回值变什么？
 *      metaobject 模块第 8 讲称过的那笔账（字符串版类型必须精确匹配）在这里再称一次，
 *      然后对比 (b) 那条 functor 版同样的错会由**编译器**拦下来 —— 说说你为什么
 *      在新代码里应该一律用 functor 版。
 *   7. 综合：把【3】的 kBatch 改成 500，观察 gEmitReturnedFirst 是不是每次都等于
 *      kBatch。若出现「delivered == i」的条数，解释它是怎么发生的（主线程被谁唤醒了），
 *      并说明为什么这个波动**不影响**「Queued 只保证同一队列内的顺序，不保证何时跑」
 *      这一条结论。
 */
// ============================================================================


