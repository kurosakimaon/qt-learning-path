// ============================================================================
// 第 4 讲 · 最小交互闭环
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   把「用户操作 → 信号(signal) → 槽/响应(slots/handler) → 界面更新」这条
//   事件驱动（event-driven）的主线，用最小的一套控件（QPushButton + QLineEdit
//   + QLabel）跑通；并**各演示一次**三种把信号接出去 connect 的写法：
//     (A) 成员函数指针 → 成员函数指针（A 类接 B 类的现成成员）
//     (B) 成员函数指针 → lambda（无 context）
//     (C) 成员函数指针 → 带 context object 的 lambda
//   顺带讲清 Q_EMIT 与 emit 的关系，以及「事件驱动 ≠ 轮询」。
//
// 对应构建目标名：start_04_first_interaction
//   add_executable(start_04_first_interaction 04_first_interaction.cpp)
//   target_link_libraries(start_04_first_interaction PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)
//
// 运行前须知：
//   · 依赖 Qt6::Widgets。本示例**不新定义 Q_OBJECT 类**（连接的全是 Qt 自带
//     信号/成员），所以不需要额外的 .moc，也再次说明：只有你自己声明信号/槽时
//     才牵扯 moc。
//   · 本机当前无法编译链接 Qt，本示例**未在本机实测编译**；API 均对本机头文件核对过。
//   · 无人值守：QTimer 驱动，替「用户」按下按钮/改文本，最后自动 quit()。
// ============================================================================

#include <QApplication>
#include <QWidget>
#include <QPushButton>   // QtWidgets/qpushbutton.h（clicked 来自 QAbstractButton）
#include <QLineEdit>     // QtWidgets/qlineedit.h
#include <QLabel>        // QtWidgets/qlabel.h
#include <QVBoxLayout>   // QtWidgets/qboxlayout.h
#include <QTimer>
#include <QCoreApplication>
#include <QDebug>
#include <QString>

// 一个在槽里被调用的普通函数（不是成员，演示「响应逻辑」本体）。
static void refreshSummary(QLabel *label, const QLineEdit *edit, const QString &who)
{
    // 核对签名：QLineEdit::text()（qlineedit.h 第 62 行 QString text() const）
    label->setText(QStringLiteral("来自 %1 的响应，输入框现在是：%2").arg(who).arg(edit->text()));
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle(QStringLiteral("第 4 讲 · 最小交互闭环"));
    window.resize(420, 200);

    QLabel     *title  = new QLabel(QStringLiteral("（这里显示响应结果）"), &window);
    QLineEdit  *edit   = new QLineEdit(&window);
    QPushButton *btn   = new QPushButton(QStringLiteral("打个招呼"), &window);
    edit->setPlaceholderText(QStringLiteral("输入点东西……"));

    QVBoxLayout *layout = new QVBoxLayout(&window);
    layout->addWidget(title);
    layout->addWidget(edit);
    layout->addWidget(btn);

    // ── (A) 成员函数指针 → 成员函数指针 ────────────────────────────────
    // QLineEdit 一编辑就发 textChanged(const QString&)（qlineedit.h 第 168 行），
    // QLabel::setText(const QString&)（qlabel.h 第 99 行）正好接得住。
    // 于是「你打字，标签实时跟着变」——一行 connect，零手写胶水代码。
    // 这种写法最省：两端都是**已存在的 Qt 成员**，连 lambda 都不用。
    QObject::connect(edit, &QLineEdit::textChanged, title, &QLabel::setText);

    // ── (B) 成员函数指针 → lambda（无 context）────────────────────────
    // clicked()（qabstractbutton.h 第 92 行）→ 一个捕获了变量的 lambda。
    // 核对签名：qobject.h 第 226 行 connect(sender, signalPtr, Functor)。
    // lambda 里做什么完全自由：这里直接调我们的响应函数，并 qDebug 一下。
    QObject::connect(btn, &QPushButton::clicked, [title, edit] {
        qInfo() << "[B] 按钮被按下：lambda（无 context）响应";
        refreshSummary(title, edit, QStringLiteral("lambda-B"));
    });

    // ── (C) 成员函数指针 → 带 context object 的 lambda ────────────────
    // 核对签名：qobject.h 第 228 行 connect(sender, signalPtr, const QObject *context, Functor, type)。
    // 中间多出来的 title 就是「context object」。它的意义是**生命周期**：
    // 当 title 被销毁时，这条连接会被 Qt **自动断开**，lambda 里再碰 title 也不
    // 会踩到野指针。凡 lambda 捕获了某个对象、又担心它先被 delete，就该给它一个
    // context——这是无 context 版(B)所不具备的保护。
    QObject::connect(btn, &QPushButton::clicked, title, [title, edit] {
        qInfo() << "[C] 按钮被按下：带 context 的 lambda（title 析构则此连接自动断开）";
        // 演示 Q_EMIT 之外还有 Qt 自带的「被动发信号」：setText 若内容变化，
        // 内部会自动 Q_EMIT 过 textChanged —— 但我们不在此重复发。
        refreshSummary(title, edit, QStringLiteral("lambda-C"));
    });

    // ── 事件驱动 ≠ 轮询：这是本讲的思想核心 ──────────────────────────
    // 【轮询写法（错的直觉）】你会想写 while(true){ if(按钮被点了) 处理; }，
    //   还要自己 sleep 一下别把 CPU 烧干——这既浪费又卡，界面还会假死。
    // 【Qt 的写法（事件驱动）】你什么都不用「问」。控件在你不察觉时，遇到动作就
    //   「喊」一声（发出信号），事件循环把这一声派给你连好的响应函数。没动作时，
    //   线程阻塞在事件循环里，CPU 占用接近 0。所以正确心智模型是：
    //   「**别去查有没有事，把有事时该干嘛登记好，然后交给事件循环。**」
    //   也正因为这样：绝不能在某个响应函数里干长时间的活（见第六讲常见坑），
    //   否则事件循环被堵，所有登记都响应不了，界面立刻「未响应」。

    // ── 自动演示：替「用户」操作，无人值守 ────────────────────────────
    // 400ms：改文本——触发 textChanged，(A) 那条连接会把标题实时改掉。
    QTimer::singleShot(400, [edit] { edit->setText(QStringLiteral("你好，Qt")); });

    // 800ms：模拟点按钮——(B)(C) 两条连接都会被依次调用。
    // 核对签名：QAbstractButton 有个「编程式按下」的动作用 click()。
    QTimer::singleShot(800, [btn] { btn->click(); });

    // 1300ms：收尾退出。
    QTimer::singleShot(1300, [] {
        qInfo() << "交互闭环演示结束，退出";
        QCoreApplication::quit();
    });

    return app.exec();
}

// ============================================================================
// 附：Q_EMIT 和 emit 到底是什么关系？（本讲纯文字讲透，示例里没有自定义信号）
// ============================================================================
/*
 * 核对依据：本机 QtCore/qtmetamacros.h 第 51、53 行
 *     #define Q_EMIT          ← 空替换
 *     #define emit            ← 也是空替换
 * 也就是说：`Q_EMIT` 和 `emit` **都不是关键字，都是被预处理器擦掉的空宏**。
 * 它俩在编译器眼里一模一样，作用只是给人看的「这里在发一个信号」的记号。
 *   · 老代码/很多教程写 `emit foo();`
 *   · Qt **自己的源码**统一写 `Q_EMIT foo();`
 * 为什么 Qt 内部偏爱 Q_EMIT？因为 `emit` 这四个字母太容易和别的东西撞名了——
 * 有的第三方头文件、甚至某些编译器扩展会自带一个叫 emit 的宏/关键字，撞上就
 * 天下大乱；而带 Q_ 前缀的 Q_EMIT 几乎不可能冲突。所以经验法则：
 *   在自己项目里两种都能用，但要跟 Qt 源码风格、或要最大化兼容，就选 Q_EMIT。
 * 注意：只有在**带 Q_OBJECT 的类内部**才能 Q_EMIT 自己的信号（本示例没自定义
 * 信号，所以看不到 Q_EMIT；你在第 2 讲的 Pinger/StartWidget 里已经见过它了）。
 *
 * 【Qt5 → Qt6 差异提醒（资深读者关注）】老式的「字符串宏语法」connect，例如
 *     connect(btn, SIGNAL(clicked()), title, SLOT(setText(QString)));
 * 在 Qt6 里**仍然能编译**，但官方和新代码一律推荐上面这三种函数指针/lambda 形式，
 * 因为它们：编译期就能检查签名（字符串版要到运行时才报错）、支持 lambda、
 * 能被 IDE 重构/跳转。教学示例里我一次都没用字符串版，就是这个原因。
 */

// ——— 老师傅的话 ———
/*
 * 徒弟，这一讲最容易被误解的一件事：**带 context 的 lambda，那个 context 不是
 * 「用谁的数据」，而是「随谁的生老病死自动断开连接」。** 新手会把 (C) 里多出来的
 * title 参数误以为是「告诉 lambda 用哪个 title」，其实捕获列表 [title, edit] 才是
 * 决定 lambda 用谁的指针；中间那个 title 只干一件事——一旦这个 title 对象被销毁，
 * Qt 就悄悄把这条 connect 拆掉，lambda 不会再被调用，从而不会去碰已经没了的 title。
 * 记住这个区分，你就理解了 Qt 里一大半「对象析构后信号乱飞导致崩溃」的 bug 该怎么防：
 * 给 lambda 一个 context（通常是它所捕获、且可能先死的那个对象）。没有 context 的
 * (B) 版在对象寿命都覆盖到程序结束时无所谓，但真实项目里控件是会被动态销毁的。
 */

// ——— 小练习 ———
/*
 *   1. 给 edit 再接一条 connect：用 (C) 带 context 的写法把
 *      QLineEdit::returnPressed（按回车，qlineedit.h 第 171 行）接到一个 lambda，
 *      让它把当前文本 qInfo 打印出来（自动演示里可以改用 edit->setText 触发不了它，
 *      你可以手动跑一次按回车试试，或改成点按钮时手动调用相关逻辑）。
 *   2. 把 (B) 那条无 context 连接，故意改成 (C) 带 context=title，然后在自动
 *      演示里加一个 QTimer 提前 title->deleteLater()，比较点按钮时 (B)/(C) 谁还响、
 *      谁被自动断开——亲手验证 context 的「保命」作用。
 *   3. 加第二个 QPushButton，用 (A) 成员函数指针的写法，把它 clicked() 接到
 *      QLineEdit::clear（qlineedit.h 有 clear()），体会「两个现成 Qt 成员对接、
 *      一行代码、零 lambda」的干净。
 */
// ============================================================================
