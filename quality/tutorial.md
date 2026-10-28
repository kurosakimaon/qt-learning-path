# 第拾阶段 · 工程质量与资深实践（Qt 从入门到资深）

> 老师傅带你换一种工作方式：从「改完跑一下看看」，换成「改完让机器告诉我它还对不对」

## 一、模块简介

徒弟，走到这一关，你已经能让程序弹窗、画图、存数据、联网、多线程了。但「能跑」和「能交付」之间隔着一层东西：**下次改动之后，你怎么知道它还对？**

这一阶段 7 讲，回答的就是这个问题。前两讲讲「怎么验」（单元测试、界面测试），中间三讲讲「怎么让代码验得动、改得动」（分层、所有权、错误），最后两讲讲「怎么量」（性能测量、百万行视图）。

它们有一个共同点：**把「我觉得应该没问题」换成机器能判定的结论。**

> 环境事实：Qt 6.12.0、Windows、MinGW-w64（MSYS2 ucrt64，gcc 15.2/16.2）、CMake ≥ 3.21 + Ninja、C++17。目标名 `quality_0N_xxx`，已在顶层 `CMakeLists.txt` 登记，本组额外链 `Qt6::Test` 与 `Qt6::Concurrent`；顶层把每个示例注册成 CTest 用例并强制 `QT_QPA_PLATFORM=offscreen` 运行。
>
> 诚实标注：**本机当前无法编译链接 Qt 程序**，7 个示例**都未在本机实测运行**（两个 QtTest 用例还没在 CTest 里跑过）。每个类、函数、宏、枚举都对着本机 `C:\msys64\ucrt64\include\qt6` 里真实头文件核对过（注释里给了文件名与行号），核对不上的 API 一律不用。**性能数字不编造**：示例只承诺相对关系，具体毫秒数以你的机器打印为准。

## 二、学习目标

学完本模块，你要能做到：

1. 写一个 QtTest 测试类（`QObject` + `private slots`），说得清 `initTestCase` / `init` / `cleanup` / `cleanupTestCase` 谁每轮跑、谁只跑一次
2. 解释 `QCOMPARE` 与 `QVERIFY` 的输出差别，会用数据驱动（`f_data()` + `QTest::newRow`）把同一逻辑铺成多组用例
3. 用 `QSignalSpy` + `spy.wait(期限)` 测异步信号，用 `QTRY_VERIFY` / `QTRY_COMPARE` 代替固定 `qWait`
4. 在无头机器上把按钮点下去，并知道 `QTest::keyClicks` 只认 Latin-1 这个边界
5. 画出自己的分层（UI / 应用 / 领域 / 基础设施），并守住「依赖只朝一个方向」
6. 为每一个 `new` 说出它的所有者，会用 `QPointer` 检测悬空、用 `deleteLater` 延迟销毁、用 `connect` 的 context 参数绑定连接寿命
7. 分清 Qt 的返回值型错误与 C++ 的异常型错误，会写一个带「错误码 + 消息 + 可恢复性 + 上下文」的 `Result<T>`
8. 搭一套最小可信的性能测量（预热 + 多轮取中位数），并用它回答「这里为什么慢」
9. 解释为什么百万行的表格可以只问几百次 `data()`，以及哪些「无辜的」调用会把它变成一百万次

## 三、文件列表与学习重点

```text
  01_qtest_unit.cpp             ← QtTest 单元测试（第一道坎：可测性）
    重点：测试类 = QObject 子类 + private slots；四个钩子谁每轮跑、谁只跑一次
          （本讲把计数与顺序真打出来）；
          QCOMPARE 会打印操作数原文 + 实际值 + 类型，QVERIFY 只有表达式文本
          （对着 qtestcase.h 的宏展开看）；
          数据驱动两套写法：void f_data() + QTest::newRow("tag") << ...
          与 QTest::addRow("%d", x) << ...；QTest::addColumn<T>() 声明列，
          QFETCH(Type, name) 取值；
          QEXPECT_FAIL 标已知缺陷（模式只有 QTest::Abort / QTest::Continue）；
          最值钱的一半在代码外：折扣逻辑抽成不碰 QWidget 的 PricingEngine
    诚实标注：本讲是 QTEST_GUILESS_MAIN，不需要平台插件；用例本机未跑

  02_qtest_gui.cpp              ← 界面测试（第二道坎：无头点按钮）
    重点：QTEST_MAIN 与 QTEST_GUILESS_MAIN 只差一行，差的是「要不要 QApplication
          与平台插件」；QTest::keyClicks(QWidget*, ...) 只操作你递过去的控件、
          不依赖焦点（qtestkeyboard.h 第 194~197 行，widget 为空才回去找焦点）；
          ★ 它只认 Latin-1：sequence.at(i).toLatin1()（第 265 行），中文敲不进去；
          QSignalSpy 数次数与参数，spy.wait(期限) 等异步信号；
          模态 exec() 开新事件循环 → QTimer::singleShot(0, &dlg, &QDialog::accept) 关掉；
          QTRY_VERIFY / QTRY_COMPARE 按步长重试，成功立刻过
    诚实标注：跑法需要 QT_QPA_PLATFORM=offscreen；真实窗口管理器的差异不在承诺范围

  03_architecture_layers.cpp    ← 分层架构
    重点：四层各自只认识下一层：UI（TodoPage : QWidget，只认识 Service）/
          应用层（TodoService : QObject，只认识 ITodoStore）/ 领域层（TodoItem、
          ITodoStore，只认 QtCore 值类型）/ 基础设施层（Memory / Json / Audited / Fake）；
          每一层用运行期证据钉住，而不是一句「要有架构」；
          业务里 include <QWidget> 的三个代价：只能在有窗口的进程里跑、
          换存储要改界面、写测试要连数据库；
          FakeStore 证明「换掉一层，别的层不用动」
    诚实标注：分层是边界纪律，不是框架；本讲不引入第三方 DI 容器

  04_ownership_and_lifetime.cpp ← 所有权与生命周期
    重点：六件工具逐个量：parent/child 树 + children()、std::unique_ptr、
          deleteLater + DeferredDelete、QPointer（裸指针做不到悬空检测）、
          connect 的 context 参数、QObject 禁止拷贝（编译期事实）；
          ★ 销毁顺序是「实现细节」不是契约——本讲只承诺「父析构会收走子」，
          不承诺子对象之间的先后；
          每个阶段都落到一道能红的账上（删除计数、悬空检测、连接失效计数）
    诚实标注：DeferredDelete 何时处理取决于事件循环，本讲用事件循环回到空闲作为观测点

  05_error_handling.cpp         ← 错误处理
    重点：返回值型（绝大多数 Qt API）：QFile::open + error()/errorString()、
          QJsonDocument::fromJson + QJsonParseError 出参、QSqlQuery::exec + lastError()，
          **一个异常都不抛**；
          抛异常型：std::vector::at 越界（std::out_of_range）、operator new 失败
          （std::bad_alloc）、某些配置下 QVariant::to<T>() 的 QtException 家族；
          本讲把 catch 计数摆成 0、同一函数里越界 at() 抓到 1 次，两种风格一次对齐；
          层间传错自己写 Result<T>：错误码 + 消息 + 是否可恢复 + 上下文字段
    诚实标注：Qt 的异常支持依赖构建配置（QT_EXCEPTIONS）；本讲只对「本机头文件里
              声明会抛的」下断言，并注明它可能在别的配置下不同

  06_performance_profiling.cpp  ← 性能：先量，再猜
    重点：先立测量脚手架：预热 + 多轮取中位数 + 用一次「已知答案」的测量校准计时器；
          五个高发区：字符串拼接与隐式共享、容器增长与 reserve（capacity 可见）、
          线性 vs 哈希查找（QMap 与 QHash 的差别不是常数级）、重绘 vs 缓存、
          日志与调试输出（关掉的类别几乎零成本）；
          顺序不能反：先学量，再学优化
    诚实标注：所有数字由示例当场测量打印，文中不写具体毫秒值；结论只承诺相对关系

  07_large_view_optimization.cpp ← 百万行不卡
    重点：「你有没有把一百万行都塞进控件」——setModel 之后视图问 rowCount()，
          按「行数 × 行高」算滚动范围，不一定逐行问数据；
          六个阶段：①视图只问可见区（一百万里只问几百次 data()）
          ②canFetchMore / fetchMore 分批取 ③列宽 Fixed 一秒摆好、
          ResizeToContents 要按行名去量（代价可数）④代理在 12 万行上过滤 + 排序，
          一秒内出结果且能对得上 ⑤批量更新：500 次单条插入 vs 关掉更新再插
          ⑥data() 里的重活：每次调用都格式化，调用一多就是真钱
    诚实标注：六个阶段共同手法只有一句——数调用次数；本讲每个模型都装了 data() 计数器
```

## 四、三张表

**表一：测试选哪个宏**

| 被测对象 | 宏 | 需要平台插件 | 典型用途 |
|---|---|---|---|
| 纯逻辑（不碰 QWidget） | `QTEST_GUILESS_MAIN` | 否 | 算法、解析、状态机 |
| 界面（要 QApplication） | `QTEST_MAIN` | 是（`offscreen` 即可） | 控件行为、信号、焦点 |

**表二：所有权速查**

| 对象 | 归谁 | 收尾 |
|---|---|---|
| 设了 parent 的 `QObject` | 父亲 | 随父析构，别手动 delete |
| 栈上的 `QObject` | 作用域 | 出生入死，别设 parent |
| 无 parent 的 `QObject` | 你 | `delete` 或 `deleteLater` |
| 非 `QObject` 的独占资源 | `std::unique_ptr` | 作用域结束自动释放 |
| 跨对象引用 | `QPointer`（QObject）/ `QPersistentModelIndex`（索引） | 自动置空/失效 |

**表三：错误处理选哪条道**

| 场景 | 手段 |
|---|---|
| 可预期的失败（文件不存在、解析失败、连不上） | 返回值 + 错误细节 |
| 程序性错误（越界、空指针解引用） | 断言 / 让它崩在 Debug |
| 资源耗尽（内存） | 异常或让它崩，别静默降级 |
| 需要跨层带上下文 | 自己的 `Result<T>` |

## 五、常见误区 → 正解

| 误区 | 正解 |
|---|---|
| 「测试就是 `qDebug()` 打一下」 | 那是观察，不是判定。测试要有断言、有退出码 |
| 「界面没法自动测」 | 能测。`QTest::keyClicks` 不依赖焦点，`offscreen` 不需要显示器 |
| 「`QVERIFY(a == b)` 和 `QCOMPARE(a, b)` 一样」 | 失败时 `QCOMPARE` 告诉你两边各是什么、什么类型 |
| 「业务层 include 一下 Widget 头没关系」 | 那它就只能活在 GUI 进程里，也测不动 |
| 「`delete` 掉控件就完事了」 | 它还在父对象的子列表里、信号还连着。用 `deleteLater`，或让父对象收 |
| 「`try { file.open(...) } catch` 能兜住」 | `QFile::open` 不抛。要检查返回值 |
| 「一次测出来的耗时就是性能」 | 那是噪声。预热 + 多轮 + 中位数 |
| 「视图慢是因为模型数据多」 | 常常是因为某个调用被调了一百万次。先数调用次数 |
| 「优化完感觉快了就行」 | 用同一套脚手架量前后对比。感觉不算证据 |

## 六、练习

1. **给现有代码加测试**：挑你之前写的一个函数，先写出 3 个用例，再故意改坏它，看测试是否变红。红不了，说明测试没测到点子上。
2. **数据驱动**：把第 1 讲的折扣用例改成 5 组数据，用 `addRow("%d", n)` 命名，观察失败时输出里能否直接看出是哪一组。
3. **无头点按钮**：把第 2 讲的用例跑在 `offscreen` 下，然后试着用 `keyClicks` 敲中文，确认它敲不进去——这就是边界。
4. **依赖方向检查**：在你的项目里搜 `#include <QWidget>`，看看业务文件里有没有。有就说明分层漏了。
5. **悬空指针**：写一个 `QPointer` 与裸指针的对照实验：对象销毁后分别读它们。
6. **误用异常**：写一段 `try { QFile f("/nonexistent"); f.open(...); } catch (...)`，确认 catch 一次都没进。
7. **性能脚手架**：用第 6 讲的写法测一测你自己的某个函数，跑 1 次和跑 30 次取中位数，比较差异有多大。
8. **百万行**：把第 7 讲的模型行数改成 100 万，用你装的 `data()` 计数器确认它只被问了几百次。再把 `resizeColumnsToContents()` 加上，看计数器变化。

## 七、验收标准

- [ ] 你的项目里至少有一组能自动跑的测试，且改坏代码时它会红
- [ ] 你能一句话说出每一层的职责和它的 `#include` 边界
- [ ] 项目里没有「不知道谁负责删」的对象
- [ ] 每个可能失败的调用都有处理路径，错误信息能回答「哪儿错、能不能救」
- [ ] 性能改动前后有同一套测量数据，而不是「感觉快了」
- [ ] 你能解释自己表格里 `data()` 被调用了多少次、为什么是这个数

## 八、下一站

代码能验、能改、能量了，最后一步是把它**装进别人的电脑**。第拾壹阶段「构建与部署」会从零建五个独立工程，讲清 CMake 的目录结构、`find_package` 与目标、资源和翻译、`windeployqt` 与 `qt.conf`、以及静态链接的代价。

想看动画复盘，随时回来翻 [图解 · 四道质量门](../visuals/quality_gate.html)、[图解 · 对象树](../visuals/qobject_tree.html)、[图解 · 模型/视图](../visuals/model_view.html) 与 [图解 · 绘制管线](../visuals/painting_pipeline.html)。加油，徒弟！
