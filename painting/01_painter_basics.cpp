// ============================================================================
// 第 1 讲 · QPainter 的三条铁律与基础图元
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   绘图模块的第一课，先把「拿笔往哪儿画」这条最要命的规矩立起来，再用**可核查的
//   像素数字**把八个基础图元（点、线、矩形、圆角矩形、椭圆、圆弧、扇形/弦、多边形）
//   与 QPen / QBrush 的第一面讲清楚：
//     1) 铁律一：对**控件（QWidget）**作画只允许发生在 paintEvent() 里；
//        对 **QImage / QPixmap** 作画则随时可以——本讲把这条差别亲手测出来
//        （同一个绘制函数，一次画进 QImage，一次画进控件，两张图内容一致）；
//     2) 铁律二：painter 必须在设备销毁之前析构（局部变量写在 paintEvent 内即自然满足）；
//     3) 铁律三：begin()/end() 与 RAII —— 构造函数带设备就是 RAII，
//        isActive() 是你唯一可靠的自检手段；
//     4) 入口概念：**逻辑坐标 vs 设备坐标**。高 DPI 下同一份绘制代码在物理像素上
//        的尺寸是不同的，本讲用 1x 与 2x 两张画布实测「同样的逻辑图形，着墨像素差约 4 倍」。
//
// 对应构建目标名：painting_01_painter_basics
//   find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)   # 本组统一链 Svg（第 8 讲要用），本讲只用到 Widgets
//   add_executable(painting_01_painter_basics 01_painter_basics.cpp)
//   target_link_libraries(painting_01_painter_basics PRIVATE Qt6::Widgets Qt6::Svg)
//   set(CMAKE_AUTOMOC ON)
//   注意：本文件里**没有 Q_OBJECT 类**——只覆写 paintEvent() 不需要元对象
//   （moc 是为信号/槽/qobject_cast 服务的），所以末尾也就不用 #include "01_painter_basics.moc"。
//
// 运行前须知：
//   · 会往**当前工作目录**写 painting_01_painter_basics.png 与 painting_01_hidpi_1x.png /
//     painting_01_hidpi_2x.png，请在可写目录下运行。
//   · 无人值守：QTimer::singleShot 排几步动作 → 存图 → 打印 → QCoreApplication::quit()，
//     全程不要求鼠标键盘，约 2 秒跑完。**不要在示例里用 QDialog::exec() 或裸 QEventLoop::exec()**。
//   · 可以无头跑：`painting_01_painter_basics -platform offscreen`（需要 platforms/qoffscreen.dll）。
//     offscreen 平台**照样会派发 paint 事件**，所以控件那条证据链不会断。
//   · 本机当前无法编译链接 Qt 运行时，本示例**未在本机实测编译**；
//     每一个 API 都对着 C:\msys64\ucrt64\include\qt6 下的真实头文件逐个核对过（见下面签名清单）。
//     此外只跑过一次 `g++ -fsyntax-only -I <Qt6 头文件目录>`：那是**纯语法核对**
//     （不产出目标文件、**没有链接、没有运行、没有生成可执行文件**），
//     它只证明「这些调用对着 Qt6 头文件编得过」（确实靠它抓出过 setBrush 两参数的错写），
//     绝不等于画面正确 —— 文中所有「预期」都是推理，**实测数字请你自己跑出来填回去**。
//   · Qt5 → Qt6 差异本讲涉及两处：QPainter::RenderHint 里**没有** HighQualityAntialiasing
//     （Qt6 只剩 6 个 hint，见下）；QImage::pixel() 返回 QRgb（uint），QColor 版本叫 pixelColor()。
// ============================================================================

#include <QApplication>          // QtWidgets/qapplication.h
#include <QWidget>               // QtWidgets/qwidget.h：控件本体 + protected paintEvent
#include <QPaintEvent>           // QtGui/qevent.h：paintEvent 的参数类型
#include <QPainter>              // QtGui/qpainter.h：核心
#include <QPen>                  // QtGui/qpen.h
#include <QBrush>                // QtGui/qbrush.h（Qt6 里 QGradient 也在这个头文件中）
#include <QColor>                // QtGui/qcolor.h（QRgb 的 typedef 也由此引入）
#include <QImage>                // QtGui/qimage.h：可以「随时画」的设备
#include <QPaintDevice>          // QtGui/qpaintdevice.h：devicePixelRatio()/logicalDpiX()
#include <QPoint>                // QtCore/qpoint.h
#include <QRect>                 // QtCore/qrect.h
#include <QSize>                 // QtCore/qsize.h
#include <QString>
#include <QTimer>                // QtCore/qtimer.h
#include <QCoreApplication>      // QtCore/qcoreapplication.h：quit()
#include <QGuiApplication>       // QtGui/qguiapplication.h：platformName()
#include <QDebug>

// ============================================================================
// 一、核对签名（本机 Qt6 头文件，全部带行号；对不上的一律不用）
// ============================================================================
// QtGui/qpainter.h
//   第 52~57 行  enum RenderHint { Antialiasing = 0x01, TextAntialiasing = 0x02,
//                  SmoothPixmapTransform = 0x04, VerticalSubpixelPositioning = 0x08,
//                  LosslessImageRendering = 0x40, NonCosmeticBrushPatterns = 0x80 };
//                  ← 本机这份头文件里**没有** HighQualityAntialiasing（Qt5 老代码请删掉它）
//   第 89 行     explicit QPainter(QPaintDevice *);        ← RAII 入口
//   第 92 行     QPaintDevice *device() const;
//   第 94 行     bool begin(QPaintDevice *);               第 95 行  bool end();
//   第 96 行     bool isActive() const;                    ← 唯一的「笔到底开没开」自检
//   第 148 行    void setFont(const QFont &f);
//   第 153/155/157 行  void setPen(const QColor&); / setPen(const QPen&); / setPen(Qt::PenStyle);
//   第 161/163/164 行  void setBrush(const QBrush&); / setBrush(Qt::BrushStyle); / setBrush(QColor);
//   第 201 行    void save();                              第 202 行 void restore();
//   第 218/220/222~224 行 void scale(qreal,qreal); rotate(qreal); translate(...);
//   第 226/230 行 QRect window() const;  QRect viewport() const;
//   第 242~244 行 void drawPoint(const QPointF&)/(const QPoint&)/(int x, int y);
//   第 251~255 行 void drawLine(const QLineF&)/(const QLine&)/(int,int,int,int)/...
//   第 266~268 行 void drawRect(const QRectF&)/(int x1,int y1,int w,int h)/(const QRect&);
//   第 275~280 行 void drawEllipse(const QRectF&)/(const QRect&)/(int x,int y,int w,int h)/
//                  (const QPointF &center, qreal rx, qreal ry);
//   第 282~285 行 void drawPolyline(const QPoint *points, int pointCount); ← 裸数组版
//   第 287~290 行 void drawPolygon(const QPoint *points, int pointCount,
//                  Qt::FillRule fillRule = Qt::OddEvenFill);
//   第 297~299 行 void drawArc(const QRectF&, int a, int alen);  ← a/alen 单位是 **1/16 度**
//   第 301~307 行 drawPie(...)、drawChord(...) 同样签名的三个版本
//   第 309~313 行 void drawRoundedRect(const QRectF&, qreal xr, qreal yr, Qt::SizeMode = ...);
//   第 368~370 行 void drawText(const QPointF&)/(const QPoint&)/(int x, int y, const QString&);
//                  ← 这里的 (x,y) 是**基线**（baseline），不是左上角！第 4 讲正面算它
//   第 414 行    void setRenderHint(RenderHint hint, bool on = true);
// QtGui/qpen.h
//   第 34/35 行  QPen(const QBrush&, qreal width, Qt::PenStyle = Qt::SolidLine,
//                  Qt::PenCapStyle = Qt::SquareCap, Qt::PenJoinStyle = Qt::BevelJoin);
//                  ← 注意默认帽是 **SquareCap**、默认连接是 **BevelJoin**（很多人以为默认是 Flat）
//   第 49 行 setStyle(Qt::PenStyle) / 第 51 行 dashPattern() / 第 52 行 setDashPattern(QList<qreal>)
//   第 60/61 行 qreal widthF() const; setWidthF(qreal);   第 64 行 void setWidth(int);
//   第 74~81 行 capStyle()/setCapStyle/joinStyle()/setJoinStyle/isCosmetic()/setCosmetic(bool)
// QtGui/qbrush.h
//   第 32~44 行  QBrush(Qt::BrushStyle) / QBrush(const QColor&, Qt::BrushStyle = Qt::SolidPattern)
//                / QBrush(const QImage&) / QBrush(const QGradient&)
//   第 65~68 行  setTexture(const QPixmap&) / textureImage() / setTextureImage(const QImage&)
//   第 157 行    class Q_GUI_EXPORT QGradient   ← Qt6 里渐变声明在 qbrush.h，**没有 qgradient.h**
//   第 176~180 行 enum CoordinateMode { LogicalMode, StretchToDeviceMode, ObjectBoundingMode, ObjectMode };
//                  ← Qt5 那份叫 { LogicalMode, DeviceMode, ObjectBoundingMode }，Qt6 改名并加了 ObjectMode
// QtGui/qimage.h
//   第 42~49 行  enum Format { ..., Format_RGB32, Format_ARGB32, Format_ARGB32_Premultiplied, ... };
//   第 88 行     QImage(int width, int height, Format format);   ← 这里的宽高是**物理像素**
//   第 148 行    QRect rect() const;
//   第 167~169 行 uchar *scanLine(int); const uchar *scanLine(int) const; const uchar *constScanLine(int) const;
//   第 178/184/187 行 QRgb pixel(int x,int y) const; QColor pixelColor(int,int) const;
//                  void setPixelColor(int x,int y,const QColor&);
//                  ← Qt6 里 pixel() 只给 QRgb（uint），要 QColor 用 pixelColor()（第 5 讲细说）
//   第 193/194 行 qreal devicePixelRatio() const; void setDevicePixelRatio(qreal);
//   第 197~199 行 void fill(uint)/fill(const QColor&)/fill(Qt::GlobalColor);
//   第 202 行    bool hasAlphaChannel() const;             第 268 行 bool save(const QString&, const char *format = nullptr, int quality = -1) const;
// QtGui/qpaintdevice.h
//   第 44~49 行  int width()/height()/widthMM()/heightMM()/logicalDpiX()/logicalDpiY()（都是 metric 的包装）
//   第 52/53 行  qreal devicePixelRatio() const; qreal devicePixelRatioF() const;
// QtWidgets/qwidget.h
//   第 214 行    explicit QWidget(QWidget *parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());
//   第 470 行    void update();     第 471 行 void repaint();
//   第 503 行    void resize(int w, int h);
//   第 674 行    protected: virtual void paintEvent(QPaintEvent *event);   ← 铁律一的家门口
// QtCore/qnamespace.h
//   第 1115~1126 行 enum PenStyle { NoPen, SolidLine, DashLine, DotLine, DashDotLine,
//                  DashDotDotLine, CustomDashLine, MPenStyle = 0x0f };
//   第 1128~1133 行 enum PenCapStyle { FlatCap = 0x00, SquareCap = 0x10, RoundCap = 0x20, MPenCapStyle = 0x30 };
//   第 1135~1141 行 enum PenJoinStyle { MiterJoin = 0x00, BevelJoin = 0x40, RoundJoin = 0x80, SvgMiterJoin = 0x100 };
//   第 1143~1163 行 enum BrushStyle { NoBrush, SolidPattern, Dense1Pattern ... Dense7Pattern,
//                  HorPattern, VerPattern, CrossPattern, BDiagPattern, FDiagPattern,
//                  DiagCrossPattern, LinearGradientPattern, RadialGradientPattern,
//                  ConicalGradientPattern, TexturePattern = 24 };
//                  ← 是 **HorPattern / VerPattern**（Qt4 时代叫 HorizonPattern/VerticalPattern，老代码要改）
//   第 1329~1332 行 enum FillRule { OddEvenFill, WindingFill };
//   第 29 行起     enum GlobalColor { black, white, darkGray, gray, lightGray, red, green,
//                  blue, cyan, magenta, yellow, ... };（是**小写**开头的枚举名）

// ============================================================================
// 二、像素证据工具：数「着墨像素」
// ----------------------------------------------------------------------------
// 为什么自己数像素：示例必须在无头环境下也能证明「画对了」。
// 只用核对过的 API：QImage::constScanLine(int)（qimage.h 169 行）逐行取指针，
// 32 位格式下每像素 4 字节，QRgb 的值语义是 (a<<24)|(r<<16)|(g<<8)|b，
// 所以直接对 uint 右移 24 位取 alpha 是稳的（不依赖大小端解释，只依赖值语义）。
// ============================================================================
struct InkStats
{
    long long count = 0;      // alpha != 0 的像素个数
    long long sumX = 0;
    long long sumY = 0;
    int minX = 1 << 30, maxX = -(1 << 30);
    int minY = 1 << 30, maxY = -(1 << 30);

    int inkedPixels() const { return int(count); }
    int centroidX() const { return count ? int(sumX / count) : -1; }
    int centroidY() const { return count ? int(sumY / count) : -1; }
};

// 在 [x0,x1) x [y0,y1) 这个范围内统计不透明像素（范围传整幅图就是全图统计）
static InkStats scanInk(const QImage &img, int x0, int x1, int y0, int y1)
{
    InkStats s;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > img.width())  x1 = img.width();     // QImage::width()/height() 来自 qimage.h 的 size 访问器
    if (y1 > img.height()) y1 = img.height();

    for (int y = y0; y < y1; ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));  // qimage.h 169 行
        for (int x = x0; x < x1; ++x) {
            if ((row[x] >> 24) != 0) {   // alpha 非 0 才算着墨
                ++s.count;
                s.sumX += x;
                s.sumY += y;
                if (x < s.minX) s.minX = x;
                if (x > s.maxX) s.maxX = x;
                if (y < s.minY) s.minY = y;
                if (y > s.maxY) s.maxY = y;
            }
        }
    }
    return s;
}

static InkStats scanInk(const QImage &img)
{
    return scanInk(img, 0, img.width(), 0, img.height());
}

// ============================================================================
// 三、同一个绘制函数：既能画进 QImage，也能画进控件
// ----------------------------------------------------------------------------
// 这是本讲的核心设计。**绘制代码本身与设备无关**，不同的只是「什么时候允许拿笔」。
// 把它写成函数，才能在两个设备上各跑一次并对比数字。
// ============================================================================
static void drawBasicScene(QPainter &p, int w, int h)
{
    p.fillRect(0, 0, w, h, QColor(250, 250, 252));   // qpainter.h 的 fillRect 系列（与 drawRect 同族的填充分支）

    // ── ① 点：三种写法都在（QPointF / QPoint / int,int）──
    p.setPen(QPen(QColor(200, 30, 30), 1));
    p.drawPoint(8, 8);
    p.setPen(QPen(QColor(30, 30, 200), 3));          // 宽度 3 的点：便于像素统计
    QPoint pts[3] = { QPoint(24, 8), QPoint(40, 8), QPoint(56, 8) };
    p.drawPoints(pts, 3);                            // qpainter.h 248 行（裸数组版）

    // ── ② 线：Qt::PenStyle 五种实线/虚线（枚举名逐一对着头文件抄的）──
    const Qt::PenStyle styles[5] = { Qt::SolidLine, Qt::DashLine, Qt::DotLine,
                                     Qt::DashDotLine, Qt::DashDotDotLine };
    for (int i = 0; i < 5; ++i) {
        QPen pen(QColor(40, 40, 40), 2);            // QColor → QBrush 是一次隐式转换（qbrush.h 34 行），合法
        pen.setStyle(styles[i]);                     // qpen.h 49 行
        p.setPen(pen);
        p.drawLine(8, 24 + i * 12, 120, 24 + i * 12);
    }

    // ── ③ 矩形 / 圆角矩形：QRect 版与 QRectF 版都在（266~268、309~313 行）──
    p.setPen(QPen(QColor(0, 130, 60), 2));
    p.setBrush(QColor(200, 240, 210));               // qpainter.h 164 行：setBrush(QColor) = SolidPattern
    p.drawRect(QRect(140, 8, 60, 40));
    p.setBrush(Qt::NoBrush);                         // qpainter.h 163 行：setBrush(Qt::BrushStyle)
    p.drawRoundedRect(QRectF(206, 8, 60, 40), 10.0, 6.0);

    // ── ④ 椭圆 / 圆：drawEllipse(QRectF) 与 (center, rx, ry) 两个版本 ──
    p.setBrush(QColor(120, 170, 255));
    p.drawEllipse(QRectF(272, 8, 56, 40));
    p.drawEllipse(QPointF(352, 28), 20.0, 20.0);

    // ── ⑤ 圆弧 / 扇形 / 弦：角度单位 1/16 度（297~307 行）──
    // 90 度 → 90 * 16 = 1440。这条「乘以 16」的规矩是最常见的踩坑点。
    p.setPen(QPen(QColor(180, 90, 0), 3));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(8, 96, 64, 64), 0, 90 * 16);
    p.setBrush(QColor(255, 200, 120));
    p.drawPie(QRectF(80, 96, 64, 64), 45 * 16, 90 * 16);
    p.drawChord(QRectF(152, 96, 64, 64), 20 * 16, 120 * 16);

    // ── ⑥ 多边形 / 折线：裸数组 + pointCount（282~290 行），Qt6 不需要 QPolygon 的初始化列表构造 ──
    QPoint poly[4] = { QPoint(224, 100), QPoint(284, 112), QPoint(256, 156), QPoint(210, 140) };
    p.setPen(QPen(QColor(60, 60, 160), 2));
    p.setBrush(QColor(190, 190, 240));
    p.drawPolygon(poly, 4);                          // 闭合并填充
    QPoint openLine[3] = { QPoint(296, 100), QPoint(330, 140), QPoint(364, 104) };
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(openLine, 3);                     // 不闭合、不填充

    // ── ⑦ QBrush 的图案样式（Qt::BrushStyle 真名核对：Dense*Pattern / HorPattern / DiagCrossPattern）──
    const Qt::BrushStyle bs[4] = { Qt::Dense3Pattern, Qt::HorPattern, Qt::VerPattern, Qt::DiagCrossPattern };
    for (int i = 0; i < 4; ++i) {
        p.setPen(QColor(70, 70, 70));
        p.setBrush(QBrush(QColor(255, 180, 60), bs[i]));  // qbrush.h 34 行：QBrush(const QColor&, Qt::BrushStyle)
                                                          // 注意 QPainter::setBrush 只有单参数版本（qpainter.h 161~165 行），
                                                          // 想「颜色 + 图案」必须自己构造 QBrush
        p.drawRect(300 + i * 26, 168, 22, 22);       // 越界也无妨：QPaintDevice 会自动裁剪
    }

    // ── ⑧ 文本：这里的 (x, y) 是**基线**，不是左上角（第 4 讲专门算这个偏移）──
    p.setPen(QColor(20, 20, 20));
    p.setBrush(Qt::NoBrush);
    p.drawText(8, 210, QStringLiteral("QPainter basics · 基线在 y=210"));
}

// ============================================================================
// 四、把同一份场景画进 QImage（「随时可画」的那一半证据）
// ============================================================================
static QImage renderSceneToImage(int w, int h, qreal scaleForHiDpi)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);   // qimage.h 88 行
    img.fill(Qt::transparent);                               // GlobalColor 里有 transparent（29 行起那份枚举）
    {
        QPainter p(&img);                                    // qpainter.h 89 行：RAII，构造即 begin
        if (!p.isActive()) {                                 // qpainter.h 96 行
            qWarning() << "  QPainter::isActive() == false —— 对 QImage 也开不了笔，说明 begin 失败";
            return img;
        }
        if (scaleForHiDpi != 1.0)
            p.scale(scaleForHiDpi, scaleForHiDpi);           // qpainter.h 218 行
        drawBasicScene(p, 400, 220);                         // 逻辑画布固定 400x220，物理尺寸由 scale 决定
    }                                                        // ← 铁律二：p 在 img 离开作用域之前析构
    return img;
}

// ============================================================================
// 五、画在控件上：唯一的合法入口是 paintEvent()
// ============================================================================
class Stage : public QWidget
{
public:
    explicit Stage(QWidget *parent = nullptr) : QWidget(parent) {}   // qwidget.h 214 行

    int paints = 0;
    qreal lastDpr = -1.0;
    int lastDpiX = -1, lastDpiY = -1;
    QSize lastViewport;
    QSize lastWindow;

protected:
    // 核对：qwidget.h 第 674 行 protected virtual void paintEvent(QPaintEvent *)
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);                       // 本讲不去引用它（引用需要先核对 QPaintEvent::rect()）
        QPainter p(this);                      // ← 铁律一：只有在这里，this 才是合法的绘制设备
        if (!p.isActive()) {
            qWarning() << "  paintEvent 里画笔都没开：这不可能，说明 show() 之后设备已被销毁";
            return;                            // 铁律二：无论如何 p 会先于 this 析构（它就是本栈帧的局部变量）
        }
        ++paints;
        const int w = width();
        const int h = height();
        drawBasicScene(p, w, h);

        // 「逻辑坐标 vs 设备坐标」的现场读数：只有在这里才拿得到真实设备
        QPaintDevice *dev = p.device();                          // qpainter.h 92 行
        lastDpr = dev->devicePixelRatio();                       // qpaintdevice.h 52 行
        lastDpiX = dev->logicalDpiX();                           // qpaintdevice.h 48 行
        lastDpiY = dev->logicalDpiY();
        lastViewport = p.viewport().size();                      // qpainter.h 230 行 viewport()
        lastWindow = p.window().size();                          // qpainter.h 226 行 window()
    }
};

// ============================================================================
// 六、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    qInfo() << "平台插件 =" << QGuiApplication::platformName()
            << "（用 -platform offscreen 可以无头跑，paint 事件照样派发）";

    // ────────────────────────────────────────────────────────────────
    // 证据 ① 「对 QImage 可以随时画」：此刻还没进事件循环、也没 show 任何窗口
    // ────────────────────────────────────────────────────────────────
    // 这一段全部在 exec() 之前，属于「程序刚起步」的时刻。QImage 只是一个内存里的
    // 光栅设备，它不属于窗口系统，所以不需要等 paint 事件。
    QImage scene = renderSceneToImage(400, 220, 1.0);
    const bool saved = scene.save(QStringLiteral("painting_01_painter_basics.png"));   // qimage.h 268 行
    const InkStats all = scanInk(scene);
    qInfo() << "\n=== 证据① 画进 QImage（exec() 之前，合法）===";
    qInfo() << "  图像尺寸 =" << scene.width() << "x" << scene.height()
            << "，rect() =" << scene.rect()
            << "，hasAlphaChannel =" << scene.hasAlphaChannel();      // qimage.h 202 行
    qInfo() << "  save() 返回 =" << saved << "（false 表示当前目录不可写或格式不支持）";
    qInfo() << "  不透明像素总数 =" << all.inkedPixels();
    qInfo() << "  着墨包围盒 = x[" << all.minX << ".." << all.maxX << "]"
            << " y[" << all.minY << ".." << all.maxY << "]"
            << "，重心 =(" << all.centroidX() << "," << all.centroidY() << ")";
    // 抽查一个坐标的像素值：(8,8) 是第一个点（红色 200,30,30 宽 1）
    const QColor c88 = scene.pixelColor(8, 8);                        // qimage.h 184 行
    qInfo() << "  pixelColor(8,8) =" << c88.red() << c88.green() << c88.blue() << c88.alpha()
            << "，name =" << c88.name();                               // qcolor.h 62 行
    qInfo() << "  对照：pixel(8,8) 给的是 QRgb = 0x"
            << QString::number(quint64(scene.pixel(8, 8)), 16);        // qimage.h 178 行（uint，不是 QColor）

    // ────────────────────────────────────────────────────────────────
    // 证据 ② 圆弧的正负方向：用采样确认，不靠记忆
    // ────────────────────────────────────────────────────────────────
    // drawArc(QRectF(8,96,64,64), 0, +90*16) 的圆心在 (40,128)、半径 32。
    // 数学上 45 度方向有两个候选像素：y 更小（屏幕上方）或 y 更大（下方）。
    // 设备坐标 y 轴向下，所以「数学逆时针」在屏幕上看是**朝上还是朝下**必须实测。
    {
        QImage probe(80, 80, QImage::Format_ARGB32_Premultiplied);
        probe.fill(Qt::transparent);
        QPainter p(&probe);
        p.setPen(QPen(QColor(Qt::red), 3.0));                       // qpen.h 34 行：QPen(const QBrush&, qreal width, ...)
                                                                    // QColor(Qt::red) 来自 qcolor.h 40 行
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(4, 4, 72, 72), 0, 90 * 16);                  // 圆心 (40,40)，半径 36
        const InkStats up   = scanInk(probe, 41, 80, 0, 40);          // 圆心右上（数学 y 向上那侧）
        const InkStats down = scanInk(probe, 41, 80, 41, 80);         // 圆心右下
        qInfo() << "\n=== 证据② drawArc(0, +90°) 落在哪一侧 ===";
        qInfo() << "  右上象限着墨 =" << up.inkedPixels() << " 右下象限着墨 =" << down.inkedPixels();
        qInfo() << "  结论请按数字读：**像素数大的那一侧就是正角度去的方向**"
                << "（y 轴向下让「数学逆时针」的观感很容易记反，所以别背结论，跑一次）。";
    }

    // ────────────────────────────────────────────────────────────────
    // 证据 ③ QPen / QBrush 的第一面：把默认值和枚举值打印出来（第 3 讲再深挖）
    // ────────────────────────────────────────────────────────────────
    {
        QPen def;                                    // qpen.h：默认构造 = 宽 1 的 SolidLine
        qInfo() << "\n=== 证据③ QPen/QBrush 默认值 ===";
        qInfo() << "  默认 QPen：widthF =" << def.widthF() << "（qpen.h 60 行）"
                << "isCosmetic =" << def.isCosmetic() << "（80 行）"
                << "style 枚举值 =" << int(def.style()) << "（Qt::SolidLine）"
                << "cap 枚举值 =" << int(def.capStyle()) << "（Qt::SquareCap=0x10）"
                << "join 枚举值 =" << int(def.joinStyle()) << "（Qt::BevelJoin=0x40）";
        qInfo() << "  提醒：Qt 的默认帽是 SquareCap、默认连接是 BevelJoin，不是 Flat/Miter —— "
                << "画细虚线时端头会「长出一截」，第 3 讲用测量证明。";
        QBrush b(Qt::Dense3Pattern);                 // qbrush.h 33 行
        qInfo() << "  QBrush(Qt::Dense3Pattern)：style 枚举值 =" << int(b.style())
                << "，TexturePattern 的枚举值 = 24（头文件 1163 行显式赋值）";
    }

    // ────────────────────────────────────────────────────────────────
    // 证据 ④ 逻辑坐标 vs 设备坐标：1x 与 2x 画布同一份逻辑代码
    // ────────────────────────────────────────────────────────────────
    // 真实高 DPI 是 Qt 自动做的；这里用 painter.scale(2,2) 手工复现同一件事，
    // 好处是数字可控、能直接对比。**注意填充图形的着墨像素约 4 倍，
    // 而 1 像素宽的线只有约 2 倍**（线宽不随 scale 变的话就是 cosmetic，第 3 讲正面讲）。
    {
        QImage one = renderSceneToImage(400, 220, 1.0);
        QImage two = renderSceneToImage(800, 440, 2.0);
        const InkStats s1 = scanInk(one);
        const InkStats s2 = scanInk(two);
        one.save(QStringLiteral("painting_01_hidpi_1x.png"));
        two.save(QStringLiteral("painting_01_hidpi_2x.png"));
        qInfo() << "\n=== 证据④ 同一份逻辑绘制，1x 与 2x 画布 ===";
        qInfo() << "  1x：物理" << one.width() << "x" << one.height()
                << " devicePixelRatio =" << one.devicePixelRatio()   // qimage.h 193 行
                << " 着墨 =" << s1.inkedPixels();
        qInfo() << "  2x：物理" << two.width() << "x" << two.height()
                << " devicePixelRatio =" << two.devicePixelRatio()
                << " 着墨 =" << s2.inkedPixels();
        qInfo() << "  着墨比 =" << (s1.inkedPixels() ? double(s2.inkedPixels()) / s1.inkedPixels() : 0.0)
                << "（填充图形主导时接近 4：面积按边长的平方涨。若明显小于 4，"
                << "说明细线部分没跟着放大 —— 那正是高 DPI 下「线看起来更细」的原因）";
        // setDevicePixelRatio 只是**元数据**：告诉 Qt「这份 800x440 的图，逻辑上是 400x220」。
        two.setDevicePixelRatio(2.0);                                 // qimage.h 194 行
        qInfo() << "  给 2x 图 setDevicePixelRatio(2.0) 之后：devicePixelRatio ="
                << two.devicePixelRatio() << "，而 size() 仍是"
                << two.width() << "x" << two.height()
                << "→ 它不重采样像素，只改元数据；拿它去 drawImage 才能按逻辑尺寸贴回去。";
    }

    // ────────────────────────────────────────────────────────────────
    // 证据 ⑤ 控件这一半：paintEvent 里画 + 读设备的 DPI/DPR
    // ────────────────────────────────────────────────────────────────
    Stage stage;
    stage.setWindowTitle(QStringLiteral("第 1 讲 · QPainter 三条铁律"));
    stage.resize(480, 320);                                          // qwidget.h 503 行
    stage.show();                                                    // 只是「挂号」：paintEvent 由事件循环派发

    QTimer::singleShot(400, [&stage] {
        qInfo() << "\n=== 证据⑤ 控件里的 paintEvent 现场读数 ===";
        qInfo() << "  paintEvent 已执行次数 =" << stage.paints
                << "（show() 之后至少 1 次；resize 还会再来一次）";
        qInfo() << "  devicePixelRatio =" << stage.lastDpr
                << " logicalDpi =" << stage.lastDpiX << "x" << stage.lastDpiY;
        qInfo() << "  painter.viewport() =" << stage.lastViewport
                << " painter.window() =" << stage.lastWindow
                << "（默认两者相同；不同就是第 2 讲的坐标映射）";
        qInfo() << "  如果 dpr = 2 而 dpi = 192：同一个 drawRect(140,8,60,40) 在两台机器上"
                << "占的物理像素个数不同 —— 这就是「逻辑坐标 / 设备坐标」的全部秘密。";
        stage.update();                                              // qwidget.h 470 行：再挂一次重绘
    });

    QTimer::singleShot(800, [&stage] {
        qInfo() << "  调 update() 之后 paintEvent 次数 =" << stage.paints
                << "（增 1 即证明「update() 只是再次挂号」）";
        qInfo() << "\n=== 关于「铁律一」必须说的实话 ===";
        qInfo() << "  在 paintEvent 之外对 QWidget 构造 QPainter，begin() 会**返回 false**，"
                << "后续每个 draw* 都变成空操作（isActive() 是 false，不是崩溃）。"
                << "本讲不实际演示那次非法调用：Qt 在这条路径上可能打出警告甚至断言，"
                << "而断言行为取决于 debug/release 与平台，我不替没跑过的东西作保。";
        qInfo() << "  合法的两个入口只有：① paintEvent 里对 this 画；② 对 QImage/QPixmap 等离屏设备随时画。";
    });

    QTimer::singleShot(1100, [] {
        qInfo() << "\n=== 结论 ===";
        qInfo() << "① 三条铁律：只在 paintEvent 里画控件；painter 必须先于设备析构；用 RAII + isActive() 自检。";
        qInfo() << "② 角度是 1/16 度（drawArc/drawPie/drawChord 全是 int a, int alen）；"
                << "drawText 的 y 是**基线**不是顶边。这两条是最常犯的两个错。";
        qInfo() << "③ 逻辑坐标 ≠ 物理像素：dpr/dpi 由设备给（qpaintdevice.h 48~53 行），"
                << "同一份代码在 2x 屏上要出 4 倍的填充像素，细线却不跟着涨。";
        qInfo() << "④ Qt6 本机的 RenderHint 只有 6 项，没有 HighQualityAntialiasing；"
                << "BrushStyle 是 HorPattern/VerPattern，不是 Qt4 的 HorizonPattern/VerticalPattern。";
        QCoreApplication::quit();                                    // qcoreapplication.h：静态槽 quit()
    });

    return app.exec();
}

// ============================================================================
// 附：三条铁律背后的「为什么」
// ============================================================================
/*
 *  一、为什么控件只能在 paintEvent 里画？
 *      QWidget 的绘制设备（QPaintEngine）是**按需创建、paint 期间存在**的。
 *      控件自己并不持有那块画布：它由窗口系统给（或 Qt 给的一块临时缓冲）。
 *      所以 paintEvent 之外调用 QWidget::paintEngine() 时，begin() 拿不到引擎，
 *      返回 false —— 你的绘制被静默丢弃。这不是 Qt 不近人情，而是「那块内存此刻不存在」。
 *      正确的重画姿势是 update()（攒脏区、合并、由事件循环择机派发），
 *      而不是「我现在就要画」（repaint() 是同步立即画，见 events 模块第 6 讲的合并讨论）。
 *
 *  二、为什么 painter 必须先于设备析构？
 *      QPainter 析构会调用 end()。如果设备（QImage / 控件）已经先没了，
 *      end() 就在访问一个已经不存在的对象 —— 崩溃随机出现，且常常在 release 里。
 *      两条安全写法：
 *        · 栈上：{ QPainter p(&img); ... } 让 p 和 img 的作用域自然配对（本讲 renderSceneToImage 就是这样）；
 *        · 显式：p.begin(&img); ... p.end();  —— 但必须检查 begin 的返回值，且别忘记 end。
 *      危险写法：把 QPainter 存成成员变量、或者 new 出来指望别人 delete。
 *
 *  三、逻辑坐标 / 设备坐标（Logical / Device coordinates）
 *      · 你写的每个数字都是逻辑坐标（本讲固定 400x220 那套）。
 *      · 设备坐标 = 真实像素（400x220 的 1x 图与 800x440 的 2x 图，同一句 drawRect 出的像素不同）。
 *      · QPainter 里有三层映射：世界变换（translate/rotate/scale，第 2 讲）
 *        + viewport（设备上的一块区域）+ window（逻辑坐标系里对应的区域），第 2 讲正面讲。
 *      · 高 DPI 时 Qt 把 devicePixelRatio 作为一个整体缩放塞进这条链，
 *        所以「控件看起来一样大、但更细腻」——代价是 1 像素的线需要显式决定要不要跟着缩放（cosmetic，第 3 讲）。
 *
 * ——— 老师傅的话 ———
 *   徒弟，今天不求你画得多好看，只求你把「笔什么时候能拿」这件事刻进骨头里。
 *   我带过的人里，十个有八个第一次画自定义控件是这么写的：构造函数里 QPainter p(this);
 *   p.drawLine(...); —— 程序不崩、也不报错、就是什么都没有。然后他去查三天三夜。
 *   你要学会的不是背规则，而是**判断设备**：手里这个对象是「窗口」还是「内存里的图」？
 *   窗口就得等事件循环把画笔递给你（paintEvent），内存图随时自己拿。
 *
 *   第二件事：我这讲没让你背角度方向，我让你**跑一次看像素**。
 *   y 轴向下的世界里，「正角是逆时针」这句话十次有三次把人坑哭。
 *   证据② 那种写法——两个候选象限各自数着墨像素、比大小——是这一整个模块的基本功：
 *   拿不准的，就把它变成一次测量。你以后调图形界面的 bug，靠的就是这个习惯，不是靠猜。
 *
 *   最后：本讲所有 PNG 都是代码自己存的，你打开看一眼，比读我十行注释管用。
 *   画歪了，先去看图，再去看代码，别反过来。
 *
 * ——— 小练习 ———
 *   1. 把 drawBasicScene() 里 drawText 的 y 从 210 改成 190，跑一次，
 *      再用 scanInk() 统计文本那一行（y 在 180..215 之间）的着墨像素，
 *      体会「基线」和「我以为的左上角」差了多少像素（第 4 讲会把它算成公式）。
 *   2. 把 2x 那张画布的 scale 从 2.0 改成 1.5，观察着墨比是否≈2.25，
 *      并解释为什么它一定小于「边长放大 1.5 倍」时的面积比直觉（提示：1 像素宽的线）。
 *   3. 在 paintEvent 里临时加一行 `p.setRenderHint(QPainter::Antialiasing);`
 *      （qpainter.h 414 行），重跑并存图，数一下着墨像素**变多还是变少**，
 *      再解释「抗锯齿为什么会让一条线的像素数超过 width×length」。
 */
// ============================================================================

// 本文件**没有** Q_OBJECT 类（只覆写 paintEvent，不需要元对象），因此不需要 #include "01_painter_basics.moc"。
