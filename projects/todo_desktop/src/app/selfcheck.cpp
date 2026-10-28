#include "app/selfcheck.h"

#include "core/jsoncodec.h"
#include "core/statsservice.h"
#include "core/taskfilterproxy.h"
#include "core/taskmodel.h"
#include "core/taskrepository.h"

#include <QDebug>
#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>
#include <QTemporaryDir>

namespace {

int g_failed = 0;

void check(bool ok, const QString& what)
{
    if (ok) {
        qInfo().noquote() << "  [PASS]" << what;
    } else {
        ++g_failed;
        qWarning().noquote() << "  [FAIL]" << what;
    }
}

} // namespace

int selfcheck::run()
{
    qInfo().noquote() << "== todo_desktop 自检开始 ==";

    QTemporaryDir dir;
    if (!dir.isValid()) {
        qCritical().noquote() << "  [FAIL] 无法创建临时目录";
        return 2;
    }
    const QString dbPath = dir.filePath(QStringLiteral("todo.db"));
    const QString jsonPath = dir.filePath(QStringLiteral("tasks.json"));

    // ── 1. 模型：增、改、角色、信号 ──────────────────────────────────────
    TaskModel model;
    int dataChangedCount = 0;
    int lastTotal = -1;
    int lastDone = -1;

    QObject::connect(&model, &QAbstractItemModel::dataChanged,
                     [&dataChangedCount](const QModelIndex&, const QModelIndex&,
                                         const QList<int>&) { ++dataChangedCount; });
    QObject::connect(&model, &TaskModel::countsChanged,
                     [&lastTotal, &lastDone](int total, int done) {
                         lastTotal = total;
                         lastDone = done;
                     });

    const QDate today = QDate::currentDate();

    Task writeTask;
    writeTask.title = QStringLiteral("写自检");
    writeTask.priority = Priority::High;
    writeTask.due = today.addDays(-2);
    writeTask.notes = QStringLiteral("顺手验证 dataChanged");

    Task runTask;
    runTask.title = QStringLiteral("跑线程示例");
    runTask.priority = Priority::Normal;
    runTask.due = today.addDays(-1);

    Task noteTask;
    noteTask.title = QStringLiteral("整理笔记");
    noteTask.priority = Priority::Low;

    const int idWrite = model.addTask(writeTask);
    model.addTask(runTask);
    model.addTask(noteTask);

    check(model.rowCount() == 3, QStringLiteral("插入 3 条任务"));
    check(idWrite > 0, QStringLiteral("addTask 返回非 0 id"));
    check(lastTotal == 3 && lastDone == 0, QStringLiteral("countsChanged 报告 3 条 / 0 完成"));

    check(model.index(0, TaskModel::TitleColumn).data().toString() == writeTask.title,
          QStringLiteral("DisplayRole 取回标题"));
    check(model.index(0, TaskModel::PriorityColumn).data(TaskModel::PriorityRole).toInt()
              == static_cast<int>(Priority::High),
          QStringLiteral("PriorityRole 取回数值而不是文本"));

    const int before = dataChangedCount;
    const bool toggled = model.setDone(0, true);
    check(toggled, QStringLiteral("setDone 返回 true"));
    check(dataChangedCount > before, QStringLiteral("改完成状态发出 dataChanged"));
    check(model.index(0, TaskModel::DoneColumn).data(Qt::CheckStateRole).toInt()
              == static_cast<int>(Qt::Checked),
          QStringLiteral("DoneColumn 的 CheckStateRole 变为 Checked"));
    check(lastDone == 1, QStringLiteral("countsChanged 跟进为 1 完成"));

    // ── 2. 代理：只看未完成 + 搜索两个字段 ───────────────────────────────
    TaskFilterProxy proxy;
    proxy.setSourceModel(&model);
    check(proxy.rowCount() == 3, QStringLiteral("未过滤时代理 3 行"));

    proxy.setOnlyOpen(true);
    check(proxy.rowCount() == 2, QStringLiteral("只看未完成 → 2 行"));

    proxy.setQueryText(QStringLiteral("线程"));
    check(proxy.rowCount() == 1, QStringLiteral("再按标题搜索「线程」→ 1 行"));
    check(proxy.mapToSource(proxy.index(0, 0)).row() == 1,
          QStringLiteral("mapToSource 把代理第 0 行映射回源第 1 行"));

    proxy.setQueryText(QString());
    proxy.setOnlyOpen(false);
    check(proxy.rowCount() == 3, QStringLiteral("清空过滤后恢复 3 行"));

    // ── 3. 仓储：SQLite 落库与读回 ───────────────────────────────────────
    TaskRepository repository(QStringLiteral("todo_selfcheck"));
    QString error;
    check(repository.open(dbPath, &error),
          QStringLiteral("打开临时数据库（%1）").arg(error.isEmpty() ? QFileInfo(dbPath).fileName()
                                                                   : error));

    bool persisted = true;
    for (const Task& task : model.tasks())
        persisted = repository.upsert(task, &error) && persisted;
    check(persisted, QStringLiteral("3 条任务写入 SQLite"));

    const QVector<Task> loaded = repository.loadAll(&error);
    check(loaded.size() == 3, QStringLiteral("从 SQLite 读回 3 条"));
    if (loaded.size() == 3) {
        check(loaded.at(0).title == writeTask.title
                  && loaded.at(0).priority == Priority::High
                  && loaded.at(0).due == writeTask.due
                  && loaded.at(0).done,
              QStringLiteral("读回的标题/优先级/日期/完成状态一致"));
        check(!loaded.at(2).due.isValid(), QStringLiteral("空日期往返后仍是无效 QDate"));
    }

    const int removeId = loaded.size() == 3 ? loaded.at(2).id : 0;
    check(repository.remove(removeId, &error), QStringLiteral("按 id 删除一条"));
    check(repository.loadAll(&error).size() == 2, QStringLiteral("删除后剩 2 条"));

    check(repository.replaceAll(model.tasks(), &error), QStringLiteral("整批替换成功"));
    check(repository.loadAll(&error).size() == 3, QStringLiteral("整批替换后恢复 3 条"));

    check(repository.removeCompleted(&error), QStringLiteral("清除已完成"));
    check(repository.loadAll(&error).size() == 2, QStringLiteral("清除已完成后剩 2 条"));

    // ── 4. JSON：导出、导入、坏数据 ──────────────────────────────────────
    check(JsonCodec::save(jsonPath, model.tasks(), &error),
          QStringLiteral("导出 JSON（QSaveFile 原子写）"));

    QVector<Task> imported;
    check(JsonCodec::load(jsonPath, &imported, &error), QStringLiteral("导入刚导出的 JSON"));
    check(imported.size() == 3, QStringLiteral("导入条数一致"));
    if (imported.size() == 3) {
        check(imported.at(0).title == writeTask.title
                  && imported.at(0).priority == Priority::High
                  && imported.at(0).done,
              QStringLiteral("导入后的字段一致"));
    }

    QVector<Task> rejected;
    check(!JsonCodec::fromJson(QByteArrayLiteral("{ not json"), &rejected, &error),
          QStringLiteral("坏 JSON 被拒绝"));
    check(!JsonCodec::fromJson(QByteArrayLiteral("{\"hello\": 1}"), &rejected, &error),
          QStringLiteral("缺 tasks 数组被拒绝"));

    // ── 5. 统计：同步函数 + 后台线程 ─────────────────────────────────────
    const TaskStats stats = computeStats(model.tasks(), today);
    check(stats.total == 3 && stats.done == 1 && stats.open == 2,
          QStringLiteral("computeStats：3 条 / 1 完成 / 2 未完成"));
    check(stats.overdue == 1, QStringLiteral("computeStats：逾期 1 条（已完成的不算逾期）"));
    check(stats.low == 1 && stats.normal == 1 && stats.high == 1,
          QStringLiteral("computeStats：低/普通/高 各 1 条"));

    StatsService service;
    TaskStats asyncStats;
    qint64 elapsedMs = -1;
    QEventLoop loop;

    QObject::connect(&service, &StatsService::statsReady, &loop,
                     [&asyncStats, &elapsedMs, &loop](const TaskStats& value, qint64 ms) {
                         asyncStats = value;
                         elapsedMs = ms;
                         loop.quit();
                     });
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);   // 任何等待都必须有超时
    service.request(model.tasks());
    loop.exec();

    check(elapsedMs >= 0, QStringLiteral("后台统计在 3 秒内返回（否则是线程/事件循环问题）"));
    check(asyncStats.total == 3 && asyncStats.overdue == 1,
          QStringLiteral("后台线程算出的统计与同步计算一致"));

    // ── 6. 收尾 ──────────────────────────────────────────────────────────
    repository.close();
    check(!repository.isOpen(), QStringLiteral("close() 之后连接已释放"));
    repository.close();                     // 幂等：再关一次不该出问题
    check(QFileInfo::exists(dbPath) && QFileInfo::exists(jsonPath),
          QStringLiteral("临时目录里的库与 JSON 都在（说明前面的写入真的发生了）"));

    if (g_failed == 0) {
        qInfo().noquote() << "== 自检通过：全部检查项 PASS ==";
        return 0;
    }
    qCritical().noquote() << QStringLiteral("== 自检失败：%1 项 FAIL ==").arg(g_failed);
    return 1;
}
