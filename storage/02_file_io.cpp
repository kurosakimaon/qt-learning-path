// ============================================================================
// 第 2 讲 · 文件与 IO 设备：QFile / QTextStream / QSaveFile / QLockFile / QDir
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   文件这块，Qt 的抽象只有三层——QIODeviceBase（模式）→ QIODevice（读写定位的动词）
//   → QFileDevice/QFile（落到真实文件）。新手把这三层揉成一团，就写出「读一半崩了
//   文件成半截」「大文件 readAll 把内存吃光」「文本模式打开又 seek，字节数对不上」。
//   本讲把这些一次性摊开，每一条都给可核对的数字：
//     1) openMode 的组合到底谁能和谁拼（本机 qiodevicebase.h 第 17~28 行的 9 个位），
//        Append 与 Truncate 同时给会怎样、Text 位改的是**换行翻译**不是编码、
//        ExistingOnly/NewOnly 是「防手滑」的两道闸、Unbuffered 什么时候才该用。
//     2) QFile 的一套动词：read/readAll/write/pos/seek/atEnd/size/bytesAvailable，
//        以及 seek 到文件尾之后 atEnd 的行为（本讲实测：pos==size 时 atEnd 为真）。
//     3) 文本 vs 二进制：同一份内容两种打开方式，字节数与行数怎么差；
//        换行符在 Windows 上是 \r\n，QIODevice::Text 会做转换——**pos() 于是和
//        真实文件偏移不再相等**，这是自写进度条最容易翻车的一条。
//     4) QTextStream 的编解码：★ **Qt6 已经删掉 setCodec()**，只剩
//        setEncoding(QStringConverter::Encoding)（qtextstream.h 第 89 行）。
//        老代码 `ts.setCodec("UTF-8")` / `setCodec(QTextCodec*)` 在 Qt6 全部编不过。
//        再加上 setLocale() 对数字格式的连带影响（同一句 `ts << 0.85`，
//        c  locale 出 "0.85"、德语 locale 出 "0,85"），以及
//        「直接用 QByteArray + QString::fromUtf8」为什么通常比 QTextStream 更可控。
//     5) QSaveFile 的原子替换：为什么它防得住「写一半掉电」。
//        ★ 注意本机 qsavefile.h 第 71 行 `void close() override;` 在 **private:** 段里——
//          你**调不到 QSaveFile::close()**，只有 commit()（第 46 行）和
//          cancelWriting()（第 48 行）两个开关。
//     6) QTemporaryFile 与 QLockFile 防多开。
//        ★ QTemporaryFile 的带参 open(OpenMode) 是 protected 且被注释「Hides open(flags)」
//          （qtemporaryfile.h 第 51~52、94 行），外面只能调**无参 open()**。
//        ★ QLockFile **不是 QObject**（qlockfile.h 第 18 行没有继承），析构自动解锁。
//     7) QFileInfo/QDir 的元信息与遍历：entryInfoList 的 filters 与 sortFlags、
//        nameFilters 过滤；★ Qt6 里 `QDir::Modified` 这个 filter **已弃用**
//        （qdir.h 第 35~37 行 QT_DEPRECATED_VERSION_X_6_11），`QDir::count()` 的返回
//        也从 uint 变成 **qsizetype**（第 153~156 行）。
//     8) 错误处理：Qt 的文件 IO **不抛异常、不发错误信号**，只有
//        error()（QFileDevice 第 113 行，14 个 FileError 枚举值）+
//        errorString()（QIODevice 第 115 行）。本讲故意开一个不存在的文件把两个都打出来。
//
// 对应构建目标名：storage_02_file_io
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(storage_02_file_io 02_file_io.cpp)
//   target_link_libraries(storage_02_file_io PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)     # 本文件**没有 Q_OBJECT 类**，AUTOMOC 扫到没内容可生成
//
// 运行前须知：
//   · 无人值守自动演示：QTimer::singleShot 串 8 个批次，全程无鼠标键盘，
//     末尾 qInfo() 打证据后 QCoreApplication::quit()。没有 exec() 的模态对话框。
//   · 所有写盘都落在 QStandardPaths::writableLocation(QStandardPaths::TempLocation)
//     下自建的小目录 qt_storage_02/ 里，**不碰用户文档、不碰配置目录**，
//     最后一个批次用 QDir::removeRecursively() 删干净并打印删除结果与残留检查。
//   · 大文件那一节只造 4 MiB（在临时目录里现写现删），不读任何现成的大文件；
//     涉及的耗时只承诺**相对关系**，具体毫秒请你自己跑。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 storage_02_file_io，跑到自动退出、退出码 0）。
//     但本讲**没有汇总自检行**，退出码 0 只证明「没崩、走完了」，不证明正文每个数字都对：
//     带具体数字的「预期」请以你自己那次运行输出为准（本机那一轮存档 runlogs/storage_02_file_io.log）。
// ============================================================================

#include <QCoreApplication>          // QtCore/qcoreapplication.h
#include <QStandardPaths>            // QtCore/qstandardpaths.h：TempLocation
#include <QDir>                      // QtCore/qdir.h
#include <QFile>                     // QtCore/qfile.h
#include <QFileInfo>                 // QtCore/qfileinfo.h
#include <QFileDevice>               // QtCore/qfiledevice.h：FileError / Permissions
#include <QIODevice>                 // QtCore/qiodevice.h：OpenMode 动词
#include <QIODeviceBase>             // QtCore/qiodevicebase.h：★ Qt6 的枚举老家
#include <QSaveFile>                 // QtCore/qsavefile.h
#include <QTemporaryFile>            // QtCore/qtemporaryfile.h
#include <QLockFile>                 // QtCore/qlockfile.h
#include <QTextStream>               // QtCore/qtextstream.h
#include <QStringConverter>          // QtCore/qstringconverter_base.h：Encoding 枚举
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QLocale>                   // QtCore/qlocale.h：setLocale 的坑
#include <QDateTime>
#include <QElapsedTimer>
#include <QTimer>
#include <QtGlobal>
#include <QDebug>

#include <chrono>                    // QLockFile::tryLock(std::chrono::milliseconds)

// ============================================================================
// 一、核对签名（本机 Qt 6.12 头文件，行号一律指它）
// ============================================================================
// QtCore/qiodevicebase.h —— **Qt6 相对 Qt5 最大的一次搬家**：
//   第 12 行 class QIODeviceBase
//   第 17~28 行 enum OpenModeFlag {
//        NotOpen      = 0x0000,   第 19 行 ReadOnly     = 0x0001
//        第 20 行 WriteOnly   = 0x0002
//        第 21 行 ReadWrite   = ReadOnly | WriteOnly   ← 0x0003，是**组合**不是独立位
//        第 22 行 Append      = 0x0004   ← 写之前把 pos 挪到结尾，与 Truncate 互斥
//        第 23 行 Truncate    = 0x0008   ← 打开即清空
//        第 24 行 Text        = 0x0010   ← 只做换行翻译（\n ↔ \r\n），**不管编码**
//        第 25 行 Unbuffered  = 0x0020   ← 绕过内部缓冲，read 会「有多少给多少」
//        第 26 行 NewOnly     = 0x0040   ← 已存在就 open 失败（防覆盖）
//        第 27 行 ExistingOnly= 0x0080   ← 不存在就 open 失败（防新建）
//      }
//   第 29 行 Q_DECLARE_FLAGS(OpenMode, OpenModeFlag)
//   ★ 为什么你的老代码还能写 `QIODevice::ReadWrite`：
//     qiodevice.h 第 31~38 行 `class QIODevice : public QObject, public QIODeviceBase`
//     —— 枚举是**继承**来的，名字看得到。但请注意 Qt5 的 `QIODevice::OpenModeMode`
//     这类内部名没了，跨层写 `QIODeviceBase::ReadOnly` 也合法（同一份枚举）。
//
// QtCore/qiodevice.h —— 动词表：
//   第 49 行 QIODeviceBase::OpenMode openMode() const;
//   第 51/52 行 setTextModeEnabled(bool) / isTextModeEnabled()   ← Text 位的读写开关
//   第 54~57 行 isOpen() / isReadable() / isWritable() / isSequential()
//   第 66 行 virtual bool open(QIODeviceBase::OpenMode mode);   第 67 行 virtual void close();
//   第 71 行 virtual qint64 pos() const;   第 72 行 virtual qint64 size() const;
//   第 73 行 virtual bool seek(qint64 pos); 第 74 行 virtual bool atEnd() const;
//   第 75 行 virtual bool reset();          ← 回到开头；顺序型设备用它
//   第 77/78 行 bytesAvailable() / bytesToWrite()
//   第 80 行 qint64 read(char *data, qint64 maxlen);       ← 返回**实际读到的字节数**，可能小于 maxlen
//   第 81 行 QByteArray read(qint64 maxlen);
//   第 82 行 QByteArray readAll();                         ← ★ 一次性吃进内存，大文件别用
//   第 83~85 行 readLine(char*,qint64) / QByteArray readLine(qint64 maxlen = 0) /
//              bool readLineInto(QByteArray *result, qint64 maxlen = 0)
//                  ← readLineInto **不新建 QByteArray**，循环读行时用它省一大串分配
//   第 87~91 行 readLineInto(QSpan<char>/<uchar>/<std::byte>)  ← Qt6 新写法，定长栈缓冲
//   第 93 行 virtual bool canReadLine() const;
//   第 95~98 行 startTransaction/commitTransaction/rollbackTransaction（**QFile 上没意义**，
//              那是给带缓冲的时序型设备用的；本讲不演示）
//   第 100 行 qint64 write(const char *data, qint64 len);
//   第 101 行 qint64 write(const char *data);              ← 裸 char* 那份按**字节**算
//   第 102 行 qint64 write(const QByteArray &data);
//   第 104/105 行 peek；第 106 行 qint64 skip(qint64 maxSize)
//   第 111~113 行 ungetChar / putChar / getChar(char*)
//   第 115 行 QString errorString() const;                 ← ★ 人话版错误，QIODevice 上就有
//   第 118~124 行 信号：readyRead / bytesWritten(qint64) / aboutToClose / readChannelFinished
//   第 133/136 行 protected 纯虚 readData / writeData      ← 自写设备只要实现这两个
//   第 151 行 Q_DECLARE_OPERATORS_FOR_FLAGS(QIODevice::OpenMode)  ← 所以能用 | 拼
//
// QtCore/qfiledevice.h —— 真实文件那一层：
//   第 72~88 行 enum FileError { NoError=0, ReadError=1, WriteError=2, FatalError=3,
//              ResourceError=4, OpenError=5, AbortError=6, TimeOutError=7,
//              UnspecifiedError=8, RemoveError=9, RenameError=10, PositionError=11,
//              ResizeError=12, PermissionsError=13, CopyError=14 };
//   第 90~95 行 enum FileTime { FileAccessTime, FileBirthTime, FileMetadataChangeTime,
//              FileModificationTime };
//   第 97~103 行 enum Permission { ReadOwner=0x4000, WriteOwner=0x2000, ExeOwner=0x1000,
//              ReadUser=0x0400, ... } + Q_DECLARE_FLAGS(Permissions, Permission)
//                  ← ★ 别记成 QFile::ReadOwner 就完事：QFile 继承来的，能这么写；
//                    但**权限值和 POSIX 的 rwx 不是一一对应**，Windows 上大多只有 Owner 几位。
//   第 113 行 FileError error() const;   第 114 行 void unsetError();
//   第 120 行 int handle() const;        第 121 行 virtual QString fileName() const;
//   第 123~128 行 pos/seek/atEnd/flush/size/resize 的 QFileDevice 版覆写
//   第 140 行 uchar *map(qint64 offset, qint64 size, MemoryMapFlags flags = NoOptions);
//   第 141 行 bool unmap(uchar *address);   ← 大文件「读」的正解之一，本讲提一句不演示
//   第 143/144 行 fileTime(FileTime) / setFileTime(...)
//
// QtCore/qfile.h：
//   第 78/79 行 QFile();  QFILE_MAYBE_EXPLICIT QFile(const QString &name);
//        ← ★ 第 64~68 行：Qt ≥ 6.9 时 QFILE_MAYBE_EXPLICIT 展开成 **explicit**，
//          所以 `QFile f = path;` 这种隐式写法在新版编不过，一律 `QFile f(path);`
//   第 48~62 行 QFILE_MAYBE_NODISCARD 宏——注意它在 **qfiledevice.h**，不在 qfile.h：
//        48~54 行按版本决定（QT_VERSION < 6.10 走 QT_NO_USE_NODISCARD_FILE_OPEN，
//        ≥ 6.10 才 define QT_USE_NODISCARD_FILE_OPEN），59 行才真的展开成 [[nodiscard]]。
//        所以「QFile::open 带 nodiscard」是**从 6.10 起**这件事，本讲 6.12 命中。
//   第 271 行 QFILE_MAYBE_NODISCARD bool open(OpenMode flags) override;
//   第 272 行 bool open(OpenMode flags, Permissions permissions);
//   第 273/274 行 bool open(FILE *f, OpenMode ioFlags, FileHandleFlags handleFlags = DontCloseHandle);
//                  bool open(int fd, OpenMode ioFlags, FileHandleFlags = DontCloseHandle);
//        ← ★ Qt ≥6.10 上不管返回值会出 warning。本讲每一处 open 都判了。
//   第 104 行 fileName() / 第 109 setFileName / 第 150/151 exists（成员版 + 静态版）
//   第 179/180 remove() / static remove(const QString &)
//   第 191~193 moveToTrash（supportsMoveToTrash() 先问再调）
//   第 204/205 rename / static rename
//   第 245~251 copy：★ Qt6 的真实签名是
//        `bool copy(const QString &newName, std::optional<QFileDevice::Permissions> perm = std::nullopt)`
//        —— 比 Qt5 多了第三个（可选）参数；两参静态版 `copy(旧, 新)` 也还在。
//        本讲**不用 copy()**：手工分块读写更能说明问题，还避开 optional 那层歧义。
//   第 276 行 qint64 size() const override;   ← 静态读大小用 QFileInfo::size() 更省事
//
// QtCore/qsavefile.h：
//   第 33/34 行 explicit QSaveFile(QObject *parent = nullptr);
//                  explicit QSaveFile(const QString &name, QObject *parent = nullptr);
//        ← ★ 第 30~32 行那条「只传名字、不传 parent」的重载在
//          QT_CORE_REMOVED_SINCE(6, 11) 里，Qt 6.11+ 已经**不在了**；
//          好消息是第 34 行那份的 parent 有默认值，`QSaveFile sf(path)` 照样成立。
//   第 37/38 行 fileName() / setFileName()
//   第 45 行 QFILE_MAYBE_NODISCARD bool open(OpenMode flags) override;
//   第 46 行 bool commit();            ← **唯一显式的「发布」动作**
//   第 48 行 void cancelWriting();     ← 放弃这次保存，目标文件保持原样
//   第 50/51 行 setDirectWriteFallback(bool) / directWriteFallback()
//        ← 默认 false：跨设备/网络盘上 rename 不了时会失败；置 true 才退回「直接写目标」。
//   第 68 行 protected qint64 writeData(...)；**第 71 行 private: void close() override;**
//        ← ★ QSaveFile 把 close() 藏进 private：**你不能调 sf.close()**。
//          收尾要么 commit()，要么 cancelWriting()，要么等析构。
//
// QtCore/qtemporaryfile.h：
//   第 31/32 行 QTemporaryFile(); explicit QTemporaryFile(const QString &templateName);
//        ← templateName 里的 **XXXXXX** 会被换成随机字符（本讲用 "cfg_XXXXXX.ini"）
//   第 48/49 行 autoRemove() / setAutoRemove(bool)   ← 默认 true：析构就把临时文件删了
//   第 51/52 行 // Hides open(flags)  →  `bool open() { return open(QIODevice::ReadWrite); }`
//   第 94 行 protected: bool open(OpenMode flags) override;
//        ← ★ 外部**只能调无参 open()**；想换模式得自己写子类，或者干脆别用这个类。
//   第 54 行 fileName() / 第 55 fileTemplate() / 第 56 setFileTemplate()
//   第 66/67 行 rename(newName) / renameOverwrite(newName)   ← 临时件转正的正确姿势
//   第 80~82 行 static QTemporaryFile *createNativeFile(QFile &file)
//        ← 把资源里的 :/xxx 或 Qt6 的 qrc:/xxx **落成真临时文件**，
//          交给只会读真实路径的第三方库（本讲第 8 节顺手提，不演示）
//
// QtCore/qlockfile.h（★ 它**不是** QObject，第 18 行没有继承、没有 Q_OBJECT）：
//   第 21 行 explicit QLockFile(const QString &fileName);  第 22 行 ~QLockFile()（析构自动 unlock）
//   第 24 行 QString fileName() const;
//   第 26 行 bool lock();               ← **阻塞**式：一直等到拿到锁。自动演示里**别用**
//   第 36 行 bool tryLock(std::chrono::milliseconds timeout = std::chrono::milliseconds::zero());
//   第 27~28 行 bool tryLock(int timeout);   ← 27 行是 QT_CORE_INLINE_SINCE(6, 10)，
//        兼容版，第 60~64 行内联转成 chrono
//   第 29 行 void unlock();             第 41 行 bool isLocked() const;
//   第 42 行 bool getLockInfo(qint64 *pid, QString *hostname, QString *appname) const;
//        ← 拿到锁的人是谁：pid / 主机名 / 应用名。**排查「上一个进程没退干净」就靠它**
//   第 45~51 行 enum LockError { NoError=0, LockFailedError=1, PermissionError=2,
//              UnknownError=3 };  LockError error() const;
//   第 38/39 行 setStaleLockTime(std::chrono::milliseconds) / staleLockTimeAsDuration()
//   第 32~34 行 setStaleLockTime(int) / staleLockTime()（int 版，兼容用）
//   第 43 行 bool removeStaleLockFile();
//        ← 默认 30 秒以上的锁算「过期」，可以抢；进程崩过的锁就是靠这个自愈的
//
// QtCore/qtextstream.h（★ Qt6 改得最狠的一份）：
//   第 42 行 class Q_CORE_EXPORT QTextStream QT_TEXT_STREAM_FINAL : public QIODeviceBase
//        ← 它只继承 **QIODeviceBase**（为了拿 OpenMode），**不是 QIODevice**，没有信号
//   第 58~63 行 enum Status { Ok, ReadPastEnd, ReadCorruptData, WriteFailed };
//   第 73~78 行 构造：QTextStream(); explicit QTextStream(QIODevice *device);
//        (FILE*, OpenMode=ReadWrite) / (QString*, OpenMode=ReadWrite) /
//        (QByteArray*, OpenMode=ReadWrite) / (const QByteArray&, OpenMode=ReadOnly)
//   第 89 行 void setEncoding(QStringConverter::Encoding encoding);
//   第 90 行 QStringConverter::Encoding encoding() const;
//   ★★ **Qt6 没有 setCodec()**：qtextstream.h 通读一遍，没有任何
//      setCodec(const char*) / setCodec(QTextCodec*) / codec()。
//      Qt5 那套 `ts.setCodec("UTF-8")` 在 Qt6 一律换成
//      `ts.setEncoding(QStringConverter::Utf8)`；「GBK」这类编码
//      QStringConverter::Encoding 里**根本没有对应枚举值**（见下），
//      想读写 GBK 只能自己转（QString::fromLatin1/fromUtf8 都救不了），
//      或者干脆用 QTextCodec 之外的第三方 —— 本讲**只演示头文件里存在的那些**。
//   第 93/94 行 setAutoDetectUnicode(bool) / autoDetectUnicode()
//   第 96/97 行 setGenerateByteOrderMark(bool) / generateByteOrderMark()
//        ← 写 UTF-16 时才需要考虑 BOM；写 UTF-8 别加 BOM，很多解析器（含 Qt 自己的
//          QJsonDocument::fromJson）会因为开头那三个字节报错。
//   第 99/100 行 setDevice(QIODevice*) / device()
//   第 96/97 行 setLocale(const QLocale&) / locale()   ← **数字的小数点/分组从这儿来**
//   第 105~107 行 status() / setStatus(Status) / resetStatus()
//   第 109~113 行 atEnd() / reset() / flush() / seek(qint64) / pos()
//   第 117~120 行 readLine(qint64 maxlen = 0) / readLineInto(QString*, qint64 = 0) /
//                 readAll() / read(qint64 maxlen)
//   第 164~182 行 operator<< 一整套：QChar / char / short / int / long / qlonglong /
//                 float / double / QString / QStringView / QLatin1StringView /
//                 QByteArray / const char* / const void*
//        ← **没有** operator<<(bool)：`ts << true` 会走 char 那份重载变成 \1，
//          想要 "true"/"1" 请自己写三元表达式（本讲就这么做，并把它当反面教材讲）。
//   第 184 行 explicit operator bool() const noexcept { return status() == Ok; }
//        ← 所以 `if (!ts)` 能判写失败，Qt5 时代只能自己读 status()
//   第 228~261 行 namespace Qt { bin/oct/dec/hex/fixed/scientific/left/right/center/
//                 endl/flush/reset/bom/ws } ← ★ 这些操纵符在 Qt6 全在 **Qt::** 命名空间下：
//                 `ts << Qt::endl`，**不再是裸的 endl**（不带 using namespace Qt 时）。
//   QtCore/qstringconverter_base.h 第 102~115 行 enum Encoding {
//        Utf8, Utf16, Utf16LE, Utf16BE, Utf32, Utf32LE, Utf32BE, Latin1, System,
//        LastEncoding = System };
//        ← 一共就这 9 个可用值（UTF-8/16/32 + Latin1 + System）。**没有 GBK、没有 Big5、
//          没有 Shift-JIS** —— 别在代码里写 QStringConverter::Gbk，它不存在。
//
// QtCore/qdir.h / qfileinfo.h：
//   qdir.h 第 24~50 enum Filter { Dirs=0x001, Files=0x002, Drives=0x004, NoSymLinks=0x008,
//        AllEntries=Dirs|Files|Drives, TypeMask=0x00f, Readable=0x010, Writable=0x020,
//        Executable=0x040, PermissionMask=0x070, [Modified=0x080 ← **第 35~37 行已弃用**]
//        Hidden=0x100, System=0x200, AccessMask=0x3F0, AllDirs=0x400, CaseSensitive=0x800,
//        NoDot=0x2000, NoDotDot=0x4000, NoDotAndDotDot=NoDot|NoDotDot, NoFilter=-1 };
//   第 53~66 enum SortFlag { Name=0x00, Time=0x01, Size=0x02, Unsorted=0x03,
//        SortByMask=0x03, DirsFirst=0x04, Reversed=0x08, IgnoreCase=0x10, DirsLast=0x20,
//        LocaleAware=0x40, Type=0x80, NoSort=-1 };
//   第 70 QDir(const QString &path = QString());
//   第 107~109 path() / absolutePath() / canonicalPath()（三种「路径」语义不同，本讲都打）
//   第 135 filePath(fileName) / 第 136 absoluteFilePath / 第 134 dirName
//   第 139/140 toNativeSeparators / fromNativeSeparators
//   第 145/146 nameFilters / setNameFilters；第 148/149 filter/setFilter；151 setSorting
//   第 153~156 count()：★ uint 那份在 QT_CORE_REMOVED_SINCE(6,5) 里，
//        Qt 6.12 你拿到的是 **qsizetype count()**（第 156 行）——
//        `int n = dir.count();` 在 64 位上会有收窄警告，别写 uint 接。
//   第 157 isEmpty(Filters = AllEntries|NoDotAndDotDot)
//   第 166/167 entryList(...)；**第 170/171 entryInfoList(Filters, SortFlags) 与
//        entryInfoList(const QStringList &nameFilters, Filters, SortFlags)**
//   第 178 mkdir(name, std::optional<Permissions> = nullopt)（★ Qt6 多了可选权限参数）
//   第 179 rmdir / 第 183 mkpath(name, optional<Permissions>) / 第 184 rmpath
//        （180~182 行是 `#if QT_CORE_REMOVED_SINCE(6, 10)` 包住的老 mkpath(QString) 重载，
//         6.12 里它还在，但新代码用 183 行那份）
//   **第 186 bool removeRecursively();**  ← 本讲收尾就靠它
//   第 189 exists() / 第 203 remove(file) / 第 204 rename / 第 205 exists(name)
//   第 218 separator()（Windows 给 '\\'，POSIX 给 '/'）/ 第 235 temp() / 236 tempPath()
//   第 243 cleanPath(path)（**不碰磁盘**，只做字符串整理：// → /、去掉 ./）
//   qfileinfo.h 的构造函数（第 33~44 行）：`QFileInfo(const QString &file)` 在**第 40 行**，
//        它前面带的是宏 QFILEINFO_MAYBE_EXPLICIT，而宏由 33~37 行决定：
//        定义了 QT_IMPLICIT_QFILEINFO_CONSTRUCTION 就变 Q_IMPLICIT（可隐式转），
//        否则就是 explicit —— 所以「QFileInfo 一律 explicit」这句是**有条件的**，别背死。
//   ★ 勘误：这一串旧稿整体偏 +4/+5（例如 filePath 写 91、实际 87），是按别的 Qt 版本抄的。
//     下面全部是**本机 6.12.0 数出来的**，改版本时要重新数。
//   第 83 exists() const / 第 84 static exists(const QString &file)
//   第 87 filePath / 88 absoluteFilePath / 89 canonicalFilePath
//   第 98 fileName / 99 baseName / 100 completeBaseName / 101 suffix / 103 completeSuffix
//        （102 行夹着一个 bundleName()，macOS 用，别被它带偏行号）
//        ← "a.tar.gz"：suffix() = "gz"，completeSuffix() = "tar.gz"，
//           baseName() = "a.tar"，completeBaseName() = "a"。拆包名一定要用 complete 版。
//   第 105 path / 115 dir() / 116 absoluteDir()
//   第 128 isFile() / 129 isDir() / 130 isSymLink() / 131 isSymbolicLink() / 132 isOther()
//   第 159 permission(QFile::Permissions) / 160 permissions()
//   第 162 qint64 size() const;   ← **静态拿大小**：QFileInfo(path).size()，不用开文件
//   第 164~168 不带时区的一组：birthTime / metadataChangeTime / lastModified / lastRead
//        / fileTime(QFile::FileTime)（164~167 都是内联转调 fileTime，168 才是本体）
//   第 170~174 同一组函数的 QTimeZone 重载（174 fileTime(time, tz)）
//   第 176 caching() / 177 setCaching() / 178 stat()（强制刷新缓存）
//   第 193 Q_DECLARE_SHARED(QFileInfo)；第 195 typedef QList<QFileInfo> QFileInfoList;
// ============================================================================

// ============================================================================
// 二、小工具：把错误码翻成人话、把字节数翻得能对齐
// ============================================================================
static QString errName(QFileDevice::FileError e)
{
    switch (e) {
    case QFileDevice::NoError:         return QStringLiteral("NoError");
    case QFileDevice::ReadError:       return QStringLiteral("ReadError");
    case QFileDevice::WriteError:      return QStringLiteral("WriteError");
    case QFileDevice::FatalError:      return QStringLiteral("FatalError");
    case QFileDevice::ResourceError:   return QStringLiteral("ResourceError");
    case QFileDevice::OpenError:       return QStringLiteral("OpenError");
    case QFileDevice::AbortError:      return QStringLiteral("AbortError");
    case QFileDevice::TimeOutError:    return QStringLiteral("TimeOutError");
    case QFileDevice::UnspecifiedError:return QStringLiteral("UnspecifiedError");
    case QFileDevice::RemoveError:     return QStringLiteral("RemoveError");
    case QFileDevice::RenameError:     return QStringLiteral("RenameError");
    case QFileDevice::PositionError:   return QStringLiteral("PositionError");
    case QFileDevice::ResizeError:     return QStringLiteral("ResizeError");
    case QFileDevice::PermissionsError:return QStringLiteral("PermissionsError");
    case QFileDevice::CopyError:       return QStringLiteral("CopyError");
    }
    return QStringLiteral("未知(%1)").arg(int(e));
}

static QString lockErrName(QLockFile::LockError e)
{
    switch (e) {
    case QLockFile::NoError:          return QStringLiteral("NoError");
    case QLockFile::LockFailedError:  return QStringLiteral("LockFailedError");
    case QLockFile::PermissionError:  return QStringLiteral("PermissionError");
    case QLockFile::UnknownError:     return QStringLiteral("UnknownError");
    }
    return QStringLiteral("未知(%1)").arg(int(e));
}

static QString kb(qint64 bytes)
{
    // ★ 别写成 QStringLiteral("%1 B (%2.1 KiB)")：QString::arg 会把 "%2.1" 里的
    //   **%2 当成第二个占位符**，剩下的 ".1" 原样留下，输出成 "0.5.1 KiB"。
    //   要格式化小数就老实用 asprintf。
    return QString::asprintf("%lld B (%.1f KiB)",
                             static_cast<long long>(bytes), double(bytes) / 1024.0);
}

// 数行数（不看编码，只看换行符，用来和数据条数对得上）
static int countNewlines(const QByteArray &data)
{
    int n = 0;
    for (char c : data) {
        if (c == '\n')
            ++n;
    }
    return n;
}

static void showFile(const QString &dir, const QString &caption)
{
    QDir d(dir);
    const QFileInfoList list = d.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot,
                                               QDir::Name);
    qInfo() << "  " << caption << "：" << dir << " 共" << list.size() << "项";
    for (const QFileInfo &fi : list) {
        qInfo() << QString::asprintf("     %-26s %10lld B  可读=%d 可写=%d 是文件=%d 是目录=%d",
                                     fi.fileName().toLocal8Bit().constData(),
                                     static_cast<long long>(fi.size()),
                                     int(fi.isReadable()), int(fi.isWritable()),
                                     int(fi.isFile()), int(fi.isDir()));
    }
}

// ============================================================================
// 三、步骤①② 建目录 + QFile 的动词全套
// ============================================================================
static void stepWriteAndRead(const QString &dir)
{
    qInfo() << "\n【步骤①】建自己的目录";
    const QString path = QDir(dir).filePath(QStringLiteral("notes.txt"));
    qInfo() << "  目标 =" << QDir::toNativeSeparators(path);
    qInfo() << "  openMode 的位（本机 qiodevicebase.h 第 17~28 行，十六进制自己看）：";
    qInfo() << QString::asprintf("     ReadOnly=0x%04x WriteOnly=0x%04x ReadWrite=0x%04x Append=0x%04x",
                                 int(QIODeviceBase::ReadOnly), int(QIODeviceBase::WriteOnly),
                                 int(QIODeviceBase::ReadWrite), int(QIODeviceBase::Append));
    qInfo() << QString::asprintf("     Truncate=0x%04x Text=0x%04x Unbuffered=0x%04x NewOnly=0x%04x ExistingOnly=0x%04x",
                                 int(QIODeviceBase::Truncate), int(QIODeviceBase::Text),
                                 int(QIODeviceBase::Unbuffered), int(QIODeviceBase::NewOnly),
                                 int(QIODeviceBase::ExistingOnly));
    qInfo() << "  ★ ReadWrite 是 0x0003 = ReadOnly|WriteOnly，**不是独立的位**；"
               "所以 `QIODevice::ReadOnly | QIODevice::Append` 是「只读 + 追加」这种自相矛盾的组合，"
               "行为由具体设备决定（QFile 上 append 只在可写时生效）。别把矛盾的组合写进代码。";

    // ── 写 ──
    qInfo() << "\n【步骤②】QFile 的写 → 读 → 定位 全套";
    const QByteArray body = QByteArray("第一行内容\n") + QByteArray("second line ascii\n")
                          + QByteArray("第三行带中文和标点。\n");
    {
        QFile f(path);
        const bool ok = f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        qInfo() << "  open(WriteOnly|Truncate) =" << ok
                << "｜isOpen =" << f.isOpen() << "｜isWritable =" << f.isWritable()
                << "｜isReadable =" << f.isReadable()
                << "｜isSequential =" << f.isSequential()
                << "← QFile 不是顺序设备，所以能 seek（第 118 行的 isSequential）";
        if (!ok) {
            qInfo() << "  打开失败：error =" << errName(f.error())
                    << "｜errorString =[" << f.errorString() << "]";
            return;
        }
        const qint64 w1 = f.write(body);
        qInfo() << "  write(QByteArray) 返回实际写入字节 =" << w1
                << "（第 102 行）｜bytesToWrite =" << f.bytesToWrite()
                << "← 返回值**必须比对**，短写了就是磁盘满/权限问题";
        qInfo() << "  写入后 pos =" << f.pos() << "｜size =" << f.size()
                << "｜atEnd =" << f.atEnd();
        const qint64 w2 = f.write(QByteArray("追加的一小段，没有换行"));
        qInfo() << "  再 write 一段 =" << w2 << " 字节｜现在 size =" << f.size()
                << "｜pos =" << f.pos();
        f.flush();                                 // QFileDevice 第 126 行
        qInfo() << "  flush() 之后 close()";
        f.close();
        qInfo() << "  close 之后 isOpen =" << f.isOpen() << "｜error =" << errName(f.error());
    }
    const qint64 diskSize = QFileInfo(path).size();   // qfileinfo.h 第 166 行
    qInfo() << "  磁盘上（QFileInfo::size，不用开文件）=" << kb(diskSize);

    // ── 读 ──
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            qInfo() << "  读打开失败：" << errName(f.error()) << f.errorString();
            return;
        }
        qInfo() << "  只读打开：openMode 数值 =" << int(f.openMode())
                << "｜size =" << f.size() << "｜pos =" << f.pos() << "｜atEnd =" << f.atEnd();
        const QByteArray head = f.read(12);            // 第 81 行 QByteArray read(qint64)
        qInfo() << "  read(12) → 实际拿到" << head.size() << "字节，内容是（**按 UTF-8 解**）：["
                << QString::fromUtf8(head) << "]"
                << "｜pos 现在 =" << f.pos()
                << "← 注意 read(12) 给你 12 个**字节**，不是 12 个字符；"
                   "一个中文字在 UTF-8 里占 3 字节，所以这里被切断了（下一条就证明）";
        const QByteArray raw = f.readAll();            // 第 82 行
        qInfo() << "  接着 readAll() 拿到" << raw.size() << "字节｜pos =" << f.pos()
                << "｜atEnd =" << f.atEnd()
                << "（pos 已到 size，所以 atEnd 为真）";
        const QByteArray whole = QString::fromUtf8(head + raw).toUtf8();
        qInfo() << "  两段拼回来 == 原文字节数 =" << (whole.size() == diskSize)
                << "（" << whole.size() << " vs " << diskSize << "）";

        // seek 与 reset
        const bool seekOk = f.seek(0);
        qInfo() << "  seek(0) =" << seekOk << "｜pos =" << f.pos()
                << "｜bytesAvailable =" << f.bytesAvailable() << "（第 77 行）";
        const QByteArray firstLine = f.readLine();     // 第 84 行，maxlen=0 表示读到换行为止
        qInfo() << "  readLine() →" << firstLine.size() << "字节 ["
                << QString::fromUtf8(firstLine).trimmed() << "]"
                << "｜canReadLine =" << f.canReadLine() << "｜pos =" << f.pos();
        const qint64 skipped = f.skip(6);              // 第 106 行
        qInfo() << "  skip(6) 返回实际跳过 =" << skipped << "｜pos =" << f.pos();
        const bool seekPast = f.seek(diskSize + 100);
        qInfo() << "  seek 到 size 之后 =" << seekPast
                << "｜error =" << errName(f.error())
                << "（QFile 允许 seek 越过文件尾，读出来是空；**这不是错误**，"
                   "但 QIODevice 里位置类失败会置 PositionError=11）";
        f.unsetError();                                // 第 114 行
        qInfo() << "  unsetError() 之后 error =" << errName(f.error());
        f.close();
    }
}

// ============================================================================
// 四、步骤③ 文本模式 vs 二进制模式
// ============================================================================
static void stepTextVsBinary(const QString &dir)
{
    qInfo() << "\n【步骤③】Text 位改的是换行翻译，**不是编码**";
    const QString path = QDir(dir).filePath(QStringLiteral("crlf.txt"));
    // 故意写「只有 \n」的字节进去；Windows 下 Text 模式读时会被翻成 \n，
    // 而**用二进制模式**才能看到写盘时 QFile 自己有没有加 \r。
    {
        QFile w(path);
        if (!w.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qInfo() << "  写打开失败：" << w.errorString();
            return;
        }
        w.write(QByteArray("aaa\nbbb\r\nccc\n"));
        w.close();
    }
    const qint64 realSize = QFileInfo(path).size();
    {
        QFile b(path);
        const bool openedBin = b.open(QIODevice::ReadOnly);  // 二进制：一个字节都不翻译
        if (!openedBin) { qWarning() << "  二进制打开失败：" << b.errorString(); }
        const QByteArray bin = b.readAll();
        int lf = 0, cr = 0;
        for (char c : bin) { if (c == '\n') ++lf; if (c == '\r') ++cr; }
        qInfo() << "  二进制读：磁盘" << realSize << "字节，readAll 拿到" << bin.size()
                << "字节｜\\n 个数 =" << lf << "｜\\r 个数 =" << cr
                << "｜isTextModeEnabled =" << b.isTextModeEnabled();
        b.close();
    }
    {
        QFile t(path);
        const bool openedText = t.open(QIODevice::ReadOnly | QIODevice::Text);  // 第 24 行的 Text 位
        if (!openedText) { qWarning() << "  Text 模式打开失败：" << t.errorString(); }
        const QByteArray txt = t.readAll();
        int cr = 0;
        for (char c : txt) { if (c == '\r') ++cr; }
        qInfo() << "  Text 模式读：拿到" << txt.size() << "字节｜\\r 个数 =" << cr
                << "｜isTextModeEnabled =" << t.isTextModeEnabled();
        t.seek(0);
        const qint64 posAfterSeek = t.pos();
        const QByteArray line1 = t.readLine();
        qInfo() << "  Text 模式下 seek(0) 后 pos =" << posAfterSeek
                << "｜readLine 长度 =" << line1.size() << "｜pos =" << t.pos();
        t.close();
        qInfo() << "  → 结论（★ 进度条的坑就在这）：Text 模式下 Qt 会做换行翻译，"
                   "**pos() 与磁盘真实偏移不再一一对应**。要做「读到百分之几」，"
                   "请用二进制模式打开 + 自己处理换行，或者拿 QFileInfo::size() 做分母、"
                   "readLine 的行数做分子，别拿 pos() 当精确进度。";
    }
}

// ============================================================================
// 五、步骤④⑤ QTextStream：编码（Qt6 只有 setEncoding）与 locale 的坑
// ============================================================================
static void stepTextStream(const QString &dir)
{
    qInfo() << "\n【步骤④】QTextStream 的编解码：setCodec 在 Qt6 已经不存在";
    const QString utf8Path = QDir(dir).filePath(QStringLiteral("ts_utf8.txt"));
    const QString latinPath = QDir(dir).filePath(QStringLiteral("ts_latin1.txt"));
    const QString sample = QStringLiteral("配置说明：温度上限 85.5 度，湿度 60%。");

    // (a) 默认编码写一份，看看磁盘上到底是什么字节
    {
        QFile f(utf8Path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qInfo() << "  打开失败：" << f.errorString();
            return;
        }
        QTextStream ts(&f);
        qInfo() << "  新建 QTextStream 的默认 encoding 枚举值 =" << int(ts.encoding())
                << "（0=Utf8, 1=Utf16, 2=Utf16LE, 3=Utf16BE, 4=Utf32, 5=Utf32LE, "
                   "6=Utf32BE, 7=Latin1, 8=System —— 见 qstringconverter_base.h 102~115 行）";
        qInfo() << "  generateByteOrderMark =" << ts.generateByteOrderMark()
                << "｜autoDetectUnicode =" << ts.autoDetectUnicode();
        ts << sample << Qt::endl;              // ★ Qt6 的操纵符在 Qt:: 下（第 253 行）
        ts.flush();
        qInfo() << "  写完成后 status =" << int(ts.status()) << "（0=Ok）｜if(!ts) 判到 =" << bool(ts);
        f.close();
    }
    const QByteArray utf8Raw = [&]{ QFile rf(utf8Path);
                                    if (!rf.open(QIODevice::ReadOnly)) return QByteArray{};
                                    const QByteArray d = rf.readAll(); rf.close(); return d; }();
    qInfo() << "  磁盘上：" << kb(utf8Raw.size())
            << "｜首 3 字节 =" << utf8Raw.left(3).toHex()
            << "（**没有 EF BB BF 就说明没写 BOM**）";
    qInfo() << "  原串" << sample.size() << "个 QChar → 文件" << utf8Raw.size()
            << "字节｜用 QString::fromUtf8 读回来 =="
            << (QString::fromUtf8(utf8Raw).trimmed() == sample);

    // (b) 同一句中文按 Latin1 写：Latin1 只有 0..255，中文全部保不住
    {
        QFile f(latinPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qInfo() << "  打开失败：" << f.errorString();
            return;
        }
        QTextStream ts(&f);
        ts.setEncoding(QStringConverter::Latin1);      // ★ Qt6 的正解，setCodec 已删
        qInfo() << "  setEncoding(Latin1) 之后 encoding() =" << int(ts.encoding());
        ts << sample << Qt::endl;
        ts.flush();
        f.close();
    }
    {
        QFile rf(latinPath);
        const bool latinOpened = rf.open(QIODevice::ReadOnly);
        const QByteArray d = latinOpened ? rf.readAll() : QByteArray{};
        rf.close();
        qInfo() << "  Latin1 那份磁盘字节数 =" << d.size() << "（UTF-8 那份是 "
                << utf8Raw.size() << "）｜前 40 字节 hex =" << d.left(40).toHex(' ');
        qInfo() << "  它按 UTF-8 解释回来 =[" << QString::fromUtf8(d).trimmed()
                << "] ← **中文已经变成 ? 或丢失**：Latin1 装不下汉字，"
                   "这一步是**不可逆的**，写坏就救不回来。";
    }
    qInfo() << "  → 编码三条纪律：① 自己写的文本一律显式 setEncoding(QStringConverter::Utf8)，"
               "别赌默认值；② 想兼容 GBK 之类的老文件，QStringConverter 里**没有对应枚举**，"
               "只能自己在 QByteArray 层面转，或者要求上游换成 UTF-8；"
               "③ 写 UTF-16 才考虑 BOM，UTF-8 加 BOM 会把下游解析器（含 JSON）顶死。";

    // (c) locale：小数点从哪儿来
    qInfo() << "\n【步骤⑤】setLocale：数字格式跟着 locale 走";
    const QString numPath = QDir(dir).filePath(QStringLiteral("locale_num.txt"));
    {
        QFile f(numPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return;
        QTextStream ts(&f);
        ts.setEncoding(QStringConverter::Utf8);
        ts.setLocale(QLocale::c());
        const QString cName = ts.locale().name();
        ts << QStringLiteral("C  locale: ") << 0.85 << Qt::endl;
        QTextStream ts2(&f);                    // 同一个 device 再挂一个流，换 locale
        ts2.setLocale(QLocale(QStringLiteral("de_DE")));
        ts2 << QStringLiteral("de_DE locale: ") << 0.85 << Qt::endl;
        qInfo() << "  第一次 locale().name() =[" << cName << "]"
                << "｜改过之后 ts2.locale().name() =[" << ts2.locale().name() << "]"
                << "（若本机没有德语数据，Qt 会退回 C，打印出来的名字会一样——那也是证据）";
        ts.flush();
        f.close();
    }
    {
        QFile rf(numPath);
        if (!rf.open(QIODevice::ReadOnly))              // nodiscard：开没开上必须表态
            qWarning() << "  locale 回读：open 失败" << rf.errorString();
        QTextStream rts(&rf);
        rts.setEncoding(QStringConverter::Utf8);
        rts.setLocale(QLocale::c());           // 读的时候也必须有对应的 locale
        QString line;
        int ln = 0;
        while (rts.readLineInto(&line)) {
            ++ln;
            qInfo() << "  第" << ln << "行 [" << line << "]";
        }
        qInfo() << "  行数 =" << ln << "｜读末尾 status =" << int(rts.status())
                << "（2=ReadPastEnd，readLineInto 读到结尾返回 false，正常）";
        double dv = 0.0;
        bool okNum = false;
        rf.close();
        if (!rf.open(QIODevice::ReadOnly))              // 同上：第二次打开也要接住返回值
            qWarning() << "  二次打开：open 失败" << rf.errorString();
        QTextStream rts2(&rf);
        rts2.setLocale(QLocale::c());
        QString word;
        rts2.skipWhiteSpace();
        rts2 >> word;                            // 第 157 行 operator>>(QString&)：吃一个空白分隔的词
        rts2 >> dv;                              // 第 156 行 operator>>(double&)
        qInfo() << "  以 C locale 解析德语那份数字：读到词 [" << word << "]"
                << "｜再 >> double 得到" << dv
                << "← 如果上一行真写出「0,85」，这里就读成 0 或半个数："
                   "**写用德语、读用 C 就是这种后果**（本机若德语数据缺失则两行相同，说明 locale 没生效）。";
        Q_UNUSED(okNum)
        rf.close();
    }

    // (d) 对比「不走 QTextStream，直接 QByteArray → QString」
    qInfo() << "\n  【对照】绕开 QTextStream，用 QByteArray 自己转";
    {
        QFile rf(utf8Path);
        if (!rf.open(QIODevice::ReadOnly))              // nodiscard：这次也得接住
            qWarning() << "  对照读：open 失败" << rf.errorString();
        const QByteArray d = rf.readAll(); rf.close();
        const QString viaUtf8 = QString::fromUtf8(d);
        const QString viaLatin1 = QString::fromLatin1(d);
        qInfo() << "  同一批字节 fromUtf8 →" << viaUtf8.size() << "个 QChar"
                << "｜fromLatin1 →" << viaLatin1.size() << "个 QChar"
                << "｜两者相等 =" << (viaUtf8 == viaLatin1);
        qInfo() << "  → **字节数 ≠ 字符数**：UTF-8 下一个汉字占 3 字节。"
                   "fromLatin1 把每个字节都当成一个字符，所以更长但内容全错。"
                   "这就是「pos() 当字数、size() 当字节数」混用会出鬼的原因。";
    }
}

// ============================================================================
// 六、步骤⑥ 大文件：readAll vs 分块
// ============================================================================
static void stepBigFile(const QString &dir)
{
    qInfo() << "\n【步骤⑥】大文件不要用 readAll()";
    const QString path = QDir(dir).filePath(QStringLiteral("chunk.bin"));
    const qint64 total = qint64(4) * 1024 * 1024;        // 4 MiB，够说明问题又不慢
    {
        QFile w(path);
        if (!w.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qInfo() << "  写打开失败：" << w.errorString();
            return;
        }
        const QByteArray block(64 * 1024, char(0x5A));
        QElapsedTimer t; t.start();
        qint64 wrote = 0;
        while (wrote < total) {
            const qint64 chunk = qMin<qint64>(block.size(), total - wrote);
            const qint64 n = w.write(block.constData(), chunk);   // 第 100 行：char* + 长度
            if (n <= 0) {
                qInfo() << "  写中断：error =" << errName(w.error()) << w.errorString();
                break;
            }
            wrote += n;
        }
        qInfo() << "  分块写" << wrote << "字节（" << kb(wrote) << "），"
                << int(total / (64 * 1024)) << " 次 write，耗时" << t.elapsed() << "ms"
                << "｜close 后 QFileInfo::size =" << QFileInfo(path).size();
        w.close();
    }
    // 分块读
    {
        QFile r(path);
        if (!r.open(QIODevice::ReadOnly))
            return;
        QByteArray buf(64 * 1024, '\0');
        QElapsedTimer t; t.start();
        qint64 got = 0, rounds = 0;
        while (!r.atEnd()) {
            const qint64 n = r.read(buf.data(), buf.size());      // 第 80 行
            if (n < 0) {
                qInfo() << "  read 返回负数 = 出错：" << errName(r.error());
                break;
            }
            if (n == 0)
                break;                       // 防死循环：非 Unbuffered 时一般不会到这
            got += n;
            ++rounds;
        }
        // 【为什么整串都用 %lld + static_cast】buf.size() 是 qsizetype（Qt6 里就是 long long），
        // 写成 %d 会被 -Wformat 当场抓到（本机就是这么发现的）：变参函数不看类型，
        // 只有格式串对得上才安全。要么全 %lld 配显式 cast，要么改用 QStringLiteral().arg()。
        qInfo() << QString::asprintf("  分块读：累计 %lld 字节（%s），循环 %lld 次，耗时 %lld ms；"
                                     "峰值内存 ≈ 一块 %lld 字节",
                                     static_cast<long long>(got),
                                     kb(got).toLocal8Bit().constData(),
                                     static_cast<long long>(rounds),
                                     static_cast<long long>(t.elapsed()),
                                     static_cast<long long>(buf.size()));
        r.close();
    }
    // readAll 读
    {
        QFile r(path);
        if (!r.open(QIODevice::ReadOnly))
            return;
        QElapsedTimer t; t.start();
        const QByteArray all = r.readAll();                        // 第 82 行
        qInfo() << QString::asprintf("  readAll()：%lld 字节一次性进内存，耗时 %lld ms"
                                     "｜字节数与分块读一致 ="
                                     " %s",
                                     static_cast<long long>(all.size()), static_cast<long long>(t.elapsed()),
                                     all.size() == total ? "true" : "false");
        qInfo() << "  → 4 MiB 无所谓；**400 MiB 就是一次 OOM**。"
                   "分块的代价只是几行循环，换来的是内存上限可控 + 能报真进度。"
                   "只读的超大文件还可以走 QFileDevice::map()（第 140 行）内存映射。";
        r.close();
    }
}

// ============================================================================
// 七、步骤⑦ QSaveFile：原子替换为什么防得住「写坏」
// ============================================================================
static void stepSaveFile(const QString &dir)
{
    qInfo() << "\n【步骤⑦】QSaveFile：先写临时件，再改名换上";
    const QString target = QDir(dir).filePath(QStringLiteral("state.cfg"));

    // 先放一份旧内容
    {
        QFile w(target);
        if (!w.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return;
        w.write(QByteArray("旧版本：version=1\n"));
        w.close();
    }
    const QByteArray before = [&]{ QFile rf(target);
                                   if (!rf.open(QIODevice::ReadOnly)) return QByteArray{};   // nodiscard
                                   const QByteArray d = rf.readAll(); rf.close(); return d; }();
    qInfo() << "  保存前目标文件：" << kb(before.size()) << "内容 =[" << before.trimmed() << "]";

    // (a) 正常 commit()
    {
        QSaveFile sf(target);                     // qsavefile.h 第 34 行（parent 有默认值）
        if (!sf.open(QIODevice::WriteOnly)) {     // ★ 只支持 WriteOnly，给 ReadWrite 会失败
            qInfo() << "  QSaveFile 打开失败：" << errName(sf.error()) << sf.errorString();
            return;
        }
        qInfo() << "  QSaveFile 已打开，此时 fileName() 指向的目标 ="
                << QDir::toNativeSeparators(sf.fileName());
        sf.write(QByteArray("新版本：version=2\n"));
        sf.write(QByteArray("作者=老师傅\n"));
        const bool committed = sf.commit();       // 第 46 行
        qInfo() << "  commit() =" << committed << "｜写过的字节还在缓冲里的部分由 commit 刷完"
                << "（★ 记住：**不能调 close()**，它在第 71 行的 private 段）";
    }
    const QByteArray after = [&]{ QFile rf(target);
                                  if (!rf.open(QIODevice::ReadOnly)) return QByteArray{};   // nodiscard
                                  const QByteArray d = rf.readAll(); rf.close(); return d; }();
    qInfo() << "  commit 之后目标文件：" << kb(after.size()) << "内容 =[" << after.trimmed() << "]";
    qInfo() << "  内容真的换掉了 =" << (before != after)
            << "｜行数（按 \\n 数）从" << countNewlines(before) << "变成" << countNewlines(after);

    // (b) cancelWriting()：不 commit 就撤，目标保持原样
    const QByteArray beforeCancel = after;
    {
        QSaveFile sf(target);
        if (!sf.open(QIODevice::WriteOnly))
            return;
        sf.write(QByteArray("这段如果被写下去，配置就毁了：\n"));
        sf.write(QByteArray("<半截的 XML"));       // 故意写个不完整的东西
        sf.cancelWriting();                        // 第 48 行：放弃
        qInfo() << "  写了半截然后 cancelWriting()（第 48 行）";
    }
    {
        QFile rf(target);
        if (!rf.open(QIODevice::ReadOnly))              // nodiscard：撤销后的回读也要接住
            qWarning() << "  撤销回读：open 失败" << rf.errorString();
        const QByteArray d = rf.readAll(); rf.close();
        qInfo() << "  撤销之后目标文件字节数 =" << d.size()
                << "｜内容与撤销前一致 =" << (d == beforeCancel)
                << "｜内容 =[" << d.trimmed() << "]";
    }
    showFile(dir, QStringLiteral("QSaveFile 演示后目录（临时件不该留下）"));
    qInfo() << "  → 原理：QSaveFile 把数据写进同目录下一个临时文件，commit 成功后才 rename 覆盖目标。"
               "于是「掉电/崩溃/磁盘满」发生的时候，目标要么还是完整的旧版、要么是完整的新版，"
               "**不会出现半截文件**。这就是 QSettings::isAtomicSyncRequired()（第 118 行）"
               "背后同一个思路，也是「保存」按钮该用的类。";
    qInfo() << "     三条边界：① 只支持 WriteOnly；② 跨分区/网络盘 rename 会失败，"
               "要么 setDirectWriteFallback(true)（第 50 行）退回直写，要么接受失败；"
               "③ 它保的是**单个文件的一致性**，多个文件之间的原子性它管不了。";
    qInfo() << "     待复核：析构时到底算 commit 还是 cancel，本机头文件里看不出来"
               "（close() 在 private，第 71 行）。所以纪律是：**一定要显式 commit() 或 cancelWriting()**，"
               "别把数据交给一个你没读过语义的析构函数。";
}

// ============================================================================
// 八、步骤⑧ QTemporaryFile + QLockFile
// ============================================================================
static void stepTempAndLock(const QString &dir)
{
    qInfo() << "\n【步骤⑧】QTemporaryFile 与 QLockFile";
    const QString listCaption = QStringLiteral("临时件演示前后目录对照");

    // (a) QTemporaryFile：XXXXXX 模板 + 默认 autoRemove
    const QString templatePath = QDir(dir).filePath(QStringLiteral("cfg_XXXXXX.ini"));
    qint64 tempSize = -1;
    QString tempName;
    showFile(dir, listCaption);
    {
        QTemporaryFile tf(templatePath);            // qtemporaryfile.h 第 32 行
        qInfo() << "  fileTemplate() =[" << QDir::toNativeSeparators(tf.fileTemplate()) << "]";
        if (!tf.open()) {                            // ★ 第 52 行：**无参 open()**，带参那份是 protected
            qInfo() << "  QTemporaryFile 打开失败：" << tf.errorString();
            return;
        }
        tempName = tf.fileName();
        qInfo() << "  无参 open() 成功，自动生成的 fileName =["
                << QDir::toNativeSeparators(tempName) << "]"
                << "（XXXXXX 已被换成随机串）｜openMode =" << int(tf.openMode())
                << "（无参 open 固定用 ReadWrite，见第 52 行）";
        qInfo() << "  autoRemove()（第 48 行）默认 =" << tf.autoRemove();
        tf.write(QByteArray("[临时]\nkey=tempvalue\n"));
        tf.flush();
        tempSize = tf.size();
        qInfo() << "  写入" << tempSize << "字节，QFileInfo 核对 ="
                << QFileInfo(tempName).size()
                << "｜exists =" << QFileInfo(tempName).exists();
        // 转正的正确姿势（演示完就不真 rename，避免留垃圾）
        qInfo() << "  想留下请用 rename()（第 66 行）/ renameOverwrite()（第 67 行）"
                   "或先 setAutoRemove(false)（第 49 行）；本讲走 removeRecursively 清理。";
    }   // ← 析构：autoRemove 为 true，文件被删
    {
        qInfo() << "  出了作用域（对象已析构）：写入" << tempSize << "字节的那个临时文件"
                << tempName << " 还在吗 =" << QFileInfo(tempName).exists()
                << "← autoRemove=true 的行为，临时件自己走人";
    }

    // (b) QLockFile：防多开
    const QString lockPath = QDir(dir).filePath(QStringLiteral("app.lock"));
    {
        QLockFile first(lockPath);                  // qlockfile.h 第 21 行，**不是 QObject**
        const bool got1 = first.tryLock(std::chrono::milliseconds(0));
        qInfo() << "  第一个实例 tryLock(0ms) =" << got1
                << "｜isLocked =" << first.isLocked()
                << "｜error =" << lockErrName(first.error())
                << "｜锁文件本身在不在 =" << QFileInfo(lockPath).exists();
        qint64 pid = -1;
        QString host, appname;
        const bool hasInfo = first.getLockInfo(&pid, &host, &appname);
        qInfo() << "  getLockInfo：读到 =" << hasInfo << "｜pid =" << pid
                << "｜host =[" << host << "]｜app =[" << appname << "]";
        qInfo() << "  staleLockTime（第 34 行，int 版）=" << first.staleLockTime() << "ms"
                << "｜chrono 版 =" << first.staleLockTimeAsDuration().count() << "ms"
                << "← 超过这个年龄、且持有进程已经不在了，锁才算过期、可被抢（第 43 行 removeStaleLockFile）";

        QLockFile second(lockPath);
        const bool got2 = second.tryLock(std::chrono::milliseconds(50));
        qInfo() << "  同进程第二个实例 tryLock(50ms) =" << got2
                << "｜error =" << lockErrName(second.error())
                << "｜isLocked =" << second.isLocked();
        qInfo() << "  → 语义说明（保守）：**防多开要在两个进程之间测**才说明问题。"
                   "同一进程里第二个实例的结果取决于 Qt 对「pid 是自己」的判定，"
                   "本讲只把两个布尔打出来，不替它承诺结论。真做防多开请照下面三条：";
        qInfo() << "     ① 用 tryLock(timeout)，**不要用阻塞的 lock()（第 24 行）**——"
                   "无人值守程序一阻塞就成了假死；";
        qInfo() << "     ② 拿到锁之后立刻 setStaleLockTime() 定一个比你最长运行时间短的值，"
                   "让崩过的进程能被抢回来；";
        qInfo() << "     ③ 锁文件放在**用户可写的**目录（本讲是临时目录；真实产品放 "
                   "QStandardPaths::RuntimeLocation 或 TempLocation，别放安装目录）。";

        first.unlock();                               // 第 30 行
        qInfo() << "  显式 unlock() 之后 isLocked =" << first.isLocked();
        const bool got3 = second.tryLock(std::chrono::milliseconds(0));
        qInfo() << "  unlock 之后第二个实例再 tryLock =" << got3
                << "（预期 true：锁真的放掉了）";
    }   // ← QLockFile 析构自动解锁
    qInfo() << "  对象全部出作用域后再看锁文件是否存在 =" << QFileInfo(lockPath).exists()
            << "（unlock 会把它删掉；崩过的进程才会留下孤儿锁，那是 staleLockTime 的活）";
}

// ============================================================================
// 九、步骤⑨⑩ QFileInfo / QDir 元信息与遍历 + 错误处理
// ============================================================================
static void stepDirAndErrors(const QString &dir)
{
    qInfo() << "\n【步骤⑨】QFileInfo 的拆名与 QDir 的遍历";
    // 拆名：造一个多点文件名来把 suffix 讲清楚
    const QString oddName = QStringLiteral("report.tar.gz");
    const QString oddPath = QDir(dir).filePath(oddName);
    {
        QFile w(oddPath);
        if (w.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            w.write(QByteArray("假装是个压缩包\n"));
            w.close();
        }
    }
    {
        const QFileInfo fi(oddPath);                  // qfileinfo.h 第 44 行（explicit）
        qInfo() << "  文件名 [" << oddName << "]：fileName =[" << fi.fileName()
                << "] baseName =[" << fi.baseName() << "] completeBaseName =["
                << fi.completeBaseName() << "]";
        qInfo() << "     suffix =[" << fi.suffix() << "] completeSuffix =["
                << fi.completeSuffix() << "]"
                << "← **拆「去掉扩展名」请用 completeBaseName()**，"
                   "baseName() 只砍最后一个点，" + oddName + " 会得到 report.tar";
        qInfo() << "     size =" << fi.size() << "｜isFile =" << fi.isFile()
                << "｜isDir =" << fi.isDir() << "｜isSymLink =" << fi.isSymLink()
                << "｜exists =" << fi.exists();
        qInfo() << "     path =[" << QDir::toNativeSeparators(fi.path())
                << "]｜absolutePath =[" << QDir::toNativeSeparators(fi.absolutePath())
                << "]｜canonicalFilePath =["
                << QDir::toNativeSeparators(fi.canonicalFilePath()) << "]";
        qInfo() << "     lastModified =" << fi.lastModified().toString(Qt::ISODate)
                << "｜birthTime 有效性 =" << fi.birthTime().isValid()
                << "（第 168 行；不是所有文件系统都有创建时间，ext4 上可能是无效 QDateTime）";
        const QFile::Permissions perm = fi.permissions();
        qInfo() << "     permissions 逐位：ReadOwner =" << perm.testFlag(QFileDevice::ReadOwner)
                << " WriteOwner =" << perm.testFlag(QFileDevice::WriteOwner)
                << " ReadOther =" << perm.testFlag(QFileDevice::ReadOther)
                << "（第 97~102 行的位；Windows 上 User/Group 那几位常常和 Owner 重复）";
    }

    // 遍历：三种 filters/sortFlags 组合
    QDir d(dir);
    qInfo() << "  QDir(path)：path =[" << QDir::toNativeSeparators(d.path()) << "]"
            << "absolutePath =[" << QDir::toNativeSeparators(d.absolutePath()) << "]"
            << "canonicalPath =[" << QDir::toNativeSeparators(d.canonicalPath()) << "]"
            << "（三者对相对路径才不同；本讲全程用绝对路径，所以三者一致）";
    qInfo() << "  dirName() =[" << d.dirName() << "]｜exists =" << d.exists()
            << "｜isReadable =" << d.isReadable() << "｜isEmpty(默认过滤 . 与 ..) ="
            << d.isEmpty();
    qInfo() << "  separator() ='" << QDir::separator() << "'（Windows 给 \\\\，POSIX 给 /）"
                << "｜cleanPath(\"a/./b//c\") =[" << QDir::cleanPath(QStringLiteral("a/./b//c")) << "]";
    qInfo() << "  count() 的类型：本机 qdir.h 第 153~156 行，Qt6.12 返回 **qsizetype**；"
            << "现在 dir.count()（含 . 与 ..，用 AllEntries）=" << d.count();

    const QFileInfoList all = d.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot,
                                              QDir::Name);
    qInfo() << "  entryInfoList(AllEntries|NoDotAndDotDot, Name) →" << all.size() << "项：";
    for (const QFileInfo &fi : all)
        qInfo() << QString::asprintf("     %-28s %10lld B  %s",
                                     fi.fileName().toLocal8Bit().constData(),
                                     static_cast<long long>(fi.size()),
                                     fi.isDir() ? "DIR" : "FILE");

    const QFileInfoList onlyTxt = d.entryInfoList(QStringList{ QStringLiteral("*.txt") },
                                                  QDir::Files | QDir::NoDotAndDotDot,
                                                  QDir::Name);
    const QFileInfoList bySize = d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot,
                                                 QDir::Size | QDir::Reversed);
    const auto namesOf = [](const QFileInfoList &l) {
        QStringList n;
        for (const QFileInfo &f : l)
            n << f.fileName();
        return n;
    };
    const auto top3BySize = [](const QFileInfoList &l) {
        QStringList n;
        for (qsizetype i = 0; i < l.size() && i < 3; ++i)
            n << QStringLiteral("%1(%2B)").arg(l.at(i).fileName()).arg(l.at(i).size());
        return n;
    };
    qInfo() << "  带 nameFilters 的那份（\"*.txt\"）→" << onlyTxt.size() << "项：" << namesOf(onlyTxt);
    qInfo() << "  按 Size|Reversed 排的前三项：" << top3BySize(bySize);
    qInfo() << "  → ★ entryInfoList 每项都要 stat 一次，目录大就是几十万次系统调用。"
               "只要名字的用 **entryList()**（第 166 行）；只关心大小的用 SortFlags=Size；"
               "别在 UI 线程遍历大目录（第 35~37 行的 QDir::Modified 已弃用，想按时间筛请用 "
               "SortFlags=Time 或自己比 QFileInfo::lastModified）。";

    qInfo() << "\n【步骤⑩】错误处理：开一个不存在的路径";
    const QString ghost = QDir(dir).filePath(QStringLiteral("does_not_exist/no_such_file.txt"));
    {
        QFile f(ghost);
        const bool ok = f.open(QIODevice::ReadOnly);   // ★ 第 271 行是 [[nodiscard]]，必须接
        qInfo() << "  open(不存在的只读路径) 返回 =" << ok
                << "｜error() =" << errName(f.error())
                << "（数值" << int(f.error()) << "，第 77 行 OpenError = 5）"
                << "｜errorString() =[" << f.errorString() << "]";
        qInfo() << "  exists()（成员版，第 150 行）=" << f.exists()
                << "｜exists(静态版) =" << QFile::exists(ghost)
                << "｜QFileInfo(ghost).exists() =" << QFileInfo(ghost).exists();
        f.unsetError();
        qInfo() << "  unsetError() 之后 error() =" << errName(f.error())
                << "（第 114 行；错误码是**粘性的**，不清就一直留着，别拿它当「当前状态」判断）";
    }
    {
        // NewOnly / ExistingOnly 这两道防手滑的闸
        const QString fresh = QDir(dir).filePath(QStringLiteral("gate.txt"));
        QFile a(fresh);
        const bool firstOpen = a.open(QIODevice::WriteOnly | QIODevice::NewOnly);
        qInfo() << "  第一次 open(WriteOnly|NewOnly) =" << firstOpen
                << "（文件不存在 → 允许创建）";
        if (firstOpen)
            a.write(QByteArray("闸门\n"));
        a.close();
        QFile b(fresh);
        const bool secondOpen = b.open(QIODevice::WriteOnly | QIODevice::NewOnly);
        qInfo() << "  第二次同样 NewOnly =" << secondOpen
                << "｜error =" << errName(b.error())
                << "← **已存在就拒绝打开**，这一位就是防你手滑覆盖用户文件（第 26 行）";
        b.close();
        QFile c(QDir(dir).filePath(QStringLiteral("notes.txt")));
        const bool cOpen = c.open(QIODevice::ReadOnly | QIODevice::ExistingOnly);
        qInfo() << "  ExistingOnly 开一个确实存在的文件 =" << cOpen
                << "← 这一位防的是「你以为它在」：路径打错时它拒绝新建空文件让你白写一轮（第 27 行）";
        c.close();
    }
    qInfo() << "  → 错误处理三条纪律：① **先看 open 的返回值**（Qt6 还 [[nodiscard]]，"
               "忽略返回值直接编不过/报警）；② 失败后立刻读 error() + errorString()，"
               "前者用来分支、后者用来给用户看；③ 写完必须比对 write 的返回值 == 期望字节数，"
               "**短写不报错**，只有 error() 才知道磁盘满了。";
}

// ============================================================================
// 十、main
// ============================================================================
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);            // ← 必须先有这个对象：QTimer 和 quit() 都要事件循环
    QCoreApplication::setApplicationName(QStringLiteral("storage_02_file_io"));
    QCoreApplication::setOrganizationName(QStringLiteral("QtCourseOrg"));

    const QString tmpRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString workDir = QDir(tmpRoot).filePath(QStringLiteral("qt_storage_02"));
    QDir().mkpath(workDir);            // qdir.h 第 183 行，第二个可选权限参数不给就行

    QTimer::singleShot(0, [workDir, tmpRoot]() {
        qInfo() << "============================================================";
        qInfo() << "storage 第 2 讲 · 文件与 IO 设备（Qt =" << QT_VERSION_STR << "）";
        qInfo() << "系统临时目录 writableLocation(TempLocation) =["
                << QDir::toNativeSeparators(tmpRoot) << "]";
        qInfo() << "QDir::tempPath() =[" << QDir::tempPath() << "]（第 236 行；两个来源通常一致）";
        qInfo() << "本次工作目录 =[" << QDir::toNativeSeparators(workDir) << "]"
                << "｜mkpath 后 exists =" << QDir(workDir).exists();
        qInfo() << "============================================================";
        stepWriteAndRead(workDir);
        stepTextVsBinary(workDir);
    });

    QTimer::singleShot(30, [workDir]() {
        stepTextStream(workDir);
        stepBigFile(workDir);
    });

    QTimer::singleShot(60, [workDir]() {
        stepSaveFile(workDir);
        stepTempAndLock(workDir);
    });

    QTimer::singleShot(90, [workDir]() {
        stepDirAndErrors(workDir);

        // ── 收尾清理 ──
        QTimer::singleShot(30, [workDir]() {
            qInfo() << "\n【清理】";
            QDir cleanup(workDir);
            const QStringList leftovers = cleanup.entryList(QDir::AllEntries | QDir::NoDotAndDotDot,
                                                             QDir::Name);
            qInfo() << "  待删清单（" << leftovers.size() << "项）：" << leftovers;
            const bool removed = cleanup.removeRecursively();     // 第 186 行
            qInfo() << "  removeRecursively() =" << removed
                    << "｜目录还在吗 =" << QDir(workDir).exists()
                    << "｜目录条目数 =" << QDir(workDir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size();

            qInfo() << "\n【本讲可核对的证据】";
            qInfo() << "  ① 文本文件写入字节数 vs QFileInfo::size()、read(12) 的字节数与字符数之差";
            qInfo() << "  ② Text 模式与二进制模式的字节数/\\r 个数、seek 与 pos 的错位";
            qInfo() << "  ③ QTextStream 默认 encoding 数值、UTF-8 与 Latin1 两份的磁盘字节数、BOM 有无";
            qInfo() << "  ④ locale 两份写出的文本、C locale 读回的结果";
            qInfo() << "  ⑤ 4 MiB 分块读的累计字节/循环次数 vs readAll 的字节数与耗时";
            qInfo() << "  ⑥ QSaveFile commit 前后目标文件字节数与内容、cancelWriting 后内容不变";
            qInfo() << "  ⑦ QTemporaryFile 生成的名字与析构后是否消失、QLockFile 两次 tryLock 与 getLockInfo 的 pid";
            qInfo() << "  ⑧ 不存在的文件的 error()=" << int(QFileDevice::OpenError)
                    << "（OpenError）与 errorString 原文";
            qInfo() << "  ⑨ 清理结果与残留条目数（应为 0）";
            QCoreApplication::quit();
        });
    });

    return app.exec();
}

// ============================================================================
// 附：文件 IO 的选路与纪律
// ============================================================================
/*
 * 一、用什么写、用什么读（决策表）
 *   需求                            用什么                                        为什么
 *   ---------------------------------------------------------------------------
 *   二进制/结构化数据              QFile + QByteArray（自己控制每一个字节）        没有翻译、没有编码猜测
 *   给人看的文本（日志、ini、csv） QFile + QTextStream + setEncoding(Utf8)         换行与编码交给它
 *   「保存」按钮、怕写坏           QSaveFile + 显式 commit()                       原子替换（本讲步骤⑦）
 *   中间文件、下载缓存             QTemporaryFile                                  名字自动唯一、默认自删
 *   防多开 / 独占资源              QLockFile + tryLock(timeout)                    带 pid 信息、能识别过期锁
 *   超大文件只读                   QFileDevice::map()/unmap()（第 140/141 行）      交给虚拟内存，不占堆
 *   跨进程流式                     QLocalSocket / QProcess（第玖阶段）              文件不适合当管道
 *
 * 二、openMode 的组合（本机 qiodevicebase.h 第 17~28 行）
 *   常见合法组合：ReadOnly / WriteOnly|Truncate / WriteOnly|Append / ReadOnly|Text
 *                 ReadOnly|ExistingOnly（防「你以为它在」）/ WriteOnly|NewOnly（防覆盖）
 *   别写：ReadOnly|Append（矛盾）  WriteOnly|Append|Truncate（矛盾）
 *        Unbuffered 单独给 QFile 意义不大，且 read() 变成「有多少给多少」，
 *        你的循环必须自己补齐——很多「读到一半就退」的 bug 是它造成的。
 *   ★ Text 位只管换行翻译，**不管编码**；编码只有 QTextStream::setEncoding 管。
 *     所以「用 Text 位打开，然后 QString::fromUtf8 读 QFile::readAll()」是正解；
 *     「用 Text 位打开并指望它按 GBK 解」是想多了。
 *
 * 三、错误处理的三张账单（一次都不能省）
 *   ① open 的返回值：Qt ≥6.10 是 [[nodiscard]]，不接就报警；失败读 error() + errorString()。
 *   ② write 的返回值：必须 == 你给的字节数。短写 = 磁盘满/被摘，**不抛不崩**。
 *   ③ read 的返回值：>0 实读字节、0 到结尾、-1 出错。别把 0 和 -1 混成「读完了」。
 *   补充：error() 是粘性的（要 unsetError() 才清），errorString() 的文本随平台与
 *   语言环境变，**不要拿字符串做逻辑判断**，只用来显示。
 *
 * 四、Qt6 相对 Qt5 在本讲踩过的改名/删除
 *   · setCodec() → **删了**，改 setEncoding(QStringConverter::Encoding)（第 89 行）。
 *     Encoding 只有 Utf8、Utf16（含大小端变体）、Utf32（含大小端变体）、Latin1、System
 *     这几类值，**没有 GBK/Big5**。
 *   · endl / flush 等操纵符 → 全部在 **Qt::** 命名空间下（第 228~261 行）。
 *   · QTextStream 现在继承 QIODeviceBase（第 42 行），**不是 QIODevice**，没有信号。
 *   · QFile::copy 多了 `std::optional<QFileDevice::Permissions>` 参数（第 245 行）。
 *   · QFile(QString) 在 Qt ≥6.9 变 **explicit**（第 64~68 行）。
 *   · QSaveFile 的单参构造在 6.11 起移除（第 30~32 行），用两参那份（parent 有默认值）。
 *   · QSaveFile::close() 在 **private**（第 71 行）——调不到，别写。
 *   · QDir::Modified 已弃用（第 36 行）；QDir::count() 返回 qsizetype（第 156 行）。
 *   · uint 版 operator[](int) 也没了（第 159~162 行），用 qsizetype 那份。
 *   · QFileInfo(QString) 默认 explicit（第 37~44 行），`QFileInfo fi = path;` 编不过。
 *
 * ——— 老师傅的话 ———
 *   徒弟，文件这块最容易养成坏习惯的地方是「相信工具帮你翻译」。
 *   QTextStream 帮你猜编码、Text 位帮你换行、QSettings 帮你转类型——三件事叠在
 *   一起的时候，一份文件到底写了什么，你自己都说不清。所以我的规矩是：
 *   **写的时候把每一层都写明白**：显式 setEncoding(Utf8)、显式 openMode、
 *   显式比对 write 的返回值。宁可啰嗦，不要猜。
 *
 *   第二件事：**QSaveFile 是给「用户点保存」用的，不是给程序自己用的**。
 *   程序内部的中间文件用 QFile 直写更省事，出问题也好查。反过来说，凡是
 *   「用户的数据、写坏了要赔」的，一律 QSaveFile + commit()，别嫌它多一次 rename。
 *   我在步骤⑦ 故意写了半截 XML 再 cancelWriting()，就是让你看一眼「撤销之后
 *   文件连字节数都没变」这件事有多值钱——你哪天遇上断电投诉，会回来谢这一讲。
 *
 *   第三件事：**大文件不要用 readAll**。这不是性能建议，是稳定性建议。
 *   4 MiB 你测不出来，客户那份 800 MiB 的导出文件当场就把你测出来了。
 *   分块循环那五行代码，买的是「内存有上限 + 进度条是真的」。
 *
 * ——— 小练习 ———
 *   1. 把步骤⑥ 的分块读改成边读边写（自己实现一个 chunkedCopy），
 *      再和 QFile::copy()（第 245 行，带 optional 权限参数那份）比一次：
 *      打印两份文件的字节数与 QFileInfo::permissions()，说说大文件时谁更扛得住。
 *   2. 用 QTextStream 分别以 QStringConverter::Utf8 和 Utf16LE 写同一句中文，
 *      打印两份文件的字节数、前 8 字节 hex，以及 generateByteOrderMark() 置 true 之后
 *      多了哪几个字节；再把带 BOM 的那份交给下一讲的 QJsonDocument::fromJson，
 *      看它报不报错（这一条会直接接上第 3 讲）。
 *   3. 步骤⑩ 里再补两个失败用例：对已有目录调 QFile::remove()、
 *      对不存在的文件调 rename()，把 error() 落到 14 个 FileError 里的哪一个打出来
 *      （RemoveError=9 / RenameError=10 都在 qfiledevice.h 第 81~86 行）。
 *   4. 给 showFile() 加一个「按 size 从大到小」的开关，用 entryInfoList 的
 *      SortFlags 实现；然后换成先 entryList() 拿名字、只对最大的 3 个构造 QFileInfo，
 *      用 QElapsedTimer 打两次耗时，体会「要不要 stat」的代价差多少。
 */
// ============================================================================

// 本文件没有 Q_OBJECT 类（全是静态函数与 Qt 值类型），AUTOMOC 不需要额外的 include。
