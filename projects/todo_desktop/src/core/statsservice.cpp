#include "core/statsservice.h"

#include <QDate>
#include <QElapsedTimer>
#include <QFuture>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

StatsService::StatsService(QObject* parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<Result>::finished, this, [this] {
        const Result result = m_watcher.result();
        emit statsReady(result.stats, result.elapsedMs);

        if (m_hasPending) {
            // 合并策略：忙的时候只留最后一份快照，算完立刻补上这一次。
            m_hasPending = false;
            const QVector<Task> next = std::move(m_pending);
            m_pending.clear();
            request(next);
        }
    });
}

bool StatsService::isBusy() const
{
    return m_watcher.isRunning();
}

void StatsService::request(const QVector<Task>& snapshot)
{
    if (m_watcher.isRunning()) {
        m_pending = snapshot;
        m_hasPending = true;
        return;
    }

    const QDate today = QDate::currentDate();
    QFuture<Result> future = QtConcurrent::run(
        QThreadPool::globalInstance(),
        [snapshot, today]() -> Result {
            QElapsedTimer timer;
            timer.start();

            // 故意加一点工作量：任务少时统计快得看不见，
            // 加 30ms 是为了让「它确实不在界面线程上跑」这件事可观察。
            QThread::msleep(30);

            Result result;
            result.stats = computeStats(snapshot, today);
            result.elapsedMs = timer.elapsed();
            return result;
        });

    m_watcher.setFuture(future);
}
