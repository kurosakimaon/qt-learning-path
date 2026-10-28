// ============================================================================
//  todo_desktop · 入口
// ----------------------------------------------------------------------------
//  两种跑法：
//    todo_desktop                 正常界面（数据落在系统给的应用数据目录）
//    todo_desktop --self-check    无界面自检，临时目录里跑完就退出，退出码即结论
//
//  为什么把 --self-check 做进正式程序、而不是单独一个小工具？
//    因为它要验证的正是正式程序用的那几个类（模型/仓储/JSON/后台统计）。
//    另起一个工具很容易「测的是另一个实现」，那就白测了。
// ============================================================================
#include "app/selfcheck.h"
#include "ui/mainwindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QStringList>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // QSettings 靠这两个名字决定写到哪儿；必须在任何 QSettings 之前设好。
    QCoreApplication::setOrganizationName(QStringLiteral("QtLearningPath"));
    QCoreApplication::setApplicationName(QStringLiteral("TodoDesktop"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    if (QCoreApplication::arguments().contains(QStringLiteral("--self-check")))
        return selfcheck::run();

    const QString dataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dataDir.isEmpty()) {
        qCritical("找不到可写的应用数据目录");
        return 2;
    }
    QDir().mkpath(dataDir);

    MainWindow window(QDir(dataDir).filePath(QStringLiteral("todo.db")));
    window.resize(1000, 640);
    window.show();

    return app.exec();
}
