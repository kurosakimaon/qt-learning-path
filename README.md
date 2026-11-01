# Qt 从入门到资深 · Qt Learning Path

> 一套面向**零基础到资深**的 Qt 6 系统化学习资料库。
> 从第一个窗口、事件循环、信号槽，到模型/视图、多线程、网络、构建部署，一条路走到一个能交付的桌面程序。

![Qt](https://img.shields.io/badge/Qt-6.12-41CD52?logo=qt)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus)
![Build](https://img.shields.io/badge/build-CMake%20%7C%20MinGW%20Makefiles-orange)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)
![License](https://img.shields.io/badge/license-MIT-green)

![Qt 工程师修炼路线图](_assets/preview/roadmap.png)

**十一阶段 · 73 个可编译示例 · 16 张分步动画 · 5 份独立 CMake 工程 · 1 个可交付的实战项目**

[总览路线图](学习路线图.html) · [图解专区](visuals/index.html) · [实战项目](projects/todo_desktop/README.md) · [校验脚本](_tools/check_syntax.sh)

---

## 📖 这是什么

这是一条**按顺序走**的 Qt 学习路线，不是零散代码片段的堆砌：

- **十一个阶段**，从「窗口怎么弹出来」到「拷到别人电脑上怎么跑起来」，编号即推荐顺序
- **73 个编号示例**，每个都是**完整可编译**的独立程序（不是伪代码），多数以「取证」为目标：把进入次数、到达顺序、真实耗时打印出来当结论
- **每个阶段两套材料**：`index.html`（卡片式目录，浏览器打开，知道这一阶段学什么、先看哪一篇）+ `tutorial.md`（学习指南，讲**为什么学、坑在哪、练什么**）
- **16 张分步动画图解**：把事件循环、moc、连接类型、布局协商、绘制管线、坐标变换、模型/视图、委托生命周期、线程归属、资源打包、异步网络、质量门、部署树这些「看不见的机制」拆成可翻页的动画，支持自动播放
- **5 份独立 CMake 工程** + 一篇《Qt CMake 实操手册》：构建、资源、测试、翻译、部署各一个最小可运行样例
- **1 个实战项目** `projects/todo_desktop`：把前面十一个阶段拧成一个能用的桌面程序

> 师傅领进门，修行在个人。界面是点出来的，崩溃是查出来的。

---

## 🗺️ 课程地图

| 阶段 | 目录 | 内容 | 示例 | 难度 |
|------|------|------|:----:|:----:|
| 壹 | `start/` | 起步：`main()` → 窗口 → `exec()`、工程结构、日志、无头运行 | 6 | 入门 |
| 贰 | `widgets/` | 控件与布局：常用控件、容器、对话框、`QMainWindow`、尺寸策略、QSS、国际化 | 10 | 进阶 |
| 叁 | `events/` | 事件系统：事件循环、三层派发、过滤器、定时器、输入与焦点、自定义事件、拖放 | 7 | 进阶 |
| 肆 | `metaobject/` | 元对象系统：信号槽、连接类型、moc 解剖、属性、对象树、内省 | 8 | 进阶 |
| 伍 | `painting/` | 绘图：`QPainter`、坐标变换、渐变、字体度量、自绘控件、`QGraphicsView` | 8 | 进阶 |
| 陆 | `model_view/` | 模型/视图：手写模型、编辑回路、委托、代理模型、选择模型、视图调优 | 8 | 中高级 |
| 柒 | `threads/` | 并发与线程：亲和性、跨线程信号、同步、`QtConcurrent`、线程池、收尾、IPC | 8 | 中高级 |
| 捌 | `storage/` | 数据与持久化：`QSettings`、文件 IO、JSON、qrc 资源、SQLite、SQL 模型 | 6 | 中高级 |
| 玖 | `network/` | 网络：`QNetworkAccessManager`、REST、TCP、下载与进度、DNS 与 TLS | 5 | 中高级 |
| 拾 | `quality/` | 工程质量：QtTest、分层架构、所有权审计、错误处理、性能取证、大视图优化 | 7 | 高级 |
| 拾壹 | `build_deploy/` | 构建与部署：CMake 骨架、库分层、资源、CTest、翻译、`windeployqt` | 5 工程 | 高级 |
| 🏁 | `projects/todo_desktop/` | 综合实战：桌面待办管理器 | — | 资深 |

学习顺序**就是编号顺序**，但有两句实话要先说：

- **第肆阶段（元对象）排在控件与事件之后**，是故意的。你在第贰阶段就会写 `connect()`，那时候不懂它是怎么转的没关系；第肆阶段会把 moc、连接类型、对象树一次拆到底。
- **第陆阶段（模型/视图）千万别跳**。它不改变「界面长什么样」，只改变「数据住在哪儿」——而后者才是「会摆界面」和「会做能用的数据界面」的分界。

---

## 📂 目录结构

```
.
├── 学习路线图.html            # ⭐ 总入口，浏览器打开这个（含阶段检验表与构建指引）
├── README.md                  # 你正在读的文件
├── CMakeLists.txt             # 顶层构建：73 个示例 = 73 个 ctest 用例
├── LICENSE
├── .gitignore
│
├── _assets/                   # 共用样式（module.css：所有阶段页统一外观）
├── _tools/                    # 15 个自检脚本：check_all.sh（一道命令跑完八道门）、
│                              #   check_syntax.sh（moc+语法门，不需要 Qt 运行库）、
│                              #   页面渲染 / SVG 文字越界 / 控制台字形 / 字形门自测 /
│                              #   JS 语法 / 图解步骤一致 / 引用行号 共八道门 + run_all.sh；
│                              #   audit_census.pl / audit_api_names.pl 是**辅助队列**
│                              #   （故意不进 check_all，见下）
│                              #   （每个门管什么、本机跑出什么数，见下面「怎么验证」第四节）
├── visuals/                   # 图解专区：16 张分步动画 + 首页 hub + 共用 visual.css/visual.js
│
├── start/                     # 第壹阶段 · 起步            （6 例 + index.html + tutorial.md）
├── widgets/                   # 第贰阶段 · 控件与布局      （10 例）
├── events/                    # 第叁阶段 · 事件系统        （7 例）
├── metaobject/                # 第肆阶段 · 元对象系统      （8 例）
├── painting/                  # 第伍阶段 · 绘图与自绘控件  （8 例）
├── model_view/                # 第陆阶段 · 模型/视图与委托 （8 例）
├── threads/                   # 第柒阶段 · 并发与线程      （8 例）
├── storage/                   # 第捌阶段 · 数据与持久化    （6 例）
├── network/                   # 第玖阶段 · 网络            （5 例）
├── quality/                   # 第拾阶段 · 工程质量        （7 例）
├── build_deploy/              # 第拾壹阶段 · 构建与部署    （5 份独立工程 + CMake 手册）
│
└── projects/
    └── todo_desktop/          # 综合实战 · 桌面待办管理器
```

每个阶段目录里都有 `index.html`（卡片目录）与 `tutorial.md`（学习指南）；`build_deploy/` 下的五个子目录各自带 `CMakeLists.txt`，要**单独 configure**，不能由顶层一次性构建。

---

## 🚦 从哪开始

```bash
git clone https://github.com/kurosakimaon/qt-learning-path.git
cd qt-learning-path
```

然后用浏览器打开 `学习路线图.html`（本机双击即可，不需要起服务器）。

1. **浏览器打开 [`学习路线图.html`](学习路线图.html)。** 里面有四段式地图、十一个阶段卡片、每阶段的达标检验表，以及下面的构建命令。所有页面都能离线打开，不依赖网络。
2. **进第壹阶段 [`start/index.html`](start/index.html)。** 先读卡片目录，再按编号读 `01_hello_window.cpp`。每篇顶部注释块都写了「本讲解决什么问题、构建目标叫什么」。
3. **动手改。** 学 Qt 最有效的一步是：把示例改坏一处，看它怎么坏；再改回来。这份资料的注释通常直接告诉你「改这里会看到什么」。

**前置要求**：会一点 C++（类、指针、模板能读懂即可），装好 Qt 6.x（≥ 6.5，资料按 6.12 写）+ CMake ≥ 3.21 + 任一 C++17 编译器。没装 Qt 也能先读——语法门的用法见下面「怎么验证」。

**适合谁**：从「没写过 GUI」到「写过几年 Qt 但说不清 moc 生成什么、事件循环怎么转、为什么跨线程连接会排队」的人。不必从第壹阶段一路读到第拾壹阶段，但**编号顺序是有理由的**，跳读前先看一眼路线图里的四段递进说明。

---

## ✅ 怎么验证

### 零、一道命令跑完所有源码门

```bash
bash _tools/check_all.sh
```

它按顺序跑下面第四节的八道门，一行一条判词，**任何一道真红就退出 1**。两个刻意的取舍：它**不**跑 `run_all.sh`（那会重写 `runlogs/`，校验命令不该有副作用），也**不**跑 `ctest`（那要 `build/` 已配好，塞进来只会让没装 Qt 的人一打开就一片红，那红是噪声）。找不到 `moc` 或浏览器时，那几道门记 **SKIP** 并照样让退出码非 0——「没跑」和「跑过且过了」是两件事，脚本不许把前者说成后者。

### 一、不装 Qt 也能做的校验（语法门）

```bash
bash _tools/check_syntax.sh            # 全部阶段
bash _tools/check_syntax.sh events     # 只查一阶段
```

这个脚本对每个 `.cpp` 做三件事：需要时跑 `moc`、用 `g++ -fsyntax-only -std=c++17 -Wall -Wextra` 编译（**0 error / 0 warning 才算过**）、检查编号文件有没有入口（`main` 或 `QTEST_*_MAIN`）。它**不链接、不运行**，所以能在一台没有可用 Qt 运行库的机器上判断「代码编不编得过」。

### 二、有 Qt 的机器上：编 + 跑

```bash
cmake -G "MinGW Makefiles" -B build -S . -DCMAKE_PREFIX_PATH=D:/Qt/6.12.0/mingw_64
cmake --build build --target start_01_hello_window
ctest --test-dir build -R start_01 --output-on-failure

cmake --build build --target all_demos            # 73 个示例全编
ctest --test-dir build --output-on-failure        # 全部无头跑一遍
```

顶层 `CMakeLists.txt` 把每个示例都注册成 CTest 用例，强制 `QT_QPA_PLATFORM=offscreen`、120 秒超时，所以**不需要显示器、不需要鼠标键盘**。示例自己会在末尾 `quit()`，不会挂着等你点关闭。

常用目标：`all_demos`（全部）、`all_<阶段>`（如 `all_events`）、`<阶段>_<编号>_<名字>`（单个，如 `events_02_event_dispatch`）。

**每个示例的文件头都写了「实测口径」那一段**——73 个阶段示例（74 个 .cpp，唯一的例外 `start/startwidget.cpp` 不是演示程序，它是 `start/02` 的实现文件）一条不少，写的是「本机真的编译、链接、跑过」还是「没跑过」，以及跑出来的那行硬账数字是什么。`bash _tools/run_all.sh` 最近一轮是 `pass=75 fail=0 skip=0`（73 个阶段示例 + `todo_desktop` + `tst_taskmodel`），日志逐份存在 `runlogs/`。这一句值得看，是因为它把「示例能不能跑」和「示例里印出的数字对不对」**分成了两件事**：`run_all.sh` 过了只说明没崩、走完了；带具体数字的预期，各讲自己决定要不要立硬账，立了的才有 `对上 N，不符 0` 那行。

### 三、`build_deploy/` 的五个工程单独构建

```bash
cd build_deploy/qt_cmake_01_widgets
cmake -G "MinGW Makefiles" -B build -S . && cmake --build build
./build/qt_cmake_01_widgets
```

### 四、浏览器页面自检（已实测）

页面部分不依赖 Qt，但有七道自己的门。下面这一列是**本机刚跑出来的原文输出**，不是我转述的：每条命令都可以原样敲，`ALL CLEAN` / 数字对不上就是 bug。表里 `audit_census.pl` 和 `audit_api_names.pl` 这两行（以及后者的 `--selftest`）是**辅助队列**，它们故意不算门、也不进 `check_all.sh`，读数规则见下面几段。

| 命令 | 本机实测输出 | 这道门管什么 |
| --- | --- | --- |
| `perl _tools/check_html_js.pl` | `扫描 32 个文件（18 段脚本）` → `ALL CLEAN` | HTML 里的 `<script>` 语法（括号闭合、字符串未终止等），不启浏览器 |
| `perl _tools/check_visual_steps.pl` | `共 16 个页面、103 个 step` → `ALL CLEAN` | 每页 `STEPS` 数组与导航按钮数一致、`code` 下标不越界 |
| `bash _tools/check_pages_render.sh` | `共 30 个页面、103 个图解步骤` → `ALL CLEAN` | 无头浏览器真的把每页逐步打开：计数器、标题、SVG、控制台报错 |
| `bash _tools/check_svg_overflow.sh` | `共 16 页、103 步：出界 0 处，贴边 0 处，异常 0 条` → `ALL CLEAN` | 每行文字量 `getBBox()`，四边都不许捅出包着它的 `<rect>` |
| `perl _tools/check_console_glyphs.pl` | `…（共扫 75 个文件）` + `另有 2 处码点转义是 glyph-ok 放行的` | 打印到 Windows 控制台（GBK）的字符会不会变成 `?` |
| `perl _tools/fix_console_glyphs.pl --selftest` | `自检 9 项全部通过（含「不带 LEAVE_SRC 会把源串清空」这条反向断言）` | 上一个工具自己的判卷逻辑有没有退化 |
| `perl _tools/audit_citations.pl` | `扫描 138 个文件｜解析到引用 1632 条｜文件缺失 0 条｜行号越界 0 条` + `内容核对 445 条｜符号在附近找不到 15 条（WARN，不算失败）` | 注释里写的「某头文件第 N 行」落不落在那文件的真实行数内，且附近能不能找到那个符号 |
| `perl _tools/audit_census.pl` | `片段 1429｜MISS 44｜OOB 7｜无锚点 230｜文件名无法解析 0` | **辅助队列，不是门**：专查引用门看不见的「清单行写法」（表头立文件名、下面只写裸数字） |
| `perl _tools/audit_api_names.pl` | `token 30033 次｜命中真名/白名单 29946｜前缀式简写放过 25｜name-ok 放行 62｜查无此名 0 种` | **辅助队列，不是门**：散文里那些「长得像 Qt 的名字」到底在不在 Qt 源码树里 |
| `perl _tools/audit_api_names.pl --selftest` | `--selftest：夹具 token 7｜查无此名 3 种｜放行未点名 1｜全部通过` | 上一个工具的判卷逻辑自己有没有退化 |

为什么单独立一道文字越界的门：图解的坐标全是手写的，**改一个字就可能让一行字压到框外**，而这事 `check_syntax.sh` 和 `ctest` 都看不见。判据分两级——**出界**（捅框，红，计入退出码）和**贴边**（离框不到 3px，只提示，不判失败）。

引用门（`audit_citations.pl`）也分两级：**行号越界算失败**，「附近找不到那个符号」只算 WARN——因为讲义里常一句话并列两个引用，符号是从哪一条来的判不准，硬判红会淹掉真信号。这一版把它的覆盖面从 638 条扩到 1621 条（后来又随着讲义补写到 **1632 条**）：原来只吃「`第 N 行`」这种带「第…行」的写法，而讲义排版爱把「行」字省掉（`（qnamespace.h 1926~1980）`），于是一整批引用从来没被卷过。放宽之后当场捞出两条真错：`qresource.h` 全文是 **70** 行不是 71（`wc -l`/`grep -c ''`/`awk NR` 三个口径一致；把结尾换行后的空位当成一行就数出 71），以及 `qhostaddress.h` 的 `isNull`/`isLoopback`/`protocol` 在 **123/129/107** 行、原稿写的 44/50/60 全错，连 `enum SpecialAddress` 的成员顺序也记反了（本机 `AnyIPv6` 排在 `AnyIPv4` 前面）。放宽的代价是假阳，收假阳收掉 27→16：不许把 `isValidUtf8` 里的 8、`i18n` 里的 18、`Qt 6.12.0` 里的 6 读成行号，也不许拿**下一条**引用的符号去查**这一条**的行。

**引用门有一块盲区，`_tools/audit_census.pl` 专门补它**（本轮勘误之后加上的）。讲义里还有一大类写法是「表头立文件名 + 下面每行只写裸数字」：

```
// QtTest/qtestkeyboard.h
//   第 274/280 行 keyClick(QWidget*, char) / keyClick(QWidget*, Qt::Key)
//   第 284 行 keySequence(QWidget*, const QKeySequence &)
```

这些号**不带文件名**，`audit_citations.pl` 从头到尾看不见它们。这一轮就是被这块盲区咬的：把 15 个文件里的清单行逐条人肉核对之后，又找出一批行号错——`qfutureinterface.h` 的 State/report*/setProgress* 三组全偏、`qsvgrenderer.h` 把 `isValid` 记到了构造函数那一组、`qtestmouse.h` 的 `pos.isNull()` 差 6 行、`qtestcase.h` 的宏表连号偏移、`qloggingcategory.h`、`qlayout.h` 把 `indexOf`/`replaceWidget` 记反、`qmenu.h` 干脆没有文中写的 `insertAction`、`qboxlayout.h`、`qfileinfo.h` 的 `size()` 落进了时间函数那一组等。全仓现在共 **22 个文件、37 条「★ 勘误」**（`grep -rn --include=*.cpp --include=*.h --include=*.md --include=*.html '★ 勘误'`，不含 `build/`），每一条都带本机重数的号。

它**故意不进 `check_all.sh`**，理由写在脚本头部：它是启发式的，红了通常不是讲义错。它的用法是**人工队列生成器**——跑一次，把 `MISS`/`OOB` 逐条读一遍，真错改掉，假阳留着当噪声基线。四类已知假阳（散文引用抢走块锚点、`enum`/`explicit` 这类关键词被当成符号、`index()` 大小写敏感、以及「★ 勘误」条目自己复述了旧错号）逐条列在头部注释里，别指望它们归零。

两道反「假绿」保险是踩完之后加的，别拆：
- 不带参数**不再**输出「片段 0」装作全绿——现在按引用门同一套目录规则全仓扫，一个文件都没读到就 `die`（退出码 2）。这条是用夹具验过的。
- 「算了但没核对」必须显式计数：`无锚点 230`、`文件名无法解析 0` 单列在汇总里。**核对不到 ≠ 通过**，所以命中率的分母是 `1429 − 230 = 1199`，不是 1429。片段数会随讲义增删漂（本轮从 1424 漂到 1429 是补写的「实测口径」声明和 README 新增段落里带了 `36/36` 这种裸数字），漂了不算红——要看的是 MISS/OOB 有没有多出没见过的条目。
- 源码树默认两棵（qtbase + qtsvg），因为 `QtSvg/qsvgrenderer.h` 不在 qtbase 里；只搜 qtbase 时它会被推到「无法解析」那一栏，而**就是这一栏第一次把 `isValid` 记成 37 行这条真错顶出来的**。

**还有第三块盲区：散文里的 API 名字**。引用门和清单门都只吃「文件名 + 数字」这一类断言，而 Qt5 那个 `QRegExpValidator`、老代码里常说的 `QSettingsEvent`，这种**写得像真名**的说法一个数字都不带，两把门全程看不见——本机 6.12 两个都查无此名，这里举它们就是想说清这类错长什么样。   name-ok:QRegExpValidator, QSettingsEvent 本机 6.12 查无此名，此处有意举例
这类错比行号更坏：行号错了读者翻一页能发现，**名字错了他去 grep，grep 不到，只会以为是自己找错了地方**。`_tools/audit_api_names.pl` 补的就是这块——它把 Qt 源码树（本机 6.12.0 的 `qtbase/src` + `qtsvg/src` + 安装树 `lib/cmake`）的全部标识符抓成集合（307845 个，建索引 16~17 秒），再把全仓每个 `Q*` 形状的 token 拿去对。跑出来的四类放过：真名命中、真名的**前缀式简写**（讲义爱写「三种渐变 QLinear/QRadial/QConical」）、本仓代码里自己定义的名字（**先剥注释和字符串字面量再抓**——本仓的注释就是讲义，连注释一起吃进来等于臆造名自己给自己作证），以及那一行末尾写了 `name-ok:TOKEN 理由`（和字形门的 `glyph-ok:U+XXXX` 同一套约定，**必须点名到具体 token**：只点名一个就只放行那一个；光写一个不带名字的标记不认作放行，那一行每个没被点名的名字都会单独列成一条「放行未点名」）。
本机这一轮的基线是 `token 30033｜命中 29946｜前缀放过 25｜name-ok 点名放行 62｜查无此名 0 种`。清到 0 **不等于讲义里没有错名**，只等于「每一条查无此名都被人工点名交代过一次」；它和 `audit_census.pl` 一样**故意不进 `check_all.sh`**，用法是跑一次、把冒出来的名字逐条读完。判卷逻辑自己由 `--selftest` 的 7 条夹具断言守着，其中一条是反向断言：同一行两个臆造名只点名放行一个，另一个必须还能报出来。

- 交互式：[`_tools/svg_overflow_check.js`](_tools/svg_overflow_check.js) 贴进浏览器 Console 就能跑单页；
- 批量：`bash _tools/check_svg_overflow.sh [page …]` 无头一次跑完 16 页（约 4 秒）。
- ⚠ 这个工具先前有个 off-by-one：步序哈希是 **1 基**的（`visuals/visual.js:147` 写 `#step-(当前+1)`），而旧版按 0 基跳步，于是**每页第一步被量两遍、最后一步从来没被量过**。修掉之后又补了左边和上边（旧版只判右下），当场就多报出 2 处左侧出界。现在这四边都是硬账。
- 卡片数、`<title>` 前缀（「第壹阶段」…「第拾壹阶段」）、`.header` / `.intro-section` 是否齐全，由 `check_pages_render.sh` 顺带核对：11 个阶段目录页、`visuals/index.html` 的 16 张图卡片、`学习路线图.html`。

也就是说：**页面能打开、每一步都渲染得出来、没有脚本报错、文字不出框**，这四条是实测过的；Qt 程序的运行结果同样实测过，证据在 [`runlogs/`](runlogs/)（见下面「资料边界」）。

---

## 🎬 图解专区

[`visuals/index.html`](visuals/index.html) 是 16 张动画的总入口。每页左边一张 SVG、右边讲解与代码，底下是步骤条：支持自动播放、点击跳步、键盘 `←` `→` `Home` `End`。

![事件循环图解：左侧 SVG 画布，右侧逐步讲解与对应代码，底部是步骤条](_assets/preview/animation.png)

图负责讲清「东西在哪儿、按什么顺序动」，代码负责对回讲义里的具体文件——两者一起看，比单读任何一边都稳。

![图解专区首页](_assets/preview/visuals.png)

---

## 🏁 实战项目

[`projects/todo_desktop/`](projects/todo_desktop/README.md) 是整条路线的收口：一个桌面待办管理器。它不追求功能多，追求**每一处都能指出用的是哪个阶段的知识**：模型/视图存任务、SQLite 落盘、JSON 导入导出、后台线程批量操作、自绘优先级标记、单元测试与部署。

---

## ⚠️ 资料边界（请先读）

这份资料写于 **Qt 6.12.0 / Windows / MinGW-w64 GCC 13.1.0（Qt 官方安装器自带：`D:\Qt\Tools\mingw1310_64`）/ CMake 3.30.5 / C++17**，并且**已经在这一台机器上完整地编译、链接、运行过**——不是「照着文档推的」，是跑出来的：

- 73 个编号示例全部 `moc` + 编译 **0 error / 0 warning**，全部**链接成功并无头跑完**
- 顶层 CMake 把每个示例都注册成 CTest 用例，本机 `ctest --test-dir build` 那一轮是 **75/75 全过、总耗时 101.11 秒**（含 `projects/todo_desktop` 的 `tst_taskmodel`）
- `projects/todo_desktop`（13 头 / 12 源 + 单元测试）与 `build_deploy/` 的 5 个工程都**真实 configure / build / run 过**，产物在各自 `build/` 下
- 每个示例自己打的证据行都存成 [`runlogs/<target>.log`](runlogs/)（UTF-8，共 75 份，跟着仓库走；同目录的 `*.raw` 是转换前的 GBK 原字节，不入库）。**`runlogs/summary.txt` 只记失败与超时，全过那一轮它就是空的**——看到空文件别当成「没跑」
- 讲义里凡是写「本机实测」的地方，都能在那堆日志里对上号；仍然只写「预期」的，是**行为会随 Qt 小版本 / 驱动 / 字体变**的那一类（定时器精度、字体度量、`QSqlQuery::execBatch` 的驱动支持）。这类差异**以你打印出来的为准**
- 每个类、函数、枚举值、重载都对着本机 `D:\Qt\6.12.0\mingw_64\include`（声明）与 `D:\Qt\6.12.0\Src\qtbase\src`（实现）逐字核对过，注释里标着头文件名与行号；**核对不上的一律不写**

还有一条边界值得单独说清楚：**注释里的行号只对得住本机这一份 6.12.0**。换个小版本 `qobject.cpp` 就会整体漂移，所以行号的用途是让你「当场证实或当场推翻」，不是权威引用——按行号找不到那行时，改用函数名搜，别怀疑自己。

`runlogs/` 同理：它是**那一轮的快照**，不是标准答案。你在自己机器上跑出不一样，先按上面「反馈与勘误」的办法核对差异来源（版本、字体、磁盘、网络），而不是先怀疑示例写错。

---

## 🔍 遇到问题先看这几处

| 症状 | 先看 |
|------|------|
| `找不到 Qt6` / configure 就失败 | 顶层 `CMakeLists.txt` 里的提示段；`-DCMAKE_PREFIX_PATH` 是否指向 Qt 安装目录 |
| 链接期 `vtable for XXX 未定义` | 该文件是否 `#include "本文件名.moc"`，`CMAKE_AUTOMOC` 是否打开（第肆、第拾壹阶段都有专门讲解） |
| 程序起来就退出 / `no Qt platform plugin` | 平台插件目录；`QT_QPA_PLATFORM=offscreen` 只用于自测，不是交付配置 |
| 界面卡住、定时器不响 | 事件循环是否被槽里的大循环占住（第叁阶段第 1 讲有实证） |
| 表格改完数据不刷新 | 是否 `emit dataChanged`，`beginInsertRows` / `endInsertRows` 顺序对不对（第陆阶段第 3 讲） |
| 「我这儿能跑，他那不行」 | 部署依赖是否收齐（第拾壹阶段 + `deployment_tree.html`） |

---

## 🙋 反馈与勘误

这份资料的价值在「每个数字都能被验证」。如果你跑出和注释里「预期」不一致的结果、发现某个 API 记错了、页面渲染有问题，直接开 issue 即可——你的实测输出就是最好的补丁。特别注意这类情况：不同 Qt 版本/编译器/驱动下行为本身的差异（比如 `QSqlQuery::execBatch` 各家驱动的支持、定时器精度、字体度量），这类差异值得记进讲义而不是当 bug 修。

---

## 📄 许可

MIT，见 [LICENSE](LICENSE)。
