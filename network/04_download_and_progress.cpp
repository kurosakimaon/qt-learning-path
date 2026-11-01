// ============================================================================
// 第 4 讲 · 下载任务的状态机：进度、Range、落盘与取消
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   前三讲把「发请求、收响应」跑通了，这一讲把它做成**一个能扛事的下载任务**：
//     1) downloadProgress(bytesReceived, bytesTotal) 的三个分支必须分开处理：
//        bytesTotal > 0 正常算百分比；**bytesTotal == -1 是「长度未知」**
//        （服务器没发 Content-Length，用关闭连接划线），别拿它去做除法；
//        bytesTotal == 0 是「空文件」，直接当 100% 完成，不是除零事故。
//        ★但 -1 有两个本机实测出来的前提，很多人栽在这儿：
//        ① **进度信号被节流**：qnetworkreply.cpp:15 里
//           `progressSignalInterval = 100`，qnetworkreplyhttpimpl.cpp:1252~1257 只有
//           距上次超过 100ms 才 emit。回环上 1 MiB 一坨到手、100ms 内就下完了，
//           **中途那次 -1 根本不存在**，只有收尾那一次。
//        ② **收尾那次 Qt 会改口**：qnetworkreplyhttpimpl.cpp:2240~2244，
//           totalSize==-1 时发的是 `downloadProgress(received, received)`——
//           也就是「未知」在最后一帧变成了「已收 = 总长」。
//           所以 UI 不能死等 total==-1 才画忙碌态，也不能指望它一直保持 -1。
//     2) 响应头的两种读法（逐个对过 qnetworkreply.h）：
//        「熟头」reply->header(QNetworkRequest::ContentTypeHeader / ContentLengthHeader /
//        LastModifiedHeader)（KnownHeaders 枚举在 qnetworkrequest.h 第 33~48 行，
//        header() 在 qnetworkreply.h 第 100 行）；「生头」rawHeader / rawHeaderList /
//        rawHeaderPairs（第 106~114 行），Range 场景要读的 Content-Range、Accept-Ranges
//        只存在于生头里。⚠ 网上老代码常见的 QNetworkReply::RawHeaderAvailable 信号
//        **在本机 Qt 6.12 头文件里不存在**（整个 QtNetwork 目录 grep 零命中），
//        本讲改用查证存在的正道：**metaDataChanged 之后、finished 里读 rawHeaderList()**。
//     3) 断点续传 = 请求头 "Range: bytes=a-b"（setRawHeader，qnetworkrequest.h 第 151 行）
//        + 判断状态码 206（HttpStatusCodeAttribute）与 Content-Range 回显；
//        本讲分两段下同一个文件，拼起来与整段下载**逐字节**比对。
//     4) **边下边写**：readyRead → reply->read(chunk) → QSaveFile::write。
//        攒到 finished 再 readAll() 的后果：8MB 的包在内存里放 8MB，1GB 的包直接
//        触发坏分配或被系统杀进程，而且进度条期间纹丝不动。本讲把「传输中在内存里
//        最多停过几字节」（bytesAvailable 峰值）与 readyRead 次数打出来。
//        ★实测给这句加了个限定：**边下边写不等于一定省内存**。/file 的 1 MiB 在回环上
//        一次就进齐，第一次 readyRead 时 bytesAvailable() 峰值 = 1048576 = 正文全长；
//        同一套 while 排空写法换成限速的 8 MiB 流，峰值只有 65536。
//        压住驻留靠的是「数据来得慢」或 setReadBufferSize，不是靠 read() 这个动作本身。
//        另一条更硬的实测：一次 readyRead **只 read(64KiB) 会把下载截断**
//        （1 MiB 只落 65536 字节），必须 while 读到空、finished 之后再兜底排空一次。
//     5) QSaveFile 保证「中断不留半个坏文件」：真身写临时文件，commit() 成功才改名；
//        abort 后不 commit，目标路径上**根本不会出现那个半截文件**（打印 exists()==false）。
//     6) 取消与生命周期：reply->abort()（qnetworkreply.h 第 127 行，public slot、纯虚）
//        之后**仍然会收到 finished()**，error() 变成 OperationCanceledError；
//        所以只能在 finished 之后 deleteLater——abort 当场 delete 就是 use-after-free。
//        ★两条本机实测出来的坑，都是「不声不响」型的：
//        ① 错误码是 **5**，不是网上常被引用的 41（qnetworkreply.h:33~48 数出来的，
//           号段 1~99 网络层 / 101~ 代理 / 201~ 内容 / 301~ 协议 / 401~ 服务器，41 不存在）。
//        ② **abort() 是同步的**：errorOccurred → finished → 你的 done 槽全部在
//           abort() 返回之前跑完。所以「先记状态再 abort」，别写
//           `clickedOk = dl.abortNow();` 然后在 finished 链路里读 clickedOk——
//           读到的必然是赋值前的旧值（本讲第一版就这么白判了一次红）。
//     7) 进度条与网络层解耦：网络层只发一个 Downloader::Snapshot 结构，
//        UI 侧（本讲用 QProgressBar）自己把「已知/未知/空」翻译成交互形态，
//        两边互不认识。
//
//   演示全流程（自动跑完、自己退出）：回环自建迷你 HTTP 服务器（支持 Range 与
//   每 10ms 一滴的限速流）→ ①整段下载到 QSaveFile 并 commit → ②无 Content-Length 的
//   「未知长度」分支 → ③空文件分支 → ④两段 Range 下载同一文件（各打印字节数/
//   状态码/Content-Range）→ ⑤拼接校验和 + 落盘尺寸 + 与整段下载逐字节相等 →
//   ⑥大文件下到 256KiB 时 abort，打印信号序列与「坏文件不存在」证据 →
//   汇总 → 关服务器 → quit。
//
// 对应构建目标名：network_04_download_and_progress
//   find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
//   add_executable(network_04_download_and_progress 04_download_and_progress.cpp)
//   target_link_libraries(network_04_download_and_progress PRIVATE Qt6::Widgets Qt6::Network)
//   set(CMAKE_AUTOMOC ON)                    # 本文件有 Q_OBJECT 类，需要 moc
//   # 手工核对时：moc 04_download_and_progress.cpp -o 04_download_and_progress.moc
//   #             文件末尾那句 #include "04_download_and_progress.moc" 就是给 AUTOMOC 的钩子
//   # 目标名已在仓库根 CMakeLists.txt「第玖阶段 · 网络（network/）」一节登记，构建方式见仓库根 README
//
// 运行前须知：
//   · **绝不访问公网**：示例在本机 127.0.0.1 上用 QTcpServer 自建迷你 HTTP 服务器，
//     listen(QHostAddress::LocalHost, 0) 让系统挑空闲端口，客户端只打自己这个端口。
//     全程断网（拔网线/关 Wi-Fi）也能跑完。
//   · 无人值守：QTimer::singleShot 串步骤，末尾 qInfo() 打印可核对证据后
//     QCoreApplication::exit(Ledger::exitCode())：每阶段的字节数 / 状态码 / 生头 /
//     commit 结果 / abort 信号序列都逐条核对，全对才返回 0，否则返回 1；
//     25 秒总闸一旦触发同样判红。没有任何 exec() 阻塞对话框、不需要鼠标键盘。
//   · 每阶段 6 秒超时兜底（到点 reply->abort() 并打印「超时」），外面还有一道
//     25 秒总闸：到点强制打印「超时」证据、关服务器并退出——网络卡死不会吊住进程。
//   · 落盘写到 QTemporaryDir，进程退出即回收。
//   · 图解页面：../visuals/network_async.html（异步请求状态机，本讲就是它的下载分支）；
//     信号到达时机见 ../visuals/startup_flow.html（事件循环与信号调度）。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，Windows，
//     QT_QPA_PLATFORM=offscreen；ctest 目标 network_04_download_and_progress）。
//     本机这一轮：【硬账】44/44 项核对通过、退出码 0（本机多轮重跑稳定，输出存档
//     runlogs/network_04_download_and_progress.log）。阶段②⑥那几个「节流窗口 / abort 时机」
//     的结论就是这么测出来的——改回推理版本会当场变红，别动。
// ============================================================================

#include <QApplication>                 // QtWidgets/qapplication.h
#include <QProgressBar>                 // QtWidgets/qprogressbar.h（UI 解耦演示，不 show）
#include <QNetworkAccessManager>        // QtNetwork/qnetworkaccessmanager.h
#include <QNetworkRequest>              // QtNetwork/qnetworkrequest.h
#include <QNetworkReply>                // QtNetwork/qnetworkreply.h
#include <QTcpServer>                   // QtNetwork/qtcpserver.h
#include <QTcpSocket>                   // QtNetwork/qtcpsocket.h
#include <QAbstractSocket>              // QtNetwork/qabstractsocket.h（errorOccurred 在这）
#include <QHostAddress>                 // QtNetwork/qhostaddress.h
#include <QSaveFile>                    // QtCore/qsavefile.h
#include <QFile>
#include <QByteArray>
#include <QByteArrayView>
#include <QVariant>
#include <QDateTime>
#include <QUrl>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QTemporaryDir>                // QtCore/qtemporarydir.h
#include <QDebug>

#include <cstdint>

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
// 一、签名核对记录（本机 D:\Qt\6.12.0\Src\qtbase\src\network\access，Qt 6.12.0；
//   安装版同名头文件 mingw_64\include\QtNetwork\qnetworkreply.h 行号一致，已对读）
// ============================================================================
// qnetworkreply.h：
//   第 33~48 行  enum NetworkError（Q_ENUM）：NoError=0、ConnectionRefused=1、
//                RemoteHostClosed=2、HostNotFound=3、Timeout=4、**OperationCanceled=5**、
//                SslHandshakeFailed=6 …… UnknownNetworkError=99；
//                号段：代理 101~、内容 201~、协议 301~、服务器 401~
//   （原本这里写的是 OperationCanceledError=41 —— 本机数过枚举，41 这个号根本不存在。
//     写死数字之前先数枚举，别背「好像是 41」。阶段⑥有两条硬账钉着这件事。）
//   第 100 行    QVariant header(QNetworkRequest::KnownHeaders header) const;    ← 熟头
//   第 106 行    bool hasRawHeader(QAnyStringView headerName) const;             ← 生头
//   第 107 行    QList<QByteArray> rawHeaderList() const;
//   第 111 行    QByteArray rawHeader(QAnyStringView headerName) const;
//                （QByteArray 版重载在 QT_NETWORK_REMOVED_SINCE(6,7) 宏里，Qt6 请直接用
//                  字符串字面量喂 QAnyStringView，本讲就是这么写的）
//   第 114 行    const QList<RawHeaderPair> &rawHeaderPairs() const;  ← RawHeaderPair=std::pair
//   第 118 行    QVariant attribute(QNetworkRequest::Attribute code) const;       ← 状态码走这
//   第 127 行    virtual void abort() = 0;                              ← public slot、纯虚
//   第 133 行    void metaDataChanged();     ← 熟头/生头在这之后可读
//   第 134 行    void finished();
//   第 135 行    void errorOccurred(QNetworkReply::NetworkError);       ← Qt6 用它，
//                不是 Qt5 的 error(...)（老写法抄过来编不过，本模块 1~3 讲同样注意）
//   第 145 行    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
//   ⚠ 该头文件里**没有** RawHeaderAvailable 信号，也**没有** downloadedByteCount()——
//     两者都是网络文章里的幻觉写法，本讲一律不用。
// qnetworkrequest.h：
//   第 33~48 行  enum KnownHeaders：ContentTypeHeader / ContentLengthHeader /
//                LastModifiedHeader / UserAgentHeader …（注意枚举属于 QNetworkRequest，
//                reply->header() 的参数类型就是它）
//   第 52/53 行  Attribute::HttpStatusCodeAttribute / HttpReasonPhraseAttribute
//   第 139 行    void setHeader(KnownHeaders, const QVariant&);
//   第 151 行    void setRawHeader(const QByteArray &headerName, const QByteArray &value);
//                ← Range 请求头只能走 setRawHeader（KnownHeaders 里没有 Range 这一项）
// qtcpserver.h：第 29 行 listen(const QHostAddress &address = Any, quint16 port = 0)；
//   第 30 行 close()；第 40 行 quint16 serverPort() const；第 41 行 serverAddress()；
//   第 48 行 QTcpSocket *nextPendingConnection()；第 69 行 newConnection() 信号。
// qhostaddress.h：第 46~54 行 enum SpecialAddress：**LocalHost 确认存在**（还有
//   LocalHostIPv6 / Any / AnyIPv4）；第 111 行 toString()；第 129 行 isLoopback()。
//   ⚠ QtNetwork 全套头文件里 grep 不到 toName()——网传「QHostAddress::toName() 反查」
//   在 Qt 6.12 不存在，反查请走第 5 讲的 QDnsLookup(PTR)。
// qsavefile.h：第 34 行 QSaveFile(const QString &name, QObject *parent = nullptr)；
//   第 45 行 bool open(OpenMode)；第 46 行 bool commit()；第 48 行 cancelWriting()；
//   第 71 行 close() 是 **private override**——别对 QSaveFile 调 close()，
//   commit/析构自己收尾（析构时未 commit 就自动删临时文件，这正是「坏文件不存在」的机制）。
// qabstractsocket.h：第 190 行 errorOccurred(QAbstractSocket::SocketError)；
//   第 81~90 行 SocketState（ConnectedState / ClosingState / UnconnectedState）。
// qnetworkaccessmanager.h：第 83 行 get(request)；第 139 行 finished(QNetworkReply*) 信号。
// ============================================================================

// ============================================================================
// 二、回环迷你 HTTP 服务器（只为演示；支持 Range 与背压限速流）
// ============================================================================
// 四条路由：
//   /file     1 MiB 确定性正文；带 Content-Length / Content-Type / Last-Modified /
//             Accept-Ranges；命中 Range 请求时回 206 + Content-Range（一次写完，不限速）
//   /empty    200 + Content-Length: 0        （bytesTotal==0 分支）
//   /nolength 不带 Content-Length、Connection: close，用关闭划线，正文**限速滴发**
//             （bytesTotal==-1 分支）
//   /big      8 MiB，正文**限速滴发**（给 UI 的「取消」留窗口）
//
// ★为什么要限速？这是本机实测逼出来的，不是编出来好看的：
//   Qt 把 downloadProgress **节流**到最快每 100ms 一次——
//     qnetworkreply.cpp:15   const int QNetworkReplyPrivate::progressSignalInterval = 100;
//     qnetworkreplyhttpimpl.cpp:1252~1257  只有 choke 到点才 emit downloadProgress
//   回环上 1 MiB 一坨到手，整条连接在 100ms 之内就结束了，于是**中途那次进度信号根本不存在**。
//   不限速的后果实测有两处：② 的 bytesTotal==-1 分支一次都没进过（unknownBranchHits=0），
//   ⑥ 的「收到 256KiB 再取消」第一次看到进度时正文已经全到齐、reply 早就不 running，
//   abort() 成了空操作。**限速不是造假，是把「真机上要 3 秒的下载」压缩成 1.3 秒，
//   让节流窗口真的跨过几次。**
class LoopbackHttpServer : public QObject
{
    Q_OBJECT

public:
    static constexpr qint64 kFileSize = 1024 * 1024;        // /file 正文大小
    static constexpr qint64 kBigSize  = 8 * 1024 * 1024;    // /big 正文大小
    static constexpr qint64 kChunk    = 64 * 1024;          // 限速流每滴一片
    static constexpr int    kPumpMs   = 10;                 // 每 10ms 一滴（>100ms 节流窗口）

    explicit LoopbackHttpServer(QObject *parent = nullptr) : QObject(parent)
    {
        m_payload = makePayload(kFileSize);
        connect(&m_server, &QTcpServer::newConnection,
                this, &LoopbackHttpServer::onNewConnection);
        m_pumpTimer.setInterval(kPumpMs);
        connect(&m_pumpTimer, &QTimer::timeout, this, &LoopbackHttpServer::pumpStreams);
    }

    bool start() { return m_server.listen(QHostAddress::LocalHost, 0); }  // 端口 0 = 系统挑空闲端口
    quint16 port() const { return m_server.serverPort(); }
    QString addressText() const { return m_server.serverAddress().toString(); }

    void stop()
    {
        m_server.close();
        m_pumpTimer.stop();
        m_streams.clear();              // 限速流就地作废，别让定时器再摸已 abort 的 socket
        for (QTcpSocket *s : std::as_const(m_live))
            s->abort();                     // 收尾纪律：服务器侧主动掐掉所有回环连接
        m_live.clear();
    }

    // 确定性正文：第 i 字节 = (i*31+7) 的低 8 位。内容与校验和都可复算。
    static QByteArray makePayload(qint64 n)
    {
        QByteArray b;
        b.resize(int(n));
        for (qint64 i = 0; i < n; ++i)
            b[int(i)] = char((i * 31 + 7) & 0xFF);
        return b;
    }
    static quint64 weakSum(QByteArrayView view)   // FNV-1a 变体，演示校验用，不是密码学
    {
        quint64 h = 1469598103934665603ull;
        for (char c : view)
            h = (h ^ quint8(c)) * 1099511628211ull;
        return h;
    }

private:
    void onNewConnection()
    {
        while (QTcpSocket *sock = m_server.nextPendingConnection()) {
            sock->setParent(this);
            m_live.append(sock);
            m_requestBuf.insert(sock, QByteArray());
            connect(sock, &QIODevice::readyRead, this, [this, sock]() { onRequestData(sock); });
            connect(sock, &QAbstractSocket::disconnected, this, [this, sock]() { dropSocket(sock); });
        }
    }

    void dropSocket(QTcpSocket *sock)
    {
        m_requestBuf.remove(sock);
        m_streams.remove(sock);
        m_live.removeAll(sock);
        sock->deleteLater();
    }

    void onRequestData(QTcpSocket *sock)
    {
        m_requestBuf[sock].append(sock->readAll());
        if (!m_requestBuf[sock].contains("\r\n\r\n"))
            return;                             // 请求头没收完（回环上通常一次到位）
        const QString path = requestedPath(m_requestBuf[sock]);
        const QByteArray lower = m_requestBuf[sock].toLower();

        if (path.startsWith(QStringLiteral("/big"))) {
            startStream(sock, 200, "OK", kBigSize, kBigSize);   // 带 Content-Length 的 8 MiB 限速流
            return;
        }
        if (path.startsWith(QStringLiteral("/empty"))) {
            writeWhole(sock, 200, "OK", QByteArray(), true, QString());
            return;
        }
        if (path.startsWith(QStringLiteral("/nolength"))) {
            // 故意**不发** Content-Length：长度只能靠「连接关闭」划线，而且正文一滴一滴发
            startStream(sock, 200, "OK", m_payload.size(), -1);
            return;
        }
        if (path.startsWith(QStringLiteral("/file"))) {
            const QByteArray rangeValue = findRangeBytes(lower);       // 例 "0-524287" / "524288-"
            if (!rangeValue.isEmpty()) {
                const qsizetype dash = rangeValue.indexOf('-');
                bool okA = false, okB = false;
                const qint64 a = dash > 0 ? rangeValue.left(dash).toLongLong(&okA) : -1;
                qint64 b = dash + 1 < rangeValue.size()
                               ? rangeValue.mid(dash + 1).toLongLong(&okB)
                               : m_payload.size() - 1;
                if (!okB)
                    b = m_payload.size() - 1;   // "a-" 开区间 = 到文件尾
                if (okA && a >= 0 && a < m_payload.size() && b >= a) {
                    if (b >= m_payload.size())
                        b = m_payload.size() - 1;
                    const QByteArray slice = m_payload.mid(int(a), int(b - a + 1));
                    const QString extra = QStringLiteral("Content-Range: bytes %1-%2/%3\r\n")
                                             .arg(a).arg(b).arg(m_payload.size());
                    writeWhole(sock, 206, "Partial Content", slice, true, extra);
                    return;
                }
            }
            writeWhole(sock, 200, "OK", m_payload, true, QString());
            return;
        }
        writeWhole(sock, 404, "Not Found", QByteArray(), true, QString());
    }

    static QString requestedPath(const QByteArray &request)
    {
        const QByteArray firstLine = request.left(request.indexOf("\r\n"));
        const QList<QByteArray> parts = firstLine.split(' ');
        return parts.size() >= 2 ? QString::fromLatin1(parts.at(1)) : QString();
    }

    static QByteArray findRangeBytes(const QByteArray &lowerRequest)
    {
        // 只在 toLower 后的整段请求里找小写标记，绕开大小写不敏感 contains 重载的兼容问题
        static const QByteArray mark = "range: bytes=";
        const qsizetype i = lowerRequest.indexOf(mark);
        if (i < 0)
            return QByteArray();
        const qsizetype lineEnd = lowerRequest.indexOf("\r\n", i);
        return lowerRequest.mid(i + mark.size(), lineEnd - i - mark.size()).trimmed();
    }

    void writeWhole(QTcpSocket *sock, int status, const char *reason,
                    const QByteArray &body, bool withLength, const QString &extraHeaders)
    {
        QString head = QStringLiteral("HTTP/1.1 %1 %2\r\n"
                                      "Server: qtdemo-loopback/1.0\r\n"
                                      "Date: Tue, 01 Sep 2026 08:00:00 GMT\r\n"
                                      "Content-Type: application/octet-stream\r\n"
                                      "Last-Modified: Mon, 31 Aug 2026 07:30:00 GMT\r\n"
                                      "Accept-Ranges: bytes\r\n")
                           .arg(status).arg(QString::fromLatin1(reason));
        if (withLength)
            head += QStringLiteral("Content-Length: %1\r\n").arg(body.size());
        head += QStringLiteral("Connection: close\r\n");   // 一律关连接：一个演示请求一条新连接，
                                                            // 免得 keep-alive 让下一条请求复用旧缓冲区
        head += extraHeaders;
        head += QStringLiteral("\r\n");
        sock->write(head.toLatin1());
        if (!body.isEmpty())
            sock->write(body);
        sock->disconnectFromHost();   // Qt 保证先把写缓冲冲干净再FIN
    }

    // 只发响应头，正文交给 m_pumpTimer 一滴一滴喂。
    // contentLengthHeader < 0 → 连 Content-Length 都不写（这就是「长度未知」那条路）。
    void startStream(QTcpSocket *sock, int status, const char *reason,
                     qint64 bodyTotal, qint64 contentLengthHeader)
    {
        QString head = QStringLiteral("HTTP/1.1 %1 %2\r\n"
                                      "Server: qtdemo-loopback/1.0\r\n"
                                      "Content-Type: application/octet-stream\r\n"
                                      "Accept-Ranges: bytes\r\n")
                           .arg(status).arg(QString::fromLatin1(reason));
        if (contentLengthHeader >= 0)
            head += QStringLiteral("Content-Length: %1\r\n").arg(contentLengthHeader);
        head += QStringLiteral("Connection: close\r\n\r\n");
        sock->write(head.toLatin1());
        m_streams.insert(sock, Stream{ bodyTotal, 0 });
        m_pumpTimer.start();
    }

    // 每 10ms 给每条在播的连接喂 64KiB。为什么不用 bytesWritten 驱动（背压）：
    // 背压只保证「不把写缓冲撑爆」，不保证**慢**——回环上 bytesWritten 连着秒回，
    // 8 MiB 照样在几十毫秒内倾完，节流的 downloadProgress 还是只发最后一次。
    void pumpStreams()
    {
        if (m_streams.isEmpty()) {
            m_pumpTimer.stop();
            return;
        }
        const QList<QTcpSocket *> socks = m_streams.keys();   // 快照：循环里会增删这张表
        for (QTcpSocket *sock : socks) {
            if (!m_streams.contains(sock))
                continue;
            if (sock->state() != QAbstractSocket::ConnectedState) {
                m_streams.remove(sock);                       // 对面已经 abort → 别再写
                continue;
            }
            Stream st = m_streams.value(sock);
            const qint64 n = qMin<qint64>(kChunk, st.total - st.sent);
            // 正文只有 1 MiB，8 MiB 的流就按 64KiB 对齐地循环取样（1MiB / 64KiB = 16 圈整）
            sock->write(m_payload.mid(int(st.sent % m_payload.size()), int(n)));
            st.sent += n;
            if (st.sent >= st.total) {
                m_streams.remove(sock);
                sock->disconnectFromHost();                   // 发完再划线：Qt 会先冲写缓冲
            } else {
                m_streams.insert(sock, st);
            }
        }
    }

    struct Stream { qint64 total; qint64 sent; };

    QTcpServer m_server;
    QByteArray m_payload;
    QList<QTcpSocket *> m_live;
    QHash<QTcpSocket *, QByteArray> m_requestBuf;
    QHash<QTcpSocket *, Stream> m_streams;
    QTimer m_pumpTimer;
};

// ============================================================================
// 三、下载器 = 网络层。对外只发一个 Snapshot 结构，完全不认识任何 UI 控件
// ============================================================================
class Downloader : public QObject
{
    Q_OBJECT

public:
    // 「进度快照」：网络层算好，UI 层翻成人话。解耦点就这一个结构。
    struct Snapshot {
        qint64 received = 0;
        qint64 total = -1;
        bool totalKnown = false;      // bytesTotal > 0；==0 走空文件特判；==-1 未知
        bool emptyFile = false;
        int percent = -1;             // 未知时 -1：UI 自己决定画成忙碌态，别做除法
    };

    // ── 证据读数（finished 时冻结，deleteLater 之后就读不到了）──
    Snapshot snap;
    QVariant contentType, contentLength, lastModified;      // 熟头
    QByteArray contentRangeRaw, acceptRangesRaw;            // 生头（Range 场景的证据）
    QStringList rawHeaderNames;
    QStringList signalLog;            // 按到达顺序记录：metaDataChanged / progress次数 / errorOccurred / finished
    int status = 0;
    QString reason;
    bool committed = false;
    quint64 peakInFlight = 0;         // 传输中 reply->bytesAvailable() 的峰值（内存驻留证据）
    int readyReadCount = 0;
    int unknownBranchHits = 0;        // bytesTotal==-1 分支被走过几次
    bool abortDidCut = false;         // 上一次 abortNow() 有没有真掐（先记账再 abort）

    explicit Downloader(QNetworkAccessManager *nam, QObject *parent = nullptr)
        : QObject(parent), m_nam(nam)
    {
        m_deadline.setSingleShot(true);
        connect(&m_deadline, &QTimer::timeout, this, [this]() {
            if (m_reply) {
                qWarning() << "  [兜底] 本阶段" << m_deadlineMs
                           << "ms 未完成 → 强制 reply->abort()（下面信号序列里能看到 finished 照样来）";
                signalLog << QStringLiteral("deadline-timeout → abort()");
                m_reply->abort();
            }
        });
    }

    void start(const QUrl &url, const QString &destPath,
               const QByteArray &rangeHeader = QByteArray(), int deadlineMs = 6000)
    {
        snap = Snapshot();
        signalLog.clear();
        rawHeaderNames.clear();
        contentType = contentLength = lastModified = QVariant();
        contentRangeRaw.clear();
        acceptRangesRaw.clear();
        status = 0; reason.clear(); committed = false;
        peakInFlight = 0; readyReadCount = 0; unknownBranchHits = 0;
        abortDidCut = false;

        m_file.setFileName(destPath);
        if (!m_file.open(QIODevice::WriteOnly)) {
            qWarning() << "  QSaveFile 打不开：" << destPath << "→ 本阶段直接判失败";
            emit done(destPath);
            return;
        }
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("qt-learning-path/network_04"));
        if (!rangeHeader.isEmpty())
            req.setRawHeader("Range", rangeHeader);         // KnownHeaders 没有 Range，只能生头
        m_reply = m_nam->get(req);

        // ① 边下边写：readyRead 一到就**把这一轮到手的字节全部取走**，每片最多 64KiB 落盘。
        // ★本机实测（Qt 6.12.0 / MinGW / Windows）踩到的真 bug，就出在「一次 readyRead 只读一片」：
        //   回环上数据来得太快，一次 readyRead 到手时 bytesAvailable() 常常已经堆了一大坨
        //   （峰值实测 524288 / 393216 字节，正是「攒内存」的那个反例）。
        //   一次只 read(64KiB) 的话，剩下的字节就一直压在 reply 的读缓冲里，
        //   而 Qt 在数据到手结束时**只发一次 readyRead**、紧接着就发 finished——
        //   于是 1 MiB 只落了 65536 字节，②落 589824、④各落 196608，全部截断。
        //   规矩：**readyRead 里要 while 读到空**；finished 之后还要再兜底读一次，
        //   因为「最后一个 readyRead」和「最后一个字节」不是同一件事。
        connect(m_reply, &QIODevice::readyRead, this, [this]() {
            while (m_reply->bytesAvailable() > 0) {
                // 峰值要在**取走之前**量：bytesAvailable() 才是「此刻在内存里排着的字节数」
                peakInFlight = qMax(peakInFlight, quint64(m_reply->bytesAvailable()));
                const QByteArray chunk = m_reply->read(64 * 1024);
                if (chunk.isEmpty())
                    break;                     // 防御：read 给空就别在这儿死循环
                m_file.write(chunk);
            }
            ++readyReadCount;
        });
        // ② 进度三分支：>0 算百分比；==-1 未知；==0 空文件
        connect(m_reply, &QNetworkReply::downloadProgress, this,
                [this](qint64 bytesReceived, qint64 bytesTotal) {
            snap.received = bytesReceived;
            snap.total = bytesTotal;
            if (bytesTotal < 0) {                    // 长度未知：**不做除法**
                snap.totalKnown = false;
                snap.percent = -1;
                ++unknownBranchHits;
            } else if (bytesTotal == 0) {            // 空文件：不是除零，是直接完成
                snap.totalKnown = true;
                snap.emptyFile = true;
                snap.percent = 100;
            } else {
                snap.totalKnown = true;
                snap.percent = int(qMin<qint64>(bytesReceived * 100 / bytesTotal, 100));
            }
            emit snapshot(snap);                     // ← UI 只听这一个信号
        });
        connect(m_reply, &QNetworkReply::metaDataChanged, this, [this]() {
            signalLog << QStringLiteral("metaDataChanged");
        });
        connect(m_reply, &QNetworkReply::errorOccurred, this,
                [this](QNetworkReply::NetworkError e) {
            m_error = e;
            signalLog << QStringLiteral("errorOccurred(code=%1)").arg(int(e));
        });
        connect(m_reply, &QNetworkReply::finished, this, [this, destPath]() {
            // ★这一行原本是个静默的取证 bug：格式串只有 %1/%2 两个槽，却链了三个 .arg()。
            //   结果 .arg(int(m_error)) 把**错误码**填进了 isFinished 的位置，
            //   第三个 .arg(isFinished) 无处可填，Qt 只会吐一句
            //   "QString::arg: Argument missing"（本机实测真吐了），而日志上那个 isFinished=0
            //   其实是 error 码 0，不是「没结束」。取证数字错得不声不响——所以槽位要和参数对齐。
            signalLog << QStringLiteral("finished(error=%1, code=%2, isFinished=%3)")
                             .arg(m_error == QNetworkReply::NoError ? QStringLiteral("NoError")
                                  : m_error == QNetworkReply::OperationCanceledError
                                        ? QStringLiteral("OperationCanceledError")
                                  : QStringLiteral("code%1").arg(int(m_error)))
                             .arg(int(m_error))
                             .arg(m_reply->isFinished() ? 1 : 0);
            m_deadline.stop();
            status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            reason = m_reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
            // —— 头信息在这里冻结（deleteLater 之后就没了）——
            contentType = m_reply->header(QNetworkRequest::ContentTypeHeader);
            contentLength = m_reply->header(QNetworkRequest::ContentLengthHeader);
            lastModified = m_reply->header(QNetworkRequest::LastModifiedHeader);
            rawHeaderNames.clear();
            const QList<QByteArray> names = m_reply->rawHeaderList();
            for (const QByteArray &n : names)
                rawHeaderNames << QString::fromLatin1(n);
            if (m_reply->hasRawHeader("Content-Range"))
                contentRangeRaw = m_reply->rawHeader("Content-Range");
            if (m_reply->hasRawHeader("Accept-Ranges"))
                acceptRangesRaw = m_reply->rawHeader("Accept-Ranges");
            const bool ok = (m_error == QNetworkReply::NoError);
            // ★兜底排空：finished 之后 reply 里**还可能**躺着没被取走的字节（本机实测就是这样，
            //   不兜这一手，最后一段正文就跟着 reply 一起没了，落盘永远短一截）。
            //   这也是「边下边写」和「finished 再 readAll」的真正分界：
            //   边下边写不代表 finished 时不用收尾。
            while (m_reply->bytesAvailable() > 0) {
                peakInFlight = qMax(peakInFlight, quint64(m_reply->bytesAvailable()));
                const QByteArray tail = m_reply->read(64 * 1024);
                if (tail.isEmpty())
                    break;
                m_file.write(tail);
            }
            committed = ok && m_file.commit();       // 只有成功才 commit → 改名成目标文件
            if (!ok)
                m_file.cancelWriting();              // 中断：临时文件就地销毁，目标路径干干净净
            signalLog << QStringLiteral("commit=%1").arg(committed);
            emit done(destPath);
            m_reply->deleteLater();  // 纪律：abort 后仍会来 finished，所以只能在 finished 后放手
            m_reply = nullptr;
        });
        m_deadlineMs = deadlineMs;
        m_deadline.start(m_deadlineMs);
    }

    // 返回「有没有真掐」。reply 已经不 running 时 abort() 是**空操作**：
    // 取消按钮点了个寂寞，还不报错——这种静默失效比崩溃难查十倍，所以把它做成显式状态记账。
    // ★记账必须**在 abort() 之前**：abort() 是同步的，errorOccurred → finished → done
    //   整条链会在本函数 return 之前跑完（本机实测：拿返回值往外部变量里传，
    //   检查时读到的还是赋值前的旧值，白判一次红）。
    bool abortNow()
    {
        abortDidCut = (m_reply && m_reply->isRunning());
        if (abortDidCut) {
            signalLog << QStringLiteral("外部调用 abort()");
            m_reply->abort();
            return true;
        }
        signalLog << QStringLiteral("外部调用 abort()→ 但 reply 已不 running，空操作");
        return false;
    }
    bool replyRunning() const { return m_reply && m_reply->isRunning(); }

signals:
    void snapshot(const Downloader::Snapshot &s);           // 网络层 → UI 的唯一通道
    void done(const QString &destPath);

private:
    QNetworkAccessManager *m_nam;
    QNetworkReply *m_reply = nullptr;
    QSaveFile m_file;
    QTimer m_deadline;
    int m_deadlineMs = 6000;
    QNetworkReply::NetworkError m_error = QNetworkReply::NoError;
};

// ============================================================================
// 四、小工具
// ============================================================================
static QByteArray readWholeFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll();                       // 这里 readAll 合理：演示结束后做比对用
}

// ============================================================================
// 五、main：六个阶段的状态机（全部 QTimer 串起来，无人值守）
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("network_04"));

    QTemporaryDir dir;
    LoopbackHttpServer server;
    QNetworkAccessManager nam;
    Downloader dl(&nam);
    QProgressBar bar;                          // 逻辑存在、不 show：证明解耦不需要真窗口

    if (!dir.isValid() || !server.start()) {
        qCritical() << "初始化失败（临时目录或 127.0.0.1 监听）";
        return 1;
    }
    const QUrl base = QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.port()));
    qInfo() << "================================================================";
    qInfo() << "network 第 4 讲 · 下载状态机（Qt =" << QT_VERSION_STR << "）";
    qInfo() << "回环服务器：" << base.toString()
            << "｜serverAddress =" << server.addressText()
            << "isLoopback =" << QHostAddress(server.addressText()).isLoopback();
    qInfo() << "全程只打自己这个端口，不出公网，断网可跑。";
    qInfo() << "================================================================";

    // —— UI 侧：只听 Snapshot，把三种情况翻成三种交互形态 ——
    int barBusyHits = 0;              // 忙碌态（range 0..0）真的出现过几次
    int barKnownHits = 0;             // 确定态（range 0..100）几次
    QObject::connect(&dl, &Downloader::snapshot, &bar,
                     [&bar, &barBusyHits, &barKnownHits](const Downloader::Snapshot &s) {
        if (!s.totalKnown) {
            bar.setRange(0, 0);                       // 未知长度 → 忙碌态（跑马灯）
            ++barBusyHits;
        } else {
            bar.setRange(0, 100);
            bar.setValue(s.percent);
            ++barKnownHits;
        }
    });

    // —— 总闸：25 秒到点强制收摊，网络卡死也退得出 ——
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [&]() {
        qWarning() << "【超时】总闸 25s 到点 → 关服务器并退出（这就是「超时」证据）";
        server.stop();
        // 正常流水线跑完会把 watchdog 停掉；走到这里说明某阶段卡住了 → **判红**
        checkBool("六个阶段本该在 25 秒内跑完（总闸不该被触发）", false);
        qInfo().noquote() << QStringLiteral("\n【硬账】%1/%2 项核对通过 → 退出码 %3")
                                 .arg(Ledger::passed).arg(Ledger::total).arg(Ledger::exitCode());
        QCoreApplication::exit(Ledger::exitCode());
    });
    watchdog.start(25000);

    QElapsedTimer total;
    total.start();

    const QString pFull = dir.filePath(QStringLiteral("full.bin"));
    const QString pNL   = dir.filePath(QStringLiteral("nolength.bin"));
    const QString pEmpty= dir.filePath(QStringLiteral("empty.bin"));
    const QString pS1   = dir.filePath(QStringLiteral("seg1.bin"));
    const QString pS2   = dir.filePath(QStringLiteral("seg2.bin"));
    const QString pBig  = dir.filePath(QStringLiteral("should_not_exist.bin"));

    int stage = 0;
    bool abortFired = false;
    quint64 fullSum = 0;

    // —— 取消演示：阶段 5 里进度到 256KiB 就 abortNow（网络层自己不发火，UI 只旁观）——
    // ★第一版这里点了个寂寞：/big 不限速，回环上 8 MiB 在 100ms 节流窗口里就倾完了，
    //   第一次（也是唯一一次）snapshot 到手时 received 已经等于 8388608、reply 早就不 running，
    //   abort() 成了空操作 → errorOccurred(41) 根本没出现过、「半截文件不存在」也纯属巧合。
    //   改成服务器限速后才真的掐在流中间——**取消能中断的前提是下载还没结束**。
    bool abortRunningAtClick = false;
    QObject::connect(&dl, &Downloader::snapshot, [&](const Downloader::Snapshot &s) {
        if (stage == 5 && !abortFired && s.received >= 256 * 1024) {
            abortFired = true;
            abortRunningAtClick = dl.replyRunning();
            qInfo() << "  [阶段⑥] 已收" << s.received << "字节（共 8 MiB）→ 用户点「取消」了"
                    << "| 点之前 reply->isRunning() =" << abortRunningAtClick;
            dl.abortNow();
        }
    });

    QObject::connect(&dl, &Downloader::done, [&](const QString &path) {
        Q_UNUSED(path)
        qInfo().noquote() << QStringLiteral("  信号序列：%1").arg(dl.signalLog.join("  →  "));
        qInfo() << "  头快照：contentType =" << dl.contentType.toString()
                << "| contentLength =" << dl.contentLength.toLongLong()
                << "| lastModified =" << dl.lastModified.toDateTime().toString(Qt::ISODate);
        qInfo() << "  生头列表(" << dl.rawHeaderNames.size() << "个)：" << dl.rawHeaderNames;
        qInfo() << "  边下边写：readyRead" << dl.readyReadCount << "次，传输中内存驻留峰值 ="
                << dl.peakInFlight << "字节（峰值量的是「取走之前排在 reply 缓冲里的字节数」；"
                "整包到手的 ① 与限速滴发的 ②⑥ 正好是一组对照）";

        auto nextStage = [&](int next) {
            QTimer::singleShot(30, [&, next]() {
                switch (next) {
                case 1: dl.start(QUrl(base.toString() + QStringLiteral("/nolength")), pNL); break;
                case 2: dl.start(QUrl(base.toString() + QStringLiteral("/empty")), pEmpty); break;
                case 3: dl.start(QUrl(base.toString() + QStringLiteral("/file")), pS1,
                                  QByteArray("bytes=0-524287")); break;
                case 4: dl.start(QUrl(base.toString() + QStringLiteral("/file")), pS2,
                                  QByteArray("bytes=524288-")); break;
                case 5: dl.start(QUrl(base.toString() + QStringLiteral("/big")), pBig,
                                  QByteArray(), 10000); break;
                }
            });
        };

        switch (stage) {
        case 0: {   // —— 阶段① 整段下载 ——
            const QByteArray body = readWholeFile(pFull);
            fullSum = LoopbackHttpServer::weakSum(body);
            qInfo() << "  [阶段①] 状态码 =" << dl.status
                    << "| 落盘 =" << body.size() << "字节 | committed =" << dl.committed
                    << "| 进度终值 percent =" << dl.snap.percent << "bar->value() =" << bar.value()
                    << "| 校验和 =0x" + QString::number(fullSum, 16);
            check("① 状态码 200", dl.status, 200);
            check("① 落盘字节数 = 1 MiB", body.size(), LoopbackHttpServer::kFileSize);
            checkBool("① QSaveFile commit 成功（目标文件是真身，不是临时文件）", dl.committed);
            check("① 进度终值 percent", dl.snap.percent, 100);
            check("① 进度条被 Snapshot 推到 100", bar.value(), 100);
            check("① 熟头 Content-Length", dl.contentLength.toLongLong(), LoopbackHttpServer::kFileSize);
            checkBool("① 熟头 Content-Type = application/octet-stream",
                      dl.contentType.toString() == QStringLiteral("application/octet-stream"));
            checkBool("① 熟头 Last-Modified 被解析成合法日期", dl.lastModified.toDateTime().isValid());
            checkBool("① 生头 Accept-Ranges = bytes", dl.acceptRangesRaw == "bytes");
            checkBool("① readyRead 至少触发 1 次（边下边写真的发生过）", dl.readyReadCount >= 1);
            // ★这条期望原本写的是「峰值 < 正文总量」，本机实测**不成立**：/file 的 1 MiB 在
            //   回环上一坨进缓冲，Qt 只发一次 readyRead，取走之前 bytesAvailable() 就是整包。
            //   所以「边下边写」在快网络上**并不自动省内存**——想真的压住驻留，要么网络本来就慢
            //   （对照 ②⑥ 的限速流），要么 setReadBufferSize 把缓冲卡住、逼 Qt 分次喂。
            //   与其把这条改成永远绿的废话，不如把「整包压在内存里」这个反例钉成硬账。
            checkBool("① 整包到手：驻留峰值到了正文的量级（≥ 一半）= 攒内存的反例",
                      dl.peakInFlight >= quint64(LoopbackHttpServer::kFileSize / 2));
            checkBool("① 峰值不超过正文（reply 只存正文，没把它复制两份）",
                      dl.peakInFlight <= quint64(LoopbackHttpServer::kFileSize));
            break;
        }
        case 1: {   // —— 阶段② 未知长度分支 ——
            const QByteArray body = readWholeFile(pNL);
            qInfo() << "  [阶段②] bytesTotal 分支：未知(-1)命中" << dl.unknownBranchHits
                    << "次 | 忙碌态" << barBusyHits << "次、确定态" << barKnownHits
                    << "次 | snap 终值 total =" << dl.snap.total
                    << "== received =" << dl.snap.received
                    << "（Qt 收尾时把「未知」改口成「已收」，见 qnetworkreplyhttpimpl.cpp:2240）";
            qInfo() << "           落盘 =" << body.size() << "字节，驻留峰值 =" << dl.peakInFlight
                    << "字节（限速流：一次 readyRead 只到手一两片）| 校验和一致 ="
                    << (LoopbackHttpServer::weakSum(body) == fullSum);
            check("② 状态码 200", dl.status, 200);
            checkBool("② bytesTotal == -1 的分支被走过", dl.unknownBranchHits >= 1);
            // ★「未知就是未知」只在**传输中**成立，收尾那一刻 Qt 会改口：
            //   qnetworkreplyhttpimpl.cpp:2240~2244 —— totalSize == -1 时
            //   emit downloadProgress(bytesDownloaded, bytesDownloaded)，
            //   也就是**最后一次**快照的 total 等于 received，不是 -1。
            //   指望 snap.total 一路保持 -1 是错的读法；UI 若死等 -1，进度条永远停在忙碌态。
            check("② 收尾时 Qt 把未知的 total 改口成 received（实测，不是 -1）",
                  dl.snap.total, dl.snap.received);
            checkBool("② 进度条真翻过忙碌态（Snapshot.totalKnown=false → range 0..0）",
                      barBusyHits >= 1);
            check("② 忙碌态之后又翻回确定态：终值 range 上界 = 100", bar.maximum(), 100);
            check("② 无 Content-Length 也照样把 1 MiB 落全", body.size(), LoopbackHttpServer::kFileSize);
            checkBool("② 落盘内容与阶段①整段下载逐字节一致",
                      LoopbackHttpServer::weakSum(body) == fullSum);
            checkBool("② 未知长度同样 commit 成功", dl.committed);
            break;
        }
        case 2: {   // —— 阶段③ 空文件分支（0 不是除零事故）——
            QFile f(pEmpty);
            qInfo() << "  [阶段③] 空文件：committed =" << dl.committed
                    << "| exists =" << f.exists() << "| size =" << f.size()
                    << "| snap: totalKnown =" << dl.snap.totalKnown
                    << "emptyFile =" << dl.snap.emptyFile << "percent =" << dl.snap.percent
                    << "（total==0 走「直接完成」，不是除零；没触发 progress 回调也算完成，看 finished）";
            const bool emptyBranch = dl.snap.totalKnown && dl.snap.emptyFile && dl.snap.percent == 100;
            const bool noProgress = !dl.snap.totalKnown && dl.snap.percent == -1;
            check("③ 状态码 200", dl.status, 200);
            checkBool("③ 空文件也 commit、目标文件存在且 0 字节",
                      dl.committed && f.exists() && f.size() == 0);
            // 两种实现都合法：框架发了 progress(0,0) → 走空文件分支；一次 progress 都没有 →
            // Snapshot 保持初值。**任何一条上都不许出现除零/负百分比**，这里就是这条硬账。
            checkBool("③ 空文件不产生除零：要么 percent=100，要么根本没发 progress",
                      emptyBranch || noProgress);
            break;
        }
        case 3: {   // —— 阶段④ Range 第一段 ——
            const QByteArray body = readWholeFile(pS1);
            qInfo() << "  [阶段④a] 状态码 =" << dl.status << "（206=Partial Content）"
                    << "| Content-Range 生头回显 =" << QString::fromLatin1(dl.contentRangeRaw)
                    << "| 本段落盘 =" << body.size() << "字节（应为 524288）"
                    << "| 校验和 =0x" + QString::number(LoopbackHttpServer::weakSum(body), 16);
            check("④a 状态码 206（Partial Content）", dl.status, 206);
            checkBool("④a Content-Range 生头回显正确",
                      dl.contentRangeRaw == "bytes 0-524287/1048576");
            check("④a 本段落盘 512 KiB", body.size(), 524288);
            // ★rawHeaderList() 吐出来的名字是**全小写**的（本机实测：server / date /
            //   content-type / last-modified / accept-ranges / content-length / connection /
            //   content-range）。所以按「Content-Range」这个驼峰写法去 contains 一定 false。
            //   hasRawHeader()/rawHeader() 是大小写不敏感的，list 不是——这一对差别值得记。
            checkBool("④a rawHeaderList 里能看到 Content-Range（按小写名字找）", dl.rawHeaderNames.contains(
                                                                   QStringLiteral("content-range")));
            // 顺手把「列表是全小写」这条钉成硬账，免得以后有人把它改回驼峰又「测过了」：
            checkBool("④a rawHeaderList 里**没有**驼峰写法（证明它就是全小写）",
                      !dl.rawHeaderNames.contains(QStringLiteral("Content-Range")));
            checkBool("④a Range 请求同样 commit 成功", dl.committed);
            break;
        }
        case 4: {   // —— 阶段④b Range 第二段 + 拼接总账 ——
            const QByteArray s2 = readWholeFile(pS2);
            QByteArray joined = readWholeFile(pS1);
            joined.append(s2);
            const QByteArray whole = readWholeFile(pFull);
            qInfo() << "  [阶段④b] 第二段 =" << s2.size() << "字节 | 状态码 =" << dl.status
                    << "| Content-Range =" << QString::fromLatin1(dl.contentRangeRaw);
            qInfo() << "  [阶段⑤] 拼接校验：两段之和" << (524288 + s2.size())
                    << "vs 整段" << whole.size()
                    << "| 逐字节相等 =" << (joined == whole)
                    << "| 拼接校验和 =0x" + QString::number(LoopbackHttpServer::weakSum(joined), 16)
                    << "vs 整段 0x" + QString::number(fullSum, 16);
            check("④b 第二段状态码 206", dl.status, 206);
            checkBool("④b 第二段的 Content-Range 从 524288 起、到文件尾",
                      dl.contentRangeRaw == "bytes 524288-1048575/1048576");
            check("④b 第二段落盘 512 KiB", s2.size(), 524288);
            check("⑤ 拼接结果 = 1 MiB", joined.size(), LoopbackHttpServer::kFileSize);
            checkBool("⑤ 断点续传两段拼接后与整段下载**逐字节相等**", joined == whole);
            checkBool("⑤ 拼接校验和等于整段校验和",
                      LoopbackHttpServer::weakSum(joined) == fullSum);
            break;
        }
        case 5: {   // —— 阶段⑥ abort：取消之后信号与生命周期 ——
            QFile f(pBig);
            qInfo() << "  [阶段⑥] abort 现场：上面序列里应有 errorOccurred(code=5)="
                    << "OperationCanceledError 且 finished 照样到达；abort 时状态码读数 ="
                    << dl.status;
            qInfo() << "           committed =" << dl.committed
                    << "| 目标路径 exists =" << f.exists()
                    << "← QSaveFile 没 commit，半截坏文件**根本不存在**，这就是落盘纪律";
            qInfo() << "           reply 在 finished 之后才 deleteLater（对象生命周期最后一跳）";
            const QString log = dl.signalLog.join(QStringLiteral("  →  "));
            checkBool("⑥ 取消确实由「进度到 256KiB」触发", abortFired);
            // ★勘误：这一讲原本写的是 code=41。本机对着头文件数过——
            //   qnetworkreply.h:33~48  NoError=0，网络层错误从 1 起顺序排：
            //   1 ConnectionRefused / 2 RemoteHostClosed / 3 HostNotFound / 4 Timeout /
            //   **5 OperationCanceledError**，直到 UnknownNetworkError=99；
            //   代理 101~、内容 201~、协议 301~、服务器 401~。41 这个号根本不存在。
            //   写死数字之前先数枚举，别背「好像是 41」。
            checkBool("⑥ errorOccurred(OperationCanceledError=5) 出现过",
                      log.contains(QStringLiteral("errorOccurred(code=5)")));
            checkBool("⑥ 码表 41 在序列里根本没出现（把勘误钉住，防止又改回去）",
                      !log.contains(QStringLiteral("errorOccurred(code=41)")));
            checkBool("⑥ abort 之后 finished 照样到达（信号序列里有 finished）",
                      log.contains(QStringLiteral("finished(")));
            checkBool("⑥ 被取消的下载没有 commit（signalLog 里 commit=0）",
                      !dl.committed && log.contains(QStringLiteral("commit=0")));
            checkBool("⑥ 半截坏文件不存在：目标路径上是空的", f.exists(), false);
            checkBool("⑥ 取消前确实收到了数据（不是刚发就掐）", dl.snap.received >= 256 * 1024);
            // ① 的峰值是「一坨整包」，这里是「限速流」：同一个 while 排空写法，
            // 驻留差出两个数量级——这才是「边下边写省内存」成立的条件，不是无条件的。
            checkBool("⑥ 限速流的驻留峰值远小于 8 MiB 正文（和 ① 的整包形成对照）",
                      dl.peakInFlight > 0 && dl.peakInFlight < quint64(LoopbackHttpServer::kFileSize));
            checkBool("⑥ 取消时 reply 还在 running（否则 abort() 是空操作，本机第一版就栽在这）",
                      abortRunningAtClick && dl.abortDidCut);
            break;
        }
        default:
            break;
        }
        // —— 推进状态机 / 收摊 ——
        ++stage;
        if (stage == 6) {
            watchdog.stop();
            qInfo() << "【收尾】六个阶段全部走完，总耗时" << total.elapsed() << "ms";
            qInfo() << "  一句话带走：**下载是状态机不是函数调用**——进度三分支、头要趁活着读、"
                       "落盘边下边写、QSaveFile 保终态、abort 之后先 finished 再 deleteLater。";
            qInfo().noquote() << QStringLiteral("\n【硬账】%1/%2 项核对通过 → 退出码 %3")
                                     .arg(Ledger::passed)
                                     .arg(Ledger::total)
                                     .arg(Ledger::exitCode());
            server.stop();                              // 关服务器、断连接
            QCoreApplication::exit(Ledger::exitCode());
            return;
        }
        nextStage(stage);
    });

    // —— 阶段①开跑：QTimer::singleShot(0) 让事件循环先转起来再发第一个请求 ——
    QTimer::singleShot(0, [&]() {
        dl.start(QUrl(base.toString() + QStringLiteral("/file")), pFull);
    });

    return app.exec();
}

// ============================================================================
// 六、速查与纪律（背不住就回来查）
// ============================================================================
/*
 * downloadProgress 三分支（本讲全部现场演过）：
 *   bytesTotal > 0   percent = received*100/total，UI 画确定进度
 *   bytesTotal == -1 长度未知（无 Content-Length 用关闭划线/流式），UI 画忙碌态，**别除**
 *   bytesTotal == 0  空文件，当 100% 完成，不是除零事故
 * 本机实测的三条限定（Qt 6.12.0 / MinGW / Windows，不看这三条就会误判「分支没走到」）：
 *   ① 信号**节流**：最快每 100ms 一次（qnetworkreply.cpp:15 progressSignalInterval=100，
 *      发送条件 qnetworkreplyhttpimpl.cpp:1252~1257）；快网络上的短下载根本没有中途帧。
 *   ② 收尾那一帧 Qt 改口：totalSize==-1 时发 downloadProgress(received, received)
 *      （qnetworkreplyhttpimpl.cpp:2240~2244）——最后一帧不是 -1。
 *   ③ 空文件到底走哪条看服务器：本讲 /empty 给的是 Content-Length: 0 → percent=100；
 *      若一帧 progress 都不发，Snapshot 保持初值（totalKnown=false/percent=-1）也合法，
 *      硬账只要求**不出现除零和负百分比**。
 *
 * 头的两种读法与**读取时机**（都在 metaDataChanged 之后；finished 里读最稳妥）：
 *   熟头 header(KnownHeaders)：ContentType / ContentLength / LastModified（已解析成 QVariant）
 *   生头 rawHeader("Content-Range")：Range 相关、X- 扩展头只有这条路
 *   ⚠ RawHeaderAvailable 信号在 Qt 6.12 头文件里不存在，别抄；deleteLater 之后就再也读不到
 *
 * 断点续传的最小配方：
 *   请求 req.setRawHeader("Range", "bytes=524288-")
 *   判   reply->attribute(HttpStatusCodeAttribute).toInt() == 206
 *   对   rawHeader("Content-Range") == "bytes 524288-1048575/1048576" 再续写
 *   （200 而不是 206 = 服务器不理 Range，老实从头下）
 *
 * 落盘纪律：
 *   readyRead → while 读到空（每片 ≤64KiB）→ write，**边下边写**；
 *   ★一次 readyRead 只 read 一片会被截断：Qt 在数据到手结束时只发一次 readyRead 就转
 *     finished，1 MiB 只落 65536 字节（本机实测）。finished 之后还要再兜底排空一次；
 *   ★边下边写不等于省内存：1 MiB 整包到手时峰值就是 1 MiB，8 MiB 限速流才降到 64 KiB。
 *     真要卡住驻留用 setReadBufferSize，别只靠 read()；
 *   攒 readAll 的代价是整包驻留内存；
 *   QSaveFile：open(WriteOnly) → 写 → 成功才 commit()；中断 cancelWriting/析构自动毁临时文件；
 *   close() 是 private 的，别调。
 *
 * abort 与生命周期（最容易崩的一段）：
 *   abort() 后：errorOccurred(OperationCanceledError=**5**，不是 41) → **finished() 照样来**；
 *   ★abort() 是**同步**的：errorOccurred → finished → 你的收尾槽全在它返回之前跑完，
 *     所以「先记状态再 abort」，靠返回值往外传必然拿到旧值；
 *   ★reply 已经不 running 时 abort() 是空操作（下载早结束了，取消点了个寂寞，不报错）；
 *   delete 只放在 finished 之后（本讲用 deleteLater）；
 *   用 finished(QNetworkReply*) 统一收尾时记得 reply->deleteLater()。
 */

// ——— 老师傅的话 ———
//   徒弟，下载这件事，新手写的是「一个函数」：get() 出去，等 finished，readAll，写文件。
//   能跑，但三处地方等着埋雷：进度（bytesTotal=-1 除个零试试）、内存（8MB 的包没事，
//   8GB 的包呢）、中断残留（网络一断，磁盘上多个半截文件，下次当有效的用，出事故）。
//   老师傅写的是「一个状态机」：每个 reply 从 Running 到 Finished 只走一条道，
//   进度信号三分支各管各的，数据一到 readyRead 就押去落盘，QSaveFile 只认 commit，
//   abort 是客人不是破坏者——它走了也会敲门（finished）。你把这个讲的小 demo 拆了
//   重装成自己的 Downloader 类，装不回去的地方，就是你还没懂的地方。
//   还有一条压箱底：**头要在它还活着的时候读**。finished 之后一个 deleteLater，
//   reply 就进了回收队列，你下一行再 rawHeader("Content-Range") 摸到的就是空。
//   本讲把读数全冻结在 finished 回调里，不是洁癖，是保命。
//   这一讲最扎心的一课是「**进度信号每 100ms 才发一次，而且最后一帧会把 -1 改口成已收**」：
//   你在局域网/回环上测不出未知长度分支，不是它不存在，是它太快了没被采样到。
//   想验证一个分支，先想清楚「要让它发生，需要几毫秒的窗口」——这就是为什么要限速服务器。

// ——— 小练习 ———
//   1. 给 Downloader 加「断点续传真续」：阶段⑥ abort 后不删数据，改为把已下到的偏移
//      存进 .part 文件，重新 start 时发 "bytes=<偏移>-"，服务器已支持开区间 Range；
//      对照打印两次 206 的 Content-Range 与最终校验和。
//   2. 把阶段②的 /nolength 响应改成 HTTP/1.1 分块传输（Transfer-Encoding: chunked，
//      正文按 "10000\r\n…\r\n" 格式切给服务器发）：观察 bytesTotal 此时是 -1 还是未知，
//      与「关闭划线」版对比 UI 表现（忙碌态 vs 确定进度）。
//      ★已知答案的一半：本机实测「关闭划线 + 10ms 限速」确实能拿到 -1（命中若干次），
//        而**不限速时一次都拿不到**——先想想 chunked 会不会有同样的采样问题，再动手。
//   3. 在 readyRead 回调里故意攒 readAll 不做边下边写，给 /big 加大到 64MiB，
//      用 peakInFlight 读数对比两种写法的内存驻留峰值（预期差一到两个数量级）。
//      ★注意对照组：本讲 ① 的 1 MiB 整包到手，边下边写的峰值也是 1 MiB。
//        不做限速流的话，这个实验测不出差别。
//   4. 把 abort 时机从「收到 256KiB」换成「下载速度低于 X 字节/秒」（自己用
//      downloadProgress 两次差值算速度），实现一个「太慢就放弃」的看门狗。

// 本文件有 Q_OBJECT 类（LoopbackHttpServer / Downloader），AUTOMOC 会生成并在这里包含。
#include "04_download_and_progress.moc"
