// ============================================================================
// 第 3 讲 · 事件过滤器（event filter）
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   事件过滤器是「**不继承就改行为**」的唯一正统手段。本讲把四件事钉死：
//     1) installEventFilter 挂在**目标对象**上；qApp->installEventFilter 挂「应用级」。
//        两者其实都在 QApplication::notify 内部被调用（第 2 讲那条链的第 1.5 层）。
//        ★ 但「应用级」不等于「全程序」：应用级过滤器**只在主线程跑**
//          （qcoreapplication.cpp 第 1257~1277 行的 Q_ASSERT(QThread::isMainThread()) 与
//           第 1311~1317 行的 `if (QThread::isMainThread() && ...)`），
//           子线程里 notify 的事件只过对象级过滤器（第 1279~1295 行）。
//     2) eventFilter() 返回值 true / false 的确切语义：
//        **true = 我吃掉这个事件（目标对象的 event() 不再被调用）**，
//        false = 我只是看了看，继续往下传。
//     3) 同一目标上多个过滤器的调用顺序，以及「过滤器与被过滤对象谁先死」的坑。
//     4) 用过滤器给 QPushButton 做全局「滚轮无效 + 按下拦截 + 键盘审计」，
//        全程不 subclass 任何一个控件。
//
// 对应构建目标名：events_03_event_filter
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(events_03_event_filter 03_event_filter.cpp)
//   target_link_libraries(events_03_event_filter PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)   # 本文件里有 Q_OBJECT 类，末尾必须 #include "03_event_filter.moc"
//
// 运行前须知：
//   · 无鼠标键盘：「点击」是代码构造 QMouseEvent + sendEvent 合成的（第 1、2 讲手法）。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 events_03_event_filter，退出码 0，
//     存档 runlogs/events_03_event_filter.log）。
//   · ★ 本讲现在**有合账**了：结尾 17 行「［对］/［错］」逐条对数字，对不上的行数直接当
//     退出码用（`QCoreApplication::exit(led.bad)`）。原来这里只写着「退出码 0 只证明没崩」，
//     那是本讲唯一的短板，补上之后带数字的「预期」不再是空话。
//     为什么用 exit(n) 不用 quit()：quit()（qcoreapplication.cpp 2165~2185）只投一个 Quit
//     事件、退出码恒 0，账本就白对了；exit(n)（同文件 1549~1569）才把 n 交给 exec() 的返回值。
//   · 本示例无人值守跑完：约 4.2 秒自动演示 + qInfo() 打结论 + 合账 + exit()。
//   · 「合成 press+release 会触发 clicked」依赖 QAbstractButton 内部用
//     release 坐标做命中判定；若你的按钮尚未 show 或几何无效，请核对 print 出的计数。
// ============================================================================

#include <QApplication>
#include <QCoreApplication>
#include <QObject>
#include <QEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QWidget>
#include <QPushButton>
#include <QTimer>
#include <QThread>              // 演示⑦：线程边界（QtCore/qthread.h）
#include <atomic>               // std::atomic<int>：子线程写、主线程读
#include <QPointer>            // QtCore/qpointer.h：观察「被过滤对象是否已死」
#include <QHash>
#include <QDebug>
#include <QString>
#include <QStringList>

// ============================================================================
// 一、核对签名（本讲用到的全部入口）
// ============================================================================
// QtCore/qobject.h
//   第 113 行 public:
//   第 117 行   virtual bool event(QEvent *event);
//   第 118 行   virtual bool eventFilter(QObject *watched, QEvent *event);
//   第 209 行   void installEventFilter(QObject *filterObj);      ← 挂在**我**身上，让别人过滤我
//   第 210 行   void removeEventFilter(QObject *obj);
//   第 352 行   void destroyed(QObject * = nullptr);              （Q_SIGNALS，第 350 行附近）
//   第 364 行   void deleteLater();
// QtWidgets/qapplication.h
//   第 30 行    #define qApp (static_cast<QApplication *>(QCoreApplication::instance()))
// QtWidgets/qabstractbutton.h
//   第 90~93 行 pressed() / released() / clicked(bool checked = false) / toggled(bool)
//   第 85 行    void click();       ← 注意：这是「编程触发」，**绕过事件层**，过滤器拦不住它
// QtGui/qevent.h 第 282 行 class QWheelEvent；第 197 行 class QMouseEvent
//
// 关键认知：installEventFilter 的参数是「**过滤器对象**」，装在**调用者（被过滤对象）**身上。
// 也就是说 `button->installEventFilter(&audit)` 读作：
//   「button 啊，以后你的事件先给 audit 过一遍」——**主语是被告，不是侦探。**

// ============================================================================
// 二、一个会发信号的审计过滤器（Q_OBJECT 类写在 .cpp 里）
// ============================================================================
class Audit : public QObject
{
    Q_OBJECT                      // ← 因为下面要有信号；AUTOMOC 会生成本文件的 .moc
public:
    explicit Audit(const QString &name) : m_name(name) {}

    int seen() const { return m_seen; }
    int eaten() const { return m_eaten; }
    int passed() const { return m_passed; }
    QHash<int, int> countByType() const { return m_byType; }   // key 用 int，避免 QEvent::Type 作键的歧义

    // 「要吃掉哪些类型」用数据配置，别用一堆 if——这样同一份过滤器能复用
    void setEatTypes(const QList<QEvent::Type> &types) { m_eat = types; }

    // QObject::eventFilter 在基类是 **public virtual**，这里也保持 public
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        ++m_seen;
        m_byType[int(event->type())]++;

        // 顺序日志：多个过滤器同时挂在同一目标上时，这条链就是它们的调用次序
        qInfo().noquote() << QStringLiteral("    过滤器 %1 看到：目标=%2 type=%3")
                                 .arg(m_name, watched ? watched->objectName() : QStringLiteral("<null>"))
                                 .arg(int(event->type()));

        if (m_eat.contains(event->type())) {
            ++m_eaten;
            Q_EMIT swallowed(watched, event->type());   // ← 把「系统层的事件」翻成「应用层的信号」
            // 文档语义：返回 true = **不再调用 watched->event()**，notify 直接结束这一趟派发。
            return true;
        }
        ++m_passed;
        // 重要：这里**不要**返回 watched->eventFilter(...)，也不要「顺手」调用 event 自己派发，
        // 否则你会把事件送两次。过滤器只回答「要不要吃掉」。
        return false;
    }

Q_SIGNALS:
    // 这一条是本模块「事件 vs 信号」的分界碑：
    // 事件是系统层语义（QEvent*、原始 type 码、有 accept/ignore 状态）；
    // 信号是应用层语义（业务名字、参数类型化、有连接表与生命周期管理）。
    void swallowed(QObject *watched, QEvent::Type type);
    void keySeen(int key, const QString &text);

public:
    // 键盘单独走一条业务信号，演示「事件 → 信号」的正确转换点
    void watchKeys(bool on) { m_watchKeys = on; }

    bool filterKeyEvent(QEvent *e)
    {
        if (!m_watchKeys || e->type() != QEvent::KeyPress)
            return false;
        auto *ke = static_cast<QKeyEvent *>(e);        // 类型判断在前、static_cast 在后
        Q_EMIT keySeen(ke->key(), ke->text());         // 核对：qevent.h 425 行起 key()/text()
        return false;                                   // 只看、不吃：让按钮/焦点照常工作
    }

private:
    QString m_name;
    int m_seen = 0;
    int m_eaten = 0;
    int m_passed = 0;
    bool m_watchKeys = false;
    QList<QEvent::Type> m_eat;
    QHash<int, int> m_byType;
};

// ============================================================================
// 三、把键盘事件转成信号的辅助入口
// ============================================================================// Audit 同时是「普通过滤器」，所以 KeyPress 既进统计也可能要发 keySeen。
// 这里用一个薄封装：在 eventFilter 之外不再挂钩，直接把 KeyPress 交给 filterKeyEvent。
class KeyForwarder : public QObject
{
    Q_OBJECT
public:
    explicit KeyForwarder(Audit *target) : m_target(target) {}

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        // 先让被过滤对象照常收到事件（返回 false），但顺手把按键转成信号
        // ★ 这里把 watched 打出来：本讲原来写成 Q_UNUSED(watched)，于是「按键到底被谁收到」
        //   在日志里看不出来。打出来之后，本机这一轮（runlogs/events_03_event_filter.log）的
        //   顺序是：全局/只观察先看到 **目标=btnA** 的 type=51(ShortcutOverride)+6(KeyPress)，
        //   紧接着同两个 type 以**目标=主窗口**再来一遍，然后这里打出「目标 = "主窗口" type = 6」
        //   ——我们明明是 sendEvent(btnA, KeyPress 'a')，装在父窗口上的转发器却收到了它：
        //   这就是「子控件没收下这个键 → Qt 往父级传播」那条路，从此有日志可指。
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
            qInfo() << "    转发器收到按键：目标 =" << watched->objectName()
                    << "type =" << int(event->type());
        }
        m_target->filterKeyEvent(event);
        return false;
    }

private:
    Audit *m_target;
};

// ============================================================================
// 三·补：演示⑦ 用的「只数不印」过滤器
// ============================================================================
// 线程边界那一条要拿数字说话，所以这里不打印、不自增到噪声里：
// 只认下面这两种探针事件，别的（子线程自己的 ThreadChange/Show/Polish 之类）一律放过不计数。
static const QEvent::Type kProbeEventType = QEvent::Type(int(QEvent::User) + 7);    // 子线程里 post 的
static const QEvent::Type kProbeEventType2 = QEvent::Type(int(QEvent::User) + 8);   // 主线程里 post 的

class CountingFilter : public QObject
{
public:
    CountingFilter(std::atomic<int> *hitA, std::atomic<int> *hitB, QObject *parent = nullptr)
        : QObject(parent), m_hitA(hitA), m_hitB(hitB) {}

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == kProbeEventType)
            m_hitA->fetch_add(1, std::memory_order_relaxed);
        else if (event->type() == kProbeEventType2)
            m_hitB->fetch_add(1, std::memory_order_relaxed);
        Q_UNUSED(watched)
        return false;          // 永远放行：这个类只负责「我到底被调到没有」
    }

private:
    std::atomic<int> *m_hitA;
    std::atomic<int> *m_hitB;
};

// ============================================================================
// 四、合成一次「按下 + 松开」
// ============================================================================
static void syntheticClick(QWidget *w)
{
    const QPointF local(20, 20);                       // 要保证落在 w 的矩形里
    const QPointF global = w->mapToGlobal(local.toPoint());

    // 核对：qevent.h 第 197 行起 QMouseEvent 未弃用的构造：
    //   QMouseEvent(Type, const QPointF &localPos, const QPointF &globalPos,
    //               Qt::MouseButton button, Qt::MouseButtons buttons,
    //               Qt::KeyboardModifiers modifiers,
    //               const QPointingDevice *device = QPointingDevice::primaryPointingDevice());
    QMouseEvent press(QEvent::MouseButtonPress, local, global,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, local, global,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    // 注意第二个参数：press 时 buttons 里有 LeftButton，release 时应当是 NoButton
    // ——button() 是「谁发生了变化」，buttons() 是「此刻按住了哪些」。差别见第 5 讲。
    QCoreApplication::sendEvent(w, &press);
    QCoreApplication::sendEvent(w, &release);
}

// ============================================================================
// 五、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QWidget window;
    window.setObjectName(QStringLiteral("主窗口"));   // ← 给窗口起名，日志里「目标=」才有得主
    // ★ 原来窗口没有 objectName，演示⑤ 的按键传播只能在日志里看到一串「目标= （空）」，
    //   分不清是主窗口还是别的匿名对象。起了名字，「btnA 没收下这个键 → 传播给父窗口」
    //   这条路径才变成可以指着日志讲的东西。
    window.setWindowTitle(QStringLiteral("第 3 讲 · 事件过滤器"));
    window.resize(520, 260);

    auto *btnA = new QPushButton(QStringLiteral("被过滤的按钮 A"), &window);
    btnA->setObjectName(QStringLiteral("btnA"));
    btnA->move(20, 20);
    btnA->resize(200, 60);

    auto *btnB = new QPushButton(QStringLiteral("被过滤的按钮 B"), &window);
    btnB->setObjectName(QStringLiteral("btnB"));
    btnB->move(20, 100);
    btnB->resize(200, 60);

    window.show();

    int clickCountA = 0;
    int clickCountB = 0;
    // ★ 原来这两只按钮**共用** clickCountA：演示② 点一下 btnB 也会加到 A 的账上，
    //   于是本机实测打出「拆掉后 = 2（预期 1）」—— 数字对不上的原因不在事件层，
    //   在账本串了。分开计数之后，每一句「预期」才真的对应它说的那一下。
    QObject::connect(btnA, &QPushButton::clicked, [&clickCountA] { ++clickCountA; });
    QObject::connect(btnB, &QPushButton::clicked, [&clickCountB] { ++clickCountB; });

    // ── 全局过滤器：挂在 qApp 上，看得见**主线程**分发的每一个事件
    // 核对：qobject.h 第 209 行 —— qApp->installEventFilter(&global) 意为
    // 「任何（主线程的）对象的事件，先给 &global 过一遍」。
    // ★ 「整个程序」是错的说法，师傅这里改成「主线程」，判据在 Src 树
    //   src/corelib/kernel/qcoreapplication.cpp：
    //     第 1257~1277 行 sendThroughApplicationEventFilters() 开头就是
    //         Q_ASSERT(QThread::isMainThread());
    //     注释原文："application event filters are only called for objects in the GUI thread"
    //     第 1311~1317 行 notify_helper() 用 `if (QThread::isMainThread() && ...)` 包住这个调用，
    //     注释原文："only does anything in the main thread"
    //   所以别的线程里 notify 的事件，压根不经过 qApp 上的过滤器。对象级过滤器没有这条
    //   主线程限制（第 1279~1295 行），但要求过滤器与被监视对象同线程。
    //   ★ 这两条原来只是「源码读出来」的结论，现在**本讲自己跑出来了**：见演示⑦，
    //     三个数字（跨线程装 0 次 / 子线程自装 1 次 / 应用级看到子线程那一发 0 次）
    //     全部进了结尾的合账，对不上就把退出码改成非 0。
    Audit global(QStringLiteral("全局"));
    qApp->installEventFilter(&global);

    // ── 目标级过滤器 1：吃掉 btnA 的鼠标按下（按钮从此「按不动」）
    Audit pressEater(QStringLiteral("吃掉Press"));
    pressEater.setEatTypes({ QEvent::MouseButtonPress });      // 核对 qcoreevent.h 65 行 MouseButtonPress=2
    btnA->installEventFilter(&pressEater);

    // ── 目标级过滤器 2：也挂在 btnA 上，只观察，不吃
    Audit observer(QStringLiteral("只观察"));
    btnA->installEventFilter(&observer);
    // 文档语义：同一目标上的多个过滤器**按安装的逆序**调用，
    // 即这里「只观察」先跑、「吃掉Press」后跑。下面的日志会验证这一点。
    // 核对（Src 树 src/corelib/kernel/qobject.cpp）：
    //   第 2387~2388 行 文档原话 "If multiple event filters are installed on a single object,
    //                    the filter that was installed last is activated first."
    //   第 2433~2434 行 实现：removeIf(同一个过滤器) 之后 prepend(新过滤器)，
    //                    而 qcoreapplication.cpp 第 1282~1283 行从下标 0 往后遍历 —— 逆序就是这么来的。
    //   第 2390~2391 行 重复安装同一个过滤器不算加一份，而是把它挪到「最后装」那一档（优先级变最高）。
    // ★ 另一条容易看漏的：第 2422~2427 行 —— 被装对象与过滤器**不同线程**时，
    //   installEventFilter 打一句
    //   "QObject::installEventFilter(): Cannot filter events for objects in a different thread."
    //   （本机演示⑦ 原样打出这一行）然后直接 return。这不是「装了但没生效」，是**列表里根本没有它**；
    //   排查「我的 eventFilter 怎么不跑」时，这条比顺序问题更常见，也更难发现。

    // ── 键盘审计：把按键转成信号，但一个都不吃
    Audit keyAudit(QStringLiteral("键盘审计"));
    keyAudit.watchKeys(true);
    KeyForwarder forwarder(&keyAudit);
    window.installEventFilter(&forwarder);
    int keySignals = 0;
    QObject::connect(&keyAudit, &Audit::keySeen,
                     [&keySignals](int key, const QString &text) {
                         ++keySignals;
                         qInfo() << "    信号 keySeen：key =" << key << "text =" << text
                                 << "（事件→信号：系统层语义在这里换成了业务语义）";
                     });

    // 被吃掉的事件也要有业务侧的反馈
    int swallowedCount = 0;
    QObject::connect(&pressEater, &Audit::swallowed,
                     [&swallowedCount](QObject *w, QEvent::Type t) {
                         ++swallowedCount;
                         qInfo() << "    信号 swallowed：" << (w ? w->objectName() : "?")
                                 << "的 type=" << int(t) << " 被吃掉";
                     });

    // ── 本讲新增：合账（原来只靠「退出码 0」，而那只能证明没崩、走完了）
    //    下面每一条「预期」都来自本机这一轮的打印（存档 runlogs/events_03_event_filter.log），
    //    现在把它们钉成硬账：换 Qt 版本、换平台、或者你改了演示代码，哪条先响就去看
    //    那一条对应的事件路径。对不上的行数直接当退出码用（main 里 exit(led.bad)）。
    struct Ledger {
        int rows = 0;           // 一共对了多少行
        int bad = 0;            // 对不上的行数 = 最终退出码
        int d1Clicked = -1;     // 演示①：装了吃掉Press 的按钮，clicked 几次
        int d1Seen = -1;        // 演示①：一次合成点击，过滤器看到几个事件
        int d1Eaten = -1;       // 演示①：其中吃掉几个
        int d2Observer = -1;    // 演示②：装在 btnA 上的过滤器，对 btnB 的增量
        int d2ClickB = -1;      // 演示②：btnB 的 clicked 增量
        int d3BeforeRm = -1;    // 演示③：拆掉之前那一次 clicked 增量
        int d3AfterRm = -1;     // 演示③：拆掉之后那一次 clicked 增量
        int d3Seen = -1;        // 演示③：两次合成点击，过滤器共看到几个事件
        int d4Click = -1;       // 演示④：btnA->click() 编程触发的增量
        int d5KeySignals = -1;  // 演示⑤：keySeen 触发次数
        int d5Seen = -1;        // 演示⑤：「键盘审计」自己的 seen()
        bool d6GuardNull = false;   // 演示⑥：delete watched 之后 QPointer 是否已空
        int d7Refused = -1;     // 演示⑦：主线程往子线程对象上装的过滤器，被调到几次（User+7）
        int d7RefusedB = -1;    // 演示⑦：同上（User+8）
        int d7InThread = -1;    // 演示⑦：子线程自己装的过滤器，被调到几次（User+7，子线程 post）
        int d7InThread2 = -1;   // 演示⑦：同上（User+8，由**主线程** post —— 仍旧在子线程派发）
        int d7AppLevel = -1;    // 演示⑦：qApp 上的应用级过滤器看到这两发共几次
        int d7ThreadChange = -1;// 演示⑦：应用级过滤器看到的 ThreadChange 条数（应当正好 1）
    } led;

    auto check = [&led](const QString &what, bool ok) {
        ++led.rows;
        qInfo().noquote() << QStringLiteral("      %1 %2")
                                 .arg(ok ? QStringLiteral("［对］") : QStringLiteral("［错］"), what);
        if (!ok)
            ++led.bad;
    };

    // ── 演示 ①：装了「吃掉 Press」的按钮，被合成点击两次 → clicked 一次都不发
    QTimer::singleShot(600, [&] {
        qInfo() << "\n=== 演示① true = 吃掉 ===";
        clickCountA = 0;
        const int seenBefore = pressEater.seen();
        syntheticClick(btnA);
        led.d1Clicked = clickCountA;
        led.d1Seen = pressEater.seen() - seenBefore;
        led.d1Eaten = pressEater.eaten();
        qInfo() << "演示① 结果：btnA 的 clicked 触发" << clickCountA << "次（预期 0）；"
                << "过滤器「吃掉Press」看到" << (pressEater.seen() - seenBefore)
                << "个事件，其中吃掉" << pressEater.eaten() << "个。"
                << "按钮失灵的原因不是「样式坏了」，是**事件在到达按钮之前就被 return true 拦掉了**。";
    });

    // ── 演示 ②：同一只按钮上的两个过滤器，顺序与 false 的语义
    QTimer::singleShot(1000, [&] {
        qInfo() << "\n=== 演示② false = 继续传；多过滤器顺序 ===";
        const int o = observer.seen();
        const int bBefore = clickCountB;
        const int aUntouched = clickCountA;
        syntheticClick(btnB);      // btnB 没装目标级过滤器：事件照常走完
        led.d2Observer = observer.seen() - o;
        led.d2ClickB = clickCountB - bBefore;
        qInfo() << "给 btnB 合成点击：目标级「只观察」计数增量 =" << (observer.seen() - o)
                << "（0，符合预期：它只装在 btnA 上）";
        qInfo().noquote() << "btnB 的 clicked 增量 =" << (clickCountB - bBefore) << "（预期 1）；"
                << "btnA 的计数纹丝不动 = " << clickCountA << "（预期仍是 " << aUntouched
                << "）→ 两只按钮各记各的账，A 的数字里不会混进 B 那一下。";
        qInfo() << "此刻全局过滤器共看到" << global.seen() << "个事件，按类型统计见文末总表。";
    });

    // ── 演示 ③：removeEventFilter 之后，同一个过滤器立刻失声
    QTimer::singleShot(1400, [&] {
        qInfo() << "\n=== 演示③ 拆掉过滤器 ===";
        const int aBefore = pressEater.seen();
        const int clicksBefore = clickCountA;
        syntheticClick(btnA);                       // 仍被吃掉
        const int eatenDelta = clickCountA - clicksBefore;
        btnA->removeEventFilter(&pressEater);       // 核对：qobject.h 210 行
        syntheticClick(btnA);                       // 这次该到了
        // ★ 两次点击各算各的增量：原来这里打的是**绝对值** clickCountA，
        //   于是演示② 那下 btnB（旧代码共用计数器）会把它顶成 2，和「预期 1」当街对不上。
        const int afterDelta = clickCountA - clicksBefore - eatenDelta;
        led.d3BeforeRm = eatenDelta;
        led.d3AfterRm = afterDelta;
        led.d3Seen = pressEater.seen() - aBefore;
        qInfo() << "演示③ 结果：拆掉前那一次 clicked 增量 =" << eatenDelta
                << "（预期 0：Press 被过滤器拦走，走不到 clicked）；"
                << "拆掉后那一次增量 =" << afterDelta << "（预期 1）。"
                << "过滤器见事件增量 =" << (pressEater.seen() - aBefore) << "（拦截只发生在被拆之前）。";
    });

    // ── 演示 ④：QAbstractButton::click() 绕过事件层——过滤器拦不住
    QTimer::singleShot(1800, [&] {
        qInfo() << "\n=== 演示④ 编程触发 vs 事件触发 ===";
        const int before = clickCountA;
        btnA->click();          // 核对：qabstractbutton.h 第 85 行 void click();
        led.d4Click = clickCountA - before;
        qInfo() << "btnA->click() 之后 clicked 增量 =" << (clickCountA - before)
                << "（预期 1）→ **过滤器只管事件，不管「直接发信号」**。"
                << "想连这条路也封住，得禁用控件 setEnabled(false)，或在过滤器之外另设策略。";
    });

    // ── 演示 ⑤：合成键盘事件穿过过滤器（只看、不吃）
    QTimer::singleShot(2200, [&] {
        qInfo() << "\n=== 演示⑤ 键盘审计（返回 false，一个都不吃）===";
        // 核对：qevent.h 第 425 行起 QKeyEvent(Type, int key, Qt::KeyboardModifiers,
        //                                   const QString &text = ..., bool autorep = false, quint16 count = 1)
        QKeyEvent k1(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
        QKeyEvent k2(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QString());
        QCoreApplication::sendEvent(btnA, &k1);
        QCoreApplication::sendEvent(&window, &k2);
        led.d5KeySignals = keySignals;
        led.d5Seen = keyAudit.seen();
        qInfo() << "演示⑤ 结果：keySeen 信号累计触发" << keySignals << "次；"
                << "过滤器「键盘审计」看到" << keyAudit.seen() << "个事件、吃掉"
                << keyAudit.eaten() << "个。"
                << "★ 这里 seen()=0 **不是**「它没有拦截权」——真正的原因是**键盘审计压根没被装成过滤器**："
                << "装在窗口上的是转发器 KeyForwarder，它调的是 Audit::filterKeyEvent() 这个**普通成员函数**，"
                << "这条路不经过 eventFilter()，所以计数器一动不动；eaten()=0 才是「没有拦截权」那一层意思。"
                << "（读日志别看错因果。）";
    });

    // ── 演示 ⑥：析构顺序坑
    QTimer::singleShot(2700, [&led] {
        qInfo() << "\n=== 演示⑥ 谁先死：过滤器 vs 被过滤对象 ===";

        auto *watched = new QWidget;                       // 没有父对象 → 自己管自己的命
        watched->setObjectName(QStringLiteral("短命目标"));
        auto *filter = new Audit(QStringLiteral("活得更久的过滤器"));
        watched->installEventFilter(filter);

        QPointer<QWidget> watchedGuard = watched;           // 核对：QtCore/qpointer.h
                                                            // 第 86 行 isNull()、第 88 行 explicit operator bool()

        // (a) 删掉被过滤对象：Qt 会自动把它身上装的过滤器摘掉（文档语义，
        //     QObject 析构会清理「装在我身上的过滤器」与「我过滤过的对象」两张表）。
        delete watched;
        led.d6GuardNull = watchedGuard.isNull();
        qInfo() << "  delete watched 之后：QPointer 是否已 null =" << watchedGuard.isNull()
                << "（QObject::destroyed 驱动的自动置空，见第贰阶段第 7 讲）";

        // (b) 但你自己的成员指针 / 你缓存的裸指针，**不会**跟着变 null。
        //     这就是最常见的事故：过滤器里存了 `QObject *m_lastTarget = watched;`，
        //     目标死了，下件事件时你拿它去 objectName() → 悬空读。
        //     规矩：过滤器里长期持有的目标指针，一律用 QPointer<QObject>，并且每次判空。
        // (c) 反过来（先删过滤器）也是安全的：事件不会再交给它。
        delete filter;

        // (d) 在 eventFilter 里 delete 被过滤对象 —— 禁手。
        //     正确姿势是 deleteLater()（qobject.h 第 364 行）：它只投一个
        //     QEvent::DeferredDelete（qcoreevent.h 第 105 行），等回到循环安全点才真删。
        //     反面代码留在这儿给你看一眼，别跑：
        //     bool eventFilter(QObject *w, QEvent *e) override { delete w; return true; }   // ❌

        // (e) 挂在**临时对象**上的过滤器会静默失效——新手最常见的「我的过滤器怎么没反应」：
        {
            Audit temp(QStringLiteral("临时"));
            temp.setEatTypes({ QEvent::MouseButtonPress });
            auto *btn = new QPushButton(QStringLiteral("临时过滤"), nullptr);
            btn->installEventFilter(&temp);    // temp 是栈对象
            qInfo() << "  （模拟）把过滤器装在栈上对象：作用域一结束，install 关系即被 Qt 清理";
            btn->deleteLater();
        }
        qInfo() << "  结论：**过滤器必须比它要过滤的事件活得久**；两端任一先析构，Qt 都会自动断开，"
                << "但你自己缓存的裸指针得靠 QPointer 自保。";
    });

    // ── 演示 ⑦：过滤器撞上「线程边界」——把本讲前面两条「源码读出来」的结论跑成数字
    QTimer::singleShot(3100, [&global, &led] {
        qInfo() << "\n=== 演示⑦ 线程边界：qApp 上的过滤器看不见子线程 ===";

        // 计数器用文件级静态、过滤器也做成静态/有主对象：本 lambda 结束时它们还在，
        // 万一哪天 Qt 改了「跨线程不许装」的行为，也不会变成悬空指针事故。
        static std::atomic<int> refusedHits{ 0 };    // 主线程往子线程对象上装的过滤器：User+7
        static std::atomic<int> refusedHits2{ 0 };   //                         同上：User+8
        static std::atomic<int> inThreadHits{ 0 };   // 子线程自己装的过滤器：User+7
        static std::atomic<int> inThreadHits2{ 0 };  //                 同上：User+8（主线程 post 的那发）
        static CountingFilter refused(&refusedHits, &refusedHits2); // 主线程构造 → 归属主线程

        QThread workerThread;
        QObject probe;
        probe.setObjectName(QStringLiteral("子线程探针"));
        probe.moveToThread(&workerThread);

        qInfo() << "  (a) 主线程给子线程对象装过滤器 → 期望：一句 qWarning，而且**不装**";
        probe.installEventFilter(&refused);          // ← qobject.cpp 2422~2427 行

        // (c) **主线程**给「户口已在子线程」的对象 postEvent 一发 User+8：
        //     按第 2 讲的规矩，投的是**目标线程**的队列，派发也在目标线程发生 —— 这一发
        //     和 (b) 的区别只在「谁按的门铃」，收信人还是同一条线程。
        QCoreApplication::postEvent(&probe, new QEvent(kProbeEventType2));

        // (b) 装过滤器这件事必须由**目标所在的那个线程**来做。started 是子线程发出来的，
        //     这里用「无 context 的 functor 连接」= 直连，于是这段代码跑在子线程里。
        //     注意捕获列表：inThreadHits/inThreadHits2 是 static 存储期，**不需要也不能**按引用捕获
        //     （GCC 会警告 "capture of variable with non-automatic storage duration"）。
        QObject::connect(&workerThread, &QThread::started, [&probe] {
            auto *inThread = new CountingFilter(&inThreadHits, &inThreadHits2, &probe); // 父对象=probe：同线程、随它析构
            probe.installEventFilter(inThread);      // 同线程 → 装得上
            QCoreApplication::postEvent(&probe, new QEvent(kProbeEventType));
        });

        workerThread.start();
        QThread::msleep(250);                        // 让子线程的事件循环把这两发处理掉
        workerThread.quit();
        const bool joined = workerThread.wait(2000);

        led.d7Refused = refusedHits.load();
        led.d7RefusedB = refusedHits2.load();
        led.d7InThread = inThreadHits.load();
        led.d7InThread2 = inThreadHits2.load();
        led.d7AppLevel = global.countByType().value(int(kProbeEventType))
                       + global.countByType().value(int(kProbeEventType2));
        led.d7ThreadChange = global.countByType().value(int(QEvent::ThreadChange));

        qInfo() << "  (b) 子线程里自装过滤器 + 子线程 post 一发 User+7("
                << int(kProbeEventType) << ")；(c) 主线程再 post 一发 User+8("
                << int(kProbeEventType2) << ")";
        qInfo() << "  回收：子线程是否已退出 =" << joined;
        qInfo().noquote() << QStringLiteral(
              "  数：跨线程那次装＝%1/%2 次（期望 0/0，压根没装上）；"
              "子线程自装＝%3/%4 次（期望 1/1，两发都归它 —— 主线程 post 的那发也是在这条线程派发的）；"
              "装在 qApp 上的应用级过滤器看到这两发＝%5 次（期望 0，它只在主线程跑）")
              .arg(led.d7Refused).arg(led.d7RefusedB).arg(led.d7InThread).arg(led.d7InThread2)
              .arg(led.d7AppLevel);
        qInfo() << "  → 上面那句「应用级过滤器只在主线程」从此**不再是读源码读出来的**，是这一行打印出来的。";
        qInfo() << "  注：日志里那条「过滤器 全局 看到：目标=子线程探针 type=22」**不是反例**。"
                << "22 = QEvent::ThreadChange（qcoreevent.h 第 87 行），它在"
                << "QObjectPrivate::moveToThread_helper()（qobject.cpp 第 1794~1798 行）里"
                << "是先 sendEvent 再改户口 —— 派发那一刻对象**还在主线程**，所以应用级过滤器当然看得到。"
                << "真正在子线程里派发的那一发 User+7，应用级过滤器一次都没看到（上面第三个数 = 0）。"
                << "本轮应用级过滤器共看到 ThreadChange =" << led.d7ThreadChange << "条。";
    });

    // ── 总表 + 合账 + 退出
    QTimer::singleShot(4200, [&global, &pressEater, &observer, &keyAudit, &led, &check] {
        qInfo() << "\n=== 结论 ===";
        qInfo() << "被过滤 / 被吃掉事件计数（目标级）：";
        qInfo() << "  吃掉Press：看到" << pressEater.seen() << "吃掉" << pressEater.eaten()
                << "放过" << pressEater.passed();
        qInfo() << "  只观察  ：看到" << observer.seen() << "吃掉" << observer.eaten()
                << "放过" << observer.passed();
        qInfo() << "  键盘审计：看到" << keyAudit.seen() << "吃掉" << keyAudit.eaten()
                << "（这里必须是 0：它不是过滤器，事件是靠普通函数调用转交给它的）";
        qInfo() << "全局过滤器：看到" << global.seen() << "个事件，类型分布（type → 次数）："
                << global.countByType();

        qInfo() << "\n=== 合账（每一条都对应本机这一轮的一个数字）===";
        check(QStringLiteral("演示① clicked 0 次：Press 在到达按钮之前就被 return true 拦走"), led.d1Clicked == 0);
        check(QStringLiteral("演示① 一次合成点击 = 2 个事件（Press+Release 都过它手）"), led.d1Seen == 2);
        check(QStringLiteral("演示① 其中只有 Press 被吃掉 → eaten = 1"), led.d1Eaten == 1);
        check(QStringLiteral("演示② 装在 btnA 上的过滤器，对 btnB 的增量 = 0"), led.d2Observer == 0);
        check(QStringLiteral("演示② btnB 的 clicked 增量 = 1（false = 继续传）"), led.d2ClickB == 1);
        check(QStringLiteral("演示③ 拆掉之前那次 clicked 增量 = 0"), led.d3BeforeRm == 0);
        check(QStringLiteral("演示③ 拆掉之后那次 clicked 增量 = 1"), led.d3AfterRm == 1);
        check(QStringLiteral("演示③ 两次合成点击共看到 2 个事件（拆掉之后那一次不再过它手）"), led.d3Seen == 2);
        check(QStringLiteral("演示④ btnA->click() 绕过事件层 → 增量照样是 1"), led.d4Click == 1);
        check(QStringLiteral("演示⑤ 两个按键都翻成了 keySeen（含一次 btnA 未收下→往父窗口传播）"), led.d5KeySignals == 2);
        check(QStringLiteral("演示⑤ 键盘审计 seen() = 0：转交走函数调用，不经过 eventFilter()"), led.d5Seen == 0);
        check(QStringLiteral("演示⑥ delete watched 之后 QPointer 自动置空"), led.d6GuardNull);
        check(QStringLiteral("演示⑦ 跨线程装的过滤器被调到 0/0 次（installEventFilter 直接 return，没装）"),
              led.d7Refused == 0 && led.d7RefusedB == 0);
        check(QStringLiteral("演示⑦ 子线程里自装的过滤器正好各收到 1 次（含主线程 post 的那一发 User+8）"),
              led.d7InThread == 1 && led.d7InThread2 == 1);
        check(QStringLiteral("演示⑦ 应用级过滤器（装在 qApp）看到子线程这两发 = 0 次"), led.d7AppLevel == 0);
        check(QStringLiteral("演示⑦ ThreadChange 被应用级过滤器看到正好 1 条：那是 moveToThread 改户口**之前**"
                             "在主线程发的（qobject.cpp 1794~1798），不是子线程的反例"),
              led.d7ThreadChange == 1);
        // 应用级的绝对条数取决于窗口系统与平台事件，不钉死；只钉「它在跑」这个结构事实
        check(QStringLiteral("应用级过滤器确实在收主线程事件（具体条数随平台与本轮事件走，见上面总表，故只断言 > 0）"),
              global.seen() > 0);

        qInfo().noquote() << QStringLiteral("────【合账】共 %1 行，对不上 %2 行 → exit(%2) ────")
                                 .arg(led.rows).arg(led.bad);
        if (led.bad > 0)
            qWarning() << "有行对不上：先看上面标［错］的那条，别改期望值，去查那条事件路径。";
        // 用 exit(条数) 而不是 quit()：quit() 只投一个 Quit 事件、退出码恒 0，
        // 那样 ctest 永远绿，账本就没牙了（第 1 讲第 2165~2185 行 vs 第 1549~1569 行）。
        QCoreApplication::exit(led.bad);
    });

    return app.exec();
}

// ============================================================================
// 附：什么时候用哪一层（师傅的取舍表）
// ============================================================================
/*
 *  需求                                   推荐手段                     理由
 *  ---------------------------------------------------------------------------
 *  给「现成控件」加行为，不想 subclass     installEventFilter           可装可拆、同一份代码到处复用
 *  一个类里统一处理很多种事件              重写 event()                 层 2，能看到全部类型
 *  只关心一类事件（比如滚轮）              重写 wheelEvent()            层 3，语义最清楚
 *  框架级埋点 / 审计主线程所有事件         重写 QApplication::notify     唯一收口点，代价是独占
 *  跨对象「广播我按了键」                  过滤器里发信号（本讲做法）     系统层语义 → 应用层语义的转换点
 *  屏蔽某个控件的一切输入                  setEnabled(false)            连 click() 这种编程触发也挡住
 *
 * 三条纪律：
 *  ① 过滤器的返回值是**权力**，不是装饰。每写一个 return true，都要能说出
 *     「哪个默认行为因此不发生」——说不出来就别 return true。
 *  ② 全局过滤器（挂 qApp）在**主线程的每个事件**上都要跑（其他线程的事件不经过它，
 *     见 qcoreapplication.cpp 第 1257~1277 与 1311~1317 行），别在里面做字符串拼接、
 *     找控件、写日志文件。埋点请攒计数，退出时一次性打表（本讲就是这么写的）。
 *  ③ 别在过滤器里 delete 目标，也不要在里面开事件循环（QEventLoop::exec）——
 *     那是第 1 讲的重入事故现场，而过滤器比 handler 更容易被反复触发。
 *
 * ——— 老师傅的话 ———
 *   徒弟，过滤器这东西，新手嫌它绕，老手拿它当瑞士军刀。差别在哪？在「谁该拿到
 *   这个事件」这个问题上，你有没有想过第二次。
 *
 *   新手写完控件 behavior 就开始 subclass：给每个按钮造一个 MyButton、MyCheckedButton、
 *   NoWheelButton……三个月后工程里躺着 20 个只改了一件事的派生类，而且它们互相不能叠加
 *   （一个按钮既想「无滚轮」又想「长按重复」时，派生树就崩了）。
 *
 *   师傅写过滤器：一个 NoWheelFilter，往任何控件上一贴就走；要几条规则就贴几个，
 *   组合是免费的。这就是「继承 vs 组合」在事件系统里最实际的一次落地。
 *
 *   但过滤器也是 Qt 里最容易「调不出 bug」的地方，因为它的失败是**静默**的：
 *   你的 eventFilter 没被调，多半是装错对象（installEventFilter 的主语是被过滤者！）、
 *   过滤器对象已经死了、装到了子控件而事件发给了父控件，或者——最容易看漏的一条——
 *   **过滤器和目标不在同一线程**：qobject.cpp 第 2422~2427 行打一句 qWarning 就 return，
 *   列表里根本没进这个过滤器，之后无论怎么点都不会调（本讲没有演示这一条，因为要再造一个
 *   线程才能触发；它是**源码读出来的**，不是本机跑出来的，按本讲的老规矩标明出处）。
 *   所以本讲把每一层都打了计数器——**先让「有没有被调到」变成一个能打印的数字，
 *   再讨论行为对不对。**
 *   养成这个习惯，你在 Qt 里少熬一半的夜。
 *
 * ——— 小练习 ———
 *   1. 把 pressEater 的 eatTypes 换成 { QEvent::MouseButtonRelease }，重跑演示①：
 *      clicked 变成几次？pressed 会发吗？（核对 qabstractbutton.h 90~93 行）
 *      由此说明「按钮的 clicked 是在 release 路径上发出的」这件事。
 *   2. 写一个 WheelKiller 过滤器，挂在 qApp 上，把所有控件的 QEvent::Wheel
 *      （qcoreevent.h 第 92 行）吃掉；然后用代码构造 QWheelEvent
 *      （qevent.h 第 282 行起的构造签名）投给按钮，打印「看到 N 个 / 吃掉 N 个」。
 *      再把 return true 改成 return false，看全局计数器的数字怎么变。
 *   3. 故意制造一次析构事故：把演示⑥ 的 `delete watched;` 换到 `delete filter;` 之后，
 *      并把 Audit 改成缓存 `QObject *m_last = watched;` 且在 eventFilter 末尾打
 *      m_last->objectName()。观察（并解释）它为什么**常常不立刻崩**——然后把它改对
 *      （换成 QPointer<QObject> 并判空）。
 *   4. 演示⑦ 已经把「应用级过滤器只在主线程跑」和「跨线程装过滤器 = 没装」跑成数字了。
 *      现在动它一处，猜哪个数会变，再跑一遍验证：
 *        (i)  把 (c) 那发 User+8 的 postEvent 从主线程挪进 started 的 lambda 里；
 *        (ii) 把 CountingFilter 的构造从子线程挪到主线程（也就是把 new 写在 lambda 外面）；
 *        (iii)把 workerThread.quit() 之前的 msleep(250) 改成 0。
 *      (ii) 一定让「子线程自装 = 1/1」那一行变红（过滤器户口在主线程 → 装不上）；
 *      (iii) 是竞态：主线程一 sleep 到底就 quit()，子线程可能还没来得及跑 started 那一档，
 *            数字会在 0/1 之间飘——多跑几轮，你会亲眼看到「不稳定的测试」长什么样。
 *      改错了别去改期望值：期望值是这一讲的脸面。
 */
// ============================================================================

// .cpp 里定义了 Q_OBJECT 类（Audit / KeyForwarder），AUTOMOC 会为本文件生成
// 03_event_filter.moc，必须在**所有 Q_OBJECT 类定义之后**包含它，
// 否则这两个类的 metaObject/qt_static_metacall/信号实现会找不到（链接期报 vtable）。
#include "03_event_filter.moc"
