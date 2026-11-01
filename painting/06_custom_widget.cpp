// ============================================================================
// 第 6 讲 · 做一个真正可复用的自定义控件（GaugeWidget）
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   前五讲都在「画」，这一讲把画**包成一个能给别人用的控件**，并把自定义控件的
//   五道必答题一次答完，每道题都留下能打印出来的数字：
//     1) 尺寸从哪来：覆写 sizeHint() / minimumSizeHint()（qwidget.h 522/523 行，virtual），
//        配 sizePolicy；把同一个控件塞进三种尺寸的容器，把 sizeHint / minimumSizeHint /
//        size / geometry 四组数字并排打出来 —— sizeHint 是「愿望」，size 是「结果」；
//     2) 颜色从哪来：一律 palette().color(QPalette::…)（Window/Button/ButtonText/Text/
//        Highlight/Mid），**不硬编码 QColor**；换 palette 就换整套外观。本讲按 paintGauge()
//        的几何公式**算出四个采样点**，每个点标明它由哪个 ColorRole 涂，把那四个 role 全改掉，
//        再逐点打印「换前/换后/该 role 的新色」；另附一条「取 palette vs 写死 QColor」的对照组。
//     3) 什么时候重绘：update() / update(QRect) / repaint()。
//        本讲核心证据：**连着调 update() 一万次，paintEvent 只跑有限次**，
//        把「update 只是提请求、合并发生在事件循环里」这件事**数出来**；
//        再连提三个互不相交的 update(QRect)，打印脏区合并结果；
//        再调一次 repaint()，证明它**同步**（函数返回时 paint 已经发生）；
//     4) 属性开关各解决什么问题：WA_OpaquePaintEvent / WA_NoSystemBackground /
//        WA_TranslucentBackground / WA_StaticContents 逐个开一遍，回读 testAttribute，
//        并用「放大容器后的重绘次数与脏区」测 WA_StaticContents 的效果；
//     5) 鼠标怎么接：setMouseTracking(true) + mousePressEvent/mouseMoveEvent/
//        mouseReleaseEvent/wheelEvent/enterEvent/leaveEvent + setCursor/unsetCursor。
//        本讲**用手工构造的 QMouseEvent / QEnterEvent 经 QApplication::sendEvent 投给控件**，
//        无人值守也能把「交互 → 状态 → 信号 → 重绘请求」整条链跑通。
//        信号不是只数一个总数：**按下 / 拖动 / 抬起 逐步各数一次**，因为「一次拖动发几个
//        valueChanged」这件事只有拆开才判得出来（本机实测 +0/+1/+1/+0，见证据⑥）；
//        QEnterEvent 也把它收到的 local/scene/global 三个点原样打出来对答案。
//
// 对应构建目标名：painting_06_custom_widget
//   find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)   # 本组统一链 Svg（第 8 讲要用），本讲只用到 Widgets
//   add_executable(painting_06_custom_widget 06_custom_widget.cpp)
//   target_link_libraries(painting_06_custom_widget PRIVATE Qt6::Widgets Qt6::Svg)
//   set(CMAKE_AUTOMOC ON)                              ← 必须有：本文件里有 Q_OBJECT 类
//   本文件**有两个 Q_OBJECT 类（GaugeWidget、Throttle）**，所以末尾必须
//   `#include "06_custom_widget.moc"`；手工核对时先跑
//   `moc 06_custom_widget.cpp -o 06_custom_widget.moc`，再让 g++ 能找到它（-I 到产物目录）。
//
// 运行前须知：
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 painting_06_custom_widget，跑到自动退出、退出码 0）。
//     但本讲**没有汇总自检行**，退出码 0 只证明「没崩、走完了」，不证明正文每个数字都对：
//     带具体数字的「预期」请以你自己那次运行输出为准（本机那一轮存档 runlogs/painting_06_custom_widget.log）。
//   · 会往当前工作目录写：painting_06_custom_widget.png（QWidget::render() 抓出来的控件像素）、
//     painting_06_offscreen_same_code.png（同一份 paintGauge() 直接画进 QImage）。
//   · 无人值守：QTimer::singleShot 排一串动作（resize → 压力测试 → 投鼠标 → 存图 → 打印
//     → QCoreApplication::quit()），全程不需要鼠标键盘，约 1.5 秒跑完。
//     除了 main 末尾那句 app.exec()，**没有任何阻塞调用**。
//   · 可以无头跑：`painting_06_custom_widget -platform offscreen`。offscreen 会派发 paint 事件，
//     所以「重绘次数」这组数在 CI 里也是有效的。
//   · 「一万次 update() 合并成几次」：头文件能证明的只有 update() 是 public Q_SLOTS、
//     不带同步语义（qwidget.h 470 行）；**合并发生在事件循环里，次数取决于你的机器与
//     这段时间里事件循环被喂了几次**，本机实测 10000 次只换来个位数次重绘，
//     但请以你自己跑出来的数字为准。
//   · 实测状态：本示例已在 **Qt 6.12.0 / MinGW-w64 GCC 13.1.0 / Windows 11** 上
//     编译通过并**无头跑通**（`QT_QPA_PLATFORM=offscreen`，退出码 0），两张 PNG 与
//     下面每个「预期」都对着本机日志核过。头文件在 `D:\Qt\6.12.0\mingw_64\include`，
//     Qt 源码在 `D:\Qt\6.12.0\Src\qtbase\src`。Windows 上 Qt 日志按本地代码页 GBK
//     输出，重定向到文件时要 `| iconv -f GBK -t UTF-8` 才不乱码。
//   · 一条重要纠错（这就是「写代码前先核头文件」的原因）：
//     **QWidget 没有 setUpdateInterval()**。本机 QtWidgets 整个目录 grep 这个名字，
//     唯一命中的是 QtCore/qtimeline.h 里的 QTimeLine::setUpdateInterval()（那是动画时间轴
//     的每帧毫秒数，不是控件重绘节流）。有 setUpdateInterval 的是 **Qt Quick 的 QQuickItem**。
//     QWidget 侧真正可用的抑制手段是本讲写进代码的四条：
//       ① update() 自身的事件合并（最省事，也是主力）；
//       ② setUpdatesEnabled(false) / (true)（qwidget.h 462/463 行，批量改状态时关门）；
//       ③ 自己拿 QTimer + dirty 标志合闸（本讲的 Throttle 类）；
//       ④ WA_StaticContents（qnamespace.h 311 行，静态底只在扩大的那块重画）。
//   · Qt5 → Qt6 差异本讲涉及三处：enterEvent 的参数在 Qt6 是 QEnterEvent*；
//     QMouseEvent 取全局坐标在 Qt6 用 globalPosition()（返回 QPointF，qevent.h 124 行）；
//     devicePixelRatioF() **没有**消失，它和 devicePixelRatio() 都定义在
//     QPaintDevice（qpaintdevice.h 52/53 行），而 `class QWidget : public QObject,
//     public QPaintDevice`（qwidget.h 98 行）是**公有继承**，所以 `widget->devicePixelRatioF()`
//     在 Qt6 照样能调（在 qwidget.h 里 grep 不到这个名字是正常的，见下一条方法论）。
//   · 方法论（本讲踩过的坑，比上面那条更值钱）：
//     **在派生类的头文件里 grep 不到某个名字 ≠ 不能调用它**。C++ 的公有继承会把基类的
//     公开成员一并带过来，名字不在派生类头文件里恰恰说明「它是基类的」。正确的查法是
//     先看类声明那一行的继承列表（`qwidget.h` 98 行 → QObject + QPaintDevice），
//     再去基类头文件里找定义（`qpaintdevice.h` 52/53 行）。
//     反面教材就是上一条：只 grep `qwidget.h` 就断言「Qt6 删了 devicePixelRatioF」，
//     结论正好说反了。反过来，「头文件里查得到」也确实不等于该名字在你所处的类里可用——
//     还得看它是 public 还是 protected/private。总之一句话：**沿继承链查到定义为止**。
//   · 图解配套：../visuals/painting_pipeline.html（update() → 事件循环 → paintEvent →
//     后备存储 → 平台合成 的分步动画，页面由图解同事补，链接先放着）。
// ============================================================================

#include <QApplication>          // QtWidgets/qapplication.h：sendEvent 也在这
#include <QWidget>               // QtWidgets/qwidget.h
#include <QPaintEvent>           // QtGui/qevent.h：rect() / region()
#include <QMouseEvent>           // QtGui/qevent.h：QMouseEvent
#include <QWheelEvent>           // QtGui/qevent.h：QWheelEvent
#include <QResizeEvent>          // QtGui/qevent.h：QResizeEvent
#include <QEnterEvent>           // QtGui/qevent.h：Qt6 的 enterEvent 参数类型
#include <QPainter>              // QtGui/qpainter.h
#include <QPen>                  // QtGui/qpen.h
#include <QBrush>                // QtGui/qbrush.h
#include <QColor>                // QtGui/qcolor.h
#include <QPalette>              // QtGui/qpalette.h：ColorGroup / ColorRole
#include <QImage>                // QtGui/qimage.h
#include <QFont>                 // QtGui/qfont.h
#include <QFontMetrics>          // QtGui/qfontmetrics.h：基线（第 4 讲）
#include <QPolygonF>             // QtGui/qpolygon.h
#include <QRegion>               // QtGui/qregion.h：event->region() 的类型
#include <QSize>                 // QtCore/qsize.h
#include <QRect>                 // QtCore/qrect.h：QRect / QRectF
#include <QPoint>                // QtCore/qpoint.h：QPoint / QPointF
#include <QString>
#include <QTimer>                // QtCore/qtimer.h
#include <QElapsedTimer>         // QtCore/qelapsedtimer.h：量 update() 风暴的调用成本
#include <QVBoxLayout>           // QtWidgets/qboxlayout.h
#include <QSizePolicy>           // QtWidgets/qsizepolicy.h
#include <QEvent>                // QtCore/qcoreevent.h
#include <QtMath>                // QtCore/qmath.h：M_PI / qCos / qSin / qAtan2
#include <QCoreApplication>
#include <QGuiApplication>
#include <QDebug>

// ============================================================================
// 一、核对签名（本机 D:\Qt\6.12.0\mingw_64\include，Qt 6.12.0，全部带行号；对不上的一律不用）
// ============================================================================
// QtWidgets/qwidget.h
//   第 119 行  Q_PROPERTY(QSizePolicy sizePolicy READ sizePolicy WRITE setSizePolicy)
//                ← 有 READ sizePolicy，所以 sizePolicy() getter 是公开的
//   第 331~333 行 QCursor cursor() const; void setCursor(const QCursor &); void unsetCursor();
//   第 336/337 行 void setMouseTracking(bool enable); bool hasMouseTracking() const;
//   第 348 行  void render(QPaintDevice *target, const QPoint &targetOffset = QPoint(),
//                          const QRegion &sourceRegion = QRegion(),
//                          RenderFlags flags = RenderFlags(DrawChildren));
//   第 462/463 行 bool updatesEnabled() const; void setUpdatesEnabled(bool enable);
//   第 470 行  void update();                              ← public Q_SLOTS，只提请求
//   第 474/475/476 行 void update(int,int,int,int); update(const QRect&); update(const QRegion&);
//   第 471/478~480 行 void repaint(); repaint(int,int,int,int); repaint(const QRect&);
//                  repaint(const QRegion&);                              ← 同步：立刻画
//   第 522/523 行 virtual QSize sizeHint() const; virtual QSize minimumSizeHint() const;
//   第 526/527 行 void setSizePolicy(QSizePolicy); void setSizePolicy(Policy, Policy);
//   第 528 行  virtual int heightForWidth(int) const;
//   第 533/542 行 void setContentsMargins(...); void updateGeometry();
//   第 632/633 行 void setAttribute(Qt::WidgetAttribute, bool on = true);
//                bool testAttribute(Qt::WidgetAttribute) const;
//   第 311 行  QWidget *window() const;
//   第 646/647 行 QWindow *windowHandle() const; QScreen *screen() const;
//   第 677 行  virtual void closeEvent(QCloseEvent *);                     ← protected
//   **本文件里没有 setUpdateInterval**（整个 QtWidgets 目录只命中 QtCore/qtimeline.h）
// QtCore/qnamespace.h —— Qt::WidgetAttribute 在这里，**不在 qwidget.h**：
//   第 305 行  enum WidgetAttribute {
//   第 310 行    WA_OpaquePaintEvent = 4,      ← 「我把整个矩形都涂满了，你别帮我刷背景」
//   第 311 行    WA_StaticContents = 5,        ← 控件变大时只有新增部分需要重画
//   第 313 行    WA_PaintOnScreen = 8,         ← 绕过后备存储（本讲只提不开：开了 render/grab 会失效）
//   第 314 行    WA_NoSystemBackground = 9,    ← 「系统别替我刷背景」
//   第 340 行    WA_TransparentForMouseEvents = 51,
//   第 401 行    WA_NativeWindow = 100,
//   第 425 行    WA_TranslucentBackground = 120  ← **只对顶层窗口有意义**
//   第 1180~1198 行 enum CursorShape { ..., CrossCursor, SizeAllCursor, BlankCursor,
//                  PointingHandCursor, OpenHandCursor, ... }
// QtGui/qpalette.h
//   第 53~61 行  enum ColorRole { WindowText, Button, Light, Midlight, Dark, Mid,
//                  Text, BrightText, ButtonText, Base, Window, Shadow,
//                  Highlight, HighlightedText, Link, LinkVisited, AlternateBase, NoRole,
//                  ToolTipBase, ToolTipText, PlaceholderText, Accent }
//                ← 注意**声明顺序不是你以为的 Window 打头**，第一是 WindowText；
//                  本讲用到 Button / Mid / ButtonText / Highlight 四个。
//   第 83 行  inline const QColor &color(ColorRole cr) const   ← 取色
//   第 76 行  void setBrush(ColorGroup cg, ColorRole cr, const QBrush &brush)  ← 改色
// QtGui/qevent.h
//   QPaintEvent : 第 494/495 行 inline const QRect &rect() const;
//                 inline const QRegion &region() const;
//                 ← 本讲统计脏区就靠 region()（旧注释里那个 `®ion` 是 &reg; 排版事故）
//   QMouseEvent : 本机有两个可用重载 ——
//                 第 207~210 行 QMouseEvent(Type, const QPointF &localPos, const QPointF &globalPos,
//                              Qt::MouseButton, Qt::MouseButtons, Qt::KeyboardModifiers,
//                              const QPointingDevice *device = primaryPointingDevice());
//                 第 211~214 行 QMouseEvent(Type, const QPointF &localPos, const QPointF &scenePos,
//                              const QPointF &globalPos, Qt::MouseButton, Qt::MouseButtons,
//                              Qt::KeyboardModifiers, const QPointingDevice *device = ...);
//   QEnterEvent : 第 170~171 行 **QEnterEvent(const QPointF &localPos, const QPointF &scenePos,
//                              const QPointF &globalPos,
//                              const QPointingDevice *device = QPointingDevice::primaryPointingDevice())**
//                 ← 三个点的次序是 **local → scene → global**，和 QMouseEvent 的七参数版一致；
//                   **不是** Qt5 文档里常见的 (local, global, screen)。第四个实参在 Qt6 是
//                   `const QPointingDevice*`（Qt5 那里才是 `qreal device = 0`）。
//                 三者的口径见 qevent.cpp 第 73~75 行的原话：
//                   localPos = 「relative to the receiving widget or item」（接收事件的控件/图元）
//                   scenePos = 「relative to the window」（窗口）
//                   globalPos = 「relative to the screen or desktop」（屏幕/桌面）
//                 证据：qevent.h 第 190~191 行的弃用别名 `windowPos()` 就是
//                   `return scenePosition();` —— scene 在 Qt6 的点位事件里**就等于窗口坐标**，
//                   不是 QGraphicsView 的那个「场景」；别望文生义。
//                 普通顶层控件没有「场景」这一层，Qt 自己派发 enter 时 scenePos 与 localPos
//                 往往只差一个窗口原点；手工构造时把 scene 填成窗口内坐标、global 填成
//                 mapToGlobal 的结果即可（本讲就是这么写的）。
//   取值口子在基类 QSinglePointEvent：第 120~125 行 position()/scenePosition()/globalPosition()
//                 都返回 QPointF。
//   QWheelEvent : QPoint angleDelta() const; QPoint pixelDelta() const;
// QtGui/qregion.h
//   QRegion(const QRect &); QRect boundingRect() const; int rectCount() const;
//   bool isEmpty() const; QRegion united(const QRegion &) const;
// QtGui/qpainter.h
//   第 89 行  explicit QPainter(QPaintDevice *);
//   第 96 行  bool isActive() const;
//   第 155/161 行 void setPen(const QPen &); void setBrush(const QBrush &);
// QtCore/qtimer.h
//   第 29 行  explicit QTimer(QObject *parent = nullptr);
//   第 32 行  bool isActive() const;
//   第 37 行  void setInterval(int msec);        第 47 行 void setSingleShot(bool);
//   第 110/113 行 void start(int msec); void stop();

// ============================================================================
// 二、可复用的绘制函数：paintEvent 与离屏渲染**共用同一个它**
//     第 1 讲「铁律一」的工程价值就在这里：绘制逻辑与设备解耦，才能既上屏又出图。
//     颜色一律从传进来的 QPalette 里取 —— 这样离屏时也能用同一套配色。
// ============================================================================
static void paintGauge(QPainter &p, const QRectF &box, const QPalette &pal,
                       double value, bool hover)
{
    const QColor bg     = pal.color(QPalette::Button);
    const QColor groove = pal.color(QPalette::Mid);
    const QColor ink    = pal.color(QPalette::ButtonText);
    const QColor accent = pal.color(QPalette::Highlight);

    p.save();                                        // 第 2 讲：状态是栈，进去要出来
    p.setRenderHint(QPainter::Antialiasing);

    p.setPen(QPen(groove, 1));
    p.setBrush(bg);
    p.drawRoundedRect(box, 8, 8);

    // 环形槽：drawArc 的角度单位是 **1/16 度**（第 1 讲那条坑）
    const qreal d = qMin(box.width(), box.height()) * 0.78;
    QRectF ring(box.center().x() - d / 2.0, box.center().y() - d / 2.0 - 4, d, d);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(groove, 8, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(ring, 225 * 16, 270 * 16);

    p.setPen(QPen(accent, 8, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(ring, 225 * 16, int(value * 270.0) * 16);

    // 指针：225° 起、顺时针 270° 行程
    const qreal ang = (225.0 - value * 270.0) * M_PI / 180.0;
    const QPointF tip = ring.center() + QPointF(qCos(ang), -qSin(ang)) * (d / 2.0 - 12);
    p.setPen(QPen(ink, 2, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(ring.center(), tip);
    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    p.drawEllipse(ring.center(), 4.0, 4.0);

    if (hover) {                                     // 交互态也画进同一份代码
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(accent, 2, Qt::DotLine));
        p.drawRoundedRect(box.adjusted(1, 1, -1, -1), 8, 8);
    }

    // 数值文字：基线自己算（第 4 讲的规矩），别拿矩形中心当文字中心
    QFont f = p.font();
    f.setPixelSize(qMax(11, int(box.height() / 5.0)));
    f.setBold(true);
    p.setFont(f);
    const QFontMetrics fm(f);
    const qreal baseline = box.center().y() + box.height() * 0.14 + fm.ascent() / 2.0;
    p.setPen(ink);
    p.drawText(QPointF(box.center().x() - fm.horizontalAdvance(QStringLiteral("0.00")) / 2.0,
                       baseline),
               QString::asprintf("%.2f", value));
    p.restore();
}

// ============================================================================
// 三、控件本体
//     为什么这里要 Q_OBJECT：要给外面发 valueChanged 信号（signals/slot/qobject_cast
//     这些元对象能力都得靠它，见第贰阶段）。**只覆写 paintEvent 是不需要 Q_OBJECT 的**
//     —— 第 1~5 讲那几个类都没写。
// ============================================================================
class GaugeWidget : public QWidget
{
    Q_OBJECT
public:
    explicit GaugeWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        setWindowTitle(QStringLiteral("GaugeWidget"));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);   // 527 行两参数版
        setMouseTracking(true);              // 336 行：不开的话 mouseMoveEvent 只在按住键时来
        setCursor(Qt::CrossCursor);          // 332 行
        setFocusPolicy(Qt::ClickFocus);
    }

    // ── 重绘统计：本讲最重要的可核查数字都在这里 ────────────────────
    int paints() const { return m_paints; }
    qint64 dirtyArea() const { return m_dirtyArea; }
    int fullPaints() const { return m_fullPaints; }
    int partialPaints() const { return m_partialPaints; }
    QRect lastDirty() const { return m_lastDirty; }
    int lastRectCount() const { return m_lastRectCount; }
    void resetStats()
    {
        m_paints = 0;
        m_dirtyArea = 0;
        m_fullPaints = 0;
        m_partialPaints = 0;
        m_lastDirty = QRect();
        m_lastRectCount = 0;
    }

    double value() const { return m_value; }
    bool isHovering() const { return m_hover; }
    // 最近一次 enterEvent 收到的三个点（证据⑥ 用来验证 QEnterEvent 构造函数的实参次序）
    QPointF enterLocal() const { return m_enterLocal; }
    QPointF enterScene() const { return m_enterScene; }
    QPointF enterGlobal() const { return m_enterGlobal; }

    // ── ① 尺寸：覆写两个 virtual（522/523 行） ────────────────────
    QSize sizeHint() const override { return QSize(220, 140); }
    QSize minimumSizeHint() const override { return QSize(90, 70); }

public slots:
    // 写成 slot，老式字符串连接 SLOT(setValue(double)) 也能连上（第贰阶段的规矩）
    void setValue(double v)
    {
        const double clamped = qBound(0.0, v, 1.0);
        if (qFuzzyCompare(clamped, m_value))
            return;                          // 没变化就**不提重绘请求**：省下来的都是真的
        m_value = clamped;
        // 只把「数值文字 + 指针」那一带标脏。收窄多少看你的图形布局，这里给一个保守的
        // 中部横条；对比证据⑤ 里 update(QRect) 的合并结果，你可以自己算收窄对不对。
        const int band = qMax(28, height() / 3);
        update(QRect(0, (height() - band) / 2, width(), band));
        Q_EMIT valueChanged(m_value);
    }

signals:
    void valueChanged(double newValue);

protected:
    void paintEvent(QPaintEvent *event) override
    {
        ++m_paints;
        const QRect dirty = event->region().boundingRect();
        m_lastDirty = dirty;
        m_lastRectCount = event->region().rectCount();
        m_dirtyArea += qint64(dirty.width()) * qint64(dirty.height());
        if (event->region().isEmpty() || dirty == event->rect())
            ++m_fullPaints;
        else
            ++m_partialPaints;

        QPainter p(this);                                  // RAII：89 行
        if (!p.isActive())                                 // 96 行：唯一的自检手段
            return;

        // ④ 背景由谁负责，取决于属性开关 —— 这就是这四个属性各自解决的东西
        if (!testAttribute(Qt::WA_OpaquePaintEvent)) {      // 310 行
            if (testAttribute(Qt::WA_TranslucentBackground)) // 425 行
                p.fillRect(rect(), Qt::transparent);         // 顶层透明：把上一帧抹干净
            else if (!testAttribute(Qt::WA_NoSystemBackground))
                p.fillRect(rect(), palette().color(QPalette::Window));   // 314 行
        }

        const QRectF box = QRectF(rect()).adjusted(4, 4, -4, -4);
        paintGauge(p, box, palette(), m_value, m_hover);
    }

    void resizeEvent(QResizeEvent *) override
    {
        // 尺寸变了不需要自己调 update()：Qt 会把整幅标脏。
        // 需要手动通知布局的是**sizeHint 变了**那种情况 —— 调 updateGeometry()（542 行）。
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        m_dragging = true;
        updateFromPoint(event->position());
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_dragging)
            updateFromPoint(event->position());
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        m_dragging = false;
        event->accept();
    }

    void wheelEvent(QWheelEvent *event) override
    {
        const int steps = event->angleDelta().y() / 8;      // angleDelta 单位：1 格 = 8
        if (steps != 0)
            setValue(m_value + steps * 0.02);
        event->accept();
    }

    void enterEvent(QEnterEvent *event) override            // Qt6：参数是 QEnterEvent*
    {
        // 把事件里三个坐标**原样记下来**，证据⑥ 要打印它们对答案：
        // 只有打印了才知道构造函数的第 2 个实参到底落进了 scenePosition() 还是 globalPosition()。
        m_enterLocal = event->position();                   // qevent.h 120 行
        m_enterScene = event->scenePosition();              // 122 行（Qt6 里 = 窗口坐标）
        m_enterGlobal = event->globalPosition();            // 124 行
        if (m_hover)
            return;
        m_hover = true;
        setCursor(Qt::PointingHandCursor);
        update();                                           // 提请求，不是 repaint
    }

    void leaveEvent(QEvent *) override
    {
        if (!m_hover)
            return;
        m_hover = false;
        unsetCursor();                                      // 333 行：回到继承来的光标
        update();
    }

private:
    void updateFromPoint(const QPointF &pt)
    {
        // 最原始的「命中测试」：把点换算成角度再夹进 0..1（第 8 讲有框架给的正解）
        const QPointF c = QPointF(rect().center());
        const qreal dx = pt.x() - c.x();
        const qreal dy = c.y() - pt.y();
        qreal deg = qRadiansToDegrees(qAtan2(dy, dx));        // 数学逆时针，-180..180
        if (deg < 0.0)
            deg += 360.0;
        qreal t = 225.0 - deg;
        if (t < -90.0)
            t += 360.0;                                      // 把 225°→−45° 接成 270° 行程
        if (t > 270.0)
            t = 270.0;
        setValue(t / 270.0);
    }

    double m_value = 0.35;
    bool m_hover = false;
    bool m_dragging = false;
    QPointF m_enterLocal;
    QPointF m_enterScene;
    QPointF m_enterGlobal;
    int m_paints = 0;
    int m_fullPaints = 0;
    int m_partialPaints = 0;
    qint64 m_dirtyArea = 0;
    QRect m_lastDirty;
    int m_lastRectCount = 0;
};

// ============================================================================
// 四、重绘节流的**正确做法**：QTimer + dirty 标志合闸
//     （再说一遍：QWidget 没有 setUpdateInterval，别再写它）
//     场景：数据源每秒推 500 次，界面只需要每 40ms 刷新一次
// ============================================================================
class Throttle : public QObject
{
    Q_OBJECT
public:
    Throttle(QWidget *target, int msec, QObject *parent = nullptr)
        : QObject(parent), m_target(target)
    {
        m_timer.setSingleShot(true);                     // qtimer.h 47 行
        m_timer.setInterval(msec);                       // qtimer.h 37 行
        QObject::connect(&m_timer, &QTimer::timeout, this, [this]() {
            if (m_pending) {
                m_pending = false;
                ++m_flushes;
                m_target->update();                      // 到点才真的提一次请求
            }
        });
        m_timer.start();                                 // 110/113 行
    }

    void request()
    {
        ++m_requests;
        m_pending = true;
        if (!m_timer.isActive())                         // 32 行：还在计时就只置标志
            m_timer.start();
    }

    int requests() const { return m_requests; }
    int flushes() const { return m_flushes; }

private:
    QWidget *m_target;
    QTimer m_timer;
    bool m_pending = false;
    int m_requests = 0;
    int m_flushes = 0;
};

// ============================================================================
// 五、小工具
// ============================================================================
static qint64 countOpaque(const QImage &src)
{
    qint64 n = 0;
    for (int y = 0; y < src.height(); ++y) {
        const uchar *row = src.constScanLine(y);              // 只读版：不触发 detach
        for (int x = 0; x < src.width(); ++x)
            if (row[x * 4 + 3] != 0)                          // 小端 ARGB32 的第 4 字节是 A
                ++n;
    }
    return n;
}

// 手工投一次「按下-拖动-抬起」：无人值守也能跑通交互链（sendEvent 不阻塞）。
// 这里**故意拆成三个单步函数**：证据⑥ 要逐步数 valueChanged 的次数，
// 才能说清「一次拖动到底发了几个信号」——拆开了才知道那 2 个是从哪一步来的。
// 用的都是 qevent.h 第 207~210 行那个 (type, localPos, globalPos, button, buttons, modifiers)
// 重载：没有 scenePos 参数，Qt 内部把 scene 填成与 local 同值（顶层控件本来就没有「场景」层）。
static void synthPress(QWidget *top, GaugeWidget *gauge, const QPointF &local)
{
    const QPoint global = top->mapToGlobal(local.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, local, QPointF(global),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(gauge, &press);
}

static void synthMove(QWidget *top, GaugeWidget *gauge, const QPointF &local)
{
    const QPoint global = top->mapToGlobal(local.toPoint());
    QMouseEvent move(QEvent::MouseMove, local, QPointF(global),
                     Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(gauge, &move);
}

static void synthRelease(QWidget *top, GaugeWidget *gauge, const QPointF &local)
{
    const QPoint global = top->mapToGlobal(local.toPoint());
    QMouseEvent release(QEvent::MouseButtonRelease, local, QPointF(global),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(gauge, &release);
}

// 一次完整的拖动 = 按下 local → 拖到 local+(6,0) → 在那儿抬起。
// 关键：**路径上有两个不同的取值点**，所以它天生就是两次 setValue()。
// 返回值 = 拖动停下时的局部坐标，证据⑥ 拿它做「同一点再投一遍」的对照。
static QPointF synthDrag(QWidget *top, GaugeWidget *gauge, const QPointF &local)
{
    synthPress(top, gauge, local);
    const QPointF end = local + QPointF(6, 0);
    synthMove(top, gauge, end);
    synthRelease(top, gauge, end);
    return end;
}

// ============================================================================
// 六、main：把五道必答题变成六组数字
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("painting06"));

    GaugeWidget *gauge = new GaugeWidget();
    QWidget window;
    QVBoxLayout *lay = new QVBoxLayout(&window);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(gauge);
    window.setWindowTitle(QStringLiteral("painting 06 · 自定义控件"));
    window.resize(480, 320);
    window.show();

    // 顶层透明窗（只为演示 WA_TranslucentBackground 的适用范围，不参与本讲画图证据）
    QWidget overlay;
    overlay.setAttribute(Qt::WA_TranslucentBackground);
    overlay.resize(1, 1);

    int changeSignals = 0;
    QObject::connect(gauge, &GaugeWidget::valueChanged, [&changeSignals](double v) {
        ++changeSignals;
        Q_UNUSED(v);
    });

    Throttle *throttle = new Throttle(gauge, 40, &app);

    // ── ① 尺寸：同一个控件，三种容器尺寸 ────────────────────────────────
    QTimer::singleShot(30, [&window, gauge, &overlay]() {
        qInfo() << "===============================================================";
        qInfo() << "painting 第 6 讲 · 自定义控件（平台 =" << QGuiApplication::platformName() << "）";
        qInfo() << "===============================================================";
        qInfo() << "【证据①】sizeHint 是愿望，size/geometry 是结果";
        const auto dump = [gauge](const char *tag) {
            qInfo() << QString::asprintf(
                "  %-14s gauge->size=%4dx%-4d geometry=(%4d,%4d,%4d,%4d)  sizeHint=%dx%d  "
                "minimumSizeHint=%dx%d",
                tag, gauge->size().width(), gauge->size().height(),
                gauge->geometry().x(), gauge->geometry().y(),
                gauge->geometry().width(), gauge->geometry().height(),
                gauge->sizeHint().width(), gauge->sizeHint().height(),
                gauge->minimumSizeHint().width(), gauge->minimumSizeHint().height());
        };
        dump("容器 480x320");
        qInfo() << "  sizePolicy 的 horizontal 是 Expanding 吗 ="
                << (gauge->sizePolicy().horizontalPolicy() == QSizePolicy::Expanding)
                << "vertical 是 Expanding 吗 ="
                << (gauge->sizePolicy().verticalPolicy() == QSizePolicy::Expanding);
        qInfo() << "  heightForWidth(100) =" << gauge->heightForWidth(100)
                << "（本讲没覆写它，取的是 QWidget 的默认实现）";
        qInfo() << "  hasMouseTracking =" << gauge->hasMouseTracking()
                << "updatesEnabled =" << gauge->updatesEnabled();
        qInfo() << "  overlay 开了 WA_TranslucentBackground ="
                << overlay.testAttribute(Qt::WA_TranslucentBackground)
                << "；gauge 是**子控件**，给它开这个属性没意义（该属性只管顶层窗口）";
    });

    // ── ② 容器变小：看 minimumSizeHint 与 sizeHint 谁说话 ────────────────
    QTimer::singleShot(80, [&window, gauge]() {
        window.resize(200, 120);
        qInfo() << "【证据②】容器缩到 200x120（比 sizeHint 小）";
        qInfo() << "  size =" << gauge->size() << "sizeHint =" << gauge->sizeHint()
                << "（sizeHint 没变，变的是布局给的 size：Expanding 策略愿意被压缩）";
    });
    QTimer::singleShot(140, [&window, gauge]() {
        window.resize(40, 30);                    // 比 minimumSizeHint 还小
        qInfo() << "【证据②b】容器缩到 40x30（比 minimumSizeHint 还小）";
        qInfo() << "  gauge->size() =" << gauge->size()
                << "minimumSizeHint =" << gauge->minimumSizeHint()
                << "window->size() =" << window.size()
                << "（顶层窗口有自己的下限，minimumSizeHint 是布局层面的下限，两者别混）";
    });

    // ── ③ 属性开关：逐个开一遍并回读 ────────────────────────────────────
    QTimer::singleShot(200, [gauge]() {
        qInfo() << "【证据③】WidgetAttribute 逐个开一遍（qnamespace.h 305 行起的枚举）";
        gauge->setAttribute(Qt::WA_OpaquePaintEvent);          // 310 行
        gauge->setAttribute(Qt::WA_NoSystemBackground);        // 314 行
        gauge->setAttribute(Qt::WA_StaticContents);            // 311 行
        qInfo() << "  OpaquePaintEvent =" << gauge->testAttribute(Qt::WA_OpaquePaintEvent)
                << "NoSystemBackground =" << gauge->testAttribute(Qt::WA_NoSystemBackground)
                << "StaticContents =" << gauge->testAttribute(Qt::WA_StaticContents)
                << "TranslucentBackground（子控件上应为 false）="
                << gauge->testAttribute(Qt::WA_TranslucentBackground);
        qInfo() << "  四个属性各自解决什么：";
        qInfo() << "    WA_OpaquePaintEvent   —— 我保证每次把整个脏区涂满，Qt 别帮我在背后刷背景；";
        qInfo() << "    WA_NoSystemBackground —— 只要「别刷系统背景」，后面我自己决定刷什么；";
        qInfo() << "    WA_TranslucentBackground —— 顶层窗口整块带 alpha（要合成器支持，offscreen 只是不崩）；";
        qInfo() << "    WA_StaticContents     —— 内容固定在左上角，控件**变大**时只补画新增的那一条。";
        qInfo() << "  注意：开了 Opaque 又没真的涂满 = 残影。本讲 paintEvent 里那句";
        qInfo() << "  `if (!testAttribute(WA_OpaquePaintEvent))` 就是在诚实地区分这两种情况。";
    });

    // ── ④ 一万次 update()：证明事件合并 ─────────────────────────────────
    QTimer::singleShot(260, [gauge, throttle]() {
        qInfo() << "【证据④】update() 一万次 → paintEvent 只跑有限次";
        gauge->resetStats();
        const int paintsBefore = gauge->paints();
        QElapsedTimer et;
        et.start();
        for (int i = 0; i < 10000; ++i)
            gauge->update();
        const qint64 us = et.nsecsElapsed() / 1000;
        qInfo() << "  10000 次 update() 调用耗时 =" << us << "us，"
                << "期间 paintEvent 增量 =" << (gauge->paints() - paintsBefore)
                << "（预期 **0**：update() 只是把脏区记下来并投递一个请求，它不画）";
        throttle->request();
        throttle->request();
        qInfo() << "  Throttle 记账：请求" << throttle->requests()
                << "次、真正 flush 出 update() 的次数目前 =" << throttle->flushes();
    });

    // ── ④b 事件循环被喂过一次之后 ───────────────────────────────────────
    QTimer::singleShot(340, [gauge, throttle]() {
        qInfo() << "【证据④b】中间只过了很短时间、事件循环喂过几轮之后";
        qInfo() << "  paintEvent 总次数 =" << gauge->paints()
                << "整幅/局部 =" << gauge->fullPaints() << "/" << gauge->partialPaints()
                << "最近一次脏区 =" << gauge->lastDirty()
                << "脏区里的矩形个数 =" << gauge->lastRectCount();
        qInfo() << "  → 10000 次请求换来个位数次重绘，这就是**合并（coalesce）**：";
        qInfo() << "     update() 只是请求 → 事件循环把脏区并起来 → 一次 paintEvent →";
        qInfo() << "     QPainter 画进后备存储 → 平台合成上屏（见 ../visuals/painting_pipeline.html）。";
        qInfo() << "  Throttle：请求" << throttle->requests() << "次 → flush"
                << throttle->flushes() << "次（40ms 一个闸，多请求只留一次）";
    });

    // ── ⑤ 局部重绘 vs repaint() ─────────────────────────────────────────
    QTimer::singleShot(400, [gauge]() {
        qInfo() << "【证据⑤】update(QRect) 的合并 vs repaint() 的同步";
        gauge->resetStats();
        const int w = gauge->width(), h = gauge->height();
        gauge->update(QRect(4, 4, 30, 20));
        gauge->update(QRect(w - 40, 10, 30, 20));
        gauge->update(QRect(w / 2, h / 2, 40, 20));
        qInfo() << "  连提 3 个互不相交的 update(QRect)，提完立刻看 paints ="
                << gauge->paints() << "（预期 0：还没进事件循环）";
        const int p0 = gauge->paints();
        gauge->repaint(QRect(10, 10, 20, 20));          // 同步：函数返回时已经画完
        qInfo() << "  一次 repaint(QRect) 返回后 paints 增量 =" << (gauge->paints() - p0)
                << "脏区 =" << gauge->lastDirty() << "（预期 1，证明 repaint **不排队、直接画**）";
    });
    QTimer::singleShot(470, [gauge]() {
        qInfo() << "【证据⑤b】三个 update(QRect) 进事件循环之后的合并结果";
        qInfo() << "  paints（含上面那次 repaint）=" << gauge->paints()
                << "脏区包围盒 =" << gauge->lastDirty() << "矩形块数 =" << gauge->lastRectCount();
        qInfo() << "  累计脏区面积 =" << gauge->dirtyArea()
                << " vs 控件面积 =" << qint64(gauge->width()) * qint64(gauge->height());
        qInfo() << "  → 三个窄条被并成**一个包围盒**（rectCount 可能是 1 也可能保留多块，"
                   "取决于 Qt 的合并策略；包围盒一定覆盖三者）。";
        qInfo() << "  纪律：**能算准脏区就 update(QRect)**，算不准就 update()；"
                   "repaint() 只在「我必须这一帧立刻出现在屏幕上」时用（比如刚 resize 完截图）。";
    });

    // ── ⑥ 鼠标交互：手工投事件 ──────────────────────────────────────────
    QTimer::singleShot(530, [&window, gauge, &changeSignals]() {
        qInfo() << "【证据⑥】鼠标交互（sendEvent 手工投，不需要真鼠标）";
        const double before = gauge->value();
        const int sigBefore = changeSignals;
        const int paintsBefore = gauge->paints();
        const QPointF local(gauge->width() * 0.75, gauge->height() * 0.25);
        const QPointF end = local + QPointF(6, 0);     // 拖动**停下时**的那个点

        // ① 一次完整拖动：先看总数
        synthDrag(&window, gauge, local);
        qInfo() << "  按下/拖动/抬起 之后：value" << before << "->" << gauge->value()
                << "valueChanged 信号次数 +" << (changeSignals - sigBefore)
                << "paintEvent 增量 +" << (gauge->paints() - paintsBefore)
                << "（本机实测 **+2**，不是 +1；为什么下面②逐步拆开数）";

        // ② 把那个「2」拆开数：这是唯一能分清「重复派发 / release 多发 / 路径本来有两个点」的办法。
        //    起点选在上一次拖动停下的 end，于是第一步是「同一点再按下」。
        const int s0 = changeSignals;
        synthPress(&window, gauge, end);
        const int s1 = changeSignals;
        synthMove(&window, gauge, local);
        const int s2 = changeSignals;
        synthMove(&window, gauge, end);
        const int s3 = changeSignals;
        synthRelease(&window, gauge, end);
        const int s4 = changeSignals;
        qInfo() << "  逐步归因（起点 = 上一次停下的 end）：同点按下 +" << (s1 - s0)
                << " 拖回 local +" << (s2 - s1) << " 再拖回 end +" << (s3 - s2)
                << " 抬起 +" << (s4 - s3)
                << "（判定 **+0 / +1 / +1 / +0**）";
        qInfo() << "  → 结论：一次拖动的路径上有 local 与 end **两个**取值点，"
                   "mousePressEvent 和 mouseMoveEvent 各自把点交给 updateFromPoint() 一次，"
                   "两点算出的 value 不同，于是各发一个 valueChanged；mouseReleaseEvent 压根不改 "
                   "value，所以 +0。**多出来的那一个既不是事件重复派发，也不是 release 替 press 补发的，"
                   "而是「按下点 ≠ 抬起点」——拖动本来就不是幂等操作。**";
        qInfo() << "  → 上面那个 **+0（同点按下）就是 setValue() 里 qFuzzyCompare 那道门在起作用**，"
                   "门没坏；旧版本报 +2 是因为它拿一条**折返路径**（停在 end，又从 local 重新按下）"
                   "当「同一点再点一次」的实验，中间 value 真的变了两回。";

        // qevent.h 170~171 行：QEnterEvent(localPos, scenePos, globalPos, device=默认主指点设备)
        //   local  = 相对**接收事件的控件**；scene = 相对**窗口**；global = 相对**屏幕**。
        // gauge 是 window 的客户区铺满子控件（布局 margins/spacing 都是 0），
        // 所以这里 local 与 scene 数值相同；global 才需要 mapToGlobal。
        // 旧写法把 mapToGlobal 当成了第 2 个实参（scene），语义错位，靠下面这行打印才暴露出来。
        const QPointF enterLocal(10, 10);
        QEnterEvent enter(enterLocal,                          // local  = 控件内 (10,10)
                          QPointF(gauge->mapTo(&window, enterLocal.toPoint())),  // scene = 窗口内
                          QPointF(window.mapToGlobal(enterLocal.toPoint())));    // global= 屏幕
        QApplication::sendEvent(gauge, &enter);
        qInfo() << "  投 QEvent::Enter（QEnterEvent）后 isHovering =" << gauge->isHovering()
                << "（enterEvent 在 Qt6 的参数类型就是 QEnterEvent*）";
        qInfo() << "  事件里读回三点：local =" << gauge->enterLocal()
                << "scene(=窗口) =" << gauge->enterScene()
                << "global(=屏幕) =" << gauge->enterGlobal()
                << "；window.mapToGlobal(10,10) =" << window.mapToGlobal(QPoint(10, 10))
                << " → 第 3 个实参才是 global，对得上就说明构造次序是 local/scene/global。";
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(gauge, &leave);
        qInfo() << "  投 Leave 后 isHovering =" << gauge->isHovering();

        // 真正的「同一点再点一次」：此时 value 就对应 end，所以整串都投在 end 上。
        // 判幂等必须**同一步、同一个点**地投，不能拿一条折返路径当幂等实验（见上面②）。
        const int paintsAfterFirst = gauge->paints();
        const int sigAfterFirst = changeSignals;
        synthPress(&window, gauge, end);            // 落在上一次停下的那个点上
        synthMove(&window, gauge, end);             // 原地移动：坐标没变
        synthRelease(&window, gauge, end);
        qInfo() << "  同一点（拖动终点 end）再投一遍：value =" << gauge->value()
                << "信号增量 =" << (changeSignals - sigAfterFirst)
                << "paintEvent 增量 =" << (gauge->paints() - paintsAfterFirst)
                << "（判定：两个增量都必须是 **0**，否则 setValue() 里那道 qFuzzyCompare 就是坏的）";
        // 再补一条最直接的门闩测试：拿当前值原样回灌，一个信号都不许发。
        const int sigGate = changeSignals;
        gauge->setValue(gauge->value());
        qInfo() << "  setValue(当前值) 直灌：信号增量 =" << (changeSignals - sigGate)
                << "（判定：**0** —— 这就是 qFuzzyCompare 那道门本身，和事件派发无关）";
    });

    // ── ⑦ 取色：换 palette 前后同一像素的取值 ───────────────────────────
    QTimer::singleShot(600, [gauge]() {
        qInfo() << "【证据⑦】颜色从 palette 来，不从代码里来";

        // ── 采样点必须是**算出来的**，不能拍脑袋 ─────────────────────────
        // 旧版在这里取 (width()/2, 8)，换色前后都是 #ffb8b8b8，就写成「证明颜色来自 palette」。
        // 复盘：(45,8) 正好落在**环形槽 groove** 那 8 像素宽的笔画上，用的是 QPalette::Mid，
        // 而旧版只改了 Highlight 和 Button —— 采样点根本不在被改的那个 role 上，
        // 前后相同是**必然**的，它既证不了「吃 palette」，也证不了「写死颜色」，是一张废证据。
        // 现在：按 paintGauge() 的同一套几何公式复算出四个点，每个点标注它由哪个 ColorRole 涂，
        // 并且把那四个 role **全都**改掉，实验才有判定力。
        const QRectF box = QRectF(QRect(QPoint(0, 0), gauge->size())).adjusted(4, 4, -4, -4);
        const qreal d = qMin(box.width(), box.height()) * 0.78;             // paintGauge 里同一个式子
        const QPointF rc(box.center().x(), box.center().y() - 4.0);         // ring.center()
        const double v = gauge->value();
        // 角度口径同 drawArc：0°=3 点方向、正角=逆时针；屏幕 y 轴向下，所以取 -sin
        const auto onRing = [&](qreal deg, qreal r) {
            const qreal a = deg * M_PI / 180.0;
            return rc + QPointF(qCos(a), -qSin(a)) * r;
        };
        const qreal accentEnd = 225.0 + v * 270.0;                          // 数值弧扫到哪儿
        const QPointF sAccent = onRing(225.0 + v * 135.0, d / 2.0);          // Highlight：数值弧中段
        const QPointF sGroove = onRing(accentEnd + (495.0 - accentEnd) / 2.0, d / 2.0);
                                                                            // Mid：槽里没被数值弧盖住的中段
        const qreal needleLen = d / 2.0 - 12.0;                              // 指针长度（同 paintGauge）
        const QPointF sNeedle = onRing(225.0 - v * 270.0, needleLen * 0.6);  // ButtonText：指针线上
        const QPointF sFill(box.left() + 5.0, box.top() + 5.0);              // Button：圆角矩形底

        struct Sample { const char *what; QPalette::ColorRole role; QPointF pt; };
        const Sample samples[] = {
            { "数值弧",  QPalette::Highlight, sAccent },
            { "环形槽",  QPalette::Mid,       sGroove },
            { "指针",    QPalette::ButtonText,sNeedle },
            { "圆角底",  QPalette::Button,    sFill   },
        };

        QPalette pal = gauge->palette();
        qInfo() << "  换前四个 role："
                << "Button =" << pal.color(QPalette::Button).name()
                << "Mid =" << pal.color(QPalette::Mid).name()
                << "ButtonText =" << pal.color(QPalette::ButtonText).name()
                << "Highlight =" << pal.color(QPalette::Highlight).name();
        QImage a(gauge->size(), QImage::Format_ARGB32_Premultiplied);
        a.fill(QColor(0, 0, 0, 0));
        gauge->render(&a);                                    // qwidget.h 348 行
        QImage b = a.copy();
        QPalette next = pal;
        next.setBrush(QPalette::Highlight, QColor(255, 40, 40));
        next.setBrush(QPalette::Button, QColor(30, 32, 40));
        next.setBrush(QPalette::Mid, QColor(200, 0, 200));
        next.setBrush(QPalette::ButtonText, QColor(0, 220, 0));
        gauge->setPalette(next);                              // 317 行
        b.fill(QColor(0, 0, 0, 0));
        gauge->render(&b);

        int changed = 0;
        for (const Sample &s : samples) {
            const QPoint pt = s.pt.toPoint();
            const QColor before = a.pixelColor(pt);
            const QColor after  = b.pixelColor(pt);
            const QColor expect = next.color(s.role);         // 这个点**应当**等于该 role 的新色
            const bool ok = (after.rgb() == expect.rgb());
            if (after != before)
                ++changed;
            qInfo().noquote() << QString::asprintf(
                "  %-8s(%3d,%3d) role=%-10s 换前=%s 换后=%s 该 role 新色=%s 判定=%s",
                s.what, pt.x(), pt.y(),
                s.role == QPalette::Highlight  ? "Highlight" :
                s.role == QPalette::Mid        ? "Mid" :
                s.role == QPalette::ButtonText ? "ButtonText" : "Button",
                before.name(QColor::HexArgb).toUtf8().constData(),
                after.name(QColor::HexArgb).toUtf8().constData(),
                expect.name(QColor::HexArgb).toUtf8().constData(),
                ok ? "符合" : "不符");
        }
        qInfo() << "  判定：四个采样点**逐个**等于对应 ColorRole 的新色，且 changed =" << changed
                << "（应为 4）。上一版只有 1 个点、且落在没改的 Mid 上，所以它给不出任何判定。";

        // ── 对照组：把「写死颜色就不随 palette 变」也做成可判定的实验 ──────────
        // 同一张 40x20 的条子：左半边从 palette 取 Highlight，右半边写死 QColor(0,120,215)。
        QImage strip(40, 20, QImage::Format_ARGB32_Premultiplied);
        const auto renderStrip = [&strip](const QPalette &p) {
            strip.fill(QColor(0, 0, 0, 0));
            QPainter q(&strip);
            q.fillRect(0, 0, 20, 20, p.color(QPalette::Highlight));    // 吃 palette
            q.fillRect(20, 0, 20, 20, QColor(0, 120, 215));            // 写死
            q.end();
        };
        renderStrip(pal);
        const QColor dynBefore = strip.pixelColor(5, 5);
        const QColor fixBefore = strip.pixelColor(30, 5);
        renderStrip(next);
        const QColor dynAfter  = strip.pixelColor(5, 5);
        const QColor fixAfter  = strip.pixelColor(30, 5);
        qInfo() << "  对照组·取 palette 的半边：" << dynBefore.name(QColor::HexArgb) << "->"
                << dynAfter.name(QColor::HexArgb) << " 变了 =" << (dynBefore != dynAfter)
                << "；对照组·写死的半边：" << fixBefore.name(QColor::HexArgb) << "->"
                << fixAfter.name(QColor::HexArgb) << " 变了 =" << (fixBefore != fixAfter)
                << "（判定：前者 true、后者 false —— 这才是「写死颜色就不跟着 palette 走」的证据）";
        qInfo() << "  → 纪律：自定义控件必须吃 palette，否则用户的深色模式跟你无关；"
                   "而做这种「换 palette 看变不变」的实验时，**采样点必须落在你改的那个 role 上**，"
                   "并把每个点的归属一起打出来，否则证据会自己骗你。";
        a.save(QStringLiteral("painting_06_custom_widget.png"));
        QImage off(gauge->size(), QImage::Format_ARGB32_Premultiplied);
        off.fill(QColor(0, 0, 0, 0));
        {
            QPainter p(&off);                                 // 离屏设备：随时可画（第 1 讲铁律一）
            paintGauge(p, box, next, 0.72, false);
        }
        off.save(QStringLiteral("painting_06_offscreen_same_code.png"));
        qInfo() << "  两张 PNG 已写盘：" << a.size() << "非透明像素 render 版 ="
                << countOpaque(a) << ", 离屏同代码版 =" << countOpaque(off);
        qInfo() << "  render() 只能在 **paintEvent 之外**使（文档口径：它自己要触发绘制）；"
                   "想整窗截图更简单的路子是 QWidget::grab()（起步模块第 6 讲用过）。";
    });

    // ── ⑧ 收尾 ─────────────────────────────────────────────────────────
    QTimer::singleShot(700, [gauge, &app]() {
        qInfo() << "【收尾】整轮跑完，paintEvent 总次数 =" << gauge->paints()
                << "；把上面每个数字抄回注释，就是你的实测。";
        QCoreApplication::quit();
    });

    return app.exec();
}

// ============================================================================
// 九、速查表与纪律
// ============================================================================
/*
 * 一、五道必答题与对应 API（都在本机 qwidget.h / qnamespace.h 里核过行号）
 *   要一个理想尺寸        virtual QSize sizeHint() const override            522 行
 *   要一个不能更小的尺寸  virtual QSize minimumSizeHint() const override     523 行
 *   让布局肯给我长        setSizePolicy(Expanding, Expanding)                527 行
 *   sizeHint 变了通知布局 updateGeometry()                                   542 行
 *   取用户的配色          palette().color(QPalette::…)                       132 行 Q_PROPERTY
 *   请求重绘（首选）      update() / update(QRect) / update(QRegion)         470/474~476 行
 *   立刻重绘（少见）      repaint() / repaint(QRect)                         471/478~480 行
 *   批量改状态别刷        setUpdatesEnabled(false) … (true)                  462/463 行
 *   自己节流              QTimer + dirty 标志（本讲 Throttle）；**没有 setUpdateInterval**
 *   鼠标移动要进我的处理  setMouseTracking(true)                             336 行
 *   换光标                setCursor(Qt::…) / unsetCursor()                   332/333 行
 *   背景由谁负责          WA_OpaquePaintEvent / WA_NoSystemBackground        310/314 行
 *   整窗透明              WA_TranslucentBackground（顶层）                   425 行
 *   静态底               WA_StaticContents（变大只补新区域）                311 行
 *   拿控件的像素          render(QPaintDevice*) / grab()                     348 行
 *   读控件的 DPR          devicePixelRatio() / devicePixelRatioF()  ← **定义在基类**
 *                        qpaintdevice.h 52/53 行；qwidget.h 98 行 `class QWidget :
 *                        public QObject, public QPaintDevice` 是公有继承，所以 widget 上直接能调
 *   手工造 enter 事件     QEnterEvent(local, scene, global, device=默认)     qevent.h 170~171 行
 *
 * 二、update() 与 repaint() 的关系（一句话）
 *   update() 是「登记」，repaint() 是「现在就去画」。登记会被事件循环合并，
 *   所以你在一个槽里连调一万次 update() 只会画几次；而 repaint() 每次都真画，
 *   一万次就是一万次 —— 这就是「repaint 会放大问题、update 会吸收问题」。
 *   唯一的例外还是那句：**paintEvent 里绝对不要再提重绘自己**（见下面坑③）。
 *
 * 三、把画面包成控件的四条纪律
 *  ① **绘制逻辑写成自由函数**（本讲的 paintGauge(QPainter&, QRectF, QPalette, …)）：
 *     paintEvent 只是它的一个调用者；离屏出图、单测、缩略图都调同一个函数。
 *  ② 颜色走 palette，字号走 font()（本讲两处都留了），别写死 QColor/QFont("…",12)。
 *  ③ 状态变了先判「有没有真的变」再 update()（本讲 setValue 里的 qFuzzyCompare），
 *     并且**在 paintEvent 里绝不改状态**：paintEvent 改状态 → update → 再 paintEvent
 *     → 无限重绘，风扇启动。要改状态就在事件处理函数或定时器里改。
 *  ④ 提局部重绘要算准脏区：算不准就 update() 全幅，别画一个只刷一半的「花屏」。
 *
 * ——— 老师傅的话 ———
 *   徒弟，自定义控件这活儿，考的不是画得多炫，是**你有没有把「请求」和「执行」分开**。
 *   新手一卡就 repaint()，越卡越 repaint，最后界面像开了风扇；老手只写 update()，
 *   把合并交给事件循环，然后想办法把脏区算窄。你记住这个次序：
 *   **先让画面正确，再让重绘变少，最后才谈画得快。**
 *
 *   第二件，本讲我特意把「setUpdateInterval」这条**假 API**写在运行前须知里。
 *   为什么？因为它是从 Qt Quick 那边串门过来的说法，网上很多老答案抄来抄去，
 *   你在 QWidget 里怎么都找不到，就去找文档、找插件、找一晚上。
 *   我的办法很笨但很可靠：**先在头文件里 grep 一遍**。本讲我 grep 到的事实是
 *   「整个 QtWidgets 目录没有这个名字，只有 QtCore/qtimeline.h 有」。
 *   查不到就不用它，改用 QTimer + dirty 标志自己合闸 —— 多写十行，但它是真的。
 *   以后你也这样：别人告诉你的 API，先去头文件见一面。
 *
 *   第二件半，这条更阴：grep 到了「没有」，也别急着下结论。
 *   本讲旧版在 `qwidget.h` 里搜 `devicePixelRatioF` 搜不到，就写成「Qt6 删了它，别照抄」——
 *   **结论正好说反**。名字不在派生类头文件里，通常是因为它是**基类的**：
 *   qwidget.h 98 行 `class QWidget : public QObject, public QPaintDevice`，
 *   而 devicePixelRatio()/devicePixelRatioF() 在 qpaintdevice.h 52/53 行，公有继承过来，照调不误。
 *   所以规矩要补全成两步：**先在声明类里 grep；搜不到就去看它的继承列表，沿链往上查到定义为止**。
 *   「搜不到」本身不携带任何结论，它只携带一个问题：这个名字到底在哪一层？
 *
 *   第三件：Q_OBJECT 在这讲是**为了发信号**（valueChanged 要给外面用），
 *   不是为了画。别养出「自定义控件必须写 Q_OBJECT」的错觉——
 *   第 1 讲到第 5 讲那些只覆写 paintEvent 的类一个都没写。
 *   要 Q_OBJECT 的三个理由：自定义信号槽、qobject_cast、property 系统。缺一不用加。
 *
 *   第三件半，取证的两条手艺，本讲都踩过反面：
 *   ① **一个数字要归因到一步**。旧版说「同一点再点一次，信号增量应为 0」，实测 +2，
 *      就有人怀疑事件被重复派发。把按下/拖动/抬起**分开数**以后真相很清楚：+0/+1/+1/+0。
 *      一次拖动路径上有按下点和抬起点**两个**坐标，mousePressEvent 和 mouseMoveEvent 各换算
 *      一次 value，两个点不同就各发一个信号；抬起不改 value，所以 +0。
 *      所谓「多发」，是**拿一条折返路径当幂等实验**造成的——拖动本来就不幂等。
 *      想验幂等，就把每一步都投在同一个点上（本讲验了，确实是 0）。
 *   ② **采样点必须落在你改的那个东西上**。旧版在证据⑦ 取 (width()/2, 8)，换 palette 前后
 *      都是 #ffb8b8b8，就当成「颜色写死」的表现——其实那个像素是**环形槽**用 QPalette::Mid
 *      涂的，而那实验只改了 Highlight 和 Button，Mid 当然不动。这叫废证据。
 *      现在的写法：采样点按 paintGauge() 的几何公式**算**出来，四个 role 全改掉，
 *      每个点把「它归哪个 role 涂」一起打印，实验才有判定力。
 *
 *   最后：交互那一段我用 sendEvent 手工投事件，是因为这套资料必须能在 CI 里跑完。
 *   你也该养成这个习惯：**任何交互都在代码里能造出来**，别一没鼠标就宣称「测不了」。
 *   等你熟练了，第拾阶段会用 QtTest 的 QTest::mouseClick 把这段写得更短。
 *
 * ——— 小练习 ———
 *   1. 把 setValue() 里的 update(QRect) 收窄成「只刷数值文字那一小条」，
 *      再用证据⑤ 的方法打印 dirtyArea() 与控件面积的比值；
 *      然后把收窄去掉换成 update()，把两个比值抄下来对比。
 *      （这就是为什么证据④ 里 fullPaints/partialPaints 要分开数。）
 *   2. 给 GaugeWidget 加一个 `setUpdateMode` 开关：手动实现三种抑制方式
 *      （直接 update / setUpdatesEnabled(false) 批量改完再开 / 用 Throttle 合闸），
 *      用 QElapsedTimer 各测一万次 setValue() 的耗时与 paintEvent 次数，做成一张表。
 *      顺便把本讲的结论验证一遍：**QWidget 没有 setUpdateInterval**，你只能用这三条。
 *   3. 把本控件的调色板换成深色（setPalette 改 Window/Button/Text/Highlight 四色），
 *      再把 paintGauge 里所有 QColor 换成硬编码，重复实验：
 *      说明「换 palette 后两张 PNG 分别变成什么样」，并解释为什么深色模式下
 *      硬编码的控件会在用户机器上变成「黑底黑字」。
 */
// ============================================================================

// 本文件**有 Q_OBJECT 类（GaugeWidget、Throttle）**，AUTOMOC 会生成 06_custom_widget.moc，
// 必须在所有 Q_OBJECT 类定义之后 include 进来（否则链接期报 staticMetaObject/vtable 未定义）。
#include "06_custom_widget.moc"
