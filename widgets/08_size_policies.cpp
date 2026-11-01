// ============================================================================
// 第 8 讲 · 尺寸谈判（sizeHint / minimumSizeHint / QSizePolicy / stretch）
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   第 7 讲教你「把空间怎么分」（stretch）与「贴哪边」（alignment），
//   但真正决定「一个控件能长多大、能缩多小」的是这一讲的三件事：
//     1) 控件自己报的两个尺寸：sizeHint（我希望这么大）与
//        minimumSizeHint（再小我就没法看了）；
//     2) QSizePolicy（尺寸政策，横竖各一份）：它由**四个位标志**拼出来——
//        GrowFlag=1 / ExpandFlag=2 / ShrinkFlag=4 / IgnoreFlag=8，
//        七个 Policy 就是这些位的组合（本讲把每个的整数值打出来给你看）；
//     3) 外部硬墙：setMinimumSize / setMaximumSize / setFixedSize
//        ——它们是**不可协商的下/上限**，优先级高于 sizeHint。
//   谈判的顺序，一句话背下来：
//     先按 min/max 夹住（硬墙），再用 sizePolicy 决定能不能缩/能不能长，
//     多出来的空间按 stretch 因子 + ExpandFlag 分，最后 alignment 管站位。
//   本讲**不靠肉眼**：每个控件在两轮 resize 之后的实际 geometry 全部 qInfo() 打印，
//   你不用打开窗口就能看出分配规律（在 offscreen 平台跑也照样出数）。
//   图解（一步步看谈判过程）：../visuals/layout_negotiation.html
//   （该页由「图解专区」维护；没生成前请先看本讲打印的表格与第 7 讲的嵌套结构。）
//
// 对应构建目标名：widgets_08_size_policies
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(widgets_08_size_policies 08_size_policies.cpp)
//   target_link_libraries(widgets_08_size_policies PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)   # 本文件里的 Probe 类**没有** Q_OBJECT（只是覆写虚函数），
//                           # 所以不需要 #include "08_....moc"
//
// 运行前须知：
//   · 依赖 Qt6::Widgets。Windows 上要能找到 platforms/qwindows.dll 或 qoffscreen.dll。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 widgets_08_size_policies，跑到自动退出、退出码 0）。
//     但本讲**没有汇总自检行**，退出码 0 只证明「没崩、走完了」，不证明正文每个数字都对：
//     带具体数字的「预期」请以你自己那次运行输出为准（本机那一轮存档 runlogs/widgets_08_size_policies.log）。
//   · 无人值守：QTimer::singleShot 排两轮 resize + 一次 hide()，
//     每一步读 geometry()/size() 并 qInfo() 打印，最后 QCoreApplication::quit()。
//     **没有任何阻塞调用**。
//   · 读数纪律（本讲成立的前提）：所有几何都在 QTimer 里读。
//     show() 之后、事件循环跑过一轮布局之前读到的都是草稿（第 1 讲、第 7 讲都说过）。
//   · Qt5 → Qt6 差异（本讲涉及，已核对本机头文件）：
//       - QSizePolicy 在 Qt6 是 **Q_GADGET**（QtWidgets/qsizepolicy.h 第 20 行 `Q_GADGET`），
//         不是 QObject：它能进 QVariant（第 91 行 `operator QVariant() const`），
//         但**不能 qobject_cast**、没有信号槽。
//       - Qt5 时代的 control bits 接口（setControlBit / controlBits / State / Flag /
//         Incentive / CacheFlags 那一整套）**在 Qt6 已不见**：本机 QSizePolicy 里只剩
//         ControlType 枚举（第 41~57 行）+ setControlType/controlType。
//         老代码 `sp.setControlBit(QSizePolicy::DefaultType)` 请改 `sp.setControlType(QSizePolicy::DefaultType)`。
//       - 老代码里的 `QSizePolicy::expand(int, int)` 在本机这个类里**找不到**，
//         请改用 Policy（Expanding / MinimumExpanding）表达意图。
//       - **QSizePolicy 的头文件位置**：本机它只在 **QtWidgets/qsizepolicy.h**
//         （我 `ls` 过：QtCore/qsizepolicy.h 不存在；QtWidgets/qlayoutitem.h 第 6 行
//         就是 `#include <QtWidgets/qsizepolicy.h>`）。
//         Qt5 时代它在 **QtCore**（属于 QtCore 库）。→ 从 Qt5 搬过来的工程，
//         写 `#include <QtCore/qsizepolicy.h>` 在新机器上会找不到文件；
//         用 `#include <QSizePolicy>` 或 `#include <QtWidgets/qsizepolicy.h>` 最稳，
//         并且它跟着 **Qt6::Widgets** 走，只连 Qt6::Core 是拿不到的。
// ============================================================================

#include <QApplication>           // QtWidgets/qapplication.h
#include <QCoreApplication>       // QtCore/qcoreapplication.h：static void quit()
#include <QWidget>                // QtWidgets/qwidget.h
#include <QSizePolicy>            // QtWidgets/qsizepolicy.h（⚠ 在 QtWidgets，不在 QtCore）
#include <QLayout>                // QtWidgets/qlayout.h
#include <QBoxLayout>             // QtWidgets/qboxlayout.h
#include <QPushButton>            // QtWidgets/qpushbutton.h
#include <QLabel>                 // QtWidgets/qlabel.h
#include <QTimer>
#include <QDebug>
#include <QString>

// ============================================================================
// 一、核对签名（本讲全部依据，路径相对 D:\Qt\6.12.0\mingw_64\include）
// ============================================================================
// QtWidgets/qsizepolicy.h
//   第 15~17 行 class Q_WIDGETS_EXPORT QSizePolicy { Q_GADGET
//   第 20~25 行 enum PolicyFlag { GrowFlag = 1, ExpandFlag = 2, ShrinkFlag = 4, IgnoreFlag = 8 };
//   第 27~36 行 enum Policy {
//                 Fixed            = 0,
//                 Minimum          = GrowFlag,                          // 1
//                 Maximum          = ShrinkFlag,                        // 4
//                 Preferred        = GrowFlag | ShrinkFlag,             // 5
//                 MinimumExpanding = GrowFlag | ExpandFlag,             // 3
//                 Expanding        = GrowFlag | ShrinkFlag | ExpandFlag,// 7
//                 Ignored          = ShrinkFlag | GrowFlag | IgnoreFlag  // 13
//               };   Q_ENUM(Policy)
//   第 33~49 行 enum ControlType { DefaultType=0x1, ButtonBox, CheckBox, ComboBox, Frame,
//                 GroupBox, Label, Line, LineEdit, PushButton, RadioButton, Slider,
//                 SpinBox, TabWidget, ToolButton }（+ Q_DECLARE_FLAGS(ControlTypes,...)）
//   第 54 行   constexpr QSizePolicy() noexcept : data(0) { }   ← ⚠ 默认构造是 **Fixed/Fixed**！
//   第 56 行   constexpr QSizePolicy(Policy h, Policy v, ControlType type = DefaultType)
//   第 61/62 行 constexpr Policy horizontalPolicy() const / verticalPolicy() const
//   第 66/67 行 constexpr void setHorizontalPolicy(Policy) / setVerticalPolicy(Policy)
//   第 68/63 行 void setControlType(ControlType) / ControlType controlType() const
//   第 72~75 行 constexpr Qt::Orientations expandingDirections() const  ← 按 ExpandFlag 判
//   第 77/78 行 constexpr void setHeightForWidth(bool) / bool hasHeightForWidth() const
//   第 79/80 行 constexpr void setWidthForHeight(bool) / bool hasWidthForHeight() const
//   第 91 行   operator QVariant() const;   ← 能塞进 QVariant（Q_GADGET 的福利）
//   第 93~96 行 horizontalStretch()/verticalStretch()/setHorizontalStretch(int)/setVerticalStretch(int)
//              ★ 勘误：旧稿写 89 / 91~94，整体偏 −2。本机 6.12.0：89 行是
//                `friend … size_t qHash(QSizePolicy key, size_t seed)`（夹在 86/87 的
//                operator== / != 后面），90 空行，91 才是 operator QVariant。
//                顺带一条真的坑：qHash 收的是**按值的 QSizePolicy key**，
//                所以「把 policy 当哈希键」在这套声明下是可行的，别自己另写一份。
//                ★ 这个 stretch 被 **qBound(0, n, 255)** 夹住：只有 8 位，超过 255 会**静悄悄截断**
//   第 96/97 行 constexpr bool retainSizeWhenHidden() const / setRetainSizeWhenHidden(bool)
//                ← 确实在（Qt 5.10+ 加的，Qt5 老版本没有；hidden 时仍占位）
//   第 99/100 行 transpose() / transposed()
//   ※ 位域结构 Bits（第 128 行起）：horStretch:8 / verStretch:8 / horPolicy / verPolicy /
//     retainSizeWhenHidden / hfw / wfh … —— 所以一个 QSizePolicy 就是 32 位整数，可以 == 比较
// QtWidgets/qlayoutitem.h
//   第 18 行   inline constexpr int QLAYOUTSIZE_MAX = INT_MAX/256/16
//              ← 「16777215」这个奇怪的巨大上限就是从这来的（防止乘法溢出）。
//                setMaximumHeight(16777215) 这种写法你已经见过吧？它就是 QLAYOUTSIZE_MAX 的意思。
// QtWidgets/qwidget.h
//   第 119 行  Q_PROPERTY(QSizePolicy sizePolicy READ sizePolicy WRITE setSizePolicy)
//   第 120/121 行 Q_PROPERTY(QSize minimumSize READ minimumSize WRITE setMinimumSize)
//                Q_PROPERTY(QSize maximumSize READ maximumSize WRITE setMaximumSize)
//   第 130 行  Q_PROPERTY(QSize sizeIncrement READ sizeIncrement WRITE setSizeIncrement)
//   第 148/149 行 Q_PROPERTY(QSize sizeHint READ sizeHint) / (QSize minimumSizeHint READ minimumSizeHint)
//                ← 注意**只有 READ、没有 WRITE**：这两个是「问出来的」，不能像 min/max 那样设
//   第 258 行  QSize size() const;               第 256 行 QPoint pos() const;
//   第 265/266 行 QSize minimumSize() const; QSize maximumSize() const;   ← 公开可读，本讲靠它出证据
//   第 271/272 行 void setMinimumSize(const QSize&); void setMinimumSize(int w, int h);
//   第 273/274 行 void setMaximumSize(const QSize&); void setMaximumSize(int w, int h);
//   第 275/276 行 void setMinimumWidth(int); setMinimumHeight(int);
//   第 277/278 行 void setMaximumWidth(int); setMaximumHeight(int);
//   第 285/286 行 setSizeIncrement(const QSize&) / (int, int)   ← 用户拖动窗口时的「步进」，冷门
//   第 288/289 行 setBaseSize(...)                              ← 和 sizeIncrement 配套，冷门
//   第 291/292 行 void setFixedSize(const QSize&); void setFixedSize(int w, int h);
//                ★ setFixedSize 就是把 minimum 与 maximum **同时**设成同一个值，
//                  它**不**修改 sizeHint，也**不**改 sizePolicy（本讲打印 min/max 证明）
//   第 503 行  void resize(int w, int h);
//   第 509 行  void adjustSize();          ← 「按 sizeHint 自我尺寸化」，写死尺寸时才用
//   第 522/523 行 virtual QSize sizeHint() const; virtual QSize minimumSizeHint() const;
//                ← 它们是 **protected virtual**（这就是本讲为什么要定义 Probe 子类来读）
//   第 525 行  QSizePolicy sizePolicy() const;
//   第 526 行  void setSizePolicy(QSizePolicy);
//   第 527 行  inline void setSizePolicy(QSizePolicy::Policy horizontal, QSizePolicy::Policy vertical);
//                ← 「两种重载」：给一个对象，或横竖各给一个 Policy。后者更常用
//   第 528/529 行 virtual int heightForWidth(int) const; virtual bool hasHeightForWidth() const;
//   第 542 行  void updateGeometry();      ← 你覆写了 hint 之后**必须**喊它，布局才会重算
//   第 641 行  bool autoFillBackground() const;（配 173 行的属性）——本讲用它让色块看得见
//   QtWidgets/qframe.h / qlabel.h：QLabel 的文字换行会改 minimumSizeHint（第 1 讲 80 行提到过）
// QtCore/qnamespace.h：Qt::Orientations（Qt::Horizontal=0x01 / Qt::Vertical=0x02，第 72 行附近的
//                enum Orientation），expandingDirections() 返回的就是它

// ============================================================================
// 二、Probe：把 protected 的 sizeHint / minimumSizeHint 变成「可读 + 可编」
// ============================================================================
// 【为什么要这个子类，两个理由】
//   ① QWidget::sizeHint()/minimumSizeHint() 是 **protected virtual**（qwidget.h 522/523 行），
//      外面根本调不到。想让打印出「控件自己报的偏好」，只能子类化后在 public 段
//      重新声明访问权（C++ 允许子类用 using/包装函数放宽继承成员的访问级别）。
//   ② 谈判的输入就是这两个函数：把它们做成**可控的旋钮**，
//      才能把「policy / min-max / stretch 各自改变了什么」讲成可复现的实验。
class Probe : public QPushButton
{
public:
    explicit Probe(const QString &name, QWidget *parent = nullptr)
        : QPushButton(name, parent) {}

    // 旋钮：直接指定「我报出去的尺寸」。真实控件里这两个值是 Qt 从字体、
    // 文字长度、图标、frame 宽度等等**算**出来的（第 10 讲会看到中文/德文长度差异的来源）。
    void setHints(const QSize &hint, const QSize &minHint)
    {
        m_hint = hint;
        m_minHint = minHint;
        updateGeometry();          // qwidget.h 542 行：通知布局「我的偏好变了，重谈」
    }

    QSize hint() const { return sizeHint(); }          // ← 把 protected 成员从 public 口递出去
    QSize minHint() const { return minimumSizeHint(); }

    // heightForWidth：让「高度取决于当前宽度」这件事可以被观察到（第 128/129 行虚函数）
    void setHfw(bool on)
    {
        m_hfw = on;
        QSizePolicy sp = sizePolicy();      // 525 行：先读回现有 policy 再改，别整个覆盖
        sp.setHeightForWidth(on);           // qsizepolicy.h 77 行
        setSizePolicy(sp);                  // 526 行
        updateGeometry();
    }
    bool hasHeightForWidth() const override { return m_hfw; }          // 529 行
    int  heightForWidth(int w) const override { return m_hfw ? qMax(24, w / 4) : 0; } // 528 行

protected:
    QSize sizeHint() const override { return m_hint; }                 // 522 行
    QSize minimumSizeHint() const override { return m_minHint; }        // 523 行

private:
    QSize m_hint{160, 40};
    QSize m_minHint{40, 20};
    bool m_hfw = false;
};

// 一行打印「控件自己报的偏好」——因为 sizeHint 是 protected，只有 Probe 这种子类递得出来
static QString hintsOf(const Probe *p)
{
    return QStringLiteral("%1x%2（底线 %3x%4）")
        .arg(p->hint().width()).arg(p->hint().height())
        .arg(p->minHint().width()).arg(p->minHint().height());
}

// ============================================================================
// 三、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QWidget window;
    window.setObjectName(QStringLiteral("sizeLab"));
    window.setWindowTitle(QStringLiteral("第 8 讲 · 尺寸谈判实验室"));
    window.resize(760, 520);

    auto *root = new QVBoxLayout(&window);
    root->setContentsMargins(10, 10, 10, 10);

    // ── 证据 ① 七个 Policy 的整数含义（位标志一览，跑起来先看这张表）───
    qInfo() << "① PolicyFlag：GrowFlag =" << int(QSizePolicy::GrowFlag)
            << "ExpandFlag =" << int(QSizePolicy::ExpandFlag)
            << "ShrinkFlag =" << int(QSizePolicy::ShrinkFlag)
            << "IgnoreFlag =" << int(QSizePolicy::IgnoreFlag);
    qInfo() << "① Policy 位组合：Fixed =" << int(QSizePolicy::Fixed)
            << " Minimum =" << int(QSizePolicy::Minimum)
            << " Maximum =" << int(QSizePolicy::Maximum)
            << " Preferred =" << int(QSizePolicy::Preferred)
            << " MinimumExpanding =" << int(QSizePolicy::MinimumExpanding)
            << " Expanding =" << int(QSizePolicy::Expanding)
            << " Ignored =" << int(QSizePolicy::Ignored);
    // 读法（这是本讲最值钱的一句）：
    //   GrowFlag   = 允许长到 **超过** sizeHint
    //   ShrinkFlag = 允许缩到 **小于** sizeHint（缩到 minimumSizeHint 为止）
    //   ExpandFlag = 「愿意」吃掉一切多余空间（参与 stretch 分配）
    //   IgnoreFlag = 干脆**无视** sizeHint（当它是 0）
    //   Fixed(0)      两向都不许动：sizeHint 就是最终值（硬按 hint，连 min/max 都不给弹性）
    //   Minimum(1)    至少 sizeHint，可以变大，**不许变小**
    //   Maximum(4)    至多 sizeHint，可以变小，**不许变大**
    //   Preferred(5)  最好是 sizeHint，可大可小（默认的大多数控件）
    //   MinimumExpanding(3) 至少 sizeHint 且愿意长（= Minimum + ExpandFlag，长之前不会缩）
    //   Expanding(7)  可大可小且愿意长（滚动区/文本编辑的垂直方向常见这档）
    //   Ignored(13)   无视 hint：给多少空间用多少
    qInfo() << "① 默认构造 QSizePolicy() 的横竖档分别 ="
            << int(QSizePolicy().horizontalPolicy()) << int(QSizePolicy().verticalPolicy())
            << "→ 都是 0 = **Fixed**！所以「新构造一个 policy 对象只改一边」是陷阱，"
               "必须先 sizePolicy() 读回来再改（本讲 setHfw 就是这么写的）";
    // 出厂政策读得到（sizePolicy() 是公开函数，qwidget.h 525 行）——顺便记住：
    // 按钮的垂直方向通常是 Fixed，这就是「按钮不会被纵向拉高」的根因。
    {
        QPushButton stock;          // 只为读它的出厂 policy，构造即析构（QApplication 已存在，合法）
        qInfo() << "① QPushButton 出厂 policy h/v =" << int(stock.sizePolicy().horizontalPolicy())
                << int(stock.sizePolicy().verticalPolicy())
                << "（1=Minimum / 0=Fixed；含义见上面的位标志表；以打印为准）";
    }

    // ── 实验 A：同一行五个格，五个不同水平 Policy，观察谁被拉长 ──────
    auto *rowA = new QHBoxLayout;
    auto *pFix  = new Probe(QStringLiteral("Fixed"),     &window); pFix->setObjectName(QStringLiteral("pFix"));
    auto *pMin  = new Probe(QStringLiteral("Minimum"),   &window); pMin->setObjectName(QStringLiteral("pMin"));
    auto *pMax  = new Probe(QStringLiteral("Maximum"),   &window); pMax->setObjectName(QStringLiteral("pMax"));
    auto *pPref = new Probe(QStringLiteral("Preferred"), &window); pPref->setObjectName(QStringLiteral("pPref"));
    auto *pExp  = new Probe(QStringLiteral("Expanding"), &window); pExp->setObjectName(QStringLiteral("pExp"));
    auto *pIgn  = new Probe(QStringLiteral("Ignored"),   &window); pIgn->setObjectName(QStringLiteral("pIgn"));
    for (Probe *p : {pFix, pMin, pMax, pPref, pExp, pIgn}) {
        p->setHints(QSize(120, 36), QSize(30, 16));      // 六个旋钮完全一致，只有 policy 不同
        p->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);  // 先归零：竖向我不管
    }
    pFix->setSizePolicy(QSizePolicy::Fixed,             QSizePolicy::Preferred);
    pMin->setSizePolicy(QSizePolicy::Minimum,           QSizePolicy::Preferred);
    pMax->setSizePolicy(QSizePolicy::Maximum,           QSizePolicy::Preferred);
    pPref->setSizePolicy(QSizePolicy::Preferred,        QSizePolicy::Preferred);
    pExp->setSizePolicy(QSizePolicy::Expanding,         QSizePolicy::Preferred);
    pIgn->setSizePolicy(QSizePolicy::Ignored,           QSizePolicy::Preferred);
    rowA->addWidget(pFix); rowA->addWidget(pMin); rowA->addWidget(pMax);
    rowA->addWidget(pPref); rowA->addWidget(pExp); rowA->addWidget(pIgn);
    root->addLayout(rowA);
    qInfo() << "A 组六兄弟报的 hint 完全相同（120x36 / min 30x16），只有 policy 不同。"
            << "pExp 的 expandingDirections 有横向吗 ="
            << (pExp->sizePolicy().expandingDirections().testFlag(Qt::Horizontal));
            // qsizepolicy.h 72~75 行：只有带 ExpandFlag 的档才会报对应方向

    // ── 实验 B：硬墙 vs sizeHint 的优先级 ───────────────────────────
    auto *rowB = new QHBoxLayout;
    auto *bMin = new Probe(QStringLiteral("setMinimumSize(200,60)"), &window);
    auto *bMax = new Probe(QStringLiteral("setMaximumWidth(70)"), &window);
    auto *bFix = new Probe(QStringLiteral("setFixedSize(90,28)"), &window);
    bMin->setObjectName(QStringLiteral("bMin")); bMax->setObjectName(QStringLiteral("bMax"));
    bFix->setObjectName(QStringLiteral("bFix"));
    for (Probe *p : {bMin, bMax, bFix}) p->setHints(QSize(120, 36), QSize(30, 16));
    bMin->setMinimumSize(200, 60);        // 271 行的 (int,int) 版
    bMax->setMaximumWidth(70);            // 277 行
    bFix->setFixedSize(90, 28);           // 292 行：min 与 max 同时锁死
    // 三颗都给「愿意长」的政策，好让硬墙成为**唯一**能拦住伸展的东西
    bMin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bMax->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bFix->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    rowB->addWidget(bMin); rowB->addWidget(bMax); rowB->addWidget(bFix);
    root->addLayout(rowB);
    qInfo() << "B 组：三颗都是 Expanding（想长），但被 min/max 夹住 —— "
            << "bFix->minimumSize() =" << bFix->minimumSize()
            << "maximumSize() =" << bFix->maximumSize()
            << "（265/266 行：证明 setFixedSize 只动了这两堵墙，**没动** sizeHint）";

    // ── 实验 C：stretch 因子与 ExpandFlag 的分工 ────────────────────
    auto *rowC = new QHBoxLayout;
    auto *cA = new Probe(QStringLiteral("因子 1"), &window); cA->setObjectName(QStringLiteral("cA"));
    auto *cB = new Probe(QStringLiteral("因子 3"), &window); cB->setObjectName(QStringLiteral("cB"));
    auto *cC = new Probe(QStringLiteral("因子 3 但 Minimum(不会 Expand)"), &window);
    cC->setObjectName(QStringLiteral("cC"));
    for (Probe *p : {cA, cB, cC}) p->setHints(QSize(100, 32), QSize(20, 16));
    cA->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    cB->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    cC->setSizePolicy(QSizePolicy::Minimum,   QSizePolicy::Preferred);  // 没有 ExpandFlag
    rowC->addWidget(cA, 1);                     // qboxlayout.h 39 行：第 2 个参数 = stretch 因子
    rowC->addWidget(cB, 3);
    rowC->addWidget(cC, 3);                     // ★ 因子给了 3，但它的 policy 没有 ExpandFlag
    root->addLayout(rowC);
    qInfo() << "C 组：cC 拿到和 cB 一样的因子 3，**却不会长成 3 倍**——"
               "「因子」只在「该项愿意吃空间（ExpandFlag）」时才发挥作用；"
               "Minimum/Maximum/Fixed 这些档不参与分配（第 7 讲的坑在这里闭环）";

    // ── 实验 D：retainSizeWhenHidden（隐藏了还要占位）───────────────
    auto *rowD = new QHBoxLayout;
    auto *dGone1 = new Probe(QStringLiteral("D1 普通（隐藏就不占位）"), &window);
    auto *dGone2 = new Probe(QStringLiteral("D2 retainSizeWhenHidden"), &window);
    dGone1->setObjectName(QStringLiteral("dGone1")); dGone2->setObjectName(QStringLiteral("dGone2"));
    auto *dTail = new Probe(QStringLiteral("尾巴"), &window); dTail->setObjectName(QStringLiteral("dTail"));
    for (Probe *p : {dGone1, dGone2, dTail}) p->setHints(QSize(150, 34), QSize(40, 16));
    {
        QSizePolicy sp = dGone2->sizePolicy();       // 525 行读回来
        sp.setRetainSizeWhenHidden(true);            // qsizepolicy.h 97 行（确实存在，Qt 5.10+）
        dGone2->setSizePolicy(sp);
    }
    qInfo() << "D 组：dGone2 的 retainSizeWhenHidden() 读回 ="
            << dGone2->sizePolicy().retainSizeWhenHidden()      // 96 行
            << "；dGone1 =" << dGone1->sizePolicy().retainSizeWhenHidden();
    rowD->addWidget(dGone1); rowD->addWidget(dGone2); rowD->addWidget(dTail);
    root->addLayout(rowD);

    // ── 实验 E：hasHeightForWidth —— 高度随宽度变（图文混排的本质）───
    auto *hfw = new Probe(QStringLiteral("E · heightForWidth = width/4"), &window);
    hfw->setObjectName(QStringLiteral("hfw"));
    hfw->setHints(QSize(300, 75), QSize(80, 24));
    hfw->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    hfw->setHfw(true);
    root->addWidget(hfw);
    qInfo() << "E 组：hfw->hasHeightForWidth() =" << hfw->hasHeightForWidth()
            << "（529 行的虚函数被我们覆写）；sizePolicy 里的 hfw 位 ="
            << hfw->sizePolicy().hasHeightForWidth()            // qsizepolicy.h 78 行
            << "；heightForWidth(400) 手算 =" << hfw->heightForWidth(400);
    // 【什么时候真的用得上】富文本/换行 label：宽度定了行数就定了，高度必须由宽度算出来。
    // 不用 hfw 的话，Qt 只会拿 sizeHint 的固定高度，出现「文字被截一半」的老 bug。
    // QLabel 的换行在实现里就走这条路（本讲不去依赖它的私有实现，只演示机制）。

    root->addStretch(1);   // 36 行：末尾一根 1 因子弹簧，把上面几组顶到窗口上沿
    window.show();

    // ============================================================================
    // 四、自动脚本：两轮 resize + 一次隐藏，全部用打印当证据
    // ============================================================================
    auto dumpAll = [](const char *phase, Probe *a, Probe *b, Probe *c, Probe *d, Probe *e, Probe *f,
                      Probe *bMin, Probe *bMax, Probe *bFix,
                      Probe *cA, Probe *cB, Probe *cC,
                      Probe *d1, Probe *d2, Probe *dt, Probe *hfw) {
        qInfo() << "\n———" << phase << "———";
        qInfo() << "A 组（六兄弟报的 hint 全一样，只有水平 policy 不同）：";
        qInfo() << "   它们报的 sizeHint/底线 =" << hintsOf(a) << "（policy 分别 0/1/4/5/7/13）";
        qInfo() << "   Fixed    :" << a->width() << "x" << a->height();
        qInfo() << "   Minimum  :" << b->width() << "x" << b->height();
        qInfo() << "   Maximum  :" << c->width() << "x" << c->height();
        qInfo() << "   Preferred:" << d->width() << "x" << d->height();
        qInfo() << "   Expanding:" << e->width() << "x" << e->height();
        qInfo() << "   Ignored  :" << f->width() << "x" << f->height();
        qInfo() << "B 组（都想长，被硬墙夹住）：";
        qInfo() << "   min(200,60) :" << bMin->width() << "x" << bMin->height()
                << " / max width 70 :" << bMax->width() << "x" << bMax->height()
                << " / fixed 90x28 :" << bFix->width() << "x" << bFix->height();
        qInfo() << "C 组（因子 1 / 3 / 3，其中 cC 没有 ExpandFlag）：";
        qInfo() << "   cA :" << cA->width() << " cB :" << cB->width() << " cC :" << cC->width();
        qInfo() << "D 组（隐藏占位对照）：d1 visible =" << d1->isVisible()
                << "几何 " << d1->geometry() << " | d2 visible =" << d2->isVisible()
                << "几何 " << d2->geometry() << " | 尾巴几何 " << dt->geometry();
        qInfo() << "E 组（hfw）：" << hfw->width() << "x" << hfw->height()
                << "；此刻 heightForWidth(宽) 应 =" << hfw->heightForWidth(hfw->width());
    };

    QTimer::singleShot(300, [&window, &dumpAll, pFix, pMin, pMax, pPref, pExp, pIgn,
                             bMin, bMax, bFix, cA, cB, cC, dGone1, dGone2, dTail, hfw] {
        qInfo() << "第一轮：窗口" << window.size();
        dumpAll("第一轮 760x520", pFix, pMin, pMax, pPref, pExp, pIgn,
                bMin, bMax, bFix, cA, cB, cC, dGone1, dGone2, dTail, hfw);
        qInfo() << "★ 第一轮看点：Ignored 与 Expanding 谁更「贪」；Maximum 会不会比 hint 还窄；"
                   "hfw 的高度是不是 ≈ 宽度/4。";
    });

    QTimer::singleShot(700, [&window] {
        window.resize(1100, 520);              // 只加宽 340px
        qInfo() << "第二轮：resize(1100,520) —— 多出来的宽度会流进哪些格子？";
    });

    QTimer::singleShot(1000, [&window, &dumpAll, pFix, pMin, pMax, pPref, pExp, pIgn,
                              bMin, bMax, bFix, cA, cB, cC, dGone1, dGone2, dTail, hfw] {
        qInfo() << "窗口现在" << window.size();
        dumpAll("第二轮 1100x520", pFix, pMin, pMax, pPref, pExp, pIgn,
                bMin, bMax, bFix, cA, cB, cC, dGone1, dGone2, dTail, hfw);
        qInfo() << "★ 对照第一轮：只有带 ExpandFlag 的档（Expanding / MinimumExpanding）"
                   "和 Ignored 在变宽；Fixed/Maximum 一动不动；"
                   "cA:cB 的宽度差 ≈ 1:3 的**增量**分配（不是最终宽度 1:3，"
                   "因为每格先扣掉自己的 sizeHint，剩下的才按比例分——这条最容易被误解）";
    });

    QTimer::singleShot(1300, [dGone1, dGone2, dTail] {
        dGone1->hide();
        dGone2->hide();
        qInfo() << "D 组：把 D1/D2 都 hide() ——（下一行 1600ms 读，hide 也要过一轮布局）";
        (void)dTail;
    });

    QTimer::singleShot(1600, [dGone1, dGone2, dTail, pFix] {
        qInfo() << "\n——— 隐藏之后（第三轮）———";
        qInfo() << "   D1（普通）：isVisible =" << dGone1->isVisible()
                << " 它的格子被**回收**了（尾巴 dTail 的 x 会往左移）";
        qInfo() << "   D2（retainSizeWhenHidden=true）：isVisible =" << dGone2->isVisible()
                << " 但尾巴的位置不受它影响 → 布局仍为它**留着位子**";
        qInfo() << "   dTail 几何 =" << dTail->geometry() << "；A 组 Fixed 仍 ="
                << pFix->width() << "x" << pFix->height();
        qInfo() << "★ 这条属性的用武之地：工具栏上「点了才出现」的按钮，"
                   "没有它一隐藏整排按钮就跳一下（用户最烦的抖动）。"
                   "它是 QSizePolicy 的属性，不是 QWidget 的属性（qsizepolicy.h 96/97 行）。";
        dGone1->show();
        dGone2->show();
    });

    QTimer::singleShot(1900, [&window, pIgn, pExp] {
        window.resize(300, 300);                // 缩到比「所有 minimumSizeHint 加起来」还小
        qInfo() << "第四轮：resize(300,300) → 窗口能不能真变成 300x300，"
                   "答案由**各控件 minimumSizeHint 之和**决定（不是 sizeHint）";
        (void)pIgn; (void)pExp;
    });

    QTimer::singleShot(2200, [&window, pFix, bFix] {
        qInfo() << "\n——— 第四轮实测 ———";
        qInfo() << "   window.size() =" << window.size()
                << "（如果宽 >300，说明 Qt 把窗口**顶住**在最小尺寸上了：谈判结果是 min 之和）";
        qInfo() << "   此时 pFix(Fixed) 仍 =" << pFix->width() << "x" << pFix->height()
                << "，bFix(setFixedSize) 仍 =" << bFix->width() << "x" << bFix->height()
                << " → 硬墙与 Fixed 在极端压缩下**绝不退让**，被压的是可缩的那些（Preferred/Expanding）";
    });

    QTimer::singleShot(2500, [] {
        qInfo() << "\n=== 结论 ===";
        qInfo() << "① Policy 是四个位标志的组合：Grow(能超过 hint) / Shrink(能低于 hint) / Expand(愿意吃余量) / Ignore(无视 hint)。";
        qInfo() << "② sizeHint 是「偏好」，minimumSizeHint 是「底线」，setMinimum/Maximum/FixedSize 是「硬墙」——硬墙优先。";
        qInfo() << "③ stretch 因子只在有 ExpandFlag 的项之间分配**扣掉 hint 之后的余量**；给 Minimum 项大因子是无效的。";
        qInfo() << "④ 空 alignment(0) = 填满格子；alignment 只在格子比控件大时才看得出。";
        qInfo() << "⑤ retainSizeWhenHidden 让隐藏控件继续占位；heightForWidth 让高度由宽度推导。";
        qInfo() << "⑥ 默认构造的 QSizePolicy 是 Fixed/Fixed，改单边前一定先 sizePolicy() 读回来。";
        qInfo() << "⑦ 想读 protected 的 sizeHint：子类化（本讲 Probe）；改完 hint 记得 updateGeometry()（qwidget.h 542 行）。";
        QCoreApplication::quit();
    });

    return app.exec();
}

// ============================================================================
// 附：一张「谁说话算数」的优先级表 + 三条我修过的 bug
// ============================================================================
/*
 *  同一个方向上，Qt 的取值顺序（从硬到软）：
 *    setMinimumSize/setFixedSize(下界) ≤ 最终尺寸 ≤ setMaximumSize/setFixedSize(上界)
 *      ↑ 这堵墙由 QWidget 的 min/max 决定，任何布局都不能越过去
 *    然后：能否小于 sizeHint —— 看 ShrinkFlag；能否大于 sizeHint —— 看 GrowFlag
 *    再然后：多余空间怎么分 —— 看 ExpandFlag（有份的才参与）+ 布局的 stretch 因子
 *    最后：格子比控件大时贴哪边 —— 看 alignment（第 7 讲）
 *    窗口本身能不能缩 —— 各控件 minimumSizeHint（不是 sizeHint！）经布局累加
 *
 * 三条真修过的 bug：
 *  ① 「文本框拉不动，可它明明在 Expanding 布局里」：那颗控件被 setMaximumWidth(200)
 *     过（老代码里为了对齐手工加的）。硬墙永远赢。查这类 bug 先打 maximumSize()。
 *  ② 「一个 QScrollArea 里的 widget 把窗口撑到屏幕外」：滚动区给了内容控件
 *     「无限宽」的暗示，而内容控件的 policy 是 Expanding 且没有 minimum/hfw，
 *     于是尺寸谈判发散。解法：给内容一个 QGridLayout 的 SetFixedSize（第 7 讲 sizeConstraint）
 *     或者把水平 policy 改成 Preferred（Grow 但不 Ignore）。
 *  ③ 「改了 sizeHint 界面不重算」：覆写 sizeHint() 之后没调 updateGeometry()
 *     （qwidget.h 542 行），布局一直用缓存的旧值。这是「自定义控件」最常见的
 *     一条：**报出去的尺寸变了要喊一声**。
 *
 * 顺带一条冷知识（本讲开头打印的 QLAYOUTSIZE_MAX）：
 *   qlayoutitem.h 第 18 行 `QLAYOUTSIZE_MAX = INT_MAX/256/16`，
 *   所以你写 setMaximumHeight(16777215) 时其实是在写「不设上限」，
 *   而写 99999999 会被夹到这个值——布局内部的乘法都是按这个上限防溢出的。
 *
 * ——— 老师傅的话 ———
 *   徒弟，布局是「分钱」，尺寸政策是「谁能挣钱、谁能少花」。
 *   你今天要把一句话刻住：**Qt 里没有任何一个尺寸是你写下去的，全是谈出来的。**
 *   你写的只是四样东西：偏好（sizeHint）、底线（minimumSizeHint）、
 *   脾气（QSizePolicy 的四个位）、硬墙（min/max）。剩下的是布局在替你和窗口谈。
 *
 *   所以调界面的时候别「凭感觉加 setFixedSize」。每加一堵硬墙，
 *   你的界面就多一分「换个字体、换成中文、用户调大 DPI 就崩」的风险——
 *   这三件事第 9、10 讲都会来收账。真需要死尺寸（图标按钮、进度条高度）再钉，
 *   钉的时候在注释里写清为什么。
 *
 *   还有一条：本讲为什么全部用打印？因为「肉眼看看差不多」是最坏的工作方法。
 *   你把 760 和 1100 两轮的宽度并排一看，1:3 因子到底是按比例还是按最终宽度，
 *   立刻就有答案。以后你写自定义控件，也要给自己留这种「能读数的口」
 *   （本讲那个 Probe 就是这个用途），比盯着屏幕猜快十倍。
 *   想看这套谈判的分步动画：../visuals/layout_negotiation.html。
 *
 * ——— 小练习 ———
 *   1. 把 pMax（Maximum 档）的 hint 从 120x36 改成 300x60，重跑两轮 resize，
 *      对照打印确认「Maximum 可以缩、永远不能长过 hint」；再把它的
 *      minimumSizeHint 改成 250x50，看它会不会突破 hint 变宽（答案在位标志里）。
 *   2. 给 cC 补上 ExpandFlag（改成 QSizePolicy::Expanding），重跑并对比 cB/cC 宽度，
 *      验证「因子只在有 ExpandFlag 时参与分配」这条结论；然后把 cA 的因子改成 0，
 *      看余量如何在剩下的项之间重新分。
 *   3. 把 hfw->setHfw(true) 关掉，重跑并看它的高度是否在两轮 resize 里纹丝不动；
 *      再把它换成 QLabel + setWordWrap(true)（qlabel.h 第 28 行的属性），
 *      观察 Qt 自带的换行标签在高 DPI 与中文下如何随宽度变高——
 *      这就是 heightForWidth 在真实控件里的样子。
 */
// ============================================================================
