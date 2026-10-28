// ============================================================================
// greeting.cpp · 库的实现
// ----------------------------------------------------------------------------
// 这个文件**不需要** #include "moc_greeting.cpp"：Greeter 的类定义在头文件里，
// AUTOMOC 会为那个头单独生成 moc_greeting.cpp 并编进目标。
// （只有把 Q_OBJECT 类写在 .cpp 里时，才需要在文件末尾 include "<本文件>.moc"。）
// ============================================================================

#include "greeting/greeting.h"

#include <utility>   // std::move

Greeter::Greeter(QString name, QObject *parent)
    : QObject(parent)
    , m_name(std::move(name))
{
}

QString Greeter::greet() const
{
    // arg() 会处理 %1 占位符；对用户可见的文本将来要过 tr()（第 5 个工程演示）。
    return QStringLiteral("你好，%1。这句话来自一个不碰界面的库。").arg(m_name);
}
