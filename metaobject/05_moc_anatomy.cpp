// ============================================================================
// 第 5 讲 · moc 解剖：Q_OBJECT 到底给你生成了什么
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   前四讲你在**用**信号槽；这一讲把引擎盖掀开，看清楚「声明一个信号」和
//   「moc 交给你一张表 + 一个 switch + 一段发射体」之间的对应关系。
//     1) moc 是**编译前的独立程序**，不是 C++ 宏展开：它读源码、另写一个 .cpp
//     2) Q_OBJECT 只**声明**（qtmetamacros.h 里那十几行），实现体全在 moc 产物里
//     3) 产物五件套：字符串池 + 元数据数组 + qt_static_metacall + metaObject()/
//        qt_metacast/qt_metacall + **每个信号一个实现体**
//     4) SIGNAL()/SLOT() 宏为什么带一个数字前缀（这个坑 99% 的人讲不清楚）
//        ★ 注意：**带前缀的只有宏的输出串，元对象表的字符串池里没有前缀**（见第 8 节，实测）
//     5) 名字 → 索引 → switch 的派发链，以及它和直接函数调用的性能差
//   ★ 本讲两样东西都在本机跑过：moc 产物（第三节摘录，逐字抄）+ 本文件的运行日志（见下）。
//
// 对应构建目标名：metaobject_05_moc_anatomy
//   add_executable(metaobject_05_moc_anatomy 05_moc_anatomy.cpp)
//   target_link_libraries(metaobject_05_moc_anatomy PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)
//
// 运行前须知：
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 metaobject_05_moc_anatomy，跑到自动退出、退出码 0）。
//     但本讲**没有汇总自检行**，退出码 0 只证明「没崩、走完了」，不证明正文每个数字都对：
//     带具体数字的「预期」请以你自己那次运行输出为准（本机那一轮存档 runlogs/metaobject_05_moc_anatomy.log）。
//   · 只建 QApplication 不显示窗口；结论全部 qInfo()；QTimer 驱动 + 自动 quit()。
//   · 【曾经写错的一条，现已改对】第 4 节那行「故意 invokeMethod 一个不存在的方法名」
//     **不会**断言、不会崩，任何构建类型都一样：它只是 qWarning 一句然后返回 false。
//     依据（本机实测，两条都算）：
//       ① 源码：qmetaobject.cpp 1715 行「查不到」这条路直接
//          `return printMethodNotFoundWarning(...)`；该函数体在 1495~1528 行，
//          只做一件事——qWarning（1523 行）然后 `return false`（1527 行）。
//          invokeMethodImpl 里唯一的三个 Q_ASSERT（1686~1688 行）断的是
//          paramCount/parameters/typeNames 这三个**必然成立**的入参，跟「名字找不到」无关。
//       ② 跑：拿一个只有 `void real()` 槽的类，在**没定义 QT_NO_DEBUG** 的目标文件里
//          调 invokeMethod("noSuchMethod") →（注意闸门是 QT_NO_DEBUG 而不是 NDEBUG，
//          本机 qassert.h 第 28~30 行：`#if defined(QT_NO_DEBUG) && !defined(QT_FORCE_ASSERTS)`
//          才把 Q_ASSERT 展开成空表达式）
//          打印 `QMetaObject::invokeMethod: No such method Box::noSuchMethod()`、
//          返回 false、进程退出码 0。
//     所以这一行**不再注释**，本讲把它当正面证据跑给你看（第 4 节）。
//     顺手把另一件事分清楚：connect 那条「参数个数关系」的检查**不是运行期断言**——
//     本机 qobject.h 第 257 行写的是 static_assert，编译期就把你拦下，压根到不了运行期。
//     运行期真会当场断掉的只有第 2 讲那条 `Qt::UniqueConnection` + lambda 的 Q_ASSERT_X
//     （qobject.h 第 268 行），而且**只在没定义 QT_NO_DEBUG 的构建里**：本仓库是 Release，
//     这句被编译掉，改由 qobject.cpp 第 5578~5581 行打一句 qWarning 再返回无效 Connection
//     ——两种脸第 2 讲第 5 节各跑了一遍，别把它们混成一种。
//   · 本机校验状态（照实说）：本讲**已在本机编译并运行**，退出码 0
//     （build 目录是 Release，`cmake --build build --target metaobject_05_moc_anatomy -j 6`；
//      跑法：`cd build/bin && QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1
//      ./metaobject_05_moc_anatomy.exe 2>&1 | iconv -f GBK -t UTF-8`，Windows 上 Qt 日志是 GBK）。
//     真跑过的两件事（脚本在仓库 _tools/check_syntax.sh）：
//       moc.exe → 退出码 0、无 stderr、产物 236 行（第三节的摘录就是逐字从它抄的，
//                  仓库里那份 05_moc_output.txt 与本次重跑**逐字节相同**，diff 无输出）；
//       g++.exe -fsyntax-only -std=c++17 -Wall -Wextra → 0 error、0 warning；
//       并且**已经真实编译、链接、运行**（Qt 6.12.0 + MinGW 13.1.0，offscreen，
//       ctest 目标 metaobject_05_moc_anatomy 通过、退出码 0，本机那一轮存档
//       runlogs/metaobject_05_moc_anatomy.log）。
//     下面这些行号是本机 6.12.0 头文件里 grep 出来的，不是我按记忆写的：
//       QtCore/qtmetamacros.h        —— QT_META_OBJECT_VARS 124 行；QT_OBJECT_GADGET_COMMON
//                                     133~136 行；Q_OBJECT 139~152 行（143 行那句
//                                     `static const QMetaObject staticMetaObject;` 就是本讲
//                                     一直在说的「表」的声明）；Q_MOC_OUTPUT_REVISION 17 行
//       QtCore/qobjectdefs.h         —— QMETHOD_CODE/QSLOT_CODE/QSIGNAL_CODE 41~43 行；
//                                     QLOCATION 49 行；SLOT/SIGNAL 宏 53~54 行（debug 分支）
//                                     与 59~60 行（release 分支）；
//                                     activate 三个非模板重载 310 / 311 / 312 行 +
//                                     变参模板版 313~321 行；
//                                     invokeMethod 字符串版四个重载 324 / 338 / 355 / 372 行、
//                                     functor 版 441~452 行；struct Data 614~623 行
//       QtCore/qmetaobject.h         —— QMetaMethod::MethodType 42~47 行；methodType() 48 行；
//                                     QMetaEnum 272 行起：isScoped 282 / keyCount 285 /
//                                     valueToKey(quint64) 300 / enclosingMetaObject 303 /
//                                     isValid 305 / fromType<T>() 307~316 行；
//                                     QMetaProperty 341 行起
//       QtCore/qtmocconstants.h      —— OutputRevision = 13（33 行）；PropertyFlags 35 /
//                                     MethodFlags 56 / EnumFlags 94~98（EnumIsFlag 0x1、
//                                     EnumIsScoped 0x2、EnumIs64Bit 0x40）；
//                                     文件头 11 行明写「This file is not part of the Qt API」
//       QtCore/qtmochelpers.h        —— PropertyData 291 行 / EnumData 309 行（332 行把
//                                     EnumIsScoped or 进 header[2]）/ MethodData 452 行 /
//                                     SignalData 457 行 / SlotData 462 行
//       qtbase 源码（本机 D:/Qt/6.12.0/Src）——
//                                     src/corelib/kernel/qobject.cpp 2667~2671 行 extract_code、
//                                     3142 / 3168 行「++signal / ++method  // skip code」
//                                     （**类型码前缀只活在宏串里**，第 8 节的依据）；
//                                     4486 行 `signal_index = local_signal_index +
//                                     QMetaObjectPrivate::signalOffset(m)`（第 5 节「局部索引」的依据）；
//                                     src/corelib/kernel/qmetaobject.cpp 1690~1715 行
//                                     invokeMethodImpl 查名字那一段（1702 行拿**裸名**比池子条目）；
//                                     src/tools/moc/generator.cpp 282~284 行 + 851~854 行
//                                     （**方法表按类别分组**的依据，见第 3 节）
//     moc 可执行文件：D:/Qt/6.12.0/mingw_64/bin/moc.exe —— `--version` 自报 "moc 6.12.0"，
//     产物头部（05_moc_output.txt 第 4 行）写 "The Qt Meta Object Compiler version 69 (Qt 6.12.0)"。
// ============================================================================

#include <QApplication>
#include <QByteArray>
#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaProperty>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QDebug>

// ============================================================================
// 一、两个「写错就编译不过」的反例（整段注释掉，本讲跑的是后面那个正确的 Lever）
// ----------------------------------------------------------------------------
// moc 是**编译前**的独立程序，所以它报的错长得像语法错但不归编译器管（第 1 讲那句
// 「moc 找不到实现」在这里升级成「moc 连解析都没走完」）。两个典型：
//
//   ① 关键字与值之间漏了空格 / 关键字拼错：
//        class Switch : public QObject {
//            Q_OBJECT
//            Q_PROPERTY(int level READ level WRITE setLevel NOTIFYStateChanged_UNUSED)
//        };
//      → moc 把 `NOTIFYStateChanged_UNUSED` 当一个整体，直接 Parse error。
//      Q_PROPERTY 的关键字**大小写敏感**、必须有空白分隔；本机 moc 实测只认这些：
//        READ WRITE RESET MEMBER NOTIFY DESIGNABLE SCRIPTABLE STORED USER
//        CONSTANT FINAL REQUIRED BINDABLE
//      （ACCESS / CATEGORY / INVALID / BINDING / STYLE_INFORMATION 全部 Parse error——
//        它们是 Q_PROPERTY 文档里出现过的词，但这个版本的 moc 不接，别照旧书抄。第 6 讲有实测）
//
//   ② 把槽关键字当「成员标签」用：
//        void reset() Q_SLOTS   // 【编译不过】
//      Q_SLOTS / slots 是**访问区开关**（qtmetamacros.h 44~49 行，展开成 `public slots:`
//      那种形态），必须单独占一行去圈一批成员，写在成员后面是语法错误。
//      想给单个成员开「可按名字调用」，用的是 Q_INVOKABLE；想给单个成员标记类型，
//      没有这种语法——槽的身份来自它所在的那个区。
//
// 这两处都是「一字母/一位置之差，损失一次编译」。本讲下面的 Lever 就是把它们改对之后
// 的版本，也是本模块**唯一在本机真跑过 moc** 的那个文件。
// ============================================================================

// ============================================================================
// 二、被解剖的对象：故意把每种元信息各放一个，好让 moc 的每一段都被生成出来
// ============================================================================
class Lever : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int level READ level WRITE setLevel NOTIFY levelChanged USER true)
public:
    enum class State { Off, On, Blinking };
    Q_ENUM(State)

    explicit Lever(int id, QObject *parent = nullptr) : QObject(parent), m_id(id) {}

    Q_INVOKABLE int press(int times)
    {
        m_presses += times;
        Q_EMIT pressedOut(m_presses, m_id);
        return m_presses;
    }
    void pull() { Q_EMIT flipped(); }              // 普通成员函数，**不是**槽

public Q_SLOTS:
    void reset() { m_presses = 0; }                // 无参槽
    void onLevelChanged(int level)                 // 带参槽（名字故意跟信号像）
    {
        ++m_slotHits;
        m_lastLevel = level;
    }

    int presses() const { return m_presses; }
    int slotHits() const { return m_slotHits; }
    int lastLevel() const { return m_lastLevel; }
    int level() const { return m_level; }
    void setLevel(int v)
    {
        if (v == m_level) return;
        m_level = v;
        Q_EMIT levelChanged(m_level);
    }
    State state() const { return m_state; }
    void setState(State s) { m_state = s; }

Q_SIGNALS:
    void pressedOut(int total, int id);
    void levelChanged(int level);
    void flipped();

private:
    int m_id = 0;
    int m_presses = 0;
    int m_slotHits = 0;
    int m_lastLevel = -1;
    int m_level = 0;
    State m_state = State::Off;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    Lever lever{7};

    qInfo() << "=== 1. 两个「版本号」不是一回事 ===";
    // Q_MOC_OUTPUT_REVISION：**moc 工具**的输出格式版本，只用来做「产物 vs 头文件」匹配检查。
    //   它就定义在 qtmetamacros.h 第 17 行，注释明写 "should be in sync with moc's outputrevision.h"。
    // 生成的 .moc 头部有 6 行 #if/#error 拿它对线；头文件没 include <QObject> 时
    //   它没定义 → 直接报 "doesn't include <QObject>"（这条错误信息在本讲第三节摘录的头部就能看到）。
    qInfo() << "  Q_MOC_OUTPUT_REVISION =" << Q_MOC_OUTPUT_REVISION
            << "（qtmetamacros.h 17 行；本机 moc 自报 'version 69 (Qt 6.12.0)'，两边必须相等）";
    // 另一个是**数据格式** revision：qtmocconstants.h 第 33 行 `enum { OutputRevision = 13 }`。
    // 它给 moc / QMetaObjectBuilder 用。★ 勘误：旧稿在这里还列了「QMocCreator」，  name-ok:QMocCreator ★ 勘误段落复述旧错名
    // 本机 qtbase 全树 grep 是 0 处——moc 自己的类就叫 `Moc : public Parser`
    // （src/tools/moc/moc.h 225 行），没有 QMocCreator 这个名字。  name-ok:QMocCreator 同上
    // 注意 Qt6 的 QMetaObject 结构体里
    // **已经没有 revision 字段了**（qobjectdefs.h 614~623 行那个 `struct Data` 七个成员，
    // 第一个是 superdata；Qt5 时代第一个字段是 revision）—— 我在本机对 qobjectdefs.h
    // 跑了 `grep -i revision`：**零命中**，连一个残留字段都没有。别按 Qt5 的记忆去数偏移量。

    qInfo() << "=== 2. staticMetaObject 是个静态常量，metaObject() 只是在它和动态表之间二选一 ===";
    qInfo() << "  &Lever::staticMetaObject      =" << &Lever::staticMetaObject;
    qInfo() << "  lever.metaObject()            =" << lever.metaObject();
    qInfo() << "  两者相等？" << (&Lever::staticMetaObject == lever.metaObject() ? "是" : "否")
            << " ← 因为 moc 生成的函数体就一句三元表达式：\n"
            << "     return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;";
    // superClass() 沿继承链往上走，**走到 QObject 就返回 nullptr**——所以链第二次必须判空。
    // （这里原来直接写 mo->superClass()->superClass()->className()，QObject 那一层的
    //   superClass() 是 nullptr，解引用立刻段错误。反射遍历的通用形状就是 while (mo) 往下挪。）
    {
        const QMetaObject *up1 = lever.metaObject()->superClass();
        const QMetaObject *up2 = up1 ? up1->superClass() : nullptr;
        qInfo() << "  className() =" << lever.metaObject()->className()
                << " superClass() =" << (up1 ? up1->className() : "(nullptr)")
                << " 再往上 =" << (up2 ? up2->className() : "(nullptr，到顶了)")
                << "（superClass() 到顶返回 nullptr，链之前必须判空）";
    }
    // 顺带一个 Q_OBJECT 的硬约束（qtmetamacros.h 145~148 行）：它声明的
    // metaObject/qt_metacast/qt_metacall 三个函数是 **virtual** 的。
    // 所以「继承 QObject 但**不写** Q_OBJECT」的类不会重写这三个函数——
    // 它的 metaObject() 返回**基类**的表，于是该类的信号连不上、属性找不到、qobject_cast 失败。

    qInfo() << "=== 3. 字符串池与方法表：把 moc 排的序原样打出来 ===";
    const QMetaObject *mo = lever.metaObject();
    qInfo() << "  methodOffset() =" << mo->methodOffset()
            << "（= 所有父类累计的方法条数；Lever 直接继承 QObject，所以就是 QObject 那张表的条数。"
            << "qt_metacall 里「先把 _id 交给父类、再减掉自己的条数」那套索引平移靠的就是它）";
    qInfo() << "  methodCount() =" << mo->methodCount()
            << " propertyCount() =" << mo->propertyCount()
            << " enumeratorCount() =" << mo->enumeratorCount();
    auto typeName = [](QMetaMethod::MethodType t) -> const char * {
        switch (t) {
        case QMetaMethod::Signal:      return "信号";
        case QMetaMethod::Slot:        return "槽";
        case QMetaMethod::Constructor: return "构造";
        case QMetaMethod::Method:      return "普通方法";
        }
        return "未知";
    };
    // 【Qt6/Qt7 差异】枚举本体：qmetaobject.h 42~47 行
    //   enum MethodType { Method = QMETHOD_CODE, Signal, Slot, Constructor = 3 };
    // Qt6 里 Signal=1、Slot=2；qobjectdefs.h 41~43 行那三个码是 QMETHOD=0/QSLOT=1/QSIGNAL=2；
    // 而 QT7_ONLY 又把 Signal/Slot 改成 QSIGNAL_CODE=8 / QSLOT_CODE=4。
    // → 规矩：**永远写枚举名，永远不要把 methodType() 的数字存进配置或数据库**。
    for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {
        const QMetaMethod mm = mo->method(i);
        qInfo().noquote() << QStringLiteral("    [%1] 局部索引 %2  %3%4  参数%5个  返回QMetaType::Int=%6  access=%7")
                                 .arg(i)
                                 .arg(i - mo->methodOffset())
                                 .arg(QString::fromLatin1(mm.name()))
                                 .arg(QStringLiteral("（%1）").arg(typeName(mm.methodType())))
                                 .arg(mm.parameterCount())
                                 .arg(mm.returnType() == QMetaType::Int ? "是" : "否")
                                 .arg(mm.access() == QMetaMethod::Public ? "Public" : "其他");
    }
    // 【取证：方法表到底是按什么排的】把上面这屏和第二节那段源码对读，逐条核对：
    //   源码书写顺序是  press(Q_INVOKABLE) → pull → reset → onLevelChanged → 五个 getter
    //                   → setState → pressedOut → levelChanged → flipped；
    //   表里的顺序却是   pressedOut 0 / levelChanged 1 / flipped 2
    //                   / reset 3 … setState 11 / press 12。
    //   **press 明明写在 reset 前面，却排到 12** —— 所以「顺序 = 源码书写顺序」是错的（我上一版就这么写错）。
    //   真实规则是 moc **按类别分组**输出，组内才保持源码顺序：
    //       信号（Q_SIGNALS） → 槽（slots 区里的全部成员） → Q_INVOKABLE（Method） → 构造函数
    //   依据（本机 moc 源码，不是我推测的）：
    //     qtbase/src/tools/moc/generator.cpp
    //       282~284 行：addFunctions(cdef->signalList, "Signal");
    //                   addFunctions(cdef->slotList,   "Slot");
    //                   addFunctions(cdef->methodList,  "Method");   ← 三个循环，一段接一段
    //       851~854 行：methodList += signalList; += slotList; += methodList;
    //                   （qt_metacall 里那个 `_id -= 13` 的 13 就是这条拼接的总长）
    //       521~522 行：发射体的第三个实参 = 该信号在 signalList 里的下标
    //                   （所以信号的「局部索引」既等于组内下标、也等于方法表下标——因为信号那组排最前面）
    //   解析阶段就把三类塞进三个不同的表：moc.h 202 行
    //     `QList<FunctionDef> signalList, slotList, methodList, publicList;`
    // 【这一组结论跟着一起改】
    //   ① pull()/state-change 之外的普通成员函数（pull()）根本不在表里——没登记就没法反射（见②）；
    //   ② 表里**没有**「Q_INVOKABLE 是权限」这回事：press 的身份是 Method，
    //      和它在源码里写在哪一段、是不是 public 无关，只和「有没有那个标记」有关；
    //   ③ 五个 getter 因为写在 `public Q_SLOTS` 区里，全被登记成**槽**（局部索引 5~10）——
    //      「槽」只是组名标签，不是特殊调用约定；
    //   ④ 信号即使声明在 Q_SIGNALS（= public，qtmetamacros.h 49 行）区，access 也是 Public，
    //      第 1 讲那句「槽是普通成员函数」在这里再验一遍。
    //   ⑤ 推论（很重要）：**别把数字索引写进任何配置**。加一个信号，槽那一批的索引全体后移一位。

    qInfo() << "=== 4. 名字 → 索引 → switch：invokeMethod 就是照着这张表查 ===";
    // 这条路和 connect(&Lever::press) 走的**不是**同一条：函数指针版编译期就把调用点定死了，
    // 字符串版是「查表 + void** 取参 + switch 派发」，多一次 strcmp 和一次装箱。
    int ret = -1;
    const bool okPress = QMetaObject::invokeMethod(&lever, "press",
                                                   Q_RETURN_ARG(int, ret), Q_ARG(int, 3));
    qInfo() << "  invokeMethod(\"press\", Q_ARG(int,3)) 成功？" << okPress << " 返回 =" << ret;
    const bool okReset = QMetaObject::invokeMethod(&lever, "reset");
    qInfo() << "  invokeMethod(\"reset\") 成功？" << okReset << " presses() 现在 =" << lever.presses();
    // 【反面教材·运行期才失败，但**不崩**】下面这行是本讲故意留下的失败样本：
    const bool okNoSuch = QMetaObject::invokeMethod(&lever, "noSuchMethod");
    qInfo() << "  invokeMethod(\"noSuchMethod\") 成功？" << okNoSuch
            << " ← 上一行会打出 Qt 自己的 qWarning（No such method + Candidates）";
    // 实测（本机 Release 跑，退出码 0）：控制台多出一行
    //   QMetaObject::invokeMethod: No such method Lever::noSuchMethod()
    // 然后本行返回 false。程序继续往下走，**没有任何断言**。
    // 我上一版把这里写成「Debug 构建会走 Q_ASSERT 当场断掉」，那是**照 Qt5 博客抄的**，
    // 在本机 6.12.0 上不成立：qmetaobject.cpp 1715 行查不到就走 printMethodNotFoundWarning，
    // 那个函数（1495~1528 行）只有 qWarning + return false，里面一个 Q_ASSERT 都没有。
    // 这就是「字符串宏 SIGNAL()/SLOT() 丢失编译期检查」的具体代价（第 2 讲已经埋过一次）：
    // 代价是**静默失败**（返回值 false + 一行 warning，很多人不看返回值），不是崩。
    const int pressIdx = mo->indexOfMethod("press(int)");
    qInfo() << "  indexOfMethod(\"press(int)\") =" << pressIdx;
    // 【读表时的差异，别搞混——我上一版这里写反了】
    //   字符串池里存的是**裸名字** `"press"`（第 3.2 节实测摘录 19 号条目就是 `"press"`，
    //   既没有 `'0'` 前缀，也没有参数表），参数类型另存在条目的类型栏里；
    //   带类型码前缀（'0'/'1'/'2'）的是 **SIGNAL()/SLOT() 宏拼出来的那根串**（第 8 节）。
    //   所以查表时你传 `"press(int)"`（归一化签名、无前缀），Qt 内部拿裸名 + 类型表去比：
    //   qmetaobject.cpp 1702 行那句 `name != stringDataView(meta, m.data.name())` 就是证据——
    //   它比的是 `data.name()` 指向的**名字条目本身**，压根不看前缀。
    //   前缀唯一被读出来的地方在 qobject.cpp 2667~2671 行 extract_code()，
    //   紧接着 3142 / 3168 行两句 `++signal; // skip code`、`++method; // skip code` 就把它丢了。
    int ret2 = -1;
    bool okMeta = false;
    if (pressIdx >= 0) {                        // 查不到就别构造 QMetaMethod，先判负数
        const QMetaMethod mPress = mo->method(pressIdx);
        okMeta = mPress.invoke(&lever, Qt::DirectConnection,
                               Q_RETURN_ARG(int, ret2), Q_ARG(int, 4));
    }
    qInfo() << "  换 QMetaMethod::invoke 再派一次：ok =" << okMeta << " ret2 =" << ret2
            << " presses() =" << lever.presses();

    qInfo() << "=== 5. 发射体：Q_EMIT 展开成空，真正干活的是 moc 写的那十几行 ===";
    int hits = 0;                               // 捕获引用计数；和 lever 同在 main 作用域，同生共死
    // 第 4 讲的规矩在这儿照用：lambda 一律带 context（这里 context 就是发射者自己，
    // 因为二者寿命相同，所以是**安全**的偷懒，不是通用写法）。
    QObject::connect(&lever, &Lever::pressedOut, &lever, [&hits](int total, int id) {
        ++hits;
        qInfo() << "    [lambda] total =" << total << " id =" << id;
    });
    Q_EMIT lever.pressedOut(100, 7);           // 你写的（signals 区是 public，所以外部也能发——
                                               //  这是 Qt6 的既定事实，不是 bug，第 1 讲已核对）
    // 编译器实际看到的：lever.pressedOut(100, 7) → 下面这个由 moc 生成的函数体
    //   void Lever::pressedOut(int _t1, int _t2) { QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2); }
    //                                       ↑ 局部索引是 **0**（本机实测产物 05_moc_output.txt 219~223 行）
    // 三个参数分别在说：
    //   &staticMetaObject → 「以**我**这张表为准」；
    //   局部索引 0        → 不是全局索引！全局索引 = 0 + methodOffset()（本机实测 = 4），
    //                       也等于「它是本类第一个信号」——因为 moc 把信号那一组排在方法表最前面
    //                       （依据见第 3 节：generator.cpp 521~522 行取的是 signalList 的下标）。
    //                       qt_metacall 里那个「减掉父类条数」是它的逆运算；
    //                       另一头 qobject.cpp 4486 行 `signal_index = local_signal_index
    //                       + QMetaObjectPrivate::signalOffset(m)` 是它的正向换算。
    //   nullptr           → 返回值槽（信号返回值恒 void，这里给空指针）。
    // 参数怎么送进去的？看 qobjectdefs.h 312~321 行那个变参模板：它把 args 逐个
    // std::addressof 成一个 void* 数组 `_a[]`，再调 309~311 行那个非模板 activate。
    // **_a[0] 放的是返回值地址**——这就是为什么 Qt5 时代「信号带返回值」会在很奇怪的
    // 地方崩：_a[0] 被元类型系统当成 ret，而 moc 的 switch 里 `case 0: _t->pressedOut(...); break;`
    // 直接把返回值丢了。Qt6 依然允许你声明非 void 信号（本机 moc 实测照收不误），
    // 但「值拿不到」，所以 void 是**约定**，不是编译器强制。
    qInfo() << "  命中次数 =" << hits << "（证明 activate 确实遍历了接收者列表）";
    // 对照：无参信号 flipped() 的发射体没有模板实参、也没有尾随参数
    //   void Lever::flipped() { QMetaObject::activate(this, &staticMetaObject, 2, nullptr); }
    // ——它调的是 qobjectdefs.h 310 行那个 (sender, mo, local_index, argv) 重载。
    //   flipped 的局部索引是 **2**（三个信号 pressedOut/levelChanged/flipped 里的第三个），
    //   不是 4：我上一版把「全局索引」和「局部索引」混着写，两处都错过一次。
    // 把三个信号的局部索引在这里钉成一屏证据（和 05_moc_output.txt 尾部 // SIGNAL 0/1/2 一一对应）：
    for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {
        const QMetaMethod mm = mo->method(i);
        if (mm.methodType() != QMetaMethod::Signal) continue;
        qInfo().noquote() << QStringLiteral("    发射体第三个实参（局部索引）= %1  ← %2")
                                 .arg(i - mo->methodOffset())
                                 .arg(QString::fromUtf8(mm.methodSignature()));
    }

    qInfo() << "=== 6. qt_metacast 就是 strcmp 串表；qobject_cast 靠它（第 8 讲细说） ===";
    qInfo() << "  qt_metacast(\"Lever\")   →" << (lever.qt_metacast("Lever") != nullptr ? "命中" : "空");
    qInfo() << "  qt_metacast(\"QObject\") →" << (lever.qt_metacast("QObject") != nullptr ? "命中（父类链继续找）" : "空");
    qInfo() << "  qt_metacast(\"QWidget\") →" << (lever.qt_metacast("QWidget") != nullptr ? "命中" : "空");
    // 生成体（本机实测）就三步：!_clname 返空；strcmp 命中**本类名**就 static_cast<void*>(this)；
    // 否则 return QObject::qt_metacast(_clname) 往父类递。整张表里只放了**一条**类名，
    // 所以「跨模块的接口类型」cast 不中。★ 勘误：旧稿在这里写「那是 Qt7 的
    // Q_REVISION/QT_REVISION 才管的事」——`Q_REVISION` 是真宏（qabstractitemmodel.h 等处在用，  name-ok:QT_REVISION 本行正在说 QT_REVISION 不存在（Q_REVISION 是真名）
    // 管的是**带版本的信号/槽**，不是 cast），而 `QT_REVISION` 本机 qtbase 全树 grep 是 0 处。  name-ok:QT_REVISION 同上
    // 【注意】qt_metacast 是 Q_OBJECT 声明的 **virtual** 函数（qtmetamacros.h 147 行），
    // 手写一份在类里会跟 moc 生成的重名冲突——别覆写它，想扩展行为去用 Q_DECLARE_INTERFACE/qobject_cast。

    qInfo() << "=== 7. 属性表 / 枚举表：一句话预告第 6、8 讲 ===";
    const int pi = mo->indexOfProperty("level");
    qInfo() << "  indexOfProperty(\"level\") =" << pi
            << " propertyOffset() =" << mo->propertyOffset();
    const QMetaProperty mp = mo->property(pi);
    qInfo() << "  mp.name() =" << mp.name() << " isWritable =" << mp.isWritable()
            << " hasNotifySignal =" << mp.hasNotifySignal()
            << " isUser =" << mp.isUser()          // ← Q_PROPERTY 里那个 USER true 落在了这里
            << " notifySignal().name() =" << QString::fromLatin1(mo->method(mp.notifySignalIndex()).name());
    qInfo() << "  userProperty() =" << QString::fromLatin1(mo->userProperty().name())
            << "（第一个 USER=true 的属性；没有就退化成第 0 个属性）";
    const QMetaEnum me = mo->enumerator(mo->indexOfEnumerator("State"));
    qInfo() << "  枚举 State：keyCount =" << me.keyCount() << " isFlag =" << me.isFlag()
            << " isScoped =" << me.isScoped()      // qmetaobject.h 282 行
            << " valueToKey(1) =" << QString::fromLatin1(me.valueToKey(1));

    qInfo() << "=== 8. SIGNAL()/SLOT() 宏的「真身」：一位数字前缀 + 藏起来的文件行号 ===";
    // 这节是全模块最「原来如此」的一处。qobjectdefs.h 41~43 行定义了三个类型码：
    //   QMETHOD_CODE 0 / QSLOT_CODE 1 / QSIGNAL_CODE 2
    // 53~54 行（QT_DEBUG 分支）把宏定义成：
    //   SLOT(a)   = qFlagLocation( "1" "a"  "\0" __FILE__ ":" __LINE__ )
    //   SIGNAL(a) = qFlagLocation( "2" "a"  "\0" __FILE__ ":" __LINE__ )
    // 于是 SIGNAL(pressedOut(int,int)) 展开出来的**不是** "pressedOut(int,int)"，
    // 而是带一个 '2' 前缀的字节串；后面还跟了个 '\0' 和「文件名:行号」，
    // 供调试器/Qt Creator 的连接视图反查是谁连的（qFlagLocation 声明在 39 行，非 debug 分支
    // 59~60 行就只剩前缀 + 签名，没有位置串）。
    // 【关键更正：前缀**只在宏串里**，字符串池里没有】我上一版写的是「这条前缀正是元对象表
    // 字符串池里方法签名存的样子」，并抄了一行 `qt_stringData{ "Lever", "2levelChanged", ... }`
    // ——**本机产物里根本没有这回事**。逐字看第三节 3.2（05_moc_output.txt 39~64 行）：
    //   QtMocHelpers::StringRefStorage qt_stringData{ "Lever", "pressedOut", "", "total", "id",
    //                                                 "levelChanged", "level", "flipped", "reset",
    //                                                 "onLevelChanged", ... }
    // 全是**裸名字**，一个数字前缀都没有；连参数表都不在池子里（池子只存名字，类型另存在条目里）。
    // 那前缀活在哪儿？就活在上面宏展开出来的这根串里，而且**读一眼就被扔掉**：
    //   qobject.cpp 2667~2671 行 extract_code()   —— 取首字符减 '0' 当类型码；
    //   qobject.cpp 3142 行 `++signal; // skip code`
    //   qobject.cpp 3168 行 `++method; // skip code` —— 跳掉首字符，剩下的才是拿去查表的签名。
    // 所以「字符串版 connect 为什么能工作」的正确说法是：
    //   宏拼出 "<类型码><签名>" → **extract_code 读类型码（用来区分 SLOT/SIGNAL 两种查法）、
    //   然后把类型码丢掉** → 拿裸签名去池子边上比对。类型码决定的是「用 indexOfSlot 还是
    //   indexOfSignal 那条查法」（qobject.cpp 3176~3185 行那个 switch），**不是**池子里的键。
    // 【工程结论】不变，但理由换了：**永远用 SIGNAL()/SLOT() 宏去生成这个串，别手搓裸签名**：
    //   connect(a, "pressedOut(int,int)", b, "onX(int)")   ← 前缀是你自己猜的，猜错就静默失败
    // 为什么一定失败，本机现在有实现可看（不再只能引文档）：qobject.cpp 3132 行
    // `check_signal_macro(...)` 先验信号侧前缀必须是 '2'，3136 行 `check_method_code(...)`
    // 验槽侧必须是 '1' 或 '2'，任何一边不对就 qCWarning + 返回**无效 Connection**（3133/3137 行）。
    // 也就是说裸签名（'p' 开头 → extract_code 算出乱七八糟的码）连查表那一步都进不去。
    // 规矩照旧：「要么用宏，要么用函数指针版」。
    qInfo().noquote() << "  QByteArray(SIGNAL(pressedOut(int,int))) ="
                      << QByteArray(SIGNAL(pressedOut(int,int)));
    qInfo().noquote() << "  QByteArray(SLOT(onLevelChanged(int)))   ="
                      << QByteArray(SLOT(onLevelChanged(int)));
    // 位置串在 '\0' **后面**，所以 QByteArray 只截到前缀+签名。想把它捞出来必须
    // 手动跳过那个终止符（qstrlen 见 qbytearrayalgorithms.h 180 行，就是 strlen 的空指针安全版）。
    // 【必须分编译模式】release 分支（59~60 行）**没有**位置串，硬跳 '\0' 会读到字符串字面量
    // 之外的内存——这就是未定义行为，所以这段用和头文件一模一样的条件包起来。
#if !defined(QT_NO_DEBUG)
    const char *withLoc = SIGNAL(flipped());
    qInfo().noquote() << "  strlen 截到 =" << QByteArray(withLoc)
                      << "；'\\0' 之后藏的是 =" << QByteArray(withLoc + qstrlen(withLoc) + 1);
#else
    qInfo().noquote() << "  QT_NO_DEBUG 分支（qobjectdefs.h 59~60 行）只剩签名，没有位置串："
                      << QByteArray(SIGNAL(flipped()));
#endif

    // ── 自动脚本 ──────────────────────────────────────────────────────────
    QTimer::singleShot(120, [&lever] {
        qInfo() << "=== 9. 事件循环里再发一次：查表派发完全一样 ===";
        Q_EMIT lever.levelChanged(42);         // 等价于 lever.setLevel(42) 里那句
        qInfo() << "  lever.lastLevel()（没连槽，所以不变） =" << lever.lastLevel();
        QObject::connect(&lever, &Lever::levelChanged, &lever, &Lever::onLevelChanged);
        lever.setLevel(9);
        qInfo() << "  连上槽之后再 setLevel(9)：slotHits =" << lever.slotHits()
                << " lastLevel =" << lever.lastLevel();
    });
    QTimer::singleShot(300, [] {
        qInfo() << "=== 第 5 讲演示结束，自动退出 ===";
        QCoreApplication::quit();
    });

    return app.exec();
}

#include "05_moc_anatomy.moc"

// ============================================================================
// 三、moc 到底生成了什么（★ 本机实测摘录 ★）
// ----------------------------------------------------------------------------
// 命令（在本目录下；本机可用的 moc 是 Qt 官方 MinGW 套件那一个）：
//   D:/Qt/6.12.0/mingw_64/bin/moc.exe -I ../ -I . 05_moc_anatomy.cpp -o 05_moc_output.txt
//   → 退出码 0、无 stderr；产物 **236 行**（`wc -l` 实测，`moc.exe --version` = moc 6.12.0）。
//   仓库里那份 05_moc_output.txt 我就是用这条命令生成的，再 diff 一次是**零差异**，
//   所以下面的摘录可以直接当证据用，不需要你重跑。
// 下面 3.1~3.8 是从那 236 行里**逐字**截出来的（只在行前加 `// `、行尾加注解，
// 省略处标了 `...`）。我第一版这里凭印象写错过两处（`EnumData<Lever::State>` 和
// `&Lever->onLevelChanged`），现在这两处都按本机输出改回来了——
// 生成物的细节，**跑一次比问任何人可靠**，包括问我。
// ============================================================================
// ── 3.1 起手：版本对不上就 #error，然后是一个空 tag 类型 ─────────────────────
// #if !defined(Q_MOC_OUTPUT_REVISION)
// #error "The header file '05_moc_anatomy.cpp' doesn't include <QObject>."
// #elif Q_MOC_OUTPUT_REVISION != 69
// #error "This file was generated using the moc from 6.12.0. It"
// #error "cannot be used with the include files from this version of Qt."
// #error "(The moc has changed too much.)"
// #endif
//
// namespace {
// struct qt_meta_tag_ZN5LeverE_t {};   // ← 空 tag，只为让下面那串模板特化有名字可特化
// } // unnamed namespace
//   ↑ 名字里那串 ZN5LeverE_t 是 mangled 的 "Lever"，moc 用它当「一个类一份内容」的键。
//     你在自己代码里永远不该出现这种名字——生成物只能看，不能抄。
//
// ── 3.2 字符串池：整张表只存下标，不存指针 ─────────────────────────────────
// template <> constexpr inline auto Lever::qt_create_metaobjectdata<qt_meta_tag_ZN5LeverE_t>()
// {
//     namespace QMC = QtMocConstants;
//     QtMocHelpers::StringRefStorage qt_stringData {
//         "Lever",          //  0  类名。qt_metacast 里 strcmp 比的就是它（见 3.6）
//         "pressedOut",     //  1  ↓ 以下每条目都可能是「名字」或「参数名」，看谁引用它
//         "",               //  2  空串 = 「返回类型是 void，名字栏留空」的占位
//         "total",          //  3
//         "id",             //  4
//         "levelChanged",   //  5
//         "level",          //  6
//         "flipped",        //  7
//         "reset",          //  8
//         "onLevelChanged", //  9
//         "presses",        // 10
//         "slotHits",       // 11
//         "lastLevel",      // 12
//         "setLevel",       // 13
//         "v",              // 14  ← setLevel(int v) 的参数名
//         "state",          // 15
//         "State",          // 16  ← 枚举名；下面 EnumData 的两个 16 都指它
//         "setState",       // 17
//         "s",              // 18
//         "press",          // 19  ← 只有 Q_INVOKABLE 的那位才在这儿
//         "times",          // 20
//         "Off",            // 21  ← 枚举**值**的名字，三行
//         "On",             // 22
//         "Blinking"        // 23
//     };
//   ↑ Qt6 的全部花样就在这：一个**编译期就能算完**的花括号列表。
//     Qt5 那套 `struct qt_meta_stringdata_Lever_t { QByteArrayData ...; char qtdt[...]; }`
//     加 `QT_MOC_LITERAL(0, 0, 5)` 在这版里 grep 不到了——旧书照抄必翻车。  name-ok:QT_MOC_LITERAL Qt5 的宏，本行正在说这版 grep 不到
//
// ── 3.3 方法表：每条一行模板，(名字下标, 返回类型名下标, 可见性, 返回类型, 参数) ──
//     QtMocHelpers::UintData qt_methods {
//         // Signal 'pressedOut'
//         QtMocHelpers::SignalData<void(int, int)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
//             { QMetaType::Int, 3 }, { QMetaType::Int, 4 },     // ← 参数=（类型, 名字下标）
//         }}),
//         // Signal 'levelChanged'
//         QtMocHelpers::SignalData<void(int)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
//             { QMetaType::Int, 6 },
//         }}),
//         // Signal 'flipped'
//         QtMocHelpers::SignalData<void()>(7, 2, QMC::AccessPublic, QMetaType::Void),
//         // Slot 'reset'
//         QtMocHelpers::SlotData<void()>(8, 2, QMC::AccessPublic, QMetaType::Void),
//         // Slot 'onLevelChanged'
//         QtMocHelpers::SlotData<void(int)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
//             { QMetaType::Int, 6 },
//         }}),
//         // Slot 'presses'   ← 下面这五条是「getter 写在 public Q_SLOTS 里」的代价/待遇
//         QtMocHelpers::SlotData<int() const>(10, 2, QMC::AccessPublic, QMetaType::Int),
//         // Slot 'slotHits'
//         QtMocHelpers::SlotData<int() const>(11, 2, QMC::AccessPublic, QMetaType::Int),
//         // Slot 'lastLevel'
//         QtMocHelpers::SlotData<int() const>(12, 2, QMC::AccessPublic, QMetaType::Int),
//         // Slot 'level'
//         QtMocHelpers::SlotData<int() const>(6, 2, QMC::AccessPublic, QMetaType::Int),
//         // Slot 'setLevel'
//         QtMocHelpers::SlotData<void(int)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
//             { QMetaType::Int, 14 },
//         }}),
//         // Slot 'state'
//         QtMocHelpers::SlotData<enum State() const>(15, 2, QMC::AccessPublic, 0x80000000 | 16),
//         // Slot 'setState'
//         QtMocHelpers::SlotData<void(enum State)>(17, 2, QMC::AccessPublic, QMetaType::Void, {{
//             { 0x80000000 | 16, 18 },
//         }}),
//         // Method 'press'   ← Q_INVOKABLE 才让它进表，身份是 Method 不是 Slot
//         QtMocHelpers::MethodData<int(int)>(19, 2, QMC::AccessPublic, QMetaType::Int, {{
//             { QMetaType::Int, 20 },
//         }}),
//     };
//   【这一屏里有三个值得停下来的点】
//   ① 数一下：13 条 = 3 信号 + 9 槽 + 1 Q_INVOKABLE 方法。
//      【更正一条我自己写错的】我上一版在这里写「顺序就是你写的顺序，pressedOut 在你源码里
//      第一个声明，它就排 0」——**两句都不对**：
//        · `pressedOut` 在源码里**最后**才声明（它在 Q_SIGNALS 段，那段在整个类的最底下，
//          见第二节 138~141 行），排 0 跟「写得早」没有关系；
//        · 真正决定顺序的是 moc 的**分组规则**：先把 signalList 整组写完，再写 slotList
//          整组，最后写 methodList（Q_INVOKABLE）整组，**组内**才保持源码书写顺序。
//        · 反例就在这一屏最底下：`Method 'press'` 排 12。`press` 在源码里写在 `reset` **之前**
//          （109 行 vs 118 行），表里却排在所有槽之后——因为它属于 Q_INVOKABLE 那一组。
//      依据（本机 moc 源码，不是文档）：
//        qtbase/src/tools/moc/generator.cpp 282~284 行
//            addFunctions(cdef->signalList, "Signal");
//            addFunctions(cdef->slotList,   "Slot");
//            addFunctions(cdef->methodList, "Method");
//        qtbase/src/tools/moc/moc.h 202 行
//            QList<FunctionDef> signalList, slotList, methodList, publicList;  ← 解析期就分开了
//        qtbase/src/tools/moc/generator.cpp 851~854 行（qt_metacall 用的同一份拼接）
//            methodList += cdef->signalList; methodList += cdef->slotList; methodList += cdef->methodList;
//      工程后果：**加一个信号，会让后面所有槽和所有 Q_INVOKABLE 的数字索引全体 +1。**
//      数字索引绝不能写进配置/数据库/协议（第 8 讲再钉一次）；要稳定标识就用名字。
//   ② 五个 getter 全被登记成 SlotData<...() const>，连 `const` 都在签名里。
//      「槽」在这里只是个**身份标签**，不是什么特殊调用约定——第 8 讲读表时你会看到
//      methodType() 把它们统统报成 Slot。
//   ③ `0x80000000 | 16`：这一格不是内建 QMetaType 编号，最高位一置，意思变成
//      「去字符串池 16 号按**名字**（"State"）查」。我在 6.12 的头文件里 grep 不到
//      这个位的公开名字（Qt5 时代叫 PrivateMask，本机上已经没有这个符号），
//      所以请把它当**观察到的编码约定**：能读懂，别写进自己的代码。
//
// ── 3.4 属性表与枚举表 ────────────────────────────────────────────────────
//     QtMocHelpers::UintData qt_properties {
//         // property 'level'
//         QtMocHelpers::PropertyData<int>(6, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet | QMC::User, 1),
//     };
//         //  6 = 名字下标"level"；最后的 1 = notify 在**方法表里的局部索引**（0 pressedOut、1 levelChanged）
//         //  QMC::User 就是你那句 `USER true`；Writable/StdCppSet 同理各对应一个关键字。
//         //  注意本类只有**一条**属性：state/setState 只是普通槽，没写 Q_PROPERTY 就进不了这里。
//     QtMocHelpers::UintData qt_enums {
//         // enum 'State'
//         QtMocHelpers::EnumData<enum State>(16, 16, QMC::EnumIsScoped).add({
//             {   21, State::Off },
//             {   22, State::On },
//             {   23, State::Blinking },
//         }),
//     };
//         //  (16, 16) = 枚举名与「枚举的**限定名**」此刻是同一个下标；
//         //  QMC::EnumIsScoped 是因为你写的是 `enum class State`（qtmochelpers.h 332 行
//         //  把这一位 or 进 header[2]）。三个值就是名字下标 + **真实 C++ 值**，
//         //  所以 keyToValue("On") 拿到的不是 1，而是你源码里给的那个整数。
//     return QtMocHelpers::metaObjectData<Lever, qt_meta_tag_ZN5LeverE_t>(QMC::MetaObjectFlag{}, qt_stringData,
//             qt_methods, qt_properties, qt_enums);
// }
//
// ── 3.5 那张表落地：staticMetaObject 的 7 项 ───────────────────────────────
// Q_CONSTINIT const QMetaObject Lever::staticMetaObject = { {   // ← 两层花括号：外层给 QMetaObject，
//     QMetaObject::SuperData::link<QObject::staticMetaObject>(),   // 1 superdata    内层给它的第一个成员 d
//     qt_staticMetaObjectStaticContent<qt_meta_tag_ZN5LeverE_t>.stringdata, // 2 stringdata 字符串池
//     qt_staticMetaObjectStaticContent<qt_meta_tag_ZN5LeverE_t>.data,        // 3 data       条目数组
//     qt_static_metacall,                                          // 4 static_metacall 本类的派发函数
//     nullptr,                                                     // 5 relatedMetaObjects（moc 按需才填，本类为空）
//     qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN5LeverE_t>.metaTypes,// 6 metaTypes  需要重定位的类型接口
//     nullptr                                                      // 7 extradata（reserved for future use）
// } };
//   ↑ 这 7 项的名字和顺序就照 qobjectdefs.h 614~623 行 `struct Data` 抄的
//     （superdata / stringdata / data / static_metacall / relatedMetaObjects / metaTypes / extradata）。
//     Q_CONSTINIT = 常量初始化，全进只读段，**运行时零构造成本**；这也是为什么
//     「元对象系统不拖慢启动」。前 4 项就够回答「表在哪、谁生成、谁派发」。
//
// ── 3.6 派发：一张表 + 一个 switch（本讲标题说的就是这段）──────────────────
// void Lever::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
// {
//     auto *_t = static_cast<Lever *>(_o);      // ← 一切从「还原成具体类型」开始
//     if (_c == QMetaObject::InvokeMetaMethod) {
//         switch (_id) {                        // ← _id 是**局部索引**，0 起
//         case 0: _t->pressedOut((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
//         case 1: _t->levelChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
//         case 2: _t->flipped(); break;
//         case 3: _t->reset(); break;
//         case 4: _t->onLevelChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
//         case 5: { int _r = _t->presses();
//             if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
//         ...
//         case 12: { int _r = _t->press((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
//             if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
//         default: ;
//         }
//     }
//   ↑ 三个必看细节：
//     · `_a[0]` 是**返回值的存放处**，`_a[1] _a[2] …` 才是入参。只有非 void 的方法才写回 _a[0]
//       （case 5~12 那批），信号那几条从来不写——这就是「信号有返回值也没人接」的代码形状。
//     · 从 `_a[1]` 起，不是 `_a[0]`。QMetaObject::invokeMethod 里那个「参数少一位」的坑就在这。
//     · case 顺序 = 3.3 方法表顺序（0 pressedOut、4 onLevelChanged、12 press）。
//       `Q_INVOKABLE int press(int)` 能在这里被调到，靠的是它进了表；没进表的成员函数
//       在这个 switch 里根本不存在，反射也就无从下手。
//     if (_c == QMetaObject::IndexOfMethod) {
//         if (QtMocHelpers::indexOfMethod<void (Lever::*)(int , int )>(_a, &Lever::pressedOut, 0))
//             return;
//         if (QtMocHelpers::indexOfMethod<void (Lever::*)(int )>(_a, &Lever::levelChanged, 1))
//             return;
//         if (QtMocHelpers::indexOfMethod<void (Lever::*)()>(_a, &Lever::flipped, 2))
//             return;
//     }
//   ↑ 只登记**三个信号**（槽和方法不在这儿），这就是 `&Lever::levelChanged` 这种函数指针
//     能被换算成索引（indexOfSignal）的机制。注意写的是 `Lever::`，不是箭头。
//     if (_c == QMetaObject::ReadProperty) {
//         void *_v = _a[0];
//         switch (_id) {
//         case 0: *reinterpret_cast<int*>(_v) = _t->level(); break;    // ← READ 函数在这被调
//         default: break;
//         }
//     }
//     if (_c == QMetaObject::WriteProperty) {
//         void *_v = _a[0];
//         switch (_id) {
//         case 0: _t->setLevel(*reinterpret_cast<int*>(_v)); break;    // ← **只调 setter**
//         default: break;
//         }
//     }
// }
//   ↑ WriteProperty 这段是第 6 讲那条实测结论的**证据**：moc 只替你把 setter 叫起来，
//     「改完值发 NOTIFY」得 setter 自己做。忘了 Q_EMIT，属性系统、QML、界面绑定一起哑。
//     本类没写 RESET/Bindable，所以输出里就没有 ResetProperty/BindableProperty 那两个分支
//     （moc 按需生成，不是每个类都有一整套）。
//
// ── 3.7 三个虚函数入口：metaObject / qt_metacast / qt_metacall ────────────
// const QMetaObject *Lever::metaObject() const
// {
//     return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
// }
//   ↑ 前半句是 `setProperty("objectName", ...)`/Q_PROPERTY 动态扩展那条路：
//     只有 d_ptr 里被塞过动态元对象时才走它，平时就是 return &staticMetaObject。
// void *Lever::qt_metacast(const char *_clname)
// {
//     if (!_clname) return nullptr;
//     if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN5LeverE_t>.strings))
//         return static_cast<void*>(this);    // ← .strings 的首条就是 "Lever"
//     return QObject::qt_metacast(_clname);   // ← 逐级往上问：qobject_cast 的全部秘密
// }
// int Lever::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
// {
//     _id = QObject::qt_metacall(_c, _id, _a);          // ← 先让父类吃掉它那一段
//     if (_id < 0) return _id;                          //   父类接住了就返回负数
//     if (_c == QMetaObject::InvokeMetaMethod) {
//         if (_id < 13) qt_static_metacall(this, _c, _id, _a);   // ← 13 = 本类方法条数
//         _id -= 13;                                           // ← 全局→局部，就这一行减法
//     }
//     if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
//         if (_id < 13) *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
//         _id -= 13;
//     }
//     if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
//             || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
//             || _c == QMetaObject::RegisterPropertyMetaType) {
//         qt_static_metacall(this, _c, _id, _a);
//         _id -= 1;                                             // ← 1 = 本类属性条数
//     }
//     return _id;
// }
//   ↑ 第 8 讲里 methodOffset()/propertyOffset() 那些「为什么我 dump 出一堆陌生槽」的
//     答案就在这个函数里：索引是**沿继承链累加**的，父类先减，减成负数就说明不归我管。
//
// ── 3.8 信号的实现体：moc 现写给你看的那几个函数 ───────────────────────────
// // SIGNAL 0
// void Lever::pressedOut(int _t1, int _t2)
// {
//     QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
// }
// // SIGNAL 1
// void Lever::levelChanged(int _t1)
// {
//     QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
// }
// // SIGNAL 2
// void Lever::flipped()
// {
//     QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
// }
//   ↑ 「信号只声明不实现」的完整说法在这儿落地：
//     · 你只写 `void levelChanged(int level);`，**函数体是 moc 补的**；
//     · 体内一行：`QMetaObject::activate(对象, 本类表, 信号在方法表的**局部索引**, 返回值位, 实参…)`；
//     · 索引 0/1/2 = pressedOut/levelChanged/flipped，和 3.3 的顺序严格一致（不是按源码行号，
//       是按「信号在方法表里排第几」）；
//     · 无参信号连 `<void>` 都不带，走的是不模板化的那个 activate 重载。
//     所以「信号是 public 成员函数」不是比喻：`&Lever::levelChanged` 能取地址、能当
//     connect 的第一个实参，正因为这里真有一个函数体。
// ============================================================================
// 【两处「和 Qt5 教程不一样」，务必标注清楚，别照着旧书抄】
//   ① Qt5 的产物长这样：`struct qt_meta_stringdata_Lever_t { QByteArrayData...; char qtdt[...]; }`
//      + `QT_MOC_LITERAL(0, 0, 5)` 一整套偏移量宏。Qt6 全换了：一个 `StringRefStorage`  name-ok:QT_MOC_LITERAL 同上：描述 Qt5 产物长什么样
//      花括号初始化列表 + `QtMocHelpers::SignalData<签名>` 模板 + `Q_CONSTINIT` 常量初始化，
//      编译期就把表算完了。旧书里那些「qt_meta_stringdata_XXX_t」现在 grep 不到。
//   ② 【更正一条我自己写错的】第一版这里我断言「本机输出里 IndexOfMethod 那行写的是
//      `&Lever->onLevelChanged`」——跑一遍 moc 再看，**根本没有这回事**：本机输出三行全是
//      `&Lever::pressedOut` / `&Lever::levelChanged` / `&Lever::flipped`（成员指针，双冒号），
//      而且**只登记三个信号**，槽不在这张 IndexOfMethod 名单里。
//      教训：生成物的细节不能靠印象，连老师傅的印象都不能信。
//      另外本讲正文第 6 节调 qt_metacast 用的是**对象**（lever.qt_metacast），不是指针——
//      和摘录里那个 `static_cast<void*>(this)` 是两码事，别混。
// ============================================================================

// ============================================================================
// 四、moc 在构建系统里是怎么被叫起来的（AUTOMOC 的三条硬规矩）
// ----------------------------------------------------------------------------
// 1) AUTOMOC 扫每个**被编译的** TU 的源码，发现类里有 Q_OBJECT/Q_GADGET/Q_NAMESPACE
//    之类标记就去跑 moc，产物叫 `<原文件名>.moc`（源文件是 .cpp 时）或
//    `moc_<header名>.cpp`（源文件是头文件时）。
//    → 「头文件里的 Q_OBJECT 从没被重编」多半是这个：改了 .h 但没有任何 .cpp 会重编。
// 2) 只有「moc 的产物**被 include 回**某个 TU」，那些定义才进得了链接。
//    第 1 讲的错误就是这条没做到；AUTOMOC 只在产物没被显式 include 时另外建一个
//    `<base>_automoc.cpp` 去带它，而 .cpp 里的类**必须**你自己写
//       #include "05_moc_anatomy.moc"
//    文件名要**一字不差**等于当前 TU 的文件名 + ".moc"，且放在 TU 的**最后**。
// 3) 想手动跑（本讲就是这么实测的，依据 = 上面第三节开头那条命令 + 零差异 diff）：
//       D:/Qt/6.12.0/mingw_64/bin/moc.exe 05_moc_anatomy.cpp -o 05_moc_output.txt
//       带宏开关时：moc.exe -DQT_NO_DEBUG -I D:/Qt/6.12.0/mingw_64/include xxx.h -o moc_xxx.cpp
//    看到 "Parse error" 别慌，它 99% 是这两类：Q_OBJECT 的关键字写错/漏空格，
//    或者属性/信号里有 moc 解析不了的模板（moc 的模板解析能力有限，
//    `Q_PROPERTY(QVector<int> ...)` 这种就属于要先想想的）。
// 4) **可编译的**产物不该进版本库：看到仓库里有 `moc_*.cpp` 或 `*.moc`，删掉，
//    让 AUTOMOC 生成（它们一旦和源码不同步就是慢性毒药）。本目录里的
//    `05_moc_output.txt` 是例外——它是**教学用的文本快照**（`.txt`，不参与编译），
//    内容 = 上面那条 moc 命令的原始输出，我生成后 diff 过为零差异。
// ============================================================================

// ——— 老师傅的话 ———
/*
 * 徒弟，这一讲你拿到的不是新知识，是**解释前四讲的权利**。
 * 之前我说过三句像咒语的话，现在它们都有账可查了：
 *
 *   「信号你只声明不实现」——因为 Q_OBJECT 只给了**声明**（qtmetamacros.h 139~152 行
 *   展开出来没有一个函数体），实现体是 moc 在编译前照着你的函数名现写的一个
 *   `QMetaObject::activate(...)` 转发（摘录最后那三段）。你不 include 那个 .moc，
 *   链接器就找不到它——这就是第 1 讲那个 `undefined reference to vtable for X` 的全部来历。
 *
 *   「槽是普通成员函数」——看摘录里的 switch：`case 4: _t->onLevelChanged(...)`
 *   （依据：`metaobject/05_moc_output.txt` 第 140 行，逐字），
 *   和你手动调一次没有任何区别，moc 不区分「谁在调」。`slots` 关键字改变的只有
 *   **表里那条记录用的模板类型**（`QtMocHelpers::SlotData` vs `MethodData`，见 3.3），
 *   而 '1'/'2' 那个数字前缀是 SIGNAL()/SLOT() **宏**加在传给 connect 的那根字符串上的编码
 *   （qobjectdefs.h 41~43 行；元对象字符串池里**没有**这个前缀，3.2 逐字可查）；
 *   函数指针版连这根字符串都不生成。
 *
 *   「函数指针版有编译期检查」——因为那条路**根本不经过这张表**：编译器直接
 *   对函数签名做了类型检查，运行时用成员指针地址去比。表只在 invokeMethod、
 *   字符串 connect、属性读写、QML 绑定这些「按名字查」的路上出场。
 *
 * 然后是这张表的成本，心里要有数：一次 activate 要做「查这个信号的接收者列表 →
 * 对每个接收者按连接类型决定直接调用/投递事件 → 从 void** 里 reinterpret_cast 取参数」；
 * 一次 invokeMethod("press") 还要多加一次字符串比较和 int→QMetaType 装箱。
 * 而 `connect(&Lever::press, ...)` 之后你在接收者里直接调用，是**一次普通函数调用**。
 * 差别在量大的时候才显形：一个每秒刷 60 次的表格 delegate、一条被连了上百次的信号，
 * 用「按名字查」的路子就是在给自己上税。所以我的规矩很硬：
 *   **内部热路径只走函数指针版；只有跨模块插件、QML/属性绑定、真的需要「按名字装配」
 *    的地方（objectName + connectSlotsByName）才允许出现字符串版。**
 *
 * 最后一条是我自己踩出来的读表姿势，送你：别背字段，**跑一遍 moc**。
 * 一个 30 行的类，moc 出来 236 行（`wc -l 05_moc_output.txt` 实测），你就有全部答案：
 * 顺序怎么排（信号成段、槽成段，段内才按书写顺序）、字符串池里是什么（光秃秃的名字，
 * 没有数字前缀）、switch 里有几个 case、qt_metacall 减掉了几条。看三次就终身不忘，
 * 比读十篇博客都管用——本讲我自己就是靠这个把三条凭印象写错的话改回来的。
 */

// ——— 小练习 ———
/*
 *   1. 把本讲的 moc 命令自己跑一遍（路径就在上面注释里），数一数 qt_stringData 里有
 *      几条字符串、qt_methods 里有几个条目，然后和本讲第 3 节的运行时输出一一对上：
 *      哪个索引对应哪个成员？pressedOut 的局部索引到底是几？
 *   2. 把 Lever 里的 `Q_INVOKABLE` 删掉再跑 moc：确认 press 从方法表里**整条消失**，
 *      invokeMethod("press") 从此查不到（第 4 节的 okPress 变 false）。
 *      注意它**不会断言**：qmetaobject.cpp 1715 行一路走到
 *      printMethodNotFoundWarning()（1495~1528 行，1523 行 qWarning、1527 行 return false），
 *      Debug/Release 都一样——第 4 节的实测日志就是这句警告 + false + 退出码 0。
 *      由此说明「Q_INVOKABLE 是注册，不是权限」。
 *   3. 把 `void flipped();` 改成 `int flipped();`，跑 moc 看它**照样通过**（本机实测过
 *      同构的探针：moc 不拒绝非 void 信号），再对照摘录里 `case 2: _t->flipped(); break;`
 *      （05_moc_output.txt 第 138 行）——没有 `_a[0]` 写回。用自己的话写出「信号为什么必须
 *      返回 void」的真实原因，并把它写进你团队的 code review 检查项里。
 */
// ============================================================================
