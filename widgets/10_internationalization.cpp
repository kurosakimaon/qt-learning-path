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
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 widgets_10_internationalization，跑到自动退出、退出码 0）。
//     第 ⑧ 步带一条**19 项的自检台账**（自带 .qm 的装载、查表的十一种情形，加上
//     GaugePanel 三句界面文字在 install/remove 两端的实际显示），不符就把
//     进程退出码改成 1，所以那十九行是被 ctest 盯着的；其余 ①~⑦ 步只打印不判卷：
//     带具体数字的「预期」请以你自己那次运行输出为准（本机那一轮存档 runlogs/widgets_10_internationalization.log）。
//   · 【本机实测的工具与翻译文件现状】——不是抄文档，是在这台机器上查出来的。
//     ⚠ 这一段是**环境事实**，不是 Qt 语义：换一台机器、换一种安装方式就可能整个反过来，
//     所以第 ③ 步的代码一律用 `QLibraryInfo::path(QLibraryInfo::TranslationsPath)` 现查现印，
//     结论只看它当场打印的那一行，不看我下面写的这一轮。
//       - lupdate / lrelease / linguist **在** D:\Qt\6.12.0\mingw_64\bin（本机那一轮 ls 到 3 个 .exe）；
//         同一目录里还有 moc.exe / uic.exe / rcc.exe / qhelpgenerator.exe / qlalr.exe。
//       - 翻译目录**存在**：`QLibraryInfo::path(TranslationsPath)` 指到
//         D:/Qt/6.12.0/mingw_64/translations，本机数了一下有 **314 个 .qm**，
//         其中就有 qtbase_zh_CN.qm → 所以第 ③ 步 `load("qtbase", …)` 在这台机器上返回 **true**。
//         早先这一版写的是「本机一个 .qm 都没有、目录不存在、load 必然 false」——那是
//         运行时还没装全时的老实话，**环境变了就得改口径**；现在改成两种结果都算正常，
//         示例把返回值原样打出来，并跟着返回值解释「你这次为什么是 true / false」。
//       - moc.exe 也在同一个 bin 里，`--version` 自报 "moc 6.12.0"（第 贰阶段第 5 讲用它取证）。
//       - 早先写过一条「在生成的 moc 产物里 grep 不到 tr( 实现体」的线索，那是我自己的 grep
//         姿势或编码有问题，验证不可信，本讲不把它当结论用。凡是「必须自己指定上下文」的地方，
//         一律直接用签名逐字核到的 QCoreApplication::translate(...)，行为确定、依据更硬。
//   · 演示主体**不依赖任何外部 .qm**：① ② ④ ⑤ ⑥ ⑦ 全在 tr/QLocale 这一层，本机必然有结果。
//     ③ 装载系统 catalog 那段照写，返回值老老实实打印——true 就顺手 install 一次给你看效果，
//     false 就说明你这套 Qt 没带翻译子包（不假装成功，也不因为环境差异把 ctest 判红）。
//   · 无人值守：QTimer::singleShot 排 8 步（第 ⑧ 步自带 19 项台账，会额外 show 一个窗口），
//     每步 qInfo() 打印证据，最后 QCoreApplication::quit()。
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
#include <QTranslator>           // QtCore/qtranslator.h：load 34/38/43 行、isEmpty 29 行、language/filePath 31/32 行
#include <QLibraryInfo>          // QtCore/qlibraryinfo.h：TranslationsPath 40 行 / path(LibraryPath) 49 行
#include <QDir>                  // QtCore/qdir.h：翻译目录是否存在、.qm 列表、filePath
#include <QFile>                 // QtCore/qfile.h：把 .qm 读进内存喂给 load(uchar*, len)
#include <QLocale>               // QtCore/qlocale.h：system 1168 行 / c 1167 行 / toString 1011~1033 行
#include <QDate>                 // ⚠ QtCore/qdatetime.h（Qt6 没有 qdate.h）
#include <QString>
#include <QList>
#include <QTimer>                // QtCore/qtimer.h：singleShot
#include <QDebug>                // QtCore/qdebug.h：qInfo()

// ============================================================================
// QTLP_I18N_DIR：本讲第 ⑧ 步要去哪儿找 gauge_zh_CN.qm
// ----------------------------------------------------------------------------
// 顶层 CMakeLists.txt 用 target_compile_definitions 把它定义成**绝对路径**，
// 所以 ctest 那条路上它永远在。这里给一个兜底值，只为一件事：
// _tools/check_syntax.sh 那道门是拿 g++ -fsyntax-only 单独编本文件的，命令行上
// 没有这份 -D —— 原来 `QStringLiteral(QTLP_I18N_DIR)` 当场编不过（宏是裸标识符，
// 展开成 u"" QTLP_I18N_DIR 就是语法错误），于是整个语法门被这一行拖红。
// ⚠ 兜底值故意是「当前目录」而不是抄一份路径：真跑起来如果 CMake 那份丢了，
//   账1（.qm 在吗）会当场报 no 变红，**这是要看见的失败**；把路径抄进源码反而会
//   让「构建少了一步」这件事悄悄过去。
// ============================================================================
#ifndef QTLP_I18N_DIR
#define QTLP_I18N_DIR "."
#endif

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

// ============================================================================
// GaugePanel：第 ⑧ 步的主角 —— 把「切语言之后界面到底会不会自己变」变成看得见的数
// ----------------------------------------------------------------------------
// 类名故意就叫 GaugePanel，和 widgets/i18n/gauge_zh_CN.ts 里的 <context> 对上：
// tr() 的上下文 = **写调用点那一行所在的类名**（第 ① 步讲过的那条规矩），
// 所以这个类里的 tr("Pressure gauge") 查的就是 catalog 里 GaugePanel 那一条。
// ============================================================================
class GaugePanel : public QWidget
{
    Q_OBJECT
public:
    explicit GaugePanel(QWidget *parent = nullptr) : QWidget(parent)
    {
        m_name = new QLabel(this);
        m_count = new QLabel(this);
        m_action = new QLabel(this);
        auto *lay = new QVBoxLayout(this);          // QtWidgets/qboxlayout.h
        lay->addWidget(m_name);
        lay->addWidget(m_count);
        lay->addWidget(m_action);
        retranslate();                              // 构造时先填一遍（结果**不缓存**）
    }

    int changeHits() const { return m_changeHits; }
    QString shownName() const { return m_name->text(); }
    QString shownCount() const { return m_count->text(); }
    QString shownAction() const { return m_action->text(); }

protected:
    // 签名核到：QtWidgets/qwidget.h 701 行 `virtual void changeEvent(QEvent *);`
    // 触发链三段全部本机源码逐字核到（这条链就是「为什么覆写 changeEvent 就够」的依据）：
    //   1) installTranslator / removeTranslator 给 **app 对象**发一个 QEvent::LanguageChange
    //      （corelib/kernel/qcoreapplication.cpp 2261~2262 与 2290~2291，都是 sendEvent）；
    //   2) QGuiApplication::event 接住它，然后 **postEvent 给每个 top-level window**
    //      （gui/kernel/qguiapplication.cpp 2101~2106）——注意是 post 不是 send，
    //      所以界面不是在 installTranslator 那一行当场变的，要等事件循环走一圈；
    //   3) QWidget 把 LanguageChange 派发到 changeEvent。枚举值在
    //      QtCore/qcoreevent.h 128 行 `LanguageChange = 89`
    //      （★ Qt6 它**不在** qnamespace.h 里，我在 qnamespace.h grep 过：零命中。
    //        Qt5 时代抄下来的「去 qnamespace.h 找 QEvent 枚举」这条路径已经变了）。
    void changeEvent(QEvent *e) override
    {
        if (e->type() == QEvent::LanguageChange) {
            ++m_changeHits;
            retranslate();                 // ← 正路：文本每次现取，切语言就重设一遍
        }
        QWidget::changeEvent(e);           // 基类还要处理 Palette/Font/LayoutDirection 那些
    }

private:
    void retranslate()
    {
        m_name->setText(tr("Pressure gauge"));                        // 上下文 = GaugePanel
        m_count->setText(tr("%n valve(s) open", nullptr, kValves));   // 复数：n 走第三个参数
        // 第二个参数是**给译者的消歧注释**（disambiguation）。本机拿最小样本量过一遍，
        // 结论和很多人以为的不一样：
        //   · 它**不是查表键**。同一个 context 下源文都是 "Open" 时，
        //     translate("Panel","Open") / 带空注释 / 带正确注释 / 带完全无关的注释
        //     四种调用**返回同一句**——.qm 里键只有 context + 源文（+ 复数 n）。
        //   · 它在 .ts 里让两条同名源文**并存**（各带自己的注释），但一旦**两条都填了
        //     译文**，lrelease 会打 "Warning: dropping duplicate messages"（列出
        //     Context / Source / Line / Duplicate at line）并**只保留第一条**，
        //     计数变成 "Generated 1 translation(s)"。
        //     所以「靠注释区分两个不同译文的 Open」这条路**在 .qm 里是走不通的**，
        //     真要不同的译文就换源文，或者换 context（= 换类名 / translate 的第一参）。
        //   · 注释本身进 .qm 吗：以 <extracomment> 写在 .ts 里给人看，不参与命中。
        // 本行留在这里，是因为 widgets/i18n/gauge_zh_CN.ts 需要一条**真实存在**的
        // 带消歧条目当样本；GaugePanel 这个 context 下 "Open" 只有这一条，不撞自己。
        m_action->setText(tr("Open", "菜单动作「打开文件」，不是「开源软件」的 open"));
    }

    QLabel *m_name = nullptr;
    QLabel *m_count = nullptr;
    QLabel *m_action = nullptr;
    int m_changeHits = 0;
    static constexpr int kValves = 3;     // 复数条目用的数量（常量，不参与任何判卷）
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

        // 这四条现在都会把**源文**原样还给你，原因有两层，别混成一层：
        //   ① 这一步还没 installTranslator（③ 才装），查表路径根本没接上译文；
        //   ② 就算装上 qtbase_zh_CN.qm 也不会变——那里面只有 Qt 自带类的上下文，
        //      "AlphaProbe"/"BetaProbe"/"FileMenu" 这种自定义上下文压根不在目录里。
        // 所以「四个都等于原文」不是环境限制，是 tr 的查表规则；差异只发生在
        // **.qm 里恰好有 (这个上下文, 这个源文) 这一条**的时候 —— ③ 段用 QDialogButtonBox/"OK"
        // 演示的正是「有这一条」的样子。
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
        qInfo() << "   （诚实提醒：这一段演示的是**没有命中 .qm 时的退化路径**——此刻还没 installTranslator，"
                   "而且这些自定义上下文本来也不在任何系统 catalog 里。"
                   "把它当「%n 的形态由谁决定」的证据，别当最终译文承诺；命中译文的长相在 ③ 段。）";
    });

    // ── ③ 1600ms：QTranslator 装载（目录与返回值都现查现印，不抄注释里的老皇历） ──
    QTimer::singleShot(1600, [] {
        qInfo() << "③ QTranslator：装载路径与 catalog 命名规则";
        QTranslator catalog;                        // QtCore/qtranslator.h 23 行

        // ── 重载①（qtranslator.h 34~37 行，实现 qtranslator.cpp 466~512 行）─────────
        //   bool load(const QString &filename,
        //             const QString &directory = QString(),
        //             const QString &search_delimiters = QString(),
        //             const QString &suffix = QString())
        // ⚠ 这一版的注释里我原来写的是「按当前 locale 去匹」——**错的，它根本不看 locale**。
        //   实现只做一件事：把文件名当死字符串，先试 `名 + suffix`、再试裸 `名`，两个都不存在
        //   就从右往左按 search_delimiters（空串时默认 "_.")切一刀再试；切无可切（rightmost == 0）
        //   就 return false（499~505 行那三句就是这条退出路径）。
        //   「语言」从哪来？——**由调用者自己写在文件名里**。文档 434~458 行给的例子是 fr_CA 下
        //   调 load("foo.fr_ca", "/opt/foolib")，依次试 foo.fr_ca.qm / foo.fr_ca / foo.fr.qm /
        //   foo.fr / foo.qm / foo。所以传裸的 "qtbase" 时，目录里那些
        //   qtbase_zh_CN.qm 一个都轮不到：既没有裸 qtbase.qm，"qtbase" 里也没有 '_' '.' 可切
        //   → **必然 false，跟装没装翻译子包无关**。
        //   早先那一版把这个 false 归因成「本机没有 translations 目录」——环境事实变了以后
        //   假结论就露馅了，真因一直在 API 用法上。这一处勘误本身就值得记住：
        //   **同一个返回值可以有两种解释，只有读实现能分辨**（文档 460~463 行也明说了：
        //   想按界面语言找，请用下面那个带 QLocale 的重载，因为它用的是 uiLanguages()）。
        const QString sysDir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
        const bool dirExists = QDir(sysDir).exists();
        const QStringList qmFiles = dirExists
                ? QDir(sysDir).entryList({QStringLiteral("*.qm")}, QDir::Files)
                : QStringList{};
        qInfo() << "   QLibraryInfo::path(TranslationsPath) =" << sysDir
                << "｜目录存在 =" << dirExists << "｜.qm 个数 =" << qmFiles.size();

        const bool okBare = catalog.load(QStringLiteral("qtbase"), sysDir,
                                         QStringLiteral("_."), QStringLiteral(".qm"));
        qInfo() << "   重载①：load(\"qtbase\", 目录, \"_.\", \".qm\") 返回 =" << okBare
                << " ← 裸文件名里没有语言标记，切无可切（不是「目录不存在」那条原因）";

        // ── 重载②（qtranslator.h 38~42 行，实现 738~752 行）───────────────────────
        //   bool load(const QLocale &locale, const QString &filename, const QString &prefix,
        //             const QString &directory, const QString &suffix)
        // 745 行那句 `locale.uiLanguages(QLocale::TagSeparator::Underscore)` 才是「按界面语言找」：
        // zh-CN 会转成 "zh_CN"，再按 filename + prefix + 语言 + suffix 逐个试（749 行 load_translation）。
        // 注意分隔符：uiLanguages 给的是下划线形态，所以这里传 "_"，不是 "_.")。
        const QLocale sysLocale = QLocale::system();
        const bool okBase = catalog.load(sysLocale, QStringLiteral("qtbase"),
                                         QStringLiteral("_"), sysDir, QStringLiteral(".qm"));
        qInfo() << "   重载②：load(QLocale::system(), \"qtbase\", \"_\", 目录, \".qm\") 返回 =" << okBase
                << "｜uiLanguages =" << sysLocale.uiLanguages();
        qInfo() << "   isEmpty() =" << catalog.isEmpty()
                << "  language() =" << catalog.language()
                << "  filePath() =" << catalog.filePath();

        // 装上去，看同一个 (context, key) 在装前装后的两种长相 —— 这才是「翻译真的改了查表结果」
        // 的直接证据。三个探针的来源都在 qtbase 源码里逐字核过：
        //   QDialogButtonBox / "OK"     ← qmessagebox.cpp 2136 行 `QDialogButtonBox::tr("OK")`、2588 行同
        //   QProgressDialog  / "Cancel" ← qprogressdialog.cpp 143 行 `setCancelButtonText(QProgressDialog::tr("Cancel"))`
        //   QMessageBox      / "OK"     ← **故意放的错上下文**：消息框的按钮文字并不在 QMessageBox 这个
        //                                  context 里，所以它查不中、原样还给你（查不中不报错，这正是要看的）
        // 本机那一轮：装了 qtbase_zh_CN.qm，前两个变中文、第三个保持 "OK"。**换一台没装翻译包的机器，
        // 三条都会原样返回** —— 这不算示例失败，示例只打印，不拿环境差异当断言。
        struct Probe { const char *context; const char *key; };
        static const Probe probes[] = {
            {"QDialogButtonBox", "OK"},
            {"QProgressDialog", "Cancel"},
            {"QMessageBox", "OK"}
        };
        if (okBase) {
            QStringList before;
            before.reserve(int(sizeof(probes) / sizeof(probes[0])));
            for (const Probe &p : probes)
                before << QCoreApplication::translate(p.context, p.key);   // 还没装 → 必然是原文
            QCoreApplication::installTranslator(&catalog);                 // qcoreapplication.h 186 行
            for (int i = 0; i < int(sizeof(probes) / sizeof(probes[0])); ++i) {
                const Probe &p = probes[i];
                qInfo().noquote() << QStringLiteral("   translate(\"%1\",\"%2\")：装前 = %3 ｜ 装后 = %4")
                                         .arg(QLatin1String(p.context), QLatin1String(p.key),
                                              before.at(i), QCoreApplication::translate(p.context, p.key));
            }
            // ⚠ 出作用域前必须摘掉：catalog 是这里的栈对象，装着它销毁 = 悬空指针挂在应用上。
            QCoreApplication::removeTranslator(&catalog);                  // 187 行
        } else {
            qInfo() << "   没 catalog 可装，跳过 install —— 但你仍然能看清「查不中就把原文还给你」这条路：";
            for (const Probe &p : probes) {
                qInfo().noquote() << QStringLiteral("   translate(\"%1\",\"%2\") = %3")
                                         .arg(QLatin1String(p.context), QLatin1String(p.key),
                                              QCoreApplication::translate(p.context, p.key));
            }
        }

        // 第二个重载（38~42 行）：显式给 locale + 文件名 + 前缀 + 目录 + 后缀，
        // 适合应用自己的 catalog（例：myapp_zh_CN.qm）。
        QTranslator appCat;
        const bool okApp = appCat.load(QLocale(QStringLiteral("zh_CN")),
                                       QStringLiteral("myapp"),
                                       QStringLiteral("_"),
                                       sysDir,
                                       QStringLiteral(".qm"));
        qInfo() << "   load(locale, \"myapp\", \"_\", 目录, \".qm\") 返回 =" << okApp
                << " ← 翻译目录里从来没有 myapp_zh_CN.qm（那是应用自己的 catalog，要 lrelease 生成），"
                   "所以这里是 false；装了也白装，本讲**不**为它 installTranslator";

        // 第三个重载（qtranslator.h 43~44 行，实现 qtranslator.cpp 764 行；文档 758~759 行）：
        //   bool load(const uchar *data, int len, const QString &directory = QString())
        // ⚠ 文档 758 行那句 "**The data is not copied**" 是这个重载最容易被吞掉的一句：
        //   translator 只存指针，不复制。所以 `bytes` 必须活得比 `fromMemory` 久，
        //   而且中途不能 realloc。这里两者的声明顺序就是故意的：bytes 在前、fromMemory 在后，
        //   析构反过来（先 fromMemory 后 bytes）→ 不会悬空。真工程里更稳的做法是从
        //   QResource / 一块 static 缓冲里拿指针（.qm 嵌进 qrc 就是这么用的）。
        //   本机这一轮翻译目录里有真的 qtbase_zh_CN.qm，所以这段是**实测**；你那台没装翻译
        //   子包就会走 else 分支老实说「读不到」——两条分支都正常，示例不拿环境差异当断言。
        QFile qmFile(QDir(sysDir).filePath(QStringLiteral("qtbase_zh_CN.qm")));
        if (qmFile.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = qmFile.readAll();
            QTranslator fromMemory;
            const bool okMem = fromMemory.load(reinterpret_cast<const uchar *>(bytes.constData()),
                                               int(bytes.size()));
            qInfo() << "   load(uchar*, len) 从内存装载" << bytes.size() << "字节，返回 =" << okMem
                    << "｜filePath() =" << fromMemory.filePath()
                    << " ← 内存版没有文件，所以 filePath 是空串（不是 bug）";
            // 这一句是另一个容易混的点：**没 installTranslator**，直接调 translator 对象自己的
            // translate()（qtranslator.h 26~27 行的那个虚函数）照样出中文。install 只是把这张表
            // 挂到 QCoreApplication 的查表链上，让 tr()/QCoreApplication::translate() 能找到它；
            // 对对象本身调 translate() 从来不需要 install。
            qInfo() << "   不 install 也能问它本人：fromMemory.translate(\"QDialogButtonBox\",\"OK\") ="
                    << fromMemory.translate("QDialogButtonBox", "OK");
        } else {
            qInfo() << "   读不到" << qmFile.fileName()
                    << "（你的 Qt 没带这个 catalog）→ 跳过内存装载演示，不是失败";
        }

        // 装卸只有一进一出两个名字（qcoreapplication.h 186/187 行）：
        //   static bool installTranslator(QTranslator *messageFile);
        //   static bool removeTranslator(QTranslator *messageFile);
        // removeInstalledTranslator：本机头文件里 grep 不到 → 不用（见顶部差异条目）。
        qInfo() << "   对没装上去的 appCat 再 removeTranslator 一次，返回值 ="
                << QCoreApplication::removeTranslator(&appCat)
                << " ← 从没 install 过，摘不掉，返回 false；这条也是证据"
                   "（上面那个 catalog 已经在 install 之后当场摘掉了：它是栈对象，装着销毁会留悬空指针）";
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

    // ── 4100ms：⑧ 仓库自带的 .qm —— 把 Linguist 那条链闭合在自己身上 ──────────
    // 为什么要有这一步：③ 用的是 **Qt 自己**的 catalog（qtbase_zh_CN.qm），
    // 它属于可选安装组件，换一台「只装了 base」的机器就 load 不回来 ——
    // 于是「翻译到底有没有生效」这一课在那些机器上就只剩注释了。
    // 这一步换成**本仓库自己的** catalog：widgets/i18n/gauge_zh_CN.ts（译文源，进仓库）
    // → 构建期由 lrelease 6.12.0 编成 build/i18n/gauge_zh_CN.qm（213 字节，产物不进仓库）。
    // 【勘误】这里原先写的是「.qm 已提交」：.gitignore 第 68 行的 `*.qm` 根本不收它，
    //   所以新克隆里既没有 .qm、连 .ts 也可能是空的，第 ⑧ 步的账1/账2 当场判错。
    //   现在 CMake 找到 LinguistTools 就现场生成（找不到时 configure 会给一条 WARNING，
    //   把该手敲的那条 lrelease 命令打给你），仓库里只保留 .ts 这一份真相。
    // 它不依赖任何可选组件、不依赖系统 locale、不依赖网络，所以每一条都是硬结论。
    // 路径由 CMake 用 QTLP_I18N_DIR 在编译期钉成**绝对路径**，
    // 这样 ctest 把工作目录设在 build/bin 也照样找得到（不靠「恰好在那儿」）。
    struct QmLedger { int ok = 0; int bad = 0; } qmLedger;

    // ★ 标签一律用 QString，不要 const char* + QLatin1String：
    //   源文件是 UTF-8，窄字面量里存的是 UTF-8 字节，QLatin1String 会**逐字节**当 Latin-1
    //   解释 → 中文变乱码，再经 Windows 控制台按 GBK 编一遍就成了双重乱码。
    //   QStringLiteral 在编译期直接把 UTF-8 源码转成 UTF-16，才是中文该走的路。
    auto expect = [&qmLedger](const QString &what, const QString &got, const QString &want) {
        const bool hit = got == want;
        qmLedger.ok += hit ? 1 : 0;
        qmLedger.bad += hit ? 0 : 1;
        qInfo().noquote() << QStringLiteral("      [%1] %2 → 期望 \"%3\"，实测 \"%4\"")
                                 .arg(hit ? QStringLiteral("对") : QStringLiteral("错"),
                                      what, want, got);
    };

    QTimer::singleShot(4100, [&qmLedger, expect] {
        qInfo() << "\n⑧ 自带 catalog：tr → lupdate → 填译 → lrelease → load → translate 全链闭环";
        const QString dir = QStringLiteral(QTLP_I18N_DIR);
        const QString qmFile = dir + QStringLiteral("/gauge_zh_CN.qm");
        qInfo().noquote() << QStringLiteral("   catalog 目录 = %1｜.qm 在吗 = %2")
                                 .arg(dir).arg(QFile::exists(qmFile));
        expect("账1 .qm 文件存在（它是构建期由同仓库的 .ts 经 lrelease 生成的产物，不在就是构建缺了这一步）",
               QFile::exists(qmFile) ? QStringLiteral("yes") : QStringLiteral("no"),
               QStringLiteral("yes"));

        QTranslator demo;
        // 重载①（qtranslator.h 34 行）：load(filename, directory, search_delimiters, suffix)。
        // 实现先试 `filename + suffix`（qtranslator.cpp 466~512 行），所以我们给
        // 「gauge_zh_CN」+「.qm」时它第一枪就打中 gauge_zh_CN.qm —— 这也是本讲第 ③ 步
        // 用「qtbase」打不中同一个文件的那条命名规则的正面用例。
        const bool loaded = demo.load(QStringLiteral("gauge_zh_CN"), dir,
                                      QStringLiteral("_."), QStringLiteral(".qm"));
        expect("账2 load 返回", loaded ? QStringLiteral("true") : QStringLiteral("false"),
               QStringLiteral("true"));
        qInfo().noquote() << QStringLiteral("   isEmpty = %1｜language = %2｜filePath = %3")
                                 .arg(demo.isEmpty()).arg(demo.language(), demo.filePath());

        static const char *ctx = "GaugePanel";
        // 装之前：catalog 装了但没 install，查表链上没有它 → 必然是原文。
        expect("账3 install 之前查表", QCoreApplication::translate(ctx, "Pressure gauge"),
               QStringLiteral("Pressure gauge"));
        QCoreApplication::installTranslator(&demo);        // qcoreapplication.h 186 行

        expect("账4 单条：Pressure gauge", QCoreApplication::translate(ctx, "Pressure gauge"),
               QStringLiteral("压力表"));
        expect("账5 带译者注释的 Open（注释不参与查表键）",
               QCoreApplication::translate(ctx, "Open", "给译者的注释"),
               QStringLiteral("打开"));
        // ★ 换一条**完全无关**的注释，必须拿回同一句。这一笔是把
        //   「disambiguation 不是 .qm 的键」从注释变成判卷：键只有 context + 源文。
        expect("账6 同一条目、给个八竿子打不着的注释 → 还是那句译文",
               QCoreApplication::translate(ctx, "Open", "随便写点什么"),
               QStringLiteral("打开"));
        // 复数：n 走 translate 的第 4 个参数（qcoreapplication.h 190~193 行），
        // %n 由 translate 自己填 —— 上一节刚说过 arg() 填不了它，这里就是当面对质。
        expect("账7 复数 n=1（%n 由 translate 填）",
               QCoreApplication::translate(ctx, "%n valve(s) open", nullptr, 1),
               QStringLiteral("1 个阀门开着"));
        expect("账8 复数 n=7（同一条目，换 n 就换句子）",
               QCoreApplication::translate(ctx, "%n valve(s) open", nullptr, 7),
               QStringLiteral("7 个阀门开着"));
        // 两种「查不中」：源文不在 catalog 里；源文在但**上下文不对**。
        // 都不报错、都安静地把原文还给你 —— 这正是拼错上下文最难查的原因。
        expect("账9 查不中（catalog 里没这句）",
               QCoreApplication::translate(ctx, "NotInCatalog"), QStringLiteral("NotInCatalog"));
        expect("账10 上下文错了（源文一样、类名不一样 → 也查不中）",
               QCoreApplication::translate("OtherPanel", "Pressure gauge"),
               QStringLiteral("Pressure gauge"));

        QCoreApplication::removeTranslator(&demo);         // 187 行
        expect("账11 remove 之后回到原文（install/remove 就是那个开关）",
               QCoreApplication::translate(ctx, "Pressure gauge"),
               QStringLiteral("Pressure gauge"));

        // ── ⑧-b 界面到底会不会自己变？把 GaugePanel 拉出来当证人 ──────────────
        // 上面十一条证的是「translate() 这个函数查表的结果变了」；
        // 工程里真正重要的是「已经 set 到界面上的那些文字会不会跟着换」——
        // 答案是：**不会自动换**，除非你按 changeEvent/QEvent::LanguageChange 这条路重设。
        // 这一步就是把这个「会 / 不会」跑成几行数。
        GaugePanel panel;                 // 无 parent → top-level（2105 行只发给 top-level）
        panel.resize(240, 120);
        panel.show();
        // offscreen 平台插件在 show() 时会回一句
        //   "This plugin does not support propagateSizeHints()"
        // —— 那是**平台的坦白**，不是错误：无头平台没有真的窗口管理器可以去转达尺寸提示。
        // 本机实测这一句出现在账11 与账12 之间，不影响任何计数，所以本讲不屏蔽它。
        QCoreApplication::processEvents();               // 让 show 落地，QWindow 建出来
        expect("账12 装 catalog 之前，界面上是源文（此刻 catalog 已从查表链上摘掉）",
               panel.shownName(), QStringLiteral("Pressure gauge"));
        expect("账13 复数那句也是源文",
               panel.shownCount(), QStringLiteral("3 valve(s) open"));
        expect("账14 带消歧注释那句也是源文",
               panel.shownAction(), QStringLiteral("Open"));
        const int hitsBefore = panel.changeHits();

        QCoreApplication::installTranslator(&demo);
        // ★ 必须 processEvents：qguiapplication.cpp 2106 那一句是 **postEvent**，
        //   不是 sendEvent —— installTranslator 返回的那一刻界面还没变，
        //   要等事件循环把 LanguageChange 送到那个窗口。这一条坑过很多人。
        QCoreApplication::processEvents();
        expect("账15 install + 走一圈事件循环 → 界面文字换成译文",
               panel.shownName(), QStringLiteral("压力表"));
        expect("账16 复数条目按 kValves=3 填 %n",
               panel.shownCount(), QStringLiteral("3 个阀门开着"));
        expect("账17 消歧那句也换了（catalog 里它就是普通的一条 Open）",
               panel.shownAction(), QStringLiteral("打开"));
        expect("账18 changeEvent 确实被调了几次（不是我们自己调的）",
               panel.changeHits() > hitsBefore ? QStringLiteral("yes") : QStringLiteral("no"),
               QStringLiteral("yes"));
        qInfo().noquote() << QStringLiteral("   changeHits：%1 → %2（一次 install、一次 remove，"
                                            "各触发一轮 LanguageChange）")
                                 .arg(hitsBefore).arg(panel.changeHits());

        QCoreApplication::removeTranslator(&demo);
        QCoreApplication::processEvents();
        expect("账19 remove + 走一圈 → 界面又回到源文（开关就是 install/remove）",
               panel.shownName(), QStringLiteral("Pressure gauge"));
        qInfo() << "   对照第 ⑦ 步那条「把 tr 结果存进 static」的坑："
                   "那份缓存不会因为 LanguageChange 而更新，"
                   "而 GaugePanel 每次都在 retranslate() 里现取 —— 这就是两种写法的实际差别。";

        qInfo().noquote() << QStringLiteral("   【⑧ 台账】核对 %1 项，不符 %2 项 → "
                                            "不符就参与进程退出码")
                                 .arg(qmLedger.ok + qmLedger.bad).arg(qmLedger.bad);
    });

    // ── 4600ms：收尾退出 ────────────────────────────────────────────
    QTimer::singleShot(4600, [&qmLedger] {
        qInfo() << "全部 8 步打印完毕，自动退出（全程无阻塞调用）";
        QCoreApplication::quit();
    });

    int rc = app.exec();
    qInfo() << "事件循环退出，exec() 返回" << rc;
    // ⑧ 的台账不符 → 退出码非 0，ctest 才拦得住（打印出来的账不进门 = 装饰）。
    return rc != 0 ? rc : (qmLedger.bad > 0 ? 1 : 0);
}

// ============================================================================
// 附一：Linguist 工作流（四步，注释里说清；本机工具实测状态见顶部）
// ============================================================================
/*
 * 1) 抽取（lupdate）：扫源码，把所有 tr("…") / QT_TR_NOOP("…") /
 *    QCoreApplication::translate(ctx, "…") 的字面量连同「上下文 + 源文 + 注释」写进 .ts（XML，给人改）。
 *       D:\Qt\6.12.0\mingw_64\bin\lupdate.exe  <源码目录> -recursive -ts  myapp_zh_CN.ts
 *    ⚠ 上面那条命令的参数集请以 `lupdate -help` 为准。之前这一版写的是「我没在本机跑过」，
 *      理由是编不动 Qt——**那句话已经过期了**，现在跑过了，本机那一轮：
 *        lupdate version 6.12.0 ／ lrelease version 6.12.0（都是 -version 自报）
 *        lupdate 10_internationalization.cpp -ts demo.ts
 *          → "Found 16 source text(s) (16 new and 0 already existing)"
 *            （本讲加进 GaugePanel 之后重跑的数字；早先那一轮是 13，改代码就得重数，
 *             「讲义里的数字」和「代码」之间没有自动同步，只有重跑有。）
 *          → 抽出来的 <name>（上下文）共 7 个：AlphaProbe 5 条 / MainWindow 4 条 /
 *            GaugePanel 2 条 / BetaProbe 2 条 / QDialogButtonBox 1 条 /
 *            OtherPanel 1 条 / FileMenu 1 条 —— **上下文 = 类名**这条规矩，
 *            到这里就不只是注释里的话，是工具自己这么写的。
 *          → ★ 注意 OtherPanel 这一条：代码里**没有**叫 OtherPanel 的类，它是
 *            647 行那句 QCoreApplication::translate("OtherPanel", …) 的字面量。
 *            lupdate 不校验上下文是不是真类型，它就是照抄那个字符串。反过来说：
 *            上下文只要是**运行时拼出来**的字符串，lupdate 就扫不到，这条永远没译文
 *            （build_deploy/qt_cmake_05_translations 的账10 演示的就是这一格）。
 *          → ★ 还有一条更容易中招的：**同文启发式**。代码里新冒出一个「不同上下文 +
 *            完全相同的源文」，lupdate 会把别的上下文里已有的译文**抄过来**并标成
 *            unfinished，本机原文是 "Same-text heuristic provided 1 translation(s)"。
 *            配合上面「unfinished 填了字照样生效」，结果就是**上下文拼错了界面却仍有译文**。
 *      lupdate 还接 `-no-obsolete`、`-extensions <ext,...>`（默认扩展名列表里就有
 *      cpp/h/hpp/qml/ui/qrc，help 原文抄的），所以「一个目录 recursive 扫」是常规用法。
 *      Qt6 的 CMake 工程有整套管线：`qt_add_lupdate` / `qt_add_lrelease` /
 *      `qt_add_translations` / `qt6_create_translation` / `qt6_add_translation`
 *      —— 这些不是头文件里的东西，它们在
 *      <Qt>\lib\cmake\Qt6LinguistTools\Qt6LinguistToolsMacros.cmake（本机逐个 function() 数出来的），
 *      需要 find_package(Qt6 COMPONENTS LinguistTools)。本仓库里
 *      **build_deploy/qt_cmake_05_translations** 就是一个用 qt_add_translations 的完整独立工程。
 *
 * 2) 翻译（Linguist GUI，或直接改 .ts）：逐条填 translation；带 n 的条目要填**每个复数形态**；
 *    上下文不同的同名源文是两条独立条目（这就是第 ① 步打印的那个差别）。
 *
 * 3) 编译（lrelease）：.ts → .qm（二进制，运行时只查表、不解析 XML）。
 *       D:\Qt\6.12.0\mingw_64\bin\lrelease.exe  myapp_zh_CN.ts  -qm  myapp_zh_CN.qm
 *    本机真实跑过的那一条是对 widgets/i18n/gauge_zh_CN.ts 执行，输出：
 *       "Generated 3 translation(s) (3 finished and 0 unfinished)" → 213 字节的 .qm。
 *    一条只有跑起来才知道的事：.ts 顶部可以写 XML 注释（<!DOCTYPE TS> 之后、<TS>
 *      之前，本仓库那份 gauge_zh_CN.ts 就写了十几行来路说明），lrelease 照样吃、
 *      照样生成上面那行计数。
 *    ⚠ 但**扛不住 lupdate 重写**：build_deploy/qt_cmake_05_translations 那份 .ts
 *      原本带着 qt_add_translations 自己写的一句注释，跑过一次 update_translations
 *      之后整条注释没了（本机对过前后两份文件）。所以「给 .ts 写说明」这件事，
 *      只要这份 .ts 还会被 lupdate 碰，就该写在讲义里而不是写在 .ts 里。
 *    ★ 顺带纠正我自己先前写在这里的一句**推测**。上一版我说「<translation
 *      type="unfinished"> 的条目不会进 .qm 的可用译文，运行时查不中、退回源文」，
 *      并且把「那句 %n 退回源文」当成观察结果写。这不是观察，是我脑补的因果。
 *      这次用三组最小 .ts 在本机各跑一遍 lrelease + 一次 QTranslator::load()
 *      + QCoreApplication::translate()（注意：translate() 只查**已 install** 的
 *      translator，光 load 不 install 的话三条都会退回源文——我第一版探针就栽在这），
 *      量到的原文是：
 *        有译文 + type="unfinished" → "Generated 1 translation(s) (0 finished and 1 unfinished)"
 *                                      .qm 113 字节，load()=true，translate() **返回译文**。
 *        空译文 + type="unfinished" → "Generated 0 translation(s) (0 finished and 0 unfinished)"
 *                                      + "Ignored 1 untranslated source text(s)"
 *                                      .qm 只剩 30 字节，load()=**依然 true**，translate() 退回源文。
 *        复数条目两个 <numerusform> 都填 + type="unfinished" → n=1、n=3 两个形态**都生效**，
 *                                      和去掉 unfinished 的版本 .qm 同为 152 字节。
 *      结论跟上一版相反：**决定生死的是「译文有没有填」，不是那个 type 属性。**
 *      unfinished 在 linguist 那一侧是给**人**看的进度标记，lrelease 会把它编出来
 *      （计数时单独列一栏），运行期照查照用。真正被踢出 .qm 的是「译文为空」的条目，
 *      lrelease 还专门给它打一行 Ignored N untranslated source text(s)。
 *    · 真要把 unfinished 挡在包外，得**显式加开关**（lrelease -help 原文，本机跑过）：
 *        -nounfinished          Do not include unfinished translations
 *                               （加上它重跑那份 unfinished-有字的样本：
 *                                "Generated 0 translation(s) (0 finished and 0 unfinished)"，
 *                                .qm 从 113 字节掉到 30 字节 —— 连「unfinished 有几条」
 *                                都不报了，一条没编。这才是「unfinished 不生效」
 *                                唯一成立的那条路径。）
 *        -fail-on-unfinished    有 unfinished 就当错误；本机实测退出码 **1**，适合进 CI。
 *        -markuntranslated <前缀> / -removeidentical / -compress 也都在同一份 help 里。
 *    · 同 context 同 source 的两条（就算注释不同）**都填了字**时，lrelease 打
 *        "Warning: dropping duplicate messages in 'x.qm':
 *         * Context: Panel  * Source: Open  * Line in .ts file: 6  * Duplicate at line: 10"
 *      并只留第一条。→ 别指望用消歧注释给同一句源文配两句不同译文。
 *    · 代码里能程序化分辨「表是空的」的那个判据是 **QTranslator::isEmpty()**
 *      （QtCore/qtranslator.h 29 行，virtual bool isEmpty() const）。本机探针：
 *      30 字节那个 → load()=true、isEmpty()=**true**；109 字节那个 → isEmpty()=false。
 *    ★★ 而上面那行「30 字节的空 .qm 照样 load()=true」是这一讲最值钱的一条：
 *      **装载成功 ≠ 翻译生效**。想验证「翻译有没有真的生效」，唯一办法是拿译文和
 *      源文比一次，看返回的字符串变没变。build_deploy/qt_cmake_05_translations 那个
 *      工程就曾经因为 .ts 是空的而打印「翻译是否生效： true」——那个 true 量的只是
 *      load()+installTranslator()，现在那个文件里改成了真比对。
 *
 * 4) 运行时装载：本文件有两段，分工不同。
 *    第 ③ 步装的是 **Qt 自身控件**的 catalog（qtbase_zh_CN.qm：右键菜单、对话框按钮那套），
 *    目录用 QLibraryInfo::path(QLibraryInfo::TranslationsPath) 现查。本机这一轮那个目录
 *    **存在且有 314 个 .qm**，load 回来的是 qtbase_zh_CN.qm，于是
 *    translate("QDialogButtonBox","OK") 从 "OK" 变成 "确定"。
 *    ⚠ 这一条**是环境事实**（翻译子组件装不装由安装器决定），所以示例只打印不判卷：
 *    换一台没装它的机器，load 返回 false，示例照样绿。早先这一版写的
 *    「本机这个目录不存在、load 必然 false」同样是环境事实，环境装全了它就变成假话——
 *    这就是「环境事实要注明是哪一轮查的、怎么现查」的必要性。
 *    第 ⑧ 步装的才是**本仓库自己的** catalog（源文件 widgets/i18n/gauge_zh_CN.ts 进仓库，
 *    .qm 由构建期的 lrelease 生成到 build/i18n —— 早先这里写「.qm 已提交」是错的：
 *    .gitignore 里 `*.qm` 一律不收，新克隆根本没有那份二进制，账1 当场红），
 *    它不依赖任何可选组件，所以那 19 项才敢当成硬账、敢参与退出码。
 *       QTranslator t;
 *       if (t.load(QStringLiteral("gauge_zh_CN"), dir, QStringLiteral("_."), QStringLiteral(".qm")))
 *           QCoreApplication::installTranslator(&t);
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
 *   2) QWidget 级的 layoutDirection —— 之前写「没逐字核签名，所以代码里没写」，
 *      这一版核到了，行号在这儿：QtWidgets/qwidget.h
 *        414 `void setLayoutDirection(Qt::LayoutDirection direction);`
 *        415 `Qt::LayoutDirection layoutDirection() const;`
 *        416 `void unsetLayoutDirection();`
 *        422/423 inline `isRightToLeft()` / `isLeftToRight()`（就是拿 415 比较出来的）
 *        171  Q_PROPERTY(Qt::LayoutDirection layoutDirection READ ... WRITE ... RESET ...)
 *      有 Q_PROPERTY 就意味着它能进属性系统、能被 QML/绑定用（第 陆阶段第 6 讲那条线）。
 *      应用级那三个（qguiapplication.h 108/109/111/112）代码里已经在用；widget 级这组
 *      本讲**代码里仍然没写**——写出来就是 RTL 布局那一整讲的量，塞进这一讲只会淹掉主线。
 *      把行号抄在这儿的目的只有一个：你要用的时候不用猜、不用搜。
 *   3) QWidget::changeEvent(QEvent *) 与 QEvent::LanguageChange —— **这一条已经从
 *      「没核，所以不写」升级成「核到，并且跑给你看了」**：
 *        签名 QtWidgets/qwidget.h 701 行 `virtual void changeEvent(QEvent *);`
 *        枚举值 QtCore/qcoreevent.h 128 行 `LanguageChange = 89`
 *        ★ 注意它**不在** qnamespace.h（本机 grep：零命中）—— Qt5 时代「QEvent 的
 *          枚举去 qnamespace.h 找」那条肌肉记忆在 Qt6 会扑空，这是环境类勘误里最容易
 *          让老代码编不过的一种。
 *        触发链 corelib/kernel/qcoreapplication.cpp 2261~2262（install）与
 *          2290~2291（remove）给 app 发 LanguageChange →
 *          gui/kernel/qguiapplication.cpp 2101~2106 接住后 **postEvent 给每个 top-level**。
 *      代码里的 GaugePanel::changeEvent 就是按这条链接的，第 ⑧ 步账11~账16 实测：
 *      install + processEvents → 界面文字变「压力表」；remove + processEvents → 变回
 *      "Pressure gauge"；changeHits 0 → 1。★ 关键那一步是 **processEvents**：
 *      2106 用的是 postEvent 而不是 sendEvent，所以 installTranslator 返回的那一刻
 *      界面**还没变**，要等事件循环走一圈——「我明明换了翻译器怎么界面不动」就是这儿。
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
 *   你今天打印的四条「结果都一样」不是失败——那正是「这条 (上下文, 源文) 没命中任何译文条目」
 *   的证据：那一步还没 installTranslator，而且自定义上下文本来也不在系统 catalog 里。
 *
 *   第二个：**把翻译和格式化混成一锅**。QLocale 管的是「这个数字/日期给人看时长什么样」，
 *   tr 管的是「这句话怎么换成另一种语言」。两码事。日期一旦以字符串形态进了代码、进了文件，
 *   你就把「3 月 1 日」和「1 月 3 日」的赌注押在了读它的人身上——本讲 ⑤ 里那串
 *   "01/03/2026" 在不同 locale 下解出不同日子，就是这个赌局开牌。存 QDate、
 *   存 ISO，只在最后一刻格式化给人看。
 *
 *   还有句实在话，是这一讲自己踩出来的：早先我把本机「有没有 lupdate、有没有 .qm」ls 了一遍
 *   写进注释，白纸黑字说「translations 目录不存在，一个 .qm 都没有」。后来这台机器的 Qt 装全了，
 *   那个目录里冒出 314 个 .qm——「load 必然 false」这句老实话一夜之间就成了假结论。
 *   所以这一版把规矩改成两条：**环境事实要现查**（用 QLibraryInfo::path(TranslationsPath)，
 *   别把某台机器的安装路径写死进代码），**结论要打印**（返回值原样吐出来，true/false 各有解释）。
 *   你以后写 i18n 也要这个习惯——先确认环境里到底有没有那份 .qm，再决定是
 *   「我的翻译没生效」还是「压根没东西可加载」，这两种病的药方完全不同。
 *
 * ——— 小练习 ———
 *   1. 把 AlphaProbe::openLabel() 的源文从 "Open" 改成 "打开"，再用本机的
 *      D:\Qt\6.12.0\mingw_64\bin\lupdate.exe 对这个文件抽一次 .ts，打开 XML 亲眼看
 *      「上下文 = AlphaProbe、源文、注释」三样各长在哪一行；然后把第 ② 步里那条
 *      带中文注释的 tr() 也抽进去，比较 .ts 里 comment 字段的差别。
 *   2. 自己造一份最小的 .qm（哪怕只翻 "Open" → "开启" 一条），放到一个你新建的目录里，
 *      把第 ③ 步的 load() 目录改成它，让 okBase 真的返回 true，然后在装载前后各打印一次
 *      translate("AlphaProbe","Open")——亲眼看到「表里有 / 表里没有」的差别，
 *      再按 removeTranslator → installTranslator 的顺序验证切语言。
 *   3. 在第 ⑤ 步的循环里，把 QLocale(QStringLiteral("ar_SA"))（阿拉伯语，RTL 常用）
 *      加进 demoLocales()，配合第 ⑥ 步的 setLayoutDirection(Qt::RightToLeft)，
 *      观察标签贴边方向与数字渲染的变化；再把系统 locale 换掉跑一次 —— ★ 勘误：旧稿写
 *      「用 QT_LOCALE 之类环境变量」，本机 Qt 全树没有读 QT_LOCALE 这行代码；Qt 读的是  name-ok:QT_LOCALE ★ 勘误段落复述旧错名
 *      标准的那三个：qcoreapplication.cpp 第 600 行的注释「read LC_ALL, LC_<category>,
 *      and LANG, in order」与第 628 行 `getenv("LC_ALL") || getenv("LC_CTYPE") || getenv("LANG")`。
 *      所以在 Windows 上是 `set LC_ALL=de_DE` / `set LANG=...` 这一路，比较 ④ 里
 *      1234567 分组符的差别。
 */
// ============================================================================

#include "10_internationalization.moc"   // 本文件内定义了带 Q_OBJECT 的 AlphaProbe / BetaProbe，AUTOMOC 生成此文件
