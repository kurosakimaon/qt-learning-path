// ============================================================================
// main.cpp · 资源程序：同一个文件的三条路径，以及它们各自是怎么来的
// ----------------------------------------------------------------------------
// 本工程解决什么问题：
//   把「构建期怎么把文件塞进 exe」和「运行期怎么读出来」接成一条线：
//     · 路线 B ：qt_add_resources 不带 BASE → ":/assets/style.qss"（路径 = FILES 的相对路径）
//     · 路线 B+：qt_add_resources 带 BASE assets → ":/style.qss"（相对 BASE 压平）
//     · 路线 A ：AUTORCC + legacy.qrc（prefix=/legacy）→ ":/legacy/style.qss"
//   然后当场核对四件事：
//     1) 先把本工程涉及的三个资源目录列出来，让「谁在哪儿」一目了然，不用猜
//     2) 三个路径都能读到，且字节完全相同（同一份内容被编了三份）
//     3) QFile 读取资源的写法和读磁盘文件一模一样（open/readAll）
//     4) 资源是**只读**的：用 WriteOnly 打开会失败，而且失败得悄无声息
//
// ★ 本讲最容易踩的坑：以为 qt_add_resources 会把文件放进 ":/文件名"。
//   实测不是 —— 不带 BASE 时 alias 沿用源码树里的相对路径，所以是 ":/assets/文件名"。
//   这一条在 CMakeLists.txt 里有对应注释和 Qt6CoreMacros.cmake 的行号依据。
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
#include <QFileInfo>
#include <QDir>
#include <QStringList>
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

// 列一个资源目录里的文件与子目录。
// ★为什么必须传目录而不是从 ":" 一路递归：":" 根下除了本工程塞进去的东西，
//   还有 Qt 自己内联的 :/qt-project.org/…（样式图标、着色器等）几百个文件，
//   整棵树走一遍会把真正的证据全部刷走。所以这里只列本工程关心的那几个目录。
void listDir(const QString &dir)
{
    QDir d(dir);
    if (!d.exists()) {
        qInfo().noquote() << "  目录不存在：" << dir;
        return;
    }
    const QStringList files = d.entryList(QDir::Files, QDir::Name);
    const QStringList dirs  = d.entryList(QDir::AllDirs | QDir::NoDotAndDotDot, QDir::Name);
    qInfo().noquote() << "  " << dir << " 文件 =" << files << " 子目录 =" << dirs;
    for (const QString &name : files) {
        const QString full = dir + name;    // dir 传进来时自带结尾的 "/"
        qInfo().noquote() << "      " << full << "字节 =" << QFileInfo(full).size();
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    qInfo().noquote() << "── 0. 资源树里本工程有哪几个目录 ──";
    listDir(QStringLiteral(":/"));
    listDir(QStringLiteral(":/assets/"));
    listDir(QStringLiteral(":/legacy/"));
    qInfo().noquote() << "  ↑ 同一份 style.qss 在树里出现三次：「:/style.qss」（BASE 压平）、"
                      << "「:/assets/style.qss」（不压平）、「:/legacy/style.qss」（.qrc 的 prefix）。";

    qInfo().noquote() << "── 1. 路线 B：qt_add_resources，不带 BASE ──";
    const QByteArray styleB = readResource(QStringLiteral(":/assets/style.qss"));

    qInfo().noquote() << "── 2. 路线 B+：同一个宏，带 BASE assets（路径被压平）──";
    const QByteArray styleFlat = readResource(QStringLiteral(":/style.qss"));
    const QByteArray logo      = readResource(QStringLiteral(":/logo.svg"));

    qInfo().noquote() << "── 3. 路线 A：AUTORCC + legacy.qrc（prefix=/legacy）──";
    const QByteArray styleA = readResource(QStringLiteral(":/legacy/style.qss"));

    qInfo().noquote() << "三条路径内容相同吗？"
                      << ((styleB == styleFlat && styleB == styleA && !styleB.isEmpty()) ? "相同" : "不同");
    qInfo().noquote() << "  ← 同一份字节在 exe 里存了**三份**。alias 改的是路径，不是去重。"
                      << "第 4 讲那个结论在构建期同样成立。";
    qInfo().noquote() << "logo.svg 前 5 字节：" << logo.left(5);

    // 顺手证一句「不带 BASE 时到底进了哪个路径」：如果猜的是 :/logo.svg，
    // 上面第 2 段就该读到它；这里再读一次 :/assets/logo.svg，两个都在是因为 B 和 B+ 都写了。
    readResource(QStringLiteral(":/assets/logo.svg"));

    // 资源是只读的：这一句一定会失败，而且失败得悄无声息，必须自己检查返回值。
    QFile ro(QStringLiteral(":/style.qss"));
    const bool openedForWrite = ro.open(QIODevice::WriteOnly);
    qInfo().noquote() << "以 WriteOnly 打开资源成功了吗？" << openedForWrite
                      << "（应为 false）" << ro.errorString();

    // qss 从资源读进来，直接喂给应用。这就是「样式表也编进 exe」的实际用法。
    app.setStyleSheet(QString::fromUtf8(styleFlat));

    QLabel headline(QStringLiteral("这段文字的样式来自 :/style.qss"));
    headline.setObjectName(QStringLiteral("headline"));   // 对应 qss 里的 #headline
    headline.setAlignment(Qt::AlignCenter);
    headline.show();

    QTimer::singleShot(300, &app, &QCoreApplication::quit);
    return app.exec();
}
