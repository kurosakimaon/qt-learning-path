# 第伍阶段 · 绘图与自定义控件（Qt 从入门到资深）

> 老师傅带你从「拿笔往哪儿画」走到「造一个别人能用的控件」 · 8 讲，每讲都吐出可核查的像素数字

## 一、模块简介

徒弟，到这一关，你终于要**自己画**了。

前四个阶段（起步 / 控件与布局 / 事件系统 / 元对象系统）里，界面是 Qt 给你的控件画好的；从这一关起，
「这个表盘」「这个波形」「这个卡片」都得你亲手落笔。Qt 的绘制体系其实只有两层话：

- **一层是「笔」**：`QPainter` 拿着一套状态（`QPen` 线、`QBrush` 面、`QFont` 字、`QTransform` 变换、
  裁剪与合成），把图元变成像素；这就是**第 1~4 讲 + 第 7 讲**。
- **一层是「布」**：像素落在什么上面？`QImage`（内存里的图，谁都能读写）、`QPixmap`（面向窗口系统的图，
  只为往屏幕上 blit 优化）、`QWidget` 本身（只许在 `paintEvent` 里画）；这就是**第 5 讲**，
  而**第 6 讲**把「笔 + 布」包成一个能放进布局里的**控件**，**第 8 讲**再告诉你图元一多就该换成
  `QGraphicsScene / QGraphicsView / QGraphicsItem` 这套现成框架。

本模块一共 **8 讲**，每讲都是一个完整可编译的 `.cpp`，都遵循同一条交付纪律：
**画进 `QImage` → `save("painting_0N_xxx.png")` → `qInfo()` 打印可核查的数字**（尺寸、非透明像素数、
采样到的像素值、耗时倍数），最后 `QCoreApplication::quit()`。**全程无人值守**，不用鼠标键盘，
主路径里没有任何阻塞调用（没有 `QDialog::exec()`，没有裸 `QEventLoop::exec()`，没有静态 `QMessageBox`）。

> 环境事实：Qt 6.12.0、Windows、MinGW-w64（MSYS2 ucrt64，gcc 15.2/16.2）、CMake ≥ 3.21 + Ninja、C++17。
> 本组 8 个目标统一链 `Qt6::Widgets Qt6::Svg`（见顶层 `CMakeLists.txt` 第 163~175 行，`QT_DEMO_LIBS` 里加了 Svg）。

**本模块的证据强度，师傅给你交底**：本机 Qt 运行时库版本不匹配，**链接不了、跑不起来**。
所以第 5~8 讲做到的核对是两层——① 每个类、函数、重载、枚举值都对着
`C:\msys64\ucrt64\include\qt6` 下的真实头文件查过，行号就写在用的地方；
② 每个文件都过了 `moc` + `g++ -fsyntax-only -std=c++17 -Wall -Wextra`，**零 error、零 warning**。
第 1~4 讲由同事同样按头文件核对过。但请注意：**「编得过」不等于「画面对」**——
所有 PNG、所有打印数字都还没被任何机器验证过，你到能编译的机器上跑一遍，把真实数字抄回注释里，
那才是你的实测。查不到签名的 API 一律不用，这条规矩本模块贯彻得很彻底（详见第六节）。

## 二、学习目标

学完本模块，你要能做到：

1. 说清 `QPainter` 的三条铁律（对控件作画只在 `paintEvent()` 内、painter 必须比设备早析构、
   RAII + `isActive()` 自检），并且**能离屏复用同一段绘制代码**（画进 `QImage` 出一张 PNG）
2. 把 `translate / rotate / scale` 的**顺序不可交换**讲给别人听，能用 `QTransform` 现场证明；
   会用 `save()/restore()` 状态栈和 `viewport()/window()` 映射解决「绕一点旋转」「逻辑坐标画工程图」
3. 用 `QFontMetrics` 的 `ascent / descent / height / horizontalAdvance` 把文字**对齐到基线**，
   并说清 Qt5→Qt6 那一处必考差异（`width()` 没了）
4. 分得清 `QImage / QPixmap / QBitmap` 各自能干什么、能在哪个线程用、字节怎么排
   （`bytesPerLine()` 里的填充、`Format_Indexed8` 的索引语义），并理解隐式共享与 `detach()` 的真实行为
5. 独立做出一个**可复用自定义控件**：覆写 `paintEvent / sizeHint / minimumSizeHint`、
   颜色取自 `palette()`、有鼠标交互、**重绘策略正确**（`update()` / `update(QRect)` / `repaint()` 用得其所，
   知道 `Qt::WidgetAttribute` 那几个开关各治什么病）
6. 用 `QPainterPath` + `Qt::FillRule` + 裁剪（矩形/路径/`QRegion`）+ 合成模式做出「非矩形、挖孔、蒙版、水印」
7. 判断得出「这个界面该自绘控件还是该上 `QGraphicsView` 框架」，并会搭场景：三层坐标换算、
   `fitInView/scale`、命中测试、`CacheMode` 调优

## 三、文件列表与学习重点

八讲，按顺序来，别跳：

```text
  01_painter_basics.cpp              ← 第 1 步 · QPainter 的三条铁律与基础图元
    重点：对 QWidget 作画只允许在 paintEvent() 里、对 QImage 随时可画（两条链同时出图互证）；
          painter 比设备早析构 + begin()/end() 与 RAII + isActive() 自检；
          点/线/矩形/圆角矩形/椭圆/圆弧/扇形弦/多边形八件套；
          逻辑坐标 vs 设备坐标：1x 与 2x 画布上同一份代码，着墨像素约差 4 倍

  02_coordinates_and_transforms.cpp  ← 第 2 步 · 坐标变换三件套与它们的顺序（硬坎一）
    重点：translate/rotate/scale **不可交换**，用 QTransform::map() 把同一端点在两种顺序下的
          最终设备坐标打印出来；save()/restore() 保存的是「变换+笔+刷+字+裁剪」整套栈；
          worldTransform()/setWorldTransform() 与 operator* 的组合方向判别；
          viewport()/window() 映射（画图表/工程图的正规做法）；
          「绕某点旋转」为什么要 translate→rotate→translate 回去；y 轴向下对角度正负的影响

  03_pen_brush_gradients.cpp         ← 第 3 步 · QPen 细部、QBrush 与三种渐变
    重点：cosmetic 画笔（线宽是否随变换缩放，scale(4,4) 下 4px vs 1px 实测）；
          cap/join 用着墨包围盒量出来；setDashPattern 的数字是**线宽倍数**；
          纹理画刷的纹理由代码生成的 QImage 提供（不依赖外部图片）；
          QLinear/QRadial/QConical + setColorAt/setSpread/setCoordinateMode，
          LogicalMode 与 ObjectBoundingMode 五个采样点对照；
          Qt6 没有 QtGui/qgradient.h 这份实现头（渐变声明在 qbrush.h 第 157 行起）

  04_text_and_font_metrics.cpp       ← 第 4 步 · 文本度量与基线
    重点：setPixelSize ≠ setPointSize（后者要按 dpi/72 换算，18pt 在 96dpi 下是 24px）；
          ascent/descent/height/leading/lineSpacing/capHeight/boundingRect/tightBoundingRect/
          horizontalAdvance 逐项打印，并现场判定 height() 是否等于 ascent()+descent()；
          **Qt6 已删掉 QFontMetrics::width()**，一律改 horizontalAdvance()；fontDpi() 从 int 变 qreal；
          drawText 的对齐标志、QRect/QRectF 两个版本与 out 参数；elidedText（带 d）四档；
          WordWrap 的 flags 版 vs QTextOption 版；基线与矩形中心差半个行高

  05_images_and_pixmaps.cpp          ← 第 5 步 · 像素从哪来（QImage / QPixmap / QBitmap）
    重点：Format 决定字节布局，bytesPerLine() 含行对齐填充（宽×每像素字节数常常≠它）；
          constScanLine() 只读不 detach、非const scanLine() 会 detach（隐式共享的坑）；
          Format_Indexed8 用 setPixel(索引)/pixelIndex()/setColor/colorTable 的语义；
          QImage 可跨线程可读写像素 vs QPixmap 必须在 QGuiApplication 之后、只在 GUI 线程用；
          隐式共享用 isDetached() 翻转 + 「改一个另一个不动」双证（qimage.h 第 117~118 行，**没有 useCount()**）；
          scaled 的 Qt::AspectRatioMode 三档 × Qt::TransformationMode 两档 + Smooth 的实测倍率代价；
          QPixmapCache 的 cacheLimit/insert/find/remove/clear 与超限淘汰；
          save/load 往返（图全由代码生成，PNG/JPEG/BMP 三种字节数对照）；
          QImage::setDevicePixelRatio() 之后 QPainter 用的就是**逻辑坐标**（用着墨包围盒证实）；
          性能坑：paintEvent 里反复 QPixmap::fromImage()，本讲用计数器把它数出来

  06_custom_widget.cpp               ← 第 6 步 · 做一个能复用的自定义控件（硬坎二）
    重点：paintEvent/sizeHint/minimumSizeHint 三件套；颜色一律从 palette() 取（Button/Mid/ButtonText/
          Highlight/Window），换主题不改一行绘制代码；
          setMouseTracking + mousePress/MouseMove/MouseRelease/enterEvent(QEnterEvent*)/leaveEvent +
          setCursor 的交互闭环（角度→数值的反算）；
          **update() 只是请求、update(QRect) 只脏一块、repaint() 同步硬画**——10000 次 update() 的
          合并证据（同一轮内 paint 次数为 0，回到事件循环后合并成极有限的几次）；
          WA_OpaquePaintEvent / WA_NoSystemBackground / WA_TranslucentBackground 各治什么病（附回读）；
          **QWidget 上没有 setUpdateInterval()**（它在 QtCore/qtimeline.h 第 65 行，属于 QTimeLine），
          本讲给四条真能用的限流路线；
          QWidget::render(QPaintDevice*)（qwidget.h 第 348 行）把控件抓成 QImage 出证据

  07_painter_path_and_clipping.cpp   ← 第 7 步 · QPainterPath、裁剪与合成（高级绘制）
    重点：addRect/addEllipse/moveTo/lineTo/cubicTo/quadTo/arcTo/closeSubpath/addPolygon/addRoundedRect
          全走一遍，打印 elementCount 与逐元素 ElementType；
          Qt::FillRule 的 OddEvenFill 与 WindingFill 在自交五角星**中心像素**上的差别（qnamespace.h 1329~1332）；
          路径布尔是**成员函数** united()/intersected()/subtracted()/simplified()（qpainterpath.h 155~159），
          Qt4 那套 unitedPath(a,b) 自由函数在本机头文件里**查不到**；
          setClipRect/setClipPath/setClipRegion + setClipping/hasClipping/clipRegion 回读；
          **Qt::ClipOperation 只有 NoClip/ReplaceClip/IntersectClip**（qnamespace.h 1339~1343），
          想做「并集裁剪」请自己 QRegion::united() 再 setClipRegion；
          QPainter::CompositionMode_*（qpainter.h 98~118）做遮罩：DestinationIn/SourceIn/Clear/Source
          各自的非透明像素数与中心 ARGB，半透明蒙版另测一组；三张对照 PNG

  08_graphicsview_basics.cpp         ← 第 8 步 · 什么时候改用场景框架
    重点：scene（有什么、在哪、谁被点）/ view（看多少、多大、往哪移）/ item（我占多大、我怎么画）分工；
          2000 个自定义图元（DotItem : QGraphicsObject，末尾 #include "08_graphicsview_basics.moc"）；
          addItem/setRect/setSceneRect 与 sceneRect vs itemsBoundingRect；
          三层坐标：mapToScene/mapFromScene（item 与 view 都有，注意方向与参数类型）+ mapToGlobal，
          打印往返误差；命中测试三条路 scene->items(QPointF)/scene->itemAt(pos, transform)/view->itemAt(QPoint)；
          scale()/fitInView(QRectF, AspectRatioMode)/resetTransform + setTransformationAnchor，
          KeepAspectRatio 与 IgnoreAspectRatio 下 m11/m22 的差别（圆变椭圆那条坑在框架层重现）；
          QGraphicsItem::ItemIgnoresTransformations 让图例的设备尺寸不随缩放变；
          CacheMode 三档（None / ItemCoordinateCache / DeviceCoordinateCache，qgraphicsitem.h 111~114）
          用 paint() 次数当证据；prepareGeometryChange 与 shape() 的规矩；
          手工投递 QGraphicsSceneMouseEvent 看 selectedItems()/mouseGrabberItem() 的变化；
          两条出图路线：scene->render(QPainter*, target, source, mode)（第 99 行）与
          view->render(QPainter*, ...)（第 157 行，它**藏起来了** QWidget::render(QPaintDevice*)）
```

> 为什么第 6、8 讲多了个 moc 步骤：这两讲里有 `Q_OBJECT` 类（`GaugeWidget`/`Throttle`/`DotItem`），
> 类定义写在 `.cpp` 里，所以末尾必须 `#include "06_custom_widget.moc"` / `#include "08_graphicsview_basics.moc"`。
> 第 1、2、3、4、5、7 讲只覆写 `paintEvent()`、不需要元对象，**没有 Q_OBJECT，也就不需要 .moc**。

## 四、学习顺序建议

师傅给你划这条主线和两道硬坎。

### 主线：为什么是这个顺序

- **第 1 → 第 4 讲：Painter 的地基（图元 / 变换 / 画笔 / 文字）**。
  第 1 讲立规矩、认图元；第 2 讲给这套图元装上「坐标系」这个变量（不然一切位置都是死的）；
  第 3 讲讲「线怎么描、面怎么填」；第 4 讲讲「字怎么摆」——文字是最常用的图元，也是最容易差一点点的图元。
  这四讲是一件事的四个侧面，缺一个，第 6 讲做出来的控件就一定有地方别扭。
- **第 5 讲：像素从哪来**。前四讲一直在「拿笔往哪儿画」，这一讲回答另一半：**画布本身**。
  不懂 `QImage`/`QPixmap` 的分工与隐式共享，你写出的代码会在子线程崩、在 `paintEvent` 里慢。
- **第 6 讲：把绘制包成控件**。这是本模块的**交付形态**——前面五讲的本事变成一个类，
  别人能 `addWidget()` 进布局、能 `setPalette()` 换肤、能拖动能点得中。
  这一讲也是把「绘制」和「事件系统（第叁阶段）」「尺寸协商（第贰阶段）」两根线**打结**的地方。
- **第 7 讲：高级绘制**。路径 + 填充规则 + 裁剪 + 合成，做「圆角挖孔卡片」「异形按钮」「水印」这类
  一眼看上去「不像标准控件」的东西。它需要第 2 讲的变换和第 5 讲的像素概念垫底才不晕。
- **第 8 讲：什么时候改用场景框架**。别再往深处自绘了，看看 Qt 给你的现成框架：
  坐标系统、命中测试、缩放平移、批量重绘优化都替你做好了。这一讲的真正用途是**给你一把「拐弯」的尺子**。

### 硬坎一：第 2 讲的变换顺序

`translate/rotate/scale` 是**矩阵左乘**，写下的先后顺序直接决定结果，背结论必错。
师傅的办法是**别背**：把同一个端点在 A→B 和 B→A 两种顺序下的最终设备坐标打印出来对比
（第 2 讲就是这么做的，`painting_02_order_ab.png` / `_ba.png` 两张图也是这么来的）。
看见「同一个点算出两个位置」那一刻，这个坎就过了。附带两个实务问题一起解决：
绕某点旋转为什么要 `translate→rotate→translate 回去`；「y 轴向下」为什么让角度正负看着相反。

### 硬坎二：第 6 讲的重绘策略

这是本模块**最贵**的一道坎，因为它错了不报错、只变丑变卡。三件事必须一次搞清楚：

1. `update()` 是**请求**，`repaint()` 是**命令**；请求会被事件循环合并，命令会绕过合并、还会递归。
2. 脏区：`update(QRect)` / `update(QRegion)` 只让局部重画，`paintEvent` 里
   `event->region()` 能读到今天到底脏了哪几块（第 6 讲打印了 dirty bbox 与 rectCount）。
3. 属性开关：`WA_OpaquePaintEvent`（我保证铺满整个控件，别替我擦背景）、
   `WA_NoSystemBackground`（不要系统背景）、`WA_TranslucentBackground`（我要能透出后面的东西，
   圆角/异形窗必备）。用 `testAttribute()` 回读，别猜。

第 6 讲给的证据是硬数字：**在同一个槽里连调 10000 次 `update()`，这期间 `paintEvent` 一次都不会被调用**
（事件循环还被你的函数堵着，这正是第叁阶段讲的「槽里别干重活」）；等控制权回到事件循环，
这一万个请求被**合并**成极有限的几次重绘。把这个数字看进眼里，你这辈子就不会再去 `paintEvent` 里
调 `repaint()` 了。

### 学习方法

每个文件都按这条流程走：读顶部注释块（本讲解决什么问题 + 本机头文件签名清单）→
按注释里的 CMake 目标名构建（如 `painting_06_custom_widget`）→ 跑一遍（`-platform offscreen` 也行）→
**去 build/bin 打开 PNG，再对照 `qInfo()` 打印的数字**→ 改代码做文件末尾那三个小练习 →
改出问题了试着修。光看不练，等于白看；只看不跑，你永远不知道自己的机器上高 DPI 是几倍。

> 🎬 想看「update() → 事件循环合并 → paintEvent → 后备存储 → 平台合成 → 屏幕」这条链的分步动画：
> [图解 · 绘制流水线](../visuals/painting_pipeline.html)；
> 想看「图元坐标 / 场景坐标 / 视图（设备）坐标 / 全局坐标」与「变换顺序不可交换」的动画：
> [图解 · 三层坐标](../visuals/coordinates.html)。

## 五、核心知识点梳理

### 1. 一笔一画是怎么走到屏幕上的（本模块最重要的一条链）

```cpp
value = qBound(0.0, value, 1.0);   // ① 状态变了
update(QRect(0, h - 20, w, 20));   // ② 只说「这块脏了」，函数立刻返回——它不是命令
```

后面发生的事，一环扣一环（配合 [图解 · 绘制流水线](../visuals/painting_pipeline.html) 看）：

```text
① update() / update(QRect) —— 只是往事件队列投递一个「重绘请求」，记录脏区，立即返回
        ↓
② 事件循环（app.exec() 里） —— 同一轮里同一控件的多个请求被**合并**成一个 QPaintEvent，
   它的 region() 是各脏区的并集；这就是「10000 次 update() 只重绘几次」的真相
        ↓
③ 派发 QPaintEvent → QWidget::paintEvent(event) —— 你在里面 QPainter p(this)
   （规矩：painter 是本地的、函数返回即析构；对 QWidget 作画**只能**在这里）
        ↓
④ QPainter 把图元交给绘制后端（raster / 平台引擎），落到这块控件的**后备存储**
   （backing store：一块与顶层窗口等大的内存位图，Qt 替你维护）
        ↓
⑤ 平台合成：QPA 后端把 backing store 里脏掉的那块贴到屏幕上，窗口管理器再把它合成进桌面
        ↓
⑥ 你眼睛看到的像素 —— offscreen 平台同样走 ②③④，只是最后一步落到内存，所以无头也能出证据
```

三个必须记住的推论：

- **`update()` 之后画面不会立刻变**（要等 ②③）；要「现在就画」只有 `repaint()`，但它会绕过合并、
  会在 `paintEvent` 里递归——所以本模块的规矩是：**默认永远 `update()`，范围已知就 `update(QRect)`**。
- **脏区决定画多少**。整个窗口 1000×800，你只改了底部 20 高的条，`update(QRect)` 让 Qt 只重画那一条；
  裸 `update()` 就把整块都标脏。第 6 讲把 `lastRectCount()` 和 dirty bbox 打印出来给你看差别。
- **合并发生在「一轮事件循环」这个粒度上**。你在一个槽函数里循环调 `update()`，
  这期间一次 paint 都不会有（②还没轮到）；把限流做成「一轮最多一次」的正规办法，
  是**自己用一个 pending 标志 + 单次 `QTimer`**（第 6 讲的 `Throttle` 类就是这个东西）。
  注意：**`QWidget` 没有 `setUpdateInterval()`**——本机这个函数只在 `QtCore/qtimeline.h` 第 65 行，
  属于 `QTimeLine`，不少从 Qt4/Qt5 教程抄来的代码把它安在控件身上，那是假的。

### 2. 画布三兄弟：什么时候用哪块布

```cpp
QImage img(512, 512, QImage::Format_ARGB32_Premultiplied);   // 内存里的图：随时可画、可读像素、可跨线程
img.fill(Qt::transparent);
QPainter p(&img);                 // 铁律一：离屏设备**不限场合**
p.setRenderHint(QPainter::Antialiasing);
p.drawEllipse(...);
p.end();                           // 或让它出作用域自动析构；下面才能安全读 img 的像素
qInfo() << img.size() << img.bytesPerLine() << img.sizeInBytes();
img.save(QStringLiteral("painting_05_roundtrip.png"));       // 证据落盘

QPixmap pm(64, 64);                // ⚠ 必须在 QGuiApplication 之后创建，且只能在 GUI 线程用
pm.fill(QColor(0, 0, 0, 0));       // QPixmap 没有 (int,int,QColor) 构造函数——先建尺寸再 fill
QPainter(&pm).drawImage(...);      // 临时 painter：出这一句即析构，pm 才可用于后续
QImage back = pm.toImage();        // 想读像素？先转 QImage（这条转换很贵，别放 paintEvent 里循环做）
```

| 类型 | 定位 | 能读像素吗 | 线程 | 关键注意点 |
|---|---|---|---|---|
| `QImage` | 内存中的图，格式可控 | 能（`pixelColor/setPixelColor/scanLine`） | 任意线程 | `bytesPerLine()` 含行对齐填充；非 const `scanLine()` 会 detach |
| `QPixmap` | 面向窗口系统的图（为 blit 优化） | **不能** | **只能 GUI 线程**，且必须 app 之后 | 高 DPI 用 `setDevicePixelRatio()`；`QPixmap(256,256,QColor)` 这种构造**不存在** |
| `QBitmap` | 一比特掩膜（`setMask` 用） | 名义上单色 | 同 QPixmap | Qt6 里从图造掩膜走 `QBitmap::fromImage()`；`(const QPixmap&)` 构造已废弃 |

隐式共享（implicit sharing / copy-on-write）只留两条可测行为：**赋值不复制像素**（`bits()` 相同、
`isDetached()` 为 false），**写操作触发 detach**（改一个另一个不受影响）。`useCount()` 在本机头文件里
**不存在**，所以谁也别在代码里调它——第 5 讲用「`isDetached()` 翻转 + 交叉读写」两条行为一起证明，
不靠无法核对的 API 说话。

### 3. 变换顺序与三层坐标

```cpp
p.save();
p.translate(100, 100);      // 与下面的顺序交换，结果不同：矩阵是左乘
p.rotate(30);
p.scale(2, 2);
p.drawRect(QRectF(-20, -20, 40, 40));
p.restore();                // restore 收回的不只是变换：pen/brush/font/clip 一起回来
```

- 「绕某一点旋转」= `translate(cx,cy) → rotate(a) → translate(-cx,-cy)`，中间那句才是旋转。
- 「画图表/工程图」用 `viewport()/window()` 把逻辑坐标和设备矩形解耦，别手算 scale。
- 到第 8 讲，同一件事换了名字：图元坐标 →（item 变换）→ 场景坐标 →（view 变换）→ 视图/设备坐标
  →（窗口位置）→ 全局坐标。换算 API 成对出现：`mapToScene / mapFromScene`（`QGraphicsItem`
  和 `QGraphicsView` 都有，但**参数与返回类型不同**：view 侧是 `QPoint↔QPointF`，item 侧是
  `QPointF↔QPointF`），再加 `mapToGlobal / mapFromGlobal`。想验证自己有没有搞混，就把同一个点
  走一趟往返、打印误差（第 8 讲证据②就是这么干的）。

### 4. 路径、裁剪、合成：三件各管一件事

```cpp
QPainterPath path;
path.addRoundedRect(QRectF(0, 0, 200, 80), 16, 16);
path.addEllipse(QRectF(150, 50, 60, 60));
path.closeSubpath();
path.setFillRule(Qt::WindingFill);       // 或 OddEvenFill：自交处填不填，靠它决定

QPainter p(&sheet);
p.setClipPath(path);                     // 裁剪：后面的绘制只露出这块区域
p.setCompositionMode(QPainter::CompositionMode_DestinationIn);  // 合成：新像素怎么和已有像素混
p.drawImage(0, 0, mask);                 // DestinationIn：留下「已有像素 ∩ 新像素的 alpha」
```

三条纪律（都在第 7 讲实测过）：

- **填充规则要看自交**：五角星中心，`OddEvenFill` 空、`WindingFill` 实。中心像素 alpha 就是判据。
- **裁剪没有「并集」这个操作**：`Qt::ClipOperation` 只有 `NoClip / ReplaceClip / IntersectClip`
  （`qnamespace.h` 1339~1343）。想要「两块矩形都能露出来」，先 `QRegion a; QRegion b; a.united(b)`
  再 `setClipRegion()`。想「从裁剪里去掉一块」，用 `QRegion::subtracted()`。
- **合成模式改的是「怎么混」，不是「画在哪」**：`Source*` 系（不理会已有像素）、`Destination*` 系
  （以已有像素为主体去取新像素的一部分）、`Clear`（擦掉）、`Xor / Plus / Multiply / Screen / Darken /
  Lighten`（混合）。做蒙版最常用 `CompositionMode_DestinationIn`，第 7 讲把每种模式的
  非透明像素数与中心 ARGB 都打印出来，跑一遍就有手感。

### 5. 自绘控件还是场景框架：这把尺子随身带

| 需求 | 第 6 讲（自绘 QWidget） | 第 8 讲（Scene/View） |
|---|---|---|
| 图形数量 | 几十~几百，绘制逻辑全由你掌握 | 上千、上万（本讲 2000 个图元） |
| 要不要命中测试 / 选择 / 拖拽 | 自己算包围盒、自己记选中态 | `shape()/itemAt()/selectedItems()` 白送 |
| 要不要缩放平移 / 多视图看同一份数据 | 自己维护变换与滚动条 | `fitInView/scale/centerOn` + 多 view 共享一个 scene |
| 要不要和布局/样式表无缝贴合 | 天然就是个 QWidget，最省事 | 得把 view 塞进布局，控件级细节要走 viewport |
| 尺寸协商 | 自己写 `sizeHint/minimumSizeHint` | view 有默认 `sizeHint()`，场景用 `setSceneRect` 表达 |

经验结论：**一个「表盘/波形/卡片」用第 6 讲；一个「可缩放可框选的图/地图/流程图/大量图元」用第 8 讲。**
真上了框架，性能顺序也别搞反（第 8 讲证据⑤的三档 `CacheMode` 就是这条）：
① 让 view 只画看得见的一部分（框架默认就做）；② `BrectViewportUpdate/SmartViewportUpdate` 选对
`ViewportUpdateMode`；③ 昂贵图元开 `ItemCoordinateCache`（缩放频繁）或 `DeviceCoordinateCache`
（频繁平移、少缩放）；④ `QPixmapCache::setCacheLimit()` 留够预算（第 5 讲把 `cacheLimit()` 的原值先打印出来、再调小到 64KB
去逼出「超限淘汰」；Qt 文档口径默认约 10MB，**以你机器上打印出来的那个数为准**）；
⑤ 再不够就分图层、降精度。**别一上来就「全开缓存」，2000 个图元各缓存一张位图，先崩的是内存。**

## 六、常见坑与注意事项

### 1. 在 `paintEvent()` 之外对着控件作画

```cpp
QPainter p(this);          // ✗ 构造函数里/槽函数里这么写
p.drawLine(...);
```

对着 `QWidget` 作画**只允许发生在 `paintEvent()` 里**（第 1 讲铁律一）。外面画，轻则什么都不显示、
`isActive()` 返回 false，重则和平台的 backing store 抢时机，出现「画了又被擦掉」的闪烁；
`Qt::WA_PaintOnScreen` 一类的配置还会让它直接不可用。
**要随时画，就画到 `QImage`/`QPixmap` 这类离屏设备上去**——这也是本模块每条证据都走 `QImage` 的原因。
需要「主动把控件抓成图」，用合法出口：`QWidget::render(QPaintDevice*)`（`qwidget.h` 第 348 行）。

### 2. painter 活得比它的设备久

```cpp
QImage *img = new QImage(200, 200, QImage::Format_ARGB32);
QPainter *p = new QPainter(img);
delete img;        // ✗ 设备先没了，painter 还握着它 → 未定义行为
```

规矩：**painter 必须比它正在画的那个设备先析构**。在 `paintEvent` 里用栈对象自然满足；
手动 `begin()/end()` 的，`end()` 之后再销毁设备；同一个 painter 想换设备，先 `end()` 再
`p.begin(&other)`。写完不放心就打印 `p.isActive()` 自检——它是你唯一可靠的判断手段。

### 3. 在 `paintEvent()` 里改状态，自己造出无限重绘

```cpp
void paintEvent(QPaintEvent *) {
    QPainter p(this);
    m_phase += 0.05;              // ✗ 绘画函数里改了影响画面的状态
    drawWave(p, m_phase);
    update();                     // ✗ 于是本函数又请求了一次重绘 → 每帧都脏，永远不停
}
```

`paintEvent` 应当是**纯函数**：读状态 → 画出去 → 不写回。动画推进交给 `QTimer`（或
`QVariantAnimation`、第 8 讲的 `QTimeLine`），由它改状态后再 `update()`。第 6 讲把 `paintEvent` 的调用次数做成了计数器（`paints()` / `fullPaints()` / `partialPaints()`，
收尾那句「整轮跑完，paintEvent 总次数」就是它）。你在这套骨架上亲手加一句 `m_phase += 0.05; update();`
就立刻现形：这个数字会一路涨到程序被杀掉为止，风扇启动。第 6 讲速查表的纪律③写的就是这件事——
**先让画面正确，再让重绘变少，最后才谈画得快**。附带后果：如果尺寸/边距一类「参与布局协商」的量也被
这条链改动，还会连着触发 `updateGeometry()`，界面就开始抖。

### 4. 把 `QPixmap` 拿到别的线程去用

```cpp
QtConcurrent 里：QPixmap pm(256, 256);     // ✗ 面向窗口系统的图，只能在 GUI 线程创建/使用
```

`QPixmap` 依赖 QPA 与窗口系统资源：**必须在 `QGuiApplication` 构造之后创建，且只能在 GUI 线程用**。
后台线程要出图，就生成 `QImage`（线程安全、可随机读写），搬到 GUI 线程后再
`QPixmap::fromImage()` 转一次（这个转换本身不便宜，转完存起来复用）。
第 5 讲的 `ImageBoard` 就把 `fromImage` 只调一次并计数，专门跟「在 `paintEvent` 里反复转换」的
错误写法对照——后者会把这个昂贵的转换变成每帧一次，帧率直接跪。

### 5. 忽略高 DPI：数字都算在 96dpi 上

一台 200% 缩放的笔记本上，`devicePixelRatio()` 是 2.0（`QPaintDevice::devicePixelRatioF()`，
`qpaintdevice.h` 第 52~53 行）。三处最容易漏：

- **别硬编码像素与点的换算**。文字大小用 `setPixelSize`（明确像素）或让 Qt 按 dpi 换算 `setPointSize`，
  二者混用就会在 2x 屏上「字小得离谱」。想要确定数字，就 `QFontMetrics(font, *this)` 显式指定设备。
- **离屏出图带 DPR**。`QImage` 画完 `img.setDevicePixelRatio(2.0)`（`qimage.h` 第 194 行），
  之后 `QPainter` 用的就是**逻辑坐标**：同一份绘制代码在 400×400 的 DPR=2 画布上，着墨包围盒是
  100×100 画布的约 2 倍、面积约为 4 倍——第 5 讲把这两个比值打印出来了。
- **`QPixmap` 也要标 DPR**：`QPixmap::setDevicePixelRatio(2.0)`（`qpixmap.h` 第 68 行），
  否则 2x 屏上按逻辑尺寸贴出来就是糊的。

### 6. 各自算文字高度 → 同排基线漂移

```cpp
// 控件 A：拿 height()；控件 B：拿 ascent()+descent()；标签 C：直接按中心对齐
QRectF r = boundingRect(...);   r.center() 用来定位文字  // ✗ 看着都合理，放一起就错位
```

同一条水平线上的多个控件/多段文字，**必须按同一条基线（baseline）对齐**：
以 `y = box.top() + fontMetrics.ascent()` 落笔，或用 `QFontMetrics::height() / leading / lineSpacing`
统一算行高；水平排布要宽度就用 `horizontalAdvance()`（**Qt6 已没有 `QFontMetrics::width()`**）。
第 4 讲把「我以为的居中」与「真实着墨重心」的 y 差打印出来，那条差值差不多就是半个行高——
这就是「控件放一起文字高低不齐」的真凶。字体族也别硬编码：不同机器上字族不存在，度量数字整套变掉。

### 7. `repaint()` 当同步刷新使

`repaint()` 立即画、绕过合并、在 `paintEvent` 里递归调用它会栈溢出，而且会把「一帧只画一次」的保护
全部打掉。只在「你必须马上拿到屏幕状态」的极少数调试场景用，业务代码用
`update()` / `update(QRect)` / `update(QRegion)`。想「限速」不要靠 `repaint` 计数，
靠**状态变更时合并请求 + 单次 `QTimer`**（第 6 讲 `Throttle`）；另外别在拖拽里同时改
`minimumSize` 又 `updateGeometry()` 又 `repaint()`，那是三重奏级别的卡。

### 8. 裁剪与路径上的三个具体错觉

- **以为 `setClipRect` 可以累加成并集**：不能。`Qt::ClipOperation` 没有 `UnionClip`，
  默认 `ReplaceClip`（第 2 个参数是 `ClipOperation`）；`IntersectClip` 只会越裁越小。并集请自备
  `QRegion::united()`。
- **以为路径布尔有 `unitedPath(a, b)`**：那是 Qt4 的自由函数，本机 `qpainterpath.h` 里
  只有成员函数 `united()/intersected()/subtracted()/simplified()`（第 155~159 行）与 `| & -` 运算符。
- **忘了 `boundingRect()` 是框架的命根**：`QGraphicsItem::boundingRect()` 是纯虚函数（第 270 行），
  报小了图元会被裁掉/留残影，报大了白白重画一大片；**动了几何之前必须先
  `prepareGeometryChange()`**（第 405 行），不然旧轮廓的像素留在屏幕上。

## 七、老师傅的经验之谈

### 1. 「能离屏跑的绘制代码」才叫代码

师傅判你这段绘制代码合不合格，只看一件事：**能不能把它画进 `QImage` 出一张 PNG**。
能——它就能被自动化测试、能被复用、能被缓存、能在别的线程预备；不能——它就被 `paintEvent` 绑架，
你连「它画得对不对」都只能靠肉眼在 60fps 里抓。所以本模块每个示例都长这样：

```cpp
void paintGauge(QPainter &p, const QRectF &box, const QPalette &pal, double value, bool hover);
```

主体是个**自由函数**，`paintEvent` 只是它的两个调用者之一（另一个是 `QImage`）。
第 6 讲最后两张 PNG 就是同一段代码的两条出口：`painting_06_custom_widget.png` 由
`QWidget::render(&image)` 抓控件，`painting_06_offscreen_same_code.png` 直接调 `paintGauge()`
画进 `QImage`，两张图的非透明像素数都被打印出来做对照
（离屏那张固定 `value = 0.72 / hover = false`，所以要比的是「同一套绘制逻辑出得来同一种东西」，
不是逐像素相等）——这就是师傅给你的验收标准。

### 2. 数字优先于眼睛

「看着挺圆」不是证据，「非透明像素数 = 12566、中心 ARGB = #ff2b7de0」才是证据。
本模块每条结论后面都跟着一个可打印的量：包围盒比值、`bytesPerLine` 表、`elementCount`、
`clipRegion().rectCount()`、`paint()` 次数。你以后做 GUI 自动化测试也照这个思路走——
**能变成数字的断言，才是能进 CI 的断言。**

### 3. 不确定就别写进代码

这一条是「无谬误」三个字的落地。本模块抓出来的假 API 全在注释里明写着，你记住这四个就够本：
`QImage::useCount()` 不存在（用 `isDetached()`）、`QWidget::setUpdateInterval()` 不存在
（那是 `QTimeLine` 的）、`Qt::UnionClip` 不存在（`ClipOperation` 只有三个值）、
`unitedPath()/intersectedPath()` 在本机头文件里查不到（是成员函数）。
还有 `QPixmap` 没有 `(int,int,QColor)` 构造函数、`QPainter::HighQualityAntialiasing` 在 Qt6 已没、
`QFontMetrics::width()` 被 `horizontalAdvance()` 取代、Qt6 没有 `QtGui/qgradient.h`。
**查不到签名的一律不用，宁可用更简单的替代写法。**写 Qt 和做人一样：不能凭记忆。

### 4. 高 DPI、暗色主题、字体族，是「资深」和「会用」的分界线

新手写出能画的控件；老手写出的控件**在 200% 缩放的屏上不糊、切到暗色主题不用改代码
（颜色全从 `palette()` 取）、换台机器字体不一样也不塌**。第 6 讲证据⑦故意换了一套调色板再抓一次图，
打印同一个坐标上采样到的像素名（换前 / 换后）——颜色跟着 `palette()` 走，
这就是「用户的深色模式跟你有关」的证明。

### 5. 帧率不是优化出来的，是不浪费出来的

`update(QRect)` 而不是 `update()`、脏区外不画、昂贵效果开 `CacheMode`、
`QPixmap::fromImage()` 不放在 `paintEvent` 里、`QPixmapCache` 限额给够、
文字不重复算度量——**这些全是「别每帧都做同一件事」**，不是「把每件事做快点」。
第 5/6/8 讲各留了一组计数证据，你跑一遍就知道自己浪费在哪儿。

### 6. 跑一遍，把数字抄回注释里

本机编不动，师傅就写到这儿为止；**剩下的那一半必须你在能编译的机器上补完**：
`cmake --build build --target painting_05_images_and_pixmaps`（其余同理），
跑起来（或 `-platform offscreen`），到 `build/bin` 看图、看打印，把真实数字写回每个示例末尾
「速查表」旁边的空位。做完这 8 讲，你就能从「用 Qt 的控件」进阶到「给别人提供控件」——
这算入了 Qt 绘图的门。下一关是**第陆阶段「模型/视图与委托」**，去处理「一大堆数据怎么摆」；
你在这儿学的绘制，到时候会在 `itemDecoration` 和委托的 `paint()` 里再用上一遍。加油，徒弟！
