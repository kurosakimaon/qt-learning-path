// ============================================================================
// 第 2 讲 · 把网络层写成可测试的一层（资深向）
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   第 1 讲把 NAM 的规矩立了。这一讲解决**工程问题**：业务代码不该看见 QNetworkReply。
//   一旦界面里到处是 `reply->attribute(HttpStatusCodeAttribute).toInt()`，
//   你就得到处判断三类完全不同的失败，而且这套东西**没法测**（它依赖真实网络）。
//     1) 包一层「请求 → 回调」的 API：RestClient::get/postJson(path, ..., callback)。
//        界面只认 ApiResult，不认 QNetworkReply。（要 future 也有路：Qt6 的
//        QPromise + QFutureWatcher 在 QtCore/qpromise.h / qfuturewatcher.h，本讲用回调，
//        因为「谁发起谁负责生命周期」在回调式里最直白；future 那条在 projects 里再走一遍。）
//     2) **三类失败分开建模**，别混成一个 bool：
//        成功 / HTTP 语义错（4xx 5xx，服务器活着且答了你）/
//        网络错（连不上、超时、被取消，压根没答复）/ 解析错（200 但正文不是合法 JSON）。
//        这三类的**用户提示、重试策略、日志级别全都不同**，混了就只能一律弹「网络异常」。
//     3) 请求头怎么设：Accept / Content-Type 这类**在 KnownHeaders 里有名字**的，
//        可以走 setHeader(KnownHeaders)；
//        **Authorization 在 KnownHeaders 里没有对应项**（qnetworkrequest.h 第 33~48 行只有
//        ContentType/ContentLength/Location/LastModified/Cookie/SetCookie/ContentDisposition/
//        UserAgent/Server/IfModifiedSince/ETag/IfMatch/IfNoneMatch 这十三项，Accept 也不在里面），
//        所以 Bearer 只能 setRawHeader("Authorization", ...)；Qt 6.6+ 另有 QHttpHeaders
//        （WellKnownHeader 是 **enum class**，第 114/117/145/170 行有 Accept / Authorization /
//        ContentType），批量设头用它更整齐。**提醒：网上大量教程写的 `QNetworkRequest::AcceptHeader`
//        在本机这份头文件里不存在**，抄了就是一句编译错误。本讲两条路子都给，并且各发一发，
//        让对端把看到的头原样打回来对账（证据①a 和证据⑥）。
//     4) POST 的 JSON 体：QJsonDocument(o).toJson(QJsonDocument::Compact) 出来的**已经是
//        UTF-8 字节**（QByteArray，第 75 行那个 toJson 的返回类型），别再用 QString::toUtf8()
//        绕一遍，也别手工加 BOM。注意这个 enum 叫 **JsonFormat**（第 58 行），不叫 QJsonFormat；  name-ok:QJsonFormat 本行正在说「不叫 QJsonFormat」
//        而且 toJson 的**默认参数是 Indented**，不传就是带缩进的那版。
//        Content-Length 交给框架算，你自己算就会和 Qt 的 chunked 判断打架。
//     5) 回调的**析构安全**：connect 的 context 参数 —— functor 重载是第 3 个
//        （QObject::connect(sender, signal, context, functor, type)，qobject.h 第 228 行；
//        机制在 metaobject 第 4 讲，这里只用不展开）。证据⑦现场演示一次
//        「对象死了回调就不发」，并且**把它的代价也打出来**：回调没了，
//        客户端的 pending 表也就没人清了，所以 ~RestClient 必须自己 abort。
//     6) 重试与幂等：只对幂等方法重试（GET/PUT/DELETE），POST 默认不重试；
//        真要重试 POST，带幂等键（Idempotency-Key），证据④a 用 /v1/flaky 打出 attempts。
//     7) 可测试性：**baseUrl 可注入**是本讲的命门。演示时把它指向本机回环自建服务器，
//        生产时指向真服务。同一段客户端代码，换的是 baseUrl，不是 if 分支。
//
// 对应构建目标名：network_02_rest_client
//   find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
//   add_executable(network_02_rest_client 02_rest_client.cpp)
//   target_link_libraries(network_02_rest_client PRIVATE Qt6::Widgets Qt6::Network)
//   set(CMAKE_AUTOMOC ON)                       # 本文件**有两个 Q_OBJECT 类**，需要 moc
//   # 手工核对时：moc 02_rest_client.cpp -o 02_rest_client.moc
//   #             文件末尾那句 #include "02_rest_client.moc" 就是给 AUTOMOC 的钩子
//
// 运行前须知：
//   · **绝不访问公网**：本讲在 127.0.0.1 上用 QTcpServer 自己搭一个假 REST 服务，
//     客户端 baseUrl 注入成它。断网、无 DNS、防火墙全拦都能跑完，也不会把包打到任何第三方。
//   · **端口动态选**：listen(QHostAddress::LocalHost, 0) 之后读 serverPort() 回填 baseUrl。
//   · 有超时兜底：客户端每请求 setTransferTimeout（证据④c 故意用 400ms 去打一个
//     收了连接一个字不答的 /v1/silent），main 里另有 15 秒总闸；网络卡住不会让进程挂死。
//   · 无人值守：全自动，没有模态对话框（QDialog::exec 那种）、没有 waitFor*、没有 sleep。
//     注意 main **是要 app.exec() 的** —— 事件循环不开，reply::finished 一个都不会到；
//     这一讲推进全靠 QTimer::singleShot 串起来，exec() 只在最后收束。
//   · 收尾顺序：所有 reply 走完回调 → 停服务器（close + 断连接）→ exit(账).
//     先关对端再收摊，顺序反了会有迟到的半包把日志弄脏。
//   · ★ **退出码 = 第五节那本硬账**（Ledger::exitCode()）：核对项没全对上、或者一项都没
//     对上（checksTotal == 0 也算失败：什么都没测却说测过了）、或者总闸跳过一次 → 返回 1。
//     用 quit() 的话进程永远 0，这张表打印得再认真，CI 也看不见失败 —— 把话说到屏幕里
//     只算说了一半。
//   · 可以无头跑：`network_02_rest_client -platform offscreen`。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 network_02_rest_client）。本机这一轮：合账结果：29/29 项对上。
//     这只证明「编得过、跑得起来、上面这行为绿」——换 Qt 版本 / 换平台 / 换 CPU 核数
//     之后，正文里带具体数字的「预期」请以你自己那次运行输出为准
//     （重跑：bash _tools/run_all.sh）。
// ============================================================================

#include <QApplication>                 // QtWidgets/qapplication.h
#include <QCoreApplication>
#include <QObject>                       // QtCore/qobject.h（自由函数里用 QObject::connect）
#include <QTimer>
#include <QElapsedTimer>
#include <QDebug>
#include <QtGlobal>

#include <QNetworkAccessManager>         // QtNetwork/qnetworkaccessmanager.h
#include <QNetworkRequest>               // QtNetwork/qnetworkrequest.h
#include <QNetworkReply>                 // QtNetwork/qnetworkreply.h
#include <QHttpHeaders>                  // QtNetwork/qhttpheaders.h（Qt 6.6+，批量设头）
#include <QHostAddress>                  // QtNetwork/qhostaddress.h
#include <QTcpServer>
#include <QTcpSocket>

#include <QUrl>
#include <QUrlQuery>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>              // QtCore/qjsonparseerror.h：offset + errorString()
#include <QVariant>
#include <QHash>
#include <QList>
#include <QPointer>

#include <functional>                    // std::function<void(const ApiResult&)>
#include <chrono>

// ============================================================================
// 一、对着本机头文件核过的签名（D:\Qt\6.12.0\mingw_64\include，Qt 6.12.0）
// ============================================================================
// QtNetwork/qnetworkrequest.h
//   第 33~48 行 enum KnownHeaders { ContentTypeHeader, ContentLengthHeader, LocationHeader,
//              LastModifiedHeader, CookieHeader, SetCookieHeader, ContentDispositionHeader,
//              UserAgentHeader, ServerHeader, IfModifiedSinceHeader, ETagHeader,
//              IfMatchHeader, IfNoneMatchHeader, NumKnownHeaders }
//              ← 十三项，**没有 AuthorizationHeader**：Bearer 只能走 rawHeader（第 151 行）。
//                这一条是本讲最容易记错的，网上大量教程写的 QNetworkRequest::AcceptHeader
//                在这个 enum 里**根本不存在**（Accept 也不在里面）。
//   第 138/139 行 QVariant header(KnownHeaders) const / void setHeader(KnownHeaders, const QVariant&)
//   第 148/150 行 QByteArray rawHeader(const QByteArray&) / rawHeader(QAnyStringView) const
//   第 151 行 void setRawHeader(const QByteArray &name, const QByteArray &value);
//   第 133/134/135 行 QHttpHeaders headers() const / void setHeaders(const QHttpHeaders&)
//              / setHeaders(QHttpHeaders &&)
//              ← 三个重载挨着；**setHeaders 是整批换**，所以本文件先把 Accept/Authorization
//                都收进同一个 QHttpHeaders 再一次性交出去，不跟 setRawHeader 混着写。
//   第 111 行 DefaultTransferTimeoutConstant = 30000；第 114~115 行
//              static constexpr auto DefaultTransferTimeout = std::chrono::milliseconds(…)
//              ← 「不给超时就是 30 秒」这句话的出处，界面卡半分钟就是它
//   第 208 行 void setTransferTimeout(std::chrono::milliseconds duration = DefaultTransferTimeout);
//              ← **每个请求**一份，本讲客户端的 setTransferTimeout(ms) 最终走到这里
//   第 103~107 行 enum RedirectPolicy { ManualRedirectPolicy, NoLessSafeRedirectPolicy,
//              SameOriginRedirectPolicy, UserVerifiedRedirectPolicy }
// QtNetwork/qnetworkaccessmanager.h
//   第 83~88 行 get(…) / 第 86~90 行 post(…) / 第 93~97 行 put(…)（各有 QIODevice*、
//              QByteArray、nullptr 三种重载）
//   第 101/102 行 sendCustomRequest(request, verb, data)   ← 非标准动词走这条
//   第 119/120 行 setRedirectPolicy(…) / redirectPolicy()
//   第 128 行 setTransferTimeout(int timeout)（旧写法，175~178 行转发给下面那个）
//   第 131/132 行 setTransferTimeout(std::chrono::milliseconds = DefaultTransferTimeout)
//              ← 挂在 NAM 上是**全局兜底**，挂在 request 上是**这一发**，两个层次要分清
// QtNetwork/qhttpheaders.h
//   第 114 行 enum class WellKnownHeader { ... 第 117 Accept, 第 145 Authorization,
//              第 170 ContentType ... }   ← **enum class**，写全 QHttpHeaders::WellKnownHeader::Authorization
//   写接口只有 append / insert / replace / replaceOrAppend（第 306/309/312/315 行，全返回 bool，
//              参数是 QAnyStringView 或 WellKnownHeader）—— **没有 setHeader / setRawHeader 这两个名字**
// QtNetwork/qnetworkreply.h
//   第 94 行 NetworkError error() const / 第 135 行 errorOccurred / 第 134 行 finished
//   第 118 行 QVariant attribute(QNetworkRequest::Attribute) const
//   第 33~78 行 NetworkError 分类（★ 下面「=N」是**枚举值**，「第 X 行」是**声明行号**，
//              本机逐个数过；写死数字之前先数枚举 —— 第 4 讲就是背了个「41」被实测打脸的）：
//              1~99 网络层：ConnectionRefusedError=1（第 37 行）、RemoteHostClosedError=2（38）、
//              HostNotFoundError=3（39）、TimeoutError=4（40）、OperationCanceledError=5（41）、
//              SslHandshakeFailedError=6（42）、UnknownNetworkError=99（48）/
//              101~199 代理：ProxyConnectionRefusedError=101（第 51 行）/
//              201~299 内容：ContentAccessDenied=201（59）、ContentNotFoundError=203（61）/
//              301~399 协议：ProtocolUnknownError=301（第 69 行）/
//              401~499 服务端：InternalServerError=401（第 74 行）
//              ← 特别记一下 **OperationCanceledError = 5**：Qt 6.12 里 41 这个号根本不存在，
//                abort() 之后 errorOccurred 带的就是 5。
//              ← **这就是「网络错」和「HTTP 语义错」在源头就是两拨的证据**，
//                我们建模时也不该把它们塞进同一个字段
// QtCore/qjsondocument.h
//   第 73 行 static QJsonDocument fromJson(const QByteArray &json, QJsonParseError *error = nullptr);
//   第 58~61 行 enum JsonFormat { Indented, Compact };   ← **不是 enum class**
//              （第 62~71 行那条 #else 分支在 Qt7 才把它换成 QJsonValue::JsonFormat），
//              所以 QJsonDocument::Compact 这种老写法在 6.12 上照样成立
//   第 75 行 QByteArray toJson(JsonFormat format = JsonFormat::Indented) const;
//              ← **默认是 Indented**：不传参就多一堆空格和换行，真走流量记得给 Compact
// QtCore/qjsonparseerror.h
//   第 18~34 行 enum ParseError { NoError = 0（19）, UnterminatedObject（20）, …,
//              IllegalNumber（26）, …, GarbageAtEnd（34）}  ← **不是 enum class**
//   第 36 行 QString errorString() const;
//   第 38~39 行 std::conditional_t<QT_VERSION_MAJOR < 7, int, qint64> offset = -1;
//              ← 类型**跟着主版本变**：Qt6 是 int，Qt7 起是 qint64。
//                所以本文件写 int(pe.offset) 而不是假定它一直是 int —— 大版本升级时这一句
//                会替你挡住「窄化转换」警告。默认 -1，**不是 0**：忘了判 NoError 就会
//                把「没有错」打印成「第 -1 字节」。
//   第 40 行 ParseError error = NoError;
// QtCore/qobject.h（⑦ 那两条证据靠它）
//   第 226 行 connect(sender, signal, functor)                       ← 没有 context，谁都不管谁的死活
//   第 228 行 connect(sender, signal, const QObject *context, functor, type = AutoConnection)
//              ← **第 3 个参数就是 context object**：它一析构，这条连接自动断开，
//                lambda 永不执行。本讲 send() 用的就是这个重载。
// QtCore/qtimer.h（重试那颗 + ⑦ 那记 delete 都走这里）
//   第 52/55 行 singleShot(int msec, const QObject *receiver, const char *member)  ← 老 SLOT 写法
//   第 57 行注释 "// singleShot with context"，第 66~70 行是带 context 的 functor 模板
//              （第 68 行那个参数类型是 ContextTypeForFunctor<Functor>::ContextType *receiver，
//                名字叫 receiver，起作用的是 **context**），它转手叫第 73 行那个重载，
//                后者再进 singleShotImpl（第 194 行）
//   第 84 行注释 "// singleShot without context"，第 85~88 行 singleShot(interval, functor)
//              ← 第 87 行把 receiver **写成 nullptr**：到点必发，没人替你看对象死没死
//   ★ 带 context 那一族会把它内部的一次性定时器挂到 context 名下，所以
//     **「在自己的 singleShot 回调里 delete 那个 context」= 在信号发射途中删发送者**，
//     是危险动作。⑦ 要当场 delete 一个对象，用的就是**不带 context** 那一族 ——
//     这不是随手选的，两处只差一个参数，命运不同（详见第七节 ⑦ 那段注释）。

// QtNetwork/qnetworkrequestfactory.h（Qt 6.4+ 的现成答案，思路和本讲的 baseUrl 注入一致）
//   第 43/44 行 baseUrl() / setBaseUrl(QUrl)
//   第 51~54 行 createRequest([path][, QUrlQuery])
//              ← 真项目可以直接用 QNetworkRequestFactory 当「请求构造」那一半，
//                本讲手写是为了让你看见「注入」这件事本身
// ============================================================================

// ============================================================================
// 二、结果建模：一个结构体把三类失败装清楚（**不抛异常**）
// ============================================================================
// 为什么不用异常：网络层失败的量级是「每秒钟都可能」，不是「这辈子罕见」。
// 异常穿过 Qt 的槽函数（noexcept 的事件派发路径）就是 std::terminate，
// 而且「HTTP 404」和「连不上」用同一种异常表达，调用方还是得 if 判断——绕回来了。
struct ApiResult {
    enum class Kind {
        Success,        // 2xx 且 JSON 解析成对象
        HttpError,      // 服务器答了，但状态码是 4xx/5xx（业务语义错）
        NetworkError,   // 压根没答上：连不上 / 超时 / 被取消 / DNS
        ParseError,     // 2xx 但正文不是合法 JSON，或不是对象
        Cancelled       // 我方主动 abort()（区分于「对端/超时」很重要：日志级别不同）
    };

    Kind kind = Kind::Success;
    int httpStatus = 0;                       // 0 = 没拿到状态码
    QString httpReason;                       // HttpReasonPhraseAttribute
    QNetworkReply::NetworkError netError = QNetworkReply::NoError;
    QString netErrorText;                     // reply->errorString()
    QString message;                          // 给人看的一句话（**给用户的那句从这里出**）
    QByteArray rawBody;                       // 原文（排错与单测断言都靠它）
    QJsonObject data;                         // 解析成功才有内容
    QString parseErrorText;                   // QJsonParseError::errorString()
    int parseErrorOffset = -1;                // 坏在第几个字节
    qint64 elapsedMs = 0;
    int attempts = 1;                         // 重试了几轮（含第一次）
    QString requestTag;                       // 我自己贴的方法学，方便日志对齐

    bool isOk() const { return kind == Kind::Success; }

    static QString kindName(Kind k)
    {
        switch (k) {
        case Kind::Success:      return QStringLiteral("Success");
        case Kind::HttpError:    return QStringLiteral("HttpError");
        case Kind::NetworkError: return QStringLiteral("NetworkError");
        case Kind::ParseError:   return QStringLiteral("ParseError");
        case Kind::Cancelled:    return QStringLiteral("Cancelled");
        }
        return QStringLiteral("Unknown");
    }
    QString kindName() const { return kindName(kind); }

    // 该不该重试：**只有网络层且非「我方取消」，或 5xx/429** 才值得重试；
    // 404/400/解析错重试一百次也是那个结果，重试只会把服务打得更惨。
    bool retryWorthy() const
    {
        if (kind == Kind::NetworkError)
            return netError != QNetworkReply::OperationCanceledError;
        if (kind == Kind::HttpError)
            return httpStatus >= 500 || httpStatus == 429;
        return false;
    }

    // 把**每一个字段**打出来：这就是「分层建模成立」的证据，不是嘴上说说。
    // ★ 参数收 QString 而不是 const char*：这条日志要拼中文（"终局 tag=…"），
    //   用 const char* 传进来再 fromLatin1 转一手，就是在自己给自己造乱码（本仓库踩过）。
    void dump(const QString &stage) const
    {
        qInfo() << QStringLiteral("  ┌ %1 → kind=%2").arg(stage, kindName());
        qInfo() << QStringLiteral("  │ httpStatus=%1 reason=\"%2\" netError=%3 netErrorText=\"%4\"")
                       .arg(httpStatus).arg(httpReason).arg(int(netError)).arg(netErrorText);
        qInfo() << QStringLiteral("  │ rawBody=%1 字节｜elapsed=%2ms｜attempts=%3｜tag=%4")
                       .arg(rawBody.size()).arg(elapsedMs).arg(attempts).arg(requestTag);
        if (kind == Kind::ParseError)
            qInfo() << QStringLiteral("  │ parseError: %1（offset=%2）")
                           .arg(parseErrorText).arg(parseErrorOffset);
        qInfo() << QStringLiteral("  │ data: id=%1 name=%2 online=%3 echo.headers=%4")
                       .arg(data.value(QStringLiteral("id")).toInt())
                       .arg(data.value(QStringLiteral("name")).toString())
                       .arg(data.value(QStringLiteral("online")).toBool()
                            ? QStringLiteral("true") : QStringLiteral("-"))
                       .arg(data.value(QStringLiteral("echo")).toObject().isEmpty()
                            ? QStringLiteral("(无)")
                            : QStringLiteral("有（见下一行）"));
        if (!data.value(QStringLiteral("echo")).toObject().isEmpty())
            // 这三个值**本来就是 QString**（QJsonValue::toString 出来的）。
            // 早期版本在这里写过 fromLatin1(…toLatin1()) 兜一圈 —— 那是一趟无意义的往返，
            // 而且只要头值里有一个非 ASCII 字符（比如中文名走 RFC 5987 编码前的原文）
            // 就会被 toLatin1() 打成 '?'。别为了「看着像转码」而转码。
            qInfo() << QStringLiteral("  │   echo = auth=\"%1\" accept=\"%2\" contentType=\"%3\"")
                           .arg(data.value(QStringLiteral("echo")).toObject()
                                    .value(QStringLiteral("authorization")).toString())
                           .arg(data.value(QStringLiteral("echo")).toObject()
                                    .value(QStringLiteral("accept")).toString())
                           .arg(data.value(QStringLiteral("echo")).toObject()
                                    .value(QStringLiteral("content_type")).toString());
        qInfo() << QStringLiteral("  └ message（给用户看的那句）= \"%1\"").arg(message);
    }
};

using ApiCallback = std::function<void (const ApiResult &)>;

// ============================================================================
// 三、RestClient：界面唯一允许接触的网络类
// ============================================================================
class RestClient : public QObject
{
    Q_OBJECT

public:
    // NAM 从外面**注入**（第 1 讲的「一个应用一个」在这里落地：客户端只是使用者）
    explicit RestClient(QNetworkAccessManager *nam, QObject *parent = nullptr)
        : QObject(parent), m_nam(nam)
    {
        m_clock.start();
    }

    ~RestClient() override
    {
        // 析构安全第一步：把在飞的请求 abort 掉。
        // 光靠 context object 只能保证「回调不发」，**连接与 reply 对象还活着**，
        // 不 abort 就是白占着服务器连接和内存。
        for (QPointer<QNetworkReply> &p : m_pending) {
            if (p)
                p->abort();
        }
        m_pending.clear();
    }

    // ★ 可测试性的命门：baseUrl 可注入。测试指回环，生产指真服务，代码一行不改。
    void setBaseUrl(const QUrl &base) { m_baseUrl = base; }
    QUrl baseUrl() const { return m_baseUrl; }
    void setToken(const QString &token) { m_token = token; }
    void setMaxRetries(int n) { m_maxRetries = n < 0 ? 0 : n; }
    void setTransferTimeout(int ms) { m_timeoutMs = ms; }
    int pendingCount() const { return m_pending.size(); }

    // 主动取消在飞的请求。★ 为什么要专门留一个口子：界面关掉、用户换了筛选条件、
    //   页面切走了，这些都得能把请求收回来。而 abort() 走的是
    //   QNetworkReply::OperationCanceledError —— toResult() 把它单独归成 Cancelled，
    //   **不**混进 NetworkError：日志级别不一样（自己取消的是常态，不是故障）。
    //   取消之后 reply 照样 finished → 回调照样来（带 Cancelled），所以调用方
    //   一定会被告知结果 —— 最怕的是「取消了就静默」，界面就永远卡在转圈。
    void cancelPending()
    {
        const int n = m_pending.size();
        for (QPointer<QNetworkReply> &p : m_pending) {
            if (p)
                p->abort();
        }
        qInfo() << QStringLiteral("  [取消] 我方主动 abort 了 %1 条在飞请求").arg(n);
    }

    // ★ 最后一个参数 context：**回调挂在谁身上**。传你的界面对象（或任何比请求活得短的 QObject），
    //   它一死，回调就永远不发 —— 这是 QObject::connect 那个 context 参数给的保护
    //   （functor 重载里它是第 3 个参数，qobject.h 第 228 行；别和老 SLOT 写法的
    //    receiver/member 那两个参数记混了）。机制在 metaobject 第 4 讲，
    //   本讲证据⑦现场用一次并把它的价格也打出来。
    //   不传就挂 this（客户端自己），语义和以前一样。
    void get(const QString &path, const QUrlQuery &query, const ApiCallback &cb,
             const QString &tag = QString(), QObject *context = nullptr)
    {
        send(buildRequest(path, query), QNetworkAccessManager::GetOperation,
             QByteArray(), cb, 1, realTag(tag, path), context);
    }

    // POST JSON：正文由 QJsonDocument 产 UTF-8 字节，Content-Type 在这里统一加
    void postJson(const QString &path, const QJsonObject &body, const ApiCallback &cb,
                  const QString &tag = QString())
    {
        QNetworkRequest req = buildRequest(path, QUrlQuery());
        req.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json; charset=utf-8"));
        // toJson() 的返回值**本来就是 QByteArray（UTF-8）**，别再 toUtf8() 绕一圈
        const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
        qInfo() << "  [发] POST" << path << "正文" << payload.size() << "字节：" << payload;
        send(req, QNetworkAccessManager::PostOperation, payload, cb, 1, realTag(tag, path));
    }

    // 另一条设头的路：**只用** setHeader / setRawHeader，一个 QHttpHeaders 都不碰。
    // 本讲把两条路都跑一遍，是为了让你在对端（服务端日志）里看见它们**产出的字节是一样的** ——
    // 而不是因为可以混着用（混用恰恰是坑，见 buildRequest 的注释）。
    void getOldStyle(const QString &path, const ApiCallback &cb, const QString &tag = QString())
    {
        QUrl url = m_baseUrl;
        url.setPath(m_baseUrl.path() + path);
        QNetworkRequest req(url);
        req.setRawHeader("Accept", "application/json");            // KnownHeaders 没有 Accept
        if (!m_token.isEmpty())
            req.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toUtf8());
        req.setRawHeader("X-Client", "qt-teach/02-oldstyle");
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("qt-teach/02"));
        req.setTransferTimeout(std::chrono::milliseconds(m_timeoutMs));
        qInfo() << "  [发] GET（老写法逐条 setRawHeader）" << path;
        send(req, QNetworkAccessManager::GetOperation, QByteArray(), cb, 1, realTag(tag, path));
    }

private:
    // tag 不传就自己造一个：日志全靠 tag 对齐，空 tag 等于把三条请求写成同一条。
    QString realTag(const QString &tag, const QString &path)
    {
        return tag.isEmpty() ? QStringLiteral("%1#%2").arg(path).arg(++m_seq) : tag;
    }

    QNetworkRequest buildRequest(const QString &path, const QUrlQuery &query) const
    {
        QUrl url = m_baseUrl;
        url.setPath(m_baseUrl.path() + path);           // baseUrl 可以带前缀路径
        if (!query.isEmpty())
            url.setQuery(query);

        // Accept / Authorization 这类：KnownHeaders 里**没有 Accept**（本机 qnetworkrequest.h
        // 第 33~48 行只有 ContentType/ContentLength/.../ETag 这十三项），所以 Bearer 与 Accept
        // 都只能走 rawHeader 或 QHttpHeaders。Qt5 博客里那句 `QNetworkRequest::AcceptHeader`
        // 是不存在的写法。
        // ★ 关键一条：**别把 setRawHeader 和 setHeaders 混在同一条请求上**。
        //   setHeaders 的语义是「整批替换」（本机 qnetworkrequest.h 第 133/134 行只有声明，
        //   替换这件事写在 Qt 的文档里：this will replace all existing headers），
        //   所以「先 setRawHeader 三条、再 setHeaders 两条」的净效果**只剩那两条** ——
        //   Bearer 就是这么悄悄丢掉的：请求照发、服务器照答 401，你查的是令牌不是代码。
        //   本函数因此把三件头一次交出去，走 QHttpHeaders 这一条路；
        //   另一条路（只走 setRawHeader）在 getOldStyle() 里，两条**各自完整**、互不打架。
        QHttpHeaders hs;
        hs.append(QHttpHeaders::WellKnownHeader::Accept, QLatin1String("application/json"));
        if (!m_token.isEmpty())
            hs.append(QHttpHeaders::WellKnownHeader::Authorization,
                      QStringLiteral("Bearer %1").arg(m_token).toUtf8());
        hs.append(QLatin1String("X-Client"), QLatin1String("qt-teach/02"));

        QNetworkRequest req(url);
        req.setHeaders(hs);
        req.setTransferTimeout(std::chrono::milliseconds(m_timeoutMs));
        return req;
    }

    void send(const QNetworkRequest &req, QNetworkAccessManager::Operation op,
              const QByteArray &body, const ApiCallback &cb, int attempt,
              const QString &tag, QObject *context = nullptr)
    {
        QNetworkReply *reply = nullptr;
        switch (op) {
        case QNetworkAccessManager::GetOperation:  reply = m_nam->get(req); break;
        case QNetworkAccessManager::PostOperation: reply = m_nam->post(req, body); break;
        case QNetworkAccessManager::PutOperation:  reply = m_nam->put(req, body); break;
        default:
            reply = m_nam->sendCustomRequest(req, QByteArray("GET"), body);
            break;
        }
        m_pending.append(QPointer<QNetworkReply>(reply));
        const qint64 t0 = m_clock.elapsed();

        // 回调挂在谁身上：调用方给了就用它，没给就挂客户端自己。
        // ★ 重试的那颗 singleShot 也用同一个 context：界面死了，
        //   「15ms 后再试一次」这件事也跟着死 —— 半路给一个已经不存在的界面重试，
        //   是「用户关了对话框、程序还在后台转」的正规成因之一。
        QObject *ctx = context ? context : static_cast<QObject *>(this);

        // ★ 第五个参数 = context object：ctx 死了，这个 lambda 永不执行。
        //   机制（为什么是第五个参数、和 Qt::QueuedConnection 的关系）在 metaobject 第 4 讲，
        //   这里只用；**别忘了**：没有这个 this，reply 会替一个已经消失的界面发信号。
        QObject::connect(reply, &QNetworkReply::finished, ctx,
                         [this, reply, req, op, body, cb, attempt, t0, tag, ctx]() {
            ApiResult r = toResult(reply, req, op, t0, attempt, tag);
            dropPending(reply);
            reply->deleteLater();                      // 所有权：谁拿到的谁还

            const bool idempotent = (op != QNetworkAccessManager::PostOperation);
            if (r.retryWorthy() && idempotent && attempt < m_maxRetries + 1) {
                qInfo() << QStringLiteral("  [重试] %1｜第 %2 次失败（%3 %4）→ 幂等方法，再来一次")
                               .arg(tag).arg(attempt).arg(r.kindName())
                               .arg(r.httpStatus > 0 ? QString::number(r.httpStatus)
                                                     : QString::number(int(r.netError)));
                // cb 就在闭包里，**直接往下传**：别为了「找回回调」再建一张按 tag 索引的表 ——
                // 那张表一旦忘了写入（本讲初稿就忘了），重试成功的结果会静默消失，
                // 表现是「界面一直转圈」而不是报错，最难查的就是这种。
                const int nextAttempt = attempt + 1;
                QTimer::singleShot(15, ctx, [this, req, op, body, cb, nextAttempt, tag, ctx]() {
                    // ★ context 要一路带着：重试这一发如果换回挂 this，
                    //   「界面死了」这条保护就只在第一发有效，第二发照样回调给一个鬼。
                    send(req, op, body, cb, nextAttempt, tag, ctx);
                });
                return;
            }
            if (r.retryWorthy() && !idempotent)
                qInfo() << QStringLiteral("  [不重试] POST 非幂等，重试=可能重复下单；"
                                          "要重试请带 Idempotency-Key 头（tag=%1）").arg(tag);
            if (cb)
                cb(r);
        });
    }

    void dropPending(QNetworkReply *reply)
    {
        for (int i = 0; i < m_pending.size(); ++i) {
            if (m_pending.at(i) == reply) {
                m_pending.removeAt(i);
                return;
            }
        }
    }

    // 把「一次 reply」翻译成「一个 ApiResult」——本讲的全部功力在这一个函数里。
    // ★ 它是**成员函数**而不是 static：要拿 m_clock 补 elapsedMs。
    //   写成 static 的时候这里只能填 0，日志上就永远是「elapsed=0ms」——
    //   一个不会报错的假数字，比报错更难查（本讲初稿就是这么漏的）。
    ApiResult toResult(QNetworkReply *reply, const QNetworkRequest &req,
                       QNetworkAccessManager::Operation op, qint64 t0,
                       int attempt, const QString &tag)
    {
        ApiResult r;
        r.elapsedMs = m_clock.elapsed() - t0;
        r.attempts = attempt;
        r.requestTag = tag;
        // ★ 这一行原来写的是无条件 `reply->readAll()`。本机跑出来 ④c（超时）和 ⑤（abort）
        //   两处各冒一条警告：「QIODevice::read (QNetworkReplyHttpImpl): device not open」
        //   —— 请求以错误收场时设备已经关了，readAll() 撞上 QIODevice 的守卫，
        //   警告照打、正文给你空的。也就是说老写法**结果碰巧对，过程在喊疼**。
        //   读之前先问 isOpen()，这才是能放进生产代码的形状（判完 error() 再决定读不读也对，
        //   但本讲把取正文集中在 toResult 这一个函数里，守卫放这里最省事）。
        r.rawBody = reply->isOpen() ? reply->readAll() : QByteArray();
        r.netError = reply->error();
        r.netErrorText = reply->errorString();
        r.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        r.httpReason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
        Q_UNUSED(op)
        Q_UNUSED(req)

        if (reply->error() == QNetworkReply::OperationCanceledError) {
            r.kind = ApiResult::Kind::Cancelled;
            r.message = QStringLiteral("请求已取消");
            return r;
        }
        // ★ 判性质的正确尺子（本讲最反直觉、也最容易写歪的一处，实测踩过）：
        //   **不能**用「reply->error() 是否非 0」来分「网络错 vs HTTP 错」。
        //   因为 Qt 把 HTTP 协议层的错误编进了 NetworkError 这**同一个 enum** 里，作为独立值：
        //     ContentNotFoundError = 203、InternalServerError = 401 …（见 qnetworkreply.h 第 36~73 行）。
        //   这些值是「协议错误码」，**跟 HTTP 状态码不是一个数**：一次 404，error() 给的是 203，
        //   而 attribute(HttpStatusCodeAttribute) 给的才是 404。于是老写法「error()≠0 就归 NetworkError」
        //   会把 404/500 误判成「连不上」，还会连锁两桩错：
        //     · 提前 return，跳过了下面 maybeAttachJson → 4xx 正文里服务端给的原因被丢成空；
        //     · retryWorthy() 把 HttpError 的 5xx 当网络错一样重试，404 也被拖进重试（本讲实测 404 跑了 3 发）。
        //   真正的判据是**有没有拿到 HTTP 状态码**：HttpStatusCodeAttribute 非 0 说明对端**答了**
        //   （哪怕答的是 4xx/5xx）→ 归 HttpError；为 0 才是传输层压根没应答（连不上/超时/DNS/SSL）→ NetworkError。
        const bool gotHttpResponse = (r.httpStatus != 0);
        if (gotHttpResponse) {
            if (r.httpStatus < 200 || r.httpStatus >= 300) {
                r.kind = ApiResult::Kind::HttpError;
                r.message = QStringLiteral("服务返回 %1 %2").arg(r.httpStatus).arg(r.httpReason);
                // 4xx 的正文里通常有服务端给的原因，**别丢**：解析成功就当 data 带着走
                maybeAttachJson(&r);
                return r;
            }
            maybeAttachJson(&r);
            if (r.kind != ApiResult::Kind::ParseError)
                r.message = QStringLiteral("成功（%1）").arg(r.httpStatus);
            return r;
        }
        // 没有 HTTP 状态码 = 对端压根没应答：连接被拒 / 超时 / DNS / SSL 等传输层错误
        r.kind = ApiResult::Kind::NetworkError;
        r.message = QStringLiteral("连不上服务（%1）").arg(r.netErrorText);
        return r;
    }

    static void maybeAttachJson(ApiResult *r)
    {
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(r->rawBody, &pe);   // 第 73 行那个 error 出参
        if (pe.error != QJsonParseError::NoError) {
            r->kind = ApiResult::Kind::ParseError;
            r->parseErrorText = pe.errorString();                             // 第 36 行
            r->parseErrorOffset = int(pe.offset);                             // 第 39 行
            if (r->message.isEmpty())
                r->message = QStringLiteral("服务返回 %1，但正文不是合法 JSON（%2，第 %3 字节）")
                                 .arg(r->httpStatus).arg(r->parseErrorText).arg(r->parseErrorOffset);
            return;
        }
        if (!doc.isObject()) {
            r->kind = ApiResult::Kind::ParseError;
            r->parseErrorText = QStringLiteral("顶层不是 JSON 对象（是数组/标量）");
            r->message = QStringLiteral("服务返回的 JSON 结构不是对象，客户端契约对不上");
            return;
        }
        r->data = doc.object();
        if (r->kind != ApiResult::Kind::HttpError)
            r->kind = ApiResult::Kind::Success;
    }

    QNetworkAccessManager *m_nam = nullptr;
    QUrl m_baseUrl;
    QString m_token;
    int m_maxRetries = 0;
    int m_timeoutMs = 2000;
    QElapsedTimer m_clock;
    QList<QPointer<QNetworkReply>> m_pending;
    int m_seq = 0;                          // 只用来给没带 tag 的请求编个号（见 realTag）
};

// 提醒（本讲最容易记错的一处）：`QNetworkRequest::AcceptHeader` **不存在**。
// 本机 qnetworkrequest.h 第 33~48 行的 KnownHeaders 只有 ContentType / ContentLength /
// Location / LastModified / Cookie / SetCookie / ContentDisposition / UserAgent / Server /
// IfModifiedSince / ETag / IfMatch / IfNoneMatch 这十三项，里面**没有 Accept，也没有 Authorization**。
// 所以设这两个头只有两条正道：
//   ① req.setRawHeader("Accept", "application/json");
//   ② QHttpHeaders hs; hs.append(QHttpHeaders::WellKnownHeader::Accept, ...);
//      req.setHeaders(hs);   // 注意 QHttpHeaders 的写接口叫 append / insert / replace /
//                            // replaceOrAppend（本机 qhttpheaders.h 第 306/309/312/315 行），
//                            // **没有 setHeader / setRawHeader 这两个名字**
// 本讲两条路各走一遍、**互不混用**：②在 buildRequest()（新代码推荐），①在 getOldStyle()。
// 混用的下场见 buildRequest() 里那条 setHeaders 是整批替换的注释。
// 网上大量 Qt5 教程写的 AcceptHeader 会把人带到坑里。

// ============================================================================
// 四、假服务：把「对端」也搬到本机回环上（这就是 baseUrl 注入能成立的原因）
// ----------------------------------------------------------------------------
// ★ 为什么本讲要自己写服务器：分层建模的**验收方式**就是「对端可以是假的」。
//   真实服务什么时候给你 500 由不得你，而 /v1/flaky 前两次必 500、第三次必 200 ——
//   重试逻辑对不对，只有在这样的对端上才能变成一条**可判定的**结论（attempts==3）。
// 路由表（路径写死在这里方便你 grep）：
//   /v1/device   200 + JSON 对象（含 echo：把收到的头原样还给你看）→ 证据①a ④b ⑥
//   /v1/missing  404 + JSON 对象（服务端给的原因，**别丢**）→ 证据②
//   /v1/flaky    前 2 次 500，第 3 次才 200（用来数 attempts）→ 证据④a
//   /v1/garbage  200 + 半截 JSON（ParseError，带 offset）→ 证据③a
//   /v1/array    200 + 合法 JSON **数组**（解析成功但契约对不上）→ 证据③b
//   /v1/lag      200，但拖 350ms 才答（给「主动取消」和 context 死亡当靶子）→ 证据⑤ ⑦
//   /v1/silent   收了连接**一个字都不答**（给 transferTimeout 当靶子）→ 证据④c
//   /v1/boom     500 + JSON（retryWorthy 的固定那一类）
//                ← ★ 本讲 main 没有用它，是留给小练习 1 的口子：
//                  「哪条路由其实没被跑到」这件事也要能一眼看出来。
// ============================================================================
// 假服务在 /v1/device 里返回的那个 id。★ 放在这里（而不是第七节）是为了让它只有**一个**出处：
// 对端造这个数、调用方核这个数，引用的都是同一个符号。
// 写在第七节就晚了 —— echoBody() 在那之前，只能退回字面量 7，那就是
// 「两边各写一遍、改一边忘另一边」，最后只靠合账兜住。别靠兜底，靠引用。
static constexpr int kDeviceId = 7;

class FakeRestServer : public QObject
{
    Q_OBJECT

public:
    explicit FakeRestServer(QObject *parent = nullptr) : QObject(parent)
    {
        m_server = new QTcpServer(this);              // 挂 parent：跟着我一起收摊
        QObject::connect(m_server, &QTcpServer::newConnection,
                         this, &FakeRestServer::onNewConnection);
    }

    bool start()
    {
        // 端口 0 = 内核挑，挑完必须回来读 serverPort()（硬编码 8080 的示例在 CI 上必挂）
        if (!m_server->listen(QHostAddress::LocalHost, 0)) {
            qWarning() << "  假服务起不来：" << m_server->errorString();
            return false;
        }
        m_port = m_server->serverPort();
        return true;
    }

    // 借一个端口再立刻还掉：下面「连不上」那一条要的正是**没人听的端口**。
    // ★ 这是「拿到一个大概率空闲的号」，不是「保证空闲」——真被别人抢走了，那一发的
    //   kind 就从 NetworkError 变成别的样子。所以本讲**不**把「具体是哪一类」写成硬结论，
    //   硬账只核「绝不能被当成成功」那一条（口径见第五节：概率性的东西只打印，不进硬账）。
    static quint16 borrowThenRelease()
    {
        QTcpServer probe;
        if (!probe.listen(QHostAddress::LocalHost, 0))
            return 0;
        const quint16 p = probe.serverPort();
        probe.close();                                // 立刻还：这就是「没人听」
        return p;
    }

    void stop()
    {
        m_server->close();                            // 不再接新连接
        for (QTcpSocket *c : std::as_const(m_clients)) {
            c->disconnectFromHost();
            c->deleteLater();                         // nextPendingConnection 的所有权在我手上
        }
        m_clients.clear();
        m_buffers.clear();
    }

    quint16 port() const { return m_port; }
    int requestsSeen() const { return m_requests; }
    int lastHeaderCount() const { return m_lastHeaderCount; }
    QString lastPath() const { return m_lastPath; }
    // 「框架自己算的 Content-Length」和「我实际收到的正文字节数」——两个数相等才算
    // 这一条链路对齐了（证据①b 就核这个，见 main 里那行 checkNum）。
    int lastContentLength() const { return m_lastContentLength; }
    int lastBodyBytes() const { return m_lastBodyBytes; }
    QString headerOf(const QString &lowerName) const { return m_headers.value(lowerName); }

signals:
    void requestArrived(const QString &verb, const QString &path);

private slots:
    void onNewConnection()
    {
        while (m_server->hasPendingConnections()) {
            QTcpSocket *sock = m_server->nextPendingConnection();   // 调用方接管所有权
            m_clients.append(sock);
            m_buffers.insert(sock, QByteArray());
            QObject::connect(sock, &QTcpSocket::readyRead, this, &FakeRestServer::onReadyRead);
            QObject::connect(sock, &QTcpSocket::disconnected, this, [this, sock]() {
                m_clients.removeOne(sock);
                m_buffers.remove(sock);
                sock->deleteLater();
            });
        }
    }

    void onReadyRead()
    {
        auto *sock = qobject_cast<QTcpSocket *>(sender());
        if (!sock)
            return;
        QByteArray &buf = m_buffers[sock];
        buf.append(sock->readAll());
        const int headEnd = buf.indexOf("\r\n\r\n");
        if (headEnd < 0)
            return;                                   // 半包：头还没收完，下次再说

        const QList<QByteArray> lines = buf.left(headEnd).split('\n');
        const QList<QByteArray> parts = lines.value(0).trimmed().split(' ');
        const QString verb = QString::fromLatin1(parts.value(0));
        const QString target = QString::fromLatin1(parts.value(1));
        m_lastPath = target.section(QLatin1Char('?'), 0, 0);

        // 把收到的头存下来（键一律转小写，查的时候不用记大小写）——
        // 这是「两条设头路子在对端看起来一样」那条证据的**唯一**取证手段：
        // 只有服务器能证明 Authorization 真的上路了。
        m_headers.clear();
        int headerLines = 0;
        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            const int colon = line.indexOf(':');
            if (colon <= 0)
                continue;
            m_headers.insert(QString::fromLatin1(line.left(colon)).toLower(),
                             QString::fromLatin1(line.mid(colon + 1).trimmed()));
            ++headerLines;
        }
        m_lastHeaderCount = headerLines;

        // ★ POST/PUT 有正文：**没收完不许答**。
        //   头一到就回复 = 在对端还在写的时候关连接，客户端拿到的是协议错，
        //   而你查的方向会是「客户端的 JSON 拼装」—— 完全查错地方。
        const int contentLength = m_headers.value(QStringLiteral("content-length"),
                                                 QStringLiteral("0")).toInt();
        if (buf.size() < headEnd + 4 + contentLength)
            return;                                   // 半包（正文），下次 readyRead 再说
        m_lastBodyBytes = buf.size() - (headEnd + 4);
        m_lastContentLength = contentLength;

        ++m_requests;
        emit requestArrived(verb, m_lastPath);
        respond(sock, verb, m_lastPath);
    }

private:
    static QByteArray frame(const QByteArray &status, const QByteArray &body,
                            const QList<QByteArray> &extra = {})
    {
        QByteArray r = "HTTP/1.1 " + status + "\r\n";
        r += "Content-Type: application/json; charset=utf-8\r\n";
        r += "Content-Length: " + QByteArray::number(qint64(body.size())) + "\r\n";
        r += "Server: qteach-fake-rest/02\r\n";
        r += "Connection: close\r\n";
        for (const QByteArray &h : extra)
            r += h + "\r\n";
        r += "\r\n";
        r += body;
        return r;
    }

    // 把「收到的头」还回去。状态行和 JSON 的键都是 ASCII，所以这里不碰中文转换。
    QByteArray echoBody() const
    {
        QJsonObject e;
        e.insert(QStringLiteral("authorization"), m_headers.value(QStringLiteral("authorization")));
        e.insert(QStringLiteral("accept"), m_headers.value(QStringLiteral("accept")));
        e.insert(QStringLiteral("content_type"), m_headers.value(QStringLiteral("content-type")));
        e.insert(QStringLiteral("x_client"), m_headers.value(QStringLiteral("x-client")));
        e.insert(QStringLiteral("user_agent"), m_headers.value(QStringLiteral("user-agent")));
        QJsonObject o;
        o.insert(QStringLiteral("id"), kDeviceId);
        o.insert(QStringLiteral("name"), QStringLiteral("loopback-device"));
        o.insert(QStringLiteral("online"), true);
        o.insert(QStringLiteral("echo"), e);
        return QJsonDocument(o).toJson(QJsonDocument::Compact);
    }

    void respond(QTcpSocket *sock, const QString &verb, const QString &path)
    {
        Q_UNUSED(verb);
        // 只认 /v1/<key>；其余路由统一 404，让「忘了注册路由」在对端也看得见
        const int at = path.indexOf(QStringLiteral("/v1/"));
        const QString key = at < 0 ? QString() : path.mid(at + 4).section(QLatin1Char('/'), 0, 0);

        if (key == QStringLiteral("silent"))
            return;                                   // 一个字都不答，让客户端的 timeout 干活

        if (key == QStringLiteral("lag")) {
            // 拖 350ms。★ context 传 sock：socket 先死了（客户端 abort 掉了），
            // 这条 singleShot 自动作废 —— 第三节讲的「context object 管生命周期」，
            // 对端同样要守这条规矩，否则你就是在往一条断掉的连接上写数据。
            QTimer::singleShot(350, sock, [this, sock]() {
                writeAndClose(sock, frame("200 OK", echoBody()));
            });
            return;
        }

        QByteArray out;
        if (key == QStringLiteral("device")) {
            out = frame("200 OK", echoBody());
        } else if (key == QStringLiteral("missing")) {
            QJsonObject o;
            o.insert(QStringLiteral("error"), QStringLiteral("no_such_device"));
            o.insert(QStringLiteral("hint"), QStringLiteral("服务端给的 404 原因，正文有用，别丢"));
            out = frame("404 Not Found", QJsonDocument(o).toJson(QJsonDocument::Compact));
        } else if (key == QStringLiteral("boom")) {
            out = frame("500 Internal Server Error", "{\"error\":\"boom\"}");
        } else if (key == QStringLiteral("flaky")) {
            ++m_flakyHits;
            out = (m_flakyHits < 3)
                      ? frame("500 Internal Server Error", "{\"error\":\"flaky-early\"}")
                      : frame("200 OK", echoBody());
        } else if (key == QStringLiteral("garbage")) {
            out = frame("200 OK", "{\"id\": 7, \"name\": ");   // 故意断在半截
        } else if (key == QStringLiteral("array")) {
            out = frame("200 OK", "[1,2,3]");                  // 合法 JSON，但不是对象
        } else {
            out = frame("404 Not Found", "{\"error\":\"unknown_route\"}");
        }
        writeAndClose(sock, out);
    }

    void writeAndClose(QTcpSocket *sock, const QByteArray &bytes)
    {
        sock->write(bytes);
        sock->flush();                        // 演示要「立刻发出去」，不等写缓冲被调度
        sock->disconnectFromHost();           // Connection: close，一条请求一条连接
    }

    QTcpServer *m_server = nullptr;
    QList<QTcpSocket *> m_clients;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    QHash<QString, QString> m_headers;        // 最后一次请求的头（小写键）
    quint16 m_port = 0;
    int m_requests = 0;
    int m_flakyHits = 0;
    int m_lastHeaderCount = 0;
    int m_lastContentLength = 0;
    int m_lastBodyBytes = 0;
    QString m_lastPath;
};

// ============================================================================
// 五、合账：每一项核对都打「期望 / 实测」，最后一屏给总数
// ----------------------------------------------------------------------------
// 三条规矩（和第 1、3 讲同一套，本仓库不另立口径）：
//   · 计数器只记**真的发生了**的结果，不记「我打算检查」；
//   · 退出码就是这本账：差一项、或者总闸跳过一次 → 1。用 quit() 的话进程永远返回 0，
//     这张表打印得再认真，CI 也看不见 —— 把话说到屏幕里只算说了一半。
//   · 概率性的数（elapsedMs 实际几毫秒）**不进硬账**，只打印。
//     拿调度的结果当承诺，是技术材料最常见的作假方式。
// ★ 标签一律收 QString：本文件第四节刚骂过「中文字面量走 Latin-1 通道」，
//   自己的日志再犯一遍就说不过去了。
// ============================================================================
struct Ledger
{
    inline static int checksTotal = 0;
    inline static int checksPassed = 0;
    inline static bool watchdogFired = false;
    static int exitCode()
    {
        return (watchdogFired || checksPassed != checksTotal || checksTotal == 0) ? 1 : 0;
    }
};

static void checkNum(const QString &label, qint64 got, qint64 want)
{
    ++Ledger::checksTotal;
    const bool ok = (got == want);
    if (ok)
        ++Ledger::checksPassed;
    qInfo().noquote() << QStringLiteral("      %1 %2 ｜期望 %3 实测 %4")
                             .arg(ok ? QStringLiteral("[OK ] 核对 ") : QStringLiteral("[!! ] 不符  "),
                                  label)
                             .arg(want).arg(got);
}

static void checkStr(const QString &label, const QString &got, const QString &want)
{
    ++Ledger::checksTotal;
    const bool ok = (got == want);
    if (ok)
        ++Ledger::checksPassed;
    qInfo().noquote() << QStringLiteral("      %1 %2 ｜期望 \"%3\" 实测 \"%4\"")
                             .arg(ok ? QStringLiteral("[OK ] 核对 ") : QStringLiteral("[!! ] 不符  "),
                                  label, want, got);
}

// 三类失败分开建模，所以核对也只能按类核：这一条把「期望哪一类 / 实际哪一类」并排打出来。
static void checkKind(const QString &label, const ApiResult &r, ApiResult::Kind want)
{
    ++Ledger::checksTotal;
    const bool ok = (r.kind == want);
    if (ok)
        ++Ledger::checksPassed;
    qInfo().noquote() << QStringLiteral("      %1 %2 ｜期望 %3 实测 %4（tag=%5 http=%6 netError=%7）")
                             .arg(ok ? QStringLiteral("[OK ] 核对 ") : QStringLiteral("[!! ] 不符  "),
                                  label, ApiResult::kindName(want), r.kindName())
                             .arg(r.requestTag.isEmpty() ? QStringLiteral("(空)") : r.requestTag)
                             .arg(r.httpStatus)
                             .arg(int(r.netError));
}

// ============================================================================
// 六、演示现场：结果先留在原地，收尾时按编号一次性合账
// ----------------------------------------------------------------------------
// 为什么不在回调里当场核对：回调到达的**顺序**由对端和事件循环决定，当场核对会把日志
// 串成一团，读者跟不上（本仓库的纪律是材料要让人一步步跟得上）。
// 所以每一步只做两件事——存进这张表、打一行流水；①~⑦ 的判定全留到最后，
// 屏幕顺序 = 讲解顺序。
// ★ 表里的 int 不用原子：本讲所有回调都在主线程的事件循环里跑
//   （NAM 挂主线程、reply 也属主线程）。threads 第 2 讲的归属纪律在这里同样成立。
// ============================================================================
struct Demo
{
    ApiResult device;                 // ①a GET 成功
    ApiResult created;                // ①b POST 成功（正文是 QJsonDocument 产的）
    ApiResult missing;                // ②  404：服务器答了，语义错
    ApiResult garbage;                // ③a 200 但正文不是合法 JSON
    ApiResult arrayRes;               // ③b 200、JSON 合法、但顶层是数组
    ApiResult flaky;                  // ④a 前两次 500，第三次才成
    ApiResult refused;                // ④b 连不上（端口没人听）
    ApiResult timedOut;               // ④c 对端收了连接不答，transferTimeout 到点
    ApiResult cancelled;              // ⑤  我方主动 abort
    ApiResult oldStyle;               // ⑥  只用 setRawHeader 的那条路
    ApiResult lagLive;                // ⑦ 对照组：接收者活着 → 回调应当来
    int lagDeadCallbacks = 0;         // ⑦ 实验组：回调进来几次（期望 0）
    int resultsBack = 0;              // 一共进来了几次回调
    int postContentLengthHeader = 0;  // ①b 对端看到的 Content-Length（框架算的）
    int postBodyBytesSeen = 0;        // ①b 对端实际收到的正文字节
};
static Demo g;

// 回调的统一形状：存进指定的格子 + 到齐计数 + 打一行流水。
// slot 是指向 static Demo 成员的裸指针，本进程生命周期内不会动，所以安全。
static ApiCallback sink(ApiResult *slot)
{
    return [slot](const ApiResult &r) {
        *slot = r;
        ++g.resultsBack;
        qInfo().noquote() << QStringLiteral("  [收] %1 ｜kind=%2 http=%3 netError=%4 attempts=%5 elapsed=%6ms")
                                 .arg(r.requestTag.isEmpty() ? QStringLiteral("(无 tag)") : r.requestTag,
                                      r.kindName())
                                 .arg(r.httpStatus)
                                 .arg(int(r.netError))
                                 .arg(r.attempts)
                                 .arg(r.elapsedMs);
        if (!r.message.isEmpty())
            qInfo().noquote() << QStringLiteral("       给人看的那句：\"%1\"").arg(r.message);
        if (r.kind == ApiResult::Kind::ParseError)
            qInfo().noquote() << QStringLiteral("       解析错：%1（第 %2 字节）")
                                     .arg(r.parseErrorText).arg(r.parseErrorOffset);
    };
}

static void step(const QString &text)
{
    qInfo().noquote() << QStringLiteral("\n---------- %1 ----------").arg(text);
}

// 概率性/依赖实现的结论用这一条：打印，但**不进硬账**。
// 本讲只有两处用它（④b ④c 的具体错种），原因写在那两行旁边。
static void expectKind(const QString &label, const ApiResult &r, ApiResult::Kind want)
{
    const bool hit = (r.kind == want);
    qInfo().noquote() << QStringLiteral("      %1 %2 ｜应当 %3 实测 %4")
                             .arg(hit ? QStringLiteral("[预期中] ") : QStringLiteral("[这次没命中] "),
                                  label, ApiResult::kindName(want), r.kindName());
}

// ============================================================================
// 七、main：把七节证据串起来
// ----------------------------------------------------------------------------
// 计划数（应用层自己登记，不让传输层猜 —— 和第 3 讲同一手）：
//   假服务应当收到 13 个请求：①a 1 + ①b 1 + ② 1 + ③a 1 + ③b 1
//                             + ④a 3（重试三次）+ ④b 0（端口没人听，压根到不了）
//                             + ④c 1 + ⑤ 1 + ⑥ 1 + ⑦ 2
//   回调应当进来 11 次：①a ①b ② ③a ③b ④a ④b ④c ⑤ ⑥ ⑦对照，
//                       ⑦实验组那条**不该**进来（接收者提前死了）。
//   两条合起来才把「请求发出去了」和「结果送回了调用方」分成两件事 ——
//   只核其中一条的材料，都藏得住「静默丢回调」那种 bug。
// ============================================================================
static constexpr int kServerRequestsPlan = 13;
static constexpr int kCallbacksPlan = 11;
// 第三个计划数 kDeviceId 在第四节（假服务上面）：它必须能被 echoBody() 和这里同时引用。

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("network_02_rest_client"));

    // 一个应用一个 NAM：挂 parent 保证退出时析构（threads 第 1 讲称过的账，这里同理）
    auto *nam = new QNetworkAccessManager(&app);
    nam->setTransferTimeout(std::chrono::milliseconds(5000));              // 全局兜底
    nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);     // 显式写出，别靠记

    auto *srv = new FakeRestServer(&app);
    auto *cli = new RestClient(nam, &app);
    cli->setToken(QStringLiteral("tok-42"));      // ★ 演示令牌，不是真凭证；真令牌永远不进源码
    cli->setMaxRetries(2);                        // ④a 靠它把第三次跑出来
    cli->setTransferTimeout(1500);

    // ④b 专用的第二个客户端： baseUrl 指向一个「刚关掉、没人听」的端口。
    //    ★ 换的是 baseUrl，客户端代码一行没动 —— 这一讲的主旨就是这个动作。
    auto *refuser = new RestClient(nam, &app);
    refuser->setMaxRetries(0);
    refuser->setTransferTimeout(1200);

    // ④c 专用：同一个 baseUrl，但超时给 400ms，靶子是 /v1/silent（收了不答）
    auto *slowcli = new RestClient(nam, &app);
    slowcli->setMaxRetries(0);
    slowcli->setTransferTimeout(400);

    QTimer::singleShot(0, [srv, cli, slowcli]() {
        qInfo() << "================================================================";
        qInfo() << "network 第 2 讲 · 把网络层写成可测试的一层（Qt ="
                << QString::fromLatin1(QT_VERSION_STR) << "）";
        qInfo() << "================================================================";
        if (!srv->start()) {
            qWarning() << "  假服务起不来，这一讲没有对端就跑不下去：直接收工并返回 1";
            QCoreApplication::exit(1);
            return;
        }
        const QUrl base(QStringLiteral("http://127.0.0.1:%1").arg(srv->port()));
        cli->setBaseUrl(base);
        slowcli->setBaseUrl(base);
        qInfo().noquote() << QStringLiteral("  对端就位：%1（端口是 listen(… , 0) 现分配的，**没有硬编码**）")
                                 .arg(base.toString());
        qInfo().noquote() << QStringLiteral("  同一个 RestClient 类，四个实例三种 baseUrl/超时/重试配置 —— "
                                  "可注入就是这个意思：换配置，不换代码。");
        // 注意这里没写 nam：QNetworkAccessManager 在主线程活着，客户端各自持有它的指针，
        // 这一发「把 baseUrl 注入进去」根本不需要碰它。**没用的别抓进来。**

        step(QStringLiteral("①a 成功的样子：GET /v1/device?detail=full"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("detail"), QStringLiteral("full"));
        cli->get(QStringLiteral("/v1/device"), q, sink(&g.device), QStringLiteral("①a device"));
    });

    // ── ①b POST：正文由 QJsonDocument 产 UTF-8 字节，Content-Length 交给框架 ──
    QTimer::singleShot(250, [cli, srv]() {
        step(QStringLiteral("①b POST /v1/device：请求体是 JSON，Content-Type 由客户端统一加"));
        QJsonObject body;
        body.insert(QStringLiteral("name"), QStringLiteral("新设备-回环"));
        body.insert(QStringLiteral("qty"), 3);
        // 这一条要顺带记两个**对端侧**的数，所以不能用通用 sink：
        // 「框架算的 Content-Length」和「对端真收到的字节数」相等，才叫这条链路对齐了。
        cli->postJson(QStringLiteral("/v1/device"), body, [srv](const ApiResult &r) {
            g.created = r;
            ++g.resultsBack;
            g.postContentLengthHeader = srv->lastContentLength();
            g.postBodyBytesSeen = srv->lastBodyBytes();
            qInfo().noquote() << QStringLiteral("  [收] %1 ｜kind=%2 http=%3 ｜对端收到正文 %4/%5 字节")
                                     .arg(r.requestTag, r.kindName())
                                     .arg(r.httpStatus)
                                     .arg(g.postBodyBytesSeen)
                                     .arg(g.postContentLengthHeader);
        }, QStringLiteral("①b POST"));
    });

    // ── ② 404：服务器活着且答了你，是**业务**语义错 ────────────────────────
    QTimer::singleShot(450, [cli]() {
        step(QStringLiteral("② HttpError：GET /v1/missing → 404（服务器答了，但答的是「没有」）"));
        cli->get(QStringLiteral("/v1/missing"), QUrlQuery(), sink(&g.missing),
                 QStringLiteral("② missing"));
    });

    // ── ③ 解析错：200 也可能是坏数据，两类都要有名字 ───────────────────────
    QTimer::singleShot(650, [cli]() {
        step(QStringLiteral("③a ParseError：GET /v1/garbage → 200 + 半截 JSON"));
        cli->get(QStringLiteral("/v1/garbage"), QUrlQuery(), sink(&g.garbage),
                 QStringLiteral("③a garbage"));
    });
    QTimer::singleShot(850, [cli]() {
        step(QStringLiteral("③b ParseError：GET /v1/array → 200 + 合法 JSON **数组**（契约要对象）"));
        cli->get(QStringLiteral("/v1/array"), QUrlQuery(), sink(&g.arrayRes),
                 QStringLiteral("③b array"));
    });

    // ── ④ 网络错三种：连不上 / 超时 / 重试到第三次 ─────────────────────────
    QTimer::singleShot(1050, [cli]() {
        step(QStringLiteral("④a 重试与幂等：GET /v1/flaky（对端前两次必 500）→ attempts 应当是 3"));
        cli->get(QStringLiteral("/v1/flaky"), QUrlQuery(), sink(&g.flaky),
                 QStringLiteral("④a flaky"));
    });
    QTimer::singleShot(1300, [srv, refuser]() {
        step(QStringLiteral("④b NetworkError：baseUrl 注入到一个刚关掉的端口（连不上）"));
        const quint16 dead = FakeRestServer::borrowThenRelease();
        qInfo().noquote() << QStringLiteral("      假服务的端口=%1，这次指过去=%2（后者 listen 完立刻 close）")
                                 .arg(srv->port()).arg(dead);
        refuser->setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:%1").arg(dead)));
        refuser->get(QStringLiteral("/v1/device"), QUrlQuery(), sink(&g.refused),
                     QStringLiteral("④b refused"));
    });
    QTimer::singleShot(1450, [slowcli]() {
        step(QStringLiteral("④c 超时：GET /v1/silent（收了连接一个字不答），本客户端 timeout=400ms"));
        slowcli->get(QStringLiteral("/v1/silent"), QUrlQuery(), sink(&g.timedOut),
                     QStringLiteral("④c silent"));
    });

    // ── ⑤ 主动取消：和「对端挂了」必须分开，因为日志级别不同 ────────────────
    QTimer::singleShot(1900, [cli]() {
        step(QStringLiteral("⑤ Cancelled：GET /v1/lag（要 350ms 才答），120ms 时我方 abort"));
        cli->get(QStringLiteral("/v1/lag"), QUrlQuery(), sink(&g.cancelled),
                 QStringLiteral("⑤ cancel"));
        QTimer::singleShot(120, cli, [cli]() { cli->cancelPending(); });
    });

    // ── ⑥ 另一条设头路子：只用 setRawHeader，对端应当看到同样的字段 ─────────
    QTimer::singleShot(2450, [cli]() {
        step(QStringLiteral("⑥ 两条设头路子等价：这次走 getOldStyle()（只有 setRawHeader/setHeader）"));
        cli->getOldStyle(QStringLiteral("/v1/device"), sink(&g.oldStyle),
                         QStringLiteral("⑥ oldStyle"));
    });

    // ── ⑦ context object 死了：回调不发，但请求照跑（价格也要打出来）────────
    QTimer::singleShot(2700, [cli]() {
        step(QStringLiteral("⑦ 界面对象提前死掉：同一发请求，一个挂活对象、一个挂马上死的对象"));
        auto *liveUi = new QObject;
        liveUi->setObjectName(QStringLiteral("界面-活着"));
        auto *doomedUi = new QObject;
        doomedUi->setObjectName(QStringLiteral("界面-要死"));
        cli->get(QStringLiteral("/v1/lag"), QUrlQuery(), sink(&g.lagLive),
                 QStringLiteral("⑦ 对照（接收者活着）"), liveUi);
        cli->get(QStringLiteral("/v1/lag"), QUrlQuery(),
                 [](const ApiResult &r) {
                     ++g.lagDeadCallbacks;            // 按本讲的设计：这一行**不该**被执行
                     // ★ 这里**故意不把 r 存进表**：ApiResult 的 kind 默认就是 Success，
                     //   多开一个格子反而分不清「没人写过」和「写进来一个成功」。
                     //   这一条要的证据只有「几次回调」，那就只留那个数 ——
                     //   留一格子进去却不敢读它，是讲义代码里最常见的假严谨。
                     Q_UNUSED(r)
                 },
                 QStringLiteral("⑦ 实验（接收者要死）"), doomedUi);
        // ★ 这颗「执行 delete 的定时器」故意**不带** context，理由值得单独记一条：
        //   带 context 那一族（qtimer.h 第 66~70 行）内部造的一次性定时器会被挂到 context 名下
        //   （Qt 源码里就是 setParent(receiver)），于是「在自己的回调里 delete 这个 context」
        //   = **在信号发射途中删掉发送者**，是 Qt 点名的危险动作。
        //   不带 context 那一族（第 85~88 行）把 receiver 写成 nullptr，定时器不归属任何人，
        //   删 doomedUi 就是一次干净的外部删除。
        //   两条只差一个参数，命运完全不同 —— 这就是为什么本仓库要求你**去读那几行头文件**，
        //   而不是记住「singleShot 会替你看住生命周期」。
        QTimer::singleShot(130, [guard = QPointer<QObject>(doomedUi)]() {
            if (!guard)
                return;                              // 已经被别人删掉了，绝不二次删除
            qInfo().noquote() << QStringLiteral("      现在把「%1」delete 掉：它一死，"
                                      "挂在它身上的回调就永久失联了")
                                     .arg(guard->objectName());
            delete guard.data();                     // 真删：不是 deleteLater，本讲要当场生效
        });
        // liveUi 留到收尾再删（它还要收那发 lag 的回复，350ms 之后才到）
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, liveUi, [liveUi]() { delete liveUi; });
    });

    // ── 合账 + 收摊 ─────────────────────────────────────────────────────────
    // 捕获表只写这一节真用到的：srv（对端那本账）+ cli（⑦ 要读它的 pendingCount）。
    // refuser / slowcli 不进来 —— 它们的结果早在④b/④c 就落进 g 里了。
    // 当初这里挂过一个 `slowcppUnused = 0` 加两个 Q_UNUSED，那是为了让编译器闭嘴，
    // 不是为了让代码对：读者会照着抄，抄到的全是噪音。**要用的抓进来，不用的别抓。**
    QTimer::singleShot(3400, [srv, cli]() {
        step(QStringLiteral("合账（①~⑦ 逐条核，最后一行给总数）"));
        const QJsonObject echoDev = g.device.data.value(QStringLiteral("echo")).toObject();
        const QJsonObject echoOld = g.oldStyle.data.value(QStringLiteral("echo")).toObject();

        checkKind(QStringLiteral("①a GET 成功这一类"), g.device, ApiResult::Kind::Success);
        checkNum(QStringLiteral("①a http 状态码"), g.device.httpStatus, 200);
        checkNum(QStringLiteral("①a 解析出的 data[\"id\"]"),
                 g.device.data.value(QStringLiteral("id")).toInt(), kDeviceId);
        checkStr(QStringLiteral("①a 对端收到的 Authorization"),
                 echoDev.value(QStringLiteral("authorization")).toString(),
                 QStringLiteral("Bearer tok-42"));
        checkStr(QStringLiteral("①a 对端收到的 Accept"),
                 echoDev.value(QStringLiteral("accept")).toString(),
                 QStringLiteral("application/json"));
        checkStr(QStringLiteral("①a 对端收到的 X-Client（QHttpHeaders 那条路）"),
                 echoDev.value(QStringLiteral("x_client")).toString(),
                 QStringLiteral("qt-teach/02"));

        checkKind(QStringLiteral("①b POST 成功这一类"), g.created, ApiResult::Kind::Success);
        checkStr(QStringLiteral("①b 对端收到的 Content-Type（客户端统一加的那串）"),
                 g.created.data.value(QStringLiteral("echo")).toObject()
                       .value(QStringLiteral("content_type")).toString(),
                 QStringLiteral("application/json; charset=utf-8"));
        checkNum(QStringLiteral("①b 框架算的 Content-Length == 对端实际收到的字节数"),
                 g.postBodyBytesSeen, g.postContentLengthHeader);

        checkKind(QStringLiteral("② 404 归 HttpError（不是网络错）"), g.missing,
                  ApiResult::Kind::HttpError);
        checkNum(QStringLiteral("② http 状态码"), g.missing.httpStatus, 404);
        checkStr(QStringLiteral("② 4xx 的正文**别丢**：服务端给的原因还在"),
                 g.missing.data.value(QStringLiteral("error")).toString(),
                 QStringLiteral("no_such_device"));

        checkKind(QStringLiteral("③a 半截 JSON 归 ParseError"), g.garbage,
                  ApiResult::Kind::ParseError);
        checkNum(QStringLiteral("③a 状态码是 200（所以「HTTP 成功」不等于「数据能用」）"),
                 g.garbage.httpStatus, 200);

        checkKind(QStringLiteral("③b 合法 JSON 但不是对象，也归 ParseError"), g.arrayRes,
                  ApiResult::Kind::ParseError);

        checkKind(QStringLiteral("④a 重试到第三次应当成功"), g.flaky, ApiResult::Kind::Success);
        checkNum(QStringLiteral("④a attempts（含第一次，共 3 次）"), g.flaky.attempts, 3);

        // ④b/④c 的**具体错种**不进硬账，只打印：
        //   · ④b 借来的端口理论上可能已被别人抢走 → 到不了「连不上」那一类；
        //   · ④c 超时映射到哪个 NetworkError 枚举值随 Qt 小版本/HTTP2 栈变化。
        //   所以这两条我只把「**不是成功**」当硬结论 —— 分层建模的界面本来就只看类别。
        checkNum(QStringLiteral("④b 连不上：绝不能被当成成功"), g.refused.isOk() ? 1 : 0, 0);
        expectKind(QStringLiteral("④b 期望归 NetworkError"), g.refused, ApiResult::Kind::NetworkError);
        checkNum(QStringLiteral("④c 超时：绝不能被当成成功"), g.timedOut.isOk() ? 1 : 0, 0);
        expectKind(QStringLiteral("④c 期望归 NetworkError"), g.timedOut, ApiResult::Kind::NetworkError);
        checkNum(QStringLiteral("④c 没拿到状态码（压根没答复）"), g.timedOut.httpStatus, 0);

        checkKind(QStringLiteral("⑤ 我方 abort 归 Cancelled（和第④类分开）"), g.cancelled,
                  ApiResult::Kind::Cancelled);

        checkKind(QStringLiteral("⑥ 老写法（只 setRawHeader）也成功"), g.oldStyle,
                  ApiResult::Kind::Success);
        checkStr(QStringLiteral("⑥ 对端看到的 Authorization 和①a 一模一样"),
                 echoOld.value(QStringLiteral("authorization")).toString(),
                 QStringLiteral("Bearer tok-42"));
        checkStr(QStringLiteral("⑥ 对端看到的 X-Client 是老写法那个（证明这发走的另一条路）"),
                 echoOld.value(QStringLiteral("x_client")).toString(),
                 QStringLiteral("qt-teach/02-oldstyle"));

        // ⑦ 的三条合起来才是完整结论：**没回调 + 请求真发出去了 + 客户端账上还挂着**。
        //    少任何一条，「0」都可能只是「什么都没发生」。
        checkNum(QStringLiteral("⑦ 接收者死了：回调进来几次（期望 0）"), g.lagDeadCallbacks, 0);
        checkKind(QStringLiteral("⑦ 对照组：接收者活着的那发应当正常回来"), g.lagLive,
                  ApiResult::Kind::Success);
        checkNum(QStringLiteral("⑦ 代价：客户端 m_pending 里还挂着那条没人收的 reply"),
                 cli->pendingCount(), 1);

        // 两本总账：对端收到几个请求 / 调用方收到几个结果
        checkNum(QStringLiteral("对端收到的请求数（应用层自己登记的计划）"),
                 srv->requestsSeen(), kServerRequestsPlan);
        checkNum(QStringLiteral("回调进来次数（计划）"), g.resultsBack, kCallbacksPlan);

        qInfo().noquote() << QStringLiteral("      [实测] ①a 用时 %1ms、④c 用时 %2ms"
                                          "（毫秒数不进硬账，只给你个量感）")
                                 .arg(g.device.elapsedMs).arg(g.timedOut.elapsedMs);

        qInfo().noquote() << QStringLiteral("\n合账结果：%1/%2 项对上 ｜ 退出码 %3")
                                 .arg(Ledger::checksPassed).arg(Ledger::checksTotal)
                                 .arg(Ledger::exitCode());
        srv->stop();                       // 先关对端，再收摊：顺序反了会有迟到的半包
        QCoreApplication::exit(Ledger::exitCode());
    });

    // 总闸：15 秒。比最坏预算（3.4 秒收尾 + 一点调度余量）明显大，
    // 所以「超时」这条路正常路径永远走不到；走到了就是真卡住了。
    QTimer::singleShot(15000, [srv]() {
        Ledger::watchdogFired = true;
        qWarning().noquote() << QStringLiteral("【总闸】15 秒到点：强制收尾（网络卡住绝不让进程挂死）");
        qInfo().noquote() << QStringLiteral("      到点时已到 %1/%2 项核对，对端收到 %3 个请求")
                                 .arg(Ledger::checksPassed).arg(Ledger::checksTotal)
                                 .arg(srv->requestsSeen());
        srv->stop();
        QCoreApplication::exit(Ledger::exitCode());
    });

    // ★ 没有 waitFor*、没有模态对话框、没有 sleep：推进全靠信号 + singleShot 串时间轴。
    //   exec() 是必须开的（不开事件循环，reply::finished 一个都不会到），
    //   而返回值就是这本账 —— 见第五节开头那条规则。
    return app.exec();
}

// ============================================================================
// 八、速查（背不住就回来查）
// ============================================================================
/*
 * 三类失败 = 三个名字，别塞进一个 bool：
 *   Success / HttpError（4xx 5xx：对端活着且答了你）/
 *   NetworkError（连不上、超时、SSL、代理：压根没答复）/ ParseError（200 但数据不能用）
 *   判成功的唯一正道：reply->error() == NoError **且** 状态码在 2xx **且** 正文解析得过。
 *   三个条件各管一段，缺一个就会出现「界面显示成功、列表是空的」这种最难查的 bug。
 *
 * 设请求头（本机 6.12 头文件为准）：
 *   QNetworkRequest::KnownHeaders（qnetworkrequest.h 33~48）**只有十三项**，
 *                 没有 Accept、没有 Authorization → 网上教程的 AcceptHeader 是不存在的符号
 *   老写法：req.setHeader(QNetworkRequest::ContentTypeHeader, ...)（139）
 *           req.setRawHeader("Authorization", "Bearer …")（151）
 *   新写法：QHttpHeaders hs; hs.replaceOrAppend(QHttpHeaders::WellKnownHeader::Accept, ...);
 *           req.setHeaders(hs)（134/135）
 *                 ← QHttpHeaders 的写接口叫 append/insert/replace/replaceOrAppend
 *                   （qhttpheaders.h 306/309/312/315，都返回 bool），**没有 setHeader**
 *   ★ setHeaders 是**整批换**：先 setRawHeader 再 setHeaders 会把你前面那几条冲掉。
 *     一个请求只挑一条路；本讲 buildRequest() 走 QHttpHeaders，getOldStyle() 整条走 raw，
 *     两边各发一发，让对端把看到的头打回来 —— 别拿「我以为会合并」当假设。
 *
 * 超时（三层，别只记一层）：
 *   全局兜底：nam->setTransferTimeout(chrono)（qnetworkaccessmanager.h 131/132，旧 int 版 128）
 *   这一发的：req.setTransferTimeout(chrono)（qnetworkrequest.h 208），默认
 *                 DefaultTransferTimeout = 30000ms（111/114）—— 「界面卡半分钟」就是它
 *   进程总闸：main 里一颗 QTimer，到点 exit(1)。前两层是「这一发别太久」，
 *                 这一层是「无论如何别让进程陪我一起挂死」。
 *
 * 重试：
 *   只给幂等方法（GET/PUT/DELETE）；POST 重试 = 可能重复下单，要重试就带 Idempotency-Key
 *   计数含第一次：setMaxRetries(2) 最多跑 3 发，所以合账核的是 attempts == 3 而不是 2
 *   重试那颗 QTimer **也要带 context**：只给第一发挂 context，第二发照样回调给一个鬼
 *
 * 生命周期（本讲最贵的一条）：
 *   connect(reply, &QNetworkReply::finished, context, lambda) —— qobject.h 第 228 行那个重载，
 *                 context 析构 → 连接自动断 → lambda 永不执行
 *   它的代价：你的 dropPending() / deleteLater() 也在 lambda 里，于是 reply 挂在
 *                 客户端的 pending 表上不放。所以 ~RestClient 必须自己 abort() 一遍。
 *                 「回调没了」不等于「请求没了」—— 这一句是这讲的落点。
 *   QTimer::singleShot(interval, context, functor)（qtimer.h 66~70 行，头文件里第 57 行
 *                 就有「singleShot with context」这句注释）第 2 个参数是 context；
 *   QTimer::singleShot(interval, functor)（85~88 行，第 87 行内部把 receiver 写成 nullptr）
 *                 到点必发 —— 要「到点必发」就用后者，要「对象死了这事就作废」就用前者。
 *   ★ 别用前者去 delete 后者绑的那个对象：带 context 那一族把内部定时器挂在 context 名下，
 *     在自己的回调里删 context 就是「发射途中删发送者」。⑦ 那段就是这么绕开的。
 *
 * 可测试性：
 *   baseUrl 可注入 = 同一个类换对端。演示指 127.0.0.1:现分配端口，生产指真服务，
 *                 客户端代码一行不改；Qt 6.4+ 的现成件是 QNetworkRequestFactory
 *                 （baseUrl()/setBaseUrl，43/44；createRequest，51~54）
 *   对端要能对账：本讲假服务自己数 requestsSeen()，和调用方的回调次数分两本账 ——
 *                 「请求发出去了」和「结果送回了调用方」是两件事，只核一条就藏得住
 *                 「静默丢回调」那种 bug（本讲初稿就丢了，表现是界面一直转圈而不报错）
 */

// 本文件有两个 Q_OBJECT 类（RestClient / FakeRestServer），AUTOMOC 生成 .moc 并在这里包含。
#include "02_rest_client.moc"

// ——— 老师傅的话 ———
/*
 * 徒弟，网络这一层我见过太多「包了一层，结果只是把 QNetworkReply 换了个名字」。
 * 判断你有没有真的包对，我只看三个地方。
 *
 * 第一，失败有没有**分开**。你把 404、连不上、200 但 JSON 坏了全写成
 * `bool ok = false`，那用户就会在「服务器说这个设备不存在」的时候看到「网络异常，请检查
 * 您的网络连接」—— 网络好得很，是他自己的输入错了。这三类的**日志级别**都不一样：
 * 4xx 是 debug（用户的手误），5xx 是 warning（对面出事），超时是 warning（可能重试就好），
 * 解析错是 **error**（对端契约变了，这是要打电话的事）。你混成一个 bool，
 * 值班的人半夜被叫起来，看一眼屏幕发现自己什么也判断不了。
 * 所以 ApiResult 那个 Kind 不是审美，是运维。
 *
 * 第二，重试有没有**边界**。看到 `for (i=0;i<3;i++) { tryAgain(); }` 我要先问一句：
 * 这个方法幂等吗？POST 一个下单请求你重试三次，用户就付了三次钱 —— 这不是 bug，是事故。
 * 本讲把 /v1/flaky 写成「前两次必 500」，是为了让你能**证明**重试这条路是通的：
 * attempts == 3 是能进硬账的数，而不是「我看好它」。反过来，⑤ 那一发是我方 abort，
 * 它绝不能被算进「重试成功」里 —— 用户主动取消和对面没响应，在统计上是两件事。
 *
 * 第三，也是最容易漏的：**回调没了，请求还在不在**。
 * 你在 metaobject 第 4 学「connect 挂 context，对象死了回调就不发」，这是防崩的正手。
 * 但这一讲我要你把后半句也记住：那一刻 reply 的 finished 里那段清理代码同样不执行了，
 * dropPending 没跑、deleteLater 没跑，你客户端那张 pending 表就一直挂着这条连接。
 * 一个界面一天开关两百次，就是两百条没人收尸的 socket。
 * ⑦ 那三条核对（回调 0 次 + 请求真发出去了 + pendingCount 还是 1）合起来才是结论：
 * 单独打印一个「回调 0 次」是最坏的作假 —— 它可能只是什么都没发生。
 * 所以我要求你在析构里主动 abort()，并且把这条**代价**写在注释里。
 * 老师傅不是不犯错，是把代价写在自己看得见的地方。
 *
 * 最后一句关于「测」的。这一讲跑的是本机回环上一个我自己写的假服务。
 * 有人嫌这样「不算真测」，非要连公网那个 api.example 才安心 —— 那是拿别人的可用性
 * 当自己的测试环境：它对端一改版你的 CI 就红，你还没法让它专门吐一次「200 + 半截 JSON」。
 * 而我这个假服务能让 /v1/flaky 准不准地坏两次。
 * **能精确制造故障的对端，才叫测试对端**；剩下的叫运气。
 * 生产环境把 baseUrl 换成真的，代码一行不动 —— 这就是「可注入」这三个字的全部意义。
 */

// ——— 小练习 ———
/*
 * 1. 把 RestClient 的 Kind 加一档 `AuthError`（401/403）。要求：② 那套合账里
 *    多一项「401 归 AuthError 且**不重试**」；给假服务加一条 /v1/denied 路由。
 *    想清楚为什么 401 不能重试：不是「会失败」，是**重试一万次也是失败**，
 *    而 token 过期这件事只有界面能告诉用户去登录。
 * 2. 现在 buildRequest() 的 Accept/Authorization 都进了同一个 QHttpHeaders。
 *    故意破坏一次：在 setHeaders(hs) 之后再补一句 setRawHeader("X-Late", "1")，
 *    把对端回显的头打全，回答：哪几条活着、哪几条死了？由此说明
 *    「setHeaders 是整批换」这句话你在代码里是怎么**验**的，不是怎么**猜**的。
 * 3. 给 POST 加 Idempotency-Key：客户端用 QUuid::createUuid()（或时间戳+序号）
 *    生成、重试时**复用同一个 key**（这是重点：每次重试点一个新 key 等于没做）。
 *    合账加一项「三次重试里对端看到的 key 完全相同」。
 * 4. ~RestClient 已经替你在析构里 abort() 了一遍（第三节那段循环）。现在做两个实验：
 *    ① 把那个循环注释掉，只留 m_pending.clear()，观察日志里有没有出现
 *      「reply 一直没被 delete」的痕迹（给 QNetworkReply 挂 destroyed 信号来数），
 *      并解释为什么进程照样正常退出、看不出问题 —— 这就是这类泄漏在生产上最难被发现的原因。
 *    ② 给 cli->deleteLater() 的时机改成「⑦ 那发 lag 请求还在飞的时候」，
 *      打印 abort 掉的条数，回答：为什么必须在**析构**里做而不是在某个槽里做？
 *      （提示：能保证「无论如何都执行一次」的位置只有一个。）
 * 5. 用 QNetworkRequestFactory 重写 baseUrl/createRequest 那一半（头文件第 43~54 行），
 *    保留 ApiResult 和合账不动。做完对比一下：你少写了哪三十行，
 *    「可注入」这件事少没有？（答案应当是：少了样板，一点没少。）
 * 6. 超时那条现在只核「绝不能算成功」。你要不要把「它是超时而不是连不上」也变成硬账？
 *    想清楚再动：这需要你能**稳定制造**那一类，而不是靠这台机器的调度。
 *    写完把 /v1/silent 的挂起时长和 setTransferTimeout 的关系画成一张图，
 *    标出「改哪个数会让结论从硬变软」。
 * 7. 资深题：把这一讲的 RestClient 拆成两层——纯语义层（ApiResult + 分类规则，
 *    只吃 QByteArray + int，不发任何请求）和传输层（NAM 调用）。给纯语义层写
 *    quality 第 1 讲那样的 QTest：不需要 socket、不需要事件循环、把状态码和正文
 *    当参数喂进去，断言 Kind 判对。做完你就会明白「可测试」的真正含义是
 *    **把要测的东西从要依赖的东西里摘出来**，baseUrl 注入只是它的一半。
 */
// ============================================================================

