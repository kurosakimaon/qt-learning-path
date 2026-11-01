// ============================================================================
// 第 4 讲 · lambda 槽与对象生命周期（实战崩溃第一现场）
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   Qt 里「按一下就崩、换个界面就崩、跑十分钟才崩」，绝大多数是这一讲的事。
//     1) lambda 当槽：闭包**不是 QObject**，拿不到第 3 讲那条「析构自动断连」的保护
//     2) context object 到底protect什么：连接的寿命跟着 context 那个对象走
//     3) 「不传 context 就崩」和「传了 context 就安全」两条路径的对照演示
//     4) QPointer 当兜底：没有合适的 QObject 可当 context 时怎么写
//     5) 捕获 this 的两种命运（this 是 QObject / this 是普通类）
//     6) QSignalBlocker 的两个坑：写成匿名临时对象=白写；以及「它屏蔽的是出口不是入口」
//
// 对应构建目标名：metaobject_04_lambda_and_lifetime
//   add_executable(metaobject_04_lambda_and_lifetime 04_lambda_and_lifetime.cpp)
//   target_link_libraries(metaobject_04_lambda_and_lifetime PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)
//
// 运行前须知：
//   · 只建 QApplication 不显示窗口；结论全部 qInfo()；QTimer 驱动 + 自动 quit()。
//   · 【重要】本示例**不会**故意把自己搞崩：危险路径只用「连接还活着吗」这个可观察事实
//     来证明隐患存在，然后**不发射**那个会解引用悬空指针的信号。
//     想亲眼看崩溃，照第 1 节末尾那三行注释自己加一句 model.fire(...) 即可。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 metaobject_04_lambda_and_lifetime，跑到自动退出、退出码 0）。
//     但本讲**没有汇总自检行**，退出码 0 只证明「没崩、走完了」，不证明正文每个数字都对：
//     带具体数字的「预期」请以你自己那次运行输出为准（本机那一轮存档 runlogs/metaobject_04_lambda_and_lifetime.log）。
// ============================================================================

#include <QApplication>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QDebug>
#include <memory>

// ============================================================================
// 一、一个**长寿**的发送者：它比所有接收者都活得久
// ============================================================================
// 真实项目里这就是那个「单例 model / 后台 controller / QApplication 级别的计时器」。
// 生命周期错配的崩溃，几乎都是「长寿 sender + 短命 receiver」这一对。
class Model : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    void load(const QString &text) { m_data = text; Q_EMIT dataChanged(m_data); }
    QString data() const { return m_data; }
Q_SIGNALS:
    void dataChanged(const QString &text);
private:
    QString m_data;
};

// ============================================================================
// 二、一个**普通 C++ 类**（不是 QObject）：它当被捕获的对象时最危险
// ============================================================================
class Counter
{
public:
    explicit Counter(QString tag) : m_tag(std::move(tag)) {}
    void add() { ++m_n; }
    int  value() const { return m_n; }
    QString tag() const { return m_tag; }
private:
    QString m_tag;
    int m_n = 0;
};

// ============================================================================
// 三、一个**短命**的 QObject 接收者：它可以当 context，也可以被 QPointer 看着
// ============================================================================
class Viewer : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    // 注意：show() 是**普通成员函数**（不是 slot），靠函数指针 connect 一样能当槽用；
    // 它内部还会 Q_EMIT 一个自己的信号——第 7 节要用「v 既能收、也能发」这个身份。
    void show(const QString &text) { ++m_shown; m_last = text; Q_EMIT textShown(m_last); }
    int  shown() const { return m_shown; }
    QString last() const { return m_last; }

    // 在成员函数里连 lambda：这是「捕获 this」最常见的位置。
    // 规矩：**必须**把 this 当第三个参数（context）传进去，
    // 这样 Viewer 一析构，Qt 就顺手把这条 lambda 连接摘掉。
    void bindTo(Model &model)
    {
        QObject::connect(&model, &Model::dataChanged,
                         this,                       // ← context：连接寿命跟着 this 走
                         [this](const QString &t) { show(t); });
        // 上面那句如果写成三参版（省掉 this）：
        //   QObject::connect(&model, &Model::dataChanged, [this](const QString &t){ show(t); });
        // 按 qobject.h 第 284 行的实现，它等价于
        //   connect(&model, &Model::dataChanged, &model, lambda, Qt::DirectConnection)
        // ——context 变成了 **model**。model 比 this 长寿，this 一死，
        // 下一次 dataChanged 就在已析构的 Viewer 上调 show()。这是崩溃的标准配方。
    }

Q_SIGNALS:
    void textShown(const QString &text);          // v 自己发出的信号（第 7 节的主角）

private:
    int m_shown = 0;
    QString m_last;
};

// ============================================================================
// 四、一张对照表，背下来（本讲的全部工程价值就在这张表里）
// ============================================================================
//   connect 写法                         连接的 context      谁死时自动断连
//   ─────────────────────────────────────────────────────────────────────────
//   connect(s, &S::sig, r, &R::slot)     r（那个 QObject）    r 析构 → 断（安全）
//   connect(s, &S::sig, r, lambda)       r                  r 析构 → 断（安全）
//   connect(s, &S::sig, lambda)   三参    s（偷偷等于 sender）s 析构才断；别的都不断（危险）
//   connect(s, &S::sig, lambda, type)    ——  该重载不存在（第 5 参是 ConnectionType）
//   （QT_NO_CONTEXTLESS_CONNECT 打开后）三参版直接编译不过，逼你写 context
// ============================================================================

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    Model model;                                   // 长寿发送者，活到 main 结束

    qInfo() << "=== 1. 危险路径：lambda 捕获裸指针 + 三参 connect ===";
    Counter *raw = new Counter("raw");
    // 三参：没有 context。按头文件实现，context 被偷偷设成 &model。
    QMetaObject::Connection dangerousConn =
        QObject::connect(&model, &Model::dataChanged,
                         [raw](const QString &) { raw->add(); });
    model.load(QStringLiteral("第一次"));           // 此刻 raw 还活着，正常
    qInfo() << "  raw->value() =" << raw->value()
            << " 连接状态：" << (dangerousConn ? "活" : "死");

    delete raw;                                    // ← raw 死了，**但连接还活着**
    qInfo() << "  delete raw 之后，连接仍是：" << (dangerousConn ? "活" : "死")
            << "  ← 这一行就是崩溃的全部原因：Qt 没有任何理由断这条连接";
    // 【想亲眼见崩】把下面这行取消注释，跑一次：
    //   model.load(QStringLiteral("悬空触发"));     // ← 解引用已释放内存，未定义行为
    // 典型表现：当场段错误 / 值莫名变成大数 / 十分钟后在别处崩（最阴的一种）。
    // 更阴的是：它**常常看起来没事**——释放后的内存还残留着旧字节，你测三轮都不崩，
    // 上线第一波流量就崩。未定义行为没有「本地复现」这个保证。
    QObject::disconnect(dangerousConn);            // 手动拔掉，下面不再碰它
    qInfo() << "  手动 disconnect 后连接状态：" << (dangerousConn ? "活" : "死");

    qInfo() << "=== 2. 安全路径 A：把短命 QObject 传成 context ===";
    {
        Counter *c = new Counter("with-context");
        auto *viewer = new Viewer;
        // 四参带 context：连接登记在 **viewer** 名下。
        QMetaObject::Connection safeConn =
            QObject::connect(&model, &Model::dataChanged, viewer,
                             [c, viewer](const QString &t) { c->add(); viewer->show(t); });
        model.load(QStringLiteral("A 还在"));
        qInfo() << "  连接活：" << (safeConn ? "活" : "死")
                << " c->value() =" << c->value()
                << " viewer->shown() =" << viewer->shown();
        delete viewer;                             // ← context 死了 → 连接自动断
        qInfo() << "  delete viewer 之后连接自动变：" << (safeConn ? "活" : "死")
                << "（第 3 讲的『接收者析构自动断连』在 lambda 上**借** context 生效了）";
        model.load(QStringLiteral("A 已亡"));       // 安全：没人再碰 c
        delete c;                                  // 谁捕获的东西谁负责回收（见下方提醒）
    }
    // 【提醒】传 context 只解决「不再被调用」，**不解决所有权**。
    // lambda 捕获的裸指针指向的堆对象仍然要你自己 delete，否则就是泄漏。
    // 想连所有权一起管，两条正路：
    //   ① 让被捕获的东西本身是 QObject 并挂进父子树（第 7 讲）；
    //   ② 用 shared_ptr 按值捕获 + weak_ptr 判活（本讲第 5 节演示 QPointer 版，
    //      std::weak_ptr 版同理，且适用于非 QObject）。

    qInfo() << "=== 3. 安全路径 B：context 用 this（成员函数里最常见）===";
    {
        auto *v = new Viewer;
        v->bindTo(model);                          // 里面就是 connect(..., this, lambda)
        model.load(QStringLiteral("B 活着"));
        qInfo() << "  v->shown() =" << v->shown() << " last =" << v->last();
        delete v;                                  // this 死 → 连接随 it 一起消失
        // 这里不需要任何「判活探针」：context 就是 v 自己，
        // qobject.h 第 284 行那条「context 缺省成 sender」的后门没被走到。
        model.load(QStringLiteral("B 已亡"));       // 安全，不会碰已析构的 Viewer
        qInfo() <<  "  delete v 后再 load 也没崩；model.data() =" << model.data();
    }

    qInfo() << "=== 4. 兜底路径：没有合适 context 时，用 QPointer 在 lambda 内判活 ===";
    {
        auto *v = new Viewer;
        QPointer<Viewer> guard = v;                // qpointer.h 第 38 行 QPointer(T*)
        // 这里三参 connect 仍然「context = model」，所以 **lambda 每次都会被调用**；
        // QPointer 的作用不是断连，而是让被调用时「一看对象已死就立刻返回」。
        QObject::connect(&model, &Model::dataChanged, [guard](const QString &t) {
            if (guard.isNull()) {                  // isNull() —— qpointer.h 第 86 行
                qInfo() << "    [lambda] QPointer 已失效，安全跳过（信号仍打到了这里）";
                return;
            }
            guard->show(t);                        // operator-> —— 第 79 行，判活之后才敢用
            qInfo() << "    [lambda] 收到并转交：" << t << " shown =" << guard->shown();
        });
        model.load(QStringLiteral("C 第一次"));
        delete v;
        qInfo() << "  delete 之后 guard.isNull() =" << guard.isNull()
                << " operator bool =" << (guard ? true : false);
        model.load(QStringLiteral("C 第二次"));     // lambda 仍被调用，但被 QPointer 拦住
        qInfo() << "  注意：连接**并没有**因为 QPointer 失效而断掉（下面第 5 节给出正确组合）";
    }

    qInfo() << "=== 5. 正确组合：context + QPointer 双保险 ===";
    {
        Counter *c = new Counter("combo");
        auto *v = new Viewer;
        // 【注意】c 不是 QObject，所以 **没法**给它套 QPointer（QPointer 的模板参数
        // 必须能隐式转成 QObject*，见 qpointer.h 第 30 行内部成员的类型）。
        // 兜底只能靠两条：① context 用 v，保证「v 死后 lambda 再也不被调用」，
        // 于是 c 只可能在 v 活着时被碰到；② c 自己必须活得比 v 久
        // （下面 delete c 排在 delete v 之后，就是这个顺序要求）。
        QObject::connect(&model, &Model::dataChanged, v,
                         [c, ptr = QPointer<Viewer>(v)](const QString &t) {
                             if (!ptr) return;     // explicit operator bool()，第 88 行
                             c->add();
                             ptr->show(t);
                         });
        model.load(QStringLiteral("D 活着"));
        qInfo() << "  c->value() =" << c->value() << " v->shown() =" << v->shown();
        delete v;                                  // ① context 死 → 连接断（第一重保险）
        model.load(QStringLiteral("D 已亡"));       // ② lambda 不再被调用；即便被调用，
        qInfo() <<  "  delete v 后再 load，c->value() 仍是" << c->value()
                << "（context 已经替我拔掉了连接，QPointer 只是第二重保险）";
        delete c;
    }
    // 【QPointer 的硬限制，本机头文件可核对】
    //   · qpointer.h 第 30 行：内部成员是 QWeakPointer<QObjectType>，
    //   所以 T 必须能隐式转成 QObject*。`QPointer<Counter>`（Counter 不是 QObject）
    //   编译不过——第 5 节里那个非 QObject 的 c 就只能用 context 来保，不能用 QPointer。
    //   · 第 21 行 static_assert 明写：`QPointer<T*>` 禁止（"QPointer's template type
    //     must not be a pointer type"），要写 QPointer<Viewer> 而不是 QPointer<Viewer*>。
    // 所以：**普通 C++ 类想用「判活」兜底，请换 std::weak_ptr**，
    // 按值捕获一个 shared_ptr 的 weak 副本，进 lambda 先 lock()。

    qInfo() << "=== 6. std::weak_ptr 版兜底（适用非 QObject）===";
    {
        auto counter = std::make_shared<Counter>("shared");
        std::weak_ptr<Counter> weak = counter;
        QObject::connect(&model, &Model::dataChanged, [weak](const QString &) {
            if (auto sp = weak.lock()) sp->add();   // 标准库，判活 + 临时持有
        });
        model.load(QStringLiteral("E 第一次"));
        qInfo() << "  shared 版 value =" << counter->value();
        counter.reset();                             // 对象真死了
        model.load(QStringLiteral("E 第二次"));      // lock() 返回空，安全
        qInfo() << "  reset 后 weak.expired() =" << weak.expired()
                << "（普通 C++ 类只能靠这条路，QPointer 帮不上）";
    }

    qInfo() << "=== 7. 两个哑坑：blockSignals 的方向 + 匿名临时对象 ===";
    {
        Viewer v;                                  // v 既能收（show），也能发（textShown）
        QObject::connect(&model, &Model::dataChanged, &v, &Viewer::show);
        int textShownHits = 0;                      // 只统计 **v 自己发出**的那条信号
        QObject::connect(&v, &Viewer::textShown, &v,
                         [&textShownHits](const QString &) { ++textShownHits; });

        {
            // 正确写法：给它一个名字，作用域内一直屏蔽。
            QSignalBlocker blocker(&v);             // 内部即 blockSignals(true)（510/516 行）
            qInfo() << "  v.signalsBlocked() =" << v.signalsBlocked()
                    << "（qobject.h 140 行，直接读 d_ptr->blockSig）";
            model.load(QStringLiteral("F 屏蔽期间"));
            // 【关键认知】blockSignals 屏蔽的是「这个对象**发出去**的信号」，
            // 不是「发给它的信号」。所以：
            qInfo() << "  v.shown() =" << v.shown()
                    << " ← 槽照跑（model 的信号没被屏蔽，v 只是接收方）";
            qInfo() << "  v 发出 textShown 的命中数 =" << textShownHits
                    << " ← 只有这里是 0，这才是 blockSignals 真正管的东西";
            // 【反面教材】`QSignalBlocker(&v);` 这样写成匿名临时对象≈白写：
            // 本条语句结束就析构，546 行那个 `if (m_o && !m_inhibited)` 立刻把状态还原。
            // Qt 看这坑太常见，给三个构造函数都加了 Q_NODISCARD_CTOR（487/489/493 行）。
            // 想提前解除：blocker.unblock();（557~562 行，顺带把 m_inhibited 置真）
            // 想「就让它一直屏蔽下去、析构时别恢复」：blocker.dismiss();（564~567 行）
            // 想中途再屏蔽一次：blocker.reblock();（550~555 行）
        }                                          // ← 这里析构，恢复原状
        model.load(QStringLiteral("G 解除后"));
        qInfo() << "  出作用域后 v.shown() =" << v.shown()
                << " textShown 命中 =" << textShownHits << "（两边都恢复正常）";
    }

    qInfo() << "=== 8. 让 lambda「只跑一次」的两种做法 ===";
    {
        int onceHits = 0;
        // 做法 ①：自己在 lambda 里断。conn 要被 lambda 捕获，注意捕获**副本**的时机：
        QMetaObject::Connection conn;
        conn = QObject::connect(&model, &Model::dataChanged, [&onceHits, &conn](const QString &) {
            ++onceHits;
            QObject::disconnect(conn);             // 用捕获的句柄把自己拔掉
        });
        model.load(QStringLiteral("H1"));
        model.load(QStringLiteral("H2"));
        qInfo() << "  自断式：命中" << onceHits << "次（应为 1）";
        // 做法 ②：Qt::SingleShotConnection（qnamespace.h 第 1319 行 = 0x100）
        // 语义上就是「命中一次自动断开」，但它配套的返回类型
        // QMetaObject::ConnectResult 在本机 Qt 6.12.0 头文件里 grep 不到，
        // 所以我不用它——本机可核对的写法只有上面这种自断式。
        // 【编译不过】别写 `QMetaObject::ConnectResult r = connect(..., Qt::SingleShotConnection);`
    }

    // ── 自动脚本 ──────────────────────────────────────────────────────────
    QTimer::singleShot(120, [&model] {
        qInfo() << "=== 9. 事件循环里发射：上面的每一条连接状态都没变 ===";
        model.load(QStringLiteral("I 定时发射"));
        qInfo() << "  model.data() =" << model.data();
    });
    QTimer::singleShot(300, [] {
        qInfo() << "=== 第 4 讲演示结束，自动退出 ===";
        QCoreApplication::quit();
    });

    return app.exec();
}

#include "04_lambda_and_lifetime.moc"

// ——— 老师傅的话 ———
/*
 * 徒弟，这一讲是全模块「最值钱」的一讲，因为它是**实战崩得最多**的那一类。
 * 你只要把一句话刻进肌肉记忆，就能少踩一半的 Qt 崩溃：
 *
 *   **三参 connect 不是「没有 context」，是 context = sender。**
 *
 * 这不是我编的口诀，是 qobject.h 第 284 行那个函数体：
 *   { return connect(sender, signal, sender, std::forward<Func2>(slot), Qt::DirectConnection); }
 * 一个模板实现里的一个实参位置，把「省一个参数」变成「把寿命押在发送者身上」。
 * 而 GUI 程序的对象图恰恰是反的：**model/controller 长寿，view/widget 短命**。
 * 于是「切个标签页、关掉个对话框、刷新一下列表」就成了引爆时刻——
 * 你复现不出来，因为要「先关窗口、再等一次异步刷新」这个顺序。
 * 所以我给你三条铁律：
 *   1) lambda 一律写四参带 context，除非你能证明 sender 和捕获对象同生共死；
 *   2) 捕获 `this` 时，context 就写 `this`（前提是 this 是 QObject）；
 *   3) 被捕获的**不是 QObject**（普通类、裸指针）时，context 找一个「和这个捕获
 *      同等寿命」的 QObject 顶上，QPointer／std::weak_ptr 只作第二重保险。
 *
 * 第二个要拎清的是 **QPointer 不断连接**。很多人以为「用 QPointer 捕获就安全了」，
 * 于是信号每次发射闭包照跑、判活照做，功能上没 bug，但白跑了一堆闭包和参数构造；
 * 真正「拔掉连接」的只有 context 析构和你手动 disconnect。
 * 判活（QPointer／weak_ptr）解决「这次调用别碰死对象」，context 解决「以后别再叫我」——
 * 两个是不同层面的防护，好代码两个都上（本讲第 5 节就是双保险写法）。
 *
 * 第三个：QPointer 的边界在头文件里写着（第 30 行内部是 QWeakPointer<QObjectType>），
 * 它**只对 QObject 有效**；普通 C++ 类请走 std::shared_ptr/weak_ptr。
 * 还有那句 static_assert「QPointer's template type must not be a pointer type」——
 * 写 `QPointer<Viewer*>` 是新手误用，正确形式是 `QPointer<Viewer>`。
 *
 * 最后一条小心思：QSignalBlocker 的三个构造函数都被标了 Q_NODISCARD_CTOR（487/489/493 行）。
 * 那是 Qt 在对 `QSignalBlocker(&obj);` 这种「写了等于没写」的哑坑翻白眼。
 * 编译器肯提醒你，你倒好，别把返回值塞给 Q_UNUSED 了事，给它起个名字。
 *
 * 还有一条比语法更要命的认知偏差：**blockSignals 管的是「出口」，不是「入口」**。
 * 头文件里它就在 blockSignals/signalsBlocked 那一对（140/141 行）里，语义写得很清楚——
 * 屏蔽的是「这个对象自己发出去的信号」。多少人一句 `obj->blockSignals(true)`
 * 是奔着「让 obj 别再响应信号」去写的，方向完全反了：obj 的槽照跑不误。
 * 真要「让 obj 不再响应」，只有两条路：断掉连到它的连接（第 3 讲），
 * 或者用一个跟它同生共死的 context（本讲第 2 节）。
 */

// ——— 小练习 ———
/*
 *   1. 把第 1 节注释里那行 `model.load(QStringLiteral("悬空触发"));` 取消注释，
 *      跑一次，把崩溃现场（调用栈最上面那几帧）记下来；再改回不崩的版本，
 *      用第 2 节的方式（加 context）修好它。这一步做完，你对「不传 context」
 *      的体感就再也忘不掉了。
 *   2. 把第 3 节 Viewer::bindTo() 里的四参 connect 改成三参（删掉 `this`），
 *      然后在 main 里 new 一个 Viewer、绑定、删掉 Viewer、再 model.load(...)，
 *      观察是否崩溃；再改回来。用一句话总结「成员函数里写 lambda」的默认规矩。
 *   3. 把第 4 节改造成「context + QPointer」双保险版（照第 5 节抄结构），
 *      并在 delete v 之后打印 `QMetaObject::Connection` 的布尔值，
 *      验证「只靠 QPointer 时连接仍为活、加了 context 后连接变死」这个关键差别。
 *   4. 在第 7 节把 `QSignalBlocker blocker(&v);` 改成 `QSignalBlocker(&v);`（匿名临时对象），
 *      观察 textShown 命中数从 0 变成非 0；再改成 `v.blockSignals(true)` 不带解除，
 *      确认「出口被永久堵死」是什么现象。用一句话写出 blockSignals 的方向感。
 */
// ============================================================================
