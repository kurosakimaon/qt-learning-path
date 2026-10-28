// ============================================================================
// 第 7 讲 · 布局基础（QBoxLayout / QGridLayout / QFormLayout + 嵌套 + 所有权）
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   「界面在手大变小、变窄就挤成一团」的全部答案都在这一讲。四块硬货：
//     1) 三种主布局怎么用：QBoxLayout（横排/竖排）、QGridLayout（网格）、
//        QFormLayout（标签-字段两列，表单专用）；
//     2) addWidget / addLayout / addSpacing / addStretch 各自往布局里塞了**什么东西**
//        （QLayoutItem 的四种子类：控件项、子布局项、固定间隔项、可伸缩间隔项）；
//     3) **stretch 因子与 alignment 的区别**——新手最混的一点。
//        stretch 决定「多出来的空间怎么分」（几个格各占几份）；
//        alignment 决定「格子里那点地方，控件贴哪一边」（它自己那点尺寸不动）。
//        本讲用两轮 resize + 打印 geometry() 把这条差异做成可见证据；
//     4) 布局/控件的**所有权**：谁删谁、takeAt 之后 widget 归谁、
//        removeWidget 之后 widget 为什么还在屏幕上、同一个 widget setLayout 两次
//        会听到哪一条 Qt 警告（本讲故意撞给你看）。
//   尺寸谈判本体（sizeHint / QSizePolicy）是第 8 讲，图解见
//   ../visuals/layout_negotiation.html（该页由「图解专区」维护）。
//
// 对应构建目标名：widgets_07_layout_basics
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(widgets_07_layout_basics 07_layout_basics.cpp)
//   target_link_libraries(widgets_07_layout_basics PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)   # 本文件里**没有** Q_OBJECT 类，因此不需要 #include "07_....moc"
//
// 运行前须知：
//   · 依赖 Qt6::Widgets。Windows 上要能找到 platforms/qwindows.dll 或 qoffscreen.dll。
//   · 本机当前无法编译链接 Qt 运行时，本示例**未在本机实测编译**；每个类/函数/重载/
//     枚举都对着 C:\msys64\ucrt64\include\qt6 下的真实头文件核对过（行号见下）。
//   · 无人值守：QTimer::singleShot 驱动 resize/setSpacing/takeAt/删布局等动作，
//     每一步都用 qInfo() 打印几何或计数当证据，最后 QCoreApplication::quit()。
//     **没有任何阻塞调用**（不 exec 对话框、不弹 QMessageBox）。
//   · 关于几何读数：show() 之后**必须等事件循环跑过一轮布局**再读 geometry()/size()，
//     在 main 里读到的只是谈判前的草稿（第 1 讲「show 只是挂号」同理）。
//     本讲所有 qInfo 几何打印都排在 QTimer 里，就是这个理由。
//   · Qt5 → Qt6 差异（本讲涉及，均已核对本机头文件）：
//       - **QLayout::setMargin(int) / margin() 在 Qt6 已经没有了**：我在
//         QtWidgets/qlayout.h 里只找到 setContentsMargins（56/57 行）、
//         getContentsMargins（58 行）、unsetContentsMargins（58 行附近）与
//         contentsMargins 属性（32 行），没有任何叫 margin 的成员。
//         Qt5 老代码 `layout->setMargin(9)` → 请写 `layout->setContentsMargins(9,9,9,9)`。
//       - **QSpacerItem 没有独立头文件了**：Qt6 里它声明在
//         QtWidgets/qlayoutitem.h 第 57 行（`class Q_WIDGETS_EXPORT QSpacerItem : public QLayoutItem`），
//         构造函数是 `QSpacerItem(int w, int h, Policy hData = Minimum, Policy vData = Minimum)`
//         （61~64 行）。`#include <QSpacerItem>` 的转发头仍然可用，但别再去找 qspaceritem.h。
//       - QFormLayout 新增 `struct TakeRowResult { QLayoutItem *labelItem; ... }`
//         （qformlayout.h 第 55 行起）——这是 Qt 6.x 才有的「一行拆出来给你」的类型；
//         本讲不去调 takeRow（头文件里我只核到了这个结构体本身），要摘行请以你本机
//         qformlayout.h 的实际声明为准。
//       - SizeConstraint 枚举在 Qt6 多/留了 SetMinAndMaxSize（qlayout.h 40~46 行，
//         六个值全在），名字和 Qt5 一致，可放心跨版本用。
// ============================================================================

#include <QApplication>           // QtWidgets/qapplication.h
#include <QCoreApplication>       // QtCore/qcoreapplication.h：static void quit()
#include <QWidget>                // QtWidgets/qwidget.h：layout()/setLayout（540/541 行）
#include <QLayout>                // QtWidgets/qlayout.h：SizeConstraint / addWidget / takeAt
#include <QLayoutItem>            // ⚠ Qt6 里 QLayoutItem + QSpacerItem + QWidgetItem 都在这一个头
#include <QBoxLayout>             // QtWidgets/qboxlayout.h：QBoxLayout/QHBoxLayout/QVBoxLayout
#include <QGridLayout>            // QtWidgets/qgridlayout.h
#include <QFormLayout>            // QtWidgets/qformlayout.h
#include <QLabel>                 // QtWidgets/qlabel.h
#include <QLineEdit>              // QtWidgets/qlineedit.h
#include <QPushButton>            // QtWidgets/qpushbutton.h
#include <QMargins>               // QtCore/qmargins.h：setContentsMargins(QMargins) 那版
#include <QRect>                  // QtCore/qrect.h
#include <QTimer>
#include <QDebug>
#include <QString>
#include <QStringList>

// ============================================================================
// 一、核对签名（本讲全部依据，路径相对 C:\msys64\ucrt64\include\qt6）
// ============================================================================
// QtWidgets/qlayout.h
//   第 26 行   class Q_WIDGETS_EXPORT QLayout : public QObject, public QLayoutItem
//              ← 注意它是 **QObject**（所以有 objectName/parent），同时**继承 QLayoutItem**
//                （所以布局本身可以嵌进别的布局）
//   第 40~46 行 enum SizeConstraint { SetDefaultConstraint, SetNoConstraint, SetMinimumSize,
//                SetFixedSize, SetMaximumSize, SetMinAndMaxSize };   ← 六个值，一个不少
//   第 53/54 行 virtual int spacing() const; virtual void setSpacing(int);
//   第 56/57 行 void setContentsMargins(int l,int t,int r,int b); void setContentsMargins(const QMargins&);
//   第 58 行   void unsetContentsMargins(); void getContentsMargins(int*,int*,int*,int*) const;
//   第 67/68 行 void setSizeConstraint(SizeConstraint); SizeConstraint sizeConstraint() const;
//   第 70 行   void setSizeConstraints(SizeConstraint horizontal, SizeConstraint vertical); ← 横竖分开定
//   第 85 行   bool activate();               ← 手动跑一次布局（返回是否「还需再算」）
//   第 88 行   void addWidget(QWidget *w);    ← **基类版：没有 stretch/alignment 参数！**
//   第 89 行   virtual void addItem(QLayoutItem *) = 0;
//   第 91/92 行 void removeWidget(QWidget *w); void removeItem(QLayoutItem *);
//   第 98/99 行 virtual QLayoutItem *itemAt(int) const; virtual QLayoutItem *takeAt(int);
//   第 101 行  virtual QLayoutItem *replaceWidget... / 第 106 行 indexOf(const QLayoutItem*)
//   第 111/113 行 QSize totalMinimumSize() const; QSize totalSizeHint() const;
//   第 126 行  void addChildWidget(QWidget *w);   ← 只当子控件、不进布局表（很冷门，别乱用）
//   ※ 本文件里**没有** margin()/setMargin()：见顶部 Qt5→Qt6 说明
// QtWidgets/qboxlayout.h
//   第 24 行   enum Direction { LeftToRight, RightToLeft, TopToBottom, BottomToTop, Down, Up };
//   第 26 行   explicit QBoxLayout(Direction, QWidget *parent = nullptr);
//   第 35~41 行 void addSpacing(int size);            ← 塞一个**固定**间隔
//                void addStretch(int stretch = 0);     ← 塞一个**可伸缩**间隔（默认因子 0！）
//                void addSpacerItem(QSpacerItem*);     ← 自己造间隔项（要带 QSizePolicy）
//                void addWidget(QWidget*, int stretch = 0, Qt::Alignment = Qt::Alignment());
//                void addLayout(QLayout*, int stretch = 0);   ← 嵌套：子布局**不能有 parent widget**
//   第 42 行   void addStrut(int);                   ← 限制该方向上的最小厚度，冷门但有用
//   第 44~49 行 insertSpacing / insertStretch / insertSpacerItem / insertWidget /
//                insertLayout / insertItem          ← 下标都是「插到 index 之前」
//   第 54/55/56/57 行 setStretchFactor(QWidget*, int) → bool / setStretchFactor(QLayout*, int) → bool
//                / setStretch(int index, int) / int stretch(int index) const
//                ★ setStretch(index, n) 按**槽位下标**改因子；setStretchFactor(w, n) 按**对象**找
//                  并改；返回 bool 是因为「找不到就返回 false」——新手常传错下标还以为成功
//   第 59~61 行 sizeHint / minimumSize / maximumSize（都是 override）
//   第 63~65 行 hasHeightForWidth / heightForWidth / minimumHeightForWidth   ← 第 8 讲的主角
// QtWidgets/qgridlayout.h
//   第 36/38 行 setHorizontalSpacing(int) / setVerticalSpacing(int)   ← 网格可以横竖分别设！
//   第 43/44 行 setRowStretch(int row, int stretch) / setColumnStretch(int column, int)
//   第 48/49 行 setRowMinimumHeight(int, int) / setColumnMinimumWidth(int, int)
//   第 53/54 行 int columnCount() const; int rowCount() const;
//   第 65 行   inline void addWidget(QWidget *w) { QLayout::addWidget(w); }
//              ← 一参版是**内联转发给基类**，所以它**没有** stretch/alignment！
//   第 66/67 行 addWidget(QWidget*, int row, int col, Qt::Alignment = ...)
//                addWidget(QWidget*, int row, int col, int rowSpan, int colSpan, Qt::Alignment = ...)
//   第 68/69 行 addLayout(QLayout*, row, col[, rowSpan, colSpan][, alignment])
//   第 75 行   QLayoutItem *itemAtPosition(int row, int column) const;  ← 读某格放了什么
//   第 80 行   void addItem(QLayoutItem*, int row, int col, int rowSpan = 1, int colSpan = 1,
//                Qt::Alignment = ...)
// QtWidgets/qformlayout.h
//   第 80~85 行 addRow(QWidget *label, QWidget *field) / (QWidget*, QLayout*)
//                / (const QString &labelText, QWidget *field) / (const QString&, QLayout*)
//                / addRow(QWidget *widget)   ← **单控件版会跨两列**（放按钮/提示很好用）
//   第 87~92 行 insertRow(int row, ...) 六个对应重载
//   第 34~38 行 enum FieldGrowthPolicy { FieldsStayAtSizeHint, ExpandingFieldsGrow,
//                AllNonFixedFieldsGrow }   ← 决定「字段列」在多宽时会不会跟着长
//   第 41~44 行 enum RowWrapPolicy { DontWrapRows, WrapLongRows, WrapAllRows }
//                ← 窗口窄的时候，是把字段挪到标签下面（Wrap），还是硬挤
//   第 48~50 行 enum ItemRole { LabelRole = 0, FieldRole = 1, SpanningRole = 2 }
//   第 63~69 行 setFieldGrowthPolicy / setRowWrapPolicy / setLabelAlignment / setFormAlignment
// QtWidgets/qlayoutitem.h
//   第 24 行   class Q_WIDGETS_EXPORT QLayoutItem   ← **不是 QObject**！它没有 parent、
//                不能 qobject_cast；delete 它才是释放内存的正确动作
//   第 27~46 行 纯虚 sizeHint/minimumSize/maximumSize/expandingDirections/setGeometry/
//                geometry/isEmpty + virtual hasHeightForWidth/heightForWidth/
//                minimumHeightForWidth/invalidate + QWidget *widget() / QLayout *layout()
//                / QSpacerItem *spacerItem()（三个 downcast 钩子，返回 nullptr 表示「不是这种」）
//   第 57~64 行 class QSpacerItem : public QLayoutItem { QSpacerItem(int w,int h,
//                Policy hData = Minimum, Policy vData = Minimum) ... void changeSize(...) }
//   第 75~90 行 class QWidgetItem : public QLayoutItem（布局里代表一个控件的那个项）
// QtCore/qnamespace.h
//   Qt::Alignment 组合：AlignLeft / AlignRight / AlignHCenter / AlignJustify /
//                AlignTop / AlignBottom / AlignVCenter / AlignBaseline
//                （Alignment 是 QFlags，所以可以 AlignRight | AlignVCenter）
//   Qt::Orientations / Qt::Alignment 的默认值是 **0 = 空**，
//   空 alignment 在布局里的语义是「拉伸填满格子」，**不是**「左上对齐」——这是本讲第一大坑。

// ============================================================================
// 二、两个小 helper：把「控件」和「布局项」的几何统一成一行，方便肉眼对表
// ============================================================================
// 布局里的每一项都保证有 geometry()（qlayoutitem.h 第 35 行，纯虚且 const），
// 但只有 QWidgetItem 有 widget()，所以间隔项只能从**项本身**读几何——这正是
// 本讲「能看见 addSpacing/addStretch 到底占了多少地方」的技术前提。
static QString geoOf(const QWidget *w)
{
    const QRect g = w->geometry();     // qwidget.h 106 行的 geometry 属性（相对 parent 的坐标）
    return QStringLiteral("%1(x=%2,y=%3,%4x%5)")
        .arg(w->objectName().isEmpty() ? QString::fromUtf8(w->metaObject()->className())
                                       : w->objectName())
        .arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height());
}

// 【为什么参数是 QLayoutItem* 而不是 const QLayoutItem*】
//   核对 QtWidgets/qlayoutitem.h 第 42~44 行，Qt6 的三个探查函数 const 性**不一样**：
//     42 行  virtual QWidget *widget() const;      ← 只有这个是 const
//     43 行  virtual QLayout *layout();            ← 不是 const
//     44 行  virtual QSpacerItem *spacerItem();    ← 不是 const
//   （geometry() 是 const，35 行纯虚，所以上面照常读得到矩形。）
//   所以本函数**不能**声明成 const QLayoutItem*：一旦加了 const，
//   调 layout()/spacerItem() 就是「在 const 对象上调非 const 成员」，编译器直接报
//   passing 'const QLayoutItem' as 'this' argument discards qualifiers。
//   正确做法是把参数改成非 const 指针（调用点传的是 QLayout::itemAt(int)（98 行）
//   返回的 QLayoutItem*，本来就是非 const，不需要任何 const_cast）。
//   纪律：**别用 const_cast 糊过去**——那是在骗编译器，不是解决问题；
//   想要「只读」语义，就别在只读场合调这两个非 const 的探查函数。
static QString geoOfOrSpacer(QLayoutItem *it)
{
    if (!it) return QStringLiteral("(空项)");
    const QRect g = it->geometry();
    const QString kind = it->widget() ? QStringLiteral("控件项")
                       : it->spacerItem() ? QStringLiteral("间隔项")
                       : it->layout() ? QStringLiteral("子布局项") : QStringLiteral("未知项");
    return QStringLiteral("%1(x=%2,y=%3,%4x%5)")
        .arg(kind).arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height());
}

// ============================================================================
// 三、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // ── 舞台：一个「真实表单窗口」，用嵌套布局搭 ────────────────────
    // 结构（这就是工作里 90% 配置界面的形状）：
    //   root（QGridLayout，1 行 2 列）
    //     ├─ 左：form（QFormLayout）        ← 标签列 + 字段列
    //     └─ 右：side（QVBoxLayout）        ← 一堆按钮 + 末尾 addStretch 把它们顶到上面
    // 【为什么用嵌套而不是「一个布局装完」】
    //   单个布局的槽位是线性的（QBoxLayout）或网格的（QGridLayout），但
    //   「右边那一竖条」内部要独立分配纵向空间——交给 side 自己管，
    //   root 只知道「左边一大块、右边一小条」。分而治之，改一块不动另一块。
    QWidget window;
    window.setObjectName(QStringLiteral("formWindow"));
    window.setWindowTitle(QStringLiteral("第 7 讲 · 布局与 stretch/alignment 的证据"));
    window.resize(860, 460);

    auto *root = new QGridLayout(&window);          // qlayout.h：QLayout 构造收 parent widget
    root->setObjectName(QStringLiteral("root"));
    root->setContentsMargins(12, 12, 12, 12);       // 56 行（**不是** setMargin，Qt6 没那玩意）
    root->setHorizontalSpacing(20);                 // qgridlayout.h 36 行：横竖分开，比基类 setSpacing 细
    root->setVerticalSpacing(12);

    // 左：表单（QFormLayout）
    auto *form = new QFormLayout;                   // ⚠ 不传 parent：它将由 root->addLayout 接管
    form->setObjectName(QStringLiteral("form"));
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);  // 69 行：标签贴右、竖向居中
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);        // 63/67 行：整张表单在格子里贴左上
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);   // 34 行
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);                 // 41 行
    auto *editName  = new QLineEdit;  editName->setObjectName(QStringLiteral("editName"));
    auto *editMail  = new QLineEdit;  editMail->setObjectName(QStringLiteral("editMail"));
    auto *lblHint   = new QLabel(QStringLiteral("提示：QFormLayout 的字段列宽 = 最宽那个字段的 sizeHint"));
    lblHint->setObjectName(QStringLiteral("lblHint"));
    form->addRow(QStringLiteral("姓名:"), editName);      // 82 行 (const QString&, QWidget*)
    form->addRow(QStringLiteral("邮箱:"), editMail);
    form->addRow(lblHint);                                 // 84 行 单控件版：**跨两列**摆放
    // ⚠ 子布局绝对不能再有自己的 parent widget：
    //    如果你写 `new QFormLayout(&window)` 然后又 `root->addLayout(form)`，
    //    Qt 会警告（同一个布局同时属于两个父）——嵌套布局一律 `new QFormLayout`。

    // 右：侧栏竖排（QVBoxLayout）
    auto *side = new QVBoxLayout;
    side->setObjectName(QStringLiteral("side"));
    auto *btnOk     = new QPushButton(QStringLiteral("确定"));  btnOk->setObjectName(QStringLiteral("btnOk"));
    auto *btnApply  = new QPushButton(QStringLiteral("应用"));  btnApply->setObjectName(QStringLiteral("btnApply"));
    auto *btnCancel = new QPushButton(QStringLiteral("取消"));  btnCancel->setObjectName(QStringLiteral("btnCancel"));
    side->addWidget(btnOk);                             // qboxlayout.h 39 行 addWidget(w, stretch, alignment)
    side->addWidget(btnApply);
    side->addSpacing(16);                               // 35 行：固定 16px 间隔（不可伸缩）
    side->addWidget(btnCancel);
    side->addStretch(1);                                // 36 行：**可伸缩**间隔，因子 1
    // addStretch 的默认参数是 **0**！`addStretch()` 加的是一个「不会抢空间」的间隔，
    // 想要它真的把上面的按钮顶住，必须给非零因子：addStretch(1)。
    // 这条我见过至少二十遍：写了 addStretch() 却问「为什么按钮没沉到底」。

    root->addLayout(form, 0, 0);                        // qgridlayout.h 68 行 (QLayout*, row, col)
    root->addLayout(side, 0, 1);
    root->setColumnStretch(0, 3);                       // 43/44 行：列下标 + 因子，**不是** addWidget 的那个 stretch
    root->setColumnStretch(1, 1);
    // ★ 两种 stretch 的分工，背下来：
    //   · addWidget(w, stretch) / addLayout(l, stretch)：按**加入顺序**给这一项定因子；
    //   · setStretch(index, n)（QBoxLayout 54~57 行）/ setRowStretch/setColumnStretch
    //     （QGridLayout 43/44 行）：按**下标/行列号**改；
    //   · setStretchFactor(w, n)：按**对象**找一项再改（返回 bool，找不到 false）。
    //   多出来的空间按因子比例分。全 0 = 按比例不分，Qt 退化成「按 sizeHint 分」，
    //   这就是「我明明加了 stretch 却没反应」的第三种原因（前两种：addStretch() 忘了给因子、
    //   给错了控件的 stretch 却因为该项 sizePolicy 不 Expanding 而不吃空间）。

    // ── 证据 ① 四种 add* 的差别：往里塞的东西各不相同 ────────────────
    auto *bar = new QHBoxLayout;
    bar->setObjectName(QStringLiteral("bar"));
    auto *btnA = new QPushButton(QStringLiteral("A · stretch=2")); btnA->setObjectName(QStringLiteral("btnA"));
    auto *btnB = new QPushButton(QStringLiteral("B · stretch=1")); btnB->setObjectName(QStringLiteral("btnB"));
    auto *btnC = new QPushButton(QStringLiteral("C · 贴右边(AlignRight)")); btnC->setObjectName(QStringLiteral("btnC"));
    bar->addWidget(btnA, 2);                            // 39 行：因子 2，alignment 留空
    bar->addWidget(btnB, 1);
    bar->addSpacing(24);                                // 固定间隔：宽度恒 24
    bar->addStretch(1);                                 // 伸缩间隔：抢走剩下的空间
    bar->addWidget(btnC, 0, Qt::AlignRight);            // ★ 因子 0 + **对齐方式**=贴右
    root->addLayout(bar, 1, 0, 1, 2);                   // 68 行：跨 2 列（第 5 个参数是 columnSpan）
    qInfo() << "① root->count() =" << root->count() << "（布局里的**项数**，项 = QLayoutItem，不是控件）"
            << "bar->count() =" << bar->count();
    qInfo() << "① root->itemAtPosition(1,0)->layout() 就是 bar 吗 ="
            << (root->itemAtPosition(1, 0)->layout() == bar)
            << "；bar->itemAt(2)->spacerItem() 是间隔项（addSpacing 塞进来的）吗 ="
            << (bar->itemAt(2)->spacerItem() != nullptr);
    // ↑ 这一行是「addSpacing/addStretch 到底塞了什么」的直接证据：
    //   它们塞的是 **QSpacerItem**（qlayoutitem.h 57 行），
    //   addWidget 塞的是 QWidgetItem（75 行），addLayout 塞的就是那个子布局本身
    //   （因为 QLayout 同时继承 QLayoutItem，qlayout.h 26 行）。

    // ── 证据 ② sizeConstraint：布局反过来「命令」外层控件的尺寸 ──────
    // SizeConstraint 六档（qlayout.h 40~46 行）语义，按「布局对宿主 widget 提多大要求」排：
    //   SetDefaultConstraint  默认。有 sizeHint 的语义按 sizeHint 走（大多数情况=SetMinimumSize 的效果）
    //   SetNoConstraint       不提要求：宿主该多大多大，布局内部挤了也不管
    //   SetMinimumSize        宿主的 **minimumSize** 至少 = 布局的 minimumSize
    //   SetFixedSize         宿主的 **固定尺寸** = 布局的 sizeHint（弹窗里最爱用：内容多窄就是多窄）
    //   SetMaximumSize        宿主的 maximumSize = 布局的 sizeHint
    //   SetMinAndMaxSize      同时定最小与最大（= 上面两条叠加）
    qInfo() << "② root->sizeConstraint() 默认档位枚举值 =" << int(root->sizeConstraint())
            << "(0 = SetDefaultConstraint，qlayout.h 40~46 行的第一个值)";
    auto *dialogish = new QWidget;                       // 单独一个演示容器，不放进 window
    dialogish->setObjectName(QStringLiteral("dialogish"));
    auto *dl = new QVBoxLayout(dialogish);               // ← 传 parent 构造：布局立刻接管 dialogish
    auto *dlLong = new QLabel(QStringLiteral("这段文字很长很长很长很长很长很长很长很长很长"));
    dl->addWidget(dlLong);
    dialogish->resize(600, 400);                         // 先给个大尺寸
    dialogish->show();                                   // 让它真的参与布局谈判（offscreen 平台也会算）
    dl->setSizeConstraint(QLayout::SetFixedSize);         // 67 行
    QTimer::singleShot(400, [dialogish, dl] {
        qInfo() << "② SetFixedSize 之后 dialogish->size() =" << dialogish->size()
                << "；布局的 totalSizeHint() =" << dl->totalSizeHint()
                << "（qlayout.h 113 行）→ 宿主被布局**拽到内容尺寸**，"
                   "这就是「对话框随内容自动收缩」的实现方式，不用手写 adjustSize()";
        dl->setSizeConstraint(QLayout::SetNoConstraint);
        qInfo() << "② 换回 SetNoConstraint 之后 dialogish->size() =" << dialogish->size()
                << "（不再被拽，保持我们给的尺寸）";
        dialogish->hide();
        delete dialogish;        // ⚠ 删除容器 widget 会连带删掉它的布局与所有子控件——
                                 //   这是最省事、也最不会泄漏的删法（顺序见证据 ⑥）
    });

    window.show();
    qInfo() << "③ window 已 show()；root->activate() 返回 =" << root->activate()
            << "（qlayout.h 85 行：true 表示「算过了、还需要再算」——Qt 自己会在事件循环里补算，"
               "你几乎不需要手动调它）";

    // ============================================================================
    // 四、自动脚本：stretch vs alignment 的几何证据 + 所有权演示
    // ============================================================================
    // ── 证据 ④ 第一轮几何读数（860 宽）：stretch 与 alignment 的对照 ────
    QTimer::singleShot(320, [btnA, btnB, btnC, bar, editName, btnOk] {
        qInfo() << "\n—— 第一轮：window 860x460（列因子 3:1）——";
        qInfo() << "   editName（左侧表单的字段列）:" << geoOf(editName);
        qInfo() << "   btnOk（右侧竖排第一颗按钮）:" << geoOf(btnOk);
        qInfo() << "   bar（横排：A 因子 2、B 因子 1、固定 24、addStretch(1)、C 因子 0 且贴右）：";
        qInfo() << "   A:" << geoOf(btnA);
        qInfo() << "   B:" << geoOf(btnB);
        qInfo() << "   间隔项:" << geoOfOrSpacer(bar->itemAt(2));
        qInfo() << "   伸缩项:" << geoOfOrSpacer(bar->itemAt(3));
        qInfo() << "   C:" << geoOf(btnC);
        qInfo() << "   ★ 看点：A 宽 ≈ B 宽的 2 倍（stretch 2:1 生效）；"
                   "C 的宽度**等于它的 sizeHint**（因子 0，不抢空间），"
                   "但它的 x 靠到了格子右边（alignment 生效）。"
                   "→ **stretch 改宽度，alignment 只改「在格子里的站位」**。";
    });

    QTimer::singleShot(700, [&window, btnA, btnB, btnC] {
        qInfo() << "\n—— 第二轮：resize(1200, 460)，看谁变宽 ——";
        window.resize(1200, 460);          // qwidget.h 503 行
        qInfo() << "   （resize 之后立刻读还是旧值——布局在事件循环里重算，"
                   "所以下一行到 1000ms 才读）";
        (void)btnA; (void)btnB; (void)btnC;
    });

    QTimer::singleShot(1000, [btnA, btnB, btnC, bar, editName] {
        qInfo() << "  A:" << geoOf(btnA) << " B:" << geoOf(btnB) << " C:" << geoOf(btnC);
        qInfo() << "  editName（表单字段列，AllNonFixedFieldsGrow）:" << geoOf(editName);
        qInfo() << "  伸缩项:" << geoOfOrSpacer(bar->itemAt(3))
                << "→ 窗口变宽 340px，多出来的空间按**因子**分掉（A:B:伸缩项 = 2:1:1），"
                   "C 依旧只有 sizeHint 那么宽，只是站位贴右——**alignment 从不改变尺寸**（因子为 0 时）";
    });

    QTimer::singleShot(1300, [&window, btnA, btnC] {
        window.resize(560, 380);
        qInfo() << "\n—— 第三轮：缩到 560 宽（窄到极限）——";
        (void)btnA; (void)btnC;
    });

    QTimer::singleShot(1600, [bar, btnA, btnB, btnC, editName, editMail] {
        qInfo() << "  A:" << geoOf(btnA) << " B:" << geoOf(btnB) << " C:" << geoOf(btnC);
        qInfo() << "  editName:" << geoOf(editName) << " editMail:" << geoOf(editMail);
        qInfo() << "  ★ 看点：宽度已经降到**最小值**以下时，stretch 与 alignment 都不管用了——"
                   "这时候说话的是 minimumSizeHint / sizePolicy（第 8 讲），"
                   "以及 QFormLayout 的 RowWrapPolicy（41 行：WrapLongRows 会把字段挪到标签下面）。"
                   "新手「界面挤成一团」的真凶永远是这里，不是 stretch。";
        int l = 0, t = 0, r = 0, b = 0;
        bar->getContentsMargins(&l, &t, &r, &b);        // qlayout.h 58 行
        qInfo() << "  bar 的 contentsMargins =" << l << t << r << b
                << "spacing =" << bar->spacing()        // 53 行（未设时是 **默认值 -1/-2 语义：跟随样式**）
                << "（-1/负值表示「没设过，用样式默认」，这是 setSpacing 的哨兵值约定）";
    });

    // ── 证据 ⑤ setLayout 两次：Qt 会自己打警告，我们只负责读结果 ──────
    QTimer::singleShot(1900, [] {
        QWidget box;                       // 局部容器，出了作用域就析构，不留垃圾
        box.setObjectName(QStringLiteral("doubleLayoutBox"));
        auto *l1 = new QVBoxLayout;        // ⚠ 故意不传 parent：等下交给 setLayout 接管
        auto *l2 = new QHBoxLayout(&box);  // 这版**传了 parent**：构造即接管 box 的布局槽位
        box.setLayout(l1);                 // 第一次（其实 l2 已经占了）→ Qt 打 qWarning 并 return
        qInfo() << "⑤ 连着 setLayout / 已占用槽位之后：box.layout() 是 l2 吗 ="
                << (box.layout() == l2) << "，是 l1 吗 =" << (box.layout() == l1)
                << "→ l1 **既没被接管也没被删除**：这就是泄漏的那一条。"
                   "Qt 的 qWarning 内容大意是「widget 已经有布局了」（字符串在实现里，"
                   "本机头文件核不到原文，请以你跑出来的那条为准）。";
        // 修法：**只有一种**「给 widget 装布局」的正确写法组合：
        //   · 要么 `new QVBoxLayout(widget)`（构造即接管，之后别再 setLayout）
        //   · 要么 `widget->setLayout(l)`（l 必须还没被别的 widget 接管）
        // 两者混着写，就是本条警告的来源。
        box.setLayout(l2);                 // 再调一次：同样被拒（这次 l2 是同一个对象，无害但没意义）
        qInfo() << "⑤ 同一个布局重复 setLayout 也不会崩，但 Qt 会再打一条警告 → 日志里看到就该改代码";
    });

    // ── 证据 ⑥ takeAt / removeWidget / 删布局：三个「谁拥有谁」的问题 ──
    QTimer::singleShot(2300, [&window, bar, btnA, btnB, btnC] {
        qInfo() << "\n—— ⑥ 摘除与销毁 ——";
        qInfo() << "   摘之前：bar->count() =" << bar->count()
                << "，btnB->parentWidget() 存在吗 =" << (btnB->parentWidget() != nullptr);
        QLayoutItem *it = bar->takeAt(1);              // qlayout.h 99 行：取走**项**（index 1 = B）
        qInfo() << "   takeAt(1) 之后：count =" << bar->count()
                << "；it->widget() == btnB 吗 =" << (it->widget() == btnB)
                << "；btnB 还在 bar 的父窗口里可见吗 =" << btnB->isVisible()
                << "→ **takeAt 只把「项」摘掉，控件本身还在屏幕上、还是那个 parent 的子控件**！"
                   "这是本讲第一大所有权坑：摘完不管，界面上就留着一个「布局外飘着的」控件。";
        bar->removeWidget(btnB);                       // 91 行：此时它已经不在布局里，这条只是保险
        btnB->setParent(nullptr);                      // 交出 parent → 变成一个独立顶层窗口
        btnB->hide();
        qInfo() << "   setParent(nullptr)+hide() 之后 btnB->isVisible() =" << btnB->isVisible()
                << "（正确收尾：控件要么给新 parent，要么 deleteLater，要么 hide）";
        delete it;                                     // ★ 项（QLayoutItem）**必须自己删**：
                                                       //   它不是 QObject，Qt 不会替你回收
        qInfo() << "   现在 bar->count() =" << bar->count() << " 只剩 A/间隔/伸缩/C";

        // 演示「整条布局连同内容一起拆掉」的正确顺序
        QWidget *host = new QWidget(&window);
        host->setObjectName(QStringLiteral("throwawayHost"));
        auto *hl = new QVBoxLayout(host);
        auto *x1 = new QPushButton(QStringLiteral("子控件1"), host);
        auto *x2 = new QPushButton(QStringLiteral("子控件2"), host);
        hl->addWidget(x1); hl->addWidget(x2);
        qInfo() << "   拆 host 之前：hl->count() =" << hl->count()
                << "，host 的 children 里按钮在吗 =" << host->findChildren<QPushButton *>().size();
        delete host;      // ★ **只此一行**就够：析构 QWidget 会连带析构它的 layout()，
                          //   而 ~QLayout 会 delete 掉所有 QLayoutItem；子控件因为 parent 是 host，
                          //   也一起被删。**千万别先 delete host->layout() 再 delete host**
                          //   （那时 layout 已把 item 删干净，但控件还在——只是能跑，逻辑乱）。
        qInfo() << "   delete host 之后：x1/x2 已成为悬空指针，我们**不再访问**它们"
                   "（这段演示的是「删容器 = 删一切」，最省事也最不容易漏）；"
                   "如果只想撤布局、留控件：先 host->setLayout(nullptr)? —— 不行，"
                   "Qt 不给「卸掉布局」的公开接口，只能把控件逐个 removeWidget 再重新安排";
        (void)x1; (void)x2;   // 上面已 delete host，这两个指针只能这样「不再使用」
        (void)btnA; (void)btnC;
    });

    // ── 证据 ⑦ 结论 + 退出 ─────────────────────────────────────────
    QTimer::singleShot(2700, [bar, root, side] {
        qInfo() << "\n=== 结论 ===";
        qInfo() << "① addWidget→QWidgetItem、addLayout→子布局本身（QLayout 继承 QLayoutItem）、"
                   "addSpacing/addStretch→QSpacerItem；三种 add* 塞进去的东西**不是一个类型**。";
        qInfo() << "② addStretch() 默认因子 0（不会抢空间）；setColumnStretch/setStretch 按**下标**改因子，"
                   "setStretchFactor 按**对象**改且返回 bool。";
        qInfo() << "③ stretch 决定「多出来的空间怎么分」→ 改宽度；alignment 决定「格子里贴哪边」→ 只改站位。"
                   "空 alignment(=0) 的语义是「填满格子」，不是「左上对齐」。";
        qInfo() << "④ 挤成一团的根因不在布局参数，在 minimumSizeHint/sizePolicy（第 8 讲）。";
        qInfo() << "⑤ 一个 widget 只能有一个布局：setLayout 撞两次 Qt 会警告且不接管（泄漏）。";
        qInfo() << "⑥ takeAt 摘的是**项**，控件仍在屏幕上、仍需你安置；QLayoutItem 不是 QObject，要自己 delete；"
                   "删容器 widget 一行搞定一切。";
        qInfo() << "布局项数：root =" << root->count() << " bar =" << bar->count() << " side =" << side->count();
        QCoreApplication::quit();
    });

    return app.exec();
}

// ============================================================================
// 附：三种主布局的选型表 + 三条我修过的 bug
// ============================================================================
/*
 *  需求                              选择                       理由
 *  ---------------------------------------------------------------------------
 *  一横排/一竖排（工具条、按钮行）    QBoxLayout(H/V)            下标即顺序，addStretch 免费顶上
 *  多行多列、要对齐列宽              QGridLayout                setColumnStretch/setColumnMinimumWidth
 *  「标签: 字段」两列表单            QFormLayout                自动对齐标签列、跨列行、窄时 Wrap
 *  只在右上角放两个按钮              QDialogButtonBox           标准按钮位序随平台（第 5 讲）
 *  一整块内容需要滚动                QScrollArea + 里面一个容器  容器给 layout，滚动区管尺寸（第 3 讲）
 *
 * 三条真修过的 bug：
 *  ① 「按钮没沉到窗口底部」：写了 addStretch()（默认因子 0），等于加了个 0 因子间隔。
 *     改成 addStretch(1) 立刻好了。同类的还有 setStretch(3, 0) 想撤销 stretch，
 *     结果因子给成 0 之后该项完全不抢空间——stretch 是**比例**，不是开关。
 *  ② 「label 明明设了 AlignRight，可它就在左边」：那个 label 的 sizePolicy 是
 *     Preferred 且格子刚好和它一样宽 → alignment 无从生效。要么让它抢空间
 *     （stretch / Expanding，第 8 讲），要么外层再套一个布局。
 *     **alignment 只在「格子比控件大」时才看得见**，这条讲十遍都不嫌多。
 *  ③ 「切换标签页之后控件全浮在左上角」：他用了 takeAt() 摘项，没管摘出来的控件，
 *     控件还 parent 在旧容器上、可见、但不再受布局管——于是飘着。
 *     正解：takeAt 之后 delete item，然后对 item->widget() 做 setParent/删除/hide 三选一。
 *
 * ——— 老师傅的话 ———
 *   徒弟，布局这一层，你要换一套说法来想问题。别再说「这个按钮放在 300,120」，
 *   要说「这一行有三个格，因子 2:1:1；第三个格里的控件贴右」。
 *   位置是**谈出来的结果**，不是你写进去的输入。
 *
 *   你今天必须把三件事分开记牢：**尺寸**（sizeHint / minimumSizeHint）、
 *   **分配**（stretch 因子）、**站位**（alignment）。
 *   布局谈判依次问这三件事，一步管一步，谁也替不了谁。
 *   界面出问题时按这个顺序排查最快：
 *     先看控件是不是太小/太大 → 找尺寸那层（第 8 讲）；
 *     再看空间给了谁 → 找 stretch；
 *     最后看「空间给了它，它却没贴过去」→ 找 alignment。
 *
 *   还有一条纪律：一个 widget 一个布局，嵌套就 addLayout，别给同一个 widget
 *   装两次，也别给已经有布局的 widget 又 setLayout 一遍。Qt 给你打警告已经是客气，
 *   更多时候它是「静悄悄地把控件塞到错误的格子里」，那种 bug 你能查一整天。
 *
 * ——— 小练习 ———
 *   1. 把 bar 里 C 的 addWidget(btnC, 0, Qt::AlignRight) 改成 addWidget(btnC, 1)，
 *      再删掉 alignment，跑一遍对比 C 的 geometry：亲手确认
 *      「因子 1 = 宽度会长」「alignment = 只有站位」这两件事互不越界。
 *   2. 把 side 末尾的 addStretch(1) 删掉，再把 root 的 setColumnStretch(1,1)
 *      改成 (1,0)，观察右侧按钮列的宽高如何变化；然后试着用
 *      setRowWrapPolicy(QFormLayout::WrapLongRows)（qformlayout.h 41 行）
 *      把窗口缩窄，看字段挪到标签下面那一下。
 *   3. 故意制造一次泄漏：在证据 ⑤ 里 `new QVBoxLayout` 之后既不 setLayout
 *      也不 delete，然后用 qDebug 打印 box->layout()，把「谁接管了谁」的三种情况
 *      （构造传 parent / setLayout / addLayout）各写一行笔记。
 */
// ============================================================================
