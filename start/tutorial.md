# 第壹阶段 · 起步（Qt 从入门到资深）

> 老师傅带你把 Qt 程序的地基打牢，一步一个脚印

## 一、模块简介

徒弟啊，欢迎来到 Qt 的世界。这一关是**第壹阶段「起步」**，说白了就是打地基——把「一个 Qt 程序到底是怎么活起来的」这件事彻底搞明白。

Qt 这门东西，说难也难，说顺也顺。难在它不像标准 C++ 那样「写完就能跑」，它中间夹了一层**元对象系统（moc）**和一层**事件循环**，新手很容易被「为什么我 show() 了没东西」「为什么 connect 连不上」这类问题绕晕；顺在一旦你把 **QApplication / 事件循环 / moc** 这三根主线抓住，后面学控件、学布局、学模型视图，全是这套地基上长出来的花样。

咱这起步模块一共 **6 章**，从第一个窗口一路讲到「无头运行、自动化自测」。学完这 6 章，你算是正式入了 Qt 的门，而且**每章示例都能无人值守跑完、自己吐结论**——这是后面所有学习能自动验证的地基。

> 环境事实：Qt 6.12.0、Windows、MinGW-w64（MSYS2 ucrt64，gcc 15.2/16.2）、CMake ≥ 3.21 + Ninja、C++17。构建走 `find_package(Qt6 COMPONENTS Widgets)` + `Qt6::Widgets` + `CMAKE_AUTOMOC=ON`。

## 二、学习目标

学完本模块，你要能做到：

1. 说得清「为什么必须**先构造 QApplication、再碰 QWidget**」，并解释 `show()` 与事件循环的先后关系
2. 讲明白 `Q_OBJECT` 是什么、`moc` 生成了什么、`AUTOMOC` 替你做了什么，以及 `.cpp` 里定义的类为什么末尾要 `#include "xxx.moc"`
3. 会用 `QApplication` 把应用身份（组织名/应用名）、外观（字体/调色板/样式）和生命周期信号（`aboutToQuit`）配起来
4. 能用三种写法把信号接到响应上（成员函数指针 / lambda / 带 context 的 lambda），并说清「事件驱动 ≠ 轮询」
5. 会用分级日志 + `QLoggingCategory` + `QT_LOGGING_RULES` 调试，并能让一个 GUI 程序在 `QT_QPA_PLATFORM=offscreen` 下**跑完、截图、断言、按退出码交差**

## 三、文件列表与学习重点

咱一共 6 个示例（外加第 2 章配套的一对头/实现文件），按顺序来，别跳：

```text
  01_hello_window.cpp          ← 第一个窗口
    重点：QApplication 必须先构造（加载 QPA 插件/建事件循环/给字体样式）；
          show() 只是提重绘请求，真正绘制在 exec() 的事件循环里；Qt 内置命令行选项

  02_project_anatomy.cpp       ← 工程解剖（第一道坎：Q_OBJECT/moc）
    重点：自定义窗口类；Q_OBJECT 是给 moc 的标记；AUTOMOC 自动调度 moc；
          .cpp 内定义的 Q_OBJECT 类要 #include "02_project_anatomy.moc"；
          声明/实现分离（配套 startwidget.h 定义类 / startwidget.cpp 写实现）

  03_application_object.cpp    ← QApplication 的职责
    重点：applicationDirPath/applicationFilePath；setOrganizationName+setApplicationName
          （不设 → QSettings 组织名很奇怪）；desktopFileName；setFont/setPalette/setStyle
          + QStyleFactory::keys()；aboutToQuit 信号

  04_first_interaction.cpp     ← 最小交互闭环
    重点：QPushButton+QLineEdit+QLabel；三种 connect 写法各演一次；
          事件驱动不是轮询；Q_EMIT 只是空宏、与 emit 等价

  05_logging.cpp               ← 调试输出
    重点：qDebug/qInfo/qWarning/qCritical/qFatal 分级与后果（qFatal 会 abort）；
          qSetMessagePattern 占位符；qDebug().noquote().nospace() 链式（注意没有 quoted()）；
          QLoggingCategory + QT_LOGGING_RULES / setFilterRules 分类开关

  06_headless_and_offscreen.cpp ← 无头运行与自动化（全课自测工具）
    重点：QT_QPA_PLATFORM 的 offscreen 与 minimal 之别；为什么 CI 能测 GUI；
          grab()→QPixmap/QImage→save PNG；resize/sizeHint 断言式自检；
          platforms 插件目录与 windeployqt 的关系；用 exit(0/1) 退出码交差
```

> 第 2 章为什么多两个文件：`startwidget.h` / `startwidget.cpp` 是「一个正常 Qt 类」的标准拆法（接口在头、实现在 .cpp，AUTOMOC 自动为它生成并编译 `moc_startwidget.cpp`）。而 `02_project_anatomy.cpp` 里**故意**又塞了一个写在 .cpp 中的 Q_OBJECT 类，用来对照出「写在头里的类 vs 写在 .cpp 里的类」两种 moc 接线方式的差别。

## 四、学习顺序建议

师傅给你划这条主线，和一道硬坎：

### 主线：为什么是这个顺序

- **第 1 → 第 2**：先让窗口出来建立信心，再回头拆「它凭什么能跑」，正好引出 moc。
- **第 2 → 第 3**：认识了工程零件，再认识「大管家」QApplication 管哪些全局的事。
- **第 3 → 第 4**：应用配好了，才能让它**动起来**——信号槽交互是 Qt 的灵魂。
- **第 4 → 第 5**：程序会跑了，得学会**看它吐的话**（日志），否则调 bug 两眼一抹黑。
- **第 5 → 第 6**：最后脱离「必须有人点」的依赖，能在 CI 里**无人值守自测**——这也是全课后续每个示例的验证范式。

### 硬坎：moc（02 号文件）

这是新手在 Qt 里的第一道坎。别绕着走，正面撞。你要在三个小练习里，亲手把三种「忘了」的报错都见一遍：
忘了 `Q_OBJECT`（自定义信号连不上）、忘了 `AUTOMOC`（满屏 vtable undefined reference）、`.cpp` 里的类忘了 `#include "xxx.moc"`（只有这个类出问题）。撞过这三回，你就再不怕 moc 了。

### 学习方法

每个文件都要：先读注释块（顶部「本讲解决什么问题」）→ 按注释里的 CMake 目标名构建 → 跑一遍看自动结论 → 改代码做小练习 → 改出问题了试着修。光看不练，等于白看。

> 🎬 想看「QApplication 构造 → show() → 事件循环 → paint → 退出」这条链的分步动画：[图解 · 启动流程](../visuals/startup_flow.html)。

## 五、核心知识点梳理

把最重要的三件事——**QApplication、事件循环、moc**——的关系拎清楚：

### 1. QApplication 是地基，事件循环是心脏

```cpp
QApplication app(argc, argv);   // 先立地基：加载 QPA 插件、建事件循环、给字体/样式
window.show();                  // show 只是「挂号」：标记可见 + 投递一次重绘请求
return app.exec();              // 心脏开始跳：在这里才真正派发 paint/输入/定时器
```

`show()` 之后画面还没画出来，得等 `exec()` 把事件循环转起来。忘了 `exec()`，main 一路走到 return，窗口一闪而过。想「等窗口真画好了再干点什么」，就把那段逻辑用 `QTimer::singleShot(ms, ...)` 排进事件循环。

### 2. moc：Qt 从 C++ 里长出来的那部分

C++ 本身没有「信号/槽」和「运行时反射」。Qt 靠 **moc（元对象编译器）** 读你的 `Q_OBJECT` 类，**事先把元对象表和信号的实现代码生成出来**，再交给 g++ 正常编译。所以：

- 类写在 **头文件** → AUTOMOC 生成并**单独编译** `moc_<类名>.cpp`，源码里啥都不用 `#include`；
- 类写在 **.cpp** → AUTOMOC 生成 `<本文件名>.moc`，你必须在**类定义之后**手动 `#include "02_project_anatomy.moc"`，否则这个类的信号实现/vtable 找不到。

### 3. 三者怎么咬合

`QApplication` 造好事件循环 → 你把控件的信号用 `connect` **登记**给某个槽/lambda → `exec()` 里事件一来就派发 → 而「信号能被 connect」这件事，靠的是 `moc` 给带 `Q_OBJECT` 的类生成了元对象与信号实现。**没有 moc，信号槽这套根本不成立；没有事件循环，登记了也没人去触发。**

> 补充：`Q_EMIT` / `emit` 都是 `qtmetamacros.h` 里的**空宏**（第 51、53 行），纯给人看的记号，编译器眼里等价；Qt 自己的源码一律写 `Q_EMIT` 以避免和第三方库的 `emit` 撞名。

## 六、常见坑与注意事项

### 1. 在 QApplication 之前构造 QWidget

`QWidget` 依赖 `QApplication` 提供的 QPA 插件与事件循环。在 `app` 构造之前就 `new QWidget`，是未定义行为，轻则断言失败重则直接崩。**永远先造 QApplication。**

### 2. 忘了 exec()，窗口一闪而过

`show()` 只是提请求，真正绘制和事件派发都在 `exec()` 里。main 里调了 `show()` 却没进事件循环就 return，窗口刚出现就被销毁。记住顺序：造 app → 建窗口 → `show()` → `exec()`。

### 3. 把耗时操作写在槽里，界面卡死

槽函数是在**事件循环线程（GUI 线程）**里执行的。在里面跑大循环/同步网络/读大文件，会把整个事件循环堵死，所有登记都响应不了，界面直接「未响应」。耗时活要挪到别的线程（后面线程模块会讲），起步阶段先记住：**槽里别干重活。**

### 4. Q_OBJECT 忘写，connect 失败

自定义信号/槽、`qobject_cast`、`property()/setProperty()`（`qobject.h` 第 344 行 `bool setProperty(const char*, const QVariant&)`）这些**元对象能力**，全靠 `Q_OBJECT`。漏了它，`connect` 自定义信号会连不上或不兼容报错。加了 `Q_OBJECT` 却没让 AUTOMOC 跑，则链接期报 vtable/staticMetaObject 未定义。

### 5. 忘了开 CMAKE_AUTOMOC

Qt6 工程标配 `set(CMAKE_AUTOMOC ON)`。没开，moc 根本不被调度，任何用到信号槽/qobject_cast 的类都编不过或链不上。而且**带 Q_OBJECT 的头/源文件必须列进 `add_executable`**，没列进去的 AUTOMOC 不去扫描。

### 6. 无头环境找不到平台插件

在 CI/服务器上用 `QT_QPA_PLATFORM=offscreen`，前提是运行目录能找到 `platforms/qoffscreen.dll`。把它拷到别的机器就报「no Qt platform plugin could be initialized」——多半是 `platforms` 目录没跟过去，用 `windeployqt` 补齐（本机在 `C:\msys64\ucrt64\bin\windeployqt.exe`）。另外别用 **minimal** 平台截图，它不产生像素，要截图请用 **offscreen**。

## 七、老师傅的经验之谈

### 1. 心智模型先于 API

别急着背类名。先把「**登记响应、交给事件循环**」这九个字刻进脑子：你不该去「轮询有没有事」，而该「有事时该干嘛提前 connect 好」。这个模型通了，Qt 大半的困惑自动消解。

### 2. moc 不神秘，就是个代码生成器

很多人对 moc 有心理阴影。你就把它当「一个在你编译前，帮你把信号函数体、元对象表**生成**成普通 C++ 的工具」。三种「忘了」的报错长什么样，亲手撞一遍，从此见山是山。

### 3. 每个示例都要能无人值守跑完

这是本模块立的一条硬规矩，也是师傅的偏好：**凡「必须有人点关闭」的示例都是不合格的**。全部用 `QTimer::singleShot` 排自动脚本，最后 `quit()`/`exit(code)`。第 6 步更是把这套变成断言式自测——以后你写 GUI 也能像测纯函数一样测它。

### 4. 诚实对待「编没编过」

本机此刻运行时在升级、编不动，就大方写「未在本机实测编译」，但**每个 API 都查真实头文件核对**。查不到签名的一律不用，宁可用更简单的替代。写 Qt 和做人一样：不确定就别乱说——这正是这套资料叫「无谬误」的原因。

### 5. 多敲多跑多改

Qt 是跑起来才看得懂的。每个例子自己按注释里的目标名构建一遍、跑一遍、改一改。第 6 步的自测，你亲手用 `windows` 和 `offscreen` 各跑一次，比看十遍都管用。

> 起步阶段就到这儿。这 6 个文件只是入门，把地基和「自动化验证」这两件事立住了。等你啃下来，就能进下一阶段「控件与布局」，去把真正的界面搭起来。加油，徒弟！
