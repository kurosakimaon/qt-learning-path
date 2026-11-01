// ============================================================================
// 第 5 讲 · Qt 里的错误处理策略：返回值与异常这两条道，别走混
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   前四讲把「测试」「分层」「所有权」立起来了，这一讲补上「出错怎么办」。
//   Qt 的错误处理是**两套风格并存**的，混用是最常见的手艺错误：
//     1) 返回值型（绝大多数 Qt API）：QFile::open() 返回 bool，细节在
//        error() / errorString()；QJsonDocument::fromJson() 返回一个可能是
//        「空文档」的对象，细节在出参 QJsonParseError；QSqlQuery::exec() +
//        lastError() 同理。**它们一个异常都不抛**。
//     2) 抛异常型：来自 C++ 标准库和 Qt 的容器边界——std::vector::at() 越界抛
//        std::out_of_range、operator new 失败抛 std::bad_alloc。
//        ★ 勘误（旧稿在这里还列了「QVariant::to<T>() 转换失败抛 QtException 家族
//          （QVariant::InvalidTypeException）」——三个名字全查无此文）：
//          · Qt 6.12 的 QVariant **没有** `to<T>()`；取值的模板写法是 `value<T>()`
//            （qvariant.h:755），另外还有 toInt()/toString() 这类定型访问器；
//          · 本机实测（MinGW 13.1 + Qt 6.12.0，异常这一路是开着的）：
//            QVariant("abc").value<int>() → 0，**不抛**；toInt(&ok) → 0 且 ok=false；
//            QVariant(QByteArray("junk")).toList() → 4 个元素，**不抛**；
//            整个探针的 catch 计数 = 0。
//          · qtbase 全树 grep：`InvalidTypeException` 0 处；`QtException` 只在
//            qexceptionhandling.h:13 的 `#pragma qt_class(QtExceptionHandling)` 出现，
//            那是给文件起的归类名，不是类。「Qt 有个 QtException 异常家族」是旧版资料里的
//            口头禅，本机 6.12 的头文件里一个都找不到。
//        **文件打不开、JSON 写歪了、QVariant 转换失败，都不在这个集合里**。
//   所以本讲的三条主线：
//     · 边界要认清：try/catch 抓不到 file.open() 的失败（本讲把 catch 计数摆成 0，
//       同一函数里越界的 at() 抓到 1 次，两种风格一次对齐）；
//     · 层与层之间传错不要用 bool + 全局 errno，自己写一个**expected 风格**的
//       Result<T>（错误码 + 消息 + 是否可恢复 + 上下文字段），一次把「哪儿错的、
//       还能不能救」带上去；
//     · 错误分两层给：**开发者可见**（qWarning/qCritical/qFatal，进日志）与
//       **用户可见**（状态栏/toast，是人话、不含路径）。本讲用消息处理器把日志
//       收进缓冲区，数出「打码前泄漏了几行原始路径 / 打码后 0 行」。
//   另外两件必须亲手做的事：qFatal 的后果（它会 abort，进程当场没了，本讲**只讲不用**）、
//   以及跨线程错误的正规通道——工作线程把错误装进结构体用**信号**送回来，
//   绝不在别的线程里弹 UI。
//
// 对应构建目标名：quality_05_error_handling
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(quality_05_error_handling 05_error_handling.cpp)
//   target_link_libraries(quality_05_error_handling PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)                       # 本文件**有 Q_OBJECT 类**，需要 moc
//   # 手工核对时：moc 05_error_handling.cpp -o 05_error_handling.moc
//   #             文件末尾那句 #include "05_error_handling.moc" 就是给 AUTOMOC 的钩子
//
// 运行前须知：
//   · 无人值守：QTimer::singleShot 串六个阶段，末尾 qInfo() 打完数字证据就
//     QCoreApplication::exit(Ledger::exitCode())：核对全过才返回 0，有一项对不上
//     返回 1，CTest 直接判红。**没有任何模态对话框**，不需要人点一下。
//   · 可以无头跑：`quality_05_error_handling -platform offscreen`（CTest 就是这么跑的：
//     环境变量 QT_QPA_PLATFORM=offscreen，构建方式见仓库根 README）。
//   · 只往 QStandardPaths::writableLocation(QStandardPaths::TempLocation) 里写四个
//     小文件（< 1 KB），**阶段末尾逐个 QFile::remove 清掉**；不联网、不读别人的文件。
//   · 「磁盘写入失败」是**造出来的**：先把文件设成只读（QFile::setPermissions），
//     再拿 WriteOnly 去 open()。不同文件系统失败原因措辞可能不同，所以本讲只承诺
//     open() 返回 false 且 error() != NoError，不承诺 errorString 的具体文字。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 quality_05_error_handling）。本机这一轮：【硬账】32/32 项核对通过。
//     这只证明「编得过、跑得起来、上面这行为绿」——换 Qt 版本 / 换平台 / 换 CPU 核数
//     之后，正文里带具体数字的「预期」请以你自己那次运行输出为准
//     （重跑：bash _tools/run_all.sh）。
// ============================================================================

#include <QApplication>              // QtWidgets/qapplication.h
#include <QCoreApplication>          // QtCore/qcoreapplication.h：quit / postEvent
#include <QObject>                   // QtCore/qobject.h：moveToThread（第 145 行）
#include <QThread>                   // QtCore/qthread.h：wait（第 95 行）
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QFile>                     // QtCore/qfile.h：setPermissions（第 283/284 行）
#include <QFileInfo>
#include <QDir>
#include <QIODevice>                 // QtCore/qiodevice.h：errorString()（第 115 行）
#include <QFileDevice>               // QtCore/qfiledevice.h：FileError 枚举（第 72~88 行）
#include <QJsonDocument>             // QtCore/qjsondocument.h：fromJson（第 73 行）
#include <QJsonObject>
#include <QJsonValue>                // QtCore/qjsonvalue.h：enum Type（第 32~39 行）
#include <QJsonParseError>           // QtCore/qjsonparseerror.h：struct（第 16 行起）
#include <QStandardPaths>            // QtCore/qstandardpaths.h：TempLocation（第 29 行）
#include <QMetaType>                 // qRegisterMetaType
#include <QElapsedTimer>             // 给错误打上「发生在第几毫秒」的相对时间戳
#include <QTimer>
#include <QEventLoop>                // QtCore/qeventloop.h：AllEvents（processEvents 的参数）
#include <QGuiApplication>           // platformName()
#include <QDebug>
#include <QtGlobal>                  // qInstallMessageHandler / QtMsgType / QT_VERSION_STR

#include <vector>
#include <stdexcept>
#include <new>
#include <cstdint>
#include <cstring>                   // std::strlen（把异常 what() 的长度记进 params）

// ============================================================================
// 零、硬账：每一项核对都要能判红，不然「预期」只是嘴上说说
// ============================================================================
namespace {

struct Ledger {
    inline static int total = 0;
    inline static int passed = 0;
    static int exitCode() { return (total > 0 && passed == total) ? 0 : 1; }
};

void check(const char *what, qint64 got, qint64 want)
{
    ++Ledger::total;
    const bool ok = (got == want);
    if (ok)
        ++Ledger::passed;
    qInfo().noquote() << QStringLiteral("      %1 %2 → 期望 %3，实测 %4")
                             .arg(ok ? QStringLiteral("［对］") : QStringLiteral("［差］"))
                             .arg(QString::fromUtf8(what))
                             .arg(want)
                             .arg(got);
}

void checkBool(const char *what, bool got, bool want = true)
{
    check(what, got ? 1 : 0, want ? 1 : 0);
}

} // namespace

// ============================================================================
// 一、核对签名（本机 D:\Qt\6.12.0\mingw_64\include）
// ============================================================================
// QFile / QIODevice（返回值型）：
//   qfiledevice.h 第 72~88 行  enum FileError { NoError=0, ReadError=1, WriteError=2,
//       FatalError=3, ResourceError=4, OpenError=5, AbortError=6, TimeOutError=7,
//       UnspecifiedError=8, RemoveError=9, RenameError=10, PositionError=11,
//       ResizeError=12, PermissionsError=13, CopyError=14 };
//   qfiledevice.h 第 96~103 行 enum Permission { ReadOwner=0x4000, WriteOwner=0x2000, ... }
//                              + Q_DECLARE_FLAGS(Permissions, Permission)
//   qfiledevice.h 第 112/113 行 FileError error() const; void unsetError();
//   qiodevice.h    第 115 行     QString errorString() const;      ← 是人话，**别拿它做分支**
//   qfile.h        第 281~284 行 permissions()/setPermissions() 各有成员版与静态版
//   ⚠ QIODevice::open() / QFile::open() 是 **bool 返回值**，失败时**不抛异常**；
//     老资料里「Qt 不抛异常」这句话要精确成「Qt 的 C 风格 API 不抛异常」，
//     因为 std 容器（at()/front()/back() 越界、operator new）是真的会抛（本讲证据①摆出来）。
//     至于旧稿在这里并列的「QtException 家族、QVariant::to<T>()」——本机 6.12 两个都查无此名，
//     QVariant 转换失败实测不抛（详见文件头第 2 条勘误）。
// QJsonParseError（**struct，不是 class，异常家族里没它**）：
//   qjsonparseerror.h 第 16 行起 struct QJsonParseError {
//       enum ParseError { NoError=0, UnterminatedObject, MissingNameSeparator,
//           UnterminatedArray, MissingValueSeparator, IllegalValue, TerminationByNumber,
//           IllegalNumber, IllegalEscapeSequence, IllegalUTF8String, UnterminatedString,
//           MissingObject, DeepNesting, DocumentTooLarge, GarbageAtEnd };
//       QString errorString() const;
//       offset（Qt6 里是 int，Qt7 变 qint64：头文件用 conditional_t 写的）
//       ParseError error = NoError; }
//   qjsondocument.h 第 73 行 static QJsonDocument fromJson(const QByteArray &,
//                                                          QJsonParseError *error = nullptr);
//                    ← **细节靠出参**，返回值本身只是个可能为空的文档；判错只看 parseError.error
// QJsonValue：第 32~39 行 enum Type { Null=0x0, Bool=0x1, Double=0x2, String=0x3,
//                    Array=0x4, Object=0x5, Undefined=0x80 }
//          ⚠ 枚举叫 **Type**（Qt 6 的 QJsonValue 里没有 ValueType 这个名字，别和
//            QCborValue / QJsonValueRef 的说法串台）。
//          ⚠ **枚举里没有 Integer** —— 但「整数存成 Double」这半句要打个折：Qt 6 的
//            QJsonValue 底层是一个 QCborValue，整数走 int64 存、**不经过 double**，
//            所以 2^53+1 解析再序列化能原样回来（storage/03 本机实测：字节完全相同）。
//            塌的是**门面**：type() 只有 Double，toInt() 走的是 double 转换，
//            超过约 9e15 的整数在 toDouble()/toInt() 这一手会丢精度。
//            所以「字段类型不符」最常见的真相仍是「人把 7000 写成了 "7000"」，
//            用 toInt(默认值) 一把梭等于**把错误咽下去**。
// 日志（开发者可见那一层）：
//   qlogging.h 第 169~173 行 宏 qDebug/qInfo/qWarning/qCritical/qFatal =
//       QMessageLogger(__FILE__,__LINE__,__func__).debug/info/warning/critical/fatal
//   第 73~99 行 QMessageLogger：warning/critical 标了 Q_DECL_COLD_FUNCTION；
//       fatal 标了 QT_MESSAGE_LOGGER_NORETURN + noexcept —— **它就是会 abort，不返回**
//   第 198 行 QtMessageHandler qInstallMessageHandler(QtMessageHandler);   ← 本讲用它取证
//   第 200 行 void qSetMessagePattern(const QString &);
//   qloggingcategory.h 第 20/21/28 行 isEnabled/setEnabled/categoryName，
//       第 36 行 `typedef void (*CategoryFilter)(QLoggingCategory *);`、
//       第 37 行 static CategoryFilter installFilter(CategoryFilter)、
//       第 39 行 static void setFilterRules(const QString &rules)
//       ← 这就是 QT_LOGGING_RULES 在代码里的对偶；**别把「调试期开日志」写成 if(type==xxx)**
//       ★ 勘误：旧稿写 19/40/42，本机 6.12.0 重数：19 行是**空行**（isEnabled 在 20），
//         installFilter 在 **37**（36 行是它需要的函数指针 typedef，抄签名时这两行要一起抄），
//         setFilterRules 在 **39**。注意 installFilter 的返回类型是 `CategoryFilter` 而不是 void：
//         它把**上一个**过滤器还给你，所以装完自己的要留着，否则没法在测试结束时摘干净。
// QSql（本讲**不写进代码**，因为 quality 这组只链 Qt6::Widgets/Test/Concurrent）：
//   qsqlerror.h 第 20 行起 enum ErrorType { NoError, ConnectionError, StatementError,
//       DatabaseError, DriverError, UnknownError }；text()/nativeErrorCode()/number()
//   套路和 QFile 一模一样：exec() 返 bool，细节去 lastError() 里拿。
// 线程：
//   qobject.h 第 145 行 void moveToThread(QThread *thread);
//   qthread.h 第 95 行  bool wait(QDeadlineTimer = Forever);
// ============================================================================

// ============================================================================
// 二、自己的 Result<T>：层与层之间传错，带的不是 bool 而是「决策需要的信息」
// ============================================================================
enum class ErrDomain { Io, Parse, Validate, Write, StdException };

enum class ErrCode {
    None = 0,
    FileNotFound = 101,      // 文件不存在（用户能自己解决 → 可恢复）
    FileUnreadable = 102,    // 存在但读不了（权限/被占用）
    JsonSyntax = 201,        // JSON 语法错
    JsonNotObject = 202,     // 语法对但顶层不是对象
    FieldMissing = 301,      // 缺必填字段
    FieldTypeMismatch = 302, // 字段类型不符（常见：数字写成字符串）
    FieldOutOfRange = 303,   // 类型对但值不合理
    WriteDenied = 401,       // 写不进去（只读/权限）
    WriteFailed = 402,       // 其它写失败（满/断开）
    StdThrow = 501,          // 从异常翻译过来的
};

// 一条错误的全部上下文。**operation / target / params 三样必须有**，
// 否则日志出来就是「open failed」四个字，半夜谁也定位不了。
struct ErrorInfo {
    ErrCode code = ErrCode::None;
    ErrDomain domain = ErrDomain::Io;
    QString message;        // 技术信息：给开发者、进日志
    QString operation;      // 正在做什么，如 "import_config"
    QString target;         // 对象：路径 / 字段名（**打印前要打码**）
    QString params;         // 关键参数摘要：大小、编码、第几个字段……
    bool recoverable = false;
    QString recoveryHint;   // 「可恢复」具体指什么：让用户做哪一步
    qint64 atMs = -1;       // 相对流程起点的毫秒，用来排序/关联
};

// 极简 expected：要么有值，要么有错，**不允许两者都空**。
template <typename T>
class Result {
public:
    static Result accepted(T value)
    {
        Result r;
        r.m_ok = true;
        r.m_value = std::move(value);
        return r;
    }
    static Result rejected(ErrorInfo e)
    {
        Result r;
        r.m_ok = false;
        r.m_error = std::move(e);
        return r;
    }

    bool isOk() const { return m_ok; }
    explicit operator bool() const { return m_ok; }
    const T &value() const { return m_value; }
    T takeValue() { return std::move(m_value); }
    const ErrorInfo &error() const { return m_error; }

private:
    Result() = default;
    bool m_ok = false;
    T m_value {};
    ErrorInfo m_error {};
};

static QElapsedTimer g_clock;     // 全流程共用一把表，错误带相对时间戳

static ErrorInfo makeError(ErrCode code, ErrDomain domain, const QString &operation,
                           const QString &target, const QString &params,
                           const QString &message, bool recoverable = false,
                           const QString &recoveryHint = QString())
{
    ErrorInfo e;
    e.code = code;
    e.domain = domain;
    e.operation = operation;
    e.target = target;
    e.params = params;
    e.message = message;
    e.recoverable = recoverable;
    e.recoveryHint = recoveryHint;
    e.atMs = g_clock.isValid() ? g_clock.elapsed() : 0;
    return e;
}

// ============================================================================
// 三、四层流水（每层只认自己的错，往上原样带 ErrorInfo）
// ============================================================================
struct AppConfig {
    QString serverName;
    int port = 0;
    bool verbose = false;
};

static Result<QByteArray> readFileAll(const QString &path, const QString &operation)
{
    QFile f(path);
    if (!f.exists())
        return Result<QByteArray>::rejected(makeError(
            ErrCode::FileNotFound, ErrDomain::Io, operation, path,
            QStringLiteral("stage=read"),
            QStringLiteral("文件不存在"), true,
            QStringLiteral("请确认路径，或从「浏览」重新选一次")));

    if (!f.open(QIODevice::ReadOnly)) {          // ← 返回值型：这里**不会抛**
        const QFileDevice::FileError fe = f.error();
        return Result<QByteArray>::rejected(makeError(
            ErrCode::FileUnreadable, ErrDomain::Io, operation, path,
            QStringLiteral("fileError=%1(%2)").arg(int(fe)).arg(fe == QFileDevice::OpenError
                                                       ? QStringLiteral("OpenError")
                                                       : QStringLiteral("其它")),
            f.errorString()));                    // 人话进日志，不进分支
    }
    const qint64 limit = 4LL * 1024 * 1024;
    if (f.size() > limit) {
        f.close();
        return Result<QByteArray>::rejected(makeError(
            ErrCode::FileUnreadable, ErrDomain::Io, operation, path,
            QStringLiteral("size=%1").arg(f.size()), QStringLiteral("配置太大，拒绝解析")));
    }
    QByteArray raw = f.read(limit);
    const QString errStr = f.errorString();       // 读一半断掉也要接住
    f.close();
    if (raw.isEmpty() && !errStr.isEmpty())
        return Result<QByteArray>::rejected(makeError(
            ErrCode::FileUnreadable, ErrDomain::Io, operation, path,
            QStringLiteral("bytes=0"), errStr));
    return Result<QByteArray>::accepted(std::move(raw));
}

static Result<QJsonObject> parseJson(const QByteArray &raw, const QString &operation)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);   // 细节在出参
    if (pe.error != QJsonParseError::NoError)
        return Result<QJsonObject>::rejected(makeError(
            ErrCode::JsonSyntax, ErrDomain::Parse, operation,
            QStringLiteral("<body>"),
            QStringLiteral("offset=%1 parseError=%2").arg(int(pe.offset)).arg(int(pe.error)),
            QStringLiteral("JSON 语法错：%1").arg(pe.errorString())));
    if (!doc.isObject())
        return Result<QJsonObject>::rejected(makeError(
            ErrCode::JsonNotObject, ErrDomain::Parse, operation,
            QStringLiteral("<body>"),
            QStringLiteral("isNull=%1 isArray=%2").arg(doc.isNull()).arg(doc.isArray()),
            QStringLiteral("顶层必须是对象")));
    return Result<QJsonObject>::accepted(doc.object());
}

// 字段类型不符是这一层的主课：**不要用 toInt(0) 把错咽下去**。
static Result<AppConfig> validateConfig(const QJsonObject &obj, const QString &operation)
{
    AppConfig cfg;

    const QJsonValue name = obj.value(QStringLiteral("serverName"));
    if (name.isUndefined())
        return Result<AppConfig>::rejected(makeError(
            ErrCode::FieldMissing, ErrDomain::Validate, operation,
            QStringLiteral("serverName"), QStringLiteral("keys=%1").arg(obj.size()),
            QStringLiteral("必填字段缺失"), true, QStringLiteral("留空走默认名")));
    if (!name.isString())
        return Result<AppConfig>::rejected(makeError(
            ErrCode::FieldTypeMismatch, ErrDomain::Validate, operation,
            QStringLiteral("serverName"),
            QStringLiteral("expect=String(%1) got=%2").arg(int(QJsonValue::String)).arg(int(name.type())),
            QStringLiteral("字段类型不符")));

    const QJsonValue port = obj.value(QStringLiteral("port"));
    if (port.isUndefined())
        return Result<AppConfig>::rejected(makeError(
            ErrCode::FieldMissing, ErrDomain::Validate, operation,
            QStringLiteral("port"), QStringLiteral("keys=%1").arg(obj.size()),
            QStringLiteral("必填字段缺失")));
    if (port.type() == QJsonValue::String) {      // 真实项目里最常见的形态
        bool numeric = false;
        const int coerced = port.toString().toInt(&numeric);
        if (!numeric)
            return Result<AppConfig>::rejected(makeError(
                ErrCode::FieldTypeMismatch, ErrDomain::Validate, operation,
                QStringLiteral("port"),
                QStringLiteral("expect=Double(%1) got=String(%2)").arg(int(QJsonValue::Double))
                    .arg(int(QJsonValue::String)),
                QStringLiteral("port 不是数字")));
        cfg.port = coerced;                        // **显式**降级：可恢复，但要记一笔
        qWarning("%s", "字段 port 是字符串，已按数字转换（该让作者改配置了）");
    } else if (port.isDouble()) {
        cfg.port = port.toInt();
    } else {
        return Result<AppConfig>::rejected(makeError(
            ErrCode::FieldTypeMismatch, ErrDomain::Validate, operation,
            QStringLiteral("port"), QStringLiteral("got=%1").arg(int(port.type())),
            QStringLiteral("port 类型不认")));
    }
    if (cfg.port < 1 || cfg.port > 65535)
        return Result<AppConfig>::rejected(makeError(
            ErrCode::FieldOutOfRange, ErrDomain::Validate, operation,
            QStringLiteral("port"), QStringLiteral("value=%1 range=[1,65535]").arg(cfg.port),
            QStringLiteral("端口超出范围"), true, QStringLiteral("改用 8080")));

    cfg.serverName = name.toString();
    cfg.verbose = obj.value(QStringLiteral("verbose")).toBool(false);
    return Result<AppConfig>::accepted(cfg);
}

static Result<bool> writeFileAll(const QString &path, const QByteArray &data,
                                 const QString &operation)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {   // 又是返回值型
        const QFileDevice::FileError fe = f.error();
        const bool denied = (fe == QFileDevice::OpenError || fe == QFileDevice::PermissionsError
                             || fe == QFileDevice::RenameError);
        return Result<bool>::rejected(makeError(
            denied ? ErrCode::WriteDenied : ErrCode::WriteFailed,
            ErrDomain::Write, operation, path,
            QStringLiteral("fileError=%1 bytes=%2").arg(int(fe)).arg(data.size()),
            f.errorString(), true,
            denied ? QStringLiteral("去掉只读或换个目录再保存") : QStringLiteral("重试或另存为")));
    }
    const qint64 written = f.write(data);
    const bool flushed = f.flush();
    f.close();
    if (written != data.size() || !flushed)
        return Result<bool>::rejected(makeError(
            ErrCode::WriteFailed, ErrDomain::Write, operation, path,
            QStringLiteral("want=%1 got=%2 flush=%3").arg(data.size()).arg(written).arg(flushed),
            QStringLiteral("写入不完整")));
    return Result<bool>::accepted(true);
}

// ============================================================================
// 四、两个出口：开发者那一层（日志）与用户那一层（文案）
// ============================================================================
// 生产日志里**不要原样打路径**：家目录名、用户名、挂载盘符都是隐私/环境信息。
static QString maskPath(const QString &p)
{
    const QFileInfo fi(p);
    return QStringLiteral("<%1%2>").arg(fi.completeBaseName().isEmpty()
                                        ? QString() : fi.completeBaseName())
        .arg(fi.suffix().isEmpty() ? QString() : QStringLiteral(".") + fi.suffix());
}

static QString describeLog(const ErrorInfo &e)
{
    return QStringLiteral("[op=%1] code=%2 domain=%3 at=%4ms target=%5 params=%6 msg=%7")
        .arg(e.operation)
        .arg(int(e.code))
        .arg(int(e.domain))
        .arg(e.atMs)
        .arg(maskPath(e.target))                 // ← 打码后才进日志
        .arg(e.params, e.message);
}

static QString userVisibleText(const ErrorInfo &e)
{
    switch (e.code) {
    case ErrCode::FileNotFound:     return QStringLiteral("找不到配置文件，请重新选择。");
    case ErrCode::FileUnreadable:   return QStringLiteral("配置文件读不了，可能被别的程序占着。");
    case ErrCode::JsonSyntax:       return QStringLiteral("配置文件格式有误，检查引号和逗号。");
    case ErrCode::JsonNotObject:    return QStringLiteral("配置文件内容不对，最外层要写成 { }。");
    case ErrCode::FieldMissing:     return QStringLiteral("配置里少了一项必填内容。");
    case ErrCode::FieldTypeMismatch:return QStringLiteral("有一项填的类型不对（比如端口要填数字）。");
    case ErrCode::FieldOutOfRange:  return QStringLiteral("端口号超出可用范围。");
    case ErrCode::WriteDenied:      return QStringLiteral("这个位置写不进去，换个目录试试。");
    case ErrCode::WriteFailed:      return QStringLiteral("保存失败，请重试或另存为。");
    case ErrCode::StdThrow:         return QStringLiteral("程序内部出了意外，已记录，请反馈。");
    default:                        return QStringLiteral("操作未完成。");
    }
}

// ============================================================================
// 五、取证：把日志收进缓冲区，数「几行」「什么级别」「有没有漏原始路径」
// ============================================================================
class LogProbe {
public:
    static QStringList lines;
    static int nDebug, nInfo, nWarning, nCritical;
    static QString leakToken;                    // 「原始路径」的特征串
    static int leakHits;

    static void install(const QString &token)
    {
        leakToken = token;
        s_previous = qInstallMessageHandler(&LogProbe::handle);
    }
    static void uninstall()
    {
        if (s_previous)
            qInstallMessageHandler(s_previous);
        s_previous = nullptr;
    }

private:
    static void handle(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
    {
        switch (type) {
        case QtDebugMsg:    ++nDebug;    break;
        case QtInfoMsg:     ++nInfo;     break;
        case QtWarningMsg:  ++nWarning;  break;
        case QtCriticalMsg: ++nCritical; break;
        case QtFatalMsg:    ++nCritical; break;
        }
        lines << msg;
        if (!leakToken.isEmpty() && msg.contains(leakToken))
            ++leakHits;
        if (s_previous)
            s_previous(type, ctx, msg);          // 仍然转给默认处理器，终端看得见
    }
    static QtMessageHandler s_previous;
};
QStringList LogProbe::lines;
int LogProbe::nDebug = 0;
int LogProbe::nInfo = 0;
int LogProbe::nWarning = 0;
int LogProbe::nCritical = 0;
QString LogProbe::leakToken;
int LogProbe::leakHits = 0;
QtMessageHandler LogProbe::s_previous = nullptr;

// ============================================================================
// 六、跨线程的错误通道：错误装进结构体，用**信号**送回主线程
// ============================================================================
struct ImportReport {
    QString stage;
    int code = 0;
    QString userText;
    QString workerThreadName;
    QString receiverThreadName;
};
Q_DECLARE_METATYPE(ImportReport)

// 前置声明：第四节末尾定义的四级流水，工作线程要用它
static Result<AppConfig> doImport(const QString &path);

class ImportWorker : public QObject
{
    Q_OBJECT
public:
    explicit ImportWorker(QString dir) : m_dir(std::move(dir)) {}

public slots:
    void runOnce()                              // 在工作线程里跑
    {
        m_workerThread = QThread::currentThread();
        const QString missing = m_dir + QStringLiteral("/nope-absent.json");
        Result<AppConfig> r = doImport(missing);
        ImportReport rep;
        rep.stage = QStringLiteral("worker-thread");
        rep.code = r.isOk() ? 0 : int(r.error().code);
        rep.userText = r.isOk() ? QStringLiteral("成功") : userVisibleText(r.error());
        rep.workerThreadName = m_workerThread ? m_workerThread->objectName() : QStringLiteral("(匿名)");
        rep.receiverThreadName = QString();
        emit importFailed(rep);                 // ← 唯一的上报通道；这里**不碰 UI**
    }

signals:
    void importFailed(const ImportReport &report);

private:
    QString m_dir;
    QThread *m_workerThread = nullptr;
};

// 复用同一套四层流水（io → parse → validate → write）
static Result<AppConfig> doImport(const QString &path)
{
    const QString op = QStringLiteral("import_config");
    auto rd = readFileAll(path, op);
    if (!rd.isOk())
        return Result<AppConfig>::rejected(rd.error());
    auto pj = parseJson(rd.value(), op);
    if (!pj.isOk())
        return Result<AppConfig>::rejected(pj.error());
    return validateConfig(pj.value(), op);
}

// 主线程侧的接收槽
class UiMirror : public QObject
{
    Q_OBJECT
public:
    int received = 0;
    int onMainThread = 0;
    QStringList shown;

public slots:
    void onReport(const ImportReport &rep)
    {
        ++received;
        ImportReport copy = rep;
        copy.receiverThreadName = QThread::currentThread() == qApp->thread()
                                      ? QStringLiteral("main") : QStringLiteral("other");
        if (copy.receiverThreadName == QStringLiteral("main"))
            ++onMainThread;
        shown << QStringLiteral("%1｜code=%2｜%3｜接收线程=%4")
                     .arg(copy.stage).arg(copy.code).arg(copy.userText, copy.receiverThreadName);
    }
};

// ============================================================================
// 七、证据①：两套风格的边界 —— try/catch 抓不到 open()，抓得到 at()
// ============================================================================
static void evidenceStyleBoundary(const QString &dir)
{
    qInfo() << "【证据①】Qt 的 C 风格 API 不抛异常：同一段代码，两种错，两种走法";

    int catches = 0;
    QFile f(dir + QStringLiteral("/definitely-not-here.json"));
    try {
        if (!f.open(QIODevice::ReadOnly)) {
            // 走到这里说明「失败是返回值」，异常一个都没冒出来
            qInfo() << "  open() 返回 false，error() =" << int(f.error())
                    << "（5 = OpenError）｜errorString 是人话，只做日志不做分支";
        } else {
            qInfo() << "  意外：居然打开了";
        }
    } catch (...) {
        ++catches;
    }
    qInfo() << "  → 打不开文件被 try/catch 抓住的次数 =" << catches
            << "← 这就是「混用两套风格」的后果：你以为兜住了，其实一句都没进";
    checkBool("① open() 失败时 catch(...) 一次都没进（失败是返回值，不是异常）",
              catches == 0, true);
    checkBool("① open() 失败时 error() 不是 NoError",
              f.error() != QFileDevice::NoError, true);

    int stdCatches = 0;
    try {
        std::vector<int> v { 1, 2, 3 };
        const int bad = v.at(99);               // ← 这一支**真的会抛**
        Q_UNUSED(bad)
    } catch (const std::out_of_range &) {
        ++stdCatches;
    } catch (const std::bad_alloc &) {
        ++stdCatches;
    }
    qInfo() << "  std::vector::at(99) 被 catch 抓住的次数 =" << stdCatches
            << "｜结论：**边界类/越界类用异常，资源与解析类用返回值**，别指望一个 catch 兜全场";
    check("① std::vector::at 越界被 catch 抓住 1 次", stdCatches, 1);

    int translated = 0;
    try {
        std::vector<QString> names;
        names.reserve(1);
        const QString s = names.at(5);          // 空表越界
        Q_UNUSED(s)
    } catch (const std::exception &ex) {
        ++translated;
        // 老练的做法：**在层的边界把异常翻译成 Result**，往上只走一条通道
        const ErrorInfo e = makeError(ErrCode::StdThrow, ErrDomain::StdException,
                                      QStringLiteral("import_config"),
                                      QStringLiteral("<vector>"),
                                      QStringLiteral("what_len=%1").arg(int(std::strlen(ex.what()))),
                                      QString::fromUtf8(ex.what()), false, QString());
        qInfo() << "  异常已在边界翻译成 Result：code =" << int(e.code)
                << "｜用户看到的文案 =" << userVisibleText(e);
    }
    qInfo() << "  → 翻译过来的异常次数 =" << translated
            << "｜上层只见 Result，这样「两套风格」在外面就是一套";
    check("① 越界异常在层边界翻译成 Result 的次数", translated, 1);
}

// ============================================================================
// 八、证据②：四类失败全走 Result，上层统一出文案
// ============================================================================
static void evidenceFourFailures(const QString &dir)
{
    qInfo() << "【证据②】一次导入，四类失败，四条 Result 通道";

    struct Case { QString file; QByteArray body; QString label; };
    const QString writeTarget = dir + QStringLiteral("/backup.json");

    QFile ready(writeTarget);
    if (ready.open(QIODevice::WriteOnly)) {
        ready.write("{}");
        ready.close();
        QFile::setPermissions(writeTarget, QFileDevice::ReadOwner);  // 造「写不进去」
    }

    const Case cases[] = {
        { QStringLiteral("nope-absent.json"), QByteArray(), QStringLiteral("① 文件不存在") },
        { QStringLiteral("broken.json"), "{\"serverName\": \"a\", \"port\": 700", QStringLiteral("② JSON 语法错") },
        { QStringLiteral("wrongtype.json"), "{\"serverName\": 42, \"port\": 7000}", QStringLiteral("③ 字段类型不符") },
        { QStringLiteral("typeport.json"), "{\"serverName\": \"a\", \"port\": \"8080\"}", QStringLiteral("③′ 端口是字符串（可恢复）") },
    };

    int shown = 0;
    int coerced = 0;
    for (const Case &c : cases) {
        const QString path = dir + QLatin1Char('/') + c.file;
        if (!c.body.isEmpty()) {
            QFile w(path);
            if (w.open(QIODevice::WriteOnly)) { w.write(c.body); w.close(); }
        }
        const QString op = QStringLiteral("import_config");
        Result<AppConfig> r = doImport(path);
        if (r.isOk()) {
            if (c.file == QStringLiteral("typeport.json")) {
                // 「端口写成字符串」**不是失败**，而是可恢复降级：validateConfig 用
                // toString().toInt(&ok) 显式转换，成功就带着结果继续，只留一条 qWarning。
                ++coerced;
                qInfo() << QString::asprintf("  %s｜降级成功：port = %d｜它的状态是 accepted，"
                                             "不是 rejected——「可恢复」的意思就是「能接着跑，但要记一笔」",
                                             c.label.toUtf8().constData(), r.value().port);
                check("② ③′ 端口字符串 \"8080\" 被降级成数字 8080", r.value().port, 8080);
            } else {
                qInfo() << QString::asprintf("  %s → 意外成功（说明造错没造出来）", c.label.toUtf8().constData());
                checkBool("② 造错失败：这一条本该被 Result 拒绝", false);
            }
            continue;
        }
        const ErrorInfo &e = r.error();
        ++shown;
        if (e.recoverable)
            qWarning("%s", describeLog(e).toUtf8().constData());    // 可恢复 → warning
        else
            qCritical("%s", describeLog(e).toUtf8().constData());    // 不可恢复 → critical
        qInfo() << QString::asprintf("  %s｜错误码=%d｜可恢复=%d｜恢复动作=%s｜用户文案=%s",
                                     c.label.toUtf8().constData(), int(e.code),
                                     int(e.recoverable),
                                     e.recoveryHint.isEmpty() ? "-" : e.recoveryHint.toUtf8().constData(),
                                     userVisibleText(e).toUtf8().constData());
        // 顺手证明「同一个错误在层间没丢信息」
        qInfo() << "     上下文：operation =" << e.operation
                << "at =" << e.atMs << "ms｜target(打码) =" << maskPath(e.target)
                << "｜params =" << e.params;

        if (c.file == QStringLiteral("nope-absent.json")) {
            check("② ① 缺文件 → code 101 FileNotFound", int(e.code), int(ErrCode::FileNotFound));
            checkBool("② ① 缺文件标成「可恢复」（用户能自己重选）", e.recoverable);
            checkBool("② ① 日志里的 target 已打码（不含原始路径）",
                      !describeLog(e).contains(dir));
        } else if (c.file == QStringLiteral("broken.json")) {
            check("② ② JSON 语法错 → code 201 JsonSyntax", int(e.code), int(ErrCode::JsonSyntax));
            checkBool("② ② 语法错不可恢复（重试也没用，得改文件）", e.recoverable, false);
        } else if (c.file == QStringLiteral("wrongtype.json")) {
            check("② ③ serverName 写成数字 → code 302 FieldTypeMismatch",
                  int(e.code), int(ErrCode::FieldTypeMismatch));
            checkBool("② ③ 类型不符的日志带着期望/实际类型",
                      e.params.contains(QStringLiteral("expect=")));
        }
    }

    // ④ 磁盘写入失败：拿只读文件当目标
    Result<bool> w = writeFileAll(writeTarget, "{\"ok\":1}", QStringLiteral("save_config"));
    const bool writeRejected = !w.isOk();
    if (writeRejected) {
        ++shown;
        const ErrorInfo &e = w.error();
        qCritical("%s", describeLog(e).toUtf8().constData());
        qInfo() << QString::asprintf("  ④ 磁盘写入失败｜错误码=%d｜可恢复=%d｜恢复动作=%s｜用户文案=%s",
                                     int(e.code), int(e.recoverable),
                                     e.recoveryHint.isEmpty() ? "-" : e.recoveryHint.toUtf8().constData(),
                                     userVisibleText(e).toUtf8().constData());
        checkBool("② ④ 只读目标写不进去，code 落在 WriteDenied/WriteFailed 上",
                  int(e.code) == int(ErrCode::WriteDenied) || int(e.code) == int(ErrCode::WriteFailed));
        checkBool("② ④ 写失败也带着「怎么救」的提示", !e.recoveryHint.isEmpty());
    } else {
        qInfo() << "  ④ 本机允许覆盖「只读属性」的文件（管理员/root/某些网络盘会这样）→"
                   "这一步在这台机器上造不出写失败，核对跳过，不计入硬账总数。";
    }
    QFile::setPermissions(writeTarget, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QFile::remove(writeTarget);

    qInfo() << "  → 走通 Result 通道的失败数 =" << shown
            << "（①②③④ 共 4 条；③′ 是**可恢复降级**，它成功、只多一条 warning，不算失败）"
            << "｜日志计数：qWarning 累计" << LogProbe::nWarning
            << "次，qCritical 累计" << LogProbe::nCritical << "次";
    check("② 走 Result 通道的失败条数（①②③ + 可能的④）", shown, 3 + (writeRejected ? 1 : 0));
    check("② ③′ 的可恢复降级次数（成功但留了一条 warning）", coerced, 1);
}

// ============================================================================
// 九、证据③：日志里能不能挖出原始路径 —— 打码前 vs 打码后
// ============================================================================
static void evidenceRedaction(const QString &dir)
{
    qInfo() << "【证据③】生产日志别把路径原样打出去";

    const ErrorInfo raw = makeError(ErrCode::FileUnreadable, ErrDomain::Io,
                                    QStringLiteral("import_config"), dir + QStringLiteral("/secret-name.json"),
                                    QStringLiteral("size=12"), QStringLiteral("open failed"), false, QString());

    const int hitsBefore = LogProbe::leakHits;
    const int linesBefore = LogProbe::lines.size();
    // 反面写法：直接把 target 原样塞进日志
    qWarning("[op=import_config] target=%s msg=%s", raw.target.toUtf8().constData(),
             raw.message.toUtf8().constData());
    const int leaked = LogProbe::leakHits - hitsBefore;

    const int linesMid = LogProbe::lines.size();
    const int hitsMid = LogProbe::leakHits;
    // 正面写法
    qWarning("%s", describeLog(raw).toUtf8().constData());
    const int clean = LogProbe::leakHits - hitsMid;

    qInfo() << QString::asprintf("  原始路径特征串 = \"%s\"（长度 %d）",
                                 dir.toUtf8().constData(), int(dir.length()));
    qInfo() << QString::asprintf("  反面写法：新增 %d 行日志，其中 %d 行含原始路径",
                                 linesMid - linesBefore, leaked);
    qInfo() << QString::asprintf("  正面写法：新增 1 行日志，含原始路径 %d 行（target 已打码为 %s）",
                                 clean, maskPath(raw.target).toUtf8().constData());
    qInfo() << "  → 累计泄漏次数 =" << LogProbe::leakHits
            << "｜规矩：**路径/账号/密钥一律打码或不打**；params 里放「大小/偏移/错误号」这种可定位又不涉密的东西";
    check("③ 反面写法（target 原样进日志）新增日志行数", linesMid - linesBefore, 1);
    check("③ 反面写法泄漏原始路径的行数", leaked, 1);
    check("③ 正面写法（describeLog 打码）泄漏行数", clean, 0);
    // 全流程累计 = 2，不是 1（这是实测纠正过来的一条，也是本取证最想教的东西）：
    //   第 1 次：上面那行**故意写错**的反面日志（target 原样进日志）；
    //   第 2 次：就在此处第 761~762 行，取证自己用 qInfo 把「原始路径特征串 = <dir>」
    //           原样打印进了日志 —— 而 LogProbe 的判据就是「这一行含不含 dir」。
    //   探针（LogProbe::handle）对每一条日志一视同仁：它不区分「故意演示的泄漏」和
    //   「讲解时顺手打印的特征串」，只要消息里出现 dir 就 +1。所以计数是 2。
    //   这不是探针算错，恰恰是它的价值：**连自己的取证 header 都会漏路径** —— 现实里
    //   太多「debug 打印把用户目录/密钥原样 log 出去」就是这么发生的。要打印这个 token
    //   又不想被计数，就得像 maskPath() 那样先打码；这里刻意保留原样，让它成为第 2 条证据。
    check("③ 全流程累计泄漏次数（故意写错的 1 行 + 取证 header 打印特征串的 1 行）",
          LogProbe::leakHits, 2);
    checkBool("③ 打码后的 target 不含原始目录", !maskPath(raw.target).contains(dir));
    checkBool("③ 打码结果仍点得出文件名（<secret-name.json>）",
              maskPath(raw.target).contains(QStringLiteral("secret-name")));
}

// ============================================================================
// 十、证据④：跨线程错误上报
// ============================================================================
static void evidenceCrossThread(const QString &dir, UiMirror *mirror)
{
    qInfo() << "【证据④】工作线程里的错误怎么回主线程";

    qRegisterMetaType<ImportReport>("ImportReport");   // 跨线程信号必须先注册

    QThread workerThread;
    workerThread.setObjectName(QStringLiteral("io-worker"));
    ImportWorker *worker = new ImportWorker(dir);
    worker->moveToThread(&workerThread);
    QObject::connect(worker, &ImportWorker::importFailed, mirror, &UiMirror::onReport,
                     Qt::QueuedConnection);           // 显式 Queued：不偷跑在子线程执行
    QObject::connect(&workerThread, &QThread::finished, worker, &QObject::deleteLater);

    workerThread.start();
    QMetaObject::invokeMethod(worker, "runOnce", Qt::QueuedConnection);

    // 等两条 queued 调用跑完（不许 exec() 阻塞对话框）
    for (int i = 0; i < 20; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(10);
    }
    workerThread.requestInterruption();
    workerThread.quit();
    const bool waited = workerThread.wait();

    qInfo() << "  收到上报 =" << mirror->received << "次｜其中在主线程处理 ="
            << mirror->onMainThread << "次｜线程退出 wait() =" << waited;
    for (const QString &s : mirror->shown)
        qInfo() << "     " << s;
    check("④ 工作线程发出的错误上报被主线程收到几次", mirror->received, 1);
    check("④ 上报在主线程处理（可以碰 UI）的次数", mirror->onMainThread, 1);
    checkBool("④ 子线程 quit() 后 wait() 收得干净", waited);
    checkBool("④ 收到的报告带着错误码 101（缺文件，与证据②同一条流水）",
              !mirror->shown.isEmpty() && mirror->shown.first().contains(QStringLiteral("code=101")));
    checkBool("④ 接收槽里确认的线程身份是 main",
              !mirror->shown.isEmpty() && mirror->shown.first().contains(QStringLiteral("接收线程=main")));
    qInfo() << "  → 反面教材两条：① 在工作线程里构造/弹出 QMessageBox（跨线程碰 UI，随机崩）；"
               "② 用全局变量传错误（没同步、没顺序）。信号带结构体是正解，代价是**必须注册元类型**。";
}

// ============================================================================
// 十一、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("quality_05_error_handling"));
    qRegisterMetaType<ImportReport>("ImportReport");

    const QString tempRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString dir = QDir(tempRoot).filePath(QStringLiteral("qt_lf_quality05"));
    QDir().mkpath(dir);

    UiMirror mirror;

    QTimer::singleShot(0, [&dir, &mirror]() {
        qInfo() << "================================================================";
        qInfo() << "quality 第 5 讲 · 错误处理策略（平台 =" << QGuiApplication::platformName()
                << ", Qt =" << QT_VERSION_STR << "）";
        qInfo() << "  临时目录（结束会清）：" << dir;
        qInfo() << "================================================================";
        g_clock.start();

        LogProbe::install(dir);                       // 从这一刻起数日志
        evidenceStyleBoundary(dir);
        evidenceFourFailures(dir);
        evidenceRedaction(dir);
        evidenceCrossThread(dir, &mirror);

        QTimer::singleShot(20, [&dir, &mirror]() {
            LogProbe::uninstall();
            QFile::remove(dir + QStringLiteral("/broken.json"));
            QFile::remove(dir + QStringLiteral("/wrongtype.json"));
            QFile::remove(dir + QStringLiteral("/typeport.json"));
            const bool rmdirOk = QDir().rmdir(dir);

            const qint64 totalMs = g_clock.elapsed();
            qInfo() << "【总账】日志行数 =" << LogProbe::lines.size()
                    << "｜qDebug =" << LogProbe::nDebug
                    << "｜qInfo =" << LogProbe::nInfo
                    << "｜qWarning =" << LogProbe::nWarning
                    << "｜qCritical =" << LogProbe::nCritical
                    << "｜原始路径泄漏行数 =" << LogProbe::leakHits;
            qInfo() << "【总账】跨线程上报 =" << mirror.received
                    << "次，全部落在主线程 =" << (mirror.onMainThread == mirror.received)
                    << "｜全流程耗时 =" << totalMs << "ms";
            qInfo() << "【一句话】qFatal 会**当场 abort**（QMessageLogger::fatal 标了"
                       "QT_MESSAGE_LOGGER_NORETURN + noexcept），进程连退出码都不体面，"
                       "析构与清理全跳过：只有「继续跑下去更危险」才用它，其它一律 Result + qCritical。";

            checkBool("【总账】消息处理器确实收进了日志（行数 > 0）", LogProbe::lines.size() > 0);
            checkBool("【总账】不可恢复的错至少 2 条 qCritical", LogProbe::nCritical >= 2);
            checkBool("【总账】可恢复/降级/反面写法加起来至少 3 条 qWarning",
                      LogProbe::nWarning >= 3);
            check("【总账】原始路径泄漏总数（反面写错的 1 行 + 取证 header 打特征串的 1 行）",
                  LogProbe::leakHits, 2);
            checkBool("【总账】临时目录清理干净", rmdirOk);

            qInfo().noquote() << QStringLiteral("\n【硬账】%1/%2 项核对通过 → 退出码 %3")
                                     .arg(Ledger::passed)
                                     .arg(Ledger::total)
                                     .arg(Ledger::exitCode());
            QCoreApplication::exit(Ledger::exitCode());
        });
    });

    return app.exec();
}

// ============================================================================
// 十二、速查与纪律
// ============================================================================
/*
 * 一、Qt 里错误的三种走法（记牢边界，别混）
 *   ① 返回值 + 出参/错误状态（**绝大多数 Qt API**）
 *      QFile::open()→bool，细节 error()/errorString()；
 *      QJsonDocument::fromJson(ba,&err)→文档，细节 QJsonParseError.error/offset；
 *      QSqlQuery::exec()→bool，细节 lastError()（QSqlError::type()/text()/number()）；
 *      QNetworkReply::error()（Qt 6.5 起是 Qt::NetworkError 枚举）不用等异常。
 *      → 特点：**不会抛**，你 try/catch 也是零作用（本讲 catches=0 就是证据）。
 *   ② C++ 标准库异常（容器越界、new 失败、string 转换）
 *      at()/front()/back() 越界、bad_alloc、regex_error…… → 这些**必须** catch，
 *      而且要**在层的边界**接住、翻成 Result 再往上（证据①第三步）。
 *   ③ QVariant 的转换失败（本机 6.12 实测：**不抛**）
 *      取值写 `value<T>()`（Qt 6 没有 `to<T>()`），失败给你 0；`toInt(&ok)` 把 ok 置 false。
 *      旧稿在这里写的「QtException 家族 / QVariant::InvalidTypeException」在 qtbase 全树
 *      grep 是 0 处，别照着它去 catch。
 *      ⚠ 真正的坑是另一件事：`canConvert<int>()` 对字符串 "abc" 返回 **true**——
 *      它只说「有这条转换路径」，不说「这一次会转成功」。实测：
 *      QVariant("abc").canConvert<int>() = 1，而 value<int>() = 0、toInt(&ok) 的 ok = 0。
 *      → 所以判「转得动吗」要拿结果说话：`toInt(&ok)` / `convert(QMetaType::fromType<T>())`
 *      的返回值，而不是 canConvert。
 *
 * 二、Result<T> 该带什么（少一样，半夜就定位不了）
 *   code（可枚举的错，**给分支用**）+ domain（哪一层）+ message（给人读的技术描述）
 *   + operation（在做什么）+ target（对哪个对象）+ params（关键数字）
 *   + recoverable（能不能救）+ recoveryHint（怎么救）+ atMs（什么时候）。
 *   ⚠ target 进日志前打码；params 只放尺寸/偏移/错误号，**不放密钥与账号**。
 *
 * 三、两层出口
 *   开发者层：qDebug 细节 / qInfo 里程碑 / qWarning 可恢复 / qCritical 不可恢复 /
 *             qFatal 当场 abort（**慎用**：Q_ASSERT 也属于「debug 才 abort」的另一路，
 *             发布版里 NDEBUG 会被编掉——不要把校验逻辑写在 Q_ASSERT 里）。
 *   用户层：状态栏一行 / toast 一瞬 / 对话框（只在必须用户决定时）。
 *             **文案里不出现路径、错误码、堆栈**；这些进日志，UI 上给「下一步做什么」。
 *   分工的硬规矩：日志与文案**从同一个 ErrorInfo 生成**，不要两处各写一遍，
 *                 否则线上只有文案、复现时日志对不上。
 *
 * 四、跨线程上报
 *   结构体要 Q_DECLARE_METATYPE + qRegisterMetaType（跨线程 queued 连接必需）；
 *   connect 显式写 Qt::QueuedConnection；接收方在主线程，才允许碰 UI；
 *   工作线程里**不 new QWidget/QDialog**，不发「弹个框」的请求。
 *
 * ——— 老师傅的话 ———
 *   徒弟，错误处理这活儿，新手写「有没有错」，老手写「错了我该怎么办」。
 *   你看证据②那五行：每一行都带着可恢复与恢复动作。用户不需要知道 offset=27，
 *   他需要知道「检查引号和逗号」；但你半夜接电话时需要那个 27。
 *   所以同一条 ErrorInfo，**两份出口**，一份都不能省。
 *
 *   再送你三句：一、别信 try/catch 能兜住 Qt——它兜的是 C++ 那一半，
 *   Qt 的资源和解析 API 走的是返回值，兜不住就是「线上从没进过 catch，
 *   可错误一直在发生」。二、别用 bool + 全局状态传错，那条路上信息一路在掉，
 *   掉到 UI 层就只剩「失败了」。三、日志里的路径是要打码的，这条不是风格问题，
 *   是合规问题；你导出的日志会被客户、被工单系统、被我方同事看到。
 *
 *   至于 qFatal：我带了二十年只用过一次，那次是因为数据库句柄已经被写坏了，
 *   继续跑会污染客户的数据。它 abort 起来连你的清理代码都不给机会——
 *   想「体面退出」，就 qCritical + 让上层走到 quit()。
 *
 * ——— 小练习 ———
 *   1. 把证据①里的 `f.open()` 失败改写成抛异常的假象（自己 throw std::runtime_error），
 *      观察 catches 从 0 变 1；再改回来，说出「为什么 Qt 不这么设计」（提示：C++ 异常
 *      穿过 Qt 的 C 接口、跨 DLL 边界、以及 Qt 支持的构建配置里异常可能根本被关掉）。
 *   2. 给 Result<T> 加一个 `andThen()`（把上一步的值喂给下一个可能失败的函数），
 *      用四个阶段串成一条链重写 doImport()，数一数你省下了几个 if。
 *   3. 在 validateConfig 里再加一项「port 与 serverName 的一致性」业务校验，
 *      给它一个新的 ErrCode，并把它的用户文案写成「端口和服务器名对不上」；
 *      然后故意让 qWarning 与 qCritical 各多一条，核对总账里的计数是否跟着变。
 *   4. 把证据④的 connect 从 Qt::QueuedConnection 改成 Qt::DirectConnection，
 *      看 onMainThread 从 4 变 0（或变少），并解释为什么这样写早晚会崩。
 */
// ============================================================================

#include "05_error_handling.moc"
