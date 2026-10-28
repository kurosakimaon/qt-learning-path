// ============================================================================
// main.cpp · 界面层：只认识 greeting.h，不认识 greeting.cpp
// ----------------------------------------------------------------------------
// 本工程解决什么问题：
//   看见「一个库 + 一个界面」的最小结构，以及 PUBLIC / PRIVATE 到底影响什么：
//   · greeting.h 的路径是 PUBLIC 的 → 这里直接 #include <greeting/greeting.h>
//   · Qt6::Core 是 PUBLIC 的        → 这里没有 find_package(Qt6 COMPONENTS Core)，
//                                     但仍然能 include <QTimer>
//   · Qt6::Widgets 是这边 PRIVATE 的 → 只有本可执行文件看得见
//
// 运行前须知：
//   · 无人值守：窗口显示 300 毫秒后自动退出，退出码 0。
//   · 可以无头跑：greeting_app -platform offscreen
//   · 信号 greeted 用 lambda 接收，打印一行就结束——这是「库发、界面收」最省的写法。
//
// 对应构建目标名：greeting_app
// ============================================================================

#include <QApplication>              // 来自 Qt6::Widgets（本目标 PRIVATE 链接）
#include <QLabel>
#include <QTimer>
#include <QDebug>

#include "greeting/greeting.h"       // 来自 greeting 的 PUBLIC include 目录

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    Greeter greeter(QStringLiteral("徒弟"));

    // 信号 → lambda：库里 emit，界面里收。
    // 第三个参数 &greeter 是 context 对象：greeter 一死，这条连接自动断开。
    QObject::connect(&greeter, &Greeter::greeted, &greeter,
                     [](const QString &line) {
                         qInfo().noquote() << "[收到信号]" << line;
                     });

    const QString text = greeter.greet();
    qInfo().noquote() << "[直接调用]" << text;

    // 发一次信号：注意 emit 是空宏，真正的活儿在 moc 生成的 activate() 里。
    emit greeter.greeted(text);

    QLabel label(text);
    label.setAlignment(Qt::AlignCenter);
    label.setMinimumSize(520, 100);
    label.show();

    QTimer::singleShot(300, &app, &QCoreApplication::quit);
    const int rc = app.exec();
    qInfo().noquote() << "退出码：" << rc;
    return rc;
}
