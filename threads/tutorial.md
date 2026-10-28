# 第柒阶段 · 并发与线程（Qt 从入门到资深）

> 老师傅带你把「哪段代码在哪条线程上跑」这件事，钉成能打印、能核对的数

## 一、模块简介

徒弟，走到这一关，你的程序已经「能动」了：窗口会弹、按钮会响应、图画得出来、表格能编辑。但这些全都跑在**同一条线程**里——就是 `app.exec()` 那条。它一次只能干一件活：要么分拣事件，要么执行你写的那段代码。你让它在槽里读一个 200 MB 的文件，它就没空画界面，操作系统给你的标题栏挂上「未响应」。这就是这一阶段要解决的问题：**把活分出去，同时不把程序弄坏。**

Qt 多线程有一句必须刻在脑子里的法律条文：

> **只有 `run()` 跑在新线程里，别的都在原线程。**

`QThread` 对象本身住在创建它的那条线程，它的槽、它的信号、它在哪儿被 `delete`，全都在那条线程上发生。`QThread` 不是线程，它是**一条新线程的遥控器**。第 1 讲用三个线程身份把这句话钉死；第 2 讲把「每个 `QObject` 归哪条线程」这条第一原理讲透；第 3 讲让这两条线交汇，变成跨线程信号槽的完整规则；第 4 讲处理「万一真要共享内存」；第 5、6 讲把活交给 Qt 替你管（`QtConcurrent`、线程池）；第 7 讲学怎么正确地停下来；第 8 讲把战场从「一个进程里」换到「两个进程之间」。

本模块有一个贯穿全部 8 讲的取证手法：**把结论变成数**。步数、校验和、线程 id、计数器的差、队列里压着几个事件——凡是「时序」类的说法，都不靠嘴说，靠打印。

> 环境事实：Qt 6.12.0、Windows、MinGW-w64（MSYS2 ucrt64，gcc 15.2/16.2）、CMake ≥ 3.21 + Ninja、C++17。构建走 `find_package(Qt6 COMPONENTS Widgets Concurrent Network)`，目标名统一是 `threads_0N_xxx`，已在顶层 `CMakeLists.txt` 的「第柒阶段 · 并发与线程（threads/）」一节登记。
>
> 诚实标注：**本机当前无法编译链接 Qt 程序**，8 个示例**都未在本机实测运行**。每个类、函数、枚举、重载都对着本机 `C:\msys64\ucrt64\include\qt6` 里的真实头文件核对过（注释里给了文件名与行号），核对不上的 API 一律不用。凡是依赖调度时序的结论，文中一律标成**概率**或**关系**，不冒充实测数字。

## 二、学习目标

学完本模块，你要能做到：

1. 说清「继承 `QThread` 重写 `run()`」与「worker-object + `moveToThread`」两条路的差别，并解释为什么 `run()` 里**没有事件循环**，定时器在 `run()` 里不 `exec()` 就一次都不响
2. 用 `QThread::currentThread()` / `QObject::thread()` / `QThread::loopLevel()` 三个工具，**当场**判断某段代码、某个对象在某条线程上，而不是靠猜
3. 讲清线程亲和性：谁创建归谁、有 parent 挪不动、跨线程 `delete` 为什么要换成 `deleteLater()`
4. 背出 `Qt::AutoConnection` 的真实规则（发射那一刻比较两条线程），能解释 `Direct` / `Queued` / `BlockingQueued` 各自把槽放在哪条栈上、以及 `UniqueConnection` / `SingleShotConnection` 是**可或的标志位**
5. 说得清队列连接为什么会「拷贝参数」，以及自定义类型要能跨线程排队，究竟卡在编译期还是运行期（`QMetaType::fromType` 与 `isRegistered()` 的分工）
6. 按「线程封闭 > 消息传递 > 原子 > 锁」的顺序选同步手段；能写出一个没有丢更新的计数器、一个没有虚假唤醒 bug 的等待、一个配平的生产者消费者
7. 用 `QtConcurrent::run` + `QFutureWatcher` 跑后台任务，知道 `result()` 会阻塞而 `isFinished()` 不会，知道 `mapped` / `filtered` 是命名空间里的自由函数
8. 用 `QRunnable` + `QThreadPool` 管一批任务，说得清 `autoDelete` 决定谁删、`tryStart` 为什么不排队
9. 用协作式取消 + `quit()` + 带期限的 `wait()` 收尾，**永远不调** `QThread::terminate()`
10. 在 `QLocalSocket` 上自己造消息边界（长度前缀），并防御对端给的坏帧头

## 三、文件列表与学习重点

咱一共 8 个示例，按顺序来，别跳：

```text
  01_thread_basics.cpp          ← 两条路 + 第一道坎（run() 里没有事件循环）
    重点：同一个 QTimer，run() 里不 exec() 零 tick、exec() 后开始走（两个数都打出来）；
          run() 里 currentThread() == this（QThread 对象自己），主线程看 this->thread() 又是主线程；
          worker-object 四个零件：QThread 只当宿主 / 业务是普通 QObject / moveToThread /
          跨线程只靠信号下命令 / finished 之后收尸；
          requestInterruption() 与 isInterruptionRequested()（qthread.h 第 68、69 行）是协作式置标志；
          terminate()（第 90 行）在本讲一次都不调——它杀整条线程，锁和半截数据全留在原地；
          QThread::create()（第 86 行）是静态模板、返回堆上的 QThread*、标了 [[nodiscard]]；
          idealThreadCount() 用来解释「别开几百条线程」
    诚实标注：run() 里 exec() 之后定时器能 tick，是「有循环才能投递」的直接后果；
              具体 tick 次数以你的机器为准，本讲只承诺「不 exec 时为 0」

  02_thread_affinity.cpp        ← 线程亲和性（第二道坎）
    重点：qobject.h 第 143 行 thread() 读归属、第 147 行 moveToThread 返回 **bool**
          （第 145 行那个 void 版本被 QT_CORE_REMOVED_SINCE(6, 7) 包着，别再照旧教程写）；
          有 parent 就挪不动；loopLevel()（qthread.h 第 78 行）当场验证有没有循环；
          QTimer 必须由它所属线程 start()：正例在 worker 槽里 new，反例在主线程 start；
          跨线程 delete 的代价（析构跑错线程、摘错 parent）与 deleteLater()（qobject.h 第 364 行）
    诚实标注：moveToThread 返回 false 的**具体原因**要看你有没有 parent、对象是否正在别的线程干活；
              本讲把「挪没挪成功」打印出来，不猜 Qt 内部

  03_cross_thread_signals.cpp   ← 跨线程信号（第三道坎）
    重点：Auto 是**一条规则**不是「更聪明的选择」：发射那一刻
          currentThread() == 接收者->thread() ? Direct : Queued（qnamespace.h 第 1313~1320 行五个枚举值）；
          Direct：槽在子线程栈上执行（把两边线程句柄同时打印）；
          Queued：发射方立刻返回，槽等接收者循环（用两个原子计数证明「返回时槽还没跑」）；
          参数按值拷进事件：编译期 QMetaType::fromType<T>() 对任何合适类型都能过，
          运行期 isRegistered() 才决定能不能排队搬运；
          UniqueConnection = 0x80、SingleShotConnection = 0x100 是**可以或进连接类型的标志位**
    诚实标注：队列里有几个事件、什么时候被取走，跟调度有关；本讲承诺的是相对关系
              （「发射返回时槽计数器没变」「等循环转一圈后变了」）

  04_synchronization.cpp        ← 锁、原子、等待条件、信号量
    重点：优先顺序 线程封闭 > 消息传递 > 原子 > 锁，本讲倒着讲（你在别人代码里先撞见锁）；
          裸 ++ 的丢更新：先单线程手工交错演一遍（必然错），再真并发跑（概率）；
          两个原子操作 ≠ 一个复合原子操作（转出/转入之间的缝）；
          QMutex + QMutexLocker 的四种写错形状（两种编不过、两种静默）；
          unlock 一个没持有的锁在 Debug 下是断言（QMutexLocker qmutex.h 第 282 行、
          QMutex 本体第 52 行），Release 下是未定义行为；
          QWaitCondition 必须配锁、醒来必须重查条件；QSemaphore 配平生产者消费者
    诚实标注：并发下的「没出错」不等于「写对了」——本讲的关键结论都在单线程手工交错里
              也能复现，只有那样才是确定性证据

  05_qtconcurrent.cpp           ← 不想自己管线程
    重点：run 的四条签名（qtconcurrentrun.h 第 42/61/70/80 行）；
          第 39~40 行 Q_NODISCARD_X 明说：不要结果就别用 run，去用 QThreadPool::start；
          参数被 std::decay + std::forward 拷贝进 tuple；
          QFutureWatcher 逐个连 started / progressRangeChanged / progressValueChanged /
          resultReadyAt / finished（qfuturewatcher.h 第 50~65 行），并打印每个回调所在线程
          （watcher 内部靠 event() 把结果投递到它自己的线程）；
          result() 第一句就是 d.waitForResult(0)（qfuture.h 第 311~322 行），会阻塞；
          mapped / mappedReduced / filtered / filter 是 QtConcurrent 命名空间的自由函数
          （QFuture 没有这些成员），filter 返回 QFuture<void> 并**原地改你的容器**
    诚实标注：哪些任务先完成由线程池调度决定；本讲只承诺「watcher 的信号在 watcher 所在线程发出」

  06_qrunnable_threadpool.cpp   ← 线程池
    重点：QRunnable 不是 QObject（qrunnable.h 第 24 行只有一个纯虚 run()），
          没有元对象、不能 emit → 结果必须靠另外的 QObject 送回去（三个 static_assert 钉死）；
          autoDelete 默认 true（第 20 行）→ 谁删要写清，用析构计数器核对；
          start(QRunnable*, int priority = 0)（qthreadpool.h 第 37 行）的 priority 是裸 int，
          与 QThread::Priority 枚举不是一回事；
          tryStart（第 38 行）不排队、没空闲线程当场 false；
          maxThreadCount 压到 1 让队列排起来，核对出场顺序；waitForDone 带期限
    诚实标注：出场顺序依赖池的实现与调度，本讲在 maxThreadCount == 1 这个受控条件下核对入队序

  07_cancel_and_shutdown.cpp    ← 取消与收尾
    重点：没有任何 API 能撤下正在跑的任务；terminate 能杀但代价是整条线程；
          唯一正确形状：原子标志 + 循环体里一句 if；
          requestInterruption 管线程不管任务（线程池的工人线程拿不到）；
          QFuture::cancel()（qfuture.h 第 65 行）对「排队中 / 正在跑 / 已跑完」三件不同的事；
          QtConcurrent::run(空池, f) 会在调用者线程上同步走完 cancel + finish + delete
          （qtconcurrentrunbase.h 第 62~68 行）；
          收尾一律 quit()（qthread.h 第 92 行）+ wait(期限)（第 95~96 行）
    诚实标注：「置位时还剩多少块没做」这类前提不成立时，实验记 stalled，不硬拗成绿

  08_local_ipc.cpp              ← 本地进程间通信（换战场）
    重点：QLocalServer::listen(名字) + fullServerName()；同名第二个服务端被拒 = 单实例地基；
          字节流没有消息边界：五条消息拆三段发，靠长度前缀找回来（打印「第一块只有 7 字节」）；
          帧头是对端说的，不可信：声称 1 GiB 的帧必须拒、连接必须断，
          但已解析的好帧不回滚；
          多客户端：每个连接一份解析器，管住谁生谁删
    诚实标注：Windows 上走命名管道、Linux/macOS 上走本地 socket；本讲只用回环/本机名字，
              不碰网络
```

## 四、三张地图（贴在显示器上那种）

**地图一：这段代码在哪条线程？**

| 你写的东西 | 跑在哪 |
|---|---|
| `main()` | 主线程 |
| `QThread` 子类的 `run()` | 新线程 |
| `QThread` 子类自己的槽 | 创建它的线程（遥控器所在线程） |
| `moveToThread(t)` 过去的 worker 的槽 | `t` 那条线程 |
| 主线程对象的 `QTimer` 的 timeout | 主线程 |
| Queued 连接里接收者的槽 | 接收者所属线程 |
| `QtConcurrent::run` 里的函数 | 线程池的某条工人线程 |

**地图二：一个连接用哪种方式跑？**

```text
Auto（默认）  ← 发射那一刻：currentThread() == 接收者->thread() ? Direct : Queued
Direct        ← 就在发射者的栈上调用（调用者负线程安全的责任）
Queued        ← 参数拷贝成事件，投进接收者线程的队列（接收者必须有循环）
BlockingQueued← 同上，但发射方阻塞等槽跑完（同线程用 = 立刻死锁）
```

`UniqueConnection`（`0x80`）、`SingleShotConnection`（`0x100`）是**或进去的标志**，不是第五、第六种连接方式。

**地图三：怎么让它停？**

| 局面 | 手段 | 谁负责 |
|---|---|---|
| 自己写的 `QThread` 子类 | `requestInterruption()` + 轮询 `isInterruptionRequested()` | 循环体自己 |
| worker-object | 同上（标志住在 worker 里），或置一个 `std::atomic` | 槽里自己 |
| 线程池任务 | 自己传给任务的原子标志 | 任务自己 |
| `QFuture` | `future.cancel()` | Qt 只对「还没开始」的生效 |
| 整条线程 | `quit()` + `wait(期限)` | 线程的事件循环 |
| 谁都不该用 | `terminate()` | 没有人 |

## 五、常见误区 → 正解

| 误区 | 正解 |
|---|---|
| 「我在子线程里 `new QTimer`，它就该在子线程响」 | 在**哪条线程 `start()`**、定时器**归哪条线程**，两件事都要对；而且那条线程得有事件循环 |
| 「`connect` 用 Auto 就一定线程安全」 | Auto 只是判定规则。Direct 时槽就在发射者的栈上跑，保护是你的事 |
| 「`moveToThread` 挪的是 `run()`」 | 挪的是**对象归属**；`run()` 永远在新线程，挪不动它 |
| 「有 parent 的对象也能挪」 | 不能。归属要跟父亲一致，Qt 不让你挪 |
| 「队列连接传指针也一样」 | 参数被拷贝，拷贝的是指针本身；指过去的内存归谁、活多久，Qt 不管 |
| 「自定义类型加了 `Q_DECLARE_METATYPE` 就一定能跨线程」 | 编译期能过 ≠ 运行期登记过。要看 `QMetaType::isRegistered()` |
| 「没复现就是没有竞态」 | 竞态是概率的。要在单线程手工交错里把它演出来，才算证据 |
| 「`QMutexLocker` 写哪儿都行」 | 它有四种典型写错形状，两种静默出错。作用域就是锁的作用域 |
| 「`terminate()` 最省事」 | 它留下攥着的锁和写了一半的结构。停下来了，但不能继续用 |
| 「TCP 里一次 `readyRead` 就是一条消息」 | 字节流只有字节。边界要自己造（长度前缀 / 分隔符） |

## 六、练习

1. **线程身份四连**：写一个小程序，打印 `main`、`run()`、worker 槽、`QThread` 对象析构时各自的 `currentThreadId()`。先在心里预测，再跑，对不上就回去读第 1、2 讲。
2. **把 Direct 改成 Queued**：拿第 3 讲的例子，把连接类型从 Auto 改成 Direct，观察「发射返回时槽已经跑完了」这个计数差消失。再改回 Queued。
3. **复现丢更新**：第 4 讲的手工交错版本，把 `++` 换成原子 `fetch_add`，看差错是不是消失；再故意用两个原子操作做一个「转出+转入」，把缝找回来。
4. **给取消加期限**：给第 7 讲的循环加一个「最多等 3000 ms」的收尾路径，然后**故意**把检查点去掉一次，看程序是不是卡在 `wait()` 里。这是你在别人机器上最可能遇到的现场。
5. **造一个坏帧**：第 8 讲里把长度前缀改成 0xFFFFFFFF，确认对端断开连接、且前面已解析的消息没丢。
6. **写一个 `QThreadPool` 的单线程顺序**：`setMaxThreadCount(1)`，压 5 个任务，核对出场顺序与 `autoDelete` 的析构计数。

## 七、验收标准

能独立做到下面这些，这一阶段就算过了：

- [ ] 能在三分钟内讲清「只有 `run()` 跑在新线程」这句话，并说出三个反例
- [ ] 给你的项目加一条后台任务时，能先画出「哪条线程创建、哪条线程执行、结果怎么回来」
- [ ] 面对一个「偶发崩溃」，第一反应是查线程归属，而不是加 `try/catch`
- [ ] 写锁的时候，能说出这把锁保护的是**哪一块数据**、有没有第二条路径绕过它
- [ ] 收尾代码里找不到 `terminate()`，所有 `wait` 都带期限
- [ ] 能解释你写的每一个 `connect` 用的是哪种连接方式、为什么

## 八、下一站

线程这一关过了，你会发现「数据住在哪儿」和「代码在哪条线程跑」其实是同一个问题的两面。接下来第捌阶段「数据与持久化」，我们从「一份数据怎么活过进程重启」讲起：配置、文件、JSON、资源、SQLite——每一讲都在回答「这份数据归谁、以什么形状落盘、下次怎么原样拿回来」。

想看动画复盘，随时回来翻 [图解 · 线程归属](../visuals/thread_affinity.html)、[图解 · 连接类型](../visuals/connection_types.html) 与 [图解 · 对象树](../visuals/qobject_tree.html)。加油，徒弟！
