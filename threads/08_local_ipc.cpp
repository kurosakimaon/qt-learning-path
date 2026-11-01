// ============================================================================
// 第 8 讲 · 本地进程间通信：一条没有「消息」的管道，和一套自己定规矩的协议
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   前七讲讲的都是「一个进程里怎么不打架」。这一讲换个战场：两个进程怎么说话。
//   桌面软件的日常就在这里 —— 双击图标时把消息丢给已经在跑的那个实例、
//   主进程和崩溃监视器互相看一眼、安装器和主程序交代一句「装完了，重启吧」。
//   你要么用本地套接字，要么用一堆更麻烦的东西（临时文件 + 轮询），没有第三条路。
//
//   而第一次用 QLocalSocket 的人，几乎都会在同一个地方摔一跤：
//     「我 write 了一条消息，对面 readAll 却读到了三条。」
//     「我 write 了三次，对面只收到一次 readyRead。」
//   这不是 bug，这是字节流的本性。**这条管道里没有「消息」这个东西，
//   只有字节**。边界得你自己造 —— 这一讲【2】【3】就是在造边界，
//   并且用可核对的数证明造对了。
//
//   八节地图（每节都有可打印的结论，硬账进退出码）：
//     1) 把服务端立起来：listen(名字)、fullServerName()，以及「同名第二台被拒」
//        —— 这一条就是桌面软件「单实例」的地基，不是比喻，是同一段代码。
//     2) 字节流没有边界：五条消息拆成三段发出去，靠**长度前缀**把五条找回来。
//        我把「第一块只有 7 字节、连第一帧都装不下」这个事实也印出来 ——
//        半包长什么样，看数字比看定义清楚。
//     3) 坏帧防御：长度字段是**对端说的**，永远别信。声称 1 GiB 的帧头必须被拒、
//        连接必须被断，而它前面那条好帧不能跟着丢（已解析的不回滚）。
//     4) 多客户端：每个连接一份解析器。这一节把「连接」当资源来管：
//        谁生谁、谁删谁、断开之后对象什么时候真的消失。
//     5) 背压：write() 的返回值是「我收下了」，不是「对方收到了」。
//        这一节量两件事：写完之后还有多少字节在路上（bytesToWrite），
//        以及最后对面是不是一个字节不差（这是本讲最硬的一条账）。
//     6) 失败路径：error() 是原因，disconnected() 是结果，两者不是一回事。
//        连一个不存在的名字、和连上之后被对端掐掉，走的是两条不同的代码路径。
//     7) QLockFile：另一种互斥 —— 它不需要对端，也就没有「对面挂了怎么办」。
//        适合表达「同一份数据不许两个进程同时改」。
//     8) 收尾与选型：关掉之后能不能立刻同名重开（这个问题的答案值得单独看）、
//        removeServer 到底在清什么、以及本地 IPC 五种手段的对照表。
//
//   ★ 运行前须知（这一讲讲网络，先说清楚边界）：
//     · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//       Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 threads_08_local_ipc）。本机这一轮：【硬账】52/52 项核对通过、整节作废 0、总闸没跳。
//       这只证明「编得过、跑得完、上面这行为绿」——退出码 0 不等于正文每个数字都对：
//       换 Qt 版本 / 换平台 / 换机器后，带具体数字的「预期」请以你自己那次运行输出为准
//       （本机那一轮存档 runlogs/threads_08_local_ipc.log）。
//     · 只连本机、只用临时目录。名字里带本进程 pid，`qtlp_ipc_08_<pid>`，
//       两套并排跑（比如 CI 上并行）也不会互撞。一个字节都不出这台机器。
//     · 不在 GUI 线程上用 waitForConnected / waitForReadyRead 死等。
//       本讲的等待全部是「事件循环 + 期限」，形状在第 4 节前面写清楚了。
//     · 所有等待都有期限，全讲最坏情况约 14 秒；总闸 20 秒，进程一定会退出。
//     · 不测的东西：跨进程崩溃后的 socket 文件残留（要真起子进程，太重）、
//       Windows 上的管道 ACL（要另一个用户账号）、网络驱动器上的管道。
//       这些我把结论写在【8】的正文里，但不假装跑过。
// ============================================================================

// ============================================================================
// 一、头文件地图：这一讲引用的每一个数，都指到具体哪一行
// ----------------------------------------------------------------------------
// QtNetwork/qlocalserver.h
//   第 28 行 void newConnection();              ← 有新连接进来时发一次
//   第 31~37 行 enum SocketOption { NoOptions, UserAccessOption, GroupAccessOption,
//              OtherAccessOption, WorldAccessOption, AbstractNamespaceOption }
//              ← ★ Access 那三个只在 Unix 上有意义（文件权限）；Windows 上管的是
//                管道实例的 ACL。AbstractNamespaceOption 是 Linux 独有的抽象命名空间，
//                它连「文件」都不占，所以连残留都没有。
//   第 49 行 bool isListening() const;
//   第 50 行 bool listen(const QString &name);   ← 名字，不是端口
//   第 51 行 bool listen(qintptr socketDescriptor) ← 接手一个别人已经 listen 好的 fd
//   第 53 行 QLocalSocket *nextPendingConnection()
//              ← ★ 你**必须**调用它，否则连接一直堆在服务端内部队列里，
//                新连接会被 maxPendingConnections（第 50 行）挡住。
//   第 54/55 行 serverName() / fullServerName()
//              ← ★ 一个是你给的名字，一个是平台拼出来的全名。想连对端，
//                两个都能用，但**打印日志必须用后者** —— 出问题时你要看到真地址。
//   第 56 行 static bool removeServer(const QString &name)
//              ← 清掉一个残留的名字（Unix 上是删 socket 文件，Windows 上是空操作）
//   第 57 行 QAbstractSocket::SocketError serverError() const
// QtNetwork/qlocalsocket.h
//   第 27~41 行 enum LocalSocketError：注意它的值**就是** QAbstractSocket::SocketError
//              的那几个（ConnectionRefusedError = ...，第 29 行起逐个赋值）。
//              ← 所以本地套接字的错误码和 TCP 是同一套字母表，只是含义按管道解释。
//   第 43~50 行 enum LocalSocketState { UnconnectedState, ConnectingState,
//              ConnectedState, ClosingState } —— 没有 Listening，它是对端的事。
//   第 62/63 行 connectToServer() 两个重载            ← 异步，立刻返回
//   第 64 行 void disconnectFromServer();            ← 优雅：把写缓冲发完再断
//   第 68 行 QString fullServerName() const;
//   第 70 行 void abort();                           ← 不优雅：立刻断，写缓冲丢掉
//   第 73 行 qint64 bytesToWrite() const; override   ← ★ 背压的唯一观察窗
//   第 77 行 LocalSocketError error() const;
//   第 78 行 bool flush();                           ← 「催一下」，不是保证
//              ★ 勘误：旧稿把这两个号整体往后挪了一行（78 error / 80 flush）。本机 6.12.0：
//                77 = error()、78 = flush()、**80 = readBufferSize() const**——
//                按旧稿去 80 行找 flush 会撞进 readBufferSize，两个是完全不同的东西。
//   第 92 行 LocalSocketState state() const;
//   第 93~96 行 waitForBytesWritten / waitForConnected / waitForDisconnected /
//              waitForReadyRead                                ← 全是阻塞的，带毫秒
//   第 99/100 行 connected() / disconnected()
//   第 101 行 errorOccurred(QLocalSocket::LocalSocketError)
// QtCore/qlockfile.h
//   第 26 行 bool lock();                    ← 死等
//   第 28 行 bool tryLock(int timeout);      ← 带超时；0 = 只试一次
//   第 29 行 void unlock();
//   第 36 行 bool tryLock(std::chrono::milliseconds timeout = 0ms);  ← Qt 6 的新脸
//   第 41 行 bool isLocked() const;
//   第 42 行 bool getLockInfo(qint64 *pid, QString *hostname, QString *appname) const
//              ← ★ 能读出「谁拿着锁」，这是 QLockFile 比一个裸文件强的地方
//   第 43 行 bool removeStaleLockFile();
//   第 45~50 行 enum LockError { NoError = 0, LockFailedError = 1, PermissionError = 2,
//              UnknownError = 3 }；第 51 行 LockError error() const
//              ★ 勘误：旧稿写 46~51（把 enum 的第一项当成 enum 的起始行，又把 error() 算进范围）。
//                本机 6.12.0：45 = `enum LockError {`，46 才是 NoError = 0，50 = `};`，51 = error()。
//                顺带纠一条：这一项**每个都写了显式值**，旧稿那串「NoError, LockFailedError,
//                PermissionError, UnknownError」看着像隐式接续——照隐式去记别的 LockError 值会记错。
//              ← ★ 这个枚举**没有** Q_ENUM，所以 QMetaEnum::fromType 用不了，
//                本讲只能手写一个 switch 把名字打出来。头文件里看得见，不用猜。
//   第 71~74 行 int staleLockTime() const                 ← 默认 30 秒
//
// ★ 一个必须说清楚的事实：Windows 上 QLocalServer 用的是**命名管道**
//   （\\.\pipe\<名字>），Linux/macOS 上是**本地 Unix socket**（一个文件）。
//   两个平台都不是 TCP 回环 —— 不占端口、不经过防火墙、不会被外部连进来。
//   这个差别会在【8】里咬你一口：进程崩了，Windows 上名字随进程消失，
//   Unix 上那个 socket 文件留在 /tmp 里。
//
// 跨讲链接：
//   · threads 第 4 讲：Hard / Soft / Obs 三档口径的来历。
//   · threads 第 7 讲：本讲的等待器、总闸、退出码只算一次，全部照抄它。
//   · model_view 与 events 讲：本讲不碰界面；界面线程里绝不许 waitFor*，理由在【5】。
// ============================================================================

// ============================================================================
// 二、口径与账本：只有「不看调度器也成立」的结论才配当硬账
// ----------------------------------------------------------------------------
//   Hard —— API 语义保证，或者我的代码里两条分支穷尽了。不符就是代码错。
//   Soft —— 取决于调度器/平台，没命中不算失败，但必须打印。
//   Obs  —— 只报实测（字节数、次数、毫秒），不做承诺。
//
// ★ 这一讲的硬账大多不依赖时序，而是「守恒量」：
//   发出的帧数 = 收到的帧数；发出的字节 = 收到的字节；连接数按顺序增减。
//   守恒量的好处是：中间怎么抖都不影响结论，所以它可以当硬账。
//   反过来，「第一块是不是只到了 7 字节」「readyRead 发了几次」这类
//   完全由内核调度决定，一律 Obs。**别把一次观测当成一条规律。**
// ============================================================================

// ============================================================================
// 三、本讲唯一的等待器：事件循环 + 期限
// ----------------------------------------------------------------------------
// 【重要】这是**教材里**的便利函数。它内部开了一个嵌套事件循环，
//   而嵌套事件循环会让「重入」变成可能（正在跑的槽函数里又跑起了别的事件）。
//   生产代码里不要这么写 —— 生产代码用「发请求 → 存状态 → 信号回调 → 超时定时器」
//   这套状态机（第 7 讲【7】就是那个形状）。
//   我在这里用它，是因为每一节都是「发完就等一个很短的结果」，写成状态机
//   会把七节的代码撑成七百行，反而看不见重点。
// ============================================================================

#include <QCoreApplication>
#include <QLocalServer>       // qlocalserver.h：28 newConnection、49~51 listen、53 nextPendingConnection
#include <QLocalSocket>       // qlocalsocket.h：27~41 错误枚举、73 bytesToWrite、101 errorOccurred
#include <QAbstractSocket>    // SocketError 的 Q_ENUM（qabstractsocket.h 80 行），为了能打出名字
#include <QLockFile>          // qlockfile.h：28 tryLock、42 getLockInfo、46~51 LockError
#include <QStandardPaths>     // TempLocation：本讲所有文件只写这里
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QMetaEnum>          // 把 Q_ENUM 打成人看得懂的名字
#include <QDebug>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QList>
#include <QPair>
#include <QHash>
#include <QtEndian>           // qToBigEndian / qFromBigEndian，长度字段的网络字节序

#include <cstring>            // memcpy
#include <functional>         // pumpUntil 的谓词
#include <numeric>            // std::accumulate（【2C】把每块字节数加起来）

enum Kind { Hard, Soft, Obs };

struct Ledger {
    int total = 0;
    int failed = 0;
    int stalled = 0;
    bool watchdogFired = false;

    int exitCode() const
    {
        const int broken = failed + stalled;
        return (watchdogFired || broken > 0 || total == 0) ? 1 : 0;
    }
};
static Ledger gLedger;

static void verdict(Kind kind, const QString &label, bool ok, const QString &detail)
{
    QString head;
    switch (kind) {
    case Hard:
        ++gLedger.total;
        if (!ok)
            ++gLedger.failed;
        head = ok ? QStringLiteral("  [OK ] 核对  ") : QStringLiteral("  [!! ] 不符  ");
        break;
    case Soft: head = ok ? QStringLiteral("  [预期中] ") : QStringLiteral("  [这次没命中] "); break;
    case Obs:  head = QStringLiteral("  [实测]  "); break;
    }
    qInfo().noquote() << head + QStringLiteral("%1 = %2").arg(label, detail);
}

static void section(const QString &text)
{
    qInfo().noquote() << QStringLiteral("\n========== %1 ==========").arg(text);
}

static void checkNum(const QString &label, qint64 got, qint64 want)
{
    verdict(Hard, label, got == want, QStringLiteral("%1（期望 %2）").arg(got).arg(want));
}

static void checkBool(const QString &label, bool got, bool want)
{
    verdict(Hard, label, got == want,
            QStringLiteral("实测 %1（期望 %2）")
                .arg(got ? QStringLiteral("true") : QStringLiteral("false"),
                     want ? QStringLiteral("true") : QStringLiteral("false")));
}

static bool premise(const QString &label, bool ok, const QString &detail)
{
    verdict(Hard, QStringLiteral("前提：") + label, ok, detail);
    if (!ok) {
        ++gLedger.stalled;
        qWarning().noquote()
            << QStringLiteral("  ……… 本节前提没成立，余下的核对全部跳过（这一节记作作废）");
    }
    return ok;
}

// 在事件循环里等 predicate 成真，最多 budgetMs 毫秒。返回它最后成没成。
static bool pumpUntil(const std::function<bool()> &pred, int budgetMs)
{
    if (pred())
        return true;
    QEventLoop loop;
    QTimer poll;
    poll.setInterval(5);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
        if (pred())
            loop.quit();
    });
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start();
    guard.start(budgetMs);
    loop.exec();
    poll.stop();
    return pred();
}

// 只想「让事件循环转 millisecond 毫秒」时用它 —— 比如故意等两段数据分开到达。
static void pumpFor(int millisecond)
{
    pumpUntil([]() { return false; }, millisecond);
}

template <typename E>
static QString enumName(E value)
{
    const QMetaEnum me = QMetaEnum::fromType<E>();
    const char *key = me.valueToKey(int(value));
    return key ? QString::fromLatin1(key) : QStringLiteral("<未登记 %1>").arg(int(value));
}

// QLockFile::LockError 没有 Q_ENUM（qlockfile.h 46~51 行），只能手写。
static QString lockErrorName(QLockFile::LockError e)
{
    switch (e) {
    case QLockFile::NoError:         return QStringLiteral("NoError");
    case QLockFile::LockFailedError: return QStringLiteral("LockFailedError");
    case QLockFile::PermissionError: return QStringLiteral("PermissionError");
    case QLockFile::UnknownError:    return QStringLiteral("UnknownError");
    }
    return QStringLiteral("<未登记的 %1>").arg(int(e));
}

static QByteArray pattern(int n, int seed)
{
    QByteArray out;
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.append(char('a' + (seed + i) % 26));
    return out;
}

// ============================================================================
// 四、协议：4 字节大端长度 + 1 字节类型 + 载荷
// ----------------------------------------------------------------------------
// 为什么是「长度前缀」而不是「用 \n 分隔」？两种都能定界，选长度前缀的理由：
//   · 载荷是二进制（图片、序列化结构），里面随时可能出现 \n，分隔符要转义；
//   · 长度前缀让接收方**先知道要多少**，可以一次分配、直接读满，
//     分隔符方案只能一块一块攒、边攒边扫；
//   · 代价是 4 个字节的开销，和「必须给长度设上限」这条纪律（【3】讲的就是它）。
//
// ★ 长度字段写的是**载荷**长度，不含 5 字节头。所以一整帧 = 5 + 载荷。
//   定死一个约定，然后写进注释 —— 协议文档比协议代码重要。
// ============================================================================

struct Frame {
    quint8 type = 0;
    QByteArray payload;
};

static constexpr int kFrameHead = 5;                 // 4 字节长度 + 1 字节类型
static constexpr int kPayloadCap = 64 * 1024;        // 单帧载荷上限：64 KiB

static QByteArray encodeFrame(quint8 type, const QByteArray &payload)
{
    QByteArray out;
    out.resize(kFrameHead + payload.size());
    // ★ reinterpret_cast 不是多余的：qToBigEndian 有 void* 和 uchar* 两个重载，
    //   直接传 char* 会「同样好」地匹配到两个，编译器报 ambiguous。
    qToBigEndian<quint32>(quint32(payload.size()), reinterpret_cast<uchar *>(out.data()));
    out[4] = char(type);
    if (!payload.isEmpty())
        std::memcpy(out.data() + kFrameHead, payload.constData(), size_t(payload.size()));
    return out;
}

// 只造一个「帧头」，长度字段随便填 —— 【3】用它伪造一个 1 GiB 的谎。
static QByteArray frameHeaderOnly(quint32 claimedPayload, quint8 type)
{
    QByteArray head(kFrameHead, '\0');
    qToBigEndian<quint32>(claimedPayload, reinterpret_cast<uchar *>(head.data()));
    head[4] = char(type);
    return head;
}

// 把任意碎块喂进来，吐出一帧一帧。核心只有三件半事：
//   ① 不够 5 字节 → 等；② 长度超上限 → 报错，清空，不再接收；
//   ③ 够一整帧 → 切出来；④ 剩下的（半包）留在缓冲里等下一次 feed。
class FrameParser {
public:
    explicit FrameParser(int cap = kPayloadCap) : m_cap(cap) {}

    void feed(const QByteArray &chunk)
    {
        if (m_bad)
            return;                         // 已经坏了就不再吸收，免得喂的人以为还有效
        m_buf.append(chunk);
        for (;;) {
            if (m_buf.size() < kFrameHead)
                return;                     // ① 连帧头都不够，等
            const quint32 len = qFromBigEndian<quint32>(
                reinterpret_cast<const uchar *>(m_buf.constData()));
            if (len > quint32(m_cap)) {     // ② 对端说「我要发 1 GiB」
                m_bad = true;
                m_badLen = len;
                m_buf.clear();              // ★ 立刻松手：既不留、也不照着它分配
                return;
            }
            if (m_buf.size() < kFrameHead + qsizetype(len))
                return;                     // ③ 半包：整帧还没到齐
            Frame f;
            f.type = quint8(m_buf.at(4));
            f.payload = m_buf.mid(kFrameHead, qsizetype(len));
            m_buf.remove(0, kFrameHead + qsizetype(len));
            m_ready.append(f);              // ④ 一帧到手，接着看能不能再切一帧
            ++m_parsed;
        }
    }

    bool takeFrame(Frame *out)
    {
        if (m_ready.isEmpty())
            return false;
        *out = m_ready.takeFirst();
        return true;
    }

    bool hasError() const { return m_bad; }
    quint32 badLength() const { return m_badLen; }
    int pendingBytes() const { return int(m_buf.size()); }
    int parsedCount() const { return m_parsed; }

private:
    QByteArray m_buf;       // 没凑成整帧的尾巴
    QList<Frame> m_ready;   // 已经解析出来、还没被 takeFrame 取走的
    int m_cap;
    bool m_bad = false;
    quint32 m_badLen = 0;
    int m_parsed = 0;
};

// ============================================================================
// 五、服务端：一个 listener，N 个连接，N 份解析器
// ----------------------------------------------------------------------------
// 为什么解析器要「每个连接一份」，而不是整个服务端一份？
//   因为半包状态是**每条连接各自的**：A 的尾巴不能接到 B 的头上。
//   这是新手最容易写出的串包 bug —— 单客户端测试永远发现不了。
// ============================================================================

class IpcServer : public QObject
{
    Q_OBJECT
public:
    explicit IpcServer(QObject *parent = nullptr)
        : QObject(parent), m_server(new QLocalServer(this))
    {
        connect(m_server, &QLocalServer::newConnection, this, &IpcServer::onNewConnection);
    }

    bool start(const QString &name) { return m_server->listen(name); }
    void stop() { m_server->close(); }

    bool isListening() const { return m_server->isListening(); }
    QString fullName() const { return m_server->fullServerName(); }
    QLocalServer *raw() const { return m_server; }

    int liveConnections() const { return int(m_parsers.size()); }
    int acceptedCount() const { return m_accepted; }
    int closedCount() const { return m_closed; }
    int readyReadCount() const { return m_readyReads; }
    int frameCount() const { return m_frames; }
    int rejectedCount() const { return m_rejected; }
    quint32 lastBadLen() const { return m_lastBadLen; }
    QString lastRejectWhy() const { return m_lastRejectWhy; }
    int lastPendingBytes() const { return m_lastPending; }
    const QList<int> &chunkSizes() const { return m_chunkSizes; }
    void clearChunkSizes() { m_chunkSizes.clear(); }

    // 把所有连接一次性掐掉（【6】要让客户端体验「对端没了」）。
    void abortAll()
    {
        // findChildren 返回的是**副本**，所以可以边遍历边 abort。
        // 但 abort 可能同步回调 onDisconnected（里面 deleteLater），
        // 所以循环结束后这些指针一个都不许再用。
        const QList<QLocalSocket *> sockets = m_server->findChildren<QLocalSocket *>();
        for (QLocalSocket *s : sockets)
            s->abort();
    }

Q_SIGNALS:
    // 一帧解析完成。转发出去而不是让外边轮询，是 Qt 里「数据流」的标准形状。
    void frameArrived(QLocalSocket *from, quint8 type, const QByteArray &payload);
    // 坏帧：对端声称的长度是多少。注意信号里带的是「它说的」，不是「真实大小」。
    void refusedFrame(QLocalSocket *from, quint32 claimedBytes);

private:
    void onNewConnection()
    {
        // ★ 必须把队列里的连接全部取走。不取，新连接就会被排队上限挡住，
        //   而对端只会看到「连上了却不说话」—— 最难查的一类问题。
        while (QLocalSocket *sock = m_server->nextPendingConnection()) {
            ++m_accepted;
            m_parsers.insert(sock, new FrameParser);
            connect(sock, &QLocalSocket::readyRead, this, [this, sock]() { onReadyRead(sock); });
            connect(sock, &QLocalSocket::disconnected, this, [this, sock]() { onDisconnected(sock); });
        }
    }

    void onReadyRead(QLocalSocket *sock)
    {
        const auto it = m_parsers.find(sock);
        if (it == m_parsers.end())
            return;
        FrameParser *parser = it.value();

        const QByteArray chunk = sock->readAll();
        if (chunk.isEmpty())
            return;
        ++m_readyReads;
        m_bytesIn += chunk.size();
        m_chunkSizes.append(int(chunk.size()));

        parser->feed(chunk);
        Frame frame;
        while (parser->takeFrame(&frame)) {
            ++m_frames;
            Q_EMIT frameArrived(sock, frame.type, frame.payload);
        }
        m_lastPending = parser->pendingBytes();

        if (parser->hasError()) {
            ++m_rejected;
            m_lastBadLen = parser->badLength();
            m_lastPending = 0;
            m_lastRejectWhy = QStringLiteral("对端声称这一帧有 %1 字节，超过上限 %2 字节；立即断开，不陪它传完")
                                  .arg(m_lastBadLen)
                                  .arg(kPayloadCap);
            Q_EMIT refusedFrame(sock, m_lastBadLen);
            // ★★ 从这里往下不许再碰 parser：abort() 可能把 disconnected 同步送回来，
            //    而 onDisconnected 里已经把 parser delete 掉了。用悬垂指针读一个
            //    「已知坏了」的解析器，是这类代码最典型的崩溃点。
            sock->abort();
        }
    }

    void onDisconnected(QLocalSocket *sock)
    {
        const auto it = m_parsers.find(sock);
        if (it != m_parsers.end()) {
            delete it.value();
            m_parsers.erase(it);
        }
        ++m_closed;
        // ★ deleteLater 而不是 delete：我们现在正在这个 socket 的信号里，
        //   把它当场删掉等于在自己的栈上锯树枝。下一轮事件循环再删，安全。
        sock->deleteLater();
    }

    QLocalServer *m_server;
    QHash<QLocalSocket *, FrameParser *> m_parsers;   // 每条连接一份半包状态
    int m_accepted = 0;
    int m_closed = 0;
    int m_readyReads = 0;
    int m_frames = 0;
    int m_rejected = 0;
    quint32 m_lastBadLen = 0;
    int m_lastPending = 0;
    qint64 m_bytesIn = 0;
    QString m_lastRejectWhy;
    QList<int> m_chunkSizes;
};

// ============================================================================
// 六、客户端：把「连接」和「记录发生了什么」包在一起
// ----------------------------------------------------------------------------
// 它只做三件事：连、写、记账（连上了几次、报了几次错、断了几次）。
// 【6】要靠这份记录讲「error 和 disconnected 不是一回事」。
// ============================================================================

class ClientLink : public QObject
{
    Q_OBJECT
public:
    explicit ClientLink(QObject *parent = nullptr)
        : QObject(parent), m_socket(new QLocalSocket(this))
    {
        connect(m_socket, &QLocalSocket::connected, this, [this]() {
            ++m_connects;
            m_trail.append(QStringLiteral("connected"));
        });
        connect(m_socket, &QLocalSocket::errorOccurred, this,
                [this](QLocalSocket::LocalSocketError e) {
                    ++m_errors;
                    m_lastError = e;
                    m_trail.append(QStringLiteral("error(%1)").arg(enumName(e)));
                });
        connect(m_socket, &QLocalSocket::disconnected, this, [this]() {
            ++m_disconnects;
            m_trail.append(QStringLiteral("disconnected"));
        });
    }

    QLocalSocket *socket() const { return m_socket; }
    bool isConnected() const { return m_socket->state() == QLocalSocket::ConnectedState; }
    int connects() const { return m_connects; }
    int errors() const { return m_errors; }
    int disconnects() const { return m_disconnects; }
    QLocalSocket::LocalSocketError lastError() const { return m_lastError; }
    QString errorText() const { return m_socket->errorString(); }
    QString trail() const { return m_trail.isEmpty() ? QStringLiteral("<什么都没发生>")
                                                     : m_trail.join(QStringLiteral(" → ")); }

    void connectTo(const QString &name) { m_socket->connectToServer(name); }
    qint64 send(quint8 type, const QByteArray &payload)
    {
        return m_socket->write(encodeFrame(type, payload));
    }
    qint64 sendRaw(const QByteArray &bytes) { return m_socket->write(bytes); }
    void close() { m_socket->disconnectFromServer(); }

private:
    QLocalSocket *m_socket;
    int m_connects = 0;
    int m_errors = 0;
    int m_disconnects = 0;
    QLocalSocket::LocalSocketError m_lastError = QLocalSocket::UnknownSocketError;
    QStringList m_trail;
};

// ============================================================================
// 【1】把服务端立起来
// ============================================================================
static void phase1_startServer(IpcServer &server, const QString &name)
{
    section(QStringLiteral("【1】listen(名字) / fullServerName() / 同名第二台被拒"));

    if (!premise(QStringLiteral("【1】这台服务端 listen 成功"),
                 server.start(name),
                 QStringLiteral("名字 = %1").arg(name)))
        return;

    checkBool(QStringLiteral("【1A】listen 之后 isListening() 为真"), server.isListening(), true);
    checkBool(QStringLiteral("【1B】serverName() 原样返回我们给的名字"),
              server.raw()->serverName() == name, true);
    verdict(Obs, QStringLiteral("【1】fullServerName()：平台自己拼出来的真地址"),
            true, server.fullName());
    checkBool(QStringLiteral("【1C】fullServerName() 里带着我们的名字"),
              server.fullName().contains(name), true);

    // ★ 同名第二台：这一条在【1】里被当成「单实例」的地基来教 —— 但真正跑起来才发现，
    //   这件事**只在 Unix 成立，Windows 上恰恰相反**。逐平台核对，别把一边的经验带到另一边：
    //     · Unix：名字是 $TMPDIR 下的一个 socket 文件，已有进程 bind+listen 它，
    //       第二个 bind 撞 AddressInUseError → listen 失败。这才是「第二台起不来」的那个平台。
    //     · Windows：QLocalServer::listen 走 CreateNamedPipe，而且 maxInstances 传的是
    //       PIPE_UNLIMITED_INSTANCES（qlocalserver_win.cpp 148~159 行）—— 同名再建一个
    //       管道**实例**是允许的、会成功。也就是说 Windows 上「listen 同名失败」
    //       根本不能当单实例判据；要单实例得靠【7】的 QLockFile（文件 + pid，跨平台语义一致）。
    QLocalServer second;
    const bool secondOk = second.listen(name);
#ifdef Q_OS_WIN
    const bool wantSecondListen = true;    // Windows 实测：第二台 listen 成功
    const QString gateNote = QStringLiteral("Windows 命名管道允许多实例（PIPE_UNLIMITED_INSTANCES）=> 第二台成功");
#else
    const bool wantSecondListen = false;   // Unix：socket 文件被占 ⇒ 第二台失败
    const QString gateNote = QStringLiteral("Unix socket 文件被占 => 第二台失败");
#endif
    checkBool(QStringLiteral("【1D】同名第二台 listen 的结果（单互斥只在 Unix 成立，Windows 不成立：%1）").arg(gateNote),
              secondOk, wantSecondListen);
    verdict(Obs, QStringLiteral("【1】第二台的 serverError() / errorString()"),
            true, QStringLiteral("%1 / %2")
                      .arg(enumName(second.serverError()), second.errorString()));

    qInfo().noquote() << QStringLiteral(
        "  → 关键更正（本讲第一版的硬账就在这儿翻了车）：\n"
        "\n     「第二台 listen 一定失败」这句话**不是跨平台确定的**，它只在 Unix 成立；"
        "\n     Windows 用 PIPE_UNLIMITED_INSTANCES，同名第二台照样 listen 成功 —— 因为命名管道"
        "\n     本来就允许多个服务端实例并存，客户端连上的是任意一个空闲实例。"
        "\n     所以拿 listen() 做单实例互斥，在 Windows 上是**失效**的：你会开出第二个服务端，"
        "\n     而两个服务端会分摊后续进来的客户端，行为完全不对。"
        "\n     真正跨平台可靠的单实例是【7】的 QLockFile（或回环上占死一个 TCP 端口）。");
}

// ============================================================================
// 【2】字节流没有边界
// ============================================================================
static void phase2_framing(IpcServer &server, const QString &name)
{
    section(QStringLiteral("【2】五条消息拆成三段发出去：长度前缀把边界找回来"));

    ClientLink link;
    link.connectTo(name);
    if (!premise(QStringLiteral("【2】客户端连上了服务端"),
                 pumpUntil([&]() { return link.isConnected(); }, 2000),
                 QStringLiteral("state = %1").arg(enumName(link.socket()->state()))))
        return;
    if (!premise(QStringLiteral("【2】服务端 accept 到 1 个连接"),
                 pumpUntil([&]() { return server.liveConnections() == 1; }, 2000),
                 QStringLiteral("live = %1").arg(server.liveConnections())))
        return;

    // 临时观察者：只在这一节里收帧，节末断开连接。
    QList<Frame> got;
    const QMetaObject::Connection sub = QObject::connect(
        &server, &IpcServer::frameArrived, &link,
        [&got](QLocalSocket *, quint8 type, const QByteArray &payload) {
            Frame f;
            f.type = type;
            f.payload = payload;
            got.append(f);
        });

    // 五条消息，载荷长度故意参差：8 / 300 / 5 / 3000 / 64
    const QList<QPair<quint8, int>> specs = {
        {1, 8}, {2, 300}, {3, 5}, {4, 3000}, {5, 64}
    };
    QByteArray blob;
    QList<QByteArray> expected;
    for (int i = 0; i < specs.size(); ++i) {
        const QByteArray p = pattern(specs.at(i).second, i);
        expected.append(p);
        blob.append(encodeFrame(specs.at(i).first, p));
    }

    server.clearChunkSizes();
    // 三段切在**帧中间**：7 字节连第一帧（13 字节）都装不下，这是刻意的。
    const QByteArray c1 = blob.left(7);
    const QByteArray c2 = blob.mid(7, 900);
    const QByteArray c3 = blob.mid(907);
    link.socket()->write(c1);
    link.socket()->flush();
    pumpFor(30);                      // 让三段有机会分开到达（能不能分开由内核说了算）
    link.socket()->write(c2);
    link.socket()->flush();
    pumpFor(30);
    link.socket()->write(c3);
    link.socket()->flush();

    if (!premise(QStringLiteral("【2】五条消息在 3 秒内全部到达"),
                 pumpUntil([&]() { return got.size() == 5; }, 3000),
                 QStringLiteral("收到 %1 条").arg(got.size())))
    {
        QObject::disconnect(sub);
        link.close();
        return;
    }

    checkNum(QStringLiteral("【2A】解析出来的消息条数"), got.size(), 5);
    bool sameBytes = true;
    for (int i = 0; i < 5; ++i) {
        if (got.at(i).type != specs.at(i).first || got.at(i).payload != expected.at(i))
            sameBytes = false;
    }
    checkBool(QStringLiteral("【2B】五条消息的类型与载荷逐字节一致"), sameBytes, true);
    checkNum(QStringLiteral("【2C】服务端收到的字节数（应等于发出的字节数）"),
             server.chunkSizes().isEmpty()
                 ? 0
                 : std::accumulate(server.chunkSizes().cbegin(), server.chunkSizes().cend(), 0LL),
             blob.size());
    checkNum(QStringLiteral("【2D】全部解析完之后，解析器缓冲里剩下的字节"), server.lastPendingBytes(), 0);

    // ★ 下面是这一节的「眼见为实」：把到达的形状印出来。
    QStringList sizes;
    for (int n : server.chunkSizes())
        sizes << QString::number(n);
    verdict(Obs, QStringLiteral("【2】服务端每次 readyRead 收到的字节数"), true,
            sizes.join(QStringLiteral(" + ")));
    verdict(Obs, QStringLiteral("【2】readyRead 一共发了几次"), true,
            QString::number(server.readyReadCount()));
    if (!server.chunkSizes().isEmpty()) {
        const int first = server.chunkSizes().first();
        verdict(Obs, QStringLiteral("【2】第一块能装下几条完整消息"), true,
                QStringLiteral("%1 条（第一帧要 5+8=13 字节，这一块只有 %2 字节）")
                    .arg(first >= 13 ? 1 : 0)
                    .arg(first));
    }
    qInfo().noquote() << QStringLiteral(
        "  → 这就是「半包」与「粘包」的全部真相：管道只保证字节顺序，不保证边界。"
        "\n     我这三段切成 7 / 900 / 2495，可对面完全可能一次读到 3402 字节（粘包），"
        "\n     也可能一次只拿到 3 字节（半包）。**帧数守恒靠的是解析器，不是传输层。**");

    // 节末收干净：断开、等它真的消失，免得影响【3】【4】的连接计数。
    QObject::disconnect(sub);
    link.close();
    checkBool(QStringLiteral("【2E】这一节断开之后，服务端回到 0 个连接"),
              pumpUntil([&]() { return server.liveConnections() == 0; }, 2000), true);
}

// ============================================================================
// 【3】坏帧：长度字段是对端说的
// ============================================================================
static void phase3_badFrame(IpcServer &server)
{
    section(QStringLiteral("【3】伪造一个声称 1 GiB 的帧头：拒绝、断开、但不回滚已收到的"));

    ClientLink link;
    link.connectTo(server.fullName());
    if (!premise(QStringLiteral("【3】客户端连上了服务端"),
                 pumpUntil([&]() { return link.isConnected(); }, 2000),
                 QStringLiteral("state = %1").arg(enumName(link.socket()->state()))))
        return;
    if (!premise(QStringLiteral("【3】服务端 accept 到 1 个连接"),
                 pumpUntil([&]() { return server.liveConnections() == 1; }, 2000),
                 QStringLiteral("live = %1").arg(server.liveConnections())))
        return;

    int arrived = 0;
    quint32 refusedClaim = 0;
    const QMetaObject::Connection subFrames = QObject::connect(
        &server, &IpcServer::frameArrived, &link,
        [&arrived](QLocalSocket *, quint8, const QByteArray &) { ++arrived; });
    const QMetaObject::Connection subRefuse = QObject::connect(
        &server, &IpcServer::refusedFrame, &link,
        [&refusedClaim](QLocalSocket *, quint32 claimed) { refusedClaim = claimed; });

    const int framesBefore = server.frameCount();

    // 先发一条好帧，再发一个 1 GiB 的谎，最后补 100 字节垃圾。
    link.send(9, QByteArrayLiteral("这一条是好帧"));
    link.sendRaw(frameHeaderOnly(1u << 30, 0x77));
    link.sendRaw(QByteArray(100, 'x'));

    const bool goodKept = pumpUntil([&]() { return arrived == 1; }, 2000);
    checkBool(QStringLiteral("【3A】坏帧之前那条好帧仍然入账（已解析的不回滚）"), goodKept, true);

    const bool refused = pumpUntil([&]() { return server.rejectedCount() == 1; }, 2000);
    checkBool(QStringLiteral("【3B】坏帧被拒，且只拒一次"), refused, true);
    checkNum(QStringLiteral("【3C】服务端记下的「对端声称的长度」"),
             qint64(server.lastBadLen()), qint64(1u << 30));
    checkNum(QStringLiteral("【3D】服务端收到的帧总数（坏帧没有变成一帧）"),
             server.frameCount(), framesBefore + 1);
    checkBool(QStringLiteral("【3E】服务端把这条连接断掉了（不陪它传完 1 GiB）"),
              pumpUntil([&]() { return server.liveConnections() == 0; }, 3000), true);

    verdict(Obs, QStringLiteral("【3】refusedFrame 信号带来的「声称长度」"), true,
            QStringLiteral("%1 字节（与【3C】的落账值一致 = %2）")
                .arg(refusedClaim)
                .arg(refusedClaim == server.lastBadLen() ? QStringLiteral("一致")
                                                         : QStringLiteral("不一致")));
    verdict(Obs, QStringLiteral("【3】拒绝的理由（服务端原话）"), true, server.lastRejectWhy());
    verdict(Obs, QStringLiteral("【3】客户端这边的经历"), true, link.trail());
    qInfo().noquote() << QStringLiteral(
        "  → 三条防线，缺一条都会被对面按住打："
        "\n     ① 长度上限（64 KiB）—— 不然一行 length 就能让你申请 1 GiB；"
        "\n     ② 超限即断，不读不存 —— 不然攻击者继续喂，你继续攒；"
        "\n     ③ 半包缓存只在「长度合法」之后才建立 —— 非法长度一个字节都不留。");

    QObject::disconnect(subFrames);
    QObject::disconnect(subRefuse);
    link.close();
    pumpUntil([&]() { return server.liveConnections() == 0; }, 2000);
}

// ============================================================================
// 【4】多客户端
// ============================================================================
static void phase4_manyClients(IpcServer &server)
{
    section(QStringLiteral("【4】三个客户端同时说话：每个连接一份解析器，谁生谁删"));

    ClientLink alice, bob, carol;
    alice.connectTo(server.fullName());
    bob.connectTo(server.fullName());
    carol.connectTo(server.fullName());

    if (!premise(QStringLiteral("【4】三个客户端都在 3 秒内连上"),
                 pumpUntil([&]() {
                     return alice.isConnected() && bob.isConnected() && carol.isConnected();
                 }, 3000),
                 QStringLiteral("alice=%1 bob=%2 carol=%3")
                     .arg(alice.isConnected()).arg(bob.isConnected()).arg(carol.isConnected())))
    {
        alice.close();
        bob.close();
        carol.close();
        pumpUntil([&]() { return server.liveConnections() == 0; }, 2000);
        return;
    }

    checkBool(QStringLiteral("【4A】服务端活连接数 = 3"),
              pumpUntil([&]() { return server.liveConnections() == 3; }, 2000), true);
    checkNum(QStringLiteral("【4B】服务端名下挂着 3 个 QLocalSocket 子对象"),
             server.raw()->findChildren<QLocalSocket *>().size(), 3);

    QStringList names;
    const QMetaObject::Connection sub = QObject::connect(
        &server, &IpcServer::frameArrived, &alice,
        [&names](QLocalSocket *, quint8, const QByteArray &payload) {
            names.append(QString::fromUtf8(payload));
        });

    alice.send(2, QByteArrayLiteral("alice"));
    bob.send(2, QByteArrayLiteral("bob"));
    carol.send(2, QByteArrayLiteral("carol"));
    if (!premise(QStringLiteral("【4】三条消息都到了"),
                 pumpUntil([&]() { return names.size() == 3; }, 3000),
                 QStringLiteral("收到 %1 条").arg(names.size())))
    {
        QObject::disconnect(sub);
        alice.close();
        bob.close();
        carol.close();
        return;
    }

    QStringList sorted = names;
    sorted.sort();
    checkBool(QStringLiteral("【4C】三条消息的名字一个不差、一个不多"),
              sorted == QStringList({QStringLiteral("alice"), QStringLiteral("bob"),
                                     QStringLiteral("carol")}),
              true);

    // 掐掉一个，看另外两个还在不在 —— 「一个连接坏了不影响别人」是这类服务的底线。
    alice.close();
    checkBool(QStringLiteral("【4D】alice 断开后，服务端活连接回到 2"),
              pumpUntil([&]() { return server.liveConnections() == 2; }, 3000), true);
    checkBool(QStringLiteral("【4E】那个 socket 对象也没了（deleteLater 生效）"),
              pumpUntil([&]() {
                  return server.raw()->findChildren<QLocalSocket *>().size() == 2;
              }, 2000), true);

    bob.send(2, QByteArrayLiteral("bob-again"));
    checkBool(QStringLiteral("【4F】剩下的连接照常通信"),
              pumpUntil([&]() { return names.size() == 4; }, 3000), true);

    verdict(Obs, QStringLiteral("【4】服务端累计 accept / 断开"), true,
            QStringLiteral("%1 / %2").arg(server.acceptedCount()).arg(server.closedCount()));
    qInfo().noquote() << QStringLiteral(
        "  → 「每个连接一份解析器」不是设计口味，是正确性要求："
        "\n     如果整个服务端共用一份 m_buf，alice 的半包就会被 bob 的字节接上，"
        "\n     你会得到一个「内容看着像乱码、单客户端测试永远复现不了」的 bug。");

    QObject::disconnect(sub);
    bob.close();
    carol.close();
    checkBool(QStringLiteral("【4G】全部断开之后回到 0 个连接"),
              pumpUntil([&]() { return server.liveConnections() == 0; }, 3000), true);
}

// ============================================================================
// 【5】背压：write() 收下 ≠ 对方收到
// ============================================================================
static void phase5_backpressure(const QString &drainName)
{
    section(QStringLiteral("【5】连发 4 MiB：bytesToWrite 里还剩多少、最后是不是一个字节不差"));

    // 【5】不走帧协议，走一条「排水口」：只数字节，不看内容。
    // 这样量的就是纯粹的传输行为，不被解析逻辑干扰。
    QLocalServer drain;
    if (!premise(QStringLiteral("【5】排水口 listen 成功"),
                 drain.listen(drainName),
                 QStringLiteral("名字 = %1").arg(drainName)))
        return;

    // ★ 注意这些变量的声明顺序：sink 先于 drain 声明会怎样？
    //   drain 析构时会把它的子 socket 一起删掉，而那些 socket 的续跑里引用了 sink。
    //   局部变量按逆序析构，所以「被引用者」必须比「引用者」活得久 —— 这里两者都活到节末，
    //   我把它写在注释里，是因为这个错误在真实代码里几乎必然发生一次。
    qint64 drained = 0;
    int drainReads = 0;
    QLocalSocket sink;

    QObject::connect(&drain, &QLocalServer::newConnection, &sink, [&]() {
        while (QLocalSocket *s = drain.nextPendingConnection()) {
            QObject::connect(s, &QLocalSocket::readyRead, &sink, [&drained, &drainReads, s]() {
                const QByteArray block = s->readAll();
                drained += block.size();
                ++drainReads;
            });
        }
    });

    sink.connectToServer(drainName);
    if (!premise(QStringLiteral("【5】排水口连上了"),
                 pumpUntil([&]() { return sink.state() == QLocalSocket::ConnectedState; }, 2000),
                 QStringLiteral("state = %1").arg(enumName(sink.state()))))
    {
        drain.close();
        return;
    }

    const qint64 total = 4 * 1024 * 1024;
    const QByteArray big(total, 'q');      // 内容无关紧要，只数长度

    QElapsedTimer clock;
    clock.start();
    const qint64 wrote = sink.write(big);
    const qint64 backlogRightAway = sink.bytesToWrite();
    const qint64 msToHandOff = clock.elapsed();

    verdict(Obs, QStringLiteral("【5】write() 的返回值 vs 请求字节数"), true,
            QStringLiteral("%1 / %2").arg(wrote).arg(total));
    verdict(Obs, QStringLiteral("【5】写完立刻读 bytesToWrite()（还在路上的字节）"), true,
            QStringLiteral("%1 字节").arg(backlogRightAway));
    verdict(Obs, QStringLiteral("【5】write() 本身花了多少毫秒"), true,
            QStringLiteral("%1 ms").arg(msToHandOff));

    checkBool(QStringLiteral("【5A】排水口最终一个字节不差地收到 4 MiB"),
              pumpUntil([&]() { return drained == total; }, 8000), true);
    checkBool(QStringLiteral("【5B】客户端的发送缓冲最终清零"),
              pumpUntil([&]() { return sink.bytesToWrite() == 0; }, 3000), true);
    verdict(Obs, QStringLiteral("【5】排水口分几次读到、一共转了多少圈"), true,
            QStringLiteral("%1 次 readAll，累计 %2 字节").arg(drainReads).arg(drained));
    verdict(Obs, QStringLiteral("【5】从写完到全部送出去用了多少毫秒"), true,
            QStringLiteral("%1 ms（含上一个核对的等待）").arg(clock.elapsed()));

    qInfo().noquote() << QStringLiteral(
        "  → write() 的返回值只说一件事：「这些字节我收下了，会替你送」。"
        "\n     bytesToWrite() 才是「还有多少没交出去」。所以："
        "\n       · 想让界面不卡：别调 waitForBytesWritten，改听 bytesWritten 信号，边写边让出；"
        "\n       · flush() 只是催一下内核，它不是「保证送达」；"
        "\n       · 真正保证「送到了」的，是**对端确认**（应用层回一个 ack），不是任何本地 API。");

    sink.disconnectFromServer();
    drain.close();
}

// ============================================================================
// 【6】失败路径：error 是原因，disconnected 是结果
// ============================================================================
static void phase6_failureModes(IpcServer &server, const QString &name)
{
    section(QStringLiteral("【6】连不上 vs 被掐掉：两条不同的路径"));

    // ① 连一个根本不存在的名字。
    ClientLink ghost;
    ghost.connectTo(name + QStringLiteral("_nobody"));
    const bool reported = pumpUntil([&]() { return ghost.errors() >= 1; }, 3000);
    checkBool(QStringLiteral("【6A】连不存在的名字，一定会报错"), reported, true);
    verdict(Obs, QStringLiteral("【6】那次失败的 error() / errorString()"), true,
            QStringLiteral("%1 / %2")
                .arg(enumName(ghost.lastError()), ghost.errorText()));
    checkBool(QStringLiteral("【6B】失败之后状态回到 Unconnected"),
              pumpUntil([&]() {
                  return ghost.socket()->state() == QLocalSocket::UnconnectedState;
              }, 2000), true);
    checkNum(QStringLiteral("【6C】它一次都没「连上过」"), ghost.connects(), 0);

    qInfo().noquote() << QStringLiteral(
        "  → 本地套接字的失败码用的是 QAbstractSocket 那一套字母表（qlocalsocket.h 27~41 行"
        "\n     就是逐个赋值过去的）：最常见的是 ServerNotFoundError（名字没人应答）与"
        "\n     ConnectionRefusedError（有人在听但拒了）。本机实测给的是上面那一个；"
        "\n     ★ 具体落到哪个枚举由平台决定，所以我只把「必然报错」记硬账，把枚举值记 Obs。");

    // ② 连上之后，对端被掐掉。
    ClientLink live;
    live.connectTo(name);
    if (!premise(QStringLiteral("【6】第二段：客户端连上了"),
                 pumpUntil([&]() { return live.isConnected(); }, 2000),
                 QStringLiteral("state = %1").arg(enumName(live.socket()->state()))))
        return;
    if (!premise(QStringLiteral("【6】服务端 accept 到 1 个连接"),
                 pumpUntil([&]() { return server.liveConnections() == 1; }, 2000),
                 QStringLiteral("live = %1").arg(server.liveConnections())))
        return;

    const int before = live.disconnects();
    server.abortAll();                    // 服务端这边硬断

    checkBool(QStringLiteral("【6D】对端没了之后，disconnected() 会来"),
              pumpUntil([&]() { return live.disconnects() > before; }, 3000), true);
    checkBool(QStringLiteral("【6E】并且状态回到 Unconnected"),
              pumpUntil([&]() {
                  return live.socket()->state() == QLocalSocket::UnconnectedState;
              }, 2000), true);
    checkBool(QStringLiteral("【6F】服务端那边也认为自己没有连接了"),
              pumpUntil([&]() { return server.liveConnections() == 0; }, 2000), true);
    verdict(Obs, QStringLiteral("【6】客户端的完整经历（顺序是平台决定的）"), true,
            live.trail());

    qInfo().noquote() << QStringLiteral(
        "  → 这两条路径的区别，决定了你的错误处理该写在哪："
        "\n     · errorOccurred 有「原因」，要在那里决定「重连还是报错给用户」；"
        "\n     · disconnected 只有「结果」，对端优雅退出和网络断了在它看来没区别；"
        "\n     · 主动 disconnectFromServer() 只会带来 disconnected，不会有 error。"
        "\n     所以判断「是谁先走」只能靠你自己在 disconnect 之前的标记，套接字不替你记。");
}

// ============================================================================
// 【7】QLockFile：不需要对端的那种互斥
// ============================================================================
static void phase7_lockFile(const QString &name)
{
    section(QStringLiteral("【7】QLockFile：同一份数据，不许多个进程同时改"));

    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString lockPath = QDir(tempDir).filePath(name + QStringLiteral(".lock"));
    verdict(Obs, QStringLiteral("【7】锁文件放在哪"), true, lockPath);

    QLockFile first(lockPath);
    QLockFile second(lockPath);

    checkBool(QStringLiteral("【7A】第一把锁拿到了"), first.tryLock(0), true);
    checkBool(QStringLiteral("【7B】isLocked() 同意"), first.isLocked(), true);
    checkBool(QStringLiteral("【7C】同一路径的第二把锁必须失败"), second.tryLock(0), false);
    verdict(Obs, QStringLiteral("【7】第二把锁的 error()"), true,
            lockErrorName(second.error()));

    qint64 pid = 0;
    QString host;
    QString app;
    const bool haveInfo = first.getLockInfo(&pid, &host, &app);
    checkBool(QStringLiteral("【7D】getLockInfo() 能读出持锁者"), haveInfo, true);
    checkNum(QStringLiteral("【7E】读出来的 pid 就是本进程"), pid,
             QCoreApplication::applicationPid());
    verdict(Obs, QStringLiteral("【7】读出来的 host / app"), true,
            QStringLiteral("%1 / %2").arg(host, app));
    verdict(Obs, QStringLiteral("【7】staleLockTime 默认值"), true,
            QStringLiteral("%1 ms").arg(first.staleLockTime()));

    first.unlock();
    checkBool(QStringLiteral("【7F】放锁之后第二把立刻能拿到"), second.tryLock(0), true);

    qint64 pid2 = 0;
    QString host2;
    QString app2;
    second.getLockInfo(&pid2, &host2, &app2);
    checkBool(QStringLiteral("【7G】锁文件里的人换成了第二把的持有者"),
              pid2 == QCoreApplication::applicationPid(), true);
    second.unlock();
    QFile::remove(lockPath);

    qInfo().noquote() << QStringLiteral(
        "  → QLockFile 和 QLocalServer 互斥的分工："
        "\n     · QLockFile 表达「这段临界区只许一个进程进」，它不负责通信，"
        "\n       也不需要有人在听 —— 进程合法退出会解锁，但如果进程被强杀，"
        "\n       锁文件会留下，靠「超过 staleLockTime 就接管」这条规则自愈；"
        "\n     · QLocalServer::listen 表达「这个名字只许一个进程占」，"
        "\n       而它天然随进程消失（Windows 管道随进程回收，Unix 上文件可能残留，【8】讲）；"
        "\n     · 你要「单实例 + 能把参数传给已经在跑的那个」，用后者；"
        "\n       你要「同一份数据文件不许多开」，用前者。两个都要，就两个都上。");
}

// ============================================================================
// 【8】收尾与选型
// ============================================================================
static void phase8_cleanup(IpcServer &server, const QString &name, const QString &drainName)
{
    section(QStringLiteral("【8】关掉之后能不能立刻同名重开？removeServer 在清什么？"));

    server.abortAll();
    server.stop();                        // 只关「还能不能进来」，不等于掐断已有的连接
    checkBool(QStringLiteral("【8A】stop 之后 isListening() 为假"), server.isListening(), false);
    checkBool(QStringLiteral("【8B】abortAll 之后没有活连接了"),
              pumpUntil([&]() { return server.liveConnections() == 0; }, 2000), true);

    // ★ 这个问题很多人在论坛上问：「我 close 了，为什么同名再 listen 还是失败？」
    //   答案取决于平台在 close 时有没有把名字一起清掉 —— 这一段的实现细节
    //   在 .cpp 里，头文件看不见，所以只能实测，而且两种结果都合理 → Obs。
    QLocalServer again;
    const bool reopened = again.listen(name);
    verdict(Obs, QStringLiteral("【8】同一进程里 stop 之后直接同名重开"), reopened,
            reopened ? QStringLiteral("成功（平台在 close 时把名字一起清了）")
                     : QStringLiteral("失败：%1（名字还留着，需要 removeServer）")
                           .arg(again.errorString()));
    if (reopened)
        again.close();

    // removeServer 的返回值含义是「删到了吗/本来就没有」，两个平台不一样 → Obs。
    const bool removed = QLocalServer::removeServer(name);
    verdict(Obs, QStringLiteral("【8】removeServer(名字) 的返回值"), removed,
            removed ? QStringLiteral("true") : QStringLiteral("false"));

    // ★ 这一条可以记硬账，而且理由很值得学：**两条分支通向同一个结论**。
    //   如果 again 刚才没开成功，就是残留把名字占了，removeServer 清掉后必能开；
    //   如果本来就没有残留，removeServer 是空操作，listen 同样必能开。
    //   所以不管走哪条分支，这里都该成功 —— 「分支穷尽」也是硬账的一种来源。
    QLocalServer third;
    const bool thirdOk = third.listen(name);
    checkBool(QStringLiteral("【8C】removeServer 之后同名一定又能 listen"), thirdOk, true);
    if (thirdOk)
        third.close();
    QLocalServer::removeServer(name);
    QLocalServer::removeServer(drainName);

    checkBool(QStringLiteral("【8D】收尾之后本进程里没有活着的监听"),
              !server.isListening() && !third.isListening(), true);

    qInfo().noquote() << QStringLiteral(
        "  → 平台差异，记在这里省得你以后半夜查："
        "\n     Windows：名字是 \\\\.\\pipe\\<name>，进程退出时管道对象由内核回收，"
        "\n              所以「崩了留下名字」这种事基本不会发生；"
        "\n     Linux/macOS：名字是 $TMPDIR 下的一个文件，进程崩了文件留在地上，"
        "\n              下次 listen 时 Qt 会尝试连一下判断「有没有人在听」，"
        "\n              连不上就当陈旧文件清掉 —— 所以多数时候你不必手动 removeServer，"
        "\n              但**在容器/只读临时目录里**这一步可能失败，那就真要手动清；"
        "\n     macOS 额外一条：$TMPDIR 是「每用户每会话」的，"
        "\n              所以别把绝对路径写死在配置里，用 QStandardPaths 每次算出来；"
        "\n     sun_path 长度上限（Linux 108 字节）：名字太长会 listen 失败，"
        "\n              长名字请改用哈希。");
}

// ============================================================================
// 七、合账
// ============================================================================
static void reconcile()
{
    section(QStringLiteral("合账"));
    const int broken = gLedger.failed + gLedger.stalled;
    qInfo().noquote() << QStringLiteral("  硬账 %1 项：对上 %2，不符 %3；整节作废 %4；总闸%5")
                             .arg(gLedger.total)
                             .arg(gLedger.total - broken)
                             .arg(gLedger.failed)
                             .arg(gLedger.stalled)
                             .arg(gLedger.watchdogFired ? QStringLiteral("跳了")
                                                        : QStringLiteral("没跳"));
    qInfo().noquote() << QStringLiteral("  退出码 = %1（%2）")
                             .arg(gLedger.exitCode())
                             .arg(gLedger.exitCode() == 0
                                      ? QStringLiteral("没有硬账不符、没有整节作废")
                                      : (broken > 0 ? QStringLiteral("有硬账不符或整节作废")
                                                    : QStringLiteral("一项都没核过")));
    if (gLedger.exitCode() != 0)
        qWarning().noquote() << QStringLiteral("  ★ 请按上面的 [!! ] / [作废] / 前提 行往下查。");
}

// ============================================================================
// 八、main：一条时间轴，节与节之间靠「断开并等它消失」解耦
// ============================================================================
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("threads_08_local_ipc"));

    qInfo() << "================================================================";
    qInfo() << "threads 第 8 讲 · 本地进程间通信（Qt =" << QString::fromLatin1(QT_VERSION_STR) << "）";
    qInfo() << "================================================================";

    // ★ 名字里带 pid：CI 上并行跑两份也不会撞名。
    //   这一条不是洁癖 —— 撞名会表现为「第一次跑好好的，第二次莫名连到了上次的残骸」。
    const QString serverName =
        QStringLiteral("qtlp_ipc_08_%1").arg(QCoreApplication::applicationPid());
    const QString drainName = serverName + QStringLiteral("_drain");

    IpcServer server;

    QTimer::singleShot(0, &app, [&]() {
        phase1_startServer(server, serverName);
        phase2_framing(server, serverName);
        phase3_badFrame(server);
        phase4_manyClients(server);
        phase5_backpressure(drainName);
        phase6_failureModes(server, serverName);
        phase7_lockFile(serverName);
        phase8_cleanup(server, serverName, drainName);
        QCoreApplication::quit();
    });

    // 总闸：正常路径约 2~4 秒跑完，这是它的 5 倍以上。
    // ★ 用不带 context 的那一族 singleShot：带 context 的版本在对象先死时就不发了，
    //   而总闸恰恰要在「事情不对劲、对象都乱了」的时候还能响。
    QTimer::singleShot(20000, []() {
        gLedger.watchdogFired = true;
        qWarning().noquote() << QStringLiteral("【总闸】20 秒到点：强制收尾");
        qInfo().noquote() << QStringLiteral("      到点时硬账已到 %1 项").arg(gLedger.total);
        QCoreApplication::quit();
    });

    app.exec();

    reconcile();
    const int rc = gLedger.exitCode();
    qInfo().noquote() << QStringLiteral("进程退出码 = %1").arg(rc);
    return rc;
}

// ============================================================================
// 九、速查：背不住就回来查
// ============================================================================
/*
 * 本地 IPC 的五种手段，先选对再写代码：
 *   QLocalSocket / QLocalServer   双向消息，要自己定帧        ← 桌面软件内部通信，首选
 *   QTcpSocket 连 127.0.0.1       双向消息，有端口、过防火墙   ← 要跟别的语言/别的机器打通时才用
 *   QLockFile                     只表达「谁在用」，不传数据  ← 单实例、独占数据文件
 *   QSharedMemory + QSystemSemaphore  大块数据 + 自己同步     ← 共享几十 MB 才划算，坑也多
 *   QProcess 的 stdin/stdout      单向或双向的字节流，对端是子进程 ← 同一套父子关系里最省事
 *
 * 帧协议的三个必写项（照着【2】【3】抄）：
 *   长度前缀（网络字节序） + 长度上限（kPayloadCap） + 半包缓存（每条连接一份）
 *
 * 本地套接字的状态与信号：
 *   connectToServer()      异步，立刻返回；结果看 connected / errorOccurred
 *   state()                Unconnected / Connecting / Connected / Closing（没有 Listening）
 *   error()                ServerNotFound / ConnectionRefused / PeerClosed / SocketAccess ...
 *   disconnected()         只告诉你「没了」，不告诉你「为什么没的」
 *   abort()                立刻断，写缓冲丢掉
 *   disconnectFromServer() 优雅：先把写缓冲发完
 *
 * 五个不许：
 *   · 不许把 readAll() 的结果当成「一条消息」——它是**若干条 + 半条**
 *   · 不许信任对端给的长度（先比上限，再分配）
 *   · 不许整个服务端共用一份解析缓冲（每条连接一份）
 *   · 不许在 GUI 线程里 waitFor*（要等就在事件循环里等，带期限）
 *   · 不许在 disconnected 的槽里 delete socket（用 deleteLater）
 *
 * 一个必须记住的平台差异：
 *   Windows 管道名 = kernel 对象，随进程回收；Unix socket = 文件，会残留。
 *   所以「名字用完了要不要清」在两边答案不一样，别把一边的经验带到另一边。
 */

// ============================================================================
// 十、收尾话：这一讲真正教的不是套接字，是「边界」
// ----------------------------------------------------------------------------
// 从【2】到【4】，我一直在做同一件事：把一个**没有结构的东西**（字节流）
// 变成一个**有结构的东西**（帧）。中间那层薄薄的东西就是协议，而它的全部内容是：
//   「我打算怎么切、切错了怎么办、切到一半怎么办」。
//
// 你以后会遇到的每一个「通信」问题 —— HTTP、WebSocket、串口、共享内存、
// 甚至两个线程之间的一个队列 —— 都是这三问的变体。
// Qt 只给了你管道；边界永远是你自己给的。
// ============================================================================

// ============================================================================
// 十一、练习：从「看懂了」到「写得出」
// ----------------------------------------------------------------------------
/*
 * 1. 把【2】的三段切点改成 13（正好切在第一帧之后）和 13+305（正好切在第二帧之后）。
 *    重跑，看【2】的三条 Obs。回答：为什么「帧数守恒」这条硬账不受切点影响，
 *    而「第一块能装下几条」立刻变了？（提示：一个是守恒量，一个是观测值。）
 *
 * 2. 给协议加一个「应答」类型：客户端发 type=2 的请求，服务端回 type=0x82 的应答，
 *    载荷是「我收到了 N 字节」。要求：
 *      · 客户端用 QTimer 做 500 ms 超时；
 *      · 超时之后既不发第二次请求，也不能让程序挂住；
 *      · 把「超时」也记成一条硬账（服务端故意不回的那一条）。
 *    做完回答：为什么这个超时要写在客户端，而不是等服务端的 errorOccurred？
 *
 * 3. 把 kPayloadCap 改成 4，重跑【2】。会发生什么？哪一条硬账先红？
 *    然后回答：**上限该由谁来定** —— 由服务端防御性地定（不管对端发什么），
 *    还是由协议文档定死（两边一致）？两种做法各自会出什么事故？
 *
 * 4. 给【4】加一个「广播」：服务端收到 type=9 的消息时，把它原样转发给**除来源之外**的
 *    所有连接。要求：转发时不能往已断开的 socket 上写（提示：检查 state()），
 *    并且统计「因为对端已断而放弃发送」的次数，记成一条 Obs。
 *
 * 5. 用【7】的 QLockFile 实现「程序已经在跑就别再开一个」，然后把
 *    「已经在跑的那个收到了什么参数」这一步用【1】的 QLocalServer 补上。
 *    两段拼起来就是大多数桌面软件的单实例逻辑。
 *    回答：为什么这两件事不能合成一件（为什么 QLockFile 不能传参数）？
 *
 * 6. 资深题：给 FrameParser 加一个「流控」——当 for (;;) 循环一次切出超过 1000 帧时，
 *    先停下来，把剩下的留在缓冲里，等调用方说「接着切」。要求：
 *      · 不改接口语义（takeFrame 还是取一帧）；
 *      · 写一个小测试：往解析器里喂 5000 条帧的字节，确认一条不少、一条不重；
 *      · 说明这个改动防的是什么（提示：一个 60 MB 的粘包一次性切完，
 *        会在**一次事件循环回调里**占住主线程多久？）。
 *
 * 7. 找一段你项目里真实的进程间通信代码（哪怕是临时文件 + 轮询那种），用本讲的三个问题审它：
 *      (a) 消息边界是怎么定的？定错了一半的帧会怎样？
 *      (b) 对端给的长度/数量，有没有上限？
 *      (c) 对端消失的时候，我这边的对象谁负责删、什么时候删？
 *    把答案写成一段评审意见。这一题不写代码，但它比前面六题都用得上。
 */
// ============================================================================

// 本文件有 2 个 Q_OBJECT 类（IpcServer / ClientLink），AUTOMOC 会生成 .moc 并在这里包含。
#include "08_local_ipc.moc"
