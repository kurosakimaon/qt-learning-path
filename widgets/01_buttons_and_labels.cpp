// ============================================================================
// 第 1 讲 · 按钮与标签（QAbstractButton 家族 + QLabel）
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   「按钮」看着是 Hello World 级的东西，但工作里 70% 的界面 bug 出在
//   对它的四个信号语义理解错了。本讲把下面这几件事一次讲透：
//     1) QAbstractButton 的四种按钮（QPushButton / QToolButton / QCheckBox /
//        QRadioButton / QCommandLinkButton）共用同一套「可勾选（checkable）」机制；
//     2) pressed / released / clicked / toggled **各发几次、按什么顺序发**，
//        click() 与 animateClick() 的区别（本讲用计数器实测出来）；
//     3) autoExclusive 与 QButtonGroup 的分工：前者是「同一个父控件里自动互斥」，
//        后者是「跨父控件、并且给你 id 与统一信号」；
//     4) QLabel 不是「只能摆文字」：富文本（Rich Text）、setBuddy、pixmap、movie
//        四条路各是什么、什么时候用；
//     5) QPushButton::setMenu / QToolButton::setMenu 挂菜单的正确姿势。
//
// 对应构建目标名：widgets_01_buttons_and_labels
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(widgets_01_buttons_and_labels 01_buttons_and_labels.cpp)
//   target_link_libraries(widgets_01_buttons_and_labels PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)   # 本文件里**没有** Q_OBJECT 类，因此不需要 #include "01_....moc"
//
// 运行前须知：
//   · 依赖 Qt6::Widgets。Windows 上要保证 platforms/qwindows.dll 或 qoffscreen.dll 可寻址。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 widgets_01_buttons_and_labels，跑到自动退出、退出码 0）。
//     但本讲**没有汇总自检行**，退出码 0 只证明「没崩、走完了」，不证明正文每个数字都对：
//     带具体数字的「预期」请以你自己那次运行输出为准（本机那一轮存档 runlogs/widgets_01_buttons_and_labels.log）。
//   · 全程无人值守：用 QTimer::singleShot 依次 click()/setChecked()/setCheckState()
//     驱动，把关键结论用 qInfo() 打出来，最后 QCoreApplication::quit()。
//     **没有任何阻塞调用**：不 exec() 对话框、不弹静态 QMessageBox、不 QMenu::exec()。
//   · Qt5 → Qt6 差异（本讲涉及，已核对本机头文件）：
//       - QAction 在 Qt6 声明于 **QtGui**（QtGui/qaction.h 第 30 行 `class Q_GUI_EXPORT QAction`），
//         QtWidgets/qaction.h 只是一个转发头（里面只有一行 `#include <QtGui/QAction>`）。
//         Qt5 时代它是 Q_WIDGETS_EXPORT。→ 链接时只连 Qt6::Gui 也能拿到 QAction。
//         （注意：网上不少迁移文章把方向写反了，以本机头文件为准。）
//       - QButtonGroup 在 Qt6 只有指针版信号：buttonClicked(QAbstractButton*) 等，
//         **int 版已被移除**，取代它的是 idClicked(int)/idPressed/idReleased/idToggled(int,bool)
//         （QtWidgets/qbuttongroup.h 第 44~52 行，八个信号一目了然）。
//         Qt5 的 `connect(grp, QOverload<int>::of(&QButtonGroup::buttonClicked), ...)` 在 Qt6 编不过。
// ============================================================================

#include <QApplication>          // QtWidgets/qapplication.h
#include <QCoreApplication>      // QtCore/qcoreapplication.h：quit()
#include <QWidget>
#include <QVBoxLayout>           // QtWidgets/qboxlayout.h：addStretch/addWidget
#include <QHBoxLayout>
#include <QPushButton>           // QtWidgets/qpushbutton.h
#include <QToolButton>           // QtWidgets/qtoolbutton.h
#include <QCheckBox>             // QtWidgets/qcheckbox.h
#include <QRadioButton>          // QtWidgets/qradiobutton.h
#include <QCommandLinkButton>    // QtWidgets/qcommandlinkbutton.h
#include <QButtonGroup>          // QtWidgets/qbuttongroup.h
#include <QAbstractButton>       // QtWidgets/qabstractbutton.h
#include <QLabel>                // QtWidgets/qlabel.h
#include <QLineEdit>             // 只为演示 setBuddy 的「伙伴」
#include <QMenu>                 // QtWidgets/qmenu.h：addAction 返回 QAction*
#include <QAction>               // ⚠ Qt6 实为 QtGui/qaction.h
#include <QMovie>                // QtGui/qmovie.h
#include <QPixmap>               // QtGui/qpixmap.h
#include <QColor>
#include <QTimer>
#include <QDebug>
#include <QString>

// ============================================================================
// 一、核对签名（本讲全部依据，路径相对 D:\Qt\6.12.0\mingw_64\include）
// ============================================================================
// QtWidgets/qabstractbutton.h
//   第 23 行   class Q_WIDGETS_EXPORT QAbstractButton : public QWidget
//   第 33~39 行 属性：checkable / checked(NOTIFY toggled) / autoRepeat /
//                autoExclusive / autoRepeatDelay / autoRepeatInterval / down
//   第 45 行   void setText(const QString &text);
//   第 58~61 行 void setCheckable(bool); bool isCheckable() const; bool isChecked() const;
//   第 63~64 行 void setDown(bool); bool isDown() const;
//   第 75~76 行 void setAutoExclusive(bool); bool autoExclusive() const;
//   第 84 行   void animateClick();          ← 播一次「按下动画」，**异步**
//   第 85 行   void click();                 ← 立刻在代码里「按一下」，**同步**
//   第 86 行   void toggle();  第 87 行 void setChecked(bool);
//   第 89~93 行 Q_SIGNALS: pressed(); released(); clicked(bool checked = false); toggled(bool checked);
//              ↑ 注意 Qt6 这里**没有** toggled(bool) 的默认实参， clicked **有**（默认 false）
// QtWidgets/qpushbutton.h
//   第 20 行   class Q_WIDGETS_EXPORT QPushButton : public QAbstractButton
//   第 43~44 行 void setMenu(QMenu* menu);  QMenu* menu() const;
//   第 37~40 行 autoDefault / isDefault（键盘回车默认按钮那套）
//   第 47~48 行 setFlat / isFlat      第 52 行 void showMenu();（protected）
// QtWidgets/qtoolbutton.h
//   第 31~35 行 enum ToolButtonPopupMode { DelayedPopup, MenuButtonPopup, InstantPopup };
//   第 50 行   void setMenu(QMenu* menu);   第 53~54 行 setPopupMode / popupMode
//   第 59 行   void setAutoRaise(bool enable);
//   第 66 行   void setToolButtonStyle(Qt::ToolButtonStyle style);
//   第 70 行   Q_SIGNALS: void triggered(QAction *);      ← QToolButton 独有的「菜单项被触发」
// QtWidgets/qcheckbox.h
//   第 23 行   属性 tristate；第 33~34 行 setTristate(bool y = true) / isTristate
//   第 37 行   void setCheckState(Qt::CheckState state);
//   第 40~42 行  ★ stateChanged(int) 在 Qt6 已经**弃用**，本机头文件原文是：
//              #if QT_DEPRECATED_SINCE(6, 9)                              // 第 40 行
//                  QT_MOC_COMPAT QT_DEPRECATED_VERSION_X_6_9("Use checkStateChanged() instead")
//                  void stateChanged(int);                                // 第 42 行
//              #endif
//              两个后果：① 直接 &QCheckBox::stateChanged 编译时报警（本文件下面用
//              QT_WARNING_PUSH 把这一处框起来，为的是「0 warning」的仓库标准，同时保留对照演示）；
//              ② 一旦你在 CMake 里设了 QT_DISABLE_DEPRECATED_BEFORE=0x060900（或更高），
//              这个信号**从类里消失**，指针版 connect 直接编不过。
//              QT_MOC_COMPAT 让它仍然留在 moc 的元数据里，所以字符串版
//              connect(cb, SIGNAL(stateChanged(int)), ...) 还能连——**弃用的是 C++ 写法，不是信号本身**。
//   第 44 行   void checkStateChanged(Qt::CheckState);  ← **Qt5.9+ 新增的强类型信号**，新代码只用这个
// QtWidgets/qradiobutton.h
//   第 19 行   class Q_WIDGETS_EXPORT QRadioButton : public QAbstractButton
//              （整个类没加任何新信号/新属性：单选完全靠 autoExclusive + QButtonGroup）
// QtWidgets/qcommandLinkbutton.h
//   第 18 行   class Q_WIDGETS_EXPORT QCommandLinkButton : public QPushButton
//   第 32 行   void setDescription(const QString &description);   ← 「主标题 + 说明文字」两行式
// QtWidgets/qbuttongroup.h
//   第 19 行   class Q_WIDGETS_EXPORT QButtonGroup : public QObject
//   第 23 行   属性 exclusive（默认 true）；第 28~29 行 setExclusive / exclusive
//   第 31 行   void addButton(QAbstractButton *, int id = -1);    ← id 可省，省了就 -1
//   第 34 行   QList<QAbstractButton*> buttons() const;
//   第 36/42 行 QAbstractButton *checkedButton() const;  int checkedId() const;
//   第 44~52 行 八个信号：buttonClicked/buttonPressed/buttonReleased/buttonToggled(QAbstractButton*[,bool])
//                + idClicked(int)/idPressed(int)/idReleased(int)/idToggled(int,bool)
// QtWidgets/qlabel.h
//   第 20 行   class Q_WIDGETS_EXPORT QLabel : public QFrame
//   第 23~34 行 属性：text / textFormat / pixmap / scaledContents / alignment / wordWrap /
//                margin / indent / openExternalLinks / textInteractionFlags / selectedText
//   第 60 行   void setTextFormat(Qt::TextFormat);
//   第 78 行   void setScaledContents(bool);    第 88 行 setOpenExternalLinks(bool)
//   第 82 行   void setBuddy(QWidget *);        ← Alt+助记键时把焦点交给 buddy
//   第 99~108 行 setText / setPixmap / setPicture / setMovie / setNum(int) / setNum(double)
//   第 111~113 行 Q_SIGNALS: linkActivated(const QString&); linkHovered(const QString&);
//   第 80 行   QSize minimumSizeHint() const override;   ← 图片标签的宽高比就藏在这里（第 8 讲）
// QtGui/qmovie.h
//   第 28 行   class Q_GUI_EXPORT QMovie : public QObject     ← Qt6 里 QMovie 属于 **QtGui**
//   第 35~40 行 enum MovieState { ... } Q_ENUM(MovieState)
//   第 57 行   void setFileName(const QString &fileName);
//   第 66 行   MovieState state() const;  第 86 行 setScaledSize  第 102 行 start()  第 105 行 stop()
//   第 92~99 行 信号：started / resized / updated / stateChanged / error / finished / frameChanged(int)
// QtCore/qnamespace.h
//   第 1213~1216 行 enum TextFormat { PlainText, RichText, AutoText };   ← QLabel 默认 AutoText
//   第 1511~1514 行 enum CheckState { Unchecked, PartiallyChecked, Checked };
// QtGui/qpixmap.h
//   第 32 行   QPixmap(int w, int h);   第 62 行 void fill(const QColor &fillColor = Qt::white);

// ============================================================================
// 二、一份「信号计数器」：放在堆上，因为接收它的是事件循环里的 lambda
// ============================================================================
struct SigCount
{
    int pressed = 0;
    int released = 0;
    int clicked = 0;
    int toggled = 0;
    int lastChecked = -1;
};

static void wireCounter(QAbstractButton *btn, SigCount *c)
{
    QObject::connect(btn, &QAbstractButton::pressed,   [c] { ++c->pressed; });
    QObject::connect(btn, &QAbstractButton::released,  [c] { ++c->released; });
    QObject::connect(btn, &QAbstractButton::clicked,   [c] { ++c->clicked; });
    // toggled 的签名是 toggled(bool)（qabstractbutton.h 93 行），必须带参数接
    QObject::connect(btn, &QAbstractButton::toggled,   [c](bool on) { ++c->toggled; c->lastChecked = on ? 1 : 0; });
}

// ============================================================================
// 三、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle(QStringLiteral("第 1 讲 · 按钮与标签"));
    window.resize(560, 420);

    auto *root = new QVBoxLayout(&window);   // qboxlayout.h 39 行 addWidget(QWidget*, int stretch, ...)

    // ── 证据 ①  四个信号各自的语义（本讲最值钱的一段）───────────────
    auto *plain  = new QPushButton(QStringLiteral("普通按钮（非 checkable）"));
    auto *check  = new QPushButton(QStringLiteral("可勾选的按钮"));
    check->setCheckable(true);                        // qabstractbutton.h 58 行
    auto *tool   = new QToolButton;                   // 默认 QToolButton(QWidget *parent = nullptr)
    tool->setText(QStringLiteral("工具"));
    tool->setPopupMode(QToolButton::MenuButtonPopup); // qtoolbutton.h 31~35、53 行
    SigCount *cPlain = new SigCount, *cCheck = new SigCount, *cTool = new SigCount;
    wireCounter(plain, cPlain);
    wireCounter(check, cCheck);
    wireCounter(tool, cTool);
    root->addWidget(plain);
    root->addWidget(check);
    root->addWidget(tool);

    // 语义预习（**跑之前先立个预期，跑完拿打印对表**）：
    //   pressed()  —— 按下那一刻（鼠标按下 / 空格按下）
    //   released() —— 松开那一刻（**不论指针有没有离开按钮**都会发）
    //   clicked()  —— 「一次完整的点击」：Qt 在 released 之后补发；
    //                  只有按下与松开都发生在按钮**内部**时才算一次 click
    //   toggled()  —— 只有 checkable 的按钮才有，勾选状态**翻转**时发；
    //                  非 checkable 的按钮永远 0 次
    // 【为什么这重要】做「按下即生效」的交互（比如录音键）连 pressed；
    // 做「点一下执行一个动作」连 clicked；做「状态同步」连 toggled。
    // 连错一个，就会得到「拖出去松手也执行了」或「状态变了但没反应」这类怪 bug。

    // ── 证据 ②  QButtonGroup：跨父控件的互斥 + id ──────────────────
    auto *radioA = new QRadioButton(QStringLiteral("选项 A"));
    auto *radioB = new QRadioButton(QStringLiteral("选项 B"));
    auto *radioC = new QRadioButton(QStringLiteral("选项 C"));
    auto *rowRadio = new QHBoxLayout;
    rowRadio->addWidget(radioA); rowRadio->addWidget(radioB); rowRadio->addWidget(radioC);
    root->addLayout(rowRadio);                          // qboxlayout.h 40 行

    // QRadioButton 默认 autoExclusive == true（qabstractbutton.h 36 行的属性），
    // 于是**同一个父控件下的单选按钮自动互斥**——这就是「不用 QButtonGroup 也能单选」的原因。
    // 一旦它们被拆到不同的布局容器/不同的父控件里，自动互斥就失效了，这时必须请出 QButtonGroup。
    auto *group = new QButtonGroup(&window);            // qbuttongroup.h 31 行：addButton(QAbstractButton*, int id)
    group->addButton(radioA, 10);
    group->addButton(radioB, 20);
    group->addButton(radioC, 30);
    qInfo() << "② autoExclusive：radioA =" << radioA->autoExclusive()
            << " 临时构造一个 QCheckBox 的默认值 =" << QCheckBox().autoExclusive()
            << "（复选框当然不互斥）→ 每个按钮都带这个属性，"
               "互斥是按钮自己实现的，不是父控件的功劳；这里的临时对象只为读默认值，构造即析构";

    // ⚠ Qt5→Qt6：这里只能连指针版或 id 版；老代码的 buttonClicked(int) 在本机头文件里不存在
    QObject::connect(group, &QButtonGroup::idClicked, [](int id) {
        qInfo() << "② idClicked(" << id << ") —— Qt6 的正确写法，替代 Qt5 的 buttonClicked(int)";
    });
    QObject::connect(group, &QButtonGroup::idToggled, [](int id, bool on) {
        qInfo() << "② idToggled(id =" << id << ", on =" << on << ")";
    });
    int *clickHits = new int(0);
    // ⚠ 用「函数指针 + lambda」连接时，lambda 里**没有** QObject::sender()——
    //   它只在带 context 对象的成员函数槽里可用（metaobject 模块讲过这条）。
    //   想要发送者：要么把 group 捕获进来（下面这样），要么给 connect 传第三个参数 context。
    QObject::connect(group, &QButtonGroup::buttonClicked, [clickHits, group](QAbstractButton *b) {
        ++*clickHits;
        qInfo() << "② buttonClicked 指针版：谁被点了 =" << b->text()
                << "；group->id(b) =" << group->id(b);              // qbuttongroup.h 41 行 int id(QAbstractButton*) const
    });
    QTimer::singleShot(2850, [clickHits] {
        qInfo() << "② buttonClicked（指针版）总共收到 =" << *clickHits
                << "次；它与 idClicked 是**同一次点击的两个信号**，不是两次点击";
        delete clickHits;
    });

    // ── 证据 ③  QCheckBox 三态（tristate）与两个 stateChanged 信号 ──
    auto *tri = new QCheckBox(QStringLiteral("三态复选框"));
    tri->setTristate(true);                              // qcheckbox.h 33 行
    QObject::connect(tri, &QCheckBox::checkStateChanged, [](Qt::CheckState s) {
        // qnamespace.h 1511~1514：Unchecked=0 / PartiallyChecked=1 / Checked=2
        qInfo() << "③ checkStateChanged 强类型信号 → Qt::CheckState 枚举值 =" << int(s);
    });
    // ↓↓↓ 这一小段是**故意**连一个已弃用的信号，好让你看见「一次改动、两个广播」。
    //     用 Qt 自己的 QT_WARNING_PUSH / QT_WARNING_DISABLE_GCC / QT_WARNING_POP
    //     把警告关掉（qcompilerdetection.h 第 1253、1255、1254 行），
    //     而不是把演示删掉——**关掉的范围越小越好**，POP 一定要紧跟在这一处后面。
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_GCC("-Wdeprecated-declarations")
    QObject::connect(tri, &QCheckBox::stateChanged, [](int v) {
        qInfo() << "③ stateChanged(int) 老信号（Qt 6.9 起弃用）同时收到 =" << v
                << "（两个信号是**重复广播**，新代码请只连 checkStateChanged）";
    });
    QT_WARNING_POP
    root->addWidget(tri);

    auto *cbPlain = new QCheckBox(QStringLiteral("普通复选框（可与上面共存）"));
    cbPlain->setChecked(true);                           // setChecked 也会发 toggled
    root->addWidget(cbPlain);
    SigCount *cCb = new SigCount;
    wireCounter(cbPlain, cCb);   // 稍后打印：setChecked() 在构造期发过一次 toggled

    // ── 证据 ④  QCommandLinkButton：Vista 风格的「标题 + 说明」 ─────
    auto *clb = new QCommandLinkButton(QStringLiteral("以管理员身份继续"));
    clb->setDescription(QStringLiteral("副标题：告诉用户点下去会发生什么"));  // qcommandlinkbutton.h 32 行
    clb->setCheckable(true);                             // ← 它也是 QAbstractButton 的血脉，照样能勾选
    root->addWidget(clb);

    // ── 证据 ⑤  QLabel 的四条路：文字 / 富文本+链接 / 图片 / 动画 ────
    auto *labRich = new QLabel(QStringLiteral("支持 <b>富文本</b>、<i>斜体</i> 和"
                                              "<a href=\"#self\">链接</a>"));
    labRich->setTextFormat(Qt::RichText);                // qlabel.h 60 行 + qnamespace.h 1213 行
    labRich->setOpenExternalLinks(false);                // 88 行：false = 链接交回 linkActivated 自己处理
    labRich->setWordWrap(true);                          // 28 行属性：换行会影响 minimumSizeHint（第 8 讲）
    QObject::connect(labRich, &QLabel::linkActivated, [](const QString &link) {
        qInfo() << "⑤ linkActivated：" << link;
    });
    QObject::connect(labRich, &QLabel::linkHovered, [](const QString &link) {
        qInfo() << "⑤ linkHovered：" << link;
    });
    root->addWidget(labRich);
    // 【诚实说明】linkActivated / linkHovered 来自**真实鼠标与键盘交互**
    // （QLabel 内部靠 mousePressEvent + 富文本文档的锚点判断），
    // 没有任何公开的「编程触发」入口，所以本讲只接线、不假造信号。
    // 想在无人值守里验证它，请用 QTest::mouseClick（测试模块会正面讲）。

    auto *labBuddy = new QLabel(QStringLiteral("姓名(&N)："));
    auto *editBuddy = new QLineEdit;
    labBuddy->setBuddy(editBuddy);                       // qlabel.h 82 行
    root->addWidget(labBuddy);
    root->addWidget(editBuddy);
    // setBuddy 的用处：按 Alt+N，焦点直接跳进 editBuddy（助记键 & 必须存在）。
    // 同理它还是 QLabel 尺寸谈判里的「 buddy 依赖」（sizeHint 会看 buddy 的 hint，第 8 讲）。

    auto *labPix = new QLabel;
    QPixmap pm(160, 48);                                 // qpixmap.h 32 行
    pm.fill(QColor(Qt::darkBlue));                       // qpixmap.h 62 行 fill(const QColor&)
    labPix->setPixmap(pm);                               // qlabel.h 100 行
    labPix->setScaledContents(true);                     // 78 行：true = 图随标签尺寸拉伸（会变形，慎用）
    root->addWidget(labPix);

    auto *labNum = new QLabel;
    labNum->setNum(3.14159);                             // qlabel.h 108 行 setNum(double)
    qInfo() << "⑤ setNum(3.14159) 之后文字 =" << labNum->text()
            << "（它只是个「数字 → 本地化字符串」的快捷写法，别为它建属性绑定）";
    root->addWidget(labNum);

    auto *movie = new QMovie(&window);                   // QtGui/qmovie.h
    QObject::connect(movie, &QMovie::frameChanged, [](int f) {
        qInfo() << "⑤ QMovie::frameChanged → 帧号" << f;
    });
    QObject::connect(movie, &QMovie::finished, [] { qInfo() << "⑤ QMovie::finished（动图放完了）"; });
    qInfo() << "⑤ 未附带 .gif 资源，故只演示接线；movie->state() 当前枚举值 ="
            << int(movie->state());                     // qmovie.h 66 行 state()
    // 真正要用起来：movie->setFileName(u"loading.gif"_s)（57 行）→ label->setMovie(movie)（105 行）
    // → movie->start()（102 行）。QMovie 的解码在**当前线程**的事件循环里推进，
    // 也就是说它跟第 4 讲的定时器一样：没有事件循环，一帧都不会动。

    // ── 证据 ⑥  QPushButton::setMenu / QToolButton::setMenu ────────
    auto *drop = new QPushButton(QStringLiteral("带下拉菜单的按钮"));
    auto *menu = new QMenu(drop);
    QAction *actExport = menu->addAction(QStringLiteral("导出"));   // qmenu.h 45 行 QAction *addAction(const QString&)
    QAction *actQuit   = menu->addAction(QStringLiteral("退出"));
    QObject::connect(actExport, &QAction::triggered, [](bool checked) {
        // QtGui/qaction.h Q_SIGNALS 段：void triggered(bool checked = false);
        qInfo() << "⑥ QAction::triggered 收到，checked =" << checked;
    });
    QObject::connect(actQuit, &QAction::triggered, [] { qInfo() << "⑥ 菜单项「退出」被触发（本次不真退出）"; });
    drop->setMenu(menu);                                 // qpushbutton.h 43 行
    qInfo() << "⑥ QPushButton::setMenu 之后：menu() =" << (drop->menu() == menu)
            << "（注意 setMenu 会**改变按钮的绘制**：右侧出现箭头，点击变成弹菜单而不是发 clicked 的语义）";
    root->addWidget(drop);

    tool->setMenu(menu);                                 // qtoolbutton.h 50 行：同一个 QMenu 也能挂给工具按钮
    QObject::connect(tool, &QToolButton::triggered, [](QAction *a) {
        qInfo() << "⑥ QToolButton::triggered（菜单项被选中）：" << a->text();
    });
    // 【为什么这里不 showMenu()】两个类的 showMenu() 会弹出真正的弹出菜单（popup），
    // 弹出菜单会开始抓取鼠标，无人值守跑到一半容易被它吃掉后续事件；
    // 而 QMenu::exec()（qmenu.h 第 146 行）是**阻塞**的，属于本资料库明令禁止出现在示例主路径的东西。

    window.show();

    // ============================================================================
    // 四、自动脚本：用 click()/animateClick()/setChecked() 把信号逼出来
    // ============================================================================
    // click()（qabstractbutton.h 85 行）：在代码里按一次，**同步**依次发 pressed→released→clicked；
    //   文档明说它对 checkable 的按钮会翻转状态（因此还会发 toggled），但**不触发**自动互斥？——
    //   这一点请以你机器上的打印为准：本机只能核对签名，实现不在头文件里。
    // animateClick()（84 行）：播一次「按下动画」，动作是**异步**排进事件循环的，
    //   所以紧接着读计数器会看到 0，过一会儿才变——这是本讲第二个坑。

    QTimer::singleShot(300, [plain, cPlain] {
        plain->click();
        qInfo() << "① click() 非 checkable 之后：pressed =" << cPlain->pressed
                << " released =" << cPlain->released
                << " clicked =" << cPlain->clicked
                << " toggled =" << cPlain->toggled << "（toggled 必须是 0）";
    });

    QTimer::singleShot(600, [check, cCheck] {
        check->click();
        qInfo() << "① click() checkable 之后：pressed =" << cCheck->pressed
                << " released =" << cCheck->released
                << " clicked =" << cCheck->clicked
                << " toggled =" << cCheck->toggled
                << " isChecked =" << check->isChecked()
                << "（一次点击：前三个各 +1，toggled 也 +1）";
    });

    QTimer::singleShot(900, [check, cCheck] {
        // 纯改状态：**不发 clicked**，只发 toggled —— 这就是「状态同步」要连 toggled 的理由
        check->setChecked(false);                        // qabstractbutton.h 87 行
        qInfo() << "① 只调 setChecked(false) 之后：clicked 增量 =" << (cCheck->clicked - 1)
                << "（应为 0）toggled 累计 =" << cCheck->toggled << "（应为 2）";
    });

    QTimer::singleShot(1200, [plain, cPlain] {
        plain->animateClick();                           // qabstractbutton.h 84 行：异步
        qInfo() << "① animateClick() **刚返回**时 clicked =" << cPlain->clicked
                << "（此刻大概率还是 1 —— 动画走的是事件循环，稍后自己会补上）";
    });

    QTimer::singleShot(1500, [plain, cPlain] {
        qInfo() << "① animateClick() 300ms 之后：pressed =" << cPlain->pressed
                << " released =" << cPlain->released << " clicked =" << cPlain->clicked
                << " → 与 click() 的区别得到证据：**同样是发一遍信号， animateClick 是延后完成的**";
        (void)plain;
    });

    QTimer::singleShot(1800, [radioA, radioB, group] {
        radioA->click();                                 // qabstractbutton.h 85 行
        qInfo() << "② 点 A 之后：A checked =" << radioA->isChecked()
                << " B checked =" << radioB->isChecked()
                << " group->checkedId =" << group->checkedId()
                << "（10）group->checkedButton()->text =" << group->checkedButton()->text();
        radioB->click();
        qInfo() << "② 再点 B 之后：A checked =" << radioA->isChecked()
                << " B checked =" << radioB->isChecked()
                << " group->checkedId =" << group->checkedId() << "（20）";
    });

    QTimer::singleShot(2100, [group, radioC] {
        group->setExclusive(false);                      // qbuttongroup.h 28 行
        radioC->setChecked(true);
        qInfo() << "② setExclusive(false) 后同时勾两个："
                << " A/B/C checked =" << group->buttons().size() << "个按钮在册；"
                << " C checked =" << radioC->isChecked()
                << "；取消互斥后 checkedId 在有多个勾选时**没有意义**（文档语义，别依赖）";
    });

    QTimer::singleShot(2400, [tri] {
        tri->setCheckState(Qt::PartiallyChecked);        // qcheckbox.h 37 行 + qnamespace.h 1513 行
        qInfo() << "③ setCheckState(PartiallyChecked) 之后 isChecked() =" << tri->isChecked()
                << "（注意：**部分选中在 isChecked() 里读作 true**，三态只能用 checkState() 表达）";
        tri->click();                                    // qabstractbutton.h 85 行：三态会往下走一格
        qInfo() << "③ click() 之后 isChecked() =" << tri->isChecked()
                << "；请对照上面 checkStateChanged 那两行打印，"
                   "自己数出 Unchecked(0)→PartiallyChecked(1)→Checked(2) 的循环顺序";
    });

    QTimer::singleShot(2700, [cbPlain, cCb, clb] {
        clb->setChecked(true);
        qInfo() << "④ QCommandLinkButton 也是 QAbstractButton：isChecked =" << clb->isChecked()
                << "；构造期那句 setChecked(true) 已经让复选框 toggled 计数 =" << cCb->toggled
                << "（**先连信号再改状态**才收得到，顺序错了就漏事件——新手第一大漏）";
        (void)cbPlain;
    });

    QTimer::singleShot(3000, [] {
        qInfo() << "\n=== 结论 ===";
        qInfo() << "① clicked = pressed + released 的「合成结果」；toggled 只属于 checkable；setChecked 不发 clicked。";
        qInfo() << "② click() 同步、animateClick() 异步；测试与自动化脚本里请优先用 click()。";
        qInfo() << "③ autoExclusive 只在同一父控件里管用，跨容器必须 QButtonGroup；Qt6 请用 idClicked/idToggled。";
        qInfo() << "④ QLabel 的富文本走 Qt::RichText + AutoText 自动嗅探；链接信号只能由真实交互产生。";
        QCoreApplication::quit();                        // qcoreapplication.h：static void quit()
    });

    return app.exec();
}

// ============================================================================
// 附：按钮家族选型表 + 三个常见误用
// ============================================================================
/*
 *  需求                          选择                                  理由
 *  ---------------------------------------------------------------------------
 *  执行一个动作                  QPushButton + clicked                 一次点击一件事
 *  反复开关某个状态              QPushButton(setCheckable) + toggled   状态可视、可快捷键
 *  工具栏上的图标按钮            QToolButton(+autoRaise/setMenu)       样式更紧凑，天生配菜单
 *  互斥的 2~4 个已知选项         QRadioButton（同父控件即可自动互斥）  语义最直白
 *  互斥但跨容器/需要数字 id      QButtonGroup + addButton(b, id)       与父子关系解耦
 *  可多选的二值/三值开关         QCheckBox（tristate 用于「部分选中」） 树形全选就是三态
 *  向导/设置页里「大按钮 + 解释」 QCommandLinkButton                   两行文字（text + description）
 *  一组带箭头的命令              QPushButton::setMenu / QToolButton::setMenu 省一层弹出窗口
 *
 * 三个我这些年真修过的 bug：
 *  ① 把「按下即开始录音」连在 clicked 上：用户在按钮上按下、拖出按钮外松手，
 *     录音**没开始也没取消**——因为 clicked 只在按下与松开同区域时才发。
 *     这类「按下/松开要分开」的交互必须连 pressed 与 released 两个信号。
 *  ② 在构造函数里 `setChecked(true)`，在初始化列表之后才 connect(toggled)：
 *     界面打开时状态是勾上的，但依赖 toggled 同步的那个 pane 一片空白。
 *     改法：要么 connect 之后再 setChecked，要么在末尾**主动调一次同步函数**。
 *  ③ 用 QButtonGroup 但指望 buttonClicked(int) 编译（Qt5 代码直接搬 Qt6）：
 *     本机 qbuttongroup.h 44~52 行里没有 int 版 buttonClicked，编译器会报
 *     「no matching member function」/「ambiguous overload」。请改成 idClicked。
 *
 * ——— 老师傅的话 ———
 *   徒弟，按钮这东西，你今天的收获不是那五个类，而是**四个信号的时间线**。
 *   把它们在纸上画成一条时间轴：手指按下 → pressed；手指抬起 → released；
 *   Qt 判断这一下「算一次」→ clicked；如果是可勾选的，状态翻转的那一刻 → toggled。
 *   以后凡遇到「界面状态和内部状态不一致」的 bug，你先别急着读代码，
 *   先问自己：我连的是这条线上的哪一个点？我是不是连在「动作」上，
 *   而我真正需要的是「状态」？
 *
 *   再说 QButtonGroup。很多人以为「互斥是 QRadioButton 的天性」，
 *   于是把三个单选按钮分别塞进三个 QGroupBox，然后发现它们全都能同时勾上。
 *   天性是 autoExclusive 这个**属性**给的（qabstractbutton.h 36 行），
 *   而它的实现是「去问父控件的兄弟节点」。兄弟都散了，互斥自然就散了。
 *   记住这一条，你就理解了 QButtonGroup 存在的意义：它把互斥关系
 *   从「父子结构」里**摘出来**，交给你显式管理，还顺手给每个按钮配一个 id。
 *
 *   最后：QLabel 里的富文本是个好用但危险的家伙。它是 Qt 自己那套
 *   HTML 的**子集**（不支持 <script>、不支持大部分 CSS），
 *   但新手会顺手把用户输入塞进去——那就是界面注入。
 *   凡是带用户数据的地方，请用 setText + Qt::PlainText，或者先转义。
 *
 * ——— 小练习 ———
 *   1. 把证据① 里 `check->click()` 换成 `check->toggle()`（qabstractbutton.h 86 行），
 *      再换成 `check->setDown(true)`，把三组的 pressed/released/clicked/toggled
 *      计数写回本文件注释——亲手确认 toggle 与 setDown 各发了什么。
 *   2. 给 QCommandLinkButton 也 wireCounter 一份计数器，然后用 setCheckable(true)
 *      的按钮配合 QButtonGroup（把 checkbox、radiobutton、commandlinkbutton 全塞进同一个组），
 *      打印 group->buttons().size() 与 checkedId()，验证「QButtonGroup 只认 QAbstractButton*」。
 *   3. 把 labRich 的 setTextFormat(Qt::RichText) 改成 Qt::AutoText，
 *      再把文字里的 <b> 换成用户输入的字符串 "<b>你好</b>"，
 *      观察它**照样**渲染成粗体；然后改成 Qt::PlainText 看它原样显示。
 *      由此写一篇三行笔记：什么情况下这会变成安全漏洞。
 */
// ============================================================================
