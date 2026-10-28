// ============================================================================
// tst_calc.cpp · QtTest 用例：三个函数、五种边界
// ----------------------------------------------------------------------------
// 本工程解决什么问题：
//   把「我觉得对」换成「机器判定对」：
//     · 数据驱动的 clamp 边界（QTest::addColumn + newRow + QFETCH）
//     · 空集合与 NaN 的约定（不能用 == 比较 NaN）
//     · 除零与越界百分比（约定必须被测试守住）
//   跑法：
//     ctest --test-dir build --output-on-failure
//     ./build/tst_calc            # 单独跑，看每个用例的名字
//     ./build/tst_calc clamp数据  # 只跑某一个函数（名字以输出为准）
//
// 关于 #include "tst_calc.moc"：
//   下面的 TestCalc 类**写在 .cpp 里**，AUTOMOC 不会为 .cpp 生成独立的
//   moc_xxx.cpp；约定的钩子是：类定义之后 include "<本文件名>.moc"。
//   少了这一句，链接期会报 undefined reference to vtable / staticMetaObject。
//
// 对应构建目标名：tst_calc（用 QTEST_GUILESS_MAIN，不需要平台插件）
// ============================================================================

#include <QtTest>
#include <QDebug>           // qInfo()；QtTest 通常已经带进来，显式写出是为了不依赖它
#include <QtNumeric>

#include "calc/calc.h"

class TestCalc : public QObject
{
    Q_OBJECT

private slots:
    // initTestCase / cleanupTestCase：整个测试类各跑一次
    void initTestCase();
    void cleanupTestCase();

    // 数据驱动的写法：函数名 + _data() 提供参数表
    void clamp_data();
    void clamp();

    void meanEdgeCases();
    void percentEdgeCases();
};

void TestCalc::initTestCase()
{
    qInfo().noquote() << "开始跑 calc 的用例";
}

void TestCalc::cleanupTestCase()
{
    qInfo().noquote() << "calc 的用例跑完了";
}

void TestCalc::clamp_data()
{
    QTest::addColumn<int>("value");
    QTest::addColumn<int>("lo");
    QTest::addColumn<int>("hi");
    QTest::addColumn<int>("expected");

    // newRow(标签)：标签会出现在失败输出里，起名要能一眼看出是哪一组
    QTest::newRow("区间内原样返回") << 50 << 0 << 100 << 50;
    QTest::newRow("低于下界夹到下界") << -5 << 0 << 100 << 0;
    QTest::newRow("高于上界夹到上界") << 120 << 0 << 100 << 100;
    QTest::newRow("边界值本身不变") << 0 << 0 << 100 << 0;
    QTest::newRow("lo 等于 hi") << 7 << 3 << 3 << 3;
    QTest::newRow("lo 大于 hi 时原样返回") << 7 << 9 << 3 << 7;
}

void TestCalc::clamp()
{
    QFETCH(int, value);
    QFETCH(int, lo);
    QFETCH(int, hi);
    QFETCH(int, expected);

    // QCOMPARE 失败时会打印两边的原文与值；QVERIFY 只打印表达式。
    QCOMPARE(calc::clamp(value, lo, hi), expected);
}

void TestCalc::meanEdgeCases()
{
    // 空集合的约定：NaN
    const double empty = calc::mean({});
    QVERIFY2(qIsNaN(empty), "空集合应返回 NaN，而不是 0");

    // 非空集合：普通平均值
    QCOMPARE(calc::mean({2.0, 4.0, 6.0, 8.0}), 5.0);

    // 单个元素：平均值就是它本身
    QCOMPARE(calc::mean({3.5}), 3.5);
}

void TestCalc::percentEdgeCases()
{
    QCOMPARE(calc::percent(0, 0), 0);       // 除零的约定
    QCOMPARE(calc::percent(5, 0), 0);       // 同上
    QCOMPARE(calc::percent(0, 10), 0);
    QCOMPARE(calc::percent(10, 10), 100);
    QCOMPARE(calc::percent(11, 10), 100);   // part > total 时封顶，不出 110%
    QCOMPARE(calc::percent(7, 9), 77);      // 7*100/9 = 77.77…，向下取整
}

// 写在 .cpp 里的 Q_OBJECT 类，必须在这一句之前完成类定义；
// 这一句把 moc 生成的东西展开进来。
#include "tst_calc.moc"

// QTEST_GUILESS_MAIN 展开成 main()：创建 QCoreApplication、跑所有用例、
// 全通过返回 0，有失败返回非 0。ctest 就靠这个退出码判卷。
QTEST_GUILESS_MAIN(TestCalc)
