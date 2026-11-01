// ============================================================================
// 第 2 讲 · 用 QtTest 测界面：把「手点一遍」变成「机器每次点一遍」
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   第 1 讲测的是纯逻辑（不碰 QWidget），所以 QTEST_GUILESS_MAIN 就够了。
//   界面代码不一样：它要窗口、要事件、要焦点、要一次点击真的落到某个控件上。
//   这一讲把「怎么在无头机器上把一个按钮点下去」拆成六件事：
//     1) QTEST_MAIN 和 QTEST_GUILESS_MAIN 的差别只有一行代码（qtest.h 296/299），
//        但差别背后是「要不要 QApplication、要不要平台插件」。
//     2) QTest::keyClicks(QWidget*, ...) 只操作**你递过去的那个控件**，
//        不依赖焦点（qtestkeyboard.h 194~197 行：只有 widget 为空时才退回去找焦点）。
//        这一条让界面测试终于能在没有窗口管理器的机器上跑。
//     3) ★ 但它只认 Latin-1：`sequence.at(i).toLatin1()`（qtestkeyboard.h 265 行）。
//        中文用 keyClicks 敲不进去 —— 本机实测的结论比「敲不进去」更狠：
//        **进程当场 abort**（QTEST_ASSERT 不受 QT_NO_DEBUG 控制，见第二节的勘误块）。
//     4) QSignalSpy 是「事件发生过几次、带的什么参数」的标准工具；
//        配合 spy.wait(ms) 就能等异步信号，而不是傻等 qWait(500)。
//     5) 模态对话框（exec()）会开一个新的事件循环：测试里用
//        QTimer::singleShot(0, &dlg, &QDialog::accept) 把它关掉。
//     6) QTRY_VERIFY / QTRY_COMPARE（qtestcase.h 211/238）是「等一下再断言」，
//        比固定的 qWait 稳 —— 它按步长重试，成功就立刻过。
//
//   十三个测试函数，每一个都能单独跑：
//     qt-learning-path/build/bin/quality_02_qtest_gui -functions   列出它们
//     qt-learning-path/build/bin/quality_02_qtest_gui keyboardWithoutFocus  只跑一个
//
//   对应构建目标名：quality_02_qtest_gui
//     find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
//     add_executable(quality_02_qtest_gui 02_qtest_gui.cpp)
//     target_link_libraries(quality_02_qtest_gui PRIVATE Qt6::Widgets Qt6::Test)
//     set(CMAKE_AUTOMOC ON)     # 有 Q_OBJECT 类，末尾 #include "02_qtest_gui.moc"
//
//   ★ 运行前须知：
//     · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//       Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 quality_02_qtest_gui）。本机这一轮：QtTest Totals: 15 passed / 0 failed / 1 skipped（那条 skip 是 QSKIP 故意立的，见正文）。
//       这只证明「编得过、跑得完、上面这行为绿」——退出码 0 不等于正文每个数字都对：
//       换 Qt 版本 / 换平台 / 换机器后，带具体数字的「预期」请以你自己那次运行输出为准
//       （本机那一轮存档 runlogs/quality_02_qtest_gui.log）。
//     · 本讲是 QTEST_MAIN（需要 QApplication）。没有显示器时用
//       `-platform offscreen`，或者设环境变量 QT_QPA_PLATFORM=offscreen；
//       本仓库的 ctest 就是这么配的（CMakeLists 里的 ENVIRONMENT 那行）。
//     · 每个会模拟输入的测试都先 resize + show + qWait(0)。理由有两条：
//       ① 鼠标模拟用的是 widget->rect().center()（qtestmouse.h 150~151 行），
//          尺寸没定就没有可靠的落点；② 某些构建里事件要经平台窗口转发
//          （qtestmouse.h 159 行那半段），窗口不建出来就没有转发的地方。
//     · 不比较像素。截图对比是另一套东西（基线图 + 字体渲染差异），
//       在 CI 上极不稳定，本讲不碰。
//     · 断言优先写「文档或头文件能证明的」；凡是依赖平台语义的，本机实测之后
//       要么改成不依赖它（键盘那节就是这么改的），要么把实测值钉死并注明平台。
//       ★原本这里写的是「没有一条依赖本机看起来是这样」—— 结果 offscreen 下
//         「控件没焦点」这条就依赖了：show() 之后 Qt 真的给了焦点，判红。
// ============================================================================

// ============================================================================
// 一、头文件地图
// ----------------------------------------------------------------------------
// QtTest/qtest.h
//   第 296 行 #define QTEST_MAIN(TestObject) QTEST_MAIN_WRAPPER(TestObject, QTEST_MAIN_SETUP())
//              ← 造 QApplication，然后 QTest::qExec（里面自建事件循环）
//   第 299 行 QTEST_GUILESS_MAIN      ← 造 QCoreApplication，别的都一样
//   两个宏的共同点：**main 直接 return qExec 的结果** —— 全过 0，有一条失败非 0。
//
// QtTest/qtestkeyboard.h
//   第 194~197 行 if (!widget) widget = QApplication::focusWidget();
//              ← ★ 你传了 widget 就完全不用焦点。这是无头测试能跑的前提。
//   第 263~267 行 keyClicks(...) { for (...) keyEvent(Click, widget,
//                sequence.at(i).toLatin1(), ...); }
//              ← ★★ 每个字符先转 Latin-1，再拿这个字节去查 qasciikey.cpp 的表。
//                本机实测的完整链条（原本是「敲进去的不是中文」，太轻描淡写了）：
//                  QChar::toLatin1() 给一个 Latin-1 字节 → QTest::asciiToKey(char)
//                  的 switch 里**没有这一项** → default: QTEST_ASSERT(false)
//                  （qasciikey.cpp:192）→ qt_assert → abort → 进程退出码 0xc0000602。
//                ★ 关键在 qtestassert.h:11：QTEST_ASSERT **没有 QT_NO_DEBUG 保护**，
//                  release 的 Qt 也一样炸。所以这不是「debug 构建才会遇到」的坑。
//                ★ 表覆盖范围（本机把 qasciikey.cpp 的 case 全数过一遍）：
//                  0x08~0x7f、0xa0~0xdf，加上 0xe5 0xe6 0xf7 0xf8 0xff —— 就这些。
//                  所以连 **é (0xe9) 都会 abort**：Latin-1 里绝大多数带重音的小写字母
//                  根本不在表里。「Latin-1 能表示的就敲得进去」这句是错的。
//   第 274/280 行 keyClick(QWidget*, char) / keyClick(QWidget*, Qt::Key)
//              第 284 行 keySequence(QWidget*, const QKeySequence &)（Ctrl+S 这类）
//                ★ 勘误：旧稿写 289 行。本机 6.12.0 逐行数过：283 行是
//                  `#if QT_CONFIG(shortcut)`，284 行才是这个重载的声明行，
//                  289 行落在函数体里。多出来的信息也有用——没编进 shortcut 的
//                  Qt（裁剪构建）里 keySequence 根本不存在，keyClick 还在。
//              ← 要敲非 Latin-1 的字符，走 **Qt::Key 这个重载**或者自己 sendEvent 一个
//                带 text 的 QKeyEvent，别碰 keyClicks 的字符串版。
//
// QtTest/qtestmouse.h
//   第 156~157 行 if (pos.isNull()) pos = widget->rect().center();
//              ← 默认点「正中」，所以控件的尺寸必须先定下来
//              ★ 勘误：旧稿写 150~151。本机 6.12.0：150 是 `#ifdef QT_WIDGETS_LIB`，
//                151~152 才是 `mouseEvent(action, QWidget*, …)` 这两行声明，153 `{`、
//                154 `QTEST_ASSERT(widget);`，156 才有 pos.isNull()——整个函数体是 151~223。
//   第 159~162 行 QTEST_QPA_MOUSE_HANDLING 那一支（159 是 `#ifdef`，160~162 是函数体）：
//              走平台窗口转发，要求 widget->window()->windowHandle() 存在 —— show() 之后才有
//              ★ 勘误：旧稿写 159~163，163 行其实已经是 `#else`，把它算进「这一支」会多框一个分支。
//   第 180~210 行 mouseEvent() 里先把 meType 按 action 换算（184~194：MousePress→
//              QEvent::MouseButtonPress 等），第 210 行才真的
//              `QMouseEvent me(meType, pos, widget->mapToGlobal(pos), button, qtestMouseButtons, stateKey, QPointingDevice::primaryPointingDevice());`
//              ★ 勘误：旧稿写「218~220 行 QEvent::MouseButtonPress/Release 的构造」，那是
//                **217~218 行的 `static const char *const mouseActionNames[]`**——一个只为
//                qDebug 准备的字符串表，跟构造事件没关系；真正的 meType 换算在 180~194。
//              ← 记一件事就够：QMouseEvent 的构造**必须带 QPointingDevice**（210 行那个末位参数），
//                自己 sendEvent 造事件时漏了它会编不过。
//   第 233 行 mouseClick(QWidget*, button, modifiers, pos, delay)
//
// QtTest/qtestcase.h
//   第 177/184 行 qWait / 内部按步长处理事件
//   第 211 行 QTRY_VERIFY_WITH_TIMEOUT(expr, timeout)
//   第 238 行 QTRY_COMPARE(expr, expected)
//              ← ★ 「重试到成立或超时」，比 qWait(固定时间) 稳
//   第 299 行 QEXPECT_FAIL（第 1 讲讲过的「已知缺陷」标记）
//
// QtTest/qsignalspy.h
//   第 21 行 class QSignalSpy : public QList<QList<QVariant>>
//              ← 它就是个「参数列表的列表」，count() 是继承来的 size()
//   第 34/37 行 构造函数：可以传 PointerToMemberFunction（推荐，能编译期检查）
//   第 47/50 行 bool wait(int) / wait(std::chrono::milliseconds = 5s)
//              ← ★ 它自己跑事件循环，等到有信号或超时
//
// 跨讲链接：
//   · quality 第 1 讲：QVERIFY/QCOMPARE 的差别、数据驱动、QEXPECT_FAIL。
//   · events 模块：QTest 模拟的正是那些事件；不理解事件分发就看不懂为什么要点 center。
// ============================================================================

#include <QtTest/QTest>
// ★ 上面这个 <QtTest/QTest> 只包含 QTest 命名空间里的**核心断言**，
//   并不包含「模拟键鼠」的那批函数 —— 它们住在 qtestkeyboard.h / qtestmouse.h 里，
//   是「按需包含」的独立头文件。漏了就会报
//     error: 'keyClicks' is not a member of 'QTest'
//   很多人第一次写 GUI 测试都踩这个坑：断言能用，键鼠一调就找不到。
//   想省事可以改成伞形头 <QtTest/QtTest>，它会把这些全带上；
//   教材这里显式列出，是为了让你知道「到底缺的是哪一个」。
#include <QtTest/qtestkeyboard.h>
#include <QtTest/qtestmouse.h>
#include <QApplication>
#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QDialog>
#include <QTimer>
#include <QSignalSpy>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QString>
#include <QList>
#include <QVariant>
#include <QDebug>

// ============================================================================
// 二、被测代码：一个只会数左键的控件
// ----------------------------------------------------------------------------
// 为什么教材里要现造一个控件？因为「测标准控件」只能验证 Qt 自己，
// 而你在项目里要测的是**自己的事件处理**。
// 这个 ClickCounter 故意写得最小：只处理左键，右键交还给基类 ——
// 于是「右键不该计数」这条也能被断言。
// ============================================================================
class ClickCounter : public QWidget
{
    Q_OBJECT
public:
    explicit ClickCounter(QWidget *parent = nullptr) : QWidget(parent) {}

    int clicks() const { return m_clicks; }

Q_SIGNALS:
    void clicked(int total);

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            ++m_clicks;
            Q_EMIT clicked(m_clicks);
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);   // 右键等交给基类（会被忽略，但语义清楚）
    }

private:
    int m_clicks = 0;
};

// ============================================================================
// 三、测试类
// ============================================================================
class TestWidgets : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ---- 钩子：只跑一次 / 每个测试前各跑一次 ----
    void initTestCase();
    void init();
    void cleanup();
    void cleanupTestCase();

    // ---- 环境与基本事实 ----
    void platformIsARealQpaPlugin();
    void widgetVisibilityLifecycle();
    void findChildByObjectName();

    // ---- 键盘 ----
    void keyboardWithoutFocus();
    void keyboardIsLatin1Only();
    void keyboardLatin1AbortsProcess();     // 默认 QSKIP：它会把**本进程**打挂
    void textChangedSpy();

    // ---- 鼠标 ----
    void buttonClickBasics();
    void buttonClick_data();
    void buttonClick();
    void customWidgetCounts();

    // ---- 事件循环 ----
    void modalDialogIsClosedByATimer();
    void asyncSignalWithSpyWait();
    void tryMacroInsteadOfSleep();

private:
    // 每个测试都能用到的小工具：把控件变成「可点、可敲」的状态。
    static void makeReady(QWidget *w, int width = 240, int height = 80)
    {
        w->resize(width, height);
        w->show();
        QTest::qWait(0);          // 让平台插件真的把窗口建出来
    }

    int m_initCount = 0;
    int m_cleanupCount = 0;
};

// ============================================================================
// 四、钩子的顺序：initTestCase 一次 → (init → 测试 → cleanup) × N → cleanupTestCase
// ============================================================================
void TestWidgets::initTestCase()
{
    qInfo().noquote() << QStringLiteral("QPA 平台插件 = %1")
                             .arg(QGuiApplication::platformName());
    qInfo().noquote() << QStringLiteral(
        "  如果是 offscreen：没有窗口管理器、屏幕上是空的，但事件照样能分发 —— "
        "这正是界面测试需要的环境。★注意别推过头：本机实测 show() 之后 "
        "QWidget::hasFocus() 是 **true**（Qt 自己把焦点给了它），"
        "「没有真实焦点」指的是没有 WM 抢焦点，不等于 hasFocus()==false。");
    qInfo().noquote() << QStringLiteral("  QApplication 存在 = %1")
                             .arg(qobject_cast<QApplication *>(QCoreApplication::instance())
                                      ? QStringLiteral("是")
                                      : QStringLiteral("否"));
}

void TestWidgets::init()
{
    ++m_initCount;
}

void TestWidgets::cleanup()
{
    ++m_cleanupCount;
}

void TestWidgets::cleanupTestCase()
{
    // ★ 这一条断言的是 QtTest 的契约：每个测试函数前后各跑一次 init/cleanup。
    //   顺序不重要（QTest 的执行顺序是声明的顺序，但我不依赖它），
    //   数量必须相等 —— 不相等就说明有测试函数里抛了/失败了，钩子没配对。
    QCOMPARE(m_initCount, m_cleanupCount);
    QVERIFY2(m_initCount >= 10,
             qPrintable(QStringLiteral("只跑了 %1 轮，测试函数少了？").arg(m_initCount)));
    qInfo().noquote() << QStringLiteral("  一共跑了 %1 轮 init/cleanup").arg(m_initCount);
}

// ============================================================================
// 五、环境与基本事实
// ============================================================================
void TestWidgets::platformIsARealQpaPlugin()
{
    // 「有 QPA 插件」是界面测试的前提：没有它，QApplication 构造就会直接 abort。
    // 这里能跑说明有了；名字打出来是为了让你知道自己在什么环境里跑。
    QVERIFY(!QGuiApplication::platformName().isEmpty());
    QVERIFY(qobject_cast<QApplication *>(QCoreApplication::instance()) != nullptr);
}

void TestWidgets::widgetVisibilityLifecycle()
{
    QWidget w;                              // 栈上：函数返回就析构，不留垃圾
    QVERIFY(!w.isVisible());                // 造出来不等于显示出来
    w.resize(200, 100);
    w.show();
    QVERIFY(w.isVisible());
    w.hide();
    QVERIFY(!w.isVisible());

    // ★ 一个常见误解：isVisible() 为假不代表控件「没在跑」——
    //   它照样接收事件、照样有定时器、照样占内存。窗口关闭时该 delete 就 delete。
}

void TestWidgets::findChildByObjectName()
{
    QWidget root;
    auto *ok = new QPushButton(QStringLiteral("确定"), &root);
    ok->setObjectName(QStringLiteral("okBtn"));       // ★ 这一步是「可测性」的关键
    auto *cancel = new QPushButton(QStringLiteral("取消"), &root);
    cancel->setObjectName(QStringLiteral("cancelBtn"));

    QCOMPARE(root.findChild<QPushButton *>(QStringLiteral("okBtn")), ok);
    // 找不到时返回 nullptr，不抛异常 —— 所以断言要写 != nullptr 的相反面。
    QVERIFY(root.findChild<QPushButton *>(QStringLiteral("missing")) == nullptr);

    // 按类型找会拿到第一个；就算没有 objectName 也能找，只是顺序不保证。
    QVERIFY(root.findChild<QPushButton *>() != nullptr);

    // 交回所有权由 parent 负责：root 析构时两个按钮一起走，不用手动 delete。
}

// ============================================================================
// 六、键盘
// ============================================================================
void TestWidgets::keyboardWithoutFocus()
{
    QLineEdit edit;
    makeReady(&edit);

    // ★勘误：这里原本硬写 QVERIFY(!edit.hasFocus())，本机 offscreen 下**直接判红**——
    //   makeReady() 里的 show() 之后 Qt 真的把焦点给了这个控件。
    //   「键盘事件不要求焦点」这条本身没错（qtestkeyboard.h:194~197：你传了 widget
    //   就不去问 focusWidget），但「它一定没有焦点」是平台语义，不能当前提。
    //   所以改成：**先把焦点主动清掉、确认确实没有，再证明照样打得进去**——
    //   这样验证的是那条例，而不是本机窗口管理器的脾气。
    qInfo().noquote() << QStringLiteral("  show() 之后 hasFocus() = %1（offscreen 实测，只打印不断言）")
                             .arg(edit.hasFocus() ? QStringLiteral("true") : QStringLiteral("false"));
    edit.clearFocus();
    QVERIFY2(!edit.hasFocus(), "clearFocus() 之后还有焦点：那这条测试验证不了「无焦点」");

    QTest::keyClicks(&edit, QStringLiteral("abc"));
    QCOMPARE(edit.text(), QStringLiteral("abc"));

    QTest::keyClick(&edit, Qt::Key_Backspace);
    QCOMPARE(edit.text(), QStringLiteral("ab"));

    // 组合键：全选之后输入会替换掉原有内容。
    QTest::keyClick(&edit, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(&edit, QStringLiteral("z"));
    QCOMPARE(edit.text(), QStringLiteral("z"));
}

void TestWidgets::keyboardIsLatin1Only()
{
    // ★★ 这一节原本的说法太轻：「中文转不过 Latin-1，所以敲进去的不是原文」。
    //    本机实测（Qt 6.12.0 / MinGW release）真实链条是：
    //      keyClicks → QChar::toLatin1() 出一个 Latin-1 字节
    //                → QTest::asciiToKey(char)（qtestcase.h:515，是公开 API）
    //                → switch 里没有这一项 → default: QTEST_ASSERT(false)
    //                → qasciikey.cpp:192 → qt_assert → **abort，整个测试进程没了**
    //    两个容易误判的点：
    //      ① QTEST_ASSERT 在 qtestassert.h:11 **没有 QT_NO_DEBUG 保护**，
    //         release 的 Qt 一样炸；别以为换个构建就没事。
    //      ② 表覆盖 0x08~0x7f + 0xa0~0xdf + 0xe5 0xe6 0xf7 0xf8 0xff，
    //         所以 **é (0xe9) 也炸**：「Latin-1 能表示的字符就敲得进去」是错的，
    //         带重音的小写字母基本都不在表里。
    //    原本这里还有个「café 是对照组」的例子 —— 它恰恰是会 abort 的那个，
    //    于是整个二进制在 CI 里以 0xc0000602 收场（六个用例一起陪葬）。
    //    崩溃的部分挪到 keyboardLatin1AbortsProcess()，用环境变量开关隔离。
    QLineEdit edit;
    makeReady(&edit);

    // ① 丢信息发生在 toLatin1 这一步（纯 QtCore，安全）
    const QChar zh = u'中';
    const char zhByte = zh.toLatin1();
    qInfo().noquote() << QStringLiteral("  QChar(U+4E2D).toLatin1() = 0x%1，"
                                        "而原码点是 U+%2 —— 一个字节装不下它")
                             .arg(quint8(zhByte), 2, 16, QLatin1Char('0'))
                             .arg(QString::number(zh.unicode(), 16).toUpper());
    QVERIFY2(zh.unicode() > 0xFF, "U+4E2D 本来就不在 Latin-1 范围里，转换必然失真");

    // ② 表里有的字节：正着走一遍（0x61='a'、0x3f='?' 都在表内，不会 abort）
    QCOMPARE(QTest::asciiToKey('a'), Qt::Key_A);
    QCOMPARE(QTest::asciiToKey('?'), Qt::Key_Question);

    // ③ 反方向证明「é 不可达」：keyToAscii 把 Key_Eacute 映射成**大写** 0xc9。
    //    也就是说键→字节这一侧根本没有小写重音字母的位置，
    //    而字节→键那一侧（keyClicks 走的方向）0xe9 也不在表里 → 两头都到不了。
    const char eByte = QTest::keyToAscii(Qt::Key_Eacute);
    qInfo().noquote() << QStringLiteral("  keyToAscii(Key_Eacute) = 0x%1（那是大写 É 的字节，不是小写 é 的 0xe9）")
                             .arg(quint8(eByte), 2, 16, QLatin1Char('0'));
    QVERIFY2(int(eByte) != 0xe9, "键→字节居然给出了 0xe9：Qt 的表变了，重写这一节");
    QCOMPARE(QTest::asciiToKey(eByte), Qt::Key_Eacute);   // 0xc9 在表里，这一句安全

    // ④ 正路：要输入非 Latin-1，用 **Qt::Key 重载**或自己发带 text 的 QKeyEvent。
    //    原本这里「不敢写成断言」（怕输入法状态误红）。本机实测：**能进**，
    //    而且路径可控（不经 QTest 的 Latin-1 转换），所以钉成断言。
    QLineEdit safe;
    makeReady(&safe);
    const QString wanted = QStringLiteral("中文");
    for (const QChar ch : wanted) {
        QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(ch));
        QApplication::sendEvent(&safe, &press);
        QKeyEvent release(QEvent::KeyRelease, 0, Qt::NoModifier, QString(ch));
        QApplication::sendEvent(&safe, &release);
    }
    QVERIFY2(safe.text() == wanted,
             qPrintable(QStringLiteral("sendEvent + text 也没插进中文（本机 text()=\"%1\"）"
                                        "—— 输入法/平台相关，别把这条当普适结论")
                            .arg(safe.text())));
}

void TestWidgets::keyboardLatin1AbortsProcess()
{
    // ★这一条**默认跳过**，因为它是真的会把进程打挂：abort() 之后
    //   后面所有用例、连最终汇总都没了（ctest 里表现为 Exit code 0xc0000602）。
    //   想亲眼看一次：
    //     QTEST_FORCE_NON_LATIN1=1            ./quality_02_qtest_gui keyboardLatin1AbortsProcess  ← 「中文」
    //     QTEST_FORCE_NON_LATIN1=latin1_eacute ./quality_02_qtest_gui keyboardLatin1AbortsProcess  ← 「café」
    //   炸的原因都一样：toLatin1 出来的那个字节在 asciiToKey 的表里没有对应项。
    //   两个模式都实测过：两个都 abort，assert 都停在 qasciikey.cpp:192。
    const QByteArray mode = qEnvironmentVariable("QTEST_FORCE_NON_LATIN1").toUtf8();
    if (mode.isEmpty())
        QSKIP("打开 QTEST_FORCE_NON_LATIN1=1（或 =latin1_eacute）再单独跑这一个用例："
              "它会 abort 整个进程，留在默认用例列表里就是把整份测试一起判失败");

    const QString probe = mode == "latin1_eacute"
                              ? QStringLiteral("café")     // é = 0xe9，表里只有 0xe5/0xe6/0xf7/0xf8/0xff
                              : QStringLiteral("中文");
    QLineEdit a;
    makeReady(&a);
    QTest::keyClicks(&a, probe);                           // ← 这里就没了
    QFAIL("走到这里说明 Qt 6 改了 Latin-1 处理：这一节要重写");
}

void TestWidgets::textChangedSpy()
{
    QLineEdit edit;
    makeReady(&edit);

    // 用成员函数指针构造 QSignalSpy：签名在编译期就检查，写错名字编不过。
    QSignalSpy spy(&edit, &QLineEdit::textChanged);
    QVERIFY(spy.isValid());

    QTest::keyClicks(&edit, QStringLiteral("ab"));

    // 每敲一个键就发一次 —— 不是「敲完发一次」。
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("a"));
    QCOMPARE(spy.at(1).at(0).toString(), QStringLiteral("ab"));

    // takeFirst() 会把事件取走，count 跟着减 —— 适合「一条一条处理」的写法。
    const QList<QVariant> first = spy.takeFirst();
    QCOMPARE(first.at(0).toString(), QStringLiteral("a"));
    QCOMPARE(spy.count(), 1);
}

// ============================================================================
// 七、鼠标
// ============================================================================
void TestWidgets::buttonClickBasics()
{
    QPushButton button(QStringLiteral("确定"));
    makeReady(&button);

    QSignalSpy spy(&button, &QPushButton::clicked);
    QTest::mouseClick(&button, Qt::LeftButton);
    QCOMPARE(spy.count(), 1);

    // 禁用之后点击不该产生信号 —— 这条是「界面状态真的生效了」的证据，
    // 比「看一眼按钮变灰了」可靠得多。
    button.setEnabled(false);
    QTest::mouseClick(&button, Qt::LeftButton);
    QCOMPARE(spy.count(), 1);
}

void TestWidgets::buttonClick_data()
{
    QTest::addColumn<bool>("enabled");
    QTest::addColumn<int>("expectedClicks");

    QTest::newRow("enabled") << true << 1;
    QTest::newRow("disabled") << false << 0;
}

void TestWidgets::buttonClick()
{
    QFETCH(bool, enabled);
    QFETCH(int, expectedClicks);

    QPushButton button(QStringLiteral("确定"));
    makeReady(&button);
    button.setEnabled(enabled);

    QSignalSpy spy(&button, &QPushButton::clicked);
    QTest::mouseClick(&button, Qt::LeftButton);
    QCOMPARE(spy.count(), expectedClicks);
}

void TestWidgets::customWidgetCounts()
{
    ClickCounter counter;
    makeReady(&counter);

    QSignalSpy spy(&counter, &ClickCounter::clicked);
    QCOMPARE(counter.clicks(), 0);

    QTest::mouseClick(&counter, Qt::LeftButton);
    QTest::mouseClick(&counter, Qt::LeftButton);

    QCOMPARE(counter.clicks(), 2);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toInt(), 2);      // 信号带的是累计次数

    // 右键不该被计数（我们的 mousePressEvent 只认左键）。
    QTest::mouseClick(&counter, Qt::RightButton);
    QCOMPARE(counter.clicks(), 2);
    QCOMPARE(spy.count(), 2);
}

// ============================================================================
// 八、事件循环
// ============================================================================
void TestWidgets::modalDialogIsClosedByATimer()
{
    // exec() 会开一个**嵌套事件循环**并一直转到对话框被关闭。
    // 测试里如果直接 exec()，测试本身就挂住了 —— 所以先安排好「谁来关它」。
    // singleShot(0, ...) 的含义：等这个嵌套循环一跑起来，立刻替我点「确定」。
    QDialog dialog;
    QTimer::singleShot(0, &dialog, &QDialog::accept);
    QCOMPARE(dialog.exec(), int(QDialog::Accepted));
    QCOMPARE(dialog.result(), int(QDialog::Accepted));

    QDialog dialog2;
    QTimer::singleShot(0, &dialog2, &QDialog::reject);
    QCOMPARE(dialog2.exec(), int(QDialog::Rejected));

    // ★ 生产代码里别在测试外面这么写：嵌套事件循环会让「重入」变成可能。
    //   这是**测试**里制造「用户点了确定」的标准办法，不是应用代码的写法。
}

void TestWidgets::asyncSignalWithSpyWait()
{
    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(10);

    QSignalSpy spy(&timer, &QTimer::timeout);
    timer.start();

    // ★ spy.wait(1000)：自己跑事件循环，等到信号来或超时。
    //   比 qWait(1000) 好 —— 信号 10 ms 就来了，它 10 ms 就返回。
    QVERIFY2(spy.wait(1000), "1 秒内没等到 timeout，定时器没转起来？");
    QCOMPARE(spy.count(), 1);
}

void TestWidgets::tryMacroInsteadOfSleep()
{
    bool fired = false;
    QTimer::singleShot(20, this, [&fired]() { fired = true; });

    // ★ QTRY_VERIFY_WITH_TIMEOUT 会「按步长检查 → 没成立就再等一会」，
    //   成立就立刻通过。所以它既不慢（不像 qWait(2000) 固定等满），
    //   也不脆（不像 qWait(5) 等不够）。
    QTRY_VERIFY_WITH_TIMEOUT(fired, 2000);

    // 对照一下固定等待的写法有多容易出错（这里故意等 0 毫秒：
    // 因为上一条已经等到了，所以它是通过的 —— 但如果上面那条删掉，它多半会红）。
    QTest::qWait(0);
    QVERIFY(fired);
}

// ============================================================================
// 九、收尾话：界面测试的三条纪律
// ----------------------------------------------------------------------------
//   ① 只断言「契约」，不断言「像素」。按钮点了要发 clicked，这是契约；
//      按钮点了要变蓝，那是样式表的事，换个主题就崩。
//   ② 每个测试自己造控件、自己收拾。栈上对象最省心：函数返回就没了，
//      不会有一个隐藏的窗口活到下一个测试里捣乱。
//   ③ 异步的东西用 spy.wait / QTRY_*，不要用 qWait(固定值)。
//      「等 500 毫秒应该够了」这句话，在 CI 机器上是会打脸的。
// ============================================================================

// 本文件有 2 个 Q_OBJECT 类（ClickCounter / TestWidgets），
// AUTOMOC 生成同名 .moc 并在这里包含。
#include "02_qtest_gui.moc"

// QTEST_MAIN 自己造 QApplication 并 return qExec 的结果：全过 0，有失败非 0。
QTEST_MAIN(TestWidgets)
