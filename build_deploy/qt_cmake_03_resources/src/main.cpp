// ============================================================================
// main.cpp · 资源程序：同一个文件的两条路径
// ----------------------------------------------------------------------------
// 本工程解决什么问题：
//   把「构建期怎么把文件塞进 exe」和「运行期怎么读出来」接成一条线：
//     · 路线 B：qt_add_resources → ":/style.qss"、":/logo.svg"
//     · 路线 A：AUTORCC + legacy.qrc → ":/legacy/style.qss"
//   然后当场核对三件事：
//     1) 两个路径都能读到，且字节数相同（同一份内容被编了两次）
//     2) QFile 读取资源的写法和读磁盘文件一模一样（open/readAll）
//     3) 资源是**只读**的：用 WriteOnly 打开 ":/style.qss" 会失败
//
// 运行前须知：
//   · 无人值守：看到结论后 300 毫秒自动退出，退出码 0。
//   · 可以无头跑：resource_app -platform offscreen
//   · 不写任何磁盘文件。
//
// 对应构建目标名：resource_app
// ============================================================================

#include <QApplication>
#include <QLabel>
#include <QFile>
#include <QTimer>
#include <QDebug>

namespace {

// 读一个资源路径的全文；失败时打印原因并返回空。
QByteArray readResource(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qInfo().noquote() << "  打不开" << path << "：" << f.errorString();
        return {};
    }
    const QByteArray bytes = f.readAll();
    qInfo().noquote() << "  读到" << path << "，字节数 =" << bytes.size();
    return bytes;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    qInfo().noquote() << "── 路线 B：qt_add_resources ──";
    const QByteArray styleB = readResource(QStringLiteral(":/style.qss"));
    const QByteArray logo   = readResource(QStringLiteral(":/logo.svg"));

    qInfo().noquote() << "── 路线 A：AUTORCC + legacy.qrc ──";
    const QByteArray styleA = readResource(QStringLiteral(":/legacy/style.qss"));

    qInfo().noquote() << "两条路径内容相同吗？" << (styleB == styleA ? "相同" : "不同");
    qInfo().noquote() << "logo.svg 前 5 字节：" << logo.left(5);

    // 资源是只读的：这一句一定会失败，而且失败得悄无声息，必须自己检查返回值。
    QFile ro(QStringLiteral(":/style.qss"));
    const bool openedForWrite = ro.open(QIODevice::WriteOnly);
    qInfo().noquote() << "以 WriteOnly 打开资源成功了吗？" << openedForWrite
                      << "（应为 false）" << ro.errorString();

    // qss 从资源读进来，直接喂给应用。这就是「样式表也编进 exe」的实际用法。
    app.setStyleSheet(QString::fromUtf8(styleB));

    QLabel headline(QStringLiteral("这段文字的样式来自 :/style.qss"));
    headline.setObjectName(QStringLiteral("headline"));   // 对应 qss 里的 #headline
    headline.setAlignment(Qt::AlignCenter);
    headline.show();

    QTimer::singleShot(300, &app, &QCoreApplication::quit);
    return app.exec();
}
