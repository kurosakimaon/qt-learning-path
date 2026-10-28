// ============================================================================
// startwidget.h —— 第 2 讲的「自定义窗口类」（放在独立头文件里）
// ----------------------------------------------------------------------------
// 为什么要单独抽一个头文件？—— 讲透「声明/实现分离」：
//   · 头文件（.h）只放「类长什么样」：成员、接口、信号、槽的**声明**；
//   · 实现文件（.cpp）放「每个函数怎么写」：这些接口的**实现**。
//   谁用这个类，只需要 #include 头文件，不需要知道实现细节，也不参与重编。
//   改实现（.cpp）只重编 .cpp；改接口（.h）则所有包含它的都要重编——
//   把接口做得稳定，就是靠「声明/实现分离」换来的。
//
// 本头文件里的类 StartWidget 带 Q_OBJECT，用来演示：
//   · Q_OBJECT 让 AUTOMOC 自动为「头文件里定义的类」生成 moc_startwidget.cpp，
//     并把它作为一个独立的编译单元加进构建——你在本 .cpp 里**不需要**手动
//     #include 任何 .moc 文件。这一点和第 2 讲正文里「在 .cpp 中定义的类」
//     形成对照（那种情况才要 #include "xxx.moc"）。
// ============================================================================

#ifndef STARTWIDGET_H
#define STARTWIDGET_H

#include <QWidget>   // 基类：QtWidgets/qwidget.h

QT_BEGIN_NAMESPACE
// 前向声明（forward declaration）：这里只是告诉编译器「有这三个类」，
// 不必把它们的完整头文件拉进来——这正是「声明/实现分离」带来的编译解耦。
class QLabel;
class QPushButton;
class QVBoxLayout;
QT_END_NAMESPACE

class StartWidget : public QWidget
{
    // ── Q_OBJECT 是什么？──────────────────────────────────────────────
    // Q_OBJECT 不是一个函数调用，而是一个「给 moc（元对象编译器, Meta-Object
    // Compiler）看的标记」。写了它，这个类才拥有 Qt 的元对象能力：
    //   · signals / slots（信号与槽）
    //   · 元对象内省：QObject::property()/setProperty()、metaObject()、qobject_cast
    //   · tr() 翻译
    // 少了 Q_OBJECT，connect 自定义信号会连不上、qobject_cast 会失效——
    // 这是新手最容易踩的「忘了写 Q_OBJECT」坑。
    Q_OBJECT

public:
    explicit StartWidget(QWidget *parent = nullptr);

    // 信号（signal）：**只声明、绝不实现**。它的函数体由 moc 生成。
    // 我们只负责在合适的时候 Q_EMIT 它，谁想响应谁来 connect。
    // 语法核对：QtCore/qtmetamacros.h（signals 是 Q_SIGNALS 的别名，即 public 访问段）
signals:
    void counted(int n);

public slots:
    // 槽（slot）：一个可以被信号连接过去的普通成员函数，需要正常实现。
    // 语法核对：qtmetamacros.h 第 48 行 Q_SLOTS（slots 是其别名）
    void increase();

private:
    QLabel      *label;    // 显示当前计数
    QPushButton *button;   // 点一下 +1
    int          count = 0;
};

#endif // STARTWIDGET_H
