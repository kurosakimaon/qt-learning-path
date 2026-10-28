// ============================================================================
// 第 9 讲 · Qt 样式表（QSS）：选择器、盒模型与一键换肤
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   1) 五种选择器各自的匹配范围到底差在哪：
//        类型  QPushButton            —— 「是这个类 **或它的子类**」
//        类    .QPushButton           —— 「**正好**是这个类」（子类不命中）
//        通配  *                      —— 所有控件（本讲量得出它的代价）
//        ID    QPushButton#okButton / #okButton —— 按 objectName 精确点人
//        属性  [kind="primary"] / [objectName="nameField"] —— 按动态属性点人
//        后代  「QGroupBox QPushButton」（空格）与 子「QGroupBox > QPushButton」
//              —— 本讲用「组里的孙按钮」把两者的差别**测**出来，不靠背。
//   2) 伪状态什么时候才轮到：:hover :pressed :checked :disabled :focus :selected
//      （前两个要真实指针，本讲在无人值守下只做说明；其余全部实测）。
//   3) 盒模型三圈 margin / border / padding，以及 border-radius 与 border-style 的
//      **有限集**：本讲把候选名字逐个设上去、抓一行像素，让你亲眼看见
//      「写了一个 Qt 不认识的值」长什么样——**不报错、也不生效**，安静得像没事发生。
//   4) 那条坑过所有人的：裸 QWidget / 自定义 QWidget 子类上样式表的背景与边框不画
//      → 要么 Qt::WA_StyledBackground，要么自己画。本讲排了**四格对照**
//      （裸 QWidget 未设/已设属性、自定义子类未设/子类自己覆写 paintEvent），
//      每格打印红色像素数与蓝色像素数：**不背结论，跑出来看**（版本实现细节会变）。
//   5) setStyleSheet 作用于 qApp 与作用于单个控件的差别：作用域、级联方向、优先级；
//      以及 styleSheet() 读回来的**只是这一条控件自己写的那串**（排错第一条）。
//   6) 样式表重新 polish 的代价：本讲用 QElapsedTimer 在「240 个按钮」的窗口上实测，
//      并打印 app.style() 的实现类怎么从 QWindowsVistaStyle 变成 QStyleSheetStyle。
//   7) sub-control（::drop-down / ::down-arrow / ::up-button）与 image: url(:/...)：
//      资源路径写错**没有任何报错**。本讲用 QStyle::subControlRect 把这块区域量出来，
//      并指向 ../storage/ 的 qrc 讲——那里才真正注册 :/... 前缀。
//   最终交付一个**可运行**的「浅色 / 深色一键切换」演示：qApp 级样式表设两次，
//   每一步打印 objectName、className、QPalette 与抓取到的像素色值作为证据；
//   并且证明一件反直觉的事：**QSS 不会把颜色写回 QPalette**。
//
// 对应构建目标名：widgets_09_style_sheets
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(widgets_09_style_sheets 09_style_sheets.cpp)
//   target_link_libraries(widgets_09_style_sheets PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)   # 本文件里的 Fancy / PanelBox 两个类都**有** Q_OBJECT，
//                           # 所以文件末尾必须 #include "09_style_sheets.moc"
//
// 运行前须知：
//   · 依赖 Qt6::Widgets（样式表在 QtWidgets 里，QGuiApplication **没有** setStyleSheet）。
//     Windows 上要能找到 platforms/qwindows.dll；无桌面时用 -platform offscreen 也能跑，
//     因为本讲的所有证据都来自 grab() 抓到的像素，不依赖肉眼。
//   · 本机当前无法编译链接 Qt 运行时，本示例**未在本机实测编译**；每个类/函数/枚举值
//     都对着 C:\msys64\ucrt64\include\qt6 下的真实头文件核对过（行号见下）。
//     但请注意：**样式的最终呈现结果本机也无法实测**——凡「Qt 到底认哪个 border-style」
//     这类问题，本讲一律用「打印出来让你看」代替「我告诉你结论」。
//   · 无人值守：QTimer::singleShot 排十个步骤，每步 qInfo() 打印证据，
//     最后 QCoreApplication::quit()。**没有任何阻塞调用**：
//     不调 exec()、不调 QMessageBox 静态方法、不弹 QMenu、不抓真实鼠标。
//   · 读数纪律：样式表设完先 ensurePolished()，再 grab()。grab() 是**同步**渲染，
//     不需要等事件循环——这就是本讲能一步出数的原因（第 7、8 讲读 geometry 必须等
//     事件循环，是因为布局要等 LayoutRequest；像素不用）。
//   · Qt5 → Qt6 差异（本讲涉及，已核对本机头文件）：
//       - QApplication::setStyleSheet / styleSheet 在 Qt6 仍是**非 static 实例方法**
//         （qapplication.h 127 / 131 行）→ 必须 `app.setStyleSheet(...)` 或
//         `qApp->setStyleSheet(...)`；写成 `QApplication::setStyleSheet(...)` 会编译失败。
//         Qt5 同样如此，**但** QGuiApplication 从来没有这两个函数（本机 grep 过），
//         所以只连 QtGui 的工程里样式表是不可用的。
//       - 属性选择器、WA_StyledBackground 在 Qt6 数值不变（qnamespace.h 389 行
//         `WA_StyledBackground = 93`，注释还写着 internal）。
//       - Qt6 把 QStyleSheetStyle / QStyleSheetSelector 全放在**私有头**
//         （qstylesheetstyle_p.h 一类），公共头里没有：别去 include、也别 qobject_cast
//         到它。想知道当前谁在画，用 `app.style()->metaObject()->className()`（本讲这么干）。
//       - QWidget 的 contentsMargins/contentsRect 在 Qt6 **还在**（qwidget.h 533~537 行），
//         但它跟样式表的 padding 不是一回事：前者是 Qt 自己的边距，后者走 QStyle。
//         Qt5 时代有人用 setContentsMargins 调样式表内边距，迁移时请改用 `padding:`。
//       - 高 DPI：Qt5 要手动开 AA_EnableHighDpiScaling，Qt6 默认按屏幕缩放（第 1 讲）。
//         同一份 `padding: 6px` 在两种缩放下画出的**物理像素**不同——所以本讲的采样
//         函数一律用**比例坐标**（0.0~1.0），不用绝对像素，换 DPI 也读得到同一件事。
//       - 名字可能随版本变：`whoIsTheStyle()` 打印的是运行时 className，
//         Windows 原生风格在 Qt5 叫 QWindowsVistaStyle、Qt6 也可能叫 QWindows11Style。
//         本讲不硬编码期望值，只打印前后差异。
// ============================================================================

#include <QApplication>           // QtWidgets/qapplication.h：实例 setStyleSheet / static style()
#include <QCoreApplication>       // QtCore/qcoreapplication.h：static void quit()
#include <QWidget>                // QtWidgets/qwidget.h：setStyleSheet / grab / ensurePolished
#include <QGroupBox>              // QtWidgets/qgroupbox.h
#include <QVBoxLayout>            // QtWidgets/qboxlayout.h
#include <QHBoxLayout>
#include <QGridLayout>            // QtWidgets/qgridlayout.h：只给「批量窗口」用
#include <QLabel>                 // QtWidgets/qlabel.h
#include <QPushButton>            // QtWidgets/qpushbutton.h
#include <QLineEdit>              // QtWidgets/qlineedit.h
#include <QListWidget>            // QtWidgets/qlistwidget.h：::item:selected
#include <QComboBox>              // QtWidgets/qcombobox.h：::drop-down / ::down-arrow
#include <QSpinBox>               // QtWidgets/qspinbox.h：::up-button / ::down-button
#include <QStyle>                 // QtWidgets/qstyle.h：polish/unpolish/subControlRect
#include <QStyleOption>           // QtWidgets/qstyleoption.h：QStyleOptionComboBox/SpinBox + initFrom
#include <QStyleFactory>          // QtWidgets/qstylefactory.h：keys()
#include <QPainter>               // QtGui/qpainter.h：PanelBox 自己画那一下
#include <QPaintEvent>            // QtGui/qevent.h：paintEvent 的参数类型
#include <QTimer>
#include <QElapsedTimer>          // QtCore/qelapsedtimer.h：start()/elapsed()
#include <QPixmap>                // QtGui/qpixmap.h：toImage()
#include <QImage>                 // QtGui/qimage.h：pixel()/width()/height()/isNull()
#include <QColor>                 // QtGui/qcolor.h：fromRgba()/red()/name()
#include <QPalette>               // QtGui/qpalette.h：ColorRole / brush()/color()
#include <QGuiApplication>        // QtGui/qguiapplication.h：static palette()（对照组）
#include <QString>
#include <QStringList>
#include <QVariant>               // QtCore/qvariant.h：setProperty("kind", ...) 要用它
#include <QVector>
#include <QSet>
#include <QRect>
#include <QDebug>

// ============================================================================
// 一、核对签名（本讲全部依据，路径相对 C:\msys64\ucrt64\include\qt6）
// ============================================================================
// QtWidgets/qwidget.h
//   第 225 行  QStyle *style() const;                     ← 谁在画这个控件
//   第 239/243 行 bool isEnabled() const; void setEnabled(bool);   ← :disabled 的开关
//   第 251 行  const QRect &geometry() const;             ← 盒模型读数（第 7 讲也用它）
//   第 258/261 行 QSize size() const; inline QRect rect() const;
//   第 316 行  const QPalette &palette() const;           ← ★ 本讲的对照组：QSS 不改它
//   第 317 行  void setPalette(const QPalette &);         ← 想让 palette 变，得走这条路
//   第 356 行  Q_INVOKABLE QPixmap grab(const QRect &rectangle = QRect(QPoint(0,0), QSize(-1,-1)));
//              ← ★ 本讲的核心取证工具：把控件（或它的一块子区域）**同步**画成图
//   第 369/377 行 void setWindowTitle(const QString &); QString windowTitle() const;
//   第 371 行  void setStyleSheet(const QString& styleSheet);   ← 控件级样式表
//   第 375 行  QString styleSheet() const;                ← ★ 只返回**这条控件自己**设的那串
//   第 436 行  bool hasFocus() const;                     ← :focus 的开关
//   第 470 行  void update();                             ← 重画请求（本讲靠 grab 不依赖它）
//   第 487 行  void show();          第 503 行 void resize(int w, int h);
//   第 533~537 行 setContentsMargins(int l,int t,int r,int b) / (const QMargins&) /
//              QMargins contentsMargins() const / QRect contentsRect() const
//              ← ⚠ 与样式表的 padding **不是同一套**（前者 Qt 自己的，后者交给 QStyle）
//   第 632/633 行 void setAttribute(Qt::WidgetAttribute, bool on = true);
//              inline bool testAttribute(Qt::WidgetAttribute) const;
//   第 637 行  void ensurePolished() const;               ← ★ 保险：「现在就按样式表收拾我一遍」
//   第 642 行  void setAutoFillBackground(bool enabled);  ← 样式表出现之前的老办法（画 palette 色）
// QtWidgets/qapplication.h
//   第 44 行   Q_PROPERTY(QString styleSheet READ styleSheet WRITE setStyleSheet)
//   第 56/57 行 static QStyle *style(); static void setStyle(QStyle*);
//   第 63 行   static void setPalette(const QPalette &, const char* className = nullptr);
//   第 127 行  QString styleSheet() const;                ← ⚠ 实例方法（非 static）
//   第 131 行  void setStyleSheet(const QString& sheet);  ← ⚠ 实例方法（非 static）
// QtGui/qguiapplication.h
//   第 101 行  static QPalette palette();                 ← 对照组：全局 palette 到底动没动
//   第 108 行  static void setLayoutDirection(Qt::LayoutDirection);   ← 第 10 讲 RTL 用它
// QtGui/qpalette.h
//   第 53~64 行 enum ColorRole { WindowText, Button, Light, Midlight, Dark, Mid, Text,
//              BrightText, ButtonText, Base, Window, Shadow, Highlight, HighlightedText,
//              Link, LinkVisited, AlternateBase, NoRole, ToolTipBase, ToolTipText,
//              PlaceholderText, Accent, NColorRoles };
//   第 71 行   const QBrush &brush(ColorGroup cg, ColorRole cr) const;
//   第 83/84 行 inline const QColor &color(ColorRole cr) const / inline const QBrush &brush(ColorRole cr) const
// QtGui/qcolor.h
//   第 62 行   QString name(NameFormat format = HexRgb) const;   第 36 行 enum NameFormat { HexRgb, HexArgb };
//   第 84/85/86 行 int red() const noexcept; int green() const; int blue() const;
//   第 111 行  void setRgb(QRgb rgb) noexcept;
//   第 169/170 行 static QColor fromRgb(QRgb) / static QColor fromRgba(QRgb)   ← ★ 从像素还原颜色
//   第 110 行  QRgb rgb() const noexcept;
// QtGui/qimage.h
//   第 110 行  bool isNull() const;  第 145/146 行 int width() const; int height() const;
//   第 178 行  QRgb pixel(int x, int y) const;            ← ★ 取一个像素
// QtGui/qpixmap.h
//   第 89 行   QImage toImage() const;                    ← grab() 出来的图要转成 QImage 才能读像素
// QtCore/qnamespace.h
//   第 389 行  WA_StyledBackground = 93, // internal      ← ★ 本讲最大的那个坑，枚举值在这
// QtWidgets/qstyle.h
//   第 43/44 行 virtual void polish(QWidget *widget); virtual void unpolish(QWidget *widget);
//   第 46/47 行 virtual void polish(QApplication *application); virtual void unpolish(QApplication *);
//   第 49 行   virtual void polish(QPalette &palette);    ← 样式表**可以**改 palette（走这条路）
//   第 331~333 行 enum ComplexControl { CC_SpinBox, CC_ComboBox, ... };
//   第 347 行起 enum SubControl { SC_SpinBoxUp = 0x00000001, ... SC_ComboBoxFrame = 0x00000001,
//              SC_ComboBoxArrow = 0x00000004, ... };       ← ★ sub-control 的枚举就在这里
//   第 410/411 行 virtual QRect subControlRect(ComplexControl cc, const QStyleOptionComplex *opt,
//              SubControl sc, const QWidget *widget = nullptr) const = 0;   ← ★ 量出子控件的矩形
// QtWidgets/qstyleoption.h
//   第 71 行   void initFrom(const QWidget *w);           ← QStyleOption 的「从控件填参数」
//   第 597/611 行 class QStyleOptionComboBox : public QStyleOptionComplex { QStyleOptionComboBox(); };
//   第 547/557 行 class QStyleOptionSpinBox : public QStyleOptionComplex { QStyleOptionSpinBox(); };
// QtWidgets/qstylefactory.h
//   第 19 行   static QStringList keys();    第 20 行 static QStyle *create(const QString&);
// QtCore/qcoreevent.h（本讲只提名字，不写数值）
//   第 112/115/116 行 ChildPolished = 69 / PolishRequest = 74 / Polish = 75
//   第 98/99 行  ApplicationPaletteChange = 38 / PaletteChange = 39
//   第 141 行  StyleChange = 100                        ← setStyleSheet 之后控件收到的事件
// QtCore/qelapsedtimer.h
//   第 36 行   void start() noexcept;    第 43 行 qint64 elapsed() const noexcept;
// QtWidgets/qabstractbutton.h
//   第 58 行 setCheckable(bool) / 61 行 isChecked() / 85 行 click() / 87 行 setChecked(bool)
// QtWidgets/qlistwidget.h
//   第 178 行 QListWidget(QWidget *parent = nullptr) / 190 行 addItems(const QStringList&)
//   第 199 行 setCurrentRow(int row)                     ← :selected 的开关
// QtWidgets/qcombobox.h   第 24 行 class QComboBox : public QWidget / 146 行 addItems / 178 行 setCurrentIndex
// QtWidgets/qspinbox.h    第 16 行 class QSpinBox : public QAbstractSpinBox / 53 行 setRange / 70 行 setValue
// QtWidgets/qpushbutton.h 第 30 行 explicit QPushButton(const QString &text, QWidget *parent = nullptr);
// QtWidgets/qlabel.h      第 39 行 explicit QLabel(const QString &text, QWidget *parent = nullptr, ...);
// QtWidgets/qlineedit.h   第 59 行 explicit QLineEdit(const QString &, QWidget *parent = nullptr) / 149 行 setText
// QtWidgets/qgroupbox.h   第 28 行 explicit QGroupBox(const QString &title, QWidget *parent = nullptr);
// QtWidgets/qgridlayout.h 第 29 行 QGridLayout(QWidget *parent = nullptr) / 66 行 addWidget(QWidget*, int, int, ...)
// QtCore/qobject.h        第 127 行 setObjectName(const QString &) / 344 行 setProperty(const char*, const QVariant&)
//   第 169/187 行 template <class T> QList<T> findChildren(QAnyStringView, ...) const
//                                         QList<T> findChildren(Qt::FindChildOptions) const
//              ← ★ 第 ⑧ 步数「一棵树里有多少控件」用它；返回的是 **QList<T>**（可以放心 + / +=）
// QtGui/qpainter.h
//   第 89 行   explicit QPainter(QPaintDevice *);         ← QWidget 就是 QPaintDevice
//   第 396 行  void fillRect(const QRect &, const QColor &color);
// QtGui/qevent.h 第 487 行 class Q_GUI_EXPORT QPaintEvent : public QEvent
// QtWidgets/qwidget.h 第 674 行 virtual void paintEvent(QPaintEvent *event);   ← protected virtual
// QtGui/qrgb.h  第 14 行  typedef unsigned int QRgb;      ← 被 qcolor.h 第 9 行带进来，pixel() 的返回类型
// QtCore/qtimer.h         第 85 行 template <Duration, Functor> static void singleShot(Duration, Functor &&slot)
// ============================================================================

// ----------------------------------------------------------------------------
// Fancy：一个只为了「验证选择器怎么对待子类」而存在的 QPushButton 子类
// ----------------------------------------------------------------------------
// 类型选择器 QPushButton 命中它（它是「QPushButton 或子类」）；
// 类选择器   .QPushButton **不**命中它（类名正好是 "Fancy" 才算）。
// 这两条规则在本讲用同一个像素点把差别打出来。
class Fancy : public QPushButton
{
    Q_OBJECT
public:
    Fancy(const QString &text, QWidget *parent = nullptr)
        : QPushButton(text, parent) {}
};

// ----------------------------------------------------------------------------
// PanelBox：一块「色板」，专门用来把 WA_StyledBackground 这件事量出来
// ----------------------------------------------------------------------------
// 它是**从 QWidget 直接派生**的类——正是 Qt 文档里那句
// 「If you subclass from QWidget, you need to provide a paintEvent … 或者用样式表时要设
//  Qt::WA_StyledBackground」所说的对象。
// ownPaint 决定它自己画不画蓝色：
//   ownPaint=false → 什么都不画，交给基类（样式表如果有机会画背景，就看得见红色）
//   ownPaint=true  → 自己填蓝色并**不给基类机会**（此时样式表的红色一定不出现：
//                    这条是本讲唯一敢打包票的因果，因为画的人换成你了）
class PanelBox : public QWidget
{
    Q_OBJECT
public:
    PanelBox(const QString &name, bool ownPaint, QWidget *parent = nullptr)
        : QWidget(parent), m_ownPaint(ownPaint)
    {
        setObjectName(name);
        setFixedSize(140, 46);          // 故意钉死：它只是色板，不参与尺寸谈判（第 8 讲）
    }

protected:
    void paintEvent(QPaintEvent *ev) override
    {
        if (m_ownPaint) {
            QPainter p(this);                            // qpainter.h 89 行
            p.fillRect(rect(), QColor(33, 150, 243));    // 同上 396 行；rect() 见 qwidget.h 261
            return;                                      // ← 不叫基类，样式表没机会
        }
        QWidget::paintEvent(ev);                         // qwidget.h 674 行
    }

private:
    bool m_ownPaint = false;
};

// ============================================================================
// 二、取证工具：全部基于 grab()，同步、不等事件循环
// ============================================================================

// 取控件自身截图上「比例坐标」处那个像素的颜色，形如 "#37474f@(83,14)"。
// 用比例而不是绝对像素：换 DPI / 换缩放比，读到的还是同一个「相对位置」。
// 【参数为什么是 QWidget* 而不是 const QWidget*】
//   核对 qwidget.h 第 356 行：`Q_INVOKABLE QPixmap grab(const QRect &rectangle = ...);`
//   ——**grab() 不是 const 成员**（上一行的 render(...)（354 行）同样不是 const，
//   因为渲染要动控件内部状态：polish、布局、绘制缓存）。
//   所以 helper 里只要写 const QWidget* w，w->grab() 立刻就是
//   "passing 'const QWidget' as 'this' argument discards qualifiers"。
//   正道是把参数改成非 const 指针，**不要 const_cast 糊**：
//   「读一个像素」在 Qt 里根本不是 const 操作。
static QString swatchAt(QWidget *w, qreal fx, qreal fy)
{
    if (!w) return QStringLiteral("<空指针>");
    const QImage img = w->grab().toImage();
    if (img.isNull() || img.width() < 2 || img.height() < 2)
        return QStringLiteral("<抓到空图（控件没尺寸？没 show？）>");
    const int x = qBound(0, int(img.width() * fx), img.width() - 1);
    const int y = qBound(0, int(img.height() * fy), img.height() - 1);
    const QColor c = QColor::fromRgba(img.pixel(x, y));
    return QStringLiteral("%1@(%2,%3)").arg(c.name(QColor::HexRgb)).arg(x).arg(y);
}

// 沿着一行按比例采 samples 个点，压缩成「颜色x连续个数」的色带。
// 盒模型（边框/内边距/内容）在色带上是**看得见圈层**的，这就是本讲的尺子。
static QString bandOf(QWidget *w, qreal fy, int samples = 32)
{
    if (!w) return QStringLiteral("<空指针>");
    const QImage img = w->grab().toImage();
    if (img.isNull() || img.width() < 2 || img.height() < 2 || samples < 2)
        return QStringLiteral("<抓到空图>");
    const int y = qBound(0, int(img.height() * fy), img.height() - 1);
    QVector<QString> cells;
    for (int i = 0; i < samples; ++i) {
        const int x = (img.width() - 1) * i / (samples - 1);
        cells.append(QColor::fromRgba(img.pixel(x, y)).name(QColor::HexRgb));
    }
    QString out;
    for (int i = 0; i < cells.size(); ) {
        int j = i;
        while (j + 1 < cells.size() && cells.at(j + 1) == cells.at(i)) ++j;
        out += QStringLiteral(" %1x%2").arg(cells.at(i)).arg(j - i + 1);
        i = j + 1;
    }
    return out.trimmed();
}

// 控件上「偏红」像素的个数：WA_StyledBackground 那一步用它说话（样式表想涂的红）。
static int redPixels(QWidget *w)
{
    if (!w) return -1;
    const QImage img = w->grab().toImage();
    if (img.isNull()) return -1;
    int n = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = QColor::fromRgba(img.pixel(x, y));
            if (c.red() > 170 && c.green() < 90 && c.blue() < 90) ++n;
        }
    }
    return n;
}

// 控件上「偏蓝」像素的个数：同一个色板里，蓝 = **你自己 paintEvent 画的**（PanelBox）。
// 红与蓝谁出现，就说明这块地皮归谁在画——这一步的全部判据。
static int bluePixels(QWidget *w)
{
    if (!w) return -1;
    const QImage img = w->grab().toImage();
    if (img.isNull()) return -1;
    int n = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = QColor::fromRgba(img.pixel(x, y));
            if (c.blue() > 170 && c.red() < 90) ++n;
        }
    }
    return n;
}

// 控件（或它的某块子矩形）里出现了多少种不同颜色：
// 「这条规则到底改动画面没有」——种类数变了就是动了，没变就是没动。
static int distinctColors(QWidget *w, const QRect &sub = QRect())
{
    if (!w) return -1;
    const QPixmap pm = sub.isValid() ? w->grab(sub) : w->grab();
    const QImage img = pm.toImage();
    if (img.isNull()) return -1;
    QSet<QRgb> seen;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            seen.insert(img.pixel(x, y));
    return seen.size();
}

// 现在是谁在画全程序？样式表一旦生效，Qt 会把 QStyle 包一层 QStyleSheetStyle。
static QString whoIsTheStyle()
{
    QStyle *s = QApplication::style();          // qapplication.h 56 行（static）
    return s ? QString::fromUtf8(s->metaObject()->className()) : QStringLiteral("<无 style>");
}

// 把一棵控件树重新 unpolish + polish 一遍——样式表改完之后 Qt 内部干的就是这个动作。
// 第 ⑧ 步用它把「重新收拾一遍」的代价单独量出来（qstyle.h 43/44 行、qwidget.h 637 行）。
static int repolishTree(const QWidget *root)
{
    if (!root) return 0;
    const QList<QWidget *> kids = root->findChildren<QWidget *>();   // qobject.h 187 行
    for (QWidget *c : kids) {
        c->style()->unpolish(c);      // qwidget.h 225 行拿 style，qstyle.h 44 行
        c->style()->polish(c);        // 同上 43 行
        c->ensurePolished();          // qwidget.h 637 行：保险，「现在就收拾我」
    }
    return int(kids.size());
}

// ============================================================================
// 三、两套主题：深色 / 浅色（**qApp 级**样式表，这就是「一键换肤」的全部）
// ============================================================================
// 写法的讲究：
//   · 每条规则单独一行 C++ 字符串字面量，相邻字面量由编译器拼接——
//     比一行长串好读，也比 raw string 少了括号配对的坑。
//   · 选择器按「类型 → 类 → 属性 → ID → 伪状态」由弱到强排，
//     这样一眼能看出谁压住谁（Qt 的打分：ID=1 / 类·属性·伪状态=0,1 / 类型=0,0,1）。
//   · 刻意给「深色」的每个层级用了**互不相同**的色值：抓一个像素就知道是哪条规则赢了。
static QString darkSheet()
{
    return QStringLiteral(
        "* { outline: 0; }"                                   // 通配：代价见步骤⑦
        "QWidget { background-color: #263238; color: #eceff1; }"
        "QGroupBox { border: 1px solid #37474f; border-radius: 4px;"
        "          margin-top: 8px; padding-top: 6px; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left;"
        "                   left: 8px; color: #90a4ae; }"
        "QPushButton { background-color: #37474f; color: #eceff1;"            // 类型（子类走这条）
        "              border: 1px solid #546e7a; border-radius: 3px; padding: 6px 14px; }"
        ".QPushButton { background-color: #455a64; }"                          // 类（正好这个类）
        "[kind=\"primary\"] { background-color: #6a1b9a; border-color: #ce93d8; }" // 属性
        "QPushButton#okButton { background-color: #2e7d32; color: #ffffff; }"  // ID：压过上面三条
        "QLineEdit { background-color: #1c262b; border: 1px solid #455a64;"
        "          padding: 4px 8px; selection-background-color: #ffd54f;"
        "          selection-color: #1a1a1a; }"
        "QLabel#hintLabel { color: #b0bec5; }"
        "QListWidget { background-color: #1c262b; border: 1px solid #455a64; }"
        "QListWidget::item { padding: 4px 6px; }"
        "QListWidget::item:selected { background-color: #ffd54f; color: #1a1a1a; }"
        "QComboBox, QSpinBox { background-color: #1c262b; border: 1px solid #455a64;"
        "                      padding: 3px 6px; }");
}

static QString lightSheet()
{
    return QStringLiteral(
        "* { outline: 0; }"
        "QWidget { background-color: #f5f7f8; color: #1f2a30; }"
        "QGroupBox { border: 1px solid #cfd8dc; border-radius: 4px;"
        "          margin-top: 8px; padding-top: 6px; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left;"
        "                   left: 8px; color: #607d8b; }"
        "QPushButton { background-color: #eceff1; color: #1f2a30;"
        "              border: 1px solid #b0bec5; border-radius: 3px; padding: 6px 14px; }"
        ".QPushButton { background-color: #dfe6ea; }"
        "[kind=\"primary\"] { background-color: #d1c4e9; border-color: #9575cd; }"
        "QPushButton#okButton { background-color: #66bb6a; color: #ffffff; }"
        "QLineEdit { background-color: #ffffff; border: 1px solid #b0bec5;"
        "          padding: 4px 8px; selection-background-color: #fff176; }"
        "QLabel#hintLabel { color: #546e7a; }"
        "QListWidget { background-color: #ffffff; border: 1px solid #cfd8dc; }"
        "QListWidget::item { padding: 4px 6px; }"
        "QListWidget::item:selected { background-color: #fff176; color: #1a1a1a; }"
        "QComboBox, QSpinBox { background-color: #ffffff; border: 1px solid #b0bec5;"
        "                      padding: 3px 6px; }");
}

// ============================================================================
// 四、正片
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("widgets_09_style_sheets"));

    qInfo().noquote() << "==== 第 9 讲 · Qt 样式表实验室（本示例未在本机实测编译）====";

    // ------------------------------------------------------------------ 建树
    QWidget window;
    window.setObjectName(QStringLiteral("StyleLab"));   // ← 让 #StyleLab 这条选择器有事可干
    window.setWindowTitle(QStringLiteral("第 9 讲 · 样式表实验室"));
    window.resize(820, 640);

    auto *rootBox = new QVBoxLayout(&window);

    // ① 后代 vs 子：两个内容一样的组，唯一区别是选择器里的空格 / >
    auto *groupA = new QGroupBox(QStringLiteral("组 A：QGroupBox QPushButton（后代，空格）"), &window);
    auto *rowA = new QHBoxLayout(groupA);
    auto *btnA1 = new QPushButton(QStringLiteral("组的直子按钮"), groupA);
    auto *nestA = new QWidget(groupA);                  // 中间多垫一层 QWidget
    auto *layA = new QHBoxLayout(nestA);
    auto *btnA2 = new QPushButton(QStringLiteral("组的孙按钮"), nestA);
    layA->addWidget(btnA2);
    rowA->addWidget(btnA1);
    rowA->addWidget(nestA);

    auto *groupB = new QGroupBox(QStringLiteral("组 B：QGroupBox > QPushButton（子，>）"), &window);
    auto *rowB = new QHBoxLayout(groupB);
    auto *btnB1 = new QPushButton(QStringLiteral("组的直子按钮"), groupB);
    auto *nestB = new QWidget(groupB);
    auto *layB = new QHBoxLayout(nestB);
    auto *btnB2 = new QPushButton(QStringLiteral("组的孙按钮"), nestB);
    layB->addWidget(btnB2);
    rowB->addWidget(btnB1);
    rowB->addWidget(nestB);

    // ② 类型 / 类 / ID / 属性 四个层级排一排；Fancy 负责把「子类」这件事演出来
    auto *tierRow = new QHBoxLayout();
    auto *fancy = new Fancy(QStringLiteral("Fancy（QPushButton 子类）"), &window);
    auto *plainBtn = new QPushButton(QStringLiteral("普通 QPushButton"), &window);
    auto *okBtn = new QPushButton(QStringLiteral("确定"), &window);
    okBtn->setObjectName(QStringLiteral("okButton"));
    auto *primaryBtn = new QPushButton(QStringLiteral("主操作"), &window);
    primaryBtn->setObjectName(QStringLiteral("primaryButton"));
    primaryBtn->setProperty("kind", QStringLiteral("primary"));   // ⚠ 要在 polish 之前设（见附B）
    auto *nameField = new QLineEdit(QStringLiteral("张九"), &window);
    nameField->setObjectName(QStringLiteral("nameField"));
    tierRow->addWidget(plainBtn);
    tierRow->addWidget(fancy);
    tierRow->addWidget(primaryBtn);
    tierRow->addWidget(okBtn);
    tierRow->addWidget(nameField);

    // ③ 伪状态试验台
    auto *stateGroup = new QGroupBox(QStringLiteral("伪状态试验台"), &window);
    auto *stateRow = new QHBoxLayout(stateGroup);
    auto *toggleBtn = new QPushButton(QStringLiteral("可按下（checkable）"), stateGroup);
    toggleBtn->setCheckable(true);                     // qabstractbutton.h 58 行
    auto *deadBtn = new QPushButton(QStringLiteral("被禁用"), stateGroup);
    deadBtn->setEnabled(false);                        // :disabled
    auto *list = new QListWidget(stateGroup);
    list->addItems(QStringList{ QStringLiteral("收件箱"),
                                QStringLiteral("进行中"),
                                QStringLiteral("已完成") });   // :selected 用第 2 行
    auto *hint = new QLabel(QStringLiteral(
        "鼠标划过 / 按住时的效果需要真实指针，本演示不合成鼠标事件（见正文）"), stateGroup);
    hint->setObjectName(QStringLiteral("hintLabel"));
    stateRow->addWidget(toggleBtn);
    stateRow->addWidget(deadBtn);
    stateRow->addWidget(list, 1);
    stateRow->addWidget(hint, 1);

    // ④ 盒模型标尺 + ⑨ sub-control 标的
    auto *mixRow = new QHBoxLayout();
    auto *borderProbe = new QLabel(QStringLiteral("盒模型标尺"), &window);
    auto *combo = new QComboBox(&window);
    combo->addItems(QStringList{ QStringLiteral("选项一"), QStringLiteral("选项二"),
                                 QStringLiteral("选项三") });
    auto *spin = new QSpinBox(&window);
    spin->setRange(0, 999);
    spin->setValue(42);
    mixRow->addWidget(borderProbe);
    mixRow->addWidget(combo);
    mixRow->addWidget(spin);

    // ⑥ WA_StyledBackground 的四格对照（色板一律钉死尺寸：只要能采样的那块颜色）
    auto *panelRow = new QHBoxLayout();
    auto *pBare = new QWidget(&window);                       // 裸 QWidget，未设属性
    pBare->setObjectName(QStringLiteral("pBare"));
    pBare->setFixedSize(140, 46);
    auto *pBareStyled = new QWidget(&window);                 // 裸 QWidget，设了属性
    pBareStyled->setObjectName(QStringLiteral("pBareStyled"));
    pBareStyled->setFixedSize(140, 46);
    pBareStyled->setAttribute(Qt::WA_StyledBackground, true);  // ★ qnamespace.h 389 行 = 93
    auto *pSub = new PanelBox(QStringLiteral("pSub"), false, &window);          // 子类，未设属性
    auto *pSubPainting = new PanelBox(QStringLiteral("pSubPainting"), true, &window);
    auto *panelTagA = new QLabel(QStringLiteral("裸 QWidget\n未设 / 已设属性"), &window);
    auto *panelTagB = new QLabel(QStringLiteral("自定义子类\n未设属性 / 自己 paintEvent"), &window);
    panelRow->addWidget(panelTagA);
    panelRow->addWidget(pBare);
    panelRow->addWidget(pBareStyled);
    panelRow->addWidget(panelTagB);
    panelRow->addWidget(pSub);
    panelRow->addWidget(pSubPainting);

    rootBox->addWidget(groupA);
    rootBox->addWidget(groupB);
    rootBox->addLayout(tierRow);
    rootBox->addWidget(stateGroup);
    rootBox->addLayout(mixRow);
    rootBox->addLayout(panelRow);
    rootBox->addStretch(1);

    // ⑦ 换肤代价的「秤砣」：另外开一个窗口，塞 240 个按钮
    QWidget batch;
    batch.setObjectName(QStringLiteral("BatchWindow"));
    batch.setWindowTitle(QStringLiteral("第 9 讲 · 批量窗口（只为量换肤代价）"));
    auto *batchGrid = new QGridLayout(&batch);
    for (int i = 0; i < 240; ++i)
        batchGrid->addWidget(new QPushButton(QStringLiteral("批量 %1").arg(i), &batch),
                             i / 12, i % 12);
    batch.resize(900, 560);

    window.show();
    batch.show();

    // palette 基线：**任何**样式表都还没设的时候记下来，后面每次换肤都跟它比
    const QString baseWindow = window.palette().color(QPalette::Window).name(QColor::HexRgb);
    const QString baseButton = plainBtn->palette().color(QPalette::Button).name(QColor::HexRgb);
    const QString baseText   = hint->palette().color(QPalette::WindowText).name(QColor::HexRgb);

    // ---------------------------------------------------------------- 步骤 ①
    QTimer::singleShot(250, [&batch, plainBtn, fancy, okBtn, primaryBtn,
                             nameField, hint, baseWindow, baseButton, baseText] {
        qInfo().noquote() << "\n【① 开场体检】";
        qInfo().noquote() << "  本机可用 QStyle（QStyleFactory::keys）：" << QStyleFactory::keys();
        qInfo().noquote() << "  现在画全程序的 style 实现类：" << whoIsTheStyle();
        qInfo().noquote() << "  app.styleSheet() 长度：" << qApp->styleSheet().size()
                          << "（还没设过样式表，所以是 0）";
        qInfo().noquote() << "  QPalette 基线（后面每次换肤都跟这三个值比）：Window=" << baseWindow
                          << " Button=" << baseButton << " WindowText=" << baseText;
        qInfo().noquote() << "  控件身份清单（ID 选择器点的就是 objectName；类型选择器点的是 className）：";
        const QWidget *probe[6] = { plainBtn, fancy, okBtn, primaryBtn, nameField, hint };
        for (const QWidget *w : probe) {
            qInfo().noquote() << QStringLiteral("    objectName=%1  className=%2  自己挂的样式表长度=%3")
                                 .arg(w->objectName().isEmpty() ? QStringLiteral("<未命名>") : w->objectName())
                                 .arg(QString::fromUtf8(w->metaObject()->className()))
                                 .arg(w->styleSheet().size());
        }
        qInfo().noquote() << "  batch 窗口里的子控件总数：" << batch.findChildren<QWidget *>().size();
        qInfo().noquote() << "  ⚠ 上面这一串的意义：样式表**只认这两个身份**——objectName（#id / [属性]）"
                          "和类名（类型 / .类）。给控件起个好 objectName 不是洁癖，是让它「可被点名」。";
    });

    // ---------------------------------------------------------------- 步骤 ②
    QTimer::singleShot(650, [groupA, groupB, btnA1, btnA2, btnB1, btnB2] {
        qInfo().noquote() << "\n【② 选择器的匹配范围（控件级样式表，只作用于本组子树）】";
        const QString hit = QStringLiteral("#4dd0e1");
        groupA->setStyleSheet(QStringLiteral("QGroupBox QPushButton { background-color: %1;"
                                             " color: #000000; }").arg(hit));
        groupB->setStyleSheet(QStringLiteral("QGroupBox > QPushButton { background-color: %1;"
                                             " color: #000000; }").arg(hit));
        groupA->ensurePolished();
        groupB->ensurePolished();
        qInfo().noquote() << "  规则写的是青色" << hit << "，看谁真的被涂了（取色点取 x=6% 宽度，"
                          "避开按钮中间那行文字的字形）：";
        qInfo().noquote() << "  A 组直子 btnA1：" << swatchAt(btnA1, 0.06, 0.5);
        qInfo().noquote() << "  A 组孙   btnA2：" << swatchAt(btnA2, 0.06, 0.5)
                          << " ← 空格=后代，隔一层也算，所以**命中**";
        qInfo().noquote() << "  B 组直子 btnB1：" << swatchAt(btnB1, 0.06, 0.5);
        qInfo().noquote() << "  B 组孙   btnB2：" << swatchAt(btnB2, 0.06, 0.5)
                          << " ← > =亲儿子，隔一层就**不命中**";
        qInfo().noquote() << "  小结：空格是「子孙都算」，> 是「只认亲的」。写样式表时想清范围，"
                          "不然一个 > 会让整片区域不生效，而且**不报错**。";
    });

    // ---------------------------------------------------------------- 步骤 ③
    QTimer::singleShot(1050, [stateGroup, toggleBtn, deadBtn, nameField, list, hint] {
        qInfo().noquote() << "\n【③ 伪状态：什么时候才轮到】";
        stateGroup->setStyleSheet(QStringLiteral(
            "QPushButton { background-color: #b0bec5; color: #212121;"
            "             border: 1px solid #78909c; padding: 6px; }"
            "QPushButton:checked { background-color: #1976d2; color: #ffffff;"
            "                      border: 2px solid #90caf9; }"
            "QPushButton:disabled { background-color: #eeeeee; color: #9e9e9e; }"
            "QPushButton:hover { background-color: #ffe082; }"
            "QPushButton:pressed { background-color: #ff8a65; }"
            "QLineEdit { border: 1px solid #9e9e9e; padding: 3px 6px; }"
            "QLineEdit:focus { border: 3px solid #2e7d32; }"
            "QListWidget::item:selected { background-color: #ffd54f; color: #1a1a1a; }"
            "QListWidget::item:hover { background-color: #fff9c4; }"));
        stateGroup->ensurePolished();

        qInfo().noquote() << "  :checked —— 按下前 " << swatchAt(toggleBtn, 0.06, 0.5)
                          << "，isChecked()=" << toggleBtn->isChecked();
        toggleBtn->setChecked(true);
        toggleBtn->ensurePolished();
        qInfo().noquote() << "                按下后 " << swatchAt(toggleBtn, 0.06, 0.5)
                          << "，isChecked()=" << toggleBtn->isChecked()
                          << "（:checked 生效就是这里变色）";

        qInfo().noquote() << "  :disabled—— deadBtn->isEnabled()=" << deadBtn->isEnabled()
                          << " 取色：" << swatchAt(deadBtn, 0.06, 0.5)
                          << "（灰是样式表给的，不是系统禁用的效果）";

        qInfo().noquote() << "  :focus —— setFocus 之前 hasFocus()=" << nameField->hasFocus()
                          << " 取最左边那列像素（x=0 一定是边框）：" << swatchAt(nameField, 0.0, 0.5);
        nameField->setFocus();
        nameField->ensurePolished();
        qInfo().noquote() << "                setFocus 之后 hasFocus()=" << nameField->hasFocus()
                          << " 同一位置：" << swatchAt(nameField, 0.0, 0.5)
                          << "（1px 灰框 → 3px 绿框）";

        const int colorsBefore = distinctColors(list);
        list->setCurrentRow(1);                 // qlistwidget.h 199 行
        list->ensurePolished();
        const int colorsAfter = distinctColors(list);
        qInfo().noquote() << "  :selected—— 选中第 2 行前颜色种类=" << colorsBefore
                          << "，之后=" << colorsAfter
                          << "（::item:selected 这条规则改动了画面，数量必然变；"
                          "没变就是选择器或伪状态写错了）";
        qInfo().noquote() << "  :hover / :pressed —— 声明已经写在上面，但**本演示不触发**："
                          "它们要真实鼠标进入/按下控件。无人值守的 ctest 里不能合成指针事件"
                          "（本工程示例没连 Qt6::Test，也就没有 QTest::mousePress）。"
                          "要看效果请本地跑起来把鼠标移上去——第 11 讲（事件系统）会教你"
                          "**用代码**送事件进去，那时再来验这两条。";
    });

    // ---------------------------------------------------------------- 步骤 ④
    QTimer::singleShot(1450, [borderProbe] {
        qInfo().noquote() << "\n【④ 盒模型：margin / border / padding 三圈（这是 Qt 的盒，不是网页的盒）】";
        borderProbe->setStyleSheet(QStringLiteral(
            "QLabel { background-color: #ffffff; color: #212121;"
            "         border: 8px solid #000000; padding: 14px; margin: 10px;"
            "         border-radius: 4px; }"));
        borderProbe->ensurePolished();
        qInfo().noquote() << "  borderProbe 的 size()：" << borderProbe->size()
                          << " geometry()：" << borderProbe->geometry();
        qInfo().noquote() << "  中间那一行的色带（从左到右压缩成「颜色x连续采样数」）：";
        qInfo().noquote() << "    " << bandOf(borderProbe, 0.5, 40);
        qInfo().noquote() << "  读法：外面一圈是父容器色（margin 让出来的，**不会被画**，"
                          "它只是把控件的盒子往里推）→ 黑圈 = border 8px → 白圈 = padding 14px → "
                          "中心是文字区。颜色种类数：" << distinctColors(borderProbe);
        qInfo().noquote() << "  ⚠ 三条和 CSS 不一样的地方：① Qt 里 padding 只对**部分**控件生效"
                          "（QStyle 里有对应的盒模型才认）；② margin 只对布局里的控件有意义，"
                          "脱离布局写 margin 等于没写；③ 写 border 一定要同时写 border-style，"
                          "只写 `border: 8px` 会被当成「无边框」。";
    });

    // ---------------------------------------------------------------- 步骤 ⑤
    QTimer::singleShot(1850, [borderProbe] {
        qInfo().noquote() << "\n【⑤ border-style 到底认哪些？本讲不下结论，本讲把结果打给你看】";
        qInfo().noquote() << "  Qt 对这个集合的定义在**私有实现**（qcssparser 一类）里，公共头文件查不到；"
                          "官方文档「Box Model / border」一页为准。做法：候选名挨个设上去，"
                          "量「颜色种类数」——名字写错不会报错，只会**静悄悄不生效**。";
        const QStringList candidates{ QStringLiteral("none"), QStringLiteral("solid"),
                                      QStringLiteral("dashed"), QStringLiteral("dotted"),
                                      QStringLiteral("double"), QStringLiteral("groove"),
                                      QStringLiteral("ridge"), QStringLiteral("inset"),
                                      QStringLiteral("outset"), QStringLiteral("hidden"),
                                      QStringLiteral("squiggle") };   // 最后一条故意写错
        for (const QString &name : candidates) {
            borderProbe->setStyleSheet(QStringLiteral(
                "QLabel { background-color: #ffffff; border: 8px %1 #000000; padding: 10px; }")
                .arg(name));
            borderProbe->ensurePolished();
            qInfo().noquote() << QStringLiteral("    border-style: %1  →  颜色种类=%2  色带=%3")
                                 .arg(name, -9)
                                 .arg(distinctColors(borderProbe))
                                 .arg(bandOf(borderProbe, 0.5, 24));
        }
        qInfo().noquote() << "  对照最后一行：写一个 Qt 不认识的名字，**程序不报错、不警告**，"
                          "画面回到默认——这就是样式表最难排的地方。所以本讲的纪律是：改样式表之后，"
                          "要么截图看，要么像这样抓像素。";
    });

    // ---------------------------------------------------------------- 步骤 ⑥
    QTimer::singleShot(2300, [pBare, pBareStyled, pSub, pSubPainting] {
        qInfo().noquote() << "\n【⑥ 新人最常摔的一格：QWidget 的样式表背景/边框不画】";
        const QString sheet = QStringLiteral(
            "background-color: #e53935; border: 2px solid #7f0000;");
        // 四格用**同一串**样式表，唯一区别是「是不是子类」「设没设那个属性」「有没有自己的 paintEvent」
        pBare->setStyleSheet(sheet);
        pBareStyled->setStyleSheet(sheet);
        pSub->setStyleSheet(sheet);
        pSubPainting->setStyleSheet(sheet);
        pBare->ensurePolished();
        pBareStyled->ensurePolished();
        pSub->ensurePolished();
        pSubPainting->ensurePolished();

        struct Cell { QWidget *w; const char *desc; };
        const Cell cells[4] = {
            { pBare,        "裸 QWidget · 未设属性    " },
            { pBareStyled,  "裸 QWidget · 已设属性    " },
            { pSub,         "PanelBox 子类 · 未设属性 " },
            { pSubPainting, "PanelBox 子类 · 自己画   " },
        };
        qInfo().noquote() << "  同一串规则：" << sheet;
        qInfo().noquote() << "  红 = 样式表涂的（#e53935）；蓝 = PanelBox::paintEvent 自己涂的（33,150,243）";
        for (const Cell &c : cells) {
            qInfo().noquote() << QStringLiteral("    %1  className=%2  WA_StyledBackground=%3  红像素=%4  蓝像素=%5")
                                 .arg(QString::fromUtf8(c.desc))   // ★ desc 是中文，只能 fromUtf8
                                 .arg(QString::fromUtf8(c.w->metaObject()->className()))
                                 .arg(c.w->testAttribute(Qt::WA_StyledBackground) ? 1 : 0)
                                 .arg(redPixels(c.w))
                                 .arg(bluePixels(c.w));
        }
        qInfo().noquote() << "  → 只有最后一格「蓝>0 且 红=0」是**本讲敢打包票**的因果："
                          "你自己覆写了 paintEvent，这块地皮的画权就在你手里，样式表的 background 一定让位。";
        qInfo().noquote() << "  → 前三格请**跑出来看**，别背我的预测：Qt 在 polish 某些控件时"
                          "会替你把这个属性补上（实现细节、版本相关），但**自定义子类不补**是文档明说的："
                          "「If you subclass from QWidget … you need to provide a paintEvent」，"
                          "翻译成人话就是「你不画，就得设 Qt::WA_StyledBackground，否则样式表管不到你」。";
        qInfo().noquote() << "  → 补救动作也演示一次（很多人就是这么救的）：";
        pSub->setAttribute(Qt::WA_StyledBackground, true);
        pSub->setStyleSheet(sheet);                 // 重设一次，强制重新 polish
        pSub->ensurePolished();
        qInfo().noquote() << "    pSub 补上属性之后：WA_StyledBackground="
                          << (pSub->testAttribute(Qt::WA_StyledBackground) ? 1 : 0)
                          << " 红像素=" << redPixels(pSub)
                          << "（属性值本身 = 93，qnamespace.h 389 行）";
        qInfo().noquote() << "  ⚠ 三条相关经验：① 写自定义控件（第 12 讲）时，要么设这个属性、"
                          "要么在 paintEvent 里自己画，**别两个都要**（会打架，见 pSubPainting 那格）；"
                          "② 样式表之前的年代用 setAutoFillBackground(true) + palette"
                          "（qwidget.h 642/317 行），画的是 palette 的颜色，**不是**样式表的颜色，"
                          "两套混用时先问「谁在画」；③ 只写 `background:` 不给尺寸时，"
                          "裸 QWidget 的 sizeHint 是 (-1,-1)（第 8 讲），色板本讲靠 setFixedSize 撑开。";
    });

    // ---------------------------------------------------------------- 步骤 ⑦
    QTimer::singleShot(2750, [&app, &window, plainBtn, fancy, okBtn, primaryBtn, hint,
                              baseWindow, baseButton, baseText] {
        qInfo().noquote() << "\n【⑦ 第一次上 qApp 级样式表：作用域 / 优先级 / 谁在画】";
        qInfo().noquote() << "  设置前 app.style() 实现类：" << whoIsTheStyle();
        app.setStyleSheet(darkSheet());
        window.ensurePolished();
        qInfo().noquote() << "  设置后 app.style() 实现类：" << whoIsTheStyle()
                          << " ← Qt 把原 style 包了一层（类名是运行时打印的，别硬编码）";
        qInfo().noquote() << "  app.styleSheet() 长度：" << app.styleSheet().size()
                          << "；plainBtn->styleSheet() 长度：" << plainBtn->styleSheet().size()
                          << " ← ★ styleSheet() 读回来**只有这条控件自己写过的那串**，"
                          "app 级那条根本不在里面。排查「这条规则没生效」时先看这个，别被骗。";
        qInfo().noquote() << "  四个按钮同屏比色（深色主题里每个层级一个专属色值）：";
        qInfo().noquote() << "    plainBtn   （.QPushButton 类选择器赢）：" << swatchAt(plainBtn, 0.08, 0.5);
        qInfo().noquote() << "    fancy      （只有类型选择器命中）    ：" << swatchAt(fancy, 0.08, 0.5);
        qInfo().noquote() << "    primaryBtn （[kind=\"primary\"] 属性）  ：" << swatchAt(primaryBtn, 0.08, 0.5);
        qInfo().noquote() << "    okBtn      （#okButton ID，最具体）   ：" << swatchAt(okBtn, 0.08, 0.5);
        qInfo().noquote() << "  → 第 1、2 行不同 = 「类型含子类、类不含子类」；"
                          "第 3、4 行盖过第 1 行 = 属性/ID 的打分更高。";
        qInfo().noquote() << "  作用域方向：qApp 级向下覆盖**所有**控件；控件级只覆盖**自己和它的孩子**。";
        qInfo().noquote() << "  现在再给 plainBtn 单独挂一条控件级样式表（它比 app 级更近）：";
        plainBtn->setStyleSheet(QStringLiteral("QPushButton { background-color: #ffcc00;"
                                               " color: #000000; }"));
        plainBtn->ensurePolished();
        qInfo().noquote() << "    plainBtn（控件级）：" << swatchAt(plainBtn, 0.08, 0.5)
                          << " vs fancy（仍走 app 级）：" << swatchAt(fancy, 0.08, 0.5);
        qInfo().noquote() << "    → 控件级压过 app 级。代价是：散落在控件上的 setStyleSheet "
                          "会**吃掉**换肤结果（下一步你会看到 A/B 两个组还在发青）。";
        qInfo().noquote() << "  ★ 反直觉的一条：换肤之后 QPalette 变了吗？";
        qInfo().noquote() << "    Window 基线=" << baseWindow << " 现在=" 
                          << window.palette().color(QPalette::Window).name(QColor::HexRgb);
        qInfo().noquote() << "    Button 基线=" << baseButton << " 现在="
                          << plainBtn->palette().color(QPalette::Button).name(QColor::HexRgb);
        qInfo().noquote() << "    WindowText 基线=" << baseText << " 现在="
                          << hint->palette().color(QPalette::WindowText).name(QColor::HexRgb);
        qInfo().noquote() << "    全局对照组 QGuiApplication::palette().Window="
                          << QGuiApplication::palette().color(QPalette::Window).name(QColor::HexRgb);
        qInfo().noquote() << "    → 像素明明全变了，palette 却**一个字节都没动**。"
                          "QSS 是在「画」的层面接管，不写回 palette（想让 palette 变：QApplication::setPalette "
                          "或 style()->polish(QPalette&)，那是另一条路）。"
                          "所以任何还在直接读 palette 的代码（第 13 讲自绘）换肤后会画旧颜色。";
    });

    // ---------------------------------------------------------------- 步骤 ⑧
    QTimer::singleShot(3300, [&app, &window, &batch] {
        qInfo().noquote() << "\n【⑧ 重新 polish 的代价（QElapsedTimer 实测）】";
        qInfo().noquote() << "  参与计数的子控件数：window=" << window.findChildren<QWidget *>().size()
                          << " batch=" << batch.findChildren<QWidget *>().size();
        for (int round = 0; round < 3; ++round) {
            const QString sheet = (round % 2 == 0) ? lightSheet() : darkSheet();
            QElapsedTimer clock;
            clock.start();
            app.setStyleSheet(sheet);                       // 只是一次调用
            const qint64 callMs = clock.elapsed();
            clock.start();
            const int n = repolishTree(&window) + repolishTree(&batch);
            const qint64 rePolishMs = clock.elapsed();
            qInfo().noquote() << QStringLiteral("    第 %1 轮：setStyleSheet 调用返回=%2 ms，"
                                                "手工把 %3 个控件 unpolish+polish=%4 ms")
                                 .arg(round + 1).arg(callMs).arg(n).arg(rePolishMs);
        }
        qInfo().noquote() << "  读法：① setStyleSheet 会**立刻**收拾当前已 polish 的控件树，"
                          "剩下的活儿排在事件循环里（QEvent::Polish=75 / StyleChange=100，见 qcoreevent.h），"
                          "所以「调用返回快」不等于「用户看到的卡顿小」；"
                          "② 把 batch 那几个按钮减到 20 个再跑，数字会明显下降 —— "
                          "**控件越多，换肤越贵**，而 `* { … }` 一条规则等于给全树都加一次 polish；"
                          "③ 真要做换肤，正确姿势是「一份主题串 + app.setStyleSheet 一次」，"
                          "而不是「遍历每个控件各 setStyleSheet 一次」（后者是前者的 N 倍代价）。";
    });

    // ---------------------------------------------------------------- 步骤 ⑨
    QTimer::singleShot(3900, [&app, combo, spin] {
        qInfo().noquote() << "\n【⑨ sub-control：把「一个控件里的零件」单独点出来】";
        QStyleOptionComboBox optCb;                 // qstyleoption.h 597/611 行
        optCb.initFrom(combo);                      // 同上 71 行
        QStyleOptionSpinBox optSb;                  // 同上 547/557 行
        optSb.initFrom(spin);
        const QRect arrowBefore = app.style()->subControlRect(
            QStyle::CC_ComboBox, &optCb, QStyle::SC_ComboBoxArrow, combo);   // qstyle.h 366/410
        const QRect dropBefore = arrowBefore;
        const QRect upBefore = app.style()->subControlRect(
            QStyle::CC_SpinBox, &optSb, QStyle::SC_SpinBoxUp, spin);         // qstyle.h 359/410
        qInfo().noquote() << "  设置前：下拉箭头矩形=" << arrowBefore
                          << " 尺寸=" << arrowBefore.size();
        qInfo().noquote() << "          数字框上按钮矩形=" << upBefore
                          << " 尺寸=" << upBefore.size();
        qInfo().noquote() << "  箭头区当前颜色种类=" << distinctColors(combo, arrowBefore);

        combo->setStyleSheet(QStringLiteral(
            "QComboBox { border: 1px solid #90a4ae; padding: 3px 6px; }"
            "QComboBox::drop-down { subcontrol-origin: padding;"
            "                       subcontrol-position: top right;"
            "                       width: 34px; border-left: 1px solid #90a4ae; }"
            "QComboBox::down-arrow { image: url(:/storage/icons/down-arrow.png); }"
            "QComboBox QAbstractItemView { selection-background-color: #ffd54f;"
            "                              selection-color: #1a1a1a; }"));
        spin->setStyleSheet(QStringLiteral(
            "QSpinBox { border: 1px solid #90a4ae; padding: 3px 6px; }"
            "QSpinBox::up-button { subcontrol-origin: border;"
            "                      subcontrol-position: top right; width: 26px; }"
            "QSpinBox::down-button { subcontrol-origin: border;"
            "                        subcontrol-position: bottom right; width: 26px; }"
            "QSpinBox::up-arrow { image: url(:/storage/icons/up-arrow.png); }"
            "QSpinBox::down-arrow { image: url(:/storage/icons/down-arrow.png); }"));
        combo->ensurePolished();
        spin->ensurePolished();

        optCb.initFrom(combo);
        optSb.initFrom(spin);
        const QRect arrowAfter = app.style()->subControlRect(
            QStyle::CC_ComboBox, &optCb, QStyle::SC_ComboBoxArrow, combo);
        const QRect upAfter = app.style()->subControlRect(
            QStyle::CC_SpinBox, &optSb, QStyle::SC_SpinBoxUp, spin);
        qInfo().noquote() << "  设置后：下拉箭头矩形=" << arrowAfter
                          << " 宽度变化=" << (arrowBefore.width() == -1 ? QStringLiteral("?")
                          : QString::number(arrowAfter.width() - arrowBefore.width()));
        qInfo().noquote() << "          数字框上按钮矩形=" << upAfter
                          << " 宽度变化=" << QString::number(upAfter.width() - upBefore.width());
        qInfo().noquote() << "  → 宽度被 ::drop-down / ::up-button 的 `width:` 改掉了，"
                          "**几何读数**证明 sub-control 这条规则真的接上了（不是只写了不报错）。";
        qInfo().noquote() << "  → 那两条 `image: url(:/storage/icons/*.png)` 呢？箭头区颜色种类：设置前="
                          << distinctColors(combo, dropBefore) << " 现在="
                          << distinctColors(combo, arrowAfter);
        qInfo().noquote() << "    本讲**没有注册任何 qrc**，所以 `:/storage/...` 这个资源路径解析不出来 —— "
                          "Qt 不会报错、不会警告，只是把箭头**画不出来**（或沿用 style 原画）。"
                          "这正是「样式表的错都很安静」的第二例。";
        qInfo().noquote() << "    资源前缀 :/ 是怎么来的、qrc 怎么写、怎么确认路径存在："
                          "看 ../storage/ 那几讲（第 1 讲注册 :/storage/ 前缀）。"
                          "在那之前，别在样式表里写 url(:/...)。";
        qInfo().noquote() << "  顺带：`subcontrol-origin`（padding/border）与 `subcontrol-position`"
                          "决定这块零件的坐标系和贴边位置；QSS 还能写 `QComboBox QAbstractItemView` "
                          "这种「弹出视图」的规则——下拉列表是**另一个控件**，不是画出来的假象。";
    });

    // ---------------------------------------------------------------- 步骤 ⑩
    QTimer::singleShot(4500, [&app, &window, plainBtn, okBtn, list, groupA, btnA1, btnA2,
                              baseWindow] {
        qInfo().noquote() << "\n【⑩ 一键换肤：深色 → 浅色（可运行的两行代码）】";
        qInfo().noquote() << "  深色态读数：";
        qInfo().noquote() << "    #StyleLab 背景像素=" << swatchAt(&window, 0.5, 0.97)
                          << " plainBtn=" << swatchAt(plainBtn, 0.08, 0.5)
                          << " #okButton=" << swatchAt(okBtn, 0.5, 0.5);
        qInfo().noquote() << "    列表第 2 行（:selected）色带=" << bandOf(list, 0.5, 24);
        qInfo().noquote() << "    palette(Window)=" << window.palette().color(QPalette::Window).name(QColor::HexRgb)
                          << "（和基线" << baseWindow << "比：还是没动）";

        app.setStyleSheet(lightSheet());        // ★ 所谓「一键」，就是这一行
        window.ensurePolished();

        qInfo().noquote() << "  浅色态读数：";
        qInfo().noquote() << "    #StyleLab 背景像素=" << swatchAt(&window, 0.5, 0.97)
                          << " plainBtn=" << swatchAt(plainBtn, 0.08, 0.5)
                          << " #okButton=" << swatchAt(okBtn, 0.5, 0.5);
        qInfo().noquote() << "    列表第 2 行（:selected）色带=" << bandOf(list, 0.5, 24);
        qInfo().noquote() << "    palette(Window)=" << window.palette().color(QPalette::Window).name(QColor::HexRgb);
        qInfo().noquote() << "  ⚠ 看见没有：A/B 两个「选择器实验室」组的按钮**还是青发的**——"
                          "它们身上挂着控件级样式表（步骤②），换肤管不到它："
                          << " groupA->styleSheet() 长度=" << groupA->styleSheet().size()
                          << "，A 组直子取色=" << swatchAt(btnA1, 0.06, 0.5)
                          << " A 组孙取色=" << swatchAt(btnA2, 0.06, 0.5)
                          << "（青色 #4dd0e1 还在）";
        qInfo().noquote() << "    结论：做换肤就**别在控件上散着写 setStyleSheet**；"
                          "非要写就把这些散串集中登记，换肤时一起清（`w->setStyleSheet(QString())`）。";
    });

    // ---------------------------------------------------------------- 收尾
    QTimer::singleShot(5200, [&app, &window, plainBtn, groupA] {
        qInfo().noquote() << "\n【结论】";
        qInfo().noquote() << "  1) 选择器五层：类型(含子类) / 类(不含) / 通配 * / ID(objectName) /"
                          " 属性(动态属性)；空格=后代、> =亲子。打分：ID > 类=属性=伪状态 > 类型，"
                          "同分看谁写得靠后。";
        qInfo().noquote() << "  2) 伪状态是「状态」的镜像：:checked 来自 setChecked，:disabled 来自"
                          " setEnabled，:focus 来自焦点，:selected 来自选择模型；:hover/:pressed 需要"
                          "真实（或注入的）指针事件。";
        qInfo().noquote() << "  3) 盒模型：margin 只在布局里有意义；border 必须带 style；padding 不是"
                          "所有控件都认。border-radius 要有 border 才画得出圆角（自己试第 ⑤ 步的表）。";
        qInfo().noquote() << "  4) 自定义 QWidget 子类想让样式表画背景，要么 Qt::WA_StyledBackground（=93），"
                          "要么自己 paintEvent；两者都要 = 打架（第 ⑥ 步四格表，蓝出现红就消失）。";
        qInfo().noquote() << "  5) app 级 vs 控件级：app 级向下覆盖全部，控件级更近所以更强；"
                          "styleSheet() 只读回自己那串；换肤会被控件级散串破坏。";
        qInfo().noquote() << "  6) 样式表**不写回 QPalette**（⑦⑩ 两处对照）；自绘代码读 palette 就会用旧色。";
        qInfo().noquote() << "  7) url(:/...) 写错不报错。资源前缀的注册见 ../storage/ 那几讲。";
        qInfo().noquote() << "  8) 代价与控件数量成正比；`*` 一条给全树都加一次 polish。";

        // 收尾把散在各控件上的样式表清掉，演示「撤得干净」
        plainBtn->setStyleSheet(QString());
        groupA->setStyleSheet(QString());
        qInfo().noquote() << "  清理演示：plainBtn->styleSheet() 现在长度=" << plainBtn->styleSheet().size()
                          << " groupA=" << groupA->styleSheet().size()
                          << "；app 级还剩 " << app.styleSheet().size() << " 个字符。";
        qInfo().noquote() << "  本示例未在本机实测编译；所有颜色/几何数字请在你的机器上以实际打印为准。";
        QCoreApplication::quit();
    });

    return app.exec();
}

// ============================================================================
// 附A：本讲**故意不做**的几件事（以及为什么）
// ============================================================================
//  ① 不合成鼠标事件来验 :hover/:pressed。
//     要用 QTest::mousePress/mouseMove 就得连 Qt6::Test，本模块所有示例统一只连 Qt6::Widgets；
//     而且第 11 讲（事件系统与事件过滤器）本来就要正面讲送事件，重复一次没意义。
//  ② 不注册 qrc，也不放真的图标文件。
//     `image: url(:/...)` 的**正确用法**留给 ../storage/ 讲：那里会把 :/storage/ 前缀建起来。
//     本讲只演示「写了不存在的资源路径会怎样」，因为那个「静悄悄」的行为才是新人真正的坑。
//  ③ 不写 Qt5 兼容层（`#if QT_VERSION` 分支）。
//     差异全部写在文件头的「Qt5 → Qt6」注释里；真要在老工程落地，改法都在注释里给了。
//  ④ 不用 QApplication::setPalette 做深色主题。
//     那是另一套机制（第 8 讲讲 palette、第 13 讲讲它怎么被读），和 QSS 混用最容易出「半深半浅」，
//     本讲把「QSS 不改 palette」这条测出来给你看，就是为了以后不混用踩空。
//  ⑤ 不做「主题下拉框 + 真实产品级主题管理」。
//     那是第 10 讲（配置与持久化）之后的事；本讲只要你能背下「换肤 = 一份串 + 一次 setStyleSheet」。

// ============================================================================
// 附B：选择器速查 + 三个我在 code review 里反复看到的真 bug
// ============================================================================
//  打分口径（同分则**写在后面的赢**，这是样式表里最容易忽略的「顺序也是语义」）：
//    #okButton                      →  1,0,0   ID
//    .QPushButton / [kind="x"] / :hover / ::item →  0,1,0   类、属性、伪状态、伪元素
//    QPushButton / QGroupBox        →  0,0,1   类型
//    *                              →  0,0,0   通配（别指望它赢，但它是代价最大的）
//
//  真 bug ①「换肤之后按钮变回白底」
//    根因：某个模块在自己控件上写了 setStyleSheet("QPushButton{...}")，控件级压过 app 级。
//    本讲步骤⑩的 A/B 组就是现场复刻。治法：换肤前统一登记并清空散串，或者规定
//         「业务代码禁止调用控件级 setStyleSheet，只有主题模块可以」。
//
//  真 bug ②「动态属性选择器时灵时不灵」
//    根因：`[kind="primary"]` 读的是 polish 那一刻的属性值。先 setStyleSheet 再 setProperty，
//         或运行中 setProperty 之后不重 polish，画面就是旧的。
//    治法（择一）：a) 属性一定在挂样式表**之前**设好（本讲就是这么排的）；
//         b) 设完之后 `w->style()->unpolish(w); w->style()->polish(w);`（qstyle.h 43/44 行）
//            再 `w->update()`；c) 干脆别用属性选择器表达状态，用 :checked/:enabled 这类现成伪状态。
//
//  真 bug ③「样式表一挂，DPI 一变就全崩」
//    根因：QSS 里的 px 是**设备无关像素**，但很多人拿截图量出的**物理**像素往回写，
//         于是 150% 缩放的机器上按钮挤成一团（第 8 讲的 min/max 硬墙也在这里一起收账）。
//    治法：尺寸优先交给第 8 讲（sizeHint/sizePolicy），样式表里只写真正需要的
//         padding/border；文字大小用 `font-size: 11pt`（点）或让主题只提供色不提供尺寸。
//
//  还有一条不算 bug 但很值钱：样式表**拼在字符串里**发过去之前，先在注释里把这段 QSS
//  原文贴一份。出错时 Qt 不会告诉你哪一行不认，只有你自己能对照（本讲每条都写了行号级注释）。

// ============================================================================
// 老师傅的话 + 小练习
// ============================================================================
/*
 * ——— 老师傅的话 ———
 *   徒弟，样式表是你手里最「便宜」的一把刀：几行字符串就能让程序换个脸。
 *   但便宜的东西最容易滥用。你今天必须记住三句话：
 *
 *   第一句：**样式表的错误不说话。** 写错选择器、写错属性、写错资源路径，Qt 一声不吭。
 *   所以本讲从头到尾在教你一件事——用 grab() 把画面变成数字。以后你改样式表，
 *   不要「刷新看看好像对了」，要能说出「按钮左边框是 #000000、宽 8 个像素」。
 *   能读数的工程师，和只会肉眼调色的工程师，差距就在这一格。
 *
 *   第二句：**样式表管颜色，布局管尺寸。**（第 8 讲也说过一遍，这里再说一次。）
 *   你一旦开始用 `width:` `height:` `margin:` 在 QSS 里摆位置，
 *   就等于把界面写死了两遍——一遍在布局里，一遍在样式表里，两遍迟早打架。
 *   QSS 的正确边界是：颜色、边框、圆角、内边距、字体。尺寸交给 sizeHint 和 sizePolicy。
 *
 *   第三句：**能一次搞定的事别做 N 次。** 换肤就是 app.setStyleSheet 一次，
 *   不是遍历每个控件各设一次。第 ⑧ 步那两个毫秒数你跑出来看看，
 *   以后有人提「我想给每个按钮单独写样式表」，你能拿数字驳回他。
 *
 *   最后留一句不好听的：新人做换肤，八成会做出「一半深一半浅」的界面——
 *   因为 QSS 不写回 palette（本讲测了三次），而你的自绘代码、你的第三方控件、
 *   你的图标，全都还在读 palette 和读 PNG 原图。等你写到第 13 讲自绘、
 *   第 14 讲图标资源时，会再遇到这笔账。今天先把「谁在画」这个问题问对。
 *   想看布局与尺寸怎么谈：../visuals/layout_negotiation.html；
 *   想看这套色带读数在真实项目里怎么自动化：../storage/ 讲（把截图存成基线）。
 *
 * ——— 小练习 ———
 *   1. 把步骤⑦的 `.QPushButton { background-color:#455a64; }` 这一条**删掉**，
 *      重跑并对照打印：plainBtn 和 fancy 的取色是不是变成一样了？
 *      由此确认「类选择器只是给正好这个类加了一条更强的规则」。
 *      再把 `.QPushButton` 改成 `.Fancy`，看谁变颜色（这一步会让你彻底记住子类那件事）。
 *   2. 把 primaryBtn 的 setProperty 挪到 app.setStyleSheet(darkSheet()) **之后**执行，
 *      再在挪动后的位置加一次 `style()->unpolish/polish`，
 *      对照两次打印的取色，把「动态属性选择器要在 polish 前设好」这一条亲手做出来。
 *   3. 把 batch 窗口的 240 改成 20，重跑步骤⑧的三轮计时，把两组毫秒数并排记下来；
 *      然后给 darkSheet() 的第一条规则改成 `* { padding: 1px; }`（通配）再跑一次，
 *      看代价怎么涨。最后回答：如果你的产品有 800 个控件，换肤应该做几件事？
 *      （提示：一份串、一次调用、一套基线截图。）
 */
// ============================================================================

// 本文件定义了带 Q_OBJECT 的两个类（Fancy / PanelBox）：CMAKE_AUTOMOC 会生成 moc 代码，
// 在 .cpp 里定义的 Q_OBJECT 类必须在**文件末尾**包含它（第 1 讲讲过为什么）。
#include "09_style_sheets.moc"
