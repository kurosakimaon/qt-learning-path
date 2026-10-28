// ============================================================================
// 最小 Qt Widgets 程序：一个窗口、一句中文、三行结论
// ----------------------------------------------------------------------------
// 本工程解决什么问题：
//   把「一个 Qt 可执行文件最少需要什么」缩到最小：QApplication、一个控件、
//   show()，以及一条能自动结束的出路。你在这里看到的每一行，后面所有工程
//   都会用到。
//
// 它同时是一份「环境自检」：
//   · qVersion()        —— 实际链接上的 Qt 版本（不是你 include 的那份）
//   · QGuiApplication::platformName() —— 这次用的是哪个平台插件
//   · QScreen 的几何与 DPR —— 你在 offscreen / 真窗口下会看到不一样的答案
//
// 运行前须知：
//   · 无人值守：窗口只显示 300 毫秒，然后 QTimer::singleShot 叫停，自动退出。
//   · 可以无头跑：hello_widgets -platform offscreen
//   · 不读文件、不联网、不写配置。
//
// 对应构建目标名：hello_widgets（见同目录 CMakeLists.txt）
//   qt_add_executable(hello_widgets main.cpp)
//   target_link_libraries(hello_widgets PRIVATE Qt6::Widgets)
// ============================================================================

#include <QApplication>          // QtWidgets/qapplication.h
#include <QLabel>                // QtWidgets/qlabel.h
#include <QScreen>               // QtGui/qscreen.h
#include <QTimer>                // QtCore/qtimer.h
#include <QFont>                 // QtGui/qfont.h

int main(int argc, char *argv[])
{
    // QApplication 必须在**任何**控件之前构造：它初始化平台插件、字体数据库、
    // 事件分发器。argc/argv 要原样传给它（-platform offscreen 就是它解析的）。
    QApplication app(argc, argv);

    qInfo().noquote() << "Qt 版本：" << qVersion();
    qInfo().noquote() << "平台插件：" << QGuiApplication::platformName();

    QLabel label(QStringLiteral("你好，Qt。窗口会在 300 毫秒后自动关闭。"));
    label.setAlignment(Qt::AlignCenter);
    label.setMinimumSize(420, 120);
    label.show();

    // 屏幕信息：在 offscreen 插件下通常是 800x600 / DPR 1，真窗口下是你显示器的值。
    // 这就是「同一份代码在两台机器上打印不同」的正常现象。
    if (QScreen *screen = app.primaryScreen()) {
        qInfo().noquote() << "主屏几何：" << screen->geometry()
                          << " 可用区：" << screen->availableGeometry()
                          << " devicePixelRatio：" << screen->devicePixelRatio();
    }

    // 无人值守的关键：把 quit 排进事件队列，而不是在这里 sleep。
    // 这样 exec() 能正常转起来，窗口真的被绘制、真的被关闭，退出码是 0。
    QTimer::singleShot(300, &app, &QCoreApplication::quit);

    const int rc = app.exec();
    qInfo().noquote() << "事件循环退出码：" << rc;
    return rc;
}
