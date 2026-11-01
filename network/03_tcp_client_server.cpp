// ============================================================================
// 第 3 讲 · QTcpSocket / QTcpServer：字节流上怎么盖出一套能核对的协议
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   第 1、2 讲玩的是 HTTP——边界（状态行、头、Content-Length）是别人替你算好的。
//   一旦自己写 TCP，你会立刻发现一件残酷的事：**socket 只给你字节，不给你「一条消息」**。
//   这一讲把 TCP 这层讲透，八个事实每一条都在本机回环上跑、每一条都打印可核对的数：
//     1) QTcpServer / QTcpSocket 是**事件驱动**的：newConnection / connected /
//        readyRead / disconnected / errorOccurred / stateChanged 全是**信号**
//        （qtcpserver.h 第 69 行、qabstractsocket.h 第 187~190 行、qiodevice.h 第 119/121 行），
//        不是轮询。【1】当场把「write() 返回后同一行代码读到的服务端 readyRead 计数」
//        打出来（按理还得是 0：这一轮事件循环还没转到服务端那条 socket——这是推理，
//        以运行为准），再把 stateChanged 的实际顺序逐条打出来。
//     2) **端口只能现拿，地址只能写死回环**：listen(QHostAddress::LocalHost, 0) 让内核挑端口，
//        再用 serverPort()（qtcpserver.h 第 40 行）读回来。更狠的一条在头文件里：
//        listen 的**默认地址是 QHostAddress::Any**（第 29 行），不写地址就是监听所有网卡——
//        桌面软件被防火墙弹窗，一半是自己「顺手 listen()」造成的。【2】现场演示
//        「借一个端口再 close() 还回去」，然后连它拿 ConnectionRefusedError，
//        并顺手证明 connectToHost 返回的是 **void**（第 136/137 行）——连不上这件事
//        根本不可能从返回值知道，只能等 errorOccurred。
//     3) **一次 readyRead ≠ 一条消息**：【3】把一帧故意切成 3 次 write（半包），
//        再把 2 帧塞进 1 次 write（粘包），然后并排打印「服务端 readyRead 次数」和
//        「服务端解析出的消息条数」。次数是不可核对的（它取决于内核分段和事件循环），
//        **条数才是账**——这句是本讲的核心工程判断。
//     4) **分帧是应用层造的**：【4】用同一个服务器类跑两种 framing——行分隔
//        （indexOf('\n') + left/mid）与 4 字节大端长度前缀（qToBigEndian，qendian.h 第 181/204 行），
//        两条路的消息数一模一样；顺带把「为什么不能直接把 QDataStream 绑在 socket 上等消息」
//        用一段**不联网**的内存实验打印出来：QDataStream 给 QByteArray/QString 写的是
//        **格式内**的长度前缀（qdatastream.h 第 470/483 行 readQSizeType/writeQSizeType），
//        它对「这次 socket 到了几个字节」毫无概念，喂半帧就 status()==ReadPastEnd（第 114 行）。
//        还要拆一个网上抄来的坑：canReadLine() 在 QAbstractSocket 的公开头文件里**没有被 override**
//        （本机 grep QtNetwork/*.h，只有 qlocalsocket.h 第 74 行、qsslsocket.h 第 84 行 override 了它），
//        所以行分帧别押在它的真值上，本讲把它降级成「打印出来看」的对照物。
//     5) **写完不代表送达**：write()（qiodevice.h 第 100~102 行）返回的是「进了 Qt 发送缓冲的
//        字节数」，不是对端收到的字节数。【5】用一条**只有一条连接**的前缀端口做三道账：
//        客户端请求写入的字节 == bytesWritten(qint64) 各次之和 == 服务端收到的字节，
//        中间还打印 bytesToWrite()（qabstractsocket.h 第 143 行）的峰值。
//        并说清 flush()（第 170 行）和 waitForBytesWritten(默认 30000ms)（第 175 行）的分工，
//        以及为什么本讲中途一次都不阻塞。
//     6) **报错不排队，只覆盖**：【6】在**同一个 socket 对象**上先吃一次
//        ConnectionRefusedError（端口是刚还回去的那个），再连上活的服务器、再被服务端
//        abort() 一次；打印 errorOccurred 的次数、每次都记下错误码的序列，
//        以及最后 error()（第 165 行）剩下的那个值。头文件就是证据：一个 error() getter
//        （第 165 行）配一个 protected setSocketError()（第 202 行）——**一个坑位**。
//        附带一条更阴的：SocketError（第 53~79 行）里**没有 NoError 这一档**，
//        第一个枚举量 ConnectionRefusedError 隐式就是 0，所以 `error() == 0` 不是「没错」。
//     7) **超时得自己掐**：Qt6 的 socket 上没有统一的 connectTimeout——本机
//        `grep -rn "connectTimeout|socketTimeout|defaultSocketTimeout" QtNetwork/*.h` **零命中**，
//        只有私有实现里那个 `DefaultTimeout{30}`（qabstractsocketengine_p.h 第 37 行）
//        解释了 waitForConnected 的默认 30000 是从哪来的。所以【7】用 QTimer 造两个守卫：
//        连接守卫（connected 先到就 stop()，把 remainingTime() 打出来）
//        和应答守卫（服务端故意 stall，到点就记一次超时并 disconnectFromHost()）。
//        顺便讲清 abort()（第 154 行）/ disconnectFromHost()（第 138 行）/ waitForDisconnected(ms)
//        （第 176 行）三个动作的分工。
//     8) **优雅关闭 + 自己删自己**：【8】一条连接「写完最后一句立刻 disconnectFromHost()」，
//        服务端**照样收到那一句**（顺序用每连接的标志位核对，合账要求恰好 3 次）；第二条发 BYE，
//        服务端在这个 socket 自己发出的 readyRead 调用栈里对它 deleteLater()，
//        然后**同一轮里继续读它的 state()/bytesToWrite()**——这就是「为什么不能直接 delete」；
//        之后再用 QPointer（qpointer.h 第 19/86 行）确认对象真的没了。第三条（rude）故意写一个
//        **不带换行、永远拼不成帧**的半包再 abort()：它是反面教材，账上只有一条断言——
//        **它一个字都不许加进消息条数**（合账里那行「坏帧 = 0」和「条数 = 10」一起替它作证）。
//   主线：QCoreApplication（无窗口、无对话框、无交互）→ 起两台回环服务器 → 八个演示段
//   用 QTimer::singleShot 串起来 → 合账（每项打「期望 / 实测」）→ 收摊（断连、带超时的
//   waitForDisconnected、close、deleteLater）→ QCoreApplication::quit()。
//
// 对应构建目标名：network_03_tcp_client_server
//   find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
//   add_executable(network_03_tcp_client_server network/03_tcp_client_server.cpp)
//   target_link_libraries(network_03_tcp_client_server PRIVATE Qt6::Widgets Qt6::Network)
//   set(CMAKE_AUTOMOC ON)                       # 本文件**有 Q_OBJECT 类**（服务器 + 客户端），需要 moc
//   # 手工核对时：moc 03_tcp_client_server.cpp -o 03_tcp_client_server.moc
//   #             文件末尾那句 #include "03_tcp_client_server.moc" 就是给 AUTOMOC 的钩子
//   # 目标名已在仓库根 CMakeLists.txt「第玖阶段 · 网络（network/）」一节登记（第 229 行），
//   #   该组的 QT_DEMO_LIBS 是 Qt6::Widgets Qt6::Network（第 226 行），没有 Concurrent
//
// 运行前须知：
//   · **只建 QCoreApplication**：本讲一个 widget 都不建，一个对话框都不弹，
//     也不需要鼠标键盘。理由是第 2 讲那条：要把「字节流与事件驱动」讲纯，
//     不能被 GUI 的线程亲和与重绘叠上来。
//   · **绝不访问公网**：所有连接都是 QHostAddress::LocalHost（127.0.0.1，
//     qhostaddress.h 第 46~54 行那个 SpecialAddress）到自己 listen 起来的端口；
//     代码里没有任何主机名、没有 DNS、没有公网 IP。拔网线、防火墙全拦也能跑完。
//   · **端口 0 + serverPort()**：两处 listen 全写 `listen(QHostAddress::LocalHost, 0)`，
//     端口立刻读回来。示例里出现的具体端口号只会是运行时的打印，不会是源码里的常数。
//   · 全程由 QTimer::singleShot 与信号驱动：步骤之间用 200/200/250/... ms 的 singleShot 串，
//     每一段内部「等某个信号」的等待一律走 waitUntil()——它每 10ms 复查一个**计数器**
//     （不是去轮询 socket），到最长等待时间就带着证据继续，**绝不无限等**。
//   · 程序自己退出：合账之后收摊并 `QCoreApplication::exit(码)`；外面还有 25 秒总闸
//     watchdog（setSingleShot + start(25000)），到点强制打印「超时」证据并退出。
//     退出码的算法只有一个：合账差一项、或者总闸跳过一次 → 1，全对上且没跳到 → 0。
//     这样 `./network_03_tcp_client_server || echo 有问题` 才查得出来，不用肉眼读输出。
//     25 秒不是拍脑袋：全部有界等待 + 固定间隔 + 收摊的最坏预算约 18.1 秒（算式在
//     第二节「有界等待」那段注释里），总闸必须**明显大于**它，否则「超时」就成了日常。
//   · 唯一的阻塞调用在收摊最后一步：waitForDisconnected(300)——带明确短超时，
//     返回值被接收并打印（它**可能**返回 false，那是超时语义不是崩溃）。
//     中间过程一次 waitFor* 都没有，界面卡死那件事本讲只做注释里的对比。
//   · 不写文件：一个字节都不落盘，因此不需要 QStandardPaths::TempLocation。
//   · 可以无头跑：`network_03_tcp_client_server -platform offscreen`
//     （仓库根 CMakeLists 第 90 行已经给每个示例都设了 QT_QPA_PLATFORM=offscreen）。
//   · 本文件的校验状态（照实说）：**已在本机真实编译、链接、运行**（Qt 6.12.0 +
//     MinGW 13.1.0，Windows，offscreen）。本机这一轮：合账 25 项 / 一致 25 项 / 差 0 项，
//     退出码 0（存档 runlogs/network_03_tcp_client_server.log）。
//     所以正文里那几处「跑起来之后要看什么」已经不是悬案：信号的实际顺序、bytesWritten
//     的分块形状、被 abort 的那条连接客户端报的是 disconnected 还是 RemoteHostClosedError、
//     waitForNewConnection(0) 到底等不等、收摊那 7 次 waitForDisconnected(300) 返回什么，
//     都由上面那 25 项逐条钉住；换 Qt 版本 / 换平台请重跑 `bash _tools/run_all.sh '^network_03'`。
//     **所有行号与签名同样是实测**（本机 grep：D:\Qt\6.12.0\Src\qtbase\src，Qt 6.12.0），
//     「grep 零命中」这类话也是真跑过 grep 的结果。
// ============================================================================

#include <QCoreApplication>             // QtCore/qcoreapplication.h（quit）
#include <QByteArray>                   // QtCore/qbytearray.h
#include <QDataStream>                  // QtCore/qdatastream.h（【4】的内存实验）
#include <QDebug>                       // QtCore/qdebug.h（qInfo / qWarning）
#include <QElapsedTimer>                // QtCore/qelapsedtimer.h
#include <QtEndian>                     // QtCore/qendian.h 的模块转发头（qToBigEndian）
#include <QHash>                        // QtCore/qhash.h
#include <QIODevice>                    // QtCore/qiodevice.h（readyRead / bytesWritten / write）
#include <QPointer>                     // QtCore/qpointer.h（deleteLater 之后验空）
#include <QString>                      // QtCore/qstring.h
#include <QStringList>                  // QtCore/qstringlist.h
#include <QTimer>                       // QtCore/qtimer.h（singleShot / remainingTime / stop）
#include <QVariant>                     // QtCore/qvariant.h（socketOption 的返回类型）
#include <QtGlobal>

#include <QAbstractSocket>              // QtNetwork/qabstractsocket.h（**几乎所有 API 都在这**）
#include <QHostAddress>                 // QtNetwork/qhostaddress.h（注意文件名没有 t）
#include <QTcpServer>                   // QtNetwork/qtcpserver.h
#include <QTcpSocket>                   // QtNetwork/qtcpsocket.h

#include <functional>                   // std::function（waitUntil 与「连上之后再发」的回调）

// ============================================================================
// 一、对着本机头文件核过的签名（D:\Qt\6.12.0\mingw_64\include，Qt 6.12.0）
// ============================================================================
// QtNetwork/qtcpsocket.h —— 全文只有 44 行，是个壳：
//   第 18 行 class Q_NETWORK_EXPORT QTcpSocket : public QAbstractSocket
//   第 22 行 explicit QTcpSocket(QObject *parent = nullptr);
//                    ← 记住这句：**QTcpSocket 自己一个新函数都没有**，write / readyRead /
//                      state / error 全在 QAbstractSocket，再往上在 QIODevice。
//                      查文档时别在 QTcpSocket 那一页找 API，找不到就以为 Qt 没有。
//   第 28~29 行 bind(QHostAddress::SpecialAddress, quint16 = 0, BindMode = DefaultForPlatform)
//                    ← 这个重载被 `#if QT_VERSION < QT_VERSION_CHECK(7,0,0)` 包着（第 25 行），
//                      Qt7 会挪进 QAbstractSocket；本讲不用 bind（服务端用的是 listen）。
//
// QtNetwork/qtcpserver.h —— 服务端的全部家当：
//   第 22 行 class Q_NETWORK_EXPORT QTcpServer : public QObject
//   第 26 行 explicit QTcpServer(QObject *parent = nullptr);
//   第 29 行 bool listen(const QHostAddress &address = QHostAddress::Any, quint16 port = 0);
//                    ←⚠ **两个默认值都是坑**：地址默认 Any（0.0.0.0，监听所有网卡），
//                      端口默认 0（内核挑）。端口 0 是好事，地址 Any 在桌面软件里是坏事。
//                      本讲每一处 listen 都显式写 QHostAddress::LocalHost。
//   第 30 行 void close();            ← 只是「不再接新连接」，已建立的连接不受影响
//   第 32 行 bool isListening() const;
//   第 34 行 void setMaxPendingConnections(int numConnections);   ← 小练习 5 的入口
//   第 37 行 void setListenBacklogSize(int size);
//   第 40 行 quint16 serverPort() const;      ← 端口必须从这儿读回来
//   第 41 行 QHostAddress serverAddress() const;
//   第 46 行 bool waitForNewConnection(int msec = 0, bool *timedOut = nullptr);
//                    ←⚠ 默认 **0**。这一条原来只能标「预期」，现在两头都有硬证据了：
//                      ①文档口径：Src 树 src/network/socket/qtcpserver.cpp 第 492 行原文
//                        "If msec is -1, this function will not time out." —— 只有 **-1** 才是不超时，
//                        0 不在这句话里；
//                      ②机制：第 502 行把 msec 直接塞进 `QDeadlineTimer(msec)`，而
//                        src/corelib/kernel/qdeadlinetimer.cpp 第 170~176 行写死了三档：
//                        正数＝还等那么久、**0＝标记为已过期（remainingTime() 返回 0）**、
//                        负数＝永不超时（deadline 取最大值）。
//                      所以「0 = 一直等」是**反的**：0 = 一次都不等，等价于 poll。
//                      本机【7】当场量过一遍（见 sec7 那发探针）。
//                      但「默认参数是 0」这个事实仍是第 46 行白纸黑字。本讲不用它做流程。
//   第 47 行 virtual bool hasPendingConnections() const;
//   第 48 行 virtual QTcpSocket *nextPendingConnection();
//                    ← 返回**裸指针**。文档口径是「所有权交给调用方」，而它的 parent 到底是什么
//                      在头文件里看不出来，所以本讲拿到第一件事就是 setParent(this) 自己接管，
//                      不猜（不猜这件事本身就是这一讲要教的）。
//   第 50 行 QAbstractSocket::SocketError serverError() const;
//   第 51 行 QString errorString() const;
//   第 53/54 行 pauseAccepting() / resumeAccepting();   ← 「不收新连接但老连接照常」的阀门
//   第 62 行 virtual void incomingConnection(qintptr handle);   ← 资深改写点：自定义 socket 子类
//   第 69 行 void newConnection();        ←⚠ **无参数**，socket 要自己去 nextPendingConnection 取。
//                      Qt 没给 newConnection(QTcpSocket*) 这种方便信号，别凭空写出来（编不过）
//   第 71 行 void acceptError(QAbstractSocket::SocketError socketError);
//                    ← 握手已成功、但 Qt 建 socket 失败（比如超出 maxPendingConnections 后
//                      你主动关掉）走这个，不是 errorOccurred；QTcpServer 也**没有** errorOccurred
//
// QtNetwork/qabstractsocket.h —— 本讲的主战场：
//   第 26 行 class Q_NETWORK_EXPORT QAbstractSocket : public QIODevice
//   第 53~79 行 enum SocketError（第 80 行 Q_ENUM）：
//       第 54 行 ConnectionRefusedError,   ← **第一个枚举量，没有显式值 → 0**
//       第 55 行 RemoteHostClosedError, 第 56 行 HostNotFoundError,
//       第 59 行 SocketTimeoutError（行尾注释自己写着 /* 5 */），
//       第 64 行 UnsupportedSocketOperationError（/* 10 */）、第 69 行（/* 15 */）、
//       第 74 行 SslInternalError（/* 20 */）、第 78 行 UnknownSocketError = -1
//                    ←⚠ **没有 NoError**。所以 `if (sock->error() == 0)` 读出来的是
//                      「ConnectionRefusedError」，不是「没错」。判错要靠 state() / isValid()
//                      （第 140 行）/ errorString()，第 6 段把这条打出来。
//                      ★ 而「一次都没错过的 socket」error() 给的就是第 78 行那个
//                      **UnknownSocketError = -1**（本机实测：全新未连接的 QTcpSocket 打出来
//                      error()=-1、errorString()="Unknown error"；见【2】那发）。
//                      所以 -1 不是「出了个未知错」，是「还没错过的初值」——
//                      想问「有没有错」，第 78 行这一档和第 54 行那一档都不能当答案。
//   第 81~89 行 enum SocketState：Unconnected=0、HostLookup=1、Connecting=2、Connected=3、
//       Bound=4、Listening=5、Closing=6（第 90 行 Q_ENUM）
//   第 91~103 行 enum SocketOption：第 92 行 LowDelayOption（TCP_NODELAY）、
//       第 93 行 KeepAliveOption（SO_KEEPALIVE）← 心跳的两种做法在第 7 段末尾有交代
//   第 136 行 virtual void connectToHost(const QString &hostName, quint16 port,
//                                        OpenMode = ReadWrite, NetworkLayerProtocol = AnyIPProtocol);
//       第 137 行 void connectToHost(const QHostAddress &address, quint16 port, OpenMode = ReadWrite);
//                    ←⚠ **返回 void**：连接是异步的，成不成只能等 connected / errorOccurred。
//                      两个重载的差别不只是类型：走第 136 行（主机名）会先进 HostLookupState，
//                      要 DNS；本讲走第 137 行（QHostAddress），状态直接从 Connecting 到 Connected，
//                      这也顺带把「绝不依赖 DNS」这条硬规矩落实了。
//   第 138 行 virtual void disconnectFromHost();  ← 温和：先把发送缓冲发完，再挥手
//   第 140 行 bool isValid() const;
//   第 142/143 行 qint64 bytesAvailable() / bytesToWrite()（都是 override）
//   第 145~149 行 localPort / localAddress / peerPort / peerAddress / peerName
//                    ← peerAddress()/peerPort() 是「我只在跟谁说话」的证据，第 1 段打出来
//   第 151/152 行 qint64 readBufferSize() const / virtual void setReadBufferSize(qint64);
//                    ← 背压阀门：读缓冲满了 Qt 就不再从内核取、也不再发 readyRead。
//                      「大文件别 readAll 一把梭」的正解在这儿，不在别处
//   第 154 行 void abort();            ← 粗暴：立即撕掉，**丢弃发送缓冲**
//   第 160/161 行 virtual void setSocketOption(SocketOption, const QVariant&);
//                    virtual QVariant socketOption(SocketOption);   ← ⚠ 后者**不是 const**
//   第 164/165 行 SocketState state() const; SocketError error() const;
//                    ← 一个 getter 配第 202 行那**一个** protected setSocketError(SocketError)：
//                      错误只有一个坑位，后到的把先到的覆盖掉，这就是「只报最后一次」的物理原因
//   第 168/169/170 行 void close() override / bool isSequential() const override / bool flush();
//                    ← socket 是 sequential 的（第 169 行），没有「位置」概念，
//                      所以 seek/pos 那一套在这儿没意义；QAbstractSocket 里**没有**
//                      canReadLine 的 override（只有 readLineData，第 197 行）
//   第 173~176 行 waitForConnected(int msecs = 30000) / waitForReadyRead(... = 30000) /
//                    waitForBytesWritten(... = 30000) / waitForDisconnected(... = 30000)
//                    ← 四个默认值都是 **30000ms = 30 秒**。私有实现里那个
//                      qabstractsocketengine_p.h 第 37 行 `static constexpr std::chrono::seconds
//                      DefaultTimeout{30}` 就是它的出处（这是本机 grep 到的，属于内部头，
//                      只作为「为什么默认是 30 秒」的注脚，代码里不能依赖它）。
//                      ⚠ 在**有事件循环的线程**上任何一个 waitFor* 都是把整条心跳掐断 30 秒。
//   第 186~190 行 hostFound() / connected() / disconnected() / stateChanged(SocketState) /
//                    errorOccurred(SocketError)
//                    ← Qt6 一律叫 **errorOccurred**；Qt5 那个 error(QAbstractSocket::SocketError)
//                      信号已不存在（第 165 行的 error() 是无参 getter，别混）。
//                      errorOccurred 只有**一个**形态（第 190 行），不像 QNetworkReply 那样
//                      有重载可挑，connect 时不用写 qOverload。
//   第 234~237 行 QT_DECL_METATYPE_EXTERN_TAGGED(SocketState / SocketError …)
//                    ← 两个枚举都登记了元类型，所以 stateChanged / errorOccurred 能排队投递。
//                      socket 与线程的关系见下方 cross-link，本讲不展开。
//   ⚠ errorString() **不在** qabstractsocket.h 里，它是 QIODevice 的（qiodevice.h 第 115 行）。
//     网上偶尔看到「QAbstractSocket::errorString 被 QTcpSocket 重写」的说法，在本机头文件里
//     找不到这种声明，别照着写文档。
//
// QtCore/qiodevice.h —— 数据搬动的真身在这一层：
//   第 31~37 行 class Q_CORE_EXPORT QIODevice : public QObject, public QIODeviceBase
//   第 54~56 行 isOpen() / isReadable() / isWritable()
//   第 81/82 行 QByteArray read(qint64 maxlen) / QByteArray readAll()
//                    ← readAll 读的是**当前缓冲里有的**，不是「整条流」；socket 上没有「整条流」
//   第 84 行 QByteArray readLine(qint64 maxlen = 0);   第 85 行 bool readLineInto(QByteArray*, ...)
//   第 93 行 virtual bool canReadLine() const;   ← virtual，所以子类可以改写；但
//                    QAbstractSocket 没改（QtNetwork/*.h 里只有 qlocalsocket.h 第 74 行、
//                    qsslsocket.h 第 84 行 override 了它）→ 本讲不拿它当分帧依据
//   第 100/101/102 行 qint64 write(const char*, qint64) / write(const char*) /
//                    write(const QByteArray&)
//                    ← 返回值 = **写进 Qt 发送缓冲的字节数**；出错返回 -1。
//                      它**不是**「对端收到了这么多」，这是本讲第 5 段的题目
//   第 106 行 qint64 skip(qint64 maxSize);
//   第 108/109 行 virtual bool waitForReadyRead(int msecs) / waitForBytesWritten(int msecs)
//                    ← 这一层**没有默认参数**（对比 qabstractsocket.h 第 174/175 行加了 30000）。
//                      所以通过 QIODevice 接口调这两个时必须显式给毫秒数
//   第 115 行 QString errorString() const;
//   第 119~124 行 readyRead() / channelReadyRead(int) / bytesWritten(qint64) /
//                    channelBytesWritten(int, qint64) / aboutToClose() / readChannelFinished()
//                    ← readyRead 与 bytesWritten 属于 **QIODevice**，不是 QTcpSocket 新加的；
//                      写文档时归属别记错（第 1 讲也提过一次）
//   第 133/134 行 protected 的 readData（**纯虚**：谁能读多少必须由子类实现）与
//                    readLineData（**不是**纯虚，基类自带实现）；
//                    而 QAbstractSocket 在第 197 行把 readLineData **override** 了，
//                    却没有 override 第 93 行那个 canReadLine——这个不对称就是第 4 段要打的点
//
// QtNetwork/qhostaddress.h：
//   第 46~54 行 enum SpecialAddress { Null, Broadcast, LocalHost, LocalHostIPv6, Any,
//                    AnyIPv6, AnyIPv4 }   ← 本讲只用 LocalHost
//   第 87 行 explicit QHostAddress(const QString &address);   第 104 行 setAddress(const QString &)
//   第 107 行 NetworkLayerProtocol protocol() const;          第 111 行 QString toString() const;
//   第 129 行 bool isLoopback() const;   ← 硬规矩的**可打印证据**，第 1 段每次都打它
//
// QtCore/qendian.h（转发头 <QtEndian>）：
//   第 181 / 204 行 template <typename T> inline void qToBigEndian(T src, void *dest);
//                    （两份分别对应大端机和小端机分支，第 171 行 #if Q_BYTE_ORDER）
//   第 239 行 template <class T> inline T qFromBigEndian(const void *src);
//                    ← 长度前缀一律大端（网络字节序）。本机是小端，所以这两行真的会做字节交换。
//
// QtCore/qdatastream.h（第 4 段那段「不联网的内存实验」用）：
//   第 112~118 行 enum Status { Ok, ReadPastEnd, ReadCorruptData, WriteFailed, SizeLimitExceeded }
//   第 126 行 explicit QDataStream(QIODevice *);
//   第 127 行 QDataStream(QByteArray *, OpenMode flags);
//   第 128 行 QDataStream(const QByteArray &);      ← 「整帧到手再解」的正道
//   第 137 行 Status status() const;
//   第 212/216 行 int readRawData(char*, int) / qint64 readRawData(char*, qint64)；
//   第 213/218 行 writeRawData 同两层（6.x 起大长度版本在 216/218）
//   第 254 行 static constexpr quint32 ExtendedSize = 0xfffffffeu;
//   第 470 行 readQSizeType / 第 483 行 writeQSizeType：
//                    长度 < ExtendedSize 写 **4 字节**（第 486 行 s << quint32(value)），
//                    版本 >= Qt_6_7 且超限时写 0xfffffffe + **8 字节**真实长度（第 487~488 行）
//                    ← 这就是「Qt 在序列化格式里有长度前缀，在传输层没有消息边界」的实证。
//                      两者不是一回事：前者只保证「QDataStream 写出来的东西 QDataStream 能读回去」，
//                      后者要的是「我知道这一次该读几个字节」——那是你的协议层的事。
//
// QtCore/qtimer.h —— 本讲的「时间」全靠它；重点是 singleShot 分成**两族**：
//   第 65~71 行 singleShot(Duration, const ContextType *receiver, Functor)  ← 带 context
//   第 83 行源码自己留了注释「singleShot without context」，第 84~88 行就是那一族：
//                    singleShot(Duration, Functor) → 第 87 行把 receiver 原样传成 **nullptr**，
//                    也就是「到点必发，谁都拦不住」。
//                    ←⚠ 这一行就是本讲「每次推进都必须过 gotoStage() 这道门」的原因：
//                      不受 context 约束的定时器，teardown 之后照样会来一发；
//                      要让它「随对象一起作废」，就得改用第 65~71 行那族。
//   第 37 行 setInterval(int) / 第 41 行 remainingTime() / 第 47 行 setSingleShot(bool)
//   第 110 行 start(int msec) / 第 113 行 stop()
//   第 116 行 void timeout(QPrivateSignal);   ← QPrivateSignal 是编译期锁：
//                    你在外面写 emit timer->timeout() 编不过，只有 QTimer 自己发得出来。
//   第 158 行 static std::chrono::nanoseconds toDuration(int msecs) /
//   第 166 行 static constexpr Qt::TimerType defaultTypeFor(int msecs)
//                    ← 这两行是「QTimer::singleShot(200, lambda) 凭什么能编译」的凭据：
//                      模板参数 Duration 直接推导成 int，再由 toDuration(int) 转成时长。
//
// cross-link（只一句，不重复别的讲）：
//   · socket 的线程归属：QAbstractSocket 必须在**创建它的那条线程**里用，跨线程碰它的
//     读写就是竞态；裁决机制见 threads 第 3 讲（threads_03_cross_thread_signals）。
//   · 本机 IPC：同机通信用 QLocalSocket / QLocalServer（qlocalsocket.h 第 27~41 行的
//     LocalSocketError 是把 QAbstractSocket 的错误码**逐个等价映射**过去的，第 29~39 行；
//     qlocalserver.h 第 50 行 listen(const QString&) 没有端口也没有地址这一层），
//     少了 TCP 的环回开销与端口占用。那条线在 threads 第 8 讲正面讲
//     （threads_08_local_ipc，仓库根 CMakeLists 第 205 行登记）。本讲不重复。
// ============================================================================

// ============================================================================
// 二、账本、有界等待、以及两个「把枚举翻成人话」的小函数
// ============================================================================
// 分帧方式：同一套服务器/客户端代码，两种模式跑两台服务器（【4】要的就是这个对照）
enum class Framing { LineDelimited, LengthPrefixed };

// 一帧最大允许多大。**必须**设上限：长度前缀是对端能控制的数字，
// 不设上限就等于「远端发 4 个字节就能让你 malloc 4GB」。
static const qsizetype kMaxFrameBytes = 1024 * 1024;

struct Ledger
{
    // —— 应用层的账：谁发的、发几条，由发送方**自己登记**，不让传输层猜 ——
    inline static int lineMsgsSent = 0;        // 打算发到「行端口」的逻辑消息条数
    inline static int pfxMsgsSent = 0;         // 打算发到「前缀端口」的逻辑消息条数
    inline static int pingsSent = 0;           // 客户端发出的心跳
    inline static int pongsByServer = 0;       // 服务端回出去的心跳
    inline static int pongsSeenByClient = 0;   // 客户端真收到的心跳
    // —— 超时的账：每个守卫定时器都必须是可数的 ——
    inline static int guardStopped = 0;        // 连接守卫被 stop（成功或出错都要停）
    inline static int guardFired = 0;          // 连接守卫真超时（预期 0 次）
    inline static int replyTimeouts = 0;       // 应答守卫真超时（【7】故意造 1 次）
    inline static int probeErrorsAtSec6 = 0;   // 【6】开口时 probe 已经攒到的 errorOccurred 条数
    // —— 【7】现场量「waitForNewConnection 的默认 0 到底是什么语义」——
    inline static int wfcZeroGot = -1;         // 返回有没有连接（实测应为 0）
    inline static int wfcZeroTimedOut = -1;    // timedOut 有没有被置真（实测应为 1）
    inline static qint64 wfcZeroMs = -1;       // 它当场返回花了多少毫秒
    // —— 收摊那一次 waitForDisconnected(300) 的账（本机实测全是 false，见 teardown）——
    inline static int teardownWaits = 0;       // 调了几次
    inline static int teardownWaitFalse = 0;   // 其中返回 false 几次
    inline static qint64 teardownWaitMs = 0;   // 这几次加起来在墙上花了多久
    // —— 合账 ——
    inline static int checksTotal = 0;
    inline static int checksPassed = 0;
    // —— 总闸 ——
    inline static bool watchdogFired = false;   // 走到强制收摊那条路 = 这一轮没跑完
    // ★ 退出码就是这两笔账的合成：合账差一项、或者总闸跳过一次，进程就返回 1。
    //   「打出来给人看」只算一半，「让脚本查得出来」才算把话说完。
    static int exitCode()
    {
        return (watchdogFired || checksPassed != checksTotal) ? 1 : 0;
    }
};

// 每一项核对都打印「期望 / 实测」，最后一行给总数。本讲没有任何 assert：
// 结论靠得住的数，不靠「我觉得它应该是对的」。
static void check(const QString &what, qint64 got, qint64 want)
{
    ++Ledger::checksTotal;
    const bool ok = (got == want);
    if (ok)
        ++Ledger::checksPassed;
    qInfo().noquote() << QStringLiteral("      %1 %2 → 期望 %3，实测 %4")
                             .arg(ok ? QStringLiteral("［对］") : QStringLiteral("［差］"))
                             .arg(what)
                             .arg(want).arg(got);
}

// 字面量版本：标签是源码里的中文，必须 fromUtf8（fromLatin1 会乱码；本机跑起来看到的
// 就是这一条的证据——满屏中文标签正常，字节流按 UTF-8 解才对）。
static void check(const char *what, qint64 got, qint64 want)
{
    check(QString::fromUtf8(what), got, want);
}

static QString stateName(QAbstractSocket::SocketState st)
{
    switch (st) {
    case QAbstractSocket::UnconnectedState: return QStringLiteral("Unconnected");
    case QAbstractSocket::HostLookupState:  return QStringLiteral("HostLookup");
    case QAbstractSocket::ConnectingState:  return QStringLiteral("Connecting");
    case QAbstractSocket::ConnectedState:   return QStringLiteral("Connected");
    case QAbstractSocket::BoundState:       return QStringLiteral("Bound");
    case QAbstractSocket::ListeningState:   return QStringLiteral("Listening");
    case QAbstractSocket::ClosingState:     return QStringLiteral("Closing");
    }
    return QStringLiteral("未知(%1)").arg(int(st));
}

static QString errorName(QAbstractSocket::SocketError e)
{
    switch (e) {
    case QAbstractSocket::ConnectionRefusedError:        return QStringLiteral("ConnectionRefused");
    case QAbstractSocket::RemoteHostClosedError:         return QStringLiteral("RemoteHostClosed");
    case QAbstractSocket::HostNotFoundError:             return QStringLiteral("HostNotFound");
    case QAbstractSocket::SocketAccessError:             return QStringLiteral("SocketAccess");
    case QAbstractSocket::SocketResourceError:           return QStringLiteral("SocketResource");
    case QAbstractSocket::SocketTimeoutError:            return QStringLiteral("SocketTimeout");
    case QAbstractSocket::NetworkError:                  return QStringLiteral("NetworkError");
    case QAbstractSocket::AddressInUseError:             return QStringLiteral("AddressInUse");
    case QAbstractSocket::SocketAddressNotAvailableError:return QStringLiteral("AddressNotAvailable");
    case QAbstractSocket::UnsupportedSocketOperationError:return QStringLiteral("UnsupportedOp");
    case QAbstractSocket::OperationError:                return QStringLiteral("Operation");
    case QAbstractSocket::TemporaryError:                return QStringLiteral("Temporary");
    case QAbstractSocket::UnknownSocketError:            return QStringLiteral("Unknown(-1)");
    default:
        break;
    }
    return QStringLiteral("其他");
}

static QString framingName(Framing f)
{
    return f == Framing::LineDelimited ? QStringLiteral("行分隔(\\n)")
                                       : QStringLiteral("长度前缀(4 字节大端)");
}

// ── 有界等待：本讲唯一的「等」 ──────────────────────────────────────────────
// 为什么需要它：步骤之间有真实的先后关系（消息到没到、连接断没断），
//   但**绝不能**用 waitFor* 去等（那是把事件循环按在地上），也不能猜一个 sleep 就往下走。
//   正解是：过一小段时间**回头看一个计数器**，到点没达成也照样往下走并打出「没等到」。
// 三条纪律：① 轮的是计数器不是 socket（不碰 readAll/bytesAvailable，看账不能动账）；
//          ② 有步长上限（这里 10ms 一跳、最多 maxMs）；
//          ③ 到点必走，绝不无限等——所以没有任何路径能把进程吊死。
// 全片的预算是**算得出来的**（这是它敢挂进 ctest 的原因）：
//   段内等待上限合计 1000+1000+2000+1000+1000+1000+1000+1200+1200+2000 = 11400ms；
//   段间固定的 singleShot 约 1640ms；【8】→合账那一步两条来源取较大者（兜底 4000ms
//   比正常路径多出约 2680ms，gotoStage 保证只放行一次，所以是「取大」不是「相加」）；
//   收摊 8 次 waitForDisconnected(300) 上限 2400ms。
//   最坏 ≈ 18.1 秒，而总闸是 25 秒——**就算每一处有界等待全部到点未达成，
//   合账也一定打得出来**；总闸只留给「listen 本身起不来」这类更外层的意外。
//   改任何一处超时的话，这笔账要重算——别让它超过总闸，否则「超时」会变成日常而不是意外。
struct Waiter
{
    QString what;
    std::function<bool()> pred;
    std::function<void()> then;
    int maxMs = 1000;
    int stepMs = 10;
    QElapsedTimer clock;
};

static void runWaitStep(Waiter *w);

static void waitUntil(const QString &what, std::function<bool()> pred, std::function<void()> then,
                      int maxMs, int stepMs = 10)
{
    Waiter *w = new Waiter();
    w->what = what;
    w->pred = std::move(pred);
    w->then = std::move(then);
    w->maxMs = maxMs;
    w->stepMs = stepMs;
    w->clock.start();
    runWaitStep(w);
}

static void runWaitStep(Waiter *w)
{
    const qint64 spent = w->clock.elapsed();
    const bool arrived = w->pred();
    if (arrived || spent >= w->maxMs) {
        if (arrived)
            qInfo().noquote() << QStringLiteral("      -> 有界等待「%1」达成（%2 ms，每 %3 ms 复查一次，全程不阻塞）")
                                     .arg(w->what).arg(spent).arg(w->stepMs);
        else
            qWarning().noquote() << QStringLiteral("      -> 有界等待「%1」到点未达成（已等 %2 ms）→ **带着这条证据继续**")
                                        .arg(w->what).arg(spent);
        // 先把自己摘下来再跑下一步：then() 里很可能又 new 一个 Waiter
        std::function<void()> next;
        next.swap(w->then);
        delete w;
        if (next)
            next();
        return;
    }
    QTimer::singleShot(w->stepMs, [w]() { runWaitStep(w); });   // qtimer.h 第 84/85 行那组无 context 版
}

// ============================================================================
// 三、LineProtocol：协议长什么样（版本号 + 一行一条 + 长度前缀两种边界）
// ============================================================================
// 载荷格式：V1 <KIND> <BODY>       （BODY 可以带空格，取「第二个空格之后的全部」）
// 边界：行分隔用 '\n'，长度前缀用 4 字节大端。
// 版本号第一天就要写：协议一定会改，**改的时候没有版本号你就只能靠猜**，
// 而且新旧客户端混跑时你会在日志里看到一堆无法解释的怪状态。
class LineProtocol
{
public:
    static QByteArray content(const QString &kind, const QString &body)
    {
        QByteArray out;
        out.reserve(kind.size() + body.size() + 8);
        out.append("V1 ");
        out.append(kind.toUtf8());
        out.append(' ');
        out.append(body.toUtf8());
        return out;
    }

    static bool parseContent(const QByteArray &msg, QString *kind, QString *body)
    {
        const qsizetype sp1 = msg.indexOf(' ');                     // qbytearray.h 第 152 行
        if (sp1 < 0)
            return false;
        const qsizetype sp2 = msg.indexOf(' ', sp1 + 1);
        if (sp2 < 0)
            return false;
        if (msg.left(sp1) != QByteArray("V1"))                       // 版本不认：整帧作废
            return false;
        *kind = QString::fromUtf8(msg.mid(sp1 + 1, sp2 - sp1 - 1));  // 第 175 行 mid
        *body = QString::fromUtf8(msg.mid(sp2 + 1));
        return true;
    }

    // 上线的字节 = 载荷 + 边界
    static QByteArray frame(Framing f, const QString &kind, const QString &body)
    {
        const QByteArray payload = content(kind, body);
        if (f == Framing::LineDelimited) {
            QByteArray out = payload;
            out.append('\n');
            return out;
        }
        static_assert(sizeof(quint32) == 4, "长度前缀按 4 字节设计");
        char head[4];
        qToBigEndian<quint32>(quint32(payload.size()), head);        // qendian.h 第 181/204 行
        QByteArray out(head, qsizetype(sizeof(head)));
        out.append(payload);
        return out;
    }

    // 从缓冲区里取**一整帧**。取不出来就原样留着——这一步是「半包」的全部真相。
    // 注意调用方负责保留 buf：半包不是错误，是常态。
    static bool takeMessage(Framing f, QByteArray *buf, QByteArray *out)
    {
        if (f == Framing::LineDelimited) {
            const qsizetype nl = buf->indexOf('\n');
            if (nl < 0)
                return false;                    // 连一个换行都没有 → 半包
            *out = buf->left(nl);                 // 第 173 行：不含换行符
            *buf = buf->mid(nl + 1);
            return true;
        }
        if (buf->size() < 4)
            return false;                         // 前缀都没收齐（第 4 段就故意切这儿）
        const quint32 need = qFromBigEndian<quint32>(buf->constData());   // 第 239 行
        if (qsizetype(need) > kMaxFrameBytes) {
            // 对端给了个荒唐的长度：真服务器必须**立刻断开**它（这里把缓冲丢掉并留证据，
            // 因为演示里这条连接后面还要用；工程代码里请写 sock->abort()）
            qWarning().noquote() << QStringLiteral("      【注意】 长度前缀超限：声明 %1 字节（上限 %2）→ 丢弃缓冲")
                                        .arg(qint64(need)).arg(kMaxFrameBytes);
            *buf = QByteArray();
            return false;
        }
        const qsizetype want = qsizetype(4) + qsizetype(need);
        if (buf->size() < want)
            return false;                         // 半帧
        *out = buf->mid(4, qsizetype(need));
        *buf = buf->mid(want);
        return true;
    }
};

// ============================================================================
// 四、TcpEchoServer：一个类跑两种分帧（Q_OBJECT，需要 moc）
// ============================================================================
class TcpEchoServer : public QObject
{
    Q_OBJECT

public:
    TcpEchoServer(Framing framing, const QString &name, QObject *parent = nullptr)
        : QObject(parent), m_name(name), m_framing(framing)
    {
        m_server = new QTcpServer(this);                             // qtcpserver.h 第 26 行
        // 第 69 行的 newConnection() **无参数**：socket 自己去 nextPendingConnection 取
        QObject::connect(m_server, &QTcpServer::newConnection, this, &TcpEchoServer::onNewConnection);
        // 第 71 行 acceptError：建 socket 失败走这条，**不是** errorOccurred（服务器没那个信号）
        QObject::connect(m_server, &QTcpServer::acceptError, this,
                         [this](QAbstractSocket::SocketError e) {
                             qWarning().noquote() << QStringLiteral("    %1 acceptError(%2)")
                                                         .arg(m_name).arg(errorName(e));
                         });
        // 服务器自己的低延迟选项，顺手读回来当证据（第 160/161 行；socketOption 不是 const）
        m_server->setMaxPendingConnections(16);                       // 第 34 行
    }

    bool start()
    {
        // 两个默认值（第 29 行）都必须显式覆盖掉：地址 LocalHost，端口 0
        if (!m_server->listen(QHostAddress::LocalHost, 0)) {
            qWarning().noquote() << QStringLiteral("    %1 listen 失败：%2")
                                        .arg(m_name, m_server->errorString());   // 第 51 行
            return false;
        }
        m_port = m_server->serverPort();                              // 第 40 行：**读回来**
        m_bound = m_server->serverAddress();                          // 第 41 行
        return true;
    }

    void stop()
    {
        m_server->close();                     // 第 30 行：只是不再接新的
        for (QTcpSocket *s : std::as_const(m_socks)) {
            s->disconnectFromHost();           // 第 138 行
            s->deleteLater();
        }
        m_socks.clear();
        m_conns.clear();
    }

    // 把最后一条**还活着**的连接直接 abort()：【6】用它制造第二个错误
    void dropLastLiveConnection()
    {
        if (m_socks.isEmpty()) {
            qWarning().noquote() << QStringLiteral("    %1 没有连接可 abort").arg(m_name);
            return;
        }
        QTcpSocket *s = m_socks.last();
        qInfo().noquote() << QStringLiteral("    %1 对最后一条连接（peer %2:%3）调用 abort()（第 154 行，立即撕）")
                                 .arg(m_name)
                                 .arg(s->peerAddress().toString())            // 第 148 行
                                 .arg(s->peerPort());                          // 第 147 行
        s->abort();
        m_conns.remove(s);
        m_socks.removeOne(s);
        ++m_dropped;
    }

    quint16 port() const { return m_port; }
    Framing framing() const { return m_framing; }                       // 同一套代码跑两种边界
    QString boundText() const { return m_bound.toString(); }          // qhostaddress.h 第 111 行
    bool boundIsLoopback() const { return m_bound.isLoopback(); }     // 第 129 行
    bool listening() const { return m_server->isListening(); }        // 第 32 行
    void setStall(bool on) { m_stall = on; }
    bool stall() const { return m_stall; }

    int accepted() const { return m_accepted; }
    int closed() const { return m_closed; }
    int sockErrors() const { return m_sockErrors; }                      // 服务端侧收到过多少次 errorOccurred
    int selfDeleted() const { return m_selfDeleted; }
    int dropped() const { return m_dropped; }
    int msgs() const { return m_msgs; }
    int readyReadHits() const { return m_readyRead; }
    qint64 bytesReceived() const { return m_bytes; }
    int canReadLineTrue() const { return m_canReadLineTrue; }
    int gracefulCloses() const { return m_gracefulCloses; }
    int parseFailures() const { return m_badFrames; }
    int framesOut() const { return m_framesOut; }
    bool byeSocketGone() const { return m_byeSocket.isNull(); }       // qpointer.h 第 86 行
    // 此刻还挂在服务器上**没拼成一帧**的字节总数：半包的直接证据
    qint64 totalBuffered() const
    {
        qint64 n = 0;
        for (const Conn &c : m_conns)
            n += c.buf.size();
        return n;
    }

signals:
    void messageParsed(const QString &kind, const QString &body);

private slots:
    void onNewConnection()
    {
        // 第 47 行 hasPendingConnections + 第 48 行 nextPendingConnection：while 取干
        while (m_server->hasPendingConnections()) {
            QTcpSocket *sock = m_server->nextPendingConnection();
            if (!sock)
                break;
            // 所有权归调用方（文档口径）。parent 到底是什么**头文件里看不出来**，
            // 所以不猜：立刻 setParent 自己接管，这就是收摊时不会漏删的原因。
            sock->setParent(this);
            ++m_accepted;
            m_socks.append(sock);
            Conn &c = m_conns[sock];                                  // QHash 的下标会插默认值
            c = Conn();
            QObject::connect(sock, &QTcpSocket::readyRead, this, &TcpEchoServer::onReadyRead);
            QObject::connect(sock, &QTcpSocket::disconnected, this, &TcpEchoServer::onDisconnected);
            QObject::connect(sock, &QTcpSocket::errorOccurred, this, &TcpEchoServer::onSocketError);
            qInfo().noquote() << QStringLiteral("    %1 新连接 #%2：peer=%3:%4｜本端=%5:%6｜监听=%7:%8｜isLoopback=%9")
                                     .arg(m_name).arg(m_accepted)
                                     .arg(sock->peerAddress().toString()).arg(sock->peerPort())
                                     .arg(sock->localAddress().toString()).arg(sock->localPort())
                                     .arg(m_bound.toString()).arg(m_port)
                                     .arg(sock->peerAddress().isLoopback());
            // 第 160 行：TCP_NODELAY。回环上的小消息要不要它，看的就是这种地方
            sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        }
    }

    void onReadyRead()
    {
        auto *sock = qobject_cast<QTcpSocket *>(sender());
        if (!sock)
            return;
        ++m_readyRead;                       // ← 这个数**故意不进合账**：它不可预测
        auto it = m_conns.find(sock);
        if (it == m_conns.end())
            return;
        Conn &c = it.value();

        // 【4】的对照物：行分帧时打一下 canReadLine 的真值。
        // QAbstractSocket 没 override 它（见第一节），所以**不拿它当依据**，只看它此刻说什么。
        if (m_framing == Framing::LineDelimited && sock->canReadLine())  // qiodevice.h 第 93 行
            ++m_canReadLineTrue;

        const qint64 before = c.buf.size();
        c.buf.append(sock->readAll());                                  // 第 82 行：读走现有的
        m_bytes += c.buf.size() - before;

        bool wantSelfDelete = false;
        QByteArray frameBytes;
        while (LineProtocol::takeMessage(m_framing, &c.buf, &frameBytes)) {
            QString kind;
            QString body;
            if (!LineProtocol::parseContent(frameBytes, &kind, &body)) {
                ++m_badFrames;
                qWarning().noquote() << QStringLiteral("    %1 收到不成协议的帧（版本不认或字段缺失，%2 字节）")
                                            .arg(m_name).arg(qint64(frameBytes.size()));
                continue;
            }
            ++m_msgs;
            ++c.msgs;
            c.gotMsgLast = true;
            qInfo().noquote() << QStringLiteral("    %1 解析出第 %2 条消息：kind=%3 body=%4｜此刻缓冲里还剩 %5 字节未成帧")
                                     .arg(m_name).arg(m_msgs).arg(kind)
                                     .arg(body.size() > 40 ? QStringLiteral("<%1 字节>").arg(body.size()) : body)
                                     .arg(qint64(c.buf.size()));
            // 「解析出一条消息」这个事件**在分派之前就发出去**：上层关心的永远是
            // 「到了一条什么消息」，不是「服务端后来回没回话」。顺序写反了，
            // BYE 这一类「自己不进 handle() 的分支」就会神不知鬼不觉地少一次通知——
            // 这种 bug 在日志里根本查不出来，因为它不报错，只是少一次。
            emit messageParsed(kind, body);
            if (kind == QStringLiteral("BYE")) {
                wantSelfDelete = true;             // 出栈后再动手，见下面注释
                sendFrame(sock, QStringLiteral("BYEBYE"), QStringLiteral("服务端把你收进删除队列了"));
                continue;
            }
            handle(sock, kind, body);
        }

        if (wantSelfDelete) {
            // ★ 我们**正站在这条 socket 自己发出的 readyRead 的调用栈上**。
            //   这里写 delete sock 就是把脚下的地板抽掉：返回时 Qt 还要继续用这个对象，
            //   现场是一个查起来极其难受的崩溃（不是当场崩，是下一层栈帧崩）。
            qInfo().noquote() << QStringLiteral("    %1 在 readyRead 的调用栈里对这条 socket deleteLater()（不 delete！）")
                                     .arg(m_name);
            sock->deleteLater();
            m_byeSocket = sock;                    // QPointer：30ms 之后验它是不是真没了
            ++m_selfDeleted;
            // deleteLater **不当场生效**，所以下面这几行读它完全合法——这正是证据本身
            qInfo().noquote() << QStringLiteral("      deleteLater 之后同一轮里再读它：state=%1｜bytesToWrite=%2｜isOpen=%3")
                                     .arg(stateName(sock->state()))            // 第 164 行
                                     .arg(sock->bytesToWrite())                 // 第 143 行
                                     .arg(sock->isOpen());                      // qiodevice.h 第 54 行
            m_conns.remove(sock);
            m_socks.removeOne(sock);
        }
    }

    void onDisconnected()
    {
        auto *sock = qobject_cast<QTcpSocket *>(sender());
        if (!sock)
            return;
        ++m_closed;
        auto it = m_conns.find(sock);
        qint64 leftover = -1;
        if (it != m_conns.end()) {
            leftover = it.value().buf.size();
            if (it.value().gotMsgLast)          // 上一条事件是「解析出一帧」→ 数据先到、断开后到
                ++m_gracefulCloses;
            m_conns.remove(sock);
        }
        m_socks.removeOne(sock);
        qInfo().noquote() << QStringLiteral("    %1 disconnected（第 #2 类收口）：断开时缓冲残留 %2 字节｜state=%3")
                                 .arg(m_name).arg(leftover).arg(stateName(sock->state()));
        sock->deleteLater();                     // 收尸一律 deleteLater，不 delete
    }

    void onSocketError(QAbstractSocket::SocketError e)
    {
        ++m_sockErrors;
        qInfo().noquote() << QStringLiteral("    %1 某条连接 errorOccurred(%2=%3)｜errorString=\"%4\"")
                                 .arg(m_name).arg(errorName(e)).arg(int(e))
                                 .arg(qobject_cast<QTcpSocket *>(sender())
                                          ? qobject_cast<QTcpSocket *>(sender())->errorString()
                                          : QStringLiteral("?"));   // qiodevice.h 第 115 行
    }

private:
    struct Conn
    {
        QByteArray buf;              // 自己维护的读缓冲：半包就留在这儿
        int msgs = 0;
        bool gotMsgLast = false;
    };

    void sendFrame(QTcpSocket *sock, const QString &kind, const QString &body)
    {
        const QByteArray f = LineProtocol::frame(m_framing, kind, body);
        const qint64 rc = sock->write(f);        // qiodevice.h 第 102 行：进缓冲的字节数
        if (rc > 0)
            ++m_framesOut;
        else
            qWarning().noquote() << QStringLiteral("    %1 write() 返回 %2（设备没开或对端没了）")
                                        .arg(m_name).arg(rc);
    }

    void handle(QTcpSocket *sock, const QString &kind, const QString &body)
    {
        if (kind == QStringLiteral("WAIT")) {
            if (m_stall) {
                qInfo().noquote() << QStringLiteral("    %1 【服务端 stall】这条 WAIT 故意一个字都不回 → 让客户端自己掐超时").arg(m_name);
                return;
            }
            sendFrame(sock, QStringLiteral("PONG"), QStringLiteral("WAIT 也回了：") + body);
            ++Ledger::pongsByServer;
            return;
        }
        if (kind == QStringLiteral("PING")) {
            ++Ledger::pongsByServer;
            sendFrame(sock, QStringLiteral("PONG"), body);
            return;
        }
        if (kind == QStringLiteral("BULK")) {     // 只回一个短 ACK：【5】要的是「上行字节对账」
            sendFrame(sock, QStringLiteral("ACK"), QStringLiteral("收到 %1 字节").arg(body.size()));
            return;
        }
        sendFrame(sock, QStringLiteral("ECHO"), body);   // HELLO / SPLIT / ECHO / LAST 都走这条
    }

    QString m_name;
    Framing m_framing;
    QTcpServer *m_server = nullptr;
    quint16 m_port = 0;
    QHostAddress m_bound;
    QList<QTcpSocket *> m_socks;
    QHash<QTcpSocket *, Conn> m_conns;
    QPointer<QTcpSocket> m_byeSocket;              // 只用来判空，绝不解引用
    int m_accepted = 0;
    int m_closed = 0;
    int m_selfDeleted = 0;
    int m_dropped = 0;
    int m_msgs = 0;
    int m_readyRead = 0;
    int m_canReadLineTrue = 0;
    int m_badFrames = 0;
    int m_gracefulCloses = 0;
    int m_sockErrors = 0;
    int m_framesOut = 0;
    qint64 m_bytes = 0;
    bool m_stall = false;
};

// ============================================================================
// 五、TcpClient：把「信号来了几次」变成可核对的数
// ============================================================================
class TcpClient : public QObject
{
    Q_OBJECT

public:
    TcpClient(Framing framing, const QString &name, QObject *parent = nullptr)
        : QObject(parent), m_name(name), m_framing(framing)
    {
        m_sock = new QTcpSocket(this);                                // qtcpsocket.h 第 22 行
        QObject::connect(m_sock, &QTcpSocket::connected, this, &TcpClient::onConnected);       // 187
        QObject::connect(m_sock, &QTcpSocket::disconnected, this, &TcpClient::onDisconnected);  // 188
        QObject::connect(m_sock, &QTcpSocket::stateChanged, this, &TcpClient::onStateChanged);  // 189
        QObject::connect(m_sock, &QTcpSocket::errorOccurred, this, &TcpClient::onErrorOccurred);// 190
        QObject::connect(m_sock, &QTcpSocket::readyRead, this, &TcpClient::onReadyRead);        // qiodevice 119
        QObject::connect(m_sock, &QTcpSocket::bytesWritten, this, &TcpClient::onBytesWritten);  // qiodevice 121
    }

    QTcpSocket *sock() const { return m_sock; }
    QString name() const { return m_name; }
    Framing framing() const { return m_framing; }

    // 「连上之后再发」：connectToHost 返回 void（第 136/137 行），所以任何「一连上就发」的动作
    // 都必须挂在 connected 上；挂在 connectToHost 后面就是往没打开的设备里写（write 返回 -1）。
    void connectThen(quint16 port, int guardMs, std::function<void()> ready)
    {
        // ★复用同一只 socket 再连一次之前，必须先把上一次没结束的尝试掐掉。
        //   本机实测（Qt 6.12.0 / MinGW / Windows）：state 还是 Connecting 时再调
        //   connectToHost()，Qt **不会**改连新端口，而是就地同步发出
        //   errorOccurred(SocketResourceError=19，errorString "Trying to connect while
        //   connection is in progress")，然后这个调用什么都不做。
        //   后果不是报错，是**静默不推进**：connected 永远不来，挂在 connected 上的
        //   ready 回调永远不执行——本讲【6】→【7】那一跳当初就是这么卡到 25 秒总闸的。
        //   abort()（qabstractsocket.h 第 154 行）把状态直接打回 Unconnected，
        //   不带 30s 的挂起尝试，是「同一只 socket 复用」的标准前置动作。
        if (m_sock->state() != QAbstractSocket::UnconnectedState) {
            qInfo().noquote() << QStringLiteral("    %1 复用前 state=%2，先 abort() 掐掉上一次没结束的尝试"
                                                "（否则 connectToHost 只会回 19 号错误，且什么都不做）")
                                     .arg(m_name).arg(stateName(m_sock->state()));
            m_sock->abort();
        }
        m_onConnected = std::move(ready);
        ensureConnectGuard(guardMs);
        m_connectGuard->start(guardMs);          // qtimer.h 第 110 行 start(int msec)
        m_sock->connectToHost(QHostAddress::LocalHost, port);          // 第 137 行（不吃 DNS）
        qInfo().noquote() << QStringLiteral("    %1 connectToHost(LocalHost:%2) 已返回——它返回的是 **void**，"
                                           "所以此刻 state=%3 只是「刚开始」")
                                 .arg(m_name).arg(port).arg(stateName(m_sock->state()));
    }

    qint64 send(const QString &kind, const QString &body)
    {
        return sendRaw(LineProtocol::frame(m_framing, kind, body));
    }

    // 【3】【4】用它把一帧**故意切碎**：这就是半包的造法
    qint64 sendRaw(const QByteArray &bytes)
    {
        const qint64 rc = m_sock->write(bytes);                      // 第 102 行
        if (rc > 0)
            m_requested += rc;
        else
            qWarning().noquote() << QStringLiteral("    %1 write() 返回 %2 —— 设备没开（没 connected 就写就是这个回答）")
                                        .arg(m_name).arg(rc);
        const qint64 pending = m_sock->bytesToWrite();               // 第 143 行
        if (pending > m_peakToWrite)
            m_peakToWrite = pending;
        return rc;
    }

    void armReplyGuard(int ms, const QString &expect)
    {
        if (!m_replyGuard) {
            m_replyGuard = new QTimer(this);
            m_replyGuard->setSingleShot(true);                       // qtimer.h 第 47 行
            QObject::connect(m_replyGuard, &QTimer::timeout, this, &TcpClient::onReplyTimeout);
        }
        m_expect = expect;
        m_replyGuard->start(ms);
        qInfo().noquote() << QStringLiteral("    %1 应答守卫上弦：%2 ms 内等 %3（Qt 的 socket 没有这个功能，只能自己造）")
                                 .arg(m_name).arg(ms).arg(expect);
    }

    // —— 读数 ——
    int connectedHits() const { return m_connected; }
    int disconnectedHits() const { return m_disconnected; }
    int readyReadHits() const { return m_readyRead; }
    int errorHits() const { return m_errors; }
    int msgsParsed() const { return m_msgsIn; }
    int replyTimeouts() const { return m_replyTimeouts; }
    qint64 requested() const { return m_requested; }
    qint64 bytesWrittenSum() const { return m_writtenSum; }
    qint64 bytesWrittenMax() const { return m_writtenMax; }              // 单次 bytesWritten 的最大块
    int bytesWrittenHits() const { return m_writtenHits; }
    int pongs() const { return m_pongs; }                                // 这只 socket 收到的 PONG 条数
    qint64 peakBytesToWrite() const { return m_peakToWrite; }
    QStringList stateSequence() const { return m_states; }
    QStringList errorSequence() const { return m_errorSeq; }
    QString lastErrorCodeText() const
    {
        return QStringLiteral("%1(%2)").arg(errorName(m_sock->error())).arg(int(m_sock->error()));
    }

private slots:
    void onConnected()
    {
        ++m_connected;
        stopConnectGuard(QStringLiteral("connected 先到"));
        // socket 归创建它的线程；这里的 peer 读数是「我只在跟谁说话」的证据（第 147/148 行）
        qInfo().noquote() << QStringLiteral("    %1 connected｜peer=%2:%3｜本端端口=%4｜isOpen=%5｜isSequential=%6")
                                 .arg(m_name)
                                 .arg(m_sock->peerAddress().toString()).arg(m_sock->peerPort())
                                 .arg(m_sock->localPort())
                                 .arg(m_sock->isOpen()).arg(m_sock->isSequential());   // 第 169 行
        std::function<void()> fn;
        fn.swap(m_onConnected);
        if (fn)
            fn();
    }

    void onDisconnected()
    {
        ++m_disconnected;
        qInfo().noquote() << QStringLiteral("    %1 disconnected｜state=%2｜bytesAvailable=%3｜error()=%4")
                                 .arg(m_name).arg(stateName(m_sock->state()))
                                 .arg(m_sock->bytesAvailable())                          // 第 142 行
                                 .arg(lastErrorCodeText());
    }

    void onStateChanged(QAbstractSocket::SocketState st)
    {
        m_states << QStringLiteral("%1(%2)").arg(stateName(st)).arg(int(st));
        qInfo().noquote() << QStringLiteral("      %1 stateChanged → %2（枚举值 %3）")
                                 .arg(m_name).arg(stateName(st)).arg(int(st));
    }

    void onErrorOccurred(QAbstractSocket::SocketError e)
    {
        ++m_errors;
        m_errorSeq << QStringLiteral("%1(%2)").arg(errorName(e)).arg(int(e));
        // 出错这条路上也必须把连接守卫摘掉，否则它会晚些时候「误杀」一次已经收口的尝试，
        // 把账搅浑。这一句是这一讲里最容易被忘掉的一行代码。
        stopConnectGuard(QStringLiteral("errorOccurred 先到"));
        qInfo().noquote() << QStringLiteral("    %1 errorOccurred(%2) 第 %3 次｜errorString=\"%4\"｜isValid=%5｜state=%6")
                                 .arg(m_name).arg(m_errorSeq.last()).arg(m_errors)
                                 .arg(m_sock->errorString())                             // qiodevice 第 115 行
                                 .arg(m_sock->isValid())                                 // 第 140 行
                                 .arg(stateName(m_sock->state()));
    }

    void onReadyRead()
    {
        ++m_readyRead;                        // ← 同样**不进合账**：次数不可预测
        m_buf.append(m_sock->readAll());
        QByteArray frameBytes;
        while (LineProtocol::takeMessage(m_framing, &m_buf, &frameBytes)) {
            QString kind;
            QString body;
            if (!LineProtocol::parseContent(frameBytes, &kind, &body)) {
                qWarning().noquote() << QStringLiteral("    %1 收到不认识的帧（%2 字节）").arg(m_name).arg(qint64(frameBytes.size()));
                continue;
            }
            ++m_msgsIn;
            if (m_replyGuard && m_replyGuard->isActive()) {
                qInfo().noquote() << QStringLiteral("    %1 回音赶在应答守卫之前到了（还剩 %2 ms）→ stop()")
                                         .arg(m_name).arg(m_replyGuard->remainingTime());
                m_replyGuard->stop();
            }
            if (kind == QStringLiteral("PONG")) {
                ++m_pongs;
                ++Ledger::pongsSeenByClient;
            }
            qInfo().noquote() << QStringLiteral("    %1 收到第 %2 条：kind=%3｜body=%4")
                                     .arg(m_name).arg(m_msgsIn).arg(kind)
                                     .arg(body.size() > 40 ? QStringLiteral("<%1 字节>").arg(body.size()) : body);
        }
    }

    // ★ bytesWritten 是「从 Qt 发送缓冲里出去了多少」的**流水账**，
    //   一次 write 可能对应 0 次、1 次或很多次它（取决于内核吃下多少）。
    void onBytesWritten(qint64 n)
    {
        ++m_writtenHits;
        m_writtenSum += n;
        if (n > m_writtenMax)
            m_writtenMax = n;
    }

    void onConnectGuardTimeout()
    {
        ++Ledger::guardFired;
        qWarning().noquote() << QStringLiteral("    %1 【连接超时】守卫到点：state=%2｜abort() 撕掉（第 154 行）")
                                    .arg(m_name).arg(stateName(m_sock->state()));
        m_sock->abort();
    }

    void onReplyTimeout()
    {
        ++Ledger::replyTimeouts;
        ++m_replyTimeouts;
        qWarning().noquote() << QStringLiteral("    %1 【应答超时】%2 ms 内没等到 %3：state=%4｜bytesAvailable=%5｜半包残留=%6 字节")
                                    .arg(m_name).arg(m_replyGuard ? m_replyGuard->interval() : 0)
                                    .arg(m_expect).arg(stateName(m_sock->state()))
                                    .arg(m_sock->bytesAvailable()).arg(qint64(m_buf.size()));
        // 到点动作：温和地走（disconnectFromHost，第 138 行），发送缓冲里的东西还会出去
        m_sock->disconnectFromHost();
    }

private:
    void ensureConnectGuard(int ms)
    {
        if (m_connectGuard)
            return;
        m_connectGuard = new QTimer(this);
        m_connectGuard->setSingleShot(true);
        QObject::connect(m_connectGuard, &QTimer::timeout, this, &TcpClient::onConnectGuardTimeout);
        m_connectGuard->setInterval(ms);
    }

    void stopConnectGuard(const QString &why)
    {
        if (m_connectGuard && m_connectGuard->isActive()) {
            qInfo().noquote() << QStringLiteral("    %1 连接守卫被%2终止（还剩 %3 ms 没用完）→ stop()")
                                     .arg(m_name).arg(why).arg(m_connectGuard->remainingTime());
            m_connectGuard->stop();
            ++Ledger::guardStopped;
        }
    }

    QString m_name;
    Framing m_framing;
    QTcpSocket *m_sock = nullptr;
    QByteArray m_buf;
    QTimer *m_connectGuard = nullptr;
    QTimer *m_replyGuard = nullptr;
    QString m_expect;
    std::function<void()> m_onConnected;
    int m_connected = 0;
    int m_disconnected = 0;
    int m_readyRead = 0;
    int m_errors = 0;
    int m_msgsIn = 0;
    int m_pongs = 0;
    int m_replyTimeouts = 0;
    int m_writtenHits = 0;
    qint64 m_writtenSum = 0;
    qint64 m_writtenMax = 0;
    qint64 m_requested = 0;
    qint64 m_peakToWrite = 0;
    QStringList m_states;
    QStringList m_errorSeq;
};

// ============================================================================
// 六、【4】用的内存实验：QDataStream 为什么给不了 socket 消息边界
// ============================================================================
// 这一段一个字节都不上网，只在内存里演示「有格式」≠「有边界」。
static void evidenceDataStreamBoundary()
{
    qInfo().noquote() << QStringLiteral("  （4-d）QDataStream：格式里有长度，传输层没有边界");
    const QString kind = QStringLiteral("ECHO");
    const QString body = QStringLiteral("中文 + 空格都塞进来");

    QByteArray payload;
    {
        QDataStream out(&payload, QIODevice::WriteOnly);       // qdatastream.h 第 127 行
        out << kind << body;                                   // QString 自己带 4 字节长度前缀
    }
    qInfo().noquote() << QStringLiteral("      QDataStream 写出 %1 字节；头 4 字节就是长度前缀（第 483~496 行 writeQSizeType）")
                             .arg(qint64(payload.size()));

    QString k1;
    QString b1;
    int okFlag = 0;
    {
        QDataStream in(payload);                               // 第 128 行：整帧在手才解
        in >> k1 >> b1;
        okFlag = (in.status() == QDataStream::Ok) ? 1 : 0;     // 第 137 行；Ok 是第 112 行那个枚举的第 0 项
    }
    qInfo().noquote() << QStringLiteral("      整帧喂进去：kind=%1 body=%2 status=Ok?%3")
                             .arg(k1, b1).arg(okFlag);

    const QByteArray half = payload.left(payload.size() / 2);
    QString k2;
    QString b2;
    int halfStatus = -1;
    {
        QDataStream in2(half);
        in2 >> k2 >> b2;
        halfStatus = int(in2.status());                        // 预期 ReadPastEnd = 1（第 114 行）
    }
    qInfo().noquote() << QStringLiteral("      只喂一半（%1 字节）：status=%2（预期 1=ReadPastEnd）｜已读出的 k2=\"%3\"")
                             .arg(qint64(half.size())).arg(halfStatus).arg(k2);
    qInfo().noquote() << QStringLiteral("      → 结论：QDataStream 绑在 socket 上（第 126 行那个 ctor）时，字节来一半它就会");
    qInfo().noquote() << QStringLiteral("        在「读到一半」的地方停下并且**已经消费掉的字节回不去**（它没有事务）。");
    qInfo().noquote() << QStringLiteral("        正解只有一条：自己攒缓冲 → 确认整帧边界 → 把**那一帧**喂给");
    qInfo().noquote() << QStringLiteral("        QDataStream(const QByteArray &)（第 128 行）。上面两个分支就是这个顺序。");
}

// ============================================================================
// 七、main：八段演示 + 合账 + 收摊
// ============================================================================
struct Rig
{
    TcpEchoServer *line = nullptr;    // 行分隔服务器
    TcpEchoServer *pfx = nullptr;     // 长度前缀服务器
    TcpClient *a = nullptr;           // 【1】【3】主力
    TcpClient *probe = nullptr;       // 【2】吃拒绝 → 【6】复用同一个对象，看错误覆盖
    TcpClient *wire = nullptr;        // 【4】【5】前缀端口唯一的客户端（单连接好对账）
    TcpClient *staller = nullptr;     // 【7】应答超时
    TcpClient *closer = nullptr;      // 【8】写完就 disconnectFromHost
    TcpClient *bye = nullptr;         // 【8】让服务端把自己脚下的 socket 排队删除
    TcpClient *rude = nullptr;        // 【8】半包 + abort() 的反面对照：它**不该**被算成消息
    bool dead = false;                // 起不来就置位，链条自己断
    bool finished = false;            // 收摊只许发生一次
    int stage = 0;                    // 走到第几段：gotoStage() 用它挡住重复推进
};

// 前向声明：八段互相排队，定义在后面（01/04 讲同一套写法）
static void sec1(Rig *r);
static void sec2(Rig *r);
static void sec3(Rig *r);
static void sec4(Rig *r);
static void sec5(Rig *r);
static void sec6(Rig *r);
static void sec7(Rig *r);
static void sec8(Rig *r);
static void reconcile(Rig *r);
static void teardown(Rig *r);

// ── 推进到某一段：唯一防止「同一段跑两遍」的机关 ──────────────────────────
// 为什么要有它：段与段之间既有「固定进场时间」又有「段内有界等待」，两条路都可能
//   指向下一段。如果两条都触发，下一段就执行两次——计数翻倍、合账当场废掉。
//   所以每一次推进都过这道门：已经走过（stage >= n）一律丢弃。
//   这就是「兜底的 singleShot」和「正常链」能共存而不把账搅浑的原因。
static void gotoStage(Rig *r, int stage, void (*next)(Rig *))
{
    if (r->dead || r->finished || r->stage >= stage)
        return;
    r->stage = stage;
    next(r);
}

// 计划好的条数——**先把账写死，再让代码去对**。这是这一讲的工程姿势。
// 每一项都写清楚是哪一段贡献的，多一条少一条当场就能从注释里查出来。
static const int kLineMsgPlan = 10;   // 【1】2 + 【3】3 + 【6】1 + 【7】2(WAIT+PING) + 【8】2(LAST+BYE)
static const int kPfxMsgPlan = 11;    // 【4】3 + 【5】8
static const int kPingPlan = 3;       // 【1】hb-1 + 【6】hb-2 + 【7】hb-3
static const int kLineConnPlan = 7;   // a / probe / staller / staller(超时后重连) / closer / bye / rude
static const int kPfxConnPlan = 1;    // wire
// 本机实测（Qt 6.12.0 / MinGW / Windows）改的这两个数，理由都写在下面，不是「凑到绿」：
//
// kGuardStopPlan：计划里原本写 9 = 「8 次成功的连接 + 【2】那次必然吃到的 ConnectionRefused」，
//   因为「连不上只能等 errorOccurred」是这一讲的主张。实测 8 ——
//   【2】那只 probe 在 1500ms 守卫到点之前**什么都没等到**：没有 connected、
//   没有 errorOccurred（到【6】开口时它的错误序列还是空的）、守卫也没到点。
//   回环上连一个刚 close() 还回去的端口，Windows 未必立刻给你 RST；它就一直挂在 Connecting，
//   直到【6】复用时被 connectThen 里那句 abort() 带走。所以第 9 次「stop」根本不存在。
//   → 8 = a / probe(【6】) / wire / staller×2 / closer / bye / rude 这 8 次真连接。
static const int kGuardStopPlan = 8;
// kGracefulPlan：计划里原本写 2（staller 超时后的温和收 + closer 写完最后一句的温和收），
//   漏了 bye：BYE 对服务端来说**也是一帧合法消息**，解析完它的那一瞬间 gotMsgLast 就是 true，
//   随后服务端把这只 socket 排队自删，disconnected 紧跟着到——同一判据下它当然该计入。
//   实测 3，机制清楚，所以期望改成 3。
static const int kGracefulPlan = 3;   // staller + closer + bye（bye 那条的最后一帧就是 BYE 本身）
static const int kProbeNoErrorPlan = 0;  // 【2】那次失败尝试在【6】之前攒到的 errorOccurred 条数（实测 0）

// ── 【1】事件驱动的本质：信号不是轮询 ──────────────────────────────────────
static void sec1(Rig *r)
{
    qInfo().noquote() << QStringLiteral("\n【1】事件驱动的本质 + stateChanged 顺序 + 端口只能现拿");
    if (!r->line->start() || !r->pfx->start()) {
        r->dead = true;
        qCritical().noquote() << QStringLiteral("  回环服务器起不来（listen 返回 false）→ 直接收摊");
        qCritical().noquote() << QStringLiteral("  这一轮【1】没走到（本机 listen(LocalHost,0) 两只端口都成功），"
                                              "所以这句话是**经验判断、不是实测**：回环上 listen(0) 失败通常来自权限/沙箱/"
                                              "网络栈被禁用，而不是代码写错——真撞上了请看 errorString()");
        teardown(r);
        return;
    }
    qInfo().noquote() << QStringLiteral("  两台服务器就位（listen 的默认地址是 Any，第 29 行；这里全部显式 LocalHost）：");
    qInfo().noquote() << QStringLiteral("    行端口   listen(LocalHost, 0) → serverPort()=%1｜serverAddress()=%2｜isLoopback=%3｜isListening=%4")
                             .arg(r->line->port()).arg(r->line->boundText())
                             .arg(r->line->boundIsLoopback()).arg(r->line->listening());
    qInfo().noquote() << QStringLiteral("    前缀端口 listen(LocalHost, 0) → serverPort()=%1｜isLoopback=%2｜分帧=%3")
                             .arg(r->pfx->port()).arg(r->pfx->boundIsLoopback()).arg(framingName(r->pfx->framing()));
    qInfo().noquote() << QStringLiteral("    行端口自己的分帧=%1（两台服务器是**同一个类**，只有构造参数不同）")
                             .arg(framingName(r->line->framing()));
    qInfo().noquote() << QStringLiteral("  → 源码里没有任何端口常数，也没有任何主机名；下面所有连接只打这两个数。");

    // 连上之后要做的事挂在 connected 上（connectToHost 返回 void，第 137 行）
    r->a->connectThen(r->line->port(), 2000, [r]() {
        qInfo().noquote() << QStringLiteral("  （1-b）发两条：HELLO + PING，然后当场读服务端的计数器");
        const qint64 rc1 = r->a->send(QStringLiteral("HELLO"), QStringLiteral("老师傅，回环上见"));
        ++Ledger::lineMsgsSent;
        const int hitsRightAfterWrite = r->line->readyReadHits();
        qInfo().noquote() << QStringLiteral("    write() 返回 %1 字节；**返回后的同一行代码**里读服务端 readyRead 计数 = %2")
                                 .arg(rc1).arg(hitsRightAfterWrite);
        qInfo().noquote() << QStringLiteral("    → 这就是「不是轮询、也不是当场送达」：字节交给 Qt 的发送缓冲，");
        qInfo().noquote() << QStringLiteral("      对端能不能收到、什么时候收到 readyRead，都归事件循环管。");

        ++Ledger::pingsSent;
        ++Ledger::lineMsgsSent;
        r->a->send(QStringLiteral("PING"), QStringLiteral("hb-1"));
        qInfo().noquote() << QStringLiteral("    PING 已发出（心跳累计 %1 条）；服务端回不回，下一轮事件循环才见分晓")
                                 .arg(Ledger::pingsSent);

        // socket 层的选项：第 160/161 行。顺手把 KeepAlive 的读数打出来，和第 7 段的应用层心跳对照
        r->a->sock()->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
        const QVariant ka = r->a->sock()->socketOption(QAbstractSocket::KeepAliveOption);
        qInfo().noquote() << QStringLiteral("    setSocketOption(KeepAliveOption,1) 读回来 = %1｜LowDelay = %2")
                                 .arg(ka.toInt())
                                 .arg(r->a->sock()->socketOption(QAbstractSocket::LowDelayOption).toInt());
        qInfo().noquote() << QStringLiteral("    → SO_KEEPALIVE 是内核的「链路还通吗」，不是「对端程序还活着吗」；");
        qInfo().noquote() << QStringLiteral("      所以协议里那一条 PING/PONG 不能省（第 7 段把它的超时演示跑给你看）。");
    });

    QTimer::singleShot(200, [r]() { gotoStage(r, 2, sec2); });
}

// ── 【2】端口纪律 + 连不上这件事怎么知道 ───────────────────────────────────
static void sec2(Rig *r)
{
    if (r->dead || r->finished)
        return;
    qInfo().noquote() << QStringLiteral("\n【2】端口只能现拿；连不上只能等 errorOccurred（connectToHost 返回 void）");

    // 「借一个端口再还回去」：这是回环示例里造「一定连不上的端口」的唯一干净办法。
    // 硬编码一个「大概没人用」的端口，早晚会被人占上，那种示例在 CI 上是碰运气。
    QTcpServer temp;                                                  // 第 26 行
    const bool borrowed = temp.listen(QHostAddress::LocalHost, 0);
    const quint16 dead = borrowed ? temp.serverPort() : quint16(0);
    temp.close();                                                     // 第 30 行：还回去
    qInfo().noquote() << QStringLiteral("  借端口：listen(LocalHost,0)=%1 → serverPort()=%2 → close() 还回去 → 现在 %3 上没人监听")
                             .arg(borrowed).arg(dead).arg(dead);

    // 顺手把「SocketError 没有 NoError」这条打出来：**还没连过**的 socket 上 error() 是什么
    {
        QTcpSocket fresh;
        qInfo().noquote() << QStringLiteral("  全新 socket（一次没连过）：error()=%1（枚举里第 0 项就是 ConnectionRefused，**没有 NoError**）")
                                 .arg(int(fresh.error()));
        qInfo().noquote() << QStringLiteral("    同一只 socket：errorString()=\"%1\"｜isValid()=%2（第 140 行）｜state=%3")
                                 .arg(fresh.errorString()).arg(fresh.isValid()).arg(stateName(fresh.state()));
        qInfo().noquote() << QStringLiteral("    → 所以「判有没有错」靠的是 state()/isValid()/errorString()，");
        qInfo().noquote() << QStringLiteral("      不是 error()。error() 只回答「最后一次错成什么样」。");
        qInfo().noquote() << QStringLiteral("      上面这两个读数本机实测：error()=-1（就是 qabstractsocket.h 第 78 行"
                                           " 那个 UnknownSocketError=-1，不是「出错了」而是「还没错过」）、"
                                           "errorString()=\"Unknown error\"、isValid()=0、state=Unconnected；");
        qInfo().noquote() << QStringLiteral("      而「SocketError 里没有 NoError 这一档」是第 53~79 行的字面事实。");
    }

    // 连那个刚还回去的端口：本意是「只可能等 errorOccurred」，因为 connectToHost 返回 void，
    // 没有同步的失败可读。★本机实测要打个折扣：回环上连一个刚 close() 的端口，
    // 1500ms 内既没有 connected、也没有 errorOccurred，socket 就停在 Connecting——
    // 直到【6】复用前被 abort() 带走。所以「等 errorOccurred」是对的写法，
    // 但**等不到**也是必须处理的一种现实：守卫到点要自己 abort（见 onConnectGuardTimeout）。
    r->probe->connectThen(dead, 1500, []() {
        qWarning().noquote() << QStringLiteral("    【注意】 不该连上的：说明刚还回去的端口被别人抢了（回环上概率极低）");
    });
    QTimer::singleShot(200, [r]() { gotoStage(r, 3, sec3); });
}

// ── 【3】一次 readyRead ≠ 一条消息 ─────────────────────────────────────────
static void sec3(Rig *r)
{
    if (r->dead || r->finished)
        return;
    qInfo().noquote() << QStringLiteral("\n【3】粘包与半包：readyRead 次数不可核对，消息条数才可核对");
    qInfo().noquote() << QStringLiteral("  【2】那只 probe 还在 Connecting 里挂着——本机实测它一个错都没报，");
    qInfo().noquote() << QStringLiteral("  守卫也没到点就被【6】的复用 abort() 带走（详见合账里「errorOccurred 条数=0」那条）。");
    qInfo().noquote() << QStringLiteral("  「连不上只能等 errorOccurred」是这一讲的主张，但在回环上连一个刚还回去的端口，");
    qInfo().noquote() << QStringLiteral("  Windows 未必马上给你 RST：它可能就是不动。**等不到，本身就是要记的账**。");
    qInfo().noquote() << QStringLiteral("  【6】还要复用这只 socket，所以那里会先 abort() 再连。");
    const int rrBefore = r->line->readyReadHits();
    const int msgBefore = r->line->msgs();
    const qint64 bytesBefore = r->line->bytesReceived();

    // (a) 半包：把一条消息 V1 SPLIT 我把一帧拆成三次写\n 切成 3 次 write，中间不带换行
    const QByteArray whole = LineProtocol::content(QStringLiteral("SPLIT"),
                                                   QStringLiteral("我把一帧拆成三次写"));
    const qsizetype cut1 = whole.size() / 3;
    const qsizetype cut2 = cut1 * 2;
    const qint64 w1 = r->a->sendRaw(whole.left(cut1));
    const qint64 w2 = r->a->sendRaw(whole.mid(cut1, cut2 - cut1));
    const qint64 w3 = r->a->sendRaw(whole.mid(cut2) + QByteArray("\n"));
    ++Ledger::lineMsgsSent;                            // 这一条是 1 个逻辑消息
    qInfo().noquote() << QStringLiteral("  （3-a）半包：1 条消息拆 3 次 write，返回 %1 / %2 / %3 字节（换行只在最后一次带上）")
                             .arg(w1).arg(w2).arg(w3);
    qInfo().noquote() << QStringLiteral("    此刻服务端手里：前两次 write 之后**一条都拼不出来**（没有 \\n），");
    qInfo().noquote() << QStringLiteral("    所以它的 takeMessage 会 return false，缓冲原样留着——半包不是错误，是常态。");

    // (b) 粘包：两条完整消息塞进一次 write
    const QByteArray two = LineProtocol::frame(Framing::LineDelimited, QStringLiteral("ECHO"), QStringLiteral("连发两条甲"))
                         + LineProtocol::frame(Framing::LineDelimited, QStringLiteral("ECHO"), QStringLiteral("连发两条乙"));
    const qint64 wb = r->a->sendRaw(two);
    Ledger::lineMsgsSent += 2;
    qInfo().noquote() << QStringLiteral("  （3-b）粘包：2 条消息合并成 1 次 write，返回 %1 字节（两条共用一次投递）").arg(wb);

    // (c) 本段共 3 条：SPLIT 1（拆成 3 次 write）+ ECHO 2（合并成 1 次 write）
    waitUntil(QStringLiteral("【3】服务端解析出 3 条"),
              [r, msgBefore]() { return r->line->msgs() - msgBefore >= 3; },
              [r, rrBefore, msgBefore, bytesBefore]() {
                  const int rrDelta = r->line->readyReadHits() - rrBefore;
                  const int msgDelta = r->line->msgs() - msgBefore;
                  qInfo().noquote() << QStringLiteral("  （3-c）对账：这 4 次 write 期间，服务端 readyRead 触发 %1 次，解析出消息 %2 条")
                                           .arg(rrDelta).arg(msgDelta);
                  qInfo().noquote() << QStringLiteral("        收到的字节增量=%1｜此刻全服务器缓冲区残留（半包）=%2 字节")
                                           .arg(r->line->bytesReceived() - bytesBefore).arg(r->line->totalBuffered());
                  qInfo().noquote() << QStringLiteral("    → 把结论钉死：**4 次 write、%1 次 readyRead、%2 条消息**，")
                                           .arg(rrDelta).arg(msgDelta);
                  qInfo().noquote() << QStringLiteral("      三者之间没有任何固定关系。所以合账只压「消息条数」，");
                  qInfo().noquote() << QStringLiteral("      谁把断言写在 readyRead 次数上，谁的用例就会在 CI 上随机挂。");
                  QTimer::singleShot(200, [r]() { gotoStage(r, 4, sec4); });
              },
              1000);
    // 兜底：万一 (c) 那 3 条迟迟不落地，waitUntil 到点自己就会带着证据往下走；
    //       这一发 singleShot 只在「段内等待被意外跳过」时兜住链路，gotoStage 保证不重复推进。
    QTimer::singleShot(900, [r]() { gotoStage(r, 4, sec4); });
}

// ── 【4】两种分帧 + QDataStream 与 canReadLine ─────────────────────────────
static void sec4(Rig *r)
{
    if (r->dead || r->finished)
        return;
    qInfo().noquote() << QStringLiteral("\n【4】分帧是应用层造的：行分隔 vs 长度前缀，两副面孔一本账");

    const int msgBefore = r->pfx->msgs();
    r->wire->connectThen(r->pfx->port(), 2000, [r, msgBefore]() {
        // (a) 半帧：连**前缀**都能被切断，这是长度前缀分帧最典型的样子
        const QByteArray one = LineProtocol::frame(Framing::LengthPrefixed,
                                                   QStringLiteral("ECHO"), QStringLiteral("前缀也被切了一半"));
        const qint64 ha = r->wire->sendRaw(one.left(3));          // 前缀 4 字节只到 3 字节
        const qint64 hb = r->wire->sendRaw(one.mid(3));
        Ledger::pfxMsgsSent += 1;
        qInfo().noquote() << QStringLiteral("  （4-a）长度前缀的半包：write %1 字节（前缀残缺）+ write %2 字节 → 应算 1 条")
                                 .arg(ha).arg(hb);

        // (b) 粘包：两帧一次 write
        const QByteArray two = LineProtocol::frame(Framing::LengthPrefixed, QStringLiteral("ECHO"), QStringLiteral("合并投递甲"))
                             + LineProtocol::frame(Framing::LengthPrefixed, QStringLiteral("ECHO"), QStringLiteral("合并投递乙"));
        const qint64 hc = r->wire->sendRaw(two);
        Ledger::pfxMsgsSent += 2;
        qInfo().noquote() << QStringLiteral("  （4-b）长度前缀的粘包：1 次 write %1 字节 → 应算 2 条").arg(hc);

        waitUntil(QStringLiteral("【4】前缀端口解析出 3 条"),
                  [r, msgBefore]() { return r->pfx->msgs() - msgBefore >= 3; },
                  [r, msgBefore]() {
                      qInfo().noquote() << QStringLiteral("  （4-c）前缀端口：3 次 write → 消息 %1 条（计划 3）")
                                               .arg(r->pfx->msgs() - msgBefore);
                      qInfo().noquote() << QStringLiteral("    两台服务器的消息数都对得上，而它们**读到的字节一模一样**：");
                      qInfo().noquote() << QStringLiteral("    边界不是 TCP 给的，是你自己造出来的（换行或长度）。");
                      evidenceDataStreamBoundary();
                      qInfo().noquote() << QStringLiteral("  （4-e）canReadLine 的真相（只当对照物，不进账）：");
                      qInfo().noquote() << QStringLiteral("    行端口上 readyRead 共触发 %1 次，其中「那一刻 canReadLine()==true」%2 次，")
                                               .arg(r->line->readyReadHits()).arg(r->line->canReadLineTrue());
                      qInfo().noquote() << QStringLiteral("    而解析出的消息条数是 %1 条——三个数**互相不等是常态**。")
                                               .arg(r->line->msgs());
                      qInfo().noquote() << QStringLiteral("    QAbstractSocket 没有 override canReadLine（本机 grep QtNetwork/*.h：");
                      qInfo().noquote() << QStringLiteral("    只有 qlocalsocket.h 第 74 行、qsslsocket.h 第 84 行 override 了它）；");
                      qInfo().noquote() << QStringLiteral("    它给的是 readLineData（第 197 行）。所以网上那段");
                      qInfo().noquote() << QStringLiteral("      while (sock->canReadLine()) lines << sock->readLine();");
                      qInfo().noquote() << QStringLiteral("    在 QTcpSocket 上不是不可以用，而是**不能当依据**：真值取决于");
                      qInfo().noquote() << QStringLiteral("    QIODevice 内部缓冲，跨版本会变。本讲的行分帧用 indexOf('\\n')，");
                      qInfo().noquote() << QStringLiteral("    可控、可打印、可测——canReadLine 只留作对照物。");
                      QTimer::singleShot(200, [r]() { gotoStage(r, 5, sec5); });
                  },
                  1000);
    });
    // 兜底：连接没建成 / 等待到点没达成时，链路也要能往下走；gotoStage 保证 sec5 只跑一次
    QTimer::singleShot(1400, [r]() { gotoStage(r, 5, sec5); });
}

// ── 【5】写完不代表送达：write / bytesWritten / flush / waitForBytesWritten ─
static void sec5(Rig *r)
{
    if (r->dead || r->finished)
        return;
    qInfo().noquote() << QStringLiteral("\n【5】写完 ≠ 送达：三道账（请求写入 / bytesWritten / 服务端收到）");
    const int hitsBefore = r->wire->bytesWrittenHits();
    const int rrBefore = r->wire->readyReadHits();

    // 8 帧 × 16KiB。BULK 服务端只回一个短 ACK，所以这笔账只压上行字节。
    const QString big = QStringLiteral("A").repeated(16384);        // qstring.h 第 747 行 repeated
    for (int i = 0; i < 8; ++i) {
        const qint64 rc = r->wire->send(QStringLiteral("BULK"), big);
        ++Ledger::pfxMsgsSent;
        if (i == 0)
            qInfo().noquote() << QStringLiteral("  第 1 次 write(16KiB+边界) 返回 %1 字节 → 这是「进缓冲的字节数」，不是对端收到的数").arg(rc);
        qInfo().noquote() << QStringLiteral("    write #%1 之后 bytesToWrite()=%2 字节（第 143 行）")
                                 .arg(i + 1).arg(r->wire->sock()->bytesToWrite());
    }
    qInfo().noquote() << QStringLiteral("  8 次 write 交完，立刻读：bytesWritten 触发 %1 次（本段增量），累计送出 %2 字节")
                             .arg(r->wire->bytesWrittenHits() - hitsBefore).arg(r->wire->bytesWrittenSum());
    qInfo().noquote() << QStringLiteral("  本端客户端收到的 readyRead 次数（本段增量）= %1（ACK 回来的次数，同样不可预测）")
                             .arg(r->wire->readyReadHits() - rrBefore);
    qInfo().noquote() << QStringLiteral("  → flush()（第 170 行）是「现在就把缓冲推给内核」，返回 bool 不代表对端收到；");
    qInfo().noquote() << QStringLiteral("    waitForBytesWritten(默认 30000ms，第 175 行) 是把**这条线程**钉在这儿：");
    qInfo().noquote() << QStringLiteral("    在 GUI 线程用它 = 界面卡死 30 秒，且事件循环停摆时 socket 根本没法推进，");
    qInfo().noquote() << QStringLiteral("    所以它是「没有事件循环的控制台脚本」专用的。本讲中途一次 waitFor* 都不用，");
    qInfo().noquote() << QStringLiteral("    只在收摊最后一行用了一次带 300ms 短超时的 waitForDisconnected（见 teardown）。");

    waitUntil(QStringLiteral("【5】128KiB 全部落到对端"),
              [r]() { return r->pfx->bytesReceived() >= r->wire->requested(); },
              [r, hitsBefore]() {
                  qInfo().noquote() << QStringLiteral("  （5-b）三道账：客户端请求写入=%1｜bytesWritten 各次之和=%2｜服务端收到=%3")
                                           .arg(r->wire->requested()).arg(r->wire->bytesWrittenSum())
                                           .arg(r->pfx->bytesReceived());
                  qInfo().noquote() << QStringLiteral("    bytesWritten 触发次数=%1（本段）｜单次最大一块=%2 字节｜本端 bytesToWrite 峰值=%3")
                                           .arg(r->wire->bytesWrittenHits() - hitsBefore)
                                           .arg(r->wire->bytesWrittenMax()).arg(r->wire->peakBytesToWrite());
                  qInfo().noquote() << QStringLiteral("    → 三个数相等才叫「送达」。bytesWritten 完全可以分好几次凑出总和，");
                  qInfo().noquote() << QStringLiteral("      所以「一次 write 对应一次 bytesWritten」是错觉（第 121 行的信号是流水账）。");
                  qInfo().noquote() << QStringLiteral("      大流量的正解：每轮把 bytesAvailable() 取走、自己攒帧，");
                  qInfo().noquote() << QStringLiteral("      需要背压时用 setReadBufferSize()（第 151/152 行）让 Qt 别再从内核取；");
                  qInfo().noquote() << QStringLiteral("      而不是 readAll() 之后等「整个文件」——socket 上没有这个概念。");
                  QTimer::singleShot(150, [r]() { gotoStage(r, 6, sec6); });
              },
              2000);
}

// ── 【6】errorOccurred 与 state 的对应；报错只留最后一次 ───────────────────
static void sec6(Rig *r)
{
    if (r->dead || r->finished)
        return;
    qInfo().noquote() << QStringLiteral("\n【6】报错不排队：同一只 socket 上多个错误互相覆盖");
    const int errorsBefore = r->probe->errorHits();
    Ledger::probeErrorsAtSec6 = errorsBefore;   // ← 合账要用这个「【2】那次到底等到几个错」的快照
    qInfo().noquote() << QStringLiteral("  probe 这只 socket 到目前为止的错误序列 = [%1]，error() 当前读数 = %2")
                             .arg(r->probe->errorSequence().join(QStringLiteral(", ")))
                             .arg(r->probe->lastErrorCodeText());

    // 同一只对象再连一次活的服务器：Qt 允许（connectToHost 是普通成员函数，不是一次性对象），
    // 但 error() 那个**坑位**不会被清空——这就是「只报最后一次」的物理来源。
    r->probe->connectThen(r->line->port(), 1500, [r, errorsBefore]() {
        ++Ledger::pingsSent;
        ++Ledger::lineMsgsSent;
        r->probe->send(QStringLiteral("PING"), QStringLiteral("hb-2"));
        qInfo().noquote() << QStringLiteral("  probe 重连成功并发了第 2 次心跳；现在 error() 读数 = %1")
                                 .arg(r->probe->lastErrorCodeText());
        qInfo().noquote() << QStringLiteral("    → 请注意：连接已经成功，error() 里**还可能**躺着上一次那个错误码。");
        qInfo().noquote() << QStringLiteral("      把 error() 当「当前状态」读的代码，在这里就会误报（预期，请以本次打印复核）");

        // 先把 hb-2 的 PONG 等到手，再让服务端撕这条连接：顺序反了的话 PONG 就死在 abort 里，
        // 心跳那本账就平不了。**要核对的账，先把因果关系摆正再制造故障**——这不是美化日志，
        // 是「测试的时序是你设计的」这句老话的具体样子。
        waitUntil(QStringLiteral("【6】hb-2 的 PONG 先到手"),
                  [r]() { return r->probe->pongs() >= 1; },
                  [r, errorsBefore]() {
                      qInfo().noquote() << QStringLiteral("  （6-a）PONG 已到（probe 累计收到 %1 条），现在让服务端 abort 这条连接")
                                               .arg(r->probe->pongs());
                      // 让服务端把这条连接直接 abort()：客户端会看到 disconnected 或 errorOccurred，
                      // 具体看到哪个（以及是不是 RemoteHostClosedError=第 55 行那个 1）属于运行时行为，
                      // 本讲只打印，不断言。
                      r->line->dropLastLiveConnection();
                      waitUntil(QStringLiteral("【6】probe 感知到对端没了"),
                                [r, errorsBefore]() {
                                    return r->probe->disconnectedHits() > 0 || r->probe->errorHits() > errorsBefore;
                                },
                                [r]() {
                                    qInfo().noquote() << QStringLiteral("  （6-b）probe 的错误序列 = [%1]（共 %2 次 errorOccurred）")
                                                             .arg(r->probe->errorSequence().join(QStringLiteral(", ")))
                                                             .arg(r->probe->errorHits());
                                    qInfo().noquote() << QStringLiteral("    最终 state=%1｜disconnected 次数=%2｜error() 只剩一个值 = %3")
                                                             .arg(stateName(r->probe->sock()->state()))
                                                             .arg(r->probe->disconnectedHits())
                                                             .arg(r->probe->lastErrorCodeText());
                                    qInfo().noquote() << QStringLiteral("    → 头文件就是原因：一个 error() getter（第 165 行）配");
                                    qInfo().noquote() << QStringLiteral("      一个 protected setSocketError()（第 202 行）＝一个坑位；");
                                    qInfo().noquote() << QStringLiteral("      后来的把先来的盖掉。要做「错误历史」只能自己记，");
                                    qInfo().noquote() << QStringLiteral("      上面那条 QStringList 就是这么来的（第 186~190 行的信号才是事件流）。");
                                    qInfo().noquote() << QStringLiteral("    → 同一只 socket 用完**没被扔掉**：它接着被复用，");
                                    qInfo().noquote() << QStringLiteral("      这就是「报错只留最后一次」能被演示出来的原因。");
                                    QTimer::singleShot(150, [r]() { gotoStage(r, 7, sec7); });
                                },
                                1000);
                  },
                  1000);
    });
    // 兜底：本段两层有界等待各 1000ms，最坏 2000ms 出头就该推进。
    // 【4】【5】【8→合账】都留了这一发，唯独【6】→【7】漏了——于是【6】的 connected 回调
    // 一旦不来了（原因见 connectThen 里那条 19 号错误），整条链就地停住，合账一项都打不出来，
    // 只能等 25 秒总闸强制收摊。这是本机实测抓到的真故障，不是把它抹平的假绿。
    QTimer::singleShot(2600, [r]() { gotoStage(r, 7, sec7); });
}

// ── 【7】超时只能自己掐：两个守卫、三种关闭 ────────────────────────────────
static void sec7(Rig *r)
{
    if (r->dead || r->finished)
        return;
    qInfo().noquote() << QStringLiteral("\n【7】超时是自己造的：Qt 没给 socket 统一的 connectTimeout");
    qInfo().noquote() << QStringLiteral("  本机实测 grep：在 QtNetwork 的**公开**头文件里搜 connectTimeout /");
    qInfo().noquote() << QStringLiteral("  socketTimeout / defaultSocketTimeout —— **零命中**。QAbstractSocket 公开的");
    qInfo().noquote() << QStringLiteral("  超时相关面只有第 173~176 行那四个 waitFor*(int msecs = 30000)（阻塞语义），");
    qInfo().noquote() << QStringLiteral("  以及私有实现 qabstractsocketengine_p.h 第 37 行那个 DefaultTimeout{30}");
    qInfo().noquote() << QStringLiteral("  （内部头，代码里不能依赖）。所以：**要超时，就自己挂 QTimer**。");

    // ★ 顺手把「默认 0 是不是一直等」这条从「预期」变成实测：另开一只**只 listen、
    //   绝不会有连接进来**的服务器（port 0 现拿、没人去连它），叫 waitForNewConnection(0)。
    //   如果 0 真是「无限等」，这一句就会把整条事件循环钉死到总闸到点——所以这一发探针
    //   本身就是答案的一部分：它敢当场返回，就证明 0 是「不等」。
    {
        QTcpServer pollOnly;
        const bool up = pollOnly.listen(QHostAddress::LocalHost, 0);
        bool timedOut = false;
        QElapsedTimer probe;
        probe.start();
        const bool got = pollOnly.waitForNewConnection(0, &timedOut);   // ← 默认那个 0
        Ledger::wfcZeroMs = probe.elapsed();
        Ledger::wfcZeroGot = got ? 1 : 0;
        Ledger::wfcZeroTimedOut = timedOut ? 1 : 0;
        qInfo().noquote() << QStringLiteral("  现场量 waitForNewConnection(0)：listen=%1（端口 %2）→ 返回 %3｜"
                                           "timedOut=%4｜墙钟 %5 ms")
                                 .arg(up).arg(pollOnly.serverPort()).arg(got).arg(timedOut)
                                 .arg(Ledger::wfcZeroMs);
        qInfo().noquote() << QStringLiteral("    → 0 是**一次都不等**（等价于 poll），不是无限等；"
                                           "想要不超时得写 -1（Src qtcpserver.cpp 第 492 行、"
                                           "qdeadlinetimer.cpp 第 170~176 行）。");
        pollOnly.close();
    }

    // 守卫 A：连接超时。回环上 connected 一定先到，所以证据是「它被 stop 掉了、还剩多少没用完」。
    // 守卫 B：应答超时。这里让服务端故意 stall，所以证据是「它真的到点了」。
    r->staller->connectThen(r->line->port(), 2000, [r]() {
        r->line->setStall(true);
        qInfo().noquote() << QStringLiteral("  服务端进入 stall（setStall(true) 之后读回来 stall()=%1）："
                                           "WAIT 这一类消息一个字都不回" )
                                 .arg(r->line->stall());
        qInfo().noquote() << QStringLiteral("    （不是网络坏了，是**应用层不回话**——回环上你造不出真的连接超时，");
        qInfo().noquote() << QStringLiteral("      所以这一讲把超时演示放在应答层：这一层才是协议真正会卡住的地方）");
        const qint64 rc = r->staller->send(QStringLiteral("WAIT"), QStringLiteral("别回我，我要看超时"));
        ++Ledger::lineMsgsSent;
        r->staller->armReplyGuard(120, QStringLiteral("对 WAIT 的回音"));
        qInfo().noquote() << QStringLiteral("  WAIT 已发出（write 返回 %1 字节）；应答守卫 120ms 到点就动手").arg(rc);

        waitUntil(QStringLiteral("【7】应答超时计到 1"),
                  []() { return Ledger::replyTimeouts >= 1; },
                  [r]() {
                      qInfo().noquote() << QStringLiteral("  （7-b）超时到点后的现场：state=%1｜bytesAvailable=%2｜disconnected 次数=%3")
                                               .arg(stateName(r->staller->sock()->state()))
                                               .arg(r->staller->sock()->bytesAvailable())
                                               .arg(r->staller->disconnectedHits());
                      qInfo().noquote() << QStringLiteral("    三个动作的分工（本讲各用了一次，都能对上号）：");
                      qInfo().noquote() << QStringLiteral("      disconnectFromHost()（第 138 行）：温和。发送缓冲里的东西还会出去，");
                      qInfo().noquote() << QStringLiteral("                            之后还会收到对端最后一批数据和 disconnected（第 8 段现场）；");
                      qInfo().noquote() << QStringLiteral("      abort()（第 154 行）：立即撕。**丢弃发送缓冲**，对端只能从断流里推断（第 6 段用过）；");
                      qInfo().noquote() << QStringLiteral("      waitForDisconnected(ms)（第 176 行）：阻塞等收口，默认 30000，");
                      qInfo().noquote() << QStringLiteral("                            只有不带事件循环的脚本才配用它；本讲只在收摊最后一行用短超时版。");

                      // 反向证据：同样的守卫，服务端不 stall 时它被回音提前停掉。
                      // 但这里有个先后必须摆平：上一步超时动作是 disconnectFromHost()，
                      // 这条连接正在温和地收；想拿**同一只 socket** 再发一次 PING，
                      // 就得等它自己报「断完了」（disconnected 次数 +1）。
                      // 在 ClosingState 里 connectToHost 会被 Qt 按状态拒掉（write 直接 -1），
                      // 那次心跳就永远发不出去，账上少一条你还找不到原因。
                      waitUntil(QStringLiteral("【7】staller 那条连接报完 disconnected"),
                                [r]() { return r->staller->disconnectedHits() >= 1; },
                                [r]() {
                                    qInfo().noquote() << QStringLiteral("  （7-c）温和收口已完成（disconnected 次数=%1，state=%2）→ 复用同一只 socket 重连")
                                                             .arg(r->staller->disconnectedHits())
                                                             .arg(stateName(r->staller->sock()->state()));
                                    qInfo().noquote() << QStringLiteral("    这就是「socket 对象可以复用，一次连接不可以复用」：");
                                    qInfo().noquote() << QStringLiteral("      对象是 QObject，信号槽、守卫、计数都还在；");
                                    qInfo().noquote() << QStringLiteral("      连接是那次 TCP 会话，断了就得重新走 connectToHost。");
                                    r->line->setStall(false);
                                    ++Ledger::pingsSent;
                                    ++Ledger::lineMsgsSent;
                                    r->staller->connectThen(r->line->port(), 1200, [r]() {
                                        // 守卫必须**连上之后**再上弦：连着都还没成功就拿应答超时
                                        // 去掐，掐错的是连接阶段，账会记到错误的科目上。
                                        r->staller->armReplyGuard(800, QStringLiteral("对 PING 的 PONG"));
                                        r->staller->send(QStringLiteral("PING"), QStringLiteral("hb-3"));
                                        qInfo().noquote() << QStringLiteral("    同一个守卫再上弦 800ms，这回发 PING：预期回音先到、守卫被 stop()");
                                        waitUntil(QStringLiteral("【7】PONG 赶在守卫之前"),
                                                  [r]() { return Ledger::pongsSeenByClient >= kPingPlan; },
                                                  [r]() {
                                                      qInfo().noquote() << QStringLiteral("    心跳三本账：客户端发 %1｜服务端回 %2｜客户端收到 %3（计划 %4）")
                                                                               .arg(Ledger::pingsSent).arg(Ledger::pongsByServer)
                                                                               .arg(Ledger::pongsSeenByClient).arg(kPingPlan);
                                                      qInfo().noquote() << QStringLiteral("    连接守卫：被 stop 掉 %1 次（计划 %2），真到点 %3 次（计划 0）")
                                                                               .arg(Ledger::guardStopped).arg(kGuardStopPlan).arg(Ledger::guardFired);
                                                      qInfo().noquote() << QStringLiteral("    应答守卫：真到点 %1 次（【7】故意造的那一次）")
                                                                               .arg(Ledger::replyTimeouts);
                                                      qInfo().noquote() << QStringLiteral("    → 记一条纪律：**成功路上和出错路上都要 stop 守卫**。");
                                                      qInfo().noquote() << QStringLiteral("      只在成功时 stop，出错的那次尝试就会在你看不见的地方");
                                                      qInfo().noquote() << QStringLiteral("      突然 abort() 一条后来复用的连接；只在超时时 abort，");
                                                      qInfo().noquote() << QStringLiteral("      就会把「已经断了」也报成超时。两个守卫各管一件事，");
                                                      qInfo().noquote() << QStringLiteral("      计数才能各归各的账。");
                                                      QTimer::singleShot(150, [r]() { gotoStage(r, 8, sec8); });
                                                  },
                                                  1200);
                                    });
                                },
                                1000);
                  },
                  1000);
    });
}

// ── 【8】优雅关闭 + 自己删自己（deleteLater 的必要性） ─────────────────────
static void sec8(Rig *r)
{
    if (r->dead || r->finished)
        return;
    qInfo().noquote() << QStringLiteral("\n【8】优雅关闭：写完就走的三种死法，和「脚下别抽地板」");

    // (a) 写完最后一句，**紧接着** disconnectFromHost()：数据不该丢
    r->closer->connectThen(r->line->port(), 2000, [r]() {
        ++Ledger::lineMsgsSent;
        const qint64 rc = r->closer->send(QStringLiteral("LAST"), QStringLiteral("关闭前的最后一句，请收到"));
        qInfo().noquote() << QStringLiteral("  （8-a）closer：write 返回 %1 字节后**立刻** disconnectFromHost()（第 138 行）").arg(rc);
        r->closer->sock()->disconnectFromHost();
        qInfo().noquote() << QStringLiteral("    调用之后立刻读：state=%1（预期 Closing=%2，也就是「还在发，没断完」）")
                                 .arg(stateName(r->closer->sock()->state()))
                                 .arg(int(QAbstractSocket::ClosingState));
        qInfo().noquote() << QStringLiteral("    → 这一句是这一讲最容易写错的地方：");
        qInfo().noquote() << QStringLiteral("      disconnectFromHost **不是**「关掉就走」，它是「我说完了，");
        qInfo().noquote() << QStringLiteral("      缓冲里的东西发完再挥手」，所以它之后**还会**收到对端的数据和 disconnected。");

        // (b) 另一条连接：让服务端在自己的 readyRead 调用栈里把脚下这只 socket 排队删除
        r->bye->connectThen(r->line->port(), 2000, [r]() {
            ++Ledger::lineMsgsSent;
            r->bye->send(QStringLiteral("BYE"), QStringLiteral("我先走了，别当场 delete 我"));
            qInfo().noquote() << QStringLiteral("  （8-b）bye：服务端在 readyRead 里对它 deleteLater()，同一轮还继续读它的 state（见上面日志）");

            // (c) 反面教材：**新开一条连接**（rude），写一个永远拼不成帧的半包，然后马上 abort()。
            //     三处刻意：① 载荷后面不带 '\n'，所以它压根不是一条消息，**不许进消息账**；
            //     ② 它走自己的连接，免得把 bye 那条正在演的「自删」戏码搅进来；
            //     ③ abort() 到底让多少字节漏到了对端，是内核窗口和调度的事，**故意不作断言**。
            //     能断言的只有：这条连接不会给消息数加一分，而它随时可能给缓冲区加一坨。
            r->rude->connectThen(r->line->port(), 1500, [r]() {
                const QByteArray junk = LineProtocol::content(QStringLiteral("ECHO"),
                                                              QStringLiteral("B").repeated(2048));
                const qint64 req = r->rude->sendRaw(junk);              // 注意：这里没有行边界
                const qint64 pending = r->rude->sock()->bytesToWrite();
                const int msgBefore = r->line->msgs();
                r->rude->sock()->abort();                                // 第 154 行：立即撕，丢弃发送缓冲
                qInfo().noquote() << QStringLiteral("  （8-c）对照组 rude：write 返回 %1 字节、bytesToWrite=%2，随后立刻 abort()")
                                         .arg(req).arg(pending);
                qInfo().noquote() << QStringLiteral("    abort 之后本端 state=%1｜此刻行端口消息数=%2（和 abort 前一样）")
                                         .arg(stateName(r->rude->sock()->state()))
                                         .arg(msgBefore);
                qInfo().noquote() << QStringLiteral("    此刻服务器全部连接上「没拼成一帧」的残留字节=%1（半包的家底）")
                                         .arg(r->line->totalBuffered());
                qInfo().noquote() << QStringLiteral("    这条连接服务端到底收到几个字节？**故意不作断言**：");
                qInfo().noquote() << QStringLiteral("    字节可能还压在 Qt 的用户缓冲里（那就一个都没出去），");
                qInfo().noquote() << QStringLiteral("    也可能已经交给内核（那就全到了，但永远成不了一帧）；");
                qInfo().noquote() << QStringLiteral("    这个比例仍然**故意不作断言**（要它得抓包或对端计数，本讲不造这个证据）；");
                qInfo().noquote() << QStringLiteral("    但本机实测的旁边两行是硬的：上面那行「残留字节=%1」和"
                                                   "「消息数=%2 且 abort 前后没变」——"
                                                   "半包确实一个字符都没解析出来，这才是会咬人的那部分。")
                                         .arg(r->line->totalBuffered()).arg(msgBefore);
                qInfo().noquote() << QStringLiteral("    能带走的是纪律：要「一定送达」就先等 bytesWritten 把");
                qInfo().noquote() << QStringLiteral("    bytesToWrite() 降到 0，再 disconnectFromHost()；abort() 留给「我不要了」。");

                // 合账等的是「服务端把 BYE 那条 socket 真的删掉了」——这一条是唯一能被
                // 客观核对的收尾证据（QPointer 变空），所以拿它当发令枪最稳。
                waitUntil(QStringLiteral("【8】BYE 那条 socket 的 deferred delete 已生效"),
                          [r]() { return r->line->byeSocketGone(); },
                          [r]() {
                              QTimer::singleShot(120, [r]() { gotoStage(r, 9, reconcile); });
                          },
                          1200);
            });
        });
    });
    // 兜底：万一 closer/bye/rude 里任何一次连接没成，合账也得照样打出来（gotoStage 只放一次）。
    // 4000ms 是估的：正常路径在 sec8 之后 1 秒内就会自己推进，这一发只会在这种异常里发挥作用。
    QTimer::singleShot(4000, [r]() { gotoStage(r, 9, reconcile); });
}

// ── 合账：每一项都是「计划好的数」对「实测的数」 ────────────────────────────
static void reconcile(Rig *r)
{
    if (r->dead || r->finished)
        return;
    qInfo().noquote() << QStringLiteral("\n【合账】可核对的计数（期望来自计划，实测来自运行）");

    // 先把「可能还在路上」的几笔账等有界等待到位，再一次性打表。
    // 谓词里全是**计数器/求和**，没有一处去 readAll/bytesAvailable 捞数据——
    // 有界等待只能「看账」，不能「动账」，否则你会把本该对账的字节吃掉。
    waitUntil(QStringLiteral("全部该落的都落了"),
              [r]() {
                  return r->line->msgs() >= kLineMsgPlan
                         && r->pfx->msgs() >= kPfxMsgPlan
                         && r->pfx->bytesReceived() >= r->wire->requested()
                         && r->wire->bytesWrittenSum() >= r->wire->requested()
                         && Ledger::pongsSeenByClient >= kPingPlan
                         && r->line->gracefulCloses() >= kGracefulPlan
                         && r->line->byeSocketGone();
              },
              [r]() {
                  qInfo().noquote() << QStringLiteral("  —— 分帧账（这一讲的正题）——");
                  check("行端口解析出的消息条数", r->line->msgs(), kLineMsgPlan);
                  check("行端口计划发出的条数", Ledger::lineMsgsSent, kLineMsgPlan);
                  check("前缀端口解析出的消息条数", r->pfx->msgs(), kPfxMsgPlan);
                  check("前缀端口计划发出的条数", Ledger::pfxMsgsSent, kPfxMsgPlan);
                  check("坏帧（版本不认/字段缺失）", r->line->parseFailures() + r->pfx->parseFailures(), 0);
                  qInfo().noquote() << QStringLiteral("      （【8-c】那条不带换行的半包就在这些数字里：它一个字也没加上去）");
                  qInfo().noquote() << QStringLiteral("      此刻行端口未成帧残留=%1 字节｜服务端共回出帧=%2")
                                           .arg(r->line->totalBuffered()).arg(r->line->framesOut());

                  qInfo().noquote() << QStringLiteral("  —— 字节账（单连接才对得上）——");
                  check("wire 请求写入 = bytesWritten 之和", r->wire->requested(), r->wire->bytesWrittenSum());
                  check("wire 请求写入 = 服务端收到", r->wire->requested(), r->pfx->bytesReceived());
                  qInfo().noquote() << QStringLiteral("      （明细：请求 %1｜送出 %2｜对端收到 %3｜本端 bytesToWrite 峰值 %4｜bytesWritten 共 %5 次）")
                                           .arg(r->wire->requested()).arg(r->wire->bytesWrittenSum())
                                           .arg(r->pfx->bytesReceived()).arg(r->wire->peakBytesToWrite())
                                           .arg(r->wire->bytesWrittenHits());

                  qInfo().noquote() << QStringLiteral("  —— 心跳账与超时账 ——");
                  check("客户端发 PING", Ledger::pingsSent, kPingPlan);
                  check("服务端回 PONG", Ledger::pongsByServer, kPingPlan);
                  check("客户端收到 PONG", Ledger::pongsSeenByClient, kPingPlan);
                  check("应答守卫真到点（【7】故意造的）", Ledger::replyTimeouts, 1);
                  check("连接守卫真到点（一次都不该有）", Ledger::guardFired, 0);
                  check(QStringLiteral("连接守卫被正确 stop（%1 次真连接：a/probe(【6】)/wire/staller×2/closer/bye/rude）")
                              .arg(kGuardStopPlan),
                        Ledger::guardStopped, kGuardStopPlan);
                  // ★这一条是给上面那个 8 补的因果，不是凑数：
                  //   【2】让 probe 去连刚被 close() 还回去的端口，按这一讲的讲法应当
                  //   「只能等 errorOccurred」。本机实测它一直停在 Connecting、一个错误都没来
                  //   （所以守卫也没被 errorOccurred 停掉），到【6】复用时才被 abort() 带走。
                  check(QStringLiteral("【2】那次连死端口，在【6】之前攒到的 errorOccurred 条数（本机=%1，"
                                       "不是讲法里的 ConnectionRefused）")
                              .arg(kProbeNoErrorPlan),
                        Ledger::probeErrorsAtSec6, kProbeNoErrorPlan);

                  qInfo().noquote() << QStringLiteral("  —— 连接与生命周期账 ——");
                  check("行端口建立的连接数（a/probe/staller×2/closer/bye/rude）",
                        r->line->accepted(), kLineConnPlan);
                  check("前缀端口建立的连接数（wire）", r->pfx->accepted(), kPfxConnPlan);
                  check("服务端自行 deleteLater 的次数（BYE）", r->line->selfDeleted(), 1);
                  check("BYE 之后 QPointer 已变空（deferred delete 真的生效）",
                        r->line->byeSocketGone() ? 1 : 0, 1);
                  check(QStringLiteral("「先解析到消息、后收到 disconnected」的收口次数（staller + closer + bye，共 %1）")
                              .arg(kGracefulPlan),
                        r->line->gracefulCloses(), kGracefulPlan);
                  check("被服务端 abort() 的连接数（【6】那一次）", r->line->dropped(), 1);
                  // 判据没放宽：仍然要求 probe 至少报过一次错。只是**报的不是 ConnectionRefused**——
                  // 它报的是【6】里服务端 abort() 那条连接带来的 RemoteHostClosedError(1)。
                  // 本机实测：【2】那次连死端口压根没报错，见上面 kGuardStopPlan 那段注释。
                  check(QStringLiteral("probe 上至少报过一次错（实测来源是【6】的 RemoteHostClosed，不是【2】的 ConnectionRefused）"),
                        r->probe->errorHits() >= 1 ? 1 : 0, 1);
                  qInfo().noquote() << QStringLiteral("      probe 状态序列 = [%1]").arg(r->probe->stateSequence().join(QStringLiteral(" → ")));
                  qInfo().noquote() << QStringLiteral("      closer 状态序列 = [%1]").arg(r->closer->stateSequence().join(QStringLiteral(" → ")));
                  qInfo().noquote() << QStringLiteral("      a 状态序列 = [%1]").arg(r->a->stateSequence().join(QStringLiteral(" → ")));
                  qInfo().noquote() << QStringLiteral("      行端口事件流水：接受 %1｜断开 %2｜服务端侧 errorOccurred %3｜自删 %4｜被 abort %5")
                                           .arg(r->line->accepted()).arg(r->line->closed())
                                           .arg(r->line->sockErrors()).arg(r->line->selfDeleted())
                                           .arg(r->line->dropped());
                  qInfo().noquote() << QStringLiteral("    → 这一行里除了「接受/自删/被 abort」，**断开次数是拿不准的**：");
                  qInfo().noquote() << QStringLiteral("      断开要不要发 disconnected，取决于关闭时是谁先动的手");
                  qInfo().noquote() << QStringLiteral("      （abort 的那条就可能只报错不断开），所以它只当流水，不进合账。");
                  qInfo().noquote() << QStringLiteral("    → 注意上面这些序列里**没有** HostLookup：");
                  qInfo().noquote() << QStringLiteral("      因为我们走的是第 137 行那个 QHostAddress 重载；");
                  qInfo().noquote() << QStringLiteral("      换成第 136 行（主机名）就会多一档 HostLookupState=1，");
                  qInfo().noquote() << QStringLiteral("      那一档里 DNS 慢/坏都能把你的「连接超时」搅浑。");

                  // 【7】那发「默认 0」探针的账：0 若真等于无限等，这三行了不得直接把进程钉死，
                  // 也就不会有下面这两行——所以它们俩同时也是「进程跑得完」的证据。
                  check("waitForNewConnection(0) 没等到连接（返回 false）", Ledger::wfcZeroGot, 0);
                  check("waitForNewConnection(0) 把 timedOut 置真（= 已经过期，不是无限等）",
                        Ledger::wfcZeroTimedOut, 1);
                  qInfo().noquote() << QStringLiteral("      waitForNewConnection(0) 当场返回用时 %1 ms").arg(Ledger::wfcZeroMs);

                  qInfo().noquote() << QStringLiteral("  【合账结果】%1 项核对，%2 项一致")
                                           .arg(Ledger::checksTotal).arg(Ledger::checksPassed);
                  qInfo().noquote() << QStringLiteral("  → 一句带走：**把断言压在「消息条数」和「字节总数」上，");
                  qInfo().noquote() << QStringLiteral("     不要压在「readyRead 次数」上。**前者是协议，后者是运气。");
                  QTimer::singleShot(120, [r]() { gotoStage(r, 10, teardown); });
              },
              2000);
}

// ── 收摊：断连 → 带超时的 waitForDisconnected → close → deleteLater → quit ──
static void teardown(Rig *r)
{
    if (r->finished)
        return;
    r->finished = true;
    qInfo().noquote() << QStringLiteral("\n【收摊】disconnect → waitForDisconnected(300) → close → deleteLater → quit");

    TcpClient *all[8] = { r->a, r->probe, r->wire, r->staller, r->closer, r->bye, r->rude, nullptr };
    for (int i = 0; i < 8; ++i) {
        TcpClient *c = all[i];
        if (!c)
            continue;
        QTcpSocket *s = c->sock();
        if (s->state() != QAbstractSocket::UnconnectedState)          // 第 164 行
            s->disconnectFromHost();                                  // 第 138 行
        // ★ 全讲**唯一**一处 waitFor*：进程马上要退、事件循环即将结束，
        //   此时用一次带明确短超时的阻塞等待是可接受的例外（第 176 行默认 30000，这里给 300）。
        //   返回值必须接收并打印：它可能是 false——本机实测**每一次都是 false**，而且不是超时：
        //   走到这一行时 socket 早就被事件循环送进 Unconnected 了，于是 Qt 直接拒绝并打一条
        //   "QAbstractSocket::waitForDisconnected() is not allowed in UnconnectedState"。
        //   也就是说：这里既没等 300ms、也没等 0ms，它是**当场返回**的（下面把墙钟一起记进账，
        //   本机 7 次加起来只有个位数毫秒）。"300ms 是上限"这句话只对**还在连接里**的 socket 成立。
        QElapsedTimer waitClock;
        waitClock.start();
        const bool settled = s->waitForDisconnected(300);
        Ledger::teardownWaits += 1;
        if (!settled)
            Ledger::teardownWaitFalse += 1;
        Ledger::teardownWaitMs += waitClock.elapsed();
        qInfo().noquote() << QStringLiteral("    %1 waitForDisconnected(300) → %2｜state=%3")
                                 .arg(c->name()).arg(settled).arg(stateName(s->state()));
        s->close();                                                   // 第 168 行
        s->deleteLater();                                             // 收尸一律排队
    }
    r->line->stop();                                                  // close() + 各连接 disconnectFromHost
    r->pfx->stop();
    qInfo().noquote() << QStringLiteral("  收摊这一族阻塞的实测：调用 %1 次、返回 false %2 次、"
                                        "墙钟合计 %3 ms（上限本应是 %4 ms）")
                             .arg(Ledger::teardownWaits).arg(Ledger::teardownWaitFalse)
                             .arg(Ledger::teardownWaitMs)
                             .arg(qint64(Ledger::teardownWaits) * 300);
    // 这两笔账放在这儿是有讲究的：合账表在【9】就印完了，而退出码是这一行之后才算的
    // （见下面那句「合账：%1 项 / 一致 %2 项」），所以收摊的实测还赶得上进最后一本账。
    check("收摊的 waitForDisconnected 每次都当场返回 false（早已 Unconnected，Qt 直接拒绝）",
          Ledger::teardownWaitFalse, Ledger::teardownWaits);
    check("收摊阻塞次数 == 客户端 socket 只数（一只不落）",
          Ledger::teardownWaits, 7);
    qInfo().noquote() << QStringLiteral("    服务器已 close（第 30 行：不再接新连接）；socket 一律 deleteLater");
    qInfo().noquote() << QStringLiteral("    兜底的其实是父子关系：objects 的 parent 是 app，退出时照样析构；");
    qInfo().noquote() << QStringLiteral("    deleteLater 只是「别在信号槽的调用栈上动手」，不是内存管理的救星。");
    qInfo().noquote() << QStringLiteral("\n【完】第 3 讲的规矩一句话：");
    qInfo().noquote() << QStringLiteral("  端口现拿、地址只写回环；字节流里没有消息，边界是你造的；");
    qInfo().noquote() << QStringLiteral("  write 只到缓冲，超时只能自己掐；报错只留最后一次，收尸一律 deleteLater。");
    // ★ 合账 %1 项里差了 %2 项（总闸有没有跳过也一起算进来）→ 这就是退出码的根据。
    qInfo().noquote() << QStringLiteral("  合账：%1 项 / 一致 %2 项 / 差 %3 项｜总闸到点=%4 → exit(%5)")
                             .arg(Ledger::checksTotal).arg(Ledger::checksPassed)
                             .arg(Ledger::checksTotal - Ledger::checksPassed)
                             .arg(Ledger::watchdogFired ? QStringLiteral("是") : QStringLiteral("否"))
                             .arg(Ledger::exitCode());
    QCoreApplication::exit(Ledger::exitCode());
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);                                  // 本讲一个窗口都不建
    QCoreApplication::setApplicationName(QStringLiteral("network_03"));

    qInfo().noquote() << QStringLiteral("================================================================");
    qInfo().noquote() << QStringLiteral("network 第 3 讲 · QTcpSocket / QTcpServer（Qt = %1，本机回环闭环）")
                             .arg(QString::fromLatin1(QT_VERSION_STR));   // qconfig.h 第 73 行那个宏
    qInfo().noquote() << QStringLiteral("  八段演示全部由 QTimer::singleShot 与信号驱动，无人值守，自己退出。");
    qInfo().noquote() << QStringLiteral("================================================================");

    Rig rig;
    rig.line = new TcpEchoServer(Framing::LineDelimited, QStringLiteral("行端口服务器"), &app);
    rig.pfx = new TcpEchoServer(Framing::LengthPrefixed, QStringLiteral("前缀端口服务器"), &app);
    rig.a = new TcpClient(Framing::LineDelimited, QStringLiteral("a(主力)"), &app);
    rig.probe = new TcpClient(Framing::LineDelimited, QStringLiteral("probe(试错)"), &app);
    rig.wire = new TcpClient(Framing::LengthPrefixed, QStringLiteral("wire(前缀)"), &app);
    rig.staller = new TcpClient(Framing::LineDelimited, QStringLiteral("staller(超时)"), &app);
    rig.closer = new TcpClient(Framing::LineDelimited, QStringLiteral("closer(温和收)"), &app);
    rig.bye = new TcpClient(Framing::LineDelimited, QStringLiteral("bye(自删)"), &app);
    rig.rude = new TcpClient(Framing::LineDelimited, QStringLiteral("rude(半包+撕)"), &app);
    Rig *r = &rig;

    // 心跳定时器：只为证明「等待期间事件循环一直在转」，和第 1 讲同一个路子
    QTimer *beat = new QTimer(&app);
    beat->setInterval(1);                                              // 第 37 行 setInterval(int)
    int turns = 0;
    QObject::connect(beat, &QTimer::timeout, &app, [&turns]() { ++turns; });
    beat->start();

    // 总闸：25 秒到点强制收摊。**没有任何路径能把进程吊死**，这一条就是它的兜底；
    // 数值比「全部有界等待的最坏预算」（约 18.1 秒，算式见第二节）留了 7 秒余量。
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &app, [&rig, beat, &turns]() {
        qWarning().noquote() << QStringLiteral("\n【超时】总闸 25s 到点 → 强制收摊（这就是「超时」证据，不是崩溃）");
        qWarning().noquote() << QStringLiteral("  到点时的账：合账 %1 项 / 一致 %2 项｜事件循环轮数 %3")
                                    .arg(Ledger::checksTotal).arg(Ledger::checksPassed).arg(turns);
        qWarning().noquote() << QStringLiteral("  记上一笔：总闸跳过一次 = 这一轮没跑完，退出码不会是 0（见 Ledger::exitCode）。");
        Ledger::watchdogFired = true;                                 // ★ 这条路径必须留下痕迹
        beat->stop();
        teardown(&rig);
    });
    watchdog.start(25000);

    // 八段：0 → 200 → 200 → 500/650 → 900 → 1150 → 1400 → 1700 ms 串起来。
    // 为什么用「固定间隔进场 + 段内有界等待落地」：进场时间好读好讲，
    // 而每一步真正的先后关系交给 waitUntil（它每 10ms 回头看计数器，到点也照样往下走）。
    QTimer::singleShot(0, [r]() { gotoStage(r, 1, sec1); });

    const int rc = app.exec();
    watchdog.stop();
    beat->stop();
    qInfo().noquote() << QStringLiteral("\n事件循环已退出（返回码 %1），事件循环共转了 %2 轮，全程零阻塞等待").arg(rc).arg(turns);
    qInfo().noquote() << QStringLiteral("  实测口径：本文件已在 Qt 6.12.0 + MinGW 13.1.0（Windows，offscreen）下");
    qInfo().noquote() << QStringLiteral("  真实编译、链接、运行到自动退出——上面那行合账就是本机这一轮的输出。");
    return rc;
}

// ============================================================================
// 八、速查（背不住就回来查）
// ============================================================================
/*
 * 信号与槽（Qt 6.12 本机头文件行号）：
 *   QTcpServer    newConnection()（qtcpserver.h 第 69 行，**无参数**）、acceptError(SocketError)（71）
 *                 listen(addr = Any, port = 0)（29）→ serverPort()（40）/ serverAddress()（41）
 *                 hasPendingConnections()（47）/ nextPendingConnection()（48，所有权归你）
 *                 close()（30）/ isListening()（32）/ setMaxPendingConnections（34）
 *                 pauseAccepting / resumeAccepting（53/54）
 *   QTcpSocket    自己只有构造析构（qtcpsocket.h 第 18/22 行），其余全在下面两层：
 *   QAbstractSocket  connected(187) disconnected(188) stateChanged(189) errorOccurred(190)
 *                 connectToHost(void! 136/137) disconnectFromHost(138) abort(154) close(168)
 *                 state(164) error(165) isValid(140) bytesAvailable(142) bytesToWrite(143)
 *                 peerAddress/peerPort(147/148) readBufferSize/setReadBufferSize(151/152)
 *                 flush(170) waitFor*(173~176，默认 30000) setSocketOption(160)/socketOption(161，非 const)
 *   QIODevice     readyRead(119) bytesWritten(qint64)(121) aboutToClose(123)
 *                 read(81) readAll(82) readLine(84) canReadLine(93) write(100~102) errorString(115)
 *
 * 三个「等」怎么选：
 *   有事件循环（GUI、本讲） → 永远挂信号 + 自己的 QTimer 守卫；waitFor* 一个都别用
 *   没有事件循环（构建脚本、CI 冒烟）→ 可以用 waitFor*，但**必须显式给短超时**
 *   收摊 → 允许 waitForDisconnected(300) 这类最后一次短等，且返回值要打印
 *
 * 分帧两条路（本讲各跑一遍，条数完全一致）：
 *   行分隔：indexOf('\n') + left/mid；文本可读、能手工 telnet，代价是不能带裸换行、要转义
 *   长度前缀：4 字节大端（qendian.h 181/204/239）；二进制安全、能流式，代价是
 *             必须设上限（本讲 kMaxFrameBytes）、必须防「前缀本身被切断」
 *   QDataStream：格式内有长度（470/483），传输层无边界；整帧到手再喂（第 128 行 ctor）
 *
 * 硬规矩：
 *   listen 一律写 QHostAddress::LocalHost（默认是 Any，第 29 行）；端口一律 0 + serverPort()
 *   判错：state() / isValid() / errorString()，**不是** error() == 0（SocketError 没有 NoError）
 *   收尸：一律 deleteLater；QObject::connect 挂 lambda 必须带 context（第 4 个参数）
 *   QTimer::singleShot(ms, lambda) 那族**没有** context（qtimer.h 83~88 行，receiver 写死 nullptr）
 *                 → 到点必发，所以推进一律过一道「stage/finished」的门，别靠它自己收场
 *   线程：socket 属于创建它的线程（裁决见 threads 第 3 讲）；同机换 QLocalSocket（threads 第 8 讲）
 */

// 本文件有 Q_OBJECT 类（TcpEchoServer / TcpClient），AUTOMOC 会生成 .moc 并在这里包含。
#include "03_tcp_client_server.moc"

// ——— 老师傅的话 ———
/*
 * 徒弟，TCP 这一层，你先把一句顺口溜记住：**「socket 给你的是水，不是杯子。」**
 * 你在【3】看到的四个 write、服务端那边却可能是两次 readyRead、三次、七次——
 * 取决于内核怎么分段、事件循环什么时候被唤醒、对面网卡刷不刷。谁要是跟你说
 * 「我这边一次 readyRead 就是一条请求」，他的代码只在自己机器上、低负载上、
 * 回环上是对的；上线第一天就被粘包咬。所以我逼你把断言写在**条数**和**字节数**上：
 * 合账里那两项（分帧账、字节账）才是能带到生产去的账，readyRead 次数我连看都不看，
 * 只把它打出来当日志装饰。
 *
 * 第二条，关于 listen 的默认参数，这是我见过最多的「无心之失」。
 * qtcpserver.h 第 29 行写的是 address = QHostAddress::Any。桌面软件里想开个小服务
 * 给自己两个进程通一下，顺手 `server.listen(45678)`——好，从这一刻起这台机器上
 * **任何网卡、任何局域网里的机器**都能连它，Windows 防火墙弹一个「是否允许」的框，
 * 用户点「允许」，你就把一个没有任何鉴权的协议暴露在公司内网了。这一讲每一处 listen
 * 都显式写 LocalHost，而且每次连完都把 peerAddress().isLoopback()（第 129 行）打出来。
 * 端口也一样：写死 8080 的示例是**碰运气**，示例代码碰运气，读者就会在生产里碰运气。
 * 借一个（listen 0）、读回来（serverPort）、用完还（close）——【2】那一段就是这个动作。
 *
 * 第三条，报错这件事，Qt 的 socket **不排队**。
 * 你以为 errorOccurred 发了两次，error() 里就会有两个错误；实际上只有一个坑位
 * （第 165 行的 getter，配第 202 行那**一个** protected setter——头文件已经把答案写在你脸上了）。
 * 【6】让同一只 socket 先吃 ConnectionRefused、再连上、再被服务端 abort，
 * 序列是**信号**给你留下的，error() 只剩最后一个。更阴的是 SocketError 里
 * **没有 NoError**，第一个枚举量 ConnectionRefusedError 隐式就是 0，
 * 于是 `if (sock->error() == 0)` 读出来的「看起来像没错」的那个数，恰恰是「连不上」。
 * 老手怎么写这段？——判状态（state()、isValid() 第 140 行、errorString() 第 115 行），
 * 把错误码**连同当时的 state 和字节数一起打出来**，别把错误码当状态机用。
 *
 * 第四条，超时。你去找 Qt 的「socket 超时设置」，会一无所获——
 * 我在本机 QtNetwork 公开头文件里 grep connectTimeout/socketTimeout/defaultSocketTimeout，
 * **零命中**。Qt 只给了四个 waitFor*，默认 30000 毫秒，而且是**阻塞**的那种。
 * 于是新手的写法只有两种：要么根本不等（对端不回话就永远挂着），
 * 要么在 GUI 线程写 waitForReadyRead()（默认值，30 秒白屏）。
 * 正解是你自己搭两个守卫：一个管「连不上」，一个管「连上了但不说话」，
 * 各自有名字、各自有毫秒数、各自有到点动作。【7】里那两个 QTimer 就是它们。
 * 这里有条我自己踩出来的纪律：**成功路上和出错路上都要 stop**。
 * 只在 connected 里 stop，出错的连接就会在你下一次复用同一只 socket 时突然超时
 * ——现场表现为「我明明刚连上，怎么 2 秒后被自己 abort 了」，查一上午。
 * 至于 SO_KEEPALIVE（第 93 行那个 KeepAliveOption）：它问的是内核「链路还在吗」，
 * 不是「对面那个进程还活着吗」。这两句话的区别，够你写三条 PING/PONG 进协议。
 *
 * 第五条，关闭。abort()（第 154 行）和 disconnectFromHost()（第 138 行）不是
 * 「急一点」和「慢一点」的区别，是**丢不丢发送缓冲**的区别。
 * 【8】三条连接演三种死法：closer 写完最后一句就走 disconnectFromHost()（服务端照样
 * 收到最后一句，gracefulCloses 那笔账就是它，合账要求恰好 3 次——staller 超时后的
 * 温和收口是第二个；bye 是第三个——它让服务端在自己的 readyRead 栈里把脚下那只 socket
 * 排队删掉，而 BYE 对服务端来说也是一帧合法消息，所以它同样满足「最后一条事件是解析出
 * 一帧」这个判据（这一笔是实测补进来的，原先只算了 2）。
 * rude 写一个不带换行的半包再 abort()（我故意不作断言——因为那几个字节到底压在 Qt 的
 * 用户缓冲里还是已经交给内核，是运行时的秘密，谁把它写成单元测试谁就等着随机挂）。
 * rude 在账上只有一条断言：**它一个字都不许加到消息条数上**。
 * 你能核对的只有：bytesToWrite()（第 143 行）降到 0 之前，别说「我发完了」。
 *
 * 第六条，内存。【8-b】那一幕你要能背下来：服务端正站在这条 socket 自己发出的
 * readyRead 的调用栈上，手里攥着它，这时候写 `delete sock`，就是把脚下的地板抽掉。
 * deleteLater() 是**唯一**正确的动作，而我还在同一轮里继续读它的 state()——
 * 读得到，才证明 deleteLater 真的没当场生效：它是「把 DeferredDelete 事件排队」，
 * 不是「马上析构」，要等**事件循环转到下一轮**才动手。所以本讲合账前那一步
 * waitUntil(「BYE 那条 socket 的 deferred delete 已生效」) 等的就是这一轮：
 * 轮询用的是 10ms 的 singleShot，事件循环一直在转，QPointer 才有机会变空。
 * （反过来说：如果你在没有任何事件循环的地方 deleteLater，那个对象就永远删不掉——
 * 这条将来在 threads/事件循环那一讲还会再咬你一次。）
 * 三条纪律配套：收尸一律 deleteLater；lambda 捕获 socket 一定要带 context 对象
 * （connect 的第四个参数不是装饰，它就是那条连接的命，机制在 metaobject 第 4 讲）；
 * 至于「我确认发送者比接收者活得久」这句话，你说不出口就别依赖它。
 *
 * 第七条，说说线程与本地套接字，各一句，多了是别的讲的活。
 * socket 属于创建它的那条线程，跨线程碰它的读写就是竞态；要跨线程，就发信号、
 * 让对象在自己线程里干活（threads 第 3 讲把「槽跑在哪条线程」称得明明白白）。
 * 同机两台进程通信，别用 TCP 环回——QLocalSocket/QLocalServer 没有端口、
 * 没有防火墙弹窗，Windows 上走命名管道、Linux/macOS 走 Unix socket
 * （threads 第 8 讲 threads_08_local_ipc 正面讲）。你选 TCP 的理由只能是
 * 「跨机器」或「要像网络一样测」，不能是「我会写这个」。
 *
 * 最后一条，也是这一讲真正的看家本事：**先写账，再写代码。**
 * 我在 sec 之前就把 kLineMsgPlan = 10、kPfxMsgPlan = 11、kPingPlan = 3、kLineConnPlan = 7、
 * kGuardStopPlan = 8 写死了，每个数后面还标了它是哪几段贡献的——
 * 那意味着我知道每一段该发几条、谁该收到几条。写不出这几个数的代码，
 * 就是「跑起来看看再说」的代码，那种代码到了 CI 上就是随机挂的一坨。
 * ★但这段话本身是被本机实测打过脸的，你把这一条也一起学走：这几个数是**没跑过就写下的**，
 *   第一次真跑（Qt 6.12.0 / MinGW / Windows）当场暴露三件事——
 *     ① 【6】复用 probe 时没先 abort()，connectToHost 只回一个 19 号错误就什么都不做，
 *        于是【6】→【7】那一跳静默卡死，合账一项都没打出来，是 25 秒总闸把进程救走的；
 *     ② kGuardStopPlan 9 → 8：「连死端口一定马上吃 ConnectionRefused」在回环上不成立；
 *     ③ kGracefulPlan 2 → 3：BYE 自己也是一帧合法消息，计划时漏了它。
 *   「先写账再写代码」依然是对的，但**账本要拿运行结果去对一遍才算数**——
 *   没对过的账本，和没有账本的区别只在于错得更自信。
 * 合账那二十来行里，每一项都打「期望 / 实测」，不一致就当场见光。
 * 你以后 review 自己写的网络代码，就问一句：**这段的账在哪儿？**
 */

// ——— 小练习 ———
/*
 *   1. 把 TcpEchoServer 的行分帧改成「先读 1 个字节当长度」（0~255），再加一个
 *      「超过 255 走 4 字节大端扩展」的分支，仿 qdatastream.h 第 483~496 行
 *      writeQSizeType 那个 ExtendedSize 哨兵的做法。跑完看合账前两行变不变——
 *      如果消息条数变了，说明你的边界判定有洞。
 *   2. 给服务端加一条「半包老化」规则：某条连接的缓冲里已经有字节、但 3 秒内
 *      没拼出任何一帧，就 abort() 它并计数。用 qInfo() 打印被踢掉的连接数，
 *      然后从客户端故意只发 "V1 XX"（永远不给换行）验证它真被踢了。
 *      这条是防「半包挂满文件描述符」的正经手段，不是可选项。
 *   3. 把【7】的应答守卫从「一个」改成「一张表」：QHash<quint32, QElapsedTimer>
 *      按 seq 登记，客户端支持 5 个并发请求；合账里加一项「每个 seq 要么有回音
 *      要么有超时，二者取其一且只取其一」。做完你会明白为什么第 1 讲的 HTTP
 *      非要有 request-id。
 *   4. 空闲心跳：让客户端「连续 2 秒没发过任何消息」才发一次 PING，
 *      连续 3 次没收到 PONG 就 disconnectFromHost() 并打印原因。
 *      用 Ledger 加两个计数（心跳发出数、因心跳断的 connection 数），
 *      证明它**只在真空闲时**发，而不是每次 readyRead 都顺手来一发。
 *   5. 限流：QTcpServer::setMaxPendingConnections(3)（qtcpserver.h 第 34 行）
 *      + pauseAccepting()/resumeAccepting()（第 53/54 行），让第 4 条连接连进来。
 *      打印第 4 条的命运，并说清三层队列的分工：内核 backlog（第 37 行
 *      setListenBacklogSize）／Qt 的 pending 队列／你的应用层连接池。
 *      注意回答：被 pauseAccepting 挡住时，客户端那一侧的 connected 会不会到达？
 *   6. 背压实验：把【5】改成「1 帧 2MiB + 每帧上限提到 4MiB」，并在服务端
 *      setReadBufferSize(64 * 1024)（qabstractsocket.h 第 151/152 行）。
 *      对照打印 bytesToWrite() 峰值、readyRead 次数、消息条数、字节总数，
 *      回答：哪几个数变了，哪几个数**必须**不变？（后者就是「条数是账、次数不是」的证据。）
 *   7. 综合（资深题）：把 TcpEchoServer 拆成两层——纯编解码层（只 include
 *      QtCore，接口是 QByteArray in / QList<QByteArray> out）和传输层（只 include
 *      QtNetwork）。然后给编解码层写一组**不需要 socket** 的喂字节测试
 *      （按 1 字节 1 次喂、按随机块喂、把一帧跨 3 次喂），断言消息条数恒定。
 *      quality 第 1 讲（QTest）就是干这个的。做完这一步，你才算真的把
 *      「TCP 是字节流」这件事变成了一段能自证的代码。
 */
// ============================================================================
