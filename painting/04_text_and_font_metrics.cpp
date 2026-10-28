// ============================================================================
// 第 4 讲 · 文本度量（QFont / QFontMetrics）与基线
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   文本是自定义控件里最容易「差一点点」的绘制。本讲把底座打平，全部结论都用打印出来
//   的数字支撑：
//     1) QFont 的 setPixelSize 与 setPointSize **不是一回事**：前者是设备像素，
//        后者要经 (dpi/72) 换算；两条各设一次，把对方的读取值（预期是 -1）与
//        两种字体的高度并排打出来，用「18pt 在 96dpi 下等于 24px」这件事实测确认；
//     2) QFontMetrics / QFontMetricsF 的 ascent、descent、height、leading、lineSpacing、
//        capHeight、boundingRect、tightBoundingRect、horizontalAdvance、size：逐个打印，
//        并**在代码里自己判定** height() 是否等于 ascent()+descent()（不背公式）；
//     3) Qt5 → Qt6 必考差异：**QFontMetrics::width() 在 Qt6 已不存在**，改成
//        horizontalAdvance()；fontDpi() 从 int 变 qreal（分数 DPI）；
//        想要确定的点→像素数字，就用 QFontMetrics(font, paintDevice) 显式指定设备；
//     4) drawText 的对齐标志与两种矩形版本（QRect / QRectF），以及 out 参数
//        boundingRect*（让 Qt 自己告诉你它把文字摆哪儿了）；
//     5) elidedText（**注意名字里有个 d**）与 Qt::TextElideMode 的四档；
//     6) 多行：Qt::TextWordWrap 的 flags 版 vs QTextOption 的完整版，各自配一个
//        QPainter::boundingRect 重载，把两种排版结果的矩形都打出来；
//     7) 「基线（baseline）」为什么和矩形中心差半个行高：三种摆法各画一遍、
//        量出实际着墨的重心 y，把「我以为的对齐」和「真实的对齐」差值打印出来。
//
// 对应构建目标名：painting_04_text_and_font_metrics
//   find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)
//   add_executable(painting_04_text_and_font_metrics 04_text_and_font_metrics.cpp)
//   target_link_libraries(painting_04_text_and_font_metrics PRIVATE Qt6::Widgets Qt6::Svg)
//   set(CMAKE_AUTOMOC ON)
//   本文件不含 Q_OBJECT（只覆写 paintEvent），末尾不需要 .moc。
//
// 运行前须知：
//   · 会往当前工作目录写 painting_04_text_and_font_metrics.png（对齐/基线对照卡）。
//   · **不要硬编码字体族名**：不同机器上没装那个字族，度量数字就会变。
//     本讲全程用应用的默认字族，只改大小与字重，这样数字才可复现。
//   · 无人值守：证据都走 QImage（第 1 讲铁律一），控件只展示一次，
//     QTimer::singleShot 收尾并 QCoreApplication::quit()。
//   · 本机当前无法编译链接 Qt 运行时，本示例**未在本机实测编译**；
//     全部签名对着 C:\msys64\ucrt64\include\qt6 的头文件核对过（下面带行号）。
//     本机另外只跑过 `g++ -fsyntax-only -I <Qt6 头文件目录>`：纯语法核对
//     （不产出目标文件、**没有链接、没有运行、没有可执行文件**），
//     它只证明这些调用对着 Qt6 头文件编得过，不证明度量数字对。
//     涉及「Qt5 与 Qt6 行为不同」的地方，我只写**头文件里能看见的证据**（例如本机
//     qfontmetrics.h 里没有 width()、fontDpi() 是 qreal），文档口径的说明会标明是文档说的。
//   · 具体像素数字依赖你的字体与 DPI，**跑出来的数字要抄回注释里**，别照抄我的推测。
//   · 图解配套：../visuals/painting_pipeline.html（文本绘制管线与基线分步图，
//     页面由图解同事补，链接先放着）。
// ============================================================================

#include <QApplication>
#include <QWidget>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QColor>
#include <QImage>
#include <QFont>               // QtGui/qfont.h
#include <QFontMetrics>        // QtGui/qfontmetrics.h：QFontMetrics 与 QFontMetricsF **在同一个头文件里**
                                 // （所以本文件不需要、也不该再单独 include <QFontMetricsF>）
#include <QTextOption>         // QtGui/qtextoption.h
#include <QPaintDevice>        // QtGui/qpaintdevice.h：logicalDpiX/Y
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QCoreApplication>
#include <QDebug>

// ============================================================================
// 一、核对签名（本机 Qt6）
// ============================================================================
// QtGui/qfont.h
//   第 65~74 行  enum Weight { Thin = 100, ExtraLight = 200, Light = 300, Normal = 400,
//                  Medium = 500, DemiBold = 600, Bold = 700, ExtraBold = 800, Black = 900 };
//   第 152/153 行 QString family() const; void setFamily(const QString &);
//   第 161/162 行 int pointSize() const; void setPointSize(int);
//   第 163/164 行 qreal pointSizeF() const; void setPointSizeF(qreal);   ← 分数磅值（排版引擎给的）
//   第 166/167 行 int pixelSize() const; void setPixelSize(int);
//   第 169/170 行 Weight weight() const; void setWeight(Weight);
//   第 172/173 行 bool bold() const; void setBold(bool);   ← 375~377 行的内联实现就是 setWeight(Bold/Normal)
//   第 179 行     void setItalic(bool b);
//   第 198/199 行 void setStyleHint(StyleHint, StyleStrategy = PreferDefault); void setStyleStrategy(StyleStrategy s);
//   第 319/320 行 setLegacyWeight()/legacyWeight() 已被标 QT_DEPRECATED_VERSION_X_6_0("Use setWeight() instead")
//                  ← 这条是看得见的 Qt5→Qt6 差异：老代码 setLegacyWeight/整型 weight 要换成 Weight 枚举
// QtGui/qfontmetrics.h（int 版，度量单位=像素）
//   第 23 行  explicit QFontMetrics(const QFont &);
//   第 24 行  QFontMetrics(const QFont &font, const QPaintDevice *pd);   ← **指定设备**，数字才可复现
//   第 35 行  int ascent() const;        第 36 行 int capHeight() const;   第 37 行 int descent() const;
//   第 38 行  int height() const;        第 39 行 int leading() const;     第 40 行 int lineSpacing() const;
//   第 42 行  int maxWidth() const;      第 45/46 行 int xHeight()/averageCharWidth();
//   第 48/49 行 bool inFont(QChar)/inFontUcs4(uint);
//   第 51/52 行 int leftBearing(QChar)/rightBearing(QChar);
//   第 54 行  int horizontalAdvance(const QString &, int len = -1) const;
//   第 55 行  int horizontalAdvance(const QString &, const QTextOption &) const;
//   第 56 行  int horizontalAdvance(QChar) const;
//   第 58/60/61 行 QRect boundingRect(QChar)/(const QString&)/(const QString&, const QTextOption&) const;
//   第 62 行  QRect boundingRect(const QRect &r, int flags, const QString &text,
//                  int tabstops = 0, int *tabarray = nullptr) const;
//   第 64 行  inline QRect boundingRect(int x, int y, int w, int h, int flags, const QString &, ...);
//   第 66 行  QSize size(int flags, const QString& str, int tabstops = 0, int *tabarray = nullptr) const;
//   第 68 行  QRect tightBoundingRect(const QString &text) const;
//   第 71 行  QString elidedText(const QString &text, Qt::TextElideMode mode, int width, int flags = 0) const;
//   第 73~76 行 int underlinePos()/overlinePos()/strikeOutPos()/lineWidth();
//   第 78 行附近 qreal fontDpi() const;   ← **Qt5 这里是 int，Qt6 变 qreal**（分数 DPI 的直接证据）
//   整份头文件里**没有** `int width(...)` —— Qt6 已删除，只留 maxWidth()。老代码必须改名。
//   QFontMetricsF（同文件 95 行起，度量单位=qreal，**不取整**）
//   第 95/96 行 explicit QFontMetricsF(const QFont &); QFontMetricsF(const QFont&, const QPaintDevice *);
//   第 108/110/111/112/113 行 qreal ascent()/descent()/height()/leading()/lineSpacing() const;
//   第 127/128/129 行 qreal horizontalAdvance(const QString&, int)/ (QChar)/ (const QString&, const QTextOption&);
//   第 131/132/133/134 行 QRectF boundingRect(...) 四个版本
//   第 137/138 行 QRectF tightBoundingRect(...)
//   第 140 行 QString elidedText(const QString&, Qt::TextElideMode, qreal width, int flags = 0) const;
//                  ← 注意宽度参数是 qreal（int 版是 int）
// QtGui/qpainter.h（文本相关，行号同第 1/2 讲那份）
//   第 368 行  void drawText(const QPointF &p, const QString &s);      ← p 是**基线**起点
//   第 369 行  inline void drawText(const QPoint &p, const QString &s);
//   第 370 行  inline void drawText(int x, int y, const QString &s);
//   第 372 行  void drawText(const QPointF &p, const QString &str, int tf, int justificationPadding);
//   第 374 行  void drawText(const QRectF &r, int flags, const QString &text, QRectF *br = nullptr);
//   第 375 行  void drawText(const QRect &r, int flags, const QString &text, QRect *br = nullptr);
//   第 376 行  inline void drawText(int x, int y, int w, int h, int flags, const QString &text, QRect *br = nullptr);
//   第 378 行  void drawText(const QRectF &r, const QString &text, const QTextOption &o = QTextOption());
//   第 380 行  QRectF boundingRect(const QRectF &rect, int flags, const QString &text);
//   第 381 行  QRect boundingRect(const QRect &rect, int flags, const QString &text);
//   第 384 行  QRectF boundingRect(const QRectF &rect, const QString &text, const QTextOption &o = QTextOption());
//   第 148 行  void setFont(const QFont &f);
// QtGui/qtextoption.h
//   第 47/48 行 QTextOption(); Q_IMPLICIT QTextOption(Qt::Alignment alignment);
//   第 54 行   inline void setAlignment(Qt::Alignment alignment);
//   第 60~66 行 enum WrapMode { ... WrapAtWordBoundaryOrAnywhere, ... }; 第 67 行 setWrapMode(WrapMode);
//   第 70/81 行 enum Flag { ... }; inline void setFlags(Flags flags);
// QtCore/qnamespace.h
//   第 155~176 行 enum AlignmentFlag { AlignLeft = 0x0001, AlignRight = 0x0002, AlignHCenter = 0x0004,
//                  AlignJustify = 0x0008, AlignAbsolute = 0x0010, AlignTop = 0x0020, AlignBottom = 0x0040,
//                  AlignVCenter = 0x0080, AlignBaseline = 0x0100, AlignCenter = AlignVCenter|AlignHCenter };
//   第 171~174 行 头文件里的原注释值得抄给学员：AlignBaseline 与 TextSingleLine 都是 0x100 会撞车，
//                  而「AlignBaseline 只给布局用，传给 QPainter::drawText() 没有意义」——
//                  也就是说**别指望用 AlignBaseline 去对齐文字**。
//   第 183~196 行 enum TextFlag { TextSingleLine = 0x0100, TextDontClip = 0x0200, TextExpandTabs = 0x0400,
//                  TextShowMnemonic = 0x0800, TextWordWrap = 0x1000, TextWrapAnywhere = 0x2000,
//                  TextDontPrint = 0x4000, ..., TextHideMnemonic = 0x8000, ... };
//   第 200~205 行 enum TextElideMode { ElideLeft, ElideRight, ElideMiddle, ElideNone };
//   第 198 行   Q_DECLARE_MIXED_ENUM_OPERATORS_SYMMETRIC(int, AlignmentFlag, TextFlag);
//                  ← 这行保证 `Qt::AlignLeft | Qt::TextWordWrap` 能安全地当 int flags 传给 drawText
//   ⚠ 但**两个 AlignmentFlag 相加**（如 AlignLeft | AlignVCenter）得到的是 QFlags<AlignmentFlag>，
//     而 drawText/boundingRect 的形参是裸 `int flags`。Qt 各版本对 QFlags→int 的隐式转换并不保证，
//     所以本文件统一写成 `int(Qt::AlignLeft) | int(Qt::AlignVCenter)` ——
//     无作用域枚举转 int 是 C++ 标准规定的，**任何 Qt 版本都不会出问题**。

// ============================================================================
// 二、像素证据工具
// ============================================================================
struct InkBox
{
    int count = 0;
    int minY = 1 << 30, maxY = -(1 << 30);
    int minX = 1 << 30, maxX = -(1 << 30);
    long long sumY = 0;

    int centroidY() const { return count ? int(sumY / count) : -1; }
    int inked() const { return count; }
};

// 在指定矩形范围内量不透明像素（用于「哪个摆法把字放在了哪儿」）
static InkBox measureInk(const QImage &img, const QRect &area)
{
    InkBox b;
    const int x1 = qMin(area.right(), img.width() - 1);   // qglobal.h 的 qMin/qMax：老老朋友，不另核
    const int y1 = qMin(area.bottom(), img.height() - 1);
    for (int y = qMax(area.top(), 0); y <= y1; ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));   // qimage.h 169 行
        for (int x = qMax(area.left(), 0); x <= x1; ++x) {
            if ((row[x] >> 24) != 0) {
                ++b.count;
                b.sumY += y;
                if (y < b.minY) b.minY = y;
                if (y > b.maxY) b.maxY = y;
                if (x < b.minX) b.minX = x;
                if (x > b.maxX) b.maxX = x;
            }
        }
    }
    return b;
}

static int countInk(const QImage &img)
{
    return measureInk(img, QRect(0, 0, img.width(), img.height())).inked();
}

// 「只数深色像素」的版本：证据⑤ 的框线是浅灰、基线是红、文字是接近黑的深色，
// 直接数不透明像素会把框线和基线一起算进去（那条横线会把重心拽到基线上），
// 所以量文字重心必须先按颜色把干扰项滤掉。**这是本讲唯一需要小心的统计技巧。**
static InkBox measureTextInk(const QImage &img, const QRect &area)
{
    InkBox b;
    const int x1 = qMin(area.right(), img.width() - 1);
    const int y1 = qMin(area.bottom(), img.height() - 1);
    for (int y = qMax(area.top(), 0); y <= y1; ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = qMax(area.left(), 0); x <= x1; ++x) {
            const QColor c = QColor::fromRgba(row[x]);      // qcolor.h 170 行
            const int mx = qMax(c.red(), qMax(c.green(), c.blue()));
            if (c.alpha() > 0 && mx < 80) {                 // 深色 → 认定是文字墨迹
                ++b.count;
                b.sumY += y;
                if (y < b.minY) b.minY = y;
                if (y > b.maxY) b.maxY = y;
                if (x < b.minX) b.minX = x;
                if (x > b.maxX) b.maxX = x;
            }
        }
    }
    return b;
}

// ============================================================================
// 三、证据 ①：pixelSize 与 pointSize 到底是两回事
// ============================================================================
static void proofFontUnits(const QPaintDevice *pd)
{
    qInfo() << "\n=== 证据① setPixelSize(24) vs setPointSize(18) ===";
    qInfo() << "  设备 logicalDpi =" << pd->logicalDpiX() << "x" << pd->logicalDpiY()   // qpaintdevice.h 48/49 行
            << " devicePixelRatio =" << pd->devicePixelRatio();                         // qpaintdevice.h 52 行

    QFont byPixel;
    byPixel.setPixelSize(24);                        // qfont.h 167 行
    QFont byPoint;
    byPoint.setPointSize(18);                        // qfont.h 162 行
    qInfo() << "  byPixel （setPixelSize(24)）：pixelSize() =" << byPixel.pixelSize()
            << " pointSize() =" << byPixel.pointSize()
            << "（**用 pixelSize 设过之后，pointSize() 预期读到 -1**，那是「单位未定」的哨兵值）";
    qInfo() << "  byPoint （setPointSize(18)）：pointSize() =" << byPoint.pointSize()
            << " pixelSize() =" << byPoint.pixelSize()
            << "（预期同样是 -1；像素尺寸要**问度量才知道**，QFont 自己不知道）";

    // 关键：把度量算子绑到**具体设备**上（qfontmetrics.h 24 行），数字才可复现。
    const QFontMetrics fmPixel(byPixel, pd);
    const QFontMetrics fmPoint(byPoint, pd);
    qInfo() << "  同一台设备上：24px 字体 height() =" << fmPixel.height()
            << "，18pt 字体 height() =" << fmPoint.height();
    qInfo() << "  两者相等吗 =" << (fmPixel.height() == fmPoint.height())
            << "；fontDpi() 分别 =" << fmPixel.fontDpi() << fmPoint.fontDpi()
            << "（**fontDpi() 在 Qt6 是 qreal**，分数 DPI 会打出小数）";
    qInfo() << "  换算关系：像素 = 磅 × dpi / 72。请按你机器上的 fontDpi 手算 18 × dpi / 72，"
            << "和上面 18pt 的高度对一下；**dpi 不是 96 时两者就不会相等**——"
            << "这就是「同一份 UI 在不同机器上字大小不一致」的全部原因。";
    qInfo() << "  实务结论：控件里想「字号恒定」→ setPixelSize；想「跟随系统字号/DPI」→ setPointSize；"
            << "两者混用时，**永远不要假设 pointSize 与 pixelSize 都能读到有效值**。";

    const QFontMetricsF fmp(byPixel, pd);            // qfontmetrics.h 96 行
    qInfo() << "  QFontMetricsF（不取整）版本：ascentF =" << fmp.ascent() << " heightF =" << fmp.height()
            << "；对应的 int 版 ascent =" << fmPixel.ascent() << " height =" << fmPixel.height()
            << "（差值来自取整——做居中对齐时这种 1px 抖动就是「文字总差一格」的病根）";
}

// ============================================================================
// 四、证据 ②：QFontMetrics 的一整排字段
// ============================================================================
static void proofMetricsFields(const QPaintDevice *pd)
{
    qInfo() << "\n=== 证据② ascent/descent/height/leading/lineSpacing/capHeight/xHeight ===";
    QFont f;
    f.setPixelSize(28);                              // 只用像素尺寸，避免 DPI 干扰这一组数字
    f.setWeight(QFont::Normal);                      // qfont.h 170 行 + Weight 枚举 65~74 行
    const QFontMetrics fm(f, pd);                    // qfontmetrics.h 24 行

    qInfo() << "  ascent =" << fm.ascent()            // 35 行：基线**往上**多少（正数）
            << " descent =" << fm.descent()          // 37 行：基线**往下**多少（正数，向下为正）
            << " height =" << fm.height()            // 38 行
            << " leading =" << fm.leading()          // 39 行
            << " lineSpacing =" << fm.lineSpacing(); // 40 行
    qInfo() << "  capHeight =" << fm.capHeight()      // 36 行：大写字母高度（做「视觉居中」时比 height() 好用）
            << " xHeight =" << fm.xHeight()          // 45 行：小写 x 高度
            << " averageCharWidth =" << fm.averageCharWidth()
            << " maxWidth =" << fm.maxWidth();       // 42 行：全字宽上限
    qInfo() << "  自检（代码里自己判，不背公式）：ascent+descent =" << (fm.ascent() + fm.descent())
            << " 与 height =" << fm.height() << " 是否相等 =" << (fm.ascent() + fm.descent() == fm.height())
            << "; lineSpacing - height =" << (fm.lineSpacing() - fm.height())
            << " 这一列就是实际的行间距（leading 的说法请以这两行为准）";
    qInfo() << "  画一条「行盒」时要记住：顶边 = 基线 - ascent，底边 = 基线 + descent。"
            << "Qt 的 descent 已经是正数，**不要再写成 -descent**。";
    qInfo() << "  下划线/删除线的位置也是现成的：underlinePos =" << fm.underlinePos()
            << " strikeOutPos =" << fm.strikeOutPos() << " lineWidth =" << fm.lineWidth(); // 73~76 行
}

// ============================================================================
// 五、证据 ③：advance 与 boundingRect 的区别（Qt6 改名那条）
// ============================================================================
static void proofAdvanceVsBounds(const QPaintDevice *pd)
{
    qInfo() << "\n=== 证据③ horizontalAdvance / boundingRect / tightBoundingRect ===";
    QFont f;
    f.setPixelSize(32);
    const QFontMetrics fm(f, pd);
    const QString s = QStringLiteral("Hangping 评测");

    const int adv = fm.horizontalAdvance(s);         // 54 行 —— **Qt6 的正式名字**
    const QRect box = fm.boundingRect(s);            // 60 行：墨迹的外接矩形（相对基线，x/y 可负）
    const QRect tight = fm.tightBoundingRect(s);     // 68 行：贴着墨迹量的
    const QSize sz = fm.size(Qt::TextSingleLine, s); // 66 行：排版推进用的尺寸（Qt5 里叫 size，Qt6 保留）
    qInfo() << "  horizontalAdvance(\"" << s << "\") =" << adv << " px —— 这是「笔往下走多少」";
    qInfo() << "  boundingRect = " << box << "（x=" << box.x() << " y=" << box.y()
            << " w=" << box.width() << " h=" << box.height() << "）—— y 通常是**负数**，因为它以基线为原点向上";
    qInfo() << "  tightBoundingRect =" << tight << "；size(TextSingleLine) =" << sz;
    qInfo() << "  advance 与 boundingRect().width() 的差 =" << (adv - box.width())
            << "（首尾留白/边距 bearing 造成的，别把两者当同一个数用）";
    qInfo() << "  单个大写字母也能量（用 54 行的字符串版，别去赌 QChar 的隐式转换）："
            << "horizontalAdvance(\"H\") =" << fm.horizontalAdvance(QStringLiteral("H"))
            << "，horizontalAdvance(\"i\") =" << fm.horizontalAdvance(QStringLiteral("i"))
            << " → 两者不相等就是比例字体的证据；等宽字体才会一样。"
            << "（想拿首尾留白还有一条 leftBearing/rightBearing，在 qfontmetrics.h 第 51/52 行，"
            << "参数是 QChar，本讲不调用它。）";
    qInfo() << "  Qt5→Qt6 必考：Qt5 里这些叫 fm.width(s)；**本机 Qt6 的 qfontmetrics.h 里没有 width()**，"
            << "只有 horizontalAdvance() 与 maxWidth()。老工程升级时 width() 是编译错误，"
            << "改名要改干净（还有 QFontMetricsF 的同名函数，127~129 行）。";
    qInfo() << "  另外 QFont::setLegacyWeight(int)/legacyWeight() 在本机头文件里已带 "
            << "QT_DEPRECATED_VERSION_X_6_0 标记（qfont.h 319/320 行）→ 请用 setWeight(QFont::Bold) 这样的枚举。";
}

// ============================================================================
// 六、证据 ④：elidedText 三种截断
// ============================================================================
static void proofElide(const QPaintDevice *pd)
{
    qInfo() << "\n=== 证据④ elidedText（名字里有 d）与 Qt::TextElideMode ===";
    QFont f;
    f.setPixelSize(20);
    const QFontMetrics fm(f, pd);
    const QString text = QStringLiteral("Qt 从入门到资深 · 绘图与自定义控件 · 第伍阶段");
    const int full = fm.horizontalAdvance(text);
    const int avail = 180;
    qInfo() << "  原文完整宽度 =" << full << " 可用宽度 =" << avail;

    // 核对签名：qfontmetrics.h 71 行
    //   QString elidedText(const QString &text, Qt::TextElideMode mode, int width, int flags = 0) const;
    const Qt::TextElideMode modes[4] = { Qt::ElideLeft, Qt::ElideRight, Qt::ElideMiddle, Qt::ElideNone };
    const char *modeNames[4] = { "ElideLeft", "ElideRight", "ElideMiddle", "ElideNone" };
    for (int i = 0; i < 4; ++i) {
        const QString el = fm.elidedText(text, modes[i], avail);       // 枚举名见 qnamespace.h 200~205 行
        qInfo() << "  " << modeNames[i] << "（枚举值 " << int(modes[i]) << "）→ \"" << el << "\""
                << " 实际宽度 =" << fm.horizontalAdvance(el)
                << " 是否放得下 =" << (fm.horizontalAdvance(el) <= avail);
    }
    qInfo() << "  两个坑：① 名字是 elidedText（过去分词），**不是 elideText**，写错就是「找不到成员」；"
            << "② flags 默认是 0，不是 Qt::TextShowMnemonic —— 带 & 的文本想让 & 生效要显式传 flags，"
            << "否则省略计算会把 & 当普通字符数进去。";
    qInfo() << "  ③ ElideNone 只是「原文返回」，**不会**替你截断，放得下与否自己负责；"
            << "真正的裁剪要靠 painter 的裁剪区（第 7 讲）。";
}

// ============================================================================
// 七、证据 ⑤：三种摆法 + 基线的真实落点（存 PNG 的那张卡）
// ============================================================================
static void proofBaselineAndAlign(QImage &card)
{
    qInfo() << "\n=== 证据⑤ 基线 / 中心 / AlignVCenter 的实测差值 ===";
    QFont f;
    f.setPixelSize(26);
    {
        QPainter p(&card);
        p.setFont(f);                                                // qpainter.h 148 行
        const QFontMetrics fm(f, &card);                              // 度量也绑到**这块画布**上（24 行）
        const QString s = QStringLiteral("Hg 基线");

        const int boxX = 12, boxW = 380, boxH = 70;
        int boxY = 12;

        // 摆法 A：把「基线」摆在 boxY + ascent —— 这就是「行盒」的正常排版位置
        const int baselineA = boxY + fm.ascent();
        p.setPen(QColor(210, 210, 220));
        p.setBrush(Qt::NoBrush);
        p.drawRect(boxX, boxY, boxW, boxH);
        p.setPen(QPen(QColor(220, 60, 60), 1.0));
        p.drawLine(boxX, baselineA, boxX + boxW, baselineA);          // 把基线画出来，肉眼核对
        p.setPen(QColor(20, 20, 20));
        p.drawText(boxX + 6, baselineA, s);                           // 370 行：(int x, int y, text)，y 是基线

        // 摆法 B：把基线摆在矩形的**垂直中线**上 —— 新手最常见的错（以为 y 是「中心」）
        boxY += boxH + 14;
        const int baselineB = boxY + boxH / 2;
        p.setPen(QColor(210, 210, 220));
        p.drawRect(boxX, boxY, boxW, boxH);
        p.setPen(QPen(QColor(220, 60, 60), 1.0));
        p.drawLine(boxX, baselineB, boxX + boxW, baselineB);
        p.setPen(QColor(20, 20, 20));
        p.drawText(boxX + 6, baselineB, s);

        // 摆法 C：交给 Qt —— drawText(QRect, flags, text) 的 AlignLeft|AlignVCenter，并索取 br
        boxY += boxH + 14;
        p.setPen(QColor(210, 210, 220));
        p.drawRect(boxX, boxY, boxW, boxH);
        QRect got;                                                     // out 参数：Qt5/Qt6 都有（375 行）
        p.setPen(QColor(20, 120, 40));
        p.drawText(QRect(boxX + 6, boxY, boxW - 12, boxH),
                   int(Qt::AlignLeft) | int(Qt::AlignVCenter), s, &got);   // qnamespace.h 155~176 行的标志组合
        qInfo() << "  A：基线放在 boxY+ascent =" << baselineA << "（ascent =" << fm.ascent() << "）";
        qInfo() << "  B：基线放在 boxY+boxH/2 =" << baselineB << "（boxH/2 =" << boxH / 2 << "）";
        qInfo() << "  C：Qt 自己算的 boundingRect（drawText 的 out 参数）=" << got;
        qInfo() << "  行盒：height() =" << fm.height() << " ascent =" << fm.ascent()
                << " descent =" << fm.descent()
                << " → 半行高 = height()/2 =" << fm.height() / 2;
        qInfo() << "  **A 与 B 的基线差就是 (ascent - height/2) 那一截**（ascent+descent 与 height 的关系见证据②），"
                << "字越大偏得越多；C 是 Qt 摆的，把 got 的 top 与 A 的行盒顶边对一下就知道 Qt 按哪个基准居中。";

        // 多行：flags 版（TextWordWrap）与 QTextOption 版并排，两个 boundingRect 都打出来
        const QString para = QStringLiteral("绘图与自定义控件这一阶段要把像素、坐标、度量三件事理顺，"
                                            "否则控件放大就糊、字体一大就溢出。");
        boxY += boxH + 16;
        const QRect leftArea(boxX, boxY, 180, 130);
        p.setPen(QColor(210, 210, 220));
        p.drawRect(leftArea);
        QRect brFlags;
        p.setPen(QColor(30, 30, 30));
        // 375/376 行：矩形 + int flags 版本。官方文档口径：TextWordWrap 只在**词边界**断行，
        // 而中文没有空格 —— 断得开断不开，**看下面 needed 的高度就知道**，别信我的转述。
        const QRect needed = fm.boundingRect(leftArea, int(Qt::AlignLeft) | int(Qt::TextWordWrap), para); // 62 行
        p.drawText(leftArea, int(Qt::AlignLeft) | int(Qt::TextWordWrap), para, &brFlags);
        qInfo() << "  多行(flags)：这段文本**需要的**矩形 boundingRect(QRect,flags,text) =" << needed
                << "（area 高 =" << leftArea.height() << "）→ 若 needed.height() > area.height()，"
                << "说明这块放不下，超出部分会被 area 裁掉";
        qInfo() << "           实到的 br（drawText 的 out 参数）=" << brFlags;

        const QRectF rightArea(QRectF(boxX + 190, boxY, 190, 130));
        p.setPen(QColor(210, 210, 220));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rightArea);
        QTextOption opt;                                               // qtextoption.h 47 行
        opt.setAlignment(Qt::AlignLeft);                               // 54 行
        opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);     // 60~67 行：允许在任意位置断行（中文必需）
        p.setPen(QColor(30, 30, 30));
        p.drawText(rightArea, para, opt);                              // 378 行：QRectF + QTextOption 版本
        const QRectF neededF = p.boundingRect(rightArea, para, opt);   // 384 行
        qInfo() << "  多行(QTextOption)：boundingRect(QRectF,text,opt) =" << neededF;
        qInfo() << "  两条并排看：flags 版走的是**简单换行**（官方文档口径：不支持制表符、缩进与完整复杂文字布局），"
                << "QTextOption 版才是「带排版选项的完整版本」。上面两组 needed/实测矩形就是证据："
                << "中文这种没有空格的文本能不能断行，**以你跑出来的数字为准**，"
                << "我的建议是——需要可靠换行就上 QTextOption"
                << "（想再快一步是 QStaticText：qpainter.h 第 364~366 行有一整组 drawStaticText 重载，"
                << "本讲不展开，属于文本性能专题）。";
    }

    // 逐块测量：A / B 两个框里**文字**的着墨重心 y（用深色过滤，避开灰框与红色基线）
    struct Named { const char *name; QRect area; };
    const Named probes[2] = {
        { "A(基线 = boxY + ascent)", QRect(12, 12, 380, 70) },
        { "B(基线 = boxY + boxH/2)", QRect(12, 96, 380, 70) },
    };
    for (int i = 0; i < 2; ++i) {
        const InkBox b = measureTextInk(card, probes[i].area);
        qInfo() << "  实测" << probes[i].name << "：文字墨迹 y[" << b.minX << " x" << b.minY
                << " 到 " << b.maxX << " x" << b.maxY << "] 重心 y =" << b.centroidY()
                << " 墨迹像素 =" << b.inked();
    }
    const InkBox a = measureTextInk(card, probes[0].area);
    const InkBox b = measureTextInk(card, probes[1].area);
    qInfo() << "  两种摆法的墨迹重心差 =" << (b.centroidY() - a.centroidY())
            << "px；两个框的几何中心都应该是各自 boxY+boxH/2 —— "
            << "谁落在框正中、谁偏上了半行，看这三行数字就有答案。";
}

// ============================================================================
// 八、控件：同样的文本摆法搬到窗口上
// ============================================================================
class Stage : public QWidget
{
public:
    explicit Stage(QWidget *parent = nullptr) : QWidget(parent) {}

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter p(this);                                              // 第 1 讲铁律一
        p.fillRect(0, 0, width(), height(), QColor(248, 248, 252));
        QFont f = font();                                              // QApplication 给的全局字族（不硬编码）
        f.setPixelSize(22);
        p.setFont(f);
        const QFontMetrics fm(f, this);                                // qfontmetrics.h 24 行：绑到本控件
        const QString s = QStringLiteral("基线演示 Hg");
        const QRect r(10, 10, width() - 20, 60);
        p.setPen(QColor(200, 200, 210));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r);
        p.setPen(QColor(220, 60, 60));
        p.drawLine(r.left(), r.top() + fm.ascent(), r.right(), r.top() + fm.ascent());
        p.setPen(QColor(20, 20, 20));
        p.drawText(r, int(Qt::AlignLeft) | int(Qt::AlignVCenter), s);
        qInfo() << "  Stage::paintEvent：ascent =" << fm.ascent() << " height =" << fm.height()
                << " fontDpi =" << fm.fontDpi() << " devicePixelRatio =" << devicePixelRatio();
    }
};

// ============================================================================
// 九、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 版式预算：A 框 y=12..82，B 框 96..166，C 框 180..250，两段多行文本 264..394 → 画布高 410 起
    QImage card(420, 410, QImage::Format_ARGB32_Premultiplied);
    card.fill(Qt::white);                          // qimage.h 199 行：fill(Qt::GlobalColor)

    // 度量需要一个 QPaintDevice：QImage 本身就是设备（qpaintdevice.h 是它的基类），
    // 于是这些数字与「这张 PNG」严格绑定，可复现度最高。
    proofFontUnits(&card);
    proofMetricsFields(&card);
    proofAdvanceVsBounds(&card);
    proofElide(&card);
    proofBaselineAndAlign(card);

    const bool saved = card.save(QStringLiteral("painting_04_text_and_font_metrics.png"));
    qInfo() << "\n=== 存证 ===";
    qInfo() << "  PNG 尺寸 =" << card.width() << "x" << card.height()
            << " save() =" << saved << " 非透明像素 =" << countInk(card)
            << "（背景是不透明的白，所以应当 = 宽 x 高 =" << card.width() * card.height() << "）";
    qInfo() << "  文字抽查 pixelColor(20,30) =" << card.pixelColor(20, 30).name();   // qimage.h 184 行

    Stage stage;
    stage.setWindowTitle(QStringLiteral("第 4 讲 · 文本度量与基线"));
    stage.resize(420, 120);
    stage.show();

    QTimer::singleShot(450, [] {
        qInfo() << "\n=== 结论 ===";
        qInfo() << "① pixelSize 与 pointSize 是两种单位，互相读对方时预期得到 -1；点→像素 = dpi/72，"
                << "要确定数字就传 QPaintDevice（qfontmetrics.h 24/96 行）。";
        qInfo() << "② 度量三兄弟：horizontalAdvance 管「走多远」、boundingRect 管「墨在哪儿」（以基线为原点）、"
                << "tightBoundingRect 管「紧贴墨」；用错任何一个，居中就会差几像素。";
        qInfo() << "③ Qt6 改名清单：width()→horizontalAdvance()；fontDpi() 由 int 变 qreal；"
                << "QFont::setLegacyWeight()/legacyWeight() 已弃用（改 setWeight(QFont::Bold)）。";
        qInfo() << "④ 文本垂直居中有两条正路：让 Qt 摆（drawText(QRectF/QRect, flags, text, &br)）"
                << "或自己按基线摆（y = 顶边 + ascent）。**AlignBaseline 不是给 drawText 用的**（qnamespace.h 171~174 行原注释）。";
        qInfo() << "⑤ 中文换行请上 QTextOption + WrapAtWordBoundaryOrAnywhere；"
                << "flags 版只保证简单换行。要截断用 elidedText（带 d），要真裁剪用裁剪区（第 7 讲）。";
        qInfo() << "  图解：../visuals/painting_pipeline.html 把「字族→度量→基线→栅格化→合成」画成分步图，跑完去看。";
        QCoreApplication::quit();
    });

    return app.exec();
}

// ============================================================================
// 附：文本绘制的小抄
// ============================================================================
/*
 *  想要的效果                正确写法                                                   为什么会错
 *  -------------------------------------------------------------------------
 *  文字贴着一行的顶排         y = 顶边 + ascent()                                       拿 top 当 y → 字跑到框外
 *  文字垂直居中               drawText(rect, Qt::AlignVCenter|..., text, &br)          拿 rect.center() 当基线 → 差 ~height/2
 *  视觉居中（大写/数字）      y = 中线 - capHeight()/2（qfontmetrics.h 36 行）         按 height() 居中会「看起来偏下」
 *  量一段文字多宽             horizontalAdvance(text)                                  用 boundingRect().width() → 少算首尾留白
 *  中文/无空格文本换行        drawText(QRectF, text, QTextOption(WrapAtWordBoundaryOrAnywhere))
 *                                                                                            flags 版 TextWordWrap 只在空格断
 *  按钮里放不下               elidedText(text, Qt::ElideRight, avail)                   自己 substr → 截在半个字上/宽度算错
 *  多行文本的行距             逐行 y += lineSpacing()                                   用 height() → 行间挤在一起
 *  对齐到「下划线/删除线」    underlinePos() / strikeOutPos() / lineWidth()             凭手感画线 → 与字体不匹配
 *
 * 三条纪律：
 *  ① **度量要绑设备**：QFontMetrics(font) 用的是「主屏 DPI」，QFontMetrics(font, pd) 才是这块画布的。
 *     同一个 pointSize 的字体在 100% 与 200% 缩放的屏上高度不同，本讲一律用
 *     setPixelSize + 显式传设备，就是为了数字可复现。
 *  ② 不要在示例或控件里硬编码 family("Microsoft YaHei")：换台机器就退化成默认字族，
 *     度量全变、数字全部对不上。要挑字体，先用 QFontDatabase 查这台机器上有哪些字族、
 *     再给一个能落空的顺序（本模块不展开这条，QFontDatabase 属于字体专题）。
 *  ③ 文字与几何共用一个坐标系：想调「差一点点对不齐」，先在 paintEvent 里把基线画出来
 *     （本讲那条红色横线），99% 的错位一眼就看穿了，比改数值试错快得多。
 *
 * ——— 老师傅的话 ———
 *   徒弟，文本这块，新手和老手的分界线只有一个词：**基线**。
 *   新手把 drawText 的 y 当「文字顶部」，于是永远觉得字往下掉；
 *   老手把 y 当基线，再拿 ascent/descent 去框住它，于是怎么缩放都不慌。
 *   证据⑤ 我特意让你量 A 与 B 两个摆法的着墨重心 —— 那个差值我一句结论都没给，
 *   就是要你**用尺子**认识它。以后你写任何按钮、标签、图表刻度，脑子里都会先浮出这条线。
 *
 *   第二件：这一讲我把 Qt5→Qt6 的改名摊开写，不是凑字数。
 *   width()→horizontalAdvance() 这条，升级工程时是全仓替换级别的改动，
 *   而且它有隐蔽的一面：QFontMetrics::maxWidth() 还在、名字里也有 width，
 *   编译器不会提醒你，只有你自己核头文件才知道。我核到的证据就是「本机头文件里没有 width()」。
 *   同理 fontDpi() 变成 qreal：老代码 `int dpi = fm.fontDpi();` 在 Qt6 会**默默截断**，
 *   分数 DPI（150% 缩放 = 144 那种还好，125% 是 120，但有些机器是 96.9622…）一截断，
 *   你的点→像素换算就偏 1 像素，文字开始抖。**这种坑只能靠读头文件避开。**
 *
 *   最后一件：凡是「我以为」的公式，我都写成代码里的比较再打印，
 *   比如 ascent+descent 是否等于 height()。你以后写图形代码，请把这类自检留在注释里，
 *   一年后你自己都会感谢它。
 *
 * ——— 小练习 ———
 *   1. 把证据① 里 byPoint 的 18 改成 12，跑一次，把 12pt 与 16px 两个 height() 记下来，
 *      反解出你机器的 fontDpi（height 比值 × 已知像素尺寸），再和 logicalDpiY 对照。
 *   2. 用 capHeight() 做「数字居中」：把证据⑤ 的摆法 A 改成
 *      y = boxY + boxH/2 + fm.capHeight()/2，量出重心并与 C（Qt 摆的）比较，
 *      看谁更接近视觉居中，把三个重心值记进注释。
 *   3. 把证据④ 的 avail 从 180 改成 40（比一个省略号还窄），
 *      观察四种 TextElideMode 各返回什么（会不会仍然超长），
 *      并给「放不下的时候干脆不画」写一个用 horizontalAdvance 判断的小函数。
 */
// ============================================================================

// 本文件没有 Q_OBJECT 类，不需要 #include "04_text_and_font_metrics.moc"。
