// ============================================================================
// main.cpp · 翻译链路的运行期那一半
// ----------------------------------------------------------------------------
// 本工程解决什么问题：
//   构建期（CMake）负责把 .ts 变成 .qm 并塞进资源；运行期要做的是三件事：
//     1) 找到 .qm（这里是 ":/i18n/translations_app_en.qm"）
//     2) QTranslator::load() 装进来
//     3) QCoreApplication::installTranslator() 让它生效
//   然后，tr() 才会去查翻译表；查不到就回落到源码里的中文——这是**优雅降级**，
//   不是错误。
//
// 关于 %n（复数）：
//   tr("当前有 %n 个窗口", nullptr, n) 会按 n 选复数形式。中文没有复数变化，
//   英文有（1 window / 2 windows）。.ts 里对应的条目会带 <numerusform>。
//
// 关于 #include "main.moc"：
//   Greeter 的 Q_OBJECT 写在 .cpp 里，AUTOMOC 不会为 .cpp 生成 moc_main.cpp，
//   约定的钩子就是在类定义之后 include "<本文件名>.moc"。
//
// 运行前须知：
//   · 无人值守：显示 300 毫秒后自动退出。
//   · 可以无头跑：translations_app -platform offscreen
//   · 首次构建时生成的 translations_app_en.ts 是**空的**（源串还没翻译）。
//     想让英文真的出现，用 Qt Linguist 打开它、填译文，再重新构建。
//
// 对应构建目标名：translations_app
// ============================================================================

#include <QApplication>
#include <QLabel>
#include <QLocale>
#include <QTranslator>
#include <QDir>
#include <QTimer>
#include <QDebug>

class Greeter : public QObject
{
    Q_OBJECT
public:
    explicit Greeter(QObject *parent = nullptr) : QObject(parent) {}

    // lupdate 会扫这里，把 "Greeter" 作为上下文、把这两句原文收进 .ts。
    QString line() const { return tr("你好，Qt。这句话可以被翻译。"); }
    QString windowCount(int n) const { return tr("当前有 %n 个窗口", nullptr, n); }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    qInfo().noquote() << "系统语言：" << QLocale::system().name()
                      << " uiLanguages：" << QLocale::system().uiLanguages();

    // ── 第一步：看看资源里到底有哪些 .qm ───────────────────────────────────
    const QStringList qmFiles = QDir(QStringLiteral(":/i18n")).entryList({QStringLiteral("*.qm")});
    qInfo().noquote() << ":/i18n 里的 qm 文件：" << qmFiles;

    // ── 第二步：按优先级尝试装载 ──────────────────────────────────────────
    // 顺序很重要：先系统语言，再英文。第一个装载成功的留下。
    QTranslator translator;
    bool installed = false;

    QStringList candidates;
    candidates << QLocale::system().name();          // 例如 zh_CN
    candidates << QStringLiteral("en");             // 兜底

    for (const QString &lang : candidates) {
        if (lang.isEmpty() || lang == QLatin1String("zh_CN"))
            continue;                               // 源码语言不需要翻译
        const QString path = QStringLiteral(":/i18n/translations_app_%1.qm").arg(lang);
        if (translator.load(path)) {
            app.installTranslator(&translator);
            installed = true;
            qInfo().noquote() << "已装载翻译：" << path;
            break;
        }
        qInfo().noquote() << "没有可用的翻译：" << path << "（回落源码语言）";
    }

    qInfo().noquote() << "翻译是否生效：" << installed;

    // ── 第三步：tr() 现在会走翻译表；查不到就回中文 ───────────────────────
    Greeter greeter;
    qInfo().noquote() << "句子：" << greeter.line();
    qInfo().noquote() << "复数 1：" << greeter.windowCount(1);
    qInfo().noquote() << "复数 3：" << greeter.windowCount(3);

    QLabel label(greeter.line() + QStringLiteral("\n") + greeter.windowCount(3));
    label.setAlignment(Qt::AlignCenter);
    label.setMinimumSize(520, 120);
    label.show();

    QTimer::singleShot(300, &app, &QCoreApplication::quit);
    return app.exec();
}

// 写在 .cpp 里的 Q_OBJECT 类：把 moc 生成的东西展开进来。
#include "main.moc"
