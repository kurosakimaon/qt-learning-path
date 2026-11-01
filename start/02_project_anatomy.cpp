// ============================================================================
// 第 2 讲 · 工程解剖
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   拆开一个 Qt 工程的「零件」，讲清 Q_OBJECT / moc / AUTOMOC 这三兄弟，
//   并把「声明/实现分离」这条讲透。同一个示例里放两种自定义类做对照：
//     A. StartWidget —— 定义在独立头文件 startwidget.h（实现 startwidget.cpp）；
//     B. Pinger      —— 直接定义在本 .cpp 里。
//   两者的差别，正好暴露出 moc 的两种接线方式：
//     A（头文件里的 Q_OBJECT 类）：AUTOMOC 自动生成并**单独编译**
//        moc_startwidget.cpp，你在源码里什么都不用 #include；
//     B（.cpp 里的 Q_OBJECT 类）：AUTOMOC 会为本 .cpp 生成
//        02_project_anatomy.moc，你必须在本 .cpp **末尾手动**
//        #include "02_project_anatomy.moc"，否则 B 的信号/元对象实现
//        根本没被编译进来，链接时报 vtable / staticMetaObject 未定义。
//
// 对应构建目标名：start_02_project_anatomy
//   add_executable(start_02_project_anatomy
//                  02_project_anatomy.cpp startwidget.cpp startwidget.h)
//   target_link_libraries(start_02_project_anatomy PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)          # 关键：开着它，moc 才会被自动调度
//   为什么 startwidget.cpp/.h 也要列进源文件：AUTOMOC 只会为「参与构建的
//   源文件」里的 Q_OBJECT 类生成 moc；没列进去的头文件，它根本不去扫描。
//
// 运行前须知：
//   · 依赖 Qt6::Widgets，且必须以 CMAKE_AUTOMOC=ON 构建（这是 Qt6 工程的标配）。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 start_02_project_anatomy，跑到自动退出、退出码 0）。
//     但本讲**没有汇总自检行**，退出码 0 只证明「没崩、走完了」，不证明正文每个数字都对：
//     带具体数字的「预期」请以你自己那次运行输出为准（本机那一轮存档 runlogs/start_02_project_anatomy.log）。
//   · 无人值守：QTimer 驱动，结束自动 quit()。
// ============================================================================

#include <QApplication>
#include <QWidget>
#include <QTimer>
#include <QCoreApplication>
#include <QDebug>
#include <QString>

#include "startwidget.h"   // 类 A：头文件里的 Q_OBJECT 类

// ============================================================================
// 类 B：直接写在本 .cpp 里的 Q_OBJECT 类
// ============================================================================
// 为什么要有类 B？为了制造一个「AUTOMOC 无法单独成编译单元」的场景。
// moc 的规则是：Q_OBJECT 类出现在**头文件**里，就生成 moc_<头名>.cpp（独立编译）；
// 出现在 **.cpp 文件**里，就生成 <本cpp名>.moc（一段要被 #include 回来的代码）。
// 所以下面这个类逼着我们在文件末尾写 #include "02_project_anatomy.moc"。
class Pinger : public QObject
{
    Q_OBJECT                 // 缺了它，下面的 signals 根本编译不过 / 连不上

public:
    // 显式把 parent 转给基类构造函数（QObject(QObject*)：qobject.h 第 114 行）。
    // 这里没用 `using QObject::QObject;`，是为了把「父类构造怎么调」写明白。
    explicit Pinger(QObject *parent = nullptr) : QObject(parent) {}

    // 信号只能在**类内部**发出。这里包一个普通成员函数 fire() 来代发。
    void fire(int seq) { Q_EMIT pinged(seq); }

signals:
    void pinged(int seq);    // 又是「只声明、由 moc 实现」
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // ── 用类 A：StartWidget ──────────────────────────────────────────
    StartWidget w;
    w.setWindowTitle(QStringLiteral("第 2 讲 · 工程解剖"));
    w.resize(320, 160);
    w.show();

    // 类 A 的自定义信号 counted 能被 connect，本身就证明 moc 生效了。
    // 连接方式：成员函数指针 → lambda（qobject.h 第 226 行 connect(...Functor)）。
    QObject::connect(&w, &StartWidget::counted, [](int n) {
        qInfo() << "[A counted] 收到信号，n =" << n;
    });

    // ── 用类 B：Pinger ───────────────────────────────────────────────
    Pinger pinger;
    QObject::connect(&pinger, &Pinger::pinged, [](int seq) {
        qInfo() << "[B pinged] 来自 .cpp 内定义类的信号，seq =" << seq;
    });

    // ── 自动演示脚本（无人值守）──────────────────────────────────────
    // 200ms：手动触发一次 A 的 increase()（它是 public slot，可以直接调），
    //        它内部会 Q_EMIT counted(1)，上面的 lambda 就会打印。
    QTimer::singleShot(200, [&w] { w.increase(); });

    // 500ms：触发 B 的 fire(42)，发出 pinged 信号。
    QTimer::singleShot(500, [&pinger] { pinger.fire(42); });

    // 900ms：收尾退出。
    QTimer::singleShot(900, [] {
        qInfo() << "本讲要点回顾：Q_OBJECT 是给 moc 的标记；AUTOMOC 负责调度 moc；"
                   "头文件里的类自动编译，.cpp 里的类要 #include \"<本文件>.moc\"";
        QCoreApplication::quit();
    });

    return app.exec();
}

// ============================================================================
// 【本讲的核心机关，就这一行】
// 因为 Pinger（Q_OBJECT 类）定义在**本 .cpp**里，moc 会为这个源文件生成一份
// 名为 02_project_anatomy.moc 的实现片段。必须在「类定义之后」把它 #include
// 回来，Pinger 的元对象数据（staticMetaObject）与信号 pinged 的实现才会存在。
// 忘了这一行 → 链接期报 undefined reference to `Pinger::staticMetaObject'
// 或 vtable 相关错误。
// 注意：moc 输出文件名 = 当前源文件名去掉 .cpp、加 .moc，所以这里必须写
// "02_project_anatomy.moc"，与文件名严格对应——文件名一改，这行也要跟着改。
#include "02_project_anatomy.moc"

// ——— 老师傅的话 ———
/*
 * 徒弟，moc 是「Qt 从 C++ 里长出来的那部分魔法」的门面，第一道坎就在这。
 * 你别把它想复杂了：**C++ 本身没有内建「信号/槽」和「运行时反射」，Qt 是靠
 * 一个额外的编译器 moc，读你的 Q_OBJECT 类，事先把元对象表和信号的实现代码
 * 「生成」出来，再交给 g++ 正常编译。** 也就是说 moc 是个「代码生成器」，
 * 它生成的东西本质上还是 C++。理解到这个程度就够用了：
 *   · 忘了 Q_OBJECT → moc 不给这个类生成元对象 → connect 自定义信号失败；
 *   · 忘了 AUTOMOC → 根本没人去跑 moc → 满屏 vtable undefined reference；
 *   · .cpp 里的类忘了 #include "xxx.moc" → 只有这个类出问题，别的都好好的。
 * 这三种「忘了」的报错长得不一样，你在第 6 讲和无头运行时会反复见到，
 * 到那时你能一眼认出「哦，这是 moc 没接上」，就说明这一讲真的过关了。
 */

// ——— 小练习 ———
/*
 *   1. 把 startwidget.h 里 StartWidget 的 Q_OBJECT 删掉再编译，观察报错
 *      （自定义信号 counted 连不上 / 编译不过），亲手撞一次「忘写 Q_OBJECT」，
 *      再把 Q_OBJECT 加回来。
 *   2. 把本文件末尾的 #include "02_project_anatomy.moc" 注释掉再编译，
 *      看链接器怎么报 Pinger 相关的 undefined reference，体会「.cpp 里的类
 *      必须手动回接 .moc」。恢复它。
 *   3. 把 Pinger 从本 .cpp 挪进一个新头文件 pinger.h（并让 AUTOMOC 扫到），
 *      验证：这时就**不再需要** #include "02_project_anatomy.moc" 了，
 *      而 moc_pinger.cpp 会替你自动编译——彻底分清 moc 的两条接线方式。
 */
// ============================================================================
