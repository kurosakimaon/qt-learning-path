// ============================================================================
// 第 10 讲 · 国际化（i18n）：tr 的三个参数、上下文、复数与 QLocale
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   「把中文塞进 tr() 就算支持多语言了」是 Qt 里流传最广的一句错话。本讲七块硬货：
//     1) 为什么不能写裸字符串：能被 tr() 包住的**字符串字面量**才会被 lupdate 抽进 .ts，
//        拼出来的 QString 对翻译工具是隐形的——抽不到，就永远翻不了；
//     2) tr() 的三个参数各是什么：sourceText / comment（给译者看的歧义注释）/ n（复数数量）。
//        ⚠ 第二个参数**不是**上下文（context）！上下文来自「调用点所在类的类名」；
//        想手工指定上下文只有一条路：QCoreApplication::translate(context, key, disambiguation, n)
//        （核对签名：QtCore/qcoreapplication.h 第 190~193 行，四个参数一字不差）；
//     3) 复数形式：tr("1 file", "%n files", n) 与 tr("%n file(s) found", "", n) 两种写法
//        都落在同一个三参签名上；%n 是 **tr 自己在查表时填进去的**，
//        QString::arg() 填不了它（本讲有对照组：arg 之后 "%n" 原样躺在那儿）；
//     4) QTranslator：load() 的三个重载（文件名/目录/分隔符/后缀）、catalog 命名规则、
//        installTranslator / removeTranslator（**removeInstalledTranslator 本机头文件里不存在**）；
//     5) Linguist 工作流：lupdate 抽 .ts → 译者在 Linguist 里填 → lrelease 编 .qm →
//        运行时 QTranslator 装载（本讲只写流程 + 用本机实测说清「有没有这套工具」）；
//     6) 布局方向：QGuiApplication::layoutDirection() / setLayoutDirection() / isRightToLeft()
//        ——RTL（Right-To-Left，从右往左）不只是文字反着排，整套布局是**镜像**的；
//     7) QLocale：同一个数字、同一个日期在不同 locale 下的不同长相。
//        这一大块是**本机不靠 .qm 也能跑出真结果**的部分，所以演示主体压在它身上。
//
// 对应构建目标名：widgets_10_internationalization
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(widgets_10_internationalization 10_internationalization.cpp)
//   target_link_libraries(widgets_10_internationalization PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)   # 本文件里定义了 Q_OBJECT 类（AlphaProbe / BetaProbe），
//                           # 所以**必须**在文件末尾 #include "10_internationalization.moc"
//
// 运行前须知：
//   · 依赖 Qt6::Widgets（QApplication）。但要清楚各件东西住在哪个库：
//     tr / QCoreApplication::translate / QTranslator / QLocale 全在 **Qt6::Core**；
//     layoutDirection 那三个在 **Qt6::Gui**；只有把界面画出来才需要 Qt6::Widgets。
//     Windows 上要能找到 platforms/qwindows.dll；无桌面用 -platform offscreen 照样出数。
//   · 本机当前无法编译链接 Qt 运行时，本示例**未在本机实测编译**；每个类/函数/重载/枚举值
//     都对着 C:\msys64\ucrt64\include\qt6 下的真实头文件逐个核对（行号写在用到的地方）。
//     Qt 版本可在头文件里核到：QtCore/qconfig.h 第 73 行 `#define QT_VERSION_STR "6.12.0"`。
//     核对不到签名的 API，本讲一律不用（下面「诚实清单」里点名了三个）。
//   · 【本机实测的工具与翻译文件现状】——不是抄文档，是在这台机器上 ls / find 出来的：
//       - lupdate / lrelease / linguist **在**：C:\msys64\ucrt64\bin\lupdate.exe、
//         lrelease.exe、linguist.exe（另有两个 -pro 后缀的姊妹程序）。
//         注意它们**不在** C:\msys64\ucrt64\share\qt6\bin —— 那个目录里是
//         moc.exe / uic.exe / rcc.exe / qhelpgenerator.exe / qlalr.exe 这一批；
//       - **一个 .qm 都没有**：C:\msys64\ucrt64\translations 目录**不存在**，
//         `find C:\msys64\ucrt64 -maxdepth 3 -name "*.qm"` 返回空。
//         → 本机**没有** qtbase_zh_CN.qm。Qt 自身控件的中文界面翻译要另装
//           （MSYS2 的 qt6 翻译子包，通常叫 qt6-translations；本机正在升级运行时，
//           我没去装，只报「现在没有」这个事实）；
//       - 我确实在本机跑通了一个代码生成工具：share/qt6/bin/moc.exe 对着一份带 Q_OBJECT 的
//         临时头文件**成功**生成了 moc_greeter.cpp（这一步是真跑过的，说明 moc 可用）。
//         但在那份生成产物里 grep 不到 `tr(` 的实现体——连 qt_meta_stringdata 都没 grep 到，
//         说明**我的 grep 姿势或文件编码有问题，这次验证本身不可信**，所以本讲不把「moc 不抄 tr」
//         当结论用，只当一条待查线索写在这儿。本讲凡是「必须自己指定上下文」的地方，
//         一律直接用签名逐字核到的 QCoreApplication::translate(...)，行为确定、依据更硬。
//   · 演示主体**不依赖任何外部 .qm**。装载 .qm 那段代码照写，但 load() 的返回值会老老实实
//     打印成 false——没加载成功就是没成功，不假装。
//   · 无人值守：QTimer::singleShot 排 7 步，每步 qInfo() 打印证据，最后 QCoreApplication::quit()。
//     **没有任何阻塞调用**：不 exec()、不弹 QMessageBox 静态方法、不 QFileDialog::getOpenFileName、
//     不 QMenu::exec()。全程约 4.2 秒跑完。
//   · Qt5 → Qt6 差异（本讲涉及，均已核对本机头文件）：
//       - tr 的「退化实现」看得见摸得着：QtCore/qobject.h 第 120~123 行，定义了
//         QT_NO_TRANSLATION 时 `static QString tr(const char *sourceText, const char * = nullptr, int = -1)`
//         直接 `return QString::fromUtf8(sourceText);`
//         → 三个参数的形状（源文 / **没有名字**的第二参 / 数量）在这份头文件里一目了然，
//           「第二参不是上下文」也在这句话里；真正带名字的版本见下面 QMetaObject 那条。
//       - QtCore/qobjectdefs.h 第 245~246 行另有一份
//         `QString tr(const char *s, const char *c, int n = -1) const;`——那是 **QMetaObject**
//         的成员（注意是 const 成员函数，不是 static），配合同一行上方的第 237 行
//         `const char *className() const;`，正是「上下文 = 类名」这条规则的出处。
//       - QT_TR_NOOP / QT_TRANSLATE_NOOP 在 Qt6 挪到了独立的 **QtCore/qttranslation.h**
//         （第 20~25 行）：`#define QT_TR_NOOP(x) x`、`#define QT_TRANSLATE_NOOP(scope, x) x`
//         ——宏体就是 x 本身，说明它们**只给 lupdate 看**，对编译器完全透明。
//         Qt5 时代这些宏散在 qcoreapplication.h / qobjectdefs.h 里，老工程里那两处 include 可以精简。
//       - 装/卸翻译器只有两个名字：qcoreapplication.h 第 186/187 行
//         `static bool installTranslator(QTranslator *messageFile)` /
//         `static bool removeTranslator(QTranslator *messageFile)`，
//         两者还被 `#ifndef QT_NO_TRANSLATION` 包着（185/188 行）→ 剪掉翻译的构建里连这两个函数都没有。
//         我在同一份头文件里 grep **不到 removeInstalledTranslator**：网上有些文章在讲它，
//         本机这份 6.12 头文件里没有这个名字，本讲不用（诚实清单一）。
//       - QDate 的头文件在 Qt6 是 **QtCore/qdatetime.h**（没有单独的 qdate.h，我 ls 过）；
//         QDate::toString 的常用两个重载在第 108/109 行。
//       - QLocale 的枚举扩张了：FormatType 现在是**三个**值
//         `enum FormatType { LongFormat, ShortFormat, NarrowFormat }`（qlocale.h 第 881 行），
//         Qt5 只有 LongFormat/ShortFormat；NarrowFormat 在本讲打印出来了，看得见。
//       - QTranslator::load 三个重载在 Qt6 都标了 `[[nodiscard]]`（qtranslator.h 第 34/38/43 行）
//         → 在 Qt6 里把 load() 的返回值丢掉会吃到编译器警告。老代码 `tr.load(...);` 请改成
//           `if (!tr.load(...)) { ... }` 或至少把值打印出来（本讲就是这么做的）。
// ============================================================================

#include <QApplication>          // QtWidgets/qapplication.h
#include <QCoreApplication>      // QtCore/qcoreapplication.h：translate 190 行 / installTranslator 186 行 / quit()
#include <QGuiApplication>       // QtGui/qguiapplication.h：layoutDirection 108~112 行
#include <QWidget>               // QtWidgets/qwidget.h
#include <QLabel>                // QtWidgets/qlabel.h
#include <QVBoxLayout>           // QtWidgets/qboxlayout.h
#include <QObject>               // QtCore/qobject.h：tr 的退化版在 121 行
#include <QTranslator>           // QtCore/qtranslator.h：load 34/38/43 行、isEmpty 30 行、language/filePath 31/32 行
#include <QLocale>               // QtCore/qlocale.h：system 1168 行 / c 1167 行 / toString 1011~1033 行
#include <QDate>                 // ⚠ QtCore/qdatetime.h（Qt6 没有 qdate.h）
#include <QString>
#include <QList>
#include <QTimer>                // QtCore/qtimer.h：singleShot
#include <QDebug>                // QtCore/qdebug.h：qInfo()

// ============================================================================
// 两个「只差类名」的探测类：专门用来把「上下文 = 调用点类名」这件事打印出来
// ----------------------------------------------------------------------------
// 【为什么故意写两个几乎一样的类】它们返回的源文字符串**一模一样**（都是 "Open"、
// 都是 "打开"），唯一差别是类名。有了 .qm 之后，同一个 "Open" 在两个上下文里
// 可以被译成完全不同的词（菜单里是「打开」，开关语境可能是「开启」）。
// 这就教会你一件事：翻译的查表键是 **(上下文, 源文, 歧义注释, 数量)** 四元组，
// 光看源文是猜不出结果的。
// ============================================================================
class AlphaProbe : public QObject
{
    Q_OBJECT                       // 给 moc 的标记：有了它才有 staticMetaObject
public:
    explicit AlphaProbe(QObject *parent = nullptr) : QObject(parent) {}

    // 上下文自动 = "AlphaProbe"（来自 staticMetaObject.className()，qobjectdefs.h 237 行）
    QString openLabel() const { return tr("Open"); }

    // 第二参是**给译者看的注释**（disambiguation），不会出现在返回字符串里，
    // 也**不是**上下文——本讲用「两次调用结果相等」把这条钉死。
    QString openWithComment() const
    {
        return tr("Open", "这里是菜单动作「打开文件」，不是「开源软件」的那个 open");
    }

    // 中文源文照样能进 tr()：lupdate 会把它当 sourceText 抽出来
    QString openChinese() const { return tr("打开"); }

    // 复数写法一：两条文本 + n（传统写法，n 用来选单/复数槽位）
    QString fileCount(int n) const { return tr("1 file", "%n files", n); }

    // 复数写法二：一条带 %n 的文本 + 空注释 + n（Qt 文档里最常见的那个数数写法）
    QString found(int n) const { return tr("%n file(s) found", "", n); }

    // 把「本类的 tr 用的上下文到底是谁」直接读出来给你看
    static const char *contextOfChoice() { return staticMetaObject.className(); }
};

class BetaProbe : public QObject
{
    Q_OBJECT
public:
    explicit BetaProbe(QObject *parent = nullptr) : QObject(parent) {}

    QString openLabel() const { return tr("Open"); }      // 与 AlphaProbe 源文完全相同
    QString openChinese() const { return tr("打开"); }

    static const char *contextOfChoice() { return staticMetaObject.className(); }
};

// 演示用的 locale 清单（每次现取，别存成静态变量——静态化 tr/QLocale 结果本身就是坑）
static QList<QLocale> demoLocales()
{
    QList<QLocale> list;
    list.append(QLocale::system());                              // qlocale.h 1168 行
    list.append(QLocale(QStringLiteral("zh_CN")));                // qlocale.h 924 行（按名字构造）
    list.append(QLocale(QStringLiteral("en_US")));
    list.append(QLocale(QStringLiteral("de_DE")));
    list.append(QLocale::c());                                   // qlocale.h 1167 行：C locale，「不受本地习惯影响」那一档
    return list;
}

// 把 Qt::LayoutDirection 翻译成人类可读的字符串再打印。
// 【为什么不让 qInfo() 直接打枚举】枚举会被打成一串数字，你看不出所以然；
// 而我**没有**在本机头文件里逐字核到「调试输出器怎么渲染这个枚举」，所以不猜。
static QString dirName()
{
    return QGuiApplication::isRightToLeft() ? QStringLiteral("RightToLeft")
                                            : QStringLiteral("LeftToRight");
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);   // QtWidgets/qapplication.h：i18n 的运行时地基（翻译器表挂在 QCoreApplication 上）

    // ── 一个真实窗口，好让「镜像布局」不只是嘴上说说 ──────────────────
    QWidget window;
    window.setWindowTitle(QCoreApplication::translate("MainWindow", "第 10 讲 · 国际化演示"));
    window.resize(520, 200);

    QVBoxLayout *layout = new QVBoxLayout(&window);   // 所有权：layout 归 window（第 7 讲讲过谁删谁）
    QLabel *greeting = new QLabel(&window);
    greeting->setText(QCoreApplication::translate("MainWindow", "已找到 %1 个文件").arg(3));
    layout->addWidget(greeting);
    QLabel *hint = new QLabel(&window);
    hint->setText(QCoreApplication::translate("MainWindow", "注意这一行文字贴在哪一边"));
    layout->addWidget(hint);
    window.show();                                     // 第 1 讲的老规矩：show 只是挂号

    qInfo() << "══ 第 10 讲 · 国际化 ══  Qt 版本编译期宏见顶部（本机 6.12.0）";

    // ── ① 600ms：上下文到底是什么 ────────────────────────────────────
    QTimer::singleShot(600, [&window, greeting] {
        AlphaProbe a;
        BetaProbe b;
        qInfo() << "① 上下文（context）从哪来 —— 打印两个类的 className()：";
        qInfo() << "   AlphaProbe::staticMetaObject.className() =" << AlphaProbe::contextOfChoice();
        qInfo() << "   BetaProbe ::staticMetaObject.className() =" << BetaProbe::contextOfChoice();

        // 没有 .qm 时，四条路都只会把**源文**原样还给你。这正是要你看清楚的证据：
        // 差异不在这里发生，差异发生在 .qm 里那两个上下文各自的译文条目上。
        qInfo() << "   a.tr(\"Open\")                          =" << a.openLabel();
        qInfo() << "   b.tr(\"Open\")  ← 源文相同、上下文不同 =" << b.openLabel();
        qInfo() << "   translate(\"AlphaProbe\",\"Open\")      ="
                << QCoreApplication::translate("AlphaProbe", "Open");
        qInfo() << "   translate(\"BetaProbe\",\"Open\")       ="
                << QCoreApplication::translate("BetaProbe", "Open");
        qInfo() << "   translate(\"FileMenu\",\"Open\")        ="
                << QCoreApplication::translate("FileMenu", "Open");
        qInfo() << "   translate(\"MainWindow\",\"Open\")      ="
                << QCoreApplication::translate("MainWindow", "Open");

        // 第二参不是上下文：加了歧义注释，结果字符串一点没变（注释不会出现在返回值里）
        qInfo() << "   a.tr(\"Open\") =" << a.openLabel()
                << " / a.tr(\"Open\", 一段中文注释) =" << a.openWithComment()
                << " → 两者相等：第二参是 comment，不是 context";
        qInfo() << "   中文源文也一样进表：a.tr(\"打开\") =" << a.openChinese()
                << "  b.tr(\"打开\") =" << b.openChinese();
        qInfo() << "   窗口标题（translate(\"MainWindow\", …) 的结果）：" << window.windowTitle();
        qInfo() << "   标签文本（.arg 填的是 %1，**不是** %n）：" << greeting->text();
    });

    // ── ② 1100ms：复数与 %n 归谁填 ──────────────────────────────────
    QTimer::singleShot(1100, [] {
        AlphaProbe a;
        qInfo() << "② 复数（plural）：n 决定选哪个槽位，%n 由 tr 自己填";
        const int samples[] = {0, 1, 2, 5};
        for (int n : samples) {
            qInfo() << "   tr(\"1 file\", \"%n files\", n=" << n << ") ->" << a.fileCount(n);
        }
        for (int n : samples) {
            qInfo() << "   tr(\"%n file(s) found\", \"\", n=" << n << ") ->" << a.found(n);
        }
        // 对照组：arg() 手里没有 %n 这个占位符，所以它一个字也填不上。
        // 【为什么必须 tr 自己填】因为「填几个、填哪一种形态」取决于该 locale 的
        // **复数规则**（俄语三套、法语两套、中文一套），这是查表期的决定，
        // 等你拿到 QString 再 .arg() 已经太晚了——形态早定完了。
        qInfo() << "   反面教材：QString::fromUtf8(\"%n files\").arg(3) ="
                << QString::fromUtf8("%n files").arg(3) << " ← %n 原样躺在那儿，arg 无能为力";
        qInfo() << "   （诚实提醒：本机没有任何 .qm，上面打出来的是「查不到译文时的退化路径」；"
                   "把它当形态证据，别当最终译文承诺。）";
    });

    // ── ③ 1600ms：QTranslator 装载（本机必然失败，那就打印失败） ─────
    QTimer::singleShot(1600, [] {
        qInfo() << "③ QTranslator：装载路径与 catalog 命名规则";
        QTranslator catalog;                        // QtCore/qtranslator.h 23 行

        // 第一个重载（qtranslator.h 34~37 行）：
        //   bool load(const QString &filename,
        //             const QString &directory = QString(),
        //             const QString &search_delimiters = QString(),
        //             const QString &suffix = QString())
        // 语义：在 directory 里找「文件名的主体 = filename、语言标记按 delimiters 切出来、
        // 后缀 = suffix」的那个 .qm，并按**当前 locale** 去匹。所以 catalog 的命名要遵守
        // `<文件名><分隔符><语言_国家>.qm` 这一套，Qt 自己的翻译就叫 `qtbase_zh_CN.qm`。
        // 默认分隔符与后缀由实现决定（头文件里默认值是空串），Qt 文档给的是「_.」与「.qm」；
        // 这里我把两个都**显式写出来**，就是为了不依赖我核不到的默认值。
        const QString sysDir = QStringLiteral("C:/msys64/ucrt64/translations");
        const bool okBase = catalog.load(QStringLiteral("qtbase"), sysDir,
                                         QStringLiteral("_."), QStringLiteral(".qm"));
        qInfo() << "   load(\"qtbase\", \"C:/msys64/ucrt64/translations\", \"_.\", \".qm\") 返回 ="
                << okBase << " ← 本机 translations 目录不存在，所以是 false（不假装成功）";
        qInfo() << "   isEmpty() =" << catalog.isEmpty()
                << "  language() =" << catalog.language()
                << "  filePath() =" << catalog.filePath();

        // 第二个重载（38~42 行）：显式给 locale + 文件名 + 前缀 + 目录 + 后缀，
        // 适合应用自己的 catalog（例：myapp_zh_CN.qm）。
        QTranslator appCat;
        const bool okApp = appCat.load(QLocale(QStringLiteral("zh_CN")),
                                       QStringLiteral("myapp"),
                                       QStringLiteral("_"),
                                       sysDir,
                                       QStringLiteral(".qm"));
        qInfo() << "   load(locale, \"myapp\", \"_\", 目录, \".qm\") 返回 =" << okApp
                << " ← 没有 .qm 就是没有；装了也白装，所以这里**不** installTranslator";

        // 第三个重载（43~44 行）：load(const uchar *data, int len, directory)
        // ——把 .qm 嵌进资源（qrc）再从内存装载，单文件发布常用。本讲没有 .qm 字节流，不调它。

        // 装卸只有一进一出两个名字（qcoreapplication.h 186/187 行）：
        //   static bool installTranslator(QTranslator *messageFile);
        //   static bool removeTranslator(QTranslator *messageFile);
        // removeInstalledTranslator：本机头文件里 grep 不到 → 不用（见顶部差异条目）。
        qInfo() << "   removeTranslator(&catalog) 的返回值 ="
                << QCoreApplication::removeTranslator(&catalog)
                << " ← 从没 install 过，摘不掉，返回 false；这条也是证据";
        qInfo() << "   切语言的正规顺序：removeTranslator(旧的) → delete → 新 QTranslator → load → "
                   "installTranslator(新的)；装完 Qt 会广播 QEvent::LanguageChange，"
                   "各控件在 changeEvent 里把 tr() 结果重新 set 一遍（本讲不演示 changeEvent："
                   "QWidget::changeEvent 的签名我没在本机头文件里逐字核到，不凭记忆写）。";
    });

    // ── ④ 2100ms：QLocale 看数字 ────────────────────────────────────
    QTimer::singleShot(2100, [] {
        qInfo() << "④ QLocale：同一个数字的不同长相（这部分**不需要 .qm**，全是真结果）";
        const QList<QLocale> locales = demoLocales();
        for (const QLocale &loc : locales) {
            // toString(int) 995 行 → 内部转给 toString(qlonglong)；分组符（千分位）归 locale 管
            // toString(double, char format = 'g', int precision = 6) 1011 行
            qInfo() << "   locale" << loc.name()                             // name() 944 行
                    << " 1234567 ->" << loc.toString(1234567)
                    << "  1234.5 ->" << loc.toString(1234.5);
        }
        qInfo() << "   结论：数字是「人看的」还是「程序看的」，写法完全不同。"
                   "要稳定可解析请用 QLocale::c()（或直接 QString::number），"
                   "别拿系统 locale 去生成要写进文件/配置的数字。";
    });

    // ── ⑤ 2600ms：QLocale 看日期 + 用字符串存日期的坑 ────────────────
    QTimer::singleShot(2600, [] {
        const QDate d(2026, 3, 1);
        qInfo() << "⑤ 同一个 QDate(2026, 3, 1)，四种面孔：";
        // QDate::toString(Qt::DateFormat) 在 qdatetime.h 108 行；枚举值在 qnamespace.h
        // 1262（TextDate）/ 1263（ISODate）/ 1265（ISODateWithMs）
        qInfo() << "   d.toString(Qt::ISODate)  =" << d.toString(Qt::ISODate)
                << " ← 与 locale 无关，**该用它**来存储和写日志";
        qInfo() << "   d.toString(Qt::TextDate) =" << d.toString(Qt::TextDate);
        qInfo() << "   d.toString(\"yyyy年MM月dd日\") ="
                << d.toString(QStringLiteral("yyyy年MM月dd日"));

        const QList<QLocale> locales = demoLocales();
        for (const QLocale &loc : locales) {
            // toString(QDate, FormatType) 在 qlocale.h 1023 行（FormatType 三个值：881 行）
            // dateFormat(FormatType) 1037 行、monthName(int, FormatType) 1083 行、
            // dayName(int, FormatType) 1085 行
            qInfo() << "   " << loc.name()
                    << " Long =" << loc.toString(d, QLocale::LongFormat)
                    << " | Short =" << loc.toString(d, QLocale::ShortFormat)
                    << " | Narrow =" << loc.toString(d, QLocale::NarrowFormat)
                    << " | 模板 =" << loc.dateFormat(QLocale::LongFormat)
                    << " | 三月 =" << loc.monthName(3, QLocale::LongFormat)
                    << " | 星期日名 =" << loc.dayName(d.dayOfWeek(), QLocale::LongFormat);
        }

        // 把日期当字符串读回来的代价：同一串 "01/03/2026" 在不同 locale 下是**两个不同的日子**
        qInfo() << "   同一串 \"01/03/2026\" 用 toDate(..., ShortFormat) 解（qlocale.h 1046 行）：";
        for (const QLocale &loc : locales) {
            const QDate back = loc.toDate(QStringLiteral("01/03/2026"), QLocale::ShortFormat);
            qInfo() << "     " << loc.name() << " ->" << back
                    << (back.isValid() ? "  有效" : "  无效")
                    << (back.isValid() ? QStringLiteral("  (%1年%2月%3日)")
                                              .arg(back.year()).arg(back.month()).arg(back.day())
                                       : QString());
        }
        qInfo() << "   这就是「把日期硬编码成字符串」的病根：字符串没有时区、没有 locale、"
                   "没有「3 月 1 日还是 1 月 3 日」的答案。存 QDate/QDateTime（或 ISO 文本），"
                   "只在给人看的那一刻才格式化。";
    });

    // ── ⑥ 3100ms：布局方向（LTR / RTL） ─────────────────────────────
    QTimer::singleShot(3100, [&window, hint] {
        qInfo() << "⑥ 布局方向（layout direction）：RTL 是整套镜像，不是文字反排";
        // 核对签名（QtGui/qguiapplication.h）：
        //   108 行 static void setLayoutDirection(Qt::LayoutDirection direction);
        //   109 行 static Qt::LayoutDirection layoutDirection();
        //   111 行 static inline bool isRightToLeft() { return layoutDirection() == Qt::RightToLeft; }
        //   112 行 static inline bool isLeftToRight()
        qInfo() << "   现在 =" << dirName() << "  isLeftToRight =" << QGuiApplication::isLeftToRight();

        QGuiApplication::setLayoutDirection(Qt::RightToLeft);
        qInfo() << "   setLayoutDirection(Qt::RightToLeft) 之后 =" << dirName()
                << "  isRightToLeft =" << QGuiApplication::isRightToLeft();
        hint->setText(QCoreApplication::translate("MainWindow", "注意这一行文字贴在哪一边"));
        qInfo() << "   同一个标签，重新 setText 后仍在布局里（贴边方向跟着应用级方向走）："
                << hint->text();
        qInfo() << "   窗口标题不变（标题是**内容**，方向是**排版**）：" << window.windowTitle();

        QGuiApplication::setLayoutDirection(Qt::LeftToRight);   // 复原，别把后面的步骤带沟里
        qInfo() << "   复原后 =" << dirName();
        qInfo() << "   另有命令行选项 -reverse（第 1 阶段第 1 讲列过）等价于设成 RTL；"
                   "widget 级也能各自设方向，但 QWidget::setLayoutDirection 的签名我没在本机"
                   "逐字核到，本讲只演示**应用级**这一条。";
    });

    // ── ⑦ 3600ms：三条坑的对照实验 ───────────────────────────────────
    QTimer::singleShot(3600, [] {
        AlphaProbe a;
        qInfo() << "⑦ 三条坑，跑给你看：";

        // 坑一：拼接句子。英语的语序、中文的语序、德语的动词位置各不相同，
        // 把句子劈成三段分别 tr 再拼起来，等于把语序写死在代码里 → 不可译。
        const QString bad = QString::fromUtf8("Found ") + QString::number(3) + QString::fromUtf8(" files");
        const QString good = a.found(3);
        qInfo() << "   错（拼出来的，lupdate 抽不到、语序被写死）：" << bad;
        qInfo() << "   对（整句进 tr，%n 由 tr 填）                ：" << good;

        // 坑二：把 tr 的结果存进 static/成员，切语言后不会自己刷新。
        static QString cached = a.openLabel();          // ← 故意：这就是「翻译冻结」的写法
        qInfo() << "   static 缓存的 tr 结果（切语言后它就烂在库里）：" << cached;

        // 坑三：构造函数里的 tr 时机。tr 的上下文取**写调用点那个类**的类名。
        // 于是有两条真实的坑：(1) 在基类构造函数里 tr()，上下文是基类名，
        // 派生类想在 .qm 里单独给一句译文就命中不了（本讲没逐字核到 tr 的接线细节，
        // 这条只作为「提问」留给你，不作为结论）；(2) 构造函数只跑一次，
        // 切语言时它不会重跑，所以别在构造里把 tr 结果算死存进成员。
        // 实践做法：需要翻译的文本要么每次现取（函数里调 tr），要么在 retranslate
        // 里配合 QEvent::LanguageChange 重新 set 一遍。
        qInfo() << "   构造函数里的 tr：上下文 = 写调用点那**一行所在的类**名；"
                   "本讲两个 Probe 的 openLabel() 源文相同、上下文不同，就是这个道理的最小样本。";

        qInfo() << "   裸字符串那条再念一遍：只有被 tr()/QT_TR_NOOP 包住的**字面量**才会被 "
                   "lupdate 抽进 .ts。像 `QString s = 「打开文件」;` 这样光写个中文串，"
                   "工具抽不到，将来怎么翻？"
                   "（QT_TR_NOOP 宏体就是 x 本身，见 QtCore/qttranslation.h 20 行——它只给工具看。）";
    });

    // ── 4100ms：收尾退出 ────────────────────────────────────────────
    QTimer::singleShot(4100, [] {
        qInfo() << "全部 7 步打印完毕，自动退出（全程无阻塞调用）";
        QCoreApplication::quit();
    });

    int rc = app.exec();
    qInfo() << "事件循环退出，exec() 返回" << rc;
    return rc;
}

// ============================================================================
// 附一：Linguist 工作流（四步，注释里说清；本机工具实测状态见顶部）
// ============================================================================
/*
 * 1) 抽取（lupdate）：扫源码，把所有 tr("…") / QT_TR_NOOP("…") /
 *    QCoreApplication::translate(ctx, "…") 的字面量连同「上下文 + 源文 + 注释」写进 .ts（XML，给人改）。
 *       C:\msys64\ucrt64\bin\lupdate.exe  <源码目录> -recursive -ts  myapp_zh_CN.ts
 *    ⚠ 上面这条命令我没在本机跑过（本机编不动 Qt，跑它也没意义），参数集请以
 *      `lupdate -help` 为准；工具**存在**这件事是我 ls 出来的，别把两者混为一谈。
 *      Qt6 的 CMake 工程也可以用 qt_add_lupdate / qt_add_translations 这一套管线的写法，
 *      本机头文件里核不到，所以不展开，只提个名字让你知道去哪儿搜。
 *
 * 2) 翻译（Linguist GUI，或直接改 .ts）：逐条填 translation；带 n 的条目要填**每个复数形态**；
 *    上下文不同的同名源文是两条独立条目（这就是第 ① 步打印的那个差别）。
 *
 * 3) 编译（lrelease）：.ts → .qm（二进制，运行时只查表、不解析 XML）。
 *       C:\msys64\ucrt64\bin\lrelease.exe  myapp_zh_CN.ts  -qm  myapp_zh_CN.qm
 *
 * 4) 运行时装载（本文件第 ③ 步的代码）：
 *       QTranslator t;
 *       if (t.load(QLocale::system(), "myapp", "_", appDir + "/i18n", ".qm"))
 *           QCoreApplication::installTranslator(&t);
 *    Qt **自身控件**的翻译（右键菜单、对话框按钮那套）是另一个 catalog：
 *    `qtbase_zh_CN.qm`，通常从 Qt 安装目录的 translations/ 里取——**本机这个目录不存在**，
 *    所以本讲没有假装加载它，只是老老实实打印 load() 返回 false。
 *
 * 一句话记牢：.ts 是给**人**看的（可编辑），.qm 是给**程序**读的（发布产物），
 * tr() 只负责在运行时把「上下文 + 源文 + 注释 + 数量」这张查询条递进去。
 */

// ============================================================================
// 附二：本讲的「诚实清单」（核不到就不用的三样东西）
// ============================================================================
/*
 *   1) QCoreApplication::removeInstalledTranslator —— 本机 qcoreapplication.h 里 grep 不到，
 *      只有 installTranslator(186) / removeTranslator(187)。网上同名说法一律不采信。
 *   2) QWidget::setLayoutDirection / layoutDirection —— 本讲只用了**应用级**那三个
 *      （qguiapplication.h 108/109/111/112 行，逐字核到），widget 级同名接口我没逐字核签名，
 *      所以代码里没写，只在注释里提了一句「也能各自设」。
 *   3) QWidget::changeEvent(QEvent *) 与 QEvent::LanguageChange —— 语义我讲清了（切语言后
 *      重设文本），但**没有**在代码里覆写演示，因为签名与枚举值这次没核；
 *      要写这段代码之前，先去看你本机 QtWidgets/qwidget.h 与 QtCore/qnamespace.h。
 *   顺带一条**没验成**的线索：我用本机 share/qt6/bin/moc.exe 跑通过一次生成
 *   （moc_greeter.cpp 出来了），但对那份产物 grep 连常见符号都没命中，说明我的验证姿势有问题，
 *   所以「Qt 6.12 的 tr 实现到底抄在哪」这条我不下结论。做法上回避它：
 *   要指定上下文，就用签名逐字核到的 QCoreApplication::translate，别依赖生成机制的细节。
 */

// ============================================================================
// 老师傅的话 + 小练习
// ============================================================================
/*
 * ——— 老师傅的话 ———
 *   徒弟，国际化这块，新手十有八九栽在两个地方，今天师傅给你点破。
 *
 *   第一个：**把 tr() 当成「翻译函数」在用**。它不是。它是一张查询条的生成器——
 *   上面写着「我是哪个类（上下文）、原文是什么、我给译者留了什么注释、这次有几件（n）」。
 *   你真该关心的是那张 .qm 表里有没有对应的条目。所以：源文要写成**整句**、
 *   字面量要能被 lupdate 抽到、上下文靠类名走、需要精确控制就换 translate()。
 *   你今天打印的四条「结果都一样」不是失败——那正是「表里没有译文」的证据。
 *
 *   第二个：**把翻译和格式化混成一锅**。QLocale 管的是「这个数字/日期给人看时长什么样」，
 *   tr 管的是「这句话怎么换成另一种语言」。两码事。日期一旦以字符串形态进了代码、进了文件，
 *   你就把「3 月 1 日」和「1 月 3 日」的赌注押在了读它的人身上——本讲 ⑤ 里那串
 *   "01/03/2026" 在不同 locale 下解出不同日子，就是这个赌局开牌。存 QDate、
 *   存 ISO，只在最后一刻格式化给人看。
 *
 *   还有句实在话：本讲我把本机有没有 lupdate、有没有 .qm 都 ls 了一遍写在注释里。
 *   你以后写 i18n 也要这个习惯——**先确认环境里到底有没有那份 .qm**，再决定是
 *   「我的翻译没生效」还是「压根没东西可加载」。这两种病的药方完全不同。
 *
 * ——— 小练习 ———
 *   1. 把 AlphaProbe::openLabel() 的源文从 "Open" 改成 "打开"，再用本机的
 *      C:\msys64\ucrt64\bin\lupdate.exe 对这个文件抽一次 .ts，打开 XML 亲眼看
 *      「上下文 = AlphaProbe、源文、注释」三样各长在哪一行；然后把第 ② 步里那条
 *      带中文注释的 tr() 也抽进去，比较 .ts 里 comment 字段的差别。
 *   2. 自己造一份最小的 .qm（哪怕只翻 "Open" → "开启" 一条），放到一个你新建的目录里，
 *      把第 ③ 步的 load() 目录改成它，让 okBase 真的返回 true，然后在装载前后各打印一次
 *      translate("AlphaProbe","Open")——亲眼看到「表里有 / 表里没有」的差别，
 *      再按 removeTranslator → installTranslator 的顺序验证切语言。
 *   3. 在第 ⑤ 步的循环里，把 QLocale(QStringLiteral("ar_SA"))（阿拉伯语，RTL 常用）
 *      加进 demoLocales()，配合第 ⑥ 步的 setLayoutDirection(Qt::RightToLeft)，
 *      观察标签贴边方向与数字渲染的变化；再把系统 locale 用 QT_LOCALE 之类环境变量
 *      换掉跑一次，比较 ④ 里 1234567 分组符的差别。
 */
// ============================================================================

#include "10_internationalization.moc"   // 本文件内定义了带 Q_OBJECT 的 AlphaProbe / BetaProbe，AUTOMOC 生成此文件
