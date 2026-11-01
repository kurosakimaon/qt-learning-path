// ============================================================================
// 第 8 讲 · 元反射：把一张元对象表读成一份「这个类会什么」的说明书
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   第 5 讲你看见 moc 生成了那张表；这一讲教你**怎么读它、怎么用它的写回入口**。
//     1) QMetaObject 的 className()/methodCount()/propertyCount()/method(i)/property(i)/
//        enumerator(i) 各返回什么，索引从哪儿开始（offset 的坑）
//     2) QMetaMethod：name()/methodType()/parameterTypes()/parameterNames()/invoke()
//     3) QMetaProperty：read()/write()/reset()/notifySignal()/isWritable()/isConstant()
//     4) QMetaObject::invokeMethod 四个字符串重载怎么选（Q_ARG 的类型为什么必须精确）
//     5) qobject_cast 为什么比 dynamic_cast 更适合 QObject（三条真实理由）
//     6) Q_ENUM / Q_FLAG 让枚举能**按名字**读写
//   最后把一个小类拆成三张清单打印出来，再全程用反射写属性、调方法——
//   让你体验一次「插件宿主看世界」的方式。
//
// 对应构建目标名：metaobject_08_introspection
//   add_executable(metaobject_08_introspection 08_introspection.cpp)
//   target_link_libraries(metaobject_08_introspection PRIVATE Qt6::Core)
//   set(CMAKE_AUTOMOC ON)
//
// 运行前须知：
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 metaobject_08_introspection，跑到自动退出、退出码 0）。
//     但本讲**没有汇总自检行**，退出码 0 只证明「没崩、走完了」，不证明正文每个数字都对：
//     带具体数字的「预期」请以你自己那次运行输出为准（本机那一轮存档 runlogs/metaobject_08_introspection.log）。
//   · 用 QCoreApplication，无窗口；结论全部 qInfo()；QTimer::singleShot 排脚本 +
//     QCoreApplication::quit() 收尾——无人值守跑完。
//   · 反射这条路上「查不到」的返回值一律是 **-1**（indexOfXxx）/ **false**（write/invoke）/
//     **空 QVariant**（read）。本讲每次都用之前先判负数，**绝不拿 -1 去构造表条目**：
//     QMetaObject::method(int)/property(int) 不做越界检查，喂负数就是越界读（见第三节）。
//   · 本机校验状态（照实说）：本文件**已真实编译、链接、运行**（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，offscreen；ctest 目标 metaobject_08_introspection 退出码 0，输出存档在
//     runlogs/metaobject_08_introspection.log）。本讲没有汇总自检行，所以「跑绿」只证明
//     没崩、走完了——打印出来的形状可以信，具体数字仍请以你自己那次运行输出为准。
//     下面两件事也是真跑过的（脚本在仓库 `_tools/check_syntax.sh`）：
//       moc.exe（D:/Qt/6.12.0/mingw_64/bin）→ 退出码 0、无 stderr；
//       g++.exe -fsyntax-only -std=c++17 -Wall -Wextra → 0 error、0 warning。
//     本讲凡是写「这里会报错」的地方都附了**编译器原样吐出来的那句话**，
//     包括只有真编译一次才写得出的细节：全局枚举配类内 `Q_ENUM`，
//     `QMetaEnum::fromType<T>()` 会在 qmetaobject.h 310 行的 static_assert 处挂掉。
//     枚举确实进了 qt_enums 段（第 6 节遍历 enumeratorCount() 读得到），
//     但「表里读得到」和「拿类型能拿到表」是两码事——差别就是第一节②。
//     所有 API 都对着本机真实头文件核对过（行号是 6.12.0 的）：
//       QtCore/qobjectdefs.h    —— className() 237 / superClass() 238 行；
//                                  methodOffset 251 / enumeratorOffset 252 / propertyOffset 253 行 /
//                                  classInfoOffset 254 行；
//                                  methodCount 257 / enumeratorCount 258 / propertyCount 259 /
//                                  **classInfoCount 260**（公开，别信「Qt6 没有它」的说法）行；
//                                  indexOfMethod 263 / indexOfSignal 264 / indexOfSlot 265 /
//                                  indexOfEnumerator 266 / indexOfProperty 268 /
//                                  indexOfClassInfo 269 行；method(int) 272 / enumerator(int) 273 /
//                                  property(int) 274 / classInfo(int) 275 / userProperty() 276 行；
//                                  invokeMethod 四个字符串重载 324 / 338 / 355 / 372 行
//                                  （324：带 Qt::ConnectionType + QGenericReturnArgument；
//                                    338：QGenericReturnArgument 版；
//                                    355：带 Qt::ConnectionType、**只收 QGenericArgument**；
//                                    372：不带 ConnectionType、只收 QGenericArgument）、
//                                  enum Call 567~576 行、QtPrivate::qobject_cast_helper
//                                  753~759 行（内含 static_assert "qobject_cast requires the
//                                  type to have a Q_OBJECT macro"）
//       QtCore/qmetaobject.h    —— QMetaMethod 21 行起：methodSignature 26 / name 27 /
//                                  typeName 29 / returnType 30 / returnMetaType 31 /
//                                  parameterCount 32 / parameterType 33 / parameterMetaType 34 /
//                                  parameterTypes 36 / parameterTypeName 37 / parameterNames 38 /
//                                  access 41 / methodType 48 / methodIndex 51 /
//                                  relativeMethodIndex 52 / revision 53 / isConst 54 /
//                                  invoke(QObject*,…) 59 行；
//                                  QMetaEnum 272 行起：name 277 / enumName 278 / metaType 279 /
//                                  isFlag 281 / isScoped 282 / is64Bit 283 / keyCount 285 /
//                                  key 286 / value 287 / keyToValue 292 / keysToValue 293 /
//                                  valueToKey(int) 297 / valueToKeys 298 / valueToKey(quint64) 300 /
//                                  fromType<T>() 307~316 行（310~312 行就是那句 static_assert；
//                                  313~315 行拆开看只有三步：ADL 助手取 metaObject、取名字，
//                                  再 enumerator(indexOfEnumerator(name))——它纯粹是语法糖）
//                                  QMetaProperty 341 行起：name 346 / typeName 347 / metaType 358 /
//                                  isReadable 362 / isWritable 363 / isResettable 364 /
//                                  isDesignable 365 / isUser 368 / isConstant 369 / isFinal 370 /
//                                  isRequired 373 / isBindable 374 / isFlagType 376 /
//                                  isEnumType 377 / hasNotifySignal 380 / notifySignal 381 /
//                                  notifySignalIndex 382 / read(const QObject*) 386 /
//                                  write(QObject*, const QVariant&) 387 /
//                                  write(QObject*, QVariant&&) 388 / reset(QObject*) 389 /
//                                  bindable(QObject*) 391 行；QMetaClassInfo 433 行起
//        ★ 勘误：上面这一串旧稿整体偏 +1（375/377/378/381/382/383/387/388/389/390/392），
//          差在 375 与 379 这两个空行上——数的时候把空行算进去了。本机 6.12.0 重新逐行数过。
//                                  （name 437 / value 438 行）
//       QtCore/qobject.h        —— qobject_cast<T>(QObject*) 431~435 行、
//                                  qobject_cast<T>(const QObject*) 437~443 行（带「不能丢
//                                  const」的 static_assert）、qobject_iid_cast 446~448 行、
//                                  Q_DECLARE_INTERFACE 那组宏 465~468 行
//       QtCore/qtmetamacros.h   —— Q_CLASSINFO 56 行、Q_ENUMS 68 行（Qt4 遗留，**别用**）、
//                                  Q_FLAGS 70 行（同上）、Q_ENUM 76 行、Q_FLAG 77 行、
//                                  Q_ENUM_NS 81 行、Q_FLAG_NS 82 行、Q_INVOKABLE 84 行
//       QtCore/qflags.h         —— class QFlags 107 行、Q_DECLARE_FLAGS 218 行、
//                                  Q_DECLARE_OPERATORS_FOR_FLAGS 238 行
//       QtCore/qmetatype.h      —— Q_DECLARE_METATYPE 1400 行、qRegisterMetaType 857 行
// ============================================================================

#include <QCoreApplication>
#include <QByteArray>
#include <QList>
#include <QMetaClassInfo>
#include <QMetaEnum>
#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaProperty>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QDebug>

// ============================================================================
// 一、枚举写在哪，决定三件事能不能用（这一段全是本机编译实测，不是查文档）
// ----------------------------------------------------------------------------
// 「枚举放类里 / 放全局 / 放命名空间」看着是口味问题，其实它同时改变了
//   (a) moc 收不收、表里读不读得到；
//   (b) `QMetaEnum::fromType<T>()` 过不过编译；
//   (c) 函数体里 `CanScan | CanPing` 找不找得到运算符。
// 三种摆法各自的真实后果：
//
// ① 类内嵌套 + 类内 Q_ENUM —— Qt 官方推荐的写法，也是 Q_ENUM 的本意
//    坑 1：枚举必须声明在 `Q_ENUM(...)` **之前**。宏是就地展开的（qtmetamacros.h
//    73~75 行），它要在类里生成两行 friend：
//        friend constexpr const QMetaObject *qt_getEnumMetaObject(ENUM) …
//        friend constexpr const char        *qt_getEnumName(ENUM) …
//    名字还没出现就展开 → `error: 'RadarMode' has not been declared`（实测）。
//    坑 2：`Q_DECLARE_FLAGS` 能留在类里，`Q_DECLARE_OPERATORS_FOR_FLAGS` 不能
//    （它要在命名空间作用域声明运算符，qflags.h 238 行），只能写在类外；
//    于是类内**内联函数体**里的 `CanScan | CanPing` 找不到它，退化成内置整数或，
//    报 `invalid conversion from 'int' to 'RadarBit'`（实测）。
//    → 嵌套枚举要按位组合，就在类外用 `Radar::RadarCaps`，类内只用单个枚举值或
//      `QFlags` 的显式构造，别指望内联体里的竖线。
//    坑 3：类里的枚举默认落在 **private** 段（写在 `Q_OBJECT` 和 `public:` 之间就是这样）。
//    我做小练习 3 时当场撞到，g++ 的原话是
//        error: 'enum class Radar::RadarMode' is private within this context
//        note: declared private here
//    两条出路：枚举前面补 `public:`，或者类外一律写全名 `Radar::RadarMode::Scan`。
//    这不是 Qt 的规矩，是 C++ 的——所以「把枚举搬进类里」的真实成本是**所有裸用的调用点**都要改。
// ② 全局作用域 + 类内 Q_ENUM —— 本讲的 Radar 就这么放（181~182 行那两行宏写在类外，竖线才有得用）
//    【勘误】这一节原来写的是「moc 收下，enumerator 段读得到，属性照样按名字读写」——
//    本机 Qt 6.12.0 + MinGW 13.1 实测**整条都不成立**， moc 根本不登记它。
//    证据是三层扣上的：
//      · 产物层：生成的 08_introspection.moc 里搜不到 Standby / Scan / Ping 任何一个；
//        对照 06 讲（类内 enum + Q_ENUM），它的 .moc 里有 "Amber" 和 { 21, Mode::Amber }。
//      · 运行层（就是本讲下面几节打出来的数）：
//            enumeratorOffset=0  enumeratorCount=0      ← 第 3 节「枚举表」因此一行都不打
//            property("mode").isEnumType() = false      ← 属性表那列「枚举/flag」是 ×
//            pMode.write(&radar, "Scan")   = false      ← 第 4 节，值也没动（读回还是 0）
//            pCaps.isFlagType()            = false      ← Q_FLAG(RadarCaps) 同一条路，
//                                                         keysToValue("CanScan|CanTrace") 无从谈起
//      · 编译层：`QMetaEnum::fromType<RadarMode>()` 过不了编译，报错原样抄自本机编译器
//            static assertion failed: QMetaEnum::fromType only works with enums declared
//            as Q_ENUM, Q_ENUM_NS, Q_FLAG or Q_FLAG_NS
//            note: 'QtPrivate::IsQEnumHelper<RadarMode>::Value' evaluates to false
//            （qmetaobject.h 310~311 行那个 static_assert）
//    原来那段把 fromType 失败解释成「宏没写错、枚举确实进了表，只是 ADL 找不到类里的
//    friend」——前半句就是错的：**表里压根没有它**。ADL 那半句只对 fromType 成立
//    （助手确实是类里的两个 friend，qtmetamacros.h 74~75 行），但它不是「按名字读写也失败」
//    的原因；两个失败是同一个人：moc 没登记。
//    所以②号摆法剩下的唯一收益就是竖线能用，代价是枚举的元信息全丢。
//    一句话记住：**要枚举进表，枚举就得声明在类里（①）或命名空间里配 Q_ENUM_NS（③）**；
//    Q_ENUM 写在类里、枚举本体在外面，moc 不报错、也不给你登记——这种「静默不干活」
//    比编译失败难查得多，所以本讲把空表打印出来给你看（见 dumpTables 里那行计数）。
// ③ 命名空间 + Q_NAMESPACE + Q_ENUM_NS / Q_FLAG_NS —— 要全局、又要 fromType，就用它
//    Q_ENUM_NS_IMPL（qtmetamacros.h 78~80 行）生成的是**命名空间作用域的 inline 函数**
//    （不是 friend），于是同一个命名空间里的枚举天然能被 ADL 找到，fromType 通过。
//    没有 `Q_NAMESPACE`（182 行）就两处一起报错，很好认：
//        moc：  Namespace declaration lacks Q_NAMESPACE macro.
//        g++：  'staticMetaObject' was not declared in this scope   ← 那两行助手要它
//    本讲第六节带一个这样的小命名空间，把 fromType 这条路真跑给你看。
//    顺带一个 Qt6 才有的事实：`enum class` 也能配 Q_DECLARE_FLAGS/Q_FLAG
//    （qflags.h 118 行的构造函数收得住 scoped 值），写 Qt5 那套「必须裸枚举」的
//    老博客现在是错的——我在本机用 `enum class Bit : int` + Q_FLAG_NS 编译过。
//
// Q_FLAG 登记的是「QFlags<E> 这种按位组合的类型」，要用它就必须：
//     enum RadarBit {...};                              // 裸枚举
//     Q_DECLARE_FLAGS(RadarCaps, RadarBit)              // qflags.h 218 行：typedef QFlags<RadarBit>
//     Q_DECLARE_OPERATORS_FOR_FLAGS(RadarCaps)          // 238 行：生成 operator| 等
// 而这两行宏（qflags.h 的 218/238）的**书写顺序**才是本讲把枚举放全局的直接原因：
// 运算符声明必须在用它的函数体之前出现，①的坑 2 就是反面教材。
// 【Q_ENUM vs Q_FLAG 的分水岭只有一个布尔】QMetaEnum::isFlag()（qmetaobject.h 281 行）：
//   Q_ENUM 登记「值互斥」的枚举 → 属性只能写单个名字；
//   Q_FLAG 登记「可按位组合」的枚举 → 属性能接受 "CanScan|CanTrace" 这种串
//          （解析靠 keysToValue，qmetaobject.h 293 行）。
// 另外 Q_ENUMS/Q_FLAGS（qtmetamacros.h 68/70 行）是 Qt4 时代的写法，只登记枚举不给
// fromType<T>() 之类的能力，新代码一律 Q_ENUM/Q_FLAG（命名空间里用 Q_ENUM_NS/Q_FLAG_NS）。
// ============================================================================
enum class RadarMode { Standby, Scan, Ping };      // Q_ENUM：值互斥的模式

enum RadarBit { NoCaps = 0x0, CanScan = 0x1, CanPing = 0x2, CanTrace = 0x4 };   // Q_FLAG：可按位
Q_DECLARE_FLAGS(RadarCaps, RadarBit)
Q_DECLARE_OPERATORS_FOR_FLAGS(RadarCaps)

// ── ③ 号摆法的实物：不属于任何 QObject 的枚举，靠命名空间进表（第一节③）────
// 这一小段的存在理由只有一个：让 `QMetaEnum::fromType<T>()` 有一条**能编译**的路可走。
// Radar 那两个枚举是②号摆法，fromType 会 static_assert 失败（第 6 节现场演示）。
namespace alarm
{
    Q_NAMESPACE                                        // qtmetamacros.h 182 行，缺它 moc 与 g++ 同时报错
    enum class Level { Quiet, Notice, Critical };      // Qt6：scoped 枚举也能进表（第一节末尾那条）
    Q_ENUM_NS(Level)                                   // 78~80 行：助手落在本命名空间 → ADL 找得到
    enum Side : int { NoSide = 0x0, Port = 0x1, Starboard = 0x2 };
    Q_DECLARE_FLAGS(Sides, Side)
    Q_DECLARE_OPERATORS_FOR_FLAGS(Sides)               // 写在本命名空间里，竖线就近可用
    Q_FLAG_NS(Sides)                                   // 82 行：按位组合那一张
}

// ── 被反射的对象：把「方法/属性/枚举/类信息」四类元信息都摆满 ────────────
class Radar : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("author", "老师傅")                 // 任意键值对，用 indexOfClassInfo 点查
    Q_CLASSINFO("version", "2.1")
    Q_CLASSINFO("bus", "can-0")

    // 【勘误】这一行原来写的是「枚举 + Q_ENUM ⇒ 这条属性能按名字读写」——对本讲不成立。
    // 按名字读写要的是 isEnumType()，而它是「枚举在不在表里」的查表结果；下面 217 行的
    // Q_ENUM(RadarMode) 是②号摆法（枚举本体在 178 行的全局作用域），moc 压根不登记它。
    // 于是这条属性的「枚举/flag」列是 ×，第 4 节 write("Scan") 返回 false（本机实测）。
    // 想让它成立就把枚举搬进类里（①号摆法，见 06 讲）——06 讲同一个位置的 runlog 是 √。
    Q_PROPERTY(RadarMode mode READ mode WRITE setMode NOTIFY modeChanged USER true)
    // flag 属性：只读 + CONSTANT + FINAL，写它必然 false
    Q_PROPERTY(RadarCaps caps READ caps CONSTANT FINAL)
    Q_PROPERTY(int range READ range WRITE setRange NOTIFY rangeChanged RESET resetRange)
    Q_PROPERTY(QString log READ log CONSTANT)

    Q_ENUM(RadarMode)
    Q_FLAG(RadarCaps)

public:
    explicit Radar(QObject *parent = nullptr) : QObject(parent) {}

    // 普通成员函数：不带 Q_INVOKABLE 就**不进方法表**（第 5 讲已实测这条）
    double secretBearing() const { return 137.0; }

    Q_INVOKABLE int sweep(int times)
    {
        m_sweeps += times;
        Q_EMIT swept(m_sweeps);
        return m_sweeps;
    }
    Q_INVOKABLE QString describe() const { return QStringLiteral("Radar mode=%1").arg(int(m_mode)); }

    int sweepCount() const { return m_sweeps; }      // 普通 getter：故意**不**加 Q_INVOKABLE

    RadarMode mode() const { return m_mode; }
    void setMode(RadarMode m)
    {
        if (m == m_mode) return;
        m_mode = m;
        Q_EMIT modeChanged(m_mode);
    }
    // 【这里有个真实编译错误，值得记】别写成 `RadarCaps(RadarBit(CanScan | CanPing))`：
    // operator| 的返回值**已经**是 QFlags<RadarBit>（也就是 RadarCaps），再往回 cast 成裸枚举
    // 会被拦：`error: invalid cast from type 'QFlags<RadarBit>' to type 'RadarBit'`。
    // 竖线在这里能用，靠的是运算符声明在**类之前**（第一节②那条摆法的唯一收益）。
    RadarCaps caps() const { return CanScan | CanPing; }
    int range() const { return m_range; }
    void setRange(int v)
    {
        if (v == m_range) return;
        m_range = v;
        Q_EMIT rangeChanged(m_range);
    }
    void resetRange() { m_range = kDefaultRange; }
    QString log() const { return m_log; }

public Q_SLOTS:
    void mark(const QString &text) { m_log += text; ++m_marks; }
    void clearLog() { m_log.clear(); m_marks = 0; }
    // 【顺带一个反直觉事实】槽里放 getter 也会被登记进方法表（当成 Slot），
    // 所以下面 dump 里你会看到 marks 出现在「槽」那一类里——槽只是个身份标签。
    int marks() const { return m_marks; }

Q_SIGNALS:
    void modeChanged(RadarMode mode);
    void rangeChanged(int range);
    void swept(int total);

private:
    static constexpr int kDefaultRange = 100;
    RadarMode m_mode = RadarMode::Standby;
    int m_range = kDefaultRange;
    int m_sweeps = 0;
    int m_marks = 0;
    QString m_log;
};

// ============================================================================
// 二、索引的三个世界：全局索引 / 本类局部索引 / 父类累计偏移
// ----------------------------------------------------------------------------
// methodCount() 是**含所有父类**的总条数；methodOffset()（251 行）= 父类累计条数
// = 本类那一段的起点。所以「只看自己写的」一律 `for (i = offset(); i < count(); ++i)`。
// 而 qt_static_metacall / QMetaObject::activate 用的是**局部索引**：局部 = 全局 - offset
//（第 5 讲摘录里那句 `_id -= 6;` 就在做这个减法）。QMetaMethod::relativeMethodIndex()
//（52 行）直接把这个局部索引给你，对表最方便。
// ============================================================================
static QString methodKind(QMetaMethod::MethodType t)
{
    switch (t) {
    case QMetaMethod::Method:      return QStringLiteral("方法(Q_INVOKABLE)");
    case QMetaMethod::Signal:      return QStringLiteral("信号");
    case QMetaMethod::Slot:        return QStringLiteral("槽");
    case QMetaMethod::Constructor: return QStringLiteral("构造");
    }
    return QStringLiteral("未知");
}

static QString joinTypes(const QList<QByteArray> &in)
{
    QStringList out;
    for (const QByteArray &b : in) out << QString::fromLatin1(b);
    return out.join(QStringLiteral(", "));
}

static void dumpTables(const QMetaObject *mo)
{
    qInfo() << "  ── 方法表（本类段：全局索引" << mo->methodOffset() << "到" << mo->methodCount() - 1 << "）──";
    for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {
        const QMetaMethod m = mo->method(i);
        qInfo().noquote() << QStringLiteral("    [%1] %2  %3(%4)  参数名=(%5)  返回=%6  局部索引=%7  revision=%8")
                                 .arg(i)
                                 .arg(methodKind(m.methodType()),
                                      QString::fromLatin1(m.name()),
                                      joinTypes(m.parameterTypes()))
                                 .arg(joinTypes(m.parameterNames()))
                                 .arg(QString::fromLatin1(m.typeName()))
                                 .arg(m.relativeMethodIndex())
                                 .arg(m.revision());
        // parameterTypes()（36 行）给 QList<QByteArray>，是**类型名文本**；
        // parameterMetaType()（34 行）给 QMetaType 对象，是**运行时可比对的类型**。
        // 两者不是一回事：`"QVector<int>"` 这种文本能不能变成 QMetaType，取决于有没有
        // Q_DECLARE_METATYPE —— 这是第 1 讲「信号参数要可拷贝」那条铁律的另一面。
    }

    qInfo() << "  ── 属性表 ──";
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        const QMetaProperty p = mo->property(i);
        qInfo().noquote() << QStringLiteral("    [%1] %2 : %3  读=%4 写=%5 可复位=%6 常量=%7 终态=%8 NOTIFY=%9 枚举/flag=%10")
                                 .arg(i)
                                 .arg(QString::fromLatin1(p.name()),
                                      QString::fromLatin1(p.typeName()))
                                 .arg(p.isReadable() ? "√" : "×")
                                 .arg(p.isWritable() ? "√" : "×")
                                 .arg(p.isResettable() ? "√" : "×")
                                 .arg(p.isConstant() ? "√" : "×")
                                 .arg(p.isFinal() ? "√" : "×")
                                 .arg(p.hasNotifySignal()
                                      ? QString::fromLatin1(p.notifySignal().name())
                                      : QStringLiteral("无"))
                                 .arg(p.isEnumType() || p.isFlagType() ? "√" : "×");
    }

    qInfo() << "  ── 枚举表 ──";
    // 【先打区间，再打内容】这一段是本讲最重要的一处「静默失败」：②号摆法下
    // enumeratorOffset() 和 enumeratorCount() 都是 0，下面那个循环**一行都不出**。
    // 只打一个「── 枚举表 ──」标题就走，读者会以为是自己终端截断了；把区间写出来，
    // 空表才成为一条能读的证据（对照第 1 节②号摆法那段）。
    qInfo().noquote() << QStringLiteral("    区间 = [%1, %2)  共 %3 条")
                             .arg(mo->enumeratorOffset())
                             .arg(mo->enumeratorCount())
                             .arg(mo->enumeratorCount() - mo->enumeratorOffset());
    for (int i = mo->enumeratorOffset(); i < mo->enumeratorCount(); ++i) {
        const QMetaEnum e = mo->enumerator(i);
        QStringList kv;
        for (int k = 0; k < e.keyCount(); ++k)
            kv << QStringLiteral("%1=%2").arg(QString::fromLatin1(e.key(k))).arg(e.value(k));
        qInfo().noquote() << QStringLiteral("    [%1] %2  isFlag=%3 isScoped=%4 is64Bit=%5  取值(%6)")
                                 .arg(i)
                                 .arg(QString::fromLatin1(e.name()),
                                      e.isFlag() ? "√" : "×")
                                 .arg(e.isScoped() ? "√" : "×")
                                 .arg(e.is64Bit() ? "√" : "×")
                                 .arg(kv.join(QStringLiteral(", ")));
    }
    if (mo->enumeratorCount() == mo->enumeratorOffset()) {
        qInfo() << "    ↑ 0 条：moc 收下了第 217/218 行的 Q_ENUM/Q_FLAG，却没往枚举段写任何东西。"
                   "产物层直接看得见——生成的 08_introspection.moc 里那一行是空的："
                   "`QtMocHelpers::UintData qt_enums { };`；"
                << "而 06 讲（枚举声明在类里，①号摆法）同一位置是"
                   "`EnumData<enum Mode>(19, 19, ...).add({ { 20, Mode::Green }, { 21, Mode::Amber }, … })`。"
                << "  本文件里③号摆法的 alarm 那张表有"
                << alarm::staticMetaObject.enumeratorCount()
                << "条，第 6 节按名字读的就是它。";
    }

    qInfo() << "  ── 类信息（Q_CLASSINFO）──";
    // 【网上流传的「Qt6 没有 classInfoCount()，只能点查」是错的】
    // 我核对 6.12 的 qobjectdefs.h 才改口：classInfoOffset()（254 行）和
    // classInfoCount()（260 行）跟其他 offset/count 一样是**公开声明**的，
    // 所以边界齐全的遍历完全合法——从 offset 起、到 count 止，别越界就行。
    for (int i = mo->classInfoOffset(); i < mo->classInfoCount(); ++i) {
        const QMetaClassInfo info = mo->classInfo(i);        // 275 行：不做越界检查
        // 【键用 fromLatin1，值必须用 fromUtf8】键是 ASCII，值是你我写的中文。
        // moc 把宏里的字符串**原样**塞进字符串池（本机生成的 .moc 第 42 行就是
        // `"老师傅"`，字节是 E8 80 81 E5 B8 88 E5 82 A5，UTF-8），而
        // QString::fromLatin1 按「一字节一字符」解，三个字变九个人，
        // 再经 Windows 控制台（本机 GBK）就成了 `è????????`（一个 é 加八个 ?，共 9 个字符）。
        // 这条不是 Qt 的锅，是调用点的锅。
        qInfo().noquote() << QStringLiteral("    [%1] %2 = %3")
                                 .arg(i)
                                 .arg(QString::fromLatin1(info.name()),       // 437 行：键
                                      QString::fromUtf8(info.value()));        // 438 行：值
    }
    // 真正**不推荐**的是另一件事：`while (mo->classInfo(i).name()) ++i;` 这种
    // 「越界读到字符串池末尾的空串就停」的写法——它靠内部布局活着，Qt 一改就失效。
    // 而宿主只要**查一个已知键**时，点查最直白：indexOfClassInfo（269 行）拿不到给 -1，
    // 判负数再用（本讲开头那条铁律在这儿一样成立）。
    static const char *const wanted[] = { "version", "不存在的那条" };
    for (const char *key : wanted) {
        const int ci = mo->indexOfClassInfo(key);
        if (ci < 0) { qInfo().noquote() << QStringLiteral("    %1 → 没有这条（indexOfClassInfo=-1）").arg(key); continue; }
        const QMetaClassInfo info = mo->classInfo(ci);
        qInfo().noquote() << QStringLiteral("    [%1] %2 = %3")
                                 .arg(ci)
                                 .arg(QString::fromLatin1(info.name()),       // 同上：键 ASCII
                                      QString::fromUtf8(info.value()));        // 同上：值是 UTF-8
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    Radar radar;
    const QMetaObject *mo = radar.metaObject();

    qInfo() << "=== 1. 先认门：这是一张**沿继承链**的说明书 ===";
    // superClass() 走到 QObject 就返回 nullptr，第二次链之前必须判空（否则段错误）。
    {
        const QMetaObject *up1 = mo->superClass();
        const QMetaObject *up2 = up1 ? up1->superClass() : nullptr;
        qInfo() << "  className() =" << mo->className()
                << " superClass() =" << (up1 ? up1->className() : "(nullptr)")
                << " 再往上 =" << (up2 ? up2->className() : "(nullptr，到顶了)")
                << "（superClass() 到顶返回 nullptr，别无条件链两下）";
    }
    qInfo() << "  methodCount() =" << mo->methodCount() << " 本类段起点 =" << mo->methodOffset()
            << "（前面那几十条是 QObject 的，不是你写的）";
    qInfo() << "  propertyCount() =" << mo->propertyCount() << " 本类属性段起点 ="
            << mo->propertyOffset();
    qInfo() << "  enumeratorCount() =" << mo->enumeratorCount()
            << " userProperty()（第一个 USER=true）=" << QString::fromLatin1(mo->userProperty().name());

    qInfo() << "=== 2. 三张表原样打出来（「反射遍历」的本体就这二十行）===";
    dumpTables(mo);

    qInfo() << "=== 3. 按名字查：查不到一律给 -1，判负再用 ===";
    qInfo() << "  indexOfMethod(\"sweep(int)\")      =" << mo->indexOfMethod("sweep(int)");
    qInfo() << "  indexOfSignal(\"swept(int)\")      =" << mo->indexOfSignal("swept(int)");
    qInfo() << "  indexOfSlot(\"mark(QString)\")     =" << mo->indexOfSlot("mark(QString)");
    qInfo() << "  indexOfProperty(\"range\")         =" << mo->indexOfProperty("range");
    qInfo() << "  indexOfEnumerator(\"RadarMode\")   =" << mo->indexOfEnumerator("RadarMode");
    const int secretIdx = mo->indexOfMethod("secretBearing()");
    qInfo() << "  indexOfMethod(\"secretBearing()\") =" << secretIdx
            << "  ← **没写 Q_INVOKABLE 的普通成员函数根本不在表里**（跟访问权限无关），"
            << "第 5 讲那条「Q_INVOKABLE 是注册，不是权限」在这儿再验一次";
    // 【绝对不要这样写】`mo->method(-1)`：QMetaObject::method(int) 不做越界检查，
    // 传 -1 就是拿负下标去索引数据段 → 越界读，Debug 下可能断言、Release 下可能给你一个
    // 看着像「空」的假条目。判负数是**唯一**正确的用法，本讲全程照办。

    qInfo() << "=== 4. 用反射**写属性**：三条入口，落到同一个 setter ===";
    int rangeHits = 0;
    QObject::connect(&radar, &Radar::rangeChanged, &radar, [&rangeHits](int v) {
        ++rangeHits;
        qInfo() << "    [NOTIFY] rangeChanged 收到" << v;
    });
    const int pIdx = mo->indexOfProperty("range");
    const QMetaProperty pRange = mo->property(pIdx);            // 274 行
    qInfo() << "  read() 当前值 =" << pRange.read(&radar)       // 387 行
            << " 属性类型名 =" << QString::fromLatin1(pRange.typeName())
            << " QMetaType =" << pRange.metaType().name();      // 358 行
    const bool okWrite = pRange.write(&radar, 250);             // 388 行
    qInfo() << "  write(250) 成功？" << okWrite << " 读回 =" << pRange.read(&radar).toInt()
            << " 通知触发次数 =" << rangeHits
            << " ← 证据链：元对象 write → WriteProperty → switch 调 setRange → Q_EMIT → activate";
    // 【勘误】这一行原来写的是「枚举属性能直接喂字符串（Q_ENUM 的功劳）」——本机实测喂不进去，
    // 下面那行 false 就是它。为什么喂不进去，两条路都堵死了（都是 6.12 源码里核对过的行号）：
    //   · 捷径那条：QMetaProperty::write 里有个「QString → 枚举名」分支，条件写在
    //     qmetaobject.cpp 4084 行 `if (isEnumType() && !t.metaObject() && …)`。
    //     注意那个 `!t.metaObject()`：Qt 自己的注释（4085~4087 行）说明这条捷径是给
    //     **老 Q_ENUMS** 用的（Q_ENUMS 只登记枚举、不给 QMetaType 挂 metaObject）。
    //     ②号摆法在这里连门都摸不到：isEnumType() = (EnumOrFlag 位 && menum.name())
    //     （同文件 3912 行），枚举段空 → menum 无效 → false。
    //   · 兜底那条：走 QVariant::convert → qmetatype.cpp 2120 行 convertToEnum →
    //     2023 行 metaEnumFromType，它先要 t.metaObject() 非空（2026 行），
    //     实测 RadarMode 这个 QMetaType 的 metaObject() 就是 nullptr → 还是失败。
    // 对照：①号摆法（枚举在类里）两条门都开着——它的 QMetaType 名字是 "Radar::RadarMode"，
    // metaEnumFromType 在 2034~2035 行把作用域剥掉再查表，所以写名字能成（06 讲实测 true）。
    // 换句话说：这两行 false 是同一个病根，不是「Scan 这个值写错了」。想让它 true，见小练习 3。
    const QMetaProperty pMode = mo->property(mo->indexOfProperty("mode"));
    const bool okEnum = pMode.write(&radar, QStringLiteral("Scan"));
    qInfo() << "  mode.write(\"Scan\") 成功？" << okEnum << " 读回 =" << pMode.read(&radar)
            << " 用 C++ 比较 ==\"Scan\"?" << (radar.mode() == RadarMode::Scan);
    const bool okBadEnum = pMode.write(&radar, QStringLiteral("不存在值"));
    qInfo() << "  mode.write(\"不存在值\") 成功？" << okBadEnum
            << "（这一条**本来就该** false：不动值、返回 false，不抛不崩。"
            << "上面那条 Scan 也 false 是②号摆法的病，不是这条的对照组——两条别混着读。）";
    // 名字写不进去，那整数呢？②号摆法退化成什么样子，这两行是最后一块拼图（本机实测）。
    const bool okInt = pMode.write(&radar, 1);
    qInfo() << "  mode.write(1)（按整数写）成功？" << okInt << " 读回 =" << pMode.read(&radar)
            << " C++ 那边 ==" << int(radar.mode());
    // 顺手量一个**根本不存在**的整数：别以为宏摆对了元对象就会替你把关（本机两种摆法都量过）。
    const bool okBogus = pMode.write(&radar, 99);
    qInfo() << "  mode.write(99)（枚举里没这个值）成功？" << okBogus
            << " 读回 =" << pMode.read(&radar) << " C++ 那边 ==" << int(radar.mode())
            << " ← 整数路压根不查枚举表（查表那条只服务「按名字」），所以 99 也写得进去。"
            << "而且这条**跟宏摆没摆对无关**：我把 RadarMode 搬进类里再跑，一样 true，"
            << "只是读回打印成了 `Radar::RadarMode(99)`（没有键名可映射，就退回数字）。"
            << "结论：Q_ENUM 不是输入校验，脏数据要在自己的 setter 里挡。";
    const QMetaProperty pCaps = mo->property(mo->indexOfProperty("caps"));
    qInfo() << "  caps（CONSTANT FINAL）isWritable =" << pCaps.isWritable()
            << " isFlagType =" << pCaps.isFlagType()
            << " 写 7 的结果 =" << pCaps.write(&radar, 7)
            << " 读回 =" << pCaps.read(&radar);   // QVariant 直接打印（flag 类型的值）
    const bool okReset = pRange.reset(&radar);                  // 390 行
    qInfo() << "  reset() 成功？" << okReset << " range 现在 =" << pRange.read(&radar).toInt()
            << "（RESET 落点是 resetRange()，回到默认 100）";

    qInfo() << "=== 5. 用反射**调方法**：QMetaMethod::invoke 与四个 invokeMethod 重载 ===";
    // 路线 A：已经拿到 QMetaMethod（表里那一行），用它的 invoke（qmetaobject.h 59 行）
    int sweepsA = -1;
    const int sweepIdx = mo->indexOfMethod("sweep(int)");
    const QMetaMethod mSweep = mo->method(sweepIdx);
    const bool okA = sweepIdx >= 0 && mSweep.invoke(&radar, Qt::DirectConnection,
                                                   Q_RETURN_ARG(int, sweepsA), Q_ARG(int, 3));
    qInfo() << "  A) QMetaMethod::invoke(\"sweep\", 3) ok =" << okA << " 返回 =" << sweepsA
            << " 累计 =" << radar.sweepCount();
    // 路线 B：只有名字，走 QMetaObject::invokeMethod（324 行那个带 ConnectionType +
    //         QGenericReturnArgument 的重载；返回 void 的槽请用 355 行那个「只收
    //         QGenericArgument」的重载，见路线 C——这是四个重载最容易踩的区别）
    int sweepsB = -1;
    const bool okB = QMetaObject::invokeMethod(&radar, "sweep", Qt::DirectConnection,
                                               Q_RETURN_ARG(int, sweepsB), Q_ARG(int, 4));
    qInfo() << "  B) invokeMethod(\"sweep\", Q_RETURN_ARG+Q_ARG) ok =" << okB
            << " 返回 =" << sweepsB;
    // 路线 C：无返回值的槽 mark(QString) —— 不要写 Q_RETURN_ARG(void, ...)，
    //         355 行那个重载根本不接受返回参数，Q_ARG 会正确落到第 4 个形参上
    const bool okC = QMetaObject::invokeMethod(&radar, "mark", Qt::DirectConnection,
                                               Q_ARG(QString, QStringLiteral("自检 ")));
    qInfo() << "  C) invokeMethod(\"mark\", Q_ARG(QString,…)) ok =" << okC
            << " log =" << radar.log();
    // 路线 D：类型写错的代价。sweep(int) 你喂 Q_ARG(double, …)：
    int sweepsD = -1;
    const bool okD = QMetaObject::invokeMethod(&radar, "sweep", Qt::DirectConnection,
                                               Q_RETURN_ARG(int, sweepsD), Q_ARG(double, 3.0));
    qInfo() << "  D) 故意用 Q_ARG(double,3.0) 调 sweep(int) → 返回" << okD
            << "（派发前要比对**参数类型 id**，对不上就 false；Debug 构建还会 qWarning。"
            << "这就是「字符串/按名字那条路把编译期检查换成运行期失败」的具体形状。）";
    // 清一下（顺带演示 invokeMethod 调无参槽）
    QMetaObject::invokeMethod(&radar, "clearLog", Qt::DirectConnection);
    qInfo() << "  invokeMethod(\"clearLog\") 之后 log 长度 =" << radar.log().size()
            << " marks =" << radar.marks();

    qInfo() << "=== 6. Q_ENUM / Q_FLAG：枚举进表，才有「按名字」这条路 ===";
    // ── 6.1 ② 号摆法：先证明 Radar 的表是空的，再把读表演示挪到真有表的 alarm 上 ──
    // 【勘误】本节原来在 Radar 的表上演示「indexOfEnumerator → enumerator(i)」，
    //  else 分支里那句「只有把 Q_ENUM(RadarMode) 那行删掉才会这样」也不成立：
    // 宏在着，表照样是空的（第 3 节刚打出「共 0 条」）。演示挪到③号摆法的 alarm 上跑。
    const int eModeIdx = mo->indexOfEnumerator("RadarMode");
    qInfo().noquote() << QStringLiteral("  Radar 的表：indexOfEnumerator(\"RadarMode\") = %1"
                                        "  枚举段区间 = [%2, %3)")
                             .arg(eModeIdx)
                             .arg(mo->enumeratorOffset())
                             .arg(mo->enumeratorCount());
    // 【这句解释为什么跟着数走】结论必须由实测值决定：否则做完小练习 3（把枚举搬进类里）
    // 之后，程序一边打出 0、一边说「表里没有它」——讲义自己跟自己矛盾。
    if (eModeIdx < 0)
        qInfo() << "    ← 表里没有它。217/218 行那两行宏**在**，可枚举本体在 178/180 行的全局作用域，"
                   "moc 就不登记（第 1 节②号摆法）。按名字读、按名字写、QML 里列合法值，全从这张表出发。"
                   "把小练习 3 做完再跑，这一整条会换成另一条——因为它是 if 出来的。";
    else
        qInfo() << "    ← 表里有它了：这是**已经做完小练习 3**（把 RadarMode 搬进类里）的样子；"
                   "本讲正文停在②号摆法，正常跑看到的应该是上面那条 -1。";
    // 【越界不崩，但给你一个「看着正常」的空表】QMetaObject::enumerator(int) 的实现
    // （qmetaobject.cpp 1258 行）是「减 offset，不在 [0,count) 里就返回默认构造的 QMetaEnum」。
    // 我拿探针量过 enumerator(-1)：isValid()=false、name()=nullptr、keyCount=0，
    // 而 QString::fromLatin1(nullptr) 不炸，只给你一个 isNull() 的串。
    // → 所以判负数这条铁律在这儿照样成立，只是失败的样子是「全空」而不是「崩」。
    // 【查表的键不能用属性的类型名】实测：①号摆法（枚举在类里）属性表那一行的类型名是
    // 带作用域的 "Radar::RadarMode"（小练习 3 跑出来就是这样），而枚举段里存的名字不带走作用域
    // （同一次运行打的是 `[0] RadarMode 取值(Standby=0, Scan=1, Ping=2)`）。
    // 拿 typeName 直接去 indexOfEnumerator 会 -1（这一句是另拿一个等价的嵌套枚举类探针量的：
    // typeName="L::LMode"、枚举段 name="LMode"、indexOfEnumerator(typeName)=-1，
    // 而同一个属性打印 isEnumType=true——两道门是分开关的）；Qt 自己也不是直接查：
    // qmetatype.cpp 2034~2035 行先用 lastIndexOf(':') 剥掉作用域再问表。
    // 已经拿着 QMetaProperty 的时候，直接 pMode.enumerator() 就行（②号实测 isValid=false，
    // 它不会骗你说是枚举属性；①号实测 isValid=true、enclosingMetaObject 就是那个类）。
    // 【为什么这里我不用 QMetaEnum::fromType<RadarMode>()】——它过不了编译，本机试过，
    // 报错原样抄给你（这段文字就是从编译器输出里搬来的，不是我推测的）：
    //     static assertion failed: QMetaEnum::fromType only works with enums declared
    //     as Q_ENUM, Q_ENUM_NS, Q_FLAG or Q_FLAG_NS
    //     note: 'QtPrivate::IsQEnumHelper<RadarMode>::Value' evaluates to false
    // 报错里那句 `IsQEnumHelper<RadarMode>::Value evaluates to false` 是关键：fromType 的
    // **编译期**那道门要 ADL 找得到 qt_getEnumMetaObject / qt_getEnumName，而 Q_ENUM 展开成的是
    // 类里的两个 friend（qtmetamacros.h 74~75 行），全局枚举做 ADL 时压根不会进 Radar 作用域。
    // 再看 qmetaobject.h 313~315 行，fromType 的三步就是
    //     qt_getEnumMetaObject(T()) → qt_getEnumName(T()) → enumerator(indexOfEnumerator(name))
    // 最后一步还是**查表**——它只是语法糖。所以 ②号摆法在这条路上其实撞了两次同一堵墙：
    // 糖的那一步（编译期 ADL）找不到助手，糖化后的那一步（运行期查表）表又是空的（上面实测 -1）。
    // 两个失败不是两件事，是一个根因的两种表现：枚举声明在类外，moc 不认为它是「这个类的枚举」。
    // ── 读表两步走：跑在③号摆法的 alarm 上 ────────────────────────────────────
    // 【为什么换表演示】上面刚证明 Radar 的枚举段 0 条；把 API 演示写在一张空表上，
    // 读者只能看到「循环没跑」，分不清是 API 不对还是表没内容。挪到有内容的表上才叫演示。
    const QMetaObject *nmo = &alarm::staticMetaObject;
    qInfo().noquote() << QStringLiteral("  换一张有内容的表：%1 区间=[%2, %3) 共 %4 条")
                             .arg(QString::fromLatin1(nmo->className()))
                             .arg(nmo->enumeratorOffset())
                             .arg(nmo->enumeratorCount())
                             .arg(nmo->enumeratorCount() - nmo->enumeratorOffset());
    const int eLevelIdx = nmo->indexOfEnumerator("Level");
    if (eLevelIdx < 0) {
        qInfo() << "  【注意】 ③号摆法读不到 = alarm 里那行 Q_ENUM_NS(Level) 被删了（小练习 3）";
    } else {
        const QMetaEnum eLv = nmo->enumerator(eLevelIdx);
        qInfo() << "  读表两步走：indexOfEnumerator → enumerator(i)"
                << " 索引 =" << eLevelIdx
                << " name =" << QString::fromLatin1(eLv.name())
                << " keyCount =" << eLv.keyCount()
                << " isScoped =" << eLv.isScoped()              // enum class → √（282 行）
                << " isFlag =" << eLv.isFlag()                  // Q_ENUM_NS → ×（281 行）
                << " 挂在哪个表 =" << QString::fromLatin1(eLv.enclosingMetaObject()->className());
        qInfo() << "  valueToKey(1) =" << QString::fromLatin1(eLv.valueToKey(1))
                << " keyToValue(\"Critical\")（不带 ok 的重载）=" << eLv.keyToValue("Critical");
        bool okKey = false;
        const int critical = eLv.keyToValue("Critical", &okKey);
        qInfo() << "  keyToValue(\"Critical\", &ok) =" << critical << " ok =" << okKey;
        bool okYes = true;                    // 故意先给 true，看这次调用有没有把它改写掉
        const int nope = eLv.keyToValue("Nope", &okYes);
        qInfo() << "  keyToValue(\"Nope\", &ok) =" << nope << " ok =" << okYes
                << "（**用带 ok 的重载**：-1 既可能是「查不到」也可能是某个合法值本身，"
                << "别拿它当枚举值往下传。上面两行合起来就是把 ok 和返回值分开问清楚。）";
    }
    // flag 那张表：本讲故意**不按名字**找（枚举名在表里存的是 moc 写的那个文本，不同
    // Qt 版本写法有别，①号摆法还带作用域），而是遍历 enumerator 段、用 isFlag() 认它——最稳的读表姿势。
    int flagRows = 0;
    for (int i = mo->enumeratorOffset(); i < mo->enumeratorCount(); ++i) {
        const QMetaEnum e = mo->enumerator(i);
        if (!e.isFlag()) continue;
        ++flagRows;
        bool okCombo = false;
        const int combo = e.keysToValue("CanScan|CanTrace", &okCombo);
        qInfo() << "  flag 枚举" << QString::fromLatin1(e.name())
                << " keysToValue(\"CanScan|CanTrace\") =" << combo << " ok =" << okCombo
                << " valueToKeys(3) =" << QString::fromLatin1(e.valueToKeys(3))
                << " ← Q_FLAG 才给得到的待遇：按位组合的**名字串**能被解析";
    }
    if (flagRows == 0)
        qInfo() << "  ↑ 上面这个循环一行没跑：枚举段里**没有 isFlag 的条目**——180 行的 RadarBit 在全局，"
                << "Q_FLAG(RadarCaps) 因此没登记。按位组合名字串那个待遇，"
                << "下面在 alarm::Sides（Q_FLAG_NS）上照样跑得通——差别只在宏和枚举摆哪儿，不在宏本身。";
    // ── 6.2 ③ 号摆法：命名空间 + Q_NAMESPACE + Q_ENUM_NS / Q_FLAG_NS，fromType 亮绿灯 ──
    // 文件顶部 alarm 那八行在这里兑现：助手生成在**命名空间作用域**（qtmetamacros.h
    // 78~80 行用的是 inline 而不是 friend），ADL 顺手就能找到，于是可以按类型直接拿表。
    const QMetaEnum eLevel = QMetaEnum::fromType<alarm::Level>();     // qmetaobject.h 307 行
    qInfo() << "  fromType<alarm::Level>()  name =" << QString::fromLatin1(eLevel.name())
            << " enumName（不带作用域）=" << QString::fromLatin1(eLevel.enumName())
            << " scope =" << QString::fromLatin1(eLevel.scope())
            << " keyCount =" << eLevel.keyCount() << " isFlag =" << eLevel.isFlag()
            << " isScoped =" << eLevel.isScoped() << "（Qt6 里 enum class 照样进得了表）";
    const QMetaEnum eSides = QMetaEnum::fromType<alarm::Sides>();     // flag 那张也一样
    bool okSide = false;
    qInfo() << "  fromType<alarm::Sides>()  isFlag =" << eSides.isFlag()
            << " keysToValue(\"Port|Starboard\") =" << eSides.keysToValue("Port|Starboard", &okSide)
            << " ok =" << okSide
            << " ← 按位组合的名字串被解析回整数，这就是 Q_FLAG / Q_FLAG_NS 的待遇"
            << "（没有宏，keysToValue 无从谈起）";
    qInfo() << "  metaType()（279 行）= " << QString::fromLatin1(eSides.metaType().name())
            << " ← 属性系统把 QVariant 变回 Sides、QML 那边按名字取值，落点都是这个 QMetaType";
    // 一句话把 6.1/6.2 钉住：**要「按名字」这套能力（fromType、按名字读写属性、QML 里的
    // 合法值列表），枚举就放进类里（Q_ENUM）或带 Q_NAMESPACE 的命名空间里（Q_ENUM_NS/Q_FLAG_NS）。**
    // 【勘误】这里原来写的是「已经像 Radar 这样放全局了也别慌，indexOfEnumerator 那条读表路
    // 照样通」——本机实测不通：indexOfEnumerator("RadarMode") = -1，因为 moc 压根没登记，
    // 换哪条读表路都一样（它们最后一步都是 enumerator(i)）。②号摆法真正保住的只有竖线。
    // senior 面试问到这里，答得出「fromType 只是语法糖，糖化后的那一步还是查表，
    // 而表是 moc 决定写不写的」才算真懂——编译期那道门（ADL）和运行期那张表（qt_enums）
    // 是同一次「moc 认不认这是本类枚举」的两面。

    // 【少了这两行宏、或者宏摆错地方，退化成什么】元对象眼里没有这张对照表：属性那边
    // 第 4 节现场量过（write("Scan") false），按整数的路能不能走通同一节也量了。
    // 配置文件、QML、报错信息里那些「合法取值列表」全都没有——这是性价比最高的一行宏，
    // 也是**最容易摆错位置**的一行宏：宏要在类里，枚举本体也要在类里（或命名空间里配 _NS 版本）。

    qInfo() << "=== 7. qobject_cast vs dynamic_cast：三条真实差别 ===";
    QObject *asBase = &radar;                               // 静态类型退化成 QObject*
    Radar *byQt = qobject_cast<Radar*>(asBase);            // qobject.h 431 行
    Radar *byRtti = dynamic_cast<Radar*>(asBase);
    qInfo() << "  qobject_cast<Radar*> =" << (byQt ? "命中" : "空")
            << " dynamic_cast<Radar*> =" << (byRtti ? "命中" : "空")
            << " 两个指针相等？" << (byQt == byRtti);
    // ① 机制不同：qobject_cast 走 `object->qt_metacast(T::staticMetaObject.className())`
    //    （第 5 讲摘录：一次 strcmp + 沿 superClass 链往上问），**不碰 RTTI**。
    //    → 关 RTTI（-fno-rtti）的工程里 dynamic_cast 直接不能用，qobject_cast 照跑；
    //    → dynamic_cast 依赖 typeinfo 在各个 so/dll 里是同一份，跨模块 + 静态链接时
    //      更容易「明明同类型却返回空」。
    // ② 成本不同：qobject_cast = 虚函数 + strcmp；dynamic_cast 在深继承/虚继承下要走
    //    完整的 class-hierarchy 匹配，更贵，而且行为跟编译器实现绑定。
    // ③ 能力边界不同：**qobject_cast 只认 Q_OBJECT 参与生成的那张表**。
    //    没写 Q_OBJECT 的类连编译都过不去（qobjectdefs.h 753~759 行的 static_assert：
    //    "qobject_cast requires the type to have a Q_OBJECT macro"）；写了但类里没自己的
    //    表（第 5 讲那条：metaObject 返回父类的），则永远返回空指针——**空指针不报错**，
    //    所以判空那一句永远别省。
    Radar *wrongCast = qobject_cast<Radar*>(asBase);        // 同类型：命中
    QCoreApplication *castToApp = qobject_cast<QCoreApplication*>(asBase);  // 不通的类型
    qInfo() << "  同类型再 cast =" << (wrongCast ? "命中" : "空")
            << " 把 Radar* cast 成 QCoreApplication* =" << (castToApp ? "命中（不可能）" : "空（正确）");
    // ④ 想 cast 到**不是 QObject 的接口类**，靠 Q_DECLARE_INTERFACE（qobject.h 465~468 行）：
    //    它为 IFace 特化出 `qobject_cast<IFace*>(QObject*)`，实现体拿接口 iid 去
    //    qt_metacast/qobject_iid_cast（446~448 行）问——Qt 插件系统（Q_PLUGIN_METADATA +
    //    Q_INTERFACES，qtmetamacros.h 58/59 行）的底座就是这个。
    // ⑤ const 版本（437~443 行）带 static_assert：不能借 qobject_cast 丢 const。
    const QObject *cbase = &radar;
    qInfo() << "  const QObject* 也能读元信息（想 cast 成非 const 指针会被 static_assert 拦）："
            << cbase->metaObject()->className();
    // 顺带把第 7 讲的账对一下：radar 是**栈对象**，下面这个 child 会随它一起析构，
    // 不需要（也不该）手工 delete。
    QObject *childOfRadar = new QObject(&radar);
    qInfo() << "  radar.children().size() =" << radar.children().size()
            << "（new 出来的子对象挂在栈上的 radar 下——radar 析构时会把它删掉）";
    Q_UNUSED(childOfRadar);

    // ── 自动脚本 ────────────────────────────────────────────────────────
    QTimer::singleShot(120, [&radar, &rangeHits] {
        qInfo() << "=== 8. 反射写属性在事件循环里也一样：同步派发，不是消息 ===";
        const QMetaObject *m2 = radar.metaObject();
        const QMetaProperty pr = m2->property(m2->indexOfProperty("range"));
        const int beforeHits = rangeHits;
        pr.write(&radar, 777);
        qInfo() << "  write(777) 之后刷新增量 =" << (rangeHits - beforeHits)
                << " 读回 =" << pr.read(&radar).toInt()
                << "（同线程就是 DirectConnection：这一行里 setter + notify + 槽全跑完了）";
        QMetaObject::invokeMethod(&radar, "sweep", Qt::DirectConnection, Q_ARG(int, 1));
        qInfo() << "  invokeMethod(\"sweep\", Q_ARG(int,1)) 之后累计 =" << radar.sweepCount();
    });
    QTimer::singleShot(300, [] {
        qInfo() << "=== 第 8 讲演示结束，自动退出 ===";
        QCoreApplication::quit();
    });

    return app.exec();
}

#include "08_introspection.moc"

// ============================================================================
// 补：反射的能力边界（工程上你要避开的四类事）
// ----------------------------------------------------------------------------
// 1) **只能反射 Q_OBJECT 登记过的东西**。moc 不展开模板、对复杂类型名容易翻脸
//    （第 5 讲说的 Parse error），所以带模板参数的属性/方法要早做取舍。
// 2) **表里存的是字符串和 QMetaType id，不是 C++ 类型**。自定义类型想按名字传/存，
//    得 Q_DECLARE_METATYPE（qmetatype.h 1400 行附近）+ 必要时 qRegisterMetaType（857 行）。
//    枚举有 Q_ENUM 待遇（名字↔整数表），普通 struct 没有——别指望 write("...") 能解析它。
// 3) **性能**：indexOfMethod 是「对表逐条比签名文本」，invoke 还要 void** 装箱。
//    反射只用于装配期、插件期、低频配置路径；热路径一律函数指针（第 1、5 讲的结论）。
// 4) **索引会随你加一个信号而变**。绝不要把 method(i)/property(i) 的数字写进配置、
//    数据库或跨版本协议；要写就写**名字**，需要版本化就用 Q_REVISION（qtmetamacros.h 63 行）。
// ============================================================================

// ——— 老师傅的话 ———
/*
 * 徒弟，反射这块，新手学完第一反应是「好厉害，我能改任何东西」，第二反应应该是
 * 「那我为什么要用它」。我给你三条判据，比背 API 有用得多：
 *
 *   一、**「按名字」这件事必须由外部输入驱动时，才用反射。**
 *   配置文件的键、QML 的属性绑定、脚本引擎、插件宿主拿到的 objectName、单元测试里
 *   「把被测对象的所有槽遍历一遍」——这些名字不在你的编译单元里，函数指针写不出来，
 *   反射是唯一解。名字来自你自己代码的时候，函数指针版又快又有编译期检查，
 *   用反射纯属自我惩罚。本讲路线 A~D 四条我都跑了给你看，你该偏爱哪条一目了然。
 *
 *   二、**读表要懂 offset，写表要懂「查不到是 -1」。**
 *   methodOffset()/propertyOffset()/enumeratorOffset() 是**分水岭**：之前是父类
 *   （QObject 那一堆 destroyed/objectNameChanged/timerEvent…），之后才是你自己写的。
 *   你 dump 出一堆陌生槽不是代码坏了，是你在读 QObject 的账。另一半纪律是：
 *   indexOfXxx 失败给 -1，而 method(-1)/property(-1) **不做越界检查**——判负数再取值，
 *   这一条能救你一整个晚上的段错误排查。类信息那一段我原本也写着「Qt6 没有公开的
 *   classInfoCount()，只能按名字点查」——去核对 6.12 的 qobjectdefs.h 才发现
 *   classInfoOffset()（254 行）和 classInfoCount()（260 行）都堂堂正正公开声明着，
 *   所以边界齐全的循环是合法的，我在第一节就把这条改掉了。真正别抄的是另一种：
 *   「循环到 name() 返回空」——那是拿越界读赌内部字符串池的布局。
 *   教训比结论值钱：**头文件在手，别人的「没有」也要自己翻一遍。**
 *
 *   三、**qobject_cast 不是 dynamic_cast 的替代，是另一套类型系统。**
 *   它认的是 QMetaObject 的类名字符串链，所以能配 -fno-rtti、能按你登记的 iid 找
 *   非 QObject 接口（Q_DECLARE_INTERFACE），也能明确告诉你「这不是我认的类型」。
 *   代价就是：**只有 Q_OBJECT 参与**，而且失败只给你一个**不报错的空指针**。
 *   我在 review 里拒得最多的一类代码，就是 qobject_cast 之后直接解引用。
 *
 * 顺带把第 5 讲那个「一张表 + 一个 switch」在这里闭环：你现在能同时看到**表的两侧**——
 * moc 生成时的文本（第 5 讲摘录）和运行时读它的代码（本讲的 dumpTables）。
 * 两侧一对齐，你对 Qt 元对象系统就不再是「信它」，而是「知道它在干什么」。
 * 这也是我把反射放在最后一讲的原因：它需要的地基，前七讲刚好铺完。
 */

// ——— 小练习 ———
/*
 *   1. 把 sweep 上的 `Q_INVOKABLE` 删掉重跑：确认它从方法表整条消失、
 *      indexOfMethod 给 -1、路线 A/B 的返回值变 false（Debug 下 invokeMethod 还会断言）。
 *      再给 secretBearing 加上 Q_INVOKABLE，观察它的 returnType()/methodIndex() 变化。
 *   2. 把本讲的 dumpTables() 改造成通用函数 `void introspect(const QObject*)`，
 *      拿 radar 之外的对象各跑一遍（QCoreApplication::instance()、你上一模块任何
 *      QObject 子类）：数一数 QObject 自带几个信号几个槽、offset 分别是多少。
 *      这个数以后你判断「我的类里到底加了多少东西」就有参照了。
 *   3. 【这条练习我在本机真跑过一遍，下面每个数都是从那次运行里抄的】
 *      把 178 行的 `enum class RadarMode {…}` 整条搬进 Radar 类里（放在 211 行那条
 *      Q_PROPERTY 之前，217 行的 Q_ENUM(RadarMode) 原地不动），重新编译再跑：
 *        · 第 3 节「枚举表」：区间从 [0, 0) 变 [0, 1)，多出来那一行是
 *          `[0] RadarMode  isFlag=× isScoped=√ is64Bit=×  取值(Standby=0, Scan=1, Ping=2)`；
 *        · 属性表里 mode 那行：类型名从 `RadarMode` 变成带作用域的 `Radar::RadarMode`，
 *          「枚举/flag」从 × 变 √；
 *        · 第 4 节 mode.write("Scan")：false 变 true，读回 `QVariant(Radar::RadarMode, Radar::RadarMode::Scan)`；
 *        · 第 6.1 节 indexOfEnumerator("RadarMode")：-1 变 0，而那行解释自动换成另一条（if 出来的）。
 *      同一趟里 caps 那行**没变**（还是 ×）：RadarBit 仍在 180 行的全局作用域，
 *      Q_FLAG 走的是同一条不登记的路。一次只搬一个枚举，就只看得到一次变化——控制变量。
 *      搬的时候我真撞到一个错，它跟元对象无关，是 C++ 的名字可见性：
 *        · 枚举一进类就落在默认 private 段（写在 Q_OBJECT 和 public: 之间就是这样），
 *          类外那句 `radar.mode() == RadarMode::Scan` 当场报
 *          `'enum class Radar::RadarMode' is private within this context`
 *          （note 那行是 `declared private here`）→ 要么补 `public:`，要么类外一律写全名。
 *          这就是「把枚举搬进类里」在真实工程里的成本：**所有裸用的调用点**都要跟着改。
 *      第 1 节①号摆法记的另外两个坑（Q_ENUM 要在枚举**之后**、类内联体里的竖线），
 *      我这次一个都没撞上：只搬 RadarMode、没动 RadarBit，宏的位置也没变。
 *      想撞就把 RadarBit 也搬进类里——那时 caps() 里的按位组合就得改成显式构造。
 *      反过来再做一遍（把 alarm 的 Q_ENUM_NS(Level) 删掉）：6.1 那行【注意】会打印出来，
 *      而且 moc 这次照样一声不吭。我拿当前这份直接喂 `moc`：退出码 0、stderr 一个字节都没有。
 *      moc 只在**缺 Q_NAMESPACE** 时报错（「Namespace declaration lacks Q_NAMESPACE macro」），
 *      对「宏没登记任何枚举」什么都没有——这就是本讲为什么把空表的计数打出来给你看。
 */
// ============================================================================
