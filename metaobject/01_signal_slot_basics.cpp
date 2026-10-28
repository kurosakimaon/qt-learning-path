// ============================================================================
// 第 1 讲 · 自定义信号与槽的第一课
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   把「信号（signal）/ 槽（slot）」这两个关键字彻底讲明白，一次讲透四件事：
//     1) signals: / slots: 这两个「段落」在编译器眼里到底是什么（宏展开后是什么）
//     2) 信号只声明、不实现——实现体由 moc 生成（第 5 讲会看到真产物）
//     3) 一个信号可以有 0 个、1 个、N 个接收者；发射 = 挨个调用
//     4) 槽就是普通成员函数，你完全可以「不走信号」直接调它
//   另外把「信号参数必须是可拷贝类型」这条铁律讲清：为什么是 moc 帮你把参数
//   类型写进元对象表（QMetaType::Int / QMetaType::QString…）导致的。
//
// 对应构建目标名：metaobject_01_signal_slot_basics
//   add_executable(metaobject_01_signal_slot_basics 01_signal_slot_basics.cpp)
//   target_link_libraries(metaobject_01_signal_slot_basics PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)
//
// 运行前须知：
//   · 本示例只建一个 QApplication，不显示任何窗口，全部结论用 qInfo() 打到控制台，
//     并用 QTimer::singleShot 排脚本、最后自动 quit()——无人值守能跑完。
//   · 类都写在 .cpp 里，所以文件末尾必须 #include "01_signal_slot_basics.moc"
//     （AUTOMOC 会为它生成同名 .moc；这是「Q_OBJECT 类写在 .cpp」的固定接法）。
//   · 本机当前无法编译链接 Qt 程序，本示例**未在本机实测编译**。所有 API 都对着本机
//     真实头文件核对过：
//       QtCore/qobject.h        —— receivers() 第 369 行（注意它在第 366 行起的
//                                  protected 段里，外部代码调不动）、
//                                  isSignalConnected() 第 370 行、blockSignals() 第 141 行、
//                                  destroyed 信号 第 352 行
//       QtCore/qtmetamacros.h   —— 第 48~49 行 Q_SLOTS/Q_SIGNALS 宏定义、第 51 行 Q_EMIT、
//                                  第 44~45 行 slots/signals 别名、第 31~32 行
//                                  QT_ANNOTATE_ACCESS_SPECIFIER 定义为空
//       QtCore/qobjectdefs.h    —— QMetaObject::indexOfSignal 第 264 行、method(int) 第 272 行
//       QtCore/qmetaobject.h    —— QMetaMethod::methodSignature 第 26 行、parameterCount 第 32 行、
//                                  parameterTypes 第 36 行、parameterTypeName 第 37 行、
//                                  access() 第 41 行、methodType() 第 48 行
//       QtCore/qtimer.h         —— singleShot(Duration, Functor) 无 context 版第 84~88 行
//       QtCore/qstring.h        —— static QString number(double, char, int) 第 903 行
//       QtCore/qdebug.h         —— QDebug 的 quote()/noquote()（第 129/130 行；注意**没有**
//                                  quoted() 这种函数）
// ============================================================================

#include <QApplication>
#include <QObject>
#include <QMetaMethod>      // 核对：/c/msys64/ucrt64/include/qt6/QtCore/QMetaMethod 存在；
                            // 不引它，QMetaMethod 只有前置声明（qobjectdefs.h），是 incomplete type
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QDebug>

// ============================================================================
// 一、先拆宏：signals: 和 slots: 到底给不给你访问权限
// ============================================================================
// 本机 qtmetamacros.h 第 48~49 行原文：
//
//   # define Q_SLOTS    QT_ANNOTATE_ACCESS_SPECIFIER(qt_slot)
//   # define Q_SIGNALS  public QT_ANNOTATE_ACCESS_SPECIFIER(qt_signal)
//
// 而 QT_ANNOTATE_ACCESS_SPECIFIER(x) 在本机只有一个定义（第 31~32 行），就是**空**：
//
//   #ifndef QT_ANNOTATE_ACCESS_SPECIFIER
//   # define QT_ANNOTATE_ACCESS_SPECIFIER(x)
//   #endif
//
// 于是真相很反直觉，但完全可以从头文件推出来：
//   · `signals:`  →  展开成 `public:`         —— 合法，而且**永远**是 public。
//     这就是「槽可以连到信号、外部也能直接调信号」的根因：信号不是 protected 的。
//   · `slots:`    →  展开成 `:`               —— 语法错误！光一个冒号不成句。
//     所以必须写 `public slots:` / `protected slots:` / `private slots:`，
//     访问权限得你自己给；`slots` 这个词本身只递给 moc 一个记号，别的什么都不干。
//   · 顺带：`emit` / `Q_EMIT` 也是空宏（第 51 行 `# define Q_EMIT`），
//     纯粹写给人看的注释性记号，编译器眼里等于没写。Qt 自己的源码一律写 Q_EMIT，
//     是为了避开第三方库里同名的 `emit`（这个宏可以用 QT_NO_KEYWORDS 整个关掉）。
// ============================================================================

// ── 发送者（sender）：温度计 ──────────────────────────────────────────────
class Thermometer : public QObject
{
    Q_OBJECT
public:
    explicit Thermometer(QObject *parent = nullptr) : QObject(parent) {}

    // 「业务动作」里发射信号。注意：发射信号在语法上就是「调用一个成员函数」。
    void setTemperature(double celsius)
    {
        m_temperature = celsius;
        Q_EMIT temperatureChanged(m_temperature);   // 发一个带参信号
        if (m_temperature >= 100.0)
            Q_EMIT boiling();                       // 发一个无参信号
    }

    double temperature() const { return m_temperature; }

    // 【坑，很容易踩】QObject::receivers(const char *) const 在 Qt6 里是 **protected**
    //（核对 QtCore/qobject.h：第 366 行是 `protected:`，第 369 行才是
    //   `int receivers(const char *signal) const;`），所以在 main 里写
    //   `thermo.receivers(...)` 编译器直接拒绝。想要「这个信号上挂了几个接收者」
    //   这个证据，只能让被观察的对象自己开一扇门——成员函数内部访问 protected 合法。
    int receiverCount(const char *signal) const { return receivers(signal); }

    // 【关键】这里**没有**上面两个信号的任何函数体。
    // 如果你在本类的 .cpp 里写 `void Thermometer::boiling() {}`，
    // 编译器会报「redefinition」——因为 moc 已经替你在 01_signal_slot_basics.moc
    // 里生成过实现体了（第 5 讲会真的把那段生成代码跑出来看）。
Q_SIGNALS:
    // 信号声明长这样：返回类型必须是 void，不能有默认实参以外的「实现」。
    // 参数类型会被 moc 写进元对象表（第 5 讲实测输出里能看到
    //   QtMocHelpers::SignalData<void(int)>(..., { { QMetaType::Int, 3 } })
    // 这类条目），所以参数必须是「有 QMetaType 的类型」——见本讲第四节。
    void temperatureChanged(double celsius);
    void boiling();

private:
    double m_temperature = 0.0;
};

// ── 接收者 A：报警器。用标准写法把槽标成 public slots: ────────────────────
class Alarm : public QObject
{
    Q_OBJECT
public Q_SLOTS:                        // 展开 = `public:`，Qt 源码里最常见的写法
    void onTemperature(double celsius)
    {
        ++m_hits;
        m_last = celsius;
        m_log.append(QStringLiteral("Alarm 收到 %1℃")
                         .arg(QString::number(celsius, 'f', 1)));
    }
    void onBoiling()
    {
        ++m_boilHits;
        m_log.append(QStringLiteral("Alarm 尖叫：开了！"));
    }

public:
    int hits() const { return m_hits; }
    int boilHits() const { return m_boilHits; }
    QStringList log() const { return m_log; }

    double m_last = 0.0;               // 故意留 public，方便断言

private:
    int m_hits = 0;
    int m_boilHits = 0;
    QStringList m_log;
};

// ── 接收者 B：记录仪。它**没有** slots: 段，只有一个普通成员函数 ──────────
// 这演示一个常被误解的点：用「函数指针式 connect」时，被连的函数**不必**是槽，
// 也不必带 slots: 标记；`slots:` 只对字符串式 connect 和元对象反射有意义。
class Recorder : public QObject
{
    Q_OBJECT
public:
    void record(double celsius) { m_sum += celsius; ++m_count; }   // 普通成员函数
    double sum() const { return m_sum; }
    int count() const { return m_count; }

private:
    double m_sum = 0.0;
    int m_count = 0;
};

// ── 接收者 C：中继器。演示「信号 → 信号」式的转发（用槽转发，最稳）────────
class Relay : public QObject
{
    Q_OBJECT
public Q_SLOTS:
    // 收到上游信号后，原样再发一次自己的信号。这是「信号中继」的通用做法，
    // 任何 Qt 版本、任何 connect 写法都成立，不依赖「信号可以直接连信号」这种特例。
    void forwardTemperature(double celsius) { Q_EMIT relayed(celsius); }
    void forwardBoiling()                   { Q_EMIT relayedBoiling(); }

Q_SIGNALS:
    void relayed(double celsius);
    void relayedBoiling();
};

// ============================================================================
// 二、信号发射给谁？——发射 = 遍历该信号登记的槽表，逐个调用
// ============================================================================
// 你在第 5 讲会看到 moc 生成的信号实现体。本机 moc 6.12.0 对本文件的真实输出是
//（下面两行是从 moc 产物里抄的，一个带参、一个无参）：
//     void Thermometer::temperatureChanged(double _t1)
//         { QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1); }
//     void Thermometer::boiling()
//         { QMetaObject::activate(this, &staticMetaObject, 1, nullptr); }
// `activate` 干的事：拿到「this 这个对象上、这个信号索引对应的连接链表」，
// 按 connect 的先后顺序，一个一个把槽调用出来。所以：
//   · 没连接时发射信号 = 空转，什么也不会发生，也不报错（这是好事）；
//   · 连了 3 次就调 3 次（**重复 connect 会重复调用**，这是新手最常见的「槽跑两遍」事故）；
//   · 调用顺序 = connect 顺序，不是槽的优先级；
//   · 同线程下默认 Qt::AutoConnection 退化成 DirectConnection，
//     也就是说：**槽是在发射信号的那一行里被同步调完的**，Q_EMIT 返回时槽早就跑完了。
// ============================================================================

// ============================================================================
// 三、槽就是普通成员函数
// ============================================================================
// 「槽」不是一种特殊函数，它只是「被登记过、可以在事件到来时被 Qt 调用的成员函数」。
// 你随时可以自己直接调它——参数、返回值、访问权限全都按普通 C++ 规矩来。
// 本讲下面会同时演示「走信号」和「直接调」两条路，并把调用次数分别算清楚。
// ============================================================================

// ============================================================================
// 四、信号参数为什么必须「可拷贝」
// ============================================================================
// 看 moc 的输出就知道：每个信号参数的类型都被登记成一条 QMetaType（本讲用 double
// 与无参；第 5 讲实测输出里有 { QMetaType::Int, 3 } / { QMetaType::QString, 5 } 这种行）。
// 为什么要登记类型？因为**跨线程（Qt::QueuedConnection）**时，Qt 不能在你发出信号的
// 那一刻调用槽——它要把参数**打包进一个事件**丢到接收者线程的事件队列里，稍后再解包调用。
// 「打包/解包」就是拷贝构造 + QMetaType 的序列化，所以：
//   · 参数类型必须是可拷贝构造的（值语义）；
//   · 自定义类型想跨线程传，还得让它有 QMetaType：类外写 Q_DECLARE_METATYPE(MyType)
//     （核对：QtCore/qmetatype.h 第 1503 行），必要时 qRegisterMetaType<MyType>()（第 1437 行）。
//   · 声明成 `void sig(std::unique_ptr<T>)` 这种「不可拷贝」的信号，
//     编译期就会因为 QMetaType 造不出来而炸——这其实是 Qt 在保护你。
// 纯同线程的 DirectConnection 看着像能「蒙过去」，但那是巧合，别赌：
// 参数类型一旦被登记，就要求它满足上面条件（第 3、4 讲会正面处理生命周期）。
// ============================================================================

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    Thermometer thermo;
    Alarm       alarm;
    Recorder    recorder;
    Relay       relay;

    qInfo() << "=== 元信息：信号是 public 的吗？===";
    // 用元对象反射看「信号」这个方法条目的访问权限。
    // 核对：QMetaObject::indexOfSignal(const char*) —— qobjectdefs.h 第 264 行
    //       QMetaObject::method(int)                —— qobjectdefs.h 第 272 行
    //       QMetaMethod::methodType()/access()      —— qmetaobject.h 第 48/41 行
    const QMetaObject *mo = thermo.metaObject();
    const int sigIdx = mo->indexOfSignal("boiling()");
    if (sigIdx >= 0) {
        const QMetaMethod m = mo->method(sigIdx);
        qInfo() << "  boiling() 在元对象表里的索引 =" << sigIdx
                << " methodType==Signal?" << (m.methodType() == QMetaMethod::Signal)
                << " access==Public?" << (m.access() == QMetaMethod::Public);
    } else {
        qInfo() << "  找不到 boiling()：说明 Q_OBJECT 没生效或没让 moc 扫到本类";
    }

    qInfo() << "=== 1. 一个信号，两个接收者 ===";
    // 写法 A：成员函数指针当槽（Alarm::onTemperature 是真正的 slot）
    QObject::connect(&thermo, &Thermometer::temperatureChanged,
                     &alarm,  &Alarm::onTemperature);
    // 写法 B：普通成员函数当槽（Recorder::record 根本没标 slots:）
    QObject::connect(&thermo, &Thermometer::temperatureChanged,
                     &recorder, &Recorder::record);
    // 写法 C：接到中继器，中继器再往下发
    QObject::connect(&thermo, &Thermometer::temperatureChanged,
                     &relay,  &Relay::forwardTemperature);
    QObject::connect(&relay,  &Relay::relayed,
                     &recorder, &Recorder::record);      // 中继的下游再记一次

    thermo.setTemperature(42.0);
    qInfo() << "  发一次 42℃ → Alarm 次数:" << alarm.hits()
            << "Recorder 次数（含中继下游）:" << recorder.count()
            << "Recorder 累计:" << QString::number(recorder.sum(), 'f', 1);

    // receivers() 只有一个「字符串签名」版本（核对：qobject.h 第 369 行
    //   int receivers(const char *signal) const;）
    // 它没有函数指针重载，所以这里只能用 SIGNAL() 宏——这是本讲唯一用字符串宏的地方，
    // 目的是「查询」不是「建立连接」，写错了不会静默，会得到 0/负数这种明显异常值。
    // 再强调一遍：receivers() 是 protected（qobject.h 第 366 行的 `protected:` 段），
    // 外部拿不到，故走 Thermometer 自己开的 receiverCount() 这层壳。
    qInfo() << "  thermo.temperatureChanged 上登记的接收者个数 ="
            << thermo.receiverCount(SIGNAL(temperatureChanged(double)));

    qInfo() << "=== 2. 重复 connect 会重复调用（事故高发区）===";
    const int before = alarm.hits();
    QObject::connect(&thermo, &Thermometer::temperatureChanged,
                     &alarm,  &Alarm::onTemperature);        // 同一个 sender/signal/receiver/slot 再连一次
    thermo.setTemperature(50.0);
    qInfo() << "  第二次发射后 Alarm 增量 =" << (alarm.hits() - before)
            << "（=2 说明重复连接真的会调两遍；想避免请看第 2 讲的 Qt::UniqueConnection）";

    qInfo() << "=== 3. 槽可以被普通调用 ===";
    const int snapshot = alarm.hits();
    alarm.onTemperature(99.9);                               // 不经过任何信号
    qInfo() << "  直接调槽：次数从" << snapshot << "变到" << alarm.hits()
            << "，日志最后一行：" << alarm.log().last();

    qInfo() << "=== 4. 无参信号 + 一个信号被 0 个接收者听 ===";
    QObject::connect(&thermo, &Thermometer::boiling, &alarm, &Alarm::onBoiling);
    thermo.setTemperature(105.0);
    qInfo() <<  "  开了！Alarm 的 boiling 计数 =" << alarm.boilHits();

    // 断开后发射：不会有任何人收到，也不会报错。
    QObject::disconnect(&thermo, &Thermometer::boiling, &alarm, &Alarm::onBoiling);
    thermo.setTemperature(110.0);
    qInfo() << "  disconnect 后再开 110℃，Alarm 的 boiling 计数仍 =" << alarm.boilHits()
            << "（发信号给空集合是安全的）";

    qInfo() << "=== 5. blockSignals：让发射变哑 ===";
    const int hitsBeforeBlock = alarm.hits();
    thermo.blockSignals(true);          // 核对：qobject.h 第 141 行 bool blockSignals(bool)
    thermo.setTemperature(60.0);        // 内部照改，信号照「发」，但没人收到
    thermo.blockSignals(false);
    qInfo() << "  屏蔽期间发射了一次，Alarm 增量 =" << (alarm.hits() - hitsBeforeBlock)
            << "（0 = 屏蔽成功；注意 Q_EMIT 本身没被禁掉，是 activate 那一步不派发）";

    qInfo() << "=== 6. 参数类型确实被登记进了元对象表 ===";
    // 用反射把 temperatureChanged(double) 的参数类型打出来，证明「类型信息被 moc 存下来了」。
    // 核对：QMetaMethod::methodSignature() 第 26 行、parameterCount() 第 32 行、
    //       parameterTypeName(int) 第 37 行、returnType() 第 30 行（均在 QtCore/qmetaobject.h）
    const int tIdx = mo->indexOfSignal("temperatureChanged(double)");
    if (tIdx >= 0) {
        const QMetaMethod m = mo->method(tIdx);
        qInfo() << "  方法签名:" << m.methodSignature()
                << " 参数个数:" << m.parameterCount()
                << " 第 0 个参数类型名:" << m.parameterTypeName(0)
                << " 返回类型 id:" << m.returnType();
    }

    // ── 自动脚本：让示例「无人值守」也能演示事件循环里的派发 ──────────────
    // QTimer::singleShot(ms, functor) 的 functor 版会带上 sender 作为 context
    //（核对 qtimer.h 第 86~89 行：无 context 版内部转成 receiver=nullptr + DirectConnection），
    // 所以这里用 nullptr context 的写法，回调在**当前线程的事件循环**里被执行。
    QTimer::singleShot(120, [&thermo, &alarm] {
        qInfo() << "=== 7. 事件循环里的异步发射 ===";
        const int b = alarm.hits();
        thermo.setTemperature(75.0);     // 依然是同步调用完槽，但这条语句跑在 exec() 内部
        qInfo() << "  由定时器触发的发射：Alarm 增量 =" << (alarm.hits() - b);
    });
    QTimer::singleShot(300, [] {
        qInfo() << "=== 第 1 讲演示结束，自动退出 ===";
        QCoreApplication::quit();
    });

    return app.exec();
}

// Q_OBJECT 类定义在 .cpp 里时，AUTOMOC 生成的是「本文件名.moc」，
// 必须在**所有类定义之后**手动包含它，否则这些类的 vtable / staticMetaObject
// / 信号实现体都找不到，链接期一片 undefined reference。
#include "01_signal_slot_basics.moc"

// ——— 老师傅的话 ———
/*
 * 徒弟，这一讲最容易误解的一点，是你得把「信号」从「事件通知的特殊东西」这个
 * 浪漫想象里拽出来，换成一句大白话：**信号就是一个被 moc 替你写好实现体的 public
 * 成员函数，函数的内容就是「查这张连接表，把表里的槽挨个调用一遍」。**
 * 一旦你这么理解，一堆问题自动通了：
 *
 *  · 为什么没连接时发射不报错？——因为表是空的，for 循环一次都不进。
 *  · 为什么重复 connect 就重复调用？——它不查重，表里就多一条记录。
 *  · 为什么槽能当普通函数调？——槽本来就是成员函数，「槽」只是「被登记过」的身份。
 *  · 为什么 Q_EMIT 后面代码立刻继续、槽里的值已经改好了？——同线程默认是
 *    DirectConnection，发射是**同步**调用，不是「异步发消息」。这是新手写成
 *    「发完信号马上读槽的结果」却拿到旧值时最反直觉的一条：反过来，同步调用
 *    时你在槽里改的东西，Q_EMIT 返回后立刻可见。
 *
 * 第二条要记牢的是那两个宏的不对称：`signals:` 白送 public，`slots:` **什么都不送**，
 * 你必须写 `public slots:`。这不是风格问题，是宏展开（qtmetamacros.h 第 48~49 行
 * 把 Q_SLOTS 定义成空注解）决定的，写裸 `slots:` 直接语法错。
 *
 * 第三，别急着炫技。本讲唯一那处 SIGNAL(temperatureChanged(double)) 是因为
 * QObject::receivers() 至今只有字符串签名版本（qobject.h 第 369 行），没得选；
 * 而且它站在 protected 段里（同文件第 366 行），外面根本调不动，只能由本类开一层
 * receiverCount() 的壳——这一条你在第 3 讲还会再撞一次。
 * 真正建立连接，从第 2 讲起一律用函数指针写法。
 */

// ——— 小练习 ———
/*
 *   1. 给 Thermometer 再加一个信号 `void rangeExceeded(double, double limit)`，
 *      把它连到 Alarm 的新槽；然后故意在 connect 里把参数写成 (double, int)，
 *      观察编译器报什么错（函数指针写法是**编译期**就拦下，这是它的价值）。
 *   2. 把 Alarm 的 `public Q_SLOTS:` 改成 `private Q_SLOTS:`，再用函数指针写法
 *      connect —— 看看编译能不能过（提示：connect 是 QMetaObject 里的静态函数，
 *      它不需要「访问」你的私有成员，它只拿成员指针）。然后再在 main 里直接调
 *      alarm.onTemperature(1.0)，这次编译器拦不拦？
 *   3. 把 Relay 那两级中继去掉，改成把 thermo.temperatureChanged 直接连两次到
 *      recorder.record，跑一遍对比 Recorder::count()；再用
 *      `thermo.blockSignals(true)` 夹住中间一次发射，确认 count 不动。
 *      最后把 `#include "01_signal_slot_basics.moc"` 注释掉再构建，把链接期报错
 *      原文抄下来——这行报错你这辈子还会再见到。
 */
// ============================================================================
