// ============================================================================
// startwidget.cpp —— StartWidget 的实现（对应 startwidget.h）
// ----------------------------------------------------------------------------
// 这里体现「实现文件」的角色：把接口一条条写出来。
// 注意本 .cpp **没有**任何 #include "xxx.moc"——
// 因为 StartWidget 定义在头文件里，AUTOMOC 会自动扫描 startwidget.h，
// 生成 moc_startwidget.cpp 并把它编进目标，无需你操心。
// ============================================================================

#include "startwidget.h"

#include <QLabel>        // QtWidgets/qlabel.h
#include <QPushButton>   // QtWidgets/qpushbutton.h（clicked 来自基类 QAbstractButton）
#include <QVBoxLayout>   // QtWidgets/qboxlayout.h
#include <QString>

StartWidget::StartWidget(QWidget *parent)
    : QWidget(parent)                    // 把 parent 交给基类，Qt 的对象树靠它自动析构
{
    // 三个部件都把 this 当 parent：于是 StartWidget 销毁时自动回收它们，
    // 不用手动 delete——这是 Qt 的父子内存模型。
    label  = new QLabel(QStringLiteral("计数：0"), this);
    button = new QPushButton(QStringLiteral("点我 +1"), this);

    // QVBoxLayout(this)：构造时直接把这个布局设为窗口的布局（qboxlayout.h 第 96 行）。
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(label);            // qboxlayout.h 第 39 行 addWidget(QWidget*, ...)
    layout->addWidget(button);

    // 把按钮的 clicked 信号接到本类的 increase 槽。
    // clicked 带一个默认参数 bool（qabstractbutton.h 第 92 行 clicked(bool checked = false)），
    // increase() 不接收参数——信号参数比槽多，多出来的部分 Qt 会自动截断，允许连接。
    // connect 的成员函数指针重载：qobject.h 第 224 行。
    connect(button, &QPushButton::clicked, this, &StartWidget::increase);
}

void StartWidget::increase()
{
    ++count;
    // QString::arg(int)：qstring.h 第 337 行，把 %1 替换为整数。
    label->setText(QStringLiteral("计数：%1").arg(count));

    // Q_EMIT 只是一个「可读性宏」——核对：qtmetamacros.h 第 51 行 `#define Q_EMIT`
    // （空替换）。Qt 自己的源码里一律写 Q_EMIT 而不是 emit，是为了让「这里在发信号」
    // 一眼可见，也避开与某些第三方库宏 `emit` 的冲突。两种写法等价。
    Q_EMIT counted(count);
}
