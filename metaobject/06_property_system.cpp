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
//        到底有什么、**没有什么**（结论：没有 bind() 这个成员函数，见第五节实测）
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
//   · 本机当前无法编译链接 Qt 程序，本示例**未在本机实测编译**；但**本文件已用本机
//     moc 实跑过一遍**（moc 输出为本机实测）：
//       C:/msys64/ucrt64/share/qt6/bin/moc.exe -I C:/msys64/ucrt64/include/qt6
//         06_property_system.cpp → 退出码 0、无 stderr、产物 337 行。
//       产物里 QtMocHelpers::PropertyData<…> 那几行把我这五条属性的关键字**全翻译成
//       了位标记**（照抄本机输出）：
//         PropertyData<int>(5, QMetaType::Int, DefaultPropertyFlags | Writable
//                           | Resettable | StdCppSet | User, 0)          ← level：RESET/USER 生效
//         PropertyData<int>(7, QMetaType::Int, DefaultPropertyFlags | Writable | StdCppSet, 1)
//                                                                         ← raw：只有 WRITE
//         PropertyData<double>(16, QMetaType::Double, DefaultPropertyFlags | Constant | Final)
//                                                                         ← CONSTANT/FINAL 生效，
//                                                                           连 notify 槽位都没有
//         PropertyData<enum Mode>(10, 0x80000000 | 17, DefaultPropertyFlags | Writable
//                           | EnumOrFlag | StdCppSet, 2)                 ← Q_ENUM 的枚举属性
//         PropertyData<QString>(12, QMetaType::QString, DefaultPropertyFlags | Writable, 3)
//                                                                         ← MEMBER label（无 READ/WRITE
//                                                                           函数，所以 StdCppSet 位没了）
//       尾部那个整数（0/1/2/3）就是 notify 信号在**方法表里的局部索引**——本讲第 3 节
//       用 notifySignalIndex() 读到的正是它。DESIGNABLE true 没多出一个位，因为
//       DefaultPropertyFlags 里本来就把 Designable 置上了（想关掉要写 DESIGNABLE false）。
//     所有 API 都对着本机真实头文件核对过：
//       QtCore/qtmetamacros.h   —— 60 行 `#define Q_PROPERTY(...) QT_ANNOTATE_CLASS(qt_property, __VA_ARGS__)`
//                                  （= C++ 编译器眼里这行是**空**的，只有 moc 读源码时看得见）；
//                                  56 行 Q_CLASSINFO、68 行 Q_ENUMS（旧）、70 行 Q_FLAGS（旧）、
//                                  76 行 Q_ENUM、77 行 Q_FLAG、84 行 Q_INVOKABLE
//       QtCore/qobject.h        —— 109~110 行 QObject **自己**就有一行 Q_PROPERTY：
//                                  `QString objectName READ objectName WRITE setObjectName
//                                   NOTIFY objectNameChanged BINDABLE bindableObjectName`
//                                  （BINDABLE 是真被 moc 接受的关键字，这里就是活例）；
//                                  132 行 `QBindable<QString> bindableObjectName();`、
//                                  344 行 `bool setProperty(const char*, const QVariant&)`、
//                                  345 行 `bool setProperty(const char*, QVariant&&)`（右值重载）、
//                                  346 行 `QVariant property(const char*) const`、
//                                  347 行 `QList<QByteArray> dynamicPropertyNames() const`、
//                                  353 行 `void objectNameChanged(const QString&, QPrivateSignal)`
//       QtCore/qobjectdefs.h    —— 567~576 行 `enum Call { InvokeMetaMethod, ReadProperty,
//                                  WriteProperty, ResetProperty, …, RegisterPropertyMetaType, …
//                                  BindableProperty }`（第 5 讲摘录里那几个 switch 的分派码）；
//                                  268 行 indexOfProperty、274 行 property(int)、276 行 userProperty()
//       QtCore/qmetaobject.h    —— QMetaProperty 341 行起：name 346 / typeName 347 /
//                                  metaType 358 / isReadable 362 / isWritable 363 /
//                                  isResettable 364 / isDesignable 365 / isStored 367 /
//                                  isUser 368 / isConstant 369 / isFinal 370 / isRequired 373 /
//                                  isBindable 375 / isEnumType 378 / hasNotifySignal 381 /
//                                  notifySignal 382 / notifySignalIndex 383 /
//                                  read(const QObject*) 387 / write(QObject*,const QVariant&) 388 /
//                                  write(QObject*,QVariant&&) 389 / reset(QObject*) 390 /
//                                  bindable(QObject*) 392 / hasStdCppSet 399 / isValid 401 行
//       QtCore/qproperty.h      —— 222 行 `Qt::makePropertyBinding(Functor&&, location)`；
//                                  201 行 QPropertyBinding<T>；360 行 QProperty<T>
//                                  （486/491/501/509 行四个 setBinding 重载、512 hasBinding、
//                                  514 binding）；750 行 QUntypedBindable（771 isValid、
//                                  772 isBindable、773 isReadOnly、842 setBinding、861 hasBinding）；
//                                  883 行 QBindable<T>：901 行 `explicit QBindable(QObject*, const QMetaProperty&)`、
//                                  904 行 `explicit QBindable(QObject*, const char*)`、907 makeBinding、
//                                  911 binding、916 takeBinding、922/938/946 三种 setBinding、
//                                  949 value、959 setValue；1096 行 QObjectBindableProperty；
//                                  1329 行 Q_OBJECT_BINDABLE_PROPERTY 宏
//       QtCore/qflags.h         —— 218 行 Q_DECLARE_FLAGS、238 行 Q_DECLARE_OPERATORS_FOR_FLAGS
//     【本讲最重要的一次「核对不上」】`grep -n 'bind(' QtCore/qproperty.h` **零命中**、
//     `grep -n 'bind' QtCore/qvariant.h` **零命中** → 「property("x").bind(...)」这种写法
//     在本机 6.12 上根本不存在（QObject::property() 返回 QVariant，QVariant 身上没有 bind）。
//     详见第五节，代码一律用头文件里真实存在的名字写。
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

private:
    static constexpr int kInitialLevel = 20;
    int m_level = kInitialLevel;
    int m_raw = 0;
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

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    Gauge gauge;
    Panel panel;
    // 「通知信号 → 界面」这一段连接就是属性系统的下游：属性变 → 信号发 → 槽刷新
    QObject::connect(&gauge, &Gauge::levelChanged, &panel, &Panel::onLevel);
    QObject::connect(&gauge, &Gauge::stateChanged, &panel, &Panel::onState);

    const QMetaObject *mo = gauge.metaObject();

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

    qInfo() << "=== 2. 改属性 → 通知信号 → 界面刷新：把证据链打出来 ===";
    const int pIdx = mo->indexOfProperty("level");               // 268 行
    const QMetaProperty pLevel = mo->property(pIdx);
    qInfo() << "  起点：level =" << gauge.level() << " Panel 刷新次数 =" << panel.repaints();
    // 路 A：普通 setter 调用（**推荐写法**，编译期有检查）
    gauge.setLevel(55);
    qInfo() << "  A 调 setter：level =" << gauge.level() << " 刷新次数 =" << panel.repaints();
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

    qInfo() << "=== 3. Qt 不替你发 NOTIFY：raw 属性就是铁证 ===";
    int rawHits = 0;
    // 第 4 讲的规矩：lambda 一律带 context object。这里 context 用 &gauge（和
    // rawHits 的寿命不同源，但 gauge 销毁时连接自动断，不会悬空回调）。
    QObject::connect(&gauge, &Gauge::rawChanged, &gauge, [&rawHits](int v) { ++rawHits; Q_UNUSED(v); });
    gauge.setProperty("raw", 7);
    qInfo() << "  setProperty(\"raw\",7) → raw =" << gauge.property("raw").toInt()
            << " rawChanged 触发次数 =" << rawHits
            << "（0！setRaw 里没有 Q_EMIT，属性照写、通知不发）";
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

    qInfo() << "=== 4. RESET / CONSTANT / FINAL / DESIGNABLE 的真实后果 ===";
    // RESET：写了 RESET 才有 isResettable()，调 QMetaProperty::reset()（390 行）
    gauge.setLevel(250);
    const bool okReset = pLevel.reset(&gauge);
    qInfo() << "  reset() 返回 =" << okReset << " isResettable =" << pLevel.isResettable()
            << " level 现在 =" << gauge.level() << "（回到 kInitialLevel=20）"
            << " Panel 刷新次数 =" << panel.repaints()
            << " ← 注意 resetLevel() 里没有 Q_EMIT，所以**复位是不通知的**（工程陷阱）";
    // CONSTANT + FINAL：写它必须失败，而且是**返回 false**，不是断言、不是崩
    const QMetaProperty pScale = mo->property(mo->indexOfProperty("fullScale"));
    const bool okWriteConst = pScale.write(&gauge, 999.0);
    qInfo() << "  给 CONSTANT FINAL 的 fullScale 写 999 → write() =" << okWriteConst
            << " isWritable =" << pScale.isWritable() << " isConstant =" << pScale.isConstant()
            << " isFinal =" << pScale.isFinal() << " 读回 =" << pScale.read(&gauge).toDouble()
            << "（CONSTANT 还告诉绑定系统「这个值不会变，别为它装监听」）";
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

    qInfo() << "=== 5. Qt6 的属性绑定：头文件里有什么、没有什么（实测核对）===";
    // 【先说没有什么】两个 grep 结果（本机 6.12）：
    //     grep -n 'bind('  QtCore/qproperty.h   → 零命中
    //     grep -n 'bind'   QtCore/qvariant.h    → 零命中
    // 所以网上/旧笔记里那句 `obj->property("x").bind(...)` 在这台机器上**编不过**：
    // QObject::property() 的返回类型是 QVariant（qobject.h 346 行），而 QVariant 没有 bind。
    // 名字对得上的真实入口是这三类（签名都抄自本机头文件）：
    //   ① `QBindable<T> b(QObject *obj, const char *property)`（qproperty.h 904 行）
    //      `QBindable<T> b(QObject *obj, const QMetaProperty &property)`（901 行）
    //      —— 前提是该 Q_PROPERTY 写了 **BINDABLE** 关键字（见下面 objectName 那条），
    //         否则 isValid()/isBindable() 是 false，setBinding 会被拒绝并 qWarning。
    //   ② `QProperty<T>` 作为**成员变量**直接持有值（360 行），用
    //      `p.setBinding(Qt::makePropertyBinding(f))`（486/501 行 + 222 行）挂一个求值函数。
    //   ③ 类内成员属性用 `Q_OBJECT_BINDABLE_PROPERTY(Class, Type, name[, &Class::notify])`
    //      （1329 行宏，展开成 1096 行的 QObjectBindableProperty<Class,Type,offset,Signal>）：
    //      它自带 NOTIFY 触发 + 依赖追踪，是 Qt6 推荐的「新式属性」。
    //      【本讲故意不用③】4 参形式要在宏那一行取 `&Class::signal` 的成员指针，
    //      就得把信号声明在宏**之前**（类此刻还不完整），写法别扭且容易翻车；
    //      而且我无法在本机验证它的产物，宁可不写。
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
    // ① 的用法（签名正确、语义清楚；**运行效果未实测**，见顶部须知）：
    QBindable<QString> nameBindable(&gauge, "objectName");            // 904 行那个构造函数
    qInfo() << "  QBindable<QString> 建出来了：isValid =" << nameBindable.isValid()
            << " isBindable =" << nameBindable.isBindable()
            << " isReadOnly =" << nameBindable.isReadOnly()            // 771~773 行
            << " 当前 value =" << nameBindable.value();
    nameBindable.setValue(QStringLiteral("主表针"));                    // 959 行：等价于写属性
    qInfo() << "  bindable.setValue(\"主表针\") 之后 objectName =" << gauge.objectName()
            << " property(\"objectName\") =" << gauge.property("objectName").toString()
            << "（同一条链：Bindable::setValue → WRITE 函数 → NOTIFY）";
    // ② 的用法：两个独立 QProperty<int>，第二个绑定成第一个的三倍
    QProperty<int> source(10);
    QProperty<int> derived;
    derived.setBinding(Qt::makePropertyBinding([&source]() -> int { return source * 3; }));
    qInfo() << "  绑定挂上后 hasBinding =" << derived.hasBinding()
            << " 读 derived =" << int(derived) << "（应为 source*3 = 30）";
    source = 40;
    qInfo() << "  把 source 改成 40 后再读 derived =" << int(derived)
            << "（**这一行的结果本讲未实测**：QProperty 的依赖失效要有观察器就位才生效；"
            << "放进 QObject + Q_OBJECT_BINDABLE_PROPERTY + NOTIFY 才是全自动的组合。"
            << "你到有编译环境的机器上跑一次，把 30/120 这两个数记下来——这个实验很值。）";
    // 解绑定的两个正规写法（都在本机头文件里）：
    //   QBindable<T>::takeBinding()（qproperty.h 916 行）—— 返回绑定对象并摘掉；
    //   QProperty<T>::setBinding(QPropertyBinding<T>{})（486 行）—— 传一个**空绑定**清掉。
    // 本讲故意不写「解绑后再读值」的结论，因为它依赖上面那条未实测的失效机制。

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
    qInfo() << "  已经写过 level=120，但 dynamicPropertyNames 里有 \"level\" 吗？"
            << (names.contains("level") ? "有" : "没有")
            << "（有 Q_PROPERTY 的名字走静态表，不会另存一份动态副本）";
    // 【头号事故·拼错静态属性名不会报错，只会「多出一个动态属性」】
    gauge.setProperty("levl", 999);                    // 少打一个字符
    qInfo() << "  拼错的 setProperty(\"levl\",999)：gauge.property(\"levl\") ="
            << gauge.property("levl").toInt() << " 而真属性 level =" << gauge.level()
            << " ← **两个都存在、互不相干**：静态那个纹丝不动，动态那个凭空多出来。"
            << " 这就是「按名字读写」把错误推到运行期的具体形状（第 1、5 讲说过的同一件事）。";
    qInfo() << "  反过来读一个既非静态也非动态的名字：property(\"不存在\") 是无效 QVariant？"
            << (gauge.property("不存在").isValid() ? "不是" : "是")
            << "（读不存在 = 无效 QVariant，返回默认值，不崩）";
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
    // 【为什么能成】Q_ENUM（qtmetamacros.h 76 行）让 moc 额外生成一张 enumerator 表：
    // 名字↔整数一一对上，QMetaEnum::fromType<Gauge::Mode>() 能读它（第 8 讲整讲在讲读表）。
    // 少了这一行宏，上面那句 write("Alarm") 就**只能喂整数**，配置文件写法立刻退化。

    // ── 自动脚本 ────────────────────────────────────────────────────────
    QTimer::singleShot(120, [&gauge, &panel] {
        qInfo() << "=== 8. 事件循环里再改一次：属性系统没有任何特殊之处 ===";
        const int beforeRepaints = panel.repaints();
        gauge.setProperty("level", 275);
        qInfo() << "  定时器里写属性：刷新增量 =" << (panel.repaints() - beforeRepaints)
                << " level =" << gauge.property("level").toInt();
        // MEMBER 属性：label 是公有成员，写它 = 直接给成员赋值（moc 生成的路径）
        gauge.setProperty("label", QStringLiteral("压力"));
        qInfo() << "  MEMBER 属性 label：property(\"label\") =" << gauge.property("label").toString()
                << " 直接读成员 gauge.label =" << gauge.label;
    });
    QTimer::singleShot(300, [] {
        qInfo() << "=== 第 6 讲演示结束，自动退出 ===";
        QCoreApplication::quit();
    });

    return app.exec();
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
 *   还有一条我更希望你现在就形成的习惯：**这套东西的求值时机我在这台机器上验证不了**，
 *   所以我在代码里明写「未实测」，把两个数留给你在能编译的机器上记下来。
 *   资料会写错，环境会变，「不确定就标出来」这个动作不会变。
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
 *      在你**能编译**的机器上验证：改依赖值之后读属性，是自动重算还是要手动失效？
 *      把结论写回本讲第 5 节那段「未实测」的注释里，并注明你用的 Qt 版本。
 */
// ============================================================================
