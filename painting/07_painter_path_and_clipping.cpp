// ============================================================================
// 第 7 讲 · QPainterPath、裁剪与合成模式
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   把「高级绘制」三件趁手兵器一次给齐，并且每件都用像素数字交账：
//     1) QPainterPath 的构造 API：addRect / addEllipse / addRoundedRect / addPolygon /
//        addPath / moveTo / lineTo / cubicTo / quadTo / arcTo / closeSubpath
//        （逐个对着 qpainterpath.h 核过，行号在下面）。
//        外加「读回来看它记住了什么」：elementCount() / elementAt(i) / currentPosition() /
//        boundingRect() / isEmpty() / fillRule()；
//     2) 填充规则 Qt::FillRule：OddEvenFill vs WindingFill。
//        用一颗**自交的五角星**把差别打成像素：同一条路径、两种规则、中心点一个有洞一个没洞；
//     3) 路径布尔：本机头文件里它们是**成员函数** united() / intersected() / subtracted()
//        （qpainterpath.h 155/156/157 行）与 operator| & -（164~171 行），
//        **Qt4 那套自由函数 unitedPath()/intersectedPath() 在本机头文件里没有**，别照抄；
//        simplified()（159 行）存在，本讲不只打印「简化前后 elementCount 的变化」，
//        还实测了它**会不会改掉你的填充规则**（qpainterpath.cpp 3568~3573 行的文档说一律打回
//        OddEvenFill；本机这一轮：WindingFill 的星形 simplified() 后回读 fillRule() = OddEvenFill，
//        文档成立 —— 所以简化完要记得把规则设回去）。
//     4) 裁剪三件套：setClipRect / setClipPath / setClipRegion（qpainter.h 188~192 行），
//        第二个参数是 Qt::ClipOperation：本机只有 **NoClip / ReplaceClip / IntersectClip**
//        （qnamespace.h 1339~1343 行），**没有 UnionClip**——想做「并集裁剪」，
//        正确做法是把 QRegion 自己 united() 好再一次性 setClipRegion（本讲就这么做）；
//        配 hasClipping() / clipRegion() 把「裁到哪了」读回来打印；
//     5) 合成模式 QPainter::CompositionMode_*（qpainter.h 98~118 行的枚举），
//        演示「用合成模式画蒙版」：DestinationIn = 只保留已有像素在新画内容 alpha 里的部分。
//        本讲把 12 种模式画成一张色卡，逐个打印中心采样值与非透明像素数。
//
// 对应构建目标名：painting_07_painter_path_and_clipping
//   find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)   # 本组统一链 Svg（第 8 讲要用），本讲只用到 Widgets
//   add_executable(painting_07_painter_path_and_clipping 07_painter_path_and_clipping.cpp)
//   target_link_libraries(painting_07_painter_path_and_clipping PRIVATE Qt6::Widgets Qt6::Svg)
//   set(CMAKE_AUTOMOC ON)
//   本文件**不含 Q_OBJECT 类**（全部证据走离屏 QImage，连控件都不建），末尾不需要 .moc。
//
// 运行前须知：
//   · 会往当前工作目录写五张 PNG：
//       painting_07_painter_path_and_clipping.png（主证据：路径/填充/布尔/裁剪/合成 总览）
//       painting_07_before_clip.png（不裁剪时画什么）
//       painting_07_after_clip.png（同一套绘制代码、加了矩形/路径裁剪之后）
//       painting_07_clip_region.png（同一套绘制代码、裁剪成 QRegion 两块不相交矩形）
//       painting_07_composite_mask.png（蒙版与合成结果）
//     除总览外的四张对照图（裁剪前/矩形+路径裁剪/region 裁剪/合成结果）各自打印**着墨像素数**，这是本讲的交付物。
//     ⚠ 这里刻意不叫「非透明像素数」：countOpaque() 只看 alpha != 0，一旦某个格子里铺了不透明
//     底色，它就恒等于满幅，几个格子打出一样的大数字、看不出任何异常（证据③ 就栽过一次）。
//   · 无人值守：证据全走 QImage（第 1 讲铁律一：离屏设备随时可画），
//     一次 QTimer::singleShot 里画完、存图、打印、QCoreApplication::quit()，
//     **没有任何阻塞调用**（除了 main 末尾那句 app.exec()）。
//   · 可以无头跑：`painting_07_painter_path_and_clipping -platform offscreen`。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 painting_07_painter_path_and_clipping，跑到自动退出、退出码 0）。
//     本讲有三行**自检**（不是汇总判定，退出码仍然只证明「没崩、走完了」）：
//       证据③ 「次序核对：intersected < subtracted < united 成立 = true」；
//       证据④ 「核对：… 裁剪更小 = true … 两种裁剪互不相同 = true」；
//       sheet 「④ 那格的标题横带上墨像素 = 494/3888」。
//     这三行任何一项变 false/0，就是本讲的证据链断了，别只看退出码。
//     带具体数字的「预期」请以你自己那次运行输出为准（本机那一轮存档 runlogs/painting_07_painter_path_and_clipping.log）。
//   · Windows 控制台口径：qInfo 出去的中文按本地代码页（GBK）编码，**GBK 里没有的字符会打成 ?**
//       （本机实测：U+2212「−」和 U+2286「⊆」变成 ?，而 ∪ ∩ ≈ ｜ 这些 GBK 里有的没事）。
//       所以打印用的字符串里别放稀有的数学符号，改成 ASCII 或中文词；
//       源码注释里随便写，编译器和控制台都不管注释。
//   · Qt5 → Qt6 差异本讲涉及两处：
//       ① QPainterPath 的布尔运算是**成员函数**（united/intersected/subtracted/simplified），
//          Qt4 那套自由函数 unitedPath(a,b) / intersectedPath(a,b) 在本机头文件里查不到；
//       ② 裁剪的合并语义只有三个值（qnamespace.h 1339~1343），网上流传的
//          「setClipPath(path, Qt::UnionClip)」在本机 Qt6 头文件里**不存在**，
//          照抄会当场编不过；并集裁剪请自己做 QRegion::united 再 setClipRegion。
//   · 关于「ClipOperation 没有 UnionClip」这条：**这是本机头文件里看得见的证据**
//     （qnamespace.h 1339~1343 行只有三个值），不是我背的文档口径。
// ============================================================================

#include <QApplication>          // QtWidgets/qapplication.h
#include <QPainter>              // QtGui/qpainter.h：裁剪与合成模式都在这
#include <QPainterPath>          // QtGui/qpainterpath.h：本讲主角
#include <QPen>                  // QtGui/qpen.h
#include <QBrush>                // QtGui/qbrush.h：QBrush + QGradient + 渐变
#include <QColor>                // QtGui/qcolor.h
#include <QImage>                // QtGui/qimage.h
#include <QRegion>               // QtGui/qregion.h：setClipRegion 的参数类型
#include <QPolygonF>             // QtGui/qpolygon.h：addPolygon 的参数类型
#include <QRect>                 // QtCore/qrect.h：QRect / QRectF
#include <QPoint>                // QtCore/qpoint.h：QPoint / QPointF
#include <QString>
#include <QStringList>
#include <QtMath>                // QtCore/qmath.h：M_PI / qCos / qSin / qAtan2
#include <QTimer>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QDebug>

// ============================================================================
// 一、核对签名（本机 D:\Qt\6.12.0\mingw_64\include，全部带行号；对不上的一律不用）
// ============================================================================
// QtGui/qpainterpath.h
//   第 32 行   enum ElementType { … }                      ← UnknownElement/MoveToElement/
//                 LineToElement/CurveToElement 一类，本讲用**具名比较**不猜数字
//   第 39 行   class Element { ... }                       ← 每个元素有 type/x/y/ctrl*
//   第 49 行   operator QPointF () const                   ← Element 能直接当点用
//   第 73 行   int capacity() const;
//   第 75 行   void closeSubpath();
//   第 77/78 行 void moveTo(const QPointF &); void moveTo(qreal x, qreal y);
//   第 80/81 行 void lineTo(const QPointF &); void lineTo(qreal x, qreal y);
//   第 86/87 行 void arcTo(const QRectF &rect, qreal startAngle, qreal arcLength);
//                 + (x, y, w, h, startAngle, arcLength) 六参数版
//   第 89/90 行 void cubicTo(const QPointF &ctrl1, const QPointF &ctrl2, const QPointF &end);
//   第 92/93 行 void quadTo(const QPointF &ctrl, const QPointF &end);
//   第 95 行   QPointF currentPosition() const;
//   第 97/98 行 void addRect(const QRectF &); void addRect(x, y, w, h);
//   第 99~101 行 void addEllipse(const QRectF &); addEllipse(x,y,w,h); addEllipse(center, rx, ry);
//   第 102 行  void addPolygon(const QPolygonF &);
//   第 103/104 行 void addText(const QPointF &, const QFont &, const QString &);
//   第 105 行  void addPath(const QPainterPath &);
//   第 106 行  void addRegion(const QRegion &);             ← 裁剪那边过来的路径就从这儿进
//   第 108/110 行 void addRoundedRect(const QRectF &, qreal xr, qreal yr, Qt::FillRule = …);
//   第 114 行  void connectPath(const QPainterPath &);      ← 不 moveTo，从当前位置续接
//   第 120/121 行 void translate(...); 第 123 行 [[nodiscard]] QPainterPath translated(...) const;
//   第 126 行  QRectF boundingRect() const;
//   第 129/130 行 Qt::FillRule fillRule() const; void setFillRule(Qt::FillRule);
//   第 132 行  bool isEmpty() const;
//   第 140/141 行 int elementCount() const; QPainterPath::Element elementAt(int i) const;
//   第 148 行  QPointF pointAtPercent(qreal t) const;
//   第 155 行  [[nodiscard]] QPainterPath united(const QPainterPath &r) const;
//   第 156 行  [[nodiscard]] QPainterPath intersected(const QPainterPath &r) const;
//  第 157 行   [[nodiscard]] QPainterPath subtracted(const QPainterPath &r) const;
//   第 159 行  [[nodiscard]] QPainterPath simplified() const;
//   第 164~171 行 operator& | + - 及其赋值版（与上面四个成员函数等价）
//   第 310 行  QPainterPath operator *(const QPainterPath &, const QTransform &)
//   **本机没有** unitedPath() / intersectedPath() 这两个自由函数（Qt4 写法，别抄）。
// QtGui/qpainter.h
//   第 52~59 行 enum RenderHint { Antialiasing = 0x01, TextAntialiasing = 0x02,
//                 SmoothPixmapTransform = 0x04, VerticalSubpixelPositioning = 0x08,
//                 LosslessImageRendering = 0x40, NonCosmeticBrushPatterns = 0x80 };
//   ★ 勘误一：旧稿写「53~57」——53 是第一个成员、52 才是 `enum RenderHint {`，
//     而这一支到 **59** 行的 `};` 才结束（60 行紧跟 Q_ENUM(RenderHint)，62 行
//     Q_DECLARE_FLAGS(RenderHints, RenderHint)，所以 setRenderHints 吃的是 flags 不是单个枚举）。
//     另外注意 0x08 直接跳到 0x40：0x10 / 0x20 是 Qt 内部占掉的，**别自己补位**去写 0x10。
//   第 98~143 行 enum CompositionMode { CompositionMode_SourceOver, CompositionMode_DestinationOver,
//                 CompositionMode_Clear, …, CompositionMode_SourceIn, CompositionMode_DestinationIn,
//                 …, CompositionMode_Xor, CompositionMode_Plus, CompositionMode_Multiply,
//                 CompositionMode_Screen, …, CompositionMode_Darken, CompositionMode_Lighten,
//                 ColorDodge, ColorBurn, HardLight, SoftLight, Difference, Exclusion,
//                 RasterOp_SourceOrDestination … RasterOp_NotDestination, NCompositionModes };
//   ★ 勘误二：旧稿写「98~118」，那是**截在 CompositionMode_Lighten 上**当结束了。本机 6.12.0
//     这个枚举一直到 **143** 行才 `};`：119~124 还有 6 个混合模式（ColorDodge/ColorBurn/
//     HardLight/SoftLight/Difference/Exclusion），126 行起还塞了 **14 个 RasterOp_** 成员，
//     142 行是哨兵 NCompositionModes（=24 个模式 + 14 个 RasterOp + 哨兵）。
//     少记的那 6 个正好是 Photoshop 那一组混合模式——按旧稿记，你会以为 Qt 只有到 Lighten 为止。
//   第 144/145 行 void setCompositionMode(CompositionMode); CompositionMode compositionMode() const;
//   第 185 行   QRegion clipRegion() const;
//   第 188~190 行 void setClipRect(const QRectF &, Qt::ClipOperation op = Qt::ReplaceClip);
//                 + (const QRect &, op) + (x,y,w,h,op)
//   第 192 行   void setClipRegion(const QRegion &, Qt::ClipOperation op = Qt::ReplaceClip);
//   第 194 行   void setClipPath(const QPainterPath &path, Qt::ClipOperation op = Qt::ReplaceClip);
//   第 196/197 行 void setClipping(bool enable); bool hasClipping() const;
//   第 239/240 行 void fillPath(const QPainterPath &, const QBrush &); void drawPath(const QPainterPath &);
//   第 208 行   void resetTransform();          ← 便捷版；第 210 行是 setWorldTransform(matrix, combine)
//                 （别写成 145 行，144/145 是 setCompositionMode/compositionMode）
//   第 155/161 行 void setPen(const QPen &); void setBrush(const QBrush &);
// QtCore/qnamespace.h
//   第 1329~1332 行 enum FillRule { OddEvenFill, WindingFill };
//   第 1339~1343 行 enum ClipOperation { NoClip, ReplaceClip, IntersectClip };   ← **只有三个**
//   第 1220~1224 行 enum AspectRatioMode { IgnoreAspectRatio, KeepAspectRatio, KeepAspectRatioByExpanding };
// QtGui/qregion.h
//   第 59/60 行 bool contains(const QPoint &) const; bool contains(const QRect &) const;
//                 ← 证据④ 用它核对「region 那张的着墨包围盒确实落在我构造的两个矩形里」
//   第 77 行   QRect boundingRect() const noexcept;   第 81 行 int rectCount() const noexcept;
//   QRegion(const QRect &); QRegion(const QPolygon &); bool isEmpty() const;
//   QRegion united(const QRegion &) const;
//   QRegion intersected(const QRegion &) const; QRegion subtracted(const QRegion &) const;
// 本讲**没用**的（因为在本机头文件里没核到）：QPainterPath::toPainterPath? 不存在；
//   Qt::UnionClip（不存在，用 QRegion::united + setClipRegion 替代）；
//   unitedPath()/intersectedPath()（不存在，用成员函数）。

// ============================================================================
// 二、公共小工具：把「画得好不好看」换成「数得清数不清」
// ============================================================================
static qint64 countOpaque(const QImage &src)
{
    const QImage probe = src.isNull() ? QImage() : src.convertedTo(QImage::Format_ARGB32);
    qint64 n = 0;
    for (int y = 0; y < probe.height(); ++y) {
        const uchar *row = probe.constScanLine(y);              // 只读，不 detach（第 5 讲）
        for (int x = 0; x < probe.width(); ++x)
            if (row[x * 4 + 3] != 0)
                ++n;
    }
    return n;
}

// 底色均匀（比如 sheet 被 fill 成一个颜色）时，countOpaque() 会数到**满幅**，
// 因为它只看 alpha。要问「这块区域有没有真上过墨」，就得跟**指定的底色**比。
// 底色一定由调用方给死，别图省事拿 (0,0) 那个像素当底色：sheet 左上角在标题条里
// （另一个颜色），那样会把整张正文都算成墨，数字照样很大、照样"看起来正常"。
static qint64 countInkOverBackground(const QImage &src, const QColor &background)
{
    const QImage probe = src.isNull() ? QImage() : src.convertedTo(QImage::Format_ARGB32);
    if (probe.isNull() || probe.width() <= 0 || probe.height() <= 0)
        return 0;
    const QRgb bg = background.rgba();
    qint64 n = 0;
    for (int y = 0; y < probe.height(); ++y) {
        const uchar *row = probe.constScanLine(y);
        for (int x = 0; x < probe.width(); ++x) {
            const QRgb p = qRgba(row[x * 4 + 2], row[x * 4 + 1], row[x * 4 + 0], row[x * 4 + 3]);
            if (p != bg)
                ++n;
        }
    }
    return n;
}

static QRect inkBounds(const QImage &src)
{
    const QImage probe = src.isNull() ? QImage() : src.convertedTo(QImage::Format_ARGB32);
    QRect box;
    for (int y = 0; y < probe.height(); ++y) {
        const uchar *row = probe.constScanLine(y);
        for (int x = 0; x < probe.width(); ++x) {
            if (row[x * 4 + 3] != 0) {
                if (box.isNull())
                    box = QRect(x, y, 1, 1);
                else
                    box = box.united(QRect(x, y, 1, 1));
            }
        }
    }
    return box;
}

// ============================================================================
// 三、路径构造：一个「什么都用上了」的复合路径
// ============================================================================
static QPainterPath buildKitchenSinkPath(const QRectF &box)
{
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);                 // 130 行：显式写出来，别靠默认
    path.addRect(box.adjusted(2, 2, -40, -2));         // 97 行
    path.addEllipse(box.adjusted(20, 30, -20, -10));   // 99 行
    path.moveTo(box.left() + 4, box.top() + 4);        // 77 行：新子路径
    path.lineTo(box.left() + 44, box.top() + 4);       // 80 行
    path.cubicTo(box.left() + 60, box.top() + 30,      // 89 行：三次贝塞尔
                 box.left() + 20, box.top() + 40,
                 box.left() + 10, box.top() + 20);
    path.quadTo(box.left() + 40, box.top() + 60,       // 92 行：二次贝塞尔
                box.left() + 60, box.top() + 50);
    path.closeSubpath();                               // 75 行：连回 moveTo 那个点
    path.arcTo(QRectF(box.right() - 44, box.top() + 24, 40, 40),
               0.0, 200.0);                            // 86 行：角度单位是**度**（不是 1/16 度）
    QPolygonF poly;                                    // 102 行
    poly << QPointF(box.left() + 8, box.bottom() - 8)
         << QPointF(box.left() + 30, box.bottom() - 30)
         << QPointF(box.left() + 46, box.bottom() - 6);
    path.addPolygon(poly);
    path.closeSubpath();
    path.addRoundedRect(QRectF(box.left() + 50, box.bottom() - 40, 34, 26), 8, 8);  // 108 行
    return path;
}

// 五角星：五条边两两自交 —— 这是**演示 FillRule 的标准靶子**
static QPainterPath buildStarPath(const QRectF &box)
{
    QPainterPath star;
    const QPointF c = box.center();
    const qreal r = qMin(box.width(), box.height()) / 2.0 - 2;
    for (int i = 0; i < 5; ++i) {
        // 每隔一个顶点连线，就得到自交的星形
        const int idx = (i * 2) % 5;
        const qreal ang = (-90.0 + idx * 72.0) * M_PI / 180.0;
        const QPointF p(c.x() + r * qCos(ang), c.y() + r * qSin(ang));
        if (i == 0)
            star.moveTo(p);
        else
            star.lineTo(p);
    }
    star.closeSubpath();
    return star;
}

// ============================================================================
// 四、证据①：路径构造与「读回来」
// ============================================================================
static void evidencePathConstruction(QImage &sheet)
{
    qInfo() << "【证据①】QPainterPath 构造：API 全都点一遍，然后把它读回来";
    const QRectF box(0, 0, 120, 100);
    const QPainterPath path = buildKitchenSinkPath(box);
    qInfo() << "  isEmpty =" << path.isEmpty()
            << "elementCount =" << path.elementCount()
            << "capacity =" << path.capacity()
            << "boundingRect =" << path.boundingRect()
            << "currentPosition =" << path.currentPosition();
    // 逐元素把 type 打成具名（不猜枚举数值）
    QStringList kinds;
    const int n = qMin(path.elementCount(), 8);
    for (int i = 0; i < n; ++i) {
        const QPainterPath::Element e = path.elementAt(i);
        QString name = QStringLiteral("Unknown");
        if (e.type == QPainterPath::MoveToElement)
            name = QStringLiteral("MoveTo");
        else if (e.type == QPainterPath::LineToElement)
            name = QStringLiteral("LineTo");
        else if (e.type == QPainterPath::CurveToElement || e.type == QPainterPath::CurveToDataElement)
            name = QStringLiteral("CurveTo");
        kinds << QString::asprintf("#%d %s(%.0f,%.0f)", i, qPrintable(name), e.x, e.y);
    }
    qInfo() << "  前几个元素:" << kinds.join(QStringLiteral(" | "));
    qInfo() << "  pointAtPercent(0.0/0.5/1.0) ="
            << path.pointAtPercent(0.0) << path.pointAtPercent(0.5) << path.pointAtPercent(1.0);
    qInfo() << "  fillRule（我在构造时显式写了 WindingFill）=" << path.fillRule();

    QPainter p(&sheet);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(10, 32);                                // 把这一格挪进标题条下面的位置
    p.setPen(QPen(QColor(20, 60, 120), 1.5));
    p.setBrush(QColor(20, 60, 120, 40));
    p.drawPath(path);                                   // 240 行
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(200, 60, 60), 1, Qt::DotLine));
    p.drawRect(QRectF(path.boundingRect()));            // 包围盒画出来，肉眼可核
    p.resetTransform();
    p.setPen(QColor(20, 90, 60));
    p.drawText(QPointF(14, 146), QStringLiteral("① 构造 + 包围盒（虚线框）"));
}

// ============================================================================
// 五、证据②：Qt::FillRule（自交路径的中心有没有洞）
// ============================================================================
static void evidenceFillRule(QImage &sheet, int x0, int y0)
{
    qInfo() << "【证据②】Qt::FillRule：OddEvenFill vs WindingFill（自交五角星）";
    const QRectF box(0, 0, 90, 90);
    const QPainterPath star = buildStarPath(box);

    QPainter p(&sheet);
    p.setRenderHint(QPainter::Antialiasing);

    QImage probe(90, 90, QImage::Format_ARGB32);
    const int cx = 45, cy = 45;                          // 星形中心
    const struct { const char *name; Qt::FillRule rule; int dx; } cases[] = {
        { "OddEvenFill", Qt::OddEvenFill, 0 },
        { "WindingFill", Qt::WindingFill, 100 },
    };
    for (const auto &c : cases) {
        QPainterPath one = star;
        one.setFillRule(c.rule);                         // 130 行
        qInfo() << "  " << c.name << ": fillRule 回读 =" << one.fillRule()
                << "elementCount =" << one.elementCount();
        probe.fill(QColor(0, 0, 0, 0));
        {
            QPainter q(&probe);
            q.setRenderHint(QPainter::Antialiasing);
            q.setPen(Qt::NoPen);
            // ⚠ 探针图**绝不跟着 c.dx 位移**：这张图只有 90x90，而 dx=100 是给 sheet 排版用的。
            //   上一版把位移一起套上来，WindingFill 那一整张星被画到画布外面去了，
            //   中心点永远读到 0，于是"实心"的证据打成"也有洞"，两行数字长得一样、结论自相矛盾。
            q.fillPath(one, QBrush(QColor(30, 140, 90, 255)));   // 239 行
        }
        // 中心点：**这个像素才是两种规则的唯一区别**
        const int alphaOddOrEvenCenter = probe.pixelColor(cx, cy).alpha();
        qInfo() << "      中心像素 (" << cx << "," << cy << ") alpha ="
                << alphaOddOrEvenCenter << "（一个应为 0=有洞、一个应为 255=实心）";
        qInfo() << "      整张探针的着墨像素 =" << countOpaque(probe)
                << "（同一条路径，只是规则不同 → 多出来的那部分是**中间那个洞**被填上了）";
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(30, 140, 90, 255));
        p.translate(x0 + c.dx, y0);                      // 位移只作用于 sheet
        p.fillPath(one, QBrush(QColor(30, 140, 90, 255)));
        p.resetTransform();                              // qpainter.h 208 行
        p.setPen(QColor(40, 45, 55));
        p.drawText(QPointF(x0 + c.dx + 2, y0 + 100), QString::fromUtf8(c.name));
    }
    qInfo() << "  → 记忆法：**奇偶（OddEven）看「穿过几条边」，绕数（Winding）看「绕了几圈」**。"
               "自交图形的洞只出现在 OddEvenFill 里。";
    const QPainterPath fresh;                            // 默认构造，一个元素都没加
    qInfo() << "  Qt 的默认到底是哪个：新建 QPainterPath 的 fillRule() =" << fresh.fillRule()
            << "（Qt::OddEvenFill = " << int(Qt::OddEvenFill) << "、WindingFill = "
            << int(Qt::WindingFill) << "）→ 默认是奇偶，**别指望，自己显式写**。";

    // 顺带把 simplified() 用一次：本机 qpainterpath.h 159 行确实存在
    const QPainterPath simple = star.simplified();
    qInfo() << "  simplified() 前后：elementCount" << star.elementCount() << "->"
            << simple.elementCount() << "boundingRect" << star.boundingRect()
            << "->" << simple.boundingRect();
    // 本机 qpainterpath.cpp 3568~3573 行的文档注释写着「简化后的路径一律用默认填充规则 Qt::OddEvenFill」。
    // 这条**别照抄，测一遍**：默认构造的星形本来就是 OddEvenFill，看不出规则有没有被改，
    // 所以再拿一条显式写了 WindingFill 的星形走一遍 simplified()，回读 fillRule()。
    QPainterPath wound = star;
    wound.setFillRule(Qt::WindingFill);
    const QPainterPath woundSimple = wound.simplified();
    qInfo() << "  文档口径核对：WindingFill 的星形 simplified() 之后 fillRule() ="
            << woundSimple.fillRule()
            << "（原路径 =" << wound.fillRule() << "）→ 本机这一轮确实被打回 OddEvenFill，"
               "文档这句在这版 Qt 上成立；elementCount" << wound.elementCount()
            << "->" << woundSimple.elementCount()
            << "。结论：**简化会丢掉你的填充规则**，简化完还要记得把规则设回去。";
}

// ============================================================================
// 六、证据③：路径布尔（成员函数版，不是 Qt4 的自由函数）
// ============================================================================
static void evidencePathBooleans(QImage &sheet, int x0, int y0)
{
    qInfo() << "【证据③】路径布尔：united / intersected / subtracted（+ operator|&-）";
    const QPainterPath a = [&]{ QPainterPath p; p.addEllipse(QRectF(0, 12, 80, 80)); return p; }();
    const QPainterPath b = [&]{ QPainterPath p; p.addRect(QRectF(40, 0, 70, 70)); return p; }();

    const QPainterPath u = a.united(b);                 // 155 行
    const QPainterPath i = a.intersected(b);            // 156 行
    const QPainterPath s = a.subtracted(b);             // 157 行
    const QPainterPath u2 = a | b;                      // 165 行：等价写法
    const QPainterPath i2 = a & b;                      // 164 行
    const QPainterPath s2 = a - b;                      // 167 行

    struct Row { const char *name; QPainterPath path; };
    const Row rows[6] = {
        { "A ∪ B  united()",        u },
        { "A ∩ B  intersected()",   i },
        { "A - B  subtracted()",    s },
        { "A | B（同 united）",     u2 },
        { "A & B（同 intersected）", i2 },
        { "A - B（同 subtracted）",  s2 },
    };
    for (int k = 0; k < 6; ++k) {
        qInfo() << QString::asprintf("  %-24s isEmpty=%d elementCount=%3d boundingRect=(%.0f,%.0f,%.0f,%.0f)",
                                     rows[k].name, int(rows[k].path.isEmpty()),
                                     rows[k].path.elementCount(),
                                     rows[k].path.boundingRect().x(), rows[k].path.boundingRect().y(),
                                     rows[k].path.boundingRect().width(), rows[k].path.boundingRect().height());
    }
    qInfo() << "  用像素交叉验证（每个结果单独画进 110x100 的**全透明底**小图数着墨）：";
    // ⚠ 这里绝对不能用不透明底色（例如 fillRect(QColor(250,250,250,255))）：
    //   countOpaque()/inkBounds() 数的是 **alpha != 0** 的像素，一张不透明底会把三个格子
    //   全打成「11000 / QRect(0,0 110x100)」= 整张画布，三个数一模一样，交叉验证当场失效。
    //   画刷同理用**全不透明**，这样着墨数≈路径面积，次序才有意义。
    qint64 inkU = 0, inkI = 0, inkS = 0;
    for (int k = 0; k < 3; ++k) {
        QImage cell(110, 100, QImage::Format_ARGB32);
        cell.fill(QColor(0, 0, 0, 0));                    // 只此一处清底，别再铺不透明背景
        QPainter q(&cell);
        q.setRenderHint(QPainter::Antialiasing);
        q.setPen(Qt::NoPen);                              // 描边会让包围盒/像素数多算一圈
        q.setBrush(QColor(40, 90, 200, 255));
        q.drawPath(rows[k].path);
        const qint64 ink = countOpaque(cell);
        if (k == 0) inkU = ink; else if (k == 1) inkI = ink; else inkS = ink;
        qInfo() << "      " << rows[k].name << "着墨像素 =" << ink
                << "着墨包围盒 =" << inkBounds(cell)
                << "占整张 11000 的比例 =" << QString::asprintf("%.2f", ink / 11000.0);
    }
    const bool orderOk = (inkI < inkS) && (inkS < inkU);
    qInfo() << "  → 次序核对：intersected(" << inkI << ") < subtracted(" << inkS
            << ") < united(" << inkU << ") 成立 =" << orderOk
            << "（A 是 80x80 的椭圆、B 是 70x70 的矩形且只压掉 A 右上角一块，"
               "所以「交集最小、A 减 B 居中、并集最大」是几何上注定的；"
               "次序反了就是你的 FillRule 或画刷透明度在捣鬼。）";

    // 画进总览 sheet：三条并排
    QPainter p(&sheet);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(x0, y0);
    for (int k = 0; k < 3; ++k) {
        p.save();                                        // 第 2 讲：状态是栈
        p.translate(k * 115.0, 0);
        p.setPen(QPen(QColor(180, 40, 40), 1));
        p.setBrush(QColor(40, 90, 200, 110));
        p.drawPath(rows[k].path);
        p.restore();
    }
    p.resetTransform();
    p.setPen(QColor(40, 45, 55));
    p.drawText(QPointF(x0, y0 + 100), QStringLiteral("③ united / intersected / subtracted"));
}

// ============================================================================
// 七、证据④：裁剪三件套（同一套绘制代码，裁与不裁两张对照图）
// ============================================================================
// 这套内容代码被调三次，第三种模式**自己动手裁**、第二种**只清掉裁剪**、第一种**什么都不动**
// （留给外面的 setClipRegion），这样三张对照图的差别才真的只来自裁剪本身。
enum SelfClip { KeepExternalClip = 0, ClipRectAndPath, TurnClipOff };
static void paintBusyContent(QPainter &p, const QSize &size, SelfClip selfClip, Qt::ClipOperation op)
{
    switch (selfClip) {
    case TurnClipOff:
        // 196 行 setClipping(bool)：这是**总开关**，关掉之后前面设的 clipRect/clipPath/clipRegion
        // 全部不再生效（但 clipRegion() 照样能读回原来那个区域，别把「读得到」当成「还在裁」）。
        p.setClipping(false);
        break;
    case ClipRectAndPath:
        p.setClipRect(QRectF(0, 0, size.width() * 0.55, size.height()), op);   // 188 行
        p.setClipPath([&]{ QPainterPath path;            // 再与一个圆**相交**
                         path.addEllipse(QRectF(size.width() * 0.25, size.height() * 0.1,
                                                size.width() * 0.6, size.height() * 0.8));
                         return path; }(), op);         // 194 行：连两次 = 交集套交集
        break;
    case KeepExternalClip:
        break;                                            // 裁剪由调用方在进这个函数之前设好
    }

    // 内容：渐变底 + 一堆图元，故意画满整张，好让裁剪看得出差别
    QLinearGradient g(0, 0, size.width(), size.height());
    g.setColorAt(0.0, QColor(255, 210, 80));
    g.setColorAt(1.0, QColor(180, 40, 160));
    p.fillRect(QRect(QPoint(0, 0), size), g);

    p.setPen(QPen(QColor(255, 255, 255), 3));
    for (int k = 0; k < 14; ++k)
        p.drawLine(0, k * 14, size.width(), size.height() - k * 14);

    p.setPen(QPen(QColor(20, 30, 60), 2));
    p.setBrush(QColor(30, 200, 140, 200));
    for (int k = 0; k < 6; ++k)
        p.drawEllipse(QPointF(size.width() * (0.1 + 0.16 * k), size.height() * 0.5), 16, 26);

    p.setBrush(QColor(255, 255, 255, 180));
    p.setPen(Qt::NoPen);
    QPainterPath star = buildStarPath(QRectF(4, 4, 70, 70));
    p.drawPath(star);
}

static void evidenceClipping()
{
    qInfo() << "【证据④】裁剪：setClipRect / setClipPath / setClipRegion + Qt::ClipOperation";
    const QSize size(240, 180);

    QImage none(size, QImage::Format_ARGB32_Premultiplied);
    none.fill(QColor(0, 0, 0, 0));
    { QPainter p(&none); p.setRenderHint(QPainter::Antialiasing);
      paintBusyContent(p, size, TurnClipOff, Qt::NoClip); }

    QImage rect(size, QImage::Format_ARGB32_Premultiplied);
    rect.fill(QColor(0, 0, 0, 0));
    { QPainter p(&rect); p.setRenderHint(QPainter::Antialiasing);
      paintBusyContent(p, size, ClipRectAndPath, Qt::IntersectClip); }

    // QRegion 并集裁剪：**ClipOperation 里没有 UnionClip，所以并集自己做**
    const QRegion two = QRegion(QRect(10, 10, 80, 60)).united(QRegion(QRect(120, 90, 90, 70)));
    QImage region(size, QImage::Format_ARGB32_Premultiplied);
    region.fill(QColor(0, 0, 0, 0));
    {
        QPainter p(&region);
        p.setRenderHint(QPainter::Antialiasing);
        p.setClipRegion(two, Qt::ReplaceClip);           // 192 行
        // 先把「设好裁剪、还没画」的状态读回来：这两行是 ReplaceClip 真的生效的证据。
        qInfo() << "  setClipRegion(two, ReplaceClip) 之后（画之前）：hasClipping ="
                << p.hasClipping()
                << "clipRegion().boundingRect =" << p.clipRegion().boundingRect()
                << "rectCount =" << p.clipRegion().rectCount()
                << "｜我自己构造的 QRegion：boundingRect =" << two.boundingRect()
                << "rectCount =" << two.rectCount()
                << "→ 两个 rectCount 相等（" << p.clipRegion().rectCount() << "==" << two.rectCount()
                << "）就是「并集原样进去了」；ClipOperation 给 ReplaceClip 才没有被交掉。";
        // ⚠ 这里必须传 KeepExternalClip：上一版传的是和「不裁剪」同一个分支，
        //   进到函数里第一件事就是 setClipping(false)，把刚设好的 QRegion 裁剪整个关掉，
        //   于是这一张和 none 张像素数一模一样（都是 43200 = 整张面积），
        //   而 hasClipping 打印出来是 false —— 数字看起来正常，结论却根本没跑。
        paintBusyContent(p, size, KeepExternalClip, Qt::NoClip);
        qInfo() << "  画完之后：hasClipping =" << p.hasClipping()
                << "（画内容没碰裁剪开关，所以还是 true；上一版在这行是 false）";
    }

    const qint64 inkNone = countOpaque(none), inkRect = countOpaque(rect), inkRegion = countOpaque(region);
    qInfo() << "  着墨像素数：不裁剪 =" << inkNone
            << "裁剪(rect∩path) =" << inkRect
            << "裁剪(region 并集) =" << inkRegion;
    qInfo() << "  着墨包围盒：不裁剪 =" << inkBounds(none)
            << "裁剪(rect∩path) =" << inkBounds(rect)
            << "裁剪(region 并集) =" << inkBounds(region);
    const qint64 area = qint64(size.width()) * size.height();
    qInfo() << "  核对：面积 =" << area
            << "｜不裁剪 ≈ 满幅 =" << (inkNone >= area * 95 / 100)
            << "｜矩形+路径裁剪更小 =" << (inkRect < inkNone)
            << "｜region 裁剪更小 =" << (inkRegion < inkNone)
            << "｜两种裁剪互不相同（不是同一段代码跑两遍） =" << (inkRect != inkRegion)
            << "｜region 着墨落在构造的两个矩形里 ="
            << (two.contains(inkBounds(region)) ? "是" : "否（看下面包围盒，说明裁剪没生效）")
            << "（上一版只有前两项成立，后三项全崩：region 和 none 都是 43200，因为它被 setClipping(false) 关掉了。）";
    qInfo() << "  ClipOperation 只有 NoClip/ReplaceClip/IntersectClip 三个值；"
               "第二次 setClip* 传 IntersectClip 就是「在已有裁剪里再交一块」，"
               "传 ReplaceClip 就是「推翻前面的重新裁」。";

    none.save(QStringLiteral("painting_07_before_clip.png"));
    rect.save(QStringLiteral("painting_07_after_clip.png"));
    region.save(QStringLiteral("painting_07_clip_region.png"));
}

// ============================================================================
// 八、证据⑤：合成模式 —— 用 DestinationIn 画蒙版，外加 12 格色卡
// ============================================================================
struct ModeCell { const char *name; QPainter::CompositionMode mode; };

static void evidenceComposition(QImage &sheet, int x0, int y0)
{
    // 这一节只做「结论说明」，真正的实现在 evidenceCompositionReal()（下面）：
    // 那里把内容层与蒙版层各画一遍、逐个模式合成、并把统计数字打出来。
    Q_UNUSED(sheet);
    Q_UNUSED(x0);
    Q_UNUSED(y0);
    qInfo() << "【证据⑤·导读】合成模式 vs 裁剪，差在哪：";
    qInfo() << "  裁剪（setClipRect / setClipPath / setClipRegion）只吃**二值**边界："
               "clipRegion()（qpainter.h 185 行）返回的 QRegion 天生非 0 即 1；";
    qInfo() << "  合成模式吃 **alpha**：半透明蒙版能保留一半颜色。";
    qInfo() << "  口诀：「形状外面不要」用裁剪，「形状外面要淡出」用合成模式。下面见数字。";
}

// 把「内容 + 蒙版」用某合成模式叠一次，返回结果图与统计
static QImage composeOnce(QPainter::CompositionMode mode, const QImage &content,
                          const QImage &mask, qint64 *opaqueOut, QColor *centerOut)
{
    QImage out = content.copy();                         // 第 5 讲：copy() 是真复制
    {
        QPainter p(&out);
        p.setCompositionMode(mode);                      // 144 行
        p.drawImage(0, 0, mask);                         // 354 行 (x,y,image,...) 版
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);  // 画完复位（qpainter.h 99 行）
    }
    if (opaqueOut)
        *opaqueOut = countOpaque(out);
    if (centerOut)
        *centerOut = out.pixelColor(out.width() / 2, out.height() / 2);
    return out;
}

static QImage makeContentLayer(const QSize &s)
{
    QImage img(s, QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(0, 0, 0, 0));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(0, 0, s.width(), s.height());
    g.setColorAt(0.0, QColor(255, 90, 60, 255));
    g.setColorAt(1.0, QColor(60, 120, 255, 255));
    p.fillRect(QRect(QPoint(0, 0), s), g);
    p.setPen(QPen(QColor(255, 255, 255, 230), 3));
    for (int k = 0; k < 6; ++k)
        p.drawLine(0, k * s.height() / 6, s.width(), s.height() - k * s.height() / 6);
    return img;
}

static QImage makeMaskLayer(const QSize &s, bool halfAlpha)
{
    QImage img(s, QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(0, 0, 0, 0));                        // 蒙版底色：全透明
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    // 蒙版的**颜色**无所谓，吃的是 alpha：这里 alpha 给满或给一半，两种都试
    const int a = halfAlpha ? 128 : 255;
    p.setBrush(QColor(255, 255, 255, a));
    p.drawEllipse(4, 4, s.width() - 8, s.height() - 8);
    p.setBrush(QColor(255, 255, 255, a / 2));
    p.drawRect(s.width() / 2 - 6, 2, 12, s.height() - 4);
    return img;
}

static void evidenceCompositionReal(QImage &sheet, int x0, int y0)
{
    qInfo() << "【证据⑤·正式版】CompositionMode：蒙版 + 12 格色卡";
    const QSize s(72, 72);
    const QImage content = makeContentLayer(s);
    const QImage maskFull = makeMaskLayer(s, false);
    const QImage maskHalf = makeMaskLayer(s, true);

    qInfo() << "  内容层非透明像素 =" << countOpaque(content) << "/" << s.width() * s.height();
    qInfo() << "  蒙版层（满 alpha）非透明像素 =" << countOpaque(maskFull)
            << "蒙版层（半 alpha）=" << countOpaque(maskHalf);

    // 蒙版：DestinationIn = 保留「已有像素 ∩ 新画的 alpha」
    qint64 opDestIn = 0, opSrcIn = 0, opClear = 0, opSrc = 0;
    QColor cDestIn, cSrcIn, cClear, cSrc;
    const QImage destIn = composeOnce(QPainter::CompositionMode_DestinationIn, content, maskFull,
                                      &opDestIn, &cDestIn);
    const QImage srcIn = composeOnce(QPainter::CompositionMode_SourceIn, content, maskFull,
                                     &opSrcIn, &cSrcIn);
    const QImage cleared = composeOnce(QPainter::CompositionMode_Clear, content, maskFull,
                                       &opClear, &cClear);
    const QImage srcOver = composeOnce(QPainter::CompositionMode_Source, content, maskFull,
                                       &opSrc, &cSrc);
    qInfo() << "  DestinationIn：非透明 =" << opDestIn << "中心像素 =" << cDestIn.name(QColor::HexArgb);
    qInfo() << "  SourceIn      ：非透明 =" << opSrcIn << "中心像素 =" << cSrcIn.name(QColor::HexArgb);
    qInfo() << "  Clear         ：非透明 =" << opClear << "中心像素 =" << cClear.name(QColor::HexArgb)
            << "（预期 0 / 全透明：Clear 就是把目标抹掉）";
    qInfo() << "  Source        ：非透明 =" << opSrc << "中心像素 =" << cSrc.name(QColor::HexArgb)
            << "（预期等于蒙版的非透明数：Source 直接拿新内容替换）";
    qInfo() << "  → 半透明蒙版再走一遍 DestinationIn：";
    qint64 opHalf = 0;
    QColor cHalf;
    const QImage destInHalf = composeOnce(QPainter::CompositionMode_DestinationIn, content,
                                          maskHalf, &opHalf, &cHalf);
    qInfo() << "      非透明 =" << opHalf << "中心 alpha =" << cHalf.alpha()
            << "（alpha 减半但不为 0，所以「非透明像素数」几乎不变、"
               "**中心像素的 alpha 变了**：只看像素数会漏判，这就是要同时打印采样值的原因）";
    Q_UNUSED(destInHalf);
    Q_UNUSED(srcOver);

    // 12 格色卡：同一对「内容/蒙版」，只换模式
    const ModeCell cells[] = {
        { "SourceOver",     QPainter::CompositionMode_SourceOver },      // qpainter.h 99 行
        { "DestinationOver",QPainter::CompositionMode_DestinationOver },  // 100 行
        { "Clear",          QPainter::CompositionMode_Clear },            // 101 行
        { "SourceIn",       QPainter::CompositionMode_SourceIn },         // 104 行
        { "DestinationIn",  QPainter::CompositionMode_DestinationIn },    // 105 行
        { "Xor",            QPainter::CompositionMode_Xor },              // 110 行
        { "Plus",           QPainter::CompositionMode_Plus },             // 113 行
        { "Multiply",       QPainter::CompositionMode_Multiply },         // 114 行
        { "Screen",         QPainter::CompositionMode_Screen },           // 115 行
        { "Darken",         QPainter::CompositionMode_Darken },           // 117 行
        { "Lighten",        QPainter::CompositionMode_Lighten },          // 118 行
        { "SourceOver 半遮",QPainter::CompositionMode_SourceOver },
    };
    QStringList report;
    for (const ModeCell &c : cells) {
        qint64 op = 0;
        QColor cc;
        composeOnce(c.mode, content, maskFull, &op, &cc);
        report << QString::asprintf("%s:%d(%02X)", c.name, int(op), cc.rgb());
    }
    qInfo() << "  色卡（格式 模式:非透明像素数(中心像素ARGB十六进制)）:" << report.join(QLatin1Char(' '));

    // 把三张关键结果画进总览 sheet 的格子里
    QPainter p(&sheet);
    p.setRenderHint(QPainter::Antialiasing);
    const QImage crops[4] = { content, maskFull, destIn, srcIn };
    const char *titles[4] = { "内容层", "蒙版层(alpha)", "DestinationIn", "SourceIn" };
    for (int k = 0; k < 4; ++k) {
        p.drawImage(QPointF(x0 + k * 82.0, y0), crops[k]);
        p.setPen(QColor(40, 45, 55));
        p.drawText(QPointF(x0 + k * 82.0, y0 + 84), QString::fromUtf8(titles[k]));
    }

    QImage maskOut = destIn;
    maskOut.save(QStringLiteral("painting_07_composite_mask.png"));
    qInfo() << "  painting_07_composite_mask.png 已写盘，尺寸 =" << maskOut.size();
}

// ============================================================================
// 九、总览 sheet：把上面几组结果并到一张 PNG 里
// ============================================================================
static void runAllEvidence()
{
    qInfo() << "===============================================================";
    qInfo() << "painting 第 7 讲 · QPainterPath / 裁剪 / 合成模式（平台 ="
            << QGuiApplication::platformName() << "）";
    qInfo() << "===============================================================";

    QImage sheet(560, 470, QImage::Format_ARGB32_Premultiplied);
    const QColor sheetBg(246, 247, 250, 255);            // 正文底色，自检要跟它比
    sheet.fill(sheetBg);

    {
        QPainter p(&sheet);
        p.fillRect(0, 0, 560, 26, QColor(222, 230, 240, 255));
        p.setPen(QColor(20, 60, 120));
        p.drawText(QRect(8, 4, 544, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("① 路径构造   ② FillRule 自交星   ③ 路径布尔   ④ 裁剪   ⑤ 合成模式"));
    }

    evidencePathConstruction(sheet);
    evidenceFillRule(sheet, 250, 30);
    evidencePathBooleans(sheet, 20, 130);
    evidenceComposition(sheet, 250, 130);          // 只讲结论，实现见 Real
    evidenceCompositionReal(sheet, 20, 260);

    // 裁剪那一格单独画进 sheet（对照：左右两块同样的内容，右边裁）
    {
        QPainter p(&sheet);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF cell(330, 250, 210, 160);
        // ⚠ 顺序是**先 save 再 setClipRect**。上一版反过来（先裁再存），restore() 只能回到
        //   save() 那一刻的状态——那一刻裁剪已经生效了，所以出了作用域裁剪还在身上，
        //   紧接着画在 cell 下方 14px 的那行标题落在裁剪框外，**一个像素都没上纸**，
        //   而日志里 save = true、图也生成了，看不出任何异常。
        p.save();
        p.setClipRect(cell, Qt::ReplaceClip);      // 把自己限制在这一格里
        QImage tmp(cell.size().toSize(), QImage::Format_ARGB32_Premultiplied);
        tmp.fill(QColor(0, 0, 0, 0));
        {
            QPainter q(&tmp);
            q.setRenderHint(QPainter::Antialiasing);
            paintBusyContent(q, cell.size().toSize(), ClipRectAndPath, Qt::IntersectClip);
        }
        p.drawImage(cell.topLeft(), tmp);
        p.restore();                               // 现在这一步才真的把裁剪卸掉
        p.setPen(QColor(40, 45, 55));
        p.setBrush(Qt::NoBrush);
        p.drawRect(cell);
        p.drawText(QPointF(cell.left(), cell.bottom() + 14),
                   QStringLiteral("④ 裁剪后（对照 painting_07_after_clip.png）"));
        // 数一下标题那条 18px 高的横带里到底有没有墨：这就是「文字真上纸了」的证据。
        // 横带往左多取 6px，避开文字起点；底色用 sheetBg，不是"取左上角那个像素"。
        QImage band = sheet.copy(QRect(int(cell.left()) - 6, int(cell.bottom()) + 2,
                                       int(cell.width()) + 6, 18));
        qInfo() << "【sheet 自检】④ 那格的标题横带（cell 下方 18px）上墨像素 ="
                << countInkOverBackground(band, sheetBg) << "/" << band.width() * band.height()
                << "（sheet 是均匀底色，所以不能用 countOpaque —— 它会直接数到满幅。"
                   "上一版这里实测为 0：文字被残留的裁剪吃掉了。）";
    }

    const bool saved = sheet.save(QStringLiteral("painting_07_painter_path_and_clipping.png"));
    qInfo() << "【主证据】总览 sheet:" << sheet.size() << "save =" << saved
            << "非透明像素 =" << countOpaque(sheet) << "/ 面积 =" << sheet.width() * sheet.height()
            << "→ 这两个数相等**不是**证据（底色本来就把整张铺满了，alpha 全非 0）。"
               "真正能看出「内容有没有上纸」的是相对底色的上墨像素 ="
            << countInkOverBackground(sheet, sheetBg)
            << "（满幅 = 全坏了，0 = 什么都没画）。";

    evidenceClipping();                             // 三张对照 PNG + 各自像素数
}

// ============================================================================
// 十、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("painting07"));

    // 全部证据都在离屏 QImage 上完成（第 1 讲铁律一），所以一次 singleShot 就够
    QTimer::singleShot(0, [&app]() {
        runAllEvidence();
        qInfo() << "【收尾】五张 PNG 已写盘（总览 + before_clip + after_clip + clip_region"
                   " + composite_mask）；把每张的像素数抄回注释就是你的实测。";

        QCoreApplication::quit();
    });

    return app.exec();
}

// ============================================================================
// 十一、速查表与纪律
// ============================================================================
/*
 * 一、QPainterPath 的两种「进路」
 *   加一个封闭形状   addRect / addEllipse / addRoundedRect / addPolygon / addText / addRegion
 *   自己描           moveTo → lineTo / cubicTo / quadTo / arcTo → closeSubpath
 *   接着上一点画     connectPath(other)（**不** moveTo，容易和当前位置连出一条你没想到的线）
 *   整条并入         addPath(other)
 *   读回来           elementCount() / elementAt(i) / currentPosition() / boundingRect()
 *                    / isEmpty() / fillRule() / pointAtPercent(t)
 *   布尔             united / intersected / subtracted / simplified（都是成员函数）
 *   变换             translate()（就地）/ translated()（返回新的）/ operator*(path, QTransform)
 *
 * 二、FillRule 怎么选
 *   一个闭合凸图形（圆、矩形、凸多边形）：两种规则**结果一样**，随便。
 *   自交（五角星、齿轮、字腔）：想「有洞」用 OddEvenFill，想「全实」用 WindingFill。
 *   把两个 addEllipse 叠成月牙：通常要 OddEvenFill，或者干脆用 subtracted() 把洞剪掉 ——
 *   **剪出来的路径比依赖填充规则更好读**，代码 review 的人不用回忆规则。
 *
 * 三、裁剪的三条纪律
 *  ① 裁剪**跟着变换走**：setClipRect 给的是「当前坐标系」里的矩形，不是设备像素。
 *     先 translate/scale 再 setClip，和反过来，结果不同（第 2 讲的顺序问题在这里复现）。
 *     要「钉死在设备上」的裁剪：resetTransform 之后再裁，或者用 viewport（第 2 讲）。
 *  ② 裁剪**是状态**，save()/restore() 会一起存回来 —— 所以**顺序必须是先 save() 再 setClip***。
 *     本讲实测过反面：先 setClipRect(cell) 再 save()，出了作用域 restore() 时裁剪还在身上
 *     （restore 只回到 save() 那一刻的状态，那一刻已经裁上了），紧跟在 cell 下方 14px 的
 *     那行标题一个像素都没上纸：自检横带的上墨像素实测 **0**；改成先 save 后裁，同一个
 *     自检读到 **494**。而日志里 save = true、PNG 照样生成 —— 只有数像素才看得见。
 *     另外 setClip*(..., ReplaceClip) 只替换、不清空，想彻底关掉用 setClipping(false)（196 行）；
 *     ⚠ 关掉之后 clipRegion() **照样读得回**原来那个区域（本机这一轮 hasClipping = false
 *     而 clipRegion().boundingRect 仍是 QRect(10,10 200x150)）—— 别把「读得到」当成「还在裁」。
 *  ③ **ClipOperation 没有并集**（本机 qnamespace.h 1339~1343 只有三个值）。
 *     要并集就自己把 QRegion united() 好，再 setClipRegion（本讲证据④ 就这么干）；
 *     要「带 alpha 的并集遮挡」，那本来就不该用裁剪，用合成模式（证据⑤）。
 *
 * 四、合成模式速记（记住两条就够，别的都是推论）
 *   Source*   ：**新画的东西赢**，目标只按新内容的 alpha 保留一点
 *   Dest*     ：**已有的东西赢**，新画的只当遮罩用（DestinationIn 就是「按蒙版切」）
 *   名字里带 In / Out / Atop / Over 的，套的就是 Photoshop 那套 alpha 组合语义。
 *   另外：**合成模式是 QPainter 状态**，用完记得设回 CompositionMode_SourceOver
 *   （144/145 行有 getter/setter），否则下一个图形莫名其妙地「叠不上去」。
 *
 * 五、性能提醒（本模块第三条硬钉子）
 *   路径裁剪（setClipPath + 复杂路径）与合成模式都要走抗锯齿/混合，
 *   比 setClipRect 贵得多。高频重绘的控件（第 6 讲）里，能只用 setClipRect 就
 *   只用它；必须用复杂形状时，考虑把结果先画进 QImage 再 drawImage（第 5 讲）。
 *
 * ——— 老师傅的话 ———
 *   徒弟，这一讲的是「把画笔变成模具」。路径和裁剪合起来给你的能力是：
 *   **任何形状都能当边界**，渐变、纹理、别人画的图，全都往里灌。
 *   但我要提醒的恰恰是别滥用：形状越怪，代价越高，而且**调试越难**。
 *   你画出来不对，很可能不是绘制错了，是「你以为的裁剪坐标系」错了 ——
 *   所以我的习惯是：先画一条 setClipRect 的粗边框把边界画出来看一眼，
 *   再换 setClipPath。裁剪看不见，就让它先可见。
 *
 *   第二件事：FillRule 这种东西，**背结论没用**。
 *   我在证据② 里没有写「奇偶规则中心是空的」这种话，我打的是中心像素的 alpha。
 *   因为路径填充跟变换、跟子路径方向、跟你用 addEllipse 还是 cubicTo 都有关，
 *   你只有那一格的像素值是真的。以后你在别人代码里看到「为什么这里有个洞」，
 *   第一反应应该是「打印那个像素」，不是「改改看」。
 *
 *   第三件：合成模式那 12 格，我把「非透明像素数」和「中心像素 ARGB」一起打，
 *   是因为只打一个必然骗自己 —— 半透明蒙版那条就是例子：像素数几乎没变，
 *   alpha 却差一半。**统计量要选能区分错的哪一个**，这是本模块做证据的统一口径。
 *   这一讲我又栽了两次，都是统计量选错：
 *     · countOpaque() 只看 alpha != 0。所以（a）铺了不透明底色的格子里它恒等于满幅
 *       —— 证据③ 那三个格子曾经全打成 11000，交叉验证形同虚设；换成透明底 +
 *       全不透明画刷后才是 1995 / 3175 / 8075；（b）总览 sheet 有均匀底色，
 *       要看「有没有上墨」得用 countInkOverBackground() 跟**指定**底色比。
 *     · 证据④ 的 region 那张曾经和「不裁剪」一模一样（43200 = 整张面积），
 *       原因是内容函数进来第一件事就 setClipping(false)，把我刚设好的 QRegion 关了。
 *       现在它打 11100，正好等于我构造的两个矩形 4800 + 6300，且 hasClipping = true。
 *
 *   最后，本讲我一开始写的 unitedPath()、UnionClip 这两个「想当然」的名字，
 *   都在头文件里查不到（155~157 行是成员函数 united()；ClipOperation 只有三个值）。
 *   这就是为什么要跑 g++ -fsyntax-only：它当场把这两个不存在的名字点出来，
 *   而不是等三个月后你在 CI 里看到一片红。
 *
 * ——— 小练习 ———
 *   1. 把证据② 的星形改成七个顶点的自交星（i*3 % 7），
 *      再打印中心与偏离中心的两个像素 alpha，观察 OddEven/Winding 的洞有没有变位置；
 *      据此给「洞在哪里」写一句你自己的、以像素为依据的结论。
 *   2. 把证据④ 的第二段裁剪改成「先 translate(40,40) 再 setClipRect(0,0,120,120)」，
 *      与非裁剪对照，用 clipRegion().boundingRect() 与像素数说明
 *      「裁剪坐标是当前坐标系的」这条；再用 setClipping(false) 关掉，看数字回到哪一档。
 *   3. 用 subtracted() 把证据③ 的「A ∩ B」改写成「A − (A − B)」，
 *      比较两张图的包围盒与像素数是否一致；再试 simplified() 之后再填，
 *      说明为什么「先简化再填」能让自交图形的洞行为变得可预测。
 */
// ============================================================================

// 本文件没有 Q_OBJECT 类，因此不需要 #include "07_painter_path_and_clipping.moc"。
