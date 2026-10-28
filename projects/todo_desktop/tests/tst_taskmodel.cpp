// ============================================================================
//  todo_desktop · 单元测试（第拾阶段 QtTest 的落地点）
// ----------------------------------------------------------------------------
//  这里测的全是不需要窗口的东西：模型、代理、JSON、统计。
//  这就是「分层」的好处——业务逻辑能不能测，取决于它有没有依赖 QWidget。
//
//  跑法（top-level 构建目录下）：
//      ctest -R tst_taskmodel --output-on-failure
//  或者直接执行 build/bin/tst_taskmodel
// ============================================================================
#include <QtTest>
#include <QTemporaryDir>

#include "core/jsoncodec.h"
#include "core/task.h"
#include "core/taskfilterproxy.h"
#include "core/taskmodel.h"

class TestTaskModel : public QObject
{
    Q_OBJECT

private slots:
    void addTaskAssignsIncreasingIds();
    void setDoneEmitsDataChangedAndCounts();
    void emptyTitleIsRejected();
    void proxyFiltersDone();
    void proxyFiltersTextAndMapsBack();
    void jsonRoundTrip();
    void statsSkipCompletedOverdue();
};

void TestTaskModel::addTaskAssignsIncreasingIds()
{
    TaskModel model;

    Task first;
    first.title = QStringLiteral("第一条");
    Task second;
    second.title = QStringLiteral("第二条");

    const int firstId = model.addTask(first);
    const int secondId = model.addTask(second);

    QVERIFY(firstId > 0);
    QVERIFY(secondId > firstId);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.index(1, TaskModel::TitleColumn).data().toString(), second.title);
}

void TestTaskModel::setDoneEmitsDataChangedAndCounts()
{
    TaskModel model;
    model.addTask(Task{0, QStringLiteral("要做的"), false, Priority::Normal, QDate(), QString()});

    QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy countSpy(&model, &TaskModel::countsChanged);

    QVERIFY(model.setDone(0, true));
    QCOMPARE(dataSpy.count(), 1);
    QCOMPARE(countSpy.count(), 1);
    QCOMPARE(countSpy.at(0).at(0).toInt(), 1);   // total
    QCOMPARE(countSpy.at(0).at(1).toInt(), 1);   // done
    QCOMPARE(model.index(0, TaskModel::DoneColumn).data(Qt::CheckStateRole).toInt(),
             static_cast<int>(Qt::Checked));

    // 同样的值再写一次：没有变化就不该发信号（视图能省一次重绘）
    QVERIFY(!model.setDone(0, true));
    QCOMPARE(dataSpy.count(), 1);
}

void TestTaskModel::emptyTitleIsRejected()
{
    TaskModel model;
    model.addTask(Task{0, QStringLiteral("原来的标题"), false, Priority::Normal, QDate(), QString()});

    const QModelIndex index = model.index(0, TaskModel::TitleColumn);
    QVERIFY(!model.setData(index, QStringLiteral("   "), Qt::EditRole));
    QCOMPARE(model.index(0, TaskModel::TitleColumn).data().toString(),
             QStringLiteral("原来的标题"));
}

void TestTaskModel::proxyFiltersDone()
{
    TaskModel model;
    model.addTask(Task{0, QStringLiteral("甲"), false, Priority::Normal, QDate(), QString()});
    model.addTask(Task{0, QStringLiteral("乙"), false, Priority::Normal, QDate(), QString()});
    model.setDone(1, true);

    TaskFilterProxy proxy;
    proxy.setSourceModel(&model);

    QCOMPARE(proxy.rowCount(), 2);
    proxy.setOnlyOpen(true);
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, TaskModel::TitleColumn).data().toString(), QStringLiteral("甲"));
}

void TestTaskModel::proxyFiltersTextAndMapsBack()
{
    TaskModel model;
    model.addTask(Task{0, QStringLiteral("写测试"), false, Priority::Normal, QDate(),
                       QStringLiteral("QtTest")});
    model.addTask(Task{0, QStringLiteral("跑线程示例"), false, Priority::Normal, QDate(),
                       QStringLiteral("moveToThread")});

    TaskFilterProxy proxy;
    proxy.setSourceModel(&model);

    // 搜标题
    proxy.setQueryText(QStringLiteral("线程"));
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.mapToSource(proxy.index(0, 0)).row(), 1);

    // 搜备注：这就是「重写 filterAcceptsRow」比 setFilterFixedString 多出来的能力
    proxy.setQueryText(QStringLiteral("QtTest"));
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.mapToSource(proxy.index(0, 0)).row(), 0);

    proxy.setQueryText(QString());
    QCOMPARE(proxy.rowCount(), 2);
}

void TestTaskModel::jsonRoundTrip()
{
    QVector<Task> tasks;

    Task first;
    first.id = 7;
    first.title = QStringLiteral("带日期");
    first.done = true;
    first.priority = Priority::High;
    first.due = QDate(2026, 11, 3);
    first.notes = QStringLiteral("备注里有 \"引号\" 与换行\n第二行");
    tasks.append(first);

    Task second;
    second.id = 8;
    second.title = QStringLiteral("无日期");
    tasks.append(second);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("tasks.json"));

    QString error;
    QVERIFY2(JsonCodec::save(path, tasks, &error), qPrintable(error));

    QVector<Task> loaded;
    QVERIFY2(JsonCodec::load(path, &loaded, &error), qPrintable(error));

    QCOMPARE(loaded.size(), 2);
    QCOMPARE(loaded.at(0).id, 7);
    QCOMPARE(loaded.at(0).title, first.title);
    QCOMPARE(loaded.at(0).done, true);
    QCOMPARE(static_cast<int>(loaded.at(0).priority), static_cast<int>(Priority::High));
    QCOMPARE(loaded.at(0).due, QDate(2026, 11, 3));
    QCOMPARE(loaded.at(0).notes, first.notes);
    QVERIFY(!loaded.at(1).due.isValid());

    QVector<Task> rejected;
    QVERIFY(!JsonCodec::fromJson(QByteArrayLiteral("[]"), &rejected, &error));
    QVERIFY(!JsonCodec::fromJson(QByteArrayLiteral("{\"tasks\": 3}"), &rejected, &error));
}

void TestTaskModel::statsSkipCompletedOverdue()
{
    const QDate today(2026, 10, 28);

    Task overdueOpen;
    overdueOpen.title = QStringLiteral("逾期未完成");
    overdueOpen.due = today.addDays(-1);

    Task overdueDone;
    overdueDone.title = QStringLiteral("逾期但已完成");
    overdueDone.done = true;
    overdueDone.due = today.addDays(-5);

    Task noDue;
    noDue.title = QStringLiteral("没有期限");
    noDue.priority = Priority::High;

    const TaskStats stats = computeStats({overdueOpen, overdueDone, noDue}, today);

    QCOMPARE(stats.total, 3);
    QCOMPARE(stats.done, 1);
    QCOMPARE(stats.open, 2);
    QCOMPARE(stats.overdue, 1);         // 已完成的不算逾期
    QCOMPARE(stats.high, 1);
}

#include "tst_taskmodel.moc"
QTEST_GUILESS_MAIN(TestTaskModel)
