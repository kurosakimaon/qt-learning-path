#include "core/taskfilterproxy.h"

#include "core/taskmodel.h"

TaskFilterProxy::TaskFilterProxy(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
}

void TaskFilterProxy::setOnlyOpen(bool onlyOpen)
{
    if (m_onlyOpen == onlyOpen)
        return;

    beginFilterChange();
    m_onlyOpen = onlyOpen;
    endFilterChange(Direction::Rows);   // 只影响行，不涉及列过滤
}

void TaskFilterProxy::setQueryText(const QString& text)
{
    const QString query = text.trimmed();
    if (m_query == query)
        return;

    beginFilterChange();
    m_query = query;
    endFilterChange(Direction::Rows);
}

bool TaskFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    Q_UNUSED(sourceParent);

    const auto* model = qobject_cast<const TaskModel*>(sourceModel());
    if (!model)
        return false;

    const QVector<Task>& tasks = model->tasks();
    if (sourceRow < 0 || sourceRow >= tasks.size())
        return false;

    const Task& task = tasks.at(sourceRow);
    if (m_onlyOpen && task.done)
        return false;

    if (m_query.isEmpty())
        return true;

    return task.title.contains(m_query, Qt::CaseInsensitive)
           || task.notes.contains(m_query, Qt::CaseInsensitive);
}

bool TaskFilterProxy::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    const auto* model = qobject_cast<const TaskModel*>(sourceModel());
    if (!model)
        return QSortFilterProxyModel::lessThan(left, right);

    const Task lhs = model->taskAt(left.row());
    const Task rhs = model->taskAt(right.row());

    switch (left.column()) {
    case TaskModel::PriorityColumn:
        return static_cast<int>(lhs.priority) < static_cast<int>(rhs.priority);

    case TaskModel::DueColumn:
        // 没有截止日期的一律排最后：先比「有没有」，再比日期。
        if (!lhs.due.isValid())
            return false;
        if (!rhs.due.isValid())
            return true;
        return lhs.due < rhs.due;

    case TaskModel::DoneColumn:
        return static_cast<int>(lhs.done) < static_cast<int>(rhs.done);

    default:
        return QString::localeAwareCompare(lhs.title, rhs.title) < 0;
    }
}
