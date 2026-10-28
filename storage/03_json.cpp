// ============================================================================
// 第 3 讲 · JSON：QJsonDocument / QJsonObject / QJsonArray / QJsonValue
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   JSON 这块最骗人的地方有两处：一是「读不到」和「读错了」它只告诉你第一处，
//   二是「它看着像 Map，其实是 CBOR 容器上贴的一层薄皮」。新手把 QJsonObject
//   当有序字典用、把 toInt() 当「会失败的转换」用、把 toJson() 的字节当协议的一部分
//   拿去 diff，三年后都会在产线上爆。本讲八条，每条都有可数的打印输出，每条的
//   签名/枚举值/行号都能在本机头文件里对着查：
//     (1)【1】QJsonValue 是个变体，类型只有 7 个枚举成员（qjsonvalue.h 第 32~40 行：
//        Null=0x0 Bool=0x1 Double=0x2 String=0x3 Array=0x4 Object=0x5 Undefined=0x80）。
//        其中 6 个正好对上 RFC 8259 的六种值，第 7 个 Undefined 是「这个位置什么都没有」
//        的哨兵。而读的那一族 toBool/toInt/toInteger/toDouble/toString/toArray/toObject
//        （第 95~105 行）**一个 bool *ok 出参都没有**，类型不对就默默回默认值；
//        想知道「有没有这个键」只有 contains()（qjsonobject.h 第 81/86/87 行）。
//     (2)【2】QJsonObject 的键是集合不是队列：内部是 key/value 交替的扁平数组
//        （private/qjson_p.h 第 102 行 `it[n * 2]`、第 193 行 `index = index * 2 + 1`），
//        公开接口只有 keys()/size()/迭代器（qjsonobject.h 第 63/64/306~313 行），
//        「第 i 个键」的 keyAt/valueAt 被关在 private 段里（第 391/392 行）；
//        要下标语义只有 QJsonArray 的 at(i)/operator[](qsizetype)（qjsonarray.h 第 46/62 行）。
//        官方文档关于键序只有一句「The list is sorted alphabetically.」，
//        **本机头文件里没有任何排序承诺**（四个 JSON 头文件我都通读过），所以这一节把
//        插入序、keys() 序、迭代器序三份并排打出来，让输出自己说话。
//     (3)【3】顶层解析只有两个入口，而且都不给你「多文档」：
//        QJsonDocument::fromJson(const QByteArray&, QJsonParseError* = nullptr)
//        （qjsondocument.h 第 73 行；我在整个 QtCore 里只 grep 到这一处文档级 fromJson）
//        和 QJsonValue::fromJson(QByteArrayView, ...)（qjsonvalue.h 第 82 行）。
//        QJsonDocument 的构造函数全是 explicit 且只吃 object/array（第 38~41 行）；
//        QJsonParseError 能用的只有 error（默认 NoError，qjsonparseerror.h 第 40 行）、
//        offset（默认 **-1**，第 38~39 行）和 errorString()（第 36 行）。
//        NDJSON / JSON Lines 没有「一次读多条」的接口，只能自己按 \n 切。
//     (4)【4】解析器是严格的：private/qjsonparser_p.h 第 28~54 行的 Parser 类里只有
//        parseObject/parseArray/parseMember/parseString/parseValue/
//        parseValueIntoContainer/parseNumber 七个方法（第 40~46 行），
//        **没有任何注释、尾逗号、单引号、裸键、NaN/Infinity 的入口**——严格不是风格，
//        是「压根没写」。错误枚举一共 15 个（qjsonparseerror.h 第 19~33 行，NoError=0 起步
//        顺到 GarbageAtEnd=14），GarbageAtEnd 就是「文档后面还有东西」那一位，
//        NDJSON 整块喂进去撞的就是它。本讲拿 8 条非法样本 + 2 条合法但易踩的样本对着打。
//     (5)【5】数字这一条最贵：QJsonValue::Type 里**没有 Integer**（第 32~40 行只有 Double），
//        但底层 CBOR 有（qcborvalue.h 第 71~86 行：Integer=0x00、Double=0x202，第 96 行 Q_ENUM）；
//        toInt 带钳位、toInteger 不带（qjsonvalue.h 第 160~163 行把 concreteInt 的第三个实参
//        分别传 true/false，声明在第 199 行）；qRound 是「先四舍五入再强制转换 + Q_ASSERT 把关」
//        （qnumeric.h 第 570~573 行 → 第 504~528 行，第 517/523 行两个 Q_ASSERT），
//        越界在 release 里不是钳位而是未定义行为；会钳位的那个 qSaturateRound 在第 557 行，
//        但它待在 namespace QtPrivate 里（第 491~568 行），**不是公开 API**。
//        本讲把 2^53 这一条用纯 C++ 算给你看（本机算过，数字写死在注释里）。
//     (6)【6】写出来的字节你只说对一半：toJson(JsonFormat = Indented) 只有两档
//        （qjsondocument.h 第 57~61、75 行 / qjsonvalue.h 第 42~49、84 行），
//        缩进宽度是 private/qjsonwriter_p.h 第 30~32 行的 int indent **形参**不是常量；
//        进出的编码合同是 UTF-8（解析器入口是 QUtf8StringView，qjsonparser_p.h 第 31 行）。
//        非 ASCII 到底有没有被转成 \uXXXX、控制字符怎么转，本机跑不了 → 标「预期」，
//        但本讲把「字节 hex + 含不含 \\u + 含不含原始中文 UTF-8 字节」都打成可数的数。
//        顺带演示 Windows 上「记事本另存为 ANSI」的坑：真 GBK 字节 CE C2 B6 C8（本机 iconv
//        量出来的），QByteArray::isValidUtf8()（qbytearray.h 第 247 行）会说是假的，
//        而 QStringConverter::Encoding 的枚举里**根本没有 GBK**
//        （qstringconverter_base.h 第 102~114 行只有 Utf8/Utf16*/Utf32*/Latin1/System）。
//     (7)【7】没有 schema 就得自己造闸门：本讲把「读一个键」包成带 bool 返回值 +
//        原因字符串的访问器（内部只问两件事：contains() 和 type()），8 个键逐个探针，
//        成功/被拒各自计数；再用 schemaVersion 做**一次只跨一个版本**的迁移。
//        纪律是「先校验，全部通过之前一个内存字段都不改」。
//     (8)【8】存盘的纪律是「候选先过校验 → 写临时 → commit 原子替换 → 回读再校验 →
//        才允许换内存」：QSaveFile 对外只有 open/commit/cancelWriting 三个开关
//        （qsavefile.h 第 45/46/48 行），close() 在 private 段（第 71 行）你调不到。
//        QSaveFile 本身的脾气在第 2 讲已经拆过，这里不重复，只演示纪律，并且故意把
//        文件截断一半，看**读失败时内存里的版本号有没有被动过**。
//
//   顺带的线：第 1 讲（storage/01_qsettings.cpp）讲注册表/INI 这套「有 schema 的存储」，
//   第 2 讲（storage/02_file_io.cpp）讲 QFile/QSaveFile/编码，本讲只讲 JSON 这一块皮；
//   第 10 模块（quality）的 QtTest 才是做「往返序列化断言」的正确工具，本讲不越界；
//   多线程那套（threads/03_cross_thread_signals.cpp）本讲用不上——全程单线程事件循环。
//
// 对应构建目标名：storage_03_json
//   <仓库根>/CMakeLists.txt
//   第 215 行：qt_add_demo(storage storage_03_json storage/03_json.cpp)
//   该函数第 77 行做链接（QT_DEMO_LIBS 在 storage 段第 212 行被设成
//   Qt6::Widgets Qt6::Sql Qt6::Xml，后面还并了一个 Threads::Threads），
//   第 33 行开了 set(CMAKE_AUTOMOC ON)，第 88~91 行把目标注册成 CTest 用例
//   （offscreen 平台 + TIMEOUT 120）。展开成最小可复现的三行就是：
//     find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Xml)
//     add_executable(storage_03_json storage/03_json.cpp)
//     target_link_libraries(storage_03_json PRIVATE Qt6::Widgets Qt6::Sql Qt6::Xml)
//   ★ 本文件**有** Q_OBJECT 类（ConfigStore），所以必须配一句
//     #include "03_json.moc"（在文件末尾）；手动核对命令：
//     moc -I<仓库根> -I<仓库根>/storage storage/03_json.cpp -o 03_json.moc
//
// 运行前须知：
//   · 只有 QCoreApplication：不建窗口、不弹任何模态框（没有 QMessageBox、没有
//     QFileDialog、没有 getchar）。8 个批次用 QTimer::singleShot(0/120/240/…/840ms)
//     串起来，最后一个批次（960ms）打完合账自己调 QCoreApplication::exit(码)，
//     main 里的 app.exec() 随即返回。鼠标键盘一根手指都不用动。
//   · 写盘只落在 QStandardPaths::writableLocation(QStandardPaths::TempLocation)
//     （qstandardpaths.h 第 29、48 行）下自建的 qt_storage_03/ 里；**不碰用户文档、
//     不碰配置目录、不写注册表、不读仓库里任何现成文件**，最后一个批次用
//     QDir::removeRecursively()（qdir.h 第 186 行）删干净并打印残留条目数（应为 0）。
//   · 不联网；不打印任何口令/密钥/token，JSON 样本全是本文件现造的字符串和字节。
//   · 每个批次都打可数的东西（类型表 7 行、8 个键的探针表、8 条非法样本、
//     若干次存盘尝试），最后一个批次把所有计数汇成一张对账表，对不上就打「对不上」，
//     并且**用退出码把话说给 CI 听**：对不上 N 行 → 进程返回 1（全对上返回 0）。
//   · 本机验证状态（诚实版）：
//       已做：moc 通过；g++ -fsyntax-only -std=c++17 -Wall -Wextra 通过（0 error
//             0 warning）；所有签名、枚举数值、行为断言的行号都在本机
//             C:\msys64\ucrt64\include\qt6（Qt 6.12.0）的头文件里逐条 grep/读出来过。
//       没做：**没有链接、没有运行**。本机跑 Qt 程序会缺符号直接 0xC0000139，
//             所以凡是「跑起来会打出什么」的推测都在正文里标了
//             「预期（本机未运行，需在能编译的机器上复核）」，一个字都没装作实测。
//       本机确实算过的（不需要 Qt 运行时，纯 C/C++ + 同一套 MinGW 工具链）：
//             2^53+1 与 2^53 是同一个 double、(long long)9007199254740993.0 ==
//             9007199254740992、2^53+2 活着、(0.1+0.2)==0.3 为假且值是
//             0.30000000000000004441、(int)3.9==3/(int)-3.9==-3、
//             printf("%g") 把 3.14159265358979 打成 3.14159、
//             以及用 iconv 量出的「温度上限」GBK 字节 CE C2 B6 C8 C9 CF CF DE。
//             这些数字在下面的代码注释里写死了，程序自己也会重算一遍。
//       唯一一句官方文档原文（「The list is sorted alphabetically.」）是从 doc.qt.io
//             抓到的句子，抓的时候没记下它属于哪一节，所以本讲只引用句子本身，
//             键序的最终结论交给【2】的输出，不当证据用。
// ============================================================================

#include <QCoreApplication>          // QtCore/qcoreapplication.h
#include <QObject>                   // QtCore/qobject.h：ConfigStore 的基类
#include <QString>                   // QtCore/qstring.h
#include <QStringList>               // QtCore/qstringlist.h
#include <QByteArray>                // QtCore/qbytearray.h：isValidUtf8 / toHex
#include <QByteArrayView>            // QtCore/qbytearrayview.h：fromJson 的入参类型
#include <QJsonValue>                // QtCore/qjsonvalue.h
#include <QJsonObject>               // QtCore/qjsonobject.h
#include <QJsonArray>                // QtCore/qjsonarray.h
#include <QJsonDocument>             // QtCore/qjsondocument.h
#include <QJsonParseError>           // QtCore/qjsonparseerror.h
#include <QCborValue>                // QtCore/qcborvalue.h：只做类型对照用
#include <QTimer>                    // QtCore/qtimer.h
#include <QDir>                      // QtCore/qdir.h
#include <QFile>                     // QtCore/qfile.h
#include <QFileInfo>                 // QtCore/qfileinfo.h
#include <QIODevice>                 // QtCore/qiodevice.h
#include <QSaveFile>                 // QtCore/qsavefile.h
#include <QStandardPaths>            // QtCore/qstandardpaths.h
#include <QtMath>                    // qRound 在 QtCore/qnumeric.h 第 570~585 行
#include <QDebug>                    // noquote()：QtCore/qdebug.h 第 130 行
#include <QtGlobal>                  // QT_VERSION_STR / Q_UNUSED / qsizetype

// ============================================================================
// 一、核对签名 —— 全部是本机 C:\msys64\ucrt64\include\qt6（Qt 6.12.0）里
//     逐行 grep/读出来的，行号可以直接对。先核对再写代码，是本课程的规矩。
// ============================================================================
//
// ── QtCore/qjsonvalue.h（全文件 363 行）─────────────────────────────────────
//   第 29 行   class Q_CORE_EXPORT QJsonValue
//   第 32~40 行 enum Type { Null=0x0, Bool=0x1, Double=0x2, String=0x3,
//                Array=0x4, Object=0x5, Undefined=0x80 };
//              ★ 注意没有 Integer。JSON 里所有数字在这层都是 Double。
//   第 42~49 行 using JsonFormat = QJsonDocument::JsonFormat;（Qt7 之前）
//              ★ 所以 QJsonValue::Compact 这种写法在 Qt6 里是**别名生效**，
//                但枚举值的老家在 QJsonDocument（第 58~61 行），写全最保险。
//   第 51 行   QJsonValue(Type = Null);              ← 默认构出来就是 Null
//   第 52~55 行 QJsonValue(bool) / (double) / (int) / (qint64)
//   第 56~57 行 QJsonValue(const QString&) / (QLatin1StringView)
//   第 58~61 行 #ifndef QT_NO_CAST_FROM_ASCII → QJsonValue(const char *s)
//                : QJsonValue(QString::fromUtf8(s)) {}
//              ★ const char* 走的是 fromUtf8，不是 fromLocal8Bit —— 这是 Qt6 的
//                统一态度；但如果你手里的字节是 GBK，这条转换一样是错的。
//   第 62~65 行 QJsonValue(const QJsonArray&) / (QJsonArray&&) /
//              (const QJsonObject&) / (QJsonObject&&)
//   第 72~77 行 移动构造与 swap 挂 QT_CORE_INLINE_SINCE(6, 12)
//   第 79~80 行 fromVariant / toVariant
//   第 82 行   static QJsonValue fromJson(QByteArrayView json,
//                                        QJsonParseError *error = nullptr);
//   第 84 行   QByteArray toJson(JsonFormat format = JsonFormat::Indented) const;
//   第 86 行   Type type() const;                    ← 唯一的真相来源
//   第 87~93 行 isNull/isBool/isDouble/isString/isArray/isObject/isUndefined
//   第 95~105 行 toBool(bool def=false) / toInt(int def=0) / toInteger(qint64 def=0)
//              / toDouble(double def=0) / toString() / toString(const QString&)
//              / toStringView(QAnyStringView def={}) / toArray() /
//              toArray(const QJsonArray&) / toObject() / toObject(const QJsonObject&)
//              ★ 十一行看完了：没有一处 bool *ok。想失败就只能靠 type() 或 contains()。
//   第 107~110 行 四个 const operator[]（QString / QStringView / QLatin1StringView
//                 / qsizetype）—— 返回的都是**值**，不是引用，所以不会改对象。
//   第 112~115 行 operator==/!= 写在 #if QT_CORE_REMOVED_SINCE(6, 8) 里；
//                 对应用代码来说这个宏恒等于 0（qtcoreexports.h 第 49~53 行：
//                 不是 Qt 自己编库时就 #define QT_CORE_REMOVED_SINCE(m,mi) 0），
//                 所以这段声明**根本不存在**——等号是第 120 行的
//                 Q_DECLARE_EQUALITY_COMPARABLE_NON_NOEXCEPT(QJsonValue) 给的。
//   第 123 行  QJsonValue(const void *) = delete;    ← 挡住 char* 误当 bool 的暗门
//   第 131 行  QCborValue value;                     ← 「JSON 是 CBOR 的皮」的铁证
//   第 134~135 行 两个 static_assert（尺寸与 QCborValue::Type 对齐）
//   第 141 行  class QJsonValueConstRef             ← 迭代器 value() 的类型
//   第 146 行  inline operator QJsonValue() const;  ← 隐式转回值
//   第 158~166 行 ConstRef 的同一套 to*/toString：
//              第 160~161 行 toInt  → concreteInt(*this, defaultValue, true )
//              第 162~163 行 toInteger → concreteInt(*this, defaultValue, false)
//              第 199 行  concreteInt(QJsonValueConstRef, qint64 def, bool clamp)
//              ★ 钳位与不钳位的差别全在这一个 bool 上，而且是 private 段的声明。
//   第 208/211 行 objectKey() / objectKeyView()     ← 迭代器能问「我是哪个键」
//   第 263 行  class QJsonValueRef : public QJsonValueConstRef
//   第 267~268 行 两个 operator=(const QJsonValue&) / (const QJsonValueRef&)
//   第 279 行  operator QJsonValue() const          ← Ref 也能转回值
//   第 291~295 行 Ref 的 to*/toString 全部转发给 ConstRef（所以读语义完全一样）
//
// ── QtCore/qjsonobject.h（全文件 422 行）───────────────────────────────────
//   第 12 行   #include <initializer_list>
//   第 33 行   class Q_CORE_EXPORT QJsonObject
//   第 38 行   QJsonObject(std::initializer_list<std::pair<QString, QJsonValue>> args);
//   第 63~67 行 keys() / size() / count() / length() / isEmpty()
//              ★ size() 返回 qsizetype（第 64 行），打印时别再当 int 用 %d。
//   第 69~77 行 value(QString/QStringView/QLatin1StringView) 三种 const 读，
//              operator[] 的 const 版返回 QJsonValue（第 70 行），
//              **非 const 版返回 QJsonValueRef（第 71 行）** —— 同一句法两种脾气，
//              这是「顺手写 o["新键"] 结果凭空多了一个键」的根源。
//   第 79~87 行 remove ×3 / take ×3 / contains ×3（第 81/86/87 行）
//   第 90~91 行 operator==/!=（同样在 REMOVED_SINCE 段里；等号由第 367 行的宏给）
//   第 118~120 行 iterator::key() / keyView() / value()   （value 返回 QJsonValueRef）
//   第 207 行  class const_iterator
//   第 232~235 行 const_iterator::key()/keyView()/value()/operator*
//              （value 返回 QJsonValueConstRef；第 216 行 typedef const QJsonValueConstRef reference）
//   第 298~303 行 const_key_value_iterator / key_value_iterator：
//              ★ 键的类型是 **QAnyStringView**（不是 QString），所以
//              asKeyValueRange() 里 kv.first 要 .toString() 才能拿 QString。
//   第 306~313 行 begin/cbegin/constBegin/end/cend/constEnd（begin() 是非 const 的，
//              会 detach()；const 版从第 307 行起）
//   第 314~322 行 keyValueBegin/keyValueEnd/constKeyValueBegin/constKeyValueEnd
//   第 325~328 行 asKeyValueRange() 的四种引用属性
//   第 336~345 行 find/constFind 的六种重载
//   第 339/346/347 行 insert(const QString&, const QJsonValue&) /
//              insert(QStringView, …) / insert(QLatin1StringView, …)
//   第 350~352 行 mapped_type=QJsonValue / key_type=QString / size_type=qsizetype
//   第 367~369 行 Q_DECLARE_EQUALITY_COMPARABLE_NON_NOEXCEPT(QJsonObject[, QJsonValue…])
//   第 391/392 行 keyAt(qsizetype) / valueAt(qsizetype) —— 在 private 段（第 386 行往后）
//   第 395 行  void removeAt(qsizetype i);（private）
//   第 398 行  QExplicitlySharedDataPointer<QCborContainerPrivate> o;
//
// ── QtCore/qjsonarray.h（全文件 340 行）────────────────────────────────────
//   第 18 行   class Q_CORE_EXPORT QJsonArray
//   第 21 行   QJsonArray(); 第 23 行 QJsonArray(std::initializer_list<QJsonValue>)
//   第 38~40 行 fromStringList / fromVariantList / toVariantList
//   第 42~48 行 size() / count() / isEmpty() / at(qsizetype) / first() / last()
//   第 50~55 行 prepend / append / removeAt / takeAt / removeFirst / removeLast
//   第 57~58 行 insert(qsizetype, const QJsonValue&) / replace(qsizetype, …)
//   第 60~62 行 contains(const QJsonValue&) / operator[](qsizetype) 两个版本
//   第 65 行   bool operator==(const QJsonArray &other) const;（等号实为第 310 行的宏）
//   第 261~268 行 begin/end 全家 + insert(iterator)/erase(iterator)（底层转成下标）
//   第 275~287 行 operator+ / operator+= / operator<< / push_back / push_front …
//   第 316 行  QExplicitlySharedDataPointer<QCborContainerPrivate> a;
//
// ── QtCore/qjsondocument.h（全文件只有 135 行，我读到底了）─────────────────
//   第 28 行   class Q_CORE_EXPORT QJsonDocument
//   第 37 行   QJsonDocument();          ← 空文档
//   第 38~41 行 四个 **explicit** 构造：只吃 QJsonObject / QJsonArray（各 const& 与 &&）
//   第 54~55 行 fromVariant / toVariant
//   第 57~61 行 enum JsonFormat { Indented, Compact };
//   第 73 行   static QJsonDocument fromJson(const QByteArray &json,
//                                            QJsonParseError *error = nullptr);
//   第 75 行   QByteArray toJson(JsonFormat format = JsonFormat::Indented) const;
//   第 77~82 行 isEmpty() / isArray() / isObject() / object() / array()
//              ★ 只有 object/array 两个出口，**顶层标量必须走 QJsonValue::fromJson**。
//   第 84~85 行 setObject / setArray
//   第 87~90 行 四个 operator[]（三个按键、一个按下标）—— 图省事的写法，语义含糊
//   第 95 行   bool isNull() const;
//   第 103 行  Q_DECLARE_EQUALITY_COMPARABLE(QJsonDocument)
//   第 105 行  private: explicit QJsonDocument(QCborValue data);
//   第 107 行  QJsonDocumentPrivate *d = nullptr;
//   ★ 整个文件里没有 isUndefined()。文档这一层不知道「未定义」是什么。
//
// ── QtCore/qjsonparseerror.h（全文件 45 行）────────────────────────────────
//   第 16 行   struct Q_CORE_EXPORT QJsonParseError     ← 注意是 struct，不是 QObject
//   第 18~34 行 enum ParseError：NoError=0（第 19 行）之后**没有显式数值**，
//              顺下来是 UnterminatedObject=1、MissingNameSeparator=2、
//              UnterminatedArray=3、MissingValueSeparator=4、IllegalValue=5、
//              TerminationByNumber=6、IllegalNumber=7、IllegalEscapeSequence=8、
//              IllegalUTF8String=9、UnterminatedString=10、MissingObject=11、
//              DeepNesting=12、DocumentTooLarge=13、GarbageAtEnd=14。
//              ★ 头文件只给了 NoError=0，其余是「隐式顺号」。所以数值语义**不在头里**，
//                本讲的【4】把名字与 int 一起打出来，让数字自己排队，别靠记忆。
//   第 36 行   QString errorString() const;
//   第 38~39 行 std::conditional_t<QT_VERSION_MAJOR < 7, int, qint64> offset = -1;
//              ★ 默认 **-1**：成功时这个字段没被承诺填成什么，别拿它当「长度」。
//   第 40 行   ParseError error = NoError;
//
// ── QtCore/6.12.0/QtCore/private/qjsonparser_p.h ──────────────────────────
//   第 28 行   class Parser（第 30~33 行 public：explicit Parser(QUtf8StringView json)
//              与 QCborValue parse(QJsonParseError*)）
//              ★ 入参类型是 QUtf8StringView：**UTF-8 是接口签名级别的合同**。
//   第 36 行   inline void eatBOM();（第 37 行 eatSpace、第 38 行 nextToken）
//              ★ 有 eatBOM，所以「BOM 一定让 fromJson 失败」这句老话在 Qt6 不成立；
//                但只此一个特例，别外推到「Qt 宽容」。
//   第 40~46 行 parseObject/parseArray/parseMember/parseString/
//              parseValueIntoContainer/parseValue/parseNumber
//              ★ 七个方法，没有 parseComment、没有 parseTrailingComma、
//                没有单引号分支、没有 NaN/Infinity 分支 —— 严格性的证据就是这个「没有」。
//   第 47~49 行 const char *head; const char *json; const char *end;
//   第 51 行   int nestingLevel;（DeepNesting 就是它管的）
//   第 52~53 行 QJsonParseError::ParseError lastError; 与容器指针
//
// ── QtCore/6.12.0/QtCore/private/qjsonwriter_p.h ──────────────────────────
//   第 27~32 行 class Writer：objectToJson / arrayToJson / valueToJson 三个静态函数，
//              签名都是 (…, QByteArray &json, int indent, bool compact = false)
//              ★ 缩进宽度是**形参**，头文件里没有「4」这个常量；所以别把缩进空格数
//                写进你的 diff 规则里。【6】直接把第二行的前导空格数打出来。
//
// ── QtCore/6.12.0/QtCore/private/qjson_p.h ────────────────────────────────
//   第 89~106 行 class ObjectIterator：第 93~95 行 ++/+= 都是 **it += 2 / it += 2*n**，
//              第 102 行 operator[](n) → it[n * 2]
//   第 189~195 行 indexHelper：`if (r.is_object) index = index * 2 + 1;`
//              ★ 这两处一起说明：对象在内存里是 [键,值,键,值,…] 的扁平数组，
//                键序 == 数组里的物理顺序，而「按第 i 个取」没有公开出口。
//
// ── QtCore/qcborvalue.h（对照用）──────────────────────────────────────────
//   第 71~86 行 enum Type : int { Integer = 0x00（第 72 行）, …, Double = 0x202（第 86 行）… }
//   第 96 行   Q_ENUM(Type)
//   第 158/169 行 isInteger() / isDouble()
//   第 190~195 行 toInteger/toDouble 的实现就一行三目：
//              `isInteger() ? value_helper() : isDouble() ? qint64(fp_helper()) : def`
//              ★ CBOR 层知道整数与浮点两种；JSON 层（QJsonValue::Type）只留了 Double。
// ── QtCore/6.12.0/QtCore/private/qcborvalue_p.h（皮底下的容器）─────────────
//   第 216~219 行 void append(qint64) → Element(value, QCborValue::Integer)
//              ★ 你自己构造的整数走 Integer；从 JSON **文本**读进来的整数走 Double。
//                同一条数据两条路，这就是【5】要现场问清楚的「往返掉不掉精度」。
//   第 238~246 行 append(QLatin1StringView)：先 isAscii 判断，ASCII 就 8 位存，
//              注释（第 243 行）写着「US-ASCII is a subset of UTF-8, so we can keep in 8-bit」
//   第 427 行   一句注释 `// doesn't apply to JSON` 挂在 findCborMapKey 上面
//              ★ CBOR 的 map 键查找与 JSON 的那条路是分开的，别把两边行为互相推。
//
// ── QtCore/qnumeric.h（qRound 的真面目）───────────────────────────────────
//   第 500~528 行 qCheckedFPConversionToInteger：第 508、517、523 行三个 Q_ASSERT，
//              第 527 行 `return Result(value);` —— 断言过了就直接截断，**不钳位**
//   第 530~551 行 namespace QRoundImpl：第 535~538 行 ARM64 走 __builtin_round，
//              第 541~544 行 SSE2 走 `d + copysign(0.5, d)`，第 546~549 行通用
//              `d >= 0.0 ? d + 0.5 : d - 0.5` —— 三条路都是「远离零」，不是银行家舍入
//   第 553~554 行 注释：「Like qRound, but have well-defined saturating behavior.
//              / NaN is not handled.」
//   第 557~567 行 QtPrivate::qSaturateRound（在 namespace QtPrivate 第 491~568 行里）
//   第 570~573 行 qRound(double) / 第 575~578 行 qRound(float)
//   第 580~583 行 qRound64(double) / 第 585~588 行 qRound64(float)
//
// ── 周边几个这讲要用的 ────────────────────────────────────────────────────
//   QtCore/qstring.h            第 345 行 arg(double, fieldWidth, format, precision, …)
//                               第 759 行 toUtf8()（[[nodiscard]]）
//                               第 782 行 static QString fromUtf8(QByteArrayView)
//                               第 411 行 indexOf(const QString&, from, cs)
//   QtCore/qbytearray.h         第 152~154 行 indexOf(char / QByteArrayView, from)
//                               第 163~164 行 contains(char / QByteArrayView)
//                               第 173 行 left(qsizetype)
//                               第 247~249 行 isValidUtf8()（[[nodiscard]]，转 QtPrivate::isValidUtf8）
//                               第 400 行 toHex(char separator='\0')
//   QtCore/qbytearrayalgorithms.h 第 48 行 isValidUtf8(QByteArrayView)
//   QtCore/qstringconverter_base.h 第 102~114 行 enum Encoding（Utf8/Utf16/Utf16LE/
//                               Utf16BE/Utf32/Utf32LE/Utf32BE/Latin1/System）—— 无 GBK
//   QtCore/qdebug.h             第 130 行 QDebug &noquote()
//   QtCore/qstandardpaths.h     第 21 行 enum StandardLocation、第 29 行 TempLocation、
//                               第 40 行 AppConfigLocation、第 48 行 writableLocation()
//   QtCore/qdir.h               第 135 行 filePath / 第 139 行 toNativeSeparators /
//                               第 166~167 行 entryList / 第 181、183 行 mkpath /
//                               第 186 行 removeRecursively / 第 189 行 exists /
//                               第 218 行 separator / 第 236 行 tempPath
//   QtCore/qfileinfo.h          第 87 行 exists / 第 166 行 size()
//   QtCore/qfile.h              第 180 行 static remove / 第 271~272 行 open（QFILE_MAYBE_NODISCARD）/
//                               第 278~279 行 resize（实例版与静态版）
//   QtCore/qsavefile.h          第 30~32 行 单参构造在 QT_CORE_REMOVED_SINCE(6,11) 里
//                               （对应用代码恒等于 0，等于**不存在**，所以必须用第 34 行那个
//                               带 parent 默认值的版本）/ 第 45 行 open（也是 MAYBE_NODISCARD）/
//                               第 46 行 commit / 第 48 行 cancelWriting / 第 50 行
//                               setDirectWriteFallback / 第 68 行 writeData /
//                               第 71 行 void close() override 在 **private:**（第 70 行）段里
//   QtCore/qiodevicebase.h      第 19~27 行 ReadOnly/WriteOnly/ReadWrite/Append/
//                               Truncate/Text/Unbuffered/NewOnly/ExistingOnly 九个位
//   QtCore/qiodevice.h          第 82 行 readAll / 第 100~102 行 write 三个重载
//   QtCore/qtimer.h             第 52、55 行老式 (msec, receiver, member)；
//                               第 64~71 行是 #else 分支里的函数对象版模板
//   QtCore/qtcoreexports.h      第 49~53 行：QT_CORE_REMOVED_SINCE(m,mi) 在非 Qt 库编译时
//                               被定义成 0 —— 这条决定了上面几处 #if 段对你是空的
// ============================================================================

// ============================================================================
// 二、小工具 —— 把「枚举数字」变成「能读的字」，再把「静默失败」变成「明确拒绝」
// ============================================================================

// 打印 bool 用中文，别用 1/0 让人猜。
static inline QString yes(bool v)
{
    return v ? QStringLiteral("是") : QStringLiteral("否");
}

// QJsonParseError::ParseError → 名字。枚举值本身在头文件里只写了 NoError=0，
// 所以这里把「名字 + 我当时数出来的序号」一起交出去，序号由【4】的打印核对。
static QString parseErrName(QJsonParseError::ParseError e)
{
    switch (e) {
    case QJsonParseError::NoError:               return QStringLiteral("NoError");
    case QJsonParseError::UnterminatedObject:    return QStringLiteral("UnterminatedObject");
    case QJsonParseError::MissingNameSeparator:  return QStringLiteral("MissingNameSeparator");
    case QJsonParseError::UnterminatedArray:     return QStringLiteral("UnterminatedArray");
    case QJsonParseError::MissingValueSeparator: return QStringLiteral("MissingValueSeparator");
    case QJsonParseError::IllegalValue:          return QStringLiteral("IllegalValue");
    case QJsonParseError::TerminationByNumber:   return QStringLiteral("TerminationByNumber");
    case QJsonParseError::IllegalNumber:         return QStringLiteral("IllegalNumber");
    case QJsonParseError::IllegalEscapeSequence: return QStringLiteral("IllegalEscapeSequence");
    case QJsonParseError::IllegalUTF8String:     return QStringLiteral("IllegalUTF8String");
    case QJsonParseError::UnterminatedString:    return QStringLiteral("UnterminatedString");
    case QJsonParseError::MissingObject:         return QStringLiteral("MissingObject");
    case QJsonParseError::DeepNesting:           return QStringLiteral("DeepNesting");
    case QJsonParseError::DocumentTooLarge:      return QStringLiteral("DocumentTooLarge");
    case QJsonParseError::GarbageAtEnd:          return QStringLiteral("GarbageAtEnd");
    }
    // ★ 这里故意不写 Q_UNREACHABLE()：这个枚举的取值来自**外部数据**（别人写的文件），
    //   不是我们自己控制的分支。兜底打印原始数字，比在 release 里掉进未定义行为有用。
    return QStringLiteral("未知枚举(数字%1)").arg(int(e));
}

// QJsonValue::Type → 名字 + 数值。第 7 个 Undefined 也列进来，因为它真的会出现。
static QString valueTypeName(QJsonValue::Type t)
{
    switch (t) {
    case QJsonValue::Null:      return QStringLiteral("Null");
    case QJsonValue::Bool:      return QStringLiteral("Bool");
    case QJsonValue::Double:    return QStringLiteral("Double");
    case QJsonValue::String:    return QStringLiteral("String");
    case QJsonValue::Array:     return QStringLiteral("Array");
    case QJsonValue::Object:    return QStringLiteral("Object");
    case QJsonValue::Undefined: return QStringLiteral("Undefined");
    }
    return QStringLiteral("未知类型(数字%1)").arg(int(t));
}

static QString typeOf(const QJsonValue &v)
{
    return QStringLiteral("%1(0x%2)").arg(valueTypeName(v.type()))
                                     .arg(QString::number(int(v.type()), 16));
}

// 字节头几个转成 hex，肉眼比对编码用。
static QString hexHead(const QByteArray &ba, qsizetype n)
{
    const QByteArray head = ba.left(n);                   // qbytearray.h 第 173 行
    return QString::fromLatin1(head.toHex(' ').toUpper());  // 第 400 行
}

// offset → 行列。offset 是字符偏移，这里按 \n 数出行列；offset<0 时直说。
static QString lineCol(const QByteArray &whole, qint64 offset)
{
    if (offset < 0)
        return QStringLiteral("没有位置（offset=%1，就是 qjsonparseerror.h 第 39 行的默认值）")
                   .arg(offset);
    qsizetype nl = 0;
    qsizetype lastBreak = -1;
    const qsizetype limit = qMin<qsizetype>(offset, whole.size());
    for (qsizetype i = 0; i < limit; ++i) {
        if (whole.at(i) == '\n') { ++nl; lastBreak = i; }
    }
    return QStringLiteral("第 %1 行第 %2 列（offset=%3）")
        .arg(nl + 1)
        .arg(offset - lastBreak)          // 1 基列号
        .arg(offset);
}

// 数子串出现次数。QByteArray 只有 indexOf（第 152~154 行），没有 count()，就自己走。
static int countOccurrences(const QByteArray &hay, const QByteArray &needle)
{
    if (needle.isEmpty())
        return 0;
    int n = 0;
    qsizetype from = 0;
    while (from <= hay.size()) {
        const qsizetype at = hay.indexOf(needle, from);
        if (at < 0)
            break;
        ++n;
        from = at + needle.size();
    }
    return n;
}

// ★★ 本讲真正的核心：把「静默默认值」包成「明确拒绝」★★
// 头文件能保证的只有「读错了不给 ok」（qjsonvalue.h 第 95~105 行），
// 那 ok 就得自己造。规则：先问存在（contains），再问类型（type），两个都过了才拷贝。
static bool jsonValueOf(const QJsonObject &obj, const QString &key,
                        QJsonValue::Type want, QJsonValue *out, QString *why)
{
    if (!obj.contains(key)) {                              // qjsonobject.h 第 81 行
        if (why)
            *why = QStringLiteral("没有这个键（contains() 为假）");
        return false;
    }
    const QJsonValue v = obj.value(key);                   // 第 69 行：const 读，返回**值**
    if (v.type() != want) {                                // 第 86 行：唯一真相
        if (why)
            *why = QStringLiteral("类型不对：要 %1，实际是 %2")
                       .arg(valueTypeName(want), valueTypeName(v.type()));
        return false;
    }
    if (out)
        *out = v;
    if (why)
        *why = QStringLiteral("命中");
    return true;
}

// 上面那个总闸的六个具体出口。注意每个都有 bool 返回值 —— 这是 Qt 不给的，我们自己加。
static bool jsonString(const QJsonObject &o, const QString &key, QString *out, QString *why)
{
    QJsonValue v;
    if (!jsonValueOf(o, key, QJsonValue::String, &v, why))
        return false;
    *out = v.toString();                                   // 类型已确认，这次不会静默
    return true;
}

static bool jsonDouble(const QJsonObject &o, const QString &key, double *out, QString *why)
{
    QJsonValue v;
    if (!jsonValueOf(o, key, QJsonValue::Double, &v, why))
        return false;
    *out = v.toDouble();
    return true;
}

// JSON 里没有整数类型，所以「要整数」= 要 Double 再取整。多问两句：够不够得着、是不是整值。
static bool jsonInt64(const QJsonObject &o, const QString &key, qint64 *out, QString *why)
{
    double d = 0.0;
    if (!jsonDouble(o, key, &d, why))
        return false;
    // ★ 范围必须在 double 这一侧问。把超出 qint64 的 double 直接 static_cast 成整数是
    //   **未定义行为**（不是「给你一个奇怪的数」）——别人手写一个 {"schemaVersion":1e99}
    //   就能把你送进 UB。所以比较用 2^63：qint64 的可用区间是 [-2^63, 2^63)，
    //   而 -2^63 与 2^63 这两个 2 的幂都能被 double 精确表示，边界不会因取整而松动。
    constexpr double kNeg63 = -9223372036854775808.0;      // -2^63 = qint64 的最小值
    constexpr double kPos63 =  9223372036854775808.0;      //  2^63 = qint64 最大值 +1（开区间边界）
    if (!(d >= kNeg63 && d < kPos63)) {                    // ★ 用取反写：NaN 时条件为假 → 也走这里
        if (why)
            *why = QStringLiteral("数值超出 qint64 能装的范围（或者是 NaN）：%1")
                       .arg(QString::asprintf("%.17g", d));
        return false;
    }
    if (d != double(qint64(d))) {                          // 到这儿才敢转，这一句是安全的
        if (why)
            *why = QStringLiteral("是 Double 但不是整值（%1）").arg(d, 0, 'f', 6);
        return false;
    }
    *out = qint64(d);
    if (why)
        *why = QStringLiteral("命中（按整数读出）");
    return true;
}

static bool jsonBool(const QJsonObject &o, const QString &key, bool *out, QString *why)
{
    QJsonValue v;
    if (!jsonValueOf(o, key, QJsonValue::Bool, &v, why))
        return false;
    *out = v.toBool();
    return true;
}

static bool jsonObj(const QJsonObject &o, const QString &key, QJsonObject *out, QString *why)
{
    QJsonValue v;
    if (!jsonValueOf(o, key, QJsonValue::Object, &v, why))
        return false;
    *out = v.toObject();
    return true;
}

static bool jsonArray(const QJsonObject &o, const QString &key, QJsonArray *out, QString *why)
{
    QJsonValue v;
    if (!jsonValueOf(o, key, QJsonValue::Array, &v, why))
        return false;
    *out = v.toArray();
    return true;
}

// 迭代器走一遍，拿到的键序。const_iterator::key() 在 qjsonobject.h 第 232 行。
static QStringList keyOrderOf(const QJsonObject &o)
{
    QStringList out;
    for (auto it = o.constBegin(); it != o.constEnd(); ++it)   // 第 307、311 行
        out << it.key();
    return out;
}

// 把 QString 的字节数说得能读：asprintf 用 %lld，别用 %d（qsizetype 是 64 位）。
static QString kb(qint64 bytes)
{
    return QString::asprintf("%lld B (%.2f KiB)",
                             static_cast<long long>(bytes),
                             double(bytes) / 1024.0);
}

// ============================================================================
// 三、合账计数器
// ============================================================================
// 全程单线程（QTimer 的槽都在主线程），所以普通 int 就够；threads/03 那边要跨线程
// 才需要 QAtomicInt。这里的每个字段都会在最后一个批次里被打印并互相对账。
// ★ 记账的规矩只有一条：**只有实际发生了才 +1，绝不按「我设计它该发生」+1。**
//   计数器一旦替预期说话，最后那张对账表就永远对得上——那它就没用了。
struct Tally {
    int kindsPrinted = 0;      // 【1】类型表打了几个枚举成员
    int wrongReads = 0;        // 【1】故意错读类型的次数（按实际调用计）
    int objectKeys = 0;        // 【1】样本对象的键数
    int arrayItems = 0;        // 【2】数组里最后有几件事
    int orderLists = 0;        // 【2】打了几份键序
    int orderMismatch = 0;     // 【2】其中与插入序不同的有几份
    int docsParsed = 0;        // 【3】error==NoError 的解析次数
    int docsRejected = 0;      // 【3】error!=NoError 的解析次数
    int docsTried = 0;         // 【3】解析总次数（= 上面两个之和，用来自证没有漏计）
    int lineOk = 0;            // 【3】NDJSON 逐行解析成功数
    int lineNonEmpty = 0;      // 【3】NDJSON 里非空的行数
    int samplesTotal = 0;      // 【4】样本总数
    int samplesIllegal = 0;    // 【4】其中按 RFC 8259 非法的条数
    int samplesRejected = 0;   // 【4】实际被解析器拒绝的条数
    int numberChecks = 0;      // 【5】打印的数字检验条数
    bool bigRoundTripKept = false;  // 【5】大整数往返是否保住了（跑出来才知道）
    int formatsPrinted = 0;    // 【6】打了 JsonFormat 的几种
    int encodingsChecked = 0;  // 【6】做了 UTF-8 合法性判定的次数
    int gbkRejected = 0;       // 【6】GBK 那份字节喂给 fromJson **实际**被拒的次数
    int probesTotal = 0;       // 【7】防御性访问器被问的键数
    int probesOk = 0;          // 【7】放行数
    int probesRefused = 0;     // 【7】拒绝数
    int savesTried = 0;        // 【8】存盘尝试
    int savesCommitted = 0;    // 【8】commit 成功
    int savesRefused = 0;      // 【8】写盘之前就被拒
    int loadsTried = 0;        // 【8】读盘尝试
    int loadsOk = 0;           // 【8】读盘成功（内存被换）
    int loadsFailed = 0;       // 【8】读盘失败（内存没动）
    int mismatch = 0;          // 【合账】对不上的行数 → 直接决定进程退出码
};
static Tally gTally;

// ★ 解析这件事，账只按 err.error 记，不按「我以为这条会成功」记。
//   写成函数是为了让每次解析都必须从这里过一遍——想漏计都得故意漏。
static void tallyParse(const QJsonParseError &err)
{
    ++gTally.docsTried;
    if (err.error == QJsonParseError::NoError)
        ++gTally.docsParsed;
    else
        ++gTally.docsRejected;
}

// ============================================================================
// 四、ConfigStore —— 把「读进来再写回去」做成一个有纪律的对象
// ============================================================================
// 这个类是本文件唯一的 Q_OBJECT 类（所以要 #include "03_json.moc"）。
// 三条纪律，都在代码里当着面执行：
//   A. loadFromFile：读 → 判 UTF-8 → 判解析错误 → 判 isObject → 判 schema →
//      只有全部通过（或迁移后通过），才把 m_data / m_version 换掉。
//   B. saveCandidate：先校验候选 → 才碰磁盘 → 写的是 QSaveFile（临时文件 + 原子替换，
//      脾气见第 2 讲，这里不重复）→ commit 之后**再回读校验一次** → 通过才换内存。
//   C. 迁移一次只跨一个版本；跨两版就拒绝，不写「v1 直接跳到 v3」的魔法。
// 它不发信号给界面（那属于第 4~9 模块），这里的 loaded/saved/refused 只服务于一件事：
// 让【7】【8】的打印能证明「拒绝的路径上什么都没发生」。
static const int kConfigSupportedVersion = 2;

class ConfigStore : public QObject
{
    Q_OBJECT
public:
    // qsavefile.h 那边同理，QSaveFile 的单参构造在应用代码里不可见；
    // 这里 QObject 的 parent 用默认值即可（qobject.h 的 QObject(QObject *parent = nullptr)）。
    explicit ConfigStore(const QString &path, QObject *parent = nullptr)
        : QObject(parent), m_path(path) {}

    QString path() const { return m_path; }
    int schemaVersion() const { return m_version; }       // 0 = 还没成功装过任何东西
    QJsonObject data() const { return m_data; }
    qint64 lastWrittenBytes() const { return m_lastWritten; }
    int failedLoadCount() const { return m_failedLoads; }

    // ── schema：本讲故意写得很少，重点是「有没有」而不是「写得多漂亮」 ──
    static QStringList requiredKeysFor(int version)
    {
        if (version == 1) {
            return {QStringLiteral("schemaVersion"),
                    QStringLiteral("appName"),
                    QStringLiteral("maxTemp")};
        }
        if (version == 2) {
            return {QStringLiteral("schemaVersion"),
                    QStringLiteral("displayName"),
                    QStringLiteral("limits")};
        }
        return {};                                          // 不认识的版本：没什么可要求的
    }

    // 不认识的键**不算错**，只统计。扔键是最常见的「升级即数据丢失」。
    static int countUnknownKeys(const QJsonObject &obj, int version)
    {
        const QStringList known = requiredKeysFor(version);
        int n = 0;
        for (const QString &k : obj.keys()) {               // qjsonobject.h 第 63 行
            if (!known.contains(k))
                ++n;
        }
        return n;
    }

    static bool validate(const QJsonObject &obj, QString *problem, int *versionOut)
    {
        QStringList bad;
        qint64 ver = 0;
        QString why;
        if (!jsonInt64(obj, QStringLiteral("schemaVersion"), &ver, &why))
            bad << QStringLiteral("schemaVersion 不可用：%1").arg(why);
        const int version = int(ver);
        if (versionOut)
            *versionOut = version;

        if (version < 1 || version > kConfigSupportedVersion) {
            bad << QStringLiteral("版本号 %1 不在 [1, %2] 里（读旧写新都只在这区间内做）")
                       .arg(version).arg(kConfigSupportedVersion);
        }

        QString w;
        if (version == 1) {
            QString appName;
            double maxTemp = 0.0;
            if (!jsonString(obj, QStringLiteral("appName"), &appName, &w))
                bad << QStringLiteral("appName：%1").arg(w);
            if (!jsonDouble(obj, QStringLiteral("maxTemp"), &maxTemp, &w))
                bad << QStringLiteral("maxTemp：%1").arg(w);
        } else if (version == 2) {
            QString display;
            QJsonObject limits;
            double tempMax = 0.0;
            if (!jsonString(obj, QStringLiteral("displayName"), &display, &w))
                bad << QStringLiteral("displayName：%1").arg(w);
            if (!jsonObj(obj, QStringLiteral("limits"), &limits, &w)) {
                bad << QStringLiteral("limits：%1").arg(w);
            } else if (!jsonDouble(limits, QStringLiteral("tempMax"), &tempMax, &w)) {
                bad << QStringLiteral("limits.tempMax：%1").arg(w);
            }
        }

        if (bad.isEmpty())
            return true;
        if (problem)
            *problem = QStringLiteral("共 %1 处不合 schema：%2")
                           .arg(bad.size()).arg(bad.join(QStringLiteral("；")));
        return false;
    }

    // ★ 一步一版。from==1 → 2；其他版本原样退回，让调用方去拒绝。
    static QJsonObject migrate(const QJsonObject &in, int from, int *toVersion)
    {
        int to = from;
        QJsonObject out = in;                              // 先整个抄一份：不认识的键原样带走
        if (from == 1) {
            QString w;
            double maxTemp = 0.0;
            QJsonObject limits;
            if (jsonDouble(in, QStringLiteral("maxTemp"), &maxTemp, &w))
                limits.insert(QStringLiteral("tempMax"), QJsonValue(maxTemp));  // 第 339 行
            out.insert(QStringLiteral("limits"), QJsonValue(limits));
            QString appName;
            if (jsonString(in, QStringLiteral("appName"), &appName, &w))
                out.insert(QStringLiteral("displayName"), QJsonValue(appName));
            out.remove(QStringLiteral("maxTemp"));           // 第 79 行
            out.remove(QStringLiteral("appName"));
            // profile 之类的未知键：out 是从 in 抄来的，没动它，就还在。
            to = 2;
        }
        out.insert(QStringLiteral("schemaVersion"), QJsonValue(double(to)));  // Double！第 35 行
        if (toVersion)
            *toVersion = to;
        return out;
    }

    // ── 读：全部校验通过之前，一个内存字段都不改 ──
    bool loadFromFile(QString *problem)
    {
        QString msg;
        QFile f(m_path);
        if (!f.open(QIODevice::ReadOnly)) {                 // qfile.h 第 271 行是 nodiscard，必须判
            msg = QStringLiteral("打不开：%1").arg(f.errorString());
            refuse(&msg, problem);
            return false;
        }
        const QByteArray raw = f.readAll();                 // qiodevice.h 第 82 行
        f.close();

        if (!raw.isValidUtf8()) {                           // qbytearray.h 第 247 行
            msg = QStringLiteral("字节不是合法 UTF-8（前 %1 字节 hex=%2）；"
                                 "Qt6 的 JSON 入口是 QUtf8StringView（qjsonparser_p.h 第 31 行），"
                                 "而且 QStringConverter 的枚举里没有 GBK（qstringconverter_base.h 第 102~114 行），"
                                 "得自己转换后再交给 fromJson")
                      .arg(qMin<qsizetype>(raw.size(), 16))
                      .arg(hexHead(raw, 16));
            refuse(&msg, problem);
            return false;
        }

        QJsonParseError err;                                // 默认 error=NoError / offset=-1
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
        if (err.error != QJsonParseError::NoError) {
            msg = QStringLiteral("解析失败：%1(%2)｜%3")
                      .arg(parseErrName(err.error)).arg(int(err.error)).arg(err.errorString());
            refuse(&msg, problem);
            return false;
        }
        if (!doc.isObject()) {                              // 第 79 行
            msg = QStringLiteral("顶层不是对象（isObject=%1 isArray=%2 isEmpty=%3 isNull=%4）")
                      .arg(yes(doc.isObject()), yes(doc.isArray()), yes(doc.isEmpty()))
                      .arg(yes(doc.isNull()));
            refuse(&msg, problem);
            return false;
        }

        QJsonObject candidate = doc.object();               // 第 81 行
        int version = 0;
        QString why;
        if (!validate(candidate, &why, &version)) {
            int toVersion = 0;
            const QJsonObject moved = migrate(candidate, version, &toVersion);
            QString why2;
            if (toVersion == version || !validate(moved, &why2, &version)) {
                msg = QStringLiteral("不合 schema，迁移也救不回来（原问题：%1；"
                                     "试图迁到 v%2 后：%3）").arg(why).arg(toVersion).arg(why2);
                refuse(&msg, problem);
                return false;
            }
            candidate = moved;                              // 只有迁移后合规才用迁移结果
        }

        // ★ 走到这里才允许动内存
        m_data = candidate;
        m_version = version;
        emit loaded(version, qint64(raw.size()));
        return true;
    }

    // ── 写：候选先过校验 → 才碰磁盘 → commit 后回读 → 才换内存 ──
    bool saveCandidate(const QJsonObject &candidate, QString *problem)
    {
        QString msg;
        QString why;
        int version = 0;
        if (!validate(candidate, &why, &version)) {         // A. 还没开文件就先拒绝
            msg = QStringLiteral("候选内容不合 schema，磁盘一个字节都没动：%1").arg(why);
            emit refused(msg);
            if (problem)
                *problem = msg;
            return false;
        }

        const QByteArray text = QJsonDocument(candidate)
                                .toJson(QJsonDocument::Compact);   // 第 75 行
        QSaveFile out(m_path);                              // qsavefile.h 第 34 行
        if (!out.open(QIODevice::WriteOnly)) {              // 第 45 行，同样是 nodiscard
            msg = QStringLiteral("QSaveFile 打不开：%1").arg(out.errorString());
            refuseWrite(&msg, problem);
            return false;
        }
        const qint64 written = out.write(text);             // qiodevice.h 第 102 行
        if (written != text.size()) {                       // 短写：不 commit，直接取消
            out.cancelWriting();                            // 第 48 行
            msg = QStringLiteral("只写了 %1 / %2 字节，已 cancelWriting")
                      .arg(written).arg(qint64(text.size()));
            refuseWrite(&msg, problem);
            return false;
        }
        if (!out.commit()) {                                // 第 46 行
            out.cancelWriting();
            msg = QStringLiteral("commit() 返回 false：%1").arg(out.errorString());
            refuseWrite(&msg, problem);
            return false;
        }

        // ★ commit 之后目标文件已经被替换了。这时唯一还能做的是「别让内存跟着变」。
        QFile back(m_path);
        if (!back.open(QIODevice::ReadOnly)) {
            msg = QStringLiteral("文件已经换上了，但回读打不开：%1 —— 内存里那份不换").arg(back.errorString());
            emit refused(msg);
            if (problem)
                *problem = msg;
            return false;
        }
        const QByteArray raw = back.readAll();
        back.close();
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()
                || !(doc.object() == candidate)) {          // 第 367 行给的等号
            msg = QStringLiteral("写下去了但回读对不上（error=%1，字节 %2/%3）—— 内存里那份不换")
                      .arg(parseErrName(err.error))
                      .arg(qint64(raw.size())).arg(qint64(text.size()));
            emit refused(msg);
            if (problem)
                *problem = msg;
            return false;
        }

        m_data = candidate;
        m_version = version;
        m_lastWritten = written;
        emit saved(written, version);
        return true;
    }

Q_SIGNALS:
    void loaded(int version, qint64 bytes);
    void saved(qint64 bytes, int version);
    void refused(const QString &reason);

private:
    // 两个小尾巴，只为把「失败计数 + 发信号 + 回填 problem」写一遍。
    void refuse(QString *msg, QString *problem)
    {
        ++m_failedLoads;
        if (problem)
            *problem = *msg;
        emit refused(*msg);
    }
    static void refuseWrite(QString *msg, QString *problem)
    {
        if (problem)
            *problem = *msg;
        // 注意：这里不发 refused，让调用点决定；本讲【8】直接看返回值与打印。
        Q_UNUSED(msg);
    }

    QString m_path;
    QJsonObject m_data;
    int m_version = 0;
    qint64 m_lastWritten = 0;
    int m_failedLoads = 0;
};

// ============================================================================
// 五、【1】QJsonValue 的 7 个枚举，和「读错了不吭声」这件事
// ============================================================================
static void stepValueTypes()
{
    qInfo().noquote() << QStringLiteral(
        "\n────【1】QJsonValue 是个变体：类型只有 7 个枚举成员，读错类型不吭声 ────");

    // 第 32~40 行的七个成员，逐个造一个值出来，把枚举数字与 isNull/isUndefined 打全。
    static const QJsonValue::Type kTypes[7] = {
        QJsonValue::Null, QJsonValue::Bool, QJsonValue::Double, QJsonValue::String,
        QJsonValue::Array, QJsonValue::Object, QJsonValue::Undefined
    };
    for (QJsonValue::Type t : kTypes) {
        const QJsonValue v(t);                              // 第 51 行：QJsonValue(Type = Null)
        ++gTally.kindsPrinted;
        qInfo().noquote() << QStringLiteral("  第 %1 个：%2  枚举值=0x%3  isNull=%4 isUndefined=%5 isDouble=%6")
            .arg(gTally.kindsPrinted)
            .arg(valueTypeName(t))
            .arg(QString::number(int(t), 16))
            .arg(yes(v.isNull()), yes(v.isUndefined()))     // 第 87、93 行
            .arg(yes(v.isDouble()));                        // 第 89 行
    }
    qInfo().noquote() << QStringLiteral(
        "  ↑ 7 行 = qjsonvalue.h 第 32~40 行的 7 个成员。前 6 个正好是 RFC 8259 的六种值，\n"
        "    Undefined 是第 7 个哨兵：它不是 JSON 里的值，是「这个位置根本没有值」。");

    // 六种值各造一个，装进一个对象，再用迭代器把「实际类型」打出来。
    QJsonArray innerArr;
    innerArr.append(QJsonValue(1));                         // qjsonarray.h 第 51 行
    innerArr.append(QJsonValue(2));
    QJsonObject innerObj;
    innerObj.insert(QStringLiteral("内"), QJsonValue(1));   // qjsonobject.h 第 339 行
    const QJsonObject sample{                               // 第 38 行的 initializer_list 构造
        {QStringLiteral("布尔"),   QJsonValue(true)},                       // 第 52 行
        {QStringLiteral("整数"),   QJsonValue(42)},                         // 第 54 行
        {QStringLiteral("小数"),   QJsonValue(3.5)},                        // 第 53 行
        {QStringLiteral("字符串"), QJsonValue(QStringLiteral("hello"))},    // 第 56 行
        {QStringLiteral("数组"),   QJsonValue(innerArr)},                   // 第 62 行
        {QStringLiteral("对象"),   QJsonValue(innerObj)},                   // 第 64 行
        {QStringLiteral("空"),     QJsonValue(QJsonValue::Null)},           // 第 51 行
    };
    gTally.objectKeys = int(sample.size());                 // size() 返回 qsizetype，第 64 行
    qInfo().noquote() << QStringLiteral("  样本对象 size=%1（键数），逐个看 type()：")
                             .arg(qint64(sample.size()));
    for (auto it = sample.constBegin(); it != sample.constEnd(); ++it) {
        const QJsonValue v = it.value();                    // const_iterator::value() 第 234 行
        qInfo().noquote() << QStringLiteral("    键「%1」 → %2").arg(it.key()).arg(typeOf(v));
    }

    // ★ 五次「故意读错」。每一次的返回值都打印出来 —— 但先说清楚：
    //   头文件能保证的只是**签名里没有 bool *ok**（第 95~105 行），
    //   回了什么是运行期才知道的事（本机没跑，所以这里只提问不作答）。
    qInfo().noquote() << QStringLiteral("  故意拿错类型去读（看它吭不吭声）：");
    const int r1 = sample.value(QStringLiteral("布尔")).toInt(-1);          // Bool → toInt
    ++gTally.wrongReads;
    qInfo().noquote() << QStringLiteral("    1) 拿「布尔」toInt(-1)      → %1（第 96 行只有一个 int 默认值形参）").arg(r1);
    const QString r2 = sample.value(QStringLiteral("整数"))
                           .toString(QStringLiteral("(读不到时给这个)"));   // Double → toString
    ++gTally.wrongReads;
    qInfo().noquote() << QStringLiteral("    2) 拿「整数」toString(默认)  → [%1]（第 100 行只有一个 defaultValue）").arg(r2);
    const double r3 = sample.value(QStringLiteral("字符串")).toDouble(-2.5); // String → toDouble
    ++gTally.wrongReads;
    qInfo().noquote() << QStringLiteral("    3) 拿「字符串」toDouble(-2.5) → %1（第 98 行）").arg(r3, 0, 'f', 1);
    const QJsonObject r4 = sample.value(QStringLiteral("数组")).toObject(); // Array → toObject
    ++gTally.wrongReads;
    qInfo().noquote() << QStringLiteral("    4) 拿「数组」toObject()      → size=%1（第 104 行，无默认值时给空对象")
                             .arg(qint64(r4.size()));
    const QJsonArray r5 = sample.value(QStringLiteral("对象")).toArray(
        QJsonArray{QJsonValue(9)});                                          // Object → toArray(默认)
    ++gTally.wrongReads;
    qInfo().noquote() << QStringLiteral("    5) 拿「对象」toArray(给一个%1个元素的默认) → size=%2（第 103 行）")
                             .arg(qint64(1)).arg(qint64(r5.size()));
    qInfo().noquote() << QStringLiteral(
        "  ↑ 五次调用，返回值一个比一个「像成功」。这就是为什么【7】要自己造闸门：\n"
        "    这一族的 11 个读函数（第 95~105 行）没有一个能把「失败」这件事传出来。");

    // Null 与 Undefined 的分工：一个「值就是空」，一个「压根没这个键」。
    const QJsonValue nullV = sample.value(QStringLiteral("空"));
    const QJsonValue missV = sample.value(QStringLiteral("这个键根本没有"));
    qInfo().noquote() << QStringLiteral("  值为 null 的键「空」  ：%1 contains=%2 isNull=%3")
                             .arg(typeOf(nullV))
                             .arg(yes(sample.contains(QStringLiteral("空"))))
                             .arg(yes(nullV.isNull()));
    qInfo().noquote() << QStringLiteral("  不存在的键「这个键根本没有」：%1 contains=%2 isUndefined=%3")
                             .arg(typeOf(missV))
                             .arg(yes(sample.contains(QStringLiteral("这个键根本没有"))))
                             .arg(yes(missV.isUndefined()));
    qInfo().noquote() << QStringLiteral("  两次 const value() 之后 size 还是 %1（第 70 行的 const 版返回**值**，不会建键）")
                             .arg(qint64(sample.size()));
    qInfo().noquote() << QStringLiteral("  【1】小结：类型表 %1 行、故意错读 %2 次、样本键数 %3。")
                             .arg(gTally.kindsPrinted).arg(gTally.wrongReads).arg(gTally.objectKeys);
}

// ============================================================================
// 六、【2】QJsonObject 的键序，和 QJsonArray 的下标
// ============================================================================
static void stepKeyOrder()
{
    qInfo().noquote() << QStringLiteral(
        "\n────【2】对象的键是集合、数组的元素是队列：把三份键序并排打出来 ────");

    const QStringList insertion = {
        QStringLiteral("zulu"), QStringLiteral("alpha"), QStringLiteral("mike"),
        QStringLiteral("Bravo"), QStringLiteral("01num"), QStringLiteral("温度")
    };
    QJsonObject o;
    for (const QString &k : insertion)
        o.insert(k, QJsonValue(k));                         // 第 339 行
    qInfo().noquote() << QStringLiteral("  插入序      ：%1").arg(insertion.join(QStringLiteral("、")));

    const QStringList viaKeys = o.keys();                   // 第 63 行
    const QStringList viaIter = keyOrderOf(o);              // 第 232 行 key()
    QStringList viaRange;
    for (const auto &kv : o.asKeyValueRange()) {            // 第 326 行（const &）
        viaRange << kv.first.toString();   // 键是 QAnyStringView（第 298 行），要 toString()
    }
    ++gTally.orderLists;
    qInfo().noquote() << QStringLiteral("  keys() 给的 ：%1").arg(viaKeys.join(QStringLiteral("、")));
    ++gTally.orderLists;
    qInfo().noquote() << QStringLiteral("  迭代器给的  ：%1").arg(viaIter.join(QStringLiteral("、")));
    ++gTally.orderLists;
    qInfo().noquote() << QStringLiteral("  asKeyValueRange：%1").arg(viaRange.join(QStringLiteral("、")));

    if (viaKeys != insertion) ++gTally.orderMismatch;
    if (viaIter != insertion) ++gTally.orderMismatch;
    if (viaRange != insertion) ++gTally.orderMismatch;
    qInfo().noquote() << QStringLiteral("  与插入序**不同**的有 %1 份（3 份全同 = 0）").arg(gTally.orderMismatch);

    qInfo().noquote() << QStringLiteral(
        "  ── 我观察到了什么、没观察到什么（这条最容易被人糊过去）──\n"
        "    · 上面这几行是**程序跑起来才有的输出**；本机跑不了 Qt，所以我一份都没「观察到」，\n"
        "      现在能说的只有头文件里的事实：keys()/size()/迭代器存在（第 63/64/306~313 行），\n"
        "      「第 i 个键」的 keyAt/valueAt 被关在 private 段（第 391/392 行）。\n"
        "    · 官方文档关于键序我抓到的只有一句「The list is sorted alphabetically.」，\n"
        "      抓到了句子、没抓到它挂在哪一节，所以**我不把它当证据**；\n"
        "      「跑起来是不是按字母序」这句话的状态是：预期（本机未运行，需在能编译的机器上复核）。\n"
        "    · 我能负责的说法只有一句：**别把键序当数据**。真要保证顺序，用 QJsonArray。");

    // 同一份内容、不同插入顺序：字节等不等？对象等不等？
    QJsonObject a, b;
    a.insert(QStringLiteral("x"), QJsonValue(1));
    a.insert(QStringLiteral("y"), QJsonValue(2));
    b.insert(QStringLiteral("y"), QJsonValue(2));           // 反着插
    b.insert(QStringLiteral("x"), QJsonValue(1));
    const QByteArray ja = QJsonDocument(a).toJson(QJsonDocument::Compact);
    const QByteArray jb = QJsonDocument(b).toJson(QJsonDocument::Compact);
    qInfo().noquote() << QStringLiteral("  先 x 后 y：%1").arg(QString::fromLatin1(ja));
    qInfo().noquote() << QStringLiteral("  先 y 后 x：%1").arg(QString::fromLatin1(jb));
    qInfo().noquote() << QStringLiteral("  两份字节相等？%1   两个对象 operator== ？%2（等号来自第 367 行的宏）")
                             .arg(yes(ja == jb)).arg(yes(a == b));
    qInfo().noquote() << QStringLiteral(
        "  ★ 如果这两行出现「对象相等但字节不等」，就把这句话抄进你的代码评审清单：\n"
        "    语义相等 ≠ 字节相等；拿 toJson() 的字节去做 diff、哈希、签名、缓存键，\n"
        "    迟早会因为一次插入顺序变化而误报。");

    // 数组：顺序就是内容，索引可问，这才是「有序」的那一个。
    QJsonArray arr;
    for (int i = 0; i < 5; ++i)
        arr.append(QJsonValue(i * 10));                     // 第 51 行
    arr.insert(0, QJsonValue(-1));                          // 第 57 行：往头上插
    gTally.arrayItems = int(arr.size());
    qInfo().noquote() << QStringLiteral("  QJsonArray 插了 5 个又在 0 号位插 1 个，size=%1：")
                             .arg(qint64(arr.size()));
    for (qsizetype i = 0; i < arr.size(); ++i)
        qInfo().noquote() << QStringLiteral("    at(%1)=%2  type=%3")
                                 .arg(qint64(i)).arg(arr.at(i).toInt()).arg(typeOf(arr.at(i)));
    qInfo().noquote() << QStringLiteral("  Compact 出来的字节：%1").arg(QString::fromLatin1(
        QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    const QJsonValue taken = arr.takeAt(0);                 // 第 53 行：取走并返回
    qInfo().noquote() << QStringLiteral("  takeAt(0) 拿走的是 %1，剩下 size=%2，头把 at(0)=%3")
                             .arg(taken.toInt()).arg(qint64(arr.size())).arg(arr.at(0).toInt());
    qInfo().noquote() << QStringLiteral("  【2】小结：键序样本 %1 份、与插入序不同的 %2 份、数组最后 %3 项。")
                             .arg(gTally.orderLists).arg(gTally.orderMismatch).arg(gTally.arrayItems);
}

// ============================================================================
// 七、【3】解析：两个入口、一个 error 结构、没有「多文档」
// ============================================================================
static void stepParseDocument()
{
    qInfo().noquote() << QStringLiteral(
        "\n────【3】fromJson 的两个入口，和 NDJSON 撞上的那一位（GarbageAtEnd）────");

    // 先证明 QJsonParseError 的默认值（qjsonparseerror.h 第 38~40 行）。
    QJsonParseError fresh;
    qInfo().noquote() << QStringLiteral("  刚建出来的 QJsonParseError：error=%1(%2) offset=%3")
                             .arg(parseErrName(fresh.error))
                             .arg(int(fresh.error))
                             .arg(qint64(fresh.offset));
    qInfo().noquote() << QStringLiteral(
        "  ↑ offset 的默认是 -1（第 39 行）。所以「成功时 offset 指向哪」头文件没承诺，\n"
        "    下面每次成功我也把 offset 打出来，但**不拿它当长度用**，也不预设它该是几。");

    // ① 顶层是对象
    const QByteArray objText = QStringLiteral(
        "{\"炉温\":1250,\"合金\":\"Inconel 718\",\"道次\":[1,2,3],\"到位\":true,\"备注\":null}")
        .toUtf8();                                          // qstring.h 第 759 行
    QJsonParseError e1;
    const QJsonDocument dObj = QJsonDocument::fromJson(objText, &e1);   // qjsondocument.h 第 73 行
    tallyParse(e1);
    qInfo().noquote() << QStringLiteral("  ① 顶层对象：error=%1(%2) offset=%3｜isObject=%4 isArray=%5 isEmpty=%6｜键数=%7")
                             .arg(parseErrName(e1.error)).arg(int(e1.error))
                             .arg(qint64(e1.offset))
                             .arg(yes(dObj.isObject()), yes(dObj.isArray()), yes(dObj.isEmpty()))
                             .arg(qint64(dObj.object().size()));

    // ② 顶层是数组（第 40~41 行的 explicit 构造与第 82 行的 array() 出口）
    const QByteArray arrText = QStringLiteral("[\"头\",\"中\",\"尾\"]").toUtf8();
    QJsonParseError e2;
    const QJsonDocument dArr = QJsonDocument::fromJson(arrText, &e2);
    tallyParse(e2);
    qInfo().noquote() << QStringLiteral("  ② 顶层数组：error=%1 size=%2 at(1)=%3 文档下标 dArr[qsizetype(2)]=%4")
                             .arg(parseErrName(e2.error))
                             .arg(qint64(dArr.array().size()))
                             .arg(dArr.array().at(1).toString())
                             .arg(dArr[qsizetype(2)].toString());   // 第 90 行的按下标 operator[]

    // ③ 顶层是标量：QJsonDocument 这条路走不通，因为 object()/array() 是仅有的出口
    const QByteArray scalar = QStringLiteral("42").toUtf8();
    QJsonParseError e3;
    const QJsonDocument dScalar = QJsonDocument::fromJson(scalar, &e3);
    tallyParse(e3);                     // ★ 顶层标量到底算成功还是失败，让 e3 自己说，不替它把账填好
    qInfo().noquote() << QStringLiteral("  ③ 顶层标量「42」交给 QJsonDocument：error=%1｜isNull=%2 isObject=%3 isArray=%4 isEmpty=%5")
                             .arg(parseErrName(e3.error))
                             .arg(yes(dScalar.isNull()), yes(dScalar.isObject()))
                             .arg(yes(dScalar.isArray()), yes(dScalar.isEmpty()));
    QJsonParseError e3b;
    const QJsonValue vScalar = QJsonValue::fromJson(QByteArrayView(scalar), &e3b);  // 第 82 行
    tallyParse(e3b);
    qInfo().noquote() << QStringLiteral("     同一段字节交给 QJsonValue::fromJson：error=%1｜type=%2｜toInt=%3")
                             .arg(parseErrName(e3b.error)).arg(typeOf(vScalar)).arg(vScalar.toInt());
    qInfo().noquote() << QStringLiteral(
        "  ↑ 两个入口的分工看这就能记住：文档级只有 object/array（第 81/82 行的两个出口），\n"
        "    标量与任何单值都用 QJsonValue::fromJson（入参还是 QByteArrayView，UTF-8 视图）。");

    // ④ 空字节流：也是「文件是空的」的现场。撞哪一档由打印决定，注释不替它作答。
    QJsonParseError e4;
    const QJsonDocument dEmpty = QJsonDocument::fromJson(QByteArray(), &e4);
    tallyParse(e4);
    qInfo().noquote() << QStringLiteral("  ④ 空输入：error=%1(%2) offset=%3｜%4｜isNull=%5 isEmpty=%6")
                             .arg(parseErrName(e4.error)).arg(int(e4.error))
                             .arg(qint64(e4.offset)).arg(e4.errorString())
                             .arg(yes(dEmpty.isNull()), yes(dEmpty.isEmpty()));

    // ⑤ NDJSON：整块喂一定撞墙，因为**没有多文档接口**（我把 qjsondocument.h 135 行读到底了）
    const QByteArray ndjson = QStringLiteral(
        "{\"id\":1,\"msg\":\"点火\"}\n"
        "{\"id\":2,\"msg\":\"升温\"}\n"
        "{\"id\":3,\"msg\":\"保温\"}\n")
        .toUtf8();
    QJsonParseError e5;
    QJsonDocument::fromJson(ndjson, &e5);            // 返回值不用：一次只给一个文档
    tallyParse(e5);
    qInfo().noquote() << QStringLiteral("  ⑤ NDJSON 整块喂：error=%1(%2)｜%3｜%4｜字节总长 %5")
                             .arg(parseErrName(e5.error)).arg(int(e5.error))
                             .arg(e5.errorString())
                             .arg(lineCol(ndjson, e5.offset))
                             .arg(qint64(ndjson.size()));
    qInfo().noquote() << QStringLiteral(
        "  ↑ 预期撞的是 GarbageAtEnd（qjsonparseerror.h 第 33 行，隐式序号 14）——"
        "「第一个文档读完了，后面还有东西」。这是**接口缺失**而不是解析器坏：\n"
        "  它一次只给你一个文档，所以 JSON Lines 只能自己切行。下面就是「自己切」：");

    const QList<QByteArray> lines = ndjson.split('\n');     // 按字节切，不依赖任何 codec
    qsizetype lineTotal = 0;
    qsizetype nonEmpty = 0;
    for (const QByteArray &rawLine : lines) {
        ++lineTotal;
        if (rawLine.trimmed().isEmpty()) {
            qInfo().noquote() << QStringLiteral("     第 %1 段：空白（split 出来的**最后一段就是空的**，跳过）")
                                     .arg(qint64(lineTotal));
            continue;
        }
        ++nonEmpty;
        QJsonParseError el;
        const QJsonDocument dl = QJsonDocument::fromJson(rawLine, &el);
        if (el.error == QJsonParseError::NoError && dl.isObject()) {
            ++gTally.lineOk;
            tallyParse(el);
            qInfo().noquote() << QStringLiteral("     第 %1 段：OK id=%2 msg=%3")
                                     .arg(qint64(lineTotal))
                                     .arg(dl.object().value(QStringLiteral("id")).toInt())
                                     .arg(dl.object().value(QStringLiteral("msg")).toString());
        } else {
            tallyParse(el);
            qInfo().noquote() << QStringLiteral("     第 %1 段：不要 —— %2")
                                     .arg(qint64(lineTotal))
                                     .arg(el.error == QJsonParseError::NoError
                                              ? QStringLiteral("解析本身过了，但顶层不是对象")
                                              : parseErrName(el.error));
        }
    }
    gTally.lineNonEmpty = int(nonEmpty);
    qInfo().noquote() << QStringLiteral("  ↑ split 给了 %1 段，其中非空 %2 段，解析成功 %3 段。")
                             .arg(qint64(lineTotal)).arg(gTally.lineNonEmpty).arg(gTally.lineOk);
    qInfo().noquote() << QStringLiteral("  【3】小结：解析总次数 %1 = 成功 %2 + 失败 %3（含 NDJSON 整块与空输入）。"
                                        "这三个数是同一本账的三个视角，对不上就是有人在替预期记账。")
                             .arg(gTally.docsTried).arg(gTally.docsParsed).arg(gTally.docsRejected);
}

// ============================================================================
// 八、【4】严格到什么程度：拿样本一条条喂给解析器
// ============================================================================
// 证据分两层：
//   硬证据（本机头文件）：private/qjsonparser_p.h 第 40~46 行只有七个 parse* 方法，
//     没有注释/尾逗号/单引号/裸键/NaN 的入口；第 36 行有一个 eatBOM()。
//   现场证据（跑出来看）：下面 10 条样本，每条打出「按 RFC 8259 该不该过」和
//     「Qt 实际给了什么」两栏，对不上的地方自己会跳出来。
static void stepStrictParser()
{
    qInfo().noquote() << QStringLiteral(
        "\n────【4】严格解析器：8 条非法样本 + 2 条合法但容易踩的样本 ────");

    struct Sample {
        QString name;
        QByteArray text;
        bool legalByRfc8259;      // 我们自己的判断，用来跟解析器的答案对账
        QString note;
    };
    const QList<Sample> samples = {
        {QStringLiteral("对象尾逗号"),
         QStringLiteral("{\"a\":1,}").toUtf8(), false,
         QStringLiteral("没有 parseTrailingComma，第 40~46 行的七个方法里没地方容它")},
        {QStringLiteral("数组尾逗号"),
         QStringLiteral("[1,2,]").toUtf8(), false,
         QStringLiteral("同上")},
        {QStringLiteral("行注释 //"),
         QStringLiteral("{\"a\":1} // 炉前备注").toUtf8(), false,
         QStringLiteral("JSON 从来不允许注释；即便只挂在后面也撞 GarbageAtEnd")},
        {QStringLiteral("块注释 /* */"),
         QStringLiteral("{\"a\"/*x*/:1}").toUtf8(), false,
         QStringLiteral("夹在成员中间，MissingNameSeparator 那一族")},
        {QStringLiteral("单引号"),
         QStringLiteral("{'a':1}").toUtf8(), false,
         QStringLiteral("parseString 只认双引号（第 43 行），没有单引号分支")},
        {QStringLiteral("裸键"),
         QStringLiteral("{a:1}").toUtf8(), false,
         QStringLiteral("parseMember 要的是字符串（第 42 行）")},
        {QStringLiteral("NaN"),
         QStringLiteral("[NaN]").toUtf8(), false,
         QStringLiteral("RFC 8259 非法；JavaScript 的 JSON.stringify 也产不出它")},
        {QStringLiteral("负无穷"),
         QStringLiteral("[-Infinity]").toUtf8(), false,
         QStringLiteral("同上，都属于「数字面量」里的非法拼写")},
        {QStringLiteral("重复键"),
         QStringLiteral("{\"a\":1,\"a\":2}").toUtf8(), true,
         QStringLiteral("合法！RFC 8259 只说「行为未约定」；QJsonObject 是键表，"
                      "第 339 行的 insert 会覆盖 → 只剩一个 a")},
        {QStringLiteral("带 BOM"),
         QByteArray("\xEF\xBB\xBF") + QStringLiteral("{\"a\":1}").toUtf8(), true,
         QStringLiteral("解析器有 eatBOM()（第 36 行），所以这一条**过**；"
                        "注意只有这一个特例，别外推成「Qt 宽容」")},
    };

    for (const Sample &s : samples) {
        ++gTally.samplesTotal;
        if (!s.legalByRfc8259)
            ++gTally.samplesIllegal;
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(s.text, &err);
        const bool passed = (err.error == QJsonParseError::NoError);
        if (!passed)
            ++gTally.samplesRejected;
        qInfo().noquote() << QStringLiteral("  样本「%1」%2｜输入 %3 字节 hex=%4")
                                 .arg(s.name)
                                 .arg(s.legalByRfc8259 ? QStringLiteral("(按规范合法)")
                                                      : QStringLiteral("(按规范非法)"))
                                 .arg(qint64(s.text.size()))
                                 .arg(hexHead(s.text, 12));
        qInfo().noquote() << QStringLiteral("      Qt 给的答案：%1（枚举数字 %2）%3")
                                 .arg(passed ? QStringLiteral("通过") : QStringLiteral("拒绝"))
                                 .arg(int(err.error))
                                 .arg(parseErrName(err.error));
        qInfo().noquote() << QStringLiteral("      位置与原文：%1 ｜ %2")
                                 .arg(passed ? QStringLiteral("成功路径不谈位置")
                                             : lineCol(s.text, err.offset))
                                 .arg(passed ? QStringLiteral("isObject=%1").arg(yes(doc.isObject()))
                                             : err.errorString());
        qInfo().noquote() << QStringLiteral("      备注：%1").arg(s.note);
        if (passed != s.legalByRfc8259)
            qInfo().noquote() << QStringLiteral("      ★ 与我们的规范判断不一致，重点看这一条。");
    }

    qInfo().noquote() << QStringLiteral("  【4】小结：样本 %1 条，其中按规范非法 %2 条，实际被拒 %3 条（%2 与 %3 相等才配叫「严格」）。")
                             .arg(gTally.samplesTotal).arg(gTally.samplesIllegal).arg(gTally.samplesRejected);
    qInfo().noquote() << QStringLiteral(
        "  三点要把住：\n"
        "    · 枚举的**数值**不在头文件里（第 19 行只写了 NoError=0），所以我把名字和数字一起打，\n"
        "      数字对不对让上面这张表自己排；背数字没意义，背名字才有意义。\n"
        "    · 「严格」的证据是 qjsonparser_p.h 里那些**不存在**的方法名，不是文档里的形容词。\n"
        "    · eatBOM() 让「BOM 一定毁掉 fromJson」这句老话在 Qt6 不成立 —— 顺带说一句：\n"
        "      第 2 讲结尾提醒过 BOM 会咬 QJsonDocument::fromJson，那是把 Qt5 的经验搬过来了；\n"
        "      本机头文件既然摆着 eatBOM()，就以头文件为准，别以讲义为准（我照此改了自己的说法）。");
}

// ============================================================================
// 九、【5】数字：JSON 层没有整数、CBOR 层有；2^53 是一道墙；qRound 不钳位
// ============================================================================
static void stepNumbers()
{
    qInfo().noquote() << QStringLiteral(
        "\n────【5】数字：整数在 JSON 这层不存在，2^53 是墙，qRound 越界不是钳位 ────");

    // 段一：两层的类型表对不上，这就是「皮」的证据
    qInfo().noquote() << QStringLiteral("  QJsonValue::Type（qjsonvalue.h 第 32~40 行）里的成员：");
    qInfo().noquote() << QStringLiteral("    Null=%1 Bool=%2 Double=%3 String=%4 Array=%5 Object=%6 Undefined=%7")
                             .arg(int(QJsonValue::Null)).arg(int(QJsonValue::Bool))
                             .arg(int(QJsonValue::Double)).arg(int(QJsonValue::String))
                             .arg(int(QJsonValue::Array)).arg(int(QJsonValue::Object))
                             .arg(int(QJsonValue::Undefined));
    ++gTally.numberChecks;
    qInfo().noquote() << QStringLiteral(
        "  ↑ 数一遍：**没有 Integer**。JSON 里所有数字在这层都归 Double（第 35 行）。\n"
        "    可底层 CBOR 是分的：QCborValue::Type（qcborvalue.h 第 71~86 行）里 "
        "Integer=0x%1（第 72 行）、Double=0x%2（第 86 行）。")
                             .arg(QString::number(int(QCborValue::Integer), 16).toUpper())
                             .arg(QString::number(int(QCborValue::Double), 16).toUpper());
    ++gTally.numberChecks;
    qInfo().noquote() << QStringLiteral("    同一个「9」，从 JSON 文本读进来是 Double；用 QJsonValue(qint64) 造出来，"
                                        "底下 append(qint64) 打的是 QCborValue::Integer"
                                        "（private/qcborvalue_p.h 第 216~219 行）→ 一条数据两条路，"
                                        "所以【5】下面要现场问一次「往返掉不掉」。");

    // 段二：纯 C++ 的算术墙。这一段下面打的 ①~⑤ **本机真的跑过**（纯 C++、不 link Qt，
    //   所以不受「跑不了 Qt 可执行文件」那条限制），所以它们配得上「实测」两个字。
    const double twoPow53 = 9007199254740992.0;             // 2^53，精确可表示
    const double plus1 = twoPow53 + 1.0;
    const double plus2 = twoPow53 + 2.0;
    ++gTally.numberChecks;
    qInfo().noquote() << QStringLiteral("  ① double(2^53+1) == double(2^53) ？%1   本机实测=真（1.0 加不进去了）")
                             .arg(yes(plus1 == twoPow53));
    ++gTally.numberChecks;
    qInfo().noquote() << QStringLiteral("  ② (long long)double(2^53+1) = %1   本机实测=9007199254740992（掉回偶数）")
                             .arg(static_cast<long long>(plus1));
    ++gTally.numberChecks;
    qInfo().noquote() << QStringLiteral("  ③ double(2^53+2) 转回整数 = %1   本机实测=9007199254740994（偶数活着）")
                             .arg(static_cast<long long>(plus2));
    ++gTally.numberChecks;
    qInfo().noquote() << QStringLiteral("  ④ (0.1+0.2)==0.3 ？%1   本机实测（纯 C++ 独立跑过）=假")
                             .arg(yes((0.1 + 0.2) == 0.3));
    ++gTally.numberChecks;
    // ★ 同一个数，17 位和 20 位打印出来是两句话。这一对数字我是用纯 C++（不 link Qt）
    //   在本机跑出来抄下来的，所以写「实测」；跑 Qt 才能知道的都写「预期」。
    qInfo().noquote() << QStringLiteral("     ④ 的两种打印：\"%.17g\"＝%1（够还原 double）｜"
                                        "\"%.20g\"＝%2（把二进制的全部真相倒出来）")
                             .arg(QString::asprintf("%.17g", 0.1 + 0.2))
                             .arg(QString::asprintf("%.20g", 0.1 + 0.2));
    ++gTally.numberChecks;
    qInfo().noquote() << QStringLiteral("  ⑤ 同一个 double 三种打印：arg 取 15 位小数＝%1｜printf(\"%g\")＝%2｜printf(\"%.17g\")＝%3 "
                                        "（→ 别拿默认格式化的数字串当协议）")
                             .arg(3.14159265358979, 0, 'f', 15)
                             .arg(QString::asprintf("%g", 3.14159265358979))
                             .arg(QString::asprintf("%.17g", 3.14159265358979));
    qInfo().noquote() << QStringLiteral(
        "     ⑤ 的本机实测（纯 C++）：%1 用 printf(\"%g\") 打出来是 3.14159（默认只有 6 位有效数字），"
        "用 \"%.17g\" 打出来是 3.14159265358979。同一个数，两个字符串，选哪个当协议字段就差在哪。")
                             .arg(3.14159265358979, 0, 'f', 15);

    // 段三：真正走一遍 Qt：文本里的 2^53+1 读进来是什么，写回去还是不是那几个数字
    const QByteArray bigText = QStringLiteral("{\"id\":9007199254740993,\"seq\":42}").toUtf8();
    QJsonParseError bigErr;
    const QJsonDocument bigDoc = QJsonDocument::fromJson(bigText, &bigErr);
    const QJsonObject bigObj = bigDoc.object();
    const QJsonValue idV = bigObj.value(QStringLiteral("id"));
    ++gTally.numberChecks;
    qInfo().noquote() << QStringLiteral("  文本 9007199254740993 读进来：error=%1｜type=%2｜toInteger=%3｜toDouble(17 位)=%4")
                             .arg(parseErrName(bigErr.error)).arg(typeOf(idV))
                             .arg(idV.toInteger())
                             .arg(QString::asprintf("%.17g", idV.toDouble()));
    const QByteArray bigBack = QJsonDocument(bigObj).toJson(QJsonDocument::Compact);
    gTally.bigRoundTripKept = (bigBack == bigText);
    ++gTally.numberChecks;
    qInfo().noquote() << QStringLiteral("  写回去（Compact）：%1｜与原字节完全相同？%2")
                             .arg(QString::fromLatin1(bigBack)).arg(yes(gTally.bigRoundTripKept));
    qInfo().noquote() << QStringLiteral(
        "  ↑ 这一问就是本讲最贵的一问。头文件能推的是：id 的 type 是 Double（第 35 行没有 Integer），"
        "而 double 存不下 2^53+1（段二墙上的 ①②），所以**预期（本机未运行，需在能编译的机器上复核）"
        "写回去会变成 …992**。如果你的 ID 是雪花号、纳秒时间戳、数据库自增 bigint，"
        "结论就一句话：**别把它当数字放进 JSON，放字符串**，或者把它定死在 2^53 以下。");

    // 自己构造的那一侧：QJsonValue(qint64)（第 55 行）
    const QJsonValue made(qint64(9007199254740993LL));
    ++gTally.numberChecks;
    qInfo().noquote() << QStringLiteral("  自己造的 QJsonValue(qint64(2^53+1))：type=%1｜toJson(Compact)=%2")
                             .arg(typeOf(made))
                             .arg(QString::fromLatin1(made.toJson(QJsonDocument::Compact)));
    qInfo().noquote() << QStringLiteral("  ↑ 与上面「读进来」那行对照：同一数字、两条路，"
                                        "type 或字节有没有差，跑出来见（预期：读进来是 Double，造出来走 Integer）。");

    // 段四：toInt 钳位 vs toInteger 不钳位（第 160~163 行，就差一个 bool）
    const QByteArray clampText = QStringLiteral("{\"big\":5000000000,\"small\":7}").toUtf8();
    const QJsonObject clampObj = QJsonDocument::fromJson(clampText).object();
    const QJsonValue bigV = clampObj.value(QStringLiteral("big"));
    ++gTally.numberChecks;
    qInfo().noquote() << QStringLiteral("  5000000000 这一份：toInt(-7)=%1｜toInteger(-7)=%2｜toDouble=%3")
                             .arg(bigV.toInt(-7)).arg(bigV.toInteger(-7))
                             .arg(bigV.toDouble(), 0, 'f', 0);
    ++gTally.numberChecks;
    qInfo().noquote() << QStringLiteral("  同一个对象里 small=7：toInt=%1 toInteger=%2（int 上限是 %3）")
                             .arg(clampObj.value(QStringLiteral("small")).toInt())
                             .arg(clampObj.value(QStringLiteral("small")).toInteger())
                             .arg(2147483647);
    qInfo().noquote() << QStringLiteral(
        "  ↑ 两行的差别就是第 160~163 行那个 clamp：toInt 走 concreteInt(…, true)，"
        "toInteger 走 concreteInt(…, false)（声明在第 199 行）。"
        "**要 64 位就用 toInteger**，别拿 toInt 读时间戳，钳位这件事是悄悄发生的。");

    // 段五：qRound 与 static_cast：方向不同，而且 qRound 越界不是钳位
    struct Case { double d; };
    const Case cases[] = { {0.5}, {-0.5}, {2.5}, {-2.5}, {3.9}, {-3.9}, {1.5}, {-1.5} };
    qInfo().noquote() << QStringLiteral("  截断 vs 四舍五入（第 530~551 行三条路都是「远离零」）：");
    for (const Case &c : cases) {
        ++gTally.numberChecks;
        qInfo().noquote() << QStringLiteral("    %1 ：static_cast<int>=%2   qRound=%3")
                                 .arg(c.d, 0, 'f', 1)
                                 .arg(static_cast<int>(c.d))
                                 .arg(qRound(c.d));
    }
    qInfo().noquote() << QStringLiteral(
        "  ↑ 本机实测的对照（纯 C++，同一套编译器，不 link Qt）：向零截断 (int)3.9=3、(int)-3.9=-3；\n"
        "    而 qRound 那三条路都是**先按符号偏移 0.5、再截断**：正数 d+0.5、负数 d-0.5\n"
        "    （SSE 那条写成了 d + copysign(0.5, d)，ARM64 直接用 __builtin_round——三条路的**结果**\n"
        "    我都用公式复现过：0.5→1、-0.5→-1、2.5→3、-2.5→-3、3.9→4、-3.9→-4、1.5→2、-1.5→-2）。\n"
        "    所以 -0.5 会被 qRound 打成 -1，不是 0；它也不是「四舍六入五成双」，-half-away-from-zero 才是它。");
    qInfo().noquote() << QStringLiteral(
        "  ★ 越界那一刀我**故意不在这里跑**：qRound(double) 在第 570~573 行是\n"
        "    qCheckedFPConversionToInteger<int>(QRoundImpl::qRound(d))，而后者在第 517/523 行\n"
        "    只有两个 Q_ASSERT，然后第 527 行直接 `return Result(value)` —— 断言只在 debug 拦你，\n"
        "    release 里 NDBSTRONG 越界就是未定义行为，**不是钳位**。会钳位的是第 557 行的\n"
        "    qSaturateRound，但它在 namespace QtPrivate（第 491~568 行）里，不是公开 API，\n"
        "    别在应用代码里调它。要安全就自己写 qBound 把 double 先夹进 int 的范围。");
    qInfo().noquote() << QStringLiteral("  【5】小结：数字检验打了 %1 行，大整数往返字节相同=%2。")
                             .arg(gTally.numberChecks).arg(yes(gTally.bigRoundTripKept));
}

// ============================================================================
// 十、【6】写出去：两种 JsonFormat、UTF-8 合同、转义，和 Windows 的 ANSI 陷阱
// ============================================================================
static void stepWriteText(const QString &workDir)
{
    qInfo().noquote() << QStringLiteral(
        "\n────【6】toJson(Indented) 与 Compact：字节、转义，和 GBK 的那记闷棍 ────");

    QJsonObject o;
    o.insert(QStringLiteral("设备"), QStringLiteral("三号炉"));        // 非 ASCII 键与值
    o.insert(QStringLiteral("温度上限"), QJsonValue(85));
    o.insert(QStringLiteral("emoji"), QJsonValue(QStringLiteral("热\U0001F525 火")));
    // 控制字符：制表符与 BEL（第 7 号）。用 QChar 造，避免源码里塞裸控制字节。
    o.insert(QStringLiteral("note"), QStringLiteral("制表\t完了") + QChar(0x0007));
    QJsonArray path;
    path.append(QJsonValue(QStringLiteral("A1")));
    path.append(QJsonValue(QStringLiteral("B2")));
    o.insert(QStringLiteral("通道"), QJsonValue(path));

    const QByteArray indented = QJsonDocument(o).toJson(QJsonDocument::Indented);
    const QByteArray compact = QJsonDocument(o).toJson(QJsonDocument::Compact);
    ++gTally.formatsPrinted;
    qInfo().noquote() << QStringLiteral("  Compact（%1）：%2")
                             .arg(kb(qint64(compact.size())), QString::fromLatin1(compact));
    ++gTally.formatsPrinted;
    qInfo().noquote() << QStringLiteral("  Indented 前 2 行（%1）：")
                             .arg(kb(qint64(indented.size())));
    const QList<QByteArray> indLines = indented.split('\n');
    for (qsizetype i = 0; i < qMin<qsizetype>(2, indLines.size()); ++i) {
        qsizetype lead = 0;
        while (lead < indLines.at(i).size() && indLines.at(i).at(lead) == ' ')
            ++lead;
        qInfo().noquote() << QStringLiteral("    第 %1 行：前导空格 %2 个 ｜ 内容 %3")
                                 .arg(i + 1).arg(qint64(lead))
                                 .arg(QString::fromLatin1(indLines.at(i)));
    }
    qInfo().noquote() << QStringLiteral(
        "  ↑ 缩进宽度是 private/qjsonwriter_p.h 第 30~32 行那个 `int indent` **形参**，"
        "头文件里没有「4」这个常量；所以第二行前导空格数几个是运行期的事，上面这行自己会数出来。\n"
        "    Indented 比 Compact 多花 %1 字节 —— 单条无所谓，十万条就是你日志的体积。")
                             .arg(qint64(indented.size() - compact.size()));

    // 转义自检：不说「一定会/一定不会」，只数三种特征各出现几次。
    const QByteArray wenDu = QStringLiteral("温度").toUtf8();       // E6 B8 A9 E5 BA A6（UTF-8 的固定结果）
    const QByteArray backslashU = QByteArray("\\u");                // 两个字符：反斜杠 + u
    const QByteArray backslashT = QByteArray("\\t");
    qInfo().noquote() << QStringLiteral("  Compact 字节头 24 个 hex = %1").arg(hexHead(compact, 24));
    ++gTally.encodingsChecked;
    qInfo().noquote() << QStringLiteral("  自检三问（数出来，不猜）：原始中文 UTF-8 字节出现 %1 次；"
                                        "「\\u」出现 %2 次；「\\t」出现 %3 次；BEL(0x07) 原字节出现 %4 次")
                             .arg(countOccurrences(compact, wenDu))
                             .arg(countOccurrences(compact, backslashU))
                             .arg(countOccurrences(compact, backslashT))
                             .arg(countOccurrences(compact, QByteArray(1, char(0x07))));
    qInfo().noquote() << QStringLiteral(
        "  ↑ 这三行合起来回答「Qt 写 JSON 时怎么对待非 ASCII 与控制字符」。"
        "第一问 >0 就说明没转义、原样 UTF-8；第二问 >0 就说明走了 \\uXXXX。"
        "**具体是哪一种，本机跑不了 → 预期（未在实机验证，需在能编译的机器上复核）**，"
        "但这条自检语句你可以原样搬进你的项目，跑一次就知道，不用问人。\n"
        "    另外「热\U0001F525 火」里那个火苗是 U+1F525，在 U+FFFF **之外**（第 1 辅助平面，"
        "从 U+10000 起），UTF-16 得用一对代理码元（surrogate pair）才装得下它，"
        "所以 QString 里它的 length() 是 %1，而不是 1 —— 数「字符」的时候，"
        "UTF-16 的 length 不等于字数。这条是 UTF-16 的性质，跟 Qt 版本无关。")
                             .arg(qint64(QStringLiteral("\U0001F525").length()));

    // UTF-8 合法性的合同：写出去的东西必须自己也是合法 UTF-8
    ++gTally.encodingsChecked;
    qInfo().noquote() << QStringLiteral("  写出来的两份 isValidUtf8()：Compact=%1 Indented=%2（qbytearray.h 第 247 行）")
                             .arg(yes(compact.isValidUtf8()), yes(indented.isValidUtf8()));

    // 落盘（二进制）→ 读回 → 与内存对象比一次
    const QString jsonPath = QDir(workDir).filePath(QStringLiteral("cfg.json"));   // qdir.h 第 135 行
    {
        QFile w(jsonPath);
        if (!w.open(QIODevice::WriteOnly | QIODevice::Truncate)) {   // 位定义 qiodevicebase.h 第 19~27 行
            qInfo().noquote() << QStringLiteral("  写盘失败：%1").arg(w.errorString());
        } else {
            const qint64 n = w.write(compact);
            w.close();
            qInfo().noquote() << QStringLiteral("  二进制写进 %1：%2 字节（期望 %3），QFileInfo::size=%4")
                                     .arg(QDir::toNativeSeparators(jsonPath))          // 第 139 行
                                     .arg(n).arg(qint64(compact.size()))
                                     .arg(QFileInfo(jsonPath).size());                // 第 166 行
            QFile r(jsonPath);
            const bool opened = r.open(QIODevice::ReadOnly);
            const QByteArray back = opened ? r.readAll() : QByteArray();
            r.close();
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(back, &err);
            qInfo().noquote() << QStringLiteral("  读回来：open=%1 error=%2 对象相等？%3")
                                     .arg(yes(opened)).arg(parseErrName(err.error))
                                     .arg(yes(doc.object() == o));
        }
    }

    // 同一个内容用 Text 模式再写一次：Windows 上 \n 变 \r\n，字节数会变
    {
        const QString txtPath = QDir(workDir).filePath(QStringLiteral("cfg_text.json"));
        QFile w(txtPath);
        if (w.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            w.write(indented);
            w.close();
            const qint64 sizeText = QFileInfo(txtPath).size();
            qInfo().noquote() << QStringLiteral("  Text 模式写 Indented：磁盘 %1 字节 vs 内存 %2 字节"
                                                "（差 %3；Text 位改的是换行翻译，第 24 行）")
                                     .arg(sizeText).arg(qint64(indented.size()))
                                     .arg(sizeText - qint64(indented.size()));
            QFile r(txtPath);
            if (r.open(QIODevice::ReadOnly)) {          // 二进制读回来再解析
                const QByteArray raw = r.readAll();
                r.close();
                QJsonParseError err;
                QJsonDocument::fromJson(raw, &err);
                ++gTally.encodingsChecked;
                qInfo().noquote() << QStringLiteral("  这份带 \\r\\n 的文件用二进制读回来解析：%1｜isValidUtf8=%2")
                                         .arg(parseErrName(err.error)).arg(yes(raw.isValidUtf8()));
            }
        }
    }

    // Windows 的经典闷棍：记事本「另存为 ANSI」= GBK
    // 本机 iconv 实测：「温度上限」的 GBK 字节是 CE C2 B6 C8 C9 CF CF DE；
    // 这里用其中「温度」两字（CE C2 B6 C8）拼一个合法的 GBK JSON：{"温度":85}
    static const char gbkBytes[] = {
        '\x7B', '\x22', '\xCE', '\xC2', '\xB6', '\xC8', '\x22', '\x3A', '\x38', '\x35', '\x7D'
    };
    const QByteArray gbk(gbkBytes, int(sizeof(gbkBytes)));
    ++gTally.encodingsChecked;
    qInfo().noquote() << QStringLiteral("  GBK 版 {\"温度\":85}：%1 字节 hex=%2｜isValidUtf8()=%3")
                             .arg(qint64(gbk.size())).arg(hexHead(gbk, 16))
                             .arg(yes(gbk.isValidUtf8()));
    QJsonParseError gbkErr;
    const QJsonDocument gbkDoc = QJsonDocument::fromJson(gbk, &gbkErr);
    // ★ 单独记账，不混进【3】那本：【3】的账要说清「【3】里那五次解析各给了什么答案」，
    //   别的批次的失败掺进去，那张对账表就没法逐条追了。
    if (gbkErr.error != QJsonParseError::NoError)
        ++gTally.gbkRejected;
    qInfo().noquote() << QStringLiteral("     直接喂给 fromJson：%1(%2)｜%3｜%4｜回来的文档 isObject=%5 isEmpty=%6")
                             .arg(parseErrName(gbkErr.error)).arg(int(gbkErr.error))
                             .arg(gbkErr.errorString())
                             .arg(lineCol(gbk, gbkErr.offset))
                             .arg(yes(gbkDoc.isObject()), yes(gbkDoc.isEmpty()));
    qInfo().noquote() << QStringLiteral(
        "  ↑ 预期（本机未运行）：报错，而且**位置在字符串里面**（IllegalUTF8String 是第 28 行那一个，"
        "序号 9）。CE C2 为什么不是合法 UTF-8：0xCE=110xxxxx 表示两字节序列的头，"
        "跟它必须落在 10xxxxxx；而 0xC2=11000010 是另一个「头字节」，所以这一对直接判死。"
        "这条判定不依赖 Qt，是 UTF-8 自己的规则。\n"
        "    麻烦的是 Qt6 里没有 GBK 这个编码名可以选：QStringConverter::Encoding 的枚举"
        "（qstringconverter_base.h 第 102~114 行）只有 Utf8/Utf16/Utf16LE/Utf16BE/"
        "Utf32/Utf32LE/Utf32BE/Latin1/System，**没有 GBK/Big5/Shift-JIS**。"
        "所以「先转成 UTF-8 再交给 fromJson」这一步要么用 System 编码（第 113 行，"
        "跟着平台走，Windows 中文平台上就是 GBK —— 但**别把它写进跨平台协议**），"
        "要么自己在入口把文本转成 UTF-8 存起来。第 2 讲拆过 QTextStream::setCodec() 在 Qt6 已删，"
        "这里的坑是同一件事的另一面：不是 Qt 变笨了，是编码这条路只剩一条桥。");
    qInfo().noquote() << QStringLiteral("  【6】小结：格式打了 %1 份，编码/合法性判定做了 %2 次，"
                                        "GBK 那份被 fromJson 实际拒了 %3 次（0 次也别慌，把上面那行 hex 记下来回来改这一批的说法）。")
                             .arg(gTally.formatsPrinted).arg(gTally.encodingsChecked).arg(gTally.gbkRejected);
}

// ============================================================================
// 十一、【7】自己造闸门：带 bool 返回值的访问器 + schemaVersion 迁移
// ============================================================================
static void stepDefensiveAccess()
{
    qInfo().noquote() << QStringLiteral(
        "\n────【7】schema 是自己造的：8 个键逐个探针，再走一次一步的版本迁移 ────");

    QJsonObject cfg;
    cfg.insert(QStringLiteral("schemaVersion"), QJsonValue(1.0));              // Double，命中
    cfg.insert(QStringLiteral("appName"), QJsonValue(QStringLiteral("老炉子")));// String，命中
    cfg.insert(QStringLiteral("maxTemp"), QJsonValue(QStringLiteral("85")));   // ★ 类型错了：字符串
    cfg.insert(QStringLiteral("flags"), QJsonValue(QJsonArray()));             // Array，命中
    cfg.insert(QStringLiteral("limits"), QJsonValue(QJsonObject()));           // Object，命中
    cfg.insert(QStringLiteral("nothing"), QJsonValue(QJsonValue::Null));       // ★ Null
    cfg.insert(QStringLiteral("ratio"), QJsonValue(0.5));                      // Double，命中
    // 第八问：这个键根本不插，专门看看「不存在」和「值为 null」在闸门下是不是两种回答

    struct Probe {
        QString key;
        QJsonValue::Type want;
    };
    const QList<Probe> probes = {
        {QStringLiteral("schemaVersion"), QJsonValue::Double},
        {QStringLiteral("appName"),       QJsonValue::String},
        {QStringLiteral("maxTemp"),       QJsonValue::Double},
        {QStringLiteral("flags"),         QJsonValue::Array},
        {QStringLiteral("limits"),        QJsonValue::Object},
        {QStringLiteral("nothing"),       QJsonValue::String},
        {QStringLiteral("ratio"),         QJsonValue::Double},
        {QStringLiteral("这个键不存"),     QJsonValue::String},
    };
    qInfo().noquote() << QStringLiteral("  闸门（jsonValueOf：先 contains 再 type）逐个问：");
    for (const Probe &p : probes) {
        ++gTally.probesTotal;
        QJsonValue got;
        QString why;
        const bool ok = jsonValueOf(cfg, p.key, p.want, &got, &why);
        if (ok) ++gTally.probesOk; else ++gTally.probesRefused;
        qInfo().noquote() << QStringLiteral("    %1 键「%2」要 %3｜实际 %4｜%5")
                                 .arg(ok ? QStringLiteral("放行") : QStringLiteral("拒绝"))
                                 .arg(p.key).arg(valueTypeName(p.want))
                                 .arg(typeOf(cfg.value(p.key)))
                                 .arg(why);
    }
    qInfo().noquote() << QStringLiteral("  ↑ 放行 %1、拒绝 %2、总数 %3（%1+%2 必须等于 %3）")
                             .arg(gTally.probesOk).arg(gTally.probesRefused).arg(gTally.probesTotal);

    // 同一件事的两种写法并排：Qt 的静默版 vs 我们的拒绝版
    const int naive = cfg.value(QStringLiteral("maxTemp")).toInt(-1);
    qint64 guarded = 0;
    QString gWhy;
    const bool guardedOk = jsonInt64(cfg, QStringLiteral("maxTemp"), &guarded, &gWhy);
    qInfo().noquote() << QStringLiteral("  「maxTemp」是字符串 \"85\"：toInt(-1) 给了 %1（数字，看不出是失败），"
                                        "我们的 jsonInt64 给了 ok=%2｜%3")
                             .arg(naive).arg(yes(guardedOk)).arg(gWhy);
    qInfo().noquote() << QStringLiteral(
        "  ★ 这就是本讲要的「bool-ok 模式」：Qt 不给你第二个返回值（qjsonvalue.h 第 95~105 行），\n"
        "    你必须在**自己**的函数里把它补上，并且让调用方拿到 false 时无路可走。\n"
        "    补法只有两问：contains()（第 81 行）与 type()（第 86 行）；剩下的都是纪律。");

    // 纪律里最容易漏的一条：范围。别人手写一份 1e99，够不着的类型检查（它确实是 Double），
    // 但把它往 qint64 里塞的那一下就是未定义行为。所以闸门的第三问永远是范围。
    {
        const QJsonObject huge = QJsonDocument::fromJson(QStringLiteral(
            "{\"schemaVersion\":1e99,\"appName\":\"炸\",\"maxTemp\":1}").toUtf8()).object();
        QString hugeWhy;
        int hugeVer = 0;
        const bool hugeOk = ConfigStore::validate(huge, &hugeWhy, &hugeVer);
        qInfo().noquote() << QStringLiteral("  有人把 schemaVersion 写成 1e99：validate=%1｜%2")
                                 .arg(yes(hugeOk)).arg(hugeWhy);
        qInfo().noquote() << QStringLiteral(
            "  ↑ 它**是** Double，类型关过得去；拦住它的是范围关（jsonInt64 里那两句 constexpr\n"
            "    边界）。少了这一关，程序不是报错，是 UB——UB 的意思是什么都可能，包括「看着对」。\n"
            "    顺手记一句：1e99 这种写法 JSON 是允许的（数字面量支持指数），所以它一定会进来。");
    }

    // 非 const 的 operator[] 会建键：先记 size，再摸一下，再看 size
    const qsizetype sizeBefore = cfg.size();
    cfg[QStringLiteral("凭空多出来的键")] = QJsonValue(1);      // 第 71 行：返回 QJsonValueRef
    qInfo().noquote() << QStringLiteral("  非 const 的 cfg[\"凭空多出来的键\"] = …：size 从 %1 变成 %2｜contains=%3")
                             .arg(qint64(sizeBefore)).arg(qint64(cfg.size()))
                             .arg(yes(cfg.contains(QStringLiteral("凭空多出来的键"))));
    qInfo().noquote() << QStringLiteral(
        "  ↑ const 版（第 70 行）返回的是**值**，读不存在的键不会建键；非 const 版（第 71 行）"
        "返回的是**可写引用**，赋值就要有个位置放，于是键就多了。\n"
        "    预期（本机未运行）：size +1。这条一旦在你的代码里发生，症状是「配置文件里莫名多出一堆"
        "空键」，而写代码的人完全不知道自己点了那颗扳机 —— 记法很简单：**读就用 value()/const，"
        "写才用 operator[]**。顺手把 cfg 变回干净：");
    cfg.remove(QStringLiteral("凭空多出来的键"));               // 第 79 行
    qInfo().noquote() << QStringLiteral("    remove 之后 size=%1").arg(qint64(cfg.size()));

    // ── schemaVersion：校验、迁移、再校验 ──
    const QByteArray v1Text = QStringLiteral(
        "{\"schemaVersion\":1,\"appName\":\"老炉子\",\"maxTemp\":85,"
        "\"profile\":{\"当班\":\"夜班\"}}").toUtf8();
    const QJsonObject v1 = QJsonDocument::fromJson(v1Text).object();
    QString why1;
    int ver1 = 0;
    const bool v1ok = ConfigStore::validate(v1, &why1, &ver1);
    qInfo().noquote() << QStringLiteral("  旧文件（v1，还带一个我们不认识的 profile）：validate=%1｜读出的版本=%2｜不认识的键 %3 个")
                             .arg(yes(v1ok)).arg(ver1)
                             .arg(ConfigStore::countUnknownKeys(v1, ver1));
    if (!v1ok)
        qInfo().noquote() << QStringLiteral("    原因：%1").arg(why1);

    int toVer = 0;
    const QJsonObject v2 = ConfigStore::migrate(v1, ver1, &toVer);
    QString why2;
    int ver2 = 0;
    const bool v2ok = ConfigStore::validate(v2, &why2, &ver2);
    qInfo().noquote() << QStringLiteral("  migrate(%1 → %2)：键数从 %3 变 %4，validate=%5｜不认识的键还剩 %6 个")
                             .arg(ver1).arg(toVer)
                             .arg(qint64(v1.size())).arg(qint64(v2.size()))
                             .arg(yes(v2ok)).arg(ConfigStore::countUnknownKeys(v2, toVer));
    qInfo().noquote() << QStringLiteral("    迁移前的键：%1").arg(keyOrderOf(v1).join(QStringLiteral("、")));
    qInfo().noquote() << QStringLiteral("    迁移后的键：%1").arg(keyOrderOf(v2).join(QStringLiteral("、")));
    qInfo().noquote() << QStringLiteral("    profile 还在吗？%1 —— **不认识的键一个都别扔**，"
                                        "那是别人的功能在读的东西").arg(yes(v2.contains(QStringLiteral("profile"))));
    qInfo().noquote() << QStringLiteral("    新键 limits 的内容：%1").arg(QString::fromLatin1(
        QJsonDocument(v2.value(QStringLiteral("limits")).toObject()).toJson(QJsonDocument::Compact)));

    // 不合 schema 的东西长什么样：让 validate 把问题一次列全
    const QJsonObject broken = QJsonDocument::fromJson(QStringLiteral(
        "{\"schemaVersion\":2,\"limits\":{\"tempMax\":\"很高\"},\"旧字段\":1}").toUtf8()).object();
    QString whyB;
    int verB = 0;
    const bool brokenOk = ConfigStore::validate(broken, &whyB, &verB);
    qInfo().noquote() << QStringLiteral("  故意给一份坏的（v2 缺 displayName、limits.tempMax 是字符串）：validate=%1｜%2")
                             .arg(yes(brokenOk)).arg(whyB);
    qInfo().noquote() << QStringLiteral(
        "  ★ 注意上面这行的问题数：**两个都报出来了**。一次只报第一个错是界面友好，"
        "一次报全是排查友好；配置文件出错的时候人不在现场，要的是后者。\n"
        "    迁移的规矩也只有一条：**一次只跨一个版本**。v1 直接跳 v3 的代码，三个月后没人敢改。");
    qInfo().noquote() << QStringLiteral("  【7】小结：探针 %1 个（放行 %2 / 拒绝 %3），迁移 %4 → %5。")
                             .arg(gTally.probesTotal).arg(gTally.probesOk).arg(gTally.probesRefused)
                             .arg(ver1).arg(toVer);
}

// ============================================================================
// 十二、【8】存盘纪律：候选 → QSaveFile → commit → 回读 → 才换内存
// ============================================================================
static void stepAtomicSave(const QString &workDir)
{
    qInfo().noquote() << QStringLiteral(
        "\n────【8】原子替换与「读失败不许动内存」：一次升级、一次拒绝、一次截断 ────");

    const QString cfgPath = QDir(workDir).filePath(QStringLiteral("demo_cfg.json"));

    // 先造一个「别人给的」旧文件：v1，直接手写文本，不经 ConfigStore
    {
        QFile seed(cfgPath);
        if (!seed.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qInfo().noquote() << QStringLiteral("  造旧文件就失败了：%1").arg(seed.errorString());
            return;
        }
        seed.write(QStringLiteral("{\"schemaVersion\":1,\"appName\":\"老炉子\",\"maxTemp\":85}").toUtf8());
        seed.close();
    }
    qInfo().noquote() << QStringLiteral("  旧文件就位：%1｜%2 字节")
                             .arg(QDir::toNativeSeparators(cfgPath)).arg(QFileInfo(cfgPath).size());

    ConfigStore store(cfgPath);
    QObject::connect(&store, &ConfigStore::loaded, [](int version, qint64 bytes) {
        qInfo().noquote() << QStringLiteral("    [信号 loaded] 版本 v%1，读了 %2 字节")
                                 .arg(version).arg(bytes);
    });
    QObject::connect(&store, &ConfigStore::saved, [](qint64 bytes, int version) {
        qInfo().noquote() << QStringLiteral("    [信号 saved] 写出 %1 字节，内存版本升到 v%2")
                                 .arg(bytes).arg(version);
    });
    QObject::connect(&store, &ConfigStore::refused, [](const QString &reason) {
        qInfo().noquote() << QStringLiteral("    [信号 refused] %1").arg(reason);
    });

    // ① 读旧文件：v1 合法，直接装上（注意 loadFromFile 里「全过了才改内存」的顺序）
    QString p1;
    ++gTally.loadsTried;
    if (store.loadFromFile(&p1)) {
        ++gTally.loadsOk;
        qInfo().noquote() << QStringLiteral("  ① 读旧文件成功：内存版本=v%1 键=%2")
                                 .arg(store.schemaVersion()).arg(keyOrderOf(store.data()).join(QStringLiteral("、")));
    } else {
        ++gTally.loadsFailed;
        qInfo().noquote() << QStringLiteral("  ① 读旧文件失败：%1")
                                 .arg(p1.isEmpty() ? QStringLiteral("（没给原因，这本身就是要查的 bug）") : p1);
    }

    // ② 升级到 v2 再写回：saveCandidate 里先 validate、后写盘、commit 后再回读
    int toVer = 0;
    const QJsonObject upgraded = ConfigStore::migrate(store.data(), store.schemaVersion(), &toVer);
    const qint64 beforeSize = QFileInfo(cfgPath).size();
    QString p2;
    ++gTally.savesTried;
    if (store.saveCandidate(upgraded, &p2)) {
        ++gTally.savesCommitted;
        qInfo().noquote() << QStringLiteral("  ② 升级写回成功：磁盘从 %1 字节变成 %2 字节，内存版本=v%3")
                                 .arg(beforeSize).arg(QFileInfo(cfgPath).size())
                                 .arg(store.schemaVersion());
    } else {
        ++gTally.savesRefused;
        qInfo().noquote() << QStringLiteral("  ② 升级写回被拒：%1｜磁盘字节数还是 %2")
                                 .arg(p2).arg(QFileInfo(cfgPath).size());
    }
    // 换个对象重读，证明盘上真的是 v2 了
    ConfigStore reread(cfgPath);
    QObject::connect(&reread, &ConfigStore::refused, [](const QString &reason) {
        qInfo().noquote() << QStringLiteral("    [重读 refused] %1").arg(reason);
    });
    QString p2b;
    ++gTally.loadsTried;
    if (reread.loadFromFile(&p2b)) {
        ++gTally.loadsOk;
        qInfo().noquote() << QStringLiteral("     新对象重读：版本=v%1 键=%2 displayName=%3")
                                 .arg(reread.schemaVersion())
                                 .arg(keyOrderOf(reread.data()).join(QStringLiteral("、")))
                                 .arg(reread.data().value(QStringLiteral("displayName")).toString());
    } else {
        ++gTally.loadsFailed;
        qInfo().noquote() << QStringLiteral("     新对象重读失败：%1")
                                 .arg(p2b.isEmpty() ? QStringLiteral("（没给原因）") : p2b);
    }

    // ③ 交一份不合 schema 的候选：要在**开文件之前**就被拒，磁盘一个字节都不能动
    QJsonObject junk;
    junk.insert(QStringLiteral("schemaVersion"), QJsonValue(2.0));
    junk.insert(QStringLiteral("displayName"), QJsonValue(QStringLiteral("缺了 limits")));
    const qint64 sizeBeforeJunk = QFileInfo(cfgPath).size();
    QString p3;
    ++gTally.savesTried;
    const bool junkOk = store.saveCandidate(junk, &p3);
    if (junkOk) ++gTally.savesCommitted; else ++gTally.savesRefused;
    qInfo().noquote() << QStringLiteral("  ③ 坏候选（v2 但没 limits）：saveCandidate=%1｜%2")
                             .arg(yes(junkOk)).arg(p3);
    qInfo().noquote() << QStringLiteral("     磁盘字节：拒前 %1 → 拒后 %2（相等才叫「没动过盘」）")
                             .arg(sizeBeforeJunk).arg(QFileInfo(cfgPath).size());

    // ④ 把文件截断一半（模拟写一半掉电/别人改坏了），看读失败时内存有没有被动。
    //    预期（本机未运行，需在能编译的机器上复核）：截到一半的 JSON 一定不合语法，
    //    所以这一次 loadFromFile 应当返回 false。到底是不是，看下面这行自己打出来的 yes/no。
    const qint64 fullSize = QFileInfo(cfgPath).size();
    const bool shrunk = QFile::resize(cfgPath, fullSize / 2);      // qfile.h 第 279 行
    const int versionBefore = store.schemaVersion();
    const QJsonObject dataBefore = store.data();
    QString p4;
    ++gTally.loadsTried;
    const bool loadOk = store.loadFromFile(&p4);
    if (loadOk) ++gTally.loadsOk; else ++gTally.loadsFailed;
    qInfo().noquote() << QStringLiteral("  ④ resize 到一半（%1→%2 字节，shrunk=%3）后再读：loadFromFile=%4")
                             .arg(fullSize).arg(QFileInfo(cfgPath).size()).arg(yes(shrunk))
                             .arg(yes(loadOk));
    qInfo().noquote() << QStringLiteral("     %1").arg(loadOk
                             ? QStringLiteral("这一次居然大获成功 —— 那说明截断截在了一个仍是合法 JSON 的点上，"
                                              "**这一行就是本讲要复核的地方**")
                             : QStringLiteral("失败原因：%1").arg(p4));
    qInfo().noquote() << QStringLiteral("     ★ 内存里的版本：读前 v%1 → 读后 v%2；"
                                        "键数：读前 %3 → 读后 %4（读失败就必须一个都没变）；失败计数=%5")
                             .arg(versionBefore).arg(store.schemaVersion())
                             .arg(qint64(dataBefore.size())).arg(qint64(store.data().size()))
                             .arg(store.failedLoadCount());

    // ⑤ 修：用一次成功的 saveCandidate 把文件恢复，再读一次
    QString p5;
    ++gTally.savesTried;
    if (store.saveCandidate(reread.data(), &p5)) {
        ++gTally.savesCommitted;
        QString p6;
        ++gTally.loadsTried;
        const bool back = store.loadFromFile(&p6);
        if (back) ++gTally.loadsOk; else ++gTally.loadsFailed;
        qInfo().noquote() << QStringLiteral("  ⑤ 用好数据修盘：%1｜再读回来=%2｜版本=v%3｜字节=%4")
                                 .arg(p5).arg(yes(back)).arg(store.schemaVersion())
                                 .arg(QFileInfo(cfgPath).size());
    } else {
        ++gTally.savesRefused;
        qInfo().noquote() << QStringLiteral("  ⑤ 修盘也被拒：%1").arg(p5);
    }
    qInfo().noquote() << QStringLiteral(
        "  ↑ ⑤ 递给 saveCandidate 的是 reread 那份**从上一版好文件里读出来的内存数据**，\n"
        "    不是从那个被截断的坏文件里读出来的（那个读都没读成）。顺序不能反：\n"
        "    反了就是「拿坏数据覆盖好文件」，那是二次事故——第一次事故只坏了一个文件，\n"
        "    第二次事故把唯一的活口也灭了。\n"
        "    QSaveFile 为什么会给你「目标文件要么旧要么新、没有半截」的保证，第 2 讲拆过了"
        "（storage/02_file_io.cpp），这里只补一句：**它管的是字节，管不了语义**——"
        "语义要靠 commit 之后那次回读校验，本讲的回读就在 saveCandidate 的最后几行。");
    qInfo().noquote() << QStringLiteral("  【8】小结：存盘 %1 次（提交 %2 / 拒绝 %3），"
                                        "读盘 %4 次（成功 %5 / 失败 %6）。")
                             .arg(gTally.savesTried).arg(gTally.savesCommitted)
                             .arg(gTally.savesRefused).arg(gTally.loadsTried)
                             .arg(gTally.loadsOk).arg(gTally.loadsFailed);
}

// ============================================================================
// 十三、合账 + 清理 + 收尾
// ============================================================================
static void stepTallyAndCleanup(const QString &workDir, const QString &tmpRoot)
{
    qInfo().noquote() << QStringLiteral(
        "\n────【合账】把八个批次打出来的数汇成一张表，自己跟自己对────");

    auto check = [](const QString &label, bool ok) {
        qInfo().noquote() << QStringLiteral("  %1  %2")
                                 .arg(ok ? QStringLiteral("[对得上]") : QStringLiteral("[对不上]"))
                                 .arg(label);
        if (!ok)
            ++gTally.mismatch;              // ★ 对不上是要带出进程外的：CI 里 `|| exit 1` 才算真检查
    };

    qInfo().noquote() << QStringLiteral("  【1】类型表行数=%1（qjsonvalue.h 第 32~40 行有 7 个成员）；"
                                        "故意错读=%2（设计值 5）；样本键数=%3（设计值 7）")
                             .arg(gTally.kindsPrinted).arg(gTally.wrongReads).arg(gTally.objectKeys);
    check(QStringLiteral("【1】类型表行数 == 7"), gTally.kindsPrinted == 7);
    check(QStringLiteral("【1】故意错读次数 == 5"), gTally.wrongReads == 5);
    check(QStringLiteral("【1】样本键数 == 7（对象里确实摆了 7 个键）"), gTally.objectKeys == 7);

    qInfo().noquote() << QStringLiteral("  【2】键序样本=%3（设计值 3）、与插入序不同的=%1、数组最后=%2 项")
                             .arg(gTally.orderMismatch).arg(gTally.arrayItems).arg(gTally.orderLists);
    check(QStringLiteral("【2】键序样本份数 == 3"), gTally.orderLists == 3);

    qInfo().noquote() << QStringLiteral("  【3】解析总次数=%1、成功=%2、失败=%3；NDJSON 非空行=%4、逐行成功=%5")
                             .arg(gTally.docsTried).arg(gTally.docsParsed).arg(gTally.docsRejected)
                             .arg(gTally.lineNonEmpty).arg(gTally.lineOk);
    check(QStringLiteral("【3】成功 + 失败 == 总次数（这本账是闭合的，没有漏记）"),
          gTally.docsParsed + gTally.docsRejected == gTally.docsTried);
    check(QStringLiteral("【3】NDJSON 逐行成功数 == 非空行数"), gTally.lineOk == gTally.lineNonEmpty);

    qInfo().noquote() << QStringLiteral("  【4】样本=%1、按规范非法=%2、实际被拒=%3")
                             .arg(gTally.samplesTotal).arg(gTally.samplesIllegal).arg(gTally.samplesRejected);
    check(QStringLiteral("【4】样本总数 == 10"), gTally.samplesTotal == 10);
    check(QStringLiteral("【4】按规范非法数 == 实际被拒数（这一条是「严格」的定义）"),
          gTally.samplesIllegal == gTally.samplesRejected);

    qInfo().noquote() << QStringLiteral("  【5】数字检验行=%1；大整数往返字节是否完全相同=%2")
                             .arg(gTally.numberChecks).arg(yes(gTally.bigRoundTripKept));

    qInfo().noquote() << QStringLiteral("  【6】JsonFormat 打了=%1（设计值 2）；编码/合法性判定=%2")
                             .arg(gTally.formatsPrinted).arg(gTally.encodingsChecked);
    check(QStringLiteral("【6】格式份数 == 2（Indented + Compact）"), gTally.formatsPrinted == 2);

    qInfo().noquote() << QStringLiteral("  【7】探针总数=%1、放行=%2、拒绝=%3")
                             .arg(gTally.probesTotal).arg(gTally.probesOk).arg(gTally.probesRefused);
    check(QStringLiteral("【7】放行 + 拒绝 == 探针总数（没有第三种答案）"),
          gTally.probesOk + gTally.probesRefused == gTally.probesTotal);
    check(QStringLiteral("【7】探针总数 == 8"), gTally.probesTotal == 8);

    qInfo().noquote() << QStringLiteral("  【8】存盘=%1（提交 %2 / 拒绝 %3）；读盘=%4（成功 %5 / 失败 %6）")
                             .arg(gTally.savesTried).arg(gTally.savesCommitted).arg(gTally.savesRefused)
                             .arg(gTally.loadsTried).arg(gTally.loadsOk).arg(gTally.loadsFailed);
    check(QStringLiteral("【8】提交 + 拒绝 == 存盘尝试数"),
          gTally.savesCommitted + gTally.savesRefused == gTally.savesTried);
    check(QStringLiteral("【8】成功 + 失败 == 读盘尝试数（这本账也得闭合）"),
          gTally.loadsOk + gTally.loadsFailed == gTally.loadsTried);
    // ★ 【6】的 GBK 样本单独记，不混进【3】——这里把它打出来，让两本账各清各的。
    qInfo().noquote() << QStringLiteral("  【6·补】GBK 样本实际被拒=%1（这一条不进【3】的账）")
                             .arg(gTally.gbkRejected);

    qInfo().noquote() << QStringLiteral(
        "  ★ 上面这些「对不上」的行如果真出现了，说明**运行期的答案和头文件的期待不一样**——\n"
        "    那不是程序错了，是本讲有一处推测错了。请把那一行的原始输出记下来，"
        "回来改这份文件；这比让它默默通过有用得多。\n"
        "    而且它不会白说：对不上几行，进程就返回几（`exit(1)` 起步），CI 拦得住。");

    // ── 清理：本次只写了临时目录里自建的那一个目录，删干净并证明删干净了 ──
    qInfo().noquote() << QStringLiteral("\n────【清理】────");
    qInfo().noquote() << QStringLiteral("  系统临时目录 = [%1]").arg(QDir::toNativeSeparators(tmpRoot));
    qInfo().noquote() << QStringLiteral("  本次工作目录 = [%1]").arg(QDir::toNativeSeparators(workDir));
    QDir work(workDir);
    const QStringList leftovers = work.entryList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
    qInfo().noquote() << QStringLiteral("  待删清单（%1 项）：%2")
                             .arg(qint64(leftovers.size())).arg(leftovers.join(QStringLiteral("、")));
    const bool removed = work.removeRecursively();          // qdir.h 第 186 行
    const QStringList residue = QDir(workDir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
    qInfo().noquote() << QStringLiteral("  removeRecursively()=%1｜目录还在吗=%2｜残留条目=%3（应为 0）")
                             .arg(removed).arg(QDir(workDir).exists()).arg(qint64(residue.size()));
    check(QStringLiteral("清理后残留条目 == 0"), residue.isEmpty());

    qInfo().noquote() << QStringLiteral(
        "\n────【收尾声明】────\n"
        "  · 这份文件在本机只做到了 moc + g++ -fsyntax-only（0 error 0 warning）；\n"
        "  · **没有链接、没有运行**（本机跑 Qt 可执行文件缺符号，直接 0xC0000139）；\n"
        "  · 文中所有「预期」标记的地方都等实机输出定案，我没有把任何运行期结论说成实测；\n"
        "  · 所有写盘都只在 TempLocation 下的 qt_storage_03/ 里，上面已经删干净。");

    // ★ 退出码就是「对不上的行数」。用 quit() 的话进程永远返回 0，
    //   那张对账表打印得再认真，脚本与 CI 也看不见——exit(码) 才是把话说到进程外面。
    qInfo().noquote() << QStringLiteral("────【退出码】对不上一共 %1 行 → exit(%2) ────")
                             .arg(gTally.mismatch).arg(gTally.mismatch > 0 ? 1 : 0);
    QCoreApplication::exit(gTally.mismatch > 0 ? 1 : 0);
}

// ============================================================================
// 十四、main
// ============================================================================
// 全程单线程 + 事件循环驱动：QCoreApplication 必须是第一个语句（QTimer 与 exit()
// 都需要事件循环），八个批次靠 QTimer::singleShot 的毫秒数排开，最后一个自己退，
// 退的时候把「对不上的行数」当成退出码带出去（0 = 全对上）。
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("storage_03_json"));
    QCoreApplication::setOrganizationName(QStringLiteral("QtCourseOrg"));

    const QString tmpRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString workDir = QDir(tmpRoot).filePath(QStringLiteral("qt_storage_03"));
    QDir().mkpath(workDir);                                 // qdir.h 第 181/183 行

    QTimer::singleShot(0, [tmpRoot, workDir]() {
        qInfo().noquote() << QStringLiteral(
            "============================================================");
        qInfo().noquote() << QStringLiteral("storage 第 3 讲 · JSON（Qt = %1，QLatin1StringView 之类都没关掉）")
                                 .arg(QT_VERSION_STR);
        qInfo().noquote() << QStringLiteral("本机头文件核对：C:\\msys64\\ucrt64\\include\\qt6（Qt 6.12.0）");
        qInfo().noquote() << QStringLiteral("TempLocation = [%1]｜QDir::tempPath() = [%2]")
                                 .arg(QDir::toNativeSeparators(tmpRoot), QDir::tempPath());
        qInfo().noquote() << QStringLiteral("本次工作目录 = [%1]｜mkpath 后 exists = %2")
                                 .arg(QDir::toNativeSeparators(workDir)).arg(yes(QDir(workDir).exists()));
        qInfo().noquote() << QStringLiteral("提醒：本机没运行过 Qt 程序，所以下面每一行都是「跑出来才知道」的；"
                                            "凡是本讲敢写死的数字，都标了来源（头文件行号 / 纯 C++ 实测）。");
        qInfo().noquote() << QStringLiteral("============================================================");
        stepValueTypes();                                    // 【1】
    });
    QTimer::singleShot(120, []() { stepKeyOrder(); });        // 【2】
    QTimer::singleShot(240, []() { stepParseDocument(); });   // 【3】
    QTimer::singleShot(360, []() { stepStrictParser(); });    // 【4】
    QTimer::singleShot(480, []() { stepNumbers(); });         // 【5】
    QTimer::singleShot(600, [workDir]() { stepWriteText(workDir); });        // 【6】
    QTimer::singleShot(720, []() { stepDefensiveAccess(); });                // 【7】
    QTimer::singleShot(840, [workDir]() { stepAtomicSave(workDir); });       // 【8】
    QTimer::singleShot(960, [workDir, tmpRoot]() {
        stepTallyAndCleanup(workDir, tmpRoot);               // 合账 + 清理 + exit(退出码)
    });

    return app.exec();
}

#include "03_json.moc"

// ============================================================================
// ——— 老师傅的话 ———
// ============================================================================
/*
 * 一、JSON 这块，新手和老手差在哪儿？差在「失败」这两个字上。Qt 给你的 11 个读函数
 *    （toBool/toInt/toInteger/toDouble/toString/toStringView/toArray/toObject 那几个重载）
 *    一个都不告诉你失败。它不是坏心，是设计：它假设你已经知道 type() 是什么。
 *    所以老手写的代码里，「读一个配置」永远是三行——问存在、问类型、再取值——
 *    而不是链式的一句 o["x"].toInt()。链式越短，事故越长。
 *
 * 二、键序这件事，我在头文件里翻了一圈一个承诺都没有，只抓到官方文档一句
 *    「The list is sorted alphabetically.」。信不信由你，反正我不把它当证据。
 *    真正的结论是工程结论：**顺序不是数据，别拿它当数据用**。需要顺序的时候用数组，
 *    需要"按名字取"的时候用对象，两条路各走各的。你要是发现某次导出报表的列顺序变了，
 *    那不是你 Qt 版本升了，是你把集合当成了队列。
 *
 * 三、数字这一条我讲得最重，因为它爆得最晚。JSON 文本里的整数在 Qt 里是 double，
 *    而 double 到 2^53 就开始漏。你的 ID 现在是三位数，明年三万，后年三千万，
 *    等它变成雪花号或者纳秒戳那天，程序不会报错，只会安安静静地把 9007199254740993
 *    变成 9007199254740992，然后你的对账系统说「有一笔订单查不到」。
 *    对策只有一条：ID 用字符串存。这不是洁癖，是把「类型」写进数据里，让错误当场响。
 *
 * 四、"严格"这两个字，是解析器里那些**不存在**的函数名给的：qjsonparser_p.h 里
 *    七个 parse 方法，没有 parseComment，没有尾逗号分支，没有单引号。
 *    我年轻时最怕这类「因为没写所以不行」的证据，总觉得不够硬；现在我只信这种。
 *    文档里的形容词会随版本改，函数名不会骗你——它要么在那儿，要么不在。
 *
 * 五、落盘那三条纪律（先校验候选、只写临时再原子替换、回读通过才换内存），
 *    看着像强迫症，其实是掉过跟头的人才会写。你会发现第③步「坏候选」和第④步
 *    「文件截断一半」这两件事在生产线上是同一件事的两种长相：前者是有人手抖改了配置，
 *    后者是断电在凌晨三点。它们的共同要求是：进程内存里那份数据**不能被脏读**。
 *    守住这条，重启之后你的程序还知道自己在说什么；守不住，就会发生
 *    「坏配置被读进来、又被原样写回去、还加了几个新键」这种三倍坏的事故。
 *
 * 六、Windows 上做中文，编码这条坑本讲只演示了一半（GBK 字节进 fromJson 必炸）。
 *    另一半是：Qt6 把 setCodec 删了、把 QStringConverter 的枚举收成那几个，
 *    意思很明白——**入口只留 UTF-8 这一条桥**。别去搭私桥（"System" 编码
 *    跟着平台走，今天能用，明年换台机器就不能用）。你的 JSON 文件从写下的第一字节
 *    起就应该是 UTF-8，读的时候先问一句 isValidUtf8()，这个动作只花一行，
 *    但它把"文件坏了"和"我机器码页不同"这两件事分开了——分不开，排查就是猜。
 *
 * 七、别拿 toJson() 的字节去做 diff、哈希、签名、缓存键，也别拿 Indented 的输出
 *    当"给人看的那一份"存进仓库。缩进宽度是 writer 的一个形参，字段顺序是集合的
 *    偶然产物，数字格式是另一个模块的事。你要做对比，就比语义（对象 == 对象），
 *    或者干脆自己定一个"规范化输出"的函数：排序、去空白、限定数字范围——
 *    写一次，用一年，比每次 diff 都出几十个假变更便宜得多。
 *
 * 八、最后说一句教学上的取舍：这八条里没有一条是"新 API"，全是你早就会用但没验过的
 *    旧 API。我本机跑不了 Qt，所以这份文件里所有运行期的结论我都标了"预期"，
 *    连我自己算过的纯 C++ 数字都注明了是"同一套工具链、不需要 Qt 运行时"。
 *    这不是推卸，是这门课的规矩：**能给行号的给行号，只能推测的就写推测**。
 *    你以后写技术说明，也照这个标准来——别人才能在你的文档里做决定。
 */

// ============================================================================
// ——— 小练习 ———
// ============================================================================
/*
 * 1)【改代码，可验证】把【1】里那五次「故意读错」换成用 jsonValueOf/jsonInt64 走一遍，
 *    让 gTally.wrongReads 变成 probesRefused。跑起来核对：拒绝数是不是 5，
 *    【合账】那行「放行 + 拒绝 == 总数」还成不成立。
 *
 * 2)【猜错误码，再跑】往【4】的样本表里再加四条：`{"a":1}` 后面跟一个 NUL 字节、
 *    `1e999`、`0123`、`1.`。先猜各自的 ParseError 名字（TerminationByNumber？
 *    IllegalNumber？），再跑，看你猜中几个。特别注意：如果有条目**通过了**，
 *    【合账】里「非法数 == 被拒数」那一行会打「对不上」——那就是你要回去改注释的地方。
 *
 * 3)【协议设计题】把【5】的大整数从「数字」改成「字符串」：{"id":"9007199254740993"}，
 *    读回来用 toString() 再自己转 qint64。比较两种写法的 Compact 字节数、
 *    往返是否等价、以及【5】那行「往返字节相同吗」的答案变没变。
 *    然后写三句话，说服你们组把 ID 定成字符串。
 *
 * 4)【加一档版本】给 ConfigStore 加 v2→v3：把 limits.tempMax 拆成
 *    limits.tempMax 与 limits.humidityMax（缺省 60），schemaVersion=3，
 *    并让 migrate() 明确拒绝跨两版（from=1 直接要到 3 时原样退回）。
 *    再造一个 {"schemaVersion":0} 的文件，看 loadFromFile 的拒绝文案是什么。
 *
 * 5)【证明「没动盘」】在【8】的第③步之前，先记下 cfgPath 的字节数与内容 hex；
 *    把坏候选换成「校验能过、但 write 会失败」的情形不好造，那就换个更狠的：
 *    在 saveCandidate 里 commit() 之前人工 return（临时改一行），
 *    跑完看目标文件的字节数和内容有没有任何变化。答案应该是「完全没变」，
 *    因为 QSaveFile 写的是临时文件——这条纪律值得你亲手验一次。
 *
 * 6)【量成本】用【6】的两份输出估：一条记录 Indented 比 Compact 多 N 字节，
 *    十万条日志就多半 N*100000 字节。把手工估算的数字和实际用十万条跑出来的
 *    文件字节数对一下（记得写进 TempLocation 并删掉）。
 *    顺便回答：如果这份 JSON 是要通过网络发给别人，你选哪种？如果要 git diff，
 *    你选哪种？两个答案一样吗？为什么？
 *
 * 7)【把自检搬走】把【6】那三问（原始中文 UTF-8 字节出现几次 / "\\u" 出现几次 /
 *    控制字符原字节出现几次）写成你们项目里的一个单元测试，
 *    断言「序列化出来的字节必须 isValidUtf8() 且不含裸控制字符」。
 *    测试这件事属于第 10 模块（quality，QtTest），本讲故意不做——但这条断言
 *    值得今天就抄下来，因为它是唯一能替你守住「编码合同」的东西。
 */
