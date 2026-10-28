// ============================================================================
// 第 3 讲 · 事件过滤器（event filter）
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   事件过滤器是「**不继承就改行为**」的唯一正统手段。本讲把四件事钉死：
//     1) installEventFilter 挂在**目标对象**上；qApp->installEventFilter 挂全局；
//        两者其实都在 QApplication::notify 内部被调用（第 2 讲那条链的第 1.5 层）。
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
//   · 本机当前无法编译链接 Qt 运行时，本示例**未在本机实测编译**；
//     每个签名都对着 C:\msys64\ucrt64\include\qt6 的真实头文件核对过（注释给了文件与行号）。
//   · 本示例无人值守跑完：约 4 秒自动演示 + qInfo() 打结论 + QCoreApplication::quit()。
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
// ============================================================================
// Audit 同时是「普通过滤器」，所以 KeyPress 既进统计也可能要发 keySeen。
// 这里用一个薄封装：在 eventFilter 之外不再挂钩，直接把 KeyPress 交给 filterKeyEvent。
class KeyForwarder : public QObject
{
    Q_OBJECT
public:
    explicit KeyForwarder(Audit *target) : m_target(target) {}

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        // 先让被过滤对象照常收到事件（返回 false），但顺手把按键转成信号
        m_target->filterKeyEvent(event);
        Q_UNUSED(watched)
        return false;
    }

private:
    Audit *m_target;
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
    QObject::connect(btnA, &QPushButton::clicked, [&clickCountA] { ++clickCountA; });
    QObject::connect(btnB, &QPushButton::clicked, [&clickCountA] { ++clickCountA; });

    // ── 全局过滤器：挂在 qApp 上，看得见整个程序的事件（除了 notify 之前的）
    // 核对：qobject.h 第 209 行 —— qApp->installEventFilter(&global) 意为
    // 「任何对象的事件，先给 &global 过一遍」
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

    // ── 演示 ①：装了「吃掉 Press」的按钮，被合成点击两次 → clicked 一次都不发
    QTimer::singleShot(600, [&] {
        qInfo() << "\n=== 演示① true = 吃掉 ===";
        clickCountA = 0;
        const int seenBefore = pressEater.seen();
        syntheticClick(btnA);
        qInfo() << "演示① 结果：btnA 的 clicked 触发" << clickCountA << "次（预期 0）；"
                << "过滤器「吃掉Press」看到" << (pressEater.seen() - seenBefore)
                << "个事件，其中吃掉" << pressEater.eaten() << "个。"
                << "按钮失灵的原因不是「样式坏了」，是**事件在到达按钮之前就被 return true 拦掉了**。";
    });

    // ── 演示 ②：同一只按钮上的两个过滤器，顺序与 false 的语义
    QTimer::singleShot(1000, [&] {
        qInfo() << "\n=== 演示② false = 继续传；多过滤器顺序 ===";
        const int o = observer.seen();
        syntheticClick(btnB);      // btnB 没装目标级过滤器：事件照常走完
        qInfo() << "给 btnB 合成点击：目标级「只观察」计数增量 =" << (observer.seen() - o)
                << "（0，符合预期：它只装在 btnA 上）";
        qInfo() << "此刻全局过滤器共看到" << global.seen() << "个事件，按类型统计见文末总表。";
    });

    // ── 演示 ③：removeEventFilter 之后，同一个过滤器立刻失声
    QTimer::singleShot(1400, [&] {
        qInfo() << "\n=== 演示③ 拆掉过滤器 ===";
        const int aBefore = pressEater.seen();
        const int clicksBefore = clickCountA;
        syntheticClick(btnA);                       // 仍被吃掉
        btnA->removeEventFilter(&pressEater);       // 核对：qobject.h 210 行
        syntheticClick(btnA);                       // 这次该到了
        qInfo() << "演示③ 结果：拆掉前 clicked 增量 =" << (clickCountA - clicksBefore)
                << "；拆掉后 =" << clickCountA << "（预期 1）。"
                << "过滤器见事件增量 =" << (pressEater.seen() - aBefore) << "（拦截只发生在被拆之前）。";
    });

    // ── 演示 ④：QAbstractButton::click() 绕过事件层——过滤器拦不住
    QTimer::singleShot(1800, [&] {
        qInfo() << "\n=== 演示④ 编程触发 vs 事件触发 ===";
        const int before = clickCountA;
        btnA->click();          // 核对：qabstractbutton.h 第 85 行 void click();
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
        qInfo() << "演示⑤ 结果：keySeen 信号累计触发" << keySignals << "次；"
                << "过滤器「键盘审计」看到" << keyAudit.seen() << "个事件、吃掉"
                << keyAudit.eaten() << "个（0 = 它没有拦截权，只做转译）。";
    });

    // ── 演示 ⑥：析构顺序坑
    QTimer::singleShot(2700, [] {
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

    // ── 总表 + 退出
    QTimer::singleShot(3400, [&global, &pressEater, &observer, &keyAudit] {
        qInfo() << "\n=== 结论 ===";
        qInfo() << "被过滤 / 被吃掉事件计数（目标级）：";
        qInfo() << "  吃掉Press：看到" << pressEater.seen() << "吃掉" << pressEater.eaten()
                << "放过" << pressEater.passed();
        qInfo() << "  只观察  ：看到" << observer.seen() << "吃掉" << observer.eaten()
                << "放过" << observer.passed();
        qInfo() << "  键盘审计：看到" << keyAudit.seen() << "吃掉" << keyAudit.eaten();
        qInfo() << "全局过滤器：看到" << global.seen() << "个事件，类型分布（type → 次数）："
                << global.countByType();
        qInfo() << "一句话：**true = 吃掉（目标的 event() 不会被调用），false = 继续传**；"
                << "过滤器是「不继承就改行为」的正道，但它只管事件层，管不了直接发信号。";
        QCoreApplication::quit();
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
 *  框架级埋点 / 全程序审计所有事件         重写 QApplication::notify     唯一收口点，代价是独占
 *  跨对象「广播我按了键」                  过滤器里发信号（本讲做法）     系统层语义 → 应用层语义的转换点
 *  屏蔽某个控件的一切输入                  setEnabled(false)            连 click() 这种编程触发也挡住
 *
 * 三条纪律：
 *  ① 过滤器的返回值是**权力**，不是装饰。每写一个 return true，都要能说出
 *     「哪个默认行为因此不发生」——说不出来就别 return true。
 *  ② 全局过滤器（挂 qApp）在**每个事件**上都要跑，别在里面做字符串拼接、
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
 *   过滤器对象已经死了、或者装到了子控件而事件发给了父控件。所以本讲把每一层都打了
 *   计数器——**先让「有没有被调到」变成一个能打印的数字，再讨论行为对不对。**
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
 */
// ============================================================================

// .cpp 里定义了 Q_OBJECT 类（Audit / KeyForwarder），AUTOMOC 会为本文件生成
// 03_event_filter.moc，必须在**所有 Q_OBJECT 类定义之后**包含它，
// 否则这两个类的 metaObject/qt_static_metacall/信号实现会找不到（链接期报 vtable）。
#include "03_event_filter.moc"
