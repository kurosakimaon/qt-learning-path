// ============================================================================
//  todo_desktop · 领域模型
// ----------------------------------------------------------------------------
//  这一层不带任何界面依赖：只描述「一条任务是什么」与「怎么从一组任务算出统计」。
//  把它单独放一个头文件，是为了让单元测试能在不建窗口的情况下测到它
//  （第拾阶段「分层架构」那条规矩：业务逻辑不依赖 QWidget）。
//
//  本文件不声明 QObject，所以没有 Q_OBJECT、也不需要 moc。
// ============================================================================
#pragma once

#include <QDate>
#include <QString>
#include <QVector>

// 优先级用枚举而不是字符串：数据库存 int、界面显示中文、排序按数值，三者各自独立。
// 注意这里是 enum class，不能隐式转 int —— 落库与读库都得走 priorityFromInt 这一对函数。
enum class Priority {
    Low = 0,
    Normal = 1,
    High = 2
};

struct Task {
    int id = 0;              // 0 表示「还没入库的新任务」，由模型/仓储分配
    QString title;           // 必填，非空（界面层负责校验）
    bool done = false;
    Priority priority = Priority::Normal;
    QDate due;               // 无效日期 = 没有截止日期
    QString notes;           // 备注，可空
};

// 后台线程算出来的统计结果。故意做成 POD：可拷贝、无虚函数，
// 这样丢进 QtConcurrent 的 lambda 里按值捕获最省事。
struct TaskStats {
    int total = 0;
    int done = 0;
    int open = 0;
    int overdue = 0;         // 未完成且已过期
    int low = 0;
    int normal = 0;
    int high = 0;
};

QString priorityName(Priority priority);
Priority priorityFromInt(int value);

// today 带默认值是为了测试可注入固定日期；生产代码调用时不传即可。
bool isOverdue(const Task& task, QDate today = QDate::currentDate());
TaskStats computeStats(const QVector<Task>& tasks, QDate today = QDate::currentDate());
