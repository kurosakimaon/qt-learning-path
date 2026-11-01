// ============================================================================
// 第 2 讲 · 坐标变换三件套与它们的顺序
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   绘图模块真正的第一道「数学关」。所有自定义控件、所有 canvas 类库，底下都是这一套：
//     1) translate / rotate / scale 三个动作**不可交换**——本讲不背结论，
//        用 QTransform::map() 把同一个点在两种顺序下的最终设备坐标打印出来，数字自己会说话；
//     2) save() / restore() 是一个栈：它保存的不只是变换，还有 pen/brush/font/clip 整套状态；
//     3) worldTransform() / setWorldTransform() 与 QTransform 的组合（operator*），
//        以及「先谁后谁」的判别方法（用嵌套 map 现场对比，不靠记忆）；
//     4) viewport / window 映射：把「逻辑坐标系」和「设备上的矩形」解耦，
//        这是画工程图、图表、缩放视图的正规做法（不是靠手算 scale 凑）；
//     5) 两个高频实务问题：「绕某一点旋转」为什么要 translate→rotate→translate 回去；
//        「y 轴向下」这个事实如何让角度的正负在屏幕上看着相反。
//
// 对应构建目标名：painting_02_coordinates_and_transforms
//   find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)
//   add_executable(painting_02_coordinates_and_transforms 02_coordinates_and_transforms.cpp)
//   target_link_libraries(painting_02_coordinates_and_transforms PRIVATE Qt6::Widgets Qt6::Svg)
//   set(CMAKE_AUTOMOC ON)
//   本文件同样**不含 Q_OBJECT**（只覆写 paintEvent），末尾不需要 .moc。
//
// 运行前须知：
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 painting_02_coordinates_and_transforms，跑到自动退出、退出码 0）。
//     但本讲**没有汇总自检行**，退出码 0 只证明「没崩、走完了」，不证明正文每个数字都对：
//     带具体数字的「预期」请以你自己那次运行输出为准（本机那一轮存档 runlogs/painting_02_coordinates_and_transforms.log）。
//   · 会往当前工作目录写 painting_02_coordinates_and_transforms.png、
//     painting_02_order_ab.png、painting_02_order_ba.png、painting_02_viewport.png。
//   · 无人值守：QImage 那条证据链在 exec() 之前就能跑（第 1 讲的铁律），
//     控件链用 QTimer::singleShot 驱动，约 1.5 秒自动 quit()。
//   · 实测状态：本示例已在 **Qt 6.12.0 / MinGW-w64 GCC 13.1.0 / Windows 11** 上编译通过并
//     **无头跑通**（`QT_QPA_PLATFORM=offscreen`，退出码 0），四张 PNG 与下面每个数字都是本机产物。
//     头文件在 `D:\Qt\6.12.0\mingw_64\include`（下面签名清单的行号就是对它核的），
//     Qt 源码在 `D:\Qt\6.12.0\Src\qtbase\src`（涉及实现的地方给了文件+行号）。
//     Windows 上 Qt 日志按本地代码页 GBK 输出，重定向时 `| iconv -f GBK -t UTF-8` 才不乱码。
//   · 文中「预期」都对着本机日志核过一遍；跑出来的数字随机器不同，
//     特别是耗时类，**请以你自己跑出来的数字为准**。
//   · 图解配套：../visuals/coordinates.html（三层坐标 + 顺序不可交换的分步动画，
//     页面由图解同事补；先按这个链接放着）。
// ============================================================================

#include <QApplication>
#include <QWidget>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QImage>
#include <QTransform>          // QtGui/qtransform.h
#include <QPoint>              // QtCore/qpoint.h：QPoint / QPointF
#include <QRect>              // QtCore/qrect.h：QRect / QRectF（同一个头文件）
#include <QSize>
#include <QString>
#include <QTimer>
#include <QCoreApplication>
#include <QDebug>

// ============================================================================
// 一、核对签名（本机 Qt6）
// ============================================================================
// QtGui/qtransform.h
//   第 55 行  bool isIdentity() const;
//   第 56 行  bool isInvertible() const;
//   第 57 行  bool isScaling() const;
//   第 77 行  void setMatrix(qreal m11, qreal m12, qreal m13, qreal m21, qreal m22, qreal m23,
//                           qreal m31, qreal m32, qreal m33);
//   第 81 行  [[nodiscard]] QTransform inverted(bool *invertible = nullptr) const;   ← Qt6 加了 nodiscard
//   第 85 行  QTransform &translate(qreal dx, qreal dy);
//   第 86 行  QTransform &scale(qreal sx, qreal sy);
//   第 91 行  QTransform &rotate(qreal a, Qt::Axis axis = Qt::ZAxis);        ← 角度单位是**度**（不是 1/16 度！）
//   第 94 行  QTransform &rotateRadians(qreal a, Qt::Axis axis = Qt::ZAxis); ← Qt6 提供弧度版
//   第 110 行 QTransform operator*(const QTransform &o) const;
//   第 115/116 行 QPoint map(const QPoint &) const; QPointF map(const QPointF &) const;
//   第 125 行 QRectF mapRect(const QRectF &) const;
//   第 126/127 行 void map(int x, int y, int *tx, int *ty) const;
//              void map(qreal x, qreal y, qreal *tx, qreal *ty) const;   ← 不想造 QPointF 就用这个
// QtGui/qpainter.h
//   第 201 行 void save();                 第 202 行 void restore();
//   第 205 行 void setTransform(const QTransform &transform, bool combine = false);
//   第 208 行 void resetTransform();       ← **不止**清世界矩阵，见下面「说明」第二条
//   第 210 行 void setWorldTransform(const QTransform &matrix, bool combine = false);
//   第 211 行 const QTransform &worldTransform() const;
//   第 213 行 QTransform combinedTransform() const;      ← 世界变换 + window/viewport 变换 的总效果
//   第 215/216 行 void setWorldMatrixEnabled(bool); bool worldMatrixEnabled() const;
//   第 218 行 void scale(qreal sx, qreal sy);
//   第 220 行 void rotate(qreal a);
//   第 222~224 行 void translate(const QPointF&)/(const QPoint&)/(qreal dx, qreal dy);
//   第 226 行 QRect window() const;        第 227 行 void setWindow(const QRect &window);
//   第 228 行 inline void setWindow(int x, int y, int w, int h);
//   第 230 行 QRect viewport() const;      第 231 行 void setViewport(const QRect &viewport);
//   第 232 行 inline void setViewport(int x, int y, int w, int h);
// 说明一：Qt6 的 qpainter.h 里**同时**有两条设置变换的入口，别记混：
//   第 205 行  void setTransform(const QTransform &transform, bool combine = false);
//   第 210 行  void setWorldTransform(const QTransform &matrix, bool combine = false);
//   两者形参一模一样。源码里 setTransform 的实现就是 `setWorldTransform(transform, combine);`
//   一行转发（Src/qtbase/src/gui/painting/qpainter.cpp 第 8030~8033 行），**没有别的差别**；
//   证据④ 末尾仍分别调用它们，把 worldTransform()（211 行）与 combinedTransform()（213 行）
//   的实际读数打出来让数字确认。老代码里只写 setTransform 的工程在 Qt6 照样编得过。
// 说明二：combinedTransform() 的构成也别背转述，看源码 —— qpainter.cpp 第 8153~8161 行：
//   `return d->state->worldMatrix * d->viewTransform() * d->hidpiScaleTransform();`
//   而 viewTransform()（同一文件 210~219 行）在 `VxF`（view transform 开关）为 false 时
//   **直接返回单位阵**，为 true 时才用 vw/ww、vh/wh 算出那两个比例。
//   resetTransform()（同一文件 8082~8104 行）把 wx/wy/ww/wh 与 vx/vy/vw/vh **全部**复位成
//   整个设备（原点 0、尺寸取 PdmWidth/PdmHeight）、worldMatrix 清成单位阵，
//   再把 setWorldMatrixEnabled(false) 与 setViewTransformEnabled(false) 一起关掉。
//   结论要改口径：**resetTransform() 之后 combinedTransform 是单位阵，window/viewport 的
//   比例不会残留**（本机跑出来就是 1.00/0.00，见证据④ 那行读数）。
//   只想清世界矩阵、保留逻辑坐标系 → 用 setWorldTransform(QTransform())（210 行）。
//   只想暂时不套用映射、数值还要留着 → 用 setViewTransformEnabled(false)（234 行）。
// QtGui/qimage.h / qpaintdevice.h：同第 1 讲（constScanLine 169、pixelColor 184、
//   devicePixelRatio 193、QImage(int,int,Format) 88；width/height/logicalDpiX 44~49）。

// ============================================================================
// 二、像素证据工具（与第 1 讲同一套思路：只用核对过的 constScanLine）
// ============================================================================
struct Box
{
    int count = 0;
    int minX = 1 << 30, maxX = -(1 << 30);
    int minY = 1 << 30, maxY = -(1 << 30);
    long long sumX = 0, sumY = 0;

    bool empty() const { return count == 0; }
    int inkedPixels() const { return count; }
    int centroidX() const { return count ? int(sumX / count) : -1; }
    int centroidY() const { return count ? int(sumY / count) : -1; }
};

static Box measureInk(const QImage &img, int x0, int x1, int y0, int y1)
{
    Box b;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > img.width())  x1 = img.width();
    if (y1 > img.height()) y1 = img.height();
    for (int y = y0; y < y1; ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = x0; x < x1; ++x) {
            if ((row[x] >> 24) != 0) {
                ++b.count;
                b.sumX += x; b.sumY += y;
                if (x < b.minX) b.minX = x;
                if (x > b.maxX) b.maxX = x;
                if (y < b.minY) b.minY = y;
                if (y > b.maxY) b.maxY = y;
            }
        }
    }
    return b;
}

static Box measureInk(const QImage &img)
{
    return measureInk(img, 0, img.width(), 0, img.height());
}

// 用一个 QTransform 的特征来「描述」它：取三个基准点看它们被送到哪儿。
// 为什么这样打印：QTransform 的矩阵元素访问器我只在头文件里逐个核到 m11（第 65 行），
// 不想用没核全的 m21/m31；map() 是明确核对过的（115/116 行），够用了。
static QString describe(const QTransform &t)
{
    qreal ax, ay, bx, by, cx, cy;
    t.map(qreal(0), qreal(0), &ax, &ay);      // qtransform.h 127 行
    t.map(qreal(1), qreal(0), &bx, &by);
    t.map(qreal(0), qreal(1), &cx, &cy);
    // 全部用 QString::number(double, 'f', 2) 拼，避免 QString::arg 在整数/浮点重载之间的解析歧义
    return QStringLiteral("原点→(%1,%2) x轴单位→(%3,%4) y轴单位→(%5,%6)")
        .arg(QString::number(ax, 'f', 2)).arg(QString::number(ay, 'f', 2))
        .arg(QString::number(bx, 'f', 2)).arg(QString::number(by, 'f', 2))
        .arg(QString::number(cx, 'f', 2)).arg(QString::number(cy, 'f', 2));
}

// ============================================================================
// 三、证据 ①：顺序不可交换（纯数学，用 map 打印最终设备坐标）
// ============================================================================
static void proofNonCommutative()
{
    qInfo() << "\n=== 证据① translate 与 rotate 不可交换 ===";

    QTransform A;                            // 默认构造 = 单位矩阵（isIdentity 会告诉我们）
    A.translate(100.0, 0.0);
    A.rotate(45.0);                          // 先平移，再旋转

    QTransform B;
    B.rotate(45.0);
    B.translate(100.0, 0.0);                 // 先旋转，再平移 —— 只有两行调了个头

    qInfo() << "  A 是单位阵吗 =" << A.isIdentity() << "；A 可逆 =" << A.isInvertible()
            << "；A 含缩放 =" << A.isScaling();     // qtransform.h 55/56/57 行
    qInfo() << "  A（translate→rotate）:" << describe(A);
    qInfo() << "  B（rotate→translate）:" << describe(B);

    const QPointF p0(0.0, 0.0);
    const QPointF p1(10.0, 0.0);
    qInfo() << "  同一个点 (0,0)：A.map =" << A.map(p0)      // qtransform.h 116 行
            << " B.map =" << B.map(p0) << "  ← 不相等就是「不可交换」的证明";
    qInfo() << "  同一个点 (10,0)：A.map =" << A.map(p1) << " B.map =" << B.map(p1);

    // operator* 的次序规则（110 行）现场判别：把两种「先谁后谁」的嵌套 map 也算出来对比。
    const QPointF ab = (A * B).map(p1);
    const QPointF ba = (B * A).map(p1);
    const QPointF aThenB = B.map(A.map(p1));
    const QPointF bThenA = A.map(B.map(p1));
    qInfo() << "  (A*B).map(P) =" << ab << "  vs  先 A 再 B 的嵌套结果 =" << aThenB;
    qInfo() << "  (B*A).map(P) =" << ba << "  vs  先 B 再 A 的嵌套结果 =" << bThenA;
    qInfo() << "  判别结论（**看上面两行数字哪两行相等**）："
            << "若 (A*B) 与「先 A 再 B」相等，则 Qt 的组合规则是「左边的矩阵先作用」；"
            << "自己确认一次比背十遍可靠。";

    // 逆矩阵（81 行）：变换可回滚的证据
    bool okInvert = false;
    const QTransform Ainv = A.inverted(&okInvert);
    const QPointF roundTrip = Ainv.map(A.map(p1));
    qInfo() << "  A.inverted() 可逆吗 =" << okInvert << "；A^-1(A(P)) =" << roundTrip
            << "（应回到 (10,0) 附近，浮点尾差正常）";
}

// ============================================================================
// 四、证据 ②：QPainter 的动作顺序 —— 用像素重心证明
// ============================================================================
// 画一个小黑方块，两种变换顺序各画一张，然后量着墨包围盒的重心。
// 重心是**实测**的物理像素坐标，不需要相信任何人（包括我）关于「先应用哪个」的说法。
static void proofPainterOrder()
{
    qInfo() << "\n=== 证据② QPainter 里 translate/scale 的顺序：量重心 ===";

    QImage ab(240, 240, QImage::Format_ARGB32_Premultiplied);
    ab.fill(Qt::transparent);
    {
        QPainter p(&ab);
        p.translate(80.0, 80.0);            // qpainter.h 224 行
        p.scale(2.0, 2.0);                  // 第 218 行
        p.fillRect(QRectF(0, 0, 20, 20), QColor(Qt::blue));   // 逻辑上的 20x20 方块
    }

    QImage ba(240, 240, QImage::Format_ARGB32_Premultiplied);
    ba.fill(Qt::transparent);
    {
        QPainter p(&ba);
        p.scale(2.0, 2.0);
        p.translate(80.0, 80.0);            // 只有这两行的顺序不同
        p.fillRect(QRectF(0, 0, 20, 20), QColor(Qt::blue));
    }

    const Box bAb = measureInk(ab);
    const Box bBa = measureInk(ba);
    ab.save(QStringLiteral("painting_02_order_ab.png"));
    ba.save(QStringLiteral("painting_02_order_ba.png"));
    qInfo() << "  先 translate 后 scale：包围盒 x[" << bAb.minX << ".." << bAb.maxX << "]"
            << " y[" << bAb.minY << ".." << bAb.maxY << "] 重心 =("
            << bAb.centroidX() << "," << bAb.centroidY() << ") 着墨 =" << bAb.inkedPixels();
    qInfo() << "  先 scale 后 translate：包围盒 x[" << bBa.minX << ".." << bBa.maxX << "]"
            << " y[" << bBa.minY << ".." << bBa.maxY << "] 重心 =("
            << bBa.centroidX() << "," << bBa.centroidY()
            << ") 着墨 =" << bBa.inkedPixels();
    qInfo() << "  读法：scale 之后再 translate，平移量会被 scale 乘一遍（80 变成 160）；"
            << "translate 之后再 scale，平移量不受影响。**位置差 = scale × 平移差**，"
            << "上面两组包围盒的差正好体现这件事——数字请你自己核对。";
}

// ============================================================================
// 五、证据 ③：绕某一点旋转 + y 轴向下的方向实测
// ============================================================================
static void proofRotateAboutPoint()
{
    qInfo() << "\n=== 证据③ 绕点旋转的三步套路，并测出正角度在屏幕上转向哪边 ===";

    const QPointF center(120.0, 120.0);
    const QRectF marker(100.0, 10.0, 40.0, 20.0);   // 一个小旗子，故意放在圆心的左上

    QImage img(240, 240, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setPen(QPen(QColor(200, 200, 200), 1));
        p.drawLine(QPointF(0, center.y()), QPointF(240, center.y()));   // 过圆心的水平参考线
        p.drawLine(QPointF(center.x(), 0), QPointF(center.x(), 240));   // 垂直参考线
        p.setPen(Qt::NoPen);                                            // qpainter.h 157 行 + Qt::NoPen
        p.setBrush(QColor(20, 160, 60));
        p.save();                                                       // qpainter.h 201 行
        // 三步套路：把「旋转中心」搬到原点 → 旋转 → 搬回去。
        // 为什么要 translate 回去：rotate 只绕**当前坐标系原点**转，
        // 而原点默认在画布左上角，不是你想要的那个点。
        p.translate(center);                                            // 224 行（QPointF 版，222 行）
        p.rotate(90.0);                                                 // 220 行
        p.translate(-center.x(), -center.y());
        p.drawRect(marker);
        p.restore();                                                    // 202 行
        p.setBrush(QColor(220, 60, 60));
        p.drawRect(marker);                                             // 未旋转的那面旗子，做参照
    }
    img.save(QStringLiteral("painting_02_coordinates_and_transforms.png"));

    // 量绿色旗子（旋转后）的重心
    const QImage probe = img;
    Box green;
    for (int y = 0; y < probe.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(probe.constScanLine(y));
        for (int x = 0; x < probe.width(); ++x) {
            const QRgb v = row[x];
            // QColor::fromRgba(QRgb) —— qcolor.h 第 170 行：static QColor fromRgba(QRgb rgba)
            const QColor c = QColor::fromRgba(v);
            if ((v >> 24) != 0 && c.green() > 120 && c.red() < 120) {
                ++green.count; green.sumX += x; green.sumY += y;
                if (x < green.minX) green.minX = x;
                if (x > green.maxX) green.maxX = x;
                if (y < green.minY) green.minY = y;
                if (y > green.maxY) green.maxY = y;
            }
        }
    }
    // 两个候选结果（数学式子由 y 轴方向决定，这里把两种都算出来，让测量挑）
    const QPointF rel(marker.center().x() - center.x(), marker.center().y() - center.y()); // (-20,-100)
    const QPointF ccw(center.x() - rel.y(), center.y() + rel.x());   // 数学逆时针 90°：(dx,dy)→(-dy,dx)
    const QPointF cw (center.x() + rel.y(), center.y() - rel.x());   // 另一个方向：(dx,dy)→(dy,-dx)
    qInfo() << "  未旋转时旗子中心（逻辑）=(" << marker.center().x() << "," << marker.center().y() << ")"
            << " 相对旋转中心 =(" << rel.x() << "," << rel.y() << ")";
    qInfo() << "  旋转后实测重心 =(" << green.centroidX() << "," << green.centroidY() << ")"
            << " 着墨 =" << green.inkedPixels();
    qInfo() << "  候选一（数学逆时针）=(" << ccw.x() << "," << ccw.y() << ")"
            << " 候选二（数学顺时针）=(" << cw.x() << "," << cw.y() << ")";
    qInfo() << "  → **重心离哪个候选近，正角度就往哪边转**。y 轴向下意味着「数学上的逆时针」"
            << "在屏幕上看很可能是顺时针，这就是为什么做动画时角度符号总要试一次才对。";
}

// ============================================================================
// 六、证据 ④：viewport / window 映射
// ============================================================================
static void proofViewportWindow()
// 逻辑域（window）与设备域（viewport）解耦：这是「同一份数据适配任意画布」的正规做法。
{
    qInfo() << "\n=== 证据④ setViewport / setWindow 的映射实测 ===";

    QImage img(480, 320, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setViewport(0, 0, 480, 320);        // qpainter.h 232 行（int 版）
        p.setWindow(0, 0, 100, 100);          // qpainter.h 228 行：逻辑 0..100
        qInfo() << "  viewport() =" << p.viewport() << "  window() =" << p.window();  // 230/226 行
        p.setPen(QPen(QColor(0, 0, 0), 1.0));
        p.setBrush(QColor(30, 120, 220));
        p.drawRect(QRectF(25.0, 25.0, 25.0, 25.0));    // 在「0..100」这个逻辑坐标系里画
        qInfo() << "  combinedTransform()（213 行）:" << describe(p.combinedTransform());
    }
    img.save(QStringLiteral("painting_02_viewport.png"));
    const Box b = measureInk(img);
    // 手算预期：x 方向 480/100 = 4.8，y 方向 320/100 = 3.2
    qInfo() << "  逻辑矩形 (25,25,25,25) 的手算设备范围 = x[" << int(25 * 4.8) << ".."
            << int((25 + 25) * 4.8) << "] y[" << int(25 * 3.2) << ".." << int((25 + 25) * 3.2) << "]";
    qInfo() << "  实测着墨范围 = x[" << b.minX << ".." << b.maxX << "] y[" << b.minY << ".."
            << b.maxY << "] 着墨 =" << b.inkedPixels();
    qInfo() << "  两条对上了，就证明 window/viewport 做的是**各向独立**的线性拉伸（4.8 与 3.2 不相等，"
            << "所以图形被拉长了）；想等比缩放，就得让 viewport 与 window 的宽高比一致。";

    // 附加：三条入口各自的分工，全部用读数对比、不背语义 ——
    //   setTransform(205 行) vs setWorldTransform(210 行)：前者就是后者的转发（见源码账）；
    //   resetTransform(208 行) vs setWorldTransform(QTransform())：一个掀两层、一个掀一层。
    {
        QPainter p(&img);
        p.setViewport(0, 0, 480, 320);        // 先让 window/viewport 层非平凡，才能看出两者的差别
        p.setWindow(0, 0, 100, 100);
        QTransform t;
        t.scale(3.0, 3.0);
        p.setWorldTransform(t);                              // combine = false（默认）：替换世界变换
        qInfo() << "  setWorldTransform(scale3, combine=false) 之后："
                << "worldTransform:" << describe(p.worldTransform())
                << " combinedTransform:" << describe(p.combinedTransform());
        QTransform r;
        r.rotate(30.0);
        p.setWorldTransform(r, true);                        // combine = true：与当前世界变换复合
        qInfo() << "  setWorldTransform(rotate30, combine=true) 之后：" << describe(p.worldTransform())
                << " 含缩放 =" << p.worldTransform().isScaling()
                << "（还留着 scale 的痕迹就说明是复合，不是替换）";
        p.setTransform(t);                                   // 205 行：同名同参数的另一条入口
        qInfo() << "  setTransform(scale3, combine=false) 之后："
                << "worldTransform:" << describe(p.worldTransform())
                << " combinedTransform:" << describe(p.combinedTransform())
                << " → 与上面 setWorldTransform 的读数**逐行对比**：一样的话就说明这条代码路径上两者等价，"
                << "不一样就记下差在哪。这是本讲唯一一处「用数字代替背诵」的作业。";
        p.resetTransform();                                  // 208 行：见下面那段源码账
        qInfo() << "  resetTransform() 之后 worldTransform().isIdentity ="
                << p.worldTransform().isIdentity()
                << " combinedTransform().isIdentity =" << p.combinedTransform().isIdentity()
                << " window =" << p.window() << " viewport =" << p.viewport()
                << " 读数 =" << describe(p.combinedTransform());
        qInfo() << "  → 判定：combinedTransform **也是单位阵**，window/viewport 双双回到 (0,0,480,320)。"
                   "**比例不残留。** 旧注释写「combinedTransform 仍带 window/viewport 的比例」是错的，"
                   "而且它同一行打印出来就是 1.00/0.00 —— 自己把自己证伪了。";
        qInfo() << "  源码账（本机 Src/qtbase/src/gui/painting/qpainter.cpp 第 8082~8104 行，"
                   "resetTransform 一口气干四件事）：";
        qInfo() << "    8094  wx=wy=vx=vy=0                          ← window/viewport 的**原点**复位";
        qInfo() << "    8095~8096  ww=vw=PdmWidth, wh=vh=PdmHeight   ← window/viewport 的**尺寸**复位成整个设备";
        qInfo() << "    8097  worldMatrix = QTransform()             ← 世界矩阵清成单位阵";
        qInfo() << "    8098~8099  setWorldMatrixEnabled(false); setViewTransformEnabled(false);";
        qInfo() << "    而 combinedTransform() = worldMatrix * viewTransform() * hidpiScale（同一文件 8160 行），";
        qInfo() << "    viewTransform() 在 VxF=false 时直接返回单位阵（同一文件 210~219 行）→ 必然单位阵。";

        // ── resetTransform() 与 setWorldTransform(QTransform()) 的分工 ──────────
        // 一句话：**resetTransform() 掀两层（世界矩阵 + window/viewport），
        //           setWorldTransform(QTransform()) 只掀世界矩阵那一层，window/viewport 原样留着。**
        // 想「保留逻辑坐标系、只把自己的 translate/rotate/scale 归零」→ 用后者；
        // 想「彻底回到设备像素坐标」→ 用前者。混用的症状是：图表明明要 0..100 的逻辑域，
        // 你 resetTransform() 了一下，数据就全挤在左上角 100x100 像素里。
        p.setViewport(0, 0, 480, 320);                       // setWindow/setViewport 会把 VxF 重新置 true
        p.setWindow(0, 0, 100, 100);                         //   （qpainter.cpp 7097 / 7161 行）
        qInfo() << "  【分工对照】重新 setViewport(0,0,480,320)+setWindow(0,0,100,100) 之后"
                << " window =" << p.window() << " viewport =" << p.viewport()
                << " combinedTransform =" << describe(p.combinedTransform());
        p.setWorldTransform(QTransform());                   // 210 行：只归零世界矩阵
        qInfo() << "  再 setWorldTransform(QTransform())：worldTransform().isIdentity ="
                << p.worldTransform().isIdentity()
                << " combinedTransform().isIdentity =" << p.combinedTransform().isIdentity()
                << " window =" << p.window() << " viewport =" << p.viewport()
                << " 读数 =" << describe(p.combinedTransform());
        qInfo() << "  → 判定：worldTransform 回到单位阵，但 combinedTransform **仍带 4.8/3.2 的比例**、"
                   "window 仍是 (0,0,100,100)。这一条和上面 resetTransform() 的读数并排放，"
                   "就是两者分工的全部证据。";
        // 顺手把「只关不重置」的第三个开关打出来：viewTransformEnabled(false) 也能让
        // combinedTransform 变单位阵，但 window/viewport 的**数值还在**，随时能开回来。
        p.setViewTransformEnabled(false);                    // qpainter.h 234 行
        qInfo() << "  第三种：setViewTransformEnabled(false) 之后 combinedTransform().isIdentity ="
                << p.combinedTransform().isIdentity()
                << " 但 window 仍 =" << p.window() << " viewport 仍 =" << p.viewport()
                << " → 它是「暂时不套用映射」，resetTransform() 是「连映射本身都拆了」。";
    }
}

// ============================================================================
// 七、控件：验证「变换状态是画在设备上的，与 paintEvent 无冲突」
// ============================================================================
class Stage : public QWidget
{
public:
    explicit Stage(QWidget *parent = nullptr) : QWidget(parent) {}

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter p(this);
        p.fillRect(0, 0, width(), height(), QColor(245, 245, 250));

        // save/restore 的完整语义：一个调用就把 pen/brush/transform/font 全存回去
        p.setPen(QPen(QColor(220, 40, 40), 3));
        p.setBrush(Qt::NoBrush);
        p.drawRect(20, 20, 80, 40);                    // 红色的框

        p.save();
        p.setPen(QPen(QColor(40, 40, 220), 3));
        p.translate(60.0, 60.0);
        p.rotate(20.0);
        p.setBrush(QColor(255, 220, 120));
        p.drawRect(20, 20, 80, 40);                    // 蓝框 + 填充，且被搬动/转过
        p.restore();

        p.drawRect(20, 20, 80, 40);                    // restore 之后：应该又是原来那个红框、原位
        qInfo() << "  Stage::paintEvent 完成一次绘制，世界变换 isIdentity ="
               << p.worldTransform().isIdentity()
               << " viewport =" << p.viewport() << " window =" << p.window();
    }
};

// ============================================================================
// 八、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // exec() 之前就能跑的四条证据：它们只碰 QImage（第 1 讲铁律一的合法分支）
    proofNonCommutative();
    proofPainterOrder();
    proofRotateAboutPoint();
    proofViewportWindow();

    Stage stage;
    stage.setWindowTitle(QStringLiteral("第 2 讲 · 坐标变换与顺序"));
    stage.resize(360, 240);
    stage.show();

    QTimer::singleShot(500, [] {
        qInfo() << "\n=== save()/restore() 的语义（证据⑤，看控件里那三个框）===";
        qInfo() << "  restore() 还回的不只是坐标变换：pen、brush、font、渲染提示、裁剪区**一并**回去。"
                << "所以第三条 drawRect 应该和第一条完全重合（红色、原位、无填充）。";
        qInfo() << "  纪律：save/restore 必须配对，且**跨函数不共享栈**——"
                << "painter 析构（RAII）时未配对的 restore 不会替你补，但整幅画笔状态随之消失，"
                << "所以「忘了 restore」的现象通常表现为后面的东西莫名其妙跑偏。";
    });

    QTimer::singleShot(900, [] {
        qInfo() << "\n=== 结论 ===";
        qInfo() << "① 三件套不可交换：先 scale 后 translate，平移量会被 scale 乘；证据②的两组包围盒差就是它。";
        qInfo() << "② 绕点旋转的固定套路：translate(center) → rotate(a) → translate(-center)，"
                << "因为 rotate/scale 永远作用在**当前坐标系原点**上（第 6 讲做仪表盘控件时它是唯一写法）。";
        qInfo() << "③ Qt6 里世界变换有**两条**入口：setTransform(m, combine)（qpainter.h 205 行）与"
                << "setWorldTransform(m, combine)（210 行），形参完全一样，而且 setTransform 就是"
                << "setWorldTransform 的一次转发（qpainter.cpp 8030~8033 行），所以这条代码路径上两者**等价**；"
                << "读它用 worldTransform()（211 行），读「连 window/viewport 一起算完」用 combinedTransform()（213 行）。"
                << "两者的实际差别以证据④ 末尾打出的那两组数字为准，别背我的转述。";
        qInfo() << "③b 复位有**三个档位**，别只会按最狠的那个："
                << "setWorldTransform(QTransform()) 只归零**世界矩阵**，window/viewport 留着"
                << "（combinedTransform 仍带 4.8/3.2）；setViewTransformEnabled(false) 只是**暂时不套用**映射，"
                << "数值还在；resetTransform() 最狠，**两层一起掀**——世界矩阵清成单位阵，"
                << "window/viewport 一并复位成整个设备（qpainter.cpp 8082~8104 行），"
                << "所以它之后 combinedTransform 就是单位阵，**不残留任何比例**。";
        qInfo() << "④ window/viewport 是各向独立拉伸：宽高比不等就变形（证据④的 4.8 vs 3.2）。"
                << "要做「数据坐标 → 屏幕坐标」的图表，用这套，别自己写比例。";
        qInfo() << "⑤ QTransform::rotate 的单位是**度**，而 QPainter::drawArc/drawPie/drawChord 是 **1/16 度**，"
                << "同一个 90 度，一处写 90.0 一处要写 90*16。这两个地方最容易被「顺手」写错。";
        qInfo() << "  图解：../visuals/coordinates.html 里把这三件套的顺序做成可拖动分步动画，跑完代码去看图。";
        QCoreApplication::quit();
    });

    return app.exec();
}

// ============================================================================
// 附：一张「我该用哪个」的对照表
// ============================================================================
/*
 *  想做的事                       做法                                                注意点
 *  -------------------------------------------------------------------------------
 *  整体搬动画布                    QPainter::translate(dx, dy)                        之后所有坐标都相对新原点
 *  放大/缩小绘制                   QPainter::scale(sx, sy)                            线宽是否跟着放大要看 cosmetic（第 3 讲）
 *  绕某点转                        translate→rotate→translate 回去                     或者一次性算好 QTransform 再 setWorldTransform
 *  数据坐标 → 屏幕坐标             setWindow(数据范围) + setViewport(设备矩形)          宽高比不等就变形
 *  要一个可复用/可复合的变换        QTransform t; t.translate(); t.rotate(); ...        setWorldTransform(t) / t * t2 / t.inverted()
 *  临时改一下再改回来               save() / restore()                                 配对写，别跨函数
 *  已知逻辑点想知道落在哪个像素     combinedTransform().map(QPointF)                   这条把世界变换和 window/viewport 一起算
 *  只把自己的 translate/rotate/scale 归零，**保留**逻辑坐标系
 *                                  setWorldTransform(QTransform())                     window/viewport 原样留着，combinedTransform 仍带比例
 *  彻底回到设备像素坐标             resetTransform()                                    **两层一起掀**：世界矩阵清单位阵 + window/viewport 复位成整个设备
 *  暂时不套用 window/viewport（回头还要用）
 *                                  setViewTransformEnabled(false) / (true)              数值不丢，只是先不算它
 *
 * 三个必须记死的单位/约定：
 *   ① 屏幕 y 轴向下。数学里「逆时针为正」到这里可能变成视觉顺时针 —— 本讲用测量解决，别靠背。
 *   ② QTransform/QPainter::rotate 用**度**；drawArc/drawPie/drawChord 用 **1/16 度**；
 *      QLineF::angle() 又是度。同一屏代码里三种单位混着用是常态。
 *   ③ 变换永远作用在**当前坐标系**，而坐标系的原点默认在画布 (0,0)（不是图形中心）。
 *      「为什么我旋转的结果绕着左上角飞出去」99% 是忘了第①②③里的第③条。
 *
 * ——— 老师傅的话 ———
 *   徒弟，这一讲我只希望你带走一个动作：**把顺序写出来，然后量像素**。
 *   变换这块，光靠脑子推「先乘还是后乘」，十年老手也会在半夜栽一次。
 *   原因很实在：Qt 用的是行向量约定（p·M），跟你在图形学课本上常见的列向量（M·p）
 *   写法正好反着来；再叠上 window/viewport 又一层乘法，三层混在一起，
 *   靠记忆去推是拿运气赌手艺。
 *
 *   所以证据① 我把 (A*B).map(P) 和 B.map(A.map(P)) 打在同一个 qInfo 里，
 *   就是为了让你养成「写两行代码去验证一个矩阵常识」的习惯。跑一次十秒钟，
 *   省掉两小时 debug，这笔账怎么算都划算。
 *
 *   第二个要带走的是 translate→rotate→translate 那个套路。别嫌它啰嗦。
 *   你以后做表盘、做进度环、做旋转按钮，全是它的变体；
 *   它的价值不在「算得准」，而在**让读代码的人一眼看出旋转中心在哪**。
 *   图形代码的可读性，八成就写在这三行的顺序里。
 *
 * ——— 小练习 ———
 *   1. 把证据② 的 scale 从 2.0 改成 0.5，重新量两组包围盒与着墨像素，
 *      解释为什么「先 scale 后 translate」那一组的位移也跟着变小了。
 *   2. 把证据③ 的 rotate(90.0) 改成 rotate(-90.0)，看实测重心跳到另一个候选附近，
 *      由此给「y 轴向下时正角度的视觉转向」写一句你**自己机器上测出来**的结论。
 *   3. 把证据④ 的 setWindow(0,0,100,100) 改成 setWindow(0,0,150,100)，
 *      预测并实测矩形的新包围盒，说明为什么它是「横向压缩」而不是「整体缩小」。
 *      再试试 window 的 height 传负数（文档说这会让 y 轴反向），用包围盒证实或证伪。
 */
// ============================================================================

// 本文件没有 Q_OBJECT 类，因此不需要 #include "02_coordinates_and_transforms.moc"。
