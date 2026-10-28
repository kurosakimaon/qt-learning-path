// ============================================================================
// greeting.h · 库对外的脸
// ----------------------------------------------------------------------------
// 约定：
//   · 只暴露「下游该知道的东西」：一个构造函数、一个 const 方法、一个信号。
//   · 不在头文件里 include 任何 Widgets 头——这一条守住了，这个库才能在
//     没有界面的进程里（测试、命令行工具、服务）被复用。
//   · Q_OBJECT 会让 AUTOMOC 为这个头生成 moc_greeting.cpp；因为本头以
//     「库的源文件」身份列在 add_library() 里，构建系统找得到它。
// ============================================================================

#pragma once

#include <QObject>
#include <QString>

class Greeter : public QObject
{
    Q_OBJECT

public:
    // explicit：防止 QString 隐式转换成 Greeter。参数按值收，随后 move 进成员。
    explicit Greeter(QString name, QObject *parent = nullptr);

    // const：不改变对象状态，可以被 const Greeter& 调用。
    QString greet() const;

signals:
    // 信号只声明、不实现——实现由 moc 生成（见第肆阶段「信号槽与 moc」）。
    // 参数用 const QString &：对同线程槽没有拷贝开销；
    // 若连接是 Queued，Qt 会把参数**拷贝**进事件，此时引用就成了「按值拷贝」。
    void greeted(const QString &line);

private:
    QString m_name;
};
