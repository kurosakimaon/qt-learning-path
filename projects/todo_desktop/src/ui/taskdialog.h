// ============================================================================
//  todo_desktop · 新增/编辑任务对话框
// ----------------------------------------------------------------------------
//  为什么用一个对话框而不是直接在表格里编辑所有列？
//    「标题」适合就地改，「优先级/截止日期/备注」放对话框里更省地方，
//    也顺手演示第贰阶段的 QFormLayout / QDialogButtonBox 的标准用法。
//
//  这个类只负责「收集用户输入」，不碰模型、不碰数据库：
//    task() 返回一个可能 id = 0 的 Task，交给 MainWindow 决定怎么入库。
// ============================================================================
#pragma once

#include "core/task.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLineEdit;
class QPlainTextEdit;

class TaskDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TaskDialog(QWidget* parent = nullptr);

    // 编辑已有任务：填好控件、记住 id 与完成状态（这两项不归对话框改）。
    void setTask(const Task& task);

    // 取结果：id 与 done 沿用 setTask 时的值。
    Task task() const;

private:
    void updateDueState(bool checked);

    int m_id = 0;
    bool m_done = false;

    QLineEdit* m_titleEdit = nullptr;
    QComboBox* m_priorityBox = nullptr;
    QCheckBox* m_dueCheck = nullptr;
    QDateEdit* m_dueEdit = nullptr;
    QPlainTextEdit* m_notesEdit = nullptr;
};
