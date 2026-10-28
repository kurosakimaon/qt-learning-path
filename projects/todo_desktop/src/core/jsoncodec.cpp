#include "core/jsoncodec.h"

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>

namespace {

const QString kFormat = QStringLiteral("todo_desktop.tasks");
constexpr int kVersion = 1;

void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

} // namespace

QByteArray JsonCodec::toJson(const QVector<Task>& tasks)
{
    QJsonArray array;
    for (const Task& task : tasks) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), task.id);
        object.insert(QStringLiteral("title"), task.title);
        object.insert(QStringLiteral("done"), task.done);
        object.insert(QStringLiteral("priority"), static_cast<int>(task.priority));
        object.insert(QStringLiteral("due"),
                      task.due.isValid() ? task.due.toString(Qt::ISODate) : QString());
        object.insert(QStringLiteral("notes"), task.notes);
        array.append(object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), kFormat);
    root.insert(QStringLiteral("version"), kVersion);
    root.insert(QStringLiteral("exportedAt"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("tasks"), array);

    // Indented 而不是 Compact：这是给人看、给人改的文件，不是网络报文。
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool JsonCodec::fromJson(const QByteArray& json, QVector<Task>* tasks, QString* error)
{
    if (!tasks) {
        setError(error, QStringLiteral("内部错误：输出参数为空"));
        return false;
    }
    tasks->clear();

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        setError(error, QStringLiteral("JSON 解析失败（偏移 %1）：%2")
                            .arg(parseError.offset)
                            .arg(parseError.errorString()));
        return false;
    }
    if (!document.isObject()) {
        setError(error, QStringLiteral("JSON 顶层必须是对象 { ... }"));
        return false;
    }

    const QJsonObject root = document.object();
    const QJsonValue tasksValue = root.value(QStringLiteral("tasks"));
    if (!tasksValue.isArray()) {
        setError(error, QStringLiteral("缺少 tasks 数组"));
        return false;
    }

    const QJsonArray array = tasksValue.toArray();
    for (const QJsonValue& value : array) {
        if (!value.isObject())
            continue;                       // 数组里混入非对象：跳过，不整体失败

        const QJsonObject object = value.toObject();
        Task task;
        task.id = object.value(QStringLiteral("id")).toInt();
        task.title = object.value(QStringLiteral("title")).toString().trimmed();
        if (task.title.isEmpty())
            task.title = QStringLiteral("未命名任务");
        task.done = object.value(QStringLiteral("done")).toBool();
        task.priority = priorityFromInt(object.value(QStringLiteral("priority")).toInt());

        const QString due = object.value(QStringLiteral("due")).toString();
        task.due = due.isEmpty() ? QDate() : QDate::fromString(due, Qt::ISODate);
        task.notes = object.value(QStringLiteral("notes")).toString();

        tasks->append(task);
    }

    return true;
}

bool JsonCodec::save(const QString& filePath, const QVector<Task>& tasks, QString* error)
{
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, QStringLiteral("无法写入 %1：%2").arg(filePath, file.errorString()));
        return false;
    }

    const QByteArray data = toJson(tasks);
    if (file.write(data) != data.size()) {
        const QString message = file.errorString();
        file.cancelWriting();
        setError(error, QStringLiteral("写入 %1 失败：%2").arg(filePath, message));
        return false;
    }

    if (!file.commit()) {
        setError(error, QStringLiteral("提交 %1 失败：%2").arg(filePath, file.errorString()));
        return false;
    }
    return true;
}

bool JsonCodec::load(const QString& filePath, QVector<Task>* tasks, QString* error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("无法打开 %1：%2").arg(filePath, file.errorString()));
        return false;
    }

    return fromJson(file.readAll(), tasks, error);
}
