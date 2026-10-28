// ============================================================================
// main.cpp · 命令行小程序：库能被「没有界面」的程序复用
// ----------------------------------------------------------------------------
// 本工程解决什么问题：
//   证明「核心库不碰 Widgets」不是口号：同一个 calc 库，既能被测试用，
//   也能被一个 QCoreApplication 的命令行程序用。这里一个 QWidget 都没有，
//   也就不需要平台插件——它在任何无头机器上都能跑。
//
// 运行前须知：
//   · 无人值守，也不需要事件循环：打印完直接返回 0。
//
// 对应构建目标名：calc_app
// ============================================================================

#include <QCoreApplication>
#include <QList>
#include <QDebug>
#include <QtNumeric>

#include "calc/calc.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qInfo().noquote() << "clamp(120, 0, 100) =" << calc::clamp(120, 0, 100);
    qInfo().noquote() << "clamp(-5, 0, 100)  =" << calc::clamp(-5, 0, 100);

    const QList<double> samples{2.0, 4.0, 6.0, 8.0};
    qInfo().noquote() << "mean({2,4,6,8})    =" << calc::mean(samples);

    const double emptyMean = calc::mean({});
    qInfo().noquote() << "mean({}) 是 NaN 吗？" << qIsNaN(emptyMean);

    qInfo().noquote() << "percent(7, 9)      =" << calc::percent(7, 9);
    qInfo().noquote() << "percent(5, 0)      =" << calc::percent(5, 0)
                      << "（约定：没有总量就没有百分比）";

    return 0;
}
