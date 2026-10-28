#include "core/taskrepository.h"

#include <QDate>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <utility>

namespace {

void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

const QString kSelectColumns = QStringLiteral("id, title, done, priority, due, notes");

} // namespace

TaskRepository::TaskRepository(QString connectionName)
    : m_connectionName(std::move(connectionName))
{
}

TaskRepository::~TaskRepository()
{
    close();
}

bool TaskRepository::open(const QString& databasePath, QString* error)
{
    close();

    bool opened = false;
    QString message;

    // 花括号很关键：QSqlDatabase 的局部副本必须在 removeDatabase 之前析构。
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                          m_connectionName);
        database.setDatabaseName(databasePath);
        opened = database.open();
        if (!opened)
            message = database.lastError().text();
    }

    if (!opened) {
        QSqlDatabase::removeDatabase(m_connectionName);
        setError(error, QStringLiteral("打开数据库失败：%1（%2）").arg(databasePath, message));
        return false;
    }

    m_open = true;
    if (!ensureSchema(error)) {
        close();
        return false;
    }
    return true;
}

void TaskRepository::close()
{
    if (!QSqlDatabase::contains(m_connectionName)) {
        m_open = false;
        return;
    }

    {
        QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
        if (database.isValid() && database.isOpen())
            database.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
    m_open = false;
}

QSqlDatabase TaskRepository::db() const
{
    // 第二个参数 false：只取连接，不顺手打开它（打开由 open() 负责）。
    return QSqlDatabase::database(m_connectionName, false);
}

bool TaskRepository::ensureSchema(QString* error)
{
    const QString sql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS tasks ("
        "  id        INTEGER PRIMARY KEY,"
        "  title     TEXT    NOT NULL,"
        "  done      INTEGER NOT NULL DEFAULT 0,"
        "  priority  INTEGER NOT NULL DEFAULT 1,"
        "  due       TEXT,"
        "  notes     TEXT    NOT NULL DEFAULT ''"
        ")");

    QSqlQuery query(db());
    if (!query.exec(sql)) {
        setError(error, QStringLiteral("建表失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

QVector<Task> TaskRepository::loadAll(QString* error)
{
    QVector<Task> tasks;

    QSqlQuery query(db());
    const QString sql = QStringLiteral("SELECT %1 FROM tasks ORDER BY id")
                            .arg(kSelectColumns);
    if (!query.exec(sql)) {
        setError(error, QStringLiteral("查询失败：%1").arg(query.lastError().text()));
        return tasks;
    }

    while (query.next()) {
        Task task;
        task.id = query.value(0).toInt();
        task.title = query.value(1).toString();
        task.done = query.value(2).toInt() != 0;
        task.priority = priorityFromInt(query.value(3).toInt());
        const QString due = query.value(4).toString();
        task.due = due.isEmpty() ? QDate() : QDate::fromString(due, Qt::ISODate);
        task.notes = query.value(5).toString();
        tasks.append(task);
    }
    return tasks;
}

bool TaskRepository::upsert(const Task& task, QString* error)
{
    QSqlQuery query(db());
    const QString sql = QStringLiteral(
        "INSERT OR REPLACE INTO tasks (%1) VALUES (?, ?, ?, ?, ?, ?)")
                            .arg(kSelectColumns);
    if (!query.prepare(sql)) {
        setError(error, QStringLiteral("准备写入失败：%1").arg(query.lastError().text()));
        return false;
    }

    query.bindValue(0, task.id);
    query.bindValue(1, task.title);
    query.bindValue(2, task.done ? 1 : 0);
    query.bindValue(3, static_cast<int>(task.priority));
    query.bindValue(4, task.due.isValid() ? QVariant(task.due.toString(Qt::ISODate))
                                          : QVariant());
    query.bindValue(5, task.notes);

    if (!query.exec()) {
        setError(error, QStringLiteral("写入失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool TaskRepository::remove(int id, QString* error)
{
    QSqlQuery query(db());
    if (!query.prepare(QStringLiteral("DELETE FROM tasks WHERE id = ?"))) {
        setError(error, QStringLiteral("准备删除失败：%1").arg(query.lastError().text()));
        return false;
    }
    query.bindValue(0, id);
    if (!query.exec()) {
        setError(error, QStringLiteral("删除失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool TaskRepository::removeCompleted(QString* error)
{
    QSqlQuery query(db());
    if (!query.exec(QStringLiteral("DELETE FROM tasks WHERE done = 1"))) {
        setError(error, QStringLiteral("清除已完成失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool TaskRepository::replaceAll(const QVector<Task>& tasks, QString* error)
{
    QSqlDatabase database = db();
    if (!database.transaction()) {
        setError(error, QStringLiteral("开启事务失败：%1").arg(database.lastError().text()));
        return false;
    }

    {
        QSqlQuery clear(database);
        if (!clear.exec(QStringLiteral("DELETE FROM tasks"))) {
            const QString message = clear.lastError().text();
            database.rollback();
            setError(error, QStringLiteral("清空失败：%1").arg(message));
            return false;
        }
    }

    {
        QSqlQuery insert(database);
        const QString sql = QStringLiteral(
            "INSERT INTO tasks (%1) VALUES (?, ?, ?, ?, ?, ?)")
                                .arg(kSelectColumns);
        if (!insert.prepare(sql)) {
            const QString message = insert.lastError().text();
            database.rollback();
            setError(error, QStringLiteral("准备写入失败：%1").arg(message));
            return false;
        }

        for (const Task& task : tasks) {
            insert.bindValue(0, task.id);
            insert.bindValue(1, task.title);
            insert.bindValue(2, task.done ? 1 : 0);
            insert.bindValue(3, static_cast<int>(task.priority));
            insert.bindValue(4, task.due.isValid() ? QVariant(task.due.toString(Qt::ISODate))
                                                   : QVariant());
            insert.bindValue(5, task.notes);
            if (!insert.exec()) {
                const QString message = insert.lastError().text();
                database.rollback();
                setError(error, QStringLiteral("批量写入失败：%1").arg(message));
                return false;
            }
        }
    }

    if (!database.commit()) {
        const QString message = database.lastError().text();
        database.rollback();
        setError(error, QStringLiteral("提交失败：%1").arg(message));
        return false;
    }
    return true;
}
