// ============================================================================
// 第 3 讲 · 容器控件（QGroupBox / QTabWidget / QStackedWidget / QScrollArea /
//           QSplitter / QToolBox）
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   容器控件的共同点：**它们只负责「组织孩子」和「滚动/切换」，不负责尺寸谈判**。
//   把这句话刻下来，本模块后面三个坑（ splitter 拖不动、滚动区不出现滚动条、
//   Tab 页里控件挤成一团）都是同一件事——你把尺寸谈判错怪给容器了。
//   本讲讲清六件事：
//     1) QGroupBox 的 setCheckable：一个勾选框**自动**开/关整组的子控件；
//     2) QTabWidget：addTab 返回 index、页面对象的生命周期（都活着，只是隐藏）、
//        tabCloseRequested 的真实触发条件、可拆分（setMovable）与 documentMode；
//     3) QStackedWidget + QButtonGroup：向导页（Wizard）的最小可用骨架；
//     4) QScrollArea：setWidgetResizable(true) 的**真正含义**（不是「让内容可变」，
//        而是「由滚动区替内容控件决定尺寸」）；
//     5) QSplitter：setSizes() 是**请求**不是命令，sizes() 读回来的是谈判结果；
//     6) QToolBox：纵向折叠面板，和 QTabWidget 的取舍。
//   尺寸谈判本体（sizeHint / minimumSizeHint / sizePolicy / 布局的 stretch）
//   在第 8 讲正面讲，图解页见：../visuals/layout_negotiation.html
//   （该页由「图解专区」维护；若尚未生成，请先看第 8 讲正文的表格与事件序列。）
//
// 对应构建目标名：widgets_03_containers
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(widgets_03_containers 03_containers.cpp)
//   target_link_libraries(widgets_03_containers PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)   # 本文件里**没有** Q_OBJECT 类，无需 #include "03_....moc"
//
// 运行前须知：
//   · 本机当前无法编译链接 Qt 运行时，本示例**未在本机实测编译**；
//     每个 API 都对着 C:\msys64\ucrt64\include\qt6 下的真实头文件核对（行号见下）。
//   · 无人值守：QTimer::singleShot 依次 setCurrentIndex / setChecked / click / setSizes，
//     用 qInfo() 打印关键结论，最后 QCoreApplication::quit()。**不弹任何模态框**。
//   · 【重要】本讲凡是「要看几何尺寸」的证据，都必须**等事件循环跑过一轮布局之后**再读，
//     所以那些 qInfo() 全排在 QTimer 里。在 main 里 show() 后面直接读 size() 拿到的是
//     谈判前的草稿——第 1 阶段第 1 讲讲过「show 只是挂号」，这里同理。
//   · Qt5 → Qt6 差异（本讲涉及）：
//       - QSplitter::sizes() 在 Qt6 返回 **QList<int>**（qsplitter.h 第 54 行），
//         setSizes 的参数也是 const QList<int>&（第 55 行）。Qt5 时代是 QVector<int>；
//         Qt6 里 QVector 只是 QList 的别名，但**写 QVector<int> 会触发弃用警告**，
//         新代码请一律 QList<int>。
//       - QTabWidget::setTabButton / tabBar 等接口本机头文件里能看到，
//         但 Qt6 的 QTabBar 关闭按钮样式改了，别照搬 Qt5 的截图。
// ============================================================================

#include <QApplication>
#include <QCoreApplication>
#include <QWidget>
#include <QGroupBox>            // QtWidgets/qgroupbox.h
#include <QTabWidget>           // QtWidgets/qtabwidget.h
#include <QStackedWidget>       // QtWidgets/qstackedwidget.h
#include <QScrollArea>          // QtWidgets/qscrollarea.h
#include <QAbstractScrollArea>  // QtWidgets/qabstractscrollarea.h：viewport()
#include <QSplitter>            // QtWidgets/qsplitter.h
#include <QToolBox>             // QtWidgets/qtoolbox.h
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QRadioButton>
#include <QPushButton>
#include <QButtonGroup>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QList>
#include <QTimer>
#include <QDebug>
#include <QString>

// ============================================================================
// 一、核对签名（本讲全部依据）
// ============================================================================
// QtWidgets/qgroupbox.h
//   第 17 行   class Q_WIDGETS_EXPORT QGroupBox : public QWidget
//   第 22~25 行 属性 alignment / flat / checkable / checked(NOTIFY toggled)
//   第 32 行   void setTitle(const QString &title);        第 35 行 void setAlignment(int alignment);
//   第 40 行   void setFlat(bool);                        第 42 行 void setCheckable(bool checkable);
//   第 46 行   void setChecked(bool checked);
//   第 48~50 行 Q_SIGNALS: void toggled(bool);
//   ⚠ 注意 setAlignment 的参数是 **int**（不是 Qt::Alignment），这是历史包袱，编译器不会帮你查
// QtWidgets/qtabwidget.h
//   第 20 行   class Q_WIDGETS_EXPORT QTabWidget : public QWidget
//   第 23~32 行 属性 tabPosition / currentIndex(NOTIFY currentChanged) / elideMode /
//                documentMode / tabsClosable / movable
//   第 39/40 行 int addTab(QWidget *widget, const QString &) / (widget, const QIcon&, label)  ← **返回 index**
//   第 42/43 行 int insertTab(int index, QWidget*, const QString &) / 带 icon 版
//   第 45 行   void removeTab(int index);                 第 53 行 QString tabText(int) const;
//   第 71~73 行 QWidget *widget(int) const; int indexOf(const QWidget*) const; int count() const;
//   第 75~78 行 enum TabPosition { North, South, West, East }; tabPosition()/setTabPosition(...)
//   第 81 行   void setTabsClosable(bool closeable);      第 84 行 void setMovable(bool movable);
//   第 100 行  void setElideMode(Qt::TextElideMode mode);  第 109 行 void setDocumentMode(bool set);
//   第 119/120 行 void setCurrentIndex(int index); void setCurrentWidget(QWidget *widget);
//   第 122~124 行 Q_SIGNALS: currentChanged(int index); tabCloseRequested(int index);
// QtWidgets/qstackedwidget.h
//   第 17 行   class Q_WIDGETS_EXPORT QStackedWidget : public QFrame
//   第 21 行   属性 currentIndex(NOTIFY currentChanged)
//   第 27 行   int addWidget(QWidget *w);                 第 29 行 void removeWidget(QWidget *w);
//   第 32 行   int currentIndex() const;                  第 35 行 QWidget *widget(int) const;
//   第 36 行   int count() const;                         第 39 行 void setCurrentIndex(int index);
//   第 42~43 行 Q_SIGNALS: void currentChanged(int);
// QtWidgets/qscrollarea.h
//   第 17 行   class Q_WIDGETS_EXPORT QScrollArea : public QAbstractScrollArea
//   第 20 行   属性 widgetResizable
//   第 28 行   void setWidget(QWidget *widget);           第 31/32 行 widgetResizable()/setWidgetResizable(bool)
//   第 39 行   void setAlignment(Qt::Alignment);          （widgetResizable(false) 时决定内容贴哪边）
// QtWidgets/qabstractscrollarea.h
//   第 57 行   QWidget *viewport() const;                 ← 「看得见的那块」就是它
//   第 42 行   void setVerticalScrollBarPolicy(Qt::ScrollBarPolicy);（横向同名 setter 也在这一带）
// QtWidgets/qsplitter.h
//   第 21 行   class Q_WIDGETS_EXPORT QSplitter : public QFrame
//   第 35/36 行 void addWidget(QWidget *widget); void insertWidget(int index, QWidget *widget);
//   第 39/40 行 setOrientation(Qt::Orientation) / orientation()
//   第 42 行   void setChildrenCollapsible(bool);         第 45 行 void setCollapsible(int index, bool);
//   第 47 行   void setOpaqueResize(bool opaque = true);  第 60/61 行 handleWidth()/setHandleWidth(int)
//   第 54 行   QList<int> sizes() const;                  ← Qt6 返回 QList<int>
//   第 55 行   void setSizes(const QList<int> &list);     ← **请求**
//   第 70 行   void setStretchFactor(int index, int stretch);
//   第 72~73 行 Q_SIGNALS: void splitterMoved(int pos, int index);
// QtWidgets/qtoolbox.h
//   第 18 行   class Q_WIDGETS_EXPORT QToolBox : public QFrame
//   第 21 行   属性 currentIndex(NOTIFY currentChanged)
//   第 28/29 行 int addItem(QWidget *widget, const QString &text) / 带 QIcon 版
//   第 33 行   void removeItem(int index);                第 35 行 void setItemEnabled(int index, bool enabled);
//   第 38 行   void setItemText(int index, const QString &text);
//   第 49 行   int currentIndex() const;                  第 56 行 void setCurrentIndex(int index);
//   第 59~60 行 Q_SIGNALS: void currentChanged(int index);
// QtCore/qnamespace.h
//   第 1285~1288 行 enum ScrollBarPolicy { ScrollBarAsNeeded, ScrollBarAlwaysOff, ScrollBarAlwaysOn };
// QtWidgets/qboxlayout.h        第 37~40 行 addStretch / addWidget / addLayout
// QtWidgets/qwidget.h           第 503 行 resize(int,int)；第 369 行 setWindowTitle；
//                               setMinimumSize(int,int) 在同一带（尺寸谈判的三件套之一，第 8 讲）

// ============================================================================
// 二、造一点「内容」用的小工厂（返回裸指针，交给容器接管所有权）
// ============================================================================
static QWidget *makePage(const QString &name, const QString &hint)
{
    auto *page = new QWidget;
    page->setObjectName(name);                    // QObject::setObjectName：调试打印用
    auto *lay = new QVBoxLayout(page);
    auto *title = new QLabel(QStringLiteral("<b>%1</b>").arg(name));
    title->setTextFormat(Qt::RichText);           // qlabel.h 60 行
    lay->addWidget(title);
    lay->addWidget(new QLabel(hint));
    lay->addWidget(new QLineEdit);
    lay->addStretch();                            // qboxlayout.h 37 行：把内容顶到上面
    return page;
}

// ============================================================================
// 三、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle(QStringLiteral("第 3 讲 · 容器控件"));
    window.resize(760, 560);
    auto *root = new QVBoxLayout(&window);

    // ────────────────────────────────────────────────────────────────
    // 证据 ① QGroupBox：checkable 一勾，整组孩子跟着开关
    // ────────────────────────────────────────────────────────────────
    auto *gb = new QGroupBox(QStringLiteral("代理设置"));   // qgroupbox.h 17 行
    gb->setCheckable(true);                                // 42 行
    gb->setChecked(false);                                 // 46 行：默认关掉整组
    gb->setFlat(true);                                     // 40 行：只留标题下的细线，风格更轻
    auto *gbLay = new QVBoxLayout(gb);
    auto *gbHost = new QLineEdit;
    auto *gbAuth = new QCheckBox(QStringLiteral("需要认证"));
    gbLay->addWidget(gbHost);
    gbLay->addWidget(gbAuth);
    root->addWidget(gb);
    QObject::connect(gb, &QGroupBox::toggled, [](bool on) {
        qInfo() << "① QGroupBox::toggled →" << on;
    });
    qInfo() << "① setChecked(false) 之后：组里的 QLineEdit isEnabled =" << gbHost->isEnabled()
            << "（**容器帮你把 disabled 传下去了**，这就是「一个框关掉一整片设置」的实现方式，"
               "不需要你自己遍历 setEnabled）";

    // ────────────────────────────────────────────────────────────────
    // 证据 ② QTabWidget：addTab 返回 index，页面全活着只是隐藏
    // ────────────────────────────────────────────────────────────────
    auto *tabs = new QTabWidget;
    tabs->setDocumentMode(true);                            // qtabwidget.h 109 行：去掉「凸起」，像文档标签
    tabs->setMovable(true);                                 // 84 行：允许用户拖拽换序（**只能鼠标拖，代码换序请用 insertTab**）
    tabs->setTabsClosable(true);                            // 81 行：每页出现关闭按钮 → 发 tabCloseRequested
    tabs->setTabPosition(QTabWidget::North);                // 75~78 行：North/South/West/East
    tabs->setElideMode(Qt::ElideRight);                     // 100 行：标签太窄时省略号在右
    QWidget *pageGeneral = makePage(QStringLiteral("常规"), QStringLiteral("这里放常规设置"));
    QWidget *pageNet     = makePage(QStringLiteral("网络"), QStringLiteral("这里放网络设置"));
    QWidget *pageDebug   = makePage(QStringLiteral("调试"), QStringLiteral("这里放调试开关"));
    const int iGen = tabs->addTab(pageGeneral, QStringLiteral("常规"));  // 39 行
    const int iNet = tabs->addTab(pageNet, QStringLiteral("网络标签名字特别长特别长特别长"));
    const int iDbg = tabs->addTab(pageDebug, QStringLiteral("调试"));
    qInfo() << "② addTab 返回的 index =" << iGen << iNet << iDbg
            << "；count =" << tabs->count()                // 73 行
            << "；indexOf(pageNet) =" << tabs->indexOf(pageNet)   // 72 行
            << "；tabText(1) =" << tabs->tabText(1);              // 53 行
    // 【关键认知】addTab **不会**复制页面、也不会延迟创建：三个 QWidget 从一开始就都存在，
    // 切页只是 setVisible(false/true)。所以「切到第 3 页才加载数据」这个需求
    // 得你自己写：连 currentChanged(int)，在里面判断「这一页还没初始化过」再初始化。
    QObject::connect(tabs, &QTabWidget::currentChanged, [tabs](int index) {
        qInfo() << "② currentChanged → index =" << index
                << " 页面 objectName =" << (tabs->widget(index) ? tabs->widget(index)->objectName() : QStringLiteral("(无)"));
    });
    QObject::connect(tabs, &QTabWidget::tabCloseRequested, [tabs](int index) {
        // ⚠ tabCloseRequested **只在真人点击标签上的关闭按钮时**发；
        // 没有任何公开的「编程触发」入口（它来自 QTabBar 的鼠标命中测试）。
        // 所以本讲不伪造它，只在下面用 removeTab 演示「关掉一页」的效果。
        qInfo() << "② tabCloseRequested →" << index
                << "（正规处理：save 之后再 removeTab；**removeTab 不会 delete 页面**，"
                   "要么你接手所有权，要么 addTab 时给页面指定 parent）";
        QWidget *w = tabs->widget(index);
        tabs->removeTab(index);                    // 45 行
        if (w) w->deleteLater();                   // 手工补上生命周期，否则这是内存泄漏
    });
    root->addWidget(tabs);
    // 顺带认识一下 Qt 的「拆 Tab」：QTabWidget 内部就是 QTabBar + QStackedWidget。
    // 想要「Tab 拖出窗口变成浮动窗口」那种 IDE 体验，得自己接 QTabBar + 手工搬页面，
    // Qt 原生不提供可丢弃（detachable）的 TabWidget —— 别在设计评审时拍胸脯。

    // ────────────────────────────────────────────────────────────────
    // 证据 ③ QStackedWidget + QButtonGroup：向导页最小骨架
    // ────────────────────────────────────────────────────────────────
    auto *stack = new QStackedWidget;
    stack->addWidget(makePage(QStringLiteral("向导第1页"), QStringLiteral("选产品")));
    stack->addWidget(makePage(QStringLiteral("向导第2页"), QStringLiteral("选配置")));
    stack->addWidget(makePage(QStringLiteral("向导第3页"), QStringLiteral("确认")));
    auto *navRow = new QHBoxLayout;
    auto *btnGroup = new QButtonGroup(&window);      // qbuttongroup.h 31 行 addButton(QAbstractButton*, int id)
    for (int i = 0; i < stack->count(); ++i) {       // 36 行 count()
        auto *rb = new QRadioButton(QStringLiteral("第 %1 页").arg(i + 1));
        btnGroup->addButton(rb, i);                  // id 直接等于页号：省掉一张映射表
        navRow->addWidget(rb);
    }
    QObject::connect(btnGroup, &QButtonGroup::idClicked, [stack](int id) {
        stack->setCurrentIndex(id);                  // qstackedwidget.h 39 行
    });
    QObject::connect(stack, &QStackedWidget::currentChanged, [](int index) {
        qInfo() << "③ QStackedWidget::currentChanged →" << index;
    });
    root->addLayout(navRow);
    root->addWidget(stack);
    qInfo() << "③ QStackedWidget 与 QTabWidget 的分工："
            << "前者没有标签条，适合「向导 / 状态机 / 只有程序知道该显示哪页」；"
            << "后者有标签条，适合「用户自己挑」";

    // ────────────────────────────────────────────────────────────────
    // 证据 ④ QScrollArea：setWidgetResizable 的真正含义
    // ────────────────────────────────────────────────────────────────
    auto *area = new QScrollArea;
    area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);   // qabstractscrollarea.h 42 行 + qnamespace.h 1285~1288
    auto *big = new QLabel(QStringLiteral("我是一张**很长**的内容：把 minimumSize 撑大，滚动条就该出来了"));
    big->setWordWrap(true);
    big->setAlignment(Qt::AlignLeft | Qt::AlignTop);   // qnamespace.h 155~175 行
    big->setMinimumSize(680, 900);                      // qwidget.h：minimumSize 是谈判的**硬下限**
    area->setWidget(big);                               // qscrollarea.h 28 行
    qInfo() << "④ setWidgetResizable 当前值 =" << area->widgetResizable()
            << "（默认 **false**：内容保持自己的 sizeHint，超出视口的部分靠滚动条看见）";
    root->addWidget(area);

    // ────────────────────────────────────────────────────────────────
    // 证据 ⑤ QSplitter：setSizes 是请求，sizes() 读回的是谈判结果
    // ────────────────────────────────────────────────────────────────
    auto *split = new QSplitter(Qt::Horizontal);       // qsplitter.h 21 行（orientation ctor）
    split->addWidget(makePage(QStringLiteral("左pane"), QStringLiteral("左侧内容")));
    split->addWidget(makePage(QStringLiteral("右pane"), QStringLiteral("右侧内容")));
    split->setHandleWidth(10);                          // 61 行：把手加宽，方便测试拖拽
    split->setCollapsible(0, false);                    // 45 行：左边不许被拖到 0 宽
    split->setChildrenCollapsible(false);               // 42 行：整组都不许塌
    split->setStretchFactor(1, 3);                      // 70 行：**只对「多余空间怎么分」起作用**
    split->addWidget(new QLabel(QStringLiteral("<i>第三块：QSplitter 可以随便塞</i>")));
    split->setSizes({200, 300, 200});                   // 55 行 QList<int>（Qt6 别再写 QVector<int>）
    qInfo() << "⑤ setSizes 之后立刻读 sizes() =" << split->sizes()
            << "→ 注意这是「请求值」，**还没跟 sizePolicy 谈判**，几何尺寸见下面 QTimer 里的打印";
    QObject::connect(split, &QSplitter::splitterMoved, [](int pos, int index) {
        qInfo() << "⑤ splitterMoved(pos =" << pos << ", index =" << index << ")（真人拖动把手才会发）";
    });
    root->addWidget(split);

    // ────────────────────────────────────────────────────────────────
    // 证据 ⑥ QToolBox：纵向折叠面板
    // ────────────────────────────────────────────────────────────────
    auto *box = new QToolBox;
    box->addItem(makePage(QStringLiteral("高级"), QStringLiteral("只有少数人需要的一堆开关")),
                 QStringLiteral("高级"));              // qtoolbox.h 28 行
    box->addItem(makePage(QStringLiteral("关于"), QStringLiteral("版本信息与许可")),
                 QStringLiteral("关于"));
    QObject::connect(box, &QToolBox::currentChanged, [](int index) {
        qInfo() << "⑥ QToolBox::currentChanged →" << index << "（-1 表示全收起）";
    });
    box->setItemEnabled(1, false);                      // 35 行：第二项变灰
    qInfo() << "⑥ setItemEnabled(1,false) 后：currentIndex =" << box->currentIndex()
            << "；把当前页禁用会自动跳到别的页吗？看下面打印就知道（**别赌，显式 setCurrentIndex**）";
    root->addWidget(box);
    // QToolBox vs QTabWidget 的取舍：**内容高度差异很大**时用 ToolBox（展开的一页才占高度），
    // 各页体量接近、且需要「一眼看到全部标签」时用 TabWidget。
    // ToolBox 的代价：同一时刻只能看见一节，用户找不到东西的概率更高。

    window.show();

    // ── 等到事件循环跑过一轮布局，再读真实几何 ──────────────────────
    QTimer::singleShot(300, [gb, gbHost, gbAuth] {
        gb->setChecked(true);                             // 46 行 → 发 toggled(true)
        qInfo() << "① 勾上分组框之后：QLineEdit isEnabled =" << gbHost->isEnabled()
                << " QCheckBox isEnabled =" << gbAuth->isEnabled();
    });

    QTimer::singleShot(600, [gb, gbHost] {
        gb->setDisabled(false);                           // QWidget::setDisabled 走的是 enabled 属性
        qInfo() << "① 【坑】绕过勾选、直接动 enabled：对整组 setDisabled(false) 或对子控件 setEnabled(true)"
                   "会让**勾选状态与子控件可用性脱钩**。此刻实测：gb->isChecked() =" << gb->isChecked()
                << " 而组里 QLineEdit 的 isEnabled =" << gbHost->isEnabled()
                << " → 请把这两个值记回注释：只要它们不一致，用户的直觉（看勾）与程序的事实（看 enabled）就分家了。"
                   "正规做法：**只碰 QGroupBox::setChecked**，或者干脆连 toggled 自己管。";
        gb->setChecked(false);
    });

    QTimer::singleShot(900, [tabs, stack] {
        tabs->setCurrentIndex(2);                         // qtabwidget.h 119 行
        qInfo() << "② setCurrentIndex(2) 之后 currentIndex 相关：count =" << tabs->count();
        tabs->insertTab(0, makePage(QStringLiteral("新页"), QStringLiteral("insertTab 插到最前")),
                        QStringLiteral("插页"));          // 42 行：换序/加页的正道
        qInfo() << "② insertTab(0,…) 之后：tabText(0) =" << tabs->tabText(0)
                << " count =" << tabs->count();
        tabs->removeTab(tabs->count() - 1);               // 演示移除末页（不 delete，见上面的注释）
        stack->setCurrentIndex(0);                        // 39 行
    });

    QTimer::singleShot(1200, [tabs, pageGeneral] {
        // 用 tabCloseRequested 的**处理逻辑**手工走一遍（信号本身无法编程触发）
        const int idx = tabs->indexOf(pageGeneral);       // 72 行
        qInfo() << "② 模拟「用户点了常规页的关闭按钮」：indexOf(pageGeneral) =" << idx;
        if (idx >= 0) {
            QWidget *w = tabs->widget(idx);               // 71 行
            tabs->removeTab(idx);                         // 45 行
            if (w) w->deleteLater();
        }
        qInfo() << "② 移除之后：count =" << tabs->count()
                << " currentIndex =" << tabs->currentIndex()
                << "（属性 25 行 READ currentIndex）→ 关掉当前页时 Qt 会自己挑一页显示，"
                   "**但被关的页面如果没人接手就泄漏了**";
    });

    QTimer::singleShot(1500, [area, big] {
        qInfo() << "\n=== ④ setWidgetResizable 的真正含义（谈判之后的实测尺寸）===";
        qInfo() << "   false（当前）：内容 widget 的 size =" << big->size()
                << "，minimumSize =" << big->minimumSize()
                << "，viewport 的 size =" << area->viewport()->size();   // qabstractscrollarea.h 57 行
        area->setWidgetResizable(true);                                 // qscrollarea.h 32 行
        qInfo() << "   改成 true 之后（**同一轮事件循环里**，还没重排）：size =" << big->size();
    });

    QTimer::singleShot(1800, [area, big] {
        qInfo() << "   true 生效并重排之后：内容 size =" << big->size()
                << "，viewport size =" << area->viewport()->size();
        qInfo() << "   读法：**widgetResizable(true) 不是「让内容可拉伸」的开关，"
                   "而是「把内容控件的尺寸决定权交给滚动区」的开关**。"
                   "true 时滚动区按 viewport 大小给内容 resize，内容的 minimumSize 大于视口才出滚动条；"
                   "false 时内容永远按自己的 sizeHint/resize 走，视口装不下就滚。"
                   "所以「加了 QScrollArea 却没有滚动条」的第一大原因，"
                   "是 setWidgetResizable(true) + 内容没有 minimumSize 约束（谈判结果就是不滚）。"
                   "→ 尺寸谈判本体在第 8 讲，图解见 ../visuals/layout_negotiation.html";
    });

    QTimer::singleShot(2100, [split] {
        qInfo() << "\n=== ⑤ QSplitter 的请求 vs 结果 ===";
        qInfo() << "   请求的 setSizes({200,300,200}) 之后，谈判回来的 sizes() =" << split->sizes();
        const QList<int> s = split->sizes();      // qsplitter.h 54 行：Qt6 返回 QList<int>
        const int w0 = s.value(0), w1 = s.value(1), w2 = s.value(2);
        qInfo() << "   三块的实际宽度 =" << w0 << w1 << w2
                << "；注意 setStretchFactor(1,3) 只影响**总宽变化时多出来的部分怎么分**，"
                   "不影响你 setSizes 指定的这份分配（70 行的语义）";
        split->setSizes({100, 100, 100});
        qInfo() << "   再请求 {100,100,100}（总和 300 小于可见总宽）之后 sizes() =" << split->sizes()
                << "→ **setSizes 的总和与可见宽度不符时，Qt 会按谈判重分**，读回来就不是你写的数";
    });

    QTimer::singleShot(2400, [box] {
        box->setItemEnabled(1, true);                    // 35 行：解禁
        box->setItemText(1, QStringLiteral("关于（已改名）"));   // 38 行
        box->setCurrentIndex(1);                         // 56 行
        qInfo() << "⑥ 展开第二节后 currentIndex =" << box->currentIndex();
        box->setCurrentIndex(-1);                        // -1：全部收起（currentIndex 属性允许 -1）
        qInfo() << "⑥ setCurrentIndex(-1) 之后 =" << box->currentIndex()
                << "→ QToolBox 可以「一节都不开」，QTabWidget 不行（它总有一页是当前的）";
    });

    QTimer::singleShot(2700, [] {
        qInfo() << "\n=== 结论 ===";
        qInfo() << "① 容器管组织与开关，**不管尺寸谈判**：谈判是 sizeHint / minimumSizeHint / sizePolicy 三方博弈（第 8 讲）。";
        qInfo() << "② QTabWidget = QTabBar + QStackedWidget；页面全程活着，切页只是隐藏。";
        qInfo() << "③ tabCloseRequested / splitterMoved / QToolBox 的展开收起都来自**真实鼠标**，无法编程伪造；";
        qInfo() << "   要自动化就用 click()/setCurrentIndex()/setSizes() 这些「合法入口」，别去投鼠标坐标。";
        qInfo() << "④ removeTab 不 delete 页面：QTabWidget 与 QStackedWidget 都不替你回收，这是常见泄漏点。";
        QCoreApplication::quit();
    });

    return app.exec();
}

// ============================================================================
// 附：六个容器的选型表 + 一句关于「滚动区套布局」的老话
// ============================================================================
/*
 *  需求                              选择                             注意
 *  ------------------------------------------------------------------------------------
 *  一组设置可以整体开关             QGroupBox + setCheckable          只碰 setChecked，别碰孩子的 enabled
 *  用户自己挑的分类页               QTabWidget                        页面多时 setElideMode/setDocumentMode
 *  程序控制的步骤页/状态页          QStackedWidget + QButtonGroup     id == 页号，省掉映射表
 *  内容比窗口高                     QScrollArea                       setWidgetResizable 的语义见上面证据④
 *  用户要自己调两块的比例           QSplitter                         setSizes 是请求；handle 宽度要够点
 *  一叠「可能只想看一节」的面板     QToolBox                          可以 -1 全收起；节数别超过 5
 *  同一窗口里两块都能滚             QSplitter 里各放一个 QScrollArea  滚动区**不共享**滚动条
 *
 * 关于「QScrollArea 里放 QVBoxLayout」的老话：
 *   新手常写 area->setWidget(inner); 然后 inner 里塞一个 QVBoxLayout，
 *   结果怎么调都不滚。原因有两个：
 *     a) inner 没有 minimumSize，谈判结果就是「和视口一样大」→ 永远不滚；
 *     b) 布局的 sizeConstraint 默认不是 SetMinimumSize，于是布局不会把
 *        「内容需要多高」转成 inner 的 minimumSize。
 *   正规做法二选一：① setWidgetResizable(true) 并给 inner 加 minimumSize；
 *                 ② 保持 false，但显式 inner->resize(需要的宽高)。
 *   这一条在第 8 讲会画成谈判图，见 ../visuals/layout_negotiation.html。
 *
 * ——— 老师傅的话 ———
 *   徒弟，容器这类东西，代码量最小，事故最多。
 *   我给你归纳成三句，都是花钱修过的：
 *
 *   第一句：**removeTab / removeWidget 不会 delete**。
 *   Qt 的所有权规矩是「父控件 delete 孩子」，可 remove 这个动作恰恰是「断父子」，
 *   断完就没人管了。你做「可关闭标签页」的功能，跑一天内存涨一天，
 *   十有八九就漏在这。修法：先 widget(index) 取出来，再 removeTab，再 deleteLater。
 *
 *   第二句：**QGroupBox 的开关和 setEnabled 是两套世界**。
 *   有的同事为了让某个设置项生效，直接在代码里 child->setEnabled(true)，
 *   框框上的勾还挂着没打，界面看起来「关着」但值是活的。
 *   这类 bug 测试很难发现，用户投诉才浮出水面：「我明明没开代理，怎么走了代理」。
 *
 *   第三句：**别问「滚动条为什么不出现」，先问「内容控件的 minimumSize 是多少」**。
 *   滚动区不是「内容超出就滚」，而是「内容**被允许**超出才滚」。
 *   这个「被允许」就是尺寸谈判给的。你把谈判学完（第 8 讲），
 *   全 Qt 的界面行为会突然变得可预测——这不是夸张，这是分水岭。
 *
 * ——— 小练习 ———
 *   1. 用 QStackedWidget + 两个 QPushButton（「上一步」「下一步」）把证据③ 的
 *       RadioButton 导航换成真正的向导：按钮的 enabled 状态要跟着 currentIndex 变
 *      （连 currentChanged），第一页禁用「上一步」、最后一页把「下一步」改成「完成」。
 *   2. 把证据⑤ 的 QSplitter 改成三层嵌套（外层 Vertical，里层两个 Horizontal），
 *      打印最内层的 sizes()，然后观察拖动某一层把手时 splitterMoved 的 index 是几——
 *      由此体会「QSplitter 的组合树」和「布局树」是两棵不同的树。
 *   3. 给证据② 加一个「双击标签关闭」的替代实现：setTabsClosable(false)，
 *      连 QTabWidget 的 currentChanged 做「切到该页才加载数据」的懒初始化
 *      （用一个 QSet<int> 记录已初始化的 index），并打印每次加载耗时。
 */
// ============================================================================
