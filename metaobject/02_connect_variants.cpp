// ============================================================================
// 第 2 讲 · connect 的几种写法与它们的代价
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   QObject::connect 有十来个重载，写法不同，**出错时机**不同。本讲把常用的几种
//   各演一遍，并把「代价」量化成可核对的机器证据：
//     1) 函数指针式（首选）：编译期就拦下签名不匹配、类没写 Q_OBJECT
//     2) 函数指针式 + Qt::ConnectionType：多一个「怎么派发」的开关
//     3) 普通成员函数当槽 / 槽参数比信号少 / 槽有返回值：都合法，讲讲为什么
//     4) 带 context object 的 lambda 式（第 4 讲重点，这里先建立连接形态）
//     5) 老的 SIGNAL()/SLOT() 字符串式：为什么它把错误从编译期推到运行期
//   外加两件实操硬活：
//     · 重载信号怎么消歧义：qOverload<T>() / 显式成员指针类型 / qConstOverload
//     · Qt::UniqueConnection 到底能不能配 lambda（能拒，但拒的方式分两种：
//       Release 是 qWarning + 静默不连，Debug 是断言 abort —— 本讲现场演的是前者）
//
// 对应构建目标名：metaobject_02_connect_variants
//   add_executable(metaobject_02_connect_variants 02_connect_variants.cpp)
//   target_link_libraries(metaobject_02_connect_variants PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)
//
// 运行前须知：
//   · 只建 QApplication 不显示窗口；结论全部 qInfo() 打出来；QTimer 驱动 + 自动 quit()。
//   · 文件里凡标了「【反面教材】」的片段都留在注释里，别取消注释——那是本讲要教的报错，
//     取消了就构建失败或运行期断言。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 metaobject_02_connect_variants，跑到自动退出、退出码 0）。
//     但本讲**没有汇总自检行**，退出码 0 只证明「没崩、走完了」，不证明正文每个数字都对：
//     带具体数字的「预期」请以你自己那次运行输出为准（本机那一轮存档 runlogs/metaobject_02_connect_variants.log）。
// ============================================================================

#include <QApplication>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QMetaMethod>
#include <QDebug>

// ============================================================================
// 一、发送者：故意造一组「重载信号」，逼你面对消歧义
// ============================================================================
class Sensor : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;                 // 继承构造，少写 boilerplate

    void publish(int v)            { Q_EMIT valueChanged(v); }
    void publish(const QString &s) { Q_EMIT valueChanged(s); }
    void publishPair(int a, int b) { Q_EMIT valueChanged(a, b); }
    void fireTick()                { Q_EMIT tick(); }

Q_SIGNALS:
    void valueChanged(int value);                  // 三个同名不同参的信号
    void valueChanged(const QString &text);
    void valueChanged(int first, int second);
    void tick();                                   // 唯一不重载的那个，用来做「干净示范」
};

// ============================================================================
// 二、接收者：四种「可以被连」的函数形态，全都不是特殊物种
// ============================================================================
class Collector : public QObject
{
    Q_OBJECT
public Q_SLOTS:
    // 形态 1：标准槽，签名与信号严格一致
    void onInt(int v)             { ++m_intHits; m_lastInt = v; }
    void onText(const QString &t) { ++m_textHits; m_lastText = t; }
    void onPair(int a, int b)     { ++m_pairHits; m_pairSum = a + b; }

    // 形态 2：**参数比信号少**的槽。合法，Qt 只把前 N 个递给你。
    // 依据（本机 qobject.h 第 257~258 行，注意是 >= 不是 ==）：
    //   static_assert(int(SignalType::ArgumentCount) >= int(SlotType::ArgumentCount),
    //                 "The slot requires more arguments than the signal provides.");
    void onFirstOnly(int a)       { ++m_trimHits; m_first = a; }

    // 形态 3：**有返回值**的槽连 void 信号。也合法。
    // 依据：qobjectdefs.h 的 ReturnType 相容性检查走
    //   QtPrivate::AreArgumentsCompatible<SlotReturnType, SignalReturnType>
    //（qobject.h 第 240~242 行），而 qobjectdefs_impl.h 第 285 行专门特化了
    //   template<typename A> struct AreArgumentsCompatible<A, void> { enum { value = true }; };
    // 所以「槽返回 int、信号是 void」被放行。但要知道代价：信号的返回类型是 void，
    // activate 时没人接你的返回值，那个 int **凭空丢掉**。想要真拿到返回值，
    // 用 QMetaObject::invokeMethod + Q_RETURN_ARG，那是第 8 讲的事。
    int onTickCounted()           { ++m_tickHits; return m_tickHits; }

    // 形态 4：**根本不是槽**的普通成员函数。函数指针式照样能连——
    // 模板实现里只对「带信号的那个类」要求有 Q_OBJECT（qobject.h 第 254 行），
    // 对被连的函数是不是 slots: 段里的，它一个字都没查。
    void plainNonSlot(int v)      { ++m_plainHits; m_plainLast = v; }

    // 形态 5：同名 + const 性不同的两个重载 —— qConstOverload 的真正用武之地。
    //（信号侧用不上它：信号不该是 const 成员函数；槽/普通成员函数侧才有这个歧义。）
    void handle(int v)            { ++m_nonconstHits; m_lastInt = v; }
    // 非 const 版把参数用掉了；const 版只是「被命中一次」的证据，参数不参与，
    // 所以**不写参数名**（写成 `int` 只留类型）。这是消 -Wunused-parameter 的标准做法，
    // 也让读者一眼看出「这里不需要 v」——比 `(void)v;` 更诚实。
    void handle(int) const        { ++m_constHits; }

public:
    int m_intHits = 0, m_textHits = 0, m_pairHits = 0, m_trimHits = 0;
    int m_tickHits = 0, m_plainHits = 0, m_lastInt = -1, m_pairSum = 0;
    int m_first = -1, m_plainLast = -1, m_nonconstHits = 0;
    mutable int m_constHits = 0;         // const 版 handle 要改它，所以必须 mutable
    QString m_lastText;
};

// ============================================================================
// 三、字符串式 connect 到底把错误推到哪一步：看它「展开成什么」
// ============================================================================
// 本机 qobjectdefs.h 第 49~54 行（非 debug 时是 59~60 行，去掉 qFlagLocation）：
//   #  define QLOCATION   "\0" __FILE__ ":" QT_STRINGIFY(__LINE__)
//   #  define SLOT(a)     qFlagLocation(QT_STRINGIFY_SLOT(a)   QLOCATION)
//   #  define SIGNAL(a)   qFlagLocation(QT_STRINGIFY_SIGNAL(a) QLOCATION)
// QT_STRINGIFY_SIGNAL(a) = QT_PREFIX_CODE(QSIGNAL_CODE, a)，而
// QSIGNAL_CODE=2 / QSLOT_CODE=1 / QMETHOD_CODE=0（第 41~43 行）。
// 所以 SIGNAL(valueChanged(int)) 交给 connect 的**就是一串 const char\***，内容形如
//   "2valueChanged(int)\0源文件名:行号"
// qFlagLocation（第 38 行）只是把「签名 + 现场位置」打包，好让连接失败时的 qWarning
// 能告诉你「是哪一行 connect 挂了」——这是字符串式唯一的、也是相当值钱的好处。
//
// 代价：编译器完全不检查这串字面量和任何函数签名有关系。
// 【反面教材】以下三种全都「编译通过、链接通过」，只在运行期 qWarning 然后静默失效：
//   SIGNAL(valueChanged(int)      // 少一个右括号
//   SLOT(onIntTy(int))            // 槽名拼错
//   SIGNAL(valueChanged(double))  // 根本没有这个重载
// GUI 项目里「按钮点了没反应」，十有八九就是这三行之一。
//
// Qt5/Qt6 差异标注：字符串式在 Qt6 里**仍然可用**（上面那些宏原封不动还在），
// 但官方口径是兼容保留；新代码一律函数指针式。
// ============================================================================

// ============================================================================
// 四、重载信号为什么必须消歧义
// ============================================================================
// `&Sensor::valueChanged` 指向的是一个**重载集**，不是一个函数。C++ 在模板实参
// 推导阶段无法从重载集里选出唯一类型，于是报
//   error: invalid use of non-static member function ... ISO C++ forbids taking
//   the address of an object of non-polymorphic type ... / "data 'Sensor::valueChanged'
//   declared void" 之类的歧义诊断（各编译器措辞不同）。
// 三条出路，本讲全演：
//   A. qOverload<T1, T2>(...)：Qt 提供的语法糖，把「我要哪个签名」写成模板实参。
//      核对 qoverload.h 第 67 行：template <typename... Args> constexpr inline QOverload<Args...> qOverload = {};
//      它的 operator() 接 `R (T::*)(Args...)` 原样返回同一个指针——纯编译期，零运行时开销。
//   B. 显式写出成员指针类型（老代码里最常见，最好读）：
//      using Sig = void (Sensor::*)(int, int);  const Sig s = &Sensor::valueChanged;
//   C. 同名且只差 const 性时，用 qConstOverload<T> / qNonConstOverload<T>。
//      核对 qoverload.h 第 38~48 / 26~36 行：QConstOverload::operator() 只接
//      `R (T::*)(Args...) const`；QNonConstOverload 只接非 const 版。
// ============================================================================

// ============================================================================
// 五、信号能不能不是 void？
// ============================================================================
// 约定：信号必须是 void。但这条**不是 moc 强制的**——本机对
//   `Q_SIGNALS: int bad();`
// 跑 moc 6.12.0，它不报错，照样生成了 `int E::bad() { QMetaObject::activate<...>(...); }`。
// 然而没有任何机制把返回值带回发射点：Q_EMIT 是个调用表达式，值是 void，
// 你写的返回值在 activate 之后就被丢了。所以「非 void 信号」= 一个静默失效的谎言。
// 想要「问一个问题并拿到答案」，用 QMetaObject::invokeMethod + Q_RETURN_ARG（第 8 讲）。
// ============================================================================

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    Sensor    sensor;
    Collector col;

    qInfo() << "=== 1. 函数指针式（首选）：先拿一个不重载的信号做干净示范 ===";
    // 五参重载的第 5 参有默认值 Qt::AutoConnection（qobject.h 第 224 行），四参就够。
    // 返回值是 QMetaObject::Connection；它用的是「受限 bool」转换
    //（qobjectdefs.h 第 689~695 行：operator RestrictedBool() 里判 d_ptr && isConnected_helper()），
    // 头文件注释原话就是「in order to support code using copy-init (e.g. `bool ok = connect(...)`)」，
    // 所以 `const bool ok = connect(...)` 合法，而 `int x = connect(...)` 会报错。
    //
    // 顺手看一眼「编译期检查」长什么样——下面这两行**故意写成正确**的，
    // 而把错误版本留在注释里，你亲手取消注释就见到 static_assert 的人话了：
    //   【反面教材 A】QObject::connect(&sensor, &Sensor::tick, &col, &Collector::onInt);
    //     → 触发 qobject.h 第 258 行 "The slot requires more arguments than the signal provides."
    //   【反面教材 B】QObject::connect(&col, &Collector::onInt, &sensor, &Sensor::tick);
    //     → 反过来（信号无参、槽要 int）同样编译不过；另外把没有 Q_OBJECT 的类当发送者时，
    //       你会在第 254 行看到 "No Q_OBJECT in the class with the signal"。
    const bool okTick = QObject::connect(&sensor, &Sensor::tick,
                                          &col, &Collector::onTickCounted);
    sensor.fireTick();
    qInfo() << "  connect(tick -> onTickCounted) 当 bool 用：" << okTick
            << " 槽命中 =" << col.m_tickHits
            << "（槽的返回值是 onTickCounted() 里的 int，但信号是 void，返回值被丢弃）";

    qInfo() << "=== 2. 重载信号的三种消歧义 ===";
    // A. qOverload<T>
    QObject::connect(&sensor, qOverload<const QString &>(&Sensor::valueChanged),
                     &col, &Collector::onText);
    sensor.publish(QStringLiteral("hello"));
    qInfo() << "  qOverload<const QString &> ：onText 命中" << col.m_textHits
            << " lastText =" << col.m_lastText;

    // B. 显式成员指针类型
    using IntIntSig = void (Sensor::*)(int, int);
    const IntIntSig pairSignal = &Sensor::valueChanged;
    QObject::connect(&sensor, pairSignal, &col, &Collector::onPair);
    sensor.publishPair(3, 4);
    qInfo() << "  显式 void (Sensor::*)(int,int) ：onPair 命中" << col.m_pairHits
            << " pairSum =" << col.m_pairSum;

    // C. qNonConstOverload / qConstOverload（切的是**槽**那一侧的同名 const 重载）
    //    信号侧用 qOverload<int> 消歧义，槽侧用 qNonConstOverload<int> 选非 const 版。
    QObject::connect(&sensor, qOverload<int>(&Sensor::valueChanged),
                     &col, qNonConstOverload<int>(&Collector::handle));
    sensor.publish(41);
    qInfo() <<  "  qNonConstOverload<int>(&Collector::handle) ：非 const 版命中"
            << col.m_nonconstHits << " const 版命中" << col.m_constHits
            << " lastInt =" << col.m_lastInt;
    // 把上面那条的槽换成 qConstOverload<int>(&Collector::handle)，
    // 计数就会跑到 m_constHits 上——这就是这一对宏的唯一用途，别把它跟 qOverload 混了。

    qInfo() << "=== 3. 槽参数比信号少 / 普通成员函数当槽 ===";
    QObject::connect(&sensor, qOverload<int, int>(&Sensor::valueChanged),
                     &col, &Collector::onFirstOnly);
    sensor.publishPair(11, 22);
    qInfo() << "  两参信号 -> 一参槽 ：命中" << col.m_trimHits
            << " 收到第一个参数 =" << col.m_first;
    // 形态 4 的实证：plainNonSlot 从来没写进任何 slots: 段，函数指针式照样连得上。
    QObject::connect(&sensor, qOverload<int>(&Sensor::valueChanged),
                     &col, &Collector::plainNonSlot);
    sensor.publish(63);
    qInfo() << "  非槽成员函数 plainNonSlot 命中" << col.m_plainHits
            << " 收到 =" << col.m_plainLast
            << "（想让它被元对象反射/字符串式找到，就得改成真正的 slot）";

    qInfo() << "=== 4. 第 5 个参数：Qt::ConnectionType ===";
    // 核对 qnamespace.h 第 1313~1320 行：
    //   AutoConnection, DirectConnection, QueuedConnection, BlockingQueuedConnection,
    //   UniqueConnection = 0x80, SingleShotConnection = 0x100
    // 前四个是「派发方式」（枚举值从 0 连续排），后两个是**位标志**，能和前面用 | 合写。
    // 同线程下 AutoConnection 退化成 DirectConnection：发射那一行里同步把槽调完。
    QObject::connect(&sensor, qOverload<int>(&Sensor::valueChanged),
                     &col, &Collector::onInt, Qt::DirectConnection);
    sensor.publish(7);
    qInfo() <<  "  DirectConnection 下发射完立刻可见：onInt 命中" << col.m_intHits
            << " lastInt =" << col.m_lastInt;
    // 提醒：Qt::BlockingQueuedConnection 在**同线程**使用必死锁（你在阻塞自己等的队列）。
    // Qt::SingleShotConnection（0x100）语义是「命中一次就自动断开」，本机头文件里这个
    // 枚举值确实在（qnamespace.h 第 1319 行），但它配套的返回值类型
    // QMetaObject::ConnectResult 在本机 Qt 6.12.0 的头文件树里 grep 不到，
    // 所以本模块不拿它举例——读者在自己机器上先 grep 确认再用。

    qInfo() << "=== 5. Qt::UniqueConnection：防重复，但不抹历史 ===";
    // 第 4 节已经把 (sensor, valueChanged(int), col, onInt) 这条四元组连过一次了。
    // 下面再带 UniqueConnection 连**完全相同**的四元组，预期是「不新增、返回 false」。
    const int hitsBefore = col.m_intHits;
    const bool uniqueRet = QObject::connect(&sensor, qOverload<int>(&Sensor::valueChanged),
                                             &col, &Collector::onInt,
                                             Qt::UniqueConnection);
    sensor.publish(200);
    qInfo() << "  UniqueConnection 重连返回值 =" << uniqueRet << "（预期 false：没新增连接）"
            << " 发射后 onInt 增量 =" << (col.m_intHits - hitsBefore)
            << "（=1；若你此前重复连了 N 次，这里就是 N——它一条历史都不删）";
    // 关键结论：UniqueConnection 只保证「这条完全相同的连接不再加第二条」，
    // 已经存在的重复记录一条都不删。想让连接幂等，先 disconnect 再 connect，
    // 或者把连接收敛到一个只跑一次的初始化函数里。
    //
    // 而且 **lambda 用不了 UniqueConnection**。Qt 自己的话（本机 qobject.cpp 第 5482~5483 行）：
    //   "Qt::UniqueConnections do not work for lambdas, non-member functions and functors;
    //    they only apply to connecting to member functions."
    // 为什么？去重要比「槽的身份」：成员函数指针每次都是同一个地址，可比；
    // lambda 每写一次都是一个新的闭包类型/新对象，没有可比的身份，所以 Qt 直接拒掉。
    //
    // ★ 但「怎么拒」取决于构建类型，这一点比「会被拒」更容易坑到人。分水岭是 QT_NO_DEBUG：
    //   · 定义 QT_NO_DEBUG（本仓库的 Release 就是这样，见
    //     build/CMakeFiles/metaobject_02_connect_variants.dir/flags.make 里那排 -D）
    //     → qobject.h 第 268~270 行那句 Q_ASSERT_X **整个被编译掉**（qassert.h 第 44~50 行），
    //       控制流走到 qobject.cpp 第 5578~5581 行的运行期守卫：那里调 connectWarning()
    //       （本机同文件第 2773~2784 行，里面是 qCWarning(lcConnect, …)，**不是**裸 qWarning，
    //        所以这行前面带类别前缀 qt.core.qobject.connect:），
    //       然后 return 一个**无效**的 Connection。程序照常往下跑，回调永远不被叫。
    //   · 不定义 QT_NO_DEBUG（Debug 语义）→ 断言活着，同一句直接 abort 掉进程。
    //   本机各编一遍跑过，原文：
    //     Release: qt.core.qobject.connect: QObject::connect(Sensor, Collector): unique
    //              connections require a pointer to member function of a QObject subclass
    //     Debug  : ASSERT failure in : "QObject::connect: Unique connection requires the slot
    //              to be a pointer to a member function of a QObject subclass.",
    //              file D:/Qt/6.12.0/mingw_64/include/QtCore/qobject.h, line 268（退出码 127）
    // 所以下面这一小段只在 Release 里现场演；Debug 里现场演会把这一讲崩掉，那不算教学算捣乱。
#ifdef QT_NO_DEBUG
    int lambdaUniqueHits = 0;
    const QMetaObject::Connection lambdaUnique =
        QObject::connect(&sensor, &Sensor::tick, &col,
                         [&lambdaUniqueHits] { ++lambdaUniqueHits; },
                         Qt::UniqueConnection);
    sensor.fireTick();
    qInfo() << "  lambda+Unique（Release：断言被编译掉的那条路）连接有效 ="
            << bool(lambdaUnique) << "｜触发 tick 后 lambda 命中 =" << lambdaUniqueHits
            << "（两个都该是 false / 0；上面那句 qWarning 就是它的尸检报告）";
#else
    qInfo() << "  lambda+Unique：这一版没定义 QT_NO_DEBUG，qobject.h 第 268 行的断言还活着，"
            << "现场连一次会当场 abort，所以只报配置不演。想演：CMAKE_BUILD_TYPE=Debug 重编本讲。";
#endif

    qInfo() << "=== 6. 带 context object 的 lambda 式 ===";
    QObject::connect(&sensor, qOverload<const QString &>(&Sensor::valueChanged),
                     &col, [&col](const QString &t) { col.m_lastText = t + QStringLiteral("+λ"); });
    sensor.publish(QStringLiteral("ctx"));
    qInfo() << "  带 context 的 lambda ：lastText =" << col.m_lastText;

    qInfo() << "=== 7. 「无 context」三参 connect：它偷偷把 context 设成了 sender ===";
    // 这条**必须**看明白，否则第 4 讲的悬空崩溃早晚轮到你。
    // 本机 qobject.h 第 278~286 行，整段被 #ifndef QT_NO_CONTEXTLESS_CONNECT 包住：
    //   template <typename Func1, typename Func2>
    //   static inline QMetaObject::Connection
    //       connect(const typename QtPrivate::FunctionPointer<Func1>::Object *sender,
    //               Func1 signal, Func2 &&slot)
    //   { return connect(sender, signal, sender, std::forward<Func2>(slot), Qt::DirectConnection); }
    // 所以三参写法不是「没有 context」，而是「context = sender，type 强制 DirectConnection」。
    // 两个直接后果：
    //   ① 连接的寿命跟着 **sender** 走，不是跟着你捕获的东西走；sender 活得比捕获对象久 → 悬空；
    //   ② 谁发射就在谁的线程里同步执行，跨线程「自动排队」的好处一点也没有。
    // Qt 还提供了 QT_NO_CONTEXTLESS_CONNECT 这个编译期开关，把整条路堵死：
    // 开了它，三参 connect 直接编译不过，逼你显式写 context。安全至上的团队会全局打开。
    int freeHits = 0;
    QObject::connect(&sensor, &Sensor::tick, [&freeHits] { ++freeHits; });
    sensor.fireTick();
    qInfo() <<  "  三参 lambda（context 隐式=sender）：freeHits =" << freeHits;

    qInfo() << "=== 8. 字符串式 SIGNAL()/SLOT()：还活着，但错误被推到运行期 ===";
    // 核对 qobject.h 第 212~213 行：
    //   static QMetaObject::Connection connect(const QObject *sender, const char *signal,
    //                                          const QObject *receiver, const char *member,
    //                                          Qt::ConnectionType = Qt::AutoConnection);
    const bool okStr = QObject::connect(&sensor, SIGNAL(valueChanged(int)),
                                          &col, SLOT(onInt(int)));
    const int beforeStr = col.m_intHits;
    sensor.publish(555);
    qInfo() << "  字符串式 connect 得到有效连接？" << okStr
            << " 发射后 onInt 增量 =" << (col.m_intHits - beforeStr)
            << "（=2：第 4 节那条 + 本节这条，两条都是真实存在的连接）";
    // 注意 SIGNAL()/SLOT() 括号里写的是**归一化后的签名**（类型列表），不是实参。
    // 写错的时候编译不会拦你（第三节已解释）。

    qInfo() << "=== 9. 用元对象手工校验字符串签名（字符串式的最后一根救草）===";
    // 核对 qobjectdefs.h 第 278~282 行：
    //   static bool checkConnectArgs(const char *signal, const char *method);
    //   static QByteArray normalizedSignature(const char *method);
    //   static QByteArray normalizedType(const char *type);
    qInfo() << "  normalizedSignature(\"valueChanged( int )\") ="
            << QMetaObject::normalizedSignature("valueChanged( int )");
    qInfo() << "  normalizedType(\"const QString &\") ="
            << QMetaObject::normalizedType("const QString &");
    qInfo() << "  checkConnectArgs(valueChanged(int), onInt(int))   ="
            << QMetaObject::checkConnectArgs("valueChanged(int)", "onInt(int)");
    qInfo() << "  checkConnectArgs(valueChanged(int), onText(QString)) ="
            << QMetaObject::checkConnectArgs("valueChanged(int)", "onText(QString)");
    // 这两条静态函数就是「把运行期检查提前到你自己手上」的工具。
    // 但注意：它只能在你手里已经有字符串时才帮上忙——函数指针式压根不需要它，
    // 编译器已经替你做完了。

    qInfo() << "=== 10. 信号指针 → QMetaMethod（第 8 讲反射的入口）===";
    // 核对 qmetaobject.h 第 222 行：static QMetaMethod fromSignal(PointerToMemberFunction)
    const QMetaMethod mm = QMetaMethod::fromSignal(&Sensor::tick);
    qInfo() << "  fromSignal(&Sensor::tick)：" << mm.methodSignature()
            << " 是信号?" << (mm.methodType() == QMetaMethod::Signal)
            << " 所属类 =" << mm.enclosingMetaObject()->className();

    // ── 自动脚本 ──────────────────────────────────────────────────────────
    QTimer::singleShot(120, [&sensor, &col] {
        qInfo() << "=== 11. 事件循环里的发射：连接表跨事件循环一直有效 ===";
        const int a = col.m_intHits, b = col.m_textHits;
        sensor.publish(999);
        sensor.publish(QStringLiteral("timer"));
        // onInt 到此刻挂着 2 条连接：§4 的 DirectConnection 一条、§8 的字符串式一条
        //（§5 那条被 UniqueConnection 挡掉，没加进去）→ 预期增量 2；
        // onText 挂着 2 条（§2-A 与 §6）→ 预期增量 2。
        qInfo() << "  onInt 增量 =" << (col.m_intHits - a)
                << " onText 增量 =" << (col.m_textHits - b);
    });
    QTimer::singleShot(300, [] {
        qInfo() << "=== 第 2 讲演示结束，自动退出 ===";
        QCoreApplication::quit();
    });

    return app.exec();
}

#include "02_connect_variants.moc"

// ——— 老师傅的话 ———
/*
 * 徒弟，这一讲你只要带走一句话：**能用函数指针就别用字符串，能带 context 就别省。**
 *
 * 把两种写法的「出错时刻」在脑子里摆一摆就明白为什么我咬这么死：
 *   函数指针式 —— 签名不对、参数个数不对、类忘了 Q_OBJECT、重载有歧义，四类错误
 *                全在编译期被 static_assert / 模板推导拦下。报错文案是 Qt 故意写成
 *                人话的，原文就在头文件里（我上面抄了：qobject.h 第 242/250/254/258 行）。
 *                你连「跑一次看看」都不需要。
 *   字符串式   —— 这四类错误全部溜过编译器，到运行期在元对象表里查字符串，
 *                查不到就 qWarning 一句，然后**程序继续正常运行**。
 *                「继续正常运行」才是最毒的：没崩，按钮就是没反应，那句 warning
 *                在 release 包里没人看。线上你花两小时才发现是少打了一个括号。
 *                它唯一的优点是报错带现场（qobjectdefs.h 第 49 行的 QLOCATION 把文件:行号塞进签名串），
 *                讽刺的是这个优点只在「你已经犯了错」之后才生效。
 *
 * 第二个必须刻住的：**三参 connect 不是「没有 context」，是 context = sender 且强制
 * DirectConnection**（qobject.h 第 278~286 行的函数体白纸黑字）。这条实现藏在模板里，
 * 十个人有九个半没读过。它意味着连接的寿命跟着**发送者**走。你把一个捕获了 UI 控件的
 * lambda 连到生命周期更长的 model 上，控件先死、model 后发射 —— 第 4 讲那个「一按刷新
 * 就崩」的事故就是这么来的。别省那个参数，多写一个 `this` 死不了人。
 *
 * 第三，`Qt::UniqueConnection` 不是幂等开关，它只是「这条四元组（sender+signal+
 * receiver+slot）别再新增一条」。已经存在的重复它一条不删；而且判定四元组时不含
 * ConnectionType，所以「带 UniqueConnection 的那次」会被已存在的普通连接挡掉。
 * 想真幂等：先 disconnect 再 connect，或把连接收进只跑一次的初始化里。
 *
 * 最后，`qOverload` / `qConstOverload` / `qNonConstOverload` 这三个 constexpr 变量
 * 不产生任何运行时指令（qoverload.h 里它们只是把成员指针原样返回），放心用。
 * 顺手把讹传划掉：**Qt 没有 `qMetaTypeClass`**，本机整棵头文件树 grep 零命中。
 */

// ——— 小练习 ———
/*
 *   1. 把第 2 节 A 那行的 `qOverload<const QString &>` 去掉，直接写 `&Sensor::valueChanged`，
 *      构建并把编译器原始报错抄下来（GCC 会给你一长串「candidates are:」把三个重载全列出来）。
 *      再把它换成 B 那种显式成员指针类型写法，确认报错消失。
 *   2. 把第 5 节里那句 `&Collector::onInt` 换成一个捕获型 lambda，仍带
 *      `Qt::UniqueConnection`，跑一遍——你会看到 Qt 自己写的那条断言消息原文。
 *      然后把第 4 节的 `Qt::DirectConnection` 改成 `Qt::QueuedConnection` 再跑，
 *      观察「发射完立刻打印」变成「打印在后面的事件循环里」，onInt 增量那一行会变化。
 *   3. 在第 8 节把 `SLOT(onInt(int))` 改成 `SLOT(onIntTy(int))`：构建（应当照样通过），
 *      运行，观察那句 qWarning 和「onInt 增量 = 0」；再改成 `SLOT(onInt(int))` 恢复。
 *      最后给第 9 节补一条 `QMetaObject::checkConnectArgs("valueChanged(int)", "onIntTy(int)")`，
 *      体会「把运行期检查提前到自己手上」是什么感觉。
 */
// ============================================================================
