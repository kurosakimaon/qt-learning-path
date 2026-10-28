// ============================================================================
//  todo_desktop · 后台统计（第柒阶段「跨线程只能走信号」的落地点）
// ----------------------------------------------------------------------------
//  为什么统计要放到后台？
//    任务多到几千条时，一次全量统计虽然不快，但也不该占着界面线程。
//    这里用 QtConcurrent::run 把「算统计」丢进全局线程池，主线程只管收结果。
//
//  三条规矩：
//    1. 任务快照按值拷贝进 lambda —— 绝不让工作线程碰模型内部的数据。
//    2. 结果通过信号回到主线程（QFutureWatcher 的 finished 在创建它的线程发）。
//    3. 同一时刻只跑一个任务；期间来的请求只保留最后一次（统计的是「当前状态」，
//       中间态算出来也没人要看）。这就是「合并请求」最小实现。
// ============================================================================
#pragma once

#include "core/task.h"

#include <QFutureWatcher>
#include <QObject>
#include <QVector>

class StatsService : public QObject
{
    Q_OBJECT

public:
    // 工作线程的返回值：统计结果 + 这次在后台花了多久。
    // 做成公开嵌套类型，是为了让 QtConcurrent 的模板在类外也能引用它。
    struct Result {
        TaskStats stats;
        qint64 elapsedMs = 0;
    };

    explicit StatsService(QObject* parent = nullptr);

    bool isBusy() const;
    void request(const QVector<Task>& snapshot);

signals:
    void statsReady(const TaskStats& stats, qint64 elapsedMs);

private:
    QFutureWatcher<Result> m_watcher;
    QVector<Task> m_pending;
    bool m_hasPending = false;
};
