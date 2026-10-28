// ============================================================================
// 第 5 讲 · 名字与加密：从「localhost」到一个可信的连接，中间有三道门
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   前四讲都在跟「一个已知地址 + 明文」打交道。真实世界里两件事不一样：
//     · 你不知道地址，你只知道**名字**（api.example.com），得先问人；
//     · 明文的连接谁都能看、谁都能改，所以要在它上面再套一层加密和身份验证。
//   Qt 把这两件事分别放在 QHostInfo 和 QSslSocket 里，而它们的共同点是：
//   **都没有「超时」这个参数**（qhostinfo.h 第 56~92 行那一串 lookupHost 全都没有
//   毫秒参数；QSslSocket 的握手要靠你自己数时间）。所以这一讲有一半在讲
//   「怎么给一个不给你期限的 API 加上期限」。
//
//   八节地图（每一节都可运行、可打印、硬账进退出码）：
//     1) 同步解析：QHostInfo::fromName 一句话拿到地址，代价是**堵住当前线程**。
//     2) 异步解析：lookupHost 的重载、lookupId 怎么和结果对上，
//        以及「QHostInfo 不会替你超时」。
//     3) 查不到的时候：这是常态不是异常。这一节用保留域名 .invalid 演示，
//        并且证明**我们的期限一定生效**（这条是硬账，因为期限是我们自己写的）。
//     4) QHostAddress 的五个事实：isLoopback / isNull / IPv4 与 IPv6 /
//        LocalHost 与 Any 的区别。全离线、全同步，所以这一节的硬账最多。
//     5) TLS 能力探测：supportsSsl、后端名、密码套件、系统根证书。
//        「我的程序能不能上 TLS」只有一个判据，就是 supportsSsl()。
//     6) 默认配置的纪律：校验模式默认**不是** VerifyNone，而且你把副本改坏
//        不会污染全局默认（QSslConfiguration 是值类型）。
//     7) 一次真实的握手失败：让本地 TCP 服务端说一句 HTTP，让 TLS 客户端去接。
//        你会看到「不是证书有问题，是根本不是 TLS」这种错误长什么样。
//     8) 收尾：三道门 + 一张照着抄的连接清单。
//
//   ★ 运行前须知：
//     · 只在回环上建连接（QHostAddress::LocalHost + 端口 0），一个包都不出本机，
//       断网也能跑完。
//     · 不测真实 DNS 服务器的响应时间（那取决于你家的网络），也不测真实证书链
//       （那需要一张证书；本讲只探测能力与配置，握手只演示失败）。
//     · 所有等待都有期限；总闸 25 秒。
// ============================================================================

// ============================================================================
// 一、头文件地图
// ----------------------------------------------------------------------------
// QtNetwork/qhostinfo.h
//   第 25~29 行 enum HostInfoError { NoError, HostNotFound, UnknownError }
//              ← ★ 只有三个值。别再去找「DNS 服务器没响应」那种细分错误码，
//                Qt 不给你，它对上层只保证「查到了 / 没这个名 / 说不清」。
//              ★★ 而且它是**裸枚举**，没有 Q_ENUM。所以 QMetaEnum::fromType 用不了，
//                 本讲只能手写一个 switch 把名字打出来 —— 我第一版就是用
//                 fromType 写的，编译器的 static_assert 当场顶回来（qmetaobject.h 310 行）。
//   第 31 行 explicit QHostInfo(int lookupId = -1)   ← 结果对象是可以「造」出来的
//   第 43/44 行 addresses() / setAddresses()
//   第 46/47 行 error() / setError()
//   第 49/50 行 errorString() / setErrorString()
//   第 53 行 int lookupId() const        ← ★ 异步查询的「取号单」
//   第 56/58 行 static int lookupHost(name, receiver, member)   两个重载（const / 非 const）
//   第 59 行 static void abortHostLookup(int id)
//              ← ★ 名字叫 abort，语义是「别再回调我了」，不是「把 DNS 请求撤回」。
//                查询本身在系统解析器里跑，Qt 管不着。
//   第 61 行 static QHostInfo fromName(const QString &name)      ← 同步，会堵
//   第 62/63 行 localHostName() / localDomainName()
//   第 68/72/87 行 lookupHost 的 lambda 版本（带/不带 context）
//              ← ★ 全都没有 timeout 参数。这就是本讲反复讲的「自己加期限」。
//
// QtNetwork/qhostaddress.h
//   第 32 行 enum SpecialAddress { Null, LocalHost, LocalHostIPv6, Broadcast, Any, AnyIPv4, AnyIPv6 }
//   第 44 行 bool isNull() const           ← 解析失败的判据
//   第 50 行 bool isLoopback() const
//   第 60 行 Protocol protocol() const     ← IPv4Protocol / IPv6Protocol
//
// QtNetwork/qsslsocket.h
//   第 41~47 行 enum PeerVerifyMode { VerifyNone, QueryPeer, VerifyPeer, AutoVerifyPeer }
//              ← ★ 四个值，不是三个。我第一版按三个写，头文件当场纠正我。
//              AutoVerifyPeer 的意思是「客户端按 VerifyPeer，服务端按 VerifyNone」。
//   第 54/55 行 connectToHostEncrypted(...)      ← ★ 注意它是「连上之后再握」，不是「加密连接」
//   第 66/67 行 mode() / isEncrypted()           ← 握手成没成，看这两个
//   第 72/73 行 peerVerifyMode() / setPeerVerifyMode()
//   第 78/79 行 peerVerifyName() / setPeerVerifyName()   ← 证书比的是这个名字，不是 IP
//   第 120 行 waitForEncrypted(int msecs = 30000)         ← 阻塞版，GUI 线程里别用
//   第 127 行 static bool supportsSsl()                   ← ★ 唯一判据
//   第 129 行 static QString sslLibraryVersionString()
//   第 133/134 行 availableBackends() / activeBackend()
//   第 143/149 行 ignoreSslErrors(...) 两个重载          ← 危险开关，见【6】
//   第 154 行 void sslErrors(const QList<QSslError> &)   ← 握手/证书出错的通知
//
// QtNetwork/qsslconfiguration.h
//   第 62/63 行 protocol() / setProtocol()
//   第 66/67 行 peerVerifyMode() / setPeerVerifyMode()
//   第 89~92 行 ciphers() / setCiphers() / supportedCiphers()
//   第 95/96 行 caCertificates() / setCaCertificates()
//   第 103 行 static QList<QSslCertificate> systemCaCertificates()
//   第 129 行 static QSslConfiguration defaultConfiguration()
//              ← ★ 这是全局默认，改它会改掉整个进程里所有新连接的行为。
//                但 QSslConfiguration 是**值类型**：拷贝一份再改，不污染默认。
//
// QtNetwork/qssl.h
//   第 52~73 行 enum SslProtocol；★ TlsV1_0 / TlsV1_1 已经标了 QT_DEPRECATED，
//              新代码一律用 TlsV1_2OrLater / TlsV1_3OrLater。
//              （本文件里绝不写那两个弃用名 —— 引用它们会触发 -Wdeprecated-declarations，
//                而本仓库的门槛是 0 警告。）
//
// 跨讲链接：
//   · network 第 1 讲：QNetworkAccessManager 默认就开着校验，本讲【6】讲它背后的开关。
//   · network 第 3 讲：TCP 的连、断、错误信号；TLS 是「连上之后再套一层」，所以
//     这一讲只讲「套」的那一段。
// ============================================================================

// ============================================================================
// 二、口径与账本
// ----------------------------------------------------------------------------
//   Hard —— 本地算得出来的（地址对象、枚举值、配置对象）+ 我自己写的期限。
//   Soft —— 依赖网络环境或 TLS 后端（系统根证书、Schannel 给不给密码套件列表）。
//   Obs  —— 版本号、后端名、错误文本：只报，不承诺。
//
// ★ 这一讲有一条纪律值得单独说：**凡是「外面给的结果」都不记硬账。**
//   DNS 查不查得到、Schannel 有没有密码套件列表、OpenSSL 报什么文本，
//   都取决于对方的版本和你的网络。硬账只记「我这边的代码一定做得到」的事。
// ============================================================================

#include <QCoreApplication>
#include <QHostInfo>          // qhostinfo.h：25~29 错误枚举、53 lookupId、56~92 lookupHost、61 fromName
#include <QHostAddress>       // qhostaddress.h：44 isNull、50 isLoopback、60 protocol
#include <QTcpServer>         // 【7】【8】本地回环服务端，端口 0 让系统挑
#include <QTcpSocket>
#include <QSslSocket>         // qsslsocket.h：127 supportsSsl、41~47 PeerVerifyMode、154 sslErrors
#include <QSslConfiguration>  // qsslconfiguration.h：129 defaultConfiguration、103 systemCaCertificates
#include <QSslCipher>
#include <QSslCertificate>
#include <QSslError>
#include <QSsl>               // qssl.h：52~73 SslProtocol（Q_ENUM_NS，所以能打出名字）
#include <QTimer>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QMetaEnum>
#include <QDebug>
#include <QString>
#include <QStringList>
#include <QList>

#include <functional>

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

// 事件循环 + 期限。形状与 threads 第 7/8 讲同一个；生产代码请改成状态机。
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

template <typename E>
static QString enumName(E value)
{
    const QMetaEnum me = QMetaEnum::fromType<E>();
    const char *key = me.valueToKey(int(value));
    return key ? QString::fromLatin1(key) : QStringLiteral("<未登记 %1>").arg(int(value));
}

// QHostInfo::HostInfoError 没有 Q_ENUM（qhostinfo.h 25~29 行），只能手写。
static QString hostErrorName(QHostInfo::HostInfoError e)
{
    switch (e) {
    case QHostInfo::NoError:     return QStringLiteral("NoError");
    case QHostInfo::HostNotFound: return QStringLiteral("HostNotFound");
    case QHostInfo::UnknownError: return QStringLiteral("UnknownError");
    }
    return QStringLiteral("<未登记的 %1>").arg(int(e));
}

// ============================================================================
// 【1】同步解析
// ============================================================================
static void phase1_fromName()
{
    section(QStringLiteral("【1】QHostInfo::fromName：一句话拿到地址，代价是堵住当前线程"));

    // ★ 为什么敢用 localhost：它必须能从 hosts 文件里查出来，不需要任何外部 DNS。
    //   用别的名字等于把这一讲的成败押在你的网络上。
    const QHostInfo info = QHostInfo::fromName(QStringLiteral("localhost"));
    if (!premise(QStringLiteral("【1】localhost 解析成功"),
                 info.error() == QHostInfo::NoError,
                 QStringLiteral("error = %1，errorString = %2")
                     .arg(hostErrorName(info.error()), info.errorString())))
        return;

    checkBool(QStringLiteral("【1A】解析出了至少一个地址"), info.addresses().isEmpty(), false);

    QStringList texts;
    bool allLoopback = !info.addresses().isEmpty();
    for (const QHostAddress &a : info.addresses()) {
        texts << a.toString();
        if (!a.isLoopback())
            allLoopback = false;
    }
    verdict(Obs, QStringLiteral("【1】localhost 解析成了什么"), true,
            texts.join(QStringLiteral(", ")));
    checkBool(QStringLiteral("【1B】localhost 的每个地址都是回环地址"), allLoopback, true);

    verdict(Obs, QStringLiteral("【1】本机名 / 域"), true,
            QStringLiteral("%1 / %2")
                .arg(QHostInfo::localHostName(),
                     QHostInfo::localDomainName().isEmpty()
                         ? QStringLiteral("<空>")
                         : QHostInfo::localDomainName()));

    qInfo().noquote() << QStringLiteral(
        "  → fromName() 是**同步**的：这行代码返回之前，你的线程什么都干不了。"
        "\n     在网络好的时候它 1 毫秒就回来了，在 DNS 抽风的时候它能堵你 20 秒。"
        "\n     所以界面线程里不许调它 —— 一条规矩，没有例外。");
}

// ============================================================================
// 【2】异步解析
// ============================================================================
static void phase2_lookupHost(QCoreApplication &app)
{
    section(QStringLiteral("【2】lookupHost：异步，但没有超时参数"));

    int calls = 0;
    int resultId = -1;
    QHostInfo result;
    const QElapsedTimer clock = []() {
        QElapsedTimer t;
        t.start();
        return t;
    }();

    // ★ lookupId 是这一节的第二个重点：异步世界里，你必须能证明
    //   「回来的这个结果」就是「我刚才发出去的那一次查询」。
    const int issued = QHostInfo::lookupHost(
        QStringLiteral("localhost"), &app, [&](const QHostInfo &info) {
            ++calls;
            resultId = info.lookupId();
            result = info;
        });

    const bool arrived = pumpUntil([&]() { return calls > 0; }, 3000);
    if (!premise(QStringLiteral("【2】回调在 3 秒内回来了"), arrived,
                 QStringLiteral("calls = %1，真实耗时 %2 ms").arg(calls).arg(clock.elapsed())))
        return;

    checkNum(QStringLiteral("【2A】回调恰好被调用 1 次"), calls, 1);
    checkNum(QStringLiteral("【2B】结果里的 lookupId 就是发出去的那个号"),
             resultId, issued);
    checkBool(QStringLiteral("【2C】结果可用"), result.error() == QHostInfo::NoError, true);
    verdict(Obs, QStringLiteral("【2】这次异步查询花了多少毫秒"), true,
            QString::number(clock.elapsed()));

    qInfo().noquote() << QStringLiteral(
        "  → 注意 qhostinfo.h 第 56~92 行：六个 lookupHost 重载，**没有一个带毫秒参数**。"
        "\n     Qt 把「等多久」这件事留给你，是因为它没法替你决定 —— 界面上「转圈圈 3 秒」"
        "\n     和后台「重试 30 秒」是两种完全不同的产品决策。"
        "\n     abortHostLookup(id)（第 59 行）只保证「不再回调你」，不保证查询被撤回。");
}

// ============================================================================
// 【3】查不到的时候
// ============================================================================
static void phase3_lookupFails(QCoreApplication &app)
{
    section(QStringLiteral("【3】查不到不是异常，是常态：给查询加一个自己的期限"));

    // .invalid 是 RFC 2606 保留的顶级域，规范保证它永远不会存在。
    // 用它来演示「失败」，就不用担心哪天这个域名被谁注册了。
    int calls = 0;
    QHostInfo result;
    QElapsedTimer clock;
    clock.start();
    const int issued = QHostInfo::lookupHost(
        QStringLiteral("qt-learning-path.invalid"), &app, [&](const QHostInfo &info) {
            ++calls;
            result = info;
        });

    const bool arrived = pumpUntil([&]() { return calls > 0; }, 2500);
    const qint64 waited = clock.elapsed();

    // ★ 这条是硬账，而且理由非常干净：期限是**我们的代码**写的，不是对面给的。
    //   2500 ms 的预算 + 一点调度余量，一定在 2600 ms 内返回。
    checkBool(QStringLiteral("【3A】我们自己设的期限一定生效（不会无限等）"),
              waited <= 2600, true);
    verdict(Obs, QStringLiteral("【3】期限到点时，结果到了没有"), arrived,
            QStringLiteral("等了 %1 ms，calls = %2").arg(waited).arg(calls));

    if (arrived) {
        // 到了但失败 —— 这才是「正常路径上的失败」。具体是哪一种取决于你的网络：
        // 有 DNS 服务器时多半是 HostNotFound（NXDOMAIN），断网时可能是 UnknownError。
        verdict(Soft, QStringLiteral("【3B】失败原因是「没这个名」"),
                result.error() == QHostInfo::HostNotFound,
                QStringLiteral("%1（断网时也可能是 UnknownError，这不算错）")
                    .arg(hostErrorName(result.error())));
        verdict(Obs, QStringLiteral("【3】失败时 errorString"), true, result.errorString());
        checkBool(QStringLiteral("【3C】失败时地址列表是空的"), result.addresses().isEmpty(), true);
    } else {
        // 没到 —— 那就放弃它。abortHostLookup 之后即使回调来了，我们也不再理。
        QHostInfo::abortHostLookup(issued);
        verdict(Obs, QStringLiteral("【3】超时之后调用了 abortHostLookup"), true,
                QStringLiteral("id = %1").arg(issued));
        const int before = calls;
        pumpUntil([&]() { return false; }, 400);      // 再放它 400 ms，看会不会回头
        verdict(Obs, QStringLiteral("【3】abort 之后回调还会不会来"), calls > before,
                QStringLiteral("多来了 %1 次").arg(calls - before));
    }

    qInfo().noquote() << QStringLiteral(
        "  → 三条经验："
        "\n     ① 名字解析失败是**正常**返回，不是异常，不需要 try/catch，看 error() 就行；"
        "\n     ② 你自己的期限到了就继续往下走（报错给用户 / 走缓存 / 降级），"
        "\n        别指望 abortHostLookup 让系统解析器停下来；"
        "\n     ③ 同一个名字别反复查 —— 加一层缓存（QHash<名字, 地址 + 时间戳>），"
        "\n        否则用户每敲一个字你就发一次查询。");
}

// ============================================================================
// 【4】地址对象本身的五个事实
// ============================================================================
static void phase4_hostAddress()
{
    section(QStringLiteral("【4】QHostAddress：全离线、全同步的五个事实"));

    const QHostAddress v4(QStringLiteral("127.0.0.1"));
    checkBool(QStringLiteral("【4A】\"127.0.0.1\" 解析成功（不是 null）"), v4.isNull(), false);
    checkBool(QStringLiteral("【4B】它是回环地址"), v4.isLoopback(), true);
    checkBool(QStringLiteral("【4C】它是 IPv4"), v4.protocol() == QAbstractSocket::IPv4Protocol, true);

    const QHostAddress v6(QStringLiteral("::1"));
    checkBool(QStringLiteral("【4D】\"::1\" 也是回环地址"), v6.isLoopback(), true);
    checkBool(QStringLiteral("【4E】它是 IPv6"), v6.protocol() == QAbstractSocket::IPv6Protocol, true);

    const QHostAddress bad(QStringLiteral("999.1.1.1"));
    checkBool(QStringLiteral("【4F】非法字符串给出 null 地址（不抛异常）"), bad.isNull(), true);

    const QHostAddress local(QHostAddress::LocalHost);
    checkBool(QStringLiteral("【4G】QHostAddress::LocalHost 就是 127.0.0.1"),
              local.toString() == QStringLiteral("127.0.0.1"), true);
    verdict(Obs, QStringLiteral("【4】LocalHost / Any / AnyIPv6 三个常数"), true,
            QStringLiteral("%1 / %2 / %3")
                .arg(local.toString(), QHostAddress(QHostAddress::Any).toString(),
                     QHostAddress(QHostAddress::AnyIPv6).toString()));

    // 绑端口 0 = 「让系统挑一个空闲端口」。这是本地测试的标准做法：
    // 不写死端口就不会撞车，也不需要「端口被占了怎么办」的代码。
    QTcpServer server;
    const bool listening = server.listen(QHostAddress::LocalHost, 0);
    if (!premise(QStringLiteral("【4】回环上 listen(端口 0) 成功"), listening,
                 QStringLiteral("errorString = %1").arg(server.errorString())))
        return;
    checkBool(QStringLiteral("【4H】系统真的给了一个非零端口"), server.serverPort() != 0, true);
    verdict(Obs, QStringLiteral("【4】系统挑的端口"), true,
            QStringLiteral("%1（下次运行会不一样）").arg(server.serverPort()));
    verdict(Obs, QStringLiteral("【4】绑完之后 serverAddress()"), true,
            server.serverAddress().toString());
    server.close();
    checkBool(QStringLiteral("【4I】close() 之后不再监听"), server.isListening(), false);

    qInfo().noquote() << QStringLiteral(
        "  → 「监听 Any 还是 LocalHost」是一个安全问题，不是一个口味问题："
        "\n     Any 意味着同一局域网里的人也能连进来（Windows 上还会弹防火墙对话框），"
        "\n     LocalHost 只有本机能连。测试、单实例通信、进程间调用一律用后者。");
}

// ============================================================================
// 【5】TLS 能力探测
// ============================================================================
static void phase5_sslCapability()
{
    section(QStringLiteral("【5】TLS 能力探测：能不能上 TLS，只看 supportsSsl()"));

    if (!premise(QStringLiteral("【5】本机 Qt 支持 TLS"), QSslSocket::supportsSsl(),
                 QStringLiteral("supportsSsl() = false：多半是没装 TLS 后端（OpenSSL / Schannel）")))
        return;

    verdict(Obs, QStringLiteral("【5】TLS 库版本"), true, QSslSocket::sslLibraryVersionString());
    verdict(Obs, QStringLiteral("【5】编译时的后端（availableBackends）"), true,
            QSslSocket::availableBackends().join(QStringLiteral(", ")));
    verdict(Obs, QStringLiteral("【5】运行时实际用的后端（activeBackend）"), true,
            QSslSocket::activeBackend());

    const QList<QSslCipher> ciphers = QSslConfiguration::supportedCiphers();
    // ★ Soft：Schannel 后端可以不枚举密码套件（由系统策略决定），这时列表是空的 ——
    //   不是错，是后端的选择。所以不记硬账。
    verdict(Soft, QStringLiteral("【5A】能枚举出密码套件"), !ciphers.isEmpty(),
            QStringLiteral("%1 个").arg(ciphers.size()));
    if (!ciphers.isEmpty())
        verdict(Obs, QStringLiteral("【5】第一个密码套件"), true,
                QStringLiteral("%1（%2 位密钥）")
                    .arg(ciphers.first().name()).arg(ciphers.first().encryptionMethod()));

    const QList<QSslCertificate> systemCerts = QSslConfiguration::systemCaCertificates();
    verdict(Soft, QStringLiteral("【5B】系统根证书库非空"), !systemCerts.isEmpty(),
            QStringLiteral("%1 张（容器/精简系统里可能是空的，那就得自己带 CA）")
                .arg(systemCerts.size()));
    verdict(Obs, QStringLiteral("【5】默认配置当前带的 CA 数量"), true,
            QString::number(QSslConfiguration::defaultConfiguration().caCertificates().size()));

    qInfo().noquote() << QStringLiteral(
        "  → supportsSsl() 是**运行时**的答案，不是编译期的：同一个 exe 在装了 OpenSSL 的机器上"
        "\n     返回 true，在没装的机器上返回 false。所以桌面程序发布时必须带上 TLS 后端，"
        "\n     或者至少准备一个「没有 TLS 也能用」的降级路径 —— 而不是假设它一定在。");
}

// ============================================================================
// 【6】默认配置的纪律
// ============================================================================
static void phase6_defaultConfig()
{
    section(QStringLiteral("【6】默认配置：校验不能关，改副本不能污染全局"));

    if (!premise(QStringLiteral("【6】本机 Qt 支持 TLS"), QSslSocket::supportsSsl(),
                 QStringLiteral("supportsSsl() = false")))
        return;

    const QSslConfiguration def = QSslConfiguration::defaultConfiguration();

    // ★ 硬账①：默认校验模式不能是 VerifyNone —— 这是「出厂就不能明文裸奔」的不变式。
    checkBool(QStringLiteral("【6A】默认校验模式不是 VerifyNone"),
              def.peerVerifyMode() != QSslSocket::VerifyNone, true);
    verdict(Obs, QStringLiteral("【6】默认校验模式"), true, enumName(def.peerVerifyMode()));

    // ★ 硬账②：这个枚举只有四个值。我第一版按三个写，编译器当场纠正过我 ——
    //   这一条留着，提醒你「凭印象数枚举」有多不可靠。
    const QMetaEnum verifyEnum = QMetaEnum::fromType<QSslSocket::PeerVerifyMode>();
    checkNum(QStringLiteral("【6B】PeerVerifyMode 的取值个数"), verifyEnum.keyCount(), 4);

    verdict(Obs, QStringLiteral("【6】默认协议"), true, enumName(def.protocol()));
    // ★ 硬账③：「不早于 TLS 1.2」。这里只列非弃用的枚举名（写 TlsV1_0 会触发弃用警告）。
    const QSsl::SslProtocol p = def.protocol();
    const bool modern = p == QSsl::TlsV1_2 || p == QSsl::TlsV1_2OrLater
                        || p == QSsl::TlsV1_3 || p == QSsl::TlsV1_3OrLater
                        || p == QSsl::SecureProtocols || p == QSsl::AnyProtocol;
    checkBool(QStringLiteral("【6C】默认协议不早于 TLS 1.2（Qt6 已不支持 SSLv3）"), modern, true);

    // ★ 硬账④：QSslConfiguration 是值类型 —— 改副本不动全局。
    //   这一条比它看起来重要：很多人的「临时关一下校验」最后变成了全进程关校验，
    //   因为他们改的是 defaultConfiguration() 返回的那个对象（它是副本，但有人
    //   直接 setDefaultConfiguration 了，那就真的是全局）。
    QSslConfiguration copy = def;
    copy.setPeerVerifyMode(QSslSocket::VerifyNone);
    checkBool(QStringLiteral("【6D】副本上的修改生效了"),
              copy.peerVerifyMode() == QSslSocket::VerifyNone, true);
    checkBool(QStringLiteral("【6E】全局默认没被污染"),
              QSslConfiguration::defaultConfiguration().peerVerifyMode() == def.peerVerifyMode(),
              true);

    qInfo().noquote() << QStringLiteral(
        "  → 关于 ignoreSslErrors()（qsslsocket.h 143/149 行）的两句话："
        "\n     · 它是**给自签名/内网证书**准备的逃生门，不是给「调试方便」准备的；"
        "\n     · 用它必须带参数（只忽略你确认过的那一条 QSslError），"
        "\n       无参数版本等于「谁冒充都行」—— 中间人只要一张任意证书就能进来。"
        "\n     还有一个更常见的错：证书里写的是域名，你却用 IP 去连。"
        "\n     校验比的是 peerVerifyName（第 78/79 行），不是路由地址。");
}

// ============================================================================
// 【7】一次真实的握手失败
// ============================================================================
static void phase7_handshakeFails()
{
    section(QStringLiteral("【7】让本地 TCP 服务端说一句 HTTP，让 TLS 客户端去接"));

    QTcpServer server;
    if (!premise(QStringLiteral("【7】回环上 listen 成功"),
                 server.listen(QHostAddress::LocalHost, 0),
                 QStringLiteral("errorString = %1").arg(server.errorString())))
        return;

    QSslSocket client;
    int sslErrorCount = 0;
    QStringList sslErrorTexts;
    int socketErrorCount = 0;
    QString socketErrorText;

    // 服务端：accept 之后不握手，直接说一句明文 HTTP。
    // ★ 这就是「443 端口上跑了个 80 服务」的样子，运维最常见的错配之一。
    QObject::connect(&server, &QTcpServer::newConnection, &client, [&]() {
        while (QTcpSocket *peer = server.nextPendingConnection()) {
            peer->write("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
            peer->flush();
        }
    });
    QObject::connect(&client, &QSslSocket::sslErrors, &client,
                     [&](const QList<QSslError> &errors) {
                         sslErrorCount += int(errors.size());
                         for (const QSslError &e : errors)
                             sslErrorTexts << e.errorString();
                     });
    QObject::connect(&client, &QSslSocket::errorOccurred, &client,
                     [&](QAbstractSocket::SocketError) {
                         ++socketErrorCount;
                         socketErrorText = client.errorString();
                     });

    client.connectToHostEncrypted(QStringLiteral("127.0.0.1"), server.serverPort());
    const bool failed = pumpUntil(
        [&]() { return !client.isEncrypted() && (sslErrorCount > 0 || socketErrorCount > 0); },
        4000);

    if (!premise(QStringLiteral("【7】4 秒内观察到了失败（sslErrors 或 errorOccurred）"),
                 failed,
                 QStringLiteral("sslErrors = %1，socketError = %2，isEncrypted = %3")
                     .arg(sslErrorCount).arg(socketErrorCount).arg(client.isEncrypted())))
    {
        client.abort();
        server.close();
        return;
    }

    checkBool(QStringLiteral("【7A】握手没有成功：isEncrypted() 为假"), client.isEncrypted(), false);
    checkBool(QStringLiteral("【7B】连接最终没能保持（回到 Unconnected）"),
              pumpUntil([&]() {
                  return client.state() == QAbstractSocket::UnconnectedState;
              }, 2000), true);
    checkBool(QStringLiteral("【7C】至少有一个错误信号到过"),
              sslErrorCount > 0 || socketErrorCount > 0, true);

    verdict(Obs, QStringLiteral("【7】QSslSocket 的错误文本"), true,
            socketErrorText.isEmpty() ? QStringLiteral("<没有 socket 错误>") : socketErrorText);
    if (!sslErrorTexts.isEmpty())
        verdict(Obs, QStringLiteral("【7】sslErrors 的每一条"), true,
                sslErrorTexts.join(QStringLiteral(" | ")));
    else
        verdict(Obs, QStringLiteral("【7】sslErrors 的每一条"), true,
                QStringLiteral("<这次没走 sslErrors，走的是 socket 错误>"));

    qInfo().noquote() << QStringLiteral(
        "  → 请把这句话记住：**TLS 握手发生在 TCP 连接建立之后**。"
        "\n     所以客户端先看到「连上了」，然后才开始谈加密。"
        "\n     如果对面说的不是 TLS，你看到的是「记录类型/版本号不对」，"
        "\n     而不是「证书无效」—— 这两类错误指向完全不同的故障："
        "\n       · 版本/记录错误 → 端口错了，或者对面根本不是 HTTPS；"
        "\n       · 证书错误     → 连接是对的地方，但身份不对（自签名、过期、域名不符）。"
        "\n     排查顺序永远是：先确认端口对，再看证书。");

    client.abort();
    server.close();
}

// ============================================================================
// 【8】收尾：三道门
// ============================================================================
static void phase8_summary()
{
    section(QStringLiteral("【8】从名字到可信连接，一共三道门"));

    QTcpServer server;
    if (!premise(QStringLiteral("【8】回环上再 listen 一个端口"),
                 server.listen(QHostAddress::LocalHost, 0),
                 QStringLiteral("errorString = %1").arg(server.errorString())))
        return;

    // 门① 名字 → 地址
    const QHostInfo info = QHostInfo::fromName(QStringLiteral("localhost"));
    checkBool(QStringLiteral("【8A】门①：名字解析成功且给出地址"),
              info.error() == QHostInfo::NoError && !info.addresses().isEmpty(), true);

    // 门② 地址 → 连接（这里只验证「服务端确实在那个地址上听着」）
    checkBool(QStringLiteral("【8B】门②：服务端在回环地址上听着"),
              server.isListening() && server.serverAddress().isLoopback(), true);

    // 门③ 连接 → 可信（能不能上 TLS）
    checkBool(QStringLiteral("【8C】门③：本机具备 TLS 能力"), QSslSocket::supportsSsl(), true);
    checkBool(QStringLiteral("【8D】而且默认就要校验证书"),
              QSslConfiguration::defaultConfiguration().peerVerifyMode()
                  != QSslSocket::VerifyNone,
              true);

    server.close();

    qInfo().noquote() << QStringLiteral(
        "  照着抄的连接清单（客户端）："
        "\n    1. 名字解析：QHostInfo::lookupHost + 自己的 QTimer 期限；"
        "\n    2. 连接：QSslSocket::connectToHostEncrypted(host, port)，"
        "\n       注意传的是**名字**不是 IP（证书校验比名字）；"
        "\n    3. 等 encrypted() 信号，而不是 connected —— connected 只说明 TCP 通了；"
        "\n    4. 连 sslErrors()：发生即失败，除非你精确地忽略了某一条；"
        "\n    5. 收尾用 disconnectFromHost()，别 abort()（abort 会丢写缓冲）。"
        "\n  服务端："
        "\n    1. 必须同时有证书和私钥（localCertificate / privateKey），否则握手必失败；"
        "\n    2. 用 QSslServer（Qt 6.4+）或 QTcpServer + startServerEncryption；"
        "\n    3. 客户端证书校验是另一道门（QSslConfiguration::setCaCertificates）；"
        "\n    4. 只监听需要的地址，不要 listen(Any) 图省事。");
}

// ============================================================================
// 三、合账
// ============================================================================
static void reconcile()
{
    section(QStringLiteral("合账"));
    const int broken = gLedger.failed + gLedger.stalled;
    qInfo().noquote() << QStringLiteral("  硬账 %1 项：对上 %2，不符 %3；整节作废 %4")
                             .arg(gLedger.total)
                             .arg(gLedger.total - broken)
                             .arg(gLedger.failed)
                             .arg(gLedger.stalled);
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
// 四、main
// ============================================================================
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("network_05_dns_and_ssl"));

    qInfo() << "================================================================";
    qInfo() << "network 第 5 讲 · 名字与加密（Qt =" << QString::fromLatin1(QT_VERSION_STR) << "）";
    qInfo() << "================================================================";

    QTimer::singleShot(0, &app, [&]() {
        phase1_fromName();
        phase2_lookupHost(app);
        phase3_lookupFails(app);
        phase4_hostAddress();
        phase5_sslCapability();
        phase6_defaultConfig();
        phase7_handshakeFails();
        phase8_summary();
        QCoreApplication::quit();
    });

    QTimer::singleShot(25000, []() {
        gLedger.watchdogFired = true;
        qWarning().noquote() << QStringLiteral("【总闸】25 秒到点：强制收尾");
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
// 五、速查
// ============================================================================
/*
 * 名字解析（qhostinfo.h）：
 *   QHostInfo::fromName(name)                 同步，堵线程；只在后台线程或启动时用
 *   QHostInfo::lookupHost(name, ctx, fn)      异步；**没有 timeout 参数**
 *   QHostInfo::abortHostLookup(id)            只是「别再回调」，不是撤回查询
 *   info.error()                              NoError / HostNotFound / UnknownError，只有三个
 *   info.lookupId()                           把结果和「我发的那次查询」对上
 *
 * 地址对象（qhostaddress.h）：
 *   isNull()            解析失败
 *   isLoopback()        127.0.0.1 / ::1
 *   protocol()          IPv4Protocol / IPv6Protocol
 *   LocalHost           只有本机能连（测试、IPC 一律用它）
 *   Any                 真机上等于「局域网都能连」—— 想想再写
 *   端口传 0             让系统挑一个空闲端口，serverPort() 读回来
 *
 * TLS（qsslsocket.h / qsslconfiguration.h）：
 *   supportsSsl()       能不能上 TLS 的**唯一**判据（运行时，不是编译期）
 *   connectToHostEncrypted(host, port)  先 TCP 再握手；等的是 encrypted() 不是 connected()
 *   isEncrypted()       握手成没成
 *   peerVerifyMode()    默认不是 VerifyNone；改成 None 等于关掉身份验证
 *   peerVerifyName()    证书校验比的是它，不是 IP
 *   ignoreSslErrors()   逃生门；必须带具体错误列表，否则中间人随便进
 *   systemCaCertificates()  系统根证书；空的话自己带 CA
 *
 * 报错时看什么：
 *   「记录类型/版本号不对」   → 端口错了 / 对面不是 TLS
 *   「证书已过期」           → 时间不对，或对方该换证了
 *   「主机名不匹配」         → 你连的是 IP，或者证书写的不是这个域名
 *   「自签名证书」           → 内网证书，需要把 CA 加进 caCertificates
 *
 * 一条铁律：
 *   界面线程里不出现 fromName()、waitForEncrypted()、waitForReadyRead() 任何一个。
 */

// ============================================================================
// 六、练习
// ----------------------------------------------------------------------------
/*
 * 1. 把【2】的 3000 ms 期限改成 100 ms，重跑。看【2A】会不会红、退出码是什么。
 *    然后回答：**为什么「期限太短」在教材里是失败，在真实代码里可能只是「这次没查到」？**
 *    （提示：本讲的契约是「localhost 必须查得到」，这是个很少失败的操作。）
 *
 * 2. 给【3】加一层缓存：QHash<QString, QPair<QList<QHostAddress>, qint64>>，
 *    命中且未过期（比如 60 秒）就直接返回。要求：缓存命中时不许发起查询，
 *    并用一个计数器证明「第二次查同一个名字，lookupHost 一次都没调」。
 *
 * 3. 写一个函数 `bool isAddressForThisMachine(const QHostAddress &)`：
 *    用 QHostInfo::fromName(QHostInfo::localHostName()) 拿到本机所有地址，
 *    判断给定地址是不是其中之一。回答：这个函数为什么在「绑定 0.0.0.0」的机器上
 *    会给出让你意外的答案？
 *
 * 4. 把【7】改成「服务端先正常握手、再发一句明文」：用 QSslSocket 双向都试试。
 *    观察握手成功之后发明文会怎样。回答：**握手成功之后，这条连接上的
 *    明文还能不能被中间人看见？**（这一题的答案决定你有没有理解 TLS 的边界。）
 *
 * 5. 用 QSslConfiguration::supportedProtocols()（如果本机后端支持）打印一遍，
 *    然后写一个「最低版本」检查函数：给定一个 QSslConfiguration，判断它允许的最低
 *    TLS 版本是否 >= 1.2。要求在 Qt6 里只用非弃用枚举名。
 *
 * 6. 资深题：设计一个「首次连接信任（TOFU）」的策略，用于连接你自己的内网服务：
 *    第一次连时把服务器证书的指纹存下来（用 QSslCertificate::digest），
 *    之后每次比对指纹，不一致就拒绝。
 *    要求：用 sslErrors 的正确姿势实现（不是 ignoreSslErrors() 无参版本），
 *    并写清楚「证书轮换时用户会看到什么」。这一题不写完整代码，
 *    但要把「哪一步可能骗到你」写出来。
 */
// ============================================================================

// 本文件没有 Q_OBJECT 类（全部是框架类的组合，没有新增信号槽），
// 所以末尾不需要 #include "05_dns_and_ssl.moc"。
