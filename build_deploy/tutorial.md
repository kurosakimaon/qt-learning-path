# 第拾壹阶段 · 构建与部署（Qt 从入门到资深）

> 老师傅带你走完最后一公里：从六行 CMake 骨架，到一个「拷过去就能跑」的目录

## 一、模块简介

前面十个阶段都在讲「代码怎么写」。这一阶段讲**「代码怎么变成能交给别人的东西」**。

为什么单列一阶段？因为工程里最常见的一类失败根本不在 C++ 里：

- `Could not find a package configuration file provided by "Qt6"` —— 不是 Qt 没装，是 CMake 不知道它在哪；
- `undefined reference to vtable for Xxx` —— 有 `Q_OBJECT`，但 moc 没跑；
- 资源路径 `:/icons/save.png` 打不开 —— qrc 没进目标；
- 自己电脑上好好的，拷给别人报 `0xC0000135` —— DLL 没带、插件目录缺一层。

这些问题的共同点是：**构建期和运行期被混为一谈**。这一阶段先把这条界线划清，再用五个独立工程把每一块走一遍。

本阶段有 **5 个独立工程**，每个都有各自的 `CMakeLists.txt` 与 `project()`，**必须进各自目录单独 configure**——仓库顶层只检查它们存在，不会 `add_subdirectory` 进来。

> 环境事实：Qt 6.12.0、Windows、MinGW-w64（MSYS2 ucrt64，gcc 15.2/16.2）、CMake ≥ 3.21 + Ninja、C++17。
>
> 诚实标注：**本机当前无法编译链接 Qt 程序**，五份工程**未在本机实际 configure / build / run**。其中的 C++ 源码都过了 `moc` + `g++ -fsyntax-only -std=c++17 -Wall -Wextra`（零 error、零 warning）；CMake 用到的每个函数与参数都对着本机 `C:\msys64\ucrt64\lib\cmake\Qt6*` 的真实实现核对过。核对到的一条关键事实：**`qt_standard_project_setup()` 只打开 AUTOMOC 与 AUTOUIC，不打开 AUTORCC**。

## 二、学习目标

学完本模块，你要能做到：

1. 默写 Qt Widgets 工程的六行骨架，并说出每一行的作用
2. 解释 `find_package(Qt6 COMPONENTS Widgets)` 引入的 `Qt6::Widgets` 到底带进来哪三样东西（include 路径、编译定义、链接库）
3. 说清 `PUBLIC` / `PRIVATE` / `INTERFACE` 分别把属性传给谁，并解释「库的 PUBLIC 里出现 Widgets」为什么是架构问题
4. 讲清 `qt_standard_project_setup()` 在 6.12 里做了什么、**没做什么**
5. 用两条路线把资源编进可执行文件：`qt_add_resources` 与 `.qrc` + `AUTORCC`
6. 用 `enable_testing()` + `add_test()` 让 CTest 跑 QtTest 用例，并解释「退出码 0 = 通过」
7. 说清 `tr()` → `.ts` → `.qm` → `:/i18n/` 四个阶段各由谁完成
8. 用 `windeployqt` 收齐依赖，看得出目录树里少了哪一块会导致哪条报错
9. 面对「configure 挂 / build 挂 / run 挂」能立刻判断该查哪一层

## 三、五个工程与学习重点

```text
  qt_cmake_01_widgets/          ← 最小骨架
    重点：cmake_minimum_required / project(LANGUAGES CXX) / find_package /
          qt_standard_project_setup / qt_add_executable / target_link_libraries；
          qt_add_executable 与 add_executable 的差别（Windows 入口点、移动平台、
          「Qt 目标」标记）；install(TARGETS ... RUNTIME DESTINATION bin)；
          main.cpp 是一份环境自检：qVersion()、platformName()、主屏几何与 DPR
    诚实标注：qt_standard_project_setup 不收 AUTORCC，工程 03 会自己打开

  qt_cmake_02_structure/        ← 库 + 界面
    重点：add_library(greeting STATIC) 的源列表里放头文件（让 AUTOMOC 与 IDE 看得到）；
          target_include_directories(greeting PUBLIC include) → 下游写 <greeting/greeting.h>；
          Qt6::Core 是 PUBLIC（公开头里出现了 QObject），Qt6::Widgets 在 app 上 PRIVATE；
          手写 set(CMAKE_AUTOMOC/AUTOUIC/AUTORCC ON)，和标准设置对照；
          target_compile_definitions(greeting PRIVATE GREETING_LIBRARY=1) 的用途
    诚实标注：AUTOMOC 对**头文件里的 Q_OBJECT** 自动生效；写在 .cpp 里的类要 include .moc

  qt_cmake_03_resources/        ← 资源两条路线
    重点：qt_add_resources(<target> "<name>" PREFIX "/" FILES ...) 直接生成 ":/style.qss"；
          传统路线：set(CMAKE_AUTORCC ON) + target_sources(... legacy.qrc) → ":/legacy/style.qss"；
          两条路径的内容逐字节比较、字节数打印、WriteOnly 打开必失败（资源只读）；
          qss 从资源读出来喂给 setStyleSheet 的日常写法
    诚实标注：Qt6 CMake 层默认让 AUTORCC 用 zstd；本机没编 zstd 或设了
              QT_NO_AUTORCC_ZSTD 时会追加 --no-zstd

  qt_cmake_04_tests/            ← 库 + 应用 + 测试
    重点：find_package(Qt6 COMPONENTS Core Test)；enable_testing() 必须在顶层；
          add_test(NAME tst_calc COMMAND tst_calc)；
          set_tests_properties(... ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)；
          QTEST_GUILESS_MAIN（只要 QCoreApplication）与 QTEST_MAIN 的差别；
          数据驱动、QCOMPARE 对 double 的容差比较、NaN 要用 qIsNaN；
          <QtNumeric> 里的 qQNaN()（qnumeric.h 第 76 行）
    诚实标注：用例的「通过」= 进程退出码 0；QtTest 自己负责把这个码算对

  qt_cmake_05_translations/     ← 翻译链路
    重点：tr() 与 %n 复数；qt_standard_project_setup(I18N_SOURCE_LANGUAGE zh_CN
          I18N_TRANSLATED_LANGUAGES en)；qt_add_translations(target TS_FILE_BASE ...)
          创建 lupdate/lrelease 目标并把 .qm 编进 ":/i18n"；
          运行期 QDir(":/i18n").entryList() 看有哪些 qm，QTranslator::load +
          installTranslator，查不到就优雅回落源码语言；
          写在 .cpp 里的 Q_OBJECT 类要 #include "main.moc"
    诚实标注：首次构建生成的 .ts 是空的，需要 Qt Linguist 填译文后重建；
              find_package(QUIET COMPONENTS LinguistTools) 让没装 Linguist 的机器也能编
```

构建、验证、运行三条命令（每个工程目录里都能用）：

```bash
cmake -G Ninja -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure    # 只有工程 04 有用例
```

## 四、三层故障地图

| 阶段 | 谁在工作 | 典型报错 | 第一反应 |
|---|---|---|---|
| configure | CMake 读 `CMakeLists.txt` | `Could not find ... Qt6`、`Unknown CMake command` | Qt 路径（`CMAKE_PREFIX_PATH` / `qt-cmake`）、函数名拼写、CMake 版本 |
| build | 编译器 + moc/rcc/uic | `undefined reference to vtable`、`No rule to make target 'xxx.moc'` | AUTOMOC 是否对该目标生效、`.moc` include、函数签名 |
| run | 你的程序 + Qt 运行时 | `0xC0000135`、`no Qt platform plugin`、资源打不开 | DLL/插件目录、`windeployqt`、qrc 是否进目标、`QT_DEBUG_PLUGINS=1` |

记住这张表，你查构建问题的速度会快一倍——因为**报错出现的阶段，就已经告诉了你该看哪一层**。

## 五、常见误区 → 正解

| 误区 | 正解 |
|---|---|
| 「Qt 装好了，CMake 就该找得到」 | CMake 只在自己知道的路径里找。给 `-DCMAKE_PREFIX_PATH`，或用 `qt-cmake` |
| 「`qt_standard_project_setup()` 打开所有自动化」 | 6.12 里只打开 AUTOMOC、AUTOUIC。AUTORCC 要自己开 |
| 「`add_executable` 和 `qt_add_executable` 一样」 | 后者处理 Windows 入口点、移动平台，并标记成 Qt 目标 |
| 「库的链接写成 PRIVATE 更干净」 | 公开头里出现的 Qt 类型必须 PUBLIC，否则下游 include 就断 |
| 「`.qrc` 放进目录就会被处理」 | 必须**加进某个目标的源列表**，且该目标开了 AUTORCC |
| 「资源可以写」 | 资源只读。可写数据放 `QStandardPaths` 指出的目录 |
| 「测试就是看打印」 | 测试的判据是退出码。`add_test` 只认这个 |
| 「翻译就是加个 .ts」 | 还要 lrelease 成 .qm、编进资源、运行期 load + install |
| 「拷 exe 就算部署」 | 还要 DLL、`platforms/`、`styles/`、`imageformats/`、`qt.conf` |
| 「Debug 和 Release 的 DLL 混着用没事」 | 这是 `0xC0000139` 最常见的来源 |

## 六、练习

1. **默写骨架**：关掉工程 01，白纸写出六行 `CMakeLists`，再和文件对照。写错的地方就是你的盲区。
2. **制造找不到 Qt**：把 `CMAKE_PREFIX_PATH` 去掉、`find_package` 保持 REQUIRED，看报错原文；再用 `qt-cmake` 跑一遍。
3. **制造 vtable 错误**：把工程 02 的 `set(CMAKE_AUTOMOC ON)` 注释掉，重新构建，读一遍报错。
4. **资源对照**：在工程 03 里删掉 `target_sources(... legacy.qrc)`，看 `:/legacy/style.qss` 是否如预期打不开。
5. **让测试变红**：把 `calc::percent` 的封顶去掉（`part > total` 时返回 110），跑 `ctest`，看它怎么报。
6. **补一条译文**：用 Qt Linguist 打开生成的 `.ts`，把一句中文填成英文，重建后看 `installTranslator` 是否生效。
7. **部署一次**：`windeployqt --release --dir deploy`，然后**删掉 `platforms/qwindows.dll`**，运行，读那条经典报错。

## 七、验收标准

- [ ] 你的项目能从零 configure、build、run，且命令能写进 README
- [ ] 你能说出每个目标链接了什么、为什么是 PUBLIC 或 PRIVATE
- [ ] 资源文件都在 qrc 或 `qt_add_resources` 里，不存在「运行时才发现的路径错误」
- [ ] 有一条命令能让测试自己跑完并给出结论
- [ ] 用户可见的文本都过了 `tr()`，语言切换不需要改代码
- [ ] 你的发布目录里，DLL、平台插件、样式插件一个不缺，并能解释每一层的作用

## 八、下一站

构建与部署讲完，剩下的就是把前面十一个阶段**拼成一个真东西**。综合实战 `projects/todo_desktop` 会带你从零写一个桌面待办管理器：分层、模型、委托、持久化、快捷键、测试、打包，一步不缺。

想看动画复盘，随时回来翻 [图解 · 部署](../visuals/deployment_tree.html)、[图解 · 资源系统](../visuals/resource_pipeline.html) 与 [图解 · 启动流程](../visuals/startup_flow.html)。加油，徒弟！
