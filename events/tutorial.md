# 第叁阶段 · 事件系统（Qt 从入门到资深）

> 老师傅带你把 Qt 的心脏拆开看：从「事件循环怎么跳」到「每一层被谁吃掉」

## 一、模块简介

徒弟，前两阶段咱们把地基（QApplication / moc）和 Qt 独有的那层元对象系统铺好了。这一关是**第叁阶段「事件系统」**，要解决的问题只有一个：**你写下的那些 `mousePressEvent`、`eventFilter`、`QTimer` 回调，到底是被谁、在什么时候、按什么顺序叫到的。**

Qt 的程序不是「从 `main` 走到 `return`」，而是「走进 `exec()` 就再不回来，直到有人叫停」。所有你看得见的东西——画面重绘、按钮响应、定时器 tick、拖放落地——都是这台机器转出来的。它不难，但它**玄**：新手阶段积累的「为什么我 show() 了没东西」「为什么槽里跑个大循环界面就未响应」「为什么我的 eventFilter 一挂上按钮就点不动了」，病根全在这一层，答案却都在同一条链上。

本模块一共 **7 讲**。第 1 讲是总纲（事件循环）；第 2、3 讲是**同一条派发链的两个视角**（从里面改 vs 从外面插手）；第 4~7 讲把四类具体的事件各钉死一讲：定时器、输入与焦点、自定义事件、拖放。

本模块还有一个**核心技巧**贯穿全部 7 个示例：所有演示都在**没有鼠标、没有键盘**的情况下跑完。做法是自己构造 `QEvent` / `QMouseEvent` / `QKeyEvent` / `QDragEnterEvent` / `QDropEvent`，再用 `QCoreApplication::sendEvent()` / `postEvent()` 投给控件，然后**把进入次数、到达顺序打印出来当证据**。这样每个示例都能无人值守跑完、自己吐结论，也是你在 CI 里能测 GUI 的原因。

> 环境事实：Qt 6.12.0、Windows、MinGW-w64（MSYS2 ucrt64，gcc 15.2/16.2）、CMake ≥ 3.21 + Ninja、C++17。构建走 `find_package(Qt6 COMPONENTS Widgets)` + `Qt6::Widgets` + `CMAKE_AUTOMOC=ON`，目标名统一是 `events_0N_xxx`。
>
> 诚实标注：**本机当前无法编译链接 Qt 程序**，7 个示例**都未在本机实测编译**。每个类、函数、枚举、重载都对着本机 `C:\msys64\ucrt64\include\qt6` 里的真实头文件核对过（注释里给了文件名与行号），核对不上的 API 一律不用；依赖平台插件或真实指针操作的行为，都标成「文档语义／以你打印的数字为准」。

## 二、学习目标

学完本模块，你要能做到：

1. 说出 `app.exec()` 内部「取事件 → 派发 → 再取」的循环结构，并解释 `sendEvent`（同步直达）与 `postEvent`（异步入队）的时序差别，能用**到达顺序**证明它
2. 讲清事件派发的**三层链**：`QApplication::notify` → `receiver->event()` → 具体 handler，并说得清「重写 `event()` 不调基类实现」会静默丢掉什么
3. 会用 `installEventFilter` / `qApp->installEventFilter` **不继承就改行为**，确切知道返回 `true` 与 `false` 的分岔，以及多过滤器顺序与寿命坑
4. 分得清**事件（系统层语义）与信号（应用层语义）**，能解释 `QAbstractButton` 如何把 `QMouseEvent` 翻译成 `clicked()`，以及为什么 `btn->click()` 绕过了事件
5. 用对定时器家族：`QTimer`（单次/重复/`setTimerType` 三档）、低级的 `startTimer()`+`timerEvent()`、`QElapsedTimer` 实测，并知道**定时器完全依赖事件循环**
6. 读得懂 Qt6 的输入事件 API（`position()`/`scenePosition()`/`globalPosition()` 都是 `QPointF`、`button()` vs `buttons()`、`angleDelta()` vs `pixelDelta()`、`key()`/`text()`/`isAutoRepeat()`），并管理焦点（`Qt::FocusPolicy` 五档、tab 顺序、`setFocus(reason)`）
7. 能设计自定义事件：`registerEventType()` 动态注册、`QEvent` 子类带数据 + 重写 `clone()`、知道 `postEvent` 的所有权/优先级/合并语义，并判断何时该改用信号
8. 写对一个拖放：`QMimeData` 载体、`dragEnterEvent` 里 `acceptProposedAction()`、`dropEvent` 接数据、`startDragDistance()` 阈值，内部与跨进程拖放的差别

## 三、文件列表与学习重点

咱一共 7 个示例，按顺序来，别跳：

```text
  01_event_loop.cpp            ← 事件循环解剖（本模块总纲）
    重点：exec() = 取事件 → 派发 → 再取；QEventLoop 可嵌套（模态对话框的原理），
          嵌套带来重入（re-entrancy）——示例把「当前深度/最大深度」打出来；
          processEvents() 的 ProcessEventsFlag（ExcludeUserInputEvents vs AllEvents）；
          quit() / exit(int) / 从槽里 return 三者后果不同（exit 的返回码能被 outer.exec() 读到）；
          sendEvent 同步 vs postEvent 入队，用流水号顺序当证据；
          自旋 400ms 期间心跳定时器一次不响 = 「循环被占住」的实证

  02_event_dispatch.cpp        ← 一次事件的三层传递（含本模块硬坎：事件 vs 信号）
    重点：层 1 QApplication::notify（全程序唯一总入口，逐层打印进入/离开）；
          层 2 receiver->event() 闸门；层 3 mousePressEvent / paintEvent 等具体 handler；
          重写 event() 拦一类事件；**不调基类实现** → handler 不再被叫、事件不再往父控件抛；
          accept() / ignore() 与 isAccepted()；未被处理则抛给父对象（示例用两层控件把
          「抛到父控件几次」直接打印出来）
    诚实标注：本机 Qt 6.12 头里 QCoreApplication::notify 在 public 段（第 120 行），
              Qt5 时代文档标 protected，本模块不据本机断言 Qt5 头；override 与访问级别无关

  03_event_filter.cpp          ← 事件过滤器（不继承就改行为）
    重点：installEventFilter 挂在**目标对象**上；qApp->installEventFilter 全局；
          返回值 true = 我吃掉（目标的 event() 不再被调用）／false = 看完继续传；
          同一目标上多个过滤器是**逆序**调用；removeEventFilter 之后行为复原；
          过滤器与被过滤对象「谁先死」→ 用 QPointer 守；
          事件的类型直方图（QHash<int,int>）打印，一眼看出吞了哪几类；
          吞掉 press/release 能让 clicked() 计数为 0，而 btn->click() 直接发信号绕过事件
          （本讲末尾就是「事件 → 信号」的转换点证据）
    本文件有 Q_OBJECT 类，末尾必须 #include "03_event_filter.moc"

  04_timers.cpp                ← 定时器家族（回验第 1 讲）
    重点：**没有 exec()（或 processEvents），定时器一次都不会响**——先证明再排队；
          QTimer 单次三写法（setSingleShot / 静态 singleShot(msec, receiver, SLOT(...)) /
          singleShot(Duration, functor) / 带 TimerType 的重载）；
          setTimerType 三档 Qt::PreciseTimer / CoarseTimer / VeryCoarseTimer，
          用 QElapsedTimer 实测每 tick 的真实间隔并求平均；
          低级路线 startTimer()/killTimer() + timerEvent(QTimerEvent*)，
          还能 sendEvent 一个手工 QTimerEvent 看穿「它其实就是投一个事件」；
          interval=0 的合并（连续 start() 500 次 ≠ 500 个 tick）；
          QElapsedTimer::restart() 在 Qt6 返回 qint64（Qt5 是 void）
    写法纪律：跨回调的状态一律放**堆或 static**，排进循环的 lambda 用 & 捕获局部变量就是悬空
    本文件有 Q_OBJECT 类，末尾必须 #include "04_timers.moc"

  05_mouse_keyboard_focus.cpp  ← 输入事件与焦点（Qt5→Qt6 改动最集中的一片）
    重点：坐标全变 QPointF —— position() / scenePosition() / globalPosition()
          （Qt5 的 pos() / globalPos() / screenPos() 对照表在文件末尾）；
          button()「哪一个按钮变了」vs buttons()「此刻按住哪些」，双键同按实测；
          双击时序 Press → Release → MouseButtonDblClick → Release + doubleClickInterval()；
          滚轮 angleDelta()（DefaultDeltasPerStep = 120）vs pixelDelta()、phase/inverted；
          键盘 key() / text() / modifiers() / isAutoRepeat()，Qt6 的 QKeyCombination；
          Qt::FocusPolicy 五档逐个验证、setTabOrder 与创建顺序之别、
          setFocus(Qt::FocusReason) + focusIn/focusOut + QApplication::focusChanged；
          QShortcut + QKeySequence（含标准键 QKeySequence::Copy）
    本讲不需要 Q_OBJECT（重写 handler 只是虚函数继承），正好与第 3 讲「过滤器要发信号」对照

  06_custom_events.cpp         ← 自定义事件
    重点：**registerEventType() 动态注册**（返回值每次运行都可能不同 → 禁止硬编码整数），
          registerEventType(int hint) 的 hint 用法；
          QEvent 子类构造必须把 type 交给基类，重写 Qt6 新增的虚函数 clone()；
          postEvent 三条真实语义：所有权交给 Qt（**绝不能传栈上对象**）、
          int priority（High/Normal/Low，sendPostedEvents 可按类型/接收者冲刷）、
          同一接收者同一类型**不会自动合并**（500 个自定义事件全部到达）；
          removePostedEvents(receiver, type) 丢弃排队事件；
          接收侧「先 type() 判定再 static_cast」，并统计撞车探测器；
          以及：跨线程传数据该用信号槽——QueuedConnection 内部就是投一个 QMetaCallEvent
    本文件有 Q_OBJECT 类，末尾必须 #include "06_custom_events.moc"

  07_drag_and_drop.cpp         ← 拖放（唯一跨进程说话的一类事件）
    重点：QMimeData 是唯一载体（**它在 QtCore 不在 QtGui**）：
          setText/setUrls/setData + hasText/hasFormat/formats/data；
          dragEnterEvent 里必须 acceptProposedAction()（或 setDropAction）——
          只调 accept() 时 dropAction 与光标反馈走的是另一条路，示例把两者并排打印；
          dropEvent 里取数据，含自定义 MIME「application/x-qt-learning-item」的内部拖放特权；
          真正拖动：mousePressEvent 记起点 → mouseMoveEvent 里
          manhattanLength() 超过 QApplication::startDragDistance() 才 QDrag::exec()，
          exec() 内部自开嵌套事件循环（回指第 1 讲）；
          e->source() 在跨进程时为 nullptr；
          无鼠标演示：栈上 QMimeData + 手工构造 QDragEnterEvent / QDropEvent + sendEvent，
          把「接住了几个字节／什么文本」打印出来
    诚实标注：真实拖动（QDrag::exec）需要指针按下与移动，本示例**不自动跑它**，只给完整正确写法
```

> 为什么第 2、3 讲要拆成两个文件：同一条派发链，**改行为有两个入口**——第 2 讲是「你是这个控件的作者」，重写 `event()` 或某个 handler；第 3 讲是「你不是作者」，从外面挂过滤器。合在一个文件里，新手会以为拦截只有一招；分开写、各自把计数打印出来，才能看出两者在链上的**位置差**（过滤器在 `event()` 之前，`notify` 之内）。

## 四、学习顺序建议

师傅给你划这条主线，和一道硬坎：

### 主线：为什么是这个顺序

- **第 1 讲是总纲，不是第一章。** 循环不读懂，第 4 讲的「定时器不响」、第 6 讲的「postEvent 排队」、第 7 讲的「exec() 内部嵌套循环」都会变成三条要背的孤立结论。第 1 讲把「同步 vs 异步」「嵌套」「重入」四件事用数字摊开，后面全是它的复现。
- **第 2 → 第 3：同一条链的两个视角。** 第 2 讲从里面看（`notify` → `event()` → handler），第 3 讲从外面插手（过滤器落在 `notify` 内部、`event()` 之前）。两讲合起来才是完整的拦截图谱，且都靠打印计数证明，不靠你信文档。
- **第 3 → 第 4：从「别人给你的事件」转到「循环自己造的事件」。** 定时器是唯一不需要窗口系统输入就能产生的事件源，是检验第 1 讲掌握程度的最佳试纸：`exec()` 没跑，它就一次不响。
- **第 4 → 第 5：终于处理真实输入。** 鼠标、键盘、焦点占用户程序 90% 的输入面，也是 Qt5→Qt6 API 改动最集中的一片。你以后从网上抄的旧代码，八成踩在第 5 讲末尾那张对照表上。
- **第 5 → 第 6：从「接事件」到「发事件」。** 自己定义类型、自己带数据、自己排队。这一讲还会顺便回答一个前面藏了很久的问题：既然有 `postEvent`，为什么跨线程要用信号槽？
- **第 6 → 第 7：拖放是事件的「集大成」。** 它同时用到自定义 MIME（第 6 讲）、坐标与按钮状态（第 5 讲）、`acceptProposedAction` 的 accept/ignore 语义（第 2 讲）、以及嵌套事件循环（第 1 讲）。放在最后一讲，是给前面做验收。

### 硬坎：事件 vs 信号（就在第 2 讲）

这是本模块最容易和上一阶段（元对象系统）混在一起的地方。请务必把这句话背下来：

> **事件是系统层语义，信号是应用层语义。**

- 事件（`QEvent` 及其子类）来自窗口系统/QPA，由 `QCoreApplication::notify()` 派发，讲「**谁身上发生了什么**」——「在 (120,45) 按下了左键」。它的接口是**虚函数重写**（`event()` / `mousePressEvent()` / `eventFilter()`），一个事件只有一个接收者。
- 信号（`Q_SIGNALS`）是 Qt 靠 moc 造出来的**应用层通知**，讲「**谁想让谁干什么**」——「用户点击了提交按钮」。它的接口是 `connect()`，可以有多个接收者，可以是 lambda。
- **交汇点**：控件内部先在事件里把系统语义翻译成应用语义。`QAbstractButton` 收到 `QEvent::MouseButtonPress`/`Release`，判定命中后才 `emit clicked()`。所以——
  - 用事件过滤器吞掉 `MouseButtonRelease`，`clicked()` **不会**发（第 3 讲打印为 0）；
  - 直接调 `btn->click()`，是**发信号**、不经过任何事件，吞不掉。

这两句能同时成立，才说明你真的分清了。第 2 讲末尾把这两条并排打印，就是给你当试纸。

### 学习方法

每个文件都要：先读顶部注释块（「本讲解决什么问题」+ 核对过的签名与行号）→ 按注释里的 CMake 目标名 `events_0N_xxx` 构建 → 跑一遍**读它打印的数字**（到达顺序、进入次数、实测间隔）→ 改代码做末尾三个小练习 → 改出问题了先猜哪一层吃掉了事件，再验证。光看不练，等于白看。

> 🎬 想看「窗口系统 → QPA → notify → 过滤器 → event() → handler」这条链的分步动画：[图解 · 事件循环](../visuals/event_loop.html)；想看清过滤器在链上的位置与 true/false 的分岔：[图解 · 事件过滤器](../visuals/event_filter.html)。

## 五、核心知识点梳理

把本模块最重要的一件事——**那条派发链**——完整摊开。下面每一环都能在第 1、2、3 讲的打印里找到对应证据：

### 1. 一条链，七道关节

```text
① 窗口系统原生事件        Win32 的 WM_LBUTTONDOWN / X11 的 XButtonEvent …
      ↓                    （平台各异，Qt 之外的人看不见这一层）
② QPA 插件转成 QEvent     windowsplatformplugin 把原生消息 new 一个 QMouseEvent
      ↓                    ——到这一步才是「Qt 语言」，跨平台从这一层开始统一
③ QApplication::notify(receiver, event)
      ↓                    全程序**唯一**总入口；重写它 = 记录/拦截一切事件（第 2 讲）
④ 事件过滤器链            receiver 上的 installEventFilter 过滤器，
      ↓                    + qApp 上的全局过滤器；返回 true 就地终止派发（第 3 讲）
⑤ receiver->event(e)      该对象的事件闸门；重写它 = 不经过滤器也能自己拦一类事件
      ↓                    **不调基类实现 → ⑥ 全部不发生，且事件不再往父控件抛**（第 2 讲实测）
⑥ 具体 handler            mousePressEvent / keyPressEvent / paintEvent / timerEvent …
      ↓                    （QEventDispatcher 依 type() 分发；输入类事件在 QWidget 里
      ↓                     会先做 hit-test 找到几何命中的子控件，再投给它）
⑦ accept() / ignore()     ⑥ 里决定这个事件被不被「认领」
      ↓                    accept → 派发结束；ignore（未被处理）→
      ↓
⑧ 抛给父对象              父控件收到**同一个**事件，重走 ⑤→⑥→⑦
                           （第 2 讲用两层控件把「父层收到几次」打印出来）
      ↓
   一路到顶层窗口仍无人认领 → 事件被丢弃（对输入事件，Qt 还可能把它交回系统）
```

配套的三条同层机制：

```text
定时器：事件循环自己造事件源   exec() 每轮检查到期的 timer → 投 QTimerEvent → 走 ③④⑤⑥
                                    （没有 exec()，这条根本不会产生 —— 第 4 讲实证）
自定义事件：registerEventType() 拿到 User 段(1000~65535)里的一个号
                                    postEvent() 把它挂进接收者的队列，等下一轮 ③
                                    sendEvent() 立刻直达 ③，不等循环
拖放：dragEnter/dragMove/drop 全都在同一条链上跑，只是接收者是 target widget，
                                    并且 QDrag::exec() 自己在 ③ 之外又套了一层循环
```

### 2. 三层重写点：你插手的位置由「你是谁」决定

| 你想干什么 | 插手位置 | 代价 |
|---|---|---|
| 记录/审计全程序一切事件 | 层 ③ 重写 `QApplication::notify` | 得自己保证 `return` 基类实现，一处漏全程序崩 |
| 改**自己写的**控件的行为 | 层 ⑤ `event()`（拦一类）或层 ⑥ 具体 handler（拦一个） | 必须继承；忘记调基类 → 静默丢功能 |
| 改**别人写的**控件 / 一批控件 | 层 ④ `installEventFilter` | 无需继承；返回 `true` 吃掉，`false` 放行；注意寿命 |
| 全局横切（埋点、快捷键、防抖） | 层 ④ `qApp->installEventFilter` | 每个事件都会过你手上，别在这里干重活 |

### 3. 同步与异步的分岔口

```cpp
QCoreApplication::sendEvent(&target, &ev);   // ③ 立刻执行；函数返回时事件已派发完毕
QCoreApplication::postEvent(&target, new MyEvent);  // 只入队，所有权交给 Qt，等循环下一轮
QCoreApplication::sendPostedEvents(&target, QEvent::User); // 提前把队列冲出来
QCoreApplication::removePostedEvents(&target, type);       // 丢弃排队中的某类事件
```

`exec()` 内部大致是「取原生事件 → 转 QEvent → `notify` → 再取」；`processEvents()` 就是**手动跑一小段这个循环**——它是第 1 讲里唯一合法的「救急」手段，也是卡顿与重入的万恶之源。判据很简单：**你在一个已经被事件循环调用的栈上再调 `processEvents()`，就打开了重入的大门。**

### 4. Qt5 → Qt6 的必考差异（本模块集中标在第 5、4、6 讲注释里）

```text
QMouseEvent  pos()/globalPos()/windowPos() → position()/globalPosition()/scenePosition()
             （Qt6 全部是 QPointF，逻辑坐标；整数坐标请 qRound）
             mouseButtons() → buttons()，keyboardModifiers() → modifiers()
             button() 语义不变：「这一次是哪个按钮变了」；无变化时为 Qt::NoButton
QWheelEvent  构造函数参数更多（pixelDelta/angleDelta、device、phase、inverted、modifiers）
             angleDelta().y() 仍是 120 一格（QWheelEvent::DefaultDeltasPerStep）
QKeyEvent    新增 keyCombination() 返回 QKeyCombination；text()/isAutoRepeat() 照旧
QWidget      enterEvent(QEvent*) → enterEvent(QEnterEvent*)
QEvent       新增 virtual QEvent *clone() const（不重写，postEvent 的默认实现可能给出警告）
             registerEventType(int hint = -1)（Qt6 带 hint 与 noexcept）
QEvent::Type 枚举有增删（如 QEvent::Enter/Leave 号段调整、新增 DeviceRemoved 等）
             → 结论同上：**永远不要用整数硬编码事件类型**
QElapsedTimer::restart()  Qt6 返回 qint64（Qt5 返回 void）
Qt::TimerType  三档仍是 Precise/Coarse/VeryCoarse（本模块只用本机头里查得到的三个值）
notify()     本机 Qt 6.12 的 qcoreapplication.h 里它在 **public** 段；
             Qt5 时代文档标注 protected。**override 本身与访问级别无关**，
             派生类里调基类实现在两代都合法——变的只是「能不能在 main 里直接调它」。
```

> 补充：第 2 讲特意**不定义 Q_OBJECT 类**，第 5、7 讲也是。这不是偷懒——重写 `event()`/handler 只是普通**虚函数继承**，不需要元对象表；而第 3 讲的过滤器要**发信号**报告吞了谁，才必须上 `Q_OBJECT` + `#include "xxx.moc"`。两相对照，正好把第贰阶段学的「moc 为谁服务」再验一遍。

## 六、常见坑与注意事项

### 1. 忘了 exec()，一切异步都不发生

最经典的一条，也是本模块的存在理由。`show()`、`postEvent()`、`timer->start()` 都只是在**登记**，真正派发都在循环里。`main` 一路走到 `return`，窗口一闪而过、定时器一次不响、`postEvent` 的事件随进程一起消失。记住顺序：**造 app → 建窗口 → show() → exec()**。非要在 `exec()` 之前「干点活」，用 `QTimer::singleShot(0, ...)` 把它排进循环，而不是直接调——第 1 讲和第 4 讲都把「`exec()` 前计数为 0」打印出来当证据。

### 2. 在 paintEvent 里改状态，触发无限重绘

`paintEvent` 的规矩是**只读不写**：它可以读模型、读几何、读颜色，但不能改任何会 `update()`/`repaint()` 的东西（改成员变量后调 `update()`、动子控件尺寸、改 palette 都算）。因为你一改，脏区重新登记，循环下一轮又叫 `paintEvent`，于是** paint → 改 → 再 paint** 的自激环成型：程序不报错，只是 CPU 拉满、界面闪。要做「首次绘制时初始化」，把状态准备放在 `showEvent`、`resizeEvent` 或构造函数里；要测量绘制耗时，用 `QElapsedTimer` 但**别在 paint 里 restart 之后再触发重绘**。第 2 讲故意造了一个「吞掉 Paint 事件而不调基类」的类，让你看见 `paintCount` 直接归零——这就是脏区合并被破坏的一种表现。

### 3. eventFilter 返回 true 把事件吃掉，控件当场失灵

过滤器最强大的地方就是最容易翻车的地方。`return true` 意味着**目标对象的 `event()` 根本不会被调用**。你为了「打日志」而顺手 `return true`，代价是：按钮点不动（`pressed/released/clicked` 全不发）、文本框输不进字、滚动条滚不动、控件不再重绘。规矩只有一条：**观察就返回 `false`，只有你确实接管了这个事件才返回 `true`。** 更狠的坑在全局过滤器（`qApp->installEventFilter`）——它吃掉的是一整个程序的事件，症状往往在八竿子打不着的界面上。第 3 讲把「吞一类事件 → 对应计数变 0」成对打印，就是让你建立「吃了什么、少了什么」的因果直觉。

另外两条容易忽略的顺序问题：同一目标上多个过滤器是**逆序**（后装的先调）；过滤器对象**必须比被过滤对象活得久**，否则 Qt 靠 `destroyed` 信号清理，但你在过滤器里存了指向目标的裸指针就是悬空——用 `QPointer<QWidget>` 守（第 3 讲末尾专门演了一次析构顺序）。

### 4. postEvent 传栈上对象（sendEvent 传堆上对象）

两条相反的铁律，一起记：

```cpp
MyEvent e(...);
QCoreApplication::sendEvent(&target, &e);      // ✅ 同步：函数返回即结束，栈对象安全
QCoreApplication::postEvent(&target, &e);      // ❌ 悬空：循环下一轮才派发，那时 e 已销毁
QCoreApplication::postEvent(&target, new MyEvent(...));  // ✅ 所有权交给 Qt，派发后由 Qt delete
```

`postEvent` 的签名（本机 `qcoreapplication.h` 第 114 行）参数就是裸指针 `QEvent *event`，**没有任何编译期保护**。事件对象在队列里躺着的时候被提前析构，症状是随机崩溃或随机收到垃圾数据，最难查。反过来也别在 `postEvent` 之后继续用那个指针，或者自己 `delete` 它——那是 Qt 的。第 6 讲把所有权、优先级、合并三件事一次列明，就是为了让你养成「`postEvent` 就 `new`、`new` 就只为 `postEvent`」的肌肉记忆。

### 5. 在析构函数里发信号给已销毁的接收者

`~QObject()` 的时机很微妙：进入析构函数时，**你这个对象已经不再是「你」**（派生类部分先销毁，虚函数不再派发到派生类），而你的子对象也已经销毁、`QPointer` 指向你的地方可能已空。此时在析构函数里 `emit somethingChanged()`，槽里再去读你的成员——读的是已被销毁的派生类数据，未定义行为。同理，接收者已经先你而死、而连接还挂着，也是事故。

正确做法：需要「死前通知别人」，用 **`QObject::destroyed` 信号**（本机 `qobject.h` 第 364 行附近有 `deleteLater`，`destroyed` 在 Q_SIGNALS 段），它在子对象清理完毕后发出，接收方拿到的 `QObject *` 只用来比对身份、**不要 deref**；或者更稳——在**明确的收尾函数**（`close()`、`stop()`、`aboutToQuit`）里发信号，把析构函数留空。跨线程场景别用手写事件+析构通知硬凑，用 `deleteLater()` 让事件循环决定销毁时机。第 3 讲的「过滤器与被过滤对象谁先死」演示，就是这条链在事件层的翻版。

### 6. 用定时器代替事件循环里的正确异步

新手治卡顿的三板斧常常是：`while` 里加点 `QThread::sleep`、加个 10ms 的 `QTimer` 轮询状态、或者在槽里调 `processEvents()` 让界面「活过来」。三条都是错的：

- **`processEvents()` 在派发栈里再入**：你以为在处理第 3 个点击，其实第 4 个点击已经在你的槽里重入了同一个函数，状态机被踩烂、容器被边遍历边改。第 1 讲把「重入深度」打出来，就是让你看见这个数字不是理论。
- **轮询定时器**：10ms 的 `QTimer` 每秒做 100 次「有没有变化」的判断，99 次答案是「没有」。有变化时该被通知的是**事件源本身**（`textChanged`、`dataChanged`、`stateChanged`），不是你的秒表。
- **用定时器「延后」**：如果你的目的只是「等本轮事件处理完再干」，正确工具是 `QTimer::singleShot(0, ...)` 或 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`——**语义是「排到队列末尾」，定时器只是它的载体**。写 `singleShot(16, ...)` 期待「等绘制完成」就是在赌时序。

判据一句话：**「等某件事发生」→ 事件/信号；「每隔一段时间做一次」→ 定时器；「延后到本轮结束」→ `singleShot(0)`。** 三者混用就是卡顿与竞态的开端。

### 7. 额外两条：拖放与输入的「在我机器上好好的」

- **`dragEnterEvent` 里只调 `accept()`**：`accept()` 只表示「这个事件我认领了」，而**光标反馈和最终 `dropAction()` 走的是 `proposedAction()`/`acceptProposedAction()` 这条路**。只 `accept()` 不 `acceptProposedAction()`，常见症状是「能放下但一直是禁止光标」或「放下的动作和你想的不一样」。第 7 讲把两者并排打印 `dropAction()` 结果。
- **用 `button()` 判断「此刻按住了什么」**：`button()` 是「这一次**哪个**按钮变了」，`MouseMove` 事件里它常是 `Qt::NoButton`；要看按住状态用 `buttons()`。这个坑在 Qt5 时代靠 `mouseButtons()` 躲过，Qt6 改名后照踩。第 5 讲双键同按的打印专门对上了它。

## 七、老师傅的经验之谈

### 1. 背链，不是背类名

`QEvent::Type` 有几百个值，背下来一分用处都没有。真正值钱的是第五节那张链：**事件从哪儿来、经过谁、在哪一层能被吃掉、吃完之后剩下什么**。链在脑子里，遇到任何「事件没收到」的问题，你的排查顺序是天然的：先确认循环在跑（④ 之前）→ 有没有过滤器返回了 true → 你有没有漏调基类 → 是不是 accept/ignore 让父控件收不到 → 最后才怀疑 type。

### 2. 合成事件是最好的实验台

本模块最值钱的手法不是某个 API，而是**「自己造事件投进去」**。从此你验证一个行为只要两行代码，不用手点一百次，不用写 QtTest 才能测，还能测出真实鼠标永远复现不了的边界（双键同按、`angleDelta` 恰好 0、`source() == nullptr` 的跨进程 drop）。师傅把这个习惯传给你：**能用投递复现的，就别依赖人手。**

### 3. 先分清「事件还是信号」，再决定改哪层

想改一个 `QPushButton` 的行为：接 `QMouseEvent`（层 ⑥，你得继承），还是接 `clicked()`（信号，层 ⑥ 之后控件翻译好的），还是挂 `eventFilter`（层 ④，不继承），还是在 `notify` 里拦（层 ③，影响全程序）？四个位置四种代价。选错了，症状是「明明收到事件了但按钮外观不更新」（你在 `mousePressEvent` 里 return 而没调基类）或者「一个过滤器搅黄了整个界面」（全局过滤器返回了 true）。

### 4. 数字要自己跑，别信师傅报的数

本机跑不出 Qt 程序，所以师傅在示例里**一个具体数字都没编**：定时器三档的真实间隔、`paintEvent` 的次数、重入深度、`registerEventType()` 的返回值，全部写成「以你终端上的数字为准」，只承诺**相对关系**（Precise ≥ Coarse 的抖动更小、`exec()` 前 tick 为 0、吞掉 release 后 `clicked` 为 0）。这既是不骗你，也是逼你亲自跑一遍——这一阶段每个示例都设计成跑完自己吐结论，就是在训练你「看证据下判断」的手感。

### 5. 静默 bug 只能靠打印计数抓

不调基类、`postEvent` 传栈对象、`accept()` 与 `acceptProposedAction()` 之差、`true` 吃掉事件——这四类 bug 的共同点是**不崩、不报错、不警告**，只是功能悄悄没了。对付它们唯一的办法是在每一层放一个计数器，让「应该被叫到的次数」变成可见的数字。本模块 7 个示例里所有 `int xxxCount` 都是这个用途，别嫌它啰嗦；你以后查 GUI 的「点不动」问题，也是这个路子。

### 6. 诚实对待「编没编过」

本机此刻运行时在升级、编不动，就大方写「未在本机实测编译」，但**每个 API 都查真实头文件核对**，注释里给到文件与行号；本机头里查不到的重载和枚举，一律不用、或者明确标成「文档语义」。写 Qt 和做人一样：不确定就别乱说——这正是这套资料能叫「无谬误」的原因。

> 第叁阶段就到这儿。这 7 个文件把「事件到底怎么走到你手上」这件事钉死了：一条链、三个插手位置、四类具体事件、一整套无鼠标的验证手法。接下来进入绘制与控件阶段，你会看到 `paintEvent` 怎么画、布局怎么在链里响应尺寸事件——那些都建在你现在能背出的这条派发链上。想看动画复盘，随时回来翻 [图解 · 事件循环](../visuals/event_loop.html) 与 [图解 · 事件过滤器](../visuals/event_filter.html)。加油，徒弟！
