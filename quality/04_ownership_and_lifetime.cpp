// ============================================================================
// 第 4 讲 · 所有权与生命周期：这一行 delete 到底该谁写
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   Qt 项目里最难查的崩溃，九成不是「算法错了」，而是**对象死在了不该死的时候**：
//     · 一个控件被 delete 了两次（栈上一个，parent 又删一次）；
//     · 一个 lambda 抓着一个已经没了的面板的 this，信号一来就踩空；
//     · deleteLater 之后又手动 delete，或者等不到事件循环就先用了；
//     · 一个 QObject 被「拷贝」了一份（其实 Qt 根本不让你拷，但有人绕过去）。
//   Qt 对这四件事各给了一把工具，本讲把它们逐个量出来，而不是让你背文档：
//
//       谁生谁养             parent/child 树 + children()
//       非 QObject 的独占     std::unique_ptr
//       延迟销毁              deleteLater + DeferredDelete 事件
//       悬空检测              QPointer（裸指针**做不到**）
//       连接的生命周期         connect 的 context 参数
//       身份 vs 值             QObject 禁止拷贝（编译期事实）
//
//   六个阶段，每个阶段都落到一道能红的账上：
//     ① 对象树：删父删子，销毁顺序是「实现细节」不是「契约」
//     ② 栈对象挂 parent：那条路径会 double free，本讲安全地演示「挂上去再摘下来」
//     ③ deleteLater：它只是**投递一个事件**，不冲洗就永远不删
//     ④ QPointer vs 裸指针：同一个对象，整数看着还在，QPointer 已经知道它没了
//     ⑤ 带 context 的连接会随接收者断开，不带 context 的连接**谁死都不管**
//     ⑥ 值语义与对象身份：Q_DISABLE_COPY(QObject) 为什么是必然
//
// 对应构建目标名：quality_04_ownership_and_lifetime
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(quality_04_ownership_and_lifetime 04_ownership_and_lifetime.cpp)
//   target_link_libraries(quality_04_ownership_and_lifetime PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)                    # 本文件有 Q_OBJECT 类，需要 moc
//
// 运行前须知：
//   · **全程同步**，没有 app.exec()。第 ③ 节需要「冲洗延迟删除」，
//     用的是 QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete)，
//     必要时再兜底跑一圈最小事件循环 —— 都不是常驻事件循环，不会挂住。
//   · 可以无头跑：`quality_04_ownership_and_lifetime -platform offscreen`。
//   · 不写磁盘、不联网、不碰用户配置：本讲所有对象都在内存里。
//   · **不演示任何一次真崩溃**。double free、悬空解引用这类事，本讲只**描述**
//     不**执行** —— 一个会崩的讲义没法用来核对别的东西。
//   · 未实测声明：本机当前无法链接 Qt 运行时，本示例**未链接、未运行**，
//     仅通过 moc + g++ -fsyntax-only（-std=c++17 -Wall -Wextra）做编译期核对。
//     文中所有「期望」后面是推理，不是跑出来的数字。
// ============================================================================

#include <QCoreApplication>          // QtCore/qcoreapplication.h：115 行 sendPostedEvents
#include <QEvent>                    // QtCore/qcoreevent.h：105 行 DeferredDelete = 52
#include <QEventLoop>                // 兜底冲洗时的最小事件循环
#include <QTimer>
#include <QObject>                   // QtCore/qobject.h：206 children / 208 setParent / 356 parent
#include <QPointer>                  // QtCore/qpointer.h：19 行 class QPointer / 86 行 isNull
#include <QString>
#include <QStringList>
#include <QMetaObject>               // QMetaObject::Connection
#include <QDebug>

#include <memory>
#include <type_traits>
#include <utility>

// ============================================================================
// 一、核对签名（本机 C:\msys64\ucrt64\include\qt6）
// ============================================================================
// 所有权相关的全部入口，都在 qobject.h 里，一共就这些：
//   第 115 行  virtual ~QObject();                       ← 析构里会 delete 所有子对象
//   第 206 行  inline const QObjectList &children() const { return d_ptr->children; }
//   第 208 行  void setParent(QObject *parent);          ← 挂树/摘树的唯一正规口
//   第 356 行  inline QObject *parent() const { return d_ptr->parent; }
//   第 364 行  void deleteLater();                       ← 只是**投递**一个事件
//   第 352 行  void destroyed(QObject * = nullptr);       ← 信号，对象咽气时发一次
//   第 403 行  Q_DISABLE_COPY(QObject)                   ← 见第 ⑥ 节
//
// Q_DISABLE_COPY 的展开（qtclasshelpermacros.h 第 24~26 行，原文是两行用续行符接起来的）：
//     #define Q_DISABLE_COPY(Class)  Class(const Class &) = delete;  Class &operator=(const Class &) = delete;
//   ← 不是「不推荐拷贝」，是**编译期就不许**。这一条决定了后面所有写法。
//
// deleteLater 的机制（qcoreapplication.h 第 115 行）：
//     static void sendPostedEvents(QObject *receiver = nullptr, int event_type = 0);
//   配合 qcoreevent.h 第 105 行 DeferredDelete = 52。
//   deleteLater() 做的是 postEvent(DeferredDelete)，**当场什么都不删**；
//   真正 delete 发生在事件循环处理到这个事件的时候。
//   ⚠ 这条正是新手最容易误解的地方：「我调了 deleteLater，对象应该没了吧？」
//     没有。它还在，还能发信号，还能被别的东西访问。
//
// connect 的两个重载，差一个参数，差一条命（qobject.h）：
//   第 226 行 connect(sender, signal, Functor functor)
//              ← **没有 context**：连接活到 sender 死为止，跟谁都无关
//   第 228 行 connect(sender, signal, const QObject *context, Functor functor, ...)
//              ← **有 context**：context 一死，连接自动断开
//   第 224 行 connect(sender, signal, receiver, method, ...)
//              ← 成员函数版：receiver 一死，连接自动断开（与上一条同理）
//
// 跨讲链接：
//   · metaobject 第 4 讲：lambda 与生命周期，那里讲「为什么要给 lambda 一个 context」。
//   · metaobject 第 7 讲：QObject 树与 findChild，本讲第 ① 节是它的地基。
//   · threads 第 2 讲：线程亲和性 —— parent 一设，对象的 thread() 就跟着走了。
//   · quality 第 3 讲：组合根里「先造依赖、后造使用者，销毁反着来」就是本讲的规矩。
// ============================================================================

// ============================================================================
// 二、核对台账
// ============================================================================
enum Kind { Hard, Soft, Obs };

struct Ledger {
    int total = 0;
    int failed = 0;
    int stalled = 0;

    int exitCode() const { return (failed + stalled > 0 || total == 0) ? 1 : 0; }
};
static Ledger gLedger;

static void verdict(Kind kind, const QString &label, bool ok, const QString &detail)
{
    QString head;
    switch (kind) {
    case Hard:
        ++gLedger.total;
        if (!ok)
            ++gLedger.failed;
        head = ok ? QStringLiteral("  [OK ] 核对  ") : QStringLiteral("  [!! ] 不符  ");
        break;
    case Soft: head = ok ? QStringLiteral("  [预期中] ") : QStringLiteral("  [这次没命中] "); break;
    case Obs:  head = QStringLiteral("  [实测]  "); break;
    }
    qInfo().noquote() << head + QStringLiteral("%1 = %2").arg(label, detail);
}

static void section(const QString &text)
{
    qInfo().noquote() << QStringLiteral("\n========== %1 ==========").arg(text);
}

static void checkNum(const QString &label, qint64 got, qint64 want)
{
    verdict(Hard, label, got == want, QStringLiteral("%1（期望 %2）").arg(got).arg(want));
}

static void checkBool(const QString &label, bool got, bool want)
{
    verdict(Hard, label, got == want,
            QStringLiteral("实测 %1（期望 %2）")
                .arg(got ? QStringLiteral("true") : QStringLiteral("false"),
                     want ? QStringLiteral("true") : QStringLiteral("false")));
}

// ============================================================================
// 三、一只能记录自己生死的对象
// ----------------------------------------------------------------------------
// 讲所有权最怕「口头保证」。所以先造一个会在析构里记账的类：
// 谁被删了、按什么顺序删的，全落到日志里。
// 注意两个细节：
//   · 它**不拥有** m_log（裸指针，指向 main 里的一个 QStringList）——
//     日志的生命周期由调用方保证，这正是本章要讲的那件事本身；
//   · 析构里不碰任何别的对象，避免「析构顺序」把日记本自己搞坏。
// ============================================================================
static int g_live = 0;                 // 当前活着的 TrackedObject 个数

class TrackedObject : public QObject
{
    Q_OBJECT
public:
    TrackedObject(QString name, QStringList *log, QObject *parent = nullptr)
        : QObject(parent), m_name(std::move(name)), m_log(log)
    {
        ++g_live;
    }

    ~TrackedObject() override
    {
        --g_live;
        if (m_log)
            m_log->append(m_name);     // 只记账，不释放 m_log
    }

    QString name() const { return m_name; }

private:
    QString m_name;
    QStringList *m_log;
};

// ============================================================================
// 四、阶段①：对象树 —— 删父删子
// ----------------------------------------------------------------------------
// 规则一句话：**谁 new 的谁不管，设了 parent 就交给 parent 管。**
// 三个要点：
//   1) children() 是父对象手里的名单，setParent(nullptr) 就是摘名单；
//   2) 删父对象时，析构函数会把名单上的子孙全删掉 —— 你不需要（也不该）再删一遍；
//   3) 兄弟之间的销毁顺序是**实现细节**，Qt 没有承诺过。本节把它打出来当 Obs，
//      并且明确写一句：不许依赖这个顺序（一旦依赖，换个 Qt 版本就可能炸）。
// ============================================================================
static void phase1_tree()
{
    section(QStringLiteral("① 对象树：删父删子，顺序不是契约"));

    QStringList log;
    const int liveBefore = g_live;

    auto *root = new QObject();
    auto *a = new TrackedObject(QStringLiteral("a"), &log, root);
    auto *b = new TrackedObject(QStringLiteral("b"), &log, root);
    auto *a1 = new TrackedObject(QStringLiteral("a1"), &log, a);

    checkNum(QStringLiteral("【1A】root 的 direct children"), root->children().size(), 2);
    checkNum(QStringLiteral("【1B】a 的 direct children"), a->children().size(), 1);
    checkBool(QStringLiteral("【1C】a1 的 parent 就是 a"), a1->parent() == a, true);
    checkBool(QStringLiteral("【1D】root 自己不是任何人的孩子"), root->parent() == nullptr, true);
    checkBool(QStringLiteral("【1E】b 也是 root 的孩子"), b->parent() == root, true);
    checkNum(QStringLiteral("【1F】此刻活着的 TrackedObject"), g_live - liveBefore, 3);

    delete root;                     // ★ 只删这一个；a、b、a1 都被连带删掉

    checkNum(QStringLiteral("【1G】删 root 之后活着的手下"), g_live - liveBefore, 0);
    checkNum(QStringLiteral("【1H】三个对象的析构各记了一笔"), log.size(), 3);
    qInfo().noquote() << QStringLiteral("  实测销毁顺序：") + log.join(QStringLiteral(" → "));
    verdict(Obs, QStringLiteral("【1I】销毁顺序"),
            true, QStringLiteral("这是实现细节，本讲讲义不承诺，你的代码也不许依赖"));

    // 用字符串名字再确认一次：a1 一定死在 a 之后（子先于父，这条是契约）。
    const int posA = log.indexOf(QStringLiteral("a"));
    const int posA1 = log.indexOf(QStringLiteral("a1"));
    checkBool(QStringLiteral("【1J】子对象 a1 比父对象 a 先销毁"),
              posA1 >= 0 && posA >= 0 && posA1 < posA, true);
}

// ============================================================================
// 五、阶段②：栈对象挂 parent —— 那条路会 double free
// ----------------------------------------------------------------------------
// 这是 Qt 新手最常见的致命错，写法还特别像「顺手」：
//
//     void f(QWidget *parent) {
//         QPushButton btn("确定", parent);   // ← 栈上，但 parent 说「我会删它」
//     }                                      // ← 这里 btn 已经销毁
//     // 之后 parent 析构时，又 delete 一次那个地址 → 堆破坏
//
// 本讲**不执行**这个错误写法。改成安全的等价演示：
//   挂上去 → 看见名单从 0 变 1 → 立刻摘下来 → 名单回到 0。
// 只要摘干净，栈对象随作用域正常销毁，就没有第二次 delete。
// 至于「忘了摘」会发生什么：那是未定义行为，不是「可能崩」而是「崩了也别惊讶」。
//
// 正确的非 QObject 或「不设 parent 的 QObject」该怎么管？std::unique_ptr。
// 唯一要守的规矩：**一个对象只许有一个主人**。unique_ptr 和 parent 二选一。
// ============================================================================
static void phase2_stackAndParent()
{
    section(QStringLiteral("② 栈对象挂 parent：挂上去，必须摘下来"));

    QStringList log;
    const int liveBefore = g_live;

    QObject owner;
    {
        TrackedObject stackChild(QStringLiteral("stackChild"), &log);
        checkBool(QStringLiteral("【2A】刚建好，它谁也不属于"),
                  stackChild.parent() == nullptr, true);
        checkNum(QStringLiteral("【2B】owner 名下初始为空"), owner.children().size(), 0);

        stackChild.setParent(&owner);              // ★ 这一行如果留下来，就是 double free
        checkNum(QStringLiteral("【2C】挂上去之后，owner 名下有 1 个"), owner.children().size(), 1);
        checkBool(QStringLiteral("【2D】反向也能问到 parent"), stackChild.parent() == &owner, true);

        stackChild.setParent(nullptr);             // ★ 离开作用域之前必须摘掉
        checkNum(QStringLiteral("【2E】摘下来之后，owner 名下回到 0"), owner.children().size(), 0);
    }                                              // 栈对象在这里正常销毁，只死一次
    checkNum(QStringLiteral("【2F】栈对象只被销毁了一次"), log.count(QStringLiteral("stackChild")), 1);
    checkNum(QStringLiteral("【2G】此刻活着的 TrackedObject"), g_live - liveBefore, 0);

    // ── 正确姿势：unique_ptr 独占，绝不设 parent ──────────────────────────
    {
        auto owned = std::make_unique<TrackedObject>(QStringLiteral("owned"), &log, nullptr);
        checkBool(QStringLiteral("【2H】unique_ptr 管的对象没有 parent"),
                  owned->parent() == nullptr, true);
        checkNum(QStringLiteral("【2I】unique_ptr 管的对象不进任何名单"),
                  owner.children().size(), 0);
    }
    checkNum(QStringLiteral("【2J】离开作用域，unique_ptr 自己删了它"),
             log.count(QStringLiteral("owned")), 1);

    // 一句必须记牢的话：
    //   std::unique_ptr 的销毁是**确定**的（作用域结束就删）；
    //   parent 的销毁是**跟随**的（parent 死它才死）。
    //   两者混用 = 两个主人 = 迟早删两次。
    checkBool(QStringLiteral("【2K】owner 名下始终没留下栈对象"), owner.children().isEmpty(), true);
}

// ============================================================================
// 六、阶段③：deleteLater 只是投递一个事件
// ----------------------------------------------------------------------------
// 这一节要把「延迟」两个字钉死：
//   调完 deleteLater() 立刻检查 → 对象**还活着**，destroyed 一次都还没发；
//   冲洗掉 DeferredDelete 事件之后 → destroyed 恰好发一次，对象没了。
//
// 冲洗方式两种，都用上：
//   1) QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete)
//      —— 直接把排队中的删除事件派发掉，不需要跑事件循环；
//   2) 万一没生效，就兜底跑一圈最小事件循环，再冲一次。
// 第 2 条不是「保险起见」，而是因为「延迟删除事件在什么条件下被处理」
// 属于 .cpp 里的策略：在没有 exec() 的 main 里，它的处理时机跟循环层数有关。
// 讲义不猜策略，只报结果（下面有一条 Obs 会说清楚走的是哪条路）。
// ============================================================================
static int g_victimDestroyed = 0;

class Victim : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~Victim() override { ++g_victimDestroyed; }
};

// 返回 1 = 直接冲洗就够了；2 = 还跑了一圈事件循环；0 = 依然没删掉（异常）
static int flushDeferredDeletes()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (g_victimDestroyed > 0)
        return 1;

    QEventLoop loop;
    QTimer::singleShot(0, &loop, &QEventLoop::quit);
    loop.exec();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return g_victimDestroyed > 0 ? 2 : 0;
}

static void phase3_deleteLater()
{
    section(QStringLiteral("③ deleteLater：投递，不是删除"));

    g_victimDestroyed = 0;
    auto *victim = new Victim();
    QPointer<Victim> guard(victim);
    QObject::connect(victim, &QObject::destroyed, victim, []() { /* 只证明信号会来 */ });

    victim->deleteLater();

    checkNum(QStringLiteral("【3A】deleteLater 之后立刻看：destroyed 次数"),
             g_victimDestroyed, 0);
    checkBool(QStringLiteral("【3B】QPointer 这时还指得到它"), guard.isNull(), false);

    const int route = flushDeferredDeletes();
    verdict(Obs, QStringLiteral("【3C】冲洗路线"), true,
            route == 1 ? QStringLiteral("直接 sendPostedEvents 就够了")
                       : (route == 2 ? QStringLiteral("需要兜底跑一圈事件循环")
                                     : QStringLiteral("两条路都没删掉")));

    checkNum(QStringLiteral("【3D】冲洗之后 destroyed 次数"), g_victimDestroyed, 1);
    checkBool(QStringLiteral("【3E】QPointer 已经知道它没了"), guard.isNull(), true);

    // ★ 为什么不能「deleteLater 完再 delete 一次」：
    //   那等于预定了两次删除，第二次执行的时候对象已经没了。这是本讲最典型的崩溃。
    //   同理，deleteLater 之后**不许**再用裸指针碰它 —— 它现在还活着，但已经「判了刑」，
    //   任何时刻都可能被事件循环收走。
    verdict(Obs, QStringLiteral("【3F】deleteLater 与 delete 二选一"),
            true, QStringLiteral("同时写 = 预定两次删除 = 早晚 double free"));
}

// ============================================================================
// 七、阶段④：QPointer 与「裸指针看不出的悬空」
// ----------------------------------------------------------------------------
// 裸指针在对象死后**不会变成 nullptr**，它只是变成一个指向空地址的值。
// 你拿它判空、拿它调成员函数，编译器一句警告都不会给你。
// QPointer 是 QObject 的专用「观察者」：对象被销毁时，它自己被清空。
// 注意它的定位：QPointer 是**观察**，不是**拥有**。它不延长对象寿命。
// 本节刻意不「读取那块内存」（那是未定义行为），只对比两样能安全比较的东西：
//   · 销毁前记下的地址（整数）
//   · 销毁后 QPointer 会不会报 null
// ============================================================================
static void phase4_qpointer()
{
    section(QStringLiteral("④ QPointer 与裸指针：一个知道，一个不知道"));

    g_victimDestroyed = 0;
    auto *obj = new Victim();
    QPointer<Victim> watch(obj);

    // 只把地址**当作整数**存下来（安全）；绝不保留那个裸指针去跟 nullptr 比。
    const quintptr address = reinterpret_cast<quintptr>(obj);
    checkBool(QStringLiteral("【4A】对象活着时地址非零"), address != 0, true);
    checkBool(QStringLiteral("【4B】QPointer 指得到它"), watch.isNull(), false);

    delete obj;                                  // 直接删，不走 deleteLater

    checkBool(QStringLiteral("【4C】删除后 QPointer 自动变空"), watch.isNull(), true);
    checkNum(QStringLiteral("【4D】destroyed 发了一次"), g_victimDestroyed, 1);
    verdict(Obs, QStringLiteral("【4E】销毁前记下的地址"), true,
            QStringLiteral("0x") + QString::number(address, 16)
                + QStringLiteral(" —— 这个数字现在依然「看着像有效地址」，"
                                 "而 QPointer 已经空了：这就是它唯一的价值"));

    // QPointer 的三条边界，一条一条说清楚：
    //   1) 只对 QObject 有效（它内部靠 QObjectPrivate 的弱引用）；
    //   2) 它**不拥有**对象 —— 作用域结束不会删任何东西；
    //   3) 它不是线程安全的「跨线程观察」工具，别拿它当线程同步用。
    bool ownsNothing = true;
    {
        auto *probe = new Victim();
        QPointer<Victim> w(probe);
        ownsNothing = (g_victimDestroyed == 1);   // 进了新作用域，但没有新对象被删
        delete probe;
        Q_UNUSED(w);
    }
    checkBool(QStringLiteral("【4F】QPointer 自己不会删对象（要有 delete 才死）"),
              ownsNothing, true);
    checkNum(QStringLiteral("【4G】探针对象被显式 delete 后总数"), g_victimDestroyed, 2);
}

// ============================================================================
// 八、阶段⑤：连接的生命周期 —— context 参数决定「谁死会断线」
// ----------------------------------------------------------------------------
// 同一个 sender，同一段 lambda，只差一个参数：
//   带 context：connect(sender, &S::ping, receiver, lambda)
//               receiver 一死，连接自动断开 —— 这是**唯一**推荐写法
//   不带 context：connect(sender, &S::ping, lambda)
//               连接活到 sender 死为止；接收者死不死，它一无所知
// 为什么这件事归「所有权」讲？因为**连接也是一份引用**。
// 一个 lambda 抓了 receiver 的 this，就等于把 receiver 的地址存进了一个
// 你不知道存在哪的注册表里；不给 context，Qt 就不知道要替你解引用。
// 本节的 lambda **只碰一个 main 里的计数器**，所以删掉 receiver 之后发信号
// 不会踩空 —— 这正是我们要演示的「连接还在，但它已经跟你没关系了」。
// ============================================================================
class Pinger : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    void fire() { Q_EMIT ping(); }
Q_SIGNALS:
    void ping();
};

static void phase5_connectionContext()
{
    section(QStringLiteral("⑤ 连接的生命周期：context 一死就断"));

    Pinger pinger;
    int withContext = 0;
    int withoutContext = 0;

    auto *receiver = new QObject();
    const QMetaObject::Connection guarded =
        QObject::connect(&pinger, &Pinger::ping, receiver, [&withContext]() { ++withContext; });

    // 注意这里捕获的是计数器地址，**不是** receiver —— 所以即使 receiver 已经没了，
    // 这条 lambda 被调用也是安全的。副作用是：它证明了「连接并没有随 receiver 断开」。
    int *counter = &withoutContext;
    const QMetaObject::Connection loose =
        QObject::connect(&pinger, &Pinger::ping, [counter]() { ++*counter; });

    pinger.fire();
    checkNum(QStringLiteral("【5A】第一次发射，带 context 的收到"), withContext, 1);
    checkNum(QStringLiteral("【5B】第一次发射，不带 context 的收到"), withoutContext, 1);
    checkBool(QStringLiteral("【5C】两条连接一开始都有效"),
              static_cast<bool>(guarded) && static_cast<bool>(loose), true);

    delete receiver;                              // ★ 只删接收者，sender 还活着

    pinger.fire();
    checkNum(QStringLiteral("【5D】接收者死后，带 context 的那条不再被调用"), withContext, 1);
    checkNum(QStringLiteral("【5E】不带 context 的那条**照样**被调用"), withoutContext, 2);
    checkBool(QStringLiteral("【5F】guarded 连接对象已变空"),
              static_cast<bool>(guarded), false);

    // 结论：不带 context 不是「性能优化」，是「责任放弃」。
    // 它唯一合法的场景是 lambda 不碰任何可能先死的对象（比如只写日志、只改本地计数器）。
    verdict(Obs, QStringLiteral("【5G】写法与后果"), true,
            QStringLiteral("带 context = 自动断线；不带 = 你自己保证不踩空"));
}

// ============================================================================
// 九、阶段⑥：值语义与对象身份
// ----------------------------------------------------------------------------
// 为什么 QObject 不许拷贝？因为它代表的是**身份**，不是**值**：
//   · 它有地址（信号连接、parent 名单、事件队列都按地址认人）；
//   · 它有元对象（可能是通过 Q_INVOKABLE 注册进反射的）；
//   · 它可能代表一个**已经存在的系统资源**（窗口、socket、定时器）。
// 拷一份出来，立刻就有两个「同一个对象」，parent 名单里两个、连接里两个，
// 删一个另一个就悬空。所以 Qt 直接在编译期把拷贝函数 = delete（第 403 行）。
// 对比 QString：它是**值**，拷贝完全正常（而且是便宜的隐式共享）。
// ============================================================================
static void phase6_identity()
{
    section(QStringLiteral("⑥ 身份 vs 值：QObject 为什么不能拷贝"));

    // 这两行是**编译期**的事实，写在这里就是断言，编不过就不用跑了。
    static_assert(!std::is_copy_constructible<QObject>::value,
                  "QObject 必须不可拷贝：Q_DISABLE_COPY(QObject) 在 qobject.h 第 403 行");
    static_assert(!std::is_copy_assignable<QObject>::value,
                  "QObject 必须不可赋值");
    static_assert(std::is_copy_constructible<QString>::value,
                  "QString 是值类型，必须可拷贝");

    // 把编译期的事实搬进台账，让「退出码」也能覆盖到它。
    const bool objCopyable = std::is_copy_constructible<QObject>::value;
    const bool strCopyable = std::is_copy_constructible<QString>::value;
    checkBool(QStringLiteral("【6A】QObject 可拷贝"), objCopyable, false);
    checkBool(QStringLiteral("【6B】QString 可拷贝"), strCopyable, true);

    // 运行期的补充证据：每个 QObject 都有一份自己的元对象指针与连接表。
    // 这不难，但很多写了五年 Qt 的人没意识到「对象的身份靠地址」这件事。
    QObject one;
    QObject two;
    checkBool(QStringLiteral("【6C】两个对象的地址不同（身份不同）"), &one != &two, true);
    checkBool(QStringLiteral("【6D】同类对象共享同一个元对象"),
              one.metaObject() == two.metaObject(), true);

    // 顺带一条最实用的推论：如果你需要一个「可拷贝的业务对象」，
    // 那就别让它继承 QObject —— 值类型（如 TodoItem）随便拷，Qt 容器也乐得帮你省事。
    verdict(Obs, QStringLiteral("【6E】选型口诀"), true,
            QStringLiteral("要信号/要 parent/要元对象 → QObject；"
                           "要拷贝/要值语义/要塞进容器 → 普通结构体"));
}

// ============================================================================
// 十、main：合账
// ============================================================================
static void reconcile()
{
    section(QStringLiteral("合账"));
    const int broken = gLedger.failed + gLedger.stalled;
    qInfo().noquote() << QStringLiteral("  硬账 %1 项：对上 %2，不符 %3；整节作废 %4")
                             .arg(gLedger.total)
                             .arg(gLedger.total - broken)
                             .arg(gLedger.failed)
                             .arg(gLedger.stalled);
    qInfo().noquote() << QStringLiteral("  退出码 = %1（%2）")
                             .arg(gLedger.exitCode())
                             .arg(gLedger.exitCode() == 0
                                      ? QStringLiteral("没有硬账不符、没有整节作废")
                                      : QStringLiteral("有硬账不符或整节作废"));
    if (gLedger.exitCode() != 0)
        qWarning().noquote() << QStringLiteral("  ★ 请按上面的 [!! ] 行往下查。");
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("quality_04_ownership_and_lifetime"));

    qInfo() << "================================================================";
    qInfo() << "quality 第 4 讲 · 所有权与生命周期（Qt =" << QT_VERSION_STR << "）";
    qInfo() << "================================================================";

    phase1_tree();
    phase2_stackAndParent();
    phase3_deleteLater();
    phase4_qpointer();
    phase5_connectionContext();
    phase6_identity();

    // 一条收尾硬账：全程没有漏网之鱼。
    checkNum(QStringLiteral("【总账】结束时还活着的 TrackedObject"), g_live, 0);

    reconcile();

    const int rc = gLedger.exitCode();
    qInfo().noquote() << QStringLiteral("进程退出码 = %1").arg(rc);
    return rc;
}

// ============================================================================
// 十一、速查：所有权六问，写代码前先答一遍
// ============================================================================
/*
 * 一、这个对象有 parent 吗？有 → 你不要 delete 它。
 *   有 parent 的对象，生死归 parent；你唯一该做的是**别抢着删**。
 *   例外：你想把它从树里摘出来自己管 —— 那就 setParent(nullptr)，然后再 delete。
 *
 * 二、它是栈上的还是堆上的？栈上 → **绝对不许设 parent**。
 *   栈对象的销毁时机由作用域决定，parent 的销毁时机由树决定，两者不同步就是 double free。
 *   真需要临时挂一下（比如为了让布局管它），用完立刻 setParent(nullptr)。
 *
 * 三、是不是 QObject？不是 → std::unique_ptr（独占）或 std::shared_ptr（共享）。
 *   QObject 也能用 unique_ptr，前提是**不设 parent**（一个主人）。
 *
 * 四、要延迟删除吗？只有两种情况必须用 deleteLater：
 *   ① 在它自己的槽里 / 信号回调里删它自己（直接 delete 会在返回后访问已释放内存）；
 *   ② 删除跨线程的对象（由目标线程的事件循环执行删除）。
 *   其它情况直接 delete 更清楚。**两者永远不要同时写。**
 *
 * 五、这个连接要不要 context？要，除非 lambda 不碰任何可能先死的对象。
 *   判据：lambda 里出现了一个指针或引用 → 它就应该有个 context。
 *
 * 六、这个类是身份还是值？要信号/元对象/parent → QObject（禁拷贝）；
 *   只是数据 → 普通结构体（可拷贝、可进容器、可跨线程传）。
 *
 * ——— 一张图记住阶段①~④ ———
 *
 *     new QObject            → 栈上对象
 *        │                      │
 *        ├─ setParent(p) ──→ p 删它 ──→ 你不许再删
 *        └─ 无 parent ─────→ 你 delete ──→ 或者 unique_ptr
 *                               │
 *                               ├─ 立刻 delete     → 现在就没了
 *                               └─ deleteLater()   → 排队，事件循环收走
 *                                        │
 *                                        └─ 期间用 QPointer 观察，别留裸指针
 *
 * ——— 老师傅的话 ———
 *   徒弟，所有权这件事，写错一次你就记一辈子 —— 因为它的症状离现场太远。
 *   你在 A 处多写了一个 delete，崩在 B 处的一个信号回调里，栈上看不出任何关系。
 *
 *   所以我的规矩是：**每个 new 出来的时候，当场说出谁负责 delete。**
 *   说不出来，就先别写这个 new。
 *
 *   再送你三条实战经验：
 *   一、能用值就别用指针（QString、QVector、你自己的 TodoItem）。
 *      指针只在「需要身份/需要多态/需要延迟构造」时才请出来。
 *   二、栈对象不进树。这条没有例外，因为它出事的时候连栈都不给你看。
 *   三、看到别人写 connect(sender, sig, [this]{...})，先问一句「this 什么时候死」。
 *      答不上来，就把它改成 connect(sender, sig, this, [this]{...}) ——
 *      多打三个字符，少一次线上白屏。
 */

// ============================================================================
// 十二、练习
// ----------------------------------------------------------------------------
/*
 * 1. 把第 ② 节的 setParent(nullptr) 删掉再跑（**先在虚拟机里跑**）。
 *    观察崩溃发生在什么时候：是离开作用域时，还是 owner 析构时？
 *    然后回答：为什么不摘的这一行，编译器、静态检查、甚至 Qt 自己的调试断言都拦不住？
 *
 * 2. 给第 ③ 节加一个「在 destroyed 信号里访问 sender」的实验：
 *    连接 destroyed 到一个 lambda，在里面 print sender 的地址（只打印，不解引用）。
 *    然后回答：destroyed 是**对象咽气前**发还是**咽气后**发？
 *    （提示：Qt 的约定是「发信号时对象仍然完整，但已经在析构中」，
 *      所以只能读地址，不能调虚函数。）
 *
 * 3. 把第 ⑤ 节的 withoutContext 那条连接改成用一个**已经释放的堆对象**做 context 之外的捕获
 *    （比如捕获一个堆上的 int*，然后 delete 它），观察它为什么不会崩 ——
 *    然后再把它改成捕获一个 QObject* 并调用成员函数，你会得到什么？
 *    把这两次的差别写成一句话：**「不崩」和「正确」是两件事。**
 *
 * 4. 实现一个 `ObjectScope` 小工具：构造时接收若干 QObject*，析构时把它们全 delete。
 *    要求：用 QObject 树实现（把它们 setParent 到一个内部 QObject 上），
 *    而不是自己维护一个列表 —— 这样它才能自动处理「对象已经先死了」的情况。
 *    做完回答：为什么用树比用列表安全？
 *
 * 5. 把第 ① 节的销毁顺序测 100 次（换个插入顺序、换个层数），统计顺序是否稳定。
 *    然后读 Qt 文档里 ~QObject 那一段，找出它到底承诺了什么、没承诺什么。
 *    这一题没有代码产出，但它会让你以后写析构时心里有数。
 *
 * 6. 资深题：找出你项目里所有 `delete` 对 QObject 的调用，逐个回答三件事：
 *    (a) 这个对象有 parent 吗？（有就删错了）
 *    (b) 这行 delete 执行的时候，它自己的信号槽会不会还在栈上？
 *    (c) 删完之后，还有谁握着指向它的裸指针？
 *    第三问答不上来的那些地方，就是该换 QPointer 的地方。
 */
// ============================================================================

// 本文件有 3 个 Q_OBJECT 类（TrackedObject / Victim / Pinger），AUTOMOC 生成 .moc。
#include "04_ownership_and_lifetime.moc"
