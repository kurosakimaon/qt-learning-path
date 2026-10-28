#include "core/taskmodel.h"

#include <QColor>
#include <QStringList>
#include <QVariant>

#include <utility>

TaskModel::TaskModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int TaskModel::rowCount(const QModelIndex& parent) const
{
    // 表模型没有子层级：有效 parent 一律 0 行，这是最容易写错的一行。
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_tasks.size());
}

int TaskModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return ColumnCount;
}

QVariant TaskModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tasks.size())
        return {};

    const Task& task = m_tasks.at(index.row());
    const bool overdue = isOverdue(task);

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case TitleColumn:
            return task.title;
        case PriorityColumn:
            return priorityName(task.priority);
        case DueColumn:
            return task.due.isValid()
                       ? task.due.toString(QStringLiteral("yyyy-MM-dd"))
                       : QStringLiteral("—");
        case DoneColumn:
            return task.done ? QStringLiteral("已完成") : QStringLiteral("未完成");
        default:
            return {};
        }

    case Qt::EditRole:
        // 编辑器和 setData 之间走的是 EditRole，方向和 DisplayRole 相反：
        // 委托先把它递给编辑器，提交时再原样递回来。
        switch (index.column()) {
        case TitleColumn:
            return task.title;
        case PriorityColumn:
            return static_cast<int>(task.priority);
        case DueColumn:
            return task.due;
        case DoneColumn:
            return task.done;
        default:
            return {};
        }

    case Qt::CheckStateRole:
        if (index.column() == DoneColumn)
            return static_cast<int>(task.done ? Qt::Checked : Qt::Unchecked);
        return {};

    case Qt::TextAlignmentRole:
        if (index.column() == PriorityColumn || index.column() == DoneColumn)
            return static_cast<int>(Qt::AlignCenter);
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);

    case Qt::ForegroundRole:
        if (overdue)
            return QColor(0xb9, 0x1c, 0x1c);          // 逾期：红
        if (task.done)
            return QColor(0x94, 0xa3, 0xb8);          // 已完成：灰
        return {};

    case Qt::ToolTipRole: {
        QStringList lines;
        lines << task.title;
        if (task.due.isValid())
            lines << QStringLiteral("截止：%1").arg(task.due.toString(QStringLiteral("yyyy-MM-dd")));
        lines << QStringLiteral("优先级：%1").arg(priorityName(task.priority));
        if (!task.notes.isEmpty())
            lines << task.notes;
        return lines.join(QLatin1Char('\n'));
    }

    case IdRole:
        return task.id;
    case DoneRole:
        return task.done;
    case PriorityRole:
        return static_cast<int>(task.priority);
    case DueRole:
        return task.due;
    case NotesRole:
        return task.notes;

    default:
        return {};
    }
}

bool TaskModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tasks.size())
        return false;

    Task& task = m_tasks[index.row()];

    // 1) 勾选框：视图发的是 CheckStateRole，不是 EditRole。
    if (role == Qt::CheckStateRole && index.column() == DoneColumn) {
        const bool done = value.toInt() == static_cast<int>(Qt::Checked);
        if (task.done == done)
            return false;
        task.done = done;
        // done 会影响整行的前景色（灰色/红色），所以整行报变，而不是只报这一格。
        emit dataChanged(this->index(index.row(), 0),
                         this->index(index.row(), ColumnCount - 1),
                         {Qt::DisplayRole, Qt::CheckStateRole, Qt::ForegroundRole});
        emitCounts();
        return true;
    }

    if (role != Qt::EditRole && role != Qt::DisplayRole)
        return false;

    switch (index.column()) {
    case TitleColumn: {
        const QString title = value.toString().trimmed();
        if (title.isEmpty() || title == task.title)
            return false;               // 空标题不允许写回，界面保持原值
        task.title = title;
        emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole, Qt::ToolTipRole});
        return true;
    }
    case PriorityColumn: {
        const Priority priority = priorityFromInt(value.toInt());
        if (priority == task.priority)
            return false;
        task.priority = priority;
        emit dataChanged(index, index,
                         {Qt::DisplayRole, Qt::EditRole, Qt::ToolTipRole, PriorityRole});
        emitCounts();
        return true;
    }
    case DueColumn: {
        const QDate due = value.toDate();
        if (due == task.due)
            return false;
        task.due = due;                 // 无效 QDate 表示清空截止日期
        emit dataChanged(this->index(index.row(), 0),
                         this->index(index.row(), ColumnCount - 1),
                         {Qt::DisplayRole, Qt::EditRole, Qt::ForegroundRole, Qt::ToolTipRole});
        emitCounts();
        return true;
    }
    default:
        return false;
    }
}

QVariant TaskModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal) {
        if (role == Qt::DisplayRole) {
            switch (section) {
            case TitleColumn:
                return QStringLiteral("任务");
            case PriorityColumn:
                return QStringLiteral("优先级");
            case DueColumn:
                return QStringLiteral("截止日期");
            case DoneColumn:
                return QStringLiteral("完成");
            default:
                return {};
            }
        }
        if (role == Qt::TextAlignmentRole)
            return static_cast<int>(Qt::AlignCenter);
        return {};
    }

    if (role == Qt::DisplayRole)
        return section + 1;             // 竖表头显示行号（从 1 开始）
    if (role == Qt::TextAlignmentRole)
        return static_cast<int>(Qt::AlignCenter);
    return {};
}

Qt::ItemFlags TaskModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags base = QAbstractTableModel::flags(index);
    if (!index.isValid())
        return base;

    switch (index.column()) {
    case TitleColumn:
    case PriorityColumn:
    case DueColumn:
        base |= Qt::ItemIsEditable;
        break;
    case DoneColumn:
        base |= Qt::ItemIsUserCheckable;
        break;
    default:
        break;
    }
    return base;
}

bool TaskModel::insertRows(int row, int count, const QModelIndex& parent)
{
    if (parent.isValid() || count <= 0)
        return false;
    if (row < 0 || row > m_tasks.size())
        return false;

    beginInsertRows(QModelIndex(), row, row + count - 1);
    for (int i = 0; i < count; ++i) {
        Task task;
        task.id = m_nextId++;
        task.title = QStringLiteral("新任务 %1").arg(task.id);
        m_tasks.insert(row + i, task);
    }
    endInsertRows();
    emitCounts();
    return true;
}

bool TaskModel::removeRows(int row, int count, const QModelIndex& parent)
{
    if (parent.isValid() || count <= 0)
        return false;
    if (row < 0 || row + count > m_tasks.size())
        return false;

    beginRemoveRows(QModelIndex(), row, row + count - 1);
    for (int i = 0; i < count; ++i)
        m_tasks.removeAt(row);
    endRemoveRows();
    emitCounts();
    return true;
}

void TaskModel::setTasks(const QVector<Task>& tasks)
{
    beginResetModel();
    m_tasks = tasks;
    // 修正 id：导入的数据可能没有 id，也可能重复；模型内部保证唯一且递增。
    int maxId = 0;
    for (Task& task : m_tasks) {
        if (task.id <= 0)
            task.id = ++maxId;
        else
            maxId = qMax(maxId, task.id);
    }
    m_nextId = maxId + 1;
    endResetModel();
    emitCounts();
}

int TaskModel::addTask(Task task)
{
    if (task.id <= 0)
        task.id = m_nextId;
    m_nextId = qMax(m_nextId, task.id + 1);

    const int row = static_cast<int>(m_tasks.size());
    beginInsertRows(QModelIndex(), row, row);
    m_tasks.append(task);
    endInsertRows();
    emitCounts();
    return task.id;
}

bool TaskModel::updateTask(int row, const Task& task)
{
    if (row < 0 || row >= m_tasks.size())
        return false;

    Task updated = task;
    updated.id = m_tasks.at(row).id;      // id 和完成状态不归编辑对话框管
    updated.done = m_tasks.at(row).done;

    m_tasks[row] = updated;
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1),
                     {Qt::DisplayRole, Qt::EditRole, Qt::ToolTipRole,
                      PriorityRole, DueRole, NotesRole, Qt::ForegroundRole});
    emitCounts();
    return true;
}

bool TaskModel::setDone(int row, bool done)
{
    return setData(index(row, DoneColumn),
                   static_cast<int>(done ? Qt::Checked : Qt::Unchecked),
                   Qt::CheckStateRole);
}

bool TaskModel::removeCompleted()
{
    bool removed = false;
    // 倒序删：删掉第 i 行不会影响第 i-1 行，行号不用重算。
    for (int row = static_cast<int>(m_tasks.size()) - 1; row >= 0; --row) {
        if (m_tasks.at(row).done) {
            removeRows(row, 1);
            removed = true;
        }
    }
    return removed;
}

void TaskModel::clear()
{
    beginResetModel();
    m_tasks.clear();
    m_nextId = 1;
    endResetModel();
    emitCounts();
}

void TaskModel::emitCounts()
{
    int done = 0;
    for (const Task& task : std::as_const(m_tasks)) {
        if (task.done)
            ++done;
    }
    emit countsChanged(static_cast<int>(m_tasks.size()), done);
}
