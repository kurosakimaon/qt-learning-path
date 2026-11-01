// ============================================================================
// 第 1 讲 · QNetworkAccessManager 的正确姿势
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   网络是桌面程序里最容易写歪的一层，因为它**天生异步**，而新手总想把它拧成同步。
//   这一讲把 QtNetwork 的「发请求」这半套讲透：
//     1) **一个应用一个 QNetworkAccessManager**：它内部带连接池、代理、Cookie、缓存、
//        HSTS 状态。你每发一个请求 new 一个 NAM，等于每次重新握手、连接池全废、
//        Cookie 也存不住。它必须**创建并活在有事件循环的线程**（GUI 线程就是主线程，
//        app.exec() 那个循环）上；它不自己起线程，一切靠事件循环驱动。
//     2) QNetworkRequest 的构造与 **QUrl 的三个坑**：手写的字符串不是 URL
//        （"C:/data/a.json" 会被当成 scheme 是 "c"）；本地文件必须 QUrl::fromLocalFile；
//        query 参数必须 QUrlQuery 拼，别手撸 "&" 与转义（中文、空格、"&" 一进去就出事）。
//     3) 六个动词各自的用法与时机：head / get / post / put / deleteResource /
//        sendCustomRequest（**Qt6 没有名为 delete 的成员**——delete 是 C++ 关键字，
//        Qt6 头文件里它就是 deleteResource，第 100 行）。
//     4) QNetworkReply 是 **QObject**（QNetworkReply: public QIODevice，第 29 行），
//        所以它必须 deleteLater；finished() 是「这件事结束了」，readyRead() 是
//        「又来了一段正文，你现在能读」——两者的分工决定了流式下载和一次性读的写法差别。
//        error() / errorString() / attribute() **只在 finished 之后读才完整**（见证据②）。
//     5) 重定向：Qt6 的默认策略是 NoLessSafeRedirectPolicy；枚举里**已经没有**
//        OldAtLeastHttp11Redirect 之类，也**没有** Qt5 的 RedirectionType——
//        redirected 信号在 Qt6 只带一个 QUrl 参数（第 141 行）。
//     6) 超时：Qt6 请写 setTransferTimeout(std::chrono::milliseconds(...))，
//        默认常量 QNetworkRequest::DefaultTransferTimeoutConstant = 30000（第 110/114 行）。
//        老代码的 setTransferTimeout(int) 还在（第 128 行的 inline 壳），但新代码走 duration。
//     7) 为什么「同步等网络」几乎总是错的：证据⑤用计数器把「等一个请求的时间里
//        事件循环转了多少轮」打出来，你自然会明白阻塞在这里等于把整条心跳掐了。
//
// 对应构建目标名：network_01_network_access_manager
//   find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
//   add_executable(network_01_network_access_manager 01_network_access_manager.cpp)
//   target_link_libraries(network_01_network_access_manager PRIVATE Qt6::Widgets Qt6::Network)
//   set(CMAKE_AUTOMOC ON)                       # 本文件**有 Q_OBJECT 类**（自建服务器），需要 moc
//   # 手工核对时：moc 01_network_access_manager.cpp -o 01_network_access_manager.moc
//   #             文件末尾那句 #include "01_network_access_manager.moc" 就是给 AUTOMOC 的钩子
//
// 运行前须知：
//   · **绝不访问公网**：本讲自己在 127.0.0.1 上用 QTcpServer 现搭一个极简 HTTP/1.1
//     服务器，NAM 只向它自己发请求。断网、防火墙全拦、没 DNS 的机器上都照样能跑完，
//     也绝不会把你的流量打到任何第三方。
//   · **端口动态选**：代码里写的是 listen(QHostAddress::LocalHost, 0)，端口由系统给，
//     拿完立刻用 server->serverPort() 回填到 URL 里。硬编码 8080 的示例在 CI 上必挂。
//   · 有超时兜底：main 里挂一个 12 秒的看门狗 QTimer，到点强制打印「超时」证据并 quit，
//     网络卡住不会让进程挂死；正常路径走完也会 quit。
//   · 无人值守：起服务 → GET → 动词矩阵 → 重定向 → 卡住的请求 → 收尾，全程无鼠标键盘，
//     **没有模态对话框、没有 exec()、没有 waitForReadyRead**。收尾时打一张「硬账」：
//     每一条核对（状态码 / operation() / 跳转目标 / timeout 错误码 / finished 次数）都对上
//     才 exit(0)，有一项对不上 exit(1)；12 秒看门狗一旦触发也直接判红，故障藏不住。
//   · 收尾顺序：先关服务器（close），reply 早就各自 deleteLater 了，再 quit。
//   · 可以无头跑：`network_01_network_access_manager -platform offscreen`。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 network_01_network_access_manager）。本机这一轮：【硬账】67/67 项核对通过。
//     这只证明「编得过、跑得起来、上面这行为绿」——换 Qt 版本 / 换平台 / 换 CPU 核数
//     之后，正文里带具体数字的「预期」请以你自己那次运行输出为准
//     （重跑：bash _tools/run_all.sh）。
// ============================================================================

#include <QApplication>                 // QtWidgets/qapplication.h
#include <QCoreApplication>             // QtCore/qcoreapplication.h（quit / processEvents）
#include <QObject>                      // QtCore/qobject.h（本讲自由函数里要用 QObject::connect）
#include <QTimer>                       // QtCore/qtimer.h
#include <QElapsedTimer>                // QtCore/qelapsedtimer.h
#include <QDebug>                       // QtCore/qdebug.h（qInfo）
#include <QtGlobal>

#include <QNetworkAccessManager>        // QtNetwork/qnetworkaccessmanager.h
#include <QNetworkRequest>              // QtNetwork/qnetworkrequest.h
#include <QNetworkReply>                // QtNetwork/qnetworkreply.h
#include <QTcpServer>                   // QtNetwork/qtcpserver.h
#include <QTcpSocket>                   // QtNetwork/qtcpsocket.h
#include <QHostAddress>                 // QtNetwork/qhostaddress.h（注意：文件名是 qhostaddress.h）

#include <QUrl>                         // QtCore/qurl.h
#include <QUrlQuery>                    // QtCore/qurlquery.h
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QJsonObject>                  // QtCore/qjsonobject.h
#include <QJsonDocument>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QSharedPointer>               // QtCore/qsharedpointer.h（证据② 累积正文缓冲用）

#include <chrono>                       // std::chrono::milliseconds（setTransferTimeout 用）

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
// 一、对着本机头文件核过的签名（D:\Qt\6.12.0\mingw_64\include，Qt 6.12.0）
// ============================================================================
// QtNetwork/qnetworkaccessmanager.h
//   第 42~51 行 enum Operation { HeadOperation = 1, GetOperation, PutOperation,
//                                PostOperation, DeleteOperation, CustomOperation,
//                                UnknownOperation = 0 };
//                    ← 注意它是**从 1 开始**的，UnknownOperation = 0；别拿它当位掩码用
//   第 53 行  explicit QNetworkAccessManager(QObject *parent = nullptr);
//   第 82~85 行 head(get) 重载：get 有两个吃 data 的重载（QIODevice* / const QByteArray&），
//                    ← Qt6 的 get(request, data) 是「带请求体的 GET」，冷门但合法；
//                      99% 的场景你只用第 83 行那个单参数 get
//   第 86~91 行 post(request, QIODevice*) / post(request, const QByteArray&) / post(request, nullptr)
//   第 93~98 行 put(同上三种)
//   第 100 行 deleteResource(const QNetworkRequest &)     ← Qt5 老代码写 deleteLater 那种
//                    「manager->deleteResource()」名字从 Qt5.15 起就是它，**没有 delete()**
//   第 101~102 行 sendCustomRequest(request, const QByteArray &verb, [QIODevice*|const QByteArray&])
//                    ← 第三个参数在 QIODevice* 版本里有默认值 nullptr；传 QByteArray 那个没有默认，
//                      所以 sendCustomRequest(req, "PATCH") 单写两参数**只能命中 QIODevice\* 版**，
//                      编译器会替你选，不会歧义（nullptr_t 与 QByteArray 不能从缺省互推）
//   第 105~107 行 post/put/sendCustomRequest(request, QHttpMultiPart*) —— 上传文件走这条路
//   第 119 行 setRedirectPolicy(QNetworkRequest::RedirectPolicy)
//   第 122 行 autoDeleteReplies() / 第 123 行 setAutoDeleteReplies(bool)
//                    ← 设成 true 时 reply 在 finished 后自己走删除队列，**但你仍不能在这之后碰它**；
//                      本讲用默认的 false + 自己 deleteLater（自己掌控更不容易出错）
//   第 128 行 setTransferTimeout(int)（inline 壳）/ 第 131 行 setTransferTimeout(std::chrono::milliseconds
//                    = QNetworkRequest::DefaultTransferTimeout)
//
// QtNetwork/qnetworkreply.h
//   第 29 行 class Q_NETWORK_EXPORT QNetworkReply: public QIODevice   ← 它是 QObject + QIODevice
//   第 33~78 行 enum NetworkError { NoError = 0, ConnectionRefusedError = 1, ... TimeoutError = 4,
//                    OperationCanceledError, ..., TooManyRedirectsError, InsecureRedirectError,
//                    ContentNotFoundError = 203, ... InternalServerError = 401 }
//              ★ 勘误：旧稿写「36~73」。本机 6.12.0：enum NetworkError 从 **33** 行开，
//                `};` 在 **78** 行。旧稿的 36 落在 35 行那句分组注释上
//                （`// network layer errors [relating to the destination server] (1-99):` 是 36），
//                73 落在 `// Server side errors (401-499)` 那行注释上——两个数都指到了注释，
//                不是指到枚举本身。这一段里每个值都逐个核过：NoError=0(34)、
//                ConnectionRefusedError=1(37)、TimeoutError 是隐式 4(40)、
//                ContentNotFoundError 隐式 203(61)、InternalServerError=401(74)、
//                UnknownServerError=499(77)。隐式接续的那几个只能按行号数出来，
//                头文件里不写数字，别凭印象报「XXError = 几」。
//   第 94 行 NetworkError error() const;   ← Qt6 里读错误码用它；老代码 connect(nam,&...error) 早废了
//   第 95/96 行 isFinished() / isRunning()
//   第 104~114 行 rawHeader / rawHeaderList / rawHeaderPairs()；RawHeaderPair = std::pair<QByteArray,QByteArray>
//   第 118 行 QVariant attribute(QNetworkRequest::Attribute) const;   ← 状态码从这儿读
//   第 127 行 virtual void abort() = 0;（public Q_SLOTS）
//   第 134 行 void finished();       第 135 行 void errorOccurred(QNetworkReply::NetworkError);
//   第 141 行 void redirected(const QUrl &url);   ← **Qt6 只有一个参数**，Qt5 的第二参数
//                    QNetworkRequest::RedirectionType 已从枚举里删掉（本机 grep 无 RedirectionType）
//   第 144/145 行 uploadProgress / downloadProgress(qint64, qint64)
//   readyRead() 与 bytesWritten(qint64) 是**基类 QIODevice 的**信号（QtCore/qiodevice.h），
//                    不是 QNetworkReply 新加的——写文档时别记错归属。
//
// QtNetwork/qnetworkrequest.h
//   第 33~47 行 enum KnownHeaders { ContentTypeHeader, ContentLengthHeader, LocationHeader,
//                    ... UserAgentHeader, ServerHeader, ... };
//   第 51~83 行 enum Attribute { HttpStatusCodeAttribute, HttpReasonPhraseAttribute,
//                    RedirectionTargetAttribute, ..., RedirectPolicyAttribute, User = 1000 }
//                    ← Qt6 里 Attribute 就是 QNetworkRequest::Attribute（老写法
//                      QNetworkRequest::Attribute::... 之外，**QNetworkReply::NetworkError 相关
//                      的旧 HttpStatusCode 枚举在 Qt6 已不在 QNetworkReply 里**，状态码统一用
//                      HttpStatusCodeAttribute 取 int，本讲就这么做）
//   第 103~108 行 enum RedirectPolicy { ManualRedirectPolicy, NoLessSafeRedirectPolicy,
//                    SameOriginRedirectPolicy, UserVerifiedRedirectPolicy };
//                    ← Qt5 的 NoRedirects / UserVerified 之外的老名字没了；**默认策略是
//                      NoLessSafeRedirectPolicy**（跟随时只允许「不比原来更安全」的跳转）
//   第 110~112 行 enum TransferTimeoutConstant { DefaultTransferTimeoutConstant = 30000 };
//   第 114 行 static constexpr auto DefaultTransferTimeout = std::chrono::milliseconds(30000);
//   第 148 行 void setRawHeader(const QByteArray &name, const QByteArray &value);
//   第 155/156 行 header/setHeader(KnownHeaders, QVariant)、setAttribute(Attribute, QVariant)
//
// QtNetwork/qabstractsocket.h（QTcpSocket 的实现全在基类里，qtcpsocket.h 第 18 行只是壳）
//   第 136 行 connectToHost(QString, quint16, OpenMode = ReadWrite, NetworkLayerProtocol = AnyIPProtocol)
//   第 187~190 行 connected() / disconnected() / stateChanged(SocketState) / errorOccurred(SocketError)
//                    ← Qt6 一律是 **errorOccurred**；error() 是取当前错误码的函数（第 165 行）
//
// QtNetwork/qhostaddress.h 第 46~53 行 enum SpecialAddress { Null, Broadcast, LocalHost,
//   LocalHostIPv6, Any, AnyIPv6, AnyIPv4 }   ← 本讲用 LocalHost（127.0.0.1）。
//   注意头文件叫 qhostaddress.h，转发头是 <QHostAddress>；没有 qthostaddress.h 这个文件。
// ============================================================================

// ============================================================================
// 二、极简 HTTP/1.1 服务器（只为把请求接住，故意只支持这一小截）
// ============================================================================
// 真实项目你永远不会自己写 HTTP 服务器；这里写它只有一个目的：
// 让演示**在本机回环上闭环**，不欠外网一分钱，也不依赖任何第三方端口。
// 它做的事：读请求行 + 头（读到 \r\n\r\n 为止），按路径回一段固定响应，然后关连接。
class LoopbackHttpServer : public QObject
{
    Q_OBJECT

public:
    explicit LoopbackHttpServer(QObject *parent = nullptr) : QObject(parent)
    {
        m_server = new QTcpServer(this);
        // 第 69 行 newConnection() —— Qt6 是**无参数**的信号，socket 要自己 nextPendingConnection() 取
        QObject::connect(m_server, &QTcpServer::newConnection, this, &LoopbackHttpServer::onNewConnection);
    }

    // 端口传 0 = 让内核挑一个空闲端口；挑完必须回来读 serverPort()
    bool start()
    {
        if (!m_server->listen(QHostAddress::LocalHost, 0)) {
            qWarning() << "  回环服务器起不来：" << m_server->errorString();
            return false;
        }
        m_port = m_server->serverPort();
        return true;
    }

    void stop()
    {
        m_server->close();                     // 不再接新连接
        for (QTcpSocket *c : std::as_const(m_clients)) {
            c->disconnectFromHost();
            c->deleteLater();
        }
        m_clients.clear();
        m_buffers.clear();
    }

    quint16 port() const { return m_port; }
    QHostAddress boundAddress() const { return m_server->serverAddress(); }   // 第 41 行
    int requestCount() const { return m_requests; }
    QString lastVerb() const { return m_lastVerb; }
    QString lastPath() const { return m_lastPath; }
    int lastHeaderCount() const { return m_lastHeaderCount; }
    void setStall(bool on) { m_stall = on; }

signals:
    void requestArrived(const QString &verb, const QString &path);

private slots:
    void onNewConnection()
    {
        while (m_server->hasPendingConnections()) {          // 第 47 行
            QTcpSocket *sock = m_server->nextPendingConnection();  // 第 48 行：**调用方接管所有权**
            m_clients.append(sock);
            m_buffers.insert(sock, QByteArray());
            QObject::connect(sock, &QTcpSocket::readyRead, this, &LoopbackHttpServer::onClientReadyRead);
            QObject::connect(sock, &QTcpSocket::disconnected, this, [this, sock]() {
                m_clients.removeOne(sock);
                m_buffers.remove(sock);
                sock->deleteLater();
            });
        }
    }

    void onClientReadyRead()
    {
        auto *sock = qobject_cast<QTcpSocket *>(sender());
        if (!sock)
            return;
        QByteArray &buf = m_buffers[sock];
        buf.append(sock->readAll());
        const int headEnd = buf.indexOf("\r\n\r\n");
        if (headEnd < 0)
            return;                             // 头还没收完（半包），下次再说
        const QByteArray head = buf.left(headEnd);
        const QList<QByteArray> lines = head.split('\n');
        const QList<QByteArray> reqParts = lines.value(0).trimmed().split(' ');
        m_lastVerb = QString::fromLatin1(reqParts.value(0));
        m_lastPath = QString::fromLatin1(reqParts.value(1));
        m_lastHeaderCount = int(lines.size()) - 1;            // 去掉请求行
        ++m_requests;
        respond(sock, m_lastVerb, m_lastPath, head);
    }

private:
    void writeAll(QTcpSocket *sock, const QByteArray &bytes)
    {
        sock->write(bytes);
        sock->flush();                          // 演示脚本要求「立刻发出去」，不依赖写缓冲调度
        sock->disconnectFromHost();             // Connection: close，干净收场
    }

    static QByteArray frame(const QString &status, const QByteArray &body,
                            const QList<QByteArray> &extraHeaders)
    {
        QByteArray r = "HTTP/1.1 " + status.toLatin1() + "\r\n";
        r += "Content-Type: application/json; charset=utf-8\r\n";
        r += "Content-Length: " + QByteArray::number(qint64(body.size())) + "\r\n";
        r += "Server: qteach-loopback/1.0\r\n";
        r += "Connection: close\r\n";
        for (const QByteArray &h : extraHeaders)
            r += h + "\r\n";
        r += "\r\n";
        r += body;
        return r;
    }

    void respond(QTcpSocket *sock, const QString &verb, const QString &path, const QByteArray &head)
    {
        Q_UNUSED(head);
        if (path.startsWith(QStringLiteral("/stall"))) {
            if (m_stall)
                return;                         // 一个字都不回：让客户端的 transferTimeout 生效
            setStall(false);
        }
        if (path.startsWith(QStringLiteral("/moved"))) {
            writeAll(sock, frame(QStringLiteral("302 Found"), QByteArray(),
                                  { "Location: /api/device" }));
            return;
        }
        if (path.startsWith(QStringLiteral("/slow"))) {
            // 证据⑤专用：把响应压后 20ms 再写。
            // 为什么要专门慢——回环上的 GET 常在 <1ms 内就完成，客户端发出请求到收到 finished
            // 之间，事件循环可能一次 1ms 心跳都没轮到（本机实测「耗时 0ms、心跳 0 次」）。
            // 这不是「线程被钉死」，纯粹是这一趟太快、测不出轮数。给服务器加 20ms 真实延迟，
            // 让「等待一个请求」这段时间跨过若干个 1ms 心跳，才能可靠地证明事件循环一直在转。
            QTimer::singleShot(20, this, [this, sock]() {
                QJsonObject o;
                o.insert(QStringLiteral("id"), 42);
                o.insert(QStringLiteral("name"), QStringLiteral("巡检网关-A3"));
                o.insert(QStringLiteral("online"), true);
                writeAll(sock, frame(QStringLiteral("200 OK"),
                                     QJsonDocument(o).toJson(QJsonDocument::Compact), {}));
            });
            return;
        }
        if (path.startsWith(QStringLiteral("/api/device"))) {
            // 正文做成 JSON，好让客户端「解析出的字段」当证据打印
            QJsonObject o;
            o.insert(QStringLiteral("id"), 42);
            o.insert(QStringLiteral("name"), QStringLiteral("巡检网关-A3"));
            o.insert(QStringLiteral("online"), true);
            const QByteArray body = QJsonDocument(o).toJson(QJsonDocument::Compact);
            if (verb == QStringLiteral("HEAD"))
                writeAll(sock, frame(QStringLiteral("200 OK"), QByteArray(), {}));
            else if (verb == QStringLiteral("DELETE"))
                writeAll(sock, frame(QStringLiteral("204 No Content"), QByteArray(), {}));
            else
                writeAll(sock, frame(QStringLiteral("200 OK"), body, {}));
            return;
        }
        writeAll(sock, frame(QStringLiteral("404 Not Found"),
                             QByteArray("{\"err\":\"no such path\"}"), {}));
    }

    QTcpServer *m_server = nullptr;
    QList<QTcpSocket *> m_clients;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    quint16 m_port = 0;
    int m_requests = 0;
    int m_lastHeaderCount = 0;
    QString m_lastVerb;
    QString m_lastPath;
    bool m_stall = false;
};

// ============================================================================
// 三、小工具
// ============================================================================
// URL 的正规造法：一段一段 set，别拼字符串。
static QUrl loopUrl(quint16 port, const QString &path)
{
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(QStringLiteral("127.0.0.1"));
    url.setPort(int(port));
    url.setPath(path);
    return url;
}

struct Stats
{
    inline static int readyReadHits = 0;
    inline static int getReadyReads = 0;        // 只统计证据②那一次 GET
    inline static int finishedHits = 0;
    inline static int errorHits = 0;
    inline static int redirectsSeen = 0;
    inline static int progressSignals = 0;
    inline static qint64 bytesViaProgress = 0;
    inline static int eventLoopTurns = 0;       // 证据⑤：等一个请求的时间里心跳定时器跳了几次
};

// 把一次请求的「可核对证据」全部打出来
// ★ tag 里混着中文（"A 组（自动跟随）"），所以用 fromUtf8；
//   反过来，下面 rawHeader("Server") 那种**网络来的 ASCII 头**才该用 fromLatin1 —— 
//   选哪个不看顺手，看这串字节的编码是谁定的。
static void dumpReplyEvidence(const char *tag, QNetworkReply *reply, const QByteArray &body)
{
    const QVariant codeAttr = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const QVariant reasonAttr = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute);
    qInfo() << QStringLiteral("  【%1】operation=%2｜error()=%3(%4)｜errorString()=\"%5\"")
                   .arg(QString::fromUtf8(tag))
                   .arg(int(reply->operation()))
                   .arg(int(reply->error()))
                   .arg(reply->error() == QNetworkReply::NoError
                            ? QStringLiteral("NoError") : QStringLiteral("非 0"))
                   .arg(reply->errorString());
    qInfo() << QStringLiteral("  【%1】状态码 attribute(HttpStatusCode)=%2｜reason=\"%3\"")
                   .arg(QString::fromUtf8(tag)).arg(codeAttr.toInt())
                   .arg(reasonAttr.toString());
    qInfo() << QStringLiteral("  【%1】正文 %2 字节｜Server 头=\"%3\"｜头字段共 %4 个")
                   .arg(QString::fromUtf8(tag)).arg(body.size())
                   .arg(QString::fromLatin1(reply->rawHeader("Server")))
                   .arg(reply->rawHeaderList().size());
    // 解析出的字段：这一条证明「读到的字节真的能落成数据」，不是只打了个长度
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
        const QJsonObject o = doc.object();
        qInfo() << QStringLiteral("  【%1】JSON 字段：id=%2 name=%3 online=%4")
                       .arg(QString::fromUtf8(tag))
                       .arg(o.value(QStringLiteral("id")).toInt())
                       .arg(o.value(QStringLiteral("name")).toString())
                       .arg(o.value(QStringLiteral("online")).toBool());
    } else if (!body.isEmpty()) {
        qInfo() << QStringLiteral("  【%1】正文不是 JSON（%2 字节原样留着）")
                       .arg(QString::fromUtf8(tag)).arg(body.size());
    }
}

// ============================================================================
// 四、证据①：QUrl 的三个坑（只解析，不联网、不落盘）
// ============================================================================
static void evidenceUrlBasics()
{
    qInfo() << "【证据①】QUrl：手写字符串 vs 正规构造";

    const QUrl sloppy(QStringLiteral("C:/data/a.json"));
    qInfo() << "  QUrl(\"C:/data/a.json\") → scheme =" << sloppy.scheme()
            << "path =" << sloppy.path()
            << "← **「C:」被当成 scheme 了**，这就是手撸字符串的下场（第 229 行了才救得回来）";
    const QUrl right = QUrl::fromLocalFile(QStringLiteral("C:/data/a.json"));
    qInfo() << "  QUrl::fromLocalFile(...) →" << right.toString()
            << "｜toLocalFile() 回来 =" << right.toLocalFile()
            << "｜isValid =" << right.isValid()
            << "← 本地文件**只有** fromLocalFile 这一条路（NAM 也能 GET file://，第 83 行那个 get）";

    QUrl api = QUrl(QStringLiteral("http://127.0.0.1:80/api/device"));
    QUrlQuery qq;
    qq.addQueryItem(QStringLiteral("name"), QStringLiteral("网关 A3"));   // 带空格 + 中文
    qq.addQueryItem(QStringLiteral("tag"), QStringLiteral("a&b"));
    api.setQuery(qq);
    qInfo() << "  QUrlQuery 拼出来的 toEncoded() =" << QString::fromLatin1(api.toEncoded());
    qInfo() << "     对比手工拼接 QUrl(\"...?name=网关 A3&tag=a&b\") 会被拆成"
            << QUrl(QStringLiteral("http://127.0.0.1/api/device?name=网关 A3&tag=a&b")).query()
            << "← 多出来的那个空 queryItem 就是「手撸 &」的债";
    qInfo() << "  拆回来：path =" << api.path() << "queryItem(name) ="
            << QUrlQuery(api.query()).queryItemValue(QStringLiteral("name"));
    qInfo() << "  用户输入的口子：QUrl::fromUserInput(\"127.0.0.1:80/x\") →"
            << QUrl::fromUserInput(QStringLiteral("127.0.0.1:80/x")).toString()
            << "（第 180 行；只有「用户输入的地址栏文本」才用它，代码里构造 URL 别用）";

    checkBool("① 手写 \"C:/data/a.json\" 的 scheme 被误认成 c", sloppy.scheme() == QStringLiteral("c"));
    checkBool("① fromLocalFile 得到的 URL 合法", right.isValid());
    checkBool("① toLocalFile 能原样还原 Windows 路径",
              right.toLocalFile() == QStringLiteral("C:/data/a.json"));
    check("① QUrlQuery 拼出的 queryItem 数",
          int(QUrlQuery(api.query()).queryItems().size()), 2);
    checkBool("① 同一个 query 拆回来 name 仍是「网关 A3」",
              QUrlQuery(api.query()).queryItemValue(QStringLiteral("name")) == QStringLiteral("网关 A3"));
    checkBool("① 值里的 & 被编码成 %26", api.toEncoded().contains("tag=a%26b"));
    checkBool("① 中文按 UTF-8 百分号编码（%E7%BD%91 = 网）",
              api.toEncoded().contains("%E7%BD%91%E5%85%B3"));
    check("① 手撸 & 的那个 URL 被拆成 3 个 queryItem（多出一个空值项）",
          int(QUrlQuery(QUrl(QStringLiteral("http://127.0.0.1/api/device?name=网关 A3&tag=a&b")).query())
                  .queryItems()
                  .size()),
          3);
    checkBool("① fromUserInput 给裸地址补上 http scheme",
              QUrl::fromUserInput(QStringLiteral("127.0.0.1:80/x")).scheme() == QStringLiteral("http"));
}

// ============================================================================
// 五、证据②：一次规范的 GET —— finished / readyRead / error 的分工
// ============================================================================
// 本讲是一条「QTimer::singleShot 串起来的自动流水线」：
//   GET → 六个动词 → 重定向 → 事件循环取证 → 卡住的请求 → quit。
// 后面的步骤被前面调用，所以先放前向声明（定义在后面，只靠声明调用）。
static void doVerbMatrix(QNetworkAccessManager *nam, LoopbackHttpServer *srv);
static void doRedirectDemo(QNetworkAccessManager *nam, LoopbackHttpServer *srv);
static void evidenceEventLoopAlive(QNetworkAccessManager *nam, LoopbackHttpServer *srv);
static void evidenceStallAndCleanup(QNetworkAccessManager *nam, LoopbackHttpServer *srv);

static void doHttpGet(QNetworkAccessManager *nam, LoopbackHttpServer *srv)
{
    qInfo() << "【证据②】GET 一次：信号分工与读取时机";
    QNetworkRequest req(loopUrl(srv->port(), QStringLiteral("/api/device")));
    req.setRawHeader("Accept", "application/json");
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("QtTeach/1.0"));
    // Qt6：duration 版（第 131 行），别写 setTransferTimeout(3000)
    req.setTransferTimeout(std::chrono::milliseconds(3000));

    QNetworkReply *reply = nam->get(req);
    const int codeRightAfterSend = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // ★ 累积缓冲：readyRead 里读到一段就 append 一段，finished 里再把「最后一小段」读空 append 上，
    //   拼成完整正文。为什么必须这样：readyRead 与 finished 读的是**同一条 QIODevice 的读队列**，
    //   谁先 readAll() 谁就把字节取走了；如果 readyRead 已经读干，finished 再 readAll() 只会拿到 0 字节
    //   （本机实测：正文被 readyRead 吃掉后，finished 里 body.size()=0，JSON 解析全成默认值）。
    //   用 QSharedPointer 持有，两个 lambda 各自拷一份指针引用，reply 结束后自动释放。
    auto streamBuf = QSharedPointer<QByteArray>::create();
    qInfo() << "  发出去立刻看：isRunning =" << reply->isRunning()
            << "isFinished =" << reply->isFinished()
            << "｜此刻读 attribute(HttpStatusCode) =" << codeRightAfterSend
            << "← **0**：头还没到，状态码此时读不到，这就是「读取时机」四个字的含义";
    checkBool("② 刚发出去 isRunning() 还是 true", reply->isRunning());
    checkBool("② 刚发出去 isFinished() 还是 false", reply->isFinished(), false);
    check("② 刚发出去读状态码只能是 0（头还没到）", codeRightAfterSend, 0);
    checkBool("② 刚发出去 rawHeader(\"Server\") 是空的", reply->rawHeader("Server").isEmpty());

    QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply, streamBuf]() {
        ++Stats::getReadyReads;
        ++Stats::readyReadHits;
        const QByteArray chunk = reply->readAll();
        streamBuf->append(chunk);
        qInfo() << "    readyRead 第" << Stats::getReadyReads << "次：这次读到" << chunk.size()
                << "字节（有正文可读了，流式写法就在这里落盘/喂解析器）";
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [](qint64 got, qint64 total) {
        ++Stats::progressSignals;
        Stats::bytesViaProgress = got;
        Q_UNUSED(total)
    });
    QObject::connect(reply, &QNetworkReply::errorOccurred, reply, [](QNetworkReply::NetworkError e) {
        ++Stats::errorHits;
        qInfo() << "    errorOccurred(" << int(e) << ") —— Qt6 的信号名，"
                "Qt5 的 error(QNetworkReply::NetworkError) 已不存在";
    });

    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, nam, srv, streamBuf]() {
        ++Stats::finishedHits;
        // finished 里把「readyRead 之后可能还剩的一小段」也读干，拼进累积缓冲得到完整正文
        streamBuf->append(reply->readAll());
        const QByteArray body = *streamBuf;                // 一次性读法的兜底：到这里一定读空了
        dumpReplyEvidence("GET /api/device", reply, body);
        qInfo() << "  finished 时：readyRead 共触发" << Stats::getReadyReads
                << "次｜downloadProgress" << Stats::progressSignals
                << "次（末次字节" << Stats::bytesViaProgress << "）｜errorOccurred"
                << Stats::errorHits << "次";
        qInfo() << "  → 分工记一句：**readyRead = 能读了（可能很多次）；finished = 结束了（一次，必发）**。"
                   "正文小，finished 后 readAll 就够；要边下边处理，才在 readyRead 里读。";
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        check("② finished 后状态码 200", reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
        check("② finished 后 error() = NoError", int(reply->error()), int(QNetworkReply::NoError));
        checkBool("② finished 后 isFinished() = true", reply->isFinished());
        check("② 正文解析出的 id", o.value(QStringLiteral("id")).toInt(), 42);
        checkBool("② 正文解析出的 name 是中文且没被转坏",
                  o.value(QStringLiteral("name")).toString() == QStringLiteral("巡检网关-A3"));
        checkBool("② online 字段是 true", o.value(QStringLiteral("online")).toBool());
        checkBool("② Server 头来自回环服务器", reply->rawHeader("Server") == "qteach-loopback/1.0");
        checkBool("② readyRead 至少触发过 1 次（小正文通常就 1 次）", Stats::getReadyReads >= 1);
        checkBool("② downloadProgress 报出的末次字节 = 正文大小",
                  Stats::bytesViaProgress == qint64(body.size()));
        reply->deleteLater();                                    // QObject，必须交还所有权
        QTimer::singleShot(20, [nam, srv]() { doVerbMatrix(nam, srv); });
    });
}

// ============================================================================
// 六、证据③：六个动词各自什么时候用
// ============================================================================
static void doVerbMatrix(QNetworkAccessManager *nam, LoopbackHttpServer *srv)
{
    qInfo() << "【证据③】head / get / post / put / deleteResource / sendCustomRequest";
    const int requestsBefore = srv->requestCount();
    const QUrl u = loopUrl(srv->port(), QStringLiteral("/api/device"));

    // 六个请求**共用同一个 NAM**：这就是「一个应用一个」的实际收益——连接池、Cookie、
    // 缓存、代理配置全部复用；你每发一个请求 new 一个 NAM，这些全得重新付一遍。
    // 六个同时在飞，所以计数器不能放栈上（函数早就返回了），new 一块共享状态，
    // 最后完成的那个槽负责 delete ——异步回调里的常规写法。
    struct Pending { int left; int before; };
    Pending *p = new Pending{ 6, requestsBefore };

    QNetworkRequest req(u);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const char *tags[6] = {
        "head                第 82 行 只要头不要正文（探测存在性/大小）",
        "get                 第 83 行 取资源，不带请求体",
        "post(QByteArray)    第 87 行 新建/提交，正文进 body（非幂等）",
        "put(QByteArray)     第 94 行 整体替换，**幂等**：同一份内容发两次结果一样",
        "deleteResource      第 100 行 删资源；**Qt6 没有 delete() 这个方法名**",
        "sendCustomRequest   第 101 行 PATCH 这类自定义动词（verb 是 QByteArray）",
    };
    QNetworkReply *replies[6];
    replies[0] = nam->head(req);
    replies[1] = nam->get(req);
    replies[2] = nam->post(req, QByteArray("{\"op\":\"create\"}"));
    replies[3] = nam->put(req, QByteArray("{\"op\":\"replace\"}"));
    replies[4] = nam->deleteResource(req);
    replies[5] = nam->sendCustomRequest(req, "PATCH", QByteArray("{\"op\":\"patch\"}"));

    // 期望值按「数组下标」写，注意 operation() 的编号与下标**不是**一一对应：
    // head=1, get=2, post=4, put=3, delete=5, custom=6（枚举顺序就是这么排的）。
    static const int expectedOps[6] = {
        int(QNetworkAccessManager::HeadOperation),   int(QNetworkAccessManager::GetOperation),
        int(QNetworkAccessManager::PostOperation),   int(QNetworkAccessManager::PutOperation),
        int(QNetworkAccessManager::DeleteOperation), int(QNetworkAccessManager::CustomOperation),
    };
    static const int expectedCodes[6] = { 200, 200, 200, 200, 204, 200 };

    for (int i = 0; i < 6; ++i) {
        QNetworkReply *reply = replies[i];
        const char *tag = tags[i];
        QObject::connect(reply, &QNetworkReply::finished, reply, [reply, tag, p, srv, nam, i]() {
            ++Stats::finishedHits;                          // 全局计数：每条 reply 收口都算一次
            const QByteArray body = reply->readAll();
            const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            qInfo() << QStringLiteral("  %1 → 状态 %2｜operation()=%3｜正文 %4 字节｜error()=%5")
                           .arg(QString::fromUtf8(tag)).arg(code)
                           .arg(int(reply->operation())).arg(body.size())
                           .arg(int(reply->error()));
            check("③ 状态码", code, expectedCodes[i]);
            check("③ operation() 的编号", int(reply->operation()), expectedOps[i]);
            check("③ error() = NoError", int(reply->error()), int(QNetworkReply::NoError));
            if (i == 0 || i == 4)                    // HEAD 与 DELETE(204) 都没有正文
                check("③ HEAD / 204 的正文必须是 0 字节", body.size(), 0);
            else
                checkBool("③ 带正文的动词确实收到了正文", !body.isEmpty());
            reply->deleteLater();
            if (--p->left == 0) {
                qInfo() << "  六个动词全部收口：本轮服务器收到" << (srv->requestCount() - p->before)
                        << "个请求（累计" << srv->requestCount() << "）｜最后一个被看到的动词/路径 ="
                        << srv->lastVerb() << srv->lastPath();
                qInfo() << "  → operation() 的值来自第 42 行那个 Operation 枚举"
                        "（Head=1 Get=2 Put=3 Post=4 Delete=5 Custom=6，Unknown=0）；"
                        "它是 **从 1 起编号**的，别拿它做位运算。";
                check("③ 六个动词让服务器各收到 1 个请求", srv->requestCount() - p->before, 6);
                delete p;
                QTimer::singleShot(20, [srv, nam]() { doRedirectDemo(nam, srv); });
            }
        });
    }
}

// ============================================================================
// 七、证据④：重定向 —— Qt6 默认自动跟随 vs 手动策略，两条路都要会写
// ============================================================================
static void doRedirectDemo(QNetworkAccessManager *nam, LoopbackHttpServer *srv)
{
    qInfo() << "【证据④】302：自动跟随（Qt6 默认）与 ManualRedirectPolicy";

    // A 组：不设策略 → 用 NAM 的 NoLessSafeRedirectPolicy（第 119 行那个 set 我没改默认），
    // 302 会被自动跟到底，最终 finished 时你看到的是 /api/device 的 200。
    QNetworkRequest autoReq(loopUrl(srv->port(), QStringLiteral("/moved")));
    QNetworkReply *r1 = nam->get(autoReq);
    QObject::connect(r1, &QNetworkReply::redirected, r1, [](const QUrl &target) {
        ++Stats::redirectsSeen;
        qInfo() << "    A 组 redirected(const QUrl&) →" << target.toString()
                << "（Qt6 这个信号**只有一个参数**，Qt5 的第二参数 RedirectionType 本机头文件里搜不到）";
    });

    // B 组：只对这一个请求改成手动策略。
    // 保守写法：RedirectPolicyAttribute 的取值本讲用 int 存（qnetworkrequest.h 第 103~108 行的
    // RedirectPolicy **没有** Q_ENUM 注册，同文件只有 KnownHeaders 那一个枚举有 Q_ENUM；
    // QVariant::fromValue(枚举值) 依赖元类型注册，**待复核**，所以这里显式转 int 更稳）。
    QNetworkRequest manualReq(loopUrl(srv->port(), QStringLiteral("/moved")));
    manualReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                           int(QNetworkRequest::ManualRedirectPolicy));
    QNetworkReply *r2 = nam->get(manualReq);
    qInfo() << "  A 组：/moved（服务器回 302 Location: /api/device），默认策略 → 自己跟到底";
    qInfo() << "  B 组：同样的 /moved，但 setAttribute(RedirectPolicyAttribute, Manual) → 停在 302";

    // 两个请求一起收，最后那个负责跑总结、把流水线交下去
    // 两个请求一起收，最后那个负责把流水线交给下一步（用一次性标志，别用计数指针）
    struct Pair { int left; };
    Pair *pair = new Pair{ 2 };
    QObject::connect(r1, &QNetworkReply::finished, r1, [r1, pair, srv, nam]() {
        ++Stats::finishedHits;
        dumpReplyEvidence("A 组（自动跟随）", r1, r1->readAll());
        qInfo() << "  A 组最终 url() =" << r1->url().toString()
                << "｜request().url() =" << r1->request().url().toString()
                << "（跟完之后 url() 已是 Location 那条——判断「我到底拿到了谁的响应」看这里）";
        check("④ A 组自动跟随后拿到的是 200", r1->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
        checkBool("④ A 组 reply->url() 已变成跳转目标 /api/device",
                  r1->url().path() == QStringLiteral("/api/device"));
        checkBool("④ A 组 request().url() 仍是当初请求的 /moved",
                  r1->request().url().path() == QStringLiteral("/moved"));
        check("④ A 组 error() = NoError（跟随成功不算错）", int(r1->error()), int(QNetworkReply::NoError));
        r1->deleteLater();
        if (--pair->left == 0) { delete pair; QTimer::singleShot(20, [srv, nam]() { evidenceEventLoopAlive(nam, srv); }); }
    });
    QObject::connect(r2, &QNetworkReply::finished, r2, [r2, pair, srv, nam]() {
        ++Stats::finishedHits;
        dumpReplyEvidence("B 组（停在 302）", r2, r2->readAll());
        const QVariant target = r2->attribute(QNetworkRequest::RedirectionTargetAttribute);
        qInfo() << "  B 组 RedirectionTargetAttribute =" << target.toUrl().toString()
                << "｜isValid =" << target.toUrl().isValid()
                << "← 手动策略下框架把「该跳哪儿」交给你：跳不跳、要不要补 scheme、"
                   "要不要问用户，全是你的代码（UserVerifiedRedirectPolicy 就是给第三种用的）";
        check("④ B 组停在 302", r2->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 302);
        checkBool("④ B 组 RedirectionTargetAttribute 是有效 QUrl", target.toUrl().isValid());
        checkBool("④ B 组拿到的跳转目标是 /api/device",
                  target.toUrl().path() == QStringLiteral("/api/device"));
        checkBool("④ B 组 url() 没有被改写，仍是 /moved",
                  r2->url().path() == QStringLiteral("/moved"));
        r2->deleteLater();
        if (--pair->left == 0) { delete pair; QTimer::singleShot(20, [srv, nam]() { evidenceEventLoopAlive(nam, srv); }); }
    });
}

// ============================================================================
// 八、证据⑤：为什么「同步等网络」在 Qt 里几乎总是错的
// ============================================================================
// 本讲**不用**任何 waitFor*，也不套嵌套 QEventLoop。取证办法：main 里挂一个 1ms 的
// 心跳定时器专门数事件循环的轮数，再发一个真实请求，看等待期间它跳了几次。
// 次数 > 0 就是证据：请求在飞的同时，界面照常重绘、别的信号照常发、别的请求照常推进。
// 反写法的两种死法：
//   ① reply->waitForFinished() —— 线程钉在这一格，心跳不跳、界面白屏、所有定时器停摆；
//      更糟的是它在**没有事件循环**的线程上根本不能这么混用（半轮询半事件驱动，行为难料）。
//   ② QEventLoop loop; connect(finished,&loop,&quit); loop.exec(); ——「伪同步」，
//      嵌套事件循环把队列里所有等待中的信号**提前放出来**，你的槽会在想不到的时刻插队，
//      对象生命周期与状态机顺序全乱。这种代码出事时最难查，因为它「平时是好的」。
// 唯一合法的例外：**没有事件循环的控制台程序**（构建脚本、CI 冒烟测试）里用 waitFor*；
// 本讲不属于那个例外（main 里有 app.exec()），所以一次 waitFor 都没写。
static void evidenceEventLoopAlive(QNetworkAccessManager *nam, LoopbackHttpServer *srv)
{
    qInfo() << "【证据⑤】等待期间事件循环一直在转（本讲零阻塞）";
    const int turnsBefore = Stats::eventLoopTurns;

    // 走 /slow：服务器故意晚 20ms 才回（见 LoopbackHttpServer::respond 里的注释）。
    // 只有这样这一趟才跨得过若干个 1ms 心跳——回环直连的 /api/device 常在 1ms 内完成，
    // 那时「心跳次数 = 0」是真实测量、不是线程被钉死，用它当证据会误报。
    QNetworkRequest req(loopUrl(srv->port(), QStringLiteral("/slow")));
    req.setTransferTimeout(std::chrono::milliseconds(2000));
    QNetworkReply *reply = nam->get(req);
    QElapsedTimer timer;
    timer.start();

    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, srv, turnsBefore, timer, nam]() {
        ++Stats::finishedHits;
        const qint64 ms = timer.elapsed();
        const int turns = Stats::eventLoopTurns - turnsBefore;
        qInfo() << "  这一个请求耗时" << ms << "ms｜期间 1ms 心跳定时器跳了" << turns << "次";
        qInfo() << "  → 数值大小不重要（取决于机器），重要的是**它不是 0**：线程没被钉死。";
        checkBool("⑤ 等待这一个请求期间，1ms 心跳至少跳了 1 次（线程没被钉死）", turns >= 1);
        check("⑤ 被等的这个请求本身是成功的", reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
        reply->deleteLater();
        QTimer::singleShot(20, [nam, srv]() { evidenceStallAndCleanup(nam, srv); });
    });
}

// ============================================================================
// 九、证据⑥：服务器一个字都不回 —— 超时怎么兜住
// ============================================================================
static void evidenceStallAndCleanup(QNetworkAccessManager *nam, LoopbackHttpServer *srv)
{
    qInfo() << "【证据⑥】/stall：接了连接但不写任何字节 → 看 transferTimeout";
    srv->setStall(true);

    QNetworkRequest req(loopUrl(srv->port(), QStringLiteral("/stall")));
    req.setTransferTimeout(std::chrono::milliseconds(800));    // 第 131 行的 duration 重载
    qInfo() << "  本请求超时 800ms｜NAM 全局超时" << int(nam->transferTimeout())
            << "ms｜没设过时的默认常量 DefaultTransferTimeoutConstant = 30000（第 110 行）";

    QNetworkReply *reply = nam->get(req);
    QElapsedTimer stallClock;
    stallClock.start();
    QObject::connect(reply, &QNetworkReply::errorOccurred, reply, [](QNetworkReply::NetworkError e) {
        ++Stats::errorHits;                                   // 全局计数：超时这次 errorOccurred 也要记进硬账
        qInfo() << "  卡住的请求 errorOccurred →" << int(e)
                << (e == QNetworkReply::TimeoutError ? QStringLiteral("= TimeoutError(4)")
                                                     : QStringLiteral("（别的错误码）"));
    });
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, srv, nam, stallClock]() {
        ++Stats::finishedHits;
        const qint64 waited = stallClock.elapsed();
        // ★ 下面这行 readAll() 在本机会让 Qt 先打一条警告再给你空字节：
        //     QIODevice::read (QNetworkReplyHttpImpl): device not open
        //   这不是示例写歪了，是**超时收尾的真实形状**：请求以 TimeoutError 收场时设备已经关了，
        //   isOpen() = false，readAll() 走 QIODevice 的守卫分支，警告 + 返回空。
        //   故意留着：①「finished 一定到，但正文不一定有」这句话只有警告当证人才立得住；
        //   ② 工程里读之前先看 isOpen()，或者干脆判 error() != NoError 就别碰正文。
        //   （别去找什么 `isRawDataEnabled()`：本机 qiodevice.h 只有 54 行 isOpen()
        //     和 77 行 bytesAvailable()，QNetworkReply 身上是 95/96 行的
        //     isFinished()/isRunning()——网上有些文章编的开关名，编译第一关就过不去。）
        qInfo() << "  读正文前先看门的开关：isOpen() =" << reply->isOpen()
                << "｜bytesAvailable() =" << reply->bytesAvailable()
                << "（isOpen=false => 下面这口 readAll() 会带一条 device not open 的警告，属预期）";
        dumpReplyEvidence("GET /stall（超时收尾）", reply, reply->readAll());
        qInfo() << "  → 超时也一定走 finished：**收尾代码只写在 finished 里**永远是对的。"
                   "只 connect(errorOccurred) 做清理的人，正常路径漏 deleteLater；"
                   "反过来只 connect(finished) 的人，把 error 当成功处理。两个都要读：error() 判性质，"
                   "finished 做收口。";
        check("⑥ 卡住的请求以 TimeoutError 收尾", int(reply->error()), int(QNetworkReply::TimeoutError));
        checkBool("⑥ 超时至少等够了设定的 800ms（不许提前报错）", waited >= 700);
        check("⑥ 超时请求没有拿到状态码", reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 0);
        reply->deleteLater();
        srv->setStall(false);
        QTimer::singleShot(30, [srv, nam]() {
            qInfo() << "【收尾】统计：readyRead" << Stats::readyReadHits
                    << "finished" << Stats::finishedHits << "errorOccurred" << Stats::errorHits
                    << "redirected" << Stats::redirectsSeen
                    << "｜事件循环轮数" << Stats::eventLoopTurns
                    << "｜服务器累计收到" << srv->requestCount() << "个请求";
            qInfo() << "【收尾】这一讲的规矩记一句话：**NAM 一个就够，reply 必须 deleteLater，"
                       "状态只在 finished 之后读，超时必须显式设，端口必须现拿。**";

            // —— 硬账：每一条 reply 都必须恰好走到一次 finished，不多不少 ——
            check("收尾 finished 总次数（GET 1 + 六动词 6 + 重定向 2 + 取证 1 + 超时 1）",
                  Stats::finishedHits, 11);
            check("收尾 errorOccurred 总次数（只有超时那一次）", Stats::errorHits, 1);
            check("收尾 redirected 信号次数（只有 A 组真的跟了）", Stats::redirectsSeen, 1);
            check("收尾服务器收到的请求数（1 + 6 + 2 + 1 + 1 + 1）", srv->requestCount(), 12);
            checkBool("收尾服务器最后处理的是 /stall",
                      srv->lastPath() == QStringLiteral("/stall"));
            checkBool("收尾服务器绑的确实是回环地址", srv->boundAddress().isLoopback());
            checkBool("收尾端口不是硬编码的（内核分配，非 0）", srv->port() != 0);

            srv->stop();                                        // 关服务器
            qInfo().noquote() << QStringLiteral("\n【硬账】%1/%2 项核对通过 → 退出码 %3")
                                     .arg(Ledger::passed)
                                     .arg(Ledger::total)
                                     .arg(Ledger::exitCode());
            QCoreApplication::exit(Ledger::exitCode());
        });
    });
}

// ============================================================================
// 十、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("network_01"));

    // 一个应用一个 NAM：挂在 app 上（parent 保证退出时析构），活在主线程事件循环里
    auto *nam = new QNetworkAccessManager(&app);
    nam->setTransferTimeout(std::chrono::milliseconds(5000));          // 全局兜底
    nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);  // 显式写出默认值，别靠记

    auto *srv = new LoopbackHttpServer(&app);

    // 心跳定时器：只为「事件循环一直在转」这条证据服务（1ms 一跳）
    auto *beat = new QTimer(&app);
    beat->setInterval(1);
    QObject::connect(beat, &QTimer::timeout, &app, []() { ++Stats::eventLoopTurns; });
    beat->start();

    QTimer::singleShot(0, [nam, srv]() {
        qInfo() << "================================================================";
        qInfo() << "network 第 1 讲 · QNetworkAccessManager 的正确姿势（Qt =" << QT_VERSION_STR << "）";
        qInfo() << "================================================================";
        if (!srv->start()) {
            qWarning() << "回环服务器起不来，直接收工";
            checkBool("回环服务器能在 127.0.0.1 上 listen(0)", false);
            qInfo().noquote() << QStringLiteral("\n【硬账】%1/%2 项核对通过 → 退出码 %3")
                                     .arg(Ledger::passed).arg(Ledger::total).arg(Ledger::exitCode());
            QCoreApplication::exit(Ledger::exitCode());
            return;
        }
        qInfo() << "  回环服务器已就位：127.0.0.1:" << srv->port()
                << "｜serverAddress =" << srv->boundAddress().toString()
                << "（端口是 listen(QHostAddress::LocalHost, 0) 现分配的，**没有硬编码**）";

        evidenceUrlBasics();
        doHttpGet(nam, srv);
    });

    // 看门狗：不管卡在哪一步，12 秒到点必须退出并留下「超时」证据
    QTimer::singleShot(12000, [srv]() {
        qWarning() << "【超时兜底】12 秒到点：强制收尾（网络卡住绝不让进程挂死）";
        qInfo() << "  超时前统计：readyRead" << Stats::readyReadHits
                << "finished" << Stats::finishedHits << "errorOccurred" << Stats::errorHits
                << "redirected" << Stats::redirectsSeen
                << "｜事件循环轮数" << Stats::eventLoopTurns;
        srv->stop();
        // 走到这里说明正常流水线没跑完——**这一条要判红**，否则「超时退出 0」会把故障藏起来
        checkBool("流程本该在 12 秒内自行收尾（看门狗不该被触发）", false);
        qInfo().noquote() << QStringLiteral("\n【硬账】%1/%2 项核对通过 → 退出码 %3")
                                 .arg(Ledger::passed).arg(Ledger::total).arg(Ledger::exitCode());
        QCoreApplication::exit(Ledger::exitCode());
    });

    const int rc = app.exec();
    qInfo() << "事件循环已退出，返回码" << rc
            << "｜NAM / 服务器 / 心跳定时器的 parent 都是 app，随 app 析构回收";
    return rc;
}

// ============================================================================
// 十一、速查表与纪律（背不住就回来查）
// ============================================================================
/*
 * 一、六个动词的选择表（qnetworkaccessmanager.h 行号是本机 6.12.0）
 *   场景                        写什么                          行号     备注
 *   只要头（探测大小/存在性）   head(req)                       82      正文恒空，别等 readyRead
 *   取资源                      get(req)                        83      最常用；无请求体
 *   取资源并带体（冷门）        get(req, data)                  84/85   有重载，但大多数服务端不收
 *   新建/提交                   post(req, QByteArray)           87      **非幂等**：重试会重复下单
 *   新建/提交（大文件）         post(req, QIODevice*)           86      省内存：框架边读边发
 *   多表单上传                  post(req, QHttpMultiPart*)      105     文件 + 字段混合
 *   整体替换                    put(req, data)                  93/94   **幂等**，重试安全
 *   删除                        deleteResource(req)             100     不是 delete()，那是关键字
 *   其它动词                    sendCustomRequest(req, "PATCH") 101/102 verb 是 QByteArray
 *
 * 二、生命周期五条纪律
 *   ① NAM：**一个应用一个**，parent 挂到 QApplication（或你的单例）上。
 *      它内部有连接池/代理/Cookie/缓存/HSTS，多实例 = 全部重新付一遍。
 *   ② NAM 必须活在**有事件循环的线程**：GUI 线程天然满足；
 *      子线程里用的话，要么那个线程自己 exec()，要么干脆别在子线程发（Qt6 的
 *      QNetworkAccessManager 不是线程安全的，跨线程调 get() 是未定义行为）。
 *   ③ reply 是 QObject（qnetworkreply.h 第 29 行，还是 QIODevice）：
 *      **谁 new 出来谁负责** —— get/post 返回的 reply 归你，finished 里 deleteLater()。
 *      设了 setAutoDeleteReplies(true)（第 122/123 行）就别再自己 delete，
 *      而且**finished 之后任何一刻都可能被销毁**，先用局部拷贝的数据。
 *   ④ 信号槽的第五个参数（context object）在网络层是保命写法：
 *      QObject::connect(reply, &QNetworkReply::finished, this, ...) —— 这个 this 保证
 *      「发起者析构后不再回调」。详见第 2 讲与 metaobject 第 4 讲。
 *   ⑤ 不要拿 reply 的裸指针跨线程传，也不要把它塞进全局变量「回头再读」。
 *
 * 三、读取时机（本讲最容易忘的一条）
 *   发出请求后**立刻**读这些，得到的都是空/0：
 *      attribute(HttpStatusCodeAttribute)  → 0（头还没到）
 *      rawHeader("Server")                 → 空
 *      errorString()                       → 空
 *   该什么时候读：
 *      状态/头      → metaDataChanged 之后就有，**finished 里读最稳**
 *      正文         → readyRead（流式）或 finished 后 readAll()（小正文）
 *      错误码       → errorOccurred 参数给什么就是什么，但**性质要 finished 后统一判**
 *      isFinished() → finished 之后为 true
 *
 * 四、超时与重定向（Qt6 的写法）
 *   超时：request.setTransferTimeout(std::chrono::milliseconds(800));   ← 别用 int 版
 *         默认 QNetworkRequest::DefaultTransferTimeoutConstant = 30000（30 秒）
 *         NAM 上也能设全局（第 131 行），单个请求的 set 优先。
 *   注意：transferTimeout 只管「传输不动了」，**连接阶段卡住**要靠操作系统超时；
 *         所以主流程还要有自己的看门狗定时器（本讲那个 12 秒 singleShot）。
 *   重定向：默认 NoLessSafeRedirectPolicy（自动跟，但只允许「不比原来更安全」的跳转）；
 *         ManualRedirectPolicy 停在 3xx，自己读 RedirectionTargetAttribute 决定；
 *         SameOriginRedirectPolicy 只跟同源；UserVerifiedRedirectPolicy 发 redirected
 *         等你 emit redirectAllowed()（**本讲没用**：要人点一下的演示不符合无人值守）。
 *         Qt5 的 NoRedirects / NoLessSafeRedirect 之外的老名字、RedirectionType 枚举
 *         都已不在 Qt6 头文件里。
 *
 * 五、本讲最容易踩的六个坑
 *   ① 每次请求 new 一个 NAM —— 连接池全废，Cookie/缓存存不住，代理配置各走各的。
 *   ② QUrl 用 QString 拼（"C:/a.txt" 变成 scheme="c"；带中文/& 的 query 直接散架）。
 *   ③ finished 里忘了 deleteLater —— 内存稳定泄漏，且连接不释放。
 *   ④ 只 connect(errorOccurred) 做清理，正常路径漏收尾（或反过来把 error 当成功）。
 *   ⑤ 用 waitForFinished / 嵌套 QEventLoop 把异步「掰成」同步 —— 见证据⑤两种死法。
 *   ⑥ 端口硬编码 8080 且连公网 —— CI/断网机器上必挂；本讲一律回环 + 端口 0。
 *
 * ——— 老师傅的话 ———
 *   徒弟，网络代码的功夫，一半在「你会不会等」。
 *   新手写完 get() 就着急读状态码，读到 0 就去查服务器是不是坏了——服务器好着呢，
 *   是你把异步的事当同步的活儿来干了。证据②那一行「isRunning = true，状态码 = 0」
 *   就是这一讲的门牌：**发出去的那一刻，你手里只有一个还没长成的对象，不是一个答案**。
 *
 *   另一半功夫在「一个 NAM」。我在项目里见过最离谱的是每个按钮槽里
 *   `QNetworkAccessManager mgr;` 发一个请求——看着干净，代价是每次重新建连接、
 *   Cookie 存不住、代理配置形同虚设，而且栈对象析构时它的 reply 还在飞，
 *   于是拿到一堆莫名奇妙的 OperationCanceledError。
 *   NAM 是「管家」，不是「一次性手套」：一个应用请一个，用到程序结束。
 *
 *   还有一条我盯得最严：**别把异步掰成同步**。
 *   那些「用 QEventLoop 假装同步」的封装，我见过它把一个团队的界面卡成幻灯片，
 *   还顺手制造了两个只在周五晚上出现的崩。你要「等」，就把「等」做成状态：
 *   发请求 → 存 pending → finished 里落状态 → 谁关心谁问。
 *   这比一个假装的返回值好维护十倍（第 2 讲我们就把这个「层」正经写出来）。
 *
 *   最后：所有网络演示都给我在回环上跑。
 *   拿别人的服务器当你的教学靶子，一是人家关了你就挂，二是你在往公网发无意义的包。
 *   自己 listen(QHostAddress::LocalHost, 0) 起一个，端口让内核挑，
 *   这份代码在断网的机器上、在 CI 上、在半夜三点的笔记本上都一样能跑——这才叫示例。
 *
 * ——— 小练习 ———
 *   1. 把本讲的 reply 收尾改成 nam->setAutoDeleteReplies(true)，然后**故意**在
 *      finished 之后再读一次 reply->error()，看程序给你什么（这就是「所有权交出去之后
 *      就别再碰它」的代价）。改完再解释：为什么本讲仍然推荐自己 deleteLater？
 *   2. 把 /stall 路径的超时从 800ms 改成 30000ms（默认值），观察 12 秒看门狗如何接管，
 *      并把「请求级超时」和「看门狗」的分工写成三行注释放在 main 里。
 *   3. 给 A 组请求改成 SameOriginRedirectPolicy，并把服务器 302 的 Location 从
 *      "/api/device" 改成 "http://127.0.0.2:1/x"，比较跟随结果与 B 组的差别，
 *      说明「同源」判断到底比的是哪几个字段（scheme / host / port）。
 *   4. 用 sendCustomRequest(req, "OPTIONS") 让服务器回一组 Allow 头（自己加一条路由），
 *      在客户端把 rawHeaderPairs() 全打出来，看看 Qt6 里成对的头是什么容器类型。
 */
// ============================================================================

// 本文件有 Q_OBJECT 类（LoopbackHttpServer），AUTOMOC 会生成并在这里包含进来。
#include "01_network_access_manager.moc"
