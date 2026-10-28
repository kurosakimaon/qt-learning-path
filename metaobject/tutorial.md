# 元对象系统 · Meta-Object（Qt 从入门到资深）

> 老师傅带你把 Qt 最底层那一层机制拆开：从「会用信号槽」到「读懂元对象表」

## 一、模块简介

徒弟，这一关是**元对象系统（Meta-Object System）**。前面「起步」模块你已经把窗口跑起来了，但那一层底下真正让 Qt 区别于普通 C++ 的东西，我们还没拆开看过：**信号与槽（signal/slot）、`Q_OBJECT` 与 `moc`、属性系统（`Q_PROPERTY`）、`QObject` 父子树与所有权、运行时反射（`QMetaObject`）**。

C++ 本身没有「按名字通知」，也没有「运行时才知道的槽」。Qt 的答案是：**在编译前用 `moc` 给你的类生成一张静态描述表 + 一个 `switch` 派发函数 + 每个信号一段发射代码**。这张表一旦在你脑子里成形，下面这些问题就不再有神秘感：

- 为什么「没连接时发射信号」什么也不发生、也不报错？
- 为什么重复 `connect` 同一个槽就会跑两遍？
- 为什么忘了 `Q_OBJECT` 之后自定义信号连不上、`qobject_cast` 返回空、`findChild` 直接编译不过？
- 为什么 `connect` 出来的链接期错误长成 `undefined reference to vtable for X`？
- 为什么改了属性、界面却不刷新？为什么属性名少打一个字母不报错？

本模块一共 **8 讲**，前 4 讲教你**用对**，第 5 讲把 `moc` **拆开看**，后 3 讲讲**长在这张表上的三种能力**。和全课一致：每讲都是完整示例，都设计成**无人值守能自动跑完**（`QTimer::singleShot` 排脚本 + `qInfo()` 吐结论 + `QCoreApplication::quit()`）。

> 环境事实：Qt 6.12.0、Windows、MinGW-w64（MSYS2 ucrt64，gcc 15.2/16.2）、CMake ≥ 3.21 + Ninja、C++17。构建走 `find_package(Qt6 COMPONENTS Core Widgets)` + `Qt6::Core`/`Qt6::Widgets` + `CMAKE_AUTOMOC=ON`。
>
> 诚实标注：**本机无法编译链接 Qt 程序**，8 个示例**都未在本机实测编译**，所有 API 都对着 `C:\msys64\ucrt64\include\qt6` 里的真实头文件核对过（注释里给了文件名与行号）。唯一实测的是 **moc 产物**：第 5、6、7、8 讲都真用了 `C:\msys64\ucrt64\share\qt6\bin\moc.exe`（"The Qt Meta Object Compiler version 69 (Qt 6.12.0)"）跑过，输出的行数是真实的。

## 二、学习目标

学完本模块，你要能做到：

1. 说清 `signals:` / `slots:` / `emit` 三个关键字**宏展开后各是什么**（`signals:` 白送 public，裸 `slots:` 是语法错误），并解释「信号只声明不实现」的根因
2. 分清 `connect` 的几种写法（成员函数指针 / 字符串 `SIGNAL()`+`SLOT()` / lambda / 带 context 的 lambda）与各种 `Qt::ConnectionType` 的差别，并能说出**为什么内部代码一律用函数指针版**
3. 会管连接的生命周期：保存并使用 `QMetaObject::Connection`、正确 `disconnect`、理解「接收者销毁即自动断开」，以及带 context 的 lambda 为什么能救命
4. 打开 `moc` 产物逐段读懂：**字符串池 + 元数据数组 + `qt_static_metacall` 的 switch + `metaObject()/qt_metacast/qt_metacall` + 每个信号的发射体**，并解释 `SIGNAL(x)` 那个数字前缀
5. 用 `Q_PROPERTY` 声明可读可写可通知的属性，说清 `READ/WRITE/NOTIFY/RESET/MEMBER/DESIGNABLE/CONSTANT/FINAL/REQUIRED/BINDABLE` 各自的**实际后果**，并能区分静态属性与动态属性、说清「写属性 = 调 setter，Qt 不替你发 NOTIFY」
6. 讲明 `QObject` 父子树的归属规则（`setParent` 转移、父析构递归删子）、`findChild/findChildren` 的真实模板签名、`deleteLater()` 解决的确切问题，并会用 `QPointer`/`destroyed`/`QObjectCleanupHandler` 三种工具自保
7. 用 `QMetaObject`/`QMetaMethod`/`QMetaProperty`/`QMetaEnum` **遍历并写回**一个未知 `QObject`，并解释 `qobject_cast` 与 `dynamic_cast` 的三条真实差别

## 三、文件列表与学习重点

一共 8 个示例（外加第 5 讲配套的 moc 实测输出 `05_moc_output.txt`），按顺序来，别跳：

```text
  01_signal_slot_basics.cpp        ← 自定义信号与槽的第一课
    重点：signals:/slots: 宏展开的不对称（qtmetamacros.h 48~49 行）；信号只声明、实现体由
          moc 生成；发射 = 遍历该信号的连接表逐个调用；一个信号 0/1/N 个接收者；
          重复 connect 会重复调用；槽是普通成员函数（可直接调）；信号参数必须可拷贝的根因

  02_connect_variants.cpp          ← connect 的各种写法与连接类型
    重点：函数指针版 / 字符串 SIGNAL-SLOT 版 / lambda 版 / 带 context 版的取舍与代价；
          Qt::Auto·Direct·Queued·BlockingQueued·UniqueConnection 各改变了什么；
          为什么「编译期就把调用点定死」的写法要当默认选择

  03_disconnect_and_connection.cpp ← 断开连接与 QMetaObject::Connection
    重点：connect 返回的句柄（QMetaObject::Connection）怎么用、怎么判有效；
          disconnect 的几种重载与「断哪一条」的语义；接收者销毁时连接如何自动断开；
          「忘记断开」与「重复登记」两类账怎么算清

  04_lambda_and_lifetime.cpp       ← lambda 槽与生命周期（第二道坎）
    重点：按值捕获 vs 按引用捕获在槽里的真实差别；忘记传 context object 的后果；
          对象已销毁而 lambda 仍被调用 = 悬空；带 context 的 connect 为什么能自动断开；
          规矩：lambda 一律显式捕获 + 传 context

  05_moc_anatomy.cpp               ← moc 解剖（第一道坎，附本机实测产物）
    重点：moc 是编译前的独立程序，不是宏展开；Q_OBJECT 只声明（qtmetamacros.h 139~152 行）；
          产物五件套：字符串池 / 元数据数组 / qt_static_metacall（switch 派发）/
          metaObject()+qt_metacast+qt_metacall / 每个信号一个 activate 发射体；
          名字→索引→switch 的派发链与局部索引 vs 全局索引；SIGNAL()/SLOT() 的类型码前缀
          （配套 05_moc_output.txt = 本机 moc 6.12.0 对该文件的真实输出）

  06_property_system.cpp           ← Q_PROPERTY 与属性系统
    重点：Q_PROPERTY 展开成空注解（qtmetamacros.h 60 行）→ 编译器看不见它；
          本机 moc 真实接受的 12 个关键字 vs 会 Parse error 的 5 个；
          写属性 = 走 WriteProperty → setter；**Qt 不替你发 NOTIFY**（raw 属性 0 次触发为证）；
          RESET/CONSTANT/FINAL/USER/DESIGNABLE 的可观测后果；静态属性 vs 动态属性
          （setProperty/property/dynamicPropertyNames）两种完全不同的失败方式；
          Q_ENUM 让枚举属性能按名字写；Qt6 属性绑定的真实入口
          （QBindable<T>(obj,"name")、QProperty<T>::setBinding、Qt::makePropertyBinding；
           实测核对：qproperty.h 里**没有 bind() 这个成员**，所以 property("x").bind(...) 编不过）

  07_qobject_tree.cpp              ← QObject 父子树与所有权
    重点：children() 返回引用（qobject.h 206 行）、**setParent 在 Qt6 返回 void**（208 行，
          Qt5 是 bool）；父析构递归 delete 子对象；findChild<T>/findChildren<T> 的模板签名
          （T 是指针、名字参数是 QAnyStringView、内置 static_assert 要求目标类有 Q_OBJECT）、
          Qt::FindDirectChildrenOnly vs FindChildrenRecursively；
          deleteLater() 只投 QEvent::DeferredDelete，兑现要等事件循环；
          在槽里直接 delete 自己为什么危险（反面代码已注释）；QPointer / destroyed 两条自保路线；
          QObjectCleanupHandler 的 add/remove/isEmpty/clear；每节都打印父子树快照

  08_introspection.cpp             ← 元反射：读那张表，也用它写回
    重点：className/methodCount/propertyCount/enumeratorCount 与 methodOffset/propertyOffset
          这对「分水岭」；QMetaMethod 的 name/methodType/parameterTypes/parameterNames/invoke；
          QMetaProperty 的 read/write/reset/notifySignal/isWritable/isConstant；
          indexOfXxx 查不到给 -1（**别拿 -1 去构造表条目**，method(int) 不做越界检查）；
          QMetaObject::invokeMethod 四个字符串重载怎么选、Q_ARG 类型为什么要精确；
          Q_ENUM/Q_FLAG 与 QMetaEnum（keyCount/key/valueToKey/keyToValue/keysToValue/isFlag）；
          qobject_cast vs dynamic_cast 的三条真实理由（不碰 RTTI、更便宜、只认 Q_OBJECT）
```

> 第 5 讲为什么多一个 `05_moc_output.txt`：它是 `moc.exe 05_moc_anatomy.cpp` 的**本机真实产物**，用来把「一张表 + 一个 switch」这件事钉死在看得见的代码上。它是生成物，正常情况下不该进版本库——这里特意留下，是因为它同时是本讲的**教具**。

## 四、学习顺序建议

师傅给你划这条主线，和两道硬坎：

### 主线：为什么是这个顺序

- **第 1 → 第 2**：先建立「信号 = 被 moc 写好实现体的 public 成员函数，发射 = 遍历连接表」这个心智模型，再谈 `connect` 有哪几种写法和连接类型——顺序反了你会背一堆规则却不知其所以然。
- **第 2 → 第 3**：会连了，紧接着学**断**。连接是有身份的对象（`QMetaObject::Connection`），不理解它就没法解释「为什么槽还在跑」「为什么对象没了还在跑」。
- **第 3 → 第 4**：断开问题的最大来源就是 lambda 捕获的悬空指针。第 4 讲把「谁先死」这件事正面解决，并立下一条会贯穿全课的规矩：**带 context、显式捕获**。
- **第 4 → 第 5**：前四讲你在**用**这套机制；第 5 讲把引擎盖掀开，给前四讲的每条规矩找到**物证**。这是本模块的中枢。
- **第 5 → 第 6**：属性系统是元对象表的第二个消费者（第一个是信号槽）。你已经会读表了，`Q_PROPERTY` 只是表里多出来的那一段。
- **第 6 → 第 7**：属性管「一个对象的状态」，对象树管「一批对象的命」。所有权规则不清，属性通知就会打在已析构的对象上——第 4 讲的事故在这里换个面孔再来一次。
- **第 7 → 第 8**：最后一讲把表**反过来读**：不看自己写的类，而看任意一个 `QObject` 会什么。到这一步，前七讲的地基刚好用齐。

### 硬坎一：moc（05 号文件）

`Q_OBJECT` / `AUTOMOC` / `#include "xxx.moc"` 这三样，新手一律靠「抄能跑的例子」过日子，于是每次改结构就崩一次。你要在练习里**亲手撞齐三种报错**：忘写 `Q_OBJECT`（自定义信号连不上、`qobject_cast` 返回空）、没开 `CMAKE_AUTOMOC`（满屏 `undefined reference to vtable for X`）、`.cpp` 里的类忘 include `.moc`（只有那个类出问题）。撞过这三回，`moc` 就再也吓不到你。

### 硬坎二：lambda 生命周期（04 号文件）

这道坎「不疼」，所以最容易被跳过：悬空捕获常常**这次没事**，改一行无关代码之后才崩。04 号示例故意把安全写法和危险写法并排放，请一定按注释跑一遍对照输出，把「传 context」变成不用想的条件反射。

### 学习方法

每个文件都要：先读顶部注释块（「本讲解决什么问题」+「运行前须知」）→ 按注释里的 CMake 目标名（`metaobject_0N_xxx`）构建 → 跑一遍看自动结论 → 做「小练习」改代码 → 改出问题了回头解释为什么。光看不练，等于白看。

> 🎬 想看这张表怎么被生成、被派发：
> [图解 · moc 与信号槽](../visuals/moc_signal_slot.html)、
> [图解 · 连接类型](../visuals/connection_types.html)、
> [图解 · QObject 对象树](../visuals/qobject_tree.html)。

## 五、核心知识点梳理

把最重要的一件事讲透：**`moc` 把 `Q_OBJECT` 展开成一张静态描述表 + 一个 switch 派发函数，信号发射就是查表遍历接收者。**

### 1. 编译前：`moc` 生成「一张表」

你写的只有声明：

```cpp
class Lever : public QObject {
    Q_OBJECT                                  // 只声明，不实现（qtmetamacros.h 139~152 行）
    Q_PROPERTY(int level READ level WRITE setLevel NOTIFY levelChanged)
public Q_SLOTS:
    void onLevelChanged(int level);
Q_SIGNALS:
    void levelChanged(int level);
};
```

`moc` 读源码文本（注意：`Q_PROPERTY`/`Q_OBJECT` 在 C++ 眼里是**空注解**，编译器什么信息都没拿到），生成 `<文件名>.moc`，里面是：

```cpp
// ① 静态描述表：字符串池 + 方法/属性/枚举三段数据，全在只读段，运行期零构造成本
Q_CONSTINIT const QMetaObject Lever::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),   // 父表指针 → 这就是继承链
    ...stringdata,   // "Lever", "2levelChanged", "1onLevelChanged", "level", ...
    ...data,         // 每条方法的 (签名索引, 返回类型名索引, 参数类型+参数名索引)
    qt_static_metacall, nullptr, ...metaTypes, nullptr
} };
```

字符串池里每条方法签名都带一个**类型码前缀**：`'0'` = 普通方法、`'1'` = 槽、`'2'` = 信号。这正是 `SIGNAL()/SLOT()` 宏（qobjectdefs.h 53~54/59~60 行）拼出来的东西，也是「字符串版 connect 为什么能工作」的全部机制。

### 2. 派发入口：`qt_static_metacall` 里的那个 switch

```cpp
void Lever::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<Lever *>(_o);          // 从 void* 还原成具体类型
    if (_c == QMetaObject::InvokeMetaMethod) {    // 调用类
        switch (_id) {                            // ← _id 是**局部索引**（本类内从 0 起）
        case 0: _t->levelChanged(*reinterpret_cast<std::add_pointer_t<int>>(_a[1])); break;
        case 1: _t->onLevelChanged(*reinterpret_cast<std::add_pointer_t<int>>(_a[1])); break;
        case 3: _t->reset(); break;
        ...
        }
    }
    if (_c == QMetaObject::ReadProperty)  { ... *reinterpret_cast<int*>(_v) = _t->level(); }
    if (_c == QMetaObject::WriteProperty) { ... _t->setLevel(*reinterpret_cast<int*>(_v)); }
    if (_c == QMetaObject::ResetProperty) { ... }   // 没写 RESET 关键字就是空 switch
}
```

`QMetaObject::Call` 的这几个值（`InvokeMetaMethod / ReadProperty / WriteProperty / ResetProperty / RegisterPropertyMetaType / BindableProperty`，qobjectdefs.h 567~576 行）就是**元对象系统的全部动作码**：信号槽走第一支，属性读写走第二三支。索引对齐靠 `qt_metacall` 里那句 `_id -= 本类条数;`（全局索引 ↔ 局部索引的平移）。

> 注意 `WriteProperty` 分支**只有「调 setter」这一行**，没有「调完 setter 顺手发一下 NOTIFY」。这就是第 6 讲那条结论的物证：**通知由你的 setter 负责发**。

### 3. 发射：`QMetaObject::activate` 遍历接收者槽表

```cpp
// moc 为每个信号生成的实现体（本机实测输出）
void Lever::levelChanged(int _t1)
{ QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1); }
```

`activate` 干的事：拿「`this` 上、这个**局部信号索引**对应的连接链表」，按 `connect` 的先后顺序逐个调用槽；同线程下默认 `Qt::AutoConnection` 退化成 `DirectConnection`，所以**槽是在你 `Q_EMIT` 那一行里同步跑完的**。由此四条推论全部成立：没连接时发射是空转不报错；重复 `connect` 就重复调用；调用顺序 = 连接顺序；`Q_EMIT` 返回时槽早就跑完了。

### 4. 三者怎么咬合

`Q_OBJECT` →（`moc` 编译前）→ **一张静态表 + 一个 switch + 每信号一个发射体** →
`connect` 把接收者登记到表所描述的连接表上 → `Q_EMIT` 触发 `activate` 遍历登记项 →
`invokeMethod("press")` / `setProperty("level", v)` / QML 绑定 / `findChild<Part*>` 这些「按名字」的能力，**全部是在查同一张表**。

**没有 `moc` 就没有这张表；没有这张表，信号槽、属性、对象查找、反射一个都不成立。** 本模块前 4 讲讲登记，第 5 讲讲表本身，后 3 讲讲表的其他消费者。

## 六、常见坑与注意事项

### 1. 忘写 `Q_OBJECT`（或写了却没让 moc 扫到）

自定义信号/槽连不上、`qobject_cast` 返回空、`findChild<T>` 直接编译失败（qobject.h 里那句 `static_assert(HasQ_OBJECT_Macro<ObjType>::Value, "No Q_OBJECT in the class passed to QObject::findChild")`）。原因：类没重写 `metaObject()`，返回的是**基类**的表。另一半：写了 `Q_OBJECT` 但没开 `CMAKE_AUTOMOC`、或类写在 `.cpp` 里却没在文件末尾 `#include "本文件名.moc"` → 链接期报 `undefined reference to vtable for X` / `staticMetaObject`。**`#include` 的文件名必须与当前源文件名一字不差。**

### 2. 字符串版 `SIGNAL()/SLOT()` 宏把错误推到运行期

`connect(a, SIGNAL(x(int)), b, SLOT(onX(int)))` 拼出的是「类型码 + 签名文本」，然后去表里比。签名写错、参数少写一个空格、槽名大小写不一致，**编译全过**，运行时静默失败（`QObject::connect` 只会 `qWarning`）；`invokeMethod("noSuchMethod")` 在 Debug 构建下还会撞 `Q_ASSERT` 当场断掉。工程规矩：**内部代码一律用函数指针版**，只有插件装配、配置文件、`connectSlotsByName` 这类「名字来自编译单元之外」的场景才允许字符串版，且必须有失败分支。

### 3. lambda 捕获悬空指针（本模块最贵的一类 bug）

`connect(dev, &Dev::ready, [this]{ ... })` 里 `this`（或引用的局部变量）先死了，`Dev` 后发信号 → 调用已析构对象 = 未定义行为。危险之处在于**它常常不立刻崩**：内存还没被复用时看着一切正常，改一个无关文件后就崩了。解法两条一起用：**显式列捕获表**（别 `[&]` 一把梭）+ **传 context object**（见下一条）。第 4 讲把两种写法并排跑给你看。

### 4. 忘记传 context object

`connect` 有「四参 + 连接类型」和「带接收者对象」两族重载；lambda 版**只有带上 context 才会随那个对象销毁而自动断开**。漏写 context，等于你亲手放弃 Qt 的自动清理，把断开责任全压到自己身上——然后你会忘记。

```cpp
QObject::connect(dev, &Dev::ready, this,   [this]{ updateUi(); });   // ✅ 随 this 析构自动断开
QObject::connect(dev, &Dev::ready, /* 少 context */ [this]{ updateUi(); }); // ❌ dev 活得比 this 久就悬空
```

### 5. 在子线程碰 UI 对象（Qt 的对象还有「线程亲和性」这条命）

`QObject` 出生时就归属一个线程，`QTimer`/信号槽/控件都受这条约束。**跨线程直接调 UI 对象的方法、或在非 GUI 线程 `new`/`delete` 控件**，轻则随机崩溃重则「在我机器上好好的」。本模块先立规矩：**槽在哪个线程执行，取决于连接类型**——同线程 `DirectConnection` 就在发射方线程跑完；`QueuedConnection` 才会投递到接收者线程的事件循环里执行（于是参数必须可拷贝，`QMetaType` 要认得它）。真正的多线程做法放到后面的线程模块，这里先把「别在子线程碰 UI」记死。

### 6. 删父对象时，手里的子对象指针已经悬空

`delete machine;` 会递归 `delete` 整棵子树（第 7 讲实测：只删一个根，5 个 Part 全没了）。你另外存的那些裸子指针当场作废。长期持有别人子对象只有两条安全路线：`QPointer<T>`（父析构后自动变 null，判空即可）或连 `QObject::destroyed` 自己置空——**注意 `destroyed` 回调里那个对象已在析构中，只能比地址，不能调它的成员函数**。另外：`children()` 返回的是**引用**，边遍历边 `setParent`/`delete` 会让容器失效；栈上的 `QObject` 不要挂到有堆寿命的父对象下面。

### 7. `deleteLater()` 不是「异步删除的语法糖」

它只投递一个 `QEvent::DeferredDelete`（qcoreevent.h 105 行），兑现依赖**事件循环**：没进 `exec()` 就 return，对象不会被删（「明明 deleteLater 了却泄漏」的头号原因）；反过来，在正在派发的事件/槽里 `delete this` 会踩到 `activate` 手里的连接表节点——**这才是 deleteLater 存在的理由，它把销毁推迟到安全点，而不是取消销毁**。另外 `setParent()` 在 Qt6 返回 `void`（qobject.h 208 行，Qt5 是 `bool`），旧代码 `if (!w->setParent(p))` 在 Qt6 编不过。

### 8. 属性名/枚举名这类「按名字的错」，全在运行期

- 属性名拼错：`setProperty("levl", 999)` 对静态属性 `level` 毫无作用，却会**成功创建一个新的动态属性**（不报错、不返回失败），你只会看到界面纹丝不动。
- 忘写 `Q_ENUM(Mode)`：属性只能按整数读写，`write("Scan")` 直接返回 `false`。
- 枚举放在**全局作用域**却在类里写 `Q_ENUM(Mode)`：`moc` 会收下、表里也读得到，但 `QMetaEnum::fromType<Mode>()` 过不了编译（`qmetaobject.h` 310 行那句 static_assert）。原因是 `Q_ENUM` 生成的是**类里的两个 friend 助手**，全局枚举的 ADL 找不到它们。要 `fromType` 就二选一：枚举挪进类里（且声明在 `Q_ENUM` 之前），或放进带 `Q_NAMESPACE` 的命名空间用 `Q_ENUM_NS`/`Q_FLAG_NS`。详见第 8 讲第一节「枚举的三种作用域」。
- 忘写 `NOTIFY` 或 setter 里漏了 `Q_EMIT`：值改了，绑定/QML/界面都不刷新——`moc` 只登记信号索引，**不替你发**。
- 静态属性只写 `WRITE` 不写 `READ`（或反之）是能过的，读它就给你一个无效 `QVariant`。

### 9. 反射查表的两个「不检查」

`indexOfMethod/indexOfProperty/indexOfEnumerator` 查不到返回 **-1**，而 `method(int)`/`property(int)`/`enumerator(int)`/`classInfo(int)` **都不做越界检查**——拿 -1 直接构造条目是越界读。类信息那一段顺便纠个常见误传：这版 Qt（6.12）的 `QMetaObject` 是**公开**声明了 `classInfoOffset()`（qobjectdefs.h 254 行）和 `classInfoCount()`（260 行）的，所以边界齐全的 `for (i = offset; i < count; ++i)` 完全合法，按名字 `indexOfClassInfo(name)` 点查也合法；不合法的是第三种——「循环到 `name()` 返回空」，那是拿越界读赌内部字符串池的布局。遍历时一律**从 `xxOffset()` 起**，否则你读到的是 QObject 那几十条父类条目，会误以为「我的类怎么多出这么多槽」。

## 七、老师傅的经验之谈

### 1. 先立心智模型，再背 API

一句话版本：**「信号是被 moc 写好实现体的 public 成员函数，内容就是查连接表、逐个调槽；反射、属性、`findChild` 查的是同一张 `moc` 生成的表。」** 这句话通了，本模块 80% 的困惑自动消解，剩下的 20% 是「谁先死」——那是第 4、7 讲的全部内容。

### 2. `moc` 不神秘，而且很好读

怕 `moc` 的人，多半没自己跑过一次。一个 30 行的类能生成两百多行；第 5 讲那份本机实测产物（`05_moc_output.txt`，243 行）你只要对着源码读一遍：顺序怎么排、前缀是什么、`switch` 里有几个 `case`、`qt_metacall` 减掉几条，就终身不忘。**看三次生成物 > 读十篇博客。**

### 3. 两条纪律，我要求你写成条件反射

一是**带 lambda 的 `connect` 永远传 context、永远显式列捕获表**（第 4 讲）。二是**内部热路径只用函数指针版信号槽**，字符串版只留给「名字来自编译单元之外」的装配现场（第 1、2、5、8 讲）。这两条不改变你的功能，但能把偶发崩溃从你的工程里几乎清零。

### 4. 会读表，也要知道表的代价

`activate` = 查连接链表 + 按连接类型决定直接调用/投递事件 + 从 `void**` 里 `reinterpret_cast` 取参；`invokeMethod("press")` 还要多一次签名比较和 `QVariant`/`QMetaType` 装箱。一次普通函数调用没这些开销。所以 60 次每秒的 delegate、被连上百次的信号，走「按名字查」那条路就是在给自己上税。

### 5. 每个示例都要能无人值守跑完

本模块 8 个示例一律：只建一个 `QApplication`/`QCoreApplication`（第 6、7、8 讲甚至不需要 GUI），不依赖人手点击，`QTimer::singleShot` 排自动脚本，`qInfo()` 打印结论，最后 `QCoreApplication::quit()`。危险演示（Debug 下断言、槽里 `delete this`）一律**注释掉并写明怎么单独验一次**——教学材料不能自己带崩。

### 6. 诚实对待「编没编过」

本机运行时不全，8 个示例都标了「未在本机实测编译」；能实测的只有 `moc`，就明确写「moc 输出为本机实测」。查不到签名的 API 一律不用：例如第 6 讲我 grep 遍 `qproperty.h` 找不到 `bind()`，`QVariant` 身上也没有 `bind`，于是把网上流行的 `property("x").bind(...)` 判为**编不过**，改用头文件里真实存在的 `QBindable<T>(obj, "name")` / `QProperty<T>::setBinding` / `Qt::makePropertyBinding`；绑定的**求值时机**在本机验证不了，就在代码里明写「未实测」，把要记的数留给能编译的你。资料会写错，环境会变，「不确定就标出来」这个动作不能变——这正是这套资料要叫「无谬误」的原因。

> 元对象系统就到这儿。这 8 个文件把 Qt 的地基钢筋给你看清了：往后线程模块的连接类型、模型视图的 `data()/setData()`、QML 的属性绑定，全是这张表的其他用法。加油，徒弟！
