// ============================================================================
// 第 1 讲 · 用 QtTest 写单元测试：测试类就是个 QObject 子类
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   前两阶段的代码「跑起来看着对」，但没人能保证下次改完还对着。这一讲把
//   「看着对」换成「机器判定对」：用 Qt 自带的 QtTest 模块写单元测试。
//     1) 一个测试类 = QObject 子类 + private slots（槽就是测试函数），
//        四个钩子 initTestCase / init / 测试函数 / cleanup / cleanupTestCase
//        谁每轮跑、谁只跑一次，本讲把计数与顺序真打出来（见「钩子计数」）。
//     2) QVERIFY 与 QCOMPARE 的真实差别（对着 qtestcase.h 看宏展开）：
//        QCOMPARE 会把两个操作数**原文 + 实际值 + 类型**打出来，QVERIFY 只有表达式文本。
//     3) 数据驱动的两套写法：`void f_data()` + `QTest::newRow("tag") << ...`
//        与 `QTest::addRow("%d", x) << ...`（printf 风格，qtestcase.h:569）；
//        列由 `QTest::addColumn<T>("name")` 声明（同文件 562 行），
//        取值用 `QFETCH(Type, name)`（同文件 306 行）。
//     4) QEXPECT_FAIL 怎么标一个**已知缺陷**（qtestcase.h:299，
//        模式参数只有 QTest::Abort / QTest::Continue 两个值，qttestglobal.h:25）。
//     5) 最值钱的一半其实在代码外面：**被测代码得先写得能被测**——
//        依赖注入（折扣来源是接口，不是 new 出来的具体类）、
//        把纯逻辑抽成不碰 QWidget 的函数（本讲的 PricingEngine 一行 GUI 都没有）。
//
// 对应构建目标名：quality_01_qtest_unit
//   find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
//   add_executable(quality_01_qtest_unit 01_qtest_unit.cpp)
//   target_link_libraries(quality_01_qtest_unit PRIVATE Qt6::Widgets Qt6::Test)
//   set(CMAKE_AUTOMOC ON)     # 有 Q_OBJECT 类，末尾 #include "01_qtest_unit.moc"
//
// 运行前须知：
//   · 本讲是 **QTest 可执行文件**：QTEST_GUILESS_MAIN 自己造 QCoreApplication、
//     跑完所有测试函数就退出（qtest.h:299 → QTEST_MAIN_WRAPPER，main 直接 return
//     QTest::qExec(&tc, argc, argv)）。**全通过返回 0，任何一条断言失败返回非 0**，
//     这就是 CI 判绿依据；不需要显示器、不需要鼠标键盘、没有事件循环卡住的地方。
//   · 用 QTEST_GUILESS_MAIN 而不是 QTEST_MAIN：本讲的被测代码是纯逻辑，
//     拉 QApplication 反而要求 GUI 平台插件（详见第 2 讲怎么在无头环境跑 GUI）。
//   · 数据全在内存里现造，**不读文件、不联网、不碰数据库**。
//   · 命令行选项（-maxwarnings / -o file,txt / -xmlhandler / -functions / -datatags）
//     的解析代码在 qtestcase.cpp，本机只有头文件，**核不到选项表**；下面附录里
//     我只写「常见选项，具体以 QTest 文档为准」，并且把**能从头文件核到的**
//     运行期 API（QTest::qExec 重载、setThrowOnFail、ignoreMessage…）标了行号。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 quality_01_qtest_unit）。本机这一轮：Totals: 21 passed, 0 failed, 0 skipped。
//     这只证明「编得过、跑得起来、上面这行为绿」——换 Qt 版本 / 换平台 / 换 CPU 核数
//     之后，正文里带具体数字的「预期」请以你自己那次运行输出为准
//     （重跑：bash _tools/run_all.sh）。
// ============================================================================

#include <QObject>                  // QtCore/qobject.h
#include <QString>                  // QtCore/qstring.h
#include <QByteArray>               // QtCore/qbytearray.h
#include <QVector>                  // QtCore/qvector.h
#include <QHash>                    // QtCore/qhash.h
#include <QDebug>                   // qWarning / qInfo
#include <QtTest/QTest>             // QtTest/qtest.h（转发头；含 qtestcase.h / qtest_*.h）

#include <stdexcept>                // std::invalid_argument

// ============================================================================
// 一、被测代码：一个「订单金额 + 待办优先级」的小引擎
// ----------------------------------------------------------------------------
// 注意它**没有一行 GUI**：不 include QWidget、不 pointer 到任何控件、
// 不读配置文件。这就是「可测」的全部前提——能单独 new 出来、能塞假依赖、
// 结果是个纯数字，断言才有得写。
// ============================================================================

// 折扣来源被抽成**纯虚接口**（不是 QObject！见 storage/quality 第 3 讲的分层）。
// 生产环境查表，测试环境塞 stub——这就是依赖注入最小可用的形态。
class DiscountSource {
public:
    virtual ~DiscountSource() = default;
    virtual int percentFor(const QString &userId) const = 0;   // 0..100
};

// 生产实现：查一张内存表（真实项目里它可能是 SQLite，见 storage 模块）
class TableDiscounts final : public DiscountSource {
    QHash<QString, int> m_table;
public:
    explicit TableDiscounts(QHash<QString, int> table) : m_table(std::move(table)) {}
    int percentFor(const QString &userId) const override { return m_table.value(userId, 0); }
};

struct Line {
    QString sku;
    int unitCents;   // 单价用「分」存整数，别用 double 存钱（本讲刻意这么定）
    int qty;
};

class PricingEngine {
public:
    static constexpr qint64 kLineCapCents = 1000000000LL;   // 单笔行金额上限（业务规则）

    // 依赖注入：引擎**只借指针不持有**，所以析构里没有 delete（也不该有）。
    // 传 nullptr 表示「没有任何折扣来源」，此时折扣按 0 算，不崩。
    explicit PricingEngine(const DiscountSource *discounts) : m_discounts(discounts) {}

    // 纯函数①：一行的小计。负数抛异常，超过上限钳到上限。
    qint64 lineTotalCents(const Line &l) const
    {
        if (l.unitCents < 0 || l.qty < 0)
            throw std::invalid_argument("lineTotalCents: 单价与数量都不能为负");
        const qint64 raw = qint64(l.unitCents) * qint64(l.qty);
        return raw > kLineCapCents ? kLineCapCents : raw;    // 边界：上限钳制
    }

    // 纯函数②：整单金额（先求和，再按折扣百分比减，向下取整）。
    qint64 orderTotalCents(const QVector<Line> &lines, int percent) const
    {
        if (percent < 0 || percent > 100)
            throw std::invalid_argument("orderTotalCents: 折扣百分比必须在 0..100");
        qint64 sum = 0;
        for (const Line &l : lines)
            sum += lineTotalCents(l);
        return sum * qint64(100 - percent) / 100;
    }

    // 带依赖的版本：折扣从注入的接口里查，引擎自己不算规则。
    qint64 orderTotalForUser(const QVector<Line> &lines, const QString &userId) const
    {
        const int percent = m_discounts ? m_discounts->percentFor(userId) : 0;
        return orderTotalCents(lines, percent);
    }

    // 纯函数③：待办优先级 0..100。权重 1..5，逾期天数只认 0..10，done 直接 0。
    int priorityScore(const QString &state, int daysOverdue, int weight) const
    {
        if (weight < 1 || weight > 5)
            throw std::invalid_argument("priorityScore: 权重必须在 1..5");
        if (state == QLatin1String("done"))
            return 0;
        const int overdue = qBound(0, daysOverdue, 10);      // 边界：负数当 0、超过 10 当 10
        return qBound(0, weight * 12 + overdue * 4, 100);
    }

    // 配置解析：非法值要**又吵又兜底**（qWarning + 回落到 0）。
    // 这类「吵」在测试里得用 QTest::ignoreMessage 显式认领，否则测试输出脏。
    int configPercent(const QString &text) const
    {
        bool ok = false;
        const int p = text.toInt(&ok);
        if (!ok || p < 0 || p > 100) {
            qWarning("PricingEngine: bad discount config '%s', fallback to 0",
                     qPrintable(text));
            return 0;
        }
        return p;
    }

private:
    const DiscountSource *m_discounts = nullptr;
};

// ============================================================================
// 二、测试替身：假实现（test double）
// ----------------------------------------------------------------------------
// 它记录自己被调用了几次——「依赖真的被注入了吗」这条证据要从这儿来。
// ============================================================================
class StubDiscounts final : public DiscountSource {
    int m_percent;
    mutable int m_calls = 0;
public:
    explicit StubDiscounts(int percent) : m_percent(percent) {}
    int percentFor(const QString &userId) const override
    {
        ++m_calls;
        Q_UNUSED(userId);
        return m_percent;
    }
    int calls() const { return m_calls; }
};

// ============================================================================
// 三、测试类：QObject 子类 + private slots
// ============================================================================
class TestPricing : public QObject {
    Q_OBJECT
private slots:
    // ── 四个钩子（顺序见附录那张表；本讲讲计数不讲玄学） ──
    void initTestCase()
    {
        qInfo() << "[钩子] initTestCase()  —— 整个测试对象只跑 1 次，在任何测试函数之前";
        hooks << QStringLiteral("initTestCase");
    }
    void cleanupTestCase()
    {
        qInfo() << "[钩子] cleanupTestCase() —— 只跑 1 次，在所有测试函数之后；"
                << "本轮 init()/cleanup() 各跑了" << rounds << "次";
        qInfo() << "[钩子] 完整顺序（头 6 项）：" << hooks.mid(0, 6);
    }
    void init()
    {
        ++rounds;
        hooks << QStringLiteral("init");
        // 每个测试函数前重建引擎：测试之间**不许共享可变状态**（第一条纪律）
        engine = std::make_unique<PricingEngine>(&stub80);
    }
    void cleanup()
    {
        hooks << QStringLiteral("cleanup");
        // 证据：stub 每轮都会被上面那个新引擎用到，计数归零靠重建而不是手动 reset
        engine.reset();
    }

    // ── 测试①：行小计。一个函数只验一件事（「行小计等于单价×数量」），
    //    正常/边界/异常三类靠数据行区分 ──
    void lineTotal_data()
    {
        QTest::addColumn<QString>("sku");
        QTest::addColumn<int>("unitCents");
        QTest::addColumn<int>("qty");
        QTest::addColumn<qint64>("expected");

        QTest::newRow("normal_3x1990")   << QStringLiteral("A-1") << 1990 << 3  << qint64(5970);
        QTest::newRow("edge_zero_qty")   << QStringLiteral("A-2") << 1990 << 0  << qint64(0);
        QTest::newRow("edge_zero_price") << QStringLiteral("A-3") << 0    << 7  << qint64(0);
        // 边界：1990 × 600000 = 1.194e9 > 上限 1e9 → 钳到上限
        QTest::newRow("edge_capped")     << QStringLiteral("A-4") << 1990 << 600000
                                          << PricingEngine::kLineCapCents;
    }
    void lineTotal()
    {
        QFETCH(QString, sku);
        QFETCH(int, unitCents);
        QFETCH(int, qty);
        QFETCH(qint64, expected);

        PricingEngine eng(nullptr);
        QCOMPARE(eng.lineTotalCents(Line{sku, unitCents, qty}), expected);
    }

    // ── 测试②：整单金额，换成 addRow 的 printf 写法（同一件事的第二种语法） ──
    void orderTotal_data()
    {
        QTest::addColumn<int>("unitCents");
        QTest::addColumn<int>("qty");
        QTest::addColumn<int>("percent");
        QTest::addColumn<qint64>("expected");

        // addRow 的第一个参数是**标签的格式串**：这里用数量当标签，跑出来标签是 "10"/"0"
        QTest::addRow("no_discount")      << 500 << 10 << 0   << qint64(5000);
        QTest::addRow("percent_20")       << 500 << 10 << 20  << qint64(4000);
        QTest::addRow("percent_100")      << 500 << 10 << 100 << qint64(0);
        QTest::addRow("empty_order")      << 500 << 0  << 30  << qint64(0);
        // 边界：向下取整（不是四舍五入）——这条正是下面 QEXPECT_FAIL 的靶子
        QTest::addRow("rounding_199")     << 199 << 1  << 50  << qint64(99);
    }
    void orderTotal()
    {
        QFETCH(int, unitCents);
        QFETCH(int, qty);
        QFETCH(int, percent);
        QFETCH(qint64, expected);

        PricingEngine eng(nullptr);
        const QVector<Line> lines{Line{QStringLiteral("B"), unitCents, qty}};
        QCOMPARE(eng.orderTotalCents(lines, percent), expected);
    }

    // ── 测试③：优先级（数据驱动，覆盖 done / 逾期钳制 / 负数钳制） ──
    void priorityScore_data()
    {
        QTest::addColumn<QString>("state");
        QTest::addColumn<int>("daysOverdue");
        QTest::addColumn<int>("weight");
        QTest::addColumn<int>("expected");

        QTest::newRow("normal")     << QStringLiteral("open") << 2 << 3 << 44;   // 3*12+2*4
        QTest::newRow("done_zero")  << QStringLiteral("done") << 9 << 5 << 0;
        QTest::newRow("negative_due_zero") << QStringLiteral("open") << -7 << 1 << 12;
        QTest::newRow("overdue_capped")    << QStringLiteral("open") << 99 << 5 << 100;
    }
    void priorityScore()
    {
        QFETCH(QString, state);
        QFETCH(int, daysOverdue);
        QFETCH(int, weight);
        QFETCH(int, expected);

        PricingEngine eng(nullptr);
        QCOMPARE(eng.priorityScore(state, daysOverdue, weight), expected);
    }

    // ── 测试④：异常路径。「异常」也是行为，必须写测试，不能靠运气 ──
    void rejectsBadArguments()
    {
        PricingEngine eng(nullptr);

        // 宏的真名与参数顺序（qtestcase.h:131）：QVERIFY_THROWS_EXCEPTION(类型, 表达式...)
        // ⚠ Qt5 时代的 QVERIFY_EXCEPTION_THROWN(表达式, 类型) 参数是**反的**，
        //   它在 Qt6 里已被标记废弃（qtestcase.h:120 QT_DEPRECATED_SINCE(6, 3)），别再用。
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 eng.lineTotalCents(Line{QStringLiteral("C"), -1, 2}));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 eng.orderTotalCents({Line{QStringLiteral("C"), 10, 1}}, 101));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 eng.priorityScore(QStringLiteral("open"), 0, 0));

        // 顺带证明「正常值不抛」：QVERIFY_THROWS_NO_EXCEPTION（同文件 109 行）
        QVERIFY_THROWS_NO_EXCEPTION(eng.priorityScore(QStringLiteral("open"), 0, 5));
    }

    // ── 测试⑤：依赖注入的**证据**。不验数字，验「折扣确实来自被注入的对象」 ──
    void discountComesFromInjectedSource()
    {
        StubDiscounts stub(30);
        PricingEngine eng(&stub);
        const QVector<Line> lines{Line{QStringLiteral("D"), 1000, 10}};

        QCOMPARE(eng.orderTotalForUser(lines, QStringLiteral("u-1")), qint64(7000));
        QCOMPARE(stub.calls(), 1);            // 只问了一次接口，说明引擎自己没留折扣表

        // 换成另一个假实现，同一份引擎代码结果就变——这才叫「测得动」的结构
        StubDiscounts other(0);
        PricingEngine eng2(&other);
        QCOMPARE(eng2.orderTotalForUser(lines, QStringLiteral("u-1")), qint64(10000));

        // 生产实现也走同一条断言（接口一致 = 可替换）
        // ⚠ QHash **没有** initializer_list 构造函数（qhash.h 里查不到），必须 insert；
        //   这一类「凭记忆写出来的构造」正是编译器会当场拦住你的地方。
        QHash<QString, int> table;
        table.insert(QStringLiteral("u-1"), 50);
        TableDiscounts prod(table);
        PricingEngine eng3(&prod);
        QCOMPARE(eng3.orderTotalForUser(lines, QStringLiteral("u-1")), qint64(5000));
    }

    // ── 测试⑥：被测代码会 qWarning。测试要显式认领，而不是让输出变脏 ──
    void badConfigWarnsAndFallsBack()
    {
        PricingEngine eng(nullptr);

        // ignoreMessage 要求**整条消息文本完全一致**（qtestcase.h:485/487），
        // 所以格式串里别放中文（窄字符字面量的执行字符集不一定是 UTF-8）。
        QTest::ignoreMessage(QtWarningMsg,
                             "PricingEngine: bad discount config 'abc', fallback to 0");
        QCOMPARE(eng.configPercent(QStringLiteral("abc")), 0);

        QTest::ignoreMessage(QtWarningMsg,
                             "PricingEngine: bad discount config '120', fallback to 0");
        QCOMPARE(eng.configPercent(QStringLiteral("120")), 0);

        QCOMPARE(eng.configPercent(QStringLiteral("20")), 20);   // 合法值不该吵
    }

    // ── 测试⑦：已知缺陷登记处。业务说「要四舍五入」，代码只有整除（向下取整）。
    //    不改成绿的！先用 QEXPECT_FAIL 把这条记成 XFAIL，缺陷单号写进注释 ──
    void roundingShouldBeHalfUp_data()
    {
        QTest::addColumn<int>("unitCents");
        QTest::addColumn<int>("qty");
        QTest::addColumn<int>("percent");
        QTest::addColumn<qint64>("expected");

        QTest::newRow("exact")  << 1990 << 2 << 50 << qint64(1990);   // 3980*50/100 正好
        QTest::newRow("halfUp") << 199  << 1 << 50 << qint64(100);    // 199*50=9950/100=99.5 → 期望 100
    }
    void roundingShouldBeHalfUp()
    {
        QFETCH(int, unitCents);
        QFETCH(int, qty);
        QFETCH(int, percent);
        QFETCH(qint64, expected);

        PricingEngine eng(nullptr);
        const qint64 actual = eng.orderTotalCents({Line{QStringLiteral("E"), unitCents, qty}},
                                                  percent);
        // QEXPECT_FAIL 的三个参数（qtestcase.h:299）：数据行标签、说明、模式。
        // 标签必须**等于当前数据行**，所以只对那一行调用；模式只有 Abort / Continue
        // （qttestglobal.h:25 `enum TestFailMode { Abort = 1, Continue = 2 }`）：
        //   Continue = 这一条断言失败后本行剩下的语句继续跑；Abort = 当场结束本行。
        if (QByteArray(QTest::currentDataTag()) == "halfUp")
            QEXPECT_FAIL("halfUp", "已知缺陷 QTBUG-XXXX：折扣用了整除，半位被舍掉", Continue);
        QCOMPARE(actual, expected);
    }

    // ── 断言之外的「软证据」：QVERIFY2 自带人话说明（qtestcase.h:77） ──
    void capIsAppliedOnce()
    {
        PricingEngine eng(nullptr);
        const qint64 big = eng.lineTotalCents(Line{QStringLiteral("F"), 1990, 600000});
        QVERIFY2(big == PricingEngine::kLineCapCents,
                 "行金额应被钳在 kLineCapCents；失败说明上限规则没生效");
    }

private:
    QStringList hooks;                                  // 钩子顺序记录（演示用）
    int rounds = 0;                                     // init/cleanup 轮数
    StubDiscounts stub80{80};                           // 给 init() 用的常驻假依赖
    std::unique_ptr<PricingEngine> engine;
};

// 本讲没有 GUI，也不需要事件循环：QTEST_GUILESS_MAIN 造的是 QCoreApplication
// （qtest.h:299），跑完 qExec 返回失败计数，main 直接把它 return 出去。
QTEST_GUILESS_MAIN(TestPricing)

// ============================================================================
// 附一：四个钩子的执行时机（本讲打印的就是这张表的第 3~6 列）
// ============================================================================
/*
 *   调用顺序：initTestCase() → [ 测试函数 A ]* → cleanupTestCase()
 *   其中每轮 [ ] 里是：  init() → （若有 A_data() 则先跑一次 A_data()）→ A() → cleanup()
 *
 *   · initTestCase()     整个进程 1 次。用来建「贵的、只读的」资源：临时目录、
 *                        基准数据、随机种子。**不要**在这里建会被测试改坏的东西。
 *   · init()             **每个测试函数前**都跑。用来重置状态：本讲在这里重建引擎，
 *                        保证测试之间零共享可变状态——这是单元测试能乱序跑、
 *                        能单独跑（`./app priorityScore`）的前提。
 *   · <name>_data()      每个数据驱动函数**只跑 1 次**（不是每行 1 次），
 *                        它负责把表填好；然后每一行执行一次 <name>()。
 *   · cleanup()          每个测试函数后跑一次，无论中间断言有没有失败。
 *   · cleanupTestCase()  整个进程 1 次，放最后。
 *
 *   三条纪律：
 *     1) 一个测试函数只验一件事。名字能一眼看出验的是什么（本讲 7 个函数各验一件事）。
 *     2) 断言失败**不会中止进程**：QVERIFY/QCOMPARE 失败时宏内部 `return`
 *        （qtestcase.h:51 `#define QTEST_FAIL_ACTION ... return`），
 *        所以别指望「失败后后面的清理语句还会跑」——清理写在 cleanup() 里。
 *     3) 异常/崩溃是两回事：QVERIFY_THROWS_EXCEPTION 抓的是 C++ 异常；
 *        段错误由 QtTest 的崩溃处理器接住（qtestcase.h:422 `noCrashHandler`），
 *        整条测试进程当场结束，CI 里表现为「没有报告」而不是「一条 FAIL」。
 */

// ============================================================================
// 附二：QVERIFY 与 QCOMPARE（宏展开就在 qtestcase.h，别看文档别看记忆）
// ============================================================================
/*
 *   QVERIFY(expr)        → QTest::qVerify(static_cast<bool>(expr), "expr", "", 文件, 行)
 *     （qtestcase.h:65）失败时打印 `VERIFY: 2 == 3 failed` 这种**只有表达式文本**的行。
 *   QCOMPARE(a, b)       → QTest::qCompare(a, b, "a", "b", 文件, 行)  （qtestcase.h:88）
 *     失败时打印 `Compared values are not the same` 加两行：
 *        Actual   (a) : 真值
 *        Expected (b) : 真值
 *     类型由 `QTest::toString` 模板族负责（qtesttostring.h）；自己加的枚举/结构体
 *     想要可读输出，要么提供 toString 特化，要么先转成 int/QString 再比。
 *   QVERIFY2(expr, "人话")  → 比 QVERIFY 多一条你自己写的说明（qtestcase.h:77）。
 *
 *   选哪个：只要「两个值相不相等」就用 QCOMPARE（能拿到实际值，出错时救命）；
 *   只有「谓词成立吗」（容器非空、指针为 nullptr、区间关系）才用 QVERIFY/QVERIFY2。
 *   Qt6 还有一族带比较运算符的 QCOMPARE_LT/_GE/…（qtestcase.h:100~105），
 *   想写「小于等于」不用自己拼 QVERIFY(a <= b)，输出还能带两边真值。
 *
 *   浮点：qtestcase.h:574/577 给 float/double 单独立了 qCompare 重载，
 *   所以比浮点写 QCOMPARE(a, b) 而不是 QVERIFY(a == b)；
 *   要放宽阈值就比差值：QCOMPARE(qAbs(a - b), 0.0)这种写法别用，
 *   直接 QVERIFY(qAbs(a - b) < eps) 并配 QVERIFY2 说明阈值。
 */

// ============================================================================
// 附三：数据驱动两套写法 + 能从头文件核到的 QTest 运行期 API
// ============================================================================
/*
 *   写法 A（主流）：addColumn + newRow + `<<` 串参数
 *      QTest::addColumn<int>("unitCents");        // qtestcase.h:562（模板，内部走 qMetaTypeId）
 *      QTest::newRow("edge_zero_qty") << 1990 << 0 << qint64(0);   // qtestcase.h:568
 *      取值：QFETCH(int, unitCents);              // qtestcase.h:306
 *      ⚠ 列的类型必须是 **QMetaType 认识的**（QString/int/qint64/bool/QByteArray…）。
 *        自定义结构体（本讲的 Line）想直接当列，需要 Q_DECLARE_METATYPE
 *        （qmetatype.h:1503）并在 initTestCase() 里 qRegisterMetaType，
 *        否则编译期就过不去。**更省事的做法**：列只放基本类型，
 *        在测试函数里现场组装结构体（本讲就这么干的）。
 *
 *   写法 B：addRow 的 printf 风格（qtestcase.h:569，带 __attribute__((format(printf)))）
 *      QTest::addRow("percent_%d", p) << p << expected;
 *      标签是**格式化出来的**，一行代码同时表达「这是同一类的第 N 种参数」。
 *      因为是真 printf 检查，`%d` 配 int、`%lld` 配 qlonglong，写错编译器直接拦。
 *      两种写法混用没问题：addColumn 声明一次，newRow/addRow 都能填。
 *
 *   从本机头文件**确证存在**的 QtTest 宏（qtestcase.h）：
 *      QVERIFY 65 / QFAIL 71 / QVERIFY2 77 / QCOMPARE 88 /
 *      QCOMPARE_EQ|NE|LT|LE|GT|GE 100~105 /
 *      QVERIFY_THROWS_NO_EXCEPTION 109 / QVERIFY_THROWS_EXCEPTION 131 /
 *      QTRY_VERIFY 217（+ _WITH_TIMEOUT 211）/ QTRY_VERIFY2 227 /
 *      QTRY_COMPARE 238（+ _WITH_TIMEOUT **232**，另有 _EQ/_NE/_LT/_LE/_GT/_GE 242~287）/
 *      QSKIP 297 / QEXPECT_FAIL 299 / QFETCH 305 / QFETCH_GLOBAL 308 / QTEST 311 /
 *      QWARN 865 / QFINDTESTDATA 334 与 337（两支 #if 各一个）/ QEXTRACTTESTDATA 341
 *      ★ 勘误：旧稿这五个号是抄来的，本机 6.12.0 逐条 grep `^#define` 重数过——
 *        _WITH_TIMEOUT 版 QTRY_COMPARE 在 **232**（旧稿 212，那是 QTRY_VERIFY_WITH_TIMEOUT 的地盘）、
 *        QFETCH **305**（旧稿 306）、QFETCH_GLOBAL **308**（旧稿 311）、QTEST **311**（旧稿 314）。
 *        错位是连号的：305/308/311 三个都对，说明旧稿整体偏 +1/+3，不是手滑一次。
 *        顺手记一条容易踩的：QVERIFY_THROWS_EXCEPTION 有两套定义（131 是支持异常的那支，
 *        158/160 是 QT_NO_EXCEPTIONS 下的替身，后者一调用就 static_assert 拦你），
 *        所以「这个宏在不在」取决于异常开关，不只取决于头文件。
 *   **确证存在**的 main 宏（qtest.h，注意：本版本 QtTest 目录里没有 qtest_main.h，
 *   三个 main 宏都在 qtest.h 里）：
 *      QTEST_APPLESS_MAIN 269 / QTEST_MAIN 296 / QTEST_GUILESS_MAIN 299
 *   **确证不存在 / 别按记忆写**的：
 *      · `Test::qWait` 这个 Qt4/Qt5 时代的包装类在 Qt6.12 的 QtTest 头里查无此物，
 *        qWait/qSleep 现在是 `QTest::` 命名空间里的自由函数，
 *        声明在 **QtCore/qtestsupport_core.h:83/85（int 与 std::chrono::milliseconds 两套）**。
 *      · `QSignalSpy::capturedSignals()` 在本版本 qsignalspy.h 里没有（第 2 讲讲它到底有什么）。
 *      · 老的 `QTest::qTest()` / `QTestEventList` 仍在（qtestcase.h:314、qtestevent.h）。
 *   运行期 API（都能在 qtestcase.h 找到声明）：
 *      QTest::qExec(QObject*, int argc, char **argv)      // 430，自己写 main 时用
 *      QTest::qExec(QObject*, const QStringList&)         // 431
 *      QTest::setThrowOnFail(bool) / ThrowOnFailEnabler   // 439 / 443：断言失败改抛异常
 *      QTest::ignoreMessage(...) / failOnWarning(...)     // 485~492：消息与告警的白名单/黑名单
 *      QTest::currentDataTag() / currentTestFunction()    // 508/509
 *      QTest::currentTestFailed() / currentTestResolved() // 511/512
 *
 *   命令行选项：QTest 支持 -maxwarnings、-o <文件,格式>、-xmlhandler、-functions、
 *   -datatags、-silent/-v1/-v2、-fatalwarnings、-eventrotation、-duration 等
 *   （解析在 qtestcase.cpp，本机只有头，**核不到选项表，具体以 QTest 文档为准**）。
 *   本模块的 CMake 已经把每个 demo 注册成 CTest 用例（CMakeLists.txt:88~90），
 *   判绿只看退出码，不解析文本——所以**不用**在这上面花功夫。
 */

// ============================================================================
// 附四：为什么「被测代码要能被测试」是这一讲的正题
// ============================================================================
/*
 *   新手写完功能再来「加测试」，会发现测不了：函数里 new 了数据库、
 *   读了一个硬编码路径、返回值要从一个 QLabel 里 text() 出来。
 *   这不是测试的问题，是**分层**的问题（第 3 讲正面展开）。本讲用的三招：
 *     1) 把「规则」写成纯函数：输入参数、返回数字，不碰全局、不碰控件
 *        （lineTotalCents / priorityScore 一眼能看完，这就是好测的函数）。
 *     2) 外部依赖走接口 + 构造注入（DiscountSource）：
 *        测试塞 StubDiscounts(30)，生产塞 TableDiscounts(hash)。
 *        引擎**不拥有**依赖（构造收 const 指针、析构不 delete），
 *        所以测试里把依赖放栈上就行，不用操心所有权。
 *     3) 把「吵」当契约：非法配置 qWarning + 兜底值。测试用 ignoreMessage 认领，
 *        等于把「这里预期会吵」写成了可执行文档。
 *   反过来说：如果只有 QTest 而代码结构不对，你只能得到一堆「测了个寂寞」的断言。
 *
 * ——— 老师傅的话 ———
 *   徒弟，单元测试的门槛不在宏，在**你敢不敢承认自己没验过**。
 *   你在 widgets 模块里写的那些界面，跑一遍看着挺对，那叫「我信了」，不叫「我验了」。
 *   QTest 干的事就是把「我信了」变成一条机器可判的断言，加上退出码。
 *   我带过的团队里，真正省钱的不是断言多，而是**每个 bug  fix 之后补一条能复现它的测试**——
 *   本讲那条 roundingShouldBeHalfUp 就是这个意思：缺陷先登记，别急着改绿。
 *
 *   第二件事：QCOMPARE 失败会打出实际值，这个价值千金。
 *   我见过太多人写 QVERIFY(a == b)，出错时屏幕上只有一句「a == b failed」，
 *   然后开始猜。少写一个宏，多两行输出，这笔账怎么算都划算。
 *
 *   第三件事：别把测试写成第二个应用。测试函数里不要建 QApplication、
 *   不要读网络、不要依赖时间（本讲全是纯数字）。
 *   一条测试跑超过一秒，团队就会开始跳过它；跳过它的第二天，它就烂掉了。
 *   QTEST_GUILESS_MAIN 就是这个纪律的物理形式：不需要 GUI，就别拉 GUI。
 *
 * ——— 小练习 ———
 *   1. 把 `TableDiscounts` 换成一张 10 万条的 QHash，再用 QElapsedTimer 在
 *      initTestCase() 里量一次 percentFor() 的耗时并 qInfo 打出来；
 *      然后回答：为什么这条**不该**放进 CI 的断言里（提示：机器负载与阈值）。
 *   2. 给 priorityScore() 加一条「state == "blocked" 权重上限 60」的新规则，
 *      先往 priorityScore_data() 里补 3 行（含边界 blocked/0/5），
 *      再改实现，观察改实现之前那 3 行是不是**当场变红**——这就是测试的价值。
 *   3. 把 `halfUp` 那行的 QEXPECT_FAIL 删掉，改成 `// TODO 以后再修`，
 *      重跑一次：说明「XFAIL 消失」与「测试变红」在 CI 上的区别，
 *      以及为什么 QEXPECT_FAIL 必须写清缺陷单号。
 *   4. 用 QTEST_MAIN 替掉 QTEST_GUILESS_MAIN 重跑一遍（不加 -platform），
 *      看它报什么错；再对照第 2 讲说明两者的差别在哪一行代码上（qtest.h:296/299）。
 */
// ============================================================================

// 本文件有 Q_OBJECT 类（TestPricing），AUTOMOC 生成同名 .moc 在这里包含。
#include "01_qtest_unit.moc"
