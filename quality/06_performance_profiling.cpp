// ============================================================================
// 第 6 讲 · 性能：先量，再猜；量错了不如不量
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   「这儿有点卡」是项目里最没信息量的六类句子之一。它的完整版应该是
//   「这个函数每次调用 3.2 ms，一帧要调 8 次，所以 60fps 保不住」。
//   Qt 的性能问题有它固定的几个高发区，而且**几乎都能用你自己的表测出来**：
//     字符串拼接与隐式共享     —— 抄得最多、最容易想当然
//     容器的增长与预留         —— QList 的 capacity 是可见的，能数出来
//     线性 vs 哈希查找         —— QMap 与 QHash 的差别不是常数级
//     重绘 vs 缓存             —— 把「每次都重算」换成「算一次、之后 blit」
//     日志与调试输出           —— 关掉的类别几乎零成本，开着的是真钱
//
//   更要紧的是**量法**。一次取值是噪声，平均值会被离群点带偏，
//   所以本讲先立一套 min-of-N / median 的测量脚手架，再用它去测东西 ——
//   顺序不能反：先学量，再学优化。
//
//   七个阶段：
//     ① 量法：预热 + 多轮取中位数；并用一次「已知答案」的测量校准计时器
//     ② 字符串拼接：三种写法产出同一份文本，谁快谁慢由数字说
//     ③ 隐式共享：拷贝是 O(1)，一改就 detach —— isSharedWith() 当场证明
//     ④ 容器预留：reserve 把「多次搬房子」变成「一次搬房子」，增长率可数
//     ⑤ 查找结构：QMap(树) 与 QHash(哈希) 的实测差距，以及结果必须一致
//     ⑥ 绘制缓存：同一幅画，200 次重画 vs 200 次位块传送，且像素必须完全一致
//     ⑦ 日志开关：关掉的 qCDebug 是真的不进管道（用消息处理器数出来）
//
// 对应构建目标名：quality_06_performance_profiling
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(quality_06_performance_profiling 06_performance_profiling.cpp)
//   target_link_libraries(quality_06_performance_profiling PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)       # 本文件没有 Q_OBJECT 类，AUTOMOC 空转也无害
//
// 运行前须知：
//   · **全程同步**，没有 app.exec()。整讲大约跑 1~3 秒（取决于机器），
//     所有循环次数都是固定的小量级，不会出现「等半天」。
//   · 可以无头跑：`quality_06_performance_profiling -platform offscreen`。
//     第 ⑥ 节画在 QImage 上（内存位图），不需要窗口。
//   · **不写磁盘、不联网**。
//   · 打印出来的微秒数**只在本机成立**。本讲的所有硬账（== 的那些）都不依赖
//     具体数字，只依赖「结果是否正确」；性能数字一律是 Obs，不做断言 ——
//     因为性能断言最容易被机器型号打脸。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 quality_06_performance_profiling）。本机这一轮：硬账 30 项：对上 30，不符 0。
//     这只证明「编得过、跑得起来、上面这行为绿」——换 Qt 版本 / 换平台 / 换 CPU 核数
//     之后，正文里带具体数字的「预期」请以你自己那次运行输出为准
//     （重跑：bash _tools/run_all.sh）。
// ============================================================================

#include <QGuiApplication>           // ★必须是 GUI 级 app：阶段⑥ 要 QPainter/QFont（见 main 里的说明）
#include <QString>
#include <QStringList>
#include <QVector>
#include <QList>
#include <QMap>
#include <QHash>
#include <QElapsedTimer>             // QtCore/qelapsedtimer.h：36 start / 42 nsecsElapsed
#include <QLoggingCategory>          // QtCore/qloggingcategory.h：21 setEnabled / 23 isDebugEnabled
#include <QDebug>
#include <QtGlobal>                  // qInstallMessageHandler（qlogging.h 198 行）

#include <QImage>                    // QtGui/qimage.h：88 行构造函数 / 114 行 operator==
#include <QPainter>
#include <QFont>
#include <QColor>
#include <QPen>
#include <QBrush>

#include <algorithm>                 // std::sort：取中位数
#include <chrono>
#include <thread>

// ============================================================================
// 一、核对签名（本机 D:\Qt\6.12.0\mingw_64\include）
// ============================================================================
// 计时：qelapsedtimer.h
//   第 36 行 void start() noexcept;
//   第 42 行 qint64 nsecsElapsed() const noexcept;   ← 本讲全用它，纳秒分辨率
//   第 43 行 qint64 elapsed() const noexcept;        ← 毫秒，粗
//   ⚠ QElapsedTimer 用的是单调时钟（monotonic），不受系统时间被改影响。
//     别用 QDateTime/QTime 测耗时 —— 用户改一下时区你就得到负数。
//
// 隐式共享：qstring.h
//   第 306 行 inline void reserve(qsizetype size);
//   第 314 行 inline void detach();
//   第 315 行 inline bool isDetached() const;                 ← 数据是不是独占
//   第 316 行 inline bool isSharedWith(const QString &other)  ← ★ 两段 QString 共享同一份数据
//   ★ 用 isSharedWith 而不是靠时间差去推断「拷没拷」。这是**直接证据**：
//     两个 QString 只要还共享着同一块缓冲，它就说 true；谁一改，它立刻变 false。
//
// 容器：qlist.h
//   第 488 行 inline qsizetype capacity() const { return qsizetype(d.constAllocatedCapacity()); }
//   第 489 行 void reserve(qsizetype size);
//   ★ capacity() 是公开 API，于是「搬了几次家」可以**数**出来（本讲第 ④ 节）。
//
// 日志开关：qloggingcategory.h
//   第 21 行 void setEnabled(QtMsgType type, bool enable);    ← 按级别单独开关
//   第 23 行 bool isDebugEnabled() const                       ← 这就是 qCDebug 的前置判断
//   qlogging.h 第 198 行 QtMessageHandler qInstallMessageHandler(QtMessageHandler);
//   ★ qCDebug(cat) 展开成 `for (bool on = cat().isDebugEnabled(); on; )`：
//     类别关闭时，**连 QDebug 对象都不会构造**，消息也不会进管道。
//     本讲第 ⑦ 节用消息处理器把这个「进没进管道」数出来。
//
// 绘制：qimage.h
//   第 88 行 QImage(int width, int height, Format format);
//   第 114 行 bool operator==(const QImage &) const;        ← 逐像素比，缓存的正确性靠它
//
// 跨讲链接：
//   · painting 第 8 讲：QGraphicsView 的缓存策略（ItemCoordinateCache 等）是同一件事的封装。
//   · model_view 第 8 讲：视图层的性能大部分花在 data() 的调用次数上。
//   · quality 第 7 讲：百万行表格 —— 本讲的第 ④⑤ 节是那一讲的地基。
// ============================================================================

// ============================================================================
// 二、核对台账
// ============================================================================
enum Kind { Hard, Soft, Obs };

struct Ledger {
    int total = 0;
    int failed = 0;
    int stalled = 0;

    int exitCode() const { return (failed + stalled > 0 || total == 0) ? 1 : 0; }
};
static Ledger gLedger;

static void verdict(Kind kind, const QString &label, bool ok, const QString &detail)
{
    QString head;
    switch (kind) {
    case Hard:
        ++gLedger.total;
        if (!ok)
            ++gLedger.failed;
        head = ok ? QStringLiteral("  [OK ] 核对  ") : QStringLiteral("  [!! ] 不符  ");
        break;
    case Soft: head = ok ? QStringLiteral("  [预期中] ") : QStringLiteral("  [这次没命中] "); break;
    case Obs:  head = QStringLiteral("  [实测]  "); break;
    }
    qInfo().noquote() << head + QStringLiteral("%1 = %2").arg(label, detail);
}

static void section(const QString &text)
{
    qInfo().noquote() << QStringLiteral("\n========== %1 ==========").arg(text);
}

static void checkNum(const QString &label, qint64 got, qint64 want)
{
    verdict(Hard, label, got == want, QStringLiteral("%1（期望 %2）").arg(got).arg(want));
}

static void checkBool(const QString &label, bool got, bool want)
{
    verdict(Hard, label, got == want,
            QStringLiteral("实测 %1（期望 %2）")
                .arg(got ? QStringLiteral("true") : QStringLiteral("false"),
                     want ? QStringLiteral("true") : QStringLiteral("false")));
}

// ============================================================================
// 三、量法：预热 + 多轮取中位数
// ----------------------------------------------------------------------------
// 为什么不用「跑一次，记下时间」？三个原因，每个都会骗你：
//   ① 第一次调用要预热（指令缓存、堆的第一次分配、字体的第一次加载）；
//   ② 单次测量被调度器随机打断，得到的是噪声的上界；
//   ③ 平均值会被离群点带偏 —— 一次 5 ms 的 GC 能把平均值抬高十倍，
//      而中位数顶多挪一格。
// 所以：**预热一次 → 跑 rounds 轮 → 取中位数**。
// 这个脚手架本身也要校准（第 ① 节末尾的 sleep 20ms），否则你连「计时器坏了」
// 都不知道 —— 一个报出 0.00 ms 的计时器，看起来跟「极快」一模一样。
// ============================================================================
template <typename F>
static QVector<double> sampleMs(int iterations, F fn, int rounds = 5)
{
    fn(0);                                            // 预热：让缓存、分配器先热起来
    QVector<double> samples;
    samples.reserve(rounds);
    for (int r = 0; r < rounds; ++r) {
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < iterations; ++i)
            fn(i);
        samples.append(double(t.nsecsElapsed()) / 1e6);   // 纳秒 → 毫秒
    }
    std::sort(samples.begin(), samples.end());
    return samples;
}

static QString usPerOp(const QVector<double> &samples, int iterations)
{
    if (samples.isEmpty() || iterations <= 0)
        return QStringLiteral("n/a");
    const double median = samples.at(samples.size() / 2);
    return QString::number(median * 1000.0 / double(iterations), 'f', 3) + QStringLiteral(" µs/次");
}

static void phase1_methodology()
{
    section(QStringLiteral("① 量法：预热、多轮、取中位数"));

    // 校准：让程序睡 20 ms，看它测出来是多少。
    // 这是全讲唯一一次「知道答案」的测量，用它证明计时器与脚手架都是好的。
    const QVector<double> sleepy = sampleMs(1, [](int) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }, 3);

    const double median = sleepy.at(sleepy.size() / 2);
    qInfo().noquote() << QStringLiteral("  睡 20 ms，中位数测出 %1 ms")
                             .arg(QString::number(median, 'f', 3));
    checkBool(QStringLiteral("【1A】测出来的睡眠不低于 20 ms"), median >= 20.0, true);
    checkBool(QStringLiteral("【1B】也没有离谱到 200 ms 之外"), median < 200.0, true);

    verdict(Obs, QStringLiteral("【1C】三次采样"), true,
            QStringLiteral("%1 / %2 / %3 ms")
                .arg(QString::number(sleepy.at(0), 'f', 2),
                     QString::number(sleepy.at(1), 'f', 2),
                     QString::number(sleepy.at(2), 'f', 2)));
    verdict(Obs, QStringLiteral("【1D】为什么要取中位数"), true,
            QStringLiteral("平均值会被一次调度打断带偏，中位数不会"));
}

// ============================================================================
// 四、阶段②：字符串拼接，三种写法一个答案
// ----------------------------------------------------------------------------
// 这是个经典的「直觉失灵区」：
//   · 有人坚信 `s = s + x` 会产生一堆临时对象，于是改用 QStringList + join；
//   · 有人坚信 join 一定更快，于是在只有三段的场景里也建了个 list；
//   · 真相是：QString 的增长是**几何式**的（不是每次 +1），所以 += 在绝大多数
//     场景下都够快；join 的优势在于「事先知道总量、一次分配」。
// 本讲不让你背结论，而是跑给你看 —— 并且强制三条路线产出**完全相同**的字符串，
// 否则「快」就没有意义（第 ② 节的所有硬账都在这一点上）。
// ============================================================================
static void phase2_stringBuild()
{
    section(QStringLiteral("② 字符串拼接：三种写法的答案必须一样"));

    const int n = 20000;

    // 写法 A：反复 +=。每一轮开头清空，所以最后一轮结束时 s 就是「完整的那一份」，
    // 既用来计时，也用来做正确性比对 —— 一举两得，不必再算一遍。
    QString a;
    const QVector<double> sa = sampleMs(n, [&a](int i) {
        if (i == 0)
            a.clear();
        a += QString::number(i);
    });
    verdict(Obs, QStringLiteral("【2A】A: s += QString::number"), true, usPerOp(sa, n));

    // 写法 B：QStringList + join。它天然是「一次建完再拼」，所以**迭代次数要换**：
    // 一次调用就是一整份 n 条数据，硬塞进 n 次循环会变成 n×n 的工作量。
    // 这也是讲义里最容易写错的地方 —— 量法不对，比出来的数就不能用。
    QString b;
    const int bRounds = 20;
    const QVector<double> sb = sampleMs(bRounds, [&b, n](int) {
        QStringList parts;
        parts.reserve(n);
        for (int i = 0; i < n; ++i)
            parts << QString::number(i);
        b = parts.join(QString());
    });
    const double bMedianMs = sb.at(sb.size() / 2);
    verdict(Obs, QStringLiteral("【2B】B: QStringList + join（一次建 %1 条）").arg(n), true,
            QString::number(bMedianMs, 'f', 3) + QStringLiteral(" ms/次，摊到每个元素 ")
                + QString::number(bMedianMs * 1000.0 / double(n), 'f', 3)
                + QStringLiteral(" µs"));

    // 写法 C：先 reserve 再 append（QString::reserve 在 qstring.h 第 306 行）。
    // ★ clear() 与 reserve() 只能做一次，必须放在 i == 0 那一支里 ——
    //   如果每次 append 前都 clear+reserve，这段代码就变成了 O(n²)，
    //   你量到的将是「我写错的那份代码」的速度，而不是 reserve 的效果。
    QString c;
    const QVector<double> sc = sampleMs(n, [&c, n](int i) {
        if (i == 0) {
            c.clear();
            c.reserve(qsizetype(n) * 6);   // 「大致够」即可，估小了也只是多搬一次
        }
        c += QString::number(i);
    });
    verdict(Obs, QStringLiteral("【2C】C: reserve 后 += "), true, usPerOp(sc, n));

    // ★ 三条硬账：内容必须一致。这是「性能测量」的前提 ——
    //   如果三种写法算出来的东西不一样，比的就不是快慢，是错得多少。
    checkBool(QStringLiteral("【2D】A 的开头对得上"),
              a.startsWith(QStringLiteral("0123456789101112")), true);
    checkNum(QStringLiteral("【2E】A 与 B 长度相同"), a.size(), b.size());
    checkNum(QStringLiteral("【2F】B 与 C 长度相同"), b.size(), c.size());
    checkBool(QStringLiteral("【2G】A 与 B 全文相同"), a == b, true);
    checkBool(QStringLiteral("【2H】B 与 C 全文相同"), b == c, true);
}

// ============================================================================
// 五、阶段③：隐式共享 —— 拷贝是 O(1)，一改就分家
// ----------------------------------------------------------------------------
// QString 的拷贝不复制字符，只把引用计数 +1。所以「传一个 QString 参数」
// 的成本跟字符串多长**无关** —— 这是 Qt 值类型设计的核心。
// 但一旦有人写 `s[0] = 'x'`，就会 detach（复制一份再改），那一刻是 O(n)。
// 本讲不靠时间差去推断，直接用 isSharedWith()（qstring.h 第 316 行）看成败：
//   拷贝之后 → true（两块 QString 指着同一份数据）
//   一改之后 → false（已经分家，改的那一边是新分配的）
// 为什么这件事属于「性能」而不是「语法」？因为它决定了你写
//   void f(QString s)          // 便宜，别怕
// 和
//   void f(const QString &s)   // 更便宜一点点，但没有数量级差别
// 之间的取舍 —— 很多人为了「避免拷贝」把接口写成一堆 const&，
// 却不知道真正贵的是 detach，不是拷贝。
// ============================================================================
static void phase3_implicitSharing()
{
    section(QStringLiteral("③ 隐式共享：拷贝便宜，改写才贵"));

    QString big(200000, QChar(u'x'));
    checkBool(QStringLiteral("【3A】刚造出来时是独占的"), big.isDetached(), true);

    QString copy = big;                              // 只是引用计数 +1
    checkBool(QStringLiteral("【3B】拷贝之后两块 QString 共享同一份数据"),
              big.isSharedWith(copy), true);
    checkBool(QStringLiteral("【3C】所以拷贝这一侧也不是独占"),
              copy.isDetached(), false);

    copy[0] = QChar(u'y');                           // ★ 写操作触发 detach
    checkBool(QStringLiteral("【3D】一改就分家"), big.isSharedWith(copy), false);
    checkBool(QStringLiteral("【3E】改完之后两侧各自独占"),
              big.isDetached() && copy.isDetached(), true);
    checkBool(QStringLiteral("【3F】原串一个字都没被动过"), big.at(0) == QChar(u'x'), true);
    checkBool(QStringLiteral("【3G】新串首页已经改了"), copy.at(0) == QChar(u'y'), true);

    // 量化一下「拷贝 vs detach」的差距（Obs，只报数）。
    const int n = 2000;
    {
        const QVector<double> s = sampleMs(n, [&big](int) {
            QString c = big;                          // 只加引用计数
            Q_UNUSED(c);
        });
        verdict(Obs, QStringLiteral("【3H】20 万字符的拷贝"), true, usPerOp(s, n));
    }
    {
        const QVector<double> s = sampleMs(n, [&big](int) {
            QString c = big;
            c[0] = QChar(u'z');                       // 触发一次 20 万字符的复制
            Q_UNUSED(c);
        });
        verdict(Obs, QStringLiteral("【3I】拷贝 + 改一个字符（触发 detach）"), true,
                usPerOp(s, n));
    }
    verdict(Obs, QStringLiteral("【3J】结论"), true,
            QStringLiteral("拷贝与长度无关；detach 与长度成正比。"
                           "所以热路径上别改「刚拷进来」的大字符串"));
}

// ============================================================================
// 六、阶段④：容器预留 —— 把「多次搬家」变成「一次搬家」
// ----------------------------------------------------------------------------
// QList/QVector 追加元素时，容量不够就重新分配一大块再把旧数据搬过去。
// 关键是**几何增长**：容量翻倍，所以追加 n 个元素的搬家次数是 O(log n)，不是 O(n)。
// 这意味着「不 reserve」也不会死得很难看，但 reserve 能：
//   · 省掉那 log n 次分配与搬运；
//   · 更重要的是：**容量变化次数是可见的**（capacity() 是公开 API）。
// 本讲把「容量变了几次」直接数出来 —— 这是本节最硬的证据：
//   不 reserve：次数 > 0（而且和元素个数呈对数关系）
//   先 reserve：次数 == 0（够用了，一次都不搬）
// ============================================================================
template <typename Container>
static int appendAndCountGrowth(int n)
{
    Container c;
    qsizetype lastCap = c.capacity();
    int changes = 0;
    for (int i = 0; i < n; ++i) {
        c.append(i);
        if (c.capacity() != lastCap) {
            ++changes;
            lastCap = c.capacity();
        }
    }
    return changes;
}

static void phase4_containerGrowth()
{
    section(QStringLiteral("④ 容器预留：容量变化次数可以数出来"));

    const int n = 1000000;
    const int growCount = appendAndCountGrowth<QList<int>>(n);
    qInfo().noquote() << QStringLiteral("  不 reserve 追加 %1 个 int：容量变了 %2 次")
                             .arg(n).arg(growCount);
    checkBool(QStringLiteral("【4A】不 reserve 一定要扩容（次数 > 0）"), growCount > 0, true);
    checkBool(QStringLiteral("【4B】但次数是几何增长的量级（远小于 n）"),
              growCount < 100, true);

    // 先 reserve 再追加：容量一次到位，追加过程中一次都不该变。
    QList<int> reserved;
    reserved.reserve(n);
    const qsizetype capAfterReserve = reserved.capacity();
    int changesAfterReserve = 0;
    for (int i = 0; i < n; ++i) {
        reserved.append(i);
        if (reserved.capacity() != capAfterReserve)
            ++changesAfterReserve;
    }
    checkNum(QStringLiteral("【4C】reserve 之后追加过程中的容量变化次数"),
             changesAfterReserve, 0);
    checkBool(QStringLiteral("【4D】容量足够装下所有元素"), reserved.capacity() >= reserved.size(), true);
    checkNum(QStringLiteral("【4E】元素一个不少"), reserved.size(), n);
    checkNum(QStringLiteral("【4F】最后一个元素对得上"), reserved.last(), n - 1);

    // 顺便说清 reserve 的两个边界：
    //   · reserve 给多了只是浪费内存，不会破坏正确性；
    //   · QList<int> 在 Qt 6 里就是 QVector<int>（QList 已是连续数组），
    //     所以 reserve 对它是真有用的 —— Qt 5 时代的 QList 是节点数组，别照搬老经验。
    verdict(Obs, QStringLiteral("【4G】扩容代价"), true,
            QStringLiteral("每次扩容 = 一次分配 + 一次全量搬运，"
                           "这就是 reserve 省掉的东西"));
}

// ============================================================================
// 七、阶段⑤：QMap（树） vs QHash（哈希）
// ----------------------------------------------------------------------------
// 两者的差别不是「快一点」，而是复杂度不同：
//   QMap  红黑树，查找 O(log n)，**有序**，遍历时按 key 排序；
//   QHash 哈希表，查找平均 O(1)，**无序**，遍历顺序不承诺。
// 所以选型的判据从来不是「哪个快」，而是「你要不要有序 / 要不要范围查询」。
// 本节两条硬账只保证**结果一致**（同样的 key 查得到同样的 value），
// 性能差一律 Obs —— 哈希表在 key 很少的时候甚至可能输给树。
// ============================================================================
static void phase5_lookup()
{
    section(QStringLiteral("⑤ 查找结构：QMap 有序，QHash 平均常数"));

    const int n = 20000;
    QMap<QString, int> map;
    QHash<QString, int> hash;
    for (int i = 0; i < n; ++i) {
        const QString key = QStringLiteral("key_%1").arg(i, 6, 10, QLatin1Char('0'));
        map.insert(key, i);
        hash.insert(key, i);
    }
    checkNum(QStringLiteral("【5A】QMap 条数"), map.size(), n);
    checkNum(QStringLiteral("【5B】QHash 条数"), hash.size(), n);

    // 用同一个 key 序列查一遍，两边的和必须一样。
    const int lookups = 200000;
    qint64 mapSum = 0;
    qint64 hashSum = 0;
    {
        const QVector<double> s = sampleMs(lookups, [&map, &mapSum, n](int i) {
            const QString key = QStringLiteral("key_%1").arg(i % n, 6, 10, QLatin1Char('0'));
            mapSum += map.value(key, -1);
        });
        verdict(Obs, QStringLiteral("【5C】QMap::value"), true, usPerOp(s, lookups));
    }
    {
        const QVector<double> s = sampleMs(lookups, [&hash, &hashSum, n](int i) {
            const QString key = QStringLiteral("key_%1").arg(i % n, 6, 10, QLatin1Char('0'));
            hashSum += hash.value(key, -1);
        });
        verdict(Obs, QStringLiteral("【5D】QHash::value"), true, usPerOp(s, lookups));
    }
    checkNum(QStringLiteral("【5E】两边查到的东西一模一样"), hashSum, mapSum);
    checkBool(QStringLiteral("【5F】也没有查空的（-1 一个都没混进来）"),
              mapSum >= 0 && hashSum >= 0, true);

    // 有序性：QMap 的 key 是升序的，QHash 不承诺 —— 这就是选型的真正判据。
    const QStringList mapKeys = map.keys();
    bool ascending = true;
    for (int i = 1; i < mapKeys.size(); ++i) {
        if (mapKeys.at(i - 1) > mapKeys.at(i))
            ascending = false;
    }
    checkBool(QStringLiteral("【5G】QMap 的 keys() 是升序"), ascending, true);
    verdict(Obs, QStringLiteral("【5H】QHash 的 keys() 顺序"), true,
            QStringLiteral("不承诺 —— 同一份数据两次运行都可能不同"));
}

// ============================================================================
// 八、阶段⑥：绘制缓存 —— 算一次，之后只做位块传送
// ----------------------------------------------------------------------------
// 「一帧要重画 40 个装饰图形」这种背景，最省的写法是**画一次存起来**，
// 之后每次只 drawImage / drawPixmap。代价是内存（一张 240×180 的 ARGB32 约 170 KB）。
// 但缓存有个前提，必须先证：**缓存画出来跟现画出来必须逐像素一致**。
// 不然你缓存的是 bug —— 而且它会一直好用，直到某天有人改了绘制参数。
// 所以本节的结构是：先证相等（硬账），再比快慢（Obs）。
// ============================================================================
static void paintOrnament(QPainter &p, int seed)
{
    // 一串确定性图形（固定种子，不用随机数：随机会让「逐像素相等」没法比较）
    for (int i = 0; i < 40; ++i) {
        const int x = (seed * 7 + i * 11) % 220;
        const int y = (seed * 13 + i * 17) % 160;
        const QColor color = QColor::fromHsv((i * 9) % 360, 200, 220);
        p.setPen(QPen(color.darker(140), 1.5));
        p.setBrush(QBrush(color));
        p.save();
        p.translate(x, y);
        p.rotate(double((i * 23) % 360));
        p.drawRoundedRect(QRectF(-9, -6, 18, 12), 3, 3);
        p.restore();
    }
    p.setPen(QPen(Qt::black));
    p.setFont(QFont(QStringLiteral("Helvetica"), 10, QFont::Bold));
    p.drawText(QRect(8, 150, 220, 24), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("cached frame"));
}

static QImage renderDirect(int w, int h, int seed)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(250, 248, 242));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    paintOrnament(p, seed);
    return img;
}

static void phase6_paintCache()
{
    section(QStringLiteral("⑥ 绘制缓存：先证一样，再比快慢"));

    const int w = 240;
    const int h = 180;
    const int seed = 7;

    const QImage direct = renderDirect(w, h, seed);
    const QImage cached = renderDirect(w, h, seed);       // 缓存本体

    // ① 正确性：现画一张，与「先画一次再 blit 一次」的结果逐像素比（qimage.h 114 行）。
    QImage blitted(w, h, QImage::Format_ARGB32_Premultiplied);
    blitted.fill(QColor(250, 248, 242));
    {
        QPainter p(&blitted);
        p.drawImage(0, 0, cached);
    }
    checkBool(QStringLiteral("【6A】blit 的结果与现画逐像素相同"), blitted == direct, true);
    checkNum(QStringLiteral("【6B】图片宽度对得上"), direct.width(), w);

    // ② 成本：现画 200 次 vs blit 200 次。
    const int frames = 200;
    {
        const QVector<double> s = sampleMs(frames, [w, h, seed](int) {
            const QImage img = renderDirect(w, h, seed);
            Q_UNUSED(img);
        }, 3);
        verdict(Obs, QStringLiteral("【6C】每帧现画 40 个装饰图形"), true, usPerOp(s, frames));
    }
    {
        const QVector<double> s = sampleMs(frames, [&cached, w, h](int) {
            QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
            img.fill(QColor(250, 248, 242));
            QPainter p(&img);
            p.drawImage(0, 0, cached);
        }, 3);
        verdict(Obs, QStringLiteral("【6D】每帧只做位块传送"), true, usPerOp(s, frames));
    }

    verdict(Obs, QStringLiteral("【6E】这笔账怎么算"), true,
            QStringLiteral("缓存用内存换 CPU —— 但只有「图形不变、要重画很多次」才划算。"
                           "窗口一缩放就得重画缓存，那时缓存反而是负担"));
}

// ============================================================================
// 九、阶段⑦：日志开关 —— 关掉的 qCDebug 是真的一分钱不花
// ----------------------------------------------------------------------------
// 这一节用消息处理器把「消息有没有进管道」数出来，而不是靠猜：
//   类别关闭 → 消息数 0（qCDebug 在宏里就短路了，QDebug 对象都不构造）
//   类别打开 → 消息数 == 调用次数（每条都真的产生了）
// 这就是 Qt 日志设计的关键：**热路径里可以留 qCDebug**，只要那个类别是关的。
// 反过来，qInfo/qWarning 属于默认开启的类别，留在热路径里就是真开销 ——
// 这也是为什么日志规范里要求「循环里不要 qInfo」。
// ============================================================================
static int g_msgCount = 0;

static void countingHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    Q_UNUSED(type);
    Q_UNUSED(context);
    Q_UNUSED(msg);                     // 故意不打印：我们只数，不刷屏
    ++g_msgCount;
}

static void phase7_logging()
{
    section(QStringLiteral("⑦ 日志开关：关掉的类别一分钱不花"));

    QLoggingCategory benchCategory("qtlp.bench");
    const int n = 10000;
    const int rounds = 3;                 // ★这个 3 会改变下面那条硬账的期望值，见 7C 注释

    // —— 关闭状态 ——
    benchCategory.setEnabled(QtDebugMsg, false);
    checkBool(QStringLiteral("【7A】类别确实被关掉了"), benchCategory.isDebugEnabled(), false);

    g_msgCount = 0;
    QtMessageHandler previous = qInstallMessageHandler(countingHandler);
    double offMs = 0.0;
    {
        const QVector<double> s = sampleMs(n, [&benchCategory](int i) {
            qCDebug(benchCategory) << "tick" << i;
        }, rounds);
        offMs = s.at(s.size() / 2);
    }
    const int offCount = g_msgCount;

    // —— 打开状态（处理器仍然只数不打印，否则 10000 行会把终端淹了）——
    benchCategory.setEnabled(QtDebugMsg, true);
    g_msgCount = 0;
    double onMs = 0.0;
    {
        const QVector<double> s = sampleMs(n, [&benchCategory](int i) {
            qCDebug(benchCategory) << "tick" << i;
        }, rounds);
        onMs = s.at(s.size() / 2);
    }
    const int onCount = g_msgCount;
    qInstallMessageHandler(previous);              // ★ 立刻装回去，别把后面的输出也吞了

    checkNum(QStringLiteral("【7B】关闭时进管道的消息数"), offCount, 0);
    // ★期望不是 n！sampleMs 会**先预热一次**（fn(0)）再跑 rounds 轮，每轮 n 次，
    //   所以处理器数到的是 1 + rounds*n。本机实测 30001，写 n 的话这条永远红——
    //   「数错了自己脚手架的圈数」是取证代码里最常见的一类 bug。
    checkNum(QStringLiteral("【7C】打开时进管道的消息数（预热 1 次 + %1 轮 × %2）")
                 .arg(rounds).arg(n), onCount, 1 + rounds * n);
    verdict(Obs, QStringLiteral("【7D】%1 次 qCDebug 的耗时").arg(n), true,
            QStringLiteral("关: %1 ms ｜ 开: %2 ms（差 %3 倍）")
                .arg(QString::number(offMs, 'f', 2),
                     QString::number(onMs, 'f', 2),
                     QString::number(offMs > 0.0 ? onMs / offMs : 0.0, 'f', 1)));

    verdict(Obs, QStringLiteral("【7E】规矩"), true,
            QStringLiteral("热路径留 qCDebug 是安全的（类别关着就短路）；"
                           "留 qInfo/qWarning 不是 —— 它们默认开着"));
}

// ============================================================================
// 十、main：合账
// ============================================================================
static void reconcile()
{
    section(QStringLiteral("合账"));
    const int broken = gLedger.failed + gLedger.stalled;
    qInfo().noquote() << QStringLiteral("  硬账 %1 项：对上 %2，不符 %3；整节作废 %4")
                             .arg(gLedger.total)
                             .arg(gLedger.total - broken)
                             .arg(gLedger.failed)
                             .arg(gLedger.stalled);
    qInfo().noquote() << QStringLiteral("  退出码 = %1（%2）")
                             .arg(gLedger.exitCode())
                             .arg(gLedger.exitCode() == 0
                                      ? QStringLiteral("没有硬账不符、没有整节作废")
                                      : QStringLiteral("有硬账不符或整节作废"));
    if (gLedger.exitCode() != 0)
        qWarning().noquote() << QStringLiteral("  ★ 请按上面的 [!! ] 行往下查。");
}

int main(int argc, char *argv[])
{
    // ★为什么不是 QCoreApplication：阶段⑥ 要 QPainter 画 QFont 文字。本机实测这样会崩：
    //   "QGuiApplication::font(): no QGuiApplication instance and no application font set."
    //   "QFontDatabase: Must construct a QGuiApplication before accessing QFontDatabase"
    //   然后进程 fail-fast（ctest 里是 Exit code 0xc0000602），后面的核对一项都跑不到。
    //   QGuiApplication 不带窗口系统装饰、不需要 QApplication 的 Widgets，无头 offscreen 一样跑。
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("quality_06_performance_profiling"));

    qInfo() << "================================================================";
    qInfo() << "quality 第 6 讲 · 性能：先量再猜（Qt =" << QT_VERSION_STR << "）";
    qInfo() << "  提醒：下面所有 µs 数字只在本机成立，硬账不与它们挂钩。";
    qInfo() << "================================================================";

    phase1_methodology();
    phase2_stringBuild();
    phase3_implicitSharing();
    phase4_containerGrowth();
    phase5_lookup();
    phase6_paintCache();
    phase7_logging();

    reconcile();

    const int rc = gLedger.exitCode();
    qInfo().noquote() << QStringLiteral("进程退出码 = %1").arg(rc);
    return rc;
}

// ============================================================================
// 十一、速查：手感表
// ============================================================================
/*
 * 一、测量本身（最容易被跳过的一步）
 *   单调时钟 + 预热一次 + 多轮取中位数；一次取值只配叫「随手一看」。
 *   测量前先问：这段代码被优化掉了吗？（结果要真的被用到）
 *   再问：我测的是发热状态还是冷启动？（两种都合法，但别混着比）
 *
 * 二、五个高发区的直觉
 *   QString       拷贝 O(1)（隐式共享）；**改写**才 O(n)。传参别怕拷贝。
 *   QList/QVector 追加是几何增长的（摊还 O(1)）；已知总量就 reserve。
 *   QMap vs QHash 判据是「要不要有序」，不是「谁快」。
 *   绘制           图形不变、要画 N 次 → 缓存成 QImage/QPixmap；
 *                  图形随窗口变化 → 别缓存，缓存一失效就是白算。
 *   日志           关掉的类别在宏里短路；qInfo/qWarning 默认开着，别留在循环里。
 *
 * 三、四个「别做」
 *   别在 paintEvent 里 new QPixmap / 加载图片 / 查数据库 —— 那是每帧都付的钱。
 *   别用 qDebug() 拼接昂贵字符串放在热路径（哪怕它「应该」不输出）。
 *   别为了省一次拷贝把接口写成 const& 然后又把它 detach 掉 —— 白忙。
 *   别拿 QTime/QDateTime 测耗时（会被系统时间、时区影响），用 QElapsedTimer。
 *
 * 四、一条经验值的红线
 *   16 ms = 60fps 的一帧预算。你的绘制 + 布局 + 数据处理全部要装进这个数。
 *   超过 8 ms 就该开始怀疑；超过 16 ms 用户已经在拖窗口了。
 *
 * ——— 老师傅的话 ———
 *   徒弟，「这儿有点卡」不是情报，是抱怨。
 *   你要学会把它翻译成三个数字：**多少次、每次多久、占比多少**。
 *   有了这三个数，优化才有靶子；没有，你就是在改一行行「看起来慢」的代码，
 *   而且很可能把快的改慢 —— 我见过有人为了性能把 QString 换成 char*，
 *   结果慢了 30%：因为丢掉了隐式共享。
 *
 *   还有一句更重要的：**优化完必须再量一次**，而且要量同一件事。
 *   很多人改完就宣布「快了」，因为他只量了改之前和改之后的两次——
 *   两次都可能是噪声。量法不稳，结论就是玄学。
 *
 *   最后一句话送你：先让它对，再让它快；而「对」这件事，性能测试也要遵守 ——
 *   你缓存下来的东西，必须先证明和现算的一模一样。
 */

// ============================================================================
// 十二、练习
// ----------------------------------------------------------------------------
/*
 * 1. 把第 ① 节的 rounds 从 5 改成 1，再跑十次，看中位数波动多大；
 *    然后改成 21 跑一次，看它稳定到什么程度。回答：
 *    为什么「多次测量取中位数」比「一次测得更久」更有效？
 *
 * 2. 给第 ② 节加第四种写法：先用 std::string 拼，最后 QString::fromStdString。
 *    要求产出完全相同的内容。做完回答：什么时候它反而更快？
 *    （提示：跟 QChar 的 16 位存储与 UTF-8 转换的位置有关。）
 *
 * 3. 把第 ④ 节的容器换成 QHash<int,int>，重新数「容量变化次数」。
 *    你会发现 QHash 的 capacity() 语义不一样（它按质数/2 的幂增长）。
 *    回答：为什么「reserve 之后容量变化 0 次」这条在 QHash 上不一定成立？
 *
 * 4. 给第 ⑥ 节加一个「缓存失效」场景：把装饰图形加一个随帧变化的旋转角度，
 *    再跑同样的对比。回答：为什么这时缓存会**更慢**？
 *    然后把结论写成一条判断规则：「什么样的绘制才值得缓存」。
 *
 * 5. 给第 ⑦ 节加一个自定义类别 qtlp.bench.trace，用
 *    QLoggingCategory::setFilterRules("qtlp.bench.trace.debug=false") 在运行期关掉它，
 *    验证消息数从 n 变 0。回答：为什么这条规则必须写在 main 之前（或者至少
 *    在任何 qCDebug 之前），否则第一次那条还是会出去？
 *
 * 6. 资深题：用 QElapsedTimer 给你的项目做一次「启动耗时分解」——
 *    在 main 里打五个时间点（构造 QApplication / 读配置 / 建主窗口 / 首帧 / 首帧后 100ms），
 *    把五个差值打印出来。这五个数，比任何 profiler 截图都更能说服产品经理。
 */
// ============================================================================
