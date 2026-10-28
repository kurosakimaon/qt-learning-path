// ============================================================================
//  todo_desktop · 仓储层（第捌阶段 SQL 的落地点）
// ----------------------------------------------------------------------------
//  只做一件事：把 QVector<Task> 与 SQLite 表互相搬运。
//  故意不做成 QObject：它不需要信号，也不需要事件循环；上层决定什么时候读、什么时候写。
//
//  两个必须记住的规矩：
//    1. 连接是「具名」的：addDatabase("QSQLITE", 名字) 之后，全进程靠名字取连接。
//       所以析构时一定要 removeDatabase，否则同名连接第二次 open 会拿到旧的那条。
//    2. removeDatabase 之前，所有 QSqlDatabase / QSqlQuery 副本必须先析构，
//       否则 Qt 会打印 "connection is still in use" 并且真的不删。本文件的
//       每个 close/rollback 都用花括号把局部变量关在作用域里，就是为了这条。
// ============================================================================
#pragma once

#include "core/task.h"

#include <QString>
#include <QVector>

class QSqlDatabase;

class TaskRepository
{
public:
    explicit TaskRepository(QString connectionName = QStringLiteral("todo_desktop_main"));
    ~TaskRepository();

    TaskRepository(const TaskRepository&) = delete;
    TaskRepository& operator=(const TaskRepository&) = delete;

    bool open(const QString& databasePath, QString* error = nullptr);
    void close();
    bool isOpen() const { return m_open; }

    QVector<Task> loadAll(QString* error = nullptr);
    bool upsert(const Task& task, QString* error = nullptr);
    bool remove(int id, QString* error = nullptr);
    bool removeCompleted(QString* error = nullptr);

    // 导入时用：先清空再整批写入，必须在一个事务里，失败要能整体回滚。
    bool replaceAll(const QVector<Task>& tasks, QString* error = nullptr);

private:
    bool ensureSchema(QString* error);
    QSqlDatabase db() const;

    QString m_connectionName;
    bool m_open = false;
};
