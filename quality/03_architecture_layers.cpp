// ============================================================================
// 第 3 讲 · 分层架构：让依赖只朝一个方向流
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   前两讲讲「怎么验」，这一讲讲「代码该长什么样才验得动」。
//   大部分 Qt 项目烂掉不是因为不会用控件，而是因为**依赖的方向乱了**：
//     · 业务逻辑里 #include <QWidget>，于是业务只能在有窗口的进程里跑；
//     · 界面直接 new QSqlDatabase，于是换存储要改界面，写测试要连数据库；
//     · 一个 QDialog 里既查库、又算业务、又拼 SQL，谁都改不动。
//   本讲把四层立起来，并且**每一层都用运行期证据钉住**，而不是靠一句「要有架构」：
//
//        ┌──────────────────────────────────────────────┐
//        │  UI 层        TodoPage : QWidget              │  只认识 Service
//        ├──────────────────────────────────────────────┤
//        │  应用层        TodoService : QObject          │  只认识 ITodoStore（接口）
//        ├──────────────────────────────────────────────┤
//        │  领域层        TodoItem / ITodoStore          │  只认 QtCore 的值类型
//        ├──────────────────────────────────────────────┤
//        │  基础设施层    Memory / Json / Audited / Fake  │  实现 ITodoStore
//        └──────────────────────────────────────────────┘
//        依赖箭头**只能朝下**：UI→应用→领域←基础设施（基础设施也朝领域指）
//
//   七个阶段，一个阶段一条硬证据：
//     ① 分层表 + 环检测：把架构图写成**可执行的**，跑一遍就知道有没有反向边
//     ② 运行期认层：inherits() 与 dynamic_cast 当场分辨谁是 QWidget、谁是纯接口
//     ③ 元对象审计：扫描 Service 的每个方法签名，参数里不许出现任何 UI 类型
//     ④ 依赖倒置：同一份 Service 换内存/换 JSON 文件，行为逐字相同
//     ⑤ 组合根：谁 new 谁负责；**销毁顺序必须与构造顺序相反**
//     ⑥ 装饰器：审计/日志/指标挂在外层，Service 一行不改
//     ⑦ 替身：无 I/O、无事件循环地测业务，并验证「存不下去就别改内存」
//
// 对应构建目标名：quality_03_architecture_layers
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(quality_03_architecture_layers 03_architecture_layers.cpp)
//   target_link_libraries(quality_03_architecture_layers PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)                    # 本文件有 Q_OBJECT 类，需要 moc
//
// 运行前须知：
//   · **全程同步**，没有 app.exec()：所有事都在 main 里按顺序做完，
//     所以也没有「总闸」定时器 —— 一个连事件循环都不跑的程序不会挂住。
//   · 可以无头跑：`quality_03_architecture_layers -platform offscreen`
//     （CTest 就是这么跑的：QT_QPA_PLATFORM=offscreen）。
//     UI 层只被**构造**，不 show()、不画像素 —— 本讲要证的是依赖方向，不是长相。
//   · 唯一的磁盘动作：把 JSON 存储写进 QStandardPaths::TempLocation 下的一个小文件，
//     阶段末尾删掉。不联网，不碰用户配置。
//   · 未实测声明：本机当前无法链接 Qt 运行时，本示例**未链接、未运行**，
//     仅通过 moc + g++ -fsyntax-only（-std=c++17 -Wall -Wextra）做编译期核对。
//     文中所有「期望」后面是推理，不是跑出来的数字。
// ============================================================================

#include <QApplication>              // QtWidgets/qapplication.h：QWidget 的前置条件
#include <QWidget>                   // QtWidgets/qwidget.h：UI 层基类
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <QObject>                   // QtCore/qobject.h
#include <QString>
#include <QStringList>
#include <QVector>
#include <QList>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>                 // 先写临时文件再原子替换：写坏一半的文件比没写更糟
#include <QFile>                     // 读侧仍然用 QFile
#include <QDir>
#include <QStandardPaths>
#include <QMetaObject>               // qobjectdefs.h 237/238/257/272 行
#include <QMetaMethod>               // qmetaobject.h 26/27/32/37 行
#include <QDebug>

#include <functional>                // std::function：环检测里的递归 lambda
#include <memory>
#include <utility>

// ============================================================================
// 一、核对签名（本机 C:\msys64\ucrt64\include\qt6）
// ============================================================================
// 本讲用到的三个「运行期能问出真话」的 API：
//
// QObject::inherits —— qobject.h 第 358~361 行，**行内实现**：
//     inline bool inherits(const char *classname) const
//     { return const_cast<QObject *>(this)->qt_metacast(classname) != nullptr; }
//   它是一条**字符串查类链**的路径：qt_metacast 由 moc 生成，沿着元对象的
//   superClass() 一路比对类名。所以 inherits("QWidget") 问的不是「它有没有
//   某个函数」，而是「它的继承链里有没有叫 QWidget 的这一环」。
//   ← 这正是「认层」最便宜的运行期手段，不用 RTTI、不用知道具体类型。
//
// QObject::metaObject / QMetaObject —— qobjectdefs.h：
//   第 237 行 const char *className() const;
//   第 238 行 const QMetaObject *superClass() const;   ← 一路往上走的钩子
//   第 257 行 int methodCount() const;                 ← **包含继承来的方法**
//   第 272 行 QMetaMethod method(int index) const;
//
// QMetaMethod —— qmetaobject.h：
//   第 26 行 QByteArray methodSignature() const;   形如 "add(QString)"
//   第 27 行 QByteArray name() const;
//   第 32 行 int parameterCount() const;
//   第 34 行 QMetaType parameterMetaType(int index) const;
//   第 37 行 QByteArray parameterTypeName(int index) const;   ← 本讲用它做审计
//   第 42~47 行 enum MethodType { Method, Signal, Slot, Constructor };
//   ⚠ MethodType 里 Signal / Slot 的值**只有在声明了 QT_MOC_COMPAT 之类才会
//     被用到**，所以别拿 methodType()==Signal 当「它是不是信号」的判据；
//     要判信号看 QMetaMethod::methodSignature 与 QMetaObject::indexOfSignal。
//     本讲不判信号，只判**参数类型**，所以不受这条影响。
//
// 为什么不直接看源码里有没有 #include <QWidget> 来判断分层？
//   因为「有没有 include」是编译期的、只在编译单元里有意义；
//   而「这个对象认不认识界面」是运行期的事实，能被程序自己报告出来。
//   本讲的做法：**让程序审计自己**，结论落到退出码上。
//
// 跨讲链接：
//   · metaobject 第 6 讲：Q_PROPERTY 与元对象反射，本讲的 parameterTypeName 同源。
//   · quality 第 4 讲：所有权与生命周期，本讲第 ⑤ 节的销毁顺序那讲会再深挖。
//   · quality 第 5 讲：错误处理，本讲第 ⑦ 节的「保存失败要回滚」是它的应用面。
// ============================================================================

// ============================================================================
// 二、核对台账：本讲的「硬账 / 观察」约定
// ----------------------------------------------------------------------------
// 为什么每个讲义都自带一套台账？因为讲义要能**自己判自己**：
// 数字对不上就让进程以非 0 退出，谁跑谁知道哪一行出了岔子。
//   Hard：教材承诺过的事实，不符就是 bug（错一个整讲算失败）
//   Soft：官方文档写了、但要读 .cpp 才能确认实现细节的策略（两种结果都不算错）
//   Obs ：只测一次、只报数字，不下结论（性能、顺序之类）
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

static void checkStr(const QString &label, const QString &got, const QString &want)
{
    verdict(Hard, label, got == want, QStringLiteral("「%1」（期望「%2」）").arg(got, want));
}

static void checkBool(const QString &label, bool got, bool want)
{
    verdict(Hard, label, got == want,
            QStringLiteral("实测 %1（期望 %2）")
                .arg(got ? QStringLiteral("true") : QStringLiteral("false"),
                     want ? QStringLiteral("true") : QStringLiteral("false")));
}

// 前提不成立就整节作废：宁可少核，也不要拿一个错的前提去算出一堆假结论。
static bool premise(const QString &label, bool ok, const QString &detail)
{
    verdict(Hard, QStringLiteral("前提：") + label, ok, detail);
    if (!ok) {
        ++gLedger.stalled;
        qWarning().noquote()
            << QStringLiteral("  ……… 本节前提没成立，余下的核对全部跳过（这一节记作作废）");
    }
    return ok;
}

// ============================================================================
// 三、领域层：只认值类型和纯接口
// ----------------------------------------------------------------------------
// 「领域层」在这里就是两个东西：
//   TodoItem  —— 一个普通结构体。注意它**没有**继承 QObject、**没有** Q_PROPERTY。
//                领域对象不需要信号，需要信号的是「服务」，不是「数据」。
//   ITodoStore —— 一个纯接口，**也不是 QObject**（见第 ④ 节末尾的 running 证据：
//                dynamic_cast<QObject*> 返回空）。这一点很重要：接口一旦继承 QObject，
//                你就被迫把它当成 Qt 对象来管生命周期，替身也更难写。
// ============================================================================
struct TodoItem {
    qint64 id = 0;
    QString title;
    bool done = false;
};

static QString describeTodo(const TodoItem &it)
{
    return QStringLiteral("%1|%2|%3")
        .arg(it.id)
        .arg(it.title, it.done ? QStringLiteral("done") : QStringLiteral("todo"));
}

static QString describeAll(const QVector<TodoItem> &items)
{
    QStringList parts;
    parts.reserve(items.size());
    for (const TodoItem &it : items)
        parts << describeTodo(it);
    return parts.isEmpty() ? QStringLiteral("<空>") : parts.join(QStringLiteral(" ; "));
}

class ITodoStore
{
public:
    virtual ~ITodoStore() = default;

    virtual QVector<TodoItem> loadAll() const = 0;
    virtual bool saveAll(const QVector<TodoItem> &items) = 0;
    virtual QString backendName() const = 0;
    virtual qint64 callCount() const = 0;   // 供本讲数「到底调了几次 I/O」
};

// ============================================================================
// 四、应用层：TodoService，只认 ITodoStore
// ----------------------------------------------------------------------------
// 一条必须练成肌肉记忆的规矩：**存不进去，就别改内存**。
// add() 里先把整份列表复制出来、加上新项、交给 store 落盘，成功了才赋值回 m_items。
// 这样做有两个代价、三个好处：
//   代价：每次修改都复制一遍列表（本讲规模下无所谓；真到十万行要改成增量）。
//   好处：① 存储失败时自动回滚，内存与磁盘永远一致；
//         ② 界面只会收到「成功之后」的一次 changed，不会先闪一下再收回；
//         ③ 这个类不需要知道 store 是内存、是文件、还是数据库 —— 它只认接口。
// ============================================================================
class TodoService : public QObject
{
    Q_OBJECT
public:
    explicit TodoService(ITodoStore *store, QObject *parent = nullptr)
        : QObject(parent), m_store(store) {}

    // 只给审计用：本讲要证明「Service 记的是一个接口指针，而不是具体类型」。
    const ITodoStore *store() const { return m_store; }
    QVector<TodoItem> items() const { return m_items; }

public Q_SLOTS:
    void reload()
    {
        m_items = m_store->loadAll();
        m_nextId = 1;
        for (const TodoItem &it : m_items)
            m_nextId = qMax(m_nextId, it.id + 1);
        Q_EMIT changed(m_items);
    }

    void add(const QString &title)
    {
        const QString trimmed = title.trimmed();
        if (trimmed.isEmpty()) {
            Q_EMIT failed(QStringLiteral("标题不能为空"));
            return;
        }
        QVector<TodoItem> next = m_items;
        TodoItem it;
        it.id = m_nextId;
        it.title = trimmed;
        next.append(it);
        if (!m_store->saveAll(next)) {          // ← 先落盘
            Q_EMIT failed(QStringLiteral("保存失败，本次修改已丢弃"));
            return;                             // ← 内存一个字都没动
        }
        ++m_nextId;
        m_items = next;                         // ← 落盘成功才切换
        Q_EMIT changed(m_items);
    }

    void toggle(qint64 id)
    {
        QVector<TodoItem> next = m_items;
        bool hit = false;
        for (TodoItem &it : next) {
            if (it.id == id) {
                it.done = !it.done;
                hit = true;
            }
        }
        if (!hit) {
            Q_EMIT failed(QStringLiteral("没有 id=%1 这一条").arg(id));
            return;
        }
        if (!m_store->saveAll(next)) {
            Q_EMIT failed(QStringLiteral("保存失败，本次修改已丢弃"));
            return;
        }
        m_items = next;
        Q_EMIT changed(m_items);
    }

Q_SIGNALS:
    void changed(const QVector<TodoItem> &items);
    void failed(const QString &reason);

private:
    ITodoStore *m_store;
    QVector<TodoItem> m_items;
    qint64 m_nextId = 1;
};

// ============================================================================
// 五、UI 层：TodoPage 只认识 TodoService
// ----------------------------------------------------------------------------
// 它不知道有 ITodoStore 这回事，更不知道数据是 JSON 还是内存。
// 于是同一份界面代码能挂在两个完全不同的后端上 —— 第 ③ 节会用两条同样的
// 业务脚本跑出两张一模一样的界面文字，这就是「依赖方向对了」的行为证据。
// ============================================================================
class TodoPage : public QWidget
{
    Q_OBJECT
public:
    explicit TodoPage(TodoService *service, QWidget *parent = nullptr)
        : QWidget(parent), m_service(service)
    {
        auto *layout = new QVBoxLayout(this);            // 有 parent，随本页一起销毁
        m_summary = new QLabel(QStringLiteral("（尚未加载）"), this);
        m_addButton = new QPushButton(QStringLiteral("加一条"), this);
        layout->addWidget(m_summary);
        layout->addWidget(m_addButton);

        QObject::connect(m_addButton, &QPushButton::clicked, this,
                         [this]() { m_service->add(QStringLiteral("来自界面的新条目")); });
        QObject::connect(m_service, &TodoService::changed, this,
                         [this](const QVector<TodoItem> &items) { render(items); });
        QObject::connect(m_service, &TodoService::failed, this,
                         [this](const QString &reason) {
                             m_summary->setText(QStringLiteral("出错：") + reason);
                         });
        m_service->reload();
    }

    QString summaryText() const { return m_summary->text(); }

private:
    void render(const QVector<TodoItem> &items)
    {
        m_summary->setText(QStringLiteral("共 %1 条：%2")
                               .arg(items.size())
                               .arg(describeAll(items)));
    }

    TodoService *m_service;
    QLabel *m_summary = nullptr;
    QPushButton *m_addButton = nullptr;
};

// ============================================================================
// 六、基础设施层：三个真实实现 + 一个替身
// ----------------------------------------------------------------------------
// 构造/析构计数是第 ④ 节「组合根」要用的证据：它能回答
// 「到底是谁 new 的、谁删的、删了几次」——这三个问题靠读代码常常看不出来。
// ============================================================================
struct StoreCounters {
    int memConstructed = 0;
    int memDestroyed = 0;
    int jsonConstructed = 0;
    int jsonDestroyed = 0;
};
static StoreCounters g_count;

class MemoryTodoStore : public ITodoStore
{
public:
    MemoryTodoStore() { ++g_count.memConstructed; }
    ~MemoryTodoStore() override { ++g_count.memDestroyed; }

    QVector<TodoItem> loadAll() const override
    {
        ++m_calls;
        return m_items;
    }

    bool saveAll(const QVector<TodoItem> &items) override
    {
        ++m_calls;
        m_items = items;
        return true;
    }

    QString backendName() const override { return QStringLiteral("memory"); }
    qint64 callCount() const override { return m_calls; }

private:
    QVector<TodoItem> m_items;
    mutable qint64 m_calls = 0;
};

class JsonTodoStore : public ITodoStore
{
public:
    explicit JsonTodoStore(QString path) : m_path(std::move(path)) { ++g_count.jsonConstructed; }
    ~JsonTodoStore() override { ++g_count.jsonDestroyed; }

    QVector<TodoItem> loadAll() const override
    {
        ++m_calls;
        QVector<TodoItem> out;
        QFile f(m_path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return out;                          // 文件不存在 = 空库，不是错误
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError)
            return out;                          // 坏文件一律当空库：宁可空，不要崩
        const QJsonArray arr = doc.array();
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            TodoItem it;
            it.id = qint64(o.value(QStringLiteral("id")).toDouble());
            it.title = o.value(QStringLiteral("title")).toString();
            it.done = o.value(QStringLiteral("done")).toBool();
            out.append(it);
        }
        return out;
    }

    bool saveAll(const QVector<TodoItem> &items) override
    {
        ++m_calls;
        QJsonArray arr;
        for (const TodoItem &it : items) {
            QJsonObject o;
            o.insert(QStringLiteral("id"), double(it.id));
            o.insert(QStringLiteral("title"), it.title);
            o.insert(QStringLiteral("done"), it.done);
            arr.append(o);
        }
        QSaveFile f(m_path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
        return f.commit();                       // ★ 原子替换：要么旧的，要么新的
    }

    QString backendName() const override { return QStringLiteral("json"); }
    qint64 callCount() const override { return m_calls; }

private:
    QString m_path;
    mutable qint64 m_calls = 0;
};

// 装饰器：包在任意 ITodoStore 外面，只数调用、不改行为。
// 这就是「审计 / 日志 / 指标」这类跨层关注点该待的地方 —— 而不是写进 Service。
class AuditedTodoStore : public ITodoStore
{
public:
    explicit AuditedTodoStore(ITodoStore *inner) : m_inner(inner) {}

    QVector<TodoItem> loadAll() const override
    {
        ++m_calls;
        m_last = QStringLiteral("loadAll");
        return m_inner->loadAll();
    }

    bool saveAll(const QVector<TodoItem> &items) override
    {
        ++m_calls;
        m_last = QStringLiteral("saveAll(%1)").arg(items.size());
        return m_inner->saveAll(items);
    }

    QString backendName() const override
    {
        return QStringLiteral("audited(") + m_inner->backendName() + QStringLiteral(")");
    }

    qint64 callCount() const override { return m_calls; }
    QString lastOperation() const { return m_last; }

private:
    ITodoStore *m_inner;
    mutable qint64 m_calls = 0;
    mutable QString m_last = QStringLiteral("<还没有调用>");
};

// 替身：不碰磁盘、不碰事件循环，种子数据由构造参数给，还能「故意存不进去」。
// 有了它，第 ⑦ 节测业务逻辑既不需要临时目录，也不需要等 I/O。
class FakeTodoStore : public ITodoStore
{
public:
    explicit FakeTodoStore(QVector<TodoItem> seed, bool allowSave = true)
        : m_items(std::move(seed)), m_allowSave(allowSave) {}

    QVector<TodoItem> loadAll() const override
    {
        ++m_calls;
        return m_items;
    }

    bool saveAll(const QVector<TodoItem> &items) override
    {
        ++m_calls;
        if (!m_allowSave)
            return false;                        // 模拟「磁盘满了 / 没权限」
        m_items = items;
        return true;
    }

    QString backendName() const override { return QStringLiteral("fake"); }
    qint64 callCount() const override { return m_calls; }

private:
    QVector<TodoItem> m_items;
    bool m_allowSave;
    mutable qint64 m_calls = 0;
};

// ============================================================================
// 七、阶段①：把架构图写成可执行的 —— 环检测 + 分层方向
// ----------------------------------------------------------------------------
// 架构图在 PPT 上是不会错的，因为它不会被执行。写进代码它就不得不接受审问：
//   · 有没有**环**？（A 依赖 B，B 又依赖 A —— 那就分不开了，只能一起编译、一起测）
//   · 每条边是不是**朝下**的？（依赖只能指向更稳定的层）
// 下面这张表就是本讲开头那张图，一个 pair 一条边，意思是「from 依赖 to」。
// ============================================================================
struct Edge {
    QString from;
    QString to;
};

struct LayerTable {
    QStringList nodes;
    QVector<Edge> edges;
    QHash<QString, int> rank;      // 数字越小越稳定；依赖只许指向更小的数字
};

static LayerTable makeLayerTable()
{
    LayerTable t;
    t.nodes = {QStringLiteral("UI"), QStringLiteral("App"), QStringLiteral("Domain"),
               QStringLiteral("Infra")};
    // 越稳定（越靠内、越不该被别人牵着走）的层，rank 越小。
    t.rank.insert(QStringLiteral("Domain"), 0);   // 领域层：谁都不许依赖
    t.rank.insert(QStringLiteral("App"), 1);      // 应用层：只依赖领域
    t.rank.insert(QStringLiteral("Infra"), 1);    // 基础设施：也只依赖领域（实现接口）
    t.rank.insert(QStringLiteral("UI"), 2);       // 界面层：可以依赖上面两层

    t.edges = {
        {QStringLiteral("UI"), QStringLiteral("App")},
        {QStringLiteral("UI"), QStringLiteral("Domain")},
        {QStringLiteral("App"), QStringLiteral("Domain")},
        {QStringLiteral("Infra"), QStringLiteral("Domain")},
    };
    return t;
}

// 深度优先找环：白=没进过，灰=正在这条递归栈上，黑=走完了。
// 走到一个灰节点就说明「绕回来了」，把这个环原样报出来。
static QStringList findCycles(const LayerTable &t)
{
    QHash<QString, int> color;                    // 0 白 1 灰 2 黑
    for (const QString &n : t.nodes)
        color.insert(n, 0);

    QHash<QString, QStringList> adj;
    for (const Edge &e : t.edges)
        adj[e.from].append(e.to);

    QStringList cycles;
    QStringList stack;                            // 当前路径，用来打印环长什么样

    std::function<void(const QString &)> visit = [&](const QString &node) {
        color[node] = 1;
        stack.append(node);
        for (const QString &next : adj.value(node)) {
            if (color.value(next) == 1) {
                const int at = stack.indexOf(next);
                QStringList loop = stack.mid(at);
                loop.append(next);                // 首尾同名，一眼看出是个环
                cycles.append(loop.join(QStringLiteral(" → ")));
            } else if (color.value(next) == 0) {
                visit(next);
            }
        }
        stack.removeLast();
        color[node] = 2;
    };

    for (const QString &n : t.nodes) {
        if (color.value(n) == 0)
            visit(n);
    }
    return cycles;
}

// 反向边：依赖指向「更上层」（rank 不降），这在分层里是硬错。
static QStringList findUpwardEdges(const LayerTable &t)
{
    QStringList bad;
    for (const Edge &e : t.edges) {
        const int from = t.rank.value(e.from, -1);
        const int to = t.rank.value(e.to, -1);
        if (from < 0 || to < 0) {
            bad.append(QStringLiteral("%1 → %2（层名没登记）").arg(e.from, e.to));
            continue;
        }
        if (to >= from)
            bad.append(QStringLiteral("%1(%2) → %3(%4)")
                           .arg(e.from).arg(from).arg(e.to).arg(to));
    }
    return bad;
}

static void phase1_layerTable()
{
    section(QStringLiteral("① 架构图变成可执行的：环检测 + 依赖方向"));

    const LayerTable good = makeLayerTable();
    qInfo().noquote() << QStringLiteral("  层表 %1 个节点、%2 条边；依赖只许指向 rank 更小的层")
                             .arg(good.nodes.size()).arg(good.edges.size());
    for (const Edge &e : good.edges)
        qInfo().noquote() << QStringLiteral("    %1 → %2").arg(e.from, e.to);

    const QStringList goodCycles = findCycles(good);
    const QStringList goodUp = findUpwardEdges(good);
    checkNum(QStringLiteral("【1A】真实层表的环数"), goodCycles.size(), 0);
    checkNum(QStringLiteral("【1B】真实层表的反向边数"), goodUp.size(), 0);
    if (!goodCycles.isEmpty())
        qWarning().noquote() << QStringLiteral("        环：") + goodCycles.join(QStringLiteral(" ｜ "));
    if (!goodUp.isEmpty())
        qWarning().noquote() << QStringLiteral("        反向边：") + goodUp.join(QStringLiteral(" ｜ "));

    // 假边 A：让领域层去依赖界面层。既造出环，也是反向边 —— 两种检查都该响。
    LayerTable fakeA = good;
    fakeA.edges.append({QStringLiteral("Domain"), QStringLiteral("UI")});
    const QStringList cyclesA = findCycles(fakeA);
    checkNum(QStringLiteral("【1C】加一条 Domain→UI 之后的环数"), cyclesA.size(), 1);
    checkNum(QStringLiteral("【1D】加一条 Domain→UI 之后的反向边数"),
             findUpwardEdges(fakeA).size(), 1);
    if (!cyclesA.isEmpty())
        qInfo().noquote() << QStringLiteral("  检出的环：") + cyclesA.first();

    // 假边 B：应用层直接依赖基础设施层。**没有环**，但有反向边。
    // ★ 这条是本节的重点：只看环是抓不住它的，必须有「方向」这把尺子。
    LayerTable fakeB = good;
    fakeB.edges.append({QStringLiteral("App"), QStringLiteral("Infra")});
    checkNum(QStringLiteral("【1E】加一条 App→Infra 之后的环数"), findCycles(fakeB).size(), 0);
    checkNum(QStringLiteral("【1F】加一条 App→Infra 之后的反向边数"),
             findUpwardEdges(fakeB).size(), 1);

    // 定理（可以直接背走）：若每条合法边都让 rank 严格下降，
    // 那么图上不可能有环 —— 因为环要求 rank 绕一圈回到自己，而严格下降回不去。
    // 换句话说：**分层顺序本身就是一张拓扑序**，你不需要再单独跑一遍拓扑排序。
    qInfo().noquote() << QStringLiteral("  【1G】定理：分层规则成立 ⇒ 无环。"
                                        "所以【1A】的 0 是必然，【1C】的 1 也是必然。");
    verdict(Obs, QStringLiteral("【1H】真正需要单独检查的只有反向边"),
            true, QStringLiteral("环检查是保险，方向检查才是主刀"));
}

// ============================================================================
// 八、阶段②：运行期认层
// ----------------------------------------------------------------------------
// 这一段不靠命名约定、不靠目录名字，直接问对象自己：
//   「你的继承链里有没有 QWidget？」
// 三个问题对应三条硬账：
//   UI 层是 QWidget（否则它拿什么画）
//   应用层/领域层不是 QWidget（否则它们只能在有窗口的进程里活着）
//   接口不是 QObject（否则替身也得当 Qt 对象养）
// ============================================================================
static QString classChain(const QObject *obj)
{
    QStringList parts;
    for (const QMetaObject *mo = obj->metaObject(); mo != nullptr; mo = mo->superClass())
        parts << QString::fromLatin1(mo->className());
    return parts.join(QStringLiteral(" → "));
}

static void phase2_runtimeLayers(TodoService *service, QWidget *page, ITodoStore *store)
{
    section(QStringLiteral("② 运行期认层：inherits() 与 dynamic_cast"));

    qInfo().noquote() << QStringLiteral("  UI  链：") + classChain(page);
    qInfo().noquote() << QStringLiteral("  App 链：") + classChain(service);

    checkBool(QStringLiteral("【2A】界面对象 inherits(\"QWidget\")"),
              page->inherits("QWidget"), true);
    checkBool(QStringLiteral("【2B】服务对象 inherits(\"QWidget\")"),
              service->inherits("QWidget"), false);
    checkBool(QStringLiteral("【2C】界面对象 inherits(\"TodoService\")"),
              page->inherits("TodoService"), false);

    // 接口不是 QObject：这个判断只有一个正确写法 —— dynamic_cast，而不是 inherits。
    // 因为 ITodoStore 根本没有元对象，inherits() 无从下手。
    const bool storeIsQObject = (dynamic_cast<QObject *>(store) != nullptr);
    checkBool(QStringLiteral("【2D】存储接口是 QObject"), storeIsQObject, false);

    // 一个必须说清楚的细节：inherits() 是**字符串查类链**，
    // 所以拼错类名不会报错，只会安静地返回 false。
    // 拿它当你自己的「是不是这个类」判据之前，先用一个你确定的结果校一下。
    checkBool(QStringLiteral("【2E】拼错的类名 inherits(\"QWidgetX\")"),
              page->inherits("QWidgetX"), false);
}

// ============================================================================
// 九、阶段③：元对象审计 —— 接口里不许出现 UI 类型
// ----------------------------------------------------------------------------
// 「分层」这件事最容易被破坏的位置不是类图，是**函数签名**：
// 只要有一个槽写成 void showDialog(QWidget *parent)，两层的依赖就长在一起了。
// 元对象系统把每个可调用方法的参数类型名都记下来了，于是可以**自动审计**：
// 把 Service 的全部参数类型名扫一遍，凡是出现 Widget/Label/Button/Layout 的，报红。
// 这条检查的好处是：它检查的是**事实**（moc 记下来的类型名），不是人的自觉。
// ============================================================================
static bool looksLikeUiType(const QString &typeName)
{
    static const QStringList banned = {
        QStringLiteral("QWidget"),  QStringLiteral("QDialog"),   QStringLiteral("QLabel"),
        QStringLiteral("QPushButton"), QStringLiteral("QLayout"), QStringLiteral("QVBoxLayout"),
        QStringLiteral("QHBoxLayout"), QStringLiteral("QMainWindow"),
    };
    for (const QString &b : banned) {
        if (typeName.contains(b))
            return true;
    }
    return false;
}

static QStringList collectParameterTypes(const QObject *obj)
{
    QStringList out;
    const QMetaObject *mo = obj->metaObject();
    for (int i = 0; i < mo->methodCount(); ++i) {
        const QMetaMethod m = mo->method(i);
        for (int p = 0; p < m.parameterCount(); ++p)
            out << QString::fromLatin1(m.parameterTypeName(p));
    }
    return out;
}

static void phase3_metaAudit(TodoService *service, QWidget *page)
{
    section(QStringLiteral("③ 元对象审计：Service 的签名里没有 UI 类型"));

    const QStringList svcTypes = collectParameterTypes(service);
    qInfo().noquote() << QStringLiteral("  Service 元对象共 %1 个方法，参数类型名 %2 个：")
                             .arg(service->metaObject()->methodCount()).arg(svcTypes.size());
    for (const QString &t : svcTypes)
        qInfo().noquote() << QStringLiteral("      · ") + t;

    int svcBad = 0;
    for (const QString &t : svcTypes) {
        if (looksLikeUiType(t))
            ++svcBad;
    }
    checkNum(QStringLiteral("【3A】Service 参数里的 UI 类型数"), svcBad, 0);

    // 反向对照：界面层的参数里**应该**能看到 UI 类型（它本来就该认识界面）。
    // 如果这里也是 0，说明我的扫描器根本没在干活 —— 那【3A】的 0 就毫无说服力。
    //
    // ★ 这里踩过一个真坑，值得单独说：我原以为扫 QWidget 的子类一定能扫到
    //   「QWidget*」这种参数，结果并不是 —— QWidget 在 qwidget.h 第 242~245 行的
    //   Q_SLOTS 只有 setEnabled(bool) / setDisabled(bool) / setWindowModified(bool)，
    //   一个带控件指针参数的槽都没有（setParent(QWidget*) 在第 544 行，但它**不是槽**，
    //   不进元对象）。所以「扫界面层一定能扫到 UI 类型」这个前提是假的，
    //   拿它当对照会造出一条永远红的假账。
    //   正确的做法是**直接给扫描器做单元测试**：喂它几个已知答案的类型名。
    const QStringList pageTypes = collectParameterTypes(page);
    int pageUi = 0;
    for (const QString &t : pageTypes) {
        if (looksLikeUiType(t))
            ++pageUi;
    }
    qInfo().noquote() << QStringLiteral("  界面层元对象参数类型名 %1 个，其中 UI 类型 %2 个")
                             .arg(pageTypes.size()).arg(pageUi);

    const bool scannerSaysYes = looksLikeUiType(QStringLiteral("QWidget*"))
                                && looksLikeUiType(QStringLiteral("QLayout*"))
                                && looksLikeUiType(QStringLiteral("QPushButton*"));
    const bool scannerSaysNo = !looksLikeUiType(QStringLiteral("QString"))
                               && !looksLikeUiType(QStringLiteral("qint64"))
                               && !looksLikeUiType(QStringLiteral("QVector<TodoItem>"));
    checkBool(QStringLiteral("【3B】扫描器自检：认识 UI 类型也放过普通类型"),
              scannerSaysYes && scannerSaysNo, true);

    // 防止「扫了个空」蒙混过关：两个对象都得真扫出东西来。
    checkBool(QStringLiteral("【3C】确实扫到了方法参数（不是空扫描）"),
              svcTypes.size() > 0 && pageTypes.size() > 0, true);
}

// ============================================================================
// 十、阶段④：依赖倒置 —— 同一份 Service，换两种存储，行为逐字相同
// ----------------------------------------------------------------------------
// 这一节是本讲的**主证据**。做法：
//   同一段业务脚本（加载 → 加两条 → 勾掉一条）分别跑在两套装配上：
//     A: MemoryTodoStore
//     B: JsonTodoStore（临时文件）
//   然后比较三样东西：Service 里的数据、界面上的文字、以及**重启后**（再 new 一个
//   JsonTodoStore 从同一个文件读）还在不在。
// 如果 Service 或 UI 里偷偷依赖了具体存储，这三样立刻会分叉。
// ============================================================================
static QString runScript(TodoService *svc, TodoPage *page)
{
    svc->reload();
    svc->add(QStringLiteral("写周报"));
    svc->add(QStringLiteral("买牛奶"));
    svc->toggle(2);                                  // 勾掉「买牛奶」
    const QString fromService = describeAll(svc->items());
    const QString fromUi = page->summaryText();
    // 两种视角必须一致：界面显示的就是 Service 里那份数据，不多不少。
    return fromService + QStringLiteral(" ‖ ") + fromUi;
}

static void phase4_inversion(const QString &jsonPath)
{
    section(QStringLiteral("④ 依赖倒置：换存储实现，业务与界面都不改"));

    QString snapshotA;
    QString snapshotB;
    TodoItem roundTripFirst;
    {
        MemoryTodoStore mem;
        TodoService svc(&mem);
        TodoPage page(&svc);
        snapshotA = runScript(&svc, &page);
        qInfo().noquote() << QStringLiteral("  A（memory）数据：") + describeAll(svc.items());
        qInfo().noquote() << QStringLiteral("  A（memory）界面：") + page.summaryText();
    }
    {
        JsonTodoStore json(jsonPath);
        TodoService svc(&json);
        TodoPage page(&svc);
        snapshotB = runScript(&svc, &page);
        qInfo().noquote() << QStringLiteral("  B（json）  数据：") + describeAll(svc.items());
        qInfo().noquote() << QStringLiteral("  B（json）  界面：") + page.summaryText();
        if (!svc.items().isEmpty())
            roundTripFirst = svc.items().first();
    }

    checkStr(QStringLiteral("【4A】两种存储下「数据+界面」快照"), snapshotB, snapshotA);
    checkBool(QStringLiteral("【4B】A 的界面确实非空（防止两边都空着也算相等）"),
              !snapshotA.isEmpty() && snapshotA.contains(QStringLiteral("写周报")), true);

    // 「重启」：新开一个存储对象，从同一个文件读。这是文件型存储唯一真正的考试。
    JsonTodoStore reopened(jsonPath);
    const QVector<TodoItem> loaded = reopened.loadAll();
    checkNum(QStringLiteral("【4C】重启后读回的条数"), loaded.size(), 2);
    if (!loaded.isEmpty()) {
        checkStr(QStringLiteral("【4D】重启后第一条仍然对得上"),
                 describeTodo(loaded.first()), describeTodo(roundTripFirst));
        checkBool(QStringLiteral("【4E】勾选状态也被持久化了"), loaded.first().done, false);
    } else {
        verdict(Hard, QStringLiteral("【4C】重启后读回的条数"), false,
                QStringLiteral("读不到就说明文件格式或路径有问题"));
        ++gLedger.stalled;
    }

    // 版本引用的是「接口指针」，不是具体类型 —— 这一条是依赖倒置的字面证据。
    MemoryTodoStore probeMem;
    ITodoStore *asInterface = &probeMem;              // 上转型：具体 → 接口
    checkBool(QStringLiteral("【4F】具体存储能当接口用"),
              asInterface->backendName() == QStringLiteral("memory"), true);
}

// ============================================================================
// 十一、阶段⑤：组合根 —— 谁 new 谁负责，销毁顺序与构造相反
// ----------------------------------------------------------------------------
// 「组合根」就是整个程序里**唯一**允许写 new 具体实现的地方（对本讲来说就是 main）。
// 它承担两个责任：
//   1) 构造：先造依赖（store），再造使用者（service），最后造界面；
//   2) 销毁：**反着来** —— 界面先死、service 次之、store 最后。
//      反了会怎样？service 手里那个 ITodoStore* 会变成野指针，
//      而它的析构里如果碰一下（很多实现真的会，比如 flush），就是堆破坏。
// 下面用三个探针把「析构顺序」这件事量出来，因为 C++ 的规则不能只靠背：
//   同一个作用域里，**后声明的先销毁**（逆序）。
// ============================================================================
struct OrderProbe {
    QString label;
    QStringList *log;
    ~OrderProbe() { log->append(label); }
};

static void phase5_compositionRoot(const QString &jsonPath)
{
    section(QStringLiteral("⑤ 组合根：构造顺序与销毁顺序"));

    QStringList destructionLog;
    {
        OrderProbe first{QStringLiteral("第一个声明的"), &destructionLog};
        OrderProbe second{QStringLiteral("第二个声明的"), &destructionLog};
        OrderProbe third{QStringLiteral("第三个声明的"), &destructionLog};
        Q_UNUSED(first);
        Q_UNUSED(second);
        Q_UNUSED(third);
        checkNum(QStringLiteral("【5A】作用域内还没销毁"), destructionLog.size(), 0);
    }
    qInfo().noquote() << QStringLiteral("  析构日志：") + destructionLog.join(QStringLiteral(" → "));
    checkStr(QStringLiteral("【5B】析构顺序 = 声明顺序的反序"),
             destructionLog.join(','),
             QStringLiteral("第三个声明的,第二个声明的,第一个声明的"));

    // 用真实的 store/service 走一遍：数量上的证据是「各构造一次、各析构一次」。
    const int memC0 = g_count.memConstructed;
    const int memD0 = g_count.memDestroyed;
    const int jsonC0 = g_count.jsonConstructed;
    const int jsonD0 = g_count.jsonDestroyed;
    {
        // ★ 声明顺序就是依赖顺序：store 在前，service 在后。
        auto store = std::make_unique<MemoryTodoStore>();
        auto service = std::make_unique<TodoService>(store.get());
        service->reload();
        service->add(QStringLiteral("组合根里造出来的条目"));

        checkNum(QStringLiteral("【5C】组合根里 store 构造次数"), g_count.memConstructed - memC0, 1);
        checkNum(QStringLiteral("【5D】此时 store 还没被销毁"), g_count.memDestroyed - memD0, 0);
        checkNum(QStringLiteral("【5E】Service 没有自己 new 存储"),
                 store->callCount(), 2);          // reload 1 次 + add 1 次，仅此而已
    }
    checkNum(QStringLiteral("【5F】作用域结束后 store 析构次数"), g_count.memDestroyed - memD0, 1);
    qInfo().noquote() << QStringLiteral("  正序构造、逆序销毁：Service 先亡，随后 store；"
                                        "Service 全程不拥有 store 的生死");

    // 另一个组合根：JSON 那套装配。每套装配都是一次「换实现、业务不动」。
    {
        JsonTodoStore json(jsonPath);
        TodoService svc(&json);
        svc.reload();
        checkNum(QStringLiteral("【5G】json 存储被构造一次"), g_count.jsonConstructed - jsonC0, 1);
    }
    checkNum(QStringLiteral("【5H】json 存储被析构一次"), g_count.jsonDestroyed - jsonD0, 1);
}

// ============================================================================
// 十二、阶段⑥：装饰器 —— 跨层关注点不该写进业务
// ----------------------------------------------------------------------------
// 「审计、日志、指标、限流、重试」这类东西有个共同点：它们**每一层都可能要**，
// 而且都跟业务无关。塞进 Service 会让 Service 变得又长又难测；
// 正确的做法是包一层，和原来的实现实现同一个接口。
// 本节要证的两条：
//   · 套了装饰器之后，业务行为**逐字不变**（同一个快照）；
//   · 装饰器看到的调用次数 == 业务真正发起的 I/O 次数（不多不少）。
// ============================================================================
static void phase6_decorator()
{
    section(QStringLiteral("⑥ 装饰器：审计挂在外层，业务一行不改"));

    MemoryTodoStore inner;
    AuditedTodoStore audited(&inner);              // 装饰器只认识接口
    TodoService svc(&audited);
    TodoPage page(&svc);

    const QString withAudit = runScript(&svc, &page);
    qInfo().noquote() << QStringLiteral("  外层名字：") + audited.backendName();
    qInfo().noquote() << QStringLiteral("  最后一次被审计到的操作：") + audited.lastOperation();

    // 业务脚本固定是：reload + add + add + toggle = 4 次存储调用。
    checkNum(QStringLiteral("【6A】审计层看到的调用次数"), audited.callCount(), 4);
    checkNum(QStringLiteral("【6B】内层实际被调用次数"), inner.callCount(), 4);
    checkBool(QStringLiteral("【6C】装饰器没有替业务多发一次 I/O"),
              audited.callCount() == inner.callCount(), true);

    // 逐字不变：和第 ④ 节 A 的快照比。这里重新算一遍 A，避免跨节传字符串。
    MemoryTodoStore plain;
    TodoService svcPlain(&plain);
    TodoPage pagePlain(&svcPlain);
    const QString withoutAudit = runScript(&svcPlain, &pagePlain);
    checkStr(QStringLiteral("【6D】加审计前后业务结果一致"), withAudit, withoutAudit);

    checkBool(QStringLiteral("【6E】装饰器名字能一层层叠出来"),
              audited.backendName() == QStringLiteral("audited(memory)"), true);
}

// ============================================================================
// 十三、阶段⑦：替身 —— 无 I/O、无事件循环地测业务
// ----------------------------------------------------------------------------
// 最后一节回答「分层到底换来了什么」。答案很具体：**业务逻辑可以被单独摆上桌**。
// 用 FakeTodoStore：
//   · 不碰磁盘（不需要临时目录、不需要清理）
//   · 不跑事件循环（信号是直连，emit 就同步回调）
//   · 还能**故意失败**：allowSave=false 时 saveAll 返回 false，
//     于是我们第一次有机会验证「存不进去就别改内存」这条规矩。
// 这三件事在没有分层、业务和存储长在一起的项目里，一件都做不到。
// ============================================================================
static void phase7_testDouble()
{
    section(QStringLiteral("⑦ 替身：把业务逻辑单独摆上桌"));

    QVector<TodoItem> seed;
    {
        TodoItem a;
        a.id = 10;
        a.title = QStringLiteral("种子一");
        TodoItem b;
        b.id = 11;
        b.title = QStringLiteral("种子二");
        b.done = true;
        seed << a << b;
    }

    // —— 正常路径 ——
    FakeTodoStore store(seed);
    TodoService svc(&store);
    int changedCount = 0;
    int failedCount = 0;
    QObject::connect(&svc, &TodoService::changed, &svc, [&changedCount]() { ++changedCount; });
    QObject::connect(&svc, &TodoService::failed, &svc, [&failedCount]() { ++failedCount; });

    svc.reload();                                  // changed#1
    svc.add(QStringLiteral("新条目"));              // changed#2
    svc.toggle(11);                                // changed#3

    checkNum(QStringLiteral("【7A】三条命令，三次 changed"), changedCount, 3);
    checkNum(QStringLiteral("【7B】没有出错"), failedCount, 0);
    checkNum(QStringLiteral("【7C】最终条数"), svc.items().size(), 3);
    checkNum(QStringLiteral("【7D】新条目的 id 接着种子的最大值往后排"),
             svc.items().isEmpty() ? -1 : svc.items().last().id, 12);
    checkBool(QStringLiteral("【7E】toggle 把 done 取反了"),
              svc.items().size() > 1 && svc.items().at(1).done == false, true);
    qInfo().noquote() << QStringLiteral("  全部数据（无 I/O）：") + describeAll(svc.items());

    // —— 失败路径：这一次存储说「写不进去」 ——
    FakeTodoStore failing(seed, /*allowSave=*/false);
    TodoService svc2(&failing);
    int changed2 = 0;
    int failed2 = 0;
    QString reason;
    QObject::connect(&svc2, &TodoService::changed, &svc2, [&changed2]() { ++changed2; });
    QObject::connect(&svc2, &TodoService::failed, &svc2, [&failed2, &reason](const QString &r) {
        ++failed2;
        reason = r;
    });

    svc2.reload();                                 // 加载是能成功的
    svc2.add(QStringLiteral("这条存不进去"));

    checkNum(QStringLiteral("【7F】失败路径上的 changed 次数"), changed2, 1);
    checkNum(QStringLiteral("【7G】失败路径上的 failed 次数"), failed2, 1);
    checkNum(QStringLiteral("【7H】内存没有被偷偷改掉（回滚）"), svc2.items().size(), 2);
    checkBool(QStringLiteral("【7I】失败原因是人话，不是错误码"),
              reason.contains(QStringLiteral("保存失败")), true);
    qInfo().noquote() << QStringLiteral("  界面会看到的那句话：「") + reason + QStringLiteral("」");

    // 最后一条，给替身自己：它证明了「业务逻辑不依赖任何真实 I/O」。
    checkBool(QStringLiteral("【7J】替身的后端名与真实存储不同"),
              failing.backendName() == QStringLiteral("fake"), true);
}

// ============================================================================
// 十四、main：组合根 + 合账
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
        qWarning().noquote() << QStringLiteral("  ★ 请按上面的 [!! ] / 前提 行往下查。");
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("quality_03_architecture_layers"));

    qInfo() << "================================================================";
    qInfo() << "quality 第 3 讲 · 分层架构：依赖只朝一个方向流（Qt =" << QT_VERSION_STR << "）";
    qInfo() << "================================================================";

    const QString jsonPath =
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("qt_lp_quality03_todo.json"));
    QFile::remove(jsonPath);                       // 从干净状态开始（上次跑崩了也照样干净）

    // ── 组合根之一：内存装配。声明顺序 = 依赖顺序 ──────────────────────────
    MemoryTodoStore memStore;
    TodoService memService(&memStore);
    TodoPage memPage(&memService);

    phase1_layerTable();
    phase2_runtimeLayers(&memService, &memPage, &memStore);
    phase3_metaAudit(&memService, &memPage);
    phase4_inversion(jsonPath);
    phase5_compositionRoot(jsonPath);
    phase6_decorator();
    phase7_testDouble();

    // ── 收尾：只看本讲自己造出来的那个文件 ────────────────────────────────
    const bool removed = QFile::remove(jsonPath) || !QFile::exists(jsonPath);
    checkBool(QStringLiteral("【总账】临时 JSON 已清理"), removed, true);

    reconcile();

    const int rc = gLedger.exitCode();
    qInfo().noquote() << QStringLiteral("进程退出码 = %1").arg(rc);
    return rc;
}

// ============================================================================
// 十五、速查：分层这件事，记住七条就够用
// ============================================================================
/*
 * 一、四种层，一句话各说一遍
 *   UI 层     只做「把状态摆出来 + 把点击变成意图」。认识 Service，不认识 Store。
 *   应用层     编排用例：加载、新建、勾选。认识接口，不认识具体实现，不 new 具体实现。
 *   领域层     值对象 + 纯接口（TodoItem / ITodoStore）。**只认 QtCore**，不认 QWidget。
 *   基础设施   实现接口：内存、JSON、SQL、HTTP 各一份。**只认接口和领域**，不认 UI。
 *
 * 二、依赖只能朝下
 *   允许：UI→App、UI→Domain、App→Domain、Infra→Domain
 *   禁止：Domain→任何层、App→UI、App→Infra（具体）、Infra→UI
 *   判据一句话：**更稳定的层，不许知道比它更易变的层。**
 *
 * 三、分层规则成立 ⇒ 不可能有环
 *   因为每条合法边都让 rank 严格下降，绕一圈回不来。所以分层顺序就是拓扑序，
 *   你不需要额外跑拓扑排序；真正要单独查的是「反向边」（第 ① 节【1E】【1F】）。
 *
 * 四、认层用三个工具，别用命名约定
 *   inherits("QWidget")        运行期查继承链（字符串比对，拼错静默返回 false）
 *   dynamic_cast<QObject *>(p) 查「到底是不是 Qt 对象」（接口没有元对象，只能这么查）
 *   元对象参数类型扫描           查签名里有没有混进 UI 类型（第 ③ 节）
 *
 * 五、组合根只许有一个
 *   全程序唯一写 `new 具体实现` 的地方。构造顺序：依赖在前、使用者在后；
 *   销毁顺序天然相反（后声明先销毁）。service 不许拥有 store 的生死。
 *
 * 六、跨层关注点用装饰器
 *   审计/日志/指标/重试 → 实现同一个接口，包在外面。
 *   判据：套上装饰器之后，业务的结果**逐字不变**，只多出「被看了几次」。
 *
 * 七、分层换来的东西是「可测」
 *   业务逻辑能配一个 FakeTodoStore，不碰磁盘、不跑事件循环、还能故意失败。
 *   如果你写不出这个替身，说明你的业务层还粘着 I/O —— 分层没做到。
 *
 * ——— 老师傅的话 ———
 *   徒弟，架构不是画出来的，是**依赖方向**堆出来的。
 *   我见过太多项目，类图画得漂亮，一编译 40 分钟 —— 因为领域层 include 了
 *   QtWidgets，为了一个 QColor。你还不能怪写的人：那一刻他只想省一个类型转换。
 *
 *   所以这一讲我逼着你做了一件「看起来没用」的事：把这七个阶段写成会自己判定的程序。
 *   为什么值得？因为**架构这东西没有编译器**。分层错了，代码照样编得过、照样跑得动，
 *   只是三年后没人敢动。你唯一能靠的，就是把这些规矩变成能跑、能红的东西。
 *
 *   再补一句最实用的：判断一个类该放哪层，别问「它像什么」，问
 *   「**它会不会逼我引入一个更易变的依赖**」。会，就是上层。
 */

// ============================================================================
// 十六、练习：从「看懂了」到「改得动」
// ----------------------------------------------------------------------------
/*
 * 1. 给 ITodoStore 加 blames：`virtual bool remove(qint64 id) = 0;`
 *    要求同时改四个实现（Memory / Json / Audited / Fake）和 Service。
 *    做完数一数：**你一共动了几个文件、其中几个是 UI？** 如果动了 UI，说明分层破了。
 *
 * 2. 给 Audit 加「耗时」：AuditedTodoStore 里用 QElapsedTimer 记每次调用的毫秒数，
 *    提供一个 `qint64 totalMs() const`。要求：只用 QElapsedTimer，不许改 Service。
 *    （提示：这题在考你「装饰器能不能独立演进」。）
 *
 * 3. 把第 ① 节的层表从硬编码改成**从代码里推**：让每个类提供一个 `layerName()` 虚函数，
 *    遍历所有对象收集边，再跑同一套环检测。做完回答：
 *    为什么「推出来的边」一定是对的，而**人写的边**经常是错的？
 *
 * 4. 故意破一次分层：在 TodoService::add() 里直接 `#include <QMessageBox>` 弹一个提示。
 *    然后回答三个问题：
 *      (a) 第 ⑦ 节的替身测试还能跑吗？（提示：QMessageBox 是模态的，会卡住）
 *      (b) 第 ③ 节的元对象审计能不能抓到这个改动？（提示：抓不到 —— 它检查的是签名，
 *          而这里破坏发生在**函数体**。这说明元对象审计的边界在哪。）
 *      (c) 那这类破坏该由谁来抓？（答案不是工具，是 code review 规则：
 *          「领域层/应用层的 CMakeLists 里不许出现 Qt6::Widgets」。）
 *
 * 5. 资深题：把 JsonTodoStore 换成「先写 A 文件、成功后再改 B 文件」的双文件提交，
 *    模拟一次断电（在两次写之间 return false），验证重启后读到的**要么全旧、要么全新**。
 *    要求：不许用数据库，只用 QFile/QSaveFile。做完回答：
 *    为什么 QSaveFile 的 commit() 是原子的，而两次 QFile 写不是？
 *
 * 6. 给整讲加一层「插件」：启动时从某个目录加载实现 ITodoStore 的动态库
 *    （用 QPluginLoader 或 QLibrary 都行），由组合根决定用哪个。
 *    只要这件事不需要改 Service 和 UI 一行代码，就说明你的分层真的立住了。
 *    这一题不开工也行，但请想清楚：插件里的类怎么才能不掉进「反向依赖」的坑？
 */
// ============================================================================

// 本文件有 2 个 Q_OBJECT 类（TodoService / TodoPage），AUTOMOC 生成 .moc 并在下面包含。
#include "03_architecture_layers.moc"
