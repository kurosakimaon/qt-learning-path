// ============================================================================
//  todo_desktop · 优先级委托（第陆阶段第 4 讲的落地点）
// ----------------------------------------------------------------------------
//  只做两件事：把「优先级」这一格画成一枚彩色胶囊；告诉视图这一格要占多高。
//
//  最容易踩的两个点：
//    1. paint() 里必须先让样式把背景/选中态画完，再画自己的东西；
//       如果你一上来就 fillRect，选中效果就没了。
//    2. initStyleOption() 会顺手把 text 填好；我们既然自己排版，
//       就要把 opt.text 清空，否则默认绘制会再叠一层文字。
// ============================================================================
#pragma once

#include <QStyledItemDelegate>

class PriorityDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit PriorityDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
};
