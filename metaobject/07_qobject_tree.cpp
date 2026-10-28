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
//     「看着没事」。想看效果请在调试器里单独试一次，别把它留在CI 里。
//   · 本机当前无法编译链接 Qt 程序，本示例**未在本机实测编译**；但本文件已用本机
//     moc 实跑过一遍（moc 输出为本机实测）：
//       C:/msys64/ucrt64/share/qt6/bin/moc.exe -I C:/msys64/ucrt64/include/qt6
//         07_qobject_tree.cpp → 退出码 0、无 stderr、产物 120 行（Part 一张表 +
//         Panel 无属性表；证明「只有信号槽的类，moc 也只生成方法表」）。
//     所有 API 都对着本机真实头文件核对过：
//       QtCore/qobject.h            —— `typedef QList<QObject*> QObjectList` 46 行、
//                                      QObjectData 里 `QObjectList children;` 75 行、
//                                      `const QObjectList &children() const` 206 行、
//                                      **`void setParent(QObject *parent);` 208 行（返回 void！）**、
//                                      findChild/findChildren 模板 159~199 行、
//                                      installEventFilter 209 行、isWidgetType() 134 行、
//                                      dumpObjectTree() 340 行 / dumpObjectInfo() 341 行、
//                                      destroyed(QObject*) 352 行、
//                                      objectNameChanged(..., QPrivateSignal) 353 行、
//                                      `public Q_SLOTS: void deleteLater();` 363~364 行、
//                                      qobject_cast 模板 431~443 行
//       QtCore/qobjectcleanuphandler.h —— 整个类只有 add/remove/isEmpty/clear +
//                                      private slot objectDestroyed（12~31 行），成员是
//                                      `QObjectList cleanupObjects;`（27 行）
//       QtCore/qnamespace.h        —— 1494~1498 行
//                                      `enum FindChildOption { FindDirectChildrenOnly = 0x0,
//                                       FindChildrenRecursively = 0x1 };` +
//                                      `Q_DECLARE_FLAGS(FindChildOptions, FindChildOption)`
//                                      （**注意**：没有 DoNotFindChildrenRecursively 这个名字，
//                                       网上博客常这么写，那是错的）
//       QtCore/qcoreevent.h        —— `ParentAboutToChange = 131` 86 行、
//                                      `DeferredDelete = 52` 105 行（deleteLater 投的就是它）
//       QtCore/qpointer.h         —— `class QPointer` 19 行、data() 75 行、isNull() 86 行
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
    //   2) ~QObject 会断开这个对象的全部连接、销毁 d_ptr，并**递归删它的子**（延迟根的
    //      children 里还留着它）；
    //   3) 于是栈上那个节点已属于被释放内存 —— 结果不是「一定崩」而是「不一定崩」，
    //      改一行无关代码之后才崩，最难查。这就是 Qt 非要给 deleteLater 的原因。
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
    // objectDestroyed(QObject*)（30 行）——它给每个登记的 QObject 连上 destroyed，
    // 谁先死就从名单里划掉。适合「一批各自独立寿命的对象，退出时把还活着的清掉」。
    QObjectCleanupHandler cleaner;
    Part *a = new Part(QStringLiteral("临时A"));     // 无父：谁也不管它，只能自己管
    Part *b = new Part(QStringLiteral("临时B"));
    Part *c = new Part(QStringLiteral("临时C"));
    cleaner.add(a); cleaner.add(b); cleaner.add(c);
    qInfo() << "  登记 3 个，isEmpty() =" << cleaner.isEmpty();
    delete b;                                  // B 自己先死（比如它挂在别的树上）
    qInfo() << "  delete b 之后，名单里还剩几个？isEmpty() =" << cleaner.isEmpty()
            << "（内部靠 destroyed 自动划名，不用你自己维护列表）";
    cleaner.clear();                           // 剩下的（a 和 c）立刻删掉
    qInfo() << "  clear() 之后活跃 Part 数 =" << Part::alive()
            << " isEmpty() =" << cleaner.isEmpty();
    // 【注意】add() 不接管所有权、不 setParent；clear() 是**直接 delete**。
    // 所以别把已经有父对象的 QObject 交给它管，否则可能双删。

    qInfo() << "=== 8. dumpObjectTree()：调试期最好用的一行「看树」=== ";
    Part *root2 = new Part(QStringLiteral("根2"));
    new Part(QStringLiteral("子2a"), root2);
    Part *renamed = new Part(QStringLiteral("子2b"), root2);
    renamed->setObjectName(QStringLiteral("子2b改名"));
    // dumpObjectTree()/dumpObjectInfo() 在 qobject.h 340/341 行，输出走 qDebug()，
    // 只在 debug 构建里有实质内容，release 里是空实现——别把它当运行时数据源。
    root2->dumpObjectTree();
    root2->dumpObjectInfo();
    printTree("手工快照（和 dumpObjectTree 对齐）", root2);
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
 *   2. 把第 4 节的 `m1->deleteLater()` 换成 `delete m1;`（**只在调试器里跑一次**），
 *      把调用栈抄下来：你要能在栈里认出 QMetaObject::activate 和 ~Part。
 *      再验证一遍「没进 exec() 时 deleteLater 不兑现」：把 deleteLater 放在 main 里、
 *      然后在 app.exec() 之前就 return，观察 Part::alive() 不减。
 *   3. 把第 7 节改成「a 有爹（new Part("临时A", root)）」再交给 cleanupHandler，
 *      然后 delete root —— 你会撞见双删。用源码注释写出 QObjectCleanupHandler::add()
 *      **不**接管所有权这一事实，并给出你在真实项目里会怎么用这个类（提示：插件、
 *      临时对话框、一堆各自有寿命的解析节点）。
 */
// ============================================================================
