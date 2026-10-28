// ============================================================================
// 第 4 讲 · 同步：锁、原子、等待条件与信号量 —— 以及它们各自的失效形状
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   第 3 讲末尾留了一句话：「同一份数据被两条线程改的时候，你怎么把它拍下来。」
//   这一讲就是那段录像。前面三讲教你「活怎么分到别的线程」，本讲教你
//   「分出去之后，那些线程碰同一块内存时怎么办」。四个工具，一个都不能少，
//   但**顺序**有讲究：线程封闭 > 消息传递 > 原子 > 锁。本讲倒着讲（先锁，最后
//   才回到线程封闭），因为你在别人代码里先撞见的就是锁。
//   本讲拆成八个可打印的事实：
//     1) 裸 `++` 的竞争**不一定出错**。看不见 ≠ 不存在：编译器有权把整个循环
//        折叠成一次加法，x86 上一次 int 写也天生是原子的样子 —— 于是你连跑十次
//        都对，第十一次上线就错。所以【1】分两半：先**单线程手工交错**演一次
//        「丢更新」（这个结果必然错，是可看的证据），再真并发跑（这个结果是概率）。
//        一个只靠「跑出来不对」发现的 bug，等于没有发现。（qbasicatomic.h 144~145
//        行那个 `operator++` 底下是 fetchAndAddOrdered + add_no_UB，见【1】末注）
//     2) **两个原子操作不等于一个复合原子操作**。转出/转入各用一次原子加，
//        中间那道缝里总额就是错的。锁解决的问题不是「每一步原子」，
//        而是「把几步钉成一个不可分割的区间」。【2a】把那道缝直接打印出来
//        （单线程，必然出现），【2b】让另一条线程在缝里旁观（真并发，看得到就是看得到）。
//     3) `QMutex` + `QMutexLocker`：Qt 的锁天生配合 RAII，但 RAII 的**写错形状**
//        有四种，本讲四种都有本机实测结论（两种直接编译不过，两种能过但静默或警告）。
//        另有一条硬钉子：`unlock()` 一个没持有的锁，在 QMutexLocker 里是
//        `Q_ASSERT(m_isLocked)`（qmutex.h 282 行），在 QMutex 本体上是
//        `Q_ASSERT(d_ptr.loadRelaxed())`（52 行）—— Debug 构建当场断言，
//        Release 构建就是未定义行为。**别靠「跑起来没事儿」判断锁写对了。**
//     4) 死锁的根因不是「锁太多」，是「两条线程拿锁的**顺序**不一致」。
//        第一道防线是把顺序写成事实上的单一（只在最外层加锁）；
//        第二道是 `tryLock(期限)`：它把「挂死」变成「一个可以打印的 false」；
//        第三道（也是最省事的结构性解法）是 `std::scoped_lock` 一次锁多个 ——
//        它内部用死锁避免算法，**不需要你约定顺序**。QMutex 满足 std 的
//        Lockable/BasicLockable/TimedLockable 三个概念，所以 std 那套能直接用：
//        本机 qmutex.h 41/51（lock/unlock，注释 "// BasicLockable concept"）、
//        69/83（tryLock/try_lock，"// Lockable concept"）、
//        175/182（try_lock_for/try_lock_until，"// TimedLockable concept"）
//        —— 那三行注释是 Qt 自己写的，不是我给它安的名。
//     5) `QReadWriteLock`：读锁共享、写锁独占，**只在读多写少时才划算**
//        （它有记账成本，见下一段）。三条本讲实测/核对的要点：
//        · 持读锁期间，别的线程 `tryLockForRead()` 能过、`tryLockForWrite()` 必 false
//          —— 这一条【5a】当场证给你看，用的是「不等待」的 `QDeadlineTimer()`。
//          为什么空等待就是 Qt 内部的「不试了」写法：qreadwritelock.h 27~29 行
//          `tryLockForRead()` 的本体正是 `tryLockForReadInternal(QDeadlineTimer(), …)`。
//        · 6.12 的头文件把这套 API **搬了家**：实现全在基类 QBasicReadWriteLock 里
//          （23/27/31/36/40/44/49 行那些 inline 体），派生类 QReadWriteLock（148 行）
//          只剩「老签名」，而且老签名外面包着 `#if QT_CORE_REMOVED_SINCE(6, 11)`
//          （158/165/170/177/182 行）—— 意思是「这些声明在 Qt7 里就没了」。
//          真正把无参那几个名字接回来的，是 168 行和 180 行那两句
//          `using QBasicReadWriteLock::tryLockForRead;/::tryLockForWrite;`。
//          ★ 这就是 C++ 的「派生类同名函数**隐藏**基类重载」：少一句 using，
//          `rw.tryLockForRead()`（无参）就编译不过。qmutex.h 145 行那句
//          `using QBasicMutex::tryLock;` 是同一个道理的同一手补丁。
//          顺带一条可查的：`tryLockForRead(int)` / `tryLockForWrite(int)` 这两个
//          老式整数超时重载**还在**（164/176 行声明，229/234 行定义），
//          本机头文件里 grep "QT_DEPRECATED" 在这四个文件里零命中 —— 所以
//          Qt6.12 用它们不会报警告。但新代码一律写 QDeadlineTimer，理由在【5】末尾。
//        · **读锁不能升级成写锁**：同一线程持有读锁时再 `lockForWrite()`，
//          NonRecursive（151 行那个 enum 的默认值）模式下就是自己等自己放行，
//          而放行要等你把读锁交出去 —— 死锁。正确做法是「先 unlock 读，再 lock 写」，
//          代价是中间那一瞬数据可能被别人改（要重读、要重新校验）。
//          这条本讲**只写在注释里，不跑**：跑起来这个演示程序就永远停不下来。
//     6) `QWaitCondition` 最重要的一句话：**wait() 返回 ≠ 条件成立**。
//        它会因为超时、因为虚假唤醒、因为「别人 wakeAll 但叫醒的是另一件事」而返回。
//        所以谓词必须写成 `while (不满足) wait(...)`，永远不是 `if`。
//        【6a】先跑一条**确定性**的：没人通知时带期限的 wait 必然返回 false；
//        【6b】再跑生产/消费，把「wait 返回了但队列还是空」的次数打出来。
//        6.12 还有两件小事：wait 有 `QReadWriteLock` 版重载（28~30 行），
//        以及给 std 习惯留的别名 `notify_one()/notify_all()`（35~36 行，本体就是
//        wakeOne/wakeAll）。另外 QWaitCondition **不可拷贝**（39 行 Q_DISABLE_COPY）——
//        「条件变量跟着对象一起拷一份」这种写法在 Qt 里编译不过，在 std 里也是。
//     7) `QSemaphore(n)` 里那个 n **不是提示，是硬约束**：同时在里的最多 n 个。
//        【7a】单线程证明它真的会拦（许可 3 时 `tryAcquire(4, QDeadlineTimer())` 必 false）；
//        【7b】六条线程抢 3 个许可，用 CAS 打出的峰值必须 ≤ 3。
//        一条容易踩的：`QSemaphoreReleaser` **只负责还，不负责拿**
//        （qsemaphore.h 81~85 行那个析构体里只有一句 `m_sem->release(m_n)`），
//        所以「acquire 交给它」是错的，必须自己 acquire 完再构造它。
//        还有：它三个构造/移动都挂着 `Q_NODISCARD_CTOR`（67/70/72/75 行），
//        写成 `QSemaphoreReleaser(sem);` 这种不命名的形式会吃 -Wunused-result，
//        而且**立刻析构 = 立刻 release**，等于白干。
//     8) 最后回到【8】：能不共享就不共享。**线程封闭 + 末尾合并**这一路的正确性
//        不需要任何锁：四条线程各写自己那格，join 之后主线程合并。
//        本讲用它把 1..100000 求和，跟【3】那把锁的版本比时间 ——
//        数字你自己在机器上看，我只说方向：多数机器上合并版更快，
//        而且它不需要你推理「这里会不会竞争」。
//   演示主线：一个 `Shared` 结构装下所有被观察的数据（故意让裸 int 和原子 int
//   并排住在一起），一个 `runThreads()` 负责「开 N 条线程、带期限收尸、绝不
//   terminate」，八节各自跑一轮，每轮末尾打一行可核对的结论。
//
// 对应构建目标名：threads_04_synchronization
//   find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)   # 仓库根 CMakeLists 就这么给
//   add_executable(threads_04_synchronization threads/04_synchronization.cpp)
//   target_link_libraries(threads_04_synchronization PRIVATE Qt6::Widgets Qt6::Concurrent)
//   ★ 本文件**没有 Q_OBJECT**，所以不需要 moc，末尾也没有那句 #include "xxx.moc"。
//     这不是偷懒，是本讲的一条内容：锁、原子、条件变量、信号量都属于
//     「和元对象系统无关」的那半边 Qt —— 它们是 std 概念级别的东西，
//     不该条件反射地加个 Q_OBJECT。第 3 讲要 moc 是因为要发信号，这里一个信号都不发。
//
// 运行前须知（每条都是可核对的，跑起来对着看）：
//   · **本讲有一条刻意的格式偏离**：前面几讲的 main 都用 QTimer::singleShot 串步骤，
//     本讲八节**同步顺序跑完，一次事件循环都不进**。理由：本讲观察的是锁与等待，
//     不是事件调度；把每节塞进 singleShot 反而会把「哪条线程在什么时刻观察到什么」
//     搅浑。顺带这也证了第 1 讲那句话的另一半：`QThread::create` 起的线程跑的是纯函数，
//     **不要求**任何线程有事件循环。（QCoreApplication 还是要建，Qt 的一些内部设施认它。）
//   · 全程无阻塞对话框、无交互、无键盘；每个等待都带期限（`QDeadlineTimer` /
//     `tryLock` / `tryAcquire` / `wait(期限)`）；收尸一律 requestInterruption 之外
//     再加 `wait(期限)`；**没有一处 terminate()**（第 1 讲称过的账：terminate 是
//     「把别人的栈炸掉」，本讲所有线程都是自愿退出的）。
//   · 一条刻意的「不干净」：`runThreads()` 里如果某条线程超时没退出，
//     本讲**不 delete 它**（宁可泄漏，也不制造「正在运行的 QThread 被 delete」这种
//     断言级事故），并把这条打印出来。正常路径下这段一次都不会触发；触发了就说明
//     你的机器慢到超过我给的期限，那也应该先看日志而不是先把泄漏「修掉」。
//   · 不读文件、不联网、不写注册表、不建 GUI。
//   · 可核对的数（本讲全部常量都写在下面「口径」一节，改一个数就全篇跟着变）：
//     【1a】手工交错：起始 1000，两次「各自 +1」之后必须等于 **1001**（不是 1002）。
//     【1b】真并发：kThreads=2 × kIter=50000 → 原子那侧必须正好 **100000**；
//            裸 int 那侧**任何值都合法**，包括正好 100000（这就是本讲的靶心）。
//     【2a】转账前/中/后：和必须打成 200000 → **199999** → 200000（缝里少 1，必然）。
//     【2b】旁观线程：kRounds=20 轮、每轮缝宽 5ms → 旁观次数 > 0 且「看到总额不对」
//            的次数**应当**等于旁观次数（这一条是概率性的，日志里两个数都打，
//            你机器上哪怕只旁观到 1 次也算证明成立）。
//     【3】加锁那侧必须正好 **100000**；顺带把 lock/unlock/relock/isLocked 走一遍，
//          预期序列 true → false → true（282/289 行那两个 Q_ASSERT 全程不能被触发）。
//     【4】反序拿锁 + tryLock(空) 退让：两条线程各自完成数之和 == 2×kRounds4=**4000**，
//          退让次数 ≥ 0（这是概率，只报实测值）；`std::scoped_lock` 那侧退让次数
//          必须是 **0**，完成数同样 4000。
//     【5a】持读锁期间：别的线程 tryLockForRead() == **true**、
//          tryLockForWrite(QDeadlineTimer()) == **false**；持锁线程 join 之后再试
//          tryLockForWrite == **true**。三个布尔值是硬结论，不是概率。
//     【5b】1 写 4 读：读侧「看到 x+y ≠ 100」的次数必须 **== 0**（这就是读锁在干活），
//          写侧总步数 == kWriteSteps=**2000**，写写互斥那一段读者永远看不到撕裂。
//     【6a】没人通知时 `wait(&m, QDeadlineTimer(300))` 必须返回 **false**，
//          且实测耗时 ≥ 300ms（这一条是确定性的，用来钉住「超时返回 false」）。
//     【6b】生产 100 条：消费到的数字之和 == **5050**，条数 == 100，
//          「wait 返回但队列仍空」次数 ≥ 0（打实测值，不是承诺）。
//     【7a】许可 3 时 `tryAcquire(4, QDeadlineTimer())` 必须 **false**；
//          空抢一轮 `available()` 回到 3。
//     【7b】6 线程 × 4 活 = **24** 全部完成，峰值并发 ≤ **3**（应当正好 3）。
//     【8】合并求和 1..100000 == **5000050000**（公式 n(n+1)/2，程序自己算一遍对照）。
//   · **退出码 = 硬核对那本账**：`不符 + 收尸超时` 大于 0 就返回 1，全对上返回 0。
//     本讲不进事件循环，所以是 main 直接 return（别的讲用 `QCoreApplication::exit(码)`）；
//     算法在全仓库只有一个 —— 一张对账表打印得再认真，只要进程永远返回 0，
//     CI 和脚本就看不见失败，那份「核对」只对眼睛负责。
//     同一本账也解释了为什么 [预期中]/[实测] 两类**不进**分母：那是调度的结果，
//     不是代码的对错；把它们计进去，等于把「我的程序有 bug」和「今天机器忙」写成同一句话。
//   · 本文件的校验状态（照实说）：`g++ -fsyntax-only -std=c++17 -fPIC -Wall -Wextra`
//     对着本机 Qt 6.12.0 头文件通过（0 error / 0 warning），无 Q_OBJECT 故不需 moc；
//     **未链接、未运行**（本机 Qt 运行时缺符号，进程一起就 0xC0000139）。
//     所以上面每一条「预期」都是**推理**，等你在能编译的机器上复核；
//     但下面这些是**实测**：所有行号是本机头文件里逐条 grep 出来的；
//     【3】那四种 locker 写错形状的诊断文本，是我用同一套 g++ 单独跑最小样例抓的，
//     原文抄在下面第三节注释里，一个字没改；
//     ★ 第二节那条「`slots` 是个宏」不是从博客抄的 —— 是我这一版本**先写成
//     `std::vector<qint64> slots;`**，被本机 g++ 当场退回
//     （`error: declaration does not declare anything [-fpermissive]`）之后，
//     再去 qtmetamacros.h 找到那两句 `# define slots Q_SLOTS` 的。
//     一条编译错误换一节课，这笔交易很划算，所以原话留在代码里。
//   · 本讲所有 API 都对着本机真实头文件核对过（C:\msys64\ucrt64\include\qt6，Qt 6.12.0）：
//       QtCore/qmutex.h          —— 17 行 `#if QT_CONFIG(thread) || defined(Q_QDOC)`：
//                                    ★ 整段真实锁都在这个条件里，311 行 `#else` 起是
//                                    「无线程构建」的空壳：313 行 QMutex 的 lock/unlock/tryLock
//                                    是空函数（322 行那个 unlock 体是 `{}`），345 行 QMutexLocker
//                                    的构造析构也全空（349/350 行）。同理 qreadwritelock.h
//                                    340~395 行、qwaitcondition.h 49~66 行（注意 59/60 行那个
//                                    `wait(...)` **直接 return true**）。
//                                    → 也就是说「我加了锁怎么还是乱」还有一种答案是你压根
//                                    不在多线程构建里。这一条平时用不到，查到就是救命。
//                                  23 行 QBasicMutex（25 行 Q_DISABLE_COPY_MOVE、36 行 constexpr 构造）、
//                                    41 行 lock()、51 行 unlock()（52 行那句 Q_ASSERT
//                                    注释原话 "mutex must be locked"）、69 行 tryLock()、
//                                    83 行 try_lock()、86~90 行 fastTryLock() ——
//                                    无竞争路径就是 d_ptr 上一次 testAndSetAcquire，
//                                    抢到就把 nullptr→dummyLocked() 写进去，**不分配任何东西**；
//                                  125 行 QMutex（128 行 `constexpr QMutex() = default;`、
//                                    129 行析构里有真活：d_ptr 非空要 destroyInternal）、
//                                    145 行 `using QBasicMutex::tryLock;`、147 行 tryLock(int)、
//                                    152 行 tryLock(QDeadlineTimer)、175/182 行 chrono 版、
//                                  188 行 QRecursiveMutex（200 行 constexpr 构造、206 行 lock()
//                                    本体是 `tryLock(QDeadlineTimer(QDeadlineTimer::Forever))`、
//                                    209/211/214/217 行那组）、
//                                  241~242 行 `template <typename Mutex> class QMutexLocker`
//                                    （251 行 Q_NODISCARD_CTOR + 252 行 explicit 构造、
//                                    261~262 行移动构造、269 行析构、275 行 isLocked()、
//                                    280 行 unlock + 282 行 Q_ASSERT(m_isLocked)、
//                                    287 行 relock + 289 行 Q_ASSERT(!m_isLocked)、
//                                    305 行 Q_DISABLE_COPY、308 行 `bool m_isLocked = false;`）。
//       QtCore/qreadwritelock.h  —— 10 行 `#include <QtCore/qtsan_impl.h>`（★ 这版 Qt 的锁里
//                                    埋了 ThreadSanitizer 钩子，见下一行号组）、
//                                  18 行 QBasicReadWriteLock：23 lockForRead、27/31 两个
//                                    tryLockForRead、36 lockForWrite、40/44 两个 tryLockForWrite、
//                                    49 unlock（**54 行那句 Q_ASSERT_X 的原文是
//                                    "Cannot unlock an unlocked lock"**，函数名一起写进去了）、
//                                    63~67 行那五个 `lock()/lock_shared()/try_lock()/
//                                    try_lock_shared()/unlock_shared()` —— 就是照
//                                    std::shared_mutex 的脸给的，所以
//                                    `std::shared_lock<QReadWriteLock>`、`std::scoped_lock(rw)`
//                                    都能编译（本机实测过，见【4】【5】的代码）；
//                                  29/33/42/46 行（四个 tryLock 全把 QtTsan::TryLock 当参数递进去）、
//                                    56/59 行（unlock 前后的 mutexPreUnlock/PostUnlock）、
//                                    88~89 行（`QtTsan::ReadLock` + mutexPreLock）、
//                                    97~98 与 120~121 行（失败时补 TryLockFailed 再 postLock）、
//                                    112 行、134 行 `if constexpr (QtTsan::IsEnabled)` ——
//                                    这一条是本讲【2】「怎么把它钉死」的答案之一：
//                                    Qt 自己就是靠这些注解让 ThreadSanitizer 认得它的锁的，
//                                    所以带 TSan 跑【2】【5】比你自己肉眼数计数硬得多。
//                                  86 行 tryLockForReadInternal / 110 行 tryLockForWriteInternal
//                                    （真正干活的两个）、128 行 `#if QT_CORE_REMOVED_SINCE(6, 12)`、
//                                  148 行 QReadWriteLock、151 行 `enum RecursionMode { NonRecursive,
//                                    Recursive }`、153~156 行构造/析构（都挂 QT_CORE_INLINE_SINCE(6, 6)）、
//                                    158/165/170/177/182 行那五处
//                                    `#if QT_CORE_REMOVED_SINCE(6, 11) || defined(Q_QDOC)`、
//                                    164/176 行老式 int 超时重载、168/180 行两句 using、
//                                    229/234 行那两个 int 重载的体外定义、
//                                  245 行 QReadLocker（248 行 Q_NODISCARD_CTOR、252 行析构里
//                                    直接 unlock()、254 unlock、264 relock、274 readWriteLock()、
//                                    285 行那句 Q_ASSERT_X 检查指针最低位 ——「锁对象没按
//                                    对齐要求构造」的现场检测）、290 行 QWriteLocker（同构）。
//       QtCore/qwaitcondition.h  —— 18 行类、21/22 构造析构、24~25 行
//                                    `wait(QMutex*, QDeadlineTimer = Forever)`、26 行
//                                    `wait(QMutex*, unsigned long)`、28~30 行 QReadWriteLock 那两条、
//                                    32 wakeOne、33 wakeAll、35~36 行 notify_one/notify_all、
//                                    39 行 Q_DISABLE_COPY。
//       QtCore/qsemaphore.h      —— 16 行类、19 行 `explicit QSemaphore(int n = 0)`、
//                                    22 acquire(int n = 1)、23 tryAcquire(int n = 1)、
//                                    25 tryAcquire(int, int)、26 tryAcquire(int, QDeadlineTimer)、
//                                    28~31 chrono 版、33 release(int n = 1)、35 available()、
//                                    37~44 行 std::counting_semaphore 兼容层
//                                    （38 行 try_acquire、40~41 try_acquire_for、
//                                    42~44 try_acquire_until）、48 行 Q_DISABLE_COPY、
//                                    65 行 QSemaphoreReleaser（67/70/72/75 行四个
//                                    Q_NODISCARD_CTOR、71/73 行两个 explicit 构造、
//                                    81~85 行析构 = 唯一一次 release、96~99 行 cancel()）。
//       QtCore/qdeadlinetimer.h  —— 22 行类、25 行 `enum class ForeverConstant { Forever }`、
//                                    26 行 `static constexpr Forever = ForeverConstant::Forever`、
//                                    28 行默认构造、31 行 `QDeadlineTimer(ForeverConstant, type)`、
//                                    33 行 `explicit QDeadlineTimer(qint64 msecs, type = CoarseTimer)`、
//                                    40 行 hasExpired()。
//                                    ★ 33 行那个 **explicit** 决定了写法：`tryLock(300)` 走的是
//                                    int 重载，`tryLock(QDeadlineTimer(300))` 才是走期限重载；
//                                    少写那一句构造，选的就是另一个函数了。
//       QtCore/qthread.h         —— 68 requestInterruption、69 isInterruptionRequested、
//                                    78 loopLevel、80 isCurrentThread、
//                                    85~86 行 `template … [[nodiscard]] static QThread *create(...)`
//                                    （133~144 行的体：把你的函数装进
//                                    `std::async(std::launch::deferred, …)` —— 所以「返回值」
//                                    是拿不到的，要拿数就走【8】那种自带槽位的合并），
//                                    89 start、90 terminate、92 quit、
//                                    95 行 `bool wait(QDeadlineTimer = Forever)`、96 行 int 版、
//                                    103~105 行 sleep/msleep/usleep（静态，参数 unsigned long）。
//       QtCore/qatomic.h         —— 17~18 行 QAtomicInteger<T> 继承 QBasicAtomicInteger<T>、
//                                    21 行注释 "// Non-atomic API"、24~28 拷贝构造、
//                                    30~34 拷贝赋值（两个体都是
//                                    `storeRelease(other.loadAcquire())`，即**快照拷贝**）、
//                                    113 行 QAtomicInt、116 行同一句 Non-atomic API 注释、
//                                    119 行 `constexpr QAtomicInt(int value = 0)`、
//                                    132/137 行那两对 storeRelease/loadAcquire。
//       QtCore/qbasicatomic.h    —— 24 行 QBasicAtomicInteger、27 行 add_no_UB（那两个 ++ 底下
//                                    就是它 + fetchAndAddOrdered，专门用来避开有符号溢出 UB）、
//                                    47/48 loadRelaxed/storeRelaxed、
//                                    51 loadAcquire、52 storeRelease、53 `operator T()`、
//                                    54 `operator=`、66~71 三个 testAndSet{Relaxed,Acquire,Release}、
//                                    72/81 两个 testAndSetOrdered（81 行那版**顺带把当前值
//                                    写回你的引用**，这就是【7】那个 CAS 取最大值循环
//                                    能不收尾就能重试的原因）、
//                                    93 fetchAndStoreOrdered、105 fetchAndAddOrdered、
//                                    144~145 前置 ++、146~147 后置 ++、148~151 两个 --、
//                                    153~154 `+=` / `-=`，
//                                    以及 167~169 行：基类的拷贝是 **= delete** 的。
//                                    ★ 把 167~169 和 qatomic.h 24~34 对着看就明白了：
//                                    「原子量不能拷贝」是基类的事，上层 QAtomicInteger
//                                    又亲手把拷贝加了回来，代价是那句 "// Non-atomic API"。
//                                    第 3 讲第二节末尾提的就是这一条，本讲【8】把它跑成数字。
// ============================================================================

#include <QCoreApplication>
#include <QAtomicInt>            // 注意：它**不是** std::atomic 的别名，见第 3 讲第二节与本讲【8】
#include <QDebug>                // qInfo() 这组日志出口都在这里
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>          // 转发头；真实体在 qmutex.h 241~242 行（是个模板）
#include <QQueue>
#include <QReadWriteLock>
#include <QReadLocker>           // QReadLocker 与 QWriteLocker 同头文件（245 / 290 行）
#include <QSemaphore>
#include <QSemaphoreReleaser>    // 65 行那个；它只 release，不 acquire（81~85 行）
#include <QThread>
#include <QWaitCondition>
#include <QWriteLocker>

#include <mutex>                 // std::scoped_lock / std::lock / std::adopt_lock
#include <shared_mutex>          // std::shared_lock —— 用来对照 Qt 的锁满足 std 概念
#include <vector>                // 线程槽位用；注意它本身**不是**线程安全的容器

// ============================================================================
// 一、口径：本讲所有数字都集中在这儿，改一个数全篇跟着变
// ----------------------------------------------------------------------------
// 「可核对」的前提是**只有一个地方定义数字**。前面几讲踩过这个坑：同一个 5000
// 在注释里写一次、代码里写一次，改了一处忘了另一处，日志就对不上，然后你花半天
// 怀疑 Qt。所以本讲所有量都是 constexpr，并且每节末尾的核对行只引用它们。
// ============================================================================
static constexpr int kThreads       = 2;       // 【1】【3】的线程数
static constexpr int kIter          = 50000;   // 【1】【3】每线程的自增次数
static constexpr int kAcctOpen      = 1000;    // 【1a】手工交错那笔账的起始值（故意小，好口算）
static constexpr int kAcct0         = 100000;  // 【2】两个账户各自的起始额（不变量 = 2×kAcct0）
static constexpr int kRounds2       = 20;      // 【2b】转账轮数（每轮缝宽 kWindowMs）
static constexpr int kWindowMs      = 5;       // 【2b】把「两步之间」拉成肉眼可见的 5ms
static constexpr int kRounds4       = 2000;    // 【4】每条线程反序拿锁的轮数（合计 2×kRounds4）
static constexpr int kReaders5      = 4;       // 【5b】并发读者数
static constexpr int kReadSteps     = 20000;   // 【5b】每读者读多少轮
static constexpr int kWriteSteps    = 2000;    // 【5b】写者步数（一步 = x+1,y-1，和不变）
static constexpr int kSum5          = 100;     // 【5b】不变量：rwX + rwY 必须恒等于它
static constexpr int kItems6        = 100;     // 【6b】生产条数（1..kItems6，和 = 5050）
static constexpr int kWaitMs6       = 300;     // 【6a】确定性超时等待的期限
static constexpr qint64 kWaitMs6b   = 2000;    // 【6b】消费端每次 wait 的期限
static constexpr int kSlots7        = 3;       // 【7】信号量许可数
static constexpr int kWorkers7      = 6;       // 【7】抢许可的线程数（刻意大于许可数）
static constexpr int kJobs7         = 4;       // 【7】每线程的活数（合计 kWorkers7×kJobs7）
static constexpr int kJobMs         = 5;       // 【7】每件「活」占着许可多久
static constexpr int kMergeThreads  = 4;       // 【8】线程封闭：几个不相交槽位
static constexpr int kMergeTo       = 100000;  // 【8】求和上界（1..kMergeTo）

// 收尸期限：本讲没有任何一个 wait 是无期限的。这些数是**故意给得很宽**的 ——
// 超时就说明机器忙到不正常，此时正确动作是打日志并跳过（见 runThreads），
// 而不是 terminate()，也不是干脆把期限删掉。
static constexpr qint64 kJoinMs     = 20000;   // 普通节的收尸期限
static constexpr qint64 kJoinMsSlow = 40000;   // 【5b】【7】这种带 msleep 的节用这个

static QString hexPtr(const void *p)
{
    // 句柄只用来「认身份」，不表示任何东西（第 3 讲第一节同一规矩）。
    return QStringLiteral("0x") + QString::number(qulonglong(quintptr(p)), 16);
}

// 打「我现在在哪条线程」。名字是 runThreads 在 start() **之前**写进去的，
// 之后所有线程只读 —— 线程创建本身就是一次同步（happens-before），
// 所以「先写好再让别的线程读」合法；反过来「跑起来之后再改名字给别人读」就不是了。
static QString who()
{
    const QThread *cur = QThread::currentThread();
    QString name = cur->objectName();
    if (name.isEmpty())
        name = QStringLiteral("(未命名)");
    return name + QStringLiteral("@") + hexPtr(cur);
}

// 三种结论的口气必须不一样，这是本讲的一条纪律：
//   Hard —— 由 API 语义保证，跑出来不对就是代码错（计数、布尔、不变量）。
//   Soft —— 「应当」发生，但取决于调度器（旁观到几次、峰值是不是正好 3）。
//   Obs  —— 只报实测，不做任何承诺（退让次数、超时次数）。
// 把三种混成一种，是技术材料最常见的作假方式：拿概率性现象冒充确定性结论。
enum Kind { Hard, Soft, Obs };

// ★ 只有 Hard 进 failed，理由就是上面那句分类：Hard 不对 = 代码错，
//   Soft/Obs 没命中 = 这台机器这一次的调度，把它们计进来等于把「我的程序有 bug」
//   和「今天风不顺」写成同一句话。
//   计数只在**真的判定完**之后走，不做「打算检查 +1」—— 存储模块第 3 讲
//   （storage/03_json.cpp）那本账就是这么改过来的：一笔只记一次，记的是结果。
//   多出来的 stalled 不归 verdict() 管，是 runThreads() 记的「这一节没收尸」：
//   它不是「核对不通过」，是「这一节根本没资格被核对」，所以单独一个数，
//   最后进退出码时两者分开列出来，别混成一个数字骗自己。
struct HardLedger {
    int total = 0;      // 一共做了多少次硬核对
    int failed = 0;     // 其中多少次没对上
    int stalled = 0;    // 多少节「线程没收尸」：这一节后面每个数字都不作数
};
static HardLedger gHard;

static void verdict(Kind kind, const QString &label, bool ok, const QString &detail)
{
    QString head;
    switch (kind) {
    case Hard:
        ++gHard.total;
        if (!ok)
            ++gHard.failed;
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

// CAS 取最大值：qbasicatomic.h 81 行那个「带 currentValue」的 testAndSetOrdered 版
// 是关键 —— 它失败时**顺手把当前值写回你的引用**，所以循环不用重读，带着新事实重试。
// 这就是无锁结构里最常见的「乐观循环」形状：判断 → CAS → 失败就重来。
static void raiseMax(QAtomicInt &target, int candidate)
{
    int current = target.loadAcquire();
    while (candidate > current) {
        if (target.testAndSetOrdered(current, candidate, current))
            break;                      // 抢到了：target 现在是 candidate
    }                                   // 或者 candidate 已经不更大 —— 两种都该收工
}

// ——— 本讲唯一的线程基础设施 ———
// 规矩（前面三讲定下的，这里一次性实现，八个例子共用）：
//   · 用 QThread::create（qthread.h 85~86 行）—— 返回值挂 [[nodiscard]]，
//     所以必须落进容器/变量；`QThread::create(fn, i);` 光秃秃一句就是漏。
//   · start() 之前把 objectName 写好（理由见 who() 的注释）。
//   · 收尸一律 `wait(QDeadlineTimer(...))`（95 行），**绝不用无期限 wait**，
//     更不 terminate（90 行那个是「把别人的调用栈炸掉」，第 1 讲称过账）。
//   · 超时没退出 → 打日志 + **不 delete**：删一条正在运行的 QThread
//     是断言级事故，宁可泄漏一个对象，也不制造连现场都留不下的崩溃。
//     正常路径这段一次都不会走到；走到了说明这台机器慢出了本讲的预算。
template <typename Fn>
static bool runThreads(int count, Fn body, qint64 deadlineMs, const QString &tag)
{
    std::vector<QThread *> pool;
    pool.reserve(count);
    for (int i = 0; i < count; ++i) {
        QThread *t = QThread::create(body, i);     // body 被 decay 拷贝一份进去（133~144 行）
        t->setObjectName(QStringLiteral("%1-%2").arg(tag).arg(i));
        pool.push_back(t);
    }
    for (QThread *t : pool)
        t->start();                                // 89 行

    bool allDone = true;
    for (QThread *t : pool) {
        if (!t->wait(QDeadlineTimer(deadlineMs))) {
            allDone = false;
            qInfo().noquote() << QStringLiteral(
                "  ! %1：%2 在 %3ms 内没退出。按本讲规矩**不 terminate、也不 delete**它 —— "
                "正在运行的 QThread 被 delete 是事故，泄漏一个对象只是代价。")
                                      .arg(tag, t->objectName())
                                      .arg(deadlineMs);
        }
    }
    for (QThread *t : pool) {
        if (t->isRunning())
            continue;                              // 上面那条：留着它
        delete t;                                  // 已结束的线程，回收归调用方
    }
    if (!allDone)
        ++gHard.stalled;                           // 这一节的数字全部作废，见 HardLedger
    return allDone;
}

// ============================================================================
// 二、被观察的数据：一个结构体装下本讲所有靶子
// ----------------------------------------------------------------------------
// 为什么全塞一个 struct：这样「哪些字段之间有竞争」在代码里一眼看得出来，
// 而拆成十个全局变量就把关系藏进文件里了。
// ★ 顺带一条编译期事实：这个 struct **不可拷贝** —— 因为里面有 QMutex
//   （qmutex.h 25 行 Q_DISABLE_COPY_MOVE 干的）、QSemaphore（qsemaphore.h 48 行）
//   和 QWaitCondition（qwaitcondition.h 39 行 Q_DISABLE_COPY）。
//   「把带锁的对象按值传一份」这种错误，Qt 用类型系统直接拦掉了 ——
//   比任何代码评审都快。所以下面所有 lambda 一律捕获 `Shared &s`。
// ============================================================================
struct Shared {
    // ——【1】【3】计数组 ——
    int plain = 0;                  // 故意裸着：对它的一切并发读写都是 data race
    QAtomicInt atomicCount;         // 只干「加」这一件事
    QMutex guard;                   // 【3】的护身符
    int guarded = 0;                // 和 plain 一模一样的活，但套了锁

    // ——【2】转账组 ——
    QAtomicInt acctA;               // 起始 kAcct0
    QAtomicInt acctB;               // 起始 kAcct0；不变量 acctA+acctB == 2×kAcct0
    QAtomicInt holeOpen;            // 「缝正张着」的旗标
    QAtomicInt bystanderLooks;      // 旁观线程成功插进缝里的次数
    QAtomicInt bystanderBreaks;     // 其中看到总额不对的次数

    // ——【4】反序拿锁组 ——
    QMutex left;
    QMutex right;
    QAtomicInt naiveDone[2];        // 两条线程各自完成了多少轮
    QAtomicInt naiveBackoffs;       // tryLock 失败后主动松手的次数（只报实测）
    QAtomicInt scopedDone[2];
    QAtomicInt scopedStumbles;      // 换成 scoped_lock 之后这个必须是 0

    // ——【5】读写锁组 ——
    QReadWriteLock rw;              // 默认 NonRecursive（151 行那个 enum）
    int rwX = kSum5;                // 不变量：rwX + rwY == kSum5
    int rwY = 0;
    QAtomicInt rwTorn;              // 读者看到「和不对」的次数 —— 必须恒为 0
    QAtomicInt rwLiveReaders;       // 此刻在读锁里的读者数
    QAtomicInt rwReadPeak;          // 读者数峰值（CAS 打的）
    QAtomicInt rwWriteSteps;        // 写者真正走了多少步
    QAtomicInt rwReadSteps;         // 读者一共走了多少轮（合计核对用）

    // ——【6】条件变量组 ——
    QMutex qmutex;                  // wait() 要的就是**这一把**锁的指针
    QWaitCondition cond;
    QQueue<int> queue;              // 受 qmutex 保护；出了锁作用域就不许碰
    bool done = false;              // 「不会再有货了」，同样受 qmutex 保护
    QAtomicInt consumedCount;
    QAtomicInt consumedSum;
    QAtomicInt emptyWakes;          // wait 返回了、队列却还是空的次数

    // ——【7】信号量组 ——
    QSemaphore sem{ kSlots7 };      // 许可数就是硬上限（19 行 explicit 构造）
    QAtomicInt live;                // 此刻占着许可的人数
    QAtomicInt peak;                // 峰值，必须 <= kSlots7
    QAtomicInt jobsDone;
    QAtomicInt acquireTimeouts;     // tryAcquire 失败次数（只报实测）

    // ——【8】线程封闭组 ——
    // ★ 名字为什么是 mergeSlots 而不是 slots：这不是口味，是**本讲现场踩到的真坑**。
    //   qtmetamacros.h 44/45 行（在 `#if defined(QT_NO_KEYWORDS)` 的 else 分支里，
    //   40/43 行是两道开关）有这么两句：
    //       # define slots Q_SLOTS
    //       # define signals Q_SIGNALS
    //   也就是 `slots` 是一个**宏**。于是 `std::vector<qint64> slots;` 经过预处理变成
    //   `std::vector<qint64> Q_SLOTS;` —— 声明符没了，编译器只看见一个类型加一个注解，
    //   报的是「declaration does not declare anything」，跟你写的东西看起来毫无关系。
    //   （188~189 行还有一组 `#define slots slots`，那是给 moc 自己看的，
    //     因为整段 38~53 行外面包着 `#ifndef Q_MOC_RUN`。）
    //   三条出路：成员改名（本讲这条）；工程级定义 QT_NO_KEYWORDS（老代码里常见，
    //   代价是全项目必须写 Q_SLOT/Q_SIGNAL）；或者只在本文件定义
    //   QT_NO_SIGNALS_SLOTS_KEYWORDS（43 行那个开关，只关掉 slots/signals 这两个词）。
    //   教训：**Qt 的四个「关键字」是宏，不是保留字**，所以它们会吃掉任何同名标识符。
    //   emit 同理（53 行 `#define emit`，而且 QT_NO_KEYWORDS 时 41 行会打开
    //   QT_NO_EMIT 连 emit 一起关掉）。
    std::vector<qint64> mergeSlots;  // 每线程一格，**互不相交**，所以一把锁都不需要
};

// ============================================================================
// 三、locker 的四种「写错形状」（本机 g++ 实测诊断，原文照抄）
// ----------------------------------------------------------------------------
// 这一节整段是注释，因为其中两种**根本编译不过** —— 把编译不过的东西放进代码里，
// 这个文件就交付不了。但它们的价值恰恰在于「编译器怎么骂你」，所以我把手工
// 跑出来的诊断文本原样留下，你对着自己机器上的报错认一次就记住了。
// 下面假设 `QMutex m; QReadWriteLock rw;`，且这两行都写在**函数体内**。
//
// (1) `QMutexLocker(&m);`
//     → error: Explicitly typed instantiation of the C++17 class template
//              'QMutexLocker' not available with parentheses; use braces instead
//     为什么：qmutex.h 241~242 行说了，QMutexLocker 现在是个**模板**。
//     带括号的「类型 + (表达式)」在 C++ 里先按声明解析，而模板名不能用括号做
//     推导 —— 编译器直接把话说到这份上（"use braces instead"）。
//     ★ 这个错只在 C++17 起才有；Qt5 时代 QMutexLocker 不是模板，
//       同一行写出来是另一个故事（见下面 (2)）。
//
// (2) `QReadLocker(&rw);`   /   `QMutexLocker<QMutex>(&m);`
//     → error: conflicting declaration 'QReadLocker & rw'
//     → note : 'rw' has a previous declaration as 'QReadWriteLock rw'
//     这就是 C++ 最有名的 **most vexing parse**：`QReadLocker(&rw)` 被解析成
//     「声明一个叫 rw 的 QReadLocker 引用」，于是跟上面那行 `QReadWriteLock rw;`
//     撞名。它**不是 Qt 的坑，是 C++ 的坑**，Qt5 的 README 里都写过这条。
//     认法：报错里出现 "conflicting declaration" + 同一个变量名，先怀疑这一条。
//
// (3) `QMutexLocker{&m};`   /   `QSemaphoreReleaser{sem};`   /   `QReadLocker{nullptr};`
//     → warning: ignoring return value of 'QMutexLocker<Mutex>::QMutexLocker(Mutex*)
//                [with Mutex = QMutex]', declared with attribute 'nodiscard'
//                [-Wunused-result]
//     花括号能编译（(1) 说的就是这条路），但**它是匿名临时量**：语句结束时析构，
//     于是「加锁 → 立刻解锁」，保护了个寂寞。qmutex.h 251 行那句 Q_NODISCARD_CTOR
//     就是专门给这种写法挂铃铛的（qreadwritelock.h 248/293、qsemaphore.h 67/70/72/75
//     同理）。所以看到这条 warning 别去抑制它，去给变量起个名字。
//
// (4) `QMutexLocker locker(&m);`   ← 唯一正确形状，但它有自己的失效方式
//     本机实测：**这一行单独放在函数里，0 error 0 warning**。
//     也就是说 (4) 的 bug 编译器抓不到 —— 它太合法了。真正的错法是
//     **作用域不对**：把 locker 放在 if 的花括号里，出了那个括号锁就放了，
//     下面几行你以为在保护里，其实早就出来了。这是本讲最想让你记住的形状：
//     「RAII 写对了语法，写错了作用域」= 一个连编译器和 warning 都不吭声的 bug。
//     纪律：需要跨 if 保护，就在**函数开头**建 locker，或者自己写一对大括号
//     把临界区明确圈出来（本讲【3】循环体里那种就是圈内）。
//     还有一条：qmutex.h 261~262 行有移动构造、269 行有析构、305 行 Q_DISABLE_COPY
//     —— 所以 locker 可以 `auto lk2 = std::move(lk);`（所有权转移，解锁责任跟着走），
//     但不能拷。看到 `QMutexLocker lk2 = lk;` 编译不过，别慌，那是它在救你。
// ============================================================================

// ============================================================================
// 四、main：八节同步顺序跑
// ----------------------------------------------------------------------------
// 再强调一遍本讲的偏离：这里**没有 QTimer::singleShot 串步骤**，也没有 app.exec()。
// 因为要观察的是锁和等待，不是事件调度；八节按顺序跑完就 return 0。
// QCoreApplication 还是要建（Qt 不少内部设施认这个单例），但一次循环都不进。
// ============================================================================
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QThread *mainThread = QThread::currentThread();
    mainThread->setObjectName(QStringLiteral("main"));

    Shared s;
    s.acctA.storeRelease(kAcct0);              // 52 行 storeRelease，等价于「赋值 + 发布」
    s.acctB.storeRelease(kAcct0);
    s.rwX = kSum5;                             // 【5b】的不变量：rwX + rwY 恒等于 kSum5
    s.rwY = 0;
    s.mergeSlots.resize(kMergeThreads, 0);          // 【8】先分配好，之后只按下标写，**不再扩容**

    qInfo().noquote() << QStringLiteral(
        "第 4 讲 · 同步 —— 本机主线程 %1，理想线程数 %2\n"
        "口径：kThreads=%3 kIter=%4 kRounds2=%5 kWindowMs=%6 kRounds4=%7\n"
        "      kReaders5=%8 kReadSteps=%9 kWriteSteps=%10 kItems6=%11 kSlots7=%12\n"
        "      kWorkers7=%13 kJobs7=%14 kMergeThreads=%15 kMergeTo=%16\n"
        "提醒：本讲八节全部同步顺序跑，不进事件循环；每节末尾一行核对，"
        "[OK] 是可判定结论，[预期中] 是概率性结论，[实测] 只报数不承诺。")
        .arg(who())
        .arg(QThread::idealThreadCount())
        .arg(kThreads).arg(kIter).arg(kRounds2).arg(kWindowMs).arg(kRounds4)
        .arg(kReaders5).arg(kReadSteps).arg(kWriteSteps).arg(kItems6).arg(kSlots7)
        .arg(kWorkers7).arg(kJobs7).arg(kMergeThreads).arg(kMergeTo);

    // ========================================================================
    // 【1】裸 int 与原子 int：同一个循环，两种命运
    // ========================================================================
    section(QStringLiteral("【1】裸 ++ 与原子 ++（先演一次必然错的，再跑一次不保证的）"));

    // ——— 1a：确定性演出。单线程，手工把两条线程的三步交错排出来 ———
    // `++x` 在机器上是三件事：读 → 加 → 写回。所谓竞争，就是别人的三件事
    // 插进了你的三件事中间。这里我把「别人」也写成你自己的三行，
    // 于是结果**必然**是丢更新那一个 —— 这比「跑十次错一次」好懂得多，
    // 也硬得多：它是可以直接看的证据，不是概率。
    s.plain = kAcctOpen;
    const int snapA = s.plain;                    // 线程 A 读
    const int snapB = s.plain;                    // 线程 B 也读（A 还没来得及写回）
    s.plain = snapA + 1;                          // A 写回：+1 生效
    s.plain = snapB + 1;                          // B 写回：把 A 那一次**覆盖掉了**
    qInfo().noquote() << QStringLiteral(
        "  1a 演出：起始 %1；两条线程各自「+1」→ 应当是 %2，实际是 %3。")
        .arg(kAcctOpen).arg(kAcctOpen + 2).arg(s.plain);
    qInfo().noquote() << QStringLiteral(
        "     丢掉的不是「数据」，是一次已经发生的**副作用**。这就是为什么竞争 bug "
        "查起来费劲：程序看起来完全正常，只是账少了一笔。");
    verdict(Hard, QStringLiteral("1a 结果 == 起始 + 1（丢了一次更新）"),
            s.plain == kAcctOpen + 1, QString::number(s.plain));

    // ——— 1b：真并发。同一个循环体里两件事各干一遍 ———
    s.plain = 0;
    s.atomicCount.storeRelease(0);
    QElapsedTimer t1;
    t1.start();
    const bool done1 = runThreads(kThreads, [&s](int) {
        for (int i = 0; i < kIter; ++i) {
            ++s.plain;                    // ← 竞争：编译器有权把整个循环折叠成一次 +=
            ++s.atomicCount;              // ← 145 行 operator++（fetchAndAddOrdered + add_no_UB）
        }
    }, kJoinMs, QStringLiteral("【1】"));
    const qint64 ms1 = t1.elapsed();
    const int gotPlain = s.plain;
    const int gotAtomic = s.atomicCount.loadAcquire();
    qInfo().noquote() << QStringLiteral("  1b 真并发：目标 %1，裸 int 得到 %2，原子得到 %3，耗时 %4ms，收尸全部按时：%5")
        .arg(kThreads * kIter).arg(gotPlain).arg(gotAtomic).arg(ms1).arg(done1);
    verdict(Hard, QStringLiteral("1b 原子计数 == kThreads×kIter"),
            gotAtomic == kThreads * kIter, QString::number(gotAtomic));
    verdict(Obs, QStringLiteral("1b 裸 int（**任何值都合法**，包括正好等于目标值）"),
            true, QStringLiteral("%1%2").arg(gotPlain)
                      .arg(gotPlain == kThreads * kIter
                               ? QStringLiteral("  ← 看着对！这正是本节的靶心")
                               : QStringLiteral("  ← 这次被观察到了")));
    qInfo().noquote() << QStringLiteral(
        "     上一行要是打了「看着对」，别急着删锁。**看不见 ≠ 不存在**：\n"
        "     · 无竞争时它只是「两条线程没真的同时跑到」；机器一忙、核一多、\n"
        "       版本一优化，插队就出现了 —— 你的测试环境恰好是它最不容易出现的那一侧。\n"
        "     · 而 `++s.plain` 本身是**未定义行为**：编译器不需要对你保证任何东西，\n"
        "       包括「至少不会更坏」。所以判据不是「跑出来对不对」，是「有没有竞态」。\n"
        "     · 要钉死它，用 ThreadSanitizer：`-fsanitize=thread`。本讲不跑它（跑起来要等\n"
        "       好几分钟，而且 Qt 的锁里本来就埋了注解 —— qreadwritelock.h 88~89/97~98/134 行\n"
        "       那些 QtTsan:: 调用就是给 TSan 看的），但这一条请记住：**TSan 报的是可能性，\n"
        "       你的日志报的是这一次。** 前者才是你要修的东西。");

    // ========================================================================
    // 【2】两个原子操作 ≠ 一个复合原子操作
    // ========================================================================
    section(QStringLiteral("【2】转账：每一步都原子，中间那道缝照样是错的"));

    // ——— 2a：把那道缝直接打印出来（单线程，必然出现） ———
    const int before2 = s.acctA.loadAcquire() + s.acctB.loadAcquire();
    s.acctA.fetchAndAddOrdered(-1);              // 105 行：扣款完成，入账还没发生
    const int inHole2 = s.acctA.loadAcquire() + s.acctB.loadAcquire();
    s.acctB.fetchAndAddOrdered(+1);              // 入账
    const int after2 = s.acctA.loadAcquire() + s.acctB.loadAcquire();
    qInfo().noquote() << QStringLiteral("  2a 总额：扣款前 %1 → 扣完还没入账 %2 → 入账后 %3")
        .arg(before2).arg(inHole2).arg(after2);
    verdict(Hard, QStringLiteral("2a 「缝里少 1」且两头发额相等"),
            before2 == 2 * kAcct0 && after2 == before2 && inHole2 == before2 - 1,
            QStringLiteral("%1 / %2 / %3").arg(before2).arg(inHole2).arg(after2));
    qInfo().noquote() << QStringLiteral(
        "     这一行不需要多线程就能看出来：问题不在「两个 fetchAndAddOrdered 各自会不会坏」\n"
        "     （不会），而在**它们之间没有任何东西**。锁买的从来不是「原子的一步」，\n"
        "     是「把 N 步钉成一个别人插不进来的区间」。");

    // ——— 2b：真并发 + 一条旁观线程专门往缝里看 ———
    // 缝宽 kWindowMs：真代码里这一步是「写库 + 发通知 + 更新缓存」，
    // 也就是你在临界区外面做的那段活。这里用 msleep 把它拉宽到肉眼可见。
    QAtomicInt xferLeft;
    xferLeft.storeRelease(2);                   // 两条转账线程
    const int invariant = 2 * kAcct0;
    s.holeOpen.storeRelease(0);
    s.bystanderLooks.storeRelease(0);
    s.bystanderBreaks.storeRelease(0);
    const bool done2 = runThreads(3, [&](int slot) {
        if (slot < 2) {
            for (int r = 0; r < kRounds2; ++r) {
                s.acctA.fetchAndAddOrdered(-1);
                s.holeOpen.storeRelease(1);      // 缝张开了
                QThread::msleep(kWindowMs);      // ← 真代码里这里是「一段不该在锁外的活」
                s.holeOpen.storeRelease(0);
                s.acctB.fetchAndAddOrdered(+1);
            }
            xferLeft.fetchAndAddOrdered(-1);
            qInfo().noquote() << QStringLiteral("  2b %1 转账 %2 轮完成").arg(who()).arg(kRounds2);
        } else {
            int looks = 0, breaks = 0;
            while (xferLeft.loadAcquire() > 0) {
                if (s.holeOpen.loadAcquire() == 1) {
                    ++looks;
                    const int sum = s.acctA.loadAcquire() + s.acctB.loadAcquire();
                    if (sum != invariant)
                        ++breaks;               // 别的线程正在看你「钱在路上」的那个状态
                } else {
                    QThread::usleep(200);
                }
            }
            s.bystanderLooks.storeRelease(looks);
            s.bystanderBreaks.storeRelease(breaks);
            qInfo().noquote() << QStringLiteral("  2b 旁观者 %1 收工：插进缝里看 %2 次，其中 %3 次总额不对")
                .arg(who()).arg(looks).arg(breaks);
        }
    }, kJoinMsSlow, QStringLiteral("【2】"));
    const int finalSum = s.acctA.loadAcquire() + s.acctB.loadAcquire();
    qInfo().noquote() << QStringLiteral("  2b 全部结束后总额回到 %1（每轮一扣一还，净变化 0），收尸按时 %2")
        .arg(finalSum).arg(done2);
    verdict(Hard, QStringLiteral("2b 结束后总额 == 不变量"),
            finalSum == invariant, QStringLiteral("%1").arg(finalSum));
    verdict(Soft, QStringLiteral("2b 旁观次数 > 0 且「看到不对」== 旁观次数"),
            s.bystanderLooks.loadAcquire() > 0
                && s.bystanderBreaks.loadAcquire() == s.bystanderLooks.loadAcquire(),
            QStringLiteral("looks=%1 breaks=%2")
                .arg(s.bystanderLooks.loadAcquire()).arg(s.bystanderBreaks.loadAcquire()));
    qInfo().noquote() << QStringLiteral(
        "     注意区分这两件事：**2b 的「净额回到原值」是可判定的**（每条线程加减对称），\n"
        "     所以它写在 [OK] 那一类；**「旁观到几次」是概率**，所以它写在 [预期中]。\n"
        "     一条日志里有 0 次旁观，不代表没有缝 —— 只代表这次没人往里看。");

    // ========================================================================
    // 【3】QMutex + QMutexLocker：把 N 步钉成一个区间
    // ========================================================================
    section(QStringLiteral("【3】加锁的对照组：同样的循环，一个数都不许错"));

    s.guarded = 0;
    QElapsedTimer t3;
    t3.start();
    const bool done3 = runThreads(kThreads, [&s](int) {
        for (int i = 0; i < kIter; ++i) {
            QMutexLocker locker(&s.guard);      // 252 行 explicit 构造：唯一正确形状
            ++s.guarded;                        // 出了这个花括号才解锁（269 行析构）
        }
    }, kJoinMs, QStringLiteral("【3】"));
    const qint64 ms3 = t3.elapsed();
    qInfo().noquote() << QStringLiteral("  加锁版：目标 %1，实际 %2，耗时 %3ms（【1b】无锁版是 %4ms），收尸按时 %5")
        .arg(kThreads * kIter).arg(s.guarded).arg(ms3).arg(ms1).arg(done3);
    verdict(Hard, QStringLiteral("【3】guarded == kThreads×kIter（必须正好）"),
            s.guarded == kThreads * kIter, QString::number(s.guarded));
    qInfo().noquote() << QStringLiteral(
        "     两个数并排看：锁把「正确」变成了承诺，代价是把并发变成了排队。\n"
        "     %1ms 与 %2ms 的差就是这把锁的账单 —— 但**别拿这个差去论证「不该加锁」**，\n"
        "     该论证的是「这段临界区能不能整个搬进一把锁之外」（【8】）或者缩到多小。\n"
        "     顺带一句：临界区里只留**改数据**那几行，把耗时的事（IO、发信号、算字符串）\n"
        "     挪出去 —— 这不是风格洁癖，是账单。").arg(ms1).arg(ms3);

    // isLocked / unlock / relock：把「锁现在在谁手里」这个状态亲手摸一遍
    {
        QMutexLocker locker(&s.guard);
        const bool s1 = locker.isLocked();          // 275 行
        locker.unlock();                            // 280 行 + 282 行那句 Q_ASSERT(m_isLocked)
        const bool s2 = locker.isLocked();
        locker.relock();                            // 287 行 + 289 行那句 Q_ASSERT(!m_isLocked)
        const bool s3 = locker.isLocked();
        qInfo().noquote() << QStringLiteral("  lock→unlock→relock 的 isLocked() 序列：%1 %2 %3")
            .arg(s1).arg(s2).arg(s3);
        verdict(Hard, QStringLiteral("【3】isLocked 序列 == true,false,true"),
                s1 && !s2 && s3, QStringLiteral("%1,%2,%3").arg(s1).arg(s2).arg(s3));
        qInfo().noquote() << QStringLiteral(
            "     这三句都合法，因为每次状态转换都**配对着做**了。\n"
            "     282/289 行那两句 Q_ASSERT 拦的是「重复 unlock」「没持有就 unlock」——\n"
            "     Debug 构建当场断言，Release 构建（NDEBUG 把 Q_ASSERT 变成空操作）就是\n"
            "     未定义行为。所以「Release 跑得好」永远不能当作「锁写对了」的证据。\n"
            "     unlock/relock 这对工具只在一种场合值得用：等一段慢活时把锁放开，\n"
            "     回来再 relock —— 而这一放手，中间的数据随时可能变，必须重新校验。");
    }
    // 再补一条硬钉子：裸 QMutex 的 unlock 也有断言，且在**基类**里
    qInfo().noquote() << QStringLiteral(
        "     qmutex.h 52 行那句 `Q_ASSERT(d_ptr.loadRelaxed()); //mutex must be locked` ——\n"
        "     意思是「没锁着就 unlock」在 QMutex 本体上同样有检测。另外看 41/51 行：\n"
        "     lock/unlock 都写死在**基类 QBasicMutex**（23 行）里，派生 QMutex（125 行）\n"
        "     只多了「带期限的 tryLock」和 145 行那句 `using QBasicMutex::tryLock;` ——\n"
        "     少了那句 using，`m.tryLock()`（无参）会被派生类的两个同名重载**隐藏**掉，\n"
        "     直接编译不过。C++ 的名字隐藏规则在 Qt6 的锁上是有代价的，Qt 自己也在补。");

    // ========================================================================
    // 【4】死锁：两条线程拿锁顺序不一致
    // ========================================================================
    section(QStringLiteral("【4】反序拿锁：把「挂死」改写成「一个可以打印的 false」"));

    qInfo().noquote() << QStringLiteral(
        "  场景：左桶和右桶都要动。线程 0 按「先左后右」拿，线程 1 按「先右后左」拿。\n"
        "  两边都用 lock() 的话，各拿一把再各等对方 —— 死锁，而且**不崩不报错、CPU 0%**，\n"
        "  日志停在半句上。这种现场没法用「跑一遍看看」发现，所以本讲不跑真死锁，\n"
        "  改成第二把锁用 tryLock(空期限)：拿不到就**松手 + 退让**，于是死锁变统计量。");
    for (int i = 0; i < 2; ++i)
        s.naiveDone[i].storeRelease(0);
    s.naiveBackoffs.storeRelease(0);
    const bool done4a = runThreads(kThreads, [&](int slot) {
        QMutex *first  = (slot == 0) ? &s.left : &s.right;
        QMutex *second = (slot == 0) ? &s.right : &s.left;   // ← 顺序故意相反，这就是全部病根
        // ★ 循环条件是「干够 kRounds4 轮」而不是「试 kRounds4 次」：退让之后必须**重试**，
        //   否则这一轮的活就丢了。「拿不到就走」只在真能降级的场合才允许丢活（回一句
        //   「忙，稍后再试」），这里不是那种场合，所以是 while 不是 for。
        while (s.naiveDone[slot].loadAcquire() < kRounds4) {
            first->lock();
            if (second->tryLock(QDeadlineTimer())) {         // 33 行 explicit QDeadlineTimer；空的一把 = 不等
                second->unlock();
                first->unlock();
                s.naiveDone[slot].fetchAndAddOrdered(1);
            } else {
                first->unlock();                             // ★ 这一句就是「不自家锁上等着」
                s.naiveBackoffs.fetchAndAddOrdered(1);
                QThread::usleep(50);                         // 退让：别两条线程互相踩点
            }
        }
    }, kJoinMsSlow, QStringLiteral("【4a】"));
    const int naiveTotal = s.naiveDone[0].loadAcquire() + s.naiveDone[1].loadAcquire();
    qInfo().noquote() << QStringLiteral("  4a 完成轮数 %1 + %2 = %3（目标 %4），退让 %5 次，收尸按时 %6")
        .arg(s.naiveDone[0].loadAcquire()).arg(s.naiveDone[1].loadAcquire())
        .arg(naiveTotal).arg(2 * kRounds4)
        .arg(s.naiveBackoffs.loadAcquire()).arg(done4a);
    verdict(Hard, QStringLiteral("4a 完成数之和 == 2×kRounds4（退让不丢活）"),
            naiveTotal == 2 * kRounds4, QString::number(naiveTotal));
    verdict(Obs, QStringLiteral("4a 退让次数（**每一次都是一个「换成 lock() 就是死锁」的现场**）"),
            true, QString::number(s.naiveBackoffs.loadAcquire()));
    // 收工之后两把锁必须是松的 —— 用「不等待」的 tryLock 当场验一次
    {
        const bool lFree = s.left.tryLock(QDeadlineTimer());
        const bool rFree = s.right.tryLock(QDeadlineTimer());
        if (lFree) s.left.unlock();
        if (rFree) s.right.unlock();
        verdict(Hard, QStringLiteral("4a 结束后两把锁都真的松着"),
                lFree && rFree, QStringLiteral("left=%1 right=%2").arg(lFree).arg(rFree));
    }

    // 结构性解法：一次锁多个，顺序由算法决定，不由程序员决定
    for (int i = 0; i < 2; ++i)
        s.scopedDone[i].storeRelease(0);
    s.scopedStumbles.storeRelease(0);
    const bool done4b = runThreads(kThreads, [&](int slot) {
        for (int r = 0; r < kRounds4; ++r) {
            std::scoped_lock lk(s.left, s.right);   // <mutex>；内部是死锁避免算法
            // 谁先拿到哪个由算法决定 —— 所以**写代码的人不需要约定顺序**
            // 下面三行是一个**反向探针**：QMutex 默认不可重入（188 行才是 QRecursiveMutex），
            // 所以「已经攥在手里还想再 tryLock 一次」必然失败。哪天真成功了，说明
            // scoped_lock 并没有把两把都攥住 —— 那是事故，不是收获。
            if (s.left.tryLock(QDeadlineTimer())) {
                s.left.unlock();                            // 还回去，别破坏 scoped_lock 的所有权
                s.scopedStumbles.fetchAndAddOrdered(1);     // 记「意外」，本讲期望它恒为 0
            }
            ++s.scopedDone[slot];
        }
    }, kJoinMsSlow, QStringLiteral("【4b】"));
    const int scopedTotal = s.scopedDone[0].loadAcquire() + s.scopedDone[1].loadAcquire();
    qInfo().noquote() << QStringLiteral("  4b std::scoped_lock：完成 %1（目标 %2），反向探针成功 %3（期望 0），收尸按时 %4")
        .arg(scopedTotal).arg(2 * kRounds4).arg(s.scopedStumbles.loadAcquire()).arg(done4b);
    verdict(Hard, QStringLiteral("4b 完成数 == 2×kRounds4 且反向探针一次都没成功"),
            scopedTotal == 2 * kRounds4 && s.scopedStumbles.loadAcquire() == 0,
            QStringLiteral("done=%1 stumbles=%2").arg(scopedTotal).arg(s.scopedStumbles.loadAcquire()));
    qInfo().noquote() << QStringLiteral(
        "     4b 那个「反向探针成功 0 次」不是什么都没发生，它证明的正是\n"
        "     `std::scoped_lock` **把两把都攥在手里**了（攥不住才会 tryLock 成功）。\n"
        "     顺带把「递归锁不是修死锁的药」说一句：188 行那个 QRecursiveMutex 只是\n"
        "     把死锁改成「自己套自己」，等 unlock 次数凑够的时候，连你自己都数不清套了几层。");
    // 第三种写法：std::lock + adopt_lock（Qt 的锁满足 std 概念，所以这是合法搭配）
    {
        int rounds = 0;
        for (int r = 0; r < 200; ++r) {
            std::lock(s.left, s.right);                             // 不加任何锁，先由算法挑顺序
            std::unique_lock<QMutex> la(s.left, std::adopt_lock);   // 接管已经攥着的锁
            std::unique_lock<QMutex> lb(s.right, std::adopt_lock);
            ++rounds;                                               // 出作用域自动两把一起放
        }
        verdict(Hard, QStringLiteral("4c std::lock + std::adopt_lock 这一套在 QMutex 上可用"),
                rounds == 200, QString::number(rounds));
        qInfo().noquote() << QStringLiteral(
            "     三条路（tryLock 退让 / scoped_lock / std::lock+adopt_lock）都在同一行代码里，\n"
            "     选哪条不是口味问题：**需要「拿不到就走」的语义**才用第一条（比如超时降级、\n"
            "     给用户回一句「忙，稍后再试」），只是要「两把都攥住」就一律 scoped_lock。\n"
            "     而第一道防线永远是另一件事：把临界区收成一个，让「多把锁」这个需求消失。\n"
            "     QRecursiveMutex（188 行）不在本讲的推荐位上，理由上面那段已经说了。");
    }

    // ========================================================================
    // 【5】QReadWriteLock：读共享、写独占 —— 以及它到底在保护什么
    // ========================================================================
    section(QStringLiteral("【5】读写锁：先证明「读锁挡写者」，再证明「读者不互相挡」"));

    // ——— 5a：三个可判定的布尔 ———
    // 这里不用 runThreads：本节要「一条线程攥着锁、另一条在旁边试探」，
    // 而 runThreads 的语义是「一起开、一起收」，控制不到这么细。
    // 手工搭一次也有好处：你能看清 start/wait/delete 这三步各自的位置。
    QAtomicInt holding;                 // 1 = 读锁真的在人家手里
    QAtomicInt probesDone;              // 主线程试探完了，放行
    QThread *holder = QThread::create([&s, &holding, &probesDone]() {
        s.rw.lockForRead();             // 23 行
        holding.storeRelease(1);
        QDeadlineTimer patience(5000);  // 攥着等人来试探，但**绝不无限攥**
        while (!probesDone.loadAcquire() && !patience.hasExpired())
            QThread::usleep(200);
        s.rw.unlock();                  // 49 行（54 行那句 Q_ASSERT_X 就在这次调用里检查）
        holding.storeRelease(0);
        qInfo().noquote() << QStringLiteral("  5a %1 已把读锁交还").arg(who());
    });
    holder->setObjectName(QStringLiteral("【5a】holder"));
    holder->start();
    {
        QDeadlineTimer patience(3000);
        while (!holding.loadAcquire() && !patience.hasExpired())
            QThread::usleep(200);
        const bool writeBlocked = s.rw.tryLockForWrite(QDeadlineTimer());   // 期望 false
        const bool readShared   = s.rw.tryLockForRead(QDeadlineTimer());    // 期望 true
        if (readShared)
            s.rw.unlock();            // 只试探，马上还（别把自己变成第二个长期持有者）
        probesDone.storeRelease(1);   // 放行
        qInfo().noquote() << QStringLiteral(
            "  5a 有人持读锁时：主线程 tryLockForWrite() = %1，tryLockForRead() = %2")
            .arg(writeBlocked).arg(readShared);
        verdict(Hard, QStringLiteral("5a 「读锁挡写者、不挡读者」这两个布尔"),
                !writeBlocked && readShared,
                QStringLiteral("write=%1 read=%2").arg(writeBlocked).arg(readShared));
    }
    const bool holderDone = holder->wait(QDeadlineTimer(kJoinMs));
    delete holder;                              // 先 wait 成功，再 delete（本讲第 1 条纪律）
    {
        const bool writeFree = s.rw.tryLockForWrite(QDeadlineTimer());   // 期望 true
        if (writeFree)
            s.rw.unlock();
        qInfo().noquote() << QStringLiteral("  5a 持锁线程已退出（wait 按时 %1）之后 tryLockForWrite() = %2")
            .arg(holderDone).arg(writeFree);
        verdict(Hard, QStringLiteral("5a 读锁释放后写锁立刻可得"), writeFree && holderDone,
                QString::number(writeFree));
        qInfo().noquote() << QStringLiteral(
            "     三个布尔值都是**语义级**结论，不是概率：读锁存在时写锁必须拿不到，\n"
            "     读锁之间必须互不相干。为什么这里全用 QDeadlineTimer()（空期限）：\n"
            "     它是 Qt 自己表示「不等待，试一下就返回」的写法 —— 看 qreadwritelock.h\n"
            "     27~29 行，无参的 tryLockForRead() 本体就是 tryLockForReadInternal(QDeadlineTimer(), …)。\n"
            "     换成 lockForWrite() 就会**在这儿堵住**，本讲就变成死锁演示了。\n"
            "     ★ 另外记一条 C++ 的：派生类 QReadWriteLock（148 行）里那几句\n"
            "       `using QBasicReadWriteLock::tryLockForRead;/::tryLockForWrite;`（168/180 行）\n"
            "       就是为了让「无参那一版」还找得到。写代码时你不会碰到它，\n"
            "       但自己派生一个锁包装类时一定会 —— 忘了 using 就是"
            "「error: no matching function for call to ...」。");
    }

    // ——— 5b：1 个写者 + 4 个读者，证明读者看到的永远是「完整的状态」 ———
    // 不变量：rwX + rwY == kSum5。写者每步做两件事（x+1、y-1），
    // 单看每一步都不破坏不变量？不对 —— **单独做其中一件就破坏了**（和变成 kSum5+1）。
    // 所以这一节测的就是：这两步之间，读者会不会正好闯进来。
    s.rwTorn.storeRelease(0);
    s.rwReadPeak.storeRelease(0);
    s.rwLiveReaders.storeRelease(0);
    s.rwWriteSteps.storeRelease(0);
    s.rwReadSteps.storeRelease(0);
    const bool done5 = runThreads(kReaders5 + 1, [&](int slot) {
        if (slot == 0) {
            for (int i = 0; i < kWriteSteps; ++i) {
                QWriteLocker wl(&s.rw);         // 290 行：独占
                ++s.rwX;                        // ← 这两行之间，就是【2】那道缝
                --s.rwY;                        // ← 靠这把写锁，读者进不来
                s.rwWriteSteps.fetchAndAddOrdered(1);
            }
        } else {
            int localTorn = 0;
            for (int i = 0; i < kReadSteps; ++i) {
                QReadLocker rl(&s.rw);          // 245 行：共享
                const int now = s.rwLiveReaders.fetchAndAddOrdered(1) + 1;
                raiseMax(s.rwReadPeak, now);    // 有几个读者「同时在里面」
                const int sum = s.rwX + s.rwY;  // ← 本节的靶心：这一眼必须看到完整的和
                s.rwLiveReaders.fetchAndAddOrdered(-1);
                if (sum != kSum5)
                    ++localTorn;
            }
            s.rwTorn.fetchAndAddOrdered(localTorn);
            s.rwReadSteps.fetchAndAddOrdered(kReadSteps);
        }
    }, kJoinMsSlow, QStringLiteral("【5b】"));
    qInfo().noquote() << QStringLiteral("  5b 读者共看 %1 轮（%2 条 × %3），写者走了 %4 步，看到「和不对」%5 次，读者峰值 %6，收尸按时 %7")
        .arg(s.rwReadSteps.loadAcquire()).arg(kReaders5).arg(kReadSteps)
        .arg(s.rwWriteSteps.loadAcquire()).arg(s.rwTorn.loadAcquire())
        .arg(s.rwReadPeak.loadAcquire()).arg(done5);
    verdict(Hard, QStringLiteral("5b 撕裂次数 == 0（这就是读写锁在干的活）"),
            s.rwTorn.loadAcquire() == 0, QString::number(s.rwTorn.loadAcquire()));
    verdict(Hard, QStringLiteral("5b 写步数与读轮数都跑满"),
            s.rwWriteSteps.loadAcquire() == kWriteSteps
                && s.rwReadSteps.loadAcquire() == kReaders5 * kReadSteps,
            QStringLiteral("%1 / %2").arg(s.rwWriteSteps.loadAcquire())
                                     .arg(s.rwReadSteps.loadAcquire()));
    verdict(Soft, QStringLiteral("5b 读者峰值 >= 2（读锁真的允许并发）"),
            s.rwReadPeak.loadAcquire() >= 2, QString::number(s.rwReadPeak.loadAcquire()));
    qInfo().noquote() << QStringLiteral(
        "     两条使用手册不会写的实话：\n"
        "     · 峰值这一栏要是经常是 1，说明写者把读者全挡在外面了 —— 读写锁在\n"
        "       「读多写少」时才赢；写得不算少的时候它常常**比 QMutex 还慢**，因为它要记账\n"
        "       （reader 数、等待者标志都在那个 d 指针里）。别默认「读写锁更快」。\n"
        "     · 6.12 的实现里 `tryLockForRead()` 那条快路径是原子的 CAS，只有 contention\n"
        "       才走 contendedTryLockForRead（86 行 tryLockForReadInternal → 132 行那两个\n"
        "       Q_CORE_EXPORT 的 contended* 函数）。也就是说**没竞争时它几乎不花钱**，\n"
        "       有竞争时它比 QMutex 贵。这正是「读多写少才用」这句口诀的机器层理由。\n"
        "     · 还有一条 QReadLocker 自己的：285 行那句 Q_ASSERT_X 检查指针最低位，\n"
        "       因为 locker 用 q_val 的**最低位**存「这把锁是 Recursive 还是 NonRecursive」\n"
        "       （看 unlock 里 `if ((q_val & 1u) == 1u)` 那段）。一个没对齐的锁对象，\n"
        "       会把这一位吃掉 —— 这种断言看着玄，其实是内存布局检查。");

    // ——— 5c：读锁**不能**升级成写锁，正确的「先看后改」长这样 ———
    {
        int snapshotSum = 0;
        {
            QReadLocker rl(&s.rw);
            snapshotSum = s.rwX + s.rwY;      // 第一步：在锁里**只看**
        }                                     // ★ 必须在这里把读锁放掉，才能谈写锁
        {
            QWriteLocker wl(&s.rw);
            const int nowSum = s.rwX + s.rwY; // 第二步：在写锁里**重新校验**
            s.rwX = kSum5;                    // 然后才敢改
            s.rwY = 0;
            qInfo().noquote() << QStringLiteral(
                "  5c 读锁里看到的和 = %1，放开读锁、拿住写锁时已经是 %2 —— 所以校验必须重做")
                .arg(snapshotSum).arg(nowSum);
        }
        const int restored = s.rwX + s.rwY;
        verdict(Hard, QStringLiteral("5c 复位之后不变量回来"), restored == kSum5,
                QString::number(restored));
        qInfo().noquote() << QStringLiteral(
            "     反面教材（本讲**不跑**）：持有读锁时直接 `lockForWrite()`。\n"
            "     NonRecursive（151 行默认）模式下，写锁要等**所有**读者出去，其中就包括你自己；\n"
            "     而你要等写锁拿到才肯出去。两条要求互相取消 —— 死锁，不报错，不崩。\n"
            "     为什么我不敢跑：这个演示程序从此停在那一行，日志停在半句上，\n"
            "     你会以为是机器的问题。**「我知道它会死锁」不构成把死锁放进 CI 的理由。**\n"
            "     真要「先读后改」，就照上面那样：放掉读锁、拿写锁、重读重校验。\n"
            "     中间那一瞬别人可能改过，所以校验不是可省的仪式感 —— 它是这个模式的全部意义。\n"
            "     想省事可以试 Recursive 模式（154 行那个构造参数），但 Qt 的文档和实现都只\n"
            "     允许「读锁递归」和「写锁递归」，**从读升到写照样不行**，别指望换个参数绕过物理规律。");
    }

    // ========================================================================
    // 【6】QWaitCondition：wait() 返回 ≠ 条件成立
    // ========================================================================
    section(QStringLiteral("【6】条件变量：先钉死「超时返回 false」，再跑生产消费"));

    // ——— 6a：确定性的一条 ———
    bool woke6 = true;                              // 提出来是为了最后一屏能把它一起报掉
    {
        QMutexLocker lk(&s.qmutex);                 // wait 的前提：锁**已经**握在手里
        QElapsedTimer tw;
        tw.start();
        const bool woke = s.cond.wait(&s.qmutex, QDeadlineTimer(kWaitMs6));   // 24~25 行
        woke6 = woke;
        const qint64 spent = tw.elapsed();
        qInfo().noquote() << QStringLiteral("  6a 没人通知：wait(&m, %1ms) 返回 %2，实际等了 %3ms，isLocked 仍然是 %4")
            .arg(kWaitMs6).arg(woke).arg(spent).arg(lk.isLocked());
        verdict(Hard, QStringLiteral("6a 超时返回 false 且用满了期限"),
                !woke && spent >= kWaitMs6, QStringLiteral("woke=%1 spent=%2").arg(woke).arg(spent));
        qInfo().noquote() << QStringLiteral(
            "     两个细节：① wait 会**临时放开**这把锁再睡，返回前重新拿回来 ——\n"
            "       所以外面那个 QMutexLocker 全程认为自己是锁着的（上一行 isLocked=true），\n"
            "       而中间那一小段确实是放开的。这就是「wait 必须配一把外部已锁的锁」的原因。\n"
            "     ② 26 行那个 `wait(QMutex*, unsigned long)` 是老写法，28~30 行还给了\n"
            "       QReadWriteLock 版重载，35~36 行给了 std 名字 notify_one/notify_all\n"
            "       （本体就是 wakeOne/wakeAll）。39 行 Q_DISABLE_COPY —— 条件变量不可拷贝，\n"
            "       「连对象一起复制一份」在 Qt 和 std 里都是编译错误，这是好事。");
    }

    // ——— 6b：生产 / 消费，谓词循环是唯一正确形状 ———
    {
        QMutexLocker lk(&s.qmutex);
        s.queue.clear();
        s.done = false;
    }
    s.consumedCount.storeRelease(0);
    s.consumedSum.storeRelease(0);
    s.emptyWakes.storeRelease(0);
    const bool done6 = runThreads(2, [&](int slot) {
        if (slot == 0) {                                  // 消费者
            QMutexLocker lk(&s.qmutex);                   // 全程握着同一把锁
            int localCount = 0;
            qint64 localSum = 0;
            int localEmpty = 0;
            for (;;) {
                // ★ 谓词循环：while，不是 if。wait 有三种「回来了但事情没成」：
                //   超时、虚假唤醒、别人 wakeAll 但货被第三条线程抢走了。
                while (s.queue.isEmpty() && !s.done) {
                    if (!s.cond.wait(&s.qmutex, QDeadlineTimer(kWaitMs6b))) {
                        if (s.queue.isEmpty())
                            ++localEmpty;                 // 「wait 返回了，队列还是空」= 本节靶心
                    }
                }
                if (s.queue.isEmpty() && s.done)
                    break;                                // 真收工：没货了，也不会再有货
                if (!s.queue.isEmpty()) {
                    localSum += s.queue.dequeue();
                    ++localCount;
                }
            }
            s.consumedCount.storeRelease(localCount);
            s.consumedSum.storeRelease(int(localSum));
            s.emptyWakes.storeRelease(localEmpty);
            qInfo().noquote() << QStringLiteral("  6b 消费者收工：收到 %1 条、和 %2、空手而归 %3 次")
                .arg(localCount).arg(localSum).arg(localEmpty);
        } else {                                          // 生产者
            for (int v = 1; v <= kItems6; ++v) {
                { QMutexLocker pl(&s.qmutex); s.queue.enqueue(v); }
                s.cond.wakeOne();                         // 32 行：叫一个
            }
            { QMutexLocker pl(&s.qmutex); s.done = true; } // done 也在锁里改，规矩一致
            s.cond.wakeAll();                               // 33 行：收尾叫全部
        }
    }, kJoinMs, QStringLiteral("【6b】"));
    const int wantSum6 = (kItems6 * (kItems6 + 1)) / 2;
    qInfo().noquote() << QStringLiteral("  6b 期望：条数 %1、和 %2；实际条数 %3、和 %4；收尸按时 %5")
        .arg(kItems6).arg(wantSum6).arg(s.consumedCount.loadAcquire())
        .arg(s.consumedSum.loadAcquire()).arg(done6);
    verdict(Hard, QStringLiteral("6b 一条不丢、一条不重、和正确"),
            s.consumedCount.loadAcquire() == kItems6 && s.consumedSum.loadAcquire() == wantSum6,
            QStringLiteral("count=%1 sum=%2").arg(s.consumedCount.loadAcquire())
                                             .arg(s.consumedSum.loadAcquire()));
    verdict(Obs, QStringLiteral("6b 「wait 返回但队列仍空」的次数"),
            true, QString::number(s.emptyWakes.loadAcquire()));
    qInfo().noquote() << QStringLiteral(
        "     为什么这套不会丢唤醒：消费者是**握着锁**判断「空不空」的，而 wait 把\n"
        "     「放开锁 + 开始睡」做成了一步；生产者改队列时也必须先拿到同一把锁。\n"
        "     于是「我刚看完是空的，别人塞了并叫醒 —— 可我还没开始睡」这个窗口不存在。\n"
        "     反面写法 `if (s.queue.isEmpty()) wait(...);` 会怎样？wait 一回来就去 dequeue，\n"
        "     而队列可能还是空的 —— QQueue::dequeue() 在空队列上是断言，Debug 当场停。\n"
        "     这条比「虚假唤醒」更有说服力：**虚假唤醒是罕见的，谓词循环是免费的。**\n"
        "     顺带：唤醒该叫几个（wakeOne vs wakeAll）取决于「一条货能不能被一条线程消费」。\n"
        "     拿不准就 wakeAll —— 它多花的是几次「醒来发现没自己的活」，不是正确性问题。");

    // ========================================================================
    // 【7】QSemaphore：许可数是硬约束，不是提示
    // ========================================================================
    section(QStringLiteral("【7】信号量：先在单线程上钉死「不够就不给」，再让六条线程抢三个坑"));

    // ——— 7a：单线程，全部可判定 ———
    {
        const bool tookAll = s.sem.tryAcquire(kSlots7, QDeadlineTimer());    // 26 行
        const int afterTake = s.sem.available();                            // 35 行
        const bool tooMany = s.sem.tryAcquire(kSlots7 + 1, QDeadlineTimer());
        s.sem.release(kSlots7);                                             // 33 行：还回去
        const bool stdAlias = s.sem.try_acquire();                          // 38 行 std 兼容名
        if (stdAlias)
            s.sem.release();
        qInfo().noquote() << QStringLiteral("  7a 一次拿光 %1 个许可：%2，remaining available = %3；再要 %4 个：%5；std 名 try_acquire：%6")
            .arg(kSlots7).arg(tookAll).arg(afterTake).arg(kSlots7 + 1)
            .arg(tooMany).arg(stdAlias);
        verdict(Hard, QStringLiteral("7a 「拿得完 / 拿不完」这两个结论"),
                tookAll && afterTake == 0 && !tooMany,
                QStringLiteral("all=%1 avail=%2 tooMany=%3").arg(tookAll).arg(afterTake).arg(tooMany));
        verdict(Hard, QStringLiteral("7a 归还后 available() 回到许可数"),
                s.sem.available() == kSlots7, QString::number(s.sem.available()));
    }

    // ——— 7b：六条线程抢三个坑 ———
    s.live.storeRelease(0);
    s.peak.storeRelease(0);
    s.jobsDone.storeRelease(0);
    s.acquireTimeouts.storeRelease(0);
    const bool done7 = runThreads(kWorkers7, [&](int) {
        for (int j = 0; j < kJobs7; ++j) {
            s.sem.acquire(1);                       // 22 行：**拿**必须自己写
            QSemaphoreReleaser rel(s.sem);          // 71 行：析构时才**还**（81~85 行）
            const int now = s.live.fetchAndAddOrdered(1) + 1;
            raiseMax(s.peak, now);
            QThread::msleep(kJobMs);                // 占着坑干一件慢活
            s.live.fetchAndAddOrdered(-1);          // 顺序要紧：先说「我不在里了」，再让 rel 还许可
            s.jobsDone.fetchAndAddOrdered(1);
        }
    }, kJoinMsSlow, QStringLiteral("【7b】"));
    qInfo().noquote() << QStringLiteral("  7b %1 条线程 × %2 件活 = 目标 %3，完成 %4，峰值并发 %5（上限 %6），收尸按时 %7")
        .arg(kWorkers7).arg(kJobs7).arg(kWorkers7 * kJobs7)
        .arg(s.jobsDone.loadAcquire()).arg(s.peak.loadAcquire()).arg(kSlots7).arg(done7);
    verdict(Hard, QStringLiteral("7b 活一件不少（阻塞 acquire 不丢活）"),
            s.jobsDone.loadAcquire() == kWorkers7 * kJobs7,
            QString::number(s.jobsDone.loadAcquire()));
    verdict(Hard, QStringLiteral("7b 峰值 <= kSlots7（这是语义，不是运气）"),
            s.peak.loadAcquire() <= kSlots7, QString::number(s.peak.loadAcquire()));
    verdict(Soft, QStringLiteral("7b 峰值正好 == kSlots7（坑真的被占满过）"),
            s.peak.loadAcquire() == kSlots7, QString::number(s.peak.loadAcquire()));
    verdict(Hard, QStringLiteral("7b 全部收工后许可回到 kSlots7（没泄漏归还）"),
            s.sem.available() == kSlots7, QString::number(s.sem.available()));
    qInfo().noquote() << QStringLiteral(
        "     这一节最想留下的是一句 API 契约：`QSemaphoreReleaser` **只还不拿**。\n"
        "     看 81~85 行那个析构体，里面只有一句 `m_sem->release(m_n)` —— 它是个「归还器」，\n"
        "     不是「获取器」。想要 RAII 一步到位（拿 + 还）就自己写个小类，或者用\n"
        "     `QMutexLocker` 那类真的会 lock 的。这一处踩过的人不少，因为名字太像了。\n"
        "     另外三条：67/70/72/75 行都挂 Q_NODISCARD_CTOR（不命名就是临时量，立刻 release）；\n"
        "     96~99 行 `cancel()` 把归还责任**撤走**（自己接管时才用）；\n"
        "     48 行 Q_DISABLE_COPY —— 信号量同样不可拷贝。\n"
        "     用途口诀：限流、令牌、以及「A 事件必须等 B 事件跑完」。");

    // ========================================================================
    // 【8】线程封闭 + 合并：一把锁都不用的那一版
    // ========================================================================
    section(QStringLiteral("【8】最后回到最高一级：能不共享就不共享"));

    for (int i = 0; i < kMergeThreads; ++i)
        s.mergeSlots[i] = 0;
    const int perThread = kMergeTo / kMergeThreads;         // 每段多长（这里整除，省事）
    QElapsedTimer t8;
    t8.start();
    const bool done8 = runThreads(kMergeThreads, [&](int slot) {
        qint64 local = 0;
        const int from = slot * perThread;
        const int to = from + perThread;
        for (int v = from + 1; v <= to; ++v)
            local += v;                                     // 全在**局部变量**里，没有任何共享
        s.mergeSlots[slot] = local;                              // ★ 只写自己那一格：下标互不相交
    }, kJoinMs, QStringLiteral("【8】"));
    const qint64 elapsed8 = t8.elapsed();
    qint64 merged = 0;
    for (int i = 0; i < kMergeThreads; ++i)
        merged += s.mergeSlots[i];
    const qint64 formula = qint64(kMergeTo) * (kMergeTo + 1) / 2;
    qInfo().noquote() << QStringLiteral("  合并 %1 格 = %2；公式 n(n+1)/2 = %3；耗时 %4ms（【3】加锁版 %5ms）；收尸按时 %6")
        .arg(kMergeThreads).arg(merged).arg(formula).arg(elapsed8).arg(ms3).arg(done8);
    verdict(Hard, QStringLiteral("8 合并结果 == 公式值，且与格数无关"),
            merged == formula, QStringLiteral("%1").arg(merged));
    qInfo().noquote() << QStringLiteral(
        "     这段代码的正确性**不需要你推理有没有竞争**：四条线程碰的是四个不同的下标。\n"
     ) << QStringLiteral(
        "     这就是「线程封闭」的全部含义 —— 不是技巧，是把「共享」这个前提删掉。\n"
        "     代价只有一个：末尾必须有一个合并步骤（join 之后再收），\n"
        "     而 `QThread::create` 的返回值是拿不到的（133~144 行把函数塞进\n"
        "     `std::async(std::launch::deferred, …)`，结果进了 future，线程对象不给你），\n"
        "     所以槽位要**提前分配好、并且不扩容**（本讲在 main 开头 resize，之后只按下标写）。\n"
        "     要是你在子线程里 push_back 一个共享 vector，那就又是一次竞争 ——\n"
        "     封闭的「封闭」指的是每线程各一份，不是各写各的还共用一个容器。\n"
        "     一句提醒，本讲不展开：相邻下标大概率在**同一条 cache line** 上，\n"
        "     于是有「伪共享」（false sharing）这件事。它影响的是时间，不是正确性；\n"
        "     真要抠，就按 cacheline 把槽位隔开（`alignas(64)`），但那属于性能那一讲。");

    // 收尾前把第 3 讲那条坑兑现：QAtomicInt 的拷贝是「快照」
    {
        QAtomicInt shared7(7);
        QAtomicInt snap = shared7;              // qatomic.h 24~28 行：编译得过，拷进去的是**快照**
        shared7.fetchAndAddOrdered(1);          // 105 行：只有 shared7 变了
        qInfo().noquote() << QStringLiteral("  附：QAtomicInt 原值 %1，另一份「拷贝」%2 —— 它不是别名，是快照")
            .arg(shared7.loadAcquire()).arg(snap.loadAcquire());
        verdict(Hard, QStringLiteral("附 拷贝之后两边各走各的"),
                shared7.loadAcquire() == 8 && snap.loadAcquire() == 7,
                QStringLiteral("%1 / %2").arg(shared7.loadAcquire()).arg(snap.loadAcquire()));
        qInfo().noquote() << QStringLiteral(
            "     第 3 讲第二节说过「167~169 行基类把拷贝 delete 了，24~34 行上层又加回来，\n"
            "     代价是那句 // Non-atomic API」。这一行就是它落到数字上的样子。\n"
            "     落到工程上是一句具体的禁止：\n"
            "       [c = s.atomicCount]() { ... }   ← 按值捕获原子量 = 捕获一张照片\n"
            "       void work(QAtomicInt n);        ← 按值传参同理（而且大多数写法编译不过）\n"
            "     要么按引用捕获，要么干脆传普通 int 进去、结果用别的方式收。");
    }

    // ========================================================================
    // 九、八节结果一览（最后一屏给你核对）
    // ========================================================================
    section(QStringLiteral("结论一览（对着上一屏每一行 [OK] 再核一遍）"));
    qInfo().noquote() << QStringLiteral("  1 演出：起始 %1 → 两次各自 +1 → %2（应当正好 %2，即丢了一次更新）")
        .arg(kAcctOpen).arg(kAcctOpen + 1);
    qInfo().noquote() << QStringLiteral("  1 真并发：原子 %1（目标 %2）／裸 int %3（任何值都合法）")
        .arg(gotAtomic).arg(kThreads * kIter).arg(gotPlain);
    qInfo().noquote() << QStringLiteral("  2 转账：缝里总额 %1 → 收尾回到 %2；旁观 %3 次，其中 %4 次看到不对")
        .arg(inHole2).arg(finalSum).arg(s.bystanderLooks.loadAcquire())
        .arg(s.bystanderBreaks.loadAcquire());
    qInfo().noquote() << QStringLiteral("  3 加锁计数：%1（目标 %2）；耗时 %3ms vs 无锁 %4ms")
        .arg(s.guarded).arg(kThreads * kIter).arg(ms3).arg(ms1);
    qInfo().noquote() << QStringLiteral("  4 反序拿锁：完成 %1（目标 %2），退让 %3 次；scoped_lock 完成 %4、反向探针 %5")
        .arg(naiveTotal).arg(2 * kRounds4).arg(s.naiveBackoffs.loadAcquire())
        .arg(scopedTotal).arg(s.scopedStumbles.loadAcquire());
    qInfo().noquote() << QStringLiteral("  5 读写锁：撕裂 %1 次（必须 0），写步 %2/%3，读者峰值 %4")
        .arg(s.rwTorn.loadAcquire()).arg(s.rwWriteSteps.loadAcquire())
        .arg(kWriteSteps).arg(s.rwReadPeak.loadAcquire());
    qInfo().noquote() << QStringLiteral("  6 条件变量：超时返回 %1；消费 %2/%3 条、和 %4/%5、空手而归 %6 次")
        .arg(woke6).arg(s.consumedCount.loadAcquire()).arg(kItems6)
        .arg(s.consumedSum.loadAcquire()).arg(wantSum6).arg(s.emptyWakes.loadAcquire());
    qInfo().noquote() << QStringLiteral("  7 信号量：完成 %1/%2，峰值 %3（上限 %4），剩余许可 %5")
        .arg(s.jobsDone.loadAcquire()).arg(kWorkers7 * kJobs7).arg(s.peak.loadAcquire())
        .arg(kSlots7).arg(s.sem.available());
    qInfo().noquote() << QStringLiteral("  8 线程封闭：合并 %1（公式 %2），耗时 %3ms")
        .arg(merged).arg(formula).arg(elapsed8);

    // ★ 退出码就是这本账。用 app.exec() 的讲次靠 QCoreApplication::exit(码)，
    //   本讲不进事件循环，所以直接 return —— 但**算法只有一个**：
    //   对不上任何一项（硬核对失败、某一节压根没收尸、或者一本账都没记）→ 1，全对上 → 0。
    //   最后那项听着多余，它管的是「改代码的人把核对整节删了」：total=0 配一个 0 退出码
    //   是最漂亮的一种作假 —— 什么都没测，却说测过了。
    //   写死 return 0 的话，这张表打印得再认真，CI 也永远看不见失败。
    const int hardBroken = gHard.failed + gHard.stalled;
    const int exitCode = (hardBroken > 0 || gHard.total == 0) ? 1 : 0;

    // ★ 最后一屏必须把「这本账闭合了没有」说出来，而且要带数字，
    //   否则上面那几十行 [OK] 只是一串没人核过的字符。
    section(QStringLiteral("硬核对的账"));
    qInfo().noquote() << QStringLiteral("  核对总数 %1，不符 %2，收尸超时 %3 节")
                              .arg(gHard.total).arg(gHard.failed).arg(gHard.stalled);
    qInfo().noquote() << QStringLiteral("  → 退出码 %1（规则：不符 + 超时 > 0，或者核对总数是 0，就返回 1）")
                             .arg(exitCode);

    qInfo().noquote() << QStringLiteral(
        "\n优先级再背一遍：**线程封闭 > 消息传递 > 原子 > 锁。**\n"
        "  · 线程封闭（【8】）：不共享，所以不需要证明任何东西。\n"
        "  · 消息传递（第 3 讲的 QueuedConnection 就是这个）：数据所有权交出去，\n"
        "    两边各管各的；代价是延迟和一个「按值拷贝」的账单。\n"
        "  · 原子（【1】【2】）：只适合**单个值**的加减与指针交换，别指望它保护复合不变量。\n"
        "  · 锁（【3】~【5】）：万能，但它把「请推理」这件事交回给你 —— 每个临界区、\n"
        "    每把锁的顺序、每个提前 return 都要想。\n"
        "本讲八节里，能判定的结论全部可判定；概率性的一律标了 [预期中] 或 [实测]。\n"
        "下一讲（本模块第 5 讲）用 QtConcurrent 把【8】这套「分块—合并」写成一行。");

    return exitCode;
}

// ============================================================================
// 图解：把【2】那道缝画出来（这是本讲唯一一张必须看懂的图）
// ----------------------------------------------------------------------------
// 左边是「每步都原子」的代码，右边是它在两条线程 + 一条旁观线程下的真实时序。
// 竖线之间没有时间比例，但**顺序是真的**。
//
//   acctA = 100000   acctB = 100000   不变量：acctA + acctB == 200000
//
//   线程 T1（转账 1 元）           线程 T2（转账 1 元）        旁观线程 O
//   ─────────────────────┬────────────────────────┬─────────────────
//   acctA.fetchAndAdd(-1) │                        │
//        acctA=99999      │  ← 缝：此刻全局和=199999 │
//                         │  acctA.fetchAndAdd(-1) │
//                         │       acctA=99998      │  ← 缝又张开一次
//                         │                        │  sum = 99998+100000
//                         │                        │      = 199998  ✘ 看到「钱在路上」
//   acctB.fetchAndAdd(+1) │                        │
//        acctB=100001     │  acctB.fetchAndAdd(+1) │
//        和回到 200000 ✔  │       和回到 200000 ✔  │
//   ─────────────────────┴────────────────────────┴─────────────────
//
//   每一步单独看都是**原子的**（fetchAndAddOrdered 是硬保证），
//   可全局不变量在两行代码之间就是塌的 —— 因为那两行之间「没有任何东西」。
//   加锁之后：
//
//   T1 ──[lock]── acctA-1 ── acctB+1 ──[unlock]──
//                └────── 这整段，别人进不来 ──────┘
//   旁观线程 O 在 lock 与 unlock 之间撞上门，只能等；等到的那个状态一定是**完整**的。
//   这就是【3】【5】要买的唯一一样东西：**一个区间**，不是一个动作。
//
//   顺带把「为什么看不见 ≠ 不存在」也画一下：旁观线程 O 只在缝张开时才看得到。
//   如果它看的位置不对（或者它自己太快），一次都不出错 —— 于是你的测试全绿，
//   而线上那台机器有八个核。
// ============================================================================

// ——— 老师傅的话 ———
/*
 * 第一条，也是最贵的一条：**不要用「跑出来对不对」来判断并发代码。**
 * 本讲【1b】那行日志我特意留着：裸 int 完全可能正好等于 100000，然后它在后面
 * 那一栏打上「← 看着对！这正是本节的靶心」。你写并发代码的时候，测试环境是
 * 竞态**最不容易出现**的那个角落：机器空闲、核少、负载轻、编译器保守。
 * 上线那台机器八个核、旁边还跑着三个服务，插队就出现了。
 * 所以判据只有一条：有没有 data race。工具是 ThreadSanitizer（-fsanitize=thread），
 * 它报的是**可能性**；你的日志报的只是**这一次**。要修的是可能性。
 * 一件让人安心的事实：Qt 的锁里本来就埋了 TSan 注解
 * （qreadwritelock.h 88~89/97~98/134 行那些 QtTsan:: 调用，头文件第 10 行直接
 * include 了 qtsan_impl.h），所以带 TSan 跑 Qt 代码不会满屏假阳性 —— 前提是你
 * 用它的锁。自己拿 bool 当标志位手写自旋，TSan 一样会告你。
 *
 * 第二条：锁买的是**区间**，不是**一步**。
 * 看到两个原子操作并排写在一起，条件反射该是：「中间能不能插人？」
 * 能插人就得圈起来。反过来说，如果你在临界区里只放了一行 `x = 42;`，
 * 那把锁基本是白花的（x86 上一次对齐的 int/int64 写不会撕裂），
 * 你真正要保护的是「x 和 y 加起来等于 100」这种**跨行的关系**（【5b】那个靶子）。
 * 一句话：**保护不变量，不是保护变量。**
 *
 * 第三条：RAII 写对了语法，还要写对**作用域**。
 * 第三节那四种形状里，编译器会拦的那两种（`QMutexLocker(&m);` 的模板括号错、
 * `QReadLocker(&rw);` 的 most vexing parse）其实最不值钱 —— 它们当场就教你做人。
 * 会杀人的是另外两种：
 *   · 花括号版匿名临时量：编译过、跑起来「加了锁」，但语句结束就解了（251 行
 *     Q_NODISCARD_CTOR 给的就是这条铃铛，别去抑制它）；
 *   · 名字对了、作用域小了：locker 在 if 的花括号里，你以为下面十行都在保护里，
 *     其实出了那个括号锁就放了。这一种**连 warning 都没有**。
 * 所以我的规矩是写 locker 之前先回答一句话：「这个临界区到哪一行结束？」
 * 答不上来就别加锁，先把那段代码理清楚。
 *
 * 第四条：死锁的解法有顺序，别一上来就换锁。
 *   ① 让「需要两把锁」这件事消失（临界区合成一个，或者把要改的两样放进同一个结构、
 *      同一把锁）—— 这是唯一真正的解法；
 *   ② 拿不出 ①，就把**全局顺序**定死并写在注释里（所有文件都按 A→B 拿）；
 *   ③ 需要降级语义（拿不到就走、回用户一句「忙」）才用 tryLock(期限) 退让，
 *      注意【4a】那个细节：退让之后要**重试**，不然活就丢了；
 *   ④ 只是要「几把一起攥住」，用 std::scoped_lock（它内部做死锁避免，
 *      不需要你约定顺序，QMutex 天生满足 Lockable，见 qmutex.h 41/51/69/83 行
 *      那三组概念注释）；
 *   ⑤ QRecursiveMutex 不是解法。它只是把死锁改成「自己套自己」，
 *      等 unlock 次数凑够的时候，连你自己都数不清套了几层。
 * 顺带把「超时」这条说完：`wait(QDeadlineTimer)` 有期限、tryLock 有期限，
 * 但**期限不是正确性工具，是可观测性工具** —— 它让挂死变成一条能打印的日志。
 *
 * 第五条：读写锁是「读多写少」的工具，不是「更快」的工具。
 * 动手前问三句：有几个写者？每次写多久？读者看到的不变量跨几行？
 * 写得不算少时它常常比 QMutex 还慢（要记读者数、要记等待者）；没竞争时它又几乎
 * 不花钱（27~29 行那条 CAS 快路径）。所以「换读写锁就快了」这种结论必须实测，
 * 而实测要带数据规模和线程数 —— 只在一个核上跑不出名堂。
 * 还有一条物理规律：**读锁不能升级成写锁**。NonRecursive（151 行）模式下，
 * 写锁要等所有读者出去，其中就包括你自己。想「先看看再改」，就照【5c】那样：
 * 放掉读锁 → 拿写锁 → **重读重校验**。中间那一瞬别人可能改过，所以那句重新校验
 * 不是仪式感，它就是这个模式的全部意义。换个 Recursive 参数绕不过去。
 *
 * 第六条：`wait()` 返回不等于条件成立，所以谓词必须是 while。
 * 三种「回来了但事情没成」：超时、虚假唤醒、别人 wakeAll 了但货被第三条线程抢走。
 * 我讲课时不爱强调虚假唤醒（罕见，而且说了像吓唬人），我强调这条：
 * **`if` 版一旦 wait 返回就去 dequeue，而 QQueue::dequeue() 在空队列上是断言**，
 * Debug 构建当场停给你看。罕见不罕见不重要 —— 谓词循环是免费的，
 * 而漏掉它的代价是「偶发的空手操作」这种你查三个月的 bug。
 * 另一半纪律在 6b 那段注释里：判断和睡必须是「握着一把别人也拿得到的锁」做的，
 * 否则「我刚看完是空的，别人塞了并叫醒，可我还没开始睡」这个窗口就存在。
 *
 * 第七条：信号量里的数字是**配额**，不是提示。
 * 三个许可就最多三个在里面，【7b】那个峰值是我用 CAS 打出来的，不是声明出来的 ——
 * 这类「拿实测证明上限」的写法请带走，它比任何「我保证不超过 3」都硬。
 * API 上有一条名字骗人的坑：`QSemaphoreReleaser` **只还不拿**（81~85 行那个析构体
 * 里只有一句 release）。它对应的是「我已经 acquire 了，帮我记住还」，
 * 不是「帮我 acquire」。名字里带 Releaser 就该老实，但它确实容易读错。
 * 加上 67/70/72/75 行四个 Q_NODISCARD_CTOR（不命名就是临时量，立刻还回去 = 白干）、
 * 96~99 行 cancel()（把归还责任撤走，自己接管时用）、48 行 Q_DISABLE_COPY ——
 * 这一族工具的性格是一致的：**不可拷贝、必须命名、只管自己那一段。**
 *
 * 第八条，也是收口：最高一级的同步是**别共享**。
 * 【8】那半屏代码我一个字都没锁、一个字都没原子，它正确的原因是四条线程碰的是
 * 四个不同下标。这个正确性不需要推理、不需要测试环境配合、也不需要在代码里
 * 写注释提醒后人「小心竞争」—— 因为「共享」这个前提被删掉了。
 * 本讲的顺序是倒着教的（先锁后封闭），因为你在别人的代码里先撞见的是锁；
 * 但你写新代码时请正过来：**线程封闭 > 消息传递（第 3 讲的 Queued 就是这个）>
 * 原子 > 锁。**
 *
 * 最后一条送一个坑，是我这讲写的时候当场踩的，正好请你看清 Qt 的「关键字」是什么：
 * 我把【8】那个成员一开始写成 `std::vector<qint64> slots;`，编译器报
 * "declaration does not declare anything" —— 因为 qtmetamacros.h 44 行有
 * `# define slots Q_SLOTS`。`slots` 是**宏**，被展开之后连变量名都没了。
 * 同理 `signals`（45 行）、`emit`（53 行）。所以「Qt 的关键字不是保留字」这句话
 * 的实际含义是：它们会**吃掉任何同名的标识符**，包括成员名、局部变量名、参数名。
 * 三条出路我都写在第二节那段注释里（改名 / QT_NO_KEYWORDS /
 * 只关掉 slots+signals 的 QT_NO_SIGNALS_SLOTS_KEYWORDS）。
 * 我挑改名，因为后两条要动整个工程的编译定义，而你的同事不一定知道你动了。
 *
 * 这一讲的纪律，一句收尾：**并发代码的证据不是「跑对了」，是「没有那条路」。**
 * 每写一个共享点，就问一句「别人插进来的那条线在哪」；画得出来就修，
 * 画不出来就去画（本讲开头那张图，比这段注释值钱）。
 */

// ——— 小练习 ———
/*
 *   1. 【1b】里把 `++s.plain;` 依次换成这三种：
 *        (a) `s.plain = s.plain + 1;`
 *        (b) `s.plain = s.atomicCount.loadAcquire();`   ← 读的是别的变量，故意做得怪一点
 *        (c) `s.plain = s.plain + kThreads;`
 *      哪一种和原来那个 `++` 是**完全同一个错误**？哪一种看起来「更原子」了其实没有？
 *      要求用【2】那张图的三件事（读 / 改 / 写回）来回答，不许回答「有没有用原子类型」。
 *   2. 把【3】的循环体改成「`QMutexLocker locker(&s.guard);` 放在 if 的花括号里，
 *      下面 `++s.guarded;` 放在花括号外」，重跑，先预判 guarded 的值会落在哪个区间，
 *      再跑。然后回答：这个 bug 编译器为什么不拦你，以及**你**怎么在评审时一眼看出来。
 *   3. 【4a】把第二把锁的 `tryLock(QDeadlineTimer())` 换成 `lock()`：
 *      预期是挂死。**不要在本仓库的文件里改**，另存一份副本试，
 *      然后观察：日志停在哪一行、CPU 占用是多少、任务管理器里进程状态如何。
 *      这三条就是你以后判断「挂死 vs 崩溃」的依据。做完把副本删掉。
 *   4. 【4a】把循环条件从 `while (done < kRounds4)` 改回 `for (r < kRounds4)`：
 *      核对行会先报哪个不符？用这个改动体会一句话：「退让之后不重试，就是丢活」。
 *   5. 【5b】把 kWriteSteps 从 2000 提到 200000（和 kReadSteps 同量级），
 *      记录两件事：rwReadPeak 变成几、整节耗时变成几。
 *      然后回答：这时候读写锁还比 QMutex 快吗？（把【5b】临时换成一把 QMutex 实测一遍，
 *      用 QElapsedTimer 夹住整节，别凭感觉。）
 *   6. 【6b】把消费者那个 `while (s.queue.isEmpty() && !s.done)` 改成 `if`：
 *      先写出你预判的现象。提示：qqueue.h 21 行 `dequeue()` 的本体就是
 *      `QList<T>::takeFirst()`，而 qlist.h 637~639 行第一句是 `Q_ASSERT(!isEmpty());`
 *      —— Debug 构建里它当场停；Release 构建里 Q_ASSERT 是空操作，于是 641 行那句
 *      `std::move(first())` 就去动一个空表。把这一层「Debug 断言 / Release 未定义」
 *      的分裂写进你的答案里，跟【3】那两条 Q_ASSERT 是同一件事。
 *      然后回答：为什么 `if` 版在单生产单消费时常常「看起来没事」，
 *      而加上第三条消费者之后就会出事 —— 这跟【1】「看不见 ≠ 不存在」是同一件事吗？
 *   7. 【7b】把 `QSemaphoreReleaser rel(s.sem);` 挪到 `s.sem.acquire(1);` **前面**
 *      （顺序换成「先构造归还器，再 acquire」）。三问：
 *        · 循环跑完之后 `s.sem.available()` 是几？（81~85 行的析构体只有一句 release）
 *        · 【7b】的峰值那一栏会变成什么，为什么「峰值 <= 3」这条 [OK] 会失效？
 *        · 这个错误为什么 TSan 抓不到？（提示：没有 data race，只有**配额的账**错了）
 *   8. 综合：把【8】的 `s.mergeSlots[slot] = local;` 改成
 *      在子线程里对**同一个** std::vector 做 `push_back(local)`，最后合并时读整个 vector。
 *      这个改法「看起来结果还是对的」—— 请用【2】那张图和 vector 的内部结构
 *      （size / capacity / 堆上那块缓冲区）解释：错在哪一层，为什么结果常常还是对的，
 *      以及你要用什么工具（不是日志）把它抓出来。
 */
// ============================================================================

