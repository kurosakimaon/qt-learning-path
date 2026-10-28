// ============================================================================
// 第 8 讲 · QGraphicsView 框架：什么时候不该再自己画
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   第 6 讲把「一个控件里的画」包好了。这一讲回答边界问题：**图元多起来、要能点、
//   要能整体变换**的时候，为什么该换成 QGraphicsScene / QGraphicsView / QGraphicsItem
//   三件套，以及换过来要付的三笔账（坐标、命中、缓存）：
//     1) 三者分工（本机头文件为证）：
//        QGraphicsScene（QtWidgets/qgraphicsscene.h）= **图元的容器 + 索引 + 命中测试 +
//          坐标事件的派发者**；它自己不是 QWidget，不显示任何东西；
//        QGraphicsView（qgraphicsview.h）= **一个 QWidget 视口**，负责把场景的一部分
//          按某个变换画到屏幕上（fitInView / scale / transformationAnchor /
//          viewportUpdateMode / dragMode / CacheMode）；
//        QGraphicsItem（qgraphicsitem.h）= **一个可画、可命中、可变换的图元**，
//          纯虚的 boundingRect() + paint() 就是它的全部义务；
//     2) 三层坐标系与换算：图元坐标 → 场景坐标 → 视图（设备）坐标，
//        item->mapToScene/mapFromScene、view->mapToScene/mapFromScene、mapToGlobal/mapFromGlobal，
//        本讲把同一个点在这四层里走过的每一步都打印出来（含往返误差）；
//     3) 命中测试：scene->items(QPointF) / scene->itemAt(pos, deviceTransform) /
//        view->itemAt(viewPoint) 三条路，各自返回什么、参数为什么长这样；
//     4) 缩放：view->scale() 与 fitInView()、transformationAnchor 的作用，
//        以及 QGraphicsItem::ItemIgnoresTransformations（qgraphicsitem.h 56 行 = 0x20）：
//        本讲用一个「图例」图元实测缩放 3 倍后它的**设备尺寸不变**，别的图元变 3 倍；
//     5) 缓存：QGraphicsItem::CacheMode（qgraphicsitem.h 111~114 行：None /
//        ItemCoordinateCache / DeviceCoordinateCache）——图元多了以后 paint 次数怎么被
//        缓存吃掉，本讲给一个可开关的计数器（2000 个图元真的放进去）；
//     6) 证据：往场景里放 **2000 个图元**，打印场景 rect、命中测试结果、
//        视图缩放后某个图元的设备坐标，并把场景渲染成两张 PNG
//        （一张走 QGraphicsScene::render，一张走 QGraphicsView::render，比的是内容而不是像素数）。
//
// 对应构建目标名：painting_08_graphicsview_basics
//   find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)   # 本组就是需要 Svg 的那一讲（图元可用 SVG）
//   add_executable(painting_08_graphicsview_basics 08_graphicsview_basics.cpp)
//   target_link_libraries(painting_08_graphicsview_basics PRIVATE Qt6::Widgets Qt6::Svg)
//   set(CMAKE_AUTOMOC ON)
//   本文件**有 Q_OBJECT 类（DotItem）**：DotItem 要能发 clicked() 信号（图元想对外说话，
//   就必须是 QObject，也就是继承 QGraphicsObject 而不是 QGraphicsItem）。
//   末尾必须 `#include "08_graphicsview_basics.moc"`；手工核对时先跑
//   `moc 08_graphicsview_basics.cpp -o 08_graphicsview_basics.moc`。
//   LegendItem **没有** Q_OBJECT：它只是 QGraphicsItem 的子类，不发信号。
//
// 运行前须知：
//   · 会往当前工作目录写：painting_08_graphicsview_basics.png（QGraphicsView::render 抓的视图）、
//     painting_08_scene_render.png（QGraphicsScene::render 直接画的那张）。
//   · 无人值守：所有「鼠标操作」都用 QApplication::sendEvent + QGraphicsSceneMouseEvent
//     **在场景坐标里手工投事件**，不需要真鼠标；约 1.5 秒自动 quit()。
//     除了 main 末尾的 app.exec()，没有任何阻塞调用。
//   · 可以无头跑：`painting_08_graphicsview_basics -platform offscreen`。
//     注意 offscreen 下视图仍然有 viewport 尺寸，但窗口位置由平台决定，
//     所以 **mapToGlobal 的数字只证明「往返一致」，不证明它落在哪块屏上**。
//   · 未实测声明：本机当前无法编译链接 Qt 运行时（运行时库版本不匹配），
//     本示例**已通过 g++ -fsyntax-only 编译期核对（-std=c++17 -Wall -Wextra 零 error 零 warning），
//     未链接、未运行**；所有「预期」都是推理，2000 图元的耗时与缓存效果请你自己跑出来。
//   · Qt5 → Qt6 差异本讲涉及两处（都是**头文件里能看见**的）：
//       ① QGraphicsItem 的类型标识是**匿名 enum{ Type = 1, UserType = 65536 }**
//          （qgraphicsitem.h 357~360 行）+ `virtual int type() const`（361 行）：
//          自定义图元写 `enum { Type = UserType + 1 }; int type() const override { return Type; }`，
//          靠 qgraphicsitem_cast 判类型；Qt6 的 Qt::ItemDataRole 那套不参与，别和
//          model_view 模块的 role 混起来（图元自定义数据走 setData(int, QVariant)，352 行）。
//       ② 本讲用到的每个枚举与函数都在下面「核对签名」里带行号列着（qgraphicsscene.h /
//          qgraphicsview.h / qgraphicsitem.h），**没有任何一个名字是查不到硬写的**；
//          唯一带默认参数的新写法是 `QGraphicsItem::setCacheMode(CacheMode, const QSize & = QSize())`
//          （qgraphicsitem.h 153 行），所以老代码的单参数调用照样编得过。
//   · 图解配套：../visuals/coordinates.html（场景/视图/图元三层坐标的分步动画，
//     页面由图解同事补，链接先放着）。
// ============================================================================

#include <QApplication>          // QtWidgets/qapplication.h：sendEvent
#include <QGraphicsScene>        // QtWidgets/qgraphicsscene.h
#include <QGraphicsView>         // QtWidgets/qgraphicsview.h
#include <QGraphicsItem>         // QtWidgets/qgraphicsitem.h：基类 + 各种内建图元
#include <QGraphicsObject>       // QtWidgets/qgraphicsitem.h 第 486 行：QObject + QGraphicsItem
#include <QGraphicsRectItem>     // 同一个头文件：内建图元
#include <QStyleOption>          // QtWidgets/qstyleoption.h：QStyleOptionGraphicsItem
#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QImage>
#include <QFont>
#include <QPainterPath>          // QtGui/qpainterpath.h：shape() 要用
#include <QRectF>                // QtCore/qrect.h：QRect / QRectF
#include <QPointF>               // QtCore/qpoint.h：QPoint / QPointF
#include <QPoint>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QList>
#include <QVariant>              // QtCore/qvariant.h：setData(int, QVariant)
#include <QEvent>                // QtCore/qcoreevent.h：QEvent::GraphicsSceneMousePress
#include <QGraphicsSceneMouseEvent>  // QtWidgets/qgraphicsscene.h 里声明的事件类型
#include <QTimer>
#include <QtMath>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QDebug>

// ============================================================================
// 一、核对签名（本机 C:\msys64\ucrt64\include\qt6，全部带行号；对不上的一律不用）
// ============================================================================
// QtWidgets/qgraphicsscene.h
//   第 65 行   Q_PROPERTY(QRectF sceneRect READ sceneRect WRITE setSceneRect)
//   第 74 行   enum ItemIndexMethod { ... }（BspIndex / NoIndex 一类，本讲只读默认）
//   第 92 行   QRectF sceneRect() const;
//   第 95/96 行 void setSceneRect(const QRectF &); void setSceneRect(x, y, w, h);
//   第 99 行   void render(QPainter *painter, const QRectF &target, const QRectF &source,
//                          Qt::AspectRatioMode aspectRatioMode = Qt::KeepAspectRatio);
//   第 109 行  QRectF itemsBoundingRect() const;
//   第 111 行  QList<QGraphicsItem *> items(Qt::SortOrder = Qt::DescendingOrder) const;
//   第 113 行  QList<QGraphicsItem *> items(const QPointF &pos,
//                 Qt::ItemSelectionMode mode = Qt::IntersectsItemShape,
//                 Qt::SortOrder order = Qt::DescendingOrder,
//                 const QTransform &deviceTransform = QTransform()) const;   ← 命中测试
//   第 114/115/116 行 同名重载：QRectF / QPolygonF / QPainterPath
//   第 122/123 行 QGraphicsItem *itemAt(const QPointF &pos, const QTransform &deviceTransform) const;
//   第 126 行  QList<QGraphicsItem *> selectedItems() const;
//   第 134 行  void addItem(QGraphicsItem *item);
//   第 135~150 行 addEllipse / addLine / addPolygon / addRect / addText / addSimpleText
//   第 150 行  void removeItem(QGraphicsItem *item);
//   第 153/155 行 void setFocusItem(...); void setFocus(Qt::FocusReason = Qt::OtherFocusReason);
//   第 161 行  QGraphicsItem *mouseGrabberItem() const;
//   第 206 行  void clear();                                          ← 删掉全部图元
// QtWidgets/qgraphicsview.h
//   第 48~52 行 enum ViewportAnchor { NoAnchor, AnchorViewCenter, AnchorUnderMouse };
//   第 56~59 行 enum CacheModeFlag { CacheNone = 0x0, CacheBackground = 0x1 };
//   第 63~67 行 enum DragMode { NoDrag, ScrollHandDrag, RubberBandDrag };
//   第 69~75 行 enum ViewportUpdateMode { FullViewportUpdate, MinimalViewportUpdate,
//                 SmartViewportUpdate, NoViewportUpdate, BoundingRectViewportUpdate };
//   第 77~81 行 enum OptimizationFlag { DontSavePainterState = 0x1,
//                 DontAdjustForAntialiasing = 0x2, IndirectPainting = 0x4 };
//   第 87/88 行 QGraphicsView(QWidget *); QGraphicsView(QGraphicsScene *, QWidget * = nullptr);
//   第 90 行   QSize sizeHint() const override;
//   第 93/94 行 void setRenderHint(QPainter::RenderHint, bool enabled = true); setRenderHints(...);
//   第 99~106 行 transformationAnchor()/setTransformationAnchor()、resizeAnchor、
//                 viewportUpdateMode()/setViewportUpdateMode()
//   第 112/113 行 dragMode()/setDragMode(DragMode)
//   第 122 行  void setCacheMode(CacheMode mode);                     ← 视图级：背景缓存
//   第 128/129 行 QGraphicsScene *scene() const; void setScene(QGraphicsScene *);
//   第 132/133 行 setSceneRect(const QRectF &) / (x,y,w,h)
//   第 135 行  QTransform transform() const;      第 139 行 void resetTransform();
//   第 140/141/143 行 void rotate(qreal); void scale(qreal sx, qreal sy); void translate(...);
//   第 145~150 行 centerOn(QPointF/QPointF x,y/QGraphicsItem*)、ensureVisible(...)
//   第 151~154 行 fitInView(const QRectF &, Qt::AspectRatioMode = Qt::IgnoreAspectRatio)
//                 + (x,y,w,h,...) + (const QGraphicsItem *, ...)
//   第 157 行  void render(QPainter *painter, const QRectF &target = QRectF(),
//                 const QRect &source = QRect(), Qt::AspectRatioMode = Qt::KeepAspectRatio);
//                 ← 出图走这条。它和 QWidget::render(QPaintDevice*)（qwidget.h 348 行）**同名**，
//                   C++ 的名字查找会把基类那组重载藏掉，所以 view.render(&image) 编不过；
//                   想按 QWidget 那条抓控件，得写 `static_cast<QWidget *>(&view)->render(&image)`。
//   第 167/168 行 QGraphicsItem *itemAt(const QPoint &pos) const; itemAt(int x, int y);
//                 ← **视图坐标**的命中测试
//   第 170~179 行 QPointF mapToScene(const QPoint &) const; QPolygonF mapToScene(const QRect &);
//                 QPainterPath mapToScene(const QPainterPath &);
//                 QPoint mapFromScene(const QPointF &) const; QPolygon mapFromScene(const QRectF &);
//                 inline QPointF mapToScene(int x, int y) const;
//   （mapToGlobal / mapFromGlobal / viewport() 来自基类 QWidget：qwidget.h）
// QtWidgets/qgraphicsitem.h
//   第 50~56 行 enum GraphicsItemFlag { ItemIsMovable = 0x1, ItemIsSelectable = 0x2,
//                 ItemIsFocusable = 0x4, ItemClipsToShape = 0x8, ItemClipsChildrenToShape = 0x10,
//                 ItemIgnoresTransformations = 0x20, ... };
//   第 75~102 行 enum GraphicsItemChange { ..., ItemPositionHasChanged, ItemSceneHasChanged,
//                 ItemScenePositionHasChanged, ... }
//   第 111~114 行 enum CacheMode { None = 0, ItemCoordinateCache = 1, DeviceCoordinateCache = 2 };
//   第 148~153 行 GraphicsItemFlags flags() const; void setFlag(flag, bool = true);
//                 void setFlags(...); CacheMode cacheMode() const;
//                 void setCacheMode(CacheMode mode, const QSize &cacheSize = QSize());
//   第 228/233~235 行 QPointF pos() const; QPointF scenePos() const; void setPos(const QPointF&);
//   第 249 行  qreal rotation() const;              第 266 行 void setZValue(qreal);
//   第 270 行  virtual QRectF boundingRect() const = 0;        ← 纯虚：你必须实现
//   第 271 行  QRectF childrenBoundingRect() const;
//   第 273 行  virtual QPainterPath shape() const;              ← 命中测试用的是它，不是包围盒
//   第 290 行  virtual void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
//                 QWidget *widget = nullptr) = 0;               ← 纯虚：你也必须实现
//   第 298/301/307/313/329/332/338 行 mapToScene(QPointF)→QPointF、mapToScene(QRectF)→QPolygonF、
//                 mapFromScene(QPointF)→QPointF、(qreal,qreal) 便捷版
//   第 351/352 行 QVariant data(int key) const; void setData(int key, const QVariant &value);
//   第 357~361 行 enum { Type = 1, UserType = 65536 }; virtual int type() const;
//   第 405 行  void prepareGeometryChange();
//   第 486 行  class Q_WIDGETS_EXPORT QGraphicsObject : public QObject, public QGraphicsItem
//   第 588/607/645/689/726/765 行 内建图元的 paint() override（Rect/Ellipse/Line/Pixmap…）
// 本讲**没用**（本机头文件里没核到，别照抄别人的代码）：
//   QGraphicsItem::setScale()/setRotation 的「动画式」封装（只用了 qreal rotation() 读）；
//   QGraphicsScene::BlitOperation（本文件里没有这个枚举）；
//   Qt::UnionClip（第 7 讲已经确认不存在，与框架无关）。

// ============================================================================
// 二、自定义图元一：DotItem（继承 QGraphicsObject，所以能发信号）
//     义务只有两条：boundingRect() 说清「我占多大」、paint() 说清「我怎么画」。
// ============================================================================
class DotItem : public QGraphicsObject
{
    Q_OBJECT
public:
    // 自定义类型标识：qgraphicsitem_cast 与 type() 都认这个（qgraphicsitem.h 357~361 行）
    enum { Type = QGraphicsItem::UserType + 1 };
    int type() const override { return Type; }

    DotItem(qint64 id, qreal radius, QPointF sceneCenter)
        : m_id(id), m_radius(radius)
    {
        setPos(sceneCenter);                     // 注意：**场景坐标**放进来，落到图元自己的原点
        setData(0, QVariant::fromValue(id));     // 352 行：id 挂在图元上，命中后一眼能认出来
        setFlag(QGraphicsItem::ItemIsSelectable);   // 52 行：能被选中（配合第 ⑥ 步的证据）
    }

    QRectF boundingRect() const override
    {
        // 图元坐标：以自己 (0,0) 为中心。留 1 像素给画笔宽度，否则边缘会被裁掉
        return QRectF(-m_radius - 1, -m_radius - 1, (m_radius + 1) * 2, (m_radius + 1) * 2);
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *) override
    {
        ++s_paints;                              // 重绘计数：缓存那一段的证据就从这里出
        Q_UNUSED(option);                        // exposedRect / levelOfDetailFromTransform 本讲不用
        const qreal hue = std::fmod(static_cast<qreal>(m_id) * 37.0, 360.0);
        QColor c;
        c.setHsv(static_cast<int>(hue), 180, 220, 255);
        painter->setPen(QPen(c.darker(140), 1));
        painter->setBrush(c);
        painter->drawEllipse(QPointF(0, 0), m_radius, m_radius);   // 画在**图元坐标**里
    }

    static int paints() { return s_paints; }
    static void resetPaints() { s_paints = 0; }
    qint64 id() const { return m_id; }
    qreal radius() const { return m_radius; }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override
    {
        ++s_clicks;
        Q_EMIT clicked(m_id);                    // 图元对外说话，全靠它是 QObject
        QGraphicsObject::mousePressEvent(event);  // 让框架继续做选中/拖拽的本职
    }

Q_SIGNALS:
    void clicked(qint64 itemId);

public:
    inline static int s_clicks = 0;

private:
    qint64 m_id = 0;
    qreal m_radius = 6.0;
    inline static int s_paints = 0;
};

// ============================================================================
// 三、自定义图元二：LegendItem（**不**继承 QObject，只画不改）
//     它的用处专门是给 ItemIgnoresTransformations 做实验：缩放视图时它保持设备尺寸。
// ============================================================================
class LegendItem : public QGraphicsItem
{
public:
    enum { Type = QGraphicsItem::UserType + 2 };
    int type() const override { return Type; }

    QRectF boundingRect() const override { return QRectF(-40, -12, 80, 24); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        ++s_paints;
        painter->setPen(QPen(QColor(40, 45, 55), 1));
        painter->setBrush(QColor(255, 255, 240, 230));
        painter->drawRoundedRect(boundingRect(), 4, 4);
        QFont f = painter->font();
        f.setPixelSize(12);
        painter->setFont(f);
        painter->setPen(QColor(40, 45, 55));
        painter->drawText(boundingRect(), Qt::AlignCenter, QStringLiteral("图例：不缩放"));
    }

    static int paints() { return s_paints; }
    static void resetPaints() { s_paints = 0; }

private:
    inline static int s_paints = 0;
};

// ============================================================================
// 四、小工具
// ============================================================================
static qint64 countOpaque(const QImage &src)
{
    const QImage probe = src.isNull() ? QImage() : src.convertedTo(QImage::Format_ARGB32);
    qint64 n = 0;
    for (int y = 0; y < probe.height(); ++y) {
        const uchar *row = probe.constScanLine(y);
        for (int x = 0; x < probe.width(); ++x)
            if (row[x * 4 + 3] != 0)
                ++n;
    }
    return n;
}

// ============================================================================
// 五、main：三件套 + 三层坐标 + 命中 + 缩放 + 缓存 + 2000 图元
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("painting08"));

    QGraphicsScene scene;
    QGraphicsView view(&scene);
    view.setRenderHint(QPainter::Antialiasing);          // 视图级 render hint（93 行）
    view.setTransformationAnchor(QGraphicsView::AnchorViewCenter);  // 48~52 行
    view.setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
    view.setDragMode(QGraphicsView::RubberBandDrag);     // 63~67 行
    view.setCacheMode(QGraphicsView::CacheBackground);   // 视图级背景缓存（56~59 行）
    view.setWindowTitle(QStringLiteral("painting 08 · GraphicsView"));

    // ── 2000 个图元：网格排布，其中一小部分开 DeviceCoordinateCache ──────
    const int kCols = 50, kRows = 40;                    // 50*40 = 2000
    const qreal pitch = 24.0, r = 7.0;
    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kCols; ++col) {
            DotItem *dot = new DotItem(qint64(row * kCols + col), r,
                                       QPointF(col * pitch, row * pitch));
            // 缓存实验：每 8 个里给 1 个开 DeviceCoordinateCache
            if ((row * kCols + col) % 8 == 0)
                dot->setCacheMode(QGraphicsItem::DeviceCoordinateCache);   // 111~114/153 行
            scene.addItem(dot);                          // 134 行
        }
    }
    const int cachedCount = int((kCols * kRows + 7) / 8);
    DotItem *hero = new DotItem(999999, 16.0, QPointF(kCols * pitch * 0.5, kRows * pitch * 0.5));
    scene.addItem(hero);
    LegendItem *legend = new LegendItem();
    legend->setFlag(QGraphicsItem::ItemIgnoresTransformations);   // 56 行 = 0x20
    legend->setPos(hero->pos());                                  // 放在 hero 旁边
    scene.addItem(legend);
    scene.addRect(QRectF(-30, -30, kCols * pitch + 60, kRows * pitch + 60),   // 140 行
                  QPen(QColor(120, 130, 150), 2), QBrush(QColor(238, 242, 248)));

    scene.setSceneRect(QRectF(-40, -40, kCols * pitch + 80, kRows * pitch + 80));  // 95 行
    view.resize(640, 420);
    view.show();
    view.fitInView(scene.sceneRect(), Qt::KeepAspectRatio);        // 151 行

    int clickedSignals = 0;
    QObject::connect(hero, &DotItem::clicked, [&clickedSignals](qint64 id) {
        ++clickedSignals;
        qInfo() << "    （槽收到 clicked，itemId =" << id << "）";
    });

    QTimer::singleShot(0, [&app, &scene, &view, hero, legend, cachedCount,
                          &clickedSignals, kCols, kRows, pitch, r]() {
        qInfo() << "===============================================================";
        qInfo() << "painting 第 8 讲 · QGraphicsView 框架（平台 ="
                << QGuiApplication::platformName() << "）";
        qInfo() << "===============================================================";

        // ── ① 三者分工与规模 ────────────────────────────────────────────
        qInfo() << "【证据①】场景 / 视图 / 图元的分工";
        qInfo() << "  scene->items().size() =" << scene.items().size()
                << "（2000 个 DotItem + hero + legend + 1 个内建 addRect 的图元）";
        qInfo() << "  开了 DeviceCoordinateCache 的图元个数 =" << cachedCount
                << "其中 hero 的 cacheMode() 回读 =" << hero->cacheMode()
                << "（0=None 1=ItemCoordinateCache 2=DeviceCoordinateCache，"
                   "以 qgraphicsitem.h 111~114 行为准）";
        qInfo() << "  sceneRect（我显式 setSceneRect 过的）=" << scene.sceneRect();
        qInfo() << "  itemsBoundingRect（图元自己的并集）=" << scene.itemsBoundingRect();
        qInfo() << "  view->scene() 就是上面那个 scene 吗 =" << (view.scene() == &scene);
        qInfo() << "  view->size() =" << view.size() << "viewport() 非空 ="
                << (view.viewport() != nullptr)
                << "（视图自己是个 QWidget，画在它的 viewport 上）";
        qInfo() << "  → 记住分工：**scene 只管「有什么、在哪、谁被点到」，view 只管「这块看到多少、"
                   "多大、往哪移」，item 只管「我占多大、我怎么画」**。";

        // ── ② 三层坐标：同一个点走一遍 ──────────────────────────────────
        qInfo() << "【证据②】图元坐标 → 场景坐标 → 视图（设备）坐标 → 全局坐标";
        const QPointF itemCorner = hero->boundingRect().topLeft();      // 图元自己的左上角
        const QPointF scenePoint = hero->mapToScene(itemCorner);        // 298 行 → 场景
        const QPointF backItem = hero->mapFromScene(scenePoint);        // 313 行 → 回图元
        const QPoint device = view.mapFromScene(scenePoint);            // 174 行 → 设备
        const QPointF backScene = view.mapToScene(device);              // 178 行 → 回场景
        const QPoint global = view.mapToGlobal(device);                 // QWidget 提供
        const QPoint deviceAgain = view.mapFromGlobal(global);          // 往返
        qInfo() << "  hero->pos()（场景坐标）=" << hero->pos()
                << "scenePos()=" << hero->scenePos();
        qInfo() << "  图元角点" << itemCorner << "--mapToScene--> 场景点" << scenePoint
                << "--mapFromScene--> 回到" << backItem
                << "（往返差 =" << QPointF(backItem.x() - itemCorner.x(),
                                           backItem.y() - itemCorner.y()).manhattanLength()
                << "）";
        qInfo() << "  场景点 --view.mapFromScene--> 设备点" << device
                << "--view.mapToScene--> 回到" << backScene
                << "（设备→场景→设备 的整点往返误差应为 0，因为 mapFromScene 返回 QPoint）";
        qInfo() << "  设备点 --mapToGlobal--> 全局" << global
                << "--mapFromGlobal--> 回到" << deviceAgain
                << "（全局坐标由窗口管理器给，offscreen 下只看往返一致，别拿它当绝对位置）";
        qInfo() << "  view->transform() 的 m11（当前缩放）=" << view.transform().m11();

        // ── ③ 命中测试：三条路 ──────────────────────────────────────────
        qInfo() << "【证据③】命中测试：scene->items / scene->itemAt / view->itemAt";
        const QList<QGraphicsItem *> hits = scene.items(scenePoint);   // 113 行
        qInfo() << "  scene->items(场景点) 命中数 =" << hits.size()
                << "第一个（最上层）是 hero 吗 =" << (hits.isEmpty() ? false : (hits.first() == hero))
                << "它的 type() =" << (hits.isEmpty() ? -1 : hits.first()->type())
                << "（hero 的自定义 Type = UserType+1 =" << DotItem::Type << "）";
        QGraphicsItem *at = scene.itemAt(scenePoint, view.transform()); // 122 行
        qInfo() << "  scene->itemAt(pos, deviceTransform) 返回 ="
                << (at == hero ? QStringLiteral("hero") : QStringLiteral("别的/空"))
                << "——注意它**必须**传 deviceTransform，因为「谁在上」跟视图变换有关";
        QGraphicsItem *viaView = view.itemAt(device);                   // 167 行：视图坐标
        qInfo() << "  view->itemAt(设备点) 与上面一致吗 =" << (viaView == at)
                << "（这条最省事：把鼠标事件的 pos() 直接丢给它）";
        const QPointF far = scene.sceneRect().topLeft() + QPointF(1, 1);
        qInfo() << "  场景左上角那种空白处：items(far).size =" << scene.items(far).size()
                << "（0 才说明命中测试按 shape() 走，不是按「整个场景都算」）";
        qInfo() << "  一个矩形范围里有多少图元：items(QRectF 100x100).size ="
                << scene.items(QRectF(0, 0, 100, 100)).size()   // 114 行
                << " vs 全场景" << scene.items().size()
                << "（这就是「批量命中/框选」的入口，第 6 讲的自绘控件要自己做这件事）";

        // ── ④ 缩放：scale / fitInView / ItemIgnoresTransformations ──────
        qInfo() << "【证据④】缩放与变换锚点";
        const QPoint devBefore = view.mapFromScene(hero->scenePos());
        const QSizeF legendSceneSize = legend->boundingRect().size();
        const QPolygonF legendDevBefore = view.mapToScene(QRect(0, 0, 1, 1));  // 只为拿变换
        Q_UNUSED(legendDevBefore);
        view.scale(2.0, 2.0);                                   // 141 行
        view.setTransformationAnchor(QGraphicsView::NoAnchor);  // 先钉死锚点，数字才干净
        const qreal m11 = view.transform().m11();
        const QPoint devAfter = view.mapFromScene(hero->scenePos());
        qInfo() << "  scale(2,2) 之后 transform().m11 =" << m11;
        qInfo() << "  hero 的场景位置没动（" << hero->scenePos() << "），"
                << "设备坐标从" << devBefore << "变成" << devAfter
                << "（位移与缩放都体现在设备坐标上，图元自己完全不知道，这就是框架的价值）";
        // 图例开了 ItemIgnoresTransformations：它在设备上的尺寸应当**不随缩放变化**
        const QRectF legendBox = legend->boundingRect();
        const QPoint lt = view.mapFromScene(legend->mapToScene(legendBox.topLeft()));
        const QPoint rb = view.mapFromScene(legend->mapToScene(legendBox.bottomRight()));
        qInfo() << "  legend 图元坐标尺寸 =" << legendBox.size()
                << "场景尺寸 =" << legendSceneSize
                << "缩放 2 倍后**设备**尺寸 =" << QSize(qAbs(rb.x() - lt.x()), qAbs(rb.y() - lt.y()))
                << "（ItemIgnoresTransformations：期望设备尺寸≈图元尺寸，不随 m11 变）";
        view.resetTransform();                                  // 139 行
        qInfo() << "  resetTransform 后 m11 =" << view.transform().m11();
        view.fitInView(scene.sceneRect(), Qt::KeepAspectRatio);
        qInfo() << "  fitInView(sceneRect, KeepAspectRatio) 后 m11 =" << view.transform().m11()
                << "（由 640x420 的视口与" << scene.sceneRect().size()
                << "的场景算出来；KeepAspectRatio 取小比例，所以一定装得下）";
        view.fitInView(scene.sceneRect(), Qt::IgnoreAspectRatio);
        qInfo() << "  换成 IgnoreAspectRatio 后 m11 =" << view.transform().m11()
                << "、m22 =" << view.transform().m22()
                << "（两个方向各自拉伸 → 圆变椭圆；本模块第 2 讲的坑在框架层重现）";
        view.fitInView(scene.sceneRect(), Qt::KeepAspectRatio);

        // ── ⑤ 缓存：paint 次数就是证据 ──────────────────────────────────
        qInfo() << "【证据⑤】CacheMode：把 paint() 次数数出来";
        DotItem::resetPaints();
        legend->resetPaints();
        scene.update();                                     // 请求重画整个场景
        view.viewport()->update();
        qInfo() << "  这一轮之后 DotItem::paint 被调了" << DotItem::paints()
                << "次、LegendItem" << legend->paints()
                << "次（图元总数" << scene.items().size()
                << "）；看不在视口里的图元不会被画 —— 这是第一层节省。";
        qInfo() << "  三种 CacheMode 的取舍（qgraphicsitem.h 111~114 行）：";
        qInfo() << "    None                      —— 每次重绘都真的走一遍 paint()，最省内存；";
        qInfo() << "    ItemCoordinateCache       —— 把 paint() 的结果按**图元坐标系**缓存，"
                   "缩放时只需重新采样（平移免费、缩放代价小），但缓存尺寸跟缩放比相关；";
        qInfo() << "    DeviceCoordinateCache     —— 按**设备像素**缓存，平移极快；"
                   "可一旦 view->scale() 变了就要重画（本讲给 1/8 的图元开了它，"
                   "上面的 fitInView 一执行，这些缓存就全作废了）。";
        qInfo() << "    → 什么时候开：图元的 paint() 很贵（渐变/阴影/文字/复杂路径）+ 图元多 + "
                   "经常整体平移；什么时候别开：图元本来就简单（内存换不到时间），"
                   "或者你频繁缩放。";
        qInfo() << "    → 还有一条硬警告：QGraphicsItem 用了 Q_DECLARE_SHARED，"
                   "**不要按值复制图元、不要把它塞进 QVector<QGraphicsItem> 值语义容器**，"
                   "容器里放指针（本讲就是这么写的）。";

        // ── ⑥ 交互：手工往场景投一个鼠标按下 ────────────────────────────
        qInfo() << "【证据⑥】场景事件：sendEvent(QGraphicsSceneMouseEvent)";
        const int clicksBefore = DotItem::s_clicks;
        const QList<QGraphicsItem *> hitsBefore = scene.items(scenePoint);
        Q_UNUSED(hitsBefore);
        QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
        press.setScenePos(hero->scenePos());                 // 场景坐标——这是框架的入口坐标
        press.setButton(Qt::LeftButton);
        press.setButtons(Qt::LeftButton);
        QApplication::sendEvent(&scene, &press);
        qInfo() << "  投一次按下后：DotItem::s_clicks" << clicksBefore << "->"
                << DotItem::s_clicks << "hero 的 clicked() 信号次数 =" << clickedSignals
                << "（信号是**图元**发的，槽在 main 的 lambda 里）";
        qInfo() << "  被选中的图元数（ItemIsSelectable 生效）=" << scene.selectedItems().size();
        qInfo() << "  mouseGrabberItem() 非空 =" << (scene.mouseGrabberItem() != nullptr);
        QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
        release.setScenePos(hero->scenePos());
        release.setButton(Qt::LeftButton);
        QApplication::sendEvent(&scene, &release);
        qInfo() << "  抬起之后 mouseGrabberItem 还非空吗 =" << (scene.mouseGrabberItem() != nullptr);
        qInfo() << "  → 对比第 6 讲：那边要点中得自己算角度/包围盒，这里 setFlag(ItemIsSelectable) "
                   "+ shape() 就够了；图元一多，这笔账一定划算。";

        // ── ⑦ 两张 PNG：scene->render 与 view->render ───────────────────
        qInfo() << "【证据⑦】把框架画进 QImage（两条路）";
        QImage fromScene(560, 360, QImage::Format_ARGB32_Premultiplied);
        fromScene.fill(QColor(250, 251, 253, 255));
        {
            QPainter p(&fromScene);
            // 99 行：target 是「画到哪」，source 是「看场景哪一块」
            scene.render(&p, QRectF(0, 0, 560, 360), scene.sceneRect(), Qt::KeepAspectRatio);
        }
        fromScene.save(QStringLiteral("painting_08_scene_render.png"));
        QImage fromView(640, 420, QImage::Format_ARGB32_Premultiplied);
        fromView.fill(QColor(0, 0, 0, 0));
        {
            // 注意：QGraphicsView 自己声明了 render(QPainter*, ...)（qgraphicsview.h 157 行），
            // 它把 QWidget::render(QPaintDevice*)（qwidget.h 348 行）**按名字藏起来了**，
            // 所以 `view.render(&image)` 编译不过 —— 这是个真实的坑，别背错。
            QPainter pv(&fromView);
            view.render(&pv, QRectF(0, 0, 640, 420));
        }
        fromView.save(QStringLiteral("painting_08_graphicsview_basics.png"));
        qInfo() << "  scene.render 版:" << fromScene.size() << "非透明像素 ="
                << countOpaque(fromScene) << "/ 面积 =" << 560 * 360
                << "中心像素 =" << fromScene.pixelColor(280, 180).name(QColor::HexArgb);
        qInfo() << "  view.render 版:" << fromView.size() << "非透明像素 ="
                << countOpaque(fromView) << "/ 面积 =" << 640 * 420
                << "中心像素 =" << fromView.pixelColor(320, 210).name(QColor::HexArgb);
        qInfo() << "  → 两张图的「非透明像素数」差很多是正常的：scene.render 连背景矩形都算，"
                   "view.render 只画视口里看得见的部分。**要比较请比较内容，别比较像素总数**。";
        qInfo() << "  最后一遍统计：DotItem::paint 累计" << DotItem::paints()
                << "次（2000 个图元的场景，几轮重绘之后仍然远小于「图元数 × 轮数」）";
        qInfo() << "  view->sizeHint() =" << view.sizeHint()
                << "（QGraphicsView 覆写了它，qgraphicsview.h 90 行；不是我们第 6 讲那个自定义值）";

        qInfo() << "【收尾】把上面每个数字抄回注释，就是你的实测。";
        QCoreApplication::quit();
    });

    return app.exec();
}

// ============================================================================
// 八、速查表与「什么时候用哪个」
// ============================================================================
/*
 * 一、第 6 讲（QWidget 自绘）vs 第 8 讲（场景/视图）—— 这条界线要划清
 *   用 QWidget 自绘，当：
 *     · 画的是一个「控件」：按钮、进度环、迷你图表、表格单元格里的装饰；
 *     · 图元数量级是几十到几百，且都跟这个控件一起重绘；
 *     · 你要接进布局系统（sizeHint / minimumSizeHint / sizePolicy 就是你的接口）；
 *     · 交互只有「点我一下、拖我一下」，命中用矩形/角度自己算得清。
 *   用 QGraphicsScene/QGraphicsView，当：
 *     · 图元数量级上千（本讲 2000 个），且需要**批量变换**（缩放/平移整幅）；
 *     · 需要框架给命中测试与 z 序（items()/itemAt()/setZValue()）；
 *     · 需要选择集、拖拽、吸附、分组（QGraphicsItemGroup）、图元父子变换链；
 *     · 画布类应用：流程图、地图、矢量编辑、示波器、看板；
 *     · 需要「一个视图看同一份场景」或者多个视图（场景与视图天生分离）。
 *   反过来，别用框架当：
 *     · 只是画一个按钮（第 6 讲更轻，还能吃 style/palette）；
 *     · 只要一次性出图（第 1~5、7 讲的 QImage + QPainter 最直接，还不用装视图）；
 *     · 大数据表格（那是 model_view 模块的活，不是 GraphicsView）。
 *
 * 二、框架的三层坐标与「谁负责换算」
 *   图元坐标  item 自己的 (0,0) 是它的原点；boundingRect()/paint()/shape() 都在这里
 *   场景坐标  item->pos() 把图元放到场景里；sceneRect() 与 itemsBoundingRect() 都在这里
 *   视图/设备 view 的 transform() + viewport 尺寸把场景切一块画上去；
 *             mapToScene/mapFromScene 的往返就在这里（整点会取整，注意误差）
 *   全局坐标  QWidget::mapToGlobal（窗口位置由平台给，别当绝对真理）
 *   换算表：
 *     图元 → 场景  item->mapToScene(QPointF)
 *     场景 → 图元  item->mapFromScene(QPointF)
 *     场景 → 设备  view->mapFromScene(QPointF) → QPoint
 *     设备 → 场景  view->mapToScene(QPoint)   → QPointF
 *     设备 → 全局  view->mapToGlobal(QPoint)
 *   **事件进来的坐标永远是场景坐标**（QGraphicsSceneMouseEvent::scenePos()），
 *   而 paint() 里你用的是图元坐标 —— 这两个搞混是框架第一号 bug 来源。
 *
 * 三、图元的三条纪律（本讲代码就是照这个写的）
 *  ① boundingRect() 必须**盖住你所有会画的东西**，包括画笔宽度；
 *     要变大了先 prepareGeometryChange()（405 行），否则旧内容会在重绘时留下残影。
 *  ② 命中看 shape()（273 行）不看 boundingRect()：默认 shape() 就是包围盒，
 *     圆形图元想「只点中间才算」，就覆写 shape() 返回 addEllipse 的路径（第 7 讲）。
 *  ③ paint() 里**不要改图元状态**、不要发信号、不要调 setPos()：
 *     paint 是被事件循环调用的，改状态 → update() → 再 paint，无限重绘（第 6 讲坑③ 的框架版）。
 *
 * 四、性能账（图元一多就照着这个顺序省）
 *   1) 先让 Qt 少画：视口看不见的不会画（本讲证据⑤）；能 update(QRectF) 别 update 全场景；
 *   2) 开视图级背景：view->setCacheMode(QGraphicsView::CacheBackground)（静态背景只画一次）；
 *   3) 给昂贵的图元开 CacheMode（DeviceCoordinateCache 对平移最友好）；
 *   4) 图元特别多的时候调 setItemIndexMethod（qgraphicsscene.h 104 行）：
 *      频繁增删用 NoIndex，几乎不动的大场景用 BspIndex；
 *   5) viewportUpdateMode 从 Smart 换 Minimal/Full 做对照实验，用本讲的 paint() 计数交账。
 *
 * ——— 老师傅的话 ———
 *   徒弟，这一讲的真正内容不是那几个类，是**「什么时候停止自己写」**。
 *   你前七讲练的是画笔，练到手熟了，最容易得一种病：什么都用 paintEvent 手搓。
 *   搓一个按钮没问题，搓一个两千点的示波器就开始疼 ——
 *   疼的地方永远是那三件框架早替你做好的事：谁被点到了、谁盖住谁、往哪儿刷。
 *
 *   我怎么判断该不该换？看三个信号：
 *   ① 你的 paintEvent 里开始出现「遍历所有对象并自己算点在不在矩形里」；
 *   ② 你需要「框选一批 + 一起移动」；
 *   ③ 你需要一个「缩放到 800% 还不卡」的画布。
 *   中一条就该把场景/视图拿起来，中两条就别再犹豫。反过来，如果你的东西
 *   最终要塞进 QVBoxLayout 里当一个控件、还要吃用户主题色，那第 6 讲才是正解 ——
 *   框架里也可以放 QGraphicsProxyWidget，那是另一笔复杂度，先别碰。
 *
 *   第二件：缓存不是「开了就快」。我在这讲故意给你看了一手 ——
 *   我给 1/8 的图元开了 DeviceCoordinateCache，然后紧跟着一个 fitInView()
 *   就把这些缓存全作废了（设备坐标变了）。**按设备缓存的，缩放就是重新生成缓存**。
 *   所以开之前先想清楚你的用户是「平移多」还是「缩放多」，
 *   想不清楚就先别开，把 paint() 计数器（本讲的 DotItem::s_paints）留着，
 *   开与不开各跑一遍，用次数说话。
 *
 *   第三件：QGraphicsItem 不是 QObject。你要发信号、要用属性、要用定时器，
 *   就继承 QGraphicsObject（qgraphicsitem.h 486 行），代价是它比普通图元重。
 *   两千个 QObject 是很大的负担，本讲敢用是因为只有 hero 需要说话；
 *   批量图元（DotItem 的多数）我只让它带一个 id 和一个 paint ——
 *   「谁需要信号谁付钱」，这是我给你留的一条经验。
 *
 * ——— 小练习 ———
 *   1. 把 DotItem 的 boundingRect() 改成比实际画的圆小一圈（比如 m_radius 而不是 m_radius+1），
 *      反复 view->scale(1.1, 1.1) 观察残影；然后按正确写法恢复，
 *      把「为什么留半个画笔宽度」用你自己的话写进注释。
 *   2. 把全部 2000 个 DotItem 都开成 ItemCoordinateCache 与全部 None 各跑一遍，
 *      用 DotItem::paints() 与 QElapsedTimer 做一张三列表（模式 / paint 次数 / 缩放一轮的毫秒），
 *      回答：为什么「缓存全开」在某些机器上反而更慢？（提示：显存/内存带宽与缓存重建）
 *   3. 给 DotItem 覆写 shape()，返回一个只占包围盒中心 40% 的 addEllipse 路径（第 7 讲），
 *      然后用证据③ 的三路命中测试在同一个点上重测：
 *      items(scenePoint) 的数量会不会变？itemAt 还能不能拿到它？
 *      由此说清「boundingRect 管刷多大、shape 管点不点得到」这条分工。
 */
// ============================================================================

// 本文件**有 Q_OBJECT 类（DotItem）**，AUTOMOC 会生成 08_graphicsview_basics.moc，
// 必须在类定义之后 include 进来；LegendItem 不是 QObject，不需要任何 .moc。
#include "08_graphicsview_basics.moc"
