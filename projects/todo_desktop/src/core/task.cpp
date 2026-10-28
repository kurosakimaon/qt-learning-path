#include "core/task.h"

#include <QStringLiteral>

QString priorityName(Priority priority)
{
    switch (priority) {
    case Priority::Low:
        return QStringLiteral("低");
    case Priority::High:
        return QStringLiteral("高");
    case Priority::Normal:
        break;
    }
    return QStringLiteral("普通");
}

Priority priorityFromInt(int value)
{
    // 越界值一律回落到 Normal：数据可能来自用户手改的 JSON，
    // 这里不做「报错」，只保证界面不会因为一个 7 就画不出东西。
    switch (value) {
    case static_cast<int>(Priority::Low):
        return Priority::Low;
    case static_cast<int>(Priority::High):
        return Priority::High;
    default:
        return Priority::Normal;
    }
}

bool isOverdue(const Task& task, QDate today)
{
    return !task.done && task.due.isValid() && task.due < today;
}

TaskStats computeStats(const QVector<Task>& tasks, QDate today)
{
    TaskStats stats;
    stats.total = tasks.size();
    for (const Task& task : tasks) {
        if (task.done)
            ++stats.done;
        else
            ++stats.open;

        switch (task.priority) {
        case Priority::Low:
            ++stats.low;
            break;
        case Priority::High:
            ++stats.high;
            break;
        case Priority::Normal:
            ++stats.normal;
            break;
        }

        if (isOverdue(task, today))
            ++stats.overdue;
    }
    return stats;
}
