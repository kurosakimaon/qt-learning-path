// ============================================================================
// 第 3 讲 · disconnect 的全部重载，与 QMetaObject::Connection
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//     1) disconnect 一共有 7 组重载，各自「断掉哪些连接」、返回值 true/false 说明什么
//     2) QMetaObject::Connection 是什么物种：一条连接的句柄，能问它活没活、能移动
//     3) 【保命默认行为】接收者析构时，Qt 自动把它名下所有连接断干净——
//        这条能省掉 90% 的 disconnect 需求，也把「为什么要传 context」讲实
//     4) 版本核对：本机 Qt 6.12.0 里 Connection::ScopedConnection **不存在**，
//        所以本模块不使用它，改为手写一个 20 行的 RAII 壳；并给出可复现的 grep 命令
//
// 对应构建目标名：metaobject_03_disconnect_and_connection
//   add_executable(metaobject_03_disconnect_and_connection 03_disconnect_and_connection.cpp)
//   target_link_libraries(metaobject_03_disconnect_and_connection PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)
//
// 运行前须知：
//   · 只建 QApplication 不显示窗口；结论全部 qInfo()；QTimer 驱动 + 自动 quit()。
//   · 本示例里有「delete 掉接收者再发射信号」的演示——那是**安全**的（Qt 已自动断连），
//     这也正是本讲要证明的事。别把这模式推广到「lambda 捕获裸指针」（第 4 讲会炸给你看）。
//   · 本机当前无法编译链接 Qt 程序，本示例**未在本机实测编译**；API 全对本机头文件核对过：
//       QtCore/qobject.h —— disconnect 的七组重载：
//                            289~290 行 字符串式（4 参静态）
//                            291~292 行 QMetaMethod 式（4 参静态）
//                            293~295 行 成员式 disconnect(signal, receiver, member)
//                                  —— 三个参数**全都有默认值 nullptr**
//                            296~297 行 成员式 disconnect(receiver, member = nullptr)
//                            298~300 行 disconnect(const QMetaObject::Connection &)
//                                  —— 被 #ifndef QT_NO_DISCONNECT_CONST_CONNECTION 包住
//                            301~303 行 disconnect(QMetaObject::Connection &) 与 && 转发版
//                            305~325 行 成员指针式（Q_QDOC 声明 305~307、模板实现 309~325，
//                                  四元组精确匹配）
//                            326~337 行 「断掉该信号到某接收者的全部槽」模板版，
//                                  第 4 个形参是 void **zero，函数体第一句 Q_ASSERT(!zero)（333 行）
//                            connectNotify 376 行 / disconnectNotify 377 行（都是 virtual）
//                            destroyed 信号 352 行、
//                            receivers() 369 行 —— **在 366 行起的 protected 段里**，
//                                  类外调不动（第 5 节演示怎么绕），isSignalConnected() 同样 370 行、
//                            setParent 208 行
//       QtCore/qobjectdefs.h —— class QMetaObject::Connection 定义在 674~700 行：
//                            683 行析构、684~685 行默认构造标着 QT_CORE_INLINE_SINCE(6, 12)、
//                            686~687 行拷贝构造 / 拷贝赋值、
//                            689 行 Q_QDOC 下的 operator bool()（只是给文档看的），
//                            693~694 行真身：受限 bool（restricted bool）operator RestrictedBool，
//                            头文件注释原话「still using the restricted bool trick here, in
//                            order to support code using copy-init (e.g. `bool ok = connect(...)`)」
//                            697 行移动构造、698 行移动赋值（纯 swap 实现）、699 行 swap()
//       QtCore/qmetaobject.h —— QMetaMethod::fromSignal 222 行、name() 27 行、methodSignature 26 行
//     【本机实测的否定结论】在 C:\msys64\ucrt64\include\qt6 整棵树执行
//       grep -rin "scopedconnection" .
//     零命中 —— 即本机 Qt 6.12.0 **没有** QMetaObject::Connection::ScopedConnection。
//     同一棵树 grep "ConnectResult" 也零命中（Qt::SingleShotConnection 这个枚举值倒确实在
//     qnamespace.h 第 1319 行）。因此本模块不使用这两个东西；换机器请先跑上面两条 grep。
// ============================================================================

#include <QApplication>
#include <QObject>
#include <QMetaMethod>
#include <QString>
#include <QTimer>
#include <QDebug>

// ============================================================================
// 一、发送者 & 接收者
// ============================================================================
class Producer : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    void fire(int v) { Q_EMIT ping(v); }
    void fireDone()  { Q_EMIT done(); }

    // 【坑，本讲的教学点之一】QObject::receivers(const char *) const 是 **protected**：
    // 核对 QtCore/qobject.h —— 第 366 行是 `protected:`，第 369 行才是
    //   `int receivers(const char *signal) const;`
    // 所以在 main（类外）写 `prod.receivers(SIGNAL(ping(int)))` 编译器直接拒绝：
    //   error: 'int QObject::receivers(const char*) const' is protected within this context
    // 元对象侧（QMetaObject / QMetaMethod）也**没有**公开 API 能数出「这个信号挂了几条连接」，
    // 所以要这个数只有两条路：
    //   ① 被观察的对象自己开一扇门（成员函数内部访问 protected 合法）——就是下面这层壳；
    //   ② 改问「连接还活着吗」：拿 connect 返回的 QMetaObject::Connection，用它的受限 bool
    //      转换判活（qobjectdefs.h 693~694 行）。注意 Connection **没有** isNull()：
    //      核对 qobjectdefs.h 第 674~700 行那段 Connection 类定义，成员里翻不出 isNull，
    //      同文件 grep `isNull` 也是零命中——别照网上写。
    // 下面两条路本讲都用：①给数字，②给「门票」这一侧的独立证据。
    int receiverCount(const char *signal) const { return receivers(signal); }

Q_SIGNALS:
    void ping(int value);
    void done();
};

class Sink : public QObject
{
    Q_OBJECT
public Q_SLOTS:
    void onPing(int v) { ++m_hits; m_last = v; }
    void onDone()      { ++m_doneHits; }

    // 重写这两个 virtual，就能「被通知」连接建立/断开的事件。
    // 核对 qobject.h 376~377 行：
    //   virtual void connectNotify(const QMetaMethod &signal);
    //   virtual void disconnectNotify(const QMetaMethod &signal);
    // 它们是**按信号（QMetaMethod）**通知的：不告诉你对面是谁、也不告诉你断的是第几条，
    // 所以别拿它做精细记账；真要记账请存 QMetaObject::Connection。
    void connectNotify(const QMetaMethod &signal) override
    {
        ++m_connectNotifyHits;
        m_lastNotifyName = QString::fromUtf8(signal.name());
    }
    void disconnectNotify(const QMetaMethod &signal) override
    {
        ++m_disconnectNotifyHits;
        m_lastNotifyName = QStringLiteral("断开:") + QString::fromUtf8(signal.name());
    }

public:
    int m_hits = 0, m_doneHits = 0;
    int m_last = -1;
    int m_connectNotifyHits = 0, m_disconnectNotifyHits = 0;
    QString m_lastNotifyName;
};

// ============================================================================
// 二、本机没有 ScopedConnection，那就自己搭一个 20 行的 RAII 壳
// ============================================================================
// 语义：出作用域自动 QObject::disconnect(handle)。
// 注意它**不拥有**连接的所有权，只是「保证离开时断开」——这正是 ScopedConnection
// 应有的样子（本机版本没提供，所以这段是我们自己的代码，不是 Qt API）。
// 三条设计要点，都能从头文件推出来：
//   · 用移动构造接管 connect 的返回值（Connection 的移动构造在 qobjectdefs.h 697 行）；
//   · 禁拷贝，防止两个 guard 断同一条（拷贝构造/赋值虽然在 686~687 行存在，
//     但两条门票指向同一条连接时，第二条 disconnect 只会返回 false，白忙一场）；
//   · 判活写 `if (m_conn)`：受限 bool 只保证**上下文布尔转换**能用（if/while/!/&&/?:），
//     所以别写 `bool b = m_conn;` 之外的花活；想拿 bool 就写 `m_conn ? true : false`。
class ScopedConnGuard
{
public:
    explicit ScopedConnGuard(QMetaObject::Connection conn) noexcept
        : m_conn(std::move(conn)) {}
    ~ScopedConnGuard() { if (m_conn) QObject::disconnect(m_conn); }

    ScopedConnGuard(const ScopedConnGuard &) = delete;
    ScopedConnGuard &operator=(const ScopedConnGuard &) = delete;

    bool alive() const { return m_conn ? true : false; }
    QMetaObject::Connection &handle() noexcept { return m_conn; }

private:
    QMetaObject::Connection m_conn;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    Producer prod;
    Sink     a;      // 常驻接收者
    Sink     b;      // 用来演示「接收者析构 → 自动断连」

    qInfo() << "=== 1. connect 的返回值 QMetaObject::Connection 是什么 ===";
    // 它是「这一条连接」的句柄。它**不拥有**连接（析构不会断开），
    // 只是一张门票：拿着它能精确断开这一条，也能问它「还连着吗」。
    QMetaObject::Connection connA =
        QObject::connect(&prod, &Producer::ping, &a, &Sink::onPing);
    QMetaObject::Connection connB =
        QObject::connect(&prod, &Producer::ping, &b, &Sink::onPing);
    QMetaObject::Connection connDone =
        QObject::connect(&prod, &Producer::done, &a, &Sink::onDone);

    // 受限 bool 判的是 `d_ptr && isConnected_helper()`（qobjectdefs.h 第 694 行）：
    // 回答的是「这张门票背后那条连接现在还连着吗」，不是「当初 connect 成没成功」。
    // 断过、或者接收者死了，它都会变成 false。
    qInfo() << "  三条连接是否都活着："
            << (connA ? "A活" : "A死") << (connB ? "B活" : "B死") << (connDone ? "Done活" : "Done死");
    // 版本坑：本机 6.12.0 的头把**默认构造**标着 QT_CORE_INLINE_SINCE(6, 12)（684~685 行），
    // 也就是「先声明一个空 Connection 占位」这种写法要 Qt >= 6.12 才稳：
    //     QMetaObject::Connection placeholder;   // 6.12 起 OK，更早的 Qt6 编不过
    // 跨版本的保险写法是先 connect 再初始化：QMetaObject::Connection c = QObject::connect(...);
    // 拷贝构造/赋值在 686~687 行，移动构造 697 行、移动赋值经 swap（698 行）。
    // 所以想批量持有连接，用 QList<QMetaObject::Connection>（可移动）是没问题的。
    QMetaObject::Connection copyOfA = connA;       // 拷贝构造：两条门票指向**同一条**连接
    qInfo() << "  connA 的拷贝指向同一条连接：" << (copyOfA ? "活" : "死");

    prod.fire(1);
    qInfo() << "  发射 ping(1) → a.hits =" << a.m_hits << " b.hits =" << b.m_hits;

    qInfo() << "=== 2. disconnect(QMetaObject::Connection)：最精确的一把刀 ===";
    const bool off1 = QObject::disconnect(connA);       // qobject.h 301 行（非 const 引用版）
    const bool off1Again = QObject::disconnect(connA);  // 同一条再断一次
    qInfo() << "  第一次断 =" << off1 << " 再断同一条 =" << off1Again
            << " 断完后门票：" << (connA ? "活" : "死")
            << " 拷贝的那张也一起失效：" << (copyOfA ? "活" : "死");
    prod.fire(2);
    qInfo() << "  发射 ping(2) → a.hits =" << a.m_hits << " b.hits =" << b.m_hits
            << "（a 不涨了，b 还在涨：句柄断连只影响那一条）";

    qInfo() << "=== 3. 成员指针式 disconnect：按四元组精确匹配 ===";
    // 核对 qobject.h 309~325 行：
    //   template <typename Func1, typename Func2>
    //   static bool disconnect(const ...FunctionPointer<Func1>::Object *sender, Func1 signal,
    //                          const ...FunctionPointer<Func2>::Object *receiver, Func2 slot);
    // 四元组 (sender, signal, receiver, slot) 必须逐个对上才断得掉，对不上返回 false。
    // 它**只支持成员函数指针**，不支持 lambda（没有 slot 可写）。
    const bool offB = QObject::disconnect(&prod, &Producer::ping, &b, &Sink::onPing);
    qInfo() << "  disconnect(&prod,&Producer::ping,&b,&Sink::onPing) =" << offB;
    prod.fire(3);
    qInfo() << "  发射 ping(3) → a.hits =" << a.m_hits << " b.hits =" << b.m_hits;

    qInfo() << "=== 4. 「断掉该信号到某接收者的全部槽」：那个 void** zero 形参 ===";
    // 核对 qobject.h 326~337 行，头文件注释原文值得抄给你看（就在 330~332 行）：
    //   // This is the overload for when one wish to disconnect a signal from any slot.
    //   // (slot=nullptr)
    //   // Since the function template parameter cannot be deduced from '0', we use a
    //   // dummy void ** parameter that must be equal to 0
    //   Q_ASSERT(!zero);
    // 所以最后一个实参必须写 nullptr（或 0）：它既是「选这个重载的开关」，又是
    // 「通配所有槽」的意思。传非空会直接踩 Q_ASSERT。
    QObject::connect(&prod, &Producer::ping, &a, &Sink::onPing);    // 重连回 onPing
    QObject::connect(&prod, &Producer::ping, &a, &Sink::onDone);    // 再连一个「参数更少」的槽
    // ↑ 这条合法！信号 ping(int) 有 1 个参数、槽 onDone() 有 0 个，
    //   qobject.h 第 257~258 行的 assert 是 `信号参数个数 >= 槽参数个数`，方向别记反。
    const bool offAny = QObject::disconnect(&prod, &Producer::ping, &a, nullptr);
    qInfo() << "  disconnect(sender, signal, receiver, nullptr) =" << offAny;
    const int b4 = a.m_hits, b4d = a.m_doneHits;
    prod.fire(4);
    qInfo() << "  发射 ping(4) → onPing 增量 =" << (a.m_hits - b4)
            << " onDone 增量 =" << (a.m_doneHits - b4d) << "（两条都被断了，都是 0）";

    qInfo() << "=== 5. 成员式 disconnect：三个参数全可省，nullptr 是通配符 ===";
    // 核对 qobject.h 293~297 行（两个 inline 成员重载，实参全带默认值）：
    //   bool disconnect(const char *signal    = nullptr,
    //                   const QObject *receiver = nullptr,
    //                   const char *member      = nullptr) const;
    //   bool disconnect(const QObject *receiver, const char *member = nullptr) const;
    // 语义是「以 this 为发送者」，nullptr 位置 = 通配。于是这四句层层收窄：
    //   obj.disconnect()                       断掉 obj 发出的**所有**信号的一切连接
    //   obj.disconnect(SIGNAL(ping(int)))      只断 ping(int) 这一个信号的所有连接
    //   obj.disconnect(&recv)                  只断 obj → recv 的所有连接
    //   obj.disconnect(SIGNAL(ping(int)), &recv, SLOT(onPing(int)))  最窄的一条
    const QMetaObject::Connection connPing5 =
        QObject::connect(&prod, &Producer::ping,  &a, &Sink::onPing);
    const QMetaObject::Connection connDone5 =
        QObject::connect(&prod, &Producer::done,  &a, &Sink::onDone);
    const int doneBefore = a.m_doneHits;
    const bool offAll = prod.disconnect();      // 一发清零：prod 的所有连接全断
    // receivers() 是 protected（qobject.h 366 行的 `protected:` 段、369 行的声明），
    // 类外调不动，所以这里走 Producer 自己开的 receiverCount() 那层壳。
    const int pingReceivers = prod.receiverCount(SIGNAL(ping(int)));
    prod.fire(9);
    prod.fireDone();
    qInfo() << "  prod.disconnect() =" << offAll
            << " 之后 receivers(ping(int)) =" << pingReceivers
            << " 两条门票：" << (connPing5 ? "活" : "死") << (connDone5 ? "活" : "死")
            << " done 增量 =" << (a.m_doneHits - doneBefore);
    // 返回语义：Qt 的实现是「至少断掉一条才返回 true」。一条都没得断时返回 false——
    // 别把 false 当异常，它常常只是「本来就没连」。
    // 注意 receivers() 至今只有字符串签名版（qobject.h 369 行），没函数指针重载，
    // 所以这里必须用 SIGNAL()；这是本讲唯一两处字符串宏用法之一，另一处见上面四连。
    // 再加上它是 protected，「数接收者」这件事天生就是**类内**的活儿；
    // 外部代码要判断连接在不在，正解是攥住 QMetaObject::Connection 判受限 bool。

    qInfo() << "=== 6. QMetaMethod 式 disconnect：反射路径上的写法 ===";
    // 核对 qobject.h 291~292 行：
    //   static bool disconnect(const QObject *sender, const QMetaMethod &signal,
    //                          const QObject *receiver, const QMetaMethod &member);
    // 这条的价值：信号和槽都可以在**运行期**按名字/索引拿到，适合插件、脚本、
    // 通用派发器这类「写代码时不知道要连谁」的场景。
    QObject::connect(&prod, &Producer::ping, &a, &Sink::onPing);
    const QMetaMethod pingMeta = QMetaMethod::fromSignal(&Producer::ping);  // 222 行
    const QMetaObject *am = a.metaObject();
    const int slotIdx = am->indexOfSlot("onPing(int)");                     // qobjectdefs.h 265 行
    const bool offMeta = (slotIdx >= 0)
        ? QObject::disconnect(&prod, pingMeta, &a, am->method(slotIdx))
        : false;
    qInfo() << "  fromSignal 拿到信号名 =" << pingMeta.name()
            << " 槽索引 =" << slotIdx << " QMetaMethod 式断开 =" << offMeta;

    qInfo() << "=== 7. 【保命】接收者析构 → 连接自动断开 ===";
    // 这是 Qt 信号槽最实用、也最多人不知道的默认行为：
    //   连接记录挂在 sender 侧的连接表上，但记录里对 receiver 是「可失效」的登记；
    //   receiver 析构时，Qt 把它在所有 sender 上的连接全部摘掉。
    // 结论：**你不需要在接收者的析构函数里手写 disconnect。**
    // 两条同样重要的推论：
    //   ① 只有「接收者死」触发自动清理。发送者死了连接自然也没了，但那是整表消失，
    //      不会给你什么可观察的补偿；
    //   ② **lambda 连接拿不到这条保护**：闭包不是 QObject，没有析构可挂钩。
    //      所以「lambda 捕获裸指针 + connect」= 悬空调用（第 4 讲专门讲），
    //      必须显式传一个 QObject 当 context，借那个对象的析构来断连。
    {
        auto *temp = new Sink;                      // 故意 new，好手动 delete
        QMetaObject::Connection tempConn =
            QObject::connect(&prod, &Producer::ping, temp, &Sink::onPing);
        prod.fire(10);
        qInfo() << "  临时接收者活着时 hits =" << temp->m_hits
                << " 门票：" << (tempConn ? "活" : "死");
        delete temp;                                // ← 只这一句，不用 disconnect
        qInfo() << "  delete 之后门票自动变：" << (tempConn ? "活" : "死")
                << "（Connection 帮你把「接收者已死」显式化了）";
    }
    const int safeBefore = a.m_hits;
    prod.fire(11);                                  // 第 5 节已全断，这里谁都不该收到
    qInfo() << "  删掉接收者后再发射，程序没崩；a 增量 =" << (a.m_hits - safeBefore)
            << "（这就是「自动断连」救命的地方）";
    // 还有一条容易混进来的误解，顺手澄清：**setParent 不会断信号槽连接**
    //（qobject.h 208 行只改父子树归属，父子树和连接表是两套东西，第 7 讲细讲）。
    // 只有「对象析构」才触发连接清理。别把两件事混了。

    qInfo() << "=== 8. 想自己观察连接变化：connectNotify / disconnectNotify ===";
    Sink watch;
    QObject::connect(&prod, &Producer::ping, &watch, &Sink::onPing);
    qInfo() << "  连一次后 connectNotify 次数 =" << watch.m_connectNotifyHits
            << " 最后记到的名字 =" << watch.m_lastNotifyName;
    QObject::disconnect(&prod, &Producer::ping, &watch, &Sink::onPing);
    qInfo() << "  断一次后 disconnectNotify 次数 =" << watch.m_disconnectNotifyHits
            << " 最后记到的名字 =" << watch.m_lastNotifyName;
    // 粒度是「信号」，不区分接收者、不区分第几条：同一个信号连两次再断两次，
    // 计数 2/2，名字全一样——别拿它当账本。
    // 配套还有个捷径：isSignalConnected(QMetaMethod)（qobject.h 370 行），
    // 在自定义发射逻辑里判断「这个信号到底有人听吗」，没人听就省掉构造参数的开销。

    qInfo() << "=== 9. 自制 RAII 壳：出作用域自动断开 ===";
    {
        ScopedConnGuard guard(QObject::connect(&prod, &Producer::done, &a, &Sink::onDone));
        qInfo() << "  guard 里连接活着吗：" << (guard.alive() ? "活" : "死");
        const int before = a.m_doneHits;
        prod.fireDone();
        qInfo() << "  作用域内 done 增量 =" << (a.m_doneHits - before);
    }   // ← guard 析构，自动 QObject::disconnect
    const int afterScope = a.m_doneHits;
    prod.fireDone();
    qInfo() << "  出作用域后再 fireDone，增量 =" << (a.m_doneHits - afterScope)
            << "（0 = 已自动断开）";

    // ── 自动脚本 ──────────────────────────────────────────────────────────
    QTimer::singleShot(120, [&prod, &a] {
        QMetaObject::Connection c =
            QObject::connect(&prod, &Producer::done, &a, &Sink::onDone);
        const int before = a.m_doneHits;
        prod.fireDone();
        qInfo() << "=== 10. 事件循环里再验一次：断就是断，没断就是没断 ===";
        qInfo() << "  重连后 done 增量 =" << (a.m_doneHits - before)
                << " 连接仍活着吗：" << (c ? "活" : "死");
    });
    QTimer::singleShot(300, [] {
        qInfo() << "=== 第 3 讲演示结束，自动退出 ===";
        QCoreApplication::quit();
    });

    return app.exec();
}

#include "03_disconnect_and_connection.moc"

// ——— 老师傅的话 ———
/*
 * 徒弟，这一讲真正要你记进骨头的不是那七组 disconnect，而是这一句：
 *
 *   **接收者一析构，Qt 就把它在所有发送者身上的连接全摘干净。**
 *
 * 为什么这条最值钱？因为它把「信号槽会不会野指针」这个问题，在**默认路径**上
 * 直接答成了「不会」。新手最爱写的「在析构函数里挨个 disconnect」几乎全是废动作；
 * 老手反过来，靠这条默认行为写出「谁先死都不用管」的代码。
 *
 * 但这张保护伞有一个致命的适用范围：**它只保护 QObject 槽。**
 * lambda / 函数对象 / std::function 都不是 QObject，没有析构函数可挂钩，
 * Qt 无从知道「你捕获的那个指针什么时候死」。于是：
 *     connect(model, &Model::changed, [&edit] { edit->setText(... });   // 灾难
 * 三参写法的 context 还偷偷等于 **sender**（qobject.h 278~286 行，第 2 讲抄过原文），
 * 于是只要 sender 比 edit 活得久，edit 一死，下一次 changed 就把野指针解了。
 * 正确写法永远是四参带 context：`connect(model, &Model::changed, edit, []{...})`，
 * 让 **edit** 去当那个「死时顺手断连」的角色。第 4 讲会把这个崩场完整拆给你看。
 *
 * 第二件要记的：`QMetaObject::Connection` 是**门票，不是所有权**。
 * 它析构不会断开任何东西；它只是「能精确拔掉这一条」+「能问这条还活着吗」。
 * 本机头文件用的是受限 bool（qobjectdefs.h 693~694 行），
 * 意思是 `if (conn)`、`conn ? … : …`、`bool ok = connect(...)` 这些**上下文布尔转换**能用，
 * 而 `qInfo() << conn`、`int x = conn;` 这类花活编不过——不是我藏拙，是 Qt 故意不让。
 * 所以你看本讲打印判活全写成 `(conn ? "活" : "死")`。
 *
 * 第三，版本坑要当面说清：网上很多教程让你用
 * `QMetaObject::Connection::ScopedConnection` 做 RAII。本机 Qt 6.12.0 **没有**这个类
 *（整棵头文件树 grep 零命中），`QMetaObject::ConnectResult` 同样没有。
 * 我没抄它，而是写了 20 行的 ScopedConnGuard 顶上。你换机器时，第一件事就是跑
 * `grep -rin "scopedconnection" C:/msys64/ucrt64/include/qt6`，
 * 有就用官方的，没有就用这种自制壳。查不到签名的 API 一律不用，这条纪律比省 20 行代码重要。
 *
 * 最后关于「返回值 true 还是 false」：所有 disconnect 重载返回的都是
 * 「有没有真的拔掉至少一条」。所以 false 十有八九不是错误，只是「本来就没连」。
 * 别写 `if (!disconnect(...)) qWarning()`——那会在你重复清理的路径上刷一屏噪音。
 */

// ——— 小练习 ———
/*
 *   1. 把第 7 节的 `delete temp;` 改成 `temp->deleteLater();`，然后在两个
 *      QTimer::singleShot 之间（比如 160ms 处）插一条 `prod.fire(12)`，
 *      想一想 deleteLater 要等事件循环处理 DeferredDelete 才真删，这里到底断没断。
 *      （deleteLater 的机制第 7 讲细讲，这里先练手感。）
 *   2. 把 ScopedConnGuard 的拷贝构造从 `= delete` 改成允许拷贝，然后在同一个作用域
 *      里复制一个 guard、让两个都出作用域，观察第二次的 disconnect 返回 false；
 *      据此用自己的话说清「门票」和「所有权」的区别。
 *   3. 只用第 6 节的 QMetaMethod 路径，重写第 2、3、4 节的三次断开
 *     （提示：槽用 `metaObject()->indexOfSlot("onPing(int)")` + `method(i)` 拿，
 *      信号用 `QMetaMethod::fromSignal` 或 `indexOfSignal("ping(int)")` 拿），
 *      然后比较：这条路径少了什么？（答案：编译期检查——签名写错了运行期才知道。）
 */
// ============================================================================
