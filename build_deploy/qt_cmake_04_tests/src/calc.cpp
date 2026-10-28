// ============================================================================
// calc.cpp · 实现（注意每一个「约定」都对应一处 if）
// ============================================================================

#include "calc/calc.h"

#include <QtNumeric>        // qQNaN()（qnumeric.h 第 76 行）；模块头名就是 <QtNumeric>

namespace calc {

int clamp(int value, int lo, int hi)
{
    // 约定：lo > hi 属于调用方写错了，这里不纠正、不崩，原样返回。
    if (lo > hi)
        return value;
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

double mean(const QList<double> &values)
{
    // 空集合：返回 NaN。用 NaN 而不是 0，是为了让「没有数据」和「平均值为 0」
    // 在数值上就能区分开。
    if (values.isEmpty())
        return qQNaN();

    double sum = 0.0;
    for (double v : values)
        sum += v;
    return sum / static_cast<double>(values.size());
}

int percent(int part, int total)
{
    // 除零的答案由这行约定：没有总量，就没有百分比。
    if (total <= 0)
        return 0;

    const int p = static_cast<int>((static_cast<long long>(part) * 100) / total);
    // 夹在 0~100：part 为负或大于 total 时都不让界面出现 -3% 或 120%。
    return clamp(p, 0, 100);
}

} // namespace calc
