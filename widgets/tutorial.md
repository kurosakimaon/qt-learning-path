# 第贰阶段 · 控件与布局（Qt 从入门到资深）

> 老师傅带你把界面真正搭起来：认零件 → 搭骨架 → 谈尺寸 → 上外观

## 一、模块简介

徒弟，起步阶段你已经让一个窗口「活」了。这一关要往里面放东西，并且让它**经得起折腾**——拖动、缩放、换字体、换皮肤、换语言，界面都不塌。

本模块一共 **10 章**，分四段走：

- **01~05 认零件**：按钮与标签、文本控件、容器、便利类列表/树/表、对话框。这五章是「够用」，不要求你背下来，只要求你知道遇到需求该抓哪个类。
- **06 搭骨架**：`QMainWindow` 的五块保留区域，把「一块面板」升级成「一个真应用窗口」。
- **07~08 谈尺寸**：**本模块的坎**。`sizeHint` / `minimumSizeHint` / `QSizePolicy` / stretch 因子 / alignment 站位——界面「变大留白、变小挤成一团」的病根全在这里。
- **09~10 打磨**：样式表（QSS）管「长什么样」，国际化（i18n）管「说什么话、按哪里的习惯排」。

一句话概括这个模块的心智模型：**控件只报自己的尺寸，布局才是分钱的人，样式和翻译只是最后刷的那层漆。**

> 环境事实：Qt 6.12.0、Windows、MinGW-w64 GCC 13.1.0（Qt 官方安装器自带：D:\Qt\Tools\mingw1310_64）、CMake ≥ 3.21（本机 3.30.5）+ 生成器 MinGW Makefiles、C++17。构建走 `find_package(Qt6 COMPONENTS Widgets)` + `Qt6::Widgets` + `CMAKE_AUTOMOC=ON`。
>
> 实测口径：**本模块 10 个示例都已在本机真实编译、链接、运行**（`QT_QPA_PLATFORM=offscreen` 无头跑完并自己打印证据），包含在顶层 `ctest` 的 75/75 绿里（73 个编号示例 + 实战项目 2 个）；每个 .cpp 也过 `moc` + `g++ -std=c++17 -Wall -Wextra`（零 error、零 warning），行号写在用到的地方。第 10 讲的工具链与翻译现状是**本机现查**的：`lupdate.exe / lrelease.exe / linguist.exe` 在 `D:\Qt\6.12.0\mingw_64\bin`；翻译目录由 `QLibraryInfo::path(QLibraryInfo::TranslationsPath)` 给出，本机这一轮指到 `D:/Qt/6.12.0/mingw_64/translations`，里面有 **314 个 .qm**（含 `qtbase_zh_CN.qm`），所以示例装载成功后当场打印「装前 `OK` → 装后 `确定`」这样的对照。**这一类环境事实换机器就会反过来**，第 10 讲因此不把 `load()` 的返回值当断言，只把 true/false 两条分支都解释清楚（旧版在这里写过一句「本机一个 .qm 都没有、目录不存在」——那是运行时还没装全时的老实话，后来环境变了它就成假结论，所以现在的口径是**目录现查、结果打印**，别把某台机器的安装路径写死进代码）。完整输出存档在 `runlogs/widgets_10_*.log` 等（重跑：`bash _tools/run_all.sh '^widgets'`）。

## 二、学习目标

学完本模块，你要能做到：

1. 拿到一个界面需求，能在 3 秒内说出用哪个控件族（按钮族 / 文本族 / 容器族 / 便利类 / 对话框），并说清它们的**信号语义差别**（`pressed` 与 `clicked`、`textChanged` 与 `textEdited`）
2. 用 `QMainWindow` 把菜单 / 工具栏 / 中心区 / 状态栏 / dock 五块摆到位，并解释**为什么 `QMainWindow` 不能 `setLayout()`**
3. 说清一个控件对外报的**两个尺寸**（`sizeHint` / `minimumSizeHint`）与**两套政策**（水平/垂直 `QSizePolicy`），外加三道**硬墙**（`setMinimumSize` / `setMaximumSize` / `setFixedSize`）
4. 把「多出来的空间怎么分」（stretch + ExpandFlag）与「自己那点地方贴哪边」（alignment）彻底分开，并能用打印 `geometry()` 的方式**证明**自己的判断
5. 写出**非阻塞**的对话框流程（`show()` / `open()` + 定时器自动 `accept()/reject()`），并说清 `exec()` 起嵌套事件循环为什么是重入的头号来源
6. 用 QSS 做一键换肤，并且知道它**什么时候静默失效**（漏 `Qt::WA_StyledBackground`、`border-style` 不认识、`:/` 资源路径写错）
7. 把「人看的文本」全部走 `tr()`：知道上下文来自类名、第二个参数是给译者的注释、复数 `%n` 由 `tr` 自己填；数字与日期用 `QLocale` 在**最后展示那一刻**才格式化

## 三、文件列表与学习重点

十个示例，按序来，别跳：

```text
  01_buttons_and_labels.cpp     ← 按钮与标签（QAbstractButton 家族 + QLabel）
    重点：checkable 机制四族共用；pressed/released/clicked/toggled 各发几次、什么顺序
          （用计数器实测）；click() 与 animateClick() 之别；autoExclusive 与 QButtonGroup
          的分工（跨父控件 + 给 id + 统一信号）；QLabel 的富文本 / setBuddy / pixmap / movie；
          QPushButton::setMenu 与 QToolButton::setMenu 挂菜单的正确姿势

  02_text_widgets.cpp           ← 文本控件（QLineEdit / QPlainTextEdit / QTextEdit）
    重点：textChanged / textEdited / editingFinished 的真实触发时机（投 QKeyEvent 逼出来）；
          校验器三件套 QIntValidator / QDoubleValidator / QRegularExpressionValidator 与
          **三态**（Invalid / Intermediate / Acceptable）；QPlainTextEdit 与 QTextEdit 的
          性能差别来自内部文档布局；QTextCursor + beginEditBlock/endEditBlock 决定 undo 算一步

  03_containers.cpp             ← 容器控件（QGroupBox / QTabWidget / QStackedWidget /
                                   QScrollArea / QSplitter / QToolBox）
    重点：容器「只组织孩子、不管尺寸谈判」这条铁律；QGroupBox::setCheckable 一钩开关整组；
          QTabWidget 页面对象都活着只是隐藏；setWidgetResizable(true) 的真正含义；
          QSplitter::setSizes() 是**请求**，sizes() 读回来才是谈判结果

  04_convenience_itemviews.cpp  ← 便利类列表/树/表（QListWidget / QTreeWidget / QTableWidget）
    重点：三个便利类的共同套路与「**项不是控件**」；用 model() 实测证明内部就是一张 model；
          setModel 在便利类里被降级成 private（该转 model/view 的硬信号）；
          setItemWidget/setCellWidget 的代价；数据量大/在库里/要懒加载/要自定义 role 就弃用它

  05_dialogs.cpp                ← 对话框（QDialog / QDialogButtonBox / QFileDialog /
                                   QMessageBox / QColorDialog / QFontDialog）
    重点：三条出口 exec()/open()/show() 都走 done(int) → finished(int) → accepted()/rejected()；
          模态三档 Qt::WindowModality 与 setModal 只是「意图声明」；QMessageBox 的 result()
          读回的是 StandardButton 位值（Ok = 0x400 = 1024）；自定义对话框「校验不过别关」；
          **静态方法一律阻塞**，本讲只写在注释里。文件里定义 Q_OBJECT 类 → 末尾 #include "05_dialogs.moc"

  06_mainwindow.cpp             ← QMainWindow：把一个「真应用的窗口」搭起来
    重点：五块保留区域与「懒创建」语义（menuBar()/statusBar() 问到才建）；
          **硬规则：QMainWindow 不能 setLayout()**（打印内部 QMainWindowLayout 给你看）；
          QAction 一份动作喂菜单/工具栏/快捷键/状态栏提示；QActionGroup 互斥 + 标准键 +
          ShortcutContext 四档；showMessage 的 timeout 语义（0 = 常驻）+ addPermanentWidget；
          saveState/restoreState 的顺序铁律「closeEvent 里存、show() 之后取」；
          toggleViewAction 是免费的 View 菜单。文件里定义 Q_OBJECT 类 → 末尾 #include "06_mainwindow.moc"

  07_layout_basics.cpp          ← 布局基础（QBoxLayout / QGridLayout / QFormLayout + 嵌套 + 所有权）
    重点：三种主布局各管什么；addWidget/addLayout/addSpacing/addStretch 塞进去的
          **是四种不同的 QLayoutItem**；**stretch 分「多出来的空间」，alignment 管「贴哪边」**
          （两轮 resize + 打印 geometry 做成可见证据）；谁删谁、takeAt 之后 widget 归谁、
          removeWidget 之后为什么还在屏幕上、同一个 widget setLayout 两次会听到哪条警告

  08_size_policies.cpp          ← 尺寸谈判（sizeHint / minimumSizeHint / QSizePolicy / stretch）
    重点：控件自报的两个尺寸；QSizePolicy 由 GrowFlag=1 / ExpandFlag=2 / ShrinkFlag=4 /
          IgnoreFlag=8 四个**位标志**拼出来（每个 Policy 的整数值打印出来看）；
          min/max/fixed 是不可协商的硬墙；谈判顺序一句话：**先夹硬墙 → 政策决定能不能缩/长 →
          余量按 stretch + ExpandFlag 分 → alignment 管站位**。两轮 resize 后全部 geometry 打印，
          不靠肉眼（offscreen 也出数）

  09_style_sheets.cpp           ← Qt 样式表（QSS）：选择器、盒模型与一键换肤
    重点：类型 / .类 / 通配 / #ID / [属性] 五种选择器 + 后代（空格）与子（>）的实测差别；
          伪状态 :hover :pressed :checked :disabled :focus :selected；盒模型三圈 margin/border/padding
          与 border-style 的**有限集**（写了不认的值：不报错、也不生效）；
          **裸 QWidget 背景不画 → Qt::WA_StyledBackground**（四格对照抓像素）；
          qApp 级与控件级的作用域/级联/优先级，styleSheet() 读回来的只是自己那串；
          240 个按钮上实测重新 polish 的代价 + style() 类名从 QWindowsVistaStyle 变 QStyleSheetStyle；
          sub-control 与 `:/` 资源路径写错**没有任何报错**。文件里两个 Q_OBJECT 类 → 末尾 #include "09_style_sheets.moc"

  10_internationalization.cpp   ← 国际化（i18n）：tr 的三个参数、上下文、复数与 QLocale
    重点：**tr() 的第二个参数是给译者的注释，不是上下文**；上下文 = 调用点所在**类的类名**
          （staticMetaObject.className()），手工指定上下文只有 QCoreApplication::translate(
          context, key, disambiguation, n) 这条路（qcoreapplication.h 190~193 行）；
          复数 tr("1 file", "%n files", n) 与 tr("%n file(s) found", "", n)，
          **%n 由 tr 自己填，QString::arg() 填不了**；QTranslator::load 的三个重载——
          **第一个重载不看 locale**（qtranslator.cpp 466~512 行只会把「你给的文件名」按
          `search_delimiters` 从右往左切，切无可切就 false；文档 434~458 行那个 fr_CA 例子传的
          是 `load("foo.fr_ca", …)`，语言是**调用者写进文件名**的），要按界面语言找得用带
          `QLocale` 的那个（738~752 行，取 `uiLanguages(TagSeparator::Underscore)`），
          第三个重载 `load(const uchar*, int len, …)` **不复制数据**（文档 758~759 行明说
          "The data is not copied"，指针的生命周期归你）；catalog 命名规则（qtbase_zh_CN.qm 那一套）、
          installTranslator/removeTranslator（removeInstalledTranslator 本机头文件里不存在）、
          「对 translator 对象直接调 translate() 不需要 install」；QGuiApplication::layoutDirection/
          setLayoutDirection/isRightToLeft（RTL 是整套镜像）；QLocale 的数字分组与
          日期 LongFormat/ShortFormat/NarrowFormat、toDate 把 "01/03/2026" 解成不同日子。
          文件末尾 #include "10_internationalization.moc"
```

> 为什么第 5、6、9、10 讲多了一行 `#include "xxx.moc"`：这四讲把带 `Q_OBJECT` 的类**写在 .cpp 里**（起步阶段第 2 讲那道坎在这里复发）。写在头里的类由 AUTOMOC 单独编译 `moc_名字.cpp`，源码里什么都不用 include；写在 .cpp 里的类，必须在类定义之后 include 生成的 `<本文件名>.moc`。

## 四、学习顺序建议

师傅给你划这条主线和两道坎。

### 主线：四段为什么这么排

- **01 → 02 → 03 → 04**：从「按一下」（按钮）到「打一个字」（文本），到「摆一堆」（容器），到「列一片」（便利类）。难度不在 API，在**信号语义**：这四章每一章都拿计数器或事件把「到底发几次」测给你看。
- **04 → 05**：便利类讲完「数据怎么摆」，正好接对话框「数据怎么**收**」——收数据就绕不开校验和「校验不过别关」。
- **05 → 06**：对话框是「临时窗口」，06 把「常驻窗口」的结构讲完：五块保留区域 + QAction 这一份动作多处复用。
- **06 → 07 → 08**：窗口一搭起来，尺寸问题立刻爆发（dock 拖不动、状态栏挤掉按钮、中心区缩成一团）。07 讲「怎么把空间分出去」，08 讲「一个控件能长多大能缩多小」——**这两章合起来才是完整的谈判课，别只读一个**。
- **08 → 09 → 10**：形状谈定了才谈漆（QSS），漆刷完了才谈话（i18n）。顺序反了会出现「样式表改一行，布局全乱」的怪事——那是你在没搞懂尺寸的时候动了外观。

### 第一道坎：07~08 尺寸谈判（本模块的坎）

症状你肯定见过：窗口一大就一片空白、一小就挤成一团、`setSizes()` 调了没反应、加了 stretch 控件还是不肯变大。这些**都不是 bug，是谈判规则你没读**。

跨法不是背，是**测**：07/08 两讲都排了「两轮 resize，每轮把所有相关控件的 `geometry()` 打印出来」。你拿这张表去对照谈判四步（硬墙 → 政策 → 余量分配 → 站位），一遍就对上了。

> 🎬 想看「sizeHint → min/max → sizePolicy → stretch → alignment」一步步怎么算出来的分步动画：[图解 · 尺寸谈判](../visuals/layout_negotiation.html)。配合 08 讲打印的表格看，效果最好。

### 第二道坎：05 的「阻塞与重入」

`QDialog::exec()` 起的是**嵌套事件循环**：它卡在原地，同时界面还在转。人一点就没事，没人点就永远停着——本资料库所有示例都要在 ctest 里无人值守跑完，所以主路径**一律不许** `exec()`、不许静态 `QMessageBox` 方法、不许 `QFileDialog::getOpenFileName`、不许 `QMenu::exec()`。更狠的是：在槽里套 `exec()`，事件循环就重入了，你后面所有「我以为它不会触发」的逻辑都可能被突然触发。**这道坎是习惯问题，不是知识问题，从 05 讲就开始改。**

> 卡片上的「第 7 步 · 第二道坎」「第 8 步 · 第三道坎」是**全课口径**（起步阶段 02 的 Q_OBJECT/moc 是全课第一道坎）；本节说的「两道坎」是**本模块内部**口径：谈判（07~08）与阻塞重入（05）。别被数字绕晕。

### 学习方法

每个文件：先读顶部注释块（本讲解决什么 + 行号核对清单）→ 按注释里的 CMake 目标名构建 → 跑一遍看自动打印的证据 → 做小练习 → 改出问题了自己修。光看不练，等于白看。

## 五、核心知识点梳理

把这条链刻下来：**控件 → 布局 → 尺寸协商 → 绘制 → 样式**，最后 i18n 贯穿在最外层。

### 1. 控件层：只干两件事——「报尺寸」和「发信号」

每个控件对外只有两个关于大小的发言（第 8 讲）：

```cpp
// 示意；逐字签名以各讲 .cpp 里标了行号的正文为准
QSize a = btn->sizeHint();          // 「我希望这么大」
QSize b = btn->minimumSizeHint();   // 「再小我就没法看了」
QSizePolicy sp = btn->sizePolicy(); // 水平/垂直各一套政策（四个位标志拼出来的）
```

信号语义是这四章的真正内容：`pressed` 是按下、`released` 是抬起、`clicked` 是一次完整的按-抬且在控件内、`toggled` 只在 checkable 状态变化时发。`textChanged` 程序改文本也发，`textEdited` 只有用户敲键盘才发——拿错这条，你的「实时校验」会在 `setText()` 时自己咬自己。

### 2. 布局层：布局是「分钱的人」，不是「会魔法的框」

```cpp
// 示意：同一个 addWidget 的三个参数各管一件事，这是新手最容易混的一格
box->addWidget(w, 3);                    // stretch=3：多出来的空间里，w 那份给 3 份
box->addWidget(w, 0, Qt::AlignRight);    // stretch=0 + 站位：w 自己那么大，贴右边
box->addStretch();                       // 塞一个「可伸缩间隔项」顶开后面
```

`addWidget / addLayout / addSpacing / addStretch` 对应 `QLayoutItem` 的四种子类：**控件项、子布局项、固定间隔项、可伸缩间隔项**。想清楚「我这一行塞进去的是哪一种」，比记 API 有用。

### 3. 尺寸协商：四步顺序，一句话背下来

> **先用 min/max 硬墙夹住 → 再用 sizePolicy 决定能不能缩/能不能长 → 多出来的空间按 stretch 因子 + ExpandFlag 分 → 最后 alignment 管站位。**

```cpp
// 示意：第 8 讲的对照实验就是这么排的
w.setMinimumWidth(120);                       // 硬墙，不可协商，优先级高于 sizeHint
sp.setHorizontalPolicy(QSizePolicy::Expanding); // 愿意长大（ExpandFlag）
box->addWidget(&w, 1);                          // 余量按 1 份分给它
```

三个反复出现的错（第 7、8 讲都实测过）：

- `QSplitter::setSizes(list)` 之后马上 `sizes()` 读回来 ≠ 你写进去的值——那是**请求**，读回来才是谈判结果
- 同一份 `layout` / 同一个 widget **`setLayout()` 两次**：Qt 警告 `QLayout: Attempting to add QLayout ... which already has a layout`，第二个布局直接不生效（第 7 讲故意撞给你看）
- `QMainWindow` 上 `setLayout()`：**根本不允许**，它的槽位被内部 `QMainWindowLayout` 占着（第 6 讲用 `layout()->metaObject()` 把占位者打印出来了）

### 4. 绘制层：几何定完，才轮到画

布局谈完尺寸，Qt 发 `LayoutRequest` → 控件拿到最终几何 → 才有 `paintEvent` 把像素落上去。所以「读了 geometry 就断言」必须等事件循环跑过一轮（第 3、7、8 讲的读数纪律）。想跳过肉眼验证，用 `grab()` 抓像素——那是同步渲染（第 9 讲靠这个一步出数）。真正的自绘（`QPainter`、路径、渐变、离屏图像）在 **`../painting/`** 模块正面讲；本模块只要求你知道「样式表最后也是走绘制这条路」，所以顺序上 08 必须排在 09 前面。

### 5. 样式层：QSS 是「覆盖绘制」，不是「设置属性」

```css
/* 示意：五种选择器的匹配范围差别（第 9 讲实测） */
QPushButton            { }   /* 这个类**或其子类** */
.QPushButton           { }   /* **正好**是这个类，子类不命中 */
QPushButton#okButton   { }   /* 按 objectName 点人 */
QPushButton[kind="primary"] { }  /* 按动态属性点人 */
QGroupBox > QPushButton{ }   /* 子；写成空格是「后代」，会连孙子一起命中 */
```

三条硬结论（都是本模块实测/核对过的）：

- **裸 QWidget / 自定义 QWidget 子类的 background、border 默认不画** → 要么 `setAttribute(Qt::WA_StyledBackground)`，要么自己 `paintEvent`
- 写了 Qt 不认识的 `border-style` 值：**不报错、也不生效**，安静得像没事发生
- **QSS 不会把颜色写回 QPalette**（第 9 讲打印 QPalette 与抓取像素的色值做对照，两者不一致）
- `QApplication::setStyleSheet` 在 Qt6 仍是**非 static 实例方法**，`QApplication::setStyleSheet(...)` 这种写法编不过；`QGuiApplication` 没有这两个函数——只连 QtGui 的工程里样式表不可用

### 6. i18n 层：整句进 tr，数字日期交给 QLocale

```cpp
// 逐字签名核对过。**别只翻 qobject.h**：那里的 tr（121 行）整段被
// `#if defined(QT_NO_TRANSLATION) || defined(Q_QDOC)` 包着，你正常构建时它根本不参与编译，
// 而且三个参数全是匿名（`const char *, const char *, int`），当签名抄等于抄了个残骸。
// 真身有两处，都带名字、都能编进去：
//   qtmetamacros.h 92~94 行 QT_TR_FUNCTIONS —— `static inline QString tr(const char *s, const char *c = nullptr, int n = -1)`，
//     函数体就一句 `return staticMetaObject.tr(s, c, n);`（Q_OBJECT 展开时把它塞进你的类）
//   qobjectdefs.h 246 行 QMetaObject::tr(const char *s, const char *c, int n = -1) const —— 干活的那个
tr("Open");                              // 上下文 = 当前类的类名
tr("Open", "菜单动作，不是开源");        // 第二个参数是**给译者的注释**，不是上下文
tr("1 file", "%n files", n);             // 复数：%n 由 tr 自己填
tr("%n file(s) found", "", n);           // 另一种常见复数写法
QCoreApplication::translate("FileMenu", "Open");  // 手工指定上下文（qcoreapplication.h 190~193 行）
```

```cpp
// 数字与日期：只在「给人看的那一刻」格式化（qlocale.h 1023/1037/1046/1083 行）
const QLocale loc = QLocale::system();
const QDate d(2026, 3, 1);
loc.toString(1234567);                       // 分组符归 locale 管
loc.toString(d, QLocale::LongFormat);
loc.toString(d, QLocale::ShortFormat);
d.toString(Qt::ISODate);                     // 存储/日志用这个，与 locale 无关
```

Linguist 工作流四步：`lupdate` 抽 .ts（XML，给人改）→ 译者填（含每个复数形态）→ `lrelease` 编 .qm（二进制，给程序读）→ 运行时 `QTranslator::load` + `QCoreApplication::installTranslator`。切语言的正规顺序是 `removeTranslator(旧的) → delete → 新建 → load → installTranslator(新的)`，然后各控件在 `QEvent::LanguageChange` 里把 `tr()` 结果重设一遍。

## 六、常见坑与注意事项

### 1. 对同一个 widget 调了两次 setLayout()

```cpp
auto *l1 = new QVBoxLayout; auto *l2 = new QHBoxLayout;
w.setLayout(l1);
w.setLayout(l2);   // ⚠ 控制台一条警告，第二个布局直接不生效，界面长得像「随机」
```

症状是「明明写了布局却像没生效」。`QWidget::layout()` 读回来**永远只有一个**布局对象，Qt 不会替你换。要改结构，就 `delete w.layout()`（或用 takeAt 摘干净）再设，或者干脆一开始就把子控件装进一个容器 widget、只换容器里的东西。第 7 讲故意撞了一次并把警告打给你看。

### 2. 把 stretch 和 alignment 当同一件事

`addWidget(w, 0, Qt::AlignRight)` 里 stretch 是 **0**、对齐是 **右**。想要「w 变宽」你改的是 stretch（再配 `Expanding` 政策，见坑 3）；想要「w 不变大、贴右边」你改的是 alignment。混了的表现就是经典两句抱怨：「加了 stretch 它还是不肯大」和「它变大了但我想要它贴边」。这两个旋钮一个管**分多少空间**，一个管**在自己那份里贴哪边**。

### 3. 只加 stretch，忘了 sizePolicy 不肯长大

布局愿意多给，控件不肯吃。`QLabel`/`QPushButton` 默认政策不是 `Expanding`，你给再多 stretch 它也只占 `sizeHint` 那么大，剩下空间变成**空白**。对策：`sp.setHorizontalPolicy(QSizePolicy::Expanding)` 再交给布局分配；或者用 `setMinimumWidth/setFixedSize` 把结果钉死（硬墙优先级高于 sizeHint）。第 8 讲把四个位标志（GrowFlag=1 / ExpandFlag=2 / ShrinkFlag=4 / IgnoreFlag=8）和每个 Policy 的整数值都打印出来了。

### 4. QSS 背景不生效：裸 QWidget 漏了 WA_StyledBackground

```cpp
plain->setStyleSheet("background:#2b2b2b;");            // ⚠ 十有八九不画
boxed->setAttribute(Qt::WA_StyledBackground, true);     // ✔ 让 QWidget 走样式表背景绘制
boxed->setStyleSheet("background:#2b2b2b;");
```

症状：给自定义 QWidget 子类写 `background`、`border` 一点反应没有，给 QPushButton 写却好用。原因：**QPushButton 这类有原生 `paintEvent` 的控件自己会画背景，裸 QWidget 不会**。第 9 讲排了四格对照（裸/已设属性 × 自定义子类/子类自己覆写 paintEvent），每格打印红色像素数与蓝色像素数——别背结论，跑出来看。

### 5. 用便利类硬撑大数据量

```cpp
for (int i = 0; i < 200000; ++i)
    listWidget->addItem(QString::number(i));   // ⚠ 每个 item 都是真对象，还要 role 存取
```

`QListWidget/QTreeWidget/QTableWidget` 内部确实就是一张 model（第 4 讲用 `model()` 实测给你看了），但每个条目都是一个堆上的 `QxxxItem`，列数一多、行数一大就吃内存吃 CPU；再叠 `setItemWidget`（每格塞一个真控件），滚动立刻像踩了刹车。**便利类的 private `setModel` 就是「到此为止」的官方路障**：数据在库里 / 要懒加载 / 要自定义 role / 万行以上，转 model/view（`../model_view/`）。

### 6. 在槽里套 exec()，造成事件循环重入

```cpp
void Editor::onTextChanged() {
    if (QMessageBox::question(...) == QMessageBox::Yes)  // ⚠ 静态方法 + exec：嵌套事件循环
        doSomething();
}
```

`exec()` 卡住的是「你这段代码」，但**事件循环还在转**：用户依然能点你的菜单、按删除、切标签页，于是别的槽在「你以为不可能有事件」的时刻被派发——这就是重入。表现是偶发的崩溃、状态错乱、以及 CI 里永远超时的用例。对策：**主路径不用 `exec()` 与任何静态对话框方法**；要模态用 `open()`（模态但不嵌套循环）或 `show()` + 信号；示例里全部用 `QTimer::singleShot` 自动 `accept()/reject()/click()`。第 5 讲把这条写成了铁律。

### 7. i18n 里拼接句子

```cpp
// ⚠ 错：语序被写死在代码里，lupdate 抽不到，任何语言都会说错话
label->setText(tr("Found") + " " + QString::number(n) + " " + tr("files"));
// ✔ 对：整句进 tr，数量交给 %n
label->setText(tr("%n file(s) found", "", n));
```

三种语言三种语序，你把「主语/数字/宾语」拆成三段，就等于规定全世界的语言都按英语语序排。同理：**别把中文裸写在代码里**（`QString s = 「打开文件」;` 抽不到，永远翻不了），也别把 `tr()` 的结果存进 `static`/成员变量当缓存（切语言后它就烂在库里，要靠 `QEvent::LanguageChange` 重设）。

### 8. 把日期硬编码成字符串

```cpp
const QString stamp = "2026-3-1";        // ⚚ 这是「一个字符串」，不是一个日子
const QDate d(2026, 3, 1);               // ✔ 存 QDate，展示时才按 locale 格式化
```

同一串 `01/03/2026`，`QLocale("en_US").toDate(...)` 和 `QLocale("de_DE").toDate(...)` 解出来是**两个不同的日子**（第 10 讲把这个差别打印出来了）。存储与日志一律 `Qt::ISODate`；给人看的时候再走 `QLocale::toString(date, LongFormat/ShortFormat/NarrowFormat)`。数字同理：要可解析的稳定文本用 `QLocale::c()` 或 `QString::number`，别用系统 locale 生成分组符。

### 9. 在事件循环跑起来之前读 geometry

`show()` 只是「挂号」（起步阶段第 1 讲），布局的 `LayoutRequest` 要等事件循环派发。所以在 `main` 里 `show()` 后面立刻读 `size()/geometry()`，拿到的是**谈判前的草稿**，你的断言十有八九假失败。本模块所有几何打印都排在 `QTimer::singleShot` 里，就是这个理由。急着要像素证据请用 `grab()`（同步渲染）。

## 七、老师傅的经验之谈

### 1. 「控件、布局、尺寸」是三张不同的账单

新手把它们揉成一团，于是出了 bug 不知道找谁。你以后遇到界面问题，先分层：**是控件没发信号？是布局没分到空间？还是尺寸谈判根本没商量？** 三层的证据分别是：信号计数器（第 1、2 讲）、`geometry()` 表（第 7、8 讲）、`sizeHint()/minimumSizeHint()/sizePolicy()` 三件套（第 8 讲）。分层能力，就是 GUI 排错能力。

### 2. 一切证据用打印，别用肉眼

本模块每个示例都能在 `-platform offscreen` 下跑出结论：几何、像素计数、Policy 的整数位值、翻译查表结果、`load()` 的布尔值。肉眼看一次是运气，打印出来是**可复现的事实**。你以后写任何界面，先想「我怎么证明它成了」，再想怎么写——这个习惯比会多少类值钱。

### 3. 样式表是糖，也是雷

QSS 的爽在于一行代码换全套皮；雷在于它**静默失败**（不认识的属性、漏了 `WA_StyledBackground`、`:/` 资源路径写错、`sub-control` 名字打错）。师傅的规矩：改样式表之前先确认三件事——作用域（qApp 还是控件）、级联方向（谁的优先级压过谁）、`styleSheet()` 读回来是不是你写的那串。第一条排错动作永远是读回来。

### 4. 国际化第一天就做，别等「以后再加」

以后再加的代价是把每一句裸字符串从业务逻辑里往外刨。起步阶段的正确姿势：所有给用户看的文本一律 `tr("整句")`，所有数字日期走 `QLocale` 在展示那一刻格式化，所有排版别硬写左右（用布局的方向无关接口和 stretch/alignment）。做到这三条，将来加一门语言只是多一个 `.qm`。

### 5. 非阻塞不是洁癖，是可测试性的生死线

这套资料所有示例都要能在 ctest 里无人值守跑完。一个 `exec()` 就能把整条流水线钉死在原地。你把「不阻塞」当成默认写法，顺带就获得了：界面不假死、状态机不打结、CI 能测 GUI。这是习惯，不是知识——从第 5 讲开始练。

### 6. 诚实对待「核不核得到」

这一模块最能说明「核得到」和「跑得出」是两件事。第 10 讲原来写着三句「本机编不动 Qt，所以这三样我没核、不写进代码」：`QCoreApplication::removeInstalledTranslator`、`QWidget::changeEvent`、`QWidget::setLayoutDirection`。现在三句都各自有了结果，而且**结果不一样**：

- `removeInstalledTranslator` —— 今天再 grep 一遍 `qcoreapplication.h`，**确实没有**，只有 `installTranslator`（186 行）/ `removeTranslator`（187 行）。原来那句「不采信网上同名说法」站住了，保持不用。
- `QWidget::changeEvent` —— `qwidget.h` 701 行 `virtual void changeEvent(QEvent *);`，**在**。所以它从「注释里提一句」升级成了代码：第 10 讲新增 `GaugePanel` 覆写它，把「切语言后界面会不会自己变」跑成看得见的两行数。
- `QWidget::setLayoutDirection` —— `qwidget.h` 414/415/416 行（外加 171 行的 `Q_PROPERTY` 和 422/423 的两个 inline 判读），**在**，签名已抄在讲义里；但本讲代码仍然不用它——写进去就是 RTL 布局一整讲，会淹掉主线。「核到了」不等于「该展开」。

顺带一条 Qt5 肌肉记忆的坑，是这轮才撞出来的：`QEvent::LanguageChange` 在 Qt6 位于 **`qcoreevent.h` 128 行**（`= 89`），`qnamespace.h` 里 grep 它**零命中**。按老习惯去 `qnamespace.h` 找 QEvent 枚举，你会以为它被删了。

规矩没变：**核不到的东西不写进代码**，写进去的每行都要有文件与行号。变的是——编得动之后，「没核」就不该再拿「编不动」当借口了。

> 控件与布局就到这儿。这 10 个文件让你能从「放了几个控件」升级到「搭出一套站得住的界面」。下一站去看事件怎么派发与被截走（[图解 · 事件过滤器](../visuals/event_filter.html)），以及数据量大起来之后该怎么摆（`../model_view/`）。加油，徒弟！
