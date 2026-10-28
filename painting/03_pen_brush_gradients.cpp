// ============================================================================
// 第 3 讲 · QPen 细部、QBrush 与三种渐变
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   把第 1 讲「先建立概念」的两位主角彻底讲透，并让每个结论都留下可核查的像素数字：
//     1) cosmetic 画笔：线宽**是否随变换缩放**的分水岭（setCosmetic/isCosmetic）。
//        用 scale(4,4) 下的量宽度实测证明：非 cosmetic 得到 4 像素，cosmetic 得到 1 像素；
//     2) 端头（cap）与接缝（join）：用「着墨包围盒」量出 FlatCap 与 SquareCap 差半个线宽，
//        用「尖角外侧的延伸长度」量出 Miter/Bevel/Round 的次序；
//     3) 自定义虚线 setDashPattern：图案里的数字是**线宽的倍数**，实测游程长度来证实；
//     4) 纹理画刷：纹理由代码生成的 QImage 提供（**不依赖任何外部图片文件**），
//        并用「颜色翻转的 x 坐标」量出纹理周期随变换放大；
//     5) 三种渐变 QLinear/QRadial/QConical：setColorAt、setSpread、setCoordinateMode。
//        重点把 CoordinateMode 的 LogicalMode 与 ObjectBoundingMode 讲清：
//        前者用你给的**逻辑坐标**定位渐变，后者把 0..1 归一化到**被填充图形的包围盒**。
//        这条差别用「同一个渐变、同一块大矩形、两种模式、五个采样点」的数字摊开给你看。
//
// 对应构建目标名：painting_03_pen_brush_gradients
//   find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)
//   add_executable(painting_03_pen_brush_gradients 03_pen_brush_gradients.cpp)
//   target_link_libraries(painting_03_pen_brush_gradients PRIVATE Qt6::Widgets Qt6::Svg)
//   set(CMAKE_AUTOMOC ON)
//   本文件不含 Q_OBJECT（只覆写 paintEvent），末尾不需要 .moc。
//
// 运行前须知：
//   · 会往当前工作目录写 painting_03_pen_brush_gradients.png（色卡 + 渐变按钮）与
//     painting_03_gradients.png（三种渐变对照）。
//   · 无人值守：证据全部走 QImage（第 1 讲铁律一：离屏设备随时可画），
//     控件只做一次「同样的笔和刷画在窗口上」的展示，QTimer::singleShot 驱动收尾并 quit()。
//   · 本机当前无法编译链接 Qt 运行时，本示例**未在本机实测编译**；
//     用到的 API 全部对着 C:\msys64\ucrt64\include\qt6 头文件核对（下面有行号）。
//   · 特别说明：Qt6 **没有 QtGui/qgradient.h 这份实现头** —— QGradient 及三个渐变子类都声明在
//     QtGui/qbrush.h 里（第 157 行起）。大写转发包头 <QtGui/QGradient> 是有的（本机已确认存在），
//     但从 Qt5 工程搬 #include <QtGui/qgradient.h> 这种写法时会找不到文件。
//   · 本机另外只跑过 `g++ -fsyntax-only -I <Qt6 头文件目录>`：纯语法核对，
//     **没有链接、没有运行、没有可执行文件**；它只证明这些调用对着 Qt6 头文件编得过。
//   · 文中「预期」都是推理，实测数字请你自己跑出来核对。
// ============================================================================

#include <QApplication>
#include <QWidget>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QBrush>              // QtGui/qbrush.h：QBrush + QGradient + 三个渐变子类都在这
#include <QColor>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QList>               // QtCore/qlist.h：QList<qreal>（dashPattern 的类型）
#include <QString>
#include <QTimer>
#include <QCoreApplication>
#include <QDebug>

// ============================================================================
// 一、核对签名（本机 Qt6）
// ============================================================================
// QtGui/qpen.h
//   第 34/35 行  QPen(const QBrush &brush, qreal width, Qt::PenStyle s = Qt::SolidLine,
//                  Qt::PenCapStyle c = Qt::SquareCap, Qt::PenJoinStyle j = Qt::BevelJoin);
//                  ← 默认值写在头文件里：默认帽 SquareCap、默认连接 BevelJoin（亲眼所见，不是背的）
//   第 49 行  void setStyle(Qt::PenStyle);
//   第 51 行  QList<qreal> dashPattern() const;      第 52 行 void setDashPattern(const QList<qreal> &pattern);
//   第 54/55 行 qreal dashOffset() const; void setDashOffset(qreal);
//   第 58 行  void setMiterLimit(qreal limit);       ← MiterJoin 尖角长度的上限，本讲用它解释测量差异
//   第 60/61 行 qreal widthF() const; void setWidthF(qreal);   第 64 行 void setWidth(int);
//   第 67 行  void setColor(const QColor &color);    第 70 行 void setBrush(const QBrush &brush);
//   第 72 行  bool isSolid() const;
//   第 74/75 行 Qt::PenCapStyle capStyle() const; void setCapStyle(Qt::PenCapStyle);
//   第 77/78 行 Qt::PenJoinStyle joinStyle() const; void setJoinStyle(Qt::PenJoinStyle);
//   第 80/81 行 bool isCosmetic() const; void setCosmetic(bool cosmetic);
// QtGui/qbrush.h
//   第 32~44 行  QBrush() / QBrush(Qt::BrushStyle) / QBrush(const QColor&, Qt::BrushStyle = Qt::SolidPattern)
//                / QBrush(const QImage&) / QBrush(const QGradient&)
//   第 59 行  void setStyle(Qt::BrushStyle);         第 67 行 QImage textureImage() const;
//   第 65/68 行 void setTexture(const QPixmap&);     void setTextureImage(const QImage &image);
//   第 74 行  const QGradient *gradient() const;     ← 画刷里塞的是不是渐变，用它问
//   第 157 行 class Q_GUI_EXPORT QGradient
//   第 160~165 行 enum Type { LinearGradient, RadialGradient, ConicalGradient, NoGradient };
//   第 168~172 行 enum Spread { PadSpread, ReflectSpread, RepeatSpread };       ← 名字带 Spread 后缀
//   第 176~180 行 enum CoordinateMode { LogicalMode, StretchToDeviceMode, ObjectBoundingMode, ObjectMode };
//                  ← Qt5 那份是 { LogicalMode, DeviceMode, ObjectBoundingMode }；
//                    Qt6 把 DeviceMode 换成 StretchToDeviceMode，并新增 ObjectMode。**别照抄老代码的枚举名**。
//   第 369 行  inline void setSpread(Spread spread);  第 370 行 Spread spread() const;
//   第 372 行  void setColorAt(qreal pos, const QColor &color);   ← pos 归一化到 0..1
//   第 374/375 行 void setStops(const QGradientStops&); QGradientStops stops() const;
//   第 377/378 行 CoordinateMode coordinateMode() const; void setCoordinateMode(CoordinateMode mode);
//   第 416~430 行 class QLinearGradient : public QGradient
//                  第 420 行 QLinearGradient(const QPointF &start, const QPointF &finalStop);
//                  第 421 行 QLinearGradient(qreal xStart, qreal yStart, qreal xFinalStop, qreal yFinalStop);
//                  第 428/429 行 QPointF finalStop() const; void setFinalStop(const QPointF&);
//   第 434~461 行 class QRadialGradient：
//                  第 441/442 行 QRadialGradient(const QPointF &center, qreal radius);
//                            / QRadialGradient(qreal cx, qreal cy, qreal radius);
//                  第 438/439 行 带焦点的四个参数版本；第 450/454/457/458 行 setCenter/setFocalPoint/radius/setRadius
//   第 468~481 行 class QConicalGradient：
//                  第 472/473 行 QConicalGradient(const QPointF &center, qreal startAngle);
//                            / QConicalGradient(qreal cx, qreal cy, qreal startAngle);
//                  第 477/481 行 setCenter(...) / setAngle(qreal)   ← startAngle 单位是**度**
// QtCore/qnamespace.h
//   第 1115~1126 行 PenStyle：NoPen / SolidLine / DashLine / DotLine / DashDotLine /
//                            DashDotDotLine / CustomDashLine（自定义虚线必须用这一个）
//   第 1128~1133 行 PenCapStyle：FlatCap = 0x00 / SquareCap = 0x10 / RoundCap = 0x20
//   第 1135~1141 行 PenJoinStyle：MiterJoin = 0x00 / BevelJoin = 0x40 / RoundJoin = 0x80 / SvgMiterJoin = 0x100
//   第 1143~1163 行 BrushStyle：NoBrush / SolidPattern / Dense1Pattern..Dense7Pattern /
//                  HorPattern / VerPattern / CrossPattern / BDiagPattern / FDiagPattern /
//                  DiagCrossPattern / LinearGradientPattern / RadialGradientPattern /
//                  ConicalGradientPattern / TexturePattern = 24
//   第 1139 行  SvgMiterJoin —— Qt 特有的「不套 miterLimit」的尖角连接，本讲测量时把它当第五种连接看
// QtGui/qpainter.h（本讲新增）
//   第 52~57 行  RenderHint：NonCosmeticBrushPatterns = 0x80 —— 打开后**图案画刷**也按设备像素走，
//                行为向 cosmetic 靠拢；本讲不依赖它，默认关闭。
//   第 155/161 行 setPen(const QPen&) / setBrush(const QBrush&)
//   第 218 行   void scale(qreal sx, qreal sy)      （cosmetic 的对照实验要用它）

// ============================================================================
// 二、像素证据工具
// ============================================================================
struct Extent
{
    int count = 0;
    int minX = 1 << 30, maxX = -(1 << 30);
    int minY = 1 << 30, maxY = -(1 << 30);
};

// 整幅图数不透明像素（qimage.h 169 行 constScanLine，32 位格式下每像素一个 QRgb）
static int countInk(const QImage &img)
{
    int n = 0;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            if ((row[x] >> 24) != 0)
                ++n;
    }
    return n;
}

// 在指定行上量「连续着墨的游程长度」（dashPattern 的证据就靠它）
static QList<int> inkRunsOnRow(const QImage &img, int y, int x0, int x1)
{
    QList<int> runs;
    if (y < 0 || y >= img.height()) return runs;
    const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));   // qimage.h 169 行
    int run = 0;
    for (int x = x0; x < x1; ++x) {
        if ((row[x] >> 24) != 0) {
            ++run;
        } else if (run > 0) {
            runs.append(run);
            run = 0;
        }
    }
    if (run > 0)
        runs.append(run);
    return runs;
}

// 画一条水平线并返回它的着墨包围盒（cap/join 实验的通用套路）
static Extent drawHorizontalLine(int len, const QPen &pen)
{
    QImage img(len + 60, 40, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);                    // qpainter.h 163 行：setBrush(Qt::BrushStyle)
    p.drawLine(30, 20, 30 + len, 20);           // qpainter.h 253 行（int,int,int,int 版）
    Extent e;
    const QImage r = img;
    for (int y = 0; y < r.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(r.constScanLine(y));
        for (int x = 0; x < r.width(); ++x) {
            if ((row[x] >> 24) != 0) {
                ++e.count;
                if (x < e.minX) e.minX = x;
                if (x > e.maxX) e.maxX = x;
                if (y < e.minY) e.minY = y;
                if (y > e.maxY) e.maxY = y;
            }
        }
    }
    return e;
}

// ============================================================================
// 三、证据 ①：cosmetic —— 线宽到底跟不跟变换走
// ============================================================================
static void proofCosmetic()
{
    qInfo() << "\n=== 证据① setCosmetic(true/false) 在 scale(4,4) 下的线宽 ===";

    for (int variant = 0; variant < 2; ++variant) {
        const bool cosmetic = (variant == 1);
        QImage img(120, 80, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        {
            QPainter p(&img);
            p.scale(4.0, 4.0);                             // qpainter.h 218 行
            QPen pen(QColor(Qt::black), 1.0);              // 逻辑线宽 1
            pen.setCosmetic(cosmetic);                      // qpen.h 81 行
            p.setPen(pen);
            p.drawLine(10, 5, 10, 12);                     // 逻辑上长 7 的竖线
            qInfo() << "  [" << (cosmetic ? "cosmetic" : "非 cosmetic")
                    << "] pen.isCosmetic() =" << pen.isCosmetic()     // qpen.h 80 行
                    << "widthF() =" << pen.widthF();
        }
        // 在中间那一行上量「这条线占几个物理像素」
        const QList<int> runs = inkRunsOnRow(img, 34, 0, img.width());
        qInfo() << "    逻辑竖线在设备上的横向游程（像素宽）=" << runs
                << " 全图着墨 =" << countInk(img)
                << "（预期：非 cosmetic ≈ 4 宽，cosmetic ≈ 1 宽）";
    }
    qInfo() << "  为什么重要：高 DPI 下你希望 1 像素的网格线**永远是 1 像素**（不糊成 2 像素），"
            << "那就该用 cosmetic；反过来画示意图里的「粗箭头」希望随缩放变粗，就不能 cosmetic。"
            << "第 6 讲做缩略图/可缩放画布时这条会直接决定观感。";
}

// ============================================================================
// 四、证据 ②：cap（端头）—— 量包围盒就看得出 Flat 与 Square 差半个线宽
// ============================================================================
static void proofCapStyles()
{
    qInfo() << "\n=== 证据② capStyle：线从 x=30 到 x=130（长度 100），线宽 20 ===";
    const Qt::PenCapStyle caps[3] = { Qt::FlatCap, Qt::SquareCap, Qt::RoundCap };
    const char *capNames[3] = { "FlatCap", "SquareCap", "RoundCap" };
    for (int i = 0; i < 3; ++i) {
        QPen pen(QColor(0, 90, 160), 20.0);
        pen.setCapStyle(caps[i]);                           // qpen.h 75 行
        const Extent e = drawHorizontalLine(100, pen);
        qInfo() << "  " << capNames[i] << "（枚举值 " << int(caps[i]) << "）:"
                << "着墨 x[" << e.minX << ".." << e.maxX << "]"
                << " 总长 =" << (e.maxX - e.minX + 1) << "px"
                << " 着墨像素 =" << e.count;
    }
    qInfo() << "  读法：线本身占 100px（30..129）。FlatCap 应当正好 100；"
            << "SquareCap 两端各伸出 width/2 = 10px，应当 120；"
            << "RoundCap 外伸同样是 10px 但**拐角被削圆**，所以总长与 Square 接近、像素数更少。"
            << "（默认就是 SquareCap —— 第 1 讲那条「细虚线端头会长出一截」的根源在此。）";
}

// ============================================================================
// 五、证据 ③：join（接缝）—— 量尖角向外伸了多远
// ============================================================================
static void proofJoinStyles()
{
    qInfo() << "\n=== 证据③ joinStyle：V 形折线，尖角在 (60,30)，线宽 14 ===";
    const Qt::PenJoinStyle joins[4] = { Qt::MiterJoin, Qt::BevelJoin, Qt::RoundJoin, Qt::SvgMiterJoin };
    const char *joinNames[4] = { "MiterJoin", "BevelJoin", "RoundJoin", "SvgMiterJoin" };
    for (int i = 0; i < 4; ++i) {
        QImage img(120, 90, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        {
            QPainter p(&img);
            QPen pen(QColor(120, 40, 160), 14.0);
            pen.setJoinStyle(joins[i]);                     // qpen.h 78 行
            pen.setMiterLimit(8.0);                         // qpen.h 58 行：尖角最长允许 8 倍线宽
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            QPoint pts[3] = { QPoint(20, 10), QPoint(60, 60), QPoint(100, 10) };
            p.drawPolyline(pts, 3);                          // qpainter.h 285 行（裸数组版）
        }
        int ink = 0, maxY = -1;
        for (int y = 0; y < img.height(); ++y) {
            const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            for (int x = 0; x < img.width(); ++x)
                if ((row[x] >> 24) != 0) { ++ink; if (y > maxY) maxY = y; }
        }
        // 尖角顶点在 y=60，看接缝向下（y 更大）伸了多少
        qInfo() << "  " << joinNames[i] << "（枚举值 " << int(joins[i]) << "）:"
                << " 最下缘 y =" << maxY << " 尖角外伸 =" << (maxY - 60) << "px 着墨 =" << ink;
    }
    qInfo() << "  读法：Miter/SvgMiter 会在尖角外拉出一个**长刺**（外伸最大）；"
            << "Bevel 直接削平（外伸最小）；Round 补一段圆弧，外伸介于两者之间、像素数比 Bevel 多。"
            << "外伸还受 miterLimit 截断（超限会退化成 Bevel），所以别拿数字当常量看。";
}

// ============================================================================
// 六、证据 ④：CustomDashLine 与 dashPattern —— 数字是「线宽的倍数」
// ============================================================================
static void proofDashPattern()
{
    qInfo() << "\n=== 证据④ setDashPattern：图案 {2.0, 1.0, 0.5, 1.0} × 线宽 10 ===";
    QImage img(220, 30, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPen pen(QColor(20, 130, 60), 10.0);
    pen.setStyle(Qt::CustomDashLine);                       // qnamespace.h 1125 行：必须这一档
    QList<qreal> dashes;
    dashes << 2.0 << 1.0 << 0.5 << 1.0;                     // 奇数项=画、偶数项=空（单位：线宽倍数）
    pen.setDashPattern(dashes);                             // qpen.h 52 行
    {
        QPainter p(&img);
        p.setPen(pen);
        p.drawLine(5, 15, 215, 15);
    }
    const QList<int> runs = inkRunsOnRow(img, 15, 0, img.width());
    qInfo() << "  写进去的图案 =" << dashes;
    qInfo() << "  读回来的图案（dashPattern()，qpen.h 51 行）=" << pen.dashPattern();
    qInfo() << "  在中间一行实测到的着墨游程（像素）=" << runs
            << "  线宽 10 → 预期序列 20,10,5,10 循环";
    qInfo() << "  dashOffset() 当前 =" << pen.dashOffset()   // qpen.h 54 行
            << "（想「让虚线跑起来」就是改它，不是重画整条线）";
    qInfo() << "  注意两点：① 图案里的数值**不能为 0**（官方文档明确禁止，写 0 会让虚线退化成实线）；"
            << "② DotLine 这类内置样式其实就是同样的机制配好了默认图案，想精确控节奏才用 CustomDashLine。";
}

// ============================================================================
// 七、证据 ⑤：纹理画刷（代码生成的棋盘，不读外部文件）
// ============================================================================
static QImage makeChecker(int side, const QColor &a, const QColor &b)
{
    QImage img(side * 2, side * 2, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const bool hi = ((x / side) + (y / side)) % 2 == 0;
            img.setPixelColor(x, y, hi ? a : b);            // qimage.h 187 行
        }
    }
    return img;
}

static void proofTextureBrush()
{
    qInfo() << "\n=== 证据⑤ 纹理画刷：4x4 棋盘 × 变换放大后的周期 ===";
    const QImage checker = makeChecker(4, QColor(250, 200, 60), QColor(60, 60, 70));
    QBrush tex(checker);                                    // qbrush.h 40 行：QBrush(const QImage&)
    qInfo() << "  brush.style() 枚举值 =" << int(tex.style())
            << "（Qt::TexturePattern 应为 " << int(Qt::TexturePattern) << "）";
    qInfo() << "  brush.textureImage().size() =" << tex.textureImage().size();   // qbrush.h 67 行

    for (int variant = 0; variant < 2; ++variant) {
        const qreal sx = (variant == 0) ? 1.0 : 4.0;
        QImage img(200, 40, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        {
            QPainter p(&img);
            p.scale(sx, 1.0);
            p.setPen(Qt::NoPen);
            p.setBrush(tex);
            p.drawRect(QRectF(0, 4, 200.0 / sx, 32));        // 逻辑宽度调小，让设备宽度始终 200
        }
        // 数「颜色翻转」出现在哪些 x，得到纹理周期
        QList<int> flips;
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(20));
        for (int x = 1; x < img.width(); ++x)
            if (row[x] != row[x - 1]) flips.append(x);
        qInfo() << "  scale(" << sx << ",1)：前 8 个颜色翻转的 x =" << flips.mid(0, 8)
                << " 翻转次数 =" << flips.size();
    }
    qInfo() << "  读法：纹理是**跟着坐标系一起缩放**的（横向翻转的间隔从 4px 变成 16px），"
            << "这跟 cosmetic 画笔的取舍是同一条线：一个跟变换、一个不跟。"
            << "想要「纹理不随缩放」得自己算 brushTransform（QBrush::setTransform，qbrush.h 61 行附近）。";
}

// ============================================================================
// 八、证据 ⑥：三种渐变 + CoordinateMode 的实测差别
// ============================================================================
static QColor at(const QImage &img, int x, int y)
{
    return img.pixelColor(x, y);                            // qimage.h 184 行
}

static void printColor(const char *tag, const QColor &c)
{
    qInfo() << "    " << tag << " = rgb(" << c.red() << "," << c.green() << "," << c.blue()
            << ") a=" << c.alpha() << " name=" << c.name();  // qcolor.h 84~86、62 行
}

static void proofGradients()
{
    qInfo() << "\n=== 证据⑥ 三种渐变与 CoordinateMode ===";

    QImage img(620, 220, QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(250, 250, 252));                        // qimage.h 198 行：fill(const QColor&)

    // ── 线性渐变：横向黑→白，五个采样点 ──
    {
        QLinearGradient g(qreal(10), qreal(10), qreal(200), qreal(10));   // qbrush.h 421 行
        g.setColorAt(0.0, QColor(10, 10, 10));                            // qbrush.h 372 行
        g.setColorAt(1.0, QColor(250, 250, 250));
        QBrush b(g);                                                      // qbrush.h 44 行
        qInfo() << "  QBrush(线性渐变) 的 style 枚举值 =" << int(b.style())
                << "（Qt::LinearGradientPattern = " << int(Qt::LinearGradientPattern) << "）";
        QPainter p(&img);
        p.setPen(Qt::NoPen);
        p.setBrush(b);
        p.drawRect(QRectF(10, 10, 190, 40));
        p.setPen(QColor(80, 80, 80));
        p.drawRect(QRectF(10, 10, 190, 40));
        qInfo() << "  线性（LogicalMode，坐标 10..200）采样：";
        printColor("x=10 ", at(img, 11, 30));
        printColor("x=55 ", at(img, 55, 30));
        printColor("x=105", at(img, 105, 30));
        printColor("x=155", at(img, 155, 30));
        printColor("x=199", at(img, 199, 30));
    }

    // ── 径向渐变：中心→外圈采样，再用两块「全在半径外」的矩形对比 Pad 与 Repeat ──
    {
        QRadialGradient g(QPointF(300.0, 40.0), 70.0);       // qbrush.h 438 行（QPointF, qreal radius）
        g.setColorAt(0.0, QColor(255, 80, 80));
        g.setColorAt(1.0, QColor(20, 20, 90));
        g.setSpread(QGradient::PadSpread);                   // qbrush.h 369 行 + 168~172 行枚举
        // 注意：默认 CoordinateMode 是 LogicalMode（qbrush.h 409 行的成员初值），
        // 所以下面这个渐变**钉在画布坐标 (300,40) 上**，与画到哪个矩形无关。
        QPainter p(&img);
        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(g));
        p.drawRect(QRectF(230, 0, 140, 80));                 // 正好覆盖圆心 ± 半径
        qInfo() << "  径向（PadSpread）r=70，圆心 (300,40)：";
        printColor("  圆心(300,40)  ", at(img, 300, 40));
        printColor("  半半径(335,40)", at(img, 335, 40));
        printColor("  边缘内(369,40)", at(img, 369, 40));
        p.setBrush(QBrush(g));                               // 半径之外那一块，Pad 应当是一片末端色
        p.drawRect(QRectF(400, 0, 90, 80));                  // 这块矩形整体在半径外（离圆心 100~190）
        printColor("  半径外(430,40) ", at(img, 430, 40));
        printColor("  半径外(485,40) ", at(img, 485, 40));
        g.setSpread(QGradient::RepeatSpread);
        p.setBrush(QBrush(g));
        p.drawRect(QRectF(500, 0, 110, 80));                 // 同样在半径外，但用 Repeat
        printColor("  Repeat(530,40) ", at(img, 530, 40));
        printColor("  Repeat(585,40) ", at(img, 585, 40));
        qInfo() << "  → 上面四个「半径外」的采样就是 Spread 的全部差别："
                << "PadSpread 冻在最后一个色标（两个点应当同色）；"
                << "ReflectSpread 镜像循环、RepeatSpread 顺序循环（两个点应当不同色、并且与圆内的色标值成周期关系）。";
    }

    // ── 锥形（角度）渐变：startAngle 单位是度 ──
    {
        QConicalGradient g(QPointF(60.0, 150.0), 45.0);      // qbrush.h 472 行（QPointF, qreal angle）
        g.setColorAt(0.0, QColor(0, 180, 0));
        g.setColorAt(0.5, QColor(0, 0, 200));
        g.setColorAt(1.0, QColor(0, 180, 0));                // 首尾同色 → 接缝看不出来
        QPainter p(&img);
        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(g));
        p.drawRect(QRectF(0, 90, 120, 120));
        qInfo() << "  锥形（起始角 45°）在四个方向的采样：";
        printColor("  右(110,150)", at(img, 110, 150));
        printColor("  上(60,100)",  at(img, 60, 100));
        printColor("  左(10,150)",  at(img, 10, 150));
        printColor("  下(60,200)",  at(img, 60, 200));
        qInfo() << "  色标是按**角度比例**分配的（0 在起始角，1 绕一圈回来），"
                << "所以四个方向的采样值应呈现对称/循环关系 —— 数字请核对，方向和第 2 讲一样别背。";
    }

    // ── CoordinateMode：LogicalMode vs ObjectBoundingMode（本讲最要紧的一条）──
    {
        QImage cm(300, 130, QImage::Format_ARGB32_Premultiplied);
        cm.fill(Qt::transparent);
        QPainter p(&cm);

        QLinearGradient logical(qreal(0), qreal(0), qreal(60), qreal(0));
        logical.setColorAt(0.0, QColor(255, 0, 0));
        logical.setColorAt(1.0, QColor(0, 0, 255));
        logical.setCoordinateMode(QGradient::LogicalMode);   // qbrush.h 378 行（默认值，见 409 行 m_coordinateMode = LogicalMode）
        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(logical));
        p.drawRect(QRectF(0, 0, 290, 60));                   // 图形比渐变轴长得多

        QLinearGradient ob(qreal(0), qreal(0), qreal(1), qreal(0));
        ob.setColorAt(0.0, QColor(255, 0, 0));
        ob.setColorAt(1.0, QColor(0, 0, 255));
        ob.setCoordinateMode(QGradient::ObjectBoundingMode); // qbrush.h 179 行：0..1 归一化到**被画图形的包围盒**
        p.setBrush(QBrush(ob));
        p.drawRect(QRectF(0, 70, 290, 55));

        qInfo() << "  LogicalMode（渐变轴 0..60，矩形宽 290）坐标模式枚举值 =" << int(logical.coordinateMode());
        printColor("    x=20 ", at(cm, 20, 30));
        printColor("    x=59 ", at(cm, 59, 30));
        printColor("    x=150", at(cm, 150, 30));            // 早已越过渐变终点 → 应是末端色（Pad）
        printColor("    x=280", at(cm, 280, 30));
        qInfo() << "  ObjectBoundingMode（渐变轴 0..1，同一块矩形）坐标模式枚举值 =" << int(ob.coordinateMode());
        printColor("    x=20 ", at(cm, 20, 100));
        printColor("    x=150", at(cm, 150, 100));           // 图形中点 → 应是中间色
        printColor("    x=280", at(cm, 280, 100));
        qInfo() << "  → 这就是两条模式的全部差别：LogicalMode 的渐变轴是**画布上的逻辑坐标**，"
                << "图形超出范围就只有末端色（看起来像「只染了一小条」）；"
                << "ObjectBoundingMode 把渐变轴按图形包围盒归一化，坐标写 0..1，"
                << "**图形多大渐变就铺多大**，按钮、卡片、进度条一律用它。";
        cm.save(QStringLiteral("painting_03_gradients.png"));
    }
}

// ============================================================================
// 九、成品图样：色卡 + 渐变按钮（不靠任何外部素材）
// ============================================================================
static void drawSwatchesAndButton(QPainter &p, int w, int h)
{
    // 色卡：Qt::GlobalColor 的名字（小写开头，qnamespace.h 29 行起）与 Dense1..7 图案
    p.setPen(QColor(40, 40, 40));
    const Qt::GlobalColor globals[6] = { Qt::red, Qt::green, Qt::blue,
                                         Qt::cyan, Qt::magenta, Qt::yellow };
    for (int i = 0; i < 6; ++i) {
        p.setBrush(QColor(globals[i]));                     // GlobalColor → QColor（qcolor.h 40 行）
        p.drawRect(8 + i * 34, 8, 30, 30);
    }
    const Qt::BrushStyle dense[7] = { Qt::Dense1Pattern, Qt::Dense2Pattern, Qt::Dense3Pattern,
                                      Qt::Dense4Pattern, Qt::Dense5Pattern, Qt::Dense6Pattern,
                                      Qt::Dense7Pattern };
    int ink[7] = { 0, 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 7; ++i) {
        p.setPen(QColor(60, 60, 60));
        p.setBrush(QBrush(QColor(200, 60, 60), dense[i]));          // qbrush.h 34 行
        p.drawRect(8 + i * 26, 44, 22, 22);
        QImage one(22, 22, QImage::Format_ARGB32_Premultiplied);
        one.fill(Qt::transparent);
        {
            QPainter q(&one);
            q.setPen(Qt::NoPen);
            q.setBrush(QBrush(QColor(200, 60, 60), dense[i]));
            q.drawRect(0, 0, 22, 22);
        }
        for (int y = 0; y < 22; ++y) {
            const QRgb *row = reinterpret_cast<const QRgb *>(one.constScanLine(y));
            for (int x = 0; x < 22; ++x) if ((row[x] >> 24) != 0) ++ink[i];
        }
    }
    qInfo() << "  Dense1..Dense7 在 22x22 方块里的着墨像素（应单调变化）="
            << ink[0] << ink[1] << ink[2] << ink[3] << ink[4] << ink[5] << ink[6]
            << "（Dense1 最密 / Dense7 最疏 —— 谁多谁少请按数字确认，别信我的记忆）";

    // 「渐变按钮」：圆角矩形 + 纵向线性渐变（ObjectBoundingMode）+ 描边 + 文字
    const QRectF btn(8, 78, 150, 34);
    QLinearGradient sheen(btn.topLeft(), btn.bottomLeft());
    sheen.setColorAt(0.0, QColor(90, 160, 240));
    sheen.setColorAt(0.5, QColor(50, 110, 200));
    sheen.setColorAt(1.0, QColor(30, 80, 160));
    sheen.setCoordinateMode(QGradient::ObjectBoundingMode);
    p.setPen(QPen(QColor(20, 60, 120), 1.0));
    p.setBrush(QBrush(sheen));
    p.drawRoundedRect(btn, 6.0, 6.0);                       // qpainter.h 309 行
    p.setPen(QColor(255, 255, 255));
    p.drawText(btn, Qt::AlignCenter, QStringLiteral("渐变按钮"));   // qpainter.h 374/375 行的 (矩形, flags, 文本) 版

    // 外框顺便把画布尺寸用起来：证据图里它是「画布边界」的参照
    p.setPen(QColor(190, 190, 200));
    p.setBrush(Qt::NoBrush);
    p.drawRect(0, 0, w - 1, h - 1);                                   // qpainter.h 267 行（int 版）
}

class Stage : public QWidget
{
public:
    explicit Stage(QWidget *parent = nullptr) : QWidget(parent) {}

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter p(this);                                   // 第 1 讲铁律一：控件只在 paintEvent 里画
        p.setRenderHint(QPainter::Antialiasing, true);      // qpainter.h 414 行
        p.fillRect(0, 0, width(), height(), QColor(248, 248, 252));
        drawSwatchesAndButton(p, width(), height());
    }
};

// ============================================================================
// 十、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // ── 主证据：把色卡 + 渐变按钮画进 QImage 并存盘 ──
    QImage card(420, 240, QImage::Format_ARGB32_Premultiplied);
    card.fill(QColor(250, 250, 252));
    {
        QPainter p(&card);
        p.setRenderHint(QPainter::Antialiasing, true);
        drawSwatchesAndButton(p, card.width(), card.height());
    }
    const bool saved = card.save(QStringLiteral("painting_03_pen_brush_gradients.png"));
    qInfo() << "=== 成品图样 ===";
    qInfo() << "  尺寸 =" << card.width() << "x" << card.height()
            << " save() =" << saved << " 非透明像素 =" << countInk(card);
    // 按钮矩形是 (8,78,150,34) → 中心 (83,95)；纵向渐变的中点应当接近 setColorAt(0.5) 的色标
    printColor("  按钮中心 pixelColor(83,95)", at(card, 83, 95));
    printColor("  按钮上沿 pixelColor(83,80)", at(card, 83, 80));
    printColor("  按钮下沿 pixelColor(83,110)", at(card, 83, 110));
    qInfo() << "  ↑ ObjectBoundingMode 的纵向渐变，中点应该接近 setColorAt(0.5) 的 (50,110,200)；"
            << "如果差得远，先检查按钮矩形与采样坐标有没有对上，再怀疑 Qt。";
    qInfo() << "  （背景是不透明的，所以非透明像素应当正好 = 宽 x 高 = "
            << card.width() * card.height() << "；少一个就说明有一块区域没被覆盖。）";

    // ── 其余六条证据 ──
    proofCosmetic();
    proofCapStyles();
    proofJoinStyles();
    proofDashPattern();
    proofTextureBrush();
    proofGradients();

    Stage stage;
    stage.setWindowTitle(QStringLiteral("第 3 讲 · 画笔、画刷与渐变"));
    stage.resize(420, 240);
    stage.show();

    QTimer::singleShot(450, [] {
        qInfo() << "\n=== 结论 ===";
        qInfo() << "① 线宽跟不跟变换走，只有 cosmetic 一个开关（证据①：4px vs 1px）。";
        qInfo() << "② 端头/接缝/虚线全是**可测量的几何**：包围盒长、最下缘、游程序列，别背概念。";
        qInfo() << "③ 默认帽是 SquareCap、默认连接是 BevelJoin —— 这是 qpen.h 第 35 行写死的，不是我以为的。";
        qInfo() << "④ 渐变优先用 ObjectBoundingMode + 0..1 坐标（证据⑥最后一段的对比最直观）；"
                << "需要「渐变钉在画布某条线上」时才用 LogicalMode。";
        qInfo() << "⑤ Qt6 枚举名的三处坑：Spread 叫 *Spread（不是 *Mode）、BrushStyle 是 HorPattern（不是 HorizonPattern）、"
                << "CoordinateMode 没有 DeviceMode（换成了 StretchToDeviceMode / ObjectMode）。";
        QCoreApplication::quit();
    });

    return app.exec();
}

// ============================================================================
// 附：这讲的东西在真实项目里对应什么
// ============================================================================
/*
 *  需求                                  做法                                              代价
 *  -------------------------------------------------------------------------------
 *  1px 网格/分割线在任何 DPI 下都清晰      QPen + setCosmetic(true)                         放大图形时线不变粗
 *  示意图里「放大就变粗」的箭头            普通（非 cosmetic）宽笔，配合 scale              高 DPI 下要保证 widthF 按比例给
 *  圆角虚线端头不「长出」                  setCapStyle(Qt::FlatCap)                         短虚线会显得更短
 *  折线外角要尖                            MiterJoin + 调 miterLimit                        极锐角会出现长刺
 *  让虚线滚动起来                           递增 setDashOffset()                             每帧重画，注意只在 paintEvent 里
 *  棋盘/斜纹底纹                            setTextureImage(代码生成的 QImage)               纹理随变换缩放（证据⑤）
 *  按钮/卡片表面光                          QLinearGradient + ObjectBoundingMode              色标只认 0..1，越界按 Spread 规则
 *  选中高亮「从焦点发散」                   QRadialGradient(center, radius, focalPoint)      四参数版本在 qbrush.h 438/439 行
 *  饼图色环 / 方向指示                       QConicalGradient(center, startAngle)            首尾色标要接上，否则有接缝
 *
 * 三条纪律：
 *  ① **画刷填充走 draw*，不走 fill***：证据里我一律用 setBrush + drawRect，
 *     因为 setBrush 之后每个几何函数（drawRect/drawEllipse/drawPolygon/drawRoundedRect）
 *     都会用它；fillRect 只填不改状态，写混了容易出现「这里为什么没渐变」的错觉。
 *  ② 渐变的 pos / 角度单位别混：setColorAt 的 pos 是**归一化 0..1**，
 *     QConicalGradient 的 angle 是**度**（第 2 讲那条 drawArc 用 1/16 度的坑这里不适用）。
 *  ③ 想验证观感，就**先存 PNG 再看图**，别只读 qInfo 的数字：
 *     数字能证明「坐标对上了」，图片才能证明「好看」。
 *
 * ——— 老师傅的话 ———
 *   徒弟，这一讲的手艺叫「把观感换成尺寸」。
 *   「SquareCap 会伸出半个线宽」这句话，你听过一百遍也还是会忘；
 *   但你亲手量出 100px 的线变成 120px，就一辈子记得。
 *   所以我把 cap、join、dash 三处都写成「量包围盒 / 量最下缘 / 量游程」，
 *   不是为了好看，是为了让你以后遇到「线帽不对」这种问题，
 *   第一反应是去量，而不是去猜、去改三遍代码碰运气。
 *
 *   渐变这块我提醒一句：很多人把 LogicalMode 当默认用，然后抱怨
 *   「为什么这个按钮一变形，高光就只剩左边一条」。原因就在证据⑥最后那段：
 *   渐变轴是画布坐标，图形长到 290 而轴只到 60，右边当然全是末端色。
 *   做 UI 就认 ObjectBoundingMode，做「数据驱动的等高线/热图」才回去用逻辑坐标。
 *
 *   最后：纹理那一段我特意让代码自己生成棋盘。
 *   示例库里的依赖越少，越容易在 CI、在别人的机器上跑起来。
 *   一个「读一张外部 png」的示例，一半的失败是路径问题，不是 Qt 问题。
 *
 * ——— 小练习 ———
 *   1. 把证据② 的线宽从 20 改成 1，重新量三种 cap 的总长。
 *      解释为什么整数宽度 1 的线上 Flat 与 Square 的差别只有 1px，
 *      以及为什么 Qt 仍把 SquareCap 设为默认。
 *   2. 给证据④ 的图案里加一个 0.0（{2.0, 0.0, 1.0, 1.0}），
 *      实测游程序列怎么变，验证「0 不允许」这条文档说法在你机器上的实际表现，
 *      并把结论写回本文件的注释里。
 *   3. 用 QRadialGradient 的**带焦点**版本（qbrush.h 438 行）把证据⑥的径向渐变
 *      改成「高光偏左上」的按钮，采样 4 个点确认焦点生效；
 *      再把 spread 依次换 PadSpread/ReflectSpread/RepeatSpread，
 *      把三种在半径外的采样值抄进注释表格里。
 */
// ============================================================================

// 本文件没有 Q_OBJECT 类，不需要 #include "03_pen_brush_gradients.moc"。
