// ============================================================================
// 第 6 讲 · Q_PROPERTY 与属性系统：一张「可以被名字读写的成员」登记表
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   属性（property）是元对象系统里除信号槽之外的第二根柱子。把六件事一次讲透：
//     1) Q_PROPERTY 这行字**在 C++ 里根本不存在**（展开成空注解），它是给 moc 看的
//     2) 本机 Qt 6.12 的 moc 到底认哪些关键字（READ/WRITE/RESET/NOTIFY/MEMBER/
//        DESIGNABLE/SCRIPTABLE/STORED/USER/CONSTANT/FINAL/REQUIRED/BINDABLE），
//        哪些是旧书里有、这个 moc 会 Parse error 的（ACCESS/CATEGORY/INVALID/BINDING/
//        STYLE_INFORMATION）
//     3) 「改属性 → NOTIFY 信号触发 → 计数/界面变化」这条链的**真实机制**：
//        写属性 = 调 setter；NOTIFY **不会由 Qt 替你发**，只看 setter 里有没有那句 Q_EMIT
//     4) 静态属性（Q_PROPERTY 声明的）vs 动态属性（setProperty 现场塞的 QVariant）
//        —— 属性名拼错时两种完全不同的失败方式
//     5) Q_ENUM 让枚举属性能**按名字**读写
//     6) Qt6 的属性绑定（QProperty/QBindable/Qt::makePropertyBinding）在本机头文件里
//        到底有什么、**没有什么**，以及三种属性形状各自的下场：
//        访问器属性（READ+WRITE+NOTIFY）能绑、且**三条写路径只有一条解除绑定**；
//        只读属性（READ+NOTIFY）能绑但值永远落不下来，全程零警告；
//        没有 NOTIFY 的属性直接 isValid()=false，再走一步 value() 就是段错误。
//        （BINDABLE 关键字**不是**能不能绑的前提——这条本讲以前写反了，见 5 节。）
//
// 对应构建目标名：metaobject_06_property_system
//   add_executable(metaobject_06_property_system 06_property_system.cpp)
//   target_link_libraries(metaobject_06_property_system PRIVATE Qt6::Core)   ← 本讲不需要 GUI
//   set(CMAKE_AUTOMOC ON)
//
// 运行前须知：
//   · 用 QCoreApplication（属性系统属于 QtCore，和窗口无关），不显示界面；「界面变化」
//     用一个 Panel 影子对象的 repaint 计数代替——它就是「刷新了几次界面」的证据。
//   · 结论全部 qInfo()；QTimer::singleShot 排自动脚本；最后 QCoreApplication::quit()。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen）。**-Wall -Wextra 零警告，61 项自检台账全部符合，退出码 0。**
//     正文里每个「等于某个数」的断言都在 main 里有一条 expect() 兜着：
//     不符就打 [错] 并把退出码改成 1，ctest 直接红——不再依赖「跑完没崩」当证据。
//     本机那一轮存档 runlogs/metaobject_06_property_system.log。
// ============================================================================

#include <QCoreApplication>
#include <QByteArray>
#include <QMetaEnum>
#include <QMetaProperty>
#include <QMetaType>
#include <QProperty>          // Qt6：QProperty<T> / QBindable<T> / Qt::makePropertyBinding
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QDebug>

// ============================================================================
// 一、被登记的类：一行 Q_PROPERTY 对应元对象表里的一条属性条目
// ----------------------------------------------------------------------------
// 写 Q_PROPERTY 的语法骨架（顺序不固定，但关键字**大小写敏感**、必须空白分隔）：
//   Q_PROPERTY(类型 名字 READ 读函数 WRITE 写函数 NOTIFY 通知信号 RESET 复位函数
//              MEMBER 公有成员 DESIGNABLE true SCRIPTABLE true STORED true USER true
//              CONSTANT FINAL REQUIRED BINDABLE 可绑定访问器)
// 三条从本机头文件直接推得出的事实：
//   ① 它是**空宏**（qtmetamacros.h 60 行 → QT_ANNOTATE_CLASS(qt_property, …)）：
//      g++ 完全不知道有这条属性，所以「属性名拼错」编译器一个字都不会说。
//   ② READ/WRITE/NOTIFY 里写的名字必须是**本类可见的成员函数名**，moc 只登记名字，
//      由 qt_static_metacall 里的 switch 去调（第 5 讲摘录 461~475 行）。
//   ③ 只写 MEMBER 不写 READ/WRITE 也可以（moc 直接对公有成员读写，Qt6 会标 hasStdCppSet）。
// 【本机 moc 实测】第 5 讲已经用探针文件量过这套关键字；本讲这一整个文件也拿去跑了
//   moc.exe，凡是 ① 里 12 个关键字全接受，而 ACCESS/CATEGORY/INVALID/BINDING/
//   STYLE_INFORMATION 一律 Parse error（它们只出现在文档/旧代码里，这个 moc 不认）。
// ============================================================================
class Gauge : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("vendor", "老师傅仪器厂")            // 走 classInfo()，不参与属性系统

    // 【public 不是可有可无】Mode 要在**类外**被 Panel 用作槽参数、被 main 写成
    // Gauge::Green，就得放在 public 段里。Q_ENUM 只负责把枚举**登记进元对象表**
    // （属性系统按名字读写靠它），它**不改变 C++ 的访问控制**——这两件事分开理解。
public:
    // 【顺序有讲究】enum + Q_ENUM 放在所有 Q_PROPERTY **之前**：Q_PROPERTY 虽然只是给
    // moc 看的空宏（第 59 行注释那条），但 moc 生成的产物里要写 `PropertyData<Mode>`
    // 这种 C++ 类型名，而产物是 include 回**本类之后**的 TU 里的——写在类内靠后位置
    // 也一样能用，只是「类型先声明、后使用」在你把类拆到 .h 时更容易出错。
    enum Mode { Green, Amber, Alarm };
    Q_ENUM(Mode)                        // 让 Mode 进 enumerator 表（名字↔整数对照）

    // 主属性：读写齐全 + 通知 + 复位 + 设计器可见 + 当 userProperty
    Q_PROPERTY(int level READ level WRITE setLevel NOTIFY levelChanged RESET resetLevel
               DESIGNABLE true USER true)
    // 「setter 里不发信号」的属性——专门用来证明 Qt 不替你发 NOTIFY
    Q_PROPERTY(int raw READ raw WRITE setRaw NOTIFY rawChanged)
    // 只读 + 常量 + 终态：写它一定失败，而且失败方式是**返回 false**，不是崩
    Q_PROPERTY(double fullScale READ fullScale CONSTANT FINAL)
    // 【只读 + 有 NOTIFY，没有 WRITE】5 节那段「给只读属性挂绑定」的实测就冲它来的。
    // 真实代码里这种形状很多：峰值、进度百分比、只对外公布的派生值。
    Q_PROPERTY(int peak READ peak NOTIFY peakChanged)
    // 枚举属性：Q_ENUM 之后可以按名字写（"Green"/"Alarm"）
    Q_PROPERTY(Mode state READ state WRITE setState NOTIFY stateChanged)
    // 公有成员属性：MEMBER 直连，没有 READ/WRITE 函数
    Q_PROPERTY(QString label MEMBER label NOTIFY labelChanged)

public:
    explicit Gauge(QObject *parent = nullptr) : QObject(parent) {}

    int level() const { return m_level; }
    void setLevel(int v)
    {
        if (v == m_level) return;       // 经典写法：相等就不通知
        m_level = v;
        Q_EMIT levelChanged(m_level);   // ← 通知信号在这里发，**不是 Qt 发的**
    }
    void resetLevel() { m_level = kInitialLevel; }    // RESET 的落点（不通知！）

    int raw() const { return m_raw; }
    void setRaw(int v) { m_raw = v; }                 // 故意**不** Q_EMIT

    double fullScale() const { return 300.0; }

    int peak() const { return m_peak; }
    // 只读属性的「读」照样能变——只是变的路径在类内部，外部没有 WRITE 可走。
    void recordPeak(int v)
    {
        if (v == m_peak) return;
        m_peak = v;
        Q_EMIT peakChanged(m_peak);
    }

    Mode state() const { return m_mode; }
    void setState(Mode m)
    {
        if (m == m_mode) return;
        m_mode = m;
        Q_EMIT stateChanged(m_mode);
    }

    QString label;                       // 给 MEMBER label 用：公有成员，moc 直接读写它
    // MEMBER 生成的 setter 语义是「整体赋值」，不会自动发信号；所以这里靠
    // 显式赋值 + 手动 Q_EMIT（或者干脆把 labelChanged 交给外部触发）——
    // 这就是「MEMBER 很省事但通知要自己想」的那一半。

public Q_SLOTS:
    // 一个「被属性写入顺手调用」的槽，用来演示 QMetaObject::invokeMethod 之外的路径
    void noteWritten(int v) { m_lastWrittenViaMeta = v; ++m_metaWrites; }
    int metaWrites() const { return m_metaWrites; }

Q_SIGNALS:
    void levelChanged(int level);
    void rawChanged(int raw);
    void stateChanged(Gauge::Mode state);
    void labelChanged(QString label);
    void peakChanged(int peak);

private:
    static constexpr int kInitialLevel = 20;
    int m_level = kInitialLevel;
    int m_raw = 0;
    int m_peak = 0;
    int m_metaWrites = 0;
    int m_lastWrittenViaMeta = -1;
    Mode m_mode = Green;
};

// ── 界面影子：本讲用「刷新次数」当界面变化的证据（不需要 Widgets）────────
class Panel : public QObject
{
    Q_OBJECT
public Q_SLOTS:
    void onLevel(int level) { ++m_repaints; m_shown = level; }
    void onState(Gauge::Mode m) { ++m_repaints; m_state = m; }
    int repaints() const { return m_repaints; }
    int shown() const { return m_shown; }
private:
    int m_repaints = 0;
    int m_shown = -1;
    Gauge::Mode m_state = Gauge::Green;
};

// ── 新式属性（Qt6 的 QObjectBindableProperty）：本讲原来「故意不用」，现在补上 ──
// 原来那条理由站不住脚了：「4 参形式要在宏那一行取 &Class::signal 的成员指针，信号得声明在宏
// 之前，写法别扭；而且本机验证不了它的产物」。前半句是**真约束**（保留，见下面类里的注释），
// 后半句只是当时编不动的托辞——现在能编能跑，就把它跑给你看。
// 三个关键字一起用才成立（对照 Qt 文档的用法，全部签名都在本机 qproperty.h 里）：
//   Q_PROPERTY(int rpm READ rpm NOTIFY rpmChanged BINDABLE bindableRpm)
//     · READ 只要读函数；不写 WRITE —— 写只能通过 QBindable/QProperty 那条路
//     · NOTIFY 那个信号由**属性系统**发，不是你自己 Q_EMIT
//     · BINDABLE 指向一个返回 QBindable<int> 的函数（qproperty.h 904 行那个构造从
//       「成员 QProperty」拿指针）
//   Q_OBJECT_BINDABLE_PROPERTY(Class, Type, name, &Class::signal)（1329 行宏 → 1320 行
//     QT_OBJECT_BINDABLE_PROPERTY_4 → 1096 行 QObjectBindableProperty）
class SmoothGauge : public QObject
{
    Q_OBJECT
public:
    // 【真约束·顺序】信号必须声明在下面那行宏**之前**：宏的最后一个参数要在编译期取
    //   &SmoothGauge::rpmChanged 的成员指针，信号还没声明就取不到。这是「类还不完整」
    //   时代办的坑，撞上一次就记住了——把 signals: 段放到成员之前是这套写法的固定形状。
    Q_PROPERTY(int rpm READ rpm NOTIFY rpmChanged BINDABLE bindableRpm)
public:
    explicit SmoothGauge(QObject *parent = nullptr) : QObject(parent) {}

    int rpm() const { return m_rpm; }
    QBindable<int> bindableRpm() { return QBindable<int>(&m_rpm); }   // qproperty.h 904 行

    void setManual(int v) { m_rpm = v; }   // 走 QProperty 的写：仍然会发 NOTIFY，
                                           // 而且**会把这条属性上的绑定摘掉**（qproperty.h 1176~1185 行
                                           // QObjectBindableProperty::setValue 开头就是 removeBinding；
                                           // 本机实测见 6 节那串 PROBE）。
                                           // ⚠ 别把这条推广到普通 Q_PROPERTY 上：成员属性是「存储本体」，
                                           //   访问器属性走的是另一条实现，赋值**不**解除绑定（见 5 节路径 A/B/C）。

signals:
    // 注意形状：这个信号**不带参数**。属性系统按签名决定怎么发——想拿新值就连带参数声明，
    // 本讲用无参版（Qt 文档的用法示例也是无参版），少一个「信号参数必须等于属性类型」的坑。
    void rpmChanged();

private:
    Q_OBJECT_BINDABLE_PROPERTY(SmoothGauge, int, m_rpm, &SmoothGauge::rpmChanged)
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    Gauge gauge;
    Panel panel;
    // 「通知信号 → 界面」这一段连接就是属性系统的下游：属性变 → 信号发 → 槽刷新
    QObject::connect(&gauge, &Gauge::levelChanged, &panel, &Panel::onLevel);
    QObject::connect(&gauge, &Gauge::stateChanged, &panel, &Panel::onState);

    const QMetaObject *mo = gauge.metaObject();

    // ── 自检台账：正文里每一个「等于某个数」的说法，这里都留一条真判断 ──
    // 【为什么这一版才加】文件开头原来写着「本讲没有汇总自检行，退出码 0 只证明没崩」。
    // 5 节新加了二十来条断言（三条写路径谁解除绑定、只读属性、无 NOTIFY），
    // 全靠人肉对日志一定会出错——本讲自己就错过一次（拼错名字那段和绑定段的 999 撞车）。
    // 所以从现在起：**不符就 exit 1**，ctest 直接红。
    struct Ledger { int ok = 0; int bad = 0; };
    Ledger ledger;
    auto expect = [&ledger](const char *what, qlonglong got, qlonglong want) {
        const bool pass = (got == want);
        if (pass) ++ledger.ok; else ++ledger.bad;
        qInfo().noquote() << QStringLiteral("      · [%1] %2：得到 %3，期望 %4")
                                 .arg(pass ? QStringLiteral("对") : QStringLiteral("错"),
                                      QString::fromUtf8(what))
                                 .arg(got).arg(want);
    };

    qInfo() << "=== 1. 属性表长什么样：moc 真给你登记了条目 ===";
    qInfo() << "  propertyCount() =" << mo->propertyCount()
            << " 本类属性段起点 propertyOffset() =" << mo->propertyOffset();
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        const QMetaProperty p = mo->property(i);       // qobjectdefs.h 274 行
        qInfo().noquote() << QStringLiteral("    [%1] %2 : %3  读=%4 写=%5 常量=%6 终态=%7 可复位=%8 有NOTIFY=%9 是枚举=%10 可绑定=%11 是user=%12")
                                 .arg(i)
                                 .arg(QString::fromLatin1(p.name()),
                                      QString::fromLatin1(p.typeName()))
                                 .arg(p.isReadable() ? "√" : "×")
                                 .arg(p.isWritable() ? "√" : "×")
                                 .arg(p.isConstant() ? "√" : "×")
                                 .arg(p.isFinal() ? "√" : "×")
                                 .arg(p.isResettable() ? "√" : "×")
                                 .arg(p.hasNotifySignal() ? "√" : "×")
                                 .arg(p.isEnumType() ? "√" : "×")
                                 .arg(p.isBindable() ? "√" : "×")
                                 .arg(p.isUser() ? "√" : "×");
    }
    qInfo() << "  userProperty()（第一个 USER=true）="
            << QString::fromLatin1(mo->userProperty().name());   // 276 行
    expect("userProperty() 落到了第一个 USER=true 的 level 上",
           QString::fromLatin1(mo->userProperty().name()) == QStringLiteral("level") ? 1 : 0, 1);
    expect("本类登记的属性条数（propertyCount - propertyOffset）",
           mo->propertyCount() - mo->propertyOffset(), 6);

    qInfo() << "=== 2. 改属性 → 通知信号 → 界面刷新：把证据链打出来 ===";
    const int pIdx = mo->indexOfProperty("level");               // 268 行
    const QMetaProperty pLevel = mo->property(pIdx);
    qInfo() << "  起点：level =" << gauge.level() << " Panel 刷新次数 =" << panel.repaints();
    // 路 A：普通 setter 调用（**推荐写法**，编译期有检查）
    gauge.setLevel(55);
    qInfo() << "  A 调 setter：level =" << gauge.level() << " 刷新次数 =" << panel.repaints();
    expect("A 调 setter 后 Panel 刷新次数", panel.repaints(), 1);
    // 路 B：按名字写（QObject::setProperty，qobject.h 344 行）—— 内部还是走 setter
    const bool okB = gauge.setProperty("level", 88);
    qInfo() << "  B setProperty(\"level\",88) 返回 =" << okB
            << " level =" << gauge.property("level").toInt()
            << " 刷新次数 =" << panel.repaints()
            << " ← 这条链是「按名字 → 元对象表 → WriteProperty → switch 调 setLevel → Q_EMIT → activate」";
    // 路 C：直接拿 QMetaProperty 写（387/388 行；这条路 QML、动画、序列化都爱用）
    gauge.noteWritten(0);
    const bool okC = pLevel.write(&gauge, 120);
    qInfo() << "  C QMetaProperty::write(120) 返回 =" << okC << " 读回 =" << pLevel.read(&gauge).toInt()
            << " 刷新次数 =" << panel.repaints();
    // 【关键证据·相等值不通知】setLevel 里那句 `if (v == m_level) return;` 生效了：
    const int repaintsNow = panel.repaints();
    pLevel.write(&gauge, 120);                       // 同值再写一次
    qInfo() << "  同一个值再写一次：level 仍 =" << pLevel.read(&gauge).toInt()
            << " 刷新次数增量 =" << (panel.repaints() - repaintsNow)
            << "（0 = 通知被 setter 里的相等判断挡住了——Qt 不禁止你重复通知，也不替你判断）";
    expect("三条写路径（setProperty / QMetaProperty::write）都返回 true", (okB && okC) ? 1 : 0, 1);
    expect("同一个值再写一次 → 刷新增量为 0", panel.repaints() - repaintsNow, 0);

    qInfo() << "=== 3. Qt 不替你发 NOTIFY：raw 属性就是铁证 ===";
    int rawHits = 0;
    // 第 4 讲的规矩：lambda 一律带 context object。这里 context 用 &gauge（和
    // rawHits 的寿命不同源，但 gauge 销毁时连接自动断，不会悬空回调）。
    QObject::connect(&gauge, &Gauge::rawChanged, &gauge, [&rawHits](int v) { ++rawHits; Q_UNUSED(v); });
    gauge.setProperty("raw", 7);
    qInfo() << "  setProperty(\"raw\",7) → raw =" << gauge.property("raw").toInt()
            << " rawChanged 触发次数 =" << rawHits
            << "（0！setRaw 里没有 Q_EMIT，属性照写、通知不发）";
    expect("setProperty(\"raw\") 值写进去了", gauge.property("raw").toInt(), 7);
    expect("但 rawChanged 触发次数", rawHits, 0);
    // 对照第 5 讲的本机 moc 摘录：qt_static_metacall 的 WriteProperty 分支
    //     case 0: _t->setLevel(*reinterpret_cast<int*>(_v)); break;
    // 只有「调 setter」这一行，**没有任何「调完 setter 再 emit 一下」的代码**。
    // 所以 NOTIFY 的语义是「登记一个信号名字给外界（QML/绑定/属性监视器）去 connect」，
    // 发不发永远由 setter 的代码负责。你写「属性变了界面就该刷新」却不动，
    // 八成是 setter 忘了 Q_EMIT，或者 NOTIFY 名字写错了个大小写（moc 认得到就登记，
    // 认不到直接 Parse error）。
    // 【那 NOTIFY 到底被谁「自动连接」】看索引：
    const int notifyIdx = pLevel.notifySignalIndex();            // 383 行
    qInfo() << "  level 的 notifySignalIndex() =" << notifyIdx
            << " 名字 =" << QString::fromLatin1(pLevel.notifySignal().name())
            << " 而 indexOfSignal(\"levelChanged(int)\") =" << mo->indexOfSignal("levelChanged(int)")
            << " ← 两个数相等：属性表存的就是**方法表里那个信号的索引**"
            << "。QML 引擎、QPropertyAnimation、绑定系统拿这一个整数去 connect，"
            << "不需要知道你类里的任何东西——这就是「自动连接」的全部真相。";
    expect("属性表存的就是信号表里那个索引（两个整数相等）",
           (notifyIdx == mo->indexOfSignal(QByteArrayLiteral("levelChanged(int)"))) ? 1 : 0, 1);

    qInfo() << "=== 4. RESET / CONSTANT / FINAL / DESIGNABLE 的真实后果 ===";
    // RESET：写了 RESET 才有 isResettable()，调 QMetaProperty::reset()（390 行）
    gauge.setLevel(250);
    const int repaintsBeforeReset = panel.repaints();
    const bool okReset = pLevel.reset(&gauge);
    qInfo() << "  reset() 返回 =" << okReset << " isResettable =" << pLevel.isResettable()
            << " level 现在 =" << gauge.level() << "（回到 kInitialLevel=20）"
            << " Panel 刷新次数 =" << panel.repaints()
            << " ← 注意 resetLevel() 里没有 Q_EMIT，所以**复位是不通知的**（工程陷阱）";
    expect("reset() 把 level 拉回 kInitialLevel", gauge.level(), 20);
    expect("而 reset 不通知：reset 前后 Panel 刷新增量", panel.repaints() - repaintsBeforeReset, 0);
    // CONSTANT + FINAL：写它必须失败，而且是**返回 false**，不是断言、不是崩
    const QMetaProperty pScale = mo->property(mo->indexOfProperty("fullScale"));
    const bool okWriteConst = pScale.write(&gauge, 999.0);
    qInfo() << "  给 CONSTANT FINAL 的 fullScale 写 999 → write() =" << okWriteConst
            << " isWritable =" << pScale.isWritable() << " isConstant =" << pScale.isConstant()
            << " isFinal =" << pScale.isFinal() << " 读回 =" << pScale.read(&gauge).toDouble()
            << "（CONSTANT 还告诉绑定系统「这个值不会变，别为它装监听」）";
    expect("给 CONSTANT 属性写值：write() 返回 false（不是崩）", okWriteConst ? 1 : 0, 0);
    // DESIGNABLE / STORED / USER：只影响「设计器/序列化看不看你」，不影响读写行为
    qInfo() << "  level 的 isDesignable =" << pLevel.isDesignable()
            << " isUser =" << pLevel.isUser()
            << "（这两个位是 Q_PROPERTY 里 DESIGNABLE true / USER true 落到属性表里的标记）";
    // blockSignals：属性照样改，通知没人收（和第 1 讲呼应）
    const int repaintsBeforeBlock = panel.repaints();
    gauge.blockSignals(true);
    gauge.setLevel(260);
    gauge.blockSignals(false);
    qInfo() << "  blockSignals 期间 setLevel(260)：level =" << gauge.level()
            << " 但刷新增量 =" << (panel.repaints() - repaintsBeforeBlock)
            << "（改值成功、通知被哑——批量初始化时用这个避免 N 次刷新）";
    expect("blockSignals 期间：值改了", gauge.level(), 260);
    expect("blockSignals 期间：刷新增量 0", panel.repaints() - repaintsBeforeBlock, 0);

    qInfo() << "=== 5. Qt6 的属性绑定：头文件里有什么、没有什么（实测核对）===";
    // 【先说没有什么】两个 grep 结果（本机 6.12）：
    //     grep -n 'bind('  QtCore/qproperty.h   → 零命中
    //     grep -n 'bind'   QtCore/qvariant.h    → 零命中
    // 所以网上/旧笔记里那句 `obj->property("x").bind(...)` 在这台机器上**编不过**：
    // QObject::property() 的返回类型是 QVariant（qobject.h 346 行），而 QVariant 没有 bind。
    // 名字对得上的真实入口是这三类（签名都抄自本机头文件）：
    //   ① `QBindable<T> b(QObject *obj, const char *property)`（qproperty.h 904 行）
    //      `QBindable<T> b(QObject *obj, const QMetaProperty &property)`（901 行）
    //      【这一条的前置条件我写错过一次，这里按本机实测改口径】原来这一版写的是
    //      「前提是该 Q_PROPERTY 写了 BINDABLE 关键字，否则 isValid()/isBindable() 是
    //      false，setBinding 会被拒绝并 qWarning」——**整句是错的**，两个字段都错，
    //      连「BINDABLE 是前提」这个判断也错。本机拿 Gauge::level（READ+WRITE+NOTIFY，
    //      没写 BINDABLE）跑出来的是：
    //        QMetaProperty::isBindable() = false   ← 它只反映「有没有 BINDABLE 关键字」
    //        QBindable::isValid()        = true    ← 手柄本身是好的
    //        QBindable::isBindable()     = true    ← 而且真能绑
    //        setBinding(levelDep * 1000) → level = 3000（依赖一变就跟着变）
    //      ⇒ **BINDABLE 关键字不是能不能绑定的前提**，它只是「类里有一个 QProperty 成员
    //         可以对外借出来」的声明；有 READ+WRITE+NOTIFY 的普通属性走的是「访问器接口」
    //         那条实现（iface->setter 直接调 WRITE 函数），一样能绑。
    //      真正的硬条件是 **NOTIFY 信号必须存在**（不是「必须发」，是签名表里得有这一项）：
    //      本机 Gauge::fullScale 是 `READ ... CONSTANT`（没有 NOTIFY），构造手柄时立刻打
    //        qt.qproperty.binding: QUntypedBindable: Property fullScale has no notify signal
    //        isValid() = false 但 isBindable() = true
    //      ⚠ 更要命的是接下来这一步：手柄既然 invalid，就**别再往下调用任何成员**。
    //      本机把这五个成员各跑一遍（单独探针，每次只调一个）：
    //        b.value()      → Segmentation fault（MSYS exit=139；cmd/ctest 那侧 0xC0000005）
    //        b.setValue(x)  → 同样段错误
    //        b.hasBinding() → 同样段错误
    //        b.setBinding(…)→ 不是段错误，是断言中止：
    //            ASSERT: "!iface || binding.isNull() || binding.valueMetaType() == metaType()"
    //            in file .../QtCore/qproperty.h, line 924
    //        b.isReadOnly() → 唯一安全的（它只碰 iface，不碰 data）
    //      段错误那条的原因在 949 行：value() 只挡了 `if (iface)`，可 iface 是非空的
    //      （静态接口表，永远非空），空的是 `data`（属性实例指针）——于是它拿着非空 iface
    //      去调 `iface->getter(nullptr, &result)`，直接踩空。所以规矩是：
    //      **拿到 QBindable 先问 isValid()；false 就当它不存在。**
    //      本讲下面只打印它的 isValid/isBindable，**故意不调 value()**。
    //      还有一条只读陷阱：Gauge::peak 是 `READ + NOTIFY`（没有 WRITE），挂绑定**全程无警告**、
    //      hasBinding=true、依赖一写求值函数真的重算，但结果哪儿也不落——peak 一直是旧值、
    //      不发 NOTIFY、连 `b.value()` 也还是旧值（value 走 READ 访问器，不走绑定）。
    //      顺带把 `isReadOnly()` 这个名字钉死：它 773 行的定义是
    //      `!(iface && iface->setBinding && iface->setObserver)`——问的是「这条接口能不能
    //      挂绑定/挂观察者」，**不是「有没有 WRITE 函数」**。本机 peak 上它返回 false
    //      （看起来像「可写」），老实报告「没有 WRITE」的是 **QMetaProperty::isWritable()**。
    //      想知道能不能写，问元属性；想知道能不能绑，才问 QBindable。
    //   ② `QProperty<T>` 作为**成员变量**直接持有值（360 行），用
    //      `p.setBinding(Qt::makePropertyBinding(f))`（486/501 行 + 222 行）挂一个求值函数。
    //   ③ 类内成员属性用 `Q_OBJECT_BINDABLE_PROPERTY(Class, Type, name[, &Class::notify])`
    //      （1329 行宏，展开成 1096 行的 QObjectBindableProperty<Class,Type,offset,Signal>）：
    //      它自带 NOTIFY 触发 + 依赖追踪，是 Qt6 推荐的「新式属性」。
    //      【③ 本讲已经在用了】就是上面那个 SmoothGauge（168 行起）。这一版以前在这里写
    //      「本讲故意不用③……而且我无法在本机验证它的产物，宁可不写」——前半句说的
    //      「信号要声明在宏之前」是**真约束**（保留，见 SmoothGauge 类里那条），
    //      后半句纯粹是当时编不动的托辞。现在本机 `-Wall -Wextra` 零警告编过、跑过，
    //      就是下面 6 节那一串 PROBE 的原文（gear 是 QProperty<int> 依赖，rpm 是绑定属性）：
    //        挂绑定          → 求值 1 次、rpm 2000、NOTIFY 1     ← **挂上那一刻就求值**，不是惰性
    //        写依赖 gear=3   → 求值 2 次、rpm 3000、NOTIFY 2     ← 依赖一写就重算，不用有人读
    //        再写同样的 3    → 求值 2 次（没动）                 ← 值相同就不重算
    //        写 gear=4       → 求值 3 次、rpm 4000
    //        setManual(1234) → rpm 1234、NOTIFY 4，**绑定被摘掉**
    //        再写 gear=9     → rpm 仍 1234、求值仍 3             ← 已经不跟依赖走了
    //      ② 那一套结论（eager 求值 + 依赖写在依赖方身上立刻传播）在 ③ 上逐条复现，
    //      上面 ② 的注释里已经把四个时机列全了，这里不重复。
    //      还有三条只有真跑才知道：
    //        · **断绑定的写法**是 `p.setBinding(QPropertyBinding<int>())`（qproperty.h 1215 行），
    //          **没有**零参的 `setBinding()`。我第一版探针写 `setBinding()`，编译器直接
    //          「candidate expects 1 argument, 0 provided」把三个重载全列给你看。
    //          它**有返回值**，返回的是刚被换掉/断掉的那条旧绑定：断之前挂着 → isNull=false；
    //          本来就没有 → isNull=true。所以 `auto old = ...setBinding(...)` 是查「我刚才
    //          到底替换掉了什么」的手段。
    //        · **给带绑定的属性赋值 = 解除绑定**——本机那串 PROBE 里 `setManual(1234)`
    //          （内部就是 `m_rpm = v`）之后，值停在 1234、求值次数冻住、再改依赖 gear 也不动。
    //          这条在文档里不显眼，但它是「绑定怎么莫名其妙没了」的头号原因。
    //          【范围要收窄·本讲以前写宽了】上面这句只对**成员属性**（③，存储本体就是
    //          QProperty/QObjectBindableProperty）成立。普通访问器属性（①）上，
    //          「赋值」分三条路，本机实测只有一条会解除绑定：调类自己的 WRITE 不解除、
    //          QObject::setProperty 也不解除（它转手还是调 WRITE），
    //          只有 `QBindable::setValue()` 解除。细节和代码行号见上面 ① 那一段，
    //          以及下面 5 节里 [路径 A]/[路径 B]/[路径 C] 三串实跑输出。
    //        · 断完再读，拿到的是**最后一次求值的缓存值**（本机停在 9000），不会退回 0，
    //          也不会再发 NOTIFY。
    //        · 这个类身上**没有** `isBindable()` 成员（探针里 `.isBindable()` 报
    //          "has no member named isBindable"）。772 行那个 isBindable() 属于
    //          QBindable/QUntypedBindable 那一侧；成员自己身上问的是 `hasBinding()`（1243 行）。
    //          而「这个属性从元对象看可不可绑定」问的是 **QMetaProperty::isBindable()**，
    //          跟成员类没关系——本机实测：Q_PROPERTY 里写了 BINDABLE → 返回 true。
    // 【有 BINDABLE 活样本】QObject 自己就声明了 objectName 是可绑定的：
    //   qobject.h 109~110 行  Q_PROPERTY(QString objectName READ objectName WRITE setObjectName
    //                                    NOTIFY objectNameChanged BINDABLE bindableObjectName)
    //   qobject.h 132 行      QBindable<QString> bindableObjectName();
    const QMetaProperty pName = Gauge::staticMetaObject.property(
            Gauge::staticMetaObject.indexOfProperty("objectName"));   // 继承来的属性也在表里
    qInfo() << "  objectName：isBindable =" << pName.isBindable()
            << " notifySignal =" << QString::fromLatin1(pName.notifySignal().name())
            << "（objectNameChanged 带 QPrivateSignal，见 qobject.h 353 行 —— "
            << "这个信号只能 Qt/绑定系统发，你在类外 Q_EMIT 不了）";
    // ① 的用法（本机实跑：isValid/isBindable/isReadOnly/value/setValue 一路都有返回值）：
    QBindable<QString> nameBindable(&gauge, "objectName");            // 904 行那个构造函数
    qInfo() << "  QBindable<QString> 建出来了：isValid =" << nameBindable.isValid()
            << " isBindable =" << nameBindable.isBindable()
            << " isReadOnly =" << nameBindable.isReadOnly()            // 771~773 行
            << " 当前 value =" << nameBindable.value();
    nameBindable.setValue(QStringLiteral("主表针"));                    // 959 行：等价于写属性
    qInfo() << "  bindable.setValue(\"主表针\") 之后 objectName =" << gauge.objectName()
            << " property(\"objectName\") =" << gauge.property("objectName").toString()
            << "（同一条链：Bindable::setValue → WRITE 函数 → NOTIFY）";
    expect("bindable.setValue 走的确实是 WRITE（两条读法一致）",
           (gauge.objectName() == QStringLiteral("主表针")
            && gauge.property("objectName").toString() == QStringLiteral("主表针")) ? 1 : 0, 1);

    // ── ① 的完整形状实测：普通访问器属性（**没写 BINDABLE**）到底能不能绑 ──
    // 上面那一大段注释的每一个字，都出自下面这串打印。这一段最值钱的不是「能绑」，
    // 而是**三条写路径里只有一条会解除绑定**——它和 6 节 ③ 那串 PROBE 摆在一起看：
    //   ③ QObjectBindableProperty（SmoothGauge::rpm）：`m_rpm = v` → 绑定**没了**。
    //     代码在 qproperty.h 1176~1185 行 QObjectBindableProperty::setValue：
    //     开头两句就是 `auto *bd = qGetBindingStorage(owner())->bindingData(this); if (bd) bd->removeBinding();`
    //   ① 访问器属性（Gauge::level）：走哪条路要看**你调的是谁**（下面逐条跑）：
    //     · 你的 WRITE 函数（setLevel(999)）         → 绑定**还在**，下一次依赖一变就把 999 冲掉
    //     · QObject::setProperty("level", 999)       → 同上，绑定**还在**（它就是转手调 WRITE）
    //     · QBindable::setValue(999)                 → 绑定**没了**
    //     差别在 qproperty.cpp 2560~2563 行那个 accessor setter：
    //       `adaptor->bindingData().removeBinding();`  ← 先摘绑定
    //       `adaptor->metaProperty().write(...)`       ← 再调你的 WRITE
    //     也就是说「摘绑定」这一步写在 **QBindable 这一侧**，你的 WRITE 函数里、
    //     以及 setProperty 转手调它的那条路上都没有这一句。
    // ⇒ 实操结论：属性挂了绑定之后，想让手动赋值生效，**必须用 bindable.setValue()**；
    //   直接调 setter 或 setProperty 写进去的值都是「临时值」，依赖一动就被覆盖。
    qInfo() << "  —— ① 访问器属性（Gauge::level，READ+WRITE+NOTIFY，**没有** BINDABLE）——";
    const QMetaProperty pLevelBindable = Gauge::staticMetaObject.property(
            Gauge::staticMetaObject.indexOfProperty("level"));
    QBindable<int> levelBindable(&gauge, "level");
    expect("objectName 有 BINDABLE → QMetaProperty::isBindable", pName.isBindable() ? 1 : 0, 1);
    qInfo() << "    同一个 level，两个 isBindable 各说各话："
            << " QMetaProperty::isBindable =" << pLevelBindable.isBindable()
            << "（它只回答「Q_PROPERTY 里有没有 BINDABLE 关键字」）"
            << " / QBindable::isBindable =" << levelBindable.isBindable()
            << " isValid =" << levelBindable.isValid();
    expect("level 没写 BINDABLE → QMetaProperty::isBindable 是 false",
           pLevelBindable.isBindable() ? 1 : 0, 0);
    expect("但 QBindable(level) 照样 isValid && isBindable（能绑）",
           (levelBindable.isValid() && levelBindable.isBindable()) ? 1 : 0, 1);
    int levelEvals = 0;
    int levelNotifies = 0;
    QObject::connect(&gauge, &Gauge::levelChanged, [&levelNotifies](int) { ++levelNotifies; });
    QProperty<int> levelDep(3);                       // 这条是「依赖」，不是属性
    levelBindable.setBinding(Qt::makePropertyBinding(
            [&levelDep, &levelEvals]() -> int { ++levelEvals; return levelDep * 1000; }));
    qInfo() << "    挂绑定（还没读）：level =" << gauge.level()
            << " 求值 =" << levelEvals << " NOTIFY 增量 =" << levelNotifies
            << " hasBinding =" << levelBindable.hasBinding()
            << "（绑定落值走的就是你的 WRITE，所以里面那句 Q_EMIT 照发）";
    expect("挂绑定那一刻就求值 1 次（不是惰性）", levelEvals, 1);
    expect("并且立刻落值：level = 3 * 1000", gauge.level(), 3000);
    levelDep = 4;
    qInfo() << "    写依赖 levelDep=4：level =" << gauge.level()
            << " 求值 =" << levelEvals << " NOTIFY 增量 =" << levelNotifies;
    expect("写依赖立刻重算并落值：level = 4000", gauge.level(), 4000);
    gauge.setLevel(999);                              // ← 三条写路径，第 1 条
    qInfo() << "    [路径 A] 直接 setLevel(999)：level =" << gauge.level()
            << " hasBinding =" << levelBindable.hasBinding()
            << " ← 和 ③ 相反：**绑定还在**";
    expect("[A] 调自己的 WRITE：绑定还在（hasBinding=true）",
           levelBindable.hasBinding() ? 1 : 0, 1);
    const bool accessorWriteKeepsBinding = levelBindable.hasBinding();
    levelDep = 5;
    qInfo() << "    写依赖 levelDep=5：level =" << gauge.level() << " 求值 =" << levelEvals
            << " ← 999 被绑定**冲掉了**。你的 setLevel 里只有一句 m_level = v，"
            << "绑定系统不知道有人手动动过值";
    expect("[A] 的 999 被依赖变化冲掉：level 变回 5000", gauge.level(), 5000);
    gauge.setProperty("level", 999);                  // ← 第 2 条：元对象写法也一样
    qInfo() << "    [路径 B] setProperty(\"level\", 999)：level =" << gauge.level()
            << " hasBinding =" << levelBindable.hasBinding()
            << " ← 转手调 WRITE，绑定同样还在";
    expect("[B] setProperty 也不解除绑定", levelBindable.hasBinding() ? 1 : 0, 1);
    levelDep = 6;
    qInfo() << "    写依赖 levelDep=6：level =" << gauge.level() << " 求值 =" << levelEvals
            << " ← 又被冲掉一次（「通过元对象写就等于用户写」这个想当然也不成立）";
    expect("[B] 写进去的值同样被覆盖：level = 6000", gauge.level(), 6000);
    const int evalsBeforeC = levelEvals;
    levelBindable.setValue(999);                      // ← 第 3 条：唯一真的解除绑定的
    qInfo() << "    [路径 C] levelBindable.setValue(999)：level =" << gauge.level()
            << " hasBinding =" << levelBindable.hasBinding()
            << " 求值 =" << levelEvals << " ← **这条才解除绑定**（qproperty.cpp 2563 行）";
    expect("[C] bindable.setValue：hasBinding 变 false（真解除）",
           levelBindable.hasBinding() ? 1 : 0, 0);
    levelDep = 7;
    qInfo() << "    写依赖 levelDep=7：level =" << gauge.level() << " 求值 =" << levelEvals
            << " ← 冻在 999 了：① 里「赋值 = 解除绑定」只对 bindable.setValue 成立";
    expect("[C] 之后写依赖：值冻在 999", gauge.level(), 999);
    expect("[C] 之后写依赖：求值次数不再涨", levelEvals, evalsBeforeC);

    // ── 只读 + NOTIFY：绑定挂得上、算得动，就是落不了地 ──
    qInfo() << "  —— 只读属性（Gauge::peak，READ+NOTIFY，**没有** WRITE）——";
    const QMetaProperty pPeak = Gauge::staticMetaObject.property(
            Gauge::staticMetaObject.indexOfProperty("peak"));
    QBindable<int> peakBindable(&gauge, "peak");
    qInfo() << "    元属性说的是实话：isReadable =" << pPeak.isReadable()
            << " isWritable =" << pPeak.isWritable()
            << "；而 QBindable::isReadOnly =" << peakBindable.isReadOnly()
            << " ← 它问的是「能不能挂绑定」（773 行），不是「有没有 WRITE」，别看错";
    expect("只读属性：QMetaProperty::isWritable=false", pPeak.isWritable() ? 1 : 0, 0);
    expect("同一条属性：QBindable::isReadOnly=false（它不回答有没有 WRITE）",
           peakBindable.isReadOnly() ? 1 : 0, 0);
    int peakEvals = 0;
    int peakNotifies = 0;
    QProperty<int> peakDep(2);
    QObject::connect(&gauge, &Gauge::peakChanged, [&peakNotifies](int) { ++peakNotifies; });
    peakBindable.setBinding(Qt::makePropertyBinding(
            [&peakDep, &peakEvals]() -> int { ++peakEvals; return peakDep * 100; }));
    peakDep = 3;
    qInfo() << "    挂上绑定后写依赖 peakDep=3：peak =" << gauge.peak()
            << " 求值 =" << peakEvals << " hasBinding =" << peakBindable.hasBinding()
            << " peakChanged 次数 =" << peakNotifies
            << " value() =" << peakBindable.value()
            << " ← 求值函数**真的在跑**（计数从 1 涨到 2），结果没有任何地方落下来（没有 WRITE）；"
            << "value() 也不给绑定的结果，它走 READ 访问器";
    expect("只读属性挂绑定：求值函数跑了 2 次", peakEvals, 2);
    expect("但 peak 一个数都没变（还是 0）", gauge.peak(), 0);
    expect("而且一个 NOTIFY 都没发", peakNotifies, 0);
    gauge.recordPeak(88);
    qInfo() << "    类内部 recordPeak(88)：peak =" << gauge.peak()
            << " peakChanged 次数 =" << peakNotifies
            << " value() =" << peakBindable.value()
            << " ← 只读属性能变，但变的路线在类内部；绑定对它毫无影响力"
            << "（全程零警告、零报错，是最容易白写的一种）";
    expect("类内部改成 88 后 value() 也读到 88（走 READ，不走绑定）",
           (gauge.peak() == 88 && peakBindable.value() == 88) ? 1 : 0, 1);

    // ── 没有 NOTIFY：构造手柄那一刻就告诉你不行，而且**别往下走一步** ──
    qInfo() << "  —— 无 NOTIFY（Gauge::fullScale，READ+CONSTANT）——";
    // 上面注释里那条「value() 段错误」就是这么来的：iface 非空（静态接口表，永远非空）、
    // data 为空（属性实例没找到），而 949 行的 value() 只挡了 `if (iface)`，
    // 于是它调 `iface->getter(nullptr, &result)` 直接踩空。
    // 所以下面**只**打印两个标志位，不调 value()。本机单独探针里加上 value() 之后：
    //   MSYS bash 报 Segmentation fault / exit=139；cmd 与 ctest 那侧是 0xC0000005。
    const QBindable<double> fullBindable(&gauge, "fullScale");
    qInfo() << "    isValid =" << fullBindable.isValid()
            << " isBindable =" << fullBindable.isBindable()
            << " ← 手柄「看着能用」（isBindable=true）但 data 是空的；"
            << "上面那行 qt.qproperty.binding 警告就是它发的，判断只认 isValid()";
    // ⚠ 这条 expect **故意只问 isValid**：value()/setValue()/setBinding() 在
    //   isValid()==false 的手柄上会段错误（见上面注释），别在这里「顺手」加。
    expect("没有 NOTIFY 的属性：QBindable::isValid=false（不碰其它成员）",
           fullBindable.isValid() ? 1 : 0, 0);

    // ② 的用法：两个独立 QProperty<int>，第二个绑定成第一个的三倍。
    // 【本机实测，含一次推翻旧结论】这一版以前在这里写「求值时机未实测：依赖失效要有
    // 观察器就位才生效」——**那句是错的**，而且错得典型：只打印值（30→120）根本看不出
    // 「什么时候算的」。所以这里加了求值计数器，把时机本身变成可见的数字。本机那一轮：
    //   · setBinding 之后 evalCount 已经是 1 → **挂绑定的那一刻就求值一次**，绑定不是惰性的；
    //   · 源没变、连读两次 → 计数不涨 → 值是缓存的，不是每次读都重算；
    //   · `source = 40` 这一**写**就让计数 +1（读之前就已经重算完）→ 失效是**写在依赖方身上
    //     立刻传播**的，不需要有人观察 derived；
    //   · 一个从没被读过的绑定同样被立刻失效（依赖登记发生在第一次求值时，跟有没有人读过无关）；
    //   · 条件依赖会**换**：这一轮求值没读到的那个属性，下一轮它变了就不再触发重算
    //     （qproperty.cpp 346 行 clearDependencyObservers / 356 行 allocateDependencyObserver_slow
    //      就是「每轮求值重新登记依赖观察者」的两端，372 行 evaluateRecursive 是重算入口）。
    // ⇒ 要记的不是 30/120 这两个数，是这四条**时机**。
    int evalCount = 0;
    QProperty<int> source(10);
    QProperty<int> derived;
    derived.setBinding(Qt::makePropertyBinding([&source, &evalCount]() -> int {
        ++evalCount;
        return source * 3;
    }));
    qInfo() << "  setBinding 之后（还没人读过）：hasBinding =" << derived.hasBinding()
            << " 求值次数 =" << evalCount << " ← 挂上就求值，不是惰性";
    const int evalsAtAttach = evalCount;
    const int firstRead = derived;
    const int secondRead = derived;
    qInfo() << "  连读两次 derived =" << firstRead << "/" << secondRead
            << " 求值次数 =" << evalCount << " ← 值缓存着，读不会重算（应为 30/30、1）";
    const int evalsAfterReads = evalCount;
    source = 40;
    const int evalsAfterDepWrite = evalCount;
    qInfo() << "  执行 source = 40 **之后、读之前**：求值次数 =" << evalCount
            << " ← 这一行就是「什么时候算」的答案：写在依赖方身上立刻传播，无需观察器";
    qInfo() << "  再读 derived =" << int(derived) << " 求值次数 =" << evalCount
            << "（应为 120、2）";
    expect("② 挂绑定那一刻就求值 1 次（不是惰性）", evalsAtAttach, 1);
    expect("② 连读两次：值都是 30", (firstRead == 30 && secondRead == 30) ? 1 : 0, 1);
    expect("② 连读两次：求值次数没涨（值缓存）", evalsAfterReads, evalsAtAttach);
    expect("② 写 source=40 之后、读之前计数就已经 +1", evalsAfterDepWrite, evalsAfterReads + 1);
    expect("② 再读 derived = 120", int(derived), 120);
    // 条件依赖：本轮没读的那个属性，下一轮就不再是它的依赖
    QProperty<int> flag(1);
    bool readFlag = true;
    int condEvals = 0;
    QProperty<int> cond;
    cond.setBinding(Qt::makePropertyBinding([&flag, &readFlag, &condEvals]() -> int {
        ++condEvals;
        return readFlag ? flag * 2 : 7;
    }));
    qInfo() << "  条件绑定第一轮（读 flag）：" << int(cond) << " 求值次数 =" << condEvals;
    readFlag = false;
    flag = 50;
    qInfo() << "  改成不读 flag 后写 flag=50 → cond =" << int(cond) << " 求值次数 =" << condEvals
            << " ← 这次还是重算了（上一轮登记过 flag），结果走到常量分支";
    qInfo() << "  再写 flag=60 → cond =" << int(cond) << " 求值次数 =" << condEvals
            << " ← **不再重算**：上一轮没读 flag，依赖已经换掉了。这就是「条件依赖」的代价，"
               "也是绑定系统只登记「本轮真正读过的那些」的原因（qproperty.cpp 372 行 evaluateRecursive）";
    expect("② 条件依赖：上一轮读过的 flag，本轮写它仍会重算", condEvals, 2);
    const int condEvalsAfterDrop = condEvals;
    flag = 60;
    expect("② 上一轮没读 flag → 再写 flag 不再重算（依赖换掉了）", condEvals, condEvalsAfterDrop);
    // 解绑定的两个正规写法（都在本机头文件里）：
    //   QProperty<T>::takeBinding()（qproperty.h 519 行）/ QBindable<T>::takeBinding()（916 行）
    //     —— 返回绑定对象并摘掉；
    //   QProperty<T>::setBinding(QPropertyBinding<T>{})（486 行）—— 传一个**空绑定**清掉。
    derived.takeBinding();   // 返回的 QPropertyBinding<T> 这里当场丢弃（析构即释放，值留在属性上）
    qInfo() << "  takeBinding() 摘掉之后 hasBinding =" << derived.hasBinding()
            << " 值仍是最后一次求值的结果 =" << int(derived)
            << "（摘绑定**不会**把值改回绑定之前的初值，也不会再随 source 变）";
    source = 999;
    qInfo() << "  摘完再写 source=999 → derived =" << int(derived)
            << " ← 没绑定了，值冻住；想恢复自动跟随就重新 setBinding";
    expect("② takeBinding 后值仍是最后一次的 120", int(derived), 120);
    expect("② 摘完再写 source=999：值不动", (derived.hasBinding() || int(derived) != 120) ? 0 : 1, 1);

    // ③ 类内新式属性 + 自动 NOTIFY（先把数字打出来，结论写在下面）
    SmoothGauge smooth;
    int notifies = 0;
    QObject::connect(&smooth, &SmoothGauge::rpmChanged, [&notifies] { ++notifies; });
    QProperty<int> gear(2);
    int smoothEvals = 0;
    // 同名不同返回值的坑（本机头文件对照）：
    //   QProperty<T>::setBinding(const QPropertyBinding<T>&) → **返回绑定对象**（qproperty.h 486 行）
    //   QBindable<T>::setBinding(const QPropertyBinding<T>&) → 也返回绑定对象（922 行；
    //     返回的是**刚被换掉的那条旧绑定**，所以 isNull()=true 表示「挂之前本来没有绑定」，
    //     它**不是**成功/失败标志。这一条我以前写成「接口不对/属性只读时它 qWarning 并返回
    //     空绑定，判据是 isNull()」——**错的**：只读+NOTIFY 的属性上它正常返回旧绑定、
    //     不 qWarning（上面 ① 那段只读实测就是证据），而「接口不对」的那一类（没有 NOTIFY）
    //     警告是在**构造手柄**时发的，跟 setBinding 这一次调用无关。别拿 isNull 当返回值真假用。）
    //   QUntypedBindable::setBinding(const QUntypedPropertyBinding&) → **返回 bool**（842 行）
    // 用 bool 接上面那个调用会直接编不过（'QPropertyBinding<int>' 转不成 'bool'），
    // 所以这里用 auto 接，再看 isNull()。三种签名同名不同返回，写代码时别凭记忆。
    const auto bound = smooth.bindableRpm().setBinding(
            Qt::makePropertyBinding([&gear, &smoothEvals]() -> int {
                ++smoothEvals;
                return gear * 1000;
            }));
    qInfo() << "  PROBE bindableRpm().setBinding → 返回的绑定 isNull =" << bound.isNull()
            << "求值次数 =" << smoothEvals << "rpm =" << smooth.rpm()
            << "NOTIFY 次数 =" << notifies;
    gear = 3;
    qInfo() << "  PROBE 写依赖 gear=3 之后：求值次数 =" << smoothEvals
            << "rpm =" << smooth.rpm() << "NOTIFY 次数 =" << notifies;
    gear = 3;
    qInfo() << "  PROBE 再写一次同样的 gear=3：求值次数 =" << smoothEvals
            << "rpm =" << smooth.rpm() << "NOTIFY 次数 =" << notifies;
    gear = 4;
    qInfo() << "  PROBE 写 gear=4：求值次数 =" << smoothEvals
            << "rpm =" << smooth.rpm() << "NOTIFY 次数 =" << notifies;
    smooth.setManual(1234);
    qInfo() << "  PROBE 直接 setManual(1234)：rpm =" << smooth.rpm()
            << "NOTIFY 次数 =" << notifies
            << "← 这一写**就把绑定摘了**（成员属性的规矩：给带绑定的 QProperty 赋值 = 解除绑定；"
            << "普通访问器属性不遵守这条，见上面 ① 的路径 A/B/C）";
    expect("③ 成员属性上 setManual 之后 hasBinding=false",
           smooth.bindableRpm().hasBinding() ? 1 : 0, 0);
    expect("③ 而 ① 里同样的「调自己的 WRITE」当时绑定仍在（accessorWriteKeepsBinding）",
           accessorWriteKeepsBinding ? 1 : 0, 1);
    gear = 9;
    qInfo() << "  PROBE 之后再写 gear=9：rpm =" << smooth.rpm()
            << "求值次数 =" << smoothEvals << "NOTIFY 次数 =" << notifies
            << "← 求值次数冻在 3：依赖照旧有人改，但这个属性已经不跟它走了";
    // 想再用绑定就**重新挂一条**（旧的那条已经被上面那次赋值拿掉了）。
    const auto again = smooth.bindableRpm().setBinding(
            Qt::makePropertyBinding([&gear, &smoothEvals]() -> int {
                ++smoothEvals;
                return gear * 1000;
            }));
    qInfo() << "  PROBE 重新挂绑定：again.isNull（= 挂上去之前那条）=" << again.isNull()
            << "rpm =" << smooth.rpm() << "求值次数 =" << smoothEvals
            << "NOTIFY 次数 =" << notifies
            << "← 挂上的那一刻值从 1234 跳到 9000，NOTIFY 就是在这里 +1 的（不是下面那两次写）";
    // 断绑定的**正确写法**：传一个空的 QPropertyBinding，**没有**零参的 setBinding()。
    // 断完之后值不会退回 0，也不会再跟着依赖变——它停在最后一次求值的那个数上。
    const int notifiesBeforeDrop = notifies;
    const auto dropped = smooth.bindableRpm().setBinding(QPropertyBinding<int>());
    qInfo() << "  PROBE 断绑定 setBinding(QPropertyBinding<int>()) → dropped.isNull（= 断掉的那条）="
            << dropped.isNull() << "hasBinding =" << smooth.bindableRpm().hasBinding()
            << "rpm =" << smooth.rpm() << "求值次数 =" << smoothEvals;
    expect("③ 断绑定返回的是「刚被断掉的那条」→ isNull=false", dropped.isNull() ? 1 : 0, 0);
    expect("③ 断完 hasBinding=false", smooth.bindableRpm().hasBinding() ? 1 : 0, 0);
    expect("③ 断完值停在最后一次求值的 9000（不退 0）", smooth.rpm(), 9000);
    gear = 20;
    qInfo() << "  PROBE 断完之后写 gear=20：rpm =" << smooth.rpm()
            << "求值次数 =" << smoothEvals
            << "断前后 NOTIFY 增量 =" << notifies - notifiesBeforeDrop
            << "（三个都不该动：QProperty gear 还在，但它已经不是这个属性的依赖了）";
    expect("③ 断完再写依赖：rpm 仍是 9000", smooth.rpm(), 9000);
    expect("③ 断完再写依赖：NOTIFY 增量为 0", notifies - notifiesBeforeDrop, 0);

    qInfo() << "=== 6. 静态属性 vs 动态属性：拼错名字时两种完全不同的失败 ===";
    // 动态属性：任何 QObject 都能现场塞键值对，**不需要 Q_PROPERTY、不需要 moc**
    const bool okDyn = gauge.setProperty("serialNo", QStringLiteral("SN-2026"));
    gauge.setProperty("factoryYear", 2026);
    qInfo() << "  setProperty(\"serialNo\") 返回 =" << okDyn
            << " 读回 =" << gauge.property("serialNo").toString()
            << " dynamicPropertyNames() =" << gauge.dynamicPropertyNames();
    // 【注意 dynamicPropertyNames 的类型】它是 `QList<QByteArray>`（347 行），不是
    // QStringList——打印/QML 传值时常要 QString::fromLatin1 转一手。
    QStringList dynNames;
    const QList<QByteArray> names = gauge.dynamicPropertyNames();
    for (const QByteArray &n : names) dynNames << QString::fromLatin1(n);
    qInfo() << "  动态属性名（转成 QString）=" << dynNames.join(QStringLiteral(", "));
    // 【动态属性变化也有事件】本机 qcoreevent.h 401~407 行：
    //   class QDynamicPropertyChangeEvent : public QEvent {
    //       explicit QDynamicPropertyChangeEvent(const QByteArray &name);
    //       QByteArray propertyName() const; };
    // setProperty 改**动态**属性时会向该对象派发这个事件——这是「监视一个没有
    // Q_PROPERTY 的名字」的唯一正规途径（静态属性的变化靠 NOTIFY 信号，两套机制）。
    // 静态属性**不会**出现在 dynamicPropertyNames 里：
    qInfo() << "  已经写过 level=" << gauge.level()
            << "，但 dynamicPropertyNames 里有 \"level\" 吗？"
            << (names.contains("level") ? "有" : "没有")
            << "（有 Q_PROPERTY 的名字走静态表，不会另存一份动态副本）";
    // 【头号事故·拼错静态属性名不会报错，只会「多出一个动态属性」】
    // 这里故意用一个和当前 level **不一样**的值：两个数相同的话这条对比就白做了
    // （读者分不清到底哪个属性被改了），这也是本讲自己踩过的坑——5 节那段绑定
    // 实测最后把 level 留在 999，正好和这个「拼错名字」的值撞车。
    gauge.setProperty("levl", 4242);                   // 少打一个字符
    qInfo() << "  拼错的 setProperty(\"levl\",4242)：gauge.property(\"levl\") ="
            << gauge.property("levl").toInt() << " 而真属性 level =" << gauge.level()
            << " ← **两个都存在、互不相干**：静态那个纹丝不动，动态那个凭空多出来。"
            << " 这就是「按名字读写」把错误推到运行期的具体形状（第 1、5 讲说过的同一件事）。";
    expect("拼错的名字真的变成了一个动态属性（读回 4242）", gauge.property("levl").toInt(), 4242);
    expect("而静态 level 完全没被这个错名影响（仍是 999）", gauge.level(), 999);
    expect("dynamicPropertyNames 里现在能查到 \"levl\"",
           gauge.dynamicPropertyNames().contains(QByteArrayLiteral("levl")) ? 1 : 0, 1);
    qInfo() << "  反过来读一个既非静态也非动态的名字：property(\"不存在\") 是无效 QVariant？"
            << (gauge.property("不存在").isValid() ? "不是" : "是")
            << "（读不存在 = 无效 QVariant，返回默认值，不崩）";
    expect("读不存在的名字返回**无效** QVariant", gauge.property("不存在").isValid() ? 1 : 0, 0);
    // 类型不匹配：属性表里有类型，setProperty 会先做 QVariant 转换
    const QVariant asText(QStringLiteral("not-a-number"));
    const bool okType = gauge.setProperty("level", asText);
    qInfo() << "  给 int 属性 level 写 QString(\"not-a-number\") → setProperty 返回 =" << okType
            << " level 现在 =" << gauge.level()
            << "（QVariant 转换失败时的具体行为依 Qt 版本略有差别，本讲只把 API 用对、"
            << "结论留给你实测；能确定的是「编译期没人拦你」）";

    qInfo() << "=== 7. 枚举属性按名字读写（Q_ENUM 给属性系统开的门）===";
    int stateHits = 0;
    QObject::connect(&gauge, &Gauge::stateChanged, &gauge, [&stateHits](Gauge::Mode m) {
        ++stateHits; Q_UNUSED(m);
    });
    const QMetaProperty pState = mo->property(mo->indexOfProperty("state"));
    qInfo() << "  state 属性 isEnumType =" << pState.isEnumType()
            << " typeName =" << QString::fromLatin1(pState.typeName());
    const bool okEnum = pState.write(&gauge, QStringLiteral("Alarm"));   // 喂字符串！
    qInfo() << "  write(\"Alarm\") 返回 =" << okEnum << " 读回 QVariant =" << pState.read(&gauge)
            << " 用 C++ 枚举比对 ==" << (gauge.state() == Gauge::Alarm ? "Alarm" : "不是")
            << " stateChanged 触发次数 =" << stateHits;
    const bool okBadEnum = pState.write(&gauge, QStringLiteral("不存在值"));
    qInfo() << "  write(\"不存在值\") 返回 =" << okBadEnum << "（按名字查表失败 → 不写）";
    expect("枚举属性按名字写：write(\"Alarm\") 返回 true", okEnum ? 1 : 0, 1);
    expect("并且真的落到了 C++ 枚举上", gauge.state() == Gauge::Alarm ? 1 : 0, 1);
    expect("stateChanged 触发 1 次", stateHits, 1);
    expect("按名字查不到 → write 返回 false", okBadEnum ? 1 : 0, 0);
    // 【为什么能成】Q_ENUM（qtmetamacros.h 76 行）让 moc 额外生成一张 enumerator 表：
    // 名字↔整数一一对上，QMetaEnum::fromType<Gauge::Mode>() 能读它（第 8 讲整讲在讲读表）。
    // 少了这一行宏，上面那句 write("Alarm") 就**只能喂整数**，配置文件写法立刻退化。

    // ── 自动脚本 ────────────────────────────────────────────────────────
    QTimer::singleShot(120, [&gauge, &panel, &expect] {
        qInfo() << "=== 8. 事件循环里再改一次：属性系统没有任何特殊之处 ===";
        const int beforeRepaints = panel.repaints();
        gauge.setProperty("level", 275);
        qInfo() << "  定时器里写属性：刷新增量 =" << (panel.repaints() - beforeRepaints)
                << " level =" << gauge.property("level").toInt();
        // MEMBER 属性：label 是公有成员，写它 = 直接给成员赋值（moc 生成的路径）
        gauge.setProperty("label", QStringLiteral("压力"));
        qInfo() << "  MEMBER 属性 label：property(\"label\") =" << gauge.property("label").toString()
                << " 直接读成员 gauge.label =" << gauge.label;
        expect("事件循环里写属性：Panel 刷新增量 1", panel.repaints() - beforeRepaints, 1);
        expect("setProperty(\"level\",275) 落到 level()", gauge.level(), 275);
        expect("MEMBER 属性：元对象写法直接改到了公有成员",
               gauge.label == QStringLiteral("压力") ? 1 : 0, 1);
    });
    QTimer::singleShot(300, [&ledger] {
        qInfo() << "=== 第 6 讲演示结束，自动退出 ===";
        qInfo() << "自检台账：符合" << ledger.ok << "项，不符" << ledger.bad
                << "项（不符则退出码 1）";
        if (ledger.bad > 0)
            qWarning() << "本讲正文里有数字对不上，别把这次的输出当结论用。";
        QCoreApplication::quit();
    });

    const int rc = app.exec();
    return rc != 0 ? rc : (ledger.bad > 0 ? 1 : 0);
}

// Q_OBJECT 类写在 .cpp 里 → 文件末尾必须包含 AUTOMOC 生成的同名产物，
// 名字 = 本 TU 文件名 + ".moc"（第 1、5 讲的规矩，一处都不能省）。
#include "06_property_system.moc"

// ——— 老师傅的话 ———
/*
 * 徒弟，属性系统是最容易被「用成黑魔法」的一块。我给你把它拆成三句可执行的话：
 *
 *   一、**写属性 = 调 setter，仅此而已。**
 *   setProperty()/QMetaProperty::write() 干的都是「按名字找到 WriteProperty 那条 case，
 *   把 QVariant 转成属性声明的类型，然后调用你的 WRITE 函数」。所以：
 *     · setter 里没 Q_EMIT，就没有通知（本讲 raw 属性那条实验，0 次，铁证）；
 *     · setter 里有 `if (v == m) return;`，重复写就不通知（这是**对**的写法，别嫌它罗嗦）；
 *     · RESET 函数里我故意不通知，很多框架的「恢复默认值」界面就不刷新了——本讲那段
 *       输出是给你的提醒，不是 bug 演示。
 *   通知信号的本质是「一个登记在属性表里的方法索引」（notifySignalIndex 那个整数），
 *   QML、绑定系统、QPropertyAnimation 拿它自己去 connect，所以你**必须**发，Qt **不会**替你发。
 *
 *   二、**能直接调 setter 就别按名字写。**
 *   按名字这条路要 strcmp 查表、要 QVariant 装箱、要把类型转换的成败推到运行期。
 *   本讲第 6 节那个「levle/levl 少一个字母」的实验是我最想让你记住的一屏输出：
 *   它不报错、不返回 false（对动态属性而言它是**成功**的），只是安静地多造一个没人读的
 *   键值对，然后你调了三小时界面纹丝不动。什么时候该按名字？配置文件、QML、插件宿主、
 *   序列化框架、单元测试里的通用遍历——名字来自你编译单元之外的时候。
 *
 *   三、**Qt6 的绑定是新东西，但别抄「property("x").bind(...)」这种句子。**
 *   本讲我在本机头文件里逐条 grep 过：QProperty<T>、QBindable<T>、QPropertyBinding<T>、
 *   Qt::makePropertyBinding、QObjectBindableProperty、Q_OBJECT_BINDABLE_PROPERTY 全都在，
 *   唯独**没有 bind() 这个成员**，QVariant 身上也没有 bind——写出来是编译错误，不是运行时问题。
 *   正确的入口是 `QBindable<T>(对象, "属性名")`（且属性得声明 BINDABLE）、
 *   类内 `Q_OBJECT_BINDABLE_PROPERTY`，或对成员 `QProperty<T>` 直接 `setBinding(...)`。
 *   还有一条我更希望你现在就形成的习惯：**这一讲的求值时机过去写的是「本机验证不了」，
 *   现在是有计数器的那四条**（见第 5 节 ② 那段：挂绑定即求值 / 读不重算 / 写在依赖方身上
 *   立刻传播 / 条件依赖会换）。旧说法「要有观察器就位才失效」是被计数器推翻的，不是被
 *   文档推翻的——所以你要学的不是那四条结论，是**「把『什么时候算的』变成能打印的数」这个动作**。
 *   资料会写错，环境会变，「不确定就标出来」这个动作不会变；而能验证的时候，
 *   「标出来」就必须升级成「跑出来」。
 *
 * 最后一个观念：Q_PROPERTY 是**空宏**（qtmetamacros.h 60 行），g++ 压根不知道它存在。
 * 所以「属性名拼错」「类型写宽了」「忘写 Q_ENUM 结果枚举只能存整数」这类事，
 * 编译器一个都不会拦你——拦你的只有 moc 语法关和运行期那张表。写属性时先问自己：
 * 「这行如果名字错了，我能在 5 秒内发现吗？」发现不了，就别用按名字写。
 */

// ——— 小练习 ———
/*
 *   1. 给 Gauge 加一条**只有 WRITE、没有 READ** 的属性（合法写法：
 *      `Q_PROPERTY(int warnLevel WRITE setWarnLevel NOTIFY warnLevelChanged)`），
 *      用本讲第 1 节的循环打印它的 isReadable()/isWritable()，再用 QMetaProperty::read()
 *      读它：观察返回的 QVariant 是否无效，并解释「为什么 moc 不拦你只写不读」。
 *   2. 把 setLevel 里那句 `if (v == m_level) return;` 删掉，重跑第 2 节，记下
 *      Panel 刷新次数从 0 增到几；再给 Panel 加一个槽连到 levelChanged 两次（重复
 *      connect），把「一次改属性 → 刷新 N 次」的乘起来看。用第 1 讲的 activate 结论
 *      解释这两个数字。
 *   3. 用 Q_OBJECT_BINDABLE_PROPERTY(Gauge, int, m_smooth, &Gauge::smoothChanged) 替换
 *      手写的 int m_level（注意：信号必须声明在宏**之前**，否则取不到成员指针），
 *      然后给 m_smooth 挂一个 setBinding(Qt::makePropertyBinding(...))，
 *      再在**能编译**的机器上验证第 5 节那四条时机在你这版 Qt 上成不成立：
 *      改依赖值之后读属性，本机那一轮的答案是「自动重算，而且是写在依赖方那一刻就传播」，
 *      不需要观察者就位、不需要手动失效。把你那一轮的**求值次数**（不是值）记下来对比——
 *      值一样不代表次数一样。
 */
// ============================================================================
