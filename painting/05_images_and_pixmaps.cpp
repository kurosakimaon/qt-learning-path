// ============================================================================
// 第 5 讲 · QImage / QPixmap / QBitmap：像素到底从哪儿来
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   前四讲一直在讲「拿笔往哪儿画」，这一讲正面回答另外一半问题：**画布本身**。
//     1) QImage 的三条底层事实，全部用「直接读内存」的方式打出来，不靠文档口径：
//        · Format 决定字节布局（ARGB32 / RGB32 / Indexed8 / Grayscale8 / Alpha8）；
//        · 字节序（endianness）决定 scanLine() 里那四个字节谁先谁后；
//        · bytesPerLine() 里含有行对齐填充，「宽 × 每像素字节数」常常不等于它；
//     2) QImage 与 QPixmap 的分工：QImage 是**内存里的图**（可随机读写像素、可跨线程
//        搬、构造不依赖任何窗口系统），QPixmap 是**面向窗口系统的图**（为「往屏幕上
//        blit」优化，必须在 QGuiApplication 之后创建、只能在 GUI 线程用、不能读像素）；
//        QBitmap 是「一比特掩膜」，Qt6 里从 QPixmap 造它要走 QBitmap::fromPixmap；
//     3) 隐式共享（implicit sharing / copy-on-write）与 detach：本机 qimage.h 里
//        **没有 useCount()**（第 117~118 行只有 detach()/isDetached()），所以本讲
//        用「isDetached 的翻转 + 改一个另一个不受影响」两个行为一起证明；
//     4) scaled 的两组参数：Qt::AspectRatioMode 三档 × Qt::TransformationMode 两档；
//        SmoothTransformation 到底贵在哪，用 QElapsedTimer 实测一个倍率区间；
//     5) QPixmapCache：setCacheLimit/cacheLimit/insert/find/remove/clear 全套，
//        以及「超出限额就淘汰」的可测行为；
//     6) save/load：图像全部由代码生成，**不读任何外部文件**；PNG/BMP/JPEG 三种往返 +
//        QImageWriter::supportedImageFormats() 打出这台机器真的能写哪些格式；
//     7) 两条真会咬人的性能坑：
//        · paintEvent 里反复 QPixmap::fromImage() 转换（本讲用计数器把它数出来）；
//        · 高 DPI 下 QImage::setDevicePixelRatio() 之后，QPainter 用的就是**逻辑坐标**
//          ——这条不靠说，用「100×100 的逻辑方块在 DPR=2 的画布上着墨包围盒是 200×200」
//          来证实。
//
// 对应构建目标名：painting_05_images_and_pixmaps
//   find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)   # 本组统一链 Svg（第 8 讲要用），本讲只用到 Widgets
//   add_executable(painting_05_images_and_pixmaps 05_images_and_pixmaps.cpp)
//   target_link_libraries(painting_05_images_and_pixmaps PRIVATE Qt6::Widgets Qt6::Svg)
//   set(CMAKE_AUTOMOC ON)
//   本文件**不含 Q_OBJECT 类**（只覆写 paintEvent，不需要元对象），末尾不需要 .moc。
//
// 运行前须知：
//   · 会往**当前工作目录**写这些 PNG：
//       painting_05_images_and_pixmaps.png（六格总览卡）、
//       painting_05_scaled_fast.png / painting_05_scaled_smooth.png（放大对照）、
//       painting_05_hidpi_1x.png / painting_05_hidpi_2x.png（DPR 对照）、
//       painting_05_roundtrip.png（存出去再读回来的那张）。
//     另外 evidenceSaveLoad() 还会写一张原图 painting_05_images_and_pixmaps_loadsave_extra.png
//     （径向渐变那张，PNG / JPEG / BMP 三种编码都从它出发）。
//   · 无人值守：QTimer::singleShot 排动作 → 存图 → 打印 → QCoreApplication::quit()，
//     全程不要鼠标键盘，约 2 秒跑完。**没有 QDialog::exec()，也没有裸 QEventLoop::exec()**。
//   · 可以无头跑：`painting_05_images_and_pixmaps -platform offscreen`
//     （CMake 里已经把 QT_QPA_PLATFORM=offscreen 设进测试环境了）。
//   · 证据里凡是 QElapsedTimer 的耗时数字都**强烈依赖你的机器**，
//     我只承诺「相对关系」，具体数值请你自己跑出来抄回注释里。
//   · 未实测声明：本机当前无法编译链接 Qt 运行时（运行时库版本不匹配），
//     本示例**已通过 g++ -fsyntax-only 编译期核对（-std=c++17 -Wall -Wextra 零 error 零 warning），
//     未链接、未运行**；所有 PNG 与所有打印数字都还没被任何机器验证过，
//     「预期」两个字后面写的是推理，不是实测。
//   · Qt5 → Qt6 差异，本讲涉及四处（都是**头文件里能看见**的）：
//       ① QPixmap::fromImage 的第二参数在 Qt6 是 `Qt::ImageConversionFlags`（qpixmap.h 第 90 行），
//          Qt5 老代码写的是 Qt::ColorMode/Qt::DefaultColorMode，搬过来会编不过；
//       ② `QBitmap(const QPixmap &)` 与 `QBitmap::operator=(const QPixmap &)` 在 Qt6.0 起弃用
//          （qbitmap.h 第 22、29 行的 QT_DEPRECATED_VERSION_X_6_0），改用 fromPixmap()；
//       ③ `QPixmapCache::replace(const Key &, const QPixmap &)` 在 Qt6.6 起弃用
//          （qpixmapcache.h 第 50~53 行），头文件直接写明「用 remove(key) 再 key = insert(pixmap)」；
//       ④ QImage 的字节数在 Qt6 叫 `sizeInBytes()`（qimage.h 第 165 行，返回 qsizetype），
//          Qt5 那套 byteCount() 本机头文件里没有。
// ============================================================================

#include <QApplication>          // QtWidgets/qapplication.h
#include <QWidget>               // QtWidgets/qwidget.h
#include <QPaintEvent>           // QtGui/qevent.h
#include <QPainter>              // QtGui/qpainter.h
#include <QPen>                  // QtGui/qpen.h
#include <QBrush>                // QtGui/qbrush.h：QBrush + QGradient + 三个渐变子类都在这
#include <QFont>                 // QtGui/qfont.h：总览卡的标签字号
#include <QPolygonF>             // QtGui/qpolygon.h：QPolygonF / QPointF
#include <QImage>                // QtGui/qimage.h：本讲主角一
#include <QPixmap>               // QtGui/qpixmap.h：本讲主角二
#include <QBitmap>               // QtGui/qbitmap.h：本讲主角三（掩膜）
#include <QPixmapCache>          // QtGui/qpixmapcache.h
#include <QPaintDevice>          // QtGui/qpaintdevice.h：devicePixelRatio()
#include <QColor>                // QtGui/qcolor.h：QRgb 的 typedef 也在这里
#include <QImageWriter>          // QtGui/qimagewriter.h：supportedImageFormats()
#include <QBuffer>               // QtCore/qbuffer.h：内存里存/读图
#include <QByteArray>            // QtCore/qbytearray.h
#include <QIODevice>             // QtCore/qiodevice.h：WriteOnly/ReadOnly
#include <QElapsedTimer>         // QtCore/qelapsedtimer.h
#include <QSysInfo>              // QtCore/qsysinfo.h：ByteOrder
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QCoreApplication>
#include <QGuiApplication>       // QtGui/qguiapplication.h：instance()
#include <QDebug>
#include <utility>               // std::as_const（证明「只读不 detach」要用 const 引用）

// ============================================================================
// 一、核对签名（本机 C:\msys64\ucrt64\include\qt6，全部带行号；对不上的一律不用）
// ============================================================================
// QtGui/qimage.h
//   第 37 行    class Q_GUI_EXPORT QImage : public QPaintDevice   ← 所以 QImage 本身就是画布
//   第 42~67 行 enum Format { Format_Mono, Format_MonoLSB, Format_Indexed8, Format_RGB32,
//                 Format_ARGB32, Format_ARGB32_Premultiplied, Format_RGB16, …,
//                 Format_RGBA8888, Format_RGBA8888_Premultiplied, …,
//                 Format_Alpha8, Format_Grayscale8, … }
//   第 91/92 行 QImage(uchar *data, int w, int h, qsizetype bytesPerLine, Format,
//                 QImageCleanupFunction = nullptr, void *cleanupInfo = nullptr)
//                 const uchar* 版本也在 ← 「包住别人的内存」，不负责释放
//   第 110 行   bool isNull() const;
//   第 117 行   void detach();            第 118 行 bool isDetached() const;
//                 ← **本机头文件里没有 useCount()**，所以引用计数只能靠行为证明
//   第 120/121 行 [[nodiscard]] QImage copy(const QRect & = QRect()) const / copy(x,y,w,h)
//   第 126/135 行 [[nodiscard]] QImage convertToFormat(Format, Qt::ImageConversionFlags)
//                 另有带 colorTable 的三参数版本（第 135 行）
//   第 139 行   [[nodiscard]] QImage convertedTo(Format f, flags) const   ← Qt6 的新名字，等价
//   第 148 行   QRect rect() const;
//   第 151 行   int colorCount() const;   第 155 行 void setColor(int i, QRgb c);
//   第 156 行   void setColorCount(int);  第 158 行 bool allGray() const;
//   第 161/162 行 uchar *bits() / const uchar *bits() const
//   第 165 行   qsizetype sizeInBytes() const;      第 170 行 qsizetype bytesPerLine() const;
//   第 167 行   uchar *scanLine(int);               ← 可写：会让数据 detach
//   第 168 行   const uchar *scanLine(int) const;
//   第 169 行   const uchar *constScanLine(int) const; ← 只读版本，明确不 detach
//   第 175/176 行 int pixelIndex(int x, int y) const;
//   第 178/179 行 QRgb pixel(int x, int y) const;   ← 返回 uint（QRgb），不是 QColor
//   第 181/182 行 void setPixel(int x, int y, uint index_or_rgb);
//                 ← **没有 setPixelIndex()**：索引格式（Indexed8/Mono）也走这个函数，
//                   参数名 index_or_rgb 就是「按格式决定它是索引还是颜色」
//   第 184/185 行 QColor pixelColor(int x, int y) const;  第 187/188 行 setPixelColor(...)
//   第 190 行   QList<QRgb> colorTable() const;
//   第 193 行   qreal devicePixelRatio() const;     第 194 行 void setDevicePixelRatio(qreal);
//   第 197~199 行 void fill(uint) / fill(const QColor&) / fill(Qt::GlobalColor);
//   第 202 行   bool hasAlphaChannel() const;       第 203 行 void setAlphaChannel(const QImage&);
//   第 204 行   [[nodiscard]] QImage createAlphaMask(flags) const;
//   第 206 行   [[nodiscard]] QImage createHeuristicMask(bool clipTight = true) const;
//   第 210~216 行 [[nodiscard]] QImage scaled(int w, int h, Qt::AspectRatioMode = IgnoreAspectRatio,
//                 Qt::TransformationMode = FastTransformation) const;  + QSize 版
//                 + scaledToWidth(int, mode) / scaledToHeight(int, mode)
//   第 242 行   void invertPixels(InvertMode = InvertRgb);
//   第 262 行   bool load(const QString &fileName, const char *format = nullptr);
//   第 263 行   bool loadFromData(QByteArrayView data, const char *format = nullptr);
//   第 268 行   bool save(const QString &fileName, const char *format = nullptr, int quality = -1) const;
//   第 269 行   bool save(QIODevice *device, const char *format = nullptr, int quality = -1) const;
//   第 271 行   static QImage fromData(QByteArrayView data, const char *format = nullptr);
//   第 281/282 行 int dotsPerMeterX() const; / setDotsPerMeterX(int)（第 283 行）
//   第 290 行   void setText(const QString &key, const QString &value);  ← PNG 文本块
//   第 292 行   QPixelFormat pixelFormat() const noexcept;               ← Qt6 的新格式描述
// QtGui/qpixmap.h
//   第 27 行    class Q_GUI_EXPORT QPixmap : public QPaintDevice
//   第 51 行    bool isNull() const;              第 120 行 bool operator!() const { return isNull(); }
//   第 56/57 行 QSize size() const;  QRect rect() const;
//   第 62 行    void fill(const QColor &fillColor = Qt::white);
//   第 64/65 行 QBitmap mask() const; void setMask(const QBitmap &);
//   第 68 行    void setDevicePixelRatio(qreal);
//   第 71/72 行 bool hasAlpha() const; bool hasAlphaChannel() const;
//   第 79~85 行 scaled(int w,int h, AspectRatioMode = IgnoreAspectRatio, TransformationMode = Fast)
//                 / scaled(QSize, …) / scaledToWidth / scaledToHeight
//   第 89 行    QImage toImage() const;
//   第 90 行    static QPixmap fromImage(const QImage &image, Qt::ImageConversionFlags = Qt::AutoColor);
//   第 92 行    static QPixmap fromImage(QImage &&image, Qt::ImageConversionFlags = Qt::AutoColor)
//   第 97~101 行 load / loadFromData / save（与 QImage 同形，参数略有差别）
//   第 113/114 行 bool isDetached() const; void detach();
// QtGui/qbitmap.h
//   第 16 行    class Q_GUI_EXPORT QBitmap : public QPixmap     ← 注意：**是 QPixmap 的子类**
//   第 22/29 行 QBitmap(const QPixmap &) / operator=(const QPixmap &) —— Qt6.0 起弃用
//   第 23 行    QBitmap(int w, int h);            第 24 行 explicit QBitmap(const QSize &);
//   第 25 行    explicit QBitmap(const QString &fileName, const char *format = nullptr);
//   第 36/37 行 static QBitmap fromImage(const QImage &, Qt::ImageConversionFlags = Qt::AutoColor);
//   第 40 行    static QBitmap fromPixmap(const QPixmap &);
// QtGui/qpixmapcache.h
//   第 14~18 行 class QPixmapCache { class Key … }
//   第 31 行    bool isValid() const noexcept;   （Key 的）
//   第 44/45 行 static int cacheLimit(); static void setCacheLimit(int);
//   第 46/47 行 static bool find(const QString &key, QPixmap *pixmap); find(const Key &, QPixmap *);
//   第 48 行    static bool insert(const QString &key, const QPixmap &pixmap);
//   第 49 行    static Key insert(const QPixmap &pixmap);
//   第 50~53 行 replace(const Key &, const QPixmap &) —— Qt6.6 起弃用
//   第 55/56 行 static void remove(const QString &key); remove(const Key &);
//   第 57 行    static void clear();
// QtGui/qpaintdevice.h
//   第 18 行    class Q_GUI_EXPORT QPaintDevice
//   第 48/49 行 int logicalDpiX() const; int logicalDpiY() const;
//   第 52 行    qreal devicePixelRatio() const;   第 53 行 qreal devicePixelRatioF() const
//                 ← QWidget / QImage / QPixmap 三者都从这里继承到 devicePixelRatio()
// QtGui/qpainter.h
//   第 55 行    SmoothPixmapTransform = 0x04       ← 画 QPixmap/QImage 放大时要用它
//   第 342 行   void drawImage(const QRectF &targetRect, const QImage &image,
//                              const QRectF &sourceRect, Qt::AspectRatioMode = Qt::IgnoreAspectRatio);
//   第 350 行   inline void drawImage(const QRectF &r, const QImage &image);
//   第 352 行   void drawImage(const QPointF &p, const QImage &image);
// QtCore/qnamespace.h
//   第 1220~1224 行 enum AspectRatioMode { IgnoreAspectRatio, KeepAspectRatio,
//                     KeepAspectRatioByExpanding };
//   第 1358~1361 行 enum TransformationMode { FastTransformation, SmoothTransformation };
//   第 501 行     AutoColor = 0x00000000   （Qt::ImageConversionFlags 的默认值）
// 本讲**没用**、也提醒你别用的：QImage::useCount()（头文件里没有）、
//   QImage::setPixelIndex()（没有这个函数）、QImage::byteCount()（本机没有，用 sizeInBytes()）。

// ============================================================================
// 二、公共小工具：把「看得见」变成「数得出来」
// ============================================================================

// 非透明像素数。注意：先把副本转成 ARGB32，再用 scanLine 按 uint 步进取 alpha。
// convertToFormat 是 [[nodiscard]]（qimage.h 126 行），必须接住返回值。
static qint64 countOpaque(const QImage &src)
{
    if (src.isNull())
        return 0;
    const QImage probe = src.convertedTo(QImage::Format_ARGB32);
    qint64 n = 0;
    for (int y = 0; y < probe.height(); ++y) {
        const uchar *row = probe.constScanLine(y);       // 只读版本：qimage.h 169 行
        const int stridePx = int(probe.bytesPerLine() / 4);
        for (int x = 0; x < probe.width() && x < stridePx; ++x) {
            const uint v = uint(row[x * 4 + 0])          // 小端：A 在最高位、字节序里最后
                     + (uint(row[x * 4 + 1]) << 8)
                     + (uint(row[x * 4 + 2]) << 16)
                     + (uint(row[x * 4 + 3]) << 24);
            if (((v >> 24) & 0xffu) != 0)
                ++n;
        }
    }
    return n;
}

// 非透明像素的包围盒：这是本模块通用的「量一量」手段（第 1 讲就用它）。
static QRect inkBounds(const QImage &src)
{
    const QImage probe = src.isNull() ? QImage() : src.convertedTo(QImage::Format_ARGB32);
    QRect box;                                            // 默认 QRect().isNull() == true
    for (int y = 0; y < probe.height(); ++y) {
        const uchar *row = probe.constScanLine(y);
        const int stridePx = int(probe.bytesPerLine() / 4);
        for (int x = 0; x < probe.width() && x < stridePx; ++x) {
            const uchar a = row[x * 4 + 3];
            if (a != 0) {
                if (box.isNull())
                    box = QRect(x, y, 1, 1);
                else
                    box = box.united(QRect(x, y, 1, 1));
            }
        }
    }
    return box;
}

// 把一行头 8 个字节按十六进制拼成字符串，用来看「内存里到底怎么排的」。
static QString hexRow(const QImage &img, int y, int n = 8)
{
    const uchar *row = img.constScanLine(y);              // 只读，不触发 detach
    const int avail = int(img.bytesPerLine());
    QString s;
    for (int i = 0; i < n && i < avail; ++i) {
        if (i)
            s += QLatin1Char(' ');
        s += QString::asprintf("%02X", row[i]);
    }
    return s;
}

// ============================================================================
// 三、证据①：格式 × 字节序 × 行填充（直接读内存）
// ============================================================================
static void evidenceFormats()
{
    qInfo() << "【证据①】格式 / 字节序 / bytesPerLine 行填充";
    qInfo() << "  本机字节序:"
            << (QSysInfo::ByteOrder == QSysInfo::LittleEndian ? "LittleEndian（x86/ARM 常见）"
                                                             : "BigEndian");

    // 3-1 同一个颜色，两种 32 位格式，看 scanLine 里的四个字节
    QImage argb(4, 1, QImage::Format_ARGB32);
    argb.fill(QColor(0, 0, 0, 0));
    argb.setPixelColor(0, 0, QColor(0x11, 0x22, 0x33, 0xAA));   // R=11 G=22 B=33 A=AA
    QImage rgb(4, 1, QImage::Format_RGB32);
    rgb.fill(QColor(0, 0, 0));
    rgb.setPixelColor(0, 0, QColor(0x11, 0x22, 0x33, 0x00));    // 故意给 alpha=0

    qInfo() << "  ARGB32 (0,0) 内存前 4 字节:" << hexRow(argb, 0, 4)
            << "| pixel() 读回 0x" + QString::number(quint64(argb.pixel(0, 0)), 16).toUpper()
            << "| pixelColor().alpha() =" << argb.pixelColor(0, 0).alpha();
    qInfo() << "  RGB32  写入 alpha=0 后读回 alpha =" << rgb.pixelColor(0, 0).alpha()
            << "（预期恒为 255：RGB32 不存 alpha，低字节固定 0xFF）";
    qInfo() << "  hasAlphaChannel(): ARGB32 =" << argb.hasAlphaChannel()
            << ", RGB32 =" << rgb.hasAlphaChannel() << "（qimage.h 202 行）";

    // 3-2 行填充：bytesPerLine != width * 每像素字节数
    qInfo() << "  灰度/Alpha 的行对齐（bytesPerLine vs width）：";
    const int widths[] = { 1, 4, 5, 18, 19 };
    for (int w : widths) {
        const QImage g(w, 1, QImage::Format_Grayscale8);
        const QImage a(w, 1, QImage::Format_Alpha8);
        qInfo() << QString::asprintf("    w=%2d  Grayscale8: bpl=%2ld sizeInBytes=%3ld | "
                                     "Alpha8: bpl=%2ld  Mono 视角 bpl=%2ld",
                                     w, long(g.bytesPerLine()), long(g.sizeInBytes()),
                                     long(a.bytesPerLine()),
                                     long(QImage(w, 1, QImage::Format_Mono).bytesPerLine()));
    }
    qInfo() << "  → 记住这一条：写 scanLine 的裸内存循环时，**列上限要用 width()，"
               "行首地址要用 scanLine(y)**，别用 y*width*4 手算偏移。";

    // 3-3 索引格式：QImage::Format_Indexed8*（色表 + 每像素一个索引）
    QImage idx(18, 12, QImage::Format_Indexed8);
    idx.setColorCount(4);                                   // qimage.h 156 行
    idx.setColor(0, QColor(20, 20, 20).rgb());
    idx.setColor(1, QColor(220, 60, 60).rgb());
    idx.setColor(2, QColor(60, 200, 120).rgb());
    idx.setColor(3, QColor(250, 250, 40).rgb());
    idx.fill(0);                                            // fill(uint) 在索引格式里填的是**索引**
    for (int y = 0; y < idx.height(); ++y)
        for (int x = 0; x < idx.width(); ++x)
            idx.setPixel(x, y, quint8((x + y) % 4));         // 写索引也是走 setPixel(x,y,index_or_rgb)
    qInfo() << "  Indexed8: colorCount =" << idx.colorCount()
            << "色表大小 =" << idx.colorTable().size()
            << "| (0,0) 索引 =" << idx.pixelIndex(0, 0)
            << ", (3,0) 索引 =" << idx.pixelIndex(3, 0)
            << "| pixel(3,0) 当颜色读 = 0x"
            + QString::number(quint64(idx.pixel(3, 0)), 16).toUpper()
            << "（**同一个数**，索引格式里它是索引、32 位格式里它是 QRgb）";
    qInfo() << "  Indexed8 有 alpha 通道吗:" << idx.hasAlphaChannel()
            << "（色表项本身可含 alpha，所以 Qt 说没有）";

    // 3-4 掩膜：createAlphaMask → QBitmap（一比特）
    QImage withAlpha(32, 24, QImage::Format_ARGB32);
    withAlpha.fill(QColor(0, 0, 0, 0));
    {
        QPainter p(&withAlpha);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 255));
        p.drawEllipse(2, 2, 28, 20);
        p.setBrush(QColor(255, 255, 255, 128));              // 半透明：掩膜里会被判成「不算全不透明」
        p.drawRect(20, 14, 10, 8);
    }
    const QImage maskImg = withAlpha.createAlphaMask();       // qimage.h 204 行
    const QBitmap bm = QBitmap::fromImage(maskImg);           // qbitmap.h 36 行（Qt6 正路）
    qInfo() << "  createAlphaMask(): 格式 =" << int(maskImg.format())
            << "(Format_Mono/…)," << maskImg.width() << "x" << maskImg.height()
            << "bytesPerLine =" << long(maskImg.bytesPerLine());
    qInfo() << "  QBitmap::fromImage 得到" << bm.size().width() << "x" << bm.size().height()
            << "的掩膜；QBitmap 继承自 QPixmap，所以它**同样是窗口系统的图**";
    qInfo() << "  非透明像素数：原图(带 alpha) =" << countOpaque(withAlpha)
            << ", 全图面积 =" << withAlpha.width() * withAlpha.height();

    // 3-5 pixelFormat()：Qt6 用 QPixelFormat 描述布局，比 Format 枚举精细
    //（QtGui/qpixelformat.h 155/176/178/186/189 行：channelCount / alphaSize / bitsPerPixel /
    //   alphaPosition / byteOrder；**没有 alphaChannels()/isFloatingPoint() 这两个名字**）
    const QPixelFormat pf = argb.pixelFormat();               // qimage.h 292 行
    qInfo() << "  ARGB32 的 QPixelFormat: bitsPerPixel =" << pf.bitsPerPixel()
            << "channelCount =" << pf.channelCount()
            << "alphaSize =" << pf.alphaSize()
            << "alphaPosition(枚举值) =" << int(pf.alphaPosition());
}

// ============================================================================
// 四、证据②：隐式共享与 detach（本机没有 useCount()，用行为证明）
// ============================================================================
static void evidenceSharing()
{
    qInfo() << "【证据②】隐式共享（implicit sharing）与 detach";

    QImage a(200, 200, QImage::Format_ARGB32);
    a.fill(QColor(10, 120, 200, 255));
    QImage b = a;                                              // 只拷了个指针 + 引用计数
    qInfo() << "  b = a 之后: a.isDetached() =" << a.isDetached()
            << "b.isDetached() =" << b.isDetached()
            << "（预期都 false：两份 QImage 指向同一块数据）";
    qInfo() << "  数据地址是否相同: bits() 相等 =" << (a.bits() == b.bits())
            << "（qimage.h 161 行 bits()；共享就是同一个指针）";

    // 只读访问**不**触发 detach
    b = a;
    const int beforeAlpha = a.pixelColor(5, 5).red();
    const uchar *ro = std::as_const(b).constScanLine(0);       // 只读版本：169 行
    qInfo() << "  通过 constScanLine(0) 只读一行后 b.isDetached() =" << b.isDetached()
            << "（读到首字节 =" << int(ro[0]) << "，red=" << beforeAlpha << "）";

    // 写操作触发 detach
    b.setPixelColor(5, 5, QColor(255, 0, 0, 255));             // 非 const 写 → detach
    const bool sharedAfterWrite = (a.bits() == b.bits());
    qInfo() << "  b.setPixelColor(...) 之后: a.isDetached() =" << a.isDetached()
            << "b.isDetached() =" << b.isDetached()
            << "bits() 仍相等 =" << sharedAfterWrite;
    qInfo() << "  行为证明：改 b(5,5) 之后 a(5,5) ="
            << a.pixelColor(5, 5).name(QColor::HexArgb)
            << "b(5,5) =" << b.pixelColor(5, 5).name(QColor::HexArgb)
            << "（a 应保持原色：这就是 copy-on-write）";

    // 非 const 的 scanLine() 也会 detach —— 这是最容易踩的一条
    QImage c = a;
    qInfo() << "  c = a 后 c.isDetached() =" << c.isDetached();
    uchar *w = c.scanLine(0);                                  // 非 const 版本：167 行
    qInfo() << "  拿**非 const** scanLine(0) 之后 c.isDetached() =" << c.isDetached()
            << "（指针 w 有效，但 c 已经是自己的一份了）";
    w[0] = 0x40;                                               // 真的写下去：这才是非 const 的语义
    qInfo() << "  写 c 的首字节之后：c(0,0)=0x"
            << QString::number(quint64(c.pixel(0, 0)), 16).toUpper()
            << " a(0,0)=0x" << QString::number(quint64(a.pixel(0, 0)), 16).toUpper()
            << "（a 没被改动，正是 detach 的意义）";
    qInfo() << "  → 纪律：只读就用 constScanLine()，或把 QImage 声明成 const 再传。"
               "否则你以为在「读」，其实已经复制了整幅图。";

    // detach() 显式断开 + copy() 永远是独立数据
    QImage d = a;
    d.detach();                                                // 117 行
    qInfo() << "  d.detach() 后 d.isDetached() =" << d.isDetached()
            << "d.copy(QRect(0,0,50,50)).size() ="
            << d.copy(QRect(0, 0, 50, 50)).size();
    qInfo() << "  QPixmap 同理有 isDetached()/detach()（qpixmap.h 113/114 行）；"
               "但 QPixmap 你没法读像素来验证，只能 toImage() 之后测。";
}

// ============================================================================
// 五、证据③：scaled 的 AspectRatioMode × TransformationMode
// ============================================================================
static void evidenceScaled()
{
    qInfo() << "【证据③】scaled：Qt::AspectRatioMode × Qt::TransformationMode";

    const QImage src = QImage(100, 50, QImage::Format_ARGB32);
    qInfo() << "  源图尺寸:" << src.size();

    const QImage i1 = src.scaled(200, 200, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    const QImage i2 = src.scaled(200, 200, Qt::KeepAspectRatio, Qt::FastTransformation);
    const QImage i3 = src.scaled(200, 200, Qt::KeepAspectRatioByExpanding, Qt::FastTransformation);
    const QImage i4 = src.scaledToWidth(120, Qt::FastTransformation);
    const QImage i5 = src.scaledToHeight(120, Qt::FastTransformation);
    qInfo() << "  IgnoreAspectRatio        →" << i1.size() << "（宽高各拉，**变形**）";
    qInfo() << "  KeepAspectRatio          →" << i2.size() << "（取小比例，四周留空）";
    qInfo() << "  KeepAspectRatioByExpanding→" << i3.size()
            << "（取大比例：**会超出你给的目标框**，要自己裁）";
    qInfo() << "  scaledToWidth(120)       →" << i4.size() << "（比例不变，100:50=2:1）";
    qInfo() << "  scaledToHeight(120)      →" << i5.size();

    // 造一张「斜边」图，用来量平滑与最近的差别
    QImage edge(40, 40, QImage::Format_ARGB32);
    edge.fill(QColor(0, 0, 0, 0));
    {
        QPainter p(&edge);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 255));
        QPolygonF tri;
        tri << QPointF(0, 39) << QPointF(39, 0) << QPointF(39, 39);
        p.drawPolygon(tri);
    }
    const QImage fast = edge.scaled(160, 160, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    const QImage smooth = edge.scaled(160, 160, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    // 斜边附近的一个像素：最近邻只会给你「全黑或全透明」，平滑会给你中间灰度
    qInfo() << "  放大 4 倍后，斜边上一个跨界像素 (118,42) 的 alpha："
            << "Fast =" << fast.pixelColor(118, 42).alpha()
            << ", Smooth =" << smooth.pixelColor(118, 42).alpha();
    qInfo() << "  非透明像素数：Fast =" << countOpaque(fast)
            << ", Smooth =" << countOpaque(smooth)
            << "（两者都远大于 160*160/2=12800 的直觉值，因为边界被展宽）";
    smooth.save("painting_05_scaled_smooth.png");
    fast.save("painting_05_scaled_fast.png");

    // 代价实测：同一张图各放大 300 次
    QElapsedTimer t;
    qint64 nsFast = 0, nsSmooth = 0;
    for (int pass = 0; pass < 2; ++pass) {                     // 先跑一轮预热，避免首次分配干扰
        t.start();
        for (int k = 0; k < 300; ++k)
            (void)edge.scaled(300, 300, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        const qint64 f = t.nsecsElapsed();
        for (int k = 0; k < 300; ++k)
            (void)edge.scaled(300, 300, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        const qint64 s = t.nsecsElapsed();
        if (pass) { nsFast = f; nsSmooth = s; }
    }
    qInfo() << "  300 次 scaled 耗时：Fast =" << nsFast / 1000 << "us,"
            << "Smooth =" << nsSmooth / 1000 << "us,"
            << "比值 =" << (nsFast > 0 ? double(nsSmooth) / double(nsFast) : 0.0)
            << "（**具体数值看你机器**，这里只要「Smooth 明显更贵」这个关系成立）";
    qInfo() << "  同样两个模式在 QPixmap 上也有（qpixmap.h 79~85 行），"
               "但画的时候更常用 QPainter::setRenderHint(QPainter::SmoothPixmapTransform)"
               "（qpainter.h 55 行）；drawImage 重载不带模式参数，靠 render hint 决定。";
}

// ============================================================================
// 六、证据④：QPixmap 的「窗口系统」身份 + QPixmapCache
// ============================================================================
static void evidencePixmapAndCache()
{
    qInfo() << "【证据④】QPixmap 的构造时机与 QPixmapCache";

    // QPixmap 依赖窗口系统：这一步能过，恰恰因为 QApplication 已经构造好了
    qInfo() << "  QGuiApplication::instance() 非空 =" << (QGuiApplication::instance() != nullptr)
            << "平台 =" << QGuiApplication::platformName();
    QPixmap pm(64, 64);                                        // ← 换到 app 之前写这行就是崩/断言
    pm.fill(QColor(0, 0, 0, 0));
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(200, 40, 40, 255));
        p.drawRoundedRect(4, 4, 56, 56, 12, 12);
    }
    qInfo() << "  QPixmap.isNull() =" << pm.isNull() << "hasAlpha() =" << pm.hasAlpha()
            << "isDetached() =" << pm.isDetached();
    qInfo() << "  QPixmap 不能读像素：本讲所有像素统计都走 toImage()（qpixmap.h 89 行）";
    const QImage fromPm = pm.toImage();
    qInfo() << "  pm.toImage() 非透明像素 =" << countOpaque(fromPm)
            << "包围盒 =" << inkBounds(fromPm);
    qInfo() << "  toImage→fromImage 往返后再取像素：" 
            << QPixmap::fromImage(fromPm).toImage().pixelColor(32, 32).name(QColor::HexArgb);

    // QPixmapCache：键版与 Key 版两条路
    QPixmapCache::clear();                                     // 57 行：给本讲一个干净起点
    const int oldLimit = QPixmapCache::cacheLimit();           // 44 行
    QPixmapCache::setCacheLimit(64);                           // 单位：KB（45 行）
    qInfo() << "  setCacheLimit(64) 后 cacheLimit() =" << QPixmapCache::cacheLimit()
            << "KB（原来是" << oldLimit << "KB）";

    const bool okKey = QPixmapCache::insert(QStringLiteral("badge64"), pm);   // 48 行
    QPixmap out;
    const bool hitKey = QPixmapCache::find(QStringLiteral("badge64"), &out);   // 46 行
    qInfo() << "  insert(QString,…) =" << okKey << " find(QString, &pm) =" << hitKey
            << "取回尺寸 =" << out.size()
            << "取回后 out.isDetached() =" << out.isDetached()
            << "（命中的那份与缓存里那份**共享像素数据**，所以不是 detached；"
               "缓存省的就是这次复制）";

    const QPixmapCache::Key k = QPixmapCache::insert(pm);      // 49 行：返回 Key
    QPixmap out2;
    const bool hitObj = QPixmapCache::find(k, &out2);          // 47 行
    qInfo() << "  insert(pm) 返回 Key，isValid =" << k.isValid()
            << "用 Key find =" << hitObj;

    QPixmapCache::remove(QStringLiteral("badge64"));           // 55 行
    QPixmap gone;
    qInfo() << "  remove 之后再 find ="
            << QPixmapCache::find(QStringLiteral("badge64"), &gone) << "（预期 false）";
    qInfo() << "  淘汰行为：往 64KB 的限额里灌 40 张 256x256 的 ARGB32 图（每张约 256KB），"
               "早先插入的必然被挤出去：";
    QPixmap big(256, 256);                                     // QPixmap 没有「带颜色」的构造函数
    big.fill(QColor(0, 0, 0, 0));                              // 只能构造完再 fill（qpixmap.h 62 行）
    for (int i = 0; i < 40; ++i)
        QPixmapCache::insert(QString::asprintf("big_%d", i), big);
    QPixmap probe;
    const bool firstSurvived = QPixmapCache::find(QStringLiteral("big_0"), &probe);
    const bool lastSurvived = QPixmapCache::find(QStringLiteral("big_39"), &probe);
    qInfo() << "  big_0 还在吗 =" << firstSurvived << "big_39 还在吗 =" << lastSurvived
            << "（后者 true / 前者 false 才说明限额真的在起作用）";
    qInfo() << "  注意：replace(Key&, pm) 在 Qt6.6 起弃用（qpixmapcache.h 50~53 行），"
               "头文件给的替代就是 remove(key) + key = insert(pm)；"
               "另外**缓存 Key 与 QPixmapCache 都是 GUI 线程的东西**，别在别的线程里使。";
    QPixmapCache::clear();
    QPixmapCache::setCacheLimit(oldLimit);
}

// ============================================================================
// 七、证据⑤：save / load —— 全程代码生成，不碰外部文件
// ============================================================================
static void evidenceSaveLoad()
{
    qInfo() << "【证据⑤】save / load（图像由代码生成）";

    QImage img(180, 90, QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(0, 0, 0, 0));
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        QRadialGradient g(QPointF(60, 45), 55);
        g.setColorAt(0.0, QColor(255, 240, 180, 255));
        g.setColorAt(1.0, QColor(200, 60, 90, 255));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawRoundedRect(2, 2, 176, 86, 18, 18);
        p.setBrush(QColor(255, 255, 255, 200));
        p.drawEllipse(120, 12, 50, 50);
    }
    img.setText(QStringLiteral("Title"), QStringLiteral("painting-05"));   // 290 行：PNG 文本块

    const QList<QByteArray> writable = QImageWriter::supportedImageFormats(); // 本机真能写什么
    QStringList names;
    for (const QByteArray &f : writable)
        names << QString::fromLatin1(f.toUpper());
    qInfo() << "  本机 QImageWriter 支持的格式数 =" << writable.size()
            << "前几个:" << names.mid(0, 8).join(QLatin1Char(','));

    // 1) 文件往返
    const bool okPng = img.save(QStringLiteral("painting_05_roundtrip.png"));
    QImage backPng;
    const bool loadPng = backPng.load(QStringLiteral("painting_05_roundtrip.png"));
    qInfo() << "  PNG: save =" << okPng << "load =" << loadPng
            << "尺寸一致 =" << (backPng.size() == img.size())
            << "中心像素 " << img.pixelColor(90, 45).name(QColor::HexArgb)
            << "->" << backPng.pixelColor(90, 45).name(QColor::HexArgb);

    // 2) 内存往返（QBuffer）：CI 里不落盘也能验
    QByteArray bytes;
    {
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        const bool okDev = img.save(&buf, "PNG");             // 269 行：带 format 的 QIODevice 版
        buf.close();
        qInfo() << "  存进 QBuffer:" << okDev << "字节数 =" << bytes.size();
    }
    const QImage fromMem = QImage::fromData(bytes);            // 271 行：静态 fromData
    qInfo() << "  fromData 读回:" << fromMem.size()
            << "alpha =" << fromMem.pixelColor(90, 45).alpha()
            << "isNull =" << fromMem.isNull();

    // 3) 有损与无损的差别 + quality 参数
    QImage opaque = img.convertedTo(QImage::Format_RGB32);      // JPEG 不存 alpha，先转掉
    QByteArray jpg;
    {
        QBuffer buf(&jpg);
        buf.open(QIODevice::WriteOnly);
        opaque.save(&buf, "JPG", 85);                          // quality 0..100，-1 用默认
        buf.close();
    }
    QByteArray bmp;
    {
        QBuffer buf(&bmp);
        buf.open(QIODevice::WriteOnly);
        opaque.save(&buf, "BMP");
        buf.close();
    }
    qInfo() << QString::asprintf("  同一张 180x90：PNG=%d 字节, JPG(85)=%d 字节, BMP=%d 字节"
                                 "（BMP 是无压缩的裸行，正好等于 bytesPerLine*height 量级）",
                                 int(bytes.size()), int(jpg.size()), int(bmp.size()));
    qInfo() << "  save 的第二个参数是**格式名字符串**（\"PNG\"/\"JPG\"/\"BMP\"…），"
               "它和文件后缀无关：写成 .png 但传 \"BMP\" 就存成 BMP；传 nullptr 才按后缀猜。";
    qInfo() << "  无损性检查：PNG 往返后非透明像素数 " << countOpaque(img) << "→"
            << countOpaque(backPng);

    img.save(QStringLiteral("painting_05_images_and_pixmaps_loadsave_extra.png"));
}

// ============================================================================
// 八、证据⑥：高 DPI —— setDevicePixelRatio 之后 QPainter 用逻辑坐标
// ============================================================================
static void evidenceHiDpi()
{
    qInfo() << "【证据⑥】devicePixelRatio：物理像素 vs 逻辑坐标";

    // 同一个「逻辑 200x200」的规格，做两张画布：DPR=1 用 200x200 物理，DPR=2 用 400x400 物理
    QImage one(200, 200, QImage::Format_ARGB32);
    one.setDevicePixelRatio(1.0);                              // qimage.h 194 行
    QImage two(400, 400, QImage::Format_ARGB32);
    two.setDevicePixelRatio(2.0);

    for (QImage *cv : { &one, &two }) {
        cv->fill(QColor(0, 0, 0, 0));
        QPainter p(cv);
        qInfo() << "  QPainter.isActive =" << p.isActive()
                << "devicePixelRatio =" << cv->devicePixelRatio()
                << "逻辑视口 window =" << p.window() << "设备视口 viewport =" << p.viewport();
        p.setPen(QPen(QColor(30, 30, 30), 2));                 // 逻辑 2 像素宽
        p.setBrush(QColor(60, 180, 90, 255));
        p.drawRect(10, 10, 100, 100);                          // **逻辑坐标**下同一个方块
        p.setPen(Qt::NoPen);
        p.drawRect(120, 10, 50, 50);
    }

    const QRect b1 = inkBounds(one);
    const QRect b2 = inkBounds(two);
    qInfo() << "  逻辑 (10,10,100,100) 方块的着墨包围盒："
            << "DPR=1 →" << b1 << ", DPR=2 →" << b2;
    qInfo() << "  包围盒宽高比 DPR2/DPR1 ="
            << (b1.width() > 0 ? double(b2.width()) / double(b1.width()) : 0.0)
            << "（预期约 2：证明 QPainter 把 DPR 当成整体缩放塞进了变换链）";
    qInfo() << "  非透明像素数 DPR1 =" << countOpaque(one) << "DPR2 =" << countOpaque(two)
            << "面积比 ="
            << (countOpaque(one) > 0 ? double(countOpaque(two)) / double(countOpaque(one)) : 0.0)
            << "（约 4 = 2^2）";
    one.save(QStringLiteral("painting_05_hidpi_1x.png"));
    two.save(QStringLiteral("painting_05_hidpi_2x.png"));

    // QPixmap 也有 setDevicePixelRatio（qpixmap.h 68 行），控件那边则从 QPaintDevice 继承读法
    QPixmap px(200, 200);
    px.fill(QColor(0, 0, 0, 0));
    px.setDevicePixelRatio(2.0);
    qInfo() << "  QPixmap 设 DPR=2 后 size() 仍是" << px.size()
            << "，逻辑尺寸 = size()/devicePixelRatio；"
            "控件上读 DPR 用 QPaintDevice::devicePixelRatio()（qpaintdevice.h 52 行）"
            "或 windowHandle()->devicePixelRatio()（qwidget.h 646 行 windowHandle）。";
    qInfo() << "  → 实操口径：**图元资源**（图标/照片）按 DPR 备好 1x/2x；"
               "把 1x 的 QImage 直接 setDevicePixelRatio(2) 只是「告诉 Qt 我这几像素代表多大逻辑尺寸」，"
               "不会凭空变出细节，放大照样糊。";
}

// ============================================================================
// 九、一个正经的展示控件：演示「paintEvent 里不要反复 fromImage」
// ============================================================================
// 本文件唯一的自定义类，**没有 Q_OBJECT**：只覆写 paintEvent 不需要元对象。
class ImageBoard : public QWidget
{
public:
    explicit ImageBoard(const QImage &content, QWidget *parent = nullptr)
        : QWidget(parent), m_source(content)
    {
        setWindowTitle(QStringLiteral("painting 05 · QImage 板"));
        resize(m_source.size());
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        // 反例的解药：把转换**一次性**做完存成成员，paintEvent 里只画不转
        m_ready = QPixmap::fromImage(m_source);
        m_ready.setDevicePixelRatio(m_source.devicePixelRatio());
    }

    static int fromImageCallsInPaint() { return s_fromImageInPaint; }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QPainter p(this);
        // 正确姿势：直接 drawPixmap/drawImage，一次转换都不做
        p.drawImage(QPointF(0, 0), m_source);
        if (s_badDemoEnabled) {
            // 故意错的姿势：每帧重新 fromImage 一次 —— 本讲把它数出来给你看
            ++s_fromImageInPaint;
            const QPixmap tmp = QPixmap::fromImage(m_source);
            p.drawPixmap(0, height() / 2, tmp);
        }
        // 局部重绘的脏区：event->region()（第 6 讲正面讲 update(QRect)）
        if (event->region().isEmpty())
            ++s_fullPaints;
        else
            ++s_partialPaints;
    }

private:
    QImage m_source;
    QPixmap m_ready;
    inline static int s_fromImageInPaint = 0;
    inline static int s_fullPaints = 0;
    inline static int s_partialPaints = 0;
    inline static bool s_badDemoEnabled = false;

public:
    static void enableBadDemo() { s_badDemoEnabled = true; }
    static void paintStats(int *full, int *partial)
    {
        *full = s_fullPaints;
        *partial = s_partialPaints;
    }
};

// ============================================================================
// 十、总览卡：六种格式并排画进一张 QImage（本讲的主证据 PNG）
// ============================================================================
static QImage makeOverviewCard()
{
    QImage card(560, 300, QImage::Format_ARGB32_Premultiplied);
    card.fill(QColor(245, 246, 248, 255));

    // 1) 源：ARGB32 上画一个抗锯齿椭圆（下面所有「透明感」的对照都以它为底）
    QImage seed(90, 60, QImage::Format_ARGB32);
    seed.fill(QColor(0, 0, 0, 0));
    {
        QPainter sp(&seed);
        sp.setRenderHint(QPainter::Antialiasing);
        sp.setPen(Qt::NoPen);
        sp.setBrush(QColor(30, 90, 200, 255));
        sp.drawEllipse(6, 6, 78, 48);
    }

    // 2) Indexed8 棋盘：色表 + 每像素一个索引（写索引也走 setPixel）
    QImage idx(90, 60, QImage::Format_Indexed8);
    idx.setColorCount(2);
    idx.setColor(0, QColor(250, 250, 250).rgb());
    idx.setColor(1, QColor(30, 90, 200).rgb());
    idx.fill(0);
    for (int y = 0; y < idx.height(); ++y)
        for (int x = 0; x < idx.width(); ++x)
            idx.setPixel(x, y, quint8(((x >> 2) ^ (y >> 2)) & 1));

    // 3) Grayscale8 横向渐变：非 const scanLine 逐字节写
    QImage gray(90, 60, QImage::Format_Grayscale8);
    gray.fill(0);
    for (int y = 0; y < gray.height(); ++y) {
        uchar *row = gray.scanLine(y);
        for (int x = 0; x < gray.width(); ++x)
            row[x] = uchar((x * 255) / (gray.width() - 1));
    }

    // 4) RGBA8888：与 ARGB32 的差别就在**内存字节序**，直写一次你就记住了
    QImage rgba(90, 60, QImage::Format_RGBA8888);
    rgba.fill(QColor(0, 0, 0, 0));
    for (int y = 0; y < rgba.height(); ++y) {
        uchar *row = rgba.scanLine(y);
        for (int x = 0; x < rgba.width(); ++x) {
            row[x * 4 + 0] = uchar(255 - x * 2);
            row[x * 4 + 1] = uchar(y * 4);
            row[x * 4 + 2] = 180;
            row[x * 4 + 3] = 255;
        }
    }

    // 5) Mono：createAlphaMask 的产物，一比特/像素
    const QImage mono = seed.createAlphaMask();

    // 6) 两种 TransformationMode 的同尺寸对照
    const QImage tinyTri = []{
        QImage t(24, 24, QImage::Format_ARGB32);
        t.fill(QColor(0, 0, 0, 0));
        QPainter p(&t);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(200, 40, 120, 255));
        QPolygonF tri;
        tri << QPointF(2, 21) << QPointF(21, 2) << QPointF(21, 21);
        p.drawPolygon(tri);
        return t;
    }();
    const QImage smoothUp = tinyTri.scaled(90, 60, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QImage fastUp = tinyTri.scaled(90, 60, Qt::KeepAspectRatio, Qt::FastTransformation);

    struct Cell { QImage img; const char *title; };
    const Cell cells[6] = {
        { seed,                                  "① ARGB32 源图" },
        { idx.convertedTo(QImage::Format_ARGB32), "② Indexed8 棋盘" },
        { gray.convertedTo(QImage::Format_ARGB32), "③ Grayscale8 渐变" },
        { rgba,                                   "④ RGBA8888 直写" },
        { mono.convertedTo(QImage::Format_ARGB32), "⑤ Mono 掩膜" },
        { smoothUp,                               "⑥ Smooth 放大" },
    };

    QPainter p(&card);
    QFont label = p.font();
    label.setPixelSize(12);
    p.setFont(label);
    const int cellW = 90, cellH = 60, mx = 18, my = 26, gapX = 30, gapY = 88;
    for (int i = 0; i < 6; ++i) {
        const int col = i % 3, rowIdx = i / 3;
        const int x0 = mx + col * (cellW + gapX);
        const int y0 = my + rowIdx * (cellH + gapY);
        // 深浅两条底：一眼分辨「真透明」与「看着白但其实不透明」
        p.fillRect(x0, y0, cellW, cellH / 2, QColor(255, 255, 255));
        p.fillRect(x0, y0 + cellH / 2, cellW, cellH - cellH / 2, QColor(226, 231, 238));
        p.drawImage(QPoint(x0, y0), cells[i].img);             // qpainter.h 353 行 drawImage(QPoint,img)
        p.setPen(QPen(QColor(150, 160, 175), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(x0, y0, cellW, cellH);
        p.setPen(QColor(40, 45, 55));
        p.drawText(QRect(x0, y0 + cellH + 2, cellW, 16), Qt::AlignLeft | Qt::AlignVCenter,
                   QString::fromUtf8(cells[i].title));
    }
    // 右下角补一张 Fast 的同尺寸放大，与⑥直接对照锯齿
    const int fx = mx + 2 * (cellW + gapX), fy = my + cellH + gapY + 22;
    p.drawImage(QPoint(fx, fy), fastUp);
    p.setPen(QColor(40, 45, 55));
    p.drawText(QRect(fx + cellW + 6, fy, 130, 30), Qt::AlignLeft | Qt::AlignVCenter,
               QString::fromUtf8("⑦ Fast 同尺寸\n（锯齿/阶梯边）"));
    p.setPen(QColor(20, 90, 60));
    p.drawText(QRect(mx, 6, 520, 16), Qt::AlignLeft,
               QStringLiteral("QImage 六种格式并排：同一套 drawImage，底纹是深浅棋盘（看透明）"));
    return card;
}

// ============================================================================
// 十一、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("painting05"));

    // 全部证据在事件循环里跑：这样 QPixmap / QPixmapCache / 控件三样都在「有应用之后」
    QTimer::singleShot(0, [&app]() {
        qInfo() << "================================================================";
        qInfo() << "painting 第 5 讲 · QImage / QPixmap / QBitmap（平台 ="
                << QGuiApplication::platformName() << "）";
        qInfo() << "================================================================";

        evidenceFormats();
        evidenceSharing();
        evidenceScaled();
        evidencePixmapAndCache();
        evidenceSaveLoad();
        evidenceHiDpi();

        // 主证据 PNG：六种格式并排画进一张 QImage
        const QImage card = makeOverviewCard();
        const bool saved = card.save(QStringLiteral("painting_05_images_and_pixmaps.png"));
        qInfo() << "【证据⑧】总览卡:" << card.size() << "format =" << int(card.format())
                << "save =" << saved
                << "非透明像素 =" << countOpaque(card)
                << "/ 面积 =" << card.width() * card.height()
                << "（整张被 fill 过，所以两个数应当相等；不相等就是 fill 没生效）";

        // 展示控件：证明「paintEvent 里只画不转」这条路走得通，并把重绘次数打印出来
        QImage content(240, 120, QImage::Format_ARGB32_Premultiplied);
        content.fill(QColor(24, 32, 48, 255));
        {
            QPainter p(&content);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(QColor(240, 200, 60), 3));
            p.drawLine(10, 110, 230, 10);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(80, 200, 140, 255));
            p.drawEllipse(90, 40, 60, 60);
        }
        ImageBoard board(content);
        board.show();

        QTimer::singleShot(400, []() {
            int full = 0, partial = 0;
            ImageBoard::paintStats(&full, &partial);
            qInfo() << "【证据⑦】控件重绘统计：整幅" << full << "次、局部" << partial
                    << "次；paintEvent 里 QPixmap::fromImage 调用次数 ="
                    << ImageBoard::fromImageCallsInPaint()
                    << "（预期 0：本讲的板子只 drawImage，不做转换）";
            qInfo() << "  → 反面结论照旧成立：**在 paintEvent 里做 fromImage，每帧一次全图转换**，"
                       "帧率直接被它吃掉；要转就在外面转好、或者干脆 drawImage 画 QImage。";
        });

        QTimer::singleShot(900, [&app]() {
            qInfo() << "【收尾】所有 PNG 已写入当前工作目录；"
                       "把上面每一行数字抄回注释里，就是你的实测。";
            QCoreApplication::quit();
        });
    });

    return app.exec();
}

// ============================================================================
// 十二、速查表与纪律（背不住就回来查）
// ============================================================================
/*
 * 一、QImage / QPixmap / QBitmap 三个「能不能」
 *   ┌────────────────┬──────────────────┬───────────────────┬────────────────┐
 *   │                │ QImage           │ QPixmap           │ QBitmap        │
 *   ├────────────────┼──────────────────┼───────────────────┼────────────────┤
 *   │ 构造要不要 app │ 不要（纯内存）   │ **要 QGuiApplication** │ 同 QPixmap │
 *   │ 随机读写像素   │ pixel/setPixel/  │ 不行，只能 toImage │ 不行          │
 *   │                │ pixelColor/scanLine │                │                │
 *   │ 能不能当画布   │ 能（QPaintDevice）│ 能（但别在别的线程）│ 一般不当画布 │
 *   │ 跨线程搬       │ 随便搬（值语义） │ **只在 GUI 线程用** │ 同 QPixmap   │
 *   │ 存成文件       │ save()           │ save()            │ 靠 toImage 后存 │
 *   │ 往控件上画     │ drawImage        │ drawPixmap（最快） │ 主要当 mask  │
 *   └────────────────┴──────────────────┴───────────────────┴────────────────┘
 *   为什么 QPixmap 不能跨线程：它是「为这块屏幕/这个窗口系统准备的位图」，
 *   平台层可能把它放在显存或按屏幕的深度/配色生成，换线程（甚至换屏）就不成立了。
 *   要在线程里做重活，就**全程用 QImage**，算完再在主线程 fromImage 一次。
 *
 * 二、三个容易写错的地方（本讲都在头文件里核过）
 *   ① 索引格式（Format_Indexed8 / Format_Mono*）**没有 setPixelIndex()**：
 *      写索引也是 `setPixel(x, y, 索引值)`，读索引用 `pixelIndex(x, y)`；
 *      同一个 setPixel 在 32 位格式里吃的是 QRgb —— 参数名 index_or_rgb 就是这意思。
 *   ② `scanLine()` 有两个版本（167 非 const / 168 const），**非 const 那个会 detach**；
 *      只想读就用 `constScanLine()`（169）或者把对象声明成 const。
 *   ③ 遍历裸内存时列上限用 `width()`、行首用 `scanLine(y)`：
 *      `bytesPerLine()` 里有行对齐填充（本讲用 w=1/5/18/19 的四张表打出来了）。
 *
 * 三、scaled 的两组参数怎么选
 *   IgnoreAspectRatio         把宽高硬拉到目标 → 图会变形，做纹理/拉伸背景才用
 *   KeepAspectRatio           取小比例 → 完整装下，四周留空（做缩略图就用它）
 *   KeepAspectRatioByExpanding 取大比例 → 铺满目标但**超出**，要配合 copy() 裁一刀
 *   FastTransformation        最近邻：快、硬边、放大后是马赛克
 *   SmoothTransformation      双线性一类：慢（本讲实测比值），但斜边/曲线必须用
 *   经验：缩放**只发生在「资源规格 ≠ 显示规格」时**。做图标就备齐 1x/2x 两套规格，
 *   别在 paintEvent 里每次 scaled(带 Smooth)。
 *
 * 四、QPixmapCache 的正确期待
 *   它是个「按字节限额的、进程内、GUI 线程」的位图缓存：
 *     · setCacheLimit(int) 单位是 KB；超了会淘汰（本讲用 40 张大图把 big_0 挤掉来证实）；
 *     · insert 有两个版本：给你自己的 QString 键（返回 bool），或者给你一个 Key（返回 Key）；
 *     · Key 比 QString 快（不用哈希字符串），但 Key 一旦 remove/clear 就失效，find 会 false；
 *     · replace() Qt6.6 起弃用；别指望它当「图片资源池」——那是你自己 QHash<QIcon> 的活。
 *
 * 五、高 DPI 的一条主线（第 1 讲的续集）
 *   物理像素 = 逻辑尺寸 × devicePixelRatio。
 *   QImage 侧：建一张「物理尺寸」的图（400x400），setDevicePixelRatio(2.0)，
 *              之后 QPainter 全用逻辑坐标画 → 出来就是「同样大小、更细」的那张。
 *   资源侧：从文件读来的 1x 图标**不要**给它 setDevicePixelRatio(2) 来「假装高清」，
 *            那只会让它在屏幕上变大/变糊；要么按 DPR 选 2x 资源，要么用 SVG（第 8 讲之外）。
 *   控件侧：读 DPR 用继承来的 QPaintDevice::devicePixelRatio()（qpaintdevice.h 52 行），
 *            或 windowHandle()->devicePixelRatio()（qwidget.h 646 行有 windowHandle()）。
 *            **本机 QtWidgets/qwidget.h 里搜不到 devicePixelRatio / devicePixelRatioF 这两个名字**，
 *            所以 Qt5 那句 `widget->devicePixelRatioF()` 在 Qt6 别再照抄（这条是头文件证据，
 *            不是我背的文档）。
 *
 * ——— 老师傅的话 ———
 *   徒弟，这一讲的干货其实就一句话：**先分清「内存里的图」和「屏幕上的图」，
 *   再谈画得快不快。**
 *   我见过太多人把 QPixmap 当普通值类型使，在工作线程里生成一堆 QPixmap 缓存，
 *   平时好得好好的，一到多显示器或者换缩放比就随机崩。你记住：QPixmap 的命是
 *   窗口系统给的，它跟着 GUI 线程走； QImage 才是你自己的东西。
 *
 *   第二件事：不要小看「只读也会 detach」这条。你写一个 countOpaque()、
 *   传参写成 QImage img（值传），或者调了非 const 的 scanLine()，
 *   整幅 4K 图当场复制一份，还不报错——它只是慢。所以我在证据②里
 *   专门用 constScanLine 与非 const scanLine 各来一次，把 isDetached 的翻转打出来。
 *   以后你 review 别人的绘图代码，第一件事就是看**跨不跨线程、按不按引用传 const**。
 *
 *   第三件事：证据③那个耗时比值，我一句「Smooth 是 Fast 的 N 倍」都没写死。
 *   机器不同就差得远。我留的是「跑一次、把数字抄回注释」的动作——绘图这一行的
 *   经验必须是**你自己机器上的数字**，别人的数字只能当参考。
 *   还有：这讲我把 fromImage 的错误姿势做成了一个可开关的计数器（enableBadDemo），
 *   你练习时把它打开，亲眼看看帧计数怎么涨，比我说十遍都有效。
 *
 * ——— 小练习 ———
 *   1. 把证据①里的 Format_ARGB32 换成 Format_RGBA8888，重新打印 hexRow()：
 *      四个字节顺序会和 ARGB32 一样吗？据此写一段「不依赖字节序地把 QImage
 *      喂给别的图像库」的转换代码（提示：先 convertToFormat 到你要的格式，
 *      再用 pixelFormat() 的 offset/channel 描述取字节）。
 *   2. 证据②里把 `b.setPixelColor(...)` 换成 `std::as_const(b).constScanLine(0)` 与
 *      `b.scanLine(0)` 两种写法各跑一次，把三个 isDetached 的值列成表；
 *      再用 bits() 相等与否交叉验证，写一句「什么操作会复制」的结论。
 *   3. 把 ImageBoard 改成「构造时只做一次 QPixmap::fromImage、paintEvent 里 drawPixmap」，
 *      然后调用 ImageBoard::enableBadDemo() 重跑，比较【证据⑦】两个计数；
 *      再试第三条：paintEvent 里直接 drawImage(QPointF(0,0), m_source)。
 *      说明为什么后两种在「图大 + 重绘频繁」时差出一个数量级。
 */
// ============================================================================

// 本文件没有 Q_OBJECT 类，因此不需要 #include "05_images_and_pixmaps.moc"。
