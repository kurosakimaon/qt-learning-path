#include "ui/prioritydelegate.h"

#include "core/task.h"
#include "core/taskmodel.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>

PriorityDelegate::PriorityDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void PriorityDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                             const QModelIndex& index) const
{
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);

    const Priority priority = priorityFromInt(index.data(TaskModel::PriorityRole).toInt());
    const QString label = priorityName(priority);

    // 让样式先画完底（含选中高亮、焦点框），并清空 text 避免默认文字再叠一层。
    opt.text.clear();
    const QWidget* widget = opt.widget;
    QStyle* style = widget ? widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

    QColor background;
    switch (priority) {
    case Priority::Low:
        background = QColor(0x94, 0xa3, 0xb8);
        break;
    case Priority::High:
        background = QColor(0xdc, 0x26, 0x26);
        break;
    case Priority::Normal:
        background = QColor(0x16, 0xa3, 0x4a);
        break;
    }

    const QRect pill = opt.rect.adjusted(6, 5, -6, -5);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(background);
    painter->drawRoundedRect(pill, 8, 8);

    QFont font = opt.font;
    font.setBold(true);
    painter->setFont(font);
    painter->setPen(Qt::white);
    painter->drawText(pill, Qt::AlignCenter, label);
    painter->restore();
}

QSize PriorityDelegate::sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    size.setHeight(qMax(size.height(), 30));
    size.setWidth(qMax(size.width(), 76));
    return size;
}
