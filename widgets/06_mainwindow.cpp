// ============================================================================
// 第 6 讲 · QMainWindow：把一个「真应用的窗口」搭起来
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   前五讲你用的都是裸 QWidget + QVBoxLayout，那是「一块面板」。真正的应用窗口
//   是 QMainWindow：它把窗口切成**五块保留区域**，而且这五块的位置是它自己管的。
//   本讲讲清七件事：
//     1) 五块区域怎么放东西：setCentralWidget / menuBar() / addToolBar() /
//        statusBar() / addDockWidget()，以及它们各自的「懒创建」语义
//        （menuBar()、statusBar() 是「问到才建」，第一次调用才真的建出来）；
//     2) **硬规则：QMainWindow 不能 setLayout()**——它的布局槽位被内部的
//        QMainWindowLayout 占着，你 setLayout 只会被拒（本讲用 layout()->metaObject()
//        把「谁占着这个位置」打印出来给你看）；
//     3) QAction 才是窗口的「动作单元」：同一个 QAction 同时喂菜单、工具栏、快捷键、
//        状态栏提示。对比 addAction 挂在 QWidget / QMenu / QToolButton 上的三种差别；
//     4) QActionGroup 互斥（Qt6 里它在 **QtGui**）+ QKeySequence 标准键（跨平台拿到
//        Ctrl/Cmd 的正确写法）+ Qt::ShortcutContext 四档上下文；
//     5) 状态栏：showMessage 的 **timeout 语义**（0 = 常驻直到 clearMessage）
//        + addPermanentWidget 放进度条（右侧、不受临时消息挤压）；
//     6) 会话恢复：saveState(version) / restoreState(byteArray, version) 存的是
//        「工具栏和 dock 的位置与尺寸」，用 QSettings 落盘；
//        顺序铁律：**在 closeEvent 里存、在 show() 之后取**（原因见正文）；
//     7) QDockWidget 的 features / allowedAreas / toggleViewAction（把「显示/隐藏」
//        交给 View 菜单，这是免费功能，别自己写）。
//
// 对应构建目标名：widgets_06_mainwindow
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(widgets_06_mainwindow 06_mainwindow.cpp)
//   target_link_libraries(widgets_06_mainwindow PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)   # 本文件里定义了 Q_OBJECT 类 MainWindow，
//                           # 所以**必须**在文件末尾 #include "06_mainwindow.moc"
//
// 运行前须知：
//   · 依赖 Qt6::Widgets。Windows 上要能找到 platforms/qwindows.dll 或 qoffscreen.dll。
//   · 本机当前无法编译链接 Qt 运行时，本示例**未在本机实测编译**；文中每一个类、
//     函数、重载、枚举值都对着 C:\msys64\ucrt64\include\qt6 下的真实头文件逐个核对，
//     行号列在下面「核对签名」一节。核对不到的 API 一律没写。
//   · 全程无人值守：QTimer::singleShot 依次 trigger action / showMessage /
//     toggleViewAction()->trigger() / saveState / restoreState / close()，
//     关键结论用 qInfo() 打印，最后 QCoreApplication::quit()。
//   · 【铁律】主路径**没有任何阻塞调用**：不调 QMenu::exec()（qmenu.h 第 146 行，
//     同事第 5 讲已核对）、不调 QMessageBox 静态方法、不调 QFileDialog 静态方法、
//     也不要在槽里套 exec()。弹出式菜单只演示「挂上去」，不演示「弹出来」。
//   · Qt5 → Qt6 差异（本讲涉及，均已核对本机头文件）：
//       - QAction 与 QActionGroup 在 Qt6 声明于 **QtGui**：
//         QtGui/qaction.h 第 30 行 `class Q_GUI_EXPORT QAction : public QObject`、
//         QtGui/qactiongroup.h 第 17 行 `class Q_GUI_EXPORT QActionGroup : public QObject`；
//         QtWidgets/qaction.h、QtWidgets/qactiongroup.h 只是转发头（后者整份文件里
//         除了 include guard 就只有 `#include <QtGui/QActionGroup>` 一行，我已 cat 过）。
//         → Qt5 时代它们是 Q_WIDGETS_EXPORT，只连 Qt6::Gui 就能用 QAction。
//       - QWidget::addAction(QAction*) 在 Qt6 **依然存在**：
//         QtWidgets/qwidget.h 第 562 行 `void addAction(QAction *action);`，
//         第 563~566 行还有 addActions / insertActions / insertAction / removeAction。
//         第 570~571 行另有 `QAction *addAction(const QString &text)` 与
//         `QAction *addAction(const QIcon&, const QString&)`——**这两个是「新建一个
//         QAction 并挂上来」，返回新对象**，和后述 QMenu/QToolBar 的同名函数语义一致，
//         别和 562 行的「挂已有 action」搞混（本讲用指针打印区分）。
//       - QActionGroup 新增 `enum class ExclusionPolicy { None, Exclusive,
//         ExclusiveOptional }`（qactiongroup.h 第 27~33 行）与 setExclusionPolicy；
//         老的 `setExclusive(bool)`（第 54 行）**仍然保留**，两者语义重叠，
//         新代码建议用 ExclusionPolicy，老代码继续用 setExclusive 也没坏。
//       - QMenu::addMenu(QMenu*) 返回 **QAction***（qmenu.h 第 114 行），而
//         QMenuBar::addMenu(QMenu*) 返回 **void**（qmenubar.h 第 37 行）、
//         QMenuBar::addMenu(const QString&) 才返回 QMenu*（第 38 行）。Qt5 也是这样，
//         但新手在这里十有八九写错，所以本讲两处都用对了并加了打印。
//       - QStatusBar::currentMessage()（qstatusbar.h 第 36 行）与 messageChanged
//         信号（第 43 行）是 Qt 5.14 之后才有的，Qt5 老版本请只留 showMessage/clearMessage。
// ============================================================================

#include <QApplication>           // QtWidgets/qapplication.h
#include <QCoreApplication>       // QtCore/qcoreapplication.h：static void quit()
#include <QMainWindow>            // QtWidgets/qmainwindow.h
#include <QWidget>                // QtWidgets/qwidget.h
#include <QMenuBar>               // QtWidgets/qmenubar.h
#include <QMenu>                  // QtWidgets/qmenu.h
#include <QToolBar>               // QtWidgets/qtoolbar.h
#include <QToolButton>            // QtWidgets/qtoolbutton.h
#include <QStatusBar>             // QtWidgets/qstatusbar.h
#include <QDockWidget>            // QtWidgets/qdockwidget.h
#include <QAction>                // ⚠ Qt6 实为 QtGui/qaction.h（QtWidgets 的是转发头）
#include <QActionGroup>           // ⚠ Qt6 实为 QtGui/qactiongroup.h
#include <QKeySequence>           // QtGui/qkeysequence.h
#include <QLabel>                 // QtWidgets/qlabel.h
#include <QLineEdit>              // QtWidgets/qlineedit.h
#include <QListWidget>            // QtWidgets/qlistwidget.h（第 4 讲的主角，这里当 dock 内容）
#include <QProgressBar>           // QtWidgets/qprogressbar.h
#include <QLayout>                // QtWidgets/qlayout.h：证明「布局槽位被谁占了」
#include <QCloseEvent>            // QtGui/qevent.h 第 565 行：closeEvent 的参数类型
#include <QSettings>              // QtCore/qsettings.h：会话恢复的落盘容器
#include <QDir>                   // QtCore/qdir.h：把 ini 写到临时目录，别脏了用户配置
#include <QTimer>                 // QtCore/qtimer.h
#include <QDebug>                 // QtCore/qdebug.h
#include <QString>

// ============================================================================
// 一、核对签名（本讲全部依据，路径相对 C:\msys64\ucrt64\include\qt6）
// ============================================================================
// QtWidgets/qmainwindow.h
//   第 25 行   class Q_WIDGETS_EXPORT QMainWindow : public QWidget
//   第 64/67 行 void setIconSize(const QSize&); void setToolButtonStyle(Qt::ToolButtonStyle);
//   第 86/87 行 void setDockOptions(DockOptions); DockOptions dockOptions() const;
//   第 48~58 行 enum DockOption { AnimatedDocks=0x01, AllowNestedDocks=0x02,
//                AllowTabbedDocks=0x04, ForceTabbedDocks=0x08, VerticalTabs=0x10,
//                GroupedDragging=0x20 }（Q_DECLARE_FLAGS → DockOptions）
//   第 92/93 行 QMenuBar *menuBar() const;  void setMenuBar(QMenuBar *menubar);
//   第 96 行   void setMenuWidget(QWidget *menubar);   ← 想彻底换掉菜单栏才用它
//   第 100 行  QStatusBar *statusBar() const;          ← 同样是「问到才建」
//   第 104/105/107 行 QWidget *centralWidget() const; void setCentralWidget(QWidget*);
//                QWidget *takeCentralWidget();          ← take 只交出指针，不删对象
//   第 110/111 行 void setCorner(Qt::Corner, Qt::DockWidgetArea);
//                Qt::DockWidgetArea corner(Qt::Corner) const;
//   第 116~118 行 void addToolBar(Qt::ToolBarArea, QToolBar*); void addToolBar(QToolBar*);
//                QToolBar *addToolBar(const QString &title);   ← 三重载，返回值都不一样！
//   第 119/120 行 void insertToolBar(QToolBar *before, QToolBar*); void removeToolBar(QToolBar*);
//   第 114/115 行 void addToolBarBreak(Qt::ToolBarArea = Qt::TopToolBarArea);
//   第 127/128 行 Qt::ToolBarArea toolBarArea(const QToolBar*) const; bool toolBarBreak(QToolBar*) const;
//   第 132/133 行 void addDockWidget(Qt::DockWidgetArea, QDockWidget*);
//                void addDockWidget(Qt::DockWidgetArea, QDockWidget*, Qt::Orientation);
//   第 135/136/140/142 行 splitDockWidget / tabifyDockWidget / removeDockWidget /
//                restoreDockWidget(QDockWidget*) → bool
//   第 147/148 行 void resizeDocks(const QList<QDockWidget*>&, const QList<int>&,
//                Qt::Orientation);   ← 想「命令式」给 dock 定宽，只有这条路
//   第 150 行   QByteArray saveState(int version = 0) const;
//   第 151 行   bool restoreState(const QByteArray &state, int version = 0);
//   第 154 行   virtual QMenu *createPopupMenu();        ← 工具栏/dock 的右键菜单，可覆写
//   第 160 行   void setDockNestingEnabled(bool);
//   ※ 这个类里**没有** setLayout 的任何替代品，也没把 layout() 空出来——见「证据 ②」
//
// QtWidgets/qmenubar.h        31 using QWidget::addAction; / 33 QAction *addAction(const QString&)
//                            37 void addMenu(QMenu *menu); / 38 QMenu *addMenu(const QString &title);
//                            42 QAction *addSeparator(); / 49 QAction *activeAction() const;
//                            70 void setNativeMenuBar(bool);（macOS 原生菜单条，Windows 无效果）
// QtWidgets/qmenu.h          43 using QWidget::addAction; / 45 QAction *addAction(const QString&)
//                            114 QAction *addMenu(QMenu *menu); / 123 insertAction / 145 popup(QPoint,...)
//                            146 int exec() ← **阻塞**，本资料库禁止出现在示例主路径
// QtWidgets/qtoolbar.h       43 setMovable / 55 clear / 57 using QWidget::addAction;
//                            59 QAction *addAction(const QString&); / 60 (QIcon,QString)
//                            66 QAction *addSeparator(); / 69 QAction *addWidget(QWidget*);
//                            70 QAction *insertWidget(QAction *before, QWidget*);
//                            84 setFloatable / 88 setIconSize / 89 setToolButtonStyle
//                            ※ 注意：addAction/addWidget 一律**返回 QAction***，
//                              工具栏里的每个「控件」其实都被包成一个 QAction
// QtWidgets/qdockwidget.h    20 class QDockWidget : public QWidget
//                            38/39 QWidget *widget() const; void setWidget(QWidget*);
//                            41~52 enum DockWidgetFeature { DockWidgetClosable=0x01,
//                                DockWidgetMovable=0x02, DockWidgetFloatable=0x04,
//                                DockWidgetVerticalTitleBar=0x08,
//                                DockWidgetFeatureMask=0x0f, NoDockWidgetFeatures=0x00 }
//                            55/56 setFeatures / features;  58 setFloating;
//                            61 setAllowedAreas(Qt::DockWidgetAreas);  64 setTitleBarWidget
//                            78 QAction *toggleViewAction() const;   ← 免费给你的 View 菜单
//                            82 Q_SIGNAL featuresChanged(...);  90 closeEvent override
// QtWidgets/qstatusbar.h     27 void addWidget(QWidget*, int stretch = 0);
//                            28 int insertWidget(int index, QWidget*, int stretch = 0);
//                            29 void addPermanentWidget(QWidget*, int stretch = 0);
//                            30 int insertPermanentWidget(...);  31 void removeWidget(QWidget*);
//                            33/34 setSizeGripEnabled / isSizeGripEnabled
//                            36 QString currentMessage() const;
//                            39 void showMessage(const QString &text, int timeout = 0);
//                            40 void clearMessage();
//                            43 Q_SIGNAL void messageChanged(const QString &text);
// QtGui/qaction.h            125 setText / 128 setIconText / 147 setShortcut(const QKeySequence&)
//                            148 shortcut() / 150 setShortcuts(const QList<QKeySequence>&)
//                            151 setShortcuts(QKeySequence::StandardKey)  ← 直接给标准键
//                            154/155 setShortcutContext / shortcutContext（**在 QAction 上**，
//                                QWidget 侧只有 grabShortcut(key, context)，见 qwidget.h 453 行）
//                            164 setCheckable / 170 isChecked / 167/168 data/setData(QVariant)
//                            120 setActionGroup / 184 setMenu(QMenu*) / 143 setSeparator(bool)
//                            179 setMenuRole（macOS 才管用）/ 157 setAutoRepeat
//                            210 public Q_SLOTS: trigger() { activate(Trigger); } / toggle()
//                                / setChecked / setVisible / setEnabled
//                            220 Q_SIGNALS: changed() / enabledChanged(bool) / checkableChanged
//                                / visibleChanged / triggered(bool checked = false)
//                                / hovered() / toggled(bool)
// QtGui/qactiongroup.h       17 class Q_GUI_EXPORT QActionGroup : public QObject
//                            32 explicit QActionGroup(QObject *parent);   ← ⚠ parent 无默认值！
//                            37 QAction *addAction(QAction*); / 38 addAction(const QString&)
//                            40 removeAction / 41 QList<QAction*> actions() const
//                            42 QAction *checkedAction() const; / 44 isExclusive()
//                            54 void setExclusive(bool); / 56 setExclusionPolicy(ExclusionPolicy)
//                            27~31 enum class ExclusionPolicy { None, Exclusive, ExclusiveOptional }
//                            57 Q_SIGNALS: triggered(QAction*); hovered(QAction*);
// QtGui/qkeysequence.h       40~110 enum StandardKey（完整见下面「证据 ④」的清单）
//                            121 QKeySequence(const QString &key, SequenceFormat = NativeText);
//                            129 QKeySequence(StandardKey key);
//                            132 int count() const;                  ← 组合数（0~4），不是「长度」
//                            135~139 enum SequenceMatch { NoMatch, PartialMatch, ExactMatch };
//                            142 QString toString(SequenceFormat format = PortableText) const;
//                            143 static QKeySequence fromString(const QString&, SequenceFormat = PortableText);
//                            150 static QList<QKeySequence> keyBindings(StandardKey key);
//                            115~118 enum SequenceFormat { NativeText, PortableText };
//                                ※ Qt5 的 ISOText 在 Qt6 已无（本机枚举里只剩两项）
// QtCore/qnamespace.h        1322~1327 enum ShortcutContext { WidgetShortcut, WindowShortcut,
//                                ApplicationShortcut, WidgetWithChildrenShortcut };
//                            1226~1235 enum DockWidgetArea { LeftDockWidgetArea=0x1,
//                                RightDockWidgetArea=0x2, TopDockWidgetArea=0x4,
//                                BottomDockWidgetArea=0x8, AllDockWidgetAreas=0xf, NoDockWidgetArea=0 }
//                            1243~1252 enum ToolBarArea（同位值，Left/Right/Top/Bottom…）
//                            1296~1301 enum Corner { TopLeftCorner, TopRightCorner,
//                                BottomLeftCorner, BottomRightCorner }
// QtCore/qsettings.h         100 QSettings(const QString &fileName, Format format,
//                                QObject *parent = nullptr);
//                            50/51 enum Format { NativeFormat = 0, IniFormat = 1 }
//                            40~42 enum Status { NoError = 0, AccessError, ... }
//                            116 void sync(); / 117 Status status() const; / 161 QString fileName() const;
//                            122/125 beginGroup(prefix) / endGroup();
//                            143 void setValue(const QString &key, const QVariant &value);
//                            144 QVariant value(const QString &key, const QVariant &defaultValue) const;
// QtWidgets/qwidget.h        540/541 QLayout *layout() const; void setLayout(QLayout*);
//                            562 void addAction(QAction *action);
//                            563~566 addActions / insertActions / insertAction / removeAction
//                            570/571 QAction *addAction(const QString&) / (const QIcon&, const QString&)
//                            453 int grabShortcut(const QKeySequence&,
//                                Qt::ShortcutContext context = Qt::WindowShortcut);  ← 底层原语

// ============================================================================
// 二、主窗口类：唯一需要覆写的就是 closeEvent（在「几何谈定」的那一刻存状态）
// ============================================================================
class MainWindow : public QMainWindow
{
    Q_OBJECT          // 给 moc 的标记：本文件末尾必须 #include "06_mainwindow.moc"

public:
    explicit MainWindow(QWidget *parent = nullptr) : QMainWindow(parent) {}

    QString sessionFilePath() const { return m_sessionFilePath; }
    void setSessionFilePath(const QString &p) { m_sessionFilePath = p; }

    // 【为什么要覆写 closeEvent】
    //   saveState() 存的值是「工具栏与 dock 谈判完之后的最终几何」。
    //   如果你在别的地方（比如某个按钮的槽里、或者刚 addDockWidget 完）就调它，
    //   读到的可能是布局还没跑过一轮时的**草稿**。
    //   closeEvent 恰好是「用户/程序决定关闭」的那一刻，此刻窗口是稳定态，
    //   而且 Qt 明确允许你在 closeEvent 里做点收尾工作（记得 event->accept() 或
    //   ignore()，别把它吞了导致窗口关不掉）。
    //   核对签名：QtWidgets/qwidget.h 第 677 行 `virtual void closeEvent(QCloseEvent *event);`
    void closeEvent(QCloseEvent *event) override
    {
        if (!m_sessionFilePath.isEmpty()) {
            QSettings s(m_sessionFilePath, QSettings::IniFormat);   // qsettings.h 100 行
            s.setValue(QStringLiteral("ui/state"), saveState(1));  // 150 行，version 自己给 1
            s.setValue(QStringLiteral("ui/geometry"), saveGeometry()); // qwidget.h 507 行
            s.sync();                                               // 116 行：立刻落盘
            qInfo() << "⑦ closeEvent：已 saveState(version=1)"
                    << "字节数 =" << saveState(1).size()
                    << "写入 =" << s.fileName()
                    << "status() =" << int(s.status());            // 41 行 NoError = 0
        }
        event->accept();     // QCloseEvent 的 accept/ignore（QtGui/qevent.h 第 565 行起）
    }

private:
    // 命名纪律：数据成员一律 m_ 前缀，对外只有上面那对访问器
    // sessionFilePath() / setSessionFilePath()，全类**就这一个名字**，不要再起别名。
    // closeEvent 里读的也是它，不存在「 getter 和成员两套路径」的分叉。
    QString m_sessionFilePath;
};

// ============================================================================
// 三、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    auto *win = new MainWindow;
    win->setObjectName(QStringLiteral("MainWindow"));        // QtCore/qobject.h 127 行
    win->setWindowTitle(QStringLiteral("第 6 讲 · QMainWindow 的五块区域"));
    win->resize(1000, 620);

    // ── 证据 ① 五块区域：先把骨架搭出来 ──────────────────────────────
    auto *center = new QLabel(QStringLiteral("中央区域（central widget）\n"
                                             "只放**一个**控件；要放一堆，请给它自己 setLayout。"));
    center->setAlignment(Qt::AlignCenter);
    win->setCentralWidget(center);                    // qmainwindow.h 105 行
    qInfo() << "① centralWidget() 指回同一个对象吗 =" << (win->centralWidget() == center);

    // 换掉中心控件会怎样？——旧的中心控件**不会被删除**，只是从布局里摘出来，
    // parent 还挂在 QMainWindow 上，于是变成一只「看不见的孤儿控件」。
    // 官方给的出口是 takeCentralWidget()（107 行）：它交出指针、把所有权还给你。
    auto *alt = new QLabel(QStringLiteral("临时中心控件"));
    win->setCentralWidget(alt);                     // 此刻 center 被摘下来，但没被 delete
    qInfo() << "① 换过中心控件之后：centralWidget() 就是 alt 吗 =" << (win->centralWidget() == alt)
            << "；被摘下的 center 还活着 =" << (center != nullptr)
            << "，它的 parentWidget 还在 =" << (center->parentWidget() != nullptr)
            << "→「换中心控件不删旧的」：不接管就是泄漏，请自己 deleteLater";
    QWidget *taken = win->takeCentralWidget();       // 107 行：交出指针
    qInfo() << "① takeCentralWidget() 之后 centralWidget() 为空 =" << (win->centralWidget() == nullptr)
            << "；取回的正是 alt 吗 =" << (taken == alt) << "→ 所有权现在彻底在你手上";
    delete alt;                                      // 明确销毁，不留孤儿
    win->setCentralWidget(center);                   // 把真正要用的那块放回去
    center->setText(QStringLiteral("中央区域（central widget）\n"
                                    "只放**一个**控件；要放一堆，请给它自己 setLayout。"));

    // 菜单条 / 状态栏：**懒创建**。menuBar() 第一次调用时才 new 一个 QMenuBar 并挂上去。
    QMenuBar *mb = win->menuBar();                    // 92 行 const QMenuBar*，这里拿到非 const 用法
    qInfo() << "① menuBar() 懒创建后非空 =" << (mb != nullptr)
            << "；menuBar()->objectName() =" << mb->objectName()
            << "（空串是正常的：objectName 不是必需的，但第 9 讲 QSS 要用 ID 选择器时就得设）";

    QStatusBar *sb = win->statusBar();                // 100 行
    qInfo() << "① statusBar() 懒创建后非空 =" << (sb != nullptr) << "（不调它就一直不存在）";

    // ── 证据 ② 硬规则：QMainWindow 不能 setLayout ────────────────────
    // QMainWindow 的「五块区域 + 分隔条」本身就是一张布局表，Qt 内部用的是
    // QMainWindowLayout（未在公开头文件里暴露，但 layout() 会把它交出来）。
    // 而 QWidget::setLayout（qwidget.h 541 行）的实现是：
    //   「若此 widget 已经有 layout → 打一条 qWarning 并直接 return，不接管你传进来的对象」。
    // 所以对 QMainWindow 调 setLayout 只有两个后果：被拒绝 + 你 new 的 layout 泄漏。
    // 【本讲**故意不去调它**】：无人值守跑起来没必要制造一条警告和一个泄漏；
    // 第 7 讲会在**普通 QWidget** 上把这条坑完整地撞一次并打印警告，那边是正解。
    QLayout *occupied = win->layout();
    qInfo() << "② win->layout() 现在被谁占着 ="
            << (occupied ? QString::fromUtf8(occupied->metaObject()->className())
                         : QStringLiteral("(空)"))
            << "→ 这就是「不许 setLayout」的实证：槽位已经不是空的，"
               "而 QMainWindow 的中央区域只认 setCentralWidget()。";

    // ── 证据 ③ QAction：一个对象，四处复用 ──────────────────────────
    // 【心法】菜单项、工具栏按钮、快捷键、状态栏提示，在 Qt 里都应该是**同一个 QAction**。
    // 这样「禁用一个功能」只需 setEnabled(false)，四处一起灰掉；
    // 反过来，给菜单和工具栏各写一套「看起来一样」的代码，就一定会飘。
    QAction *actNew  = new QAction(QStringLiteral("新建(&N)"), win);
    actNew->setShortcut(QKeySequence::New);                       // 129 行的 StandardKey 隐式构造
    actNew->setStatusTip(QStringLiteral("新建一个文档（Ctrl+N）"));
    QObject::connect(actNew, &QAction::triggered, [sb](bool checked) {
        // 224 行 triggered(bool checked = false)：非 checkable 时永远是 false
        sb->showMessage(QStringLiteral("triggered：新建。checked = %1").arg(checked ? 1 : 0),
                        2500);                                    // qstatusbar.h 39 行 timeout=2500ms
    });

    QAction *actOpen = new QAction(QStringLiteral("打开(&O)…"), win);
    actOpen->setShortcut(QKeySequence::Open);
    // 【注意】这里**绝不能**写 QFileDialog::getOpenFileName()：它是静态便利函数，
    // 内部起模态循环、必须有人点「打开」才返回 → 无人值守必然卡到 ctest 超时。
    // 正解见第 5 讲：new QFileDialog → open()/show() + QTimer 自动 selectFile + accept()。
    QObject::connect(actOpen, &QAction::triggered, [sb] {
        sb->showMessage(QStringLiteral("triggered：打开（本演示**不弹**文件对话框，避免阻塞）"), 2500);
    });

    QAction *actSave = new QAction(QStringLiteral("保存(&S)"), win);
    actSave->setShortcuts(QKeySequence::Save);        // 151 行：直接吃 StandardKey，装成「一组」快捷键
    QObject::connect(actSave, &QAction::triggered, [win] {
        QByteArray st = win->saveState(1);                        // 150 行
        qInfo() << "⑥ 手动触发的「保存」里顺手 saveState(1)：字节数 =" << st.size();
    });

    QAction *actQuit = new QAction(QStringLiteral("退出(&Q)"), win);
    actQuit->setShortcut(QKeySequence::Quit);         // 标准键清单里确实有 Quit（103 行左右）
    // 【为什么不连 quit】连了 QCoreApplication::quit 就得有人点它才退出，
    // 本讲退出由末尾的 QTimer 兜底，这里只把「菜单项能触发」当证据。
    actQuit->setData(QStringLiteral("builtin"));      // 167/168 行：给 action 挂用户数据，
    qInfo() << "③ actQuit->data() 读回 =" << actQuit->data().toString()
            << "（配合 sender() 或 QActionGroup::triggered(QAction*) 就能认出是哪一项）";

    // actCopy：一个「已经存在」的 QAction，专门用来给证据 ⑦ 做对照
    // （QWidget::addAction(QAction*) 复用它；QMenu::addAction(QString) 每次新建一个）。
    QAction *actCopy = new QAction(QStringLiteral("复制(&C)"), win);
    actCopy->setShortcut(QKeySequence::Copy);        // 标准键 Copy 确实存在（40~110 行清单里）
    actCopy->setCheckable(false);

    QMenu *fileMenu = win->menuBar()->addMenu(QStringLiteral("文件(&F)"));  // qmenubar.h 38 行 → QMenu*
    fileMenu->addAction(actNew);                                     // qmenu.h：走 QWidget::addAction(QAction*) 路径
    fileMenu->addAction(actOpen);
    fileMenu->addAction(actSave);
    fileMenu->addSeparator();                                        // qmenubar/qmenu 都有（menu 用 inherited 版）
    fileMenu->addAction(actQuit);

    // ── 证据 ④ QKeySequence 标准键：跨平台唯一正确姿势 ───────────────
    // 【为什么必须用标准键】Mac 上「保存」是 Cmd+S、Windows/Linux 是 Ctrl+S；
    // 你自己硬写 "Ctrl+S"，在 Mac 上就是错的。标准键由 Qt 按平台翻译成对的组合，
    // 而且**用户改了系统键位**（keyBindings 返回列表）时也跟得上。
    // 本机头文件（qkeysequence.h 40~110 行）里**存在**的、本讲敢用的项：
    //   New / Open / Save / SaveAs / Close / Print / Quit / Undo / Redo / Cut / Copy /
    //   Paste / Delete / Backspace / SelectAll / Deselect / Find / FindNext /
    //   FindPrevious / Replace / Refresh / ZoomIn / ZoomOut / FullScreen / Preferences /
    //   HelpContents / WhatsThis / AddTab / NextChild / PreviousChild / Bold / Italic /
    //   Underline / DeleteCompleteLine / Cancel / Back / Forward / 以及一堆 MoveTo*/Select*
    // ⚠ **没有** QKeySequence::About（网上不少教程写了它，本机枚举里没有，别照抄）。
    const QList<QKeySequence> probes = {
        QKeySequence(QKeySequence::New),   QKeySequence(QKeySequence::Open),
        QKeySequence(QKeySequence::Save),  QKeySequence(QKeySequence::Copy),
        QKeySequence(QKeySequence::Paste), QKeySequence(QKeySequence::Undo),
        QKeySequence(QKeySequence::Find),  QKeySequence(QKeySequence::SaveAs),
        QKeySequence(QKeySequence::Print), QKeySequence(QKeySequence::Quit),
    };
    for (const QKeySequence &ks : probes) {
        // toString 默认 **PortableText**（142 行），跨平台可存回 fromString（143 行）
        qInfo() << "④ 标准键 count =" << ks.count()
                << "PortableText =" << ks.toString(QKeySequence::PortableText)
                << "NativeText =" << ks.toString(QKeySequence::NativeText)
                << "（NativeText 才是给用户看的：Mac 上会显示成 ⌘ 那一套）";
    }
    qInfo() << "④ keyBindings(Save) 返回的备选键有几个 ="
            << QKeySequence::keyBindings(QKeySequence::Save).size()
            << "→ 所以 setShortcuts(StandardKey) 比 setShortcut 更稳（它把一组都装上）";
    qInfo() << "④ matches(): Copy 对 Copy ="
            << int(QKeySequence(QKeySequence::Copy).matches(QKeySequence(QKeySequence::Copy)))
            << "（0=NoMatch / 1=PartialMatch / 2=ExactMatch，见 135~139 行）";
    qInfo() << "③ 三条 action 的 shortcut 文本：New =" << actNew->shortcut().toString()
            << " Open =" << actOpen->shortcut().toString()
            << " Quit =" << actQuit->shortcut().toString();

    // ── 证据 ⑤ 快捷键上下文 Qt::ShortcutContext（四档）───────────────
    // qnamespace.h 1322~1327 行的四档，记住这一句就够：
    //   WidgetShortcut               只有这个**控件本身**有焦点时才生效
    //   WindowShortcut（默认）        整个顶层窗口内有焦点就生效；**同一个窗口里同键只能活一个**
    //   ApplicationShortcut          全应用范围，谁都抢不过它（慎用，最容易「快捷键打架」）
    //   WidgetWithChildrenShortcut   这个控件**或它的子控件**有焦点时生效（dock 内常用）
    actSave->setShortcutContext(Qt::WindowShortcut);                  // 154 行，默认就是这个
    auto *quickEdit = new QLineEdit(win);
    quickEdit->setPlaceholderText(QStringLiteral("在这儿打字：Ctrl+S 仍归窗口管"));
    QAction *actDockKey = new QAction(QStringLiteral("dock 内专用键"), win);
    actDockKey->setShortcut(QKeySequence(QStringLiteral("Ctrl+D"))); // 121 行：从字符串构造
    actDockKey->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    qInfo() << "⑤ actSave 上下文 =" << int(actSave->shortcutContext())
            << "(1=WindowShortcut)；actDockKey 上下文 =" << int(actDockKey->shortcutContext())
            << "(3=WidgetWithChildrenShortcut)；枚举顺序见 qnamespace.h 1323~1326 行。"
               "【坑】同一个窗口里两个 action 抢同一个键 → 只有**一个**会触发，"
               "而且触发哪个取决于它们被加入的顺序；查这类 bug 先打 shortcutContext()。";
    (void)quickEdit;

    // ── 证据 ⑥ 工具栏：addToolBar 三个重载 + 每个控件都被包成 QAction ──
    QToolBar *tb = win->addToolBar(QStringLiteral("主工具栏"));   // 118 行：**返回新建的 QToolBar**
    tb->setObjectName(QStringLiteral("mainToolBar"));             // 存状态/QSS ID 选择器都要它
    tb->setMovable(true);                                        // qtoolbar.h 43 行（不可拖动就没法重排）
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);         // 89 行
    tb->addAction(actNew);                                       // 走 QWidget::addAction(QAction*)：复用同一个对象
    tb->addAction(actOpen);
    tb->addAction(actSave);
    tb->addSeparator();                                          // 66 行 → QAction*（分隔线也是个 QAction！）
    QAction *wrapEdit = tb->addWidget(quickEdit);                // 69 行 → 返回**包裹用的 QAction**
    qInfo() << "⑥ tb->actions().size() =" << tb->actions().size()
            << "；addWidget 返回的 QAction 的 menu() =" << wrapEdit->menu()
            << " text =" << wrapEdit->text().isEmpty() << "（空是正常的：它是包装用的）";
    // 工具区位置：addToolBar(QToolBar*) 不带区域参数 = 放顶部；要放左侧必须用
    // addToolBar(Qt::LeftToolBarArea, tb)（116 行），事后可以用 toolBarArea() 读回来。
    win->addToolBarBreak(Qt::TopToolBarArea);                    // 114 行：另起一行工具栏
    QToolBar *tb2 = new QToolBar(win);
    tb2->setObjectName(QStringLiteral("viewToolBar"));
    win->addToolBar(Qt::TopToolBarArea, tb2);                     // 116 行
    qInfo() << "⑥ tb2 落在哪个区 =" << int(win->toolBarArea(tb2))
            << "(4=Qt::TopToolBarArea，见 qnamespace.h 1243~1252 行)";

    // QToolButton：它才是「工具栏上的按钮」这个概念本身
    auto *tbtn = new QToolButton(tb2);
    tbtn->setDefaultAction(actNew);                    // qtoolbutton.h 67 行：整份「继承」action 的文字/图标/快捷键
    qInfo() << "⑥ tbtn->defaultAction()->text() =" << tbtn->defaultAction()->text()
            << "；**setDefaultAction 之后按钮自己就有快捷键了**，别再给按钮单设一遍";
    tbtn->addAction(actQuit);                          // ⚠ 这版是 qwidget.h 562 行的 **void** addAction(QAction*)
    qInfo() << "⑥ QToolButton 上 addAction(QAction*) 之后：tbtn->actions().size() ="
            << tbtn->actions().size() << "；返回类型是 void（562 行），"
               "所以加进去的还是我们自己那个 actQuit 指针，**不是新建的**；"
               "而 QToolBar::addAction(QString)（qtoolbar.h 59 行）返回的是新建的 QAction*";

    // ── 证据 ⑦ QWidget / QMenu / QToolButton 三种 addAction 的语义差 ───
    // 一句话：**挂已有 QAction** 的那版（qwidget.h 562）只登记，不新建；
    //           **给字符串** 的那版（570 行 / qmenu.h 45 行 / qtoolbar.h 59 行）**新建并返回** QAction*。
    // 对普通 QWidget：加进 actions() 列表，作用是**让它持有的快捷键与
    // contextMenuPolicy 生效**，界面上什么都不会画出来；
    // 对 QMenu / QMenuBar：画成菜单项；对 QToolBar：画成工具按钮；
    // 对 QToolButton：加进它的动作列表（配合 popupMode 才会画成分裂按钮）。
    QWidget *holder = new QWidget(win);
    holder->setObjectName(QStringLiteral("invisibleHolder"));
    holder->addAction(actCopy);                     // QWidget::addAction(QAction*)，qwidget.h 562 行
    qInfo() << "⑦ holder->actions().size() =" << holder->actions().size()
            << " holder 上**画不出任何东西**——普通 QWidget 只把它存进动作列表（供快捷键与"
               "CustomContextMenu 使用）；actions() 是 qwidget.h 563~566 行那组接口的读取端";
    QAction *m1 = fileMenu->addAction(QStringLiteral("重复项"));
    QAction *m2 = fileMenu->addAction(QStringLiteral("重复项"));
    qInfo() << "⑦ QMenu::addAction(字符串) 两次拿到的是**不同对象**吗 =" << (m1 != m2)
            << "（每次调用都 new 一个 QAction，parent 是菜单）→ 别在这种返回值上存业务指针";

    // ── 证据 ⑧ QActionGroup：互斥 + 统一信号 ─────────────────────────
    // ⚠ 第 32 行 `explicit QActionGroup(QObject *parent);` —— parent **没有默认值**！
    //   Qt5 时代是 QActionGroup(QObject *parent = 0)。写 `new QActionGroup;` 在 Qt6 编不过。
    auto *styleGroup = new QActionGroup(tb2);
    styleGroup->setExclusive(true);                                   // 54 行（老接口仍在）
    QAction *gIcon = styleGroup->addAction(QStringLiteral("只图标"));  // 38 行：字符串版会新建并加入组
    QAction *gText = styleGroup->addAction(QStringLiteral("只文字"));
    QAction *gBoth = styleGroup->addAction(QStringLiteral("图标+文字"));
    gIcon->setCheckable(true); gText->setCheckable(true); gBoth->setCheckable(true);
    gIcon->setData(int(Qt::ToolButtonIconOnly));
    gText->setData(int(Qt::ToolButtonTextOnly));
    gBoth->setData(int(Qt::ToolButtonTextBesideIcon));
    gBoth->setChecked(true);
    QObject::connect(styleGroup, &QActionGroup::triggered, [tb, styleGroup](QAction *a) {
        tb->setToolButtonStyle(Qt::ToolButtonStyle(a->data().toInt()));
        qInfo() << "⑧ QActionGroup::triggered →" << a->text()
                << "；此刻 checkedAction()->text() =" << styleGroup->checkedAction()->text()
                << "；工具栏 toolButtonStyle 已切成" << int(a->data().toInt())
                << "（**组只给你一个信号**，不用给三个 action 各连一遍——这就是它的价值）";
    });
    // 把这三项挂进 View 菜单，让用户真的能点（本讲靠程序 trigger() 演示）
    QMenu *viewMenu = win->menuBar()->addMenu(QStringLiteral("视图(&V)"));
    viewMenu->addAction(gIcon); viewMenu->addAction(gText); viewMenu->addAction(gBoth);
    viewMenu->addSeparator();

    // ── 证据 ⑨ QDockWidget：两侧各一个，View 菜单白拿三个 toggle ──────
    auto *leftDock = new QDockWidget(QStringLiteral("文档列表"), win);
    leftDock->setObjectName(QStringLiteral("leftDock"));
    auto *list = new QListWidget(leftDock);
    list->addItems(QStringList{QStringLiteral("第一章.md"), QStringLiteral("第二章.md"),
                               QStringLiteral("第三章.md")});   // qlistwidget.h 190 行
    leftDock->setWidget(list);                                        // qdockwidget.h 39 行
    leftDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
                          | QDockWidget::DockWidgetClosable);         // 55 行 + 41~48 行位值
    leftDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea); // 61 行 + qnamespace 1226
    win->addDockWidget(Qt::LeftDockWidgetArea, leftDock);             // 132 行

    auto *rightDock = new QDockWidget(QStringLiteral("属性"), win);
    rightDock->setObjectName(QStringLiteral("rightDock"));
    auto *propLabel = new QLabel(QStringLiteral("属性面板\n（dock 的内容 widget 得自己管自己的布局）"),
                                 rightDock);
    rightDock->setWidget(propLabel);
    win->addDockWidget(Qt::RightDockWidgetArea, rightDock);
    qInfo() << "⑨ leftDock->features() 位值 =" << int(leftDock->features())
            << "（Closable 0x01 | Movable 0x02 | Floatable 0x04 = 0x07，见 41~44 行）"
            << "；rightDock->widget() 的实际类名 ="
            << QString::fromUtf8(rightDock->widget()->metaObject()->className());
    // toggleViewAction()：免费的「显示/隐藏」菜单项，文字自动是「显示 属性」/「隐藏 属性」
    viewMenu->addAction(leftDock->toggleViewAction());                // 78 行 → QAction*
    viewMenu->addAction(rightDock->toggleViewAction());
    qInfo() << "⑨ toggleViewAction 的文字 =" << leftDock->toggleViewAction()->text()
            << "；checkable =" << leftDock->toggleViewAction()->isCheckable()
            << "→ 千万别自己写 QAction 去 setVisible()，那是重复造轮子还容易和状态栏打架";

    // tabify / split：dock 之间要叠成标签页或强制并排，只能走这两个显式接口
    win->tabifyDockWidget(leftDock, rightDock);       // 136 行（需 AllowTabbedDocks，见下）
    win->setDockOptions(QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks
                        | QMainWindow::AnimatedDocks);  // 86 行 + DockOption 位值
    win->setDockNestingEnabled(true);                 // 160 行
    qInfo() << "⑨ tabify 之后 tabifiedDockWidgets(leftDock).size() ="
            << win->tabifiedDockWidgets(leftDock).size()
            << "；dockOptions 位值 =" << int(win->dockOptions());

    // ── 证据 ⑩ 状态栏：showMessage 超时 + addPermanentWidget 进度条 ────
    QObject::connect(sb, &QStatusBar::messageChanged, [](const QString &t) {
        // qstatusbar.h 43 行。**超时后 Qt 会再发一次空串**，这条信号就是超时证据。
        qInfo() << "⑩ messageChanged：" << (t.isEmpty() ? QStringLiteral("(空串 → 临时消息已消失)") : t);
    });
    auto *hint = new QLabel(QStringLiteral("常驻提示：左侧 addWidget，不随临时消息消失"));
    sb->addWidget(hint);                             // 27 行：加在**左边**，会被临时消息挤
    auto *prog = new QProgressBar;
    prog->setRange(0, 100);                          // qprogressbar.h 67 行
    prog->setValue(35);                              // 70 行
    prog->setMaximumWidth(160);
    prog->setTextVisible(false);                     // 45 行
    sb->addPermanentWidget(prog);                    // 29 行：加在**右边**，临时消息碰不到它
    // 核对 qstatusbar.h 第 30 行：int insertPermanentWidget(int index, QWidget *widget, int stretch = 0);
    // ——**第一个参数是插入位置**，不是控件；上一版直接写 insertPermanentWidget(widget, 0)
    // 就是把它当成 addPermanentWidget（29 行，返回 void）用了。此刻常驻区只有 prog 一格，
    // 所以 index 给 1 = 追加到常驻区末尾，返回值就是 Qt 实际放进去的索引。
    qInfo() << "⑩ insertPermanentWidget(1, 新标签, 0) 返回的索引 ="
            << sb->insertPermanentWidget(1, new QLabel(QStringLiteral("第二格常驻")), 0)
            << "（30 行返回 int 索引；addWidget(27 行)/addPermanentWidget(29 行)只返回 void）";

    // ── 证据 ⑪ 会话恢复：saveState/restoreState + QSettings，以及顺序 ──
    const QString iniPath = QDir::tempPath() + QStringLiteral("/widgets06_session.ini");
    win->setSessionFilePath(iniPath);
    QByteArray fresh = win->saveState(1);            // 150 行：此刻 dock 已谈完，存得下来
    qInfo() << "⑪ 第一次 saveState(1) 字节数 =" << fresh.size()
            << "→ 这段 QByteArray 是 Qt 的**流格式**二进制（工具栏/dock 顺序、区域、尺寸），"
               "人类读不懂也别试图解析，交给 QSettings 存原始字节即可";
    {
        QSettings s(iniPath, QSettings::IniFormat);  // qsettings.h 100 行
        s.setValue(QStringLiteral("ui/state"), fresh);
        s.sync();
        qInfo() << "⑪ 写入 ini =" << s.fileName() << "status() =" << int(s.status());
    }
    // 恢复必须在 **show() 之后**：
    //   · restoreState 要按 objectName 找到已存在的 toolbar/dock 再往回摆；
    //     对象还没建或还没进区域时，它只能**返回 false**（151 行返回 bool），且不报错；
    //   · 尺寸是「请求」，show() 之前的布局是草稿，先 restore 再 show 会被二次谈判改掉；
    //   · 正确顺序：建窗口 → setCentralWidget/addToolBar/addDockWidget → show()
    //     → 读 QSettings → restoreState(bytes, version)（version 要和存的时候一致！）
    win->show();                                     // qwidget.h 487 行（第 1 讲讲过：只是挂号）
    qInfo() << "⑪ show() 已调用——**恢复一定放在这里之后**；此刻 win->layout() 仍是 ="
            << (win->layout() ? QString::fromUtf8(win->layout()->metaObject()->className())
                              : QStringLiteral("(空)"));

    // ============================================================================
    // 四、自动脚本（无人值守）
    // ============================================================================
    QTimer::singleShot(200, [win, mb, sb, tb, leftDock] {
        qInfo() << "—— 事件循环已跑过一轮布局，读到的几何才是谈判结果 ——";
        qInfo() << "① win->size() =" << win->size() << " 菜单条高度 =" << mb->height()
                << " 工具栏几何 =" << tb->geometry() << " 状态栏几何 =" << sb->geometry()
                << " leftDock 几何 =" << leftDock->geometry();
    });

    QTimer::singleShot(600, [actNew, actOpen, actSave, sb] {
        // trigger()（qaction.h 210 行）= 在代码里「点一下菜单项」，同步发 triggered。
        actNew->trigger();
        actOpen->trigger();
        qInfo() << "③ trigger() 三个动作之后状态栏临时消息 =" << sb->currentMessage()
                << "（36 行；此刻它还活着，timeout 是 2500ms）";
        (void)actSave;
    });

    QTimer::singleShot(3400, [sb] {
        // 上面 showMessage 的 2500ms 早已过 → 临时消息应已被清空
        qInfo() << "⑩ 3.4s：showMessage 的 timeout 到点后 currentMessage() ="
                << (sb->currentMessage().isEmpty() ? QStringLiteral("(空)✓ 超时语义生效")
                                                   : sb->currentMessage());
        sb->showMessage(QStringLiteral("timeout 传 0 = 常驻，直到 clearMessage() 或被下一条消息顶掉"));
        // 关键语义：showMessage 的第二个参数**默认是 0**，0 表示「不自动消失」。
        // 传正数（毫秒）则排一个内部定时器，到点自动清回「无临时消息」状态。
        // 常驻内容（进度、选中项统计）请放 addWidget/addPermanentWidget 的**控件**，
        // 别用 showMessage 当日志刷。
        sb->showMessage(QStringLiteral("这条会盖住上一条：状态栏永远只显示最近一条临时消息"), 0);
        qInfo() << "⑩ 立刻读回 currentMessage() =" << sb->currentMessage();
    });

    QTimer::singleShot(3800, [sb, prog, hint] {
        prog->setValue(88);
        qInfo() << "⑩ 进度条几何 =" << prog->geometry() << "宽度 =" << prog->width()
                << "（addPermanentWidget：在**右边**，maxWidth 160 说话算数）；"
                   "hint 几何 =" << hint->geometry() << "（addWidget：在左边）";
        sb->clearMessage();                          // 40 行：手动清
        qInfo() << "⑩ clearMessage() 之后 currentMessage() 是否为空 ="
                << sb->currentMessage().isEmpty();
    });

    QTimer::singleShot(4200, [win, leftDock, rightDock, viewMenu] {
        QAction *toggle = leftDock->toggleViewAction();
        qInfo() << "⑨ 关掉左侧 dock：toggle->text() =" << toggle->text()
                << "checked =" << toggle->isChecked();
        toggle->trigger();                            // 免费得到「隐藏/显示」行为
        qInfo() << "⑨ trigger 之后 leftDock->isVisible() =" << leftDock->isVisible()
                << "rightDock->isVisible() =" << rightDock->isVisible()
                << "；View 菜单项数 =" << viewMenu->actions().size();
        toggle->trigger();                            // 再点回来
    });

    QTimer::singleShot(4600, [styleGroup, gIcon, gText, win, tb] {
        gText->trigger();                             // 走 QActionGroup::triggered
        qInfo() << "⑧ 选「只文字」之后 checkedAction()->text() ="
                << styleGroup->checkedAction()->text()
                << "；组内动作数 =" << styleGroup->actions().size()
                << "；工具栏 toolButtonStyle =" << int(win->toolButtonStyle());
        styleGroup->setExclusive(false);
        gIcon->setChecked(true);
        qInfo() << "⑧ setExclusive(false) 后能否同时勾两个 =" << gIcon->isChecked()
                << "/" << gText->isChecked()
                << "；Qt6 更推荐 setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional)"
                   "（qactiongroup.h 27~33 行 enum class，语义比 bool 三档更准：允许「全不选」）";
    });

    QTimer::singleShot(5000, [win, iniPath] {
        // 先「动一下」界面，再验证恢复确实把状态拉回来
        const QByteArray saved = QSettings(iniPath, QSettings::IniFormat)
                                    .value(QStringLiteral("ui/state")).toByteArray(); // 144 行
        qInfo() << "⑪ 从 ini 读回的 state 字节数 =" << saved.size();
        const bool ok = win->restoreState(saved, 1);  // 151 行：**version 必须和存时一致**
        qInfo() << "⑪ restoreState(saved, 1) 返回 =" << ok
                << "（version 不匹配 / 找不到同名 dock 都会返回 false 而**不报错**，"
                   "这是全讲最隐蔽的坑：界面静悄悄回到默认布局）";
        const bool bogus = win->restoreState(saved, 99);
        qInfo() << "⑪ restoreState(saved, 99) 返回 =" << bogus << "← 换个 version 立刻失败，实测见分晓";
    });

    QTimer::singleShot(5400, [win] {
        // 走一次真正的关闭流程 → 触发 MainWindow::closeEvent → 存状态。
        // QWidget::close() 会发 QCloseEvent（不像 quit() 直接退事件循环，
        // quit() **不会**给窗口发 closeEvent——这是「退出时没保存」的经典病根）。
        qInfo() << "⑦ 即将 win->close()：只有 close() 才走 closeEvent，quit() 不走！";
        win->close();
    });

    QTimer::singleShot(5700, [] {
        qInfo() << "\n=== 结论 ===";
        qInfo() << "① 五块区域各有专属 API；menuBar()/statusBar() 是懒创建，central 只认 setCentralWidget/takeCentralWidget。";
        qInfo() << "② QMainWindow 的 layout() 槽位已被内部布局占用 → setLayout 会被拒（第 7 讲在普通 QWidget 上撞给你看）。";
        qInfo() << "③④⑤ 一个 QAction 喂菜单+工具栏+快捷键+状态提示；快捷键一律用 QKeySequence 标准键 + 想清楚 ShortcutContext。";
        qInfo() << "⑥⑦ addAction(QAction*) 只登记、addAction(QString) 新建并返回；工具栏/菜单里每个控件都被包成 QAction。";
        qInfo() << "⑧ QActionGroup 在 Qt6 属 QtGui，构造必须传 parent；互斥用 setExclusive 或 ExclusionPolicy。";
        qInfo() << "⑨⑩ dock 的显示/隐藏交给 toggleViewAction；状态栏临时消息有 timeout（0=常驻），常驻内容用 addPermanentWidget。";
        qInfo() << "⑪ saveState/restoreState + QSettings 管布局：closeEvent 里存、show() 之后恢复、version 两边写同一个数。";
        QCoreApplication::quit();
    });

    return app.exec();
}

// ============================================================================
// 附 A：本讲故意「不做」的三件事，以及为什么
// ============================================================================
/*
 *  ① 不 setLayout(win)。见证据 ②：layout() 槽位已经被 QMainWindow 自己的布局占了，
 *     QWidget::setLayout 的实现遇到「已有布局」会打 qWarning 并直接 return，
 *     你 new 的那个 QLayout 因为没被接管而泄漏。要排版，就在
 *     setCentralWidget(容器) 里给**容器** setLayout —— 那块地才归你。
 *     （第 7 讲会在普通 QWidget 上把「同一个控件 setLayout 两次」完整撞一遍。）
 *  ② 不弹 QMenu。菜单我们只「挂上去」，不调 qmenu.h 第 146 行的 exec()（阻塞），
 *     也不调 145 行的 popup(QPoint)（它会开始抓鼠标，无人值守跑到一半容易被它吃掉事件）。
 *     要演示「菜单项被点了」，正确姿势是 action->trigger()（qaction.h 210 行）——
 *     本讲全程就是这么干的，效果等价于用户点了一次，且不阻塞。
 *  ③ 不写 QFileDialog/QMessageBox 的静态便利函数。见 actOpen 那段注释：
 *     它们内部起模态循环，必须有人点才返回，是 ctest 超时的头号原因。
 *     第 5 讲给了「实例 + open()/show() + QTimer 自动 accept()」的替代写法。
 *
 * 另外三个 API 细节，本讲核对过，一并记下：
 *   · QActionGroup 的构造函数 parent **没有默认值**（qactiongroup.h 32 行），
 *     `new QActionGroup;` 在 Qt6 编不过——这是 Qt5→Qt6 最容易撞的编译错误之一。
 *   · QToolBar 里的「控件」都被包成 QAction：addWidget 返回 QAction*（qtoolbar.h 69 行）；
 *     QStatusBar 那边才是按控件摘（removeWidget，qstatusbar.h 31 行）。
 *     想从工具栏摘掉一个控件，得先找到那个包装 action。
 *   · QDockWidget::toggleViewAction() 是 const（78 行），拿到的始终是同一个 QAction，
 *     所以在 View 菜单里重复挂不会产生第二份状态，别自己 new 一个去 setVisible()。
 */

// ============================================================================
// 附 B：QMainWindowLayout 那张图，以及三条我修过的 bug
// ============================================================================
/*
 * 五块区域的「领地划分」（脑内图，本资料库第 4 阶段会有图解页）：
 *
 *   ┌──────────────────────── menuBar() ─────────────────────────┐
 *   ├──────────────────── addToolBar(Top) ───────────────────────┤  ← 可 addToolBarBreak 换行
 *   ├─ addDockWidget(Left) ─┬──── centralWidget() ────┬ addDockWidget(Right) ┤
 *   │  （可 tabify / split）│  唯一的一块「自由地」      │                      │
 *   ├──────────────────── addToolBar(Bottom) ────────────────────┤
 *   └──────────────────────── statusBar() ───────────────────────┘
 *
 * 左上/右上角归谁？默认角上那块区域属于 dock 还是工具栏，用 setCorner(Qt::TopLeftCorner,
 * Qt::LeftDockWidgetArea)（110 行）指定——新手常见困惑「dock 拖到左上角怎么占了整列」就是它。
 *
 * 三条真实踩过的坑：
 *  ① 「菜单里能看到保存，但快捷键按了没反应」：那个 action 挂在一个 setEnabled(false)
 *     的父菜单/父控件之下（QAction::setEnabled 是 215 行，会级联灰掉），
 *     或者两个 action 抢同一个键且 ShortcutContext 都是 WindowShortcut
 *     ——只有先注册的那个生效。查这类 bug 请打 shortcutContext() + shortcut()。
 *  ② 「保存的布局下次打开不见了」：十有八九是 saveState 存了、但 restoreState 在
 *     show() **之前**调（此时 dock 还没建完，返回 false），或者 version 两边不一致，
 *     或者 addToolBar 时没设 objectName（恢复是按 objectName/标题找的）。
 *  ③ 「用 quit() 退出后设置全丢」：QCoreApplication::quit() 只是退出事件循环，
 *     **不会**给每个窗口发 QCloseEvent，写在那里的保存逻辑一次都不会跑。
 *     要么程序末尾调 close()，要么把保存接在 aboutToQuit 上（第 3 讲讲过那个信号）。
 *
 * ——— 老师傅的话 ———
 *   徒弟，今天这一讲的关键词是**领地**。QMainWindow 不是「一块能塞东西的板」，
 *   它是**已经画好行政图的国家**：菜单条、工具栏、中央、四侧 dock、状态栏，
 *   每一块都有指定的入口函数。你想往中央塞一张布局表？不行——那块地只认
 *   setCentralWidget()，你要排版就在**它里面**再套一层容器 widget，那一层才归你。
 *   这条纪律换来的好处是：Qt 能替你记住「用户把工具栏拖到哪、dock 拉多宽」，
 *   一句 saveState/restoreState 就还回去了。你非要自己 setLayout，
 *   换来的是这套免费功能全部失效。
 *
 *   再说 QAction。新手把菜单当「一串带文字的条目」，把工具栏当「一排按钮」，
 *   于是写了六段代码实现同一个「保存」。记住：**用户能点的东西都应该先是 QAction**，
 *   它自带 text/icon/shortcut/checkable/enabled/statusTip/role 这一整套语义，
 *   菜单和工具栏只是它的两个「显示口」。你把它当动作单元，禁用、置灰、
 *   本地化、快捷键就都是一次性的事；把它当 UI 零件，就永远在对齐两份状态。
 *
 *   最后提醒一句快捷键上下文。WindowShortcut 是默认，也是打架最多的一档：
 *   同一窗口里同键只活一个。你做「插件式」界面时，插件里塞的 action 抢了你的键，
 *   现象就是「按了没反应」，而且查不出报错——这种时候别看代码，
 *   打一遍所有 action 的 shortcut()+shortcutContext()，两秒钟就定位了。
 *
 * ——— 小练习 ———
 *   1. 把 actSave 的 setShortcutContext 改成 Qt::WidgetShortcut，再新建第二个
 *      QAction 也用 Ctrl+S 且上下文是 WindowShortcut，用 trigger 与键盘各试一遍，
 *      把「谁生效」写回本文件注释（这就是快捷键打架的最小复现）。
 *   2. 用 win->setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea)（110 行）
 *      改变左上角归属，再 saveState(1)/restoreState(...,1) 前后对比 dock 几何，
 *      亲眼看「setCorner 也会进状态」。
 *   3. 覆写 MainWindow::createPopupMenu()（154 行，虚函数），在里面只列出
 *      两个 dock 的 toggleViewAction()，跑起来在工具栏空白处右键看看菜单变没变。
 *      （注意：Qt 自己弹那个菜单是内部的，你只需要覆写，不必自己去 exec。）
 */
// ============================================================================

#include "06_mainwindow.moc"   // 本文件内定义了带 Q_OBJECT 的 MainWindow，AUTOMOC 生成此文件
