// ============================================================================
// 第 1 讲 · 第一个窗口
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   把「一个 Qt 程序从 main() 进去到窗口显示出来」这条最短的路走通，
//   并讲清楚三件新手最容易囫囵吞枣的事：
//     1) 为什么必须先构造 QApplication，再碰任何 QWidget；
//     2) show() 只是「提出一次重绘请求」，真正的绘制发生在事件循环里；
//     3) Qt 自己会吃掉哪些命令行选项（在我们写参数之前，Qt 已经解析过一遍了）。
//
// 对应构建目标名：start_01_hello_window
//   CMake 里大致是：
//     find_package(Qt6 REQUIRED COMPONENTS Widgets)
//     add_executable(start_01_hello_window 01_hello_window.cpp)
//     target_link_libraries(start_01_hello_window PRIVATE Qt6::Widgets)
//     set(CMAKE_AUTOMOC ON)   # 本讲示例没用到 Q_OBJECT，但工程里一直保持开着即可
//
// 运行前须知：
//   · 依赖 Qt6::Widgets；QPA 平台插件目录要在 PATH 里（Windows 上通常是
//     <Qt>\bin，或程序旁边有 platforms/qwindows.dll、qoffscreen.dll）。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 start_01_hello_window，跑到自动退出、退出码 0）。
//     但本讲**没有汇总自检行**，退出码 0 只证明「没崩、走完了」，不证明正文每个数字都对：
//     带具体数字的「预期」请以你自己那次运行输出为准（本机那一轮存档 runlogs/start_01_hello_window.log）。
//   · 本示例无人值守跑完：用 QTimer::singleShot 演示几步后自动 QCoreApplication::quit()。
// ============================================================================

#include <QApplication>        // QtWidgets/qapplication.h：QApplication 本体
#include <QWidget>             // QtWidgets/qwidget.h：所有窗口部件的基类
#include <QTimer>              // QtCore/qtimer.h：singleShot 驱动自动演示
#include <QCoreApplication>    // QtCore/qcoreapplication.h：quit() 槽
#include <QGuiApplication>     // QtGui/qguiapplication.h：platformName()
#include <QDebug>              // QtCore/qdebug.h：qInfo()/qDebug() 输出
#include <QString>

int main(int argc, char *argv[])
{
    // ── 第一件事：先立起 QApplication ─────────────────────────────────
    // 【为什么必须是它】QApplication（其实是它的继承链
    // QApplication → QGuiApplication → QCoreApplication）在构造函数里干了这些活：
    //   · 加载 QPA（Qt Platform Abstraction，Qt 平台抽象）平台插件，
    //     在 Windows 上就是 platforms/qwindows.dll，没有它根本开不了窗口；
    //   · 创建事件循环（event loop）所需的分发器（event dispatcher）；
    //   · 提供全局的字体（font）、调色板（palette）、样式（style）；
    //   · 解析 Qt 自己的命令行选项（见本节末尾）。
    // 所以在 QApplication 存在之前构造任何 QWidget，都是未定义行为，
    // 轻则断言失败，重则直接崩溃——这是 Qt 的第一条铁律。
    //
    // 注意 argv 是「引用」传进去的（int &argc, char **argv）：
    // Qt 会把它认得的选项从参数表里**摘走**，所以 argc 可能比你传进来时变小。
    // 核对签名：QtWidgets/qapplication.h 第 52 行
    //   QApplication(int &argc, char **argv, int = ApplicationFlags);
    QApplication app(argc, argv);

    // platformName() 告诉我们当前实际用的是哪个 QPA 插件。
    // 在 Windows 桌面一般是 "windows"；带 QT_QPA_PLATFORM=offscreen 跑则是 "offscreen"。
    // 核对签名：QtGui/qguiapplication.h 第 74 行  static QString platformName();
    qInfo() << "当前平台插件 platformName =" << QGuiApplication::platformName();

    // ── 第二件事：造一个窗口部件 ──────────────────────────────────────
    // QWidget 是最基础的窗口部件（widget）。这里直接拿它当顶层窗口用。
    // 核对签名：
    //   QtWidgets/qwidget.h  第 369 行  void setWindowTitle(const QString &);
    //                       第 503 行  void resize(int w, int h);
    QWidget window;
    window.setWindowTitle(QStringLiteral("第 1 讲 · 我的第一个 Qt 窗口"));
    window.resize(400, 240);

    // ── 第三件事：show() 到底做了什么 ────────────────────────────────
    // 【关键认知】show() 不是「把像素画上去」。它只是：
    //   1) 把这个部件标记为可见；
    //   2) 向底层窗口系统申请一个窗口；
    //   3) 投递一次「重绘请求」（paint event）。
    // 真正调用 paintEvent() 把内容画出来，是在下面 app.exec() 跑起来之后的
    // 事件循环里才发生的。换句话说：show() 只管「挂号」，画不画得出来，
    // 得等事件循环这个「医生」上班。没跑 exec() 就 show()，窗口要么不出现、
    // 要么是个空壳——这正是「窗口一闪而过」那个经典坑的病根。
    // 核对签名：qwidget.h 第 487 行  void show();
    window.show();
    qInfo() << "show() 已调用——此时只是提出了重绘请求，还没真正绘制";

    // ── 用定时器驱动一段「自动演示脚本」，无人值守 ────────────────────
    // 【为什么要这么写】教学示例绝不能「等着有人来点关闭按钮」，
    // 那样 CI/脚本就没法跑完。这里用 QTimer::singleShot 排几步动作，
    // 到点自动执行，最后自己 quit()。
    // 核对签名：QtCore/qtimer.h 第 85 行
    //   static void singleShot(Duration interval, Functor &&slot);
    // 这里 interval 传 int（毫秒），slot 传一个无捕获 lambda。

    // 1.5 秒后：打印一句「事件循环已经转起来了，窗口此刻才是真正被画出来的」
    QTimer::singleShot(1500, [] {
        qInfo() << "1.5s：事件循环已运行，窗口此时已完成首帧绘制";
    });

    // 3.0 秒后：改标题，验证「运行中也能响应」——这仍是事件循环在派发
    QTimer::singleShot(3000, [&window] {
        window.setWindowTitle(QStringLiteral("第 1 讲 · 窗口活得好好的"));
        qInfo() << "3.0s：标题已被程序自动改掉（证明事件循环在持续派发）";
    });

    // 4.5 秒后：主动退出事件循环，程序干净结束
    // quit() 是 QCoreApplication 的静态槽：qcoreapplication.h 第 205 行
    //   static void quit();
    QTimer::singleShot(4500, [] {
        qInfo() << "4.5s：调用 QCoreApplication::quit()，自动演示结束";
        QCoreApplication::quit();
    });

    // ── 第四件事：进入事件循环 ────────────────────────────────────────
    // exec() 阻塞在这里，一边等事件（鼠标、键盘、定时器、重绘），
    // 一边把它们派发给对应的对象。上面 quit() 一被触发，exec() 就返回退出码。
    // 忘了写 exec()，main 直接往下走到 return，窗口刚 show 就被销毁 → 一闪而过。
    // 核对签名：qapplication.h 第 118 行  static int exec();
    int rc = app.exec();
    qInfo() << "事件循环已退出，exec() 返回" << rc;
    return rc;
}

// ============================================================================
// 附：Qt 会「自己吃掉」哪些命令行选项？
// ============================================================================
/*
 * 在你还没来得及用 QCommandLineParser 解析自己的参数之前，Qt 框架已经
 * 先把它的内置选项从 argv 里摘走了。下面是**有把握**的几项：
 *
 *   跨平台（QGuiApplication/QApplication 层）：
 *     -platform <名称>          指定 QPA 插件，如 windows / offscreen / minimal
 *     -platformpluginpath <路径> 指定查找平台插件的目录
 *     -platformoptions <选项>    传给该平台插件的专有选项
 *     -qwindowgeometry <几何>    设置顶层窗口的几何（形如 400x240+100+50）
 *     -qwindowtitle  <标题>      设置顶层窗口标题
 *     -qwindowicon   <图标>      设置顶层窗口图标
 *     -reverse                   把布局方向设为从右到左（RTL）
 *     -style  <名称>             选样式（Widgets 层，见第 3 讲 QStyleFactory）
 *     -stylesheet <路径>         加载全局样式表（Widgets 层）
 *
 *   仅 X11（Windows 上不生效）：
 *     -display、-geometry、-bg/-fg、-fn、-name、-title、-visual、-ncols
 *
 * 诚实说明：这些选项字符串**不在**本机公开的 Qt 头文件里（它们是在
 * .cpp 实现与平台插件内部解析的），我对着本机头文件只能核到「选项对应
 * 的 setter」，如 -style 对应 QApplication::setStyle、-platform 对应
 * 平台插件机制。上面这份清单来自 Qt 官方文档，Windows 下最常用的是
 * -platform / -platformpluginpath / -qwindowgeometry / -style 这几个，
 * 不确定的我一概没往上写。想验证：`start_01_hello_window -platform offscreen`。
 *
 * ——— 老师傅的话 ———
 *   徒弟，今天这一讲就一个最容易被误解的点：**show() 不等于画出来了**。
 *   新手以为「调了 show() 窗口就该有内容了」，于是在 show() 后面紧接着去
 *   读像素、去截图、去断言大小，结果十有八九拿到的是空的。记住这个次序：
 *   先有 QApplication，再 show() 挂号，最后 exec() 让事件循环把「挂号」
 *   变成「看病」。凡是你想「看到窗口真的画好了」的自检动作，都得放到
 *   exec() 跑起来之后、用 QTimer 排到事件循环里去干，别在 main 里直线往下写。
 *   这条理解了，后面第 6 讲的无头截图、自动化测试才不会把你绕晕。
 *
 * ——— 小练习 ———
 *   1. 删掉 app.exec() 这一行重新跑，亲眼看一次「窗口一闪而过」，
 *      再把它加回来，体会事件循环到底给你兜了什么底。
 *   2. 把 setWindowTitle/resize 之前，故意在 QApplication 构造**之前**
 *      new 一个 QWidget，观察它如何立刻断言失败/崩溃（跑完记得改回来）。
 *   3. 再排两个 QTimer::singleShot（比如 2000ms、3500ms），到点依次把
 *      窗口 resize 成不同尺寸，用眼睛数一数它「跳」了几次，理解这些都是
 *      事件循环里被依次派发的一次次重绘。
 */
// ============================================================================
