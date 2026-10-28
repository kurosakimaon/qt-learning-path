# 第捌阶段 · 数据与持久化（Qt 从入门到资深）

> 老师傅带你回答四个问题：这份数据归谁、以什么形状落盘、怎么写不坏、写坏了怎么知道

## 一、模块简介

徒弟，前七个阶段你的程序一关就什么都不剩。这一关开始，它要有记忆。

持久化看着简单——不就是存一下、读回来吗？但它有四个跟 API 无关的坑，每一个都能让程序在测试机上完美、在用户机上出事：

1. **时机**：什么时候写、什么时候读、什么时候 flush。写太早，用户还没编辑完；写太晚，程序崩了就没了。
2. **类型**：你存进去的是 `42` 还是 `"42"`？读的时候要的又是什么？Qt 的很多读接口**不报错**，只静静给你一个默认值。
3. **失败**：磁盘满了、目录没权限、文件被另一个进程占着。你的代码知道吗？
4. **完整性**：写到一半掉电，文件是「全新的」还是「半截的」？

本模块 6 讲，就是这四件事的六个战场：配置、文件、JSON、资源、SQL、数据库接界面。

> 环境事实：Qt 6.12.0、Windows、MinGW-w64（MSYS2 ucrt64，gcc 15.2/16.2）、CMake ≥ 3.21 + Ninja、C++17。目标名 `storage_0N_xxx`，已在顶层 `CMakeLists.txt` 登记；本组额外链 `Qt6::Sql` 与 `Qt6::Xml`。
>
> 诚实标注：**本机当前无法编译链接 Qt 程序**，6 个示例**都未在本机实测运行**。每个类、函数、枚举、重载都对着本机 `C:\msys64\ucrt64\include\qt6` 里真实头文件核对过（注释里给了文件名与行号），核对不上的 API 一律不用。凡策略写在 Qt 的 .cpp 里、头文件读不到的行为（比如「重新查询会不会丢掉未提交的改动」），文中标 **Soft**，只承诺能立住的部分。

## 二、学习目标

学完本模块，你要能做到：

1. 说清 `QSettings` 的组织名/应用名为什么是**构造期快照**，以及五种构造形态分别落在哪儿；会用 `setPath` 把 ini 改道到指定目录
2. 解释 `value(key, 默认值)` 的第二个参数**同时指定期望类型**，说得清「存 `"abc"` 读 `int`」会发生什么
3. 分清 `QIODeviceBase` 的模式位、`QIODevice` 的读写动词、`QFile` 的平台差异；知道 `Text` 位改的是换行翻译（于是 `pos()` 与真实偏移不等）
4. 用 `QSaveFile` 做原子替换、用 `QLockFile` 防多开写坏、用 `QDir` 正确遍历（不把 `.` 与 `..` 当条目）
5. 正确处理 `QJsonValue` 的七种类型：先 `contains()` / `isXxx()` 把形状问清，再取值；知道 `toJson()` 的字节不是协议
6. 说清 `:/prefix/name` 这条路径是怎么拼出来的，`alias` 与 `file` 的差别，以及 `AUTORCC` 在构建期做了什么
7. 用对 `QSqlDatabase` / `QSqlQuery`：连接名、绑定值、事务、`QSqlError`；说得清为什么连接不能跨线程共用
8. 用 `QSqlTableModel` 接界面，解释 `OnFieldChange` / `OnRowChange` / `OnManualSubmit` 三档编辑策略对「改了为什么库里没变」的影响

## 三、文件列表与学习重点

```text
  01_qsettings.cpp             ← 最小的一块持久化，坑在时机与类型
    重点：组织名/应用名是构造期快照（两个对象各自的 fileName() 左右对照）；
          五类构造形态（Format+Scope+组织+应用 / Scope+组织+应用 / 组织+应用 /
          fileName+Format / QObject* parent）与三种作用域；
          本讲用 IniFormat + UserScope + setPath(临时目录) 改道，**不写注册表、不碰真实用户目录**；
          层级 key、beginGroup/endGroup 配对、group()/allKeys()/childKeys()/childGroups()；
          value(key, 默认值) 的类型自动转换：42 ↔ "42" 能转，"abc" 读 int 静静回默认值
    诚实标注：平台差异（Windows 注册表 vs Linux ini）本讲只承诺「用 setPath 之后落点由你定」

  02_file_io.cpp               ← 文件与 IO 设备（第一道坎）
    重点：openMode 九个位（qiodevicebase.h 第 17~28 行）谁能和谁拼、Append 与 Truncate
          同时给的后果、Text 位改的是换行翻译不是编码、ExistingOnly / NewOnly 是两道闸；
          QFile 的动词：read/readAll/write/pos/seek/atEnd/size/bytesAvailable，
          以及 seek 到尾部后 atEnd 的实测（pos == size 时为真）；
          文本 vs 二进制的字节数与行数差；★ Qt6 已删 setCodec()，只剩
          setEncoding(QStringConverter::Encoding)（qtextstream.h 第 89 行），
          老代码 ts.setCodec("UTF-8") 编不过；setLocale() 对数字格式的连带影响；
          QSaveFile 的原子替换（★ close() 在 private: 段，用 commit()）；
          QLockFile 的互斥；QDir 的遍历与过滤
    诚实标注：`atEnd()` 的具体判定来自本机头文件与实测口径，示例里打印出来给你核对

  03_json.cpp                  ← JSON 的七种类型与四个陷阱
    重点：QJsonValue 七个枚举（Null=0x0 Bool=0x1 Double=0x2 String=0x3 Array=0x4
          Object=0x5 Undefined=0x80，qjsonvalue.h 第 32~40 行）；
          toBool/toInt/toInteger/toDouble/toString/toArray/toObject 一个 bool* 出参都没有，
          类型不对静静回默认值；想知道「有没有这个键」只有 contains()；
          QJsonObject 的键是集合不是队列（公开接口只有 keys()/size()/迭代器，
          keyAt/valueAt 在 private 段），要下标语义只有 QJsonArray；
          顶层解析两个入口都不给多文档；QJsonParseError 的用法；
          toJson() 的字节序与空格不保证，别拿去 diff 或当协议
    诚实标注：官方文档说键「按字母排序」，本机头文件里**没有排序承诺**——
              本讲把插入序、keys() 序、迭代器序三份并排打出来，让输出说话

  04_resource_system.cpp       ← qrc：把只读文件编进 exe
    重点：路径 = ":" + <qresource prefix> + 条目路径或 alias，清单见 storage/resource_demo.qrc
          （prefix/file/alias 三条规则写在那个文件的注释里）；
          同一张 logo.svg 挂两个路径：alias 改路径不改内容，但字节真的存了两份；
          AUTORCC 对每个 .qrc 调 rcc 生成 C++ 文件（qrc_<名字>.cpp），
          里面三张静态数组 + qInitResources_<名字>() + Q_CONSTRUCTOR_FUNCTION，
          在 main 之前就把资源挂进全局注册表；
          Qt6 的 CMake 层默认让 AUTORCC 用 zstd 压缩（可用 QT_NO_AUTORCC_ZSTD 关掉）；
          QResource 的公共可读项边界（Qt 6.12 的 qresource.h 只有 71 行，本讲只写存在的名字）；
          静态库里的资源要 Q_INIT_RESOURCE
    诚实标注：资源是只读的，运行时不能写；要可写请用 QStandardPaths 指到用户目录

  05_sql_database.cpp          ← 连接名、绑定值、事务、错误
    重点：addDatabase 不写第二个参数 = 默认连接名 "qt_sql_default_connection"
          （qsqldatabase.h 第 38~47 行），于是全进程共用一个连接；
          database(name, open=true)（第 112 行）还默认帮你 open；
          连接属于创建它的线程，跨线程用新名字或 cloneDatabase（第 110/111 行）；
          QSqlDatabase 是 Q_GADGET 不是 QObject（第 51 行），拷贝 = 同一个连接的第二个句柄，
          收尾必须 removeDatabase(名字)（第 114 行）；QSqlQuery 拷贝已 = delete（qsqlquery.h 第 36~44 行）；
          prepare + bindValue 防注入；transaction/commit/rollback；
          QSqlError 的 type()/nativeErrorCode()/text() 怎么读
    诚实标注：驱动差异（SQLite vs MySQL）只在示例实际使用的驱动上断言

  06_sql_model_view.cpp        ← 数据库接界面
    重点：QSqlQueryModel 是只读快照（qsqlquerymodel.h 里根本没有 setData），
          QSqlTableModel 是可编辑的行集合（qsqltablemodel.h 第 42 行 setData、第 37 行 flags）；
          三种编辑策略 OnFieldChange / OnRowChange / OnManualSubmit（第 29 行枚举）
          解释「我改了为什么库里没变」；
          setFilter/setSort 会重新查询；重新查询与未提交改动的关系标 **Soft**，
          硬账只记「显式 submitAll 之后一定不丢」；
          QSqlRelationalTableModel 让 dept_id 显示成「研发部」；
          模型是缓存：绕过模型改库，界面不会自己知道
    诚实标注：示例全部用 :memory: 数据库或临时文件，不碰任何真实数据库
```

## 四、四张对照表

**表一：选哪个容器？**

| 数据 | 容器 | 理由 |
|---|---|---|
| 窗口位置、上次打开的目录、勾选项 | `QSettings` | 键值、跨平台、无需自己管格式 |
| 一段结构化文档（用户的项目文件） | `QFile` + `QJsonDocument` | 可读、可 diff、可升级 |
| 图标、qss、翻译、默认配置 | qrc | 只读、跟着 exe 走、用户改不坏 |
| 上千条有查询/关联需求的记录 | SQLite + `QSqlTableModel` | 事务、索引、过滤、排序 |
| 大文件、二进制（图片、压缩包） | `QFile` + `QSaveFile` | 别塞进配置，也别塞进数据库 |

**表二：`open()` 之后必须问的三个问题**

```text
1) 返回值是 true 吗？                        → 失败时 errorString() 说了什么
2) 我要的模式位真的生效了吗？                 → Append？Text？NewOnly？
3) 我在读还是写？读的时候 atEnd() / pos() 对不对？
```

**表三：JSON 取值的正确顺序**

```text
doc.isObject() → obj.contains("key") → v.isDouble() → v.toInt() / v.toString()
                 ↑ 没有这一步，后面全是「静静回默认值」
```

**表四：SQL 的寿命**

| 东西 | 能拷吗 | 归谁 | 怎么收 |
|---|---|---|---|
| `QSqlDatabase` | 能（还是同一个连接） | 连接名 | `removeDatabase(名字)` |
| `QSqlQuery` | 不能（拷贝已删） | 创建它的线程/连接 | 作用域结束 |
| `QSqlTableModel` | 不能（QObject） | 设了 parent 就归父亲 | 随父析构 |

## 五、常见误区 → 正解

| 误区 | 正解 |
|---|---|
| 「`QSettings` 在哪儿 new 都一样」 | 组织名/应用名在构造那一刻就定死了。先 `setOrganizationName()` / `setApplicationName()`，再 `new QSettings` |
| 「`value(key, 0).toInt()` 就够了」 | 第二个参数**同时是期望类型**。类型不匹配时它静静回默认值，不报错 |
| 「`QIODevice::Text` 是编码设置」 | 它只做换行翻译。编码靠 `QTextStream::setEncoding()` 或 `QString::fromUtf8` |
| 「`QTextStream::setCodec` 还能用」 | Qt6 已删。编不过，不是「不生效」 |
| 「`QJsonValue::toInt()` 会告诉我失败」 | 不会。没有 `bool*` 出参。先 `isDouble()` / `contains()` |
| 「`QJsonObject` 能按下标取第 i 个键」 | 不能。公开接口没有 `keyAt`；要顺序用 `QJsonArray` |
| 「资源文件能用 `QFile` 写」 | 只读。要写就用 `QStandardPaths::writableLocation()` 指到用户目录 |
| 「`addDatabase("QSQLITE")` 每次都是新连接」 | 不写名字就是默认连接名。同名就是同一个连接，还是帮你 `open()` 过的那种 |
| 「`QSqlDatabase` 拷贝一份就是新连接」 | 拷贝是同一个连接的第二个句柄。收尾用 `removeDatabase(名字)` |
| 「`setData` 了数据库就该变」 | 要看 `editStrategy`。`OnManualSubmit` 下要 `submitAll()` |

## 六、练习

1. **改道实验**：把第 1 讲的 `setPath` 换成默认路径，跑一次，然后用 `fileName()` 找到文件，看看它到底落在哪儿。看完删掉，别留在真实用户目录。
2. **类型实验**：存一个 `"abc"`，再用四种方式读：`toInt()`、`toString()`、`toBool()`、`value(key).typeId()`。把四个结果都打出来。
3. **半截文件**：用 `QFile` 直接覆盖写，写一半 `qFatal`（在临时文件上做），看看文件剩什么；再用 `QSaveFile` 重做一遍。
4. **编码对照**：同一句中文，分别用 `QString::fromUtf8` 与 `QString::fromLatin1` 还原，打印长度。你就明白为什么仓库的检查门要拦 `QLatin1String("中文")`。
5. **资源双路径**：在第 4 讲的 qrc 里给同一张图再加一个 alias，比较 `QFile(":/…").size()` 两次。
6. **SQL 事务**：插 1000 行，先逐条提交，再包成一个事务提交，用 `QElapsedTimer` 量两次的差。这就是第 5、6 讲所有「性能」讨论的地基。
7. **编辑策略**：把第 6 讲的模型换成三档策略各跑一遍，`setData` 之后立刻查库，看哪一档已经变了。

## 七、验收标准

- [ ] 能说清你项目里每一份数据「归谁、什么形状、写坏了怎么办」
- [ ] 所有写盘路径都检查了 `open()` 返回值，关键文件用了 `QSaveFile`
- [ ] 读 JSON / `QSettings` 的代码里，能找到 `contains()` 或 `isXxx()` 这类形状检查
- [ ] 能解释你项目里数据库连接的名字是谁、归哪条线程、什么时候 `removeDatabase`
- [ ] 资源文件里没有可写内容；可写内容都在 `QStandardPaths` 指出的目录里
- [ ] 拿一个被截断的文件 / 一个坏 JSON / 一个被占用的锁文件去喂你的程序，它不崩、且能说出为什么

## 八、下一站

数据有了家，下一步是让它在**网络**上流动。第玖阶段「网络」会把 `QNetworkAccessManager` 的异步模型、REST 封装、TCP 的字节流、下载状态机、DNS 与 TLS 一次讲透——你会发现它和线程那一阶段是同一个主题的两面：**谁在什么时候、在哪条线程、以什么顺序做事**。

想看动画复盘，随时回来翻 [图解 · 资源系统](../visuals/resource_pipeline.html) 与 [图解 · 模型/视图](../visuals/model_view.html)。加油，徒弟！
