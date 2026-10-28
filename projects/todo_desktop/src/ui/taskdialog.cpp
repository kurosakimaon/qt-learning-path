#include "ui/taskdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

TaskDialog::TaskDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("任务"));
    setModal(true);
    resize(440, 380);

    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setPlaceholderText(QStringLiteral("例如：把第柒阶段的线程归属示例跑一遍"));

    m_priorityBox = new QComboBox(this);
    m_priorityBox->addItem(priorityName(Priority::Low), static_cast<int>(Priority::Low));
    m_priorityBox->addItem(priorityName(Priority::Normal), static_cast<int>(Priority::Normal));
    m_priorityBox->addItem(priorityName(Priority::High), static_cast<int>(Priority::High));
    m_priorityBox->setCurrentIndex(1);

    m_dueCheck = new QCheckBox(QStringLiteral("设置截止日期"), this);
    m_dueEdit = new QDateEdit(QDate::currentDate().addDays(7), this);
    m_dueEdit->setCalendarPopup(true);
    m_dueEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_dueEdit->setEnabled(false);
    connect(m_dueCheck, &QCheckBox::toggled, this, &TaskDialog::updateDueState);

    m_notesEdit = new QPlainTextEdit(this);
    m_notesEdit->setPlaceholderText(QStringLiteral("备注（可选）"));
    m_notesEdit->setTabChangesFocus(true);

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("标题"), m_titleEdit);
    form->addRow(QStringLiteral("优先级"), m_priorityBox);
    form->addRow(QString(), m_dueCheck);
    form->addRow(QStringLiteral("截止日期"), m_dueEdit);
    form->addRow(QStringLiteral("备注"), m_notesEdit);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));

    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (m_titleEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("还差一步"),
                                 QStringLiteral("标题不能为空。"));
            m_titleEdit->setFocus();
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void TaskDialog::setTask(const Task& task)
{
    m_id = task.id;
    m_done = task.done;

    m_titleEdit->setText(task.title);

    const int index = m_priorityBox->findData(static_cast<int>(task.priority));
    if (index >= 0)
        m_priorityBox->setCurrentIndex(index);

    m_dueCheck->setChecked(task.due.isValid());
    if (task.due.isValid())
        m_dueEdit->setDate(task.due);

    m_notesEdit->setPlainText(task.notes);
    setWindowTitle(task.id > 0 ? QStringLiteral("编辑任务") : QStringLiteral("新增任务"));
}

Task TaskDialog::task() const
{
    Task task;
    task.id = m_id;
    task.done = m_done;
    task.title = m_titleEdit->text().trimmed();
    task.priority = priorityFromInt(m_priorityBox->currentData().toInt());
    task.due = m_dueCheck->isChecked() ? m_dueEdit->date() : QDate();
    task.notes = m_notesEdit->toPlainText().trimmed();
    return task;
}

void TaskDialog::updateDueState(bool checked)
{
    m_dueEdit->setEnabled(checked);
}
