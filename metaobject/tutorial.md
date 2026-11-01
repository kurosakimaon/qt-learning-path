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

> 环境事实（本机实测核对）：Qt **6.12.0**、Windows、套件 `D:/Qt/6.12.0/mingw_64`、编译器
> `D:/Qt/Tools/mingw1310_64/bin/c++.exe`（gcc 13.1.0）、CMake + `MinGW Makefiles`、C++17。
> 构建走 `find_package(Qt6 COMPONENTS Core Widgets)` + `Qt6::Core`/`Qt6::Widgets` + `CMAKE_AUTOMOC=ON`。
> 头文件一律对 `D:/Qt/6.12.0/mingw_64/include/QtCore` 核对（注释里给了文件名与行号），
> `moc` 用 `D:/Qt/6.12.0/mingw_64/bin/moc.exe`（`--version` 报 `moc 6.12.0`，产物头部自报
> "The Qt Meta Object Compiler version **69** (Qt 6.12.0)"）。
>
> 诚实标注（**已更新，别再照旧话抄**）：本模块 8 个示例**都已在本机编译并运行过**，Release 与
> Debug 两套构建目录都建过（`build/` 与 `build_dbg/`），退出码全为 0；输出用
> `QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1` 强制落到 stderr，Windows 下日志是 GBK，
> 要 `iconv -f GBK -t UTF-8` 转一手，否则中文全是乱码会误判。以往这一版写的是「本机无法编译链接
> Qt 程序、只有 moc 实测」，那是环境没配好时的老实话，现在已经不成立——**改资料先把「实测到什么程度」
> 改准**，否则后面的结论没人敢信。批量验证用 `bash _tools/run_all.sh metaobject`（逐目标跑并记退出码）。

## 二、学习目标

学完本模块，你要能做到：

1. 说清 `signals:` / `slots:` / `emit` 三个关键字**宏展开后各是什么**（`signals:` 白送 public，裸 `slots:` 是语法错误），并解释「信号只声明不实现」的根因
2. 分清 `connect` 的几种写法（成员函数指针 / 字符串 `SIGNAL()`+`SLOT()` / lambda / 带 context 的 lambda）与各种 `Qt::ConnectionType` 的差别，并能说出**为什么内部代码一律用函数指针版**
3. 会管连接的生命周期：保存并使用 `QMetaObject::Connection`、正确 `disconnect`、理解「接收者销毁即自动断开」，以及带 context 的 lambda 为什么能救命
4. 打开 `moc` 产物逐段读懂：**字符串池 + 元数据数组 + `qt_static_metacall` 的 switch + `metaObject()/qt_metacast/qt_metacall` + 每个信号的发射体**，并解释 `SIGNAL(x)` 那个数字前缀
5. 用 `Q_PROPERTY` 声明可读可写可通知的属性，说清 `READ/WRITE/NOTIFY/RESET/MEMBER/DESIGNABLE/CONSTANT/FINAL/REQUIRED/BINDABLE` 各自的**实际后果**，并能区分静态属性与动态属性、说清「写属性 = 调 setter，Qt 不替你发 NOTIFY」；再说清 Qt6 绑定挂在三种属性形状上各自的下场（访问器属性：三条写路径**只有 `bindable.setValue()` 会解除绑定**；只读属性：挂得上、算得动、落不了值、零警告；没有 NOTIFY：`isValid()=false`，再调 `value()` 就是段错误），并解释为什么 `BINDABLE` 关键字和 `QBindable::isBindable()` 回答的根本不是同一个问题
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
          （★ 前缀在**宏产物**里，不在字符串池里；★ 方法表按「信号段→槽段→Q_INVOKABLE 段」排，
           不是源码书写顺序）
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
          三种属性形状各自的下场：访问器属性能绑但**赋值不解除绑定**（只有 bindable.setValue 解除）、
          只读属性挂绑定零警告却落不了值、没有 NOTIFY 的属性 isValid=false 且再调成员就崩；
          BINDABLE 关键字**不是**可绑定的前提。全讲 61 项 expect() 自检，不符就退出码 1）

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
    ...stringdata,   // "Lever", "pressedOut", "int", "times", "level", "levelChanged", ...
    ...data,         // 每条方法的 (签名索引, 返回类型名索引, 参数类型+参数名索引)
    qt_static_metacall, nullptr, ...metaTypes, nullptr
} };
```

字符串池里**存的是光秃秃的名字**：`05_moc_output.txt` 第 40~64 行逐字可查，池子里出现的是
`"pressedOut"`、`"onLevelChanged"`、`"level"`，**没有**任何 `0`/`1`/`2` 前缀（下标 2 那条是空串 `""`，
它是 `void` 返回类型的「类型名」占位，所以每条方法数据的第二个实参都是 `2`——这也是「表里全存下标、
不存指针」的又一个证据）——因为查表的
`invokeMethod` 走的是 `qmetaobject.cpp:1702` 那句 `name != stringDataView(meta, m.data.name())`，
拿裸名比对；要是池子里真带前缀，`invokeMethod("press")` 就永远匹配不上了。

那个数字前缀属于 **`SIGNAL()`/`SLOT()` 宏**，不属于表：`qobjectdefs.h:41~43` 定义
`QMETHOD_CODE 0`（普通方法）、`QSLOT_CODE 1`、`QSIGNAL_CODE 2`，`:44~47` 把它拼成
`QT_PREFIX_CODE`，`:49` 的 `QLOCATION` 再在 Debug 下（`:53~54`）追加 `@文件:行号`，Release
（`:59~60`）只剩「前缀 + 签名」。`QObject::connect` 收到这根字符串后，先在 `qobject.cpp:2667~2671`
的 `extract_code` 里**把前缀读出来并丢掉**，再拿剩下的裸签名去表里比（`:3142` `++signal // skip code`、
`:3168` `++method // skip code`），最后由 `:3176~3185` 的 `switch (membcode)` 决定该去
`indexOfSlotRelativeMethod` 还是 `indexOfSignalRelativeMethod`——这就是「同一条签名，`SLOT()` 写错成
`SIGNAL()` 会报不同错误」的来历；匹配失败由 `check_signal_macro`（`:2684~2698`）、`check_method_code`
（`:2700~2710`）打印 `err_method_notfound`（`:2728~2744`）。所以：**前缀是字符串版 connect 这条路上的
临时编码，元对象表本身干净得很。**（第 5 讲第 8 节有本机实测：`QByteArray(SIGNAL(pressedOut(int,int)))`
打出来就是 `2pressedOut(int,int)`。）

★ 勘误：旧稿把上面三个宏写成 `QMETHODCODE` / `QSLOTCODE` / `QSIGNALCODE`（少了一个下划线）。  name-ok:QMETHODCODE, QSLOTCODE, QSIGNALCODE ★ 勘误段落复述旧错名
本机 6.12.0 全树 grep 过：Qt 里**没有**这三个名字，`qobjectdefs.h:41~43` 的拼法是带 `_CODE` 的
`QMETHOD_CODE` / `QSLOT_CODE` / `QSIGNAL_CODE`。照旧稿去 grep 会一无所获，还以为是自己找错了文件。

**方法表的排序规则也不是「源码书写顺序」**——这一条我先前写错过，现在按 moc 的源码钉死：
`qtbase/src/tools/moc/generator.cpp:282~284` 按 `signalList → slotList → methodList`（构造函数在最前）
**分三段**输出，`:851~854` 记录每条方法落在第几个索引，`:521~522` + `moc.h:202` 说明段内才保持声明顺序。
所以 `Lever` 里排在源码第一位的 `Q_INVOKABLE int press()` 落到了局部索引 **12**（倒数第一条），
而书写在它之后的槽 `reset()` 是 **3**（本机运行日志逐条可查，第 5 讲第三节）。
记法：**先按类别分段（信号 → 槽 → Q_INVOKABLE），段内才是你写的顺序。**

### 2. 派发入口：`qt_static_metacall` 里的那个 switch

```cpp
void Lever::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<Lever *>(_o);          // 从 void* 还原成具体类型
    if (_c == QMetaObject::InvokeMetaMethod) {    // 调用类
        switch (_id) {                            // ← _id 是**局部索引**（本类内从 0 起）
        case 0: _t->pressedOut(*…<_a>(…), *…); break;      // 信号占 0~2
        case 1: _t->levelChanged(*reinterpret_cast<std::add_pointer_t<int>>(_a[1])); break;
        case 2: _t->flipped(); break;
        case 3: _t->reset(); break;                        // 槽占 3~11
        case 4: _t->onLevelChanged(*reinterpret_cast<std::add_pointer_t<int>>(_a[1])); break;
        ...
        case 12: { int _r = _t->press(…);                 // Q_INVOKABLE 只剩它自己，排最后
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {               // ← 属性/信号「按函数指针反查索引」
        if (QtMocHelpers::indexOfMethod<void (Lever::*)(int , int )>(_a, &Lever::pressedOut, 0)) return;
        if (QtMocHelpers::indexOfMethod<void (Lever::*)(int )>(_a, &Lever::levelChanged, 1)) return;
        if (QtMocHelpers::indexOfMethod<void (Lever::*)()>(_a, &Lever::flipped, 2)) return;
    }
    if (_c == QMetaObject::ReadProperty)  { ... case 0: *reinterpret_cast<int*>(_v) = _t->level(); }
    if (_c == QMetaObject::WriteProperty) { ... case 0: _t->setLevel(*reinterpret_cast<int*>(_v)); }
}
```

上面**全部是逐字摘录**（省略处标了 `…`/`...`），出处 `metaobject/05_moc_output.txt` 第 131~180 行
（`wc -l` = **236 行**，`D:/Qt/6.12.0/mingw_64/bin/moc.exe 05_moc_anatomy.cpp` 的原始输出；
仓库里那份和重跑一次的结果 `diff` 为零差异）。
三条以前被我写歪的地方，一并更正：

1. `pressedOut` 的局部索引是 **0**、`levelChanged` **1**、`flipped` **2**、`onLevelChanged` **4**；
   我上一版把 `pressedOut` 写成 2、把 `flipped` 写成 4，那是照着「信号排在槽之后」的错误印象推的。
2. `ReadProperty`/`WriteProperty` 里的 `_id` 是**属性表的局部索引**（`level` 是本类第 1 个属性 →
   在这两个 switch 里是 `case 0`），和方法索引无关——两套编号各有各的起点。
3. **`qt_static_metacall` 里根本没有 `ResetProperty` 分支。**我上一版写的是
   `if (_c == QMetaObject::ResetProperty) { ... } // 没写 RESET 关键字就是空 switch`——不对：
   没写 `RESET` 时 moc 是**整段不生成**，不是生成一个空 switch（本机产物里 `Call` 只出现
   `InvokeMetaMethod`/`IndexOfMethod`/`ReadProperty`/`WriteProperty` 四种）。对照实验刚跑过：
   把带 `RESET resetLevel` 的 `06_property_system.cpp` 喂给同一个 `moc.exe`，它的产物里就**多出**
   `if (_c == QMetaObject::ResetProperty) { … case 0: _t->resetLevel(); break; }`（第 173~175 行），
   属性 flags 里也多一个 `QMC::Resettable`（第 89 行）。所以「清属性」这件事在没有 `RESET` 时靠的是
   `QMetaProperty::reset()` 直接写回 `QVariant()`，而不是走元对象表。

`QMetaObject::Call`（`qobjectdefs.h` 567 行起，逐个是 `InvokeMetaMethod / ReadProperty /
WriteProperty / ResetProperty / CreateInstance / IndexOfMethod / RegisterPropertyMetaType /
RegisterMethodArgumentMetaType / BindableProperty / CustomCall / ConstructInPlace …`）就是
**元对象系统的全部动作码**：信号槽走第一支，属性读写走第二三支，`CreateInstance` 给
`QMetaObject::newInstance`，`IndexOfMethod` 就是上面那段「拿函数指针反查局部信号索引」。
索引对齐靠 `qt_metacall` 里那句「先交给父类，父类不接再 `_id -= 本类条数`」（全局索引 ↔ 局部索引的平移，
逐字见 `05_moc_output.txt` 第 195~215 行：`_id = QObject::qt_metacall(_c, _id, _a);` 打头，
方法段 `_id -= 13`、属性段 `_id -= 1`，两个数字就是 `Lever` 自己那两段的条数）。

> 注意 `WriteProperty` 分支**只有「调 setter」这一行**，没有「调完 setter 顺手发一下 NOTIFY」。这就是第 6 讲那条结论的物证：**通知由你的 setter 负责发**。

### 3. 发射：`QMetaObject::activate` 遍历接收者槽表

```cpp
// moc 为每个信号生成的实现体（05_moc_output.txt 第 220~235 行，逐字）
void Lever::pressedOut(int _t1, int _t2)
{ QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2); }
void Lever::levelChanged(int _t1)
{ QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1); }   // ← 局部索引 1，不是 0
void Lever::flipped()
{ QMetaObject::activate(this, &staticMetaObject, 2, nullptr); }              // ← 无参走不带模板实参的重载
```

**第三个实参是「局部信号索引」**：`levelChanged` 在 `Lever` 的三个信号里排第二，所以是 `1`；
我上一版这里抄成 `0`，是把「本类第一个信号」和「本类第一个方法」混成了一回事。
全局索引 = 局部索引 + `methodOffset()`（本机实测 `Lever` 的 `methodOffset() = 4`、
`methodCount() = 17`，于是 `pressedOut` 全局 4 / 局部 0）；`activate` 内部再由
`qobject.cpp:4486` 那句 `signal_index = local_signal_index + QMetaObjectPrivate::signalOffset(m)`
换成「信号段内」的编号——三个起点，别混。

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

`connect(a, SIGNAL(x(int)), b, SLOT(onX(int)))` 拼出的是「类型码 + 签名文本」（前缀见上一节，它属于宏、
不属于元对象表），然后去表里比。签名写错、参数少写一个空格、槽名大小写不一致，**编译全过**，
运行时静默失败（`QObject::connect` 只会 `qWarning`）。
`invokeMethod("noSuchMethod")` 也一样**不会断言**——这一条我上一版写错了（当时写的是「Debug 构建下还会撞
`Q_ASSERT` 当场断掉」），现在有两头证据：
①源码：`qmetaobject.cpp:1678~1716` 的 `invokeMethodImpl` 按名字逐个比签名（1702 行那句
`name != stringDataView(meta, m.data.name())`），全不中就直接落到 1715 行的
`printMethodNotFoundWarning()`（定义在 1495~1528 行：1523 行 `qWarning`、1527 行 `return false`）。
这条路上唯一的 `Q_ASSERT` 是 1686~1688 行那三条，检查的是**你传进来的参数个数/类型数组是否自洽**，
和「名字存不存在」无关。
②运行：本机 Release 与 Debug 两套构建都跑过 `QMetaObject::invokeMethod(&lever, "noSuchMethod")`，
打印 `QMetaObject::invokeMethod: No such method Lever::noSuchMethod()`、返回 `false`、**退出码 0**
（第 5 讲第 4 节现在就是这段活代码，不是注释）。
真正会当场断掉的是**别的东西**：`qobject.h` 里 `connect` 重载的那几条 `Q_ASSERT`（参数个数关系）、
第 2 讲那条 `Qt::UniqueConnection` + lambda 的断言。别把两件事混了。
工程规矩不变：**内部代码一律用函数指针版**，只有插件装配、配置文件、`connectSlotsByName` 这类
「名字来自编译单元之外」的场景才允许字符串版，且必须有失败分支——因为「不崩」不等于「成功」。

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
- **枚举放在全局作用域、却在类里写 `Q_ENUM(枚举名)`：`moc` 收下它，但把这条枚举整条丢掉，
  而且一句警告都不打。** 这是本模块最阴的一个坑，上一版这里我写反了（写的是「表里也读得到」），
  现在用对照实验钉死。探针（`/tmp/enumprobe`，本机可复现）：
  全局 `enum class GMode { Standby, Scan };` + 类里 `Q_ENUM(GMode)` + `Q_PROPERTY(GMode mode ...)`，
  跑 `D:/Qt/6.12.0/mingw_64/bin/moc.exe`：**退出码 0、stderr 零字节**，产物里 `qt_enums { };`
  是**空的**（第 58~59 行）。运行期随之全崩：
  `enumeratorCount() = 0`、`indexOfEnumerator("GMode") = -1`、`setProperty("mode", "Scan")` 返回 **false**
  （属性仍写着 `EnumOrFlag` 标志，但没有枚举表可查，于是只认整数；`property("mode")` 读回 `"0"`）。
  同一个探针里放一个**类内** `enum class Lock` + `Q_ENUM(Lock)` 作对照：`enumeratorCount() = 1`、
  `indexOfEnumerator("Lock") = 0`、`setProperty("lock","Sealed")` **true**、
  `QMetaEnum::fromType<Inner::Lock>()` 编译通过且 `keyCount() = 2`。
  **结论（正确说法）**：`Q_ENUM`/`Q_FLAG` 只接受**类作用域**的枚举；全局枚举要么挪进类里
  （且声明在 `Q_ENUM` 之前），要么放进带 `Q_NAMESPACE` 的命名空间用 `Q_ENUM_NS`/`Q_FLAG_NS`。
  机制：`Q_ENUM` = `QT_ANNOTATE_CLASS(qt_enums, x)` + `Q_ENUM_IMPL(x)`（`qtmetamacros.h:73~77`），
  而 `Q_ENUM_IMPL` 展开的是**两个 `friend` 函数** `qt_getEnumMetaObject` / `qt_getEnumName`
  （74~75 行，返回 `&staticMetaObject` 与 `#ENUM`）——`friend` 只能声明在类里，全局枚举配 `Q_ENUM`
  时 moc 解析不出「这条枚举属于本类的哪个作用域」，于是干脆不登记；namespace 版走的是
  `Q_ENUM_NS_IMPL` 的 `inline` 全局函数（78~82 行）+ `Q_NAMESPACE`（182 行），配套才成立。
  **最坏的地方是「静默」**：属性读写、QML 绑定、`connectSlotsByName` 全都会「按名字写不进去」，
  而 moc 和运行日志里什么线索都没有——只能靠上面那三条 `enumeratorCount()/indexOfEnumerator()`
  的自检把它逼出来。详见第 8 讲第一节的对照实验（已在本机跑通，退出码 0）。
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

**这一节原来写的是「本机运行时不全，8 个示例都标了未在本机实测编译；能实测的只有 moc」——那句话已经过期了。**现在 8 个示例全部在本机编译、链接、`QT_QPA_PLATFORM=offscreen` 无头跑过，`ctest -R "^metaobject_"` → 8/8 绿，输出存档 `runlogs/metaobject_0N_*.log`（原始字节是 GBK，`.log` 是转过 UTF-8 的那一份）；`moc` 那一路（第 5 讲）仍然是实测，而且现在连产物一起跑给你看了。

有三处结论因为「终于能跑」被**真的改掉**了，留着它们比留十条正面讲解都有用：

- 第 6 讲的属性绑定「求值时机」，原来写的是「本机验证不了，把数留给能编译的你」，还顺手给了一个猜测：「依赖失效要有观察器就位才生效」。**跑起来以后，这个猜测是错的**——而且错得很典型：光打印值（30→120）根本看不出「什么时候算的」，所以那一节加了求值计数器，把时机本身变成可见的数字。本机那一轮打出来的是四条**时机**，不是两个数：挂上绑定的那一刻就求值一次（绑定不是惰性的）；源没变连读两次计数不涨（值缓存着）；`source = 40` 这一**写**就让计数 +1，读之前已经重算完（失效从被写的依赖方立刻传播，**不需要**有人观察 derived）；条件依赖还会**换**——这一轮没读到的那个属性，下一轮它变了就不再触发重算。旧结论不是被环境推翻的，是被自己的计数器推翻的。
- 第 6 讲还有一句**我写宽的**结论，这一轮被同一批实测推翻：「给带绑定的属性赋值 = 解除绑定」。它对**成员属性**（`Q_OBJECT_BINDABLE_PROPERTY`，存储本体就是 QProperty，`qproperty.h` 1176~1185 行 `QObjectBindableProperty::setValue` 开头就 `removeBinding()`）成立；对**普通访问器属性**（`READ/WRITE/NOTIFY`）**不成立**。本机把三条写路径各跑一遍：

  | 你调的是谁 | 绑定还在吗 | 下一次依赖变动的结果 |
  |---|---|---|
  | 类自己的 `setLevel(999)` | **在** | 999 被绑定冲掉，回到 5000 |
  | `QObject::setProperty("level", 999)` | **在**（它转手还是调 WRITE） | 同样被冲掉 |
  | `QBindable<int>::setValue(999)` | **没了**（`qproperty.cpp` 2560~2563 行的 accessor setter 先 `removeBinding()` 再 `write()`） | 值冻在 999 |

  ⇒ 工程含义很直白：**属性挂着绑定时，想让手动赋值真的生效，只能用 `bindable.setValue()`**；直接调 setter 或走 `setProperty` 写进去的都是「临时值」。反过来，「我的绑定怎么莫名其妙没了」在成员属性上查赋值，在访问器属性上就查不到——两套实现，别记成一条规矩。
- 同一条链上还有一处更基础的错：我原来写「`QBindable<T>(obj,"name")` 的前提是 `Q_PROPERTY` 里有 **BINDABLE** 关键字，否则 `setBinding` 会被拒绝并 qWarning」。实测把这句话整体推翻：`Gauge::level` 没写 BINDABLE，`QMetaProperty::isBindable()` 确实是 false，但 `QBindable::isValid()/isBindable()` 都是 true，绑定**照样挂上、照样落值**——BINDABLE 只是「把类内 QProperty 成员借出去」的声明，不是可绑定的前提。真正的硬条件是**签名表里得有 NOTIFY**；少了它，构造手柄那一刻就 `qt.qproperty.binding: … has no notify signal`，`isValid()` 变 false，而接下来 `value()` / `setValue()` / `hasBinding()` **全是段错误**（`setBinding` 走 `qproperty.h` 924 行的 ASSERT 中止），只有 `isReadOnly()` 安全。这一条是整讲最容易「写下去才发现崩」的坑，所以第 6 讲现在明确教：**先问 `isValid()`，false 就当这个手柄不存在。**
- 顺带钉死两个同名不同义的问法：`QMetaProperty::isBindable()` 问「有没有 BINDABLE 关键字」，`QBindable::isBindable()` 问「这条接口能不能挂绑定」，`QBindable::isReadOnly()`（773 行定义是「能不能挂绑定/挂观察者」）**不回答「有没有 WRITE」**——只读属性上它返回 false。想知道能不能写，去问 `QMetaProperty::isWritable()`。还有一个只读陷阱：给 `READ + NOTIFY`（没 WRITE）的属性挂绑定，**全程零警告**、`hasBinding=true`、求值函数真的每次依赖变动都在跑，但结果没有任何地方落下来，`value()` 也还是 READ 的老值——这是最容易白写的一种。
- 网上流传的 `property("x").bind(...)` 这种写法，判定仍然是**编不过**：`qproperty.h` 里 grep 不到 `bind()`，`QVariant` 身上也没有。这条和环境无关，是签名级事实，所以照旧不用；替代写法用头文件里真实存在的 `QBindable<T>(obj, "name")` / `QProperty<T>::setBinding` / `Qt::makePropertyBinding`。

第 6 讲这一轮还把「跑完没崩」换成了**程序自己判卷**：正文里每一个「等于某个数」的断言，`main` 里都有一条 `expect()` 兜着（61 项），不符就打 `[错]` 并把退出码改成 1，ctest 直接红。这个门我验证过它真的会拦：故意把一条期望改成 12345，退出码立刻变 1、台账变「符合 60 / 不符 1」。**能跑之后，光打印数字不算证据，程序自己判过的数字才算。**


资料会写错，环境会变，「不确定就标出来」这个动作不能变——而这轮多出来的一条更值钱：**能跑之后，「标出来」就要升级成「跑出来」**。凡是还写着「未实测」的行，下一步动作是去验证它，不是抄它。这正是这套资料要叫「无谬误」的原因。

> 元对象系统就到这儿。这 8 个文件把 Qt 的地基钢筋给你看清了：往后线程模块的连接类型、模型视图的 `data()/setData()`、QML 的属性绑定，全是这张表的其他用法。加油，徒弟！
