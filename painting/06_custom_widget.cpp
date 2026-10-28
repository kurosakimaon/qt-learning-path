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
//        Highlight/Mid），**不硬编码 QColor**；换 palette 就换整套外观，本讲实测换色前后
//        同一个像素的取值；
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
//        无人值守也能把「交互 → 状态 → 信号 → 重绘请求」整条链跑通，并数出信号发了几次。
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
//   · 会往当前工作目录写：painting_06_custom_widget.png（QWidget::render() 抓出来的控件像素）、
//     painting_06_offscreen_same_code.png（同一份 paintGauge() 直接画进 QImage）。
//   · 无人值守：QTimer::singleShot 排一串动作（resize → 压力测试 → 投鼠标 → 存图 → 打印
//     → QCoreApplication::quit()），全程不需要鼠标键盘，约 1.5 秒跑完。
//     除了 main 末尾那句 app.exec()，**没有任何阻塞调用**。
//   · 可以无头跑：`painting_06_custom_widget -platform offscreen`。offscreen 会派发 paint 事件，
//     所以「重绘次数」这组数在 CI 里也是有效的。
//   · 「一万次 update() 合并成几次」：头文件能证明的只有 update() 是 public Q_SLOTS、
//     不带同步语义（qwidget.h 470 行）；**合并发生在事件循环里，次数取决于你的机器与
//     这段时间里事件循环被喂了几次**，预期 1 次，但请以你跑出来的数字为准。
//   · 未实测声明：本机当前无法编译链接 Qt 运行时（运行时库版本不匹配），
//     本示例**已通过 g++ -fsyntax-only 编译期核对（-std=c++17 -Wall -Wextra 零 error 零 warning），
//     未链接、未运行**；文中「预期」全是推理，PNG 与数字都还没有任何机器产出过。
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
//     QMouseEvent 的 globalPos() 在 Qt6 走 globalPosition()（返回 QPointF）；
//     QWidget::devicePixelRatioF() 在本机 qwidget.h 里搜不到（读 DPR 用继承自 QPaintDevice 的
//     devicePixelRatio()，qpaintdevice.h 52 行）。
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
// 一、核对签名（本机 C:\msys64\ucrt64\include\qt6，全部带行号；对不上的一律不用）
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
//   enum ColorRole { Window, Base, Text, Button, ButtonText, Highlight, HighlightedText, Mid, ... }
//   QColor color(ColorRole r) const;  void setBrush(ColorGroup, ColorRole, const QBrush &);
// QtGui/qevent.h
//   QPaintEvent : const QRect &rect() const;  const QRegion ®ion() const;
//   QMouseEvent : QMouseEvent(Type, const QPointF &localPos, const QPointF &globalPos,
//                             Qt::MouseButton, Qt::MouseButtons, Qt::KeyboardModifiers);
//                 QPointF position() const; QPoint pos() const;
//   QEnterEvent : QEnterEvent(const QPointF &localPos, const QPointF &globalPos,
//                             const QPointF &screenPos);
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

    void enterEvent(QEnterEvent *) override                 // Qt6：参数是 QEnterEvent*
    {
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

// 手工投一次「按下-拖动-抬起」：无人值守也能跑通交互链（sendEvent 不阻塞）
static void synthDrag(QWidget *top, GaugeWidget *gauge, const QPointF &local)
{
    const QPoint global = top->mapToGlobal(local.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, local, QPointF(global),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove, local + QPointF(6, 0),
                     QPointF(global) + QPointF(6, 0),
                     Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, local + QPointF(6, 0),
                        QPointF(global) + QPointF(6, 0),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(gauge, &press);
    QApplication::sendEvent(gauge, &move);
    QApplication::sendEvent(gauge, &release);
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
        synthDrag(&window, gauge, local);
        qInfo() << "  按下/拖动/抬起 之后：value" << before << "->" << gauge->value()
                << "valueChanged 信号次数 +" << (changeSignals - sigBefore)
                << "paintEvent 增量 +" << (gauge->paints() - paintsBefore);
        QEnterEvent enter(QPointF(10, 10), QPointF(window.mapToGlobal(QPoint(10, 10))),
                          QPointF(window.mapToGlobal(QPoint(10, 10))));
        QApplication::sendEvent(gauge, &enter);
        qInfo() << "  投 QEvent::Enter（QEnterEvent）后 isHovering =" << gauge->isHovering()
                << "（enterEvent 在 Qt6 的参数类型就是 QEnterEvent*）";
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(gauge, &leave);
        qInfo() << "  投 Leave 后 isHovering =" << gauge->isHovering();
        // 同一处再点一次：value 没变 → setValue 里的 qFuzzyCompare 应当拦掉，不再提重绘
        const int paintsAfterFirst = gauge->paints();
        const int sigAfterFirst = changeSignals;
        synthDrag(&window, gauge, local);
        qInfo() << "  同一点再点一次：value =" << gauge->value()
                << "信号增量 =" << (changeSignals - sigAfterFirst)
                << "paintEvent 增量 =" << (gauge->paints() - paintsAfterFirst)
                << "（两个增量都应为 0：没变就不提请求）";
    });

    // ── ⑦ 取色：换 palette 前后同一像素的取值 ───────────────────────────
    QTimer::singleShot(600, [gauge]() {
        qInfo() << "【证据⑦】颜色从 palette 来，不从代码里来";
        QPalette pal = gauge->palette();
        qInfo() << "  换前: Button =" << pal.color(QPalette::Button).name()
                << "Highlight =" << pal.color(QPalette::Highlight).name();
        QImage a(gauge->size(), QImage::Format_ARGB32_Premultiplied);
        a.fill(QColor(0, 0, 0, 0));
        gauge->render(&a);                                    // qwidget.h 348 行
        QImage b = a.copy();
        QPalette next = pal;
        next.setBrush(QPalette::Highlight, QColor(255, 40, 40));
        next.setBrush(QPalette::Button, QColor(30, 32, 40));
        gauge->setPalette(next);                              // 317 行
        b.fill(QColor(0, 0, 0, 0));
        gauge->render(&b);
        const QPoint sample(gauge->width() / 2, 8);
        qInfo() << "  同一像素(" << sample << ")：换前 =" << a.pixelColor(sample).name(QColor::HexArgb)
                << "换后 =" << b.pixelColor(sample).name(QColor::HexArgb);
        qInfo() << "  → 如果代码里写死 QColor(0,120,215)，这一换就什么也不会变；"
                   "自定义控件必须吃 palette，否则用户的深色模式跟你无关。";
        a.save(QStringLiteral("painting_06_custom_widget.png"));
        QImage off(gauge->size(), QImage::Format_ARGB32_Premultiplied);
        off.fill(QColor(0, 0, 0, 0));
        {
            QPainter p(&off);                                 // 离屏设备：随时可画（第 1 讲铁律一）
            paintGauge(p, QRectF(QRect(QPoint(0, 0), gauge->size())).adjusted(4, 4, -4, -4),
                       next, 0.72, false);
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
 *   第三件：Q_OBJECT 在这讲是**为了发信号**（valueChanged 要给外面用），
 *   不是为了画。别养出「自定义控件必须写 Q_OBJECT」的错觉——
 *   第 1 讲到第 5 讲那些只覆写 paintEvent 的类一个都没写。
 *   要 Q_OBJECT 的三个理由：自定义信号槽、qobject_cast、property 系统。缺一不用加。
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
