// ============================================================================
//  todo_desktop · 任务表模型（第陆阶段知识的第一个落地点）
// ----------------------------------------------------------------------------
//  为什么手写而不是用 QStandardItemModel？
//    - 数据本来就在 Task 结构里，不需要第二份「item 对象」镜像
//    - 只想暴露「标题/优先级/截止日期/完成」四列，role 自己定
//    - 需要在改数据时精确发出 dataChanged，而不是靠便利类内部代劳
//
//  五件事一件都不能少：
//    rowCount / columnCount / data / headerData / flags（+ setData 让格子可写）
//  结构变更统一走 beginInsertRows/endInsertRows、beginRemoveRows/endRemoveRows。
// ============================================================================
#pragma once

#include "core/task.h"

#include <QAbstractTableModel>

class TaskModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        TitleColumn = 0,
        PriorityColumn,
        DueColumn,
        DoneColumn,
        ColumnCount
    };

    // 自定义 role 从 Qt::UserRole 之后开始，避免和 Qt 自带 role 撞号。
    // 委托靠 PriorityRole 拿数值来决定画什么颜色，不靠解析显示文本。
    enum Role {
        IdRole = Qt::UserRole + 1,
        DoneRole,
        PriorityRole,
        DueRole,
        NotesRole
    };

    explicit TaskModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value,
                 int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    bool insertRows(int row, int count, const QModelIndex& parent = QModelIndex()) override;
    bool removeRows(int row, int count, const QModelIndex& parent = QModelIndex()) override;

    // ── 业务层用的小接口（不是 Qt 的虚函数）──────────────────────────────
    const QVector<Task>& tasks() const { return m_tasks; }
    Task taskAt(int row) const { return m_tasks.value(row); }

    void setTasks(const QVector<Task>& tasks);
    int addTask(Task task);                       // 返回最终分配的 id
    bool updateTask(int row, const Task& task);    // 保留原 id 与 done
    bool setDone(int row, bool done);
    bool removeCompleted();                        // 从后往前删，避免行号漂移
    void clear();

signals:
    // 总数/完成数变化时发一次，状态栏用它做「立刻可见」的反馈；
    // 更细的统计（逾期、各优先级）交给后台线程算，见 StatsService。
    void countsChanged(int total, int done);

private:
    void emitCounts();

    QVector<Task> m_tasks;
    int m_nextId = 1;
};
