// ============================================================================
// 第 7 讲 · QObject 父子树与所有权：谁 new 的不重要，谁当爹的才算
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   Qt 的对象生命周期不是靠智能指针堆出来的，而是靠**一棵树**：QObject 有 parent，
//   有 children 列表，父析构时递归 delete 子。把五件事一次讲透：
//     1) 归属怎么建立（构造传 parent / setParent 转移），转移时**两边表都要动**
//     2) 父对象析构 = 递归删除子对象（以及为什么「栈上 QObject 挂到堆上父」是找死）
//     3) children() / findChild<T> / findChildren<T> 的**真实模板签名**（T 是指针！）
//     4) deleteLater() 到底解决什么问题——「在事件处理函数里 delete 自己」会怎样
//     5) QObjectCleanupHandler：一堆寿命不定的临时对象，怎么一次清干净
//        （顺带纠正一条老说法：登记「已经有爹」的对象**不会**双删，本机实测；
//         它真正的答案是「名单自己会干净，你手里的裸指针不会」）
//   每节都打印一棵**父子树快照**，让你看得见「谁在谁下面」在每一步怎么变。
//
// 对应构建目标名：metaobject_07_qobject_tree
//   add_executable(metaobject_07_qobject_tree 07_qobject_tree.cpp)
//   target_link_libraries(metaobject_07_qobject_tree PRIVATE Qt6::Core)   ← 本讲不需要 Widgets
//   set(CMAKE_AUTOMOC ON)
//
// 运行前须知：
//   · 用 QCoreApplication（纯 QObject 树不需要 GUI），不显示窗口，结论全部 qInfo()，
//     QTimer::singleShot 排自动脚本，最后 QCoreApplication::quit()——无人值守能跑完。
//   · 【一处故意留白】第 4 节「在槽里直接 delete this」的反面代码是**注释掉**的：
//     那是真·未定义行为（发射者手里的连接链表节点已被销毁），跑起来可能崩、也可能
//     「看着没事」——本机把它换成正文那种写法连跑 5 次，**5 次全 exit 0**，属于后者。
//     「没崩」在这里恰恰是最坏的结果：它教会你信任一段随时会变脸的代码。
//     想看效果请在调试器里单独试一次，别把它留在 CI 里。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 metaobject_07_qobject_tree，跑到自动退出、退出码 0）。
//     但本讲**没有汇总自检行**，退出码 0 只证明「没崩、走完了」，不证明正文每个数字都对：
//     带具体数字的「预期」请以你自己那次运行输出为准（本机那一轮存档 runlogs/metaobject_07_qobject_tree.log）。
// ============================================================================

#include <QCoreApplication>
#include <QByteArray>
#include <QObjectCleanupHandler>   // 第 7 节要用它：一个都不少地批量收尾
#include <QPointer>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QDebug>

// ============================================================================
// 一、树是什么：QObject 自己带一个 QObjectList children
// ============================================================================
// 别把 Qt 的父子关系想象成什么「对象管理器容器」。它就是 QObjectData 里两个字段：
//     QObject *parent;              // 我爹是谁
//     QObjectList children;         // 我儿有谁（typedef QList<QObject*> QObjectList，46 行）
// 构造时传 parent、或事后 setParent，做的都是同一套四步：
//     从旧父的 children 里摘掉我 → 塞进新父的 children 里 → 改我的 parent 字段
//     → 过程中派发 QEvent::ParentAboutToChange（本机 qcoreevent.h 86 行，值 = 131，
//       注释原文 "sent just before the parent change is done"）这类事件，给子类留钩子
// 删除发生在**父的析构函数**里：QObject::~QObject() 会把自己的 children 逐个 delete，
// 而每个子对象的析构又递归删它自己的子——所以「delete 一个根，整棵树没了」。
// ============================================================================

// ── 被管理的零件：自己记一笔「我活着的次数」，好让删除证据可见 ────────────
class Part : public QObject
{
    Q_OBJECT
public:
    // 构造函数就把 parent 透传给 QObject —— 归属在构造那一刻就定下来了
    explicit Part(const QString &name, QObject *parent = nullptr)
        : QObject(parent), m_name(name)
    {
        setObjectName(name);            // objectName 是给 findChild 用的「门牌号」
        ++s_alive;
    }

    ~Part() override { --s_alive; }     // 谁删的我看不见，但活着的人数会减

    static int alive() { return s_alive; }
    static void resetAlive() { s_alive = 0; }

    QString partName() const { return m_name; }

Q_SIGNALS:
    // 本讲的「事件」就用一个普通信号代替：它是在**同步调用栈**里被派发的
    //（QMetaObject::activate 正在遍历连接表），正好等同于「正在处理与该对象
    // 有关的事情」——第 4 节在这里头 delete 自己，就是最危险的场合。
    void ticked(const QString &name);

private:
    QString m_name;
    inline static int s_alive = 0;       // C++17 静态成员内联定义，免额外 TU
};

// ── 打印一棵子树：所有「父子树快照」都调它 ────────────────────────────────
// 用的就是 QObject::children()（qobject.h 206 行，返回 `const QObjectList &`，
// **注意是引用**：不要拿它去接 `QObjectList list = obj->children();` 之外的野指针玩法，
// 更不要边遍历边 setParent/delete —— QList 会重分配，迭代器当场失效）。
static void dumpTree(QStringList &out, const QObject *node, int depth)
{
    const QString indent(depth * 4, QLatin1Char(' '));
    const QString label = node->objectName().isEmpty()
            ? QStringLiteral("<无名字 %1>").arg(QString::fromLatin1(node->metaObject()->className()))
            : node->objectName();
    out << QStringLiteral("%1├─ %2   [%3]  子数=%4")
           .arg(indent, label, QString::fromLatin1(node->metaObject()->className()))
           .arg(node->children().size());
    // 递归下去：Qt 的 children() 已经是「所有直接子对象」，一层层展开就是整棵树
    const QObjectList kids = node->children();       // 这里**故意拷贝一份**再遍历
    for (const QObject *kid : kids)
        dumpTree(out, kid, depth + 1);
}

static void printTree(const char *tag, const QObject *root)
{
    QStringList lines;
    lines << QStringLiteral("  ── %1 ──").arg(QString::fromUtf8(tag));
    dumpTree(lines, root, 0);
    qInfo().noquote() << lines.join(QLatin1Char('\n'));
}

// ============================================================================
// 二、findChild / findChildren 的真实签名（Qt6 和旧书不一样，两处）
// ----------------------------------------------------------------------------
// 本机 qobject.h 159~199 行原文（去掉了实现体）：
//     template<typename T>
//     T findChild(QAnyStringView aName,
//                 Qt::FindChildOptions options = Qt::FindChildrenRecursively) const;
//     template<typename T>
//     QList<T> findChildren(QAnyStringView aName,
//                 Qt::FindChildOptions options = Qt::FindChildrenRecursively) const;
//     template<typename T> QList<T> findChildren(const QRegularExpression &re,
//                 Qt::FindChildOptions options = Qt::FindChildrenRecursively) const;
// 三条硬信息：
//   ① **T 必须写成指针类型**：`findChild<Part*>("m1")`。因为返回类型就是 T 本身
//      （内部 `return static_cast<T>(qt_qFindChild_helper(...))`，165 行），
//      写 `findChild<Part>` 会得到一个 Part 值对象——编译期就报错。
//      而 `std::remove_pointer` 那一步（162 行）说明它内部会退回到类再做元对象比较。
//   ② 名字参数是 **QAnyStringView**（Qt6 的新「什么字符串都接得下」视图类型），
//      不是 const QString&；Qt5 时代是 `const QString &name`。传 const char* /
//      QString / QString::constData() 现在都不用临时构造 QString 了。
//   ③ 里面有一句 `static_assert(QtPrivate::HasQ_OBJECT_Macro<ObjType>::Value,
//      "No Q_OBJECT in the class passed to QObject::findChild")`（163~164 行）：
//      目标类**没写 Q_OBJECT 就是编译错误**，不是运行时找不到。这是第 5 讲那条
//      「没有 Q_OBJECT 就没有自己的元对象表」的直接后果——查找靠的是
//      ObjType::staticMetaObject。
// 默认选项 Qt::FindChildrenRecursively（qnamespace.h 1496 行 = 0x1）= 整棵子树都找；
// 不传名字重载（181/187 行）= 按类型找全部。
// ============================================================================

// ============================================================================
// 三、deleteLater() 解决的具体问题
// ----------------------------------------------------------------------------
// 它在 qobject.h 里被声明成 **public Q_SLOTS: void deleteLater();**（363~364 行）——
// 是槽，不是普通函数，这不是偶然：它的设计目标就是「被信号触发、并且能排在
// 当前这一串同步调用之后执行」。它干的事只有一件：
//     给这个对象投递一个 QEvent::DeferredDelete 事件，**立刻返回**。
// 真正 delete 的时间点是**事件循环收到 DeferredDelete** 那一刻（同一线程，
// 并且只有在回到事件循环之后）。于是：
//   · 你在槽里 deleteLater() 自己 → 槽函数剩下的代码照样跑完，对象还活着；
//   · 发射信号的代码（QMetaObject::activate 正在遍历那条连接链表）也不会踩到
//     刚被 free 掉的接收者；
//   · 关掉一个对话框、销毁一个正在处理点击的控件，都只能走这条路。
// 反面：在事件处理函数里**直接 delete this**，等于把「正在被遍历的连接表」
// 和「栈帧还没走完的那个对象」一起掀了。后果取决于内存是否立刻被复用——
// 可能崩在下一行，也可能「这次没事」，下次改一行代码就崩。这就是 Qt 文档反复
// 强调 deleteLater 的原因：**它把「销毁时机」推迟到一个安全点，而不是取消销毁。**
// 注意 deleteLater 依赖事件循环：没进 exec() 就 return，DeferredDelete 永远
// 不会被处理，对象**不会**被删（这是「我用 deleteLater 但内存没降」的头号原因）。
// ============================================================================

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qInfo() << "=== 1. 建树：parent 是构造函数给的，不是 new 之后补的 ===";
    Part *machine = new Part(QStringLiteral("机器"));      // 顶层（parent=nullptr）
    Part *m1 = new Part(QStringLiteral("马达1"), machine);
    Part *m2 = new Part(QStringLiteral("马达2"), machine);
    Part *bolt = new Part(QStringLiteral("螺丝"), m1);      // 孙辈：树的深度至少 3 层
    Part *orphan = new Part(QStringLiteral("游离件"));
    printTree("初始：1 个根 + 1 个游离件", machine);
    qInfo() << "  machine->children().size() =" << machine->children().size()
            << " m1->parent()->objectName() =" << m1->parent()->objectName()
            << " 活跃 Part 数 =" << Part::alive();
    // 游离件的 parent 是 nullptr：它不会被 machine 的析构管到，得自己负责。
    printTree("游离件自己就是一棵树", orphan);

    qInfo() << "=== 2. setParent：归属转移，两边表都要动（Qt6 返回 void！）===";
    // 【Qt5 → Qt6 差异，重要】Qt5 的签名是 `bool setParent(QObject*)`（失败返回 false）；
    // 本机 6.12 的 qobject.h 208 行是 **`void setParent(QObject *parent);`**，
    // 没有返回值可判。所以旧代码里 `if (!w->setParent(p)) return;` 这种写法在 Qt6
    // **编译不过**。要判失败只能自己在调用前检查（例如先比 objectName/类型）。
    orphan->setParent(m2);
    qInfo() << "  转移后：orphan->parent() =" << orphan->parent()->objectName()
            << " m2->children().size() =" << m2->children().size();
    printTree("orphan 从顶层搬进了 m2 底下", machine);
    // 反向操作：setParent(nullptr) = 从树上摘下来变成顶层对象，**不会删你**。
    orphan->setParent(nullptr);
    qInfo() << "  setParent(nullptr) 后 parent 为空？" << (orphan->parent() == nullptr)
            << " m2 子数 =" << m2->children().size();
    orphan->setParent(m2);                               // 再搬回去
    // 注意：QtWidgets 里 QWidget::setParent 还额外管「从旧窗口地图上摘下来、
    // 挂到新窗口」，失败情形更多；纯 QObject 的 setParent 只管这棵树，所以才有
    // 那个返回值的退化。别把 QWidget 的记忆直接套到 QObject 上。

    qInfo() << "=== 3. findChild / findChildren：按名字和按类型各查一次 ===";
    // 核对见上面第二节。T 一律写成 Part* / QObject*。
    Part *found = machine->findChild<Part*>(QStringLiteral("螺丝"));
    qInfo() << "  findChild<Part*>(\"螺丝\") →" << (found ? found->partName() : QStringLiteral("空指针"))
            << " found->parent()->objectName() =" << (found ? found->parent()->objectName() : QString());
    // 递归（默认）：整棵子树都找
    const QList<Part*> all = machine->findChildren<Part*>();
    QStringList names;
    for (const Part *p : all) names << p->objectName();
    qInfo() << "  findChildren<Part*>() 递归命中数 =" << all.size()
            << " 名字 =" << names.join(QStringLiteral(", "));
    // 递归 vs 只看直属子：默认 Qt::FindChildrenRecursively；换成
    // Qt::FindDirectChildrenOnly（qnamespace.h 1495 行 = 0x0）就只扫直接子对象。
    // 【别照着旧书抄这个名字】枚举量是 FindDirectChildrenOnly / FindChildrenRecursively
    // 两个，**没有** DoNotFindChildrenRecursively 这个名字（很多博客这么写）。
    const QList<Part*> direct = machine->findChildren<Part*>(
            QString(), Qt::FindDirectChildrenOnly);          // 空名字 = 不按名字筛
    QStringList dnames;
    for (const Part *p : direct) dnames << p->objectName();
    qInfo() << "  只找直属子（Qt::FindDirectChildrenOnly）命中 =" << direct.size()
            << " 名字 =" << dnames.join(QStringLiteral(", "));
    // QRegularExpression 版（qobject.h 194 行）：按正则筛名字
    const QList<Part*> byRe = machine->findChildren<Part*>(QRegularExpression(QStringLiteral("^马达")));
    qInfo() << "  findChildren<Part*>(QRegularExpression(\"^马达\")) 命中数 =" << byRe.size();
    // 查不到就返回**空指针**，不会崩；但 T 的类没 Q_OBJECT 是**编译错误**（static_assert）。
    Part *ghost = machine->findChild<Part*>(QStringLiteral("不存在的件"));
    qInfo() << "  查不存在的名字 → 得到空指针？" << (ghost == nullptr);

    qInfo() << "=== 4. deleteLater：登记是一行，兑现要等事件循环 ===";
    // 另建一棵小树专门演示延迟删除，免得和后面 delete machine 混在一起算不清账。
    Part *deferRoot = new Part(QStringLiteral("延迟根"));
    Part *deferKid  = new Part(QStringLiteral("延迟子"), deferRoot);
    QPointer<Part> deferKidPtr(deferKid);
    int slotHits = 0;
    // 正确姿势：在**这个对象自己的槽里**（= 正被 activate 遍历的那条调用栈里）deleteLater()
    QObject::connect(deferKid, &Part::ticked, deferKid,
                     [deferKid, &slotHits, &deferKidPtr](const QString &name) {
        qInfo() << "    [槽里] 收到" << name << "，准备 deleteLater()";
        deferKid->deleteLater();          // 只是**投递**一个 QEvent::DeferredDelete
        qInfo() << "    [槽里] deleteLater() 之后对象照样可用，objectName ="
                << deferKid->objectName() << " QPointer 还没空？" << (!deferKidPtr.isNull());
        ++slotHits;                       // 这行要是写在 `delete deferKid` 之后就是悬空访问
    });
    Q_EMIT deferKid->ticked(QStringLiteral("自检信号"));   // 同步派发，lambda 此刻就跑完了
    qInfo() << "  回到 main：slotHits =" << slotHits
            << " 活跃 Part 数 =" << Part::alive()
            << " → deleteLater 只登记没兑现，账上这个对象**还活着**";
    // 【反面教材·故意注释掉，别在 CI 里打开】把 lambda 里那行换成 `delete deferKid;`：
    //   1) 此刻 QMetaObject::activate 正拿着「deferKid 上这条连接」的节点指针往回走；
    //   2) ~QObject 会断开这个对象的全部连接、销毁 d_ptr，并**递归删它的子**；
    //      （顺带把它自己从 deferRoot 的 children 里摘掉——本机实测发射前后 1 → 0，
    //        所以**不会**再多删一刀，双删不是这里的失败形状。）
    //   3) 于是栈上那个节点已属于被释放内存 —— 结果不是「一定崩」而是「不一定崩」，
    //      改一行无关代码之后才崩，最难查。这就是 Qt 非要给 deleteLater 的原因。
    //   【本机实测，值得抄进你的记忆】把这个 lambda 改成 `delete deferKid;` 连跑 5 次：
    //     **5 次全部 exit 0**，~Part 在槽里就打印了，而且 delete 之后那两行 qInfo/++hits
    //     照样执行完。这正好是要你警惕的两件事：
    //       ① 「这次没崩」不是证据 —— 未定义行为的「这轮没崩」不代表下一行代码安全；
    //       ② delete 之后**代码会继续往下跑**，没人拦你。本讲 lambda 里就有一句现成的
    //          `deferKid->objectName()`，换成 delete 之后它读的是已释放内存
    //          （这一句我**没**跑：objectName() 要解引用 d_ptr，而 d_ptr 在 ~QObject 里
    //            已销毁 —— 这是源码推理，不是实测结论。实测到的只有「不崩 + 继续跑」。）
    // 【第二个坑】deleteLater **依赖事件循环**：调完就 return（不 exec()），
    //   DeferredDelete 永远不被处理，对象**不会被删** —— 「明明 deleteLater 了却泄漏」
    //   的头号原因。本讲第 9 节用定时器正面验证这个时间点。

    qInfo() << "=== 5. 父析构 = 整棵子树递归删除（用 QPointer 证明）===";
    QPointer<Part> boltPtr(bolt);         // 核对：qpointer.h 19 行，data() 75 行、isNull() 86 行
    QPointer<Part> m1Ptr(m1);
    QPointer<Part> orphanPtr(orphan);
    const int beforeAlive = Part::alive();
    delete machine;                        // 只删一个根
    qInfo() << "  delete machine 之前活跃数 =" << beforeAlive
            << "之后 =" << Part::alive()
            << "（machine 下面整棵树：机器+马达1+螺丝+马达2+游离件 全没了）";
    qInfo() << "  boltPtr.isNull() =" << boltPtr.isNull()
            << " m1Ptr.isNull() =" << m1Ptr.isNull()
            << " orphanPtr.isNull()（挂在 m2 下）=" << orphanPtr.isNull();
    // 这就是工程里最常见的悬空事故：**你手里那个子对象裸指针，在父析构之后就不作数了。**
    // 两个正规解法：① QPointer<T>（父删了你自动变 null，判空即可）；
    //                ② 连接 QObject::destroyed 信号（qobject.h 352 行，
    //                   `void destroyed(QObject * = nullptr)`）在析构时把指针置空。
    machine = nullptr; m1 = nullptr; m2 = nullptr; bolt = nullptr;   // 明确表态：这些指针作废

    qInfo() << "=== 6. destroyed 信号：长期存别人的指针时怎么自保 ===";
    Part *host = new Part(QStringLiteral("宿主"));
    Part *payload = new Part(QStringLiteral("负载"), host);
    QPointer<Part> payloadPtr(payload);
    int destroyedCount = 0;
    QObject::connect(payload, &QObject::destroyed, [&destroyedCount](QObject *obj) {
        ++destroyedCount;
        qInfo() << "    destroyed(QObject*) 回调：指针 =" << obj
                << "（此刻对象已在析构中，只比地址，**别调它的任何成员**）";
    });
    printTree("delete host 之前", host);
    const int aliveBeforeHost = Part::alive();
    delete host;              // 父析构：先从它爹的 children 里摘掉自己，再递归删自己的子
    qInfo() << "  delete host：活跃数" << aliveBeforeHost << "→" << Part::alive()
            << " destroyed 回调次数 =" << destroyedCount
            << " payloadPtr.isNull() =" << payloadPtr.isNull();
    // 两条自保路线本讲都演了：
    //   ① destroyed 信号（qobject.h 352 行 `void destroyed(QObject * = nullptr)`）：
    //      你自己写「把它从我容器里剔除」的代码，成本是一次 connect，但要注意
    //      回调里那个对象**已经进了析构**，虚函数表已退化，调 getter/槽都不安全；
    //   ② QPointer<T>：同一件事的官方封装，父删了它自己变 null，你只管判空。
    //      QPointer 不接管所有权、也不阻止删除，它只是「帮你看见已经没了」。

    qInfo() << "=== 7. QObjectCleanupHandler：一批寿命不定的临时对象，一起清 ===";
    // 核对：qobjectcleanuphandler.h 只有 4 个公开成员（20~23 行）
    //   QObject *add(QObject *object);   void remove(QObject *object);
    //   bool isEmpty() const;            void clear();
    // 内部就是一个 `QObjectList cleanupObjects;`（27 行）+ 一个 private slot
    // objectDestroyed(QObject*)（30 行）。
    // add()（同文件 cpp 59~67 行）只做两件事：给这个对象连上
    //   connect(object, SIGNAL(destroyed(QObject*)), this, SLOT(objectDestroyed(QObject*)))  ← 64 行
    // 然后 cleanupObjects.insert(0, object)                                              ← 65 行
    // 注意 insert(0)：登记是**往名单头里塞**。clear()（101~105 行）是
    //   while (!cleanupObjects.isEmpty()) delete cleanupObjects.takeFirst();             ← 104 行
    // 头塞头删，所以**后登记的先死**。析构函数（48~51 行）本身只调 clear()。
    QObjectCleanupHandler cleaner;
    Part *a = new Part(QStringLiteral("临时A"));     // 无父：谁也不管它，只能自己管
    Part *b = new Part(QStringLiteral("临时B"));
    Part *c = new Part(QStringLiteral("临时C"));
    cleaner.add(a); cleaner.add(b); cleaner.add(c);
    qInfo() << "  登记 3 个，isEmpty() =" << cleaner.isEmpty();
    delete b;                                  // B 自己先死（比如它挂在别的树上）
    qInfo() << "  delete b 之后，名单里还剩几个？isEmpty() =" << cleaner.isEmpty()
            << "（内部靠 destroyed 自动划名，不用你自己维护列表）";

    // 删除顺序实测：a、c 还在名单里，给它们各自的 destroyed 连一句打印，谁先响就是
    // clear() 先删了谁。回调里只打印、不碰 obj 的任何成员——信号发出时它已在析构中。
    int clearOrder = 0;
    QObject::connect(a, &QObject::destroyed, [&clearOrder] {
        qInfo() << "      临时A 的 destroyed 响，它是这轮里第" << ++clearOrder << "个死的";
    });
    QObject::connect(c, &QObject::destroyed, [&clearOrder] {
        qInfo() << "      临时C 的 destroyed 响，它是这轮里第" << ++clearOrder << "个死的";
    });
    cleaner.clear();                           // 剩下的（a 和 c）立刻删掉
    qInfo() << "  clear() 之后活跃 Part 数 =" << Part::alive()
            << " isEmpty() =" << cleaner.isEmpty()
            << "（上面两行谁先响，就是 clear() 先删了谁 —— 按 65/104 行，应该是后登记的 C 先死）";

    // 【实测纠正一条流传很广的说法】「已经有爹的对象别交给它管，否则双删」——
    // 本机跑下来**不成立**，下面这段就是证据：
    Part *root7 = new Part(QStringLiteral("第七节根"));
    Part *kid7 = new Part(QStringLiteral("第七节子"), root7);   // 明明白白有爹
    cleaner.add(kid7);
    qInfo() << "  登记一个「有爹的孩子」，现在 delete 它的爹（传闻这里会双删）：";
    delete root7;
    qInfo() << "    删完爹 cleaner.isEmpty() =" << cleaner.isEmpty()
            << " 活跃 Part 数 =" << Part::alive()
            << "（这行能印出来就是没当场崩；本讲整体退出码 0，见 runlogs 同名日志末行）";
    // 为什么不会双删：`emit destroyed(this)` 就写在 ~QObject 里（本机 qobject.cpp
    // 1091~1092 行），而父的 deleteChildren() 在同函数更靠后的 1189~1190 行；孩子自己
    // 一响 destroyed，cleaner 的 objectDestroyed → remove() 就把它划掉了，clear() 时名单
    // 里根本没有它。Qt 官方文档原话（qobjectcleanuphandler.cpp 26~27 行）：
    //   "If an object being tracked by the cleanup handler gets deleted by someone else
    //    it will automatically be removed from the cleanup handler."
    // 真正该小心的不是 cleaner，是**你自己手里那把裸指针**：kid7 此刻已经悬垂，
    // cleaner 一点都帮不到它 —— 能帮的只有第 6 节的 QPointer<T> 或自己置空。
    kid7 = nullptr;                            // 别让悬垂指针继续待在作用域里骗人
    // add() 不 setParent、也不接管「别人已经有的爹」：它只是「名单 + 收尾时 delete
    // 名单里还活着的」。所以它适合「一批各自有寿命的对象」，不适合当所有权模型。

    qInfo() << "=== 8. dumpObjectTree()：调试期最好用的一行「看树」=== ";
    Part *root2 = new Part(QStringLiteral("根2"));
    new Part(QStringLiteral("子2a"), root2);
    Part *renamed = new Part(QStringLiteral("子2b"), root2);
    renamed->setObjectName(QStringLiteral("子2b改名"));
    // dumpObjectTree()/dumpObjectInfo() 声明在 qobject.h 340/341 行，输出走 qDebug()。
    // 【勘误】这下面原先写的是「只在 debug 构建里有实质内容，release 里是空实现」——错的，
    // 三条证据：
    //   1) 两个函数体里压根没有 #ifdef：本机 qobject.cpp 4700~4703（dumpObjectTree）
    //      和 4714~4717（dumpObjectInfo）就是普通实现；
    //   2) 真能把 qDebug 整条编译掉的是 QT_NO_DEBUG_OUTPUT（qlogging.h 180~183 行
    //      把 qDebug 换成 QT_NO_QDEBUG_MACRO），而 CMake 连 QT_NO_DEBUG_OUTPUT 都不给你定义；
    //      Qt6 在非 Debug 配置里只加 QT_NO_DEBUG（Qt6Targets.cmake 140 行、
    //      Qt6CoreConfigExtras.cmake 15 行那句 `$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG>`），
    //      而 QT_NO_DEBUG 只影响 QT_MESSAGELOGCONTEXT（qlogging.h 151~157 行）
    //      ——少的是文件名/行号，不是打印本身；
    //   3) 本机拿这份源码按 Release 的宏配置（-O2 -DNDEBUG -DQT_NO_DEBUG）重编再跑：
    //      下面这几行一字不差照样打出来，退出码 0。
    // 所以 debug/release 的真实差别只有「日志里还带不带 file:line」。
    // 真正该记住的是另一句：这两个函数是**给人调试用的**，输出格式不是 API，
    // 别把它的文本喂给自己的代码当运行时数据源。
    root2->dumpObjectTree();
    root2->dumpObjectInfo();
    // 【实测·同一次调用里中文怎么就坏了】上面两组输出你会看见一个刺眼的差别：
    // dumpObjectTree 那三行的中文是对的，dumpObjectInfo 第一行却是 `OBJECT Part::??2`。
    // 同一个 qDebug、同一个源文件，差别只在格式说明符：
    //   · qobject.cpp 4683~4685（dumpRecursive）用 `%ls` + qUtf16Printable(...)，
    //     vasprintf 见 %ls 直接按 const char16_t* 收（qstring.cpp 7583~7590），UTF-16 原样进串；
    //   · qobject.cpp 4716~4717 用 `%s` + `objectName().toLocal8Bit().data()`，
    //     vasprintf 见 %s 走 QString::fromUtf8（qstring.cpp 7592）——本机 toLocal8Bit
    //     出来的是 GBK 字节，拿它当 UTF-8 解，非法字节各换一个 U+FFFD，
    //     再经 GBK 控制台就是那个 ??。
    // 下面这几行把这条链当场复现一遍，数字自己数出来：
    const QByteArray nameGbk = root2->objectName().toLocal8Bit();     // 本机 = GBK 字节
    const QString misDecoded = QString::fromUtf8(nameGbk);            // Qt 那行 %s 做的事
    const qsizetype fffd = misDecoded.count(QChar(0xFFFD));
    qInfo().noquote() << QStringLiteral(
                                "  复现上面那行 `??`：名字「%1」的本地 8 位字节 = %2（%3 个字节），"
                                        "按 UTF-8 硬解得 %4 个字符，其中替换符 U+FFFD 有 %5 个")
            .arg(root2->objectName())
            .arg(QString::fromLatin1(nameGbk.toHex(' ')))   // hex 全是 ASCII，fromLatin1 才安全
            .arg(qint64(nameGbk.size()))
            .arg(qint64(misDecoded.size()))
            .arg(qint64(fffd));
    if (fffd > 0) {
        qInfo().noquote() << QStringLiteral(
                                    "    ← 本机正是这一种（locale 是 GBK）：%1 个 U+FFFD 就对应上面 "
                                    "`OBJECT Part::??2` 里那几个 ?；把那句改成 "
                                    "qDebug(\"%s\", name.toUtf8().constData()) 就一致了。")
                .arg(qint64(fffd));
    } else {
        qInfo().noquote() << QStringLiteral(
                                    "    ← 这台机器的 locale 本身就是 UTF-8，toLocal8Bit 等于 toUtf8，"
                                    "没有替换符（%1 个），上面 dumpObjectInfo 那行的中文应当是好的。"
                                    "同一段 Qt 代码在两种 locale 下症状不同——所以别背症状，"
                                    "要背「哪些字节被喂进了哪个解码器」。").arg(qint64(fffd));
    }
    printTree("手工快照（和 dumpObjectTree 那三行对齐）", root2);
    delete root2;      // 整棵子树走掉

    // ── 自动脚本：让事件循环真的转一轮，把 DeferredDelete 兑现 ──────────
    // （引用捕获 main 作用域里的局部变量是安全的：lambda 在 app.exec() 内执行，
    //   而这些变量活到 exec() 返回之后才销毁 —— 第 4 讲的规矩在这儿同样成立。）
    QTimer::singleShot(80, [&deferKidPtr, &deferRoot] {
        qInfo() << "=== 9. 事件循环转过一轮：第 4 节那笔 deleteLater 结清了 ===";
        qInfo() << "  deferKidPtr.isNull() =" << deferKidPtr.isNull()
                << "（true = DeferredDelete 已被事件循环兑现，对象真的 delete 了）";
        qInfo() << "  它爹 deferRoot 还活着：" << deferRoot->objectName()
                << " children().size() =" << deferRoot->children().size()
                << " 活跃 Part 数 =" << Part::alive();
        printTree("延迟根手下少了一个", deferRoot);
    });
    QTimer::singleShot(160, [&deferRoot] {
        qInfo() << "=== 10. 收尾：delete deferRoot 看总账 ===";
        const int before = Part::alive();
        delete deferRoot;              // 树上最后一根，删完本讲不该再有 Part 存活
        deferRoot = nullptr;
        qInfo() << "  活跃数" << before << "→" << Part::alive()
                << "（deferKid 早在第 9 节就被 deleteLater 结掉了，这里只回收 deferRoot 自己）";
    });
    QTimer::singleShot(300, [] {
        qInfo() << "=== 第 7 讲演示结束，自动退出 ===";
        QCoreApplication::quit();
    });

    return app.exec();
}

// Q_OBJECT 类写在 .cpp 里 → AUTOMOC 产出的就是 <本文件名>.moc，必须手动包含，
// 且必须放在所有类定义**之后**、main 之后也照样合法（它只是普通 include）。
#include "07_qobject_tree.moc"

// ——— 老师傅的话 ———
/*
 * 徒弟，这一讲的真正考点不是 API，是**「谁负责删」这一句话你答不答得上来**。
 * Qt 的答案是：一个 QObject 只有一个爹，爹死了儿子跟着死；除此之外的所有权
 * 描述（「我 new 的我删」「智能指针管着」）都是给这套规则打补丁，别自己发明。
 *
 * 于是有三条我要求你刻进肌肉记忆的规矩：
 *
 *   一、**栈上对象别挂到有堆寿命的爹下面。**
 *   `Part p("x"); new Part("y", &p);` 看着挺美，但 p 一旦离开作用域就 delete 它的
 *   children；而「p 自己作为别人的 child」时更糟：爹析构会 delete 一个栈地址，
 *   直接崩。规矩：挂到树上的对象就一律 `new`，由树管；不挂树的一律自己管或智能指针。
 *
 *   二、**手里存别人的子对象裸指针，就要接受它会突然作废。**
 *   父析构发生在你不控制的那一刻（关窗口、切页面、模型重建）。所以长期持有的
 *   指针只有两种安全写法：`QPointer<T>`（自动置空，判空即可），或连
 *   `QObject::destroyed` 自己置空。临时用完不存，就不必付这个成本。
 *
 *   三、**「正在处理这个对象的事件时」不要 delete 它，只能 deleteLater()。**
 *   deleteLater 不是「异步删除的语法糖」，它是把销毁推迟到事件循环里一个
 *   **安全的点**：那时当前这串同步调用（包括 activate 遍历连接表）都已经返回了。
 *   反过来也要记住：deleteLater **依赖事件循环**，没进 exec()（或线程没有事件循环）
 *   它就永远不兑现——「明明 deleteLater 了却泄漏」八成是这个。
 *
 * 另外两处「和旧书对不上」的地方，本讲都实测在头文件里了，别按 Qt5 的记忆写：
 * `setParent` 在 Qt6 返回 **void**（qobject.h 208 行），旧代码 `if (!setParent(...))`
 * 编不过；`findChild<T>` 的 T 是**指针类型**、名字参数是 **QAnyStringView**（159~199 行），
 * 而且它有 static_assert 强制要求目标类写 Q_OBJECT——这条编译错误比运行时找不到好一万倍。
 *
 * 最后一句：children() 返回的是**引用**（206 行）。边遍历边 setParent/delete，
 * 就是边开车边拆轮胎。要动就先拷贝一份 QList<QObject*>，或者倒着来。
 */

// ——— 小练习 ———
/*
 *   1. 给 Part 加一个 `QPointer<Part> m_sibling;` 并在构造时指向「同一爹的下一个孩子」，
 *      然后 `delete machine;` 打印 m_sibling.isNull()；再把 QPointer 换成裸指针，
 *      在 ASan/调试器下观察它变成悬空指针的瞬间。写出「什么时候值得付 QPointer 的成本」。
 *   2. 把第 4 节 lambda 里的 `deferKid->deleteLater()` 换成 `delete deferKid;`
 *      （**只在调试器里跑一次**），把调用栈抄下来：你要能在栈里认出 QMetaObject::activate
 *      和 ~Part。本机实测这个改法连跑 5 次都是 exit 0，所以请把注意力放在**栈**上而不是
 *      崩溃与否：找出 activate 停在哪个帧、以及 delete 之后 lambda 还剩几行在跑。
 *      再验证一遍「没进 exec() 时 deleteLater 不兑现」：把 deleteLater 放在 main 里、
 *      然后在 app.exec() 之前就 return，观察 Part::alive() 不减。
 *   3. 第 7 节已经演过「登记一个有爹的孩子，再删它的爹」——**没有双删**（实测
 *      isEmpty() 变 true、进程正常退出）。请在此基础上改出**会**出事的版本：
 *        a) 把 cleaner 换成 `QPointer<Part>`，再 `delete root7`，对比两者各自告诉你什么；
 *        b) 保留 cleaner，另外用一个 `QList<Part*>` 存同一个对象，然后 delete root7，
 *           遍历打印这个 QList —— 你要在输出里认出「名单自己干净了，你的名单没干净」。
 *      再用源码注释写出 QObjectCleanupHandler::add() **不**接管所有权、clear() 走
 *      `insert(0)` + `takeFirst()` 因此**后登记的先死**这两件事（本机实测顺序见第 7 节输出），
 *      并给出你在真实项目里会怎么用这个类（提示：插件、临时对话框、一堆各自有寿命的解析节点）。
 */
// ============================================================================
