# 第玖阶段 · 网络（Qt 从入门到资深）

> 老师傅带你处理「没有你的地方正在发生的事」：一次请求的一生、一条字节流上的协议、一个扛得住断线的任务

## 一、模块简介

徒弟，网络和前面几块的根本区别是：**你发出去的东西，结果不在你的栈上**。`get()` 立刻返回，回程的车什么时候到、到的时候你是不是还活着，都得你自己安排。

新手在这一层栽的跟头，姿势几乎一模一样：

- 在按钮槽里 `reply->waitForFinished()` —— 事件循环停了，界面「未响应」；
- 在 `finished` 之前读 `errorString()` / 状态码 —— 读到的是「还没发生」；
- 把 `error() == NoError` 当成成功 —— 500 也是 NoError；
- 把 TCP 的「一次 `readyRead`」当成「一条消息」 —— 半包与粘包让你三分之一的消息对不上；
- 每个请求 new 一个 `QNetworkAccessManager` —— 连接池、Cookie、缓存全废。

这 5 讲就是把上面每一条，变成你亲手能打出来的证据。

> 环境事实：Qt 6.12.0、Windows、MinGW-w64 GCC 13.1.0（Qt 官方安装器自带：D:\Qt\Tools\mingw1310_64）、CMake ≥ 3.21（本机 3.30.5）+ 生成器 MinGW Makefiles、C++17。目标名 `network_0N_xxx`，已在顶层 `CMakeLists.txt` 登记，本组额外链 `Qt6::Network`。
>
> 实测口径：**5 个示例都在本机真实编译、链接、无头运行过**（Qt 6.12.0 + MinGW 13.1.0，`QT_QPA_PLATFORM=offscreen`；`ctest -R "^network_"` → 5/5 绿，退出码全 0，完整输出存档 `runlogs/network_0N_*.log`；原始字节是 GBK，`.log` 是转过 UTF-8 的那一份）。这一组的台账比讲义硬：本机那一轮五张账分别是 **67/67**（第 1 讲【硬账】）、**29/29**（第 2 讲合账）、**25 项一致 25、差 0**（第 3 讲合账）、**44/44**（第 4 讲【硬账】）、**36 项对上 36、不符 0、整节作废 0**（第 5 讲硬账）；账不符的示例会把退出码变成非 0，所以「打印出来的结论」是被 ctest 盯着的，不是背出来的。每个类、函数、枚举、重载都对着本机 `D:\Qt\6.12.0\mingw_64\include` 里真实头文件核对过（注释里给了文件名与行号），核对不上的 API 一律不用。
>
> **「对方行为不可预知」这条不因能编译而作废**：本机这 5 轮一次也没出网，全部只连回环（自建 `QTcpServer` + `LocalHost`，端口现拿），所以「公网服务器会怎么答、证书链谁签的、DNS 给几个地址」仍然是条件句，不是实测结论。第 5 讲那节标题就写着「离线、全同步的五个事实」——它是**故意设计成不依赖网络也能立住**的；联网时才成立的部分（外网 DNS、真实站点证书）留在「可选实验」里，请你自己跑一次把数字补进去。
>
> 顺带一条本轮改掉的真实缺陷：第 1、2 讲原来在 `finished` 里**无条件** `reply->readAll()`，超时/abort 收尾的那两条 reply 设备已经关了，于是控制台各冒一条 `QIODevice::read (QNetworkReplyHttpImpl): device not open`。结果碰巧对（读回来是空的），过程在喊疼。第 2 讲现在读前先 `isOpen()`；第 1 讲**故意保留**这一次读取并先把 `isOpen()`/`bytesAvailable()` 打出来，因为「finished 一定到、正文不一定有」这句话需要那条警告当证人——它是**预期内**的，不是示例坏了。
>
> **联网规矩**：涉及真实网络的示例只连本地回环（`QHostAddress::LocalHost`），端口 `listen(LocalHost, 0)` 现拿，临时文件放在 `QStandardPaths::TempLocation` 且用完就删，所有等待都带期限。DNS/TLS 那讲写成了「离线也成立的能力探测 + 联网时的可选实验」。

## 二、学习目标

学完本模块，你要能做到：

1. 说清 `QNetworkAccessManager` 为什么应该一个应用一个实例，以及它必须活在**有事件循环的线程**上的原因
2. 用 `QUrl` 正确地构造带 query 的地址（`QUrlQuery`）、本地文件地址（`fromLocalFile`），说出「手写字符串当 URL」的三种坏法
3. 区分 `finished()` 与 `readyRead()` 的职责，知道哪些结论必须等 `finished`、哪些正文可以边收边用
4. 把网络的失败分成三类（HTTP 语义错 / 网络错 / 解析错），并为每一类给出不同的用户提示与重试策略
5. 写出一个只连回环的 `QTcpServer` + `QTcpSocket` 对话，用长度前缀保证「发 5 条收 5 条」，并能解释半包与粘包
6. 实现一个下载任务：进度三分支（`>0` / `-1` / `0`）、Range 断点续传与 206 校验、边下边写、`abort()` 取消与 `deleteLater()` 收尸
7. 说清 `setTransferTimeout()` 是「静默超时」而不是「总时长上限」，并会给一个不给你期限的 API（`QHostInfo`、`QSslSocket` 握手）自己加期限
8. 用 `supportsSsl()` 判断当前 Qt 构建能否上 TLS，并知道「证书错误不该被忽略」的原因

## 三、文件列表与学习重点

```text
  01_network_access_manager.cpp   ← NAM 的规矩（第一道坎）
    重点：一个应用一个 NAM（内部带连接池、代理、Cookie、缓存、HSTS 状态），
          必须创建并活在有事件循环的线程上；它不自己起线程，一切靠事件循环驱动；
          QUrl 三个坑：手写字符串不是 URL（"C:/data/a.json" 的 scheme 会变成 "c"）、
          本地文件 QUrl::fromLocalFile、query 用 QUrlQuery；
          六个动词 head/get/post/put/deleteResource/sendCustomRequest
          （qnetworkaccessmanager.h 里没有名为 delete 的成员，delete 是关键字）；
          QNetworkReply 是 QObject 也是 QIODevice → 必须 deleteLater；
          finished() 与 readyRead() 的分工；error()/errorString()/attribute()
          只在 finished 之后完整（证据②）；
          重定向：默认 NoLessSafeRedirectPolicy（qnetworkrequest.h 第 74/105 行），
          Qt6 没有 Qt5 的 RedirectionType；setTransferTimeout（第 205/208 行）是静默超时
    诚实标注：本讲只连本机回环服务；「什么时候能收到第一段正文」依赖网络与时序，不做承诺

  02_rest_client.cpp              ← 把网络层包成可测试的一层
    重点：业务代码看不到 QNetworkReply；RestClient::get/postJson(path, ..., callback)；
          ApiResult 把三类失败分开：成功 / HTTP 语义错（服务器活着且答了，4xx 5xx）/
          网络错（连不上、超时、被取消，压根没答）/ 解析错（200 但正文不是合法 JSON）；
          三类的用户提示、重试策略、日志级别都不同，混成 bool 就只能一律「网络异常」；
          KnownHeaders 十三项里没有 Authorization（也没有 Accept），Bearer 只能
          setRawHeader("Authorization", ...)；Qt 6.6+ 另有 QHttpHeaders（WellKnownHeader 是 enum class）
    诚实标注：本讲用本机回环上的一个小服务模拟各类失败；真实服务器的错误页格式千奇百怪

  03_tcp_client_server.cpp        ← 字节流上造协议（第二道坎）
    重点：QTcpServer/QTcpSocket 全事件驱动（newConnection/connected/readyRead/
          disconnected/errorOccurred/stateChanged 都是信号）；
          listen(QHostAddress::LocalHost, 0) + serverPort() 现拿端口；
          ★ listen 的默认地址是 QHostAddress::Any（qtcpserver.h 第 29 行），
          不写地址就是监听所有网卡；connectToHost 返回 void（qabstractsocket.h
          第 136/137 行，别和上面的 qtcpserver.h 混成同一个文件），
          连不上只能等 errorOccurred——
          ★ 本机实测给这句补一刀：Windows 回环上连一个**刚 close() 的死端口**，
            可能几秒内既不 connected 也不 errorOccurred。
            「只能等」是对的，「一定等得到」是错的：每一个等待都要自己挂兜底定时器，
            不然整条状态机停在第 N 步不动，还不报错（本讲第一版就这么卡的）。
          ★ 复用同一个 QTcpSocket 前必须先 abort()（qabstractsocket.h 第 154 行）：
            它还在 ConnectingState 时再调 connectToHost，只会同步收到一个
            errorOccurred(SocketResourceError=19)，然后**什么都不做**——
            不是重试，不是排队，是静默失效；这一条也是上面那次卡死的真凶。
          一次 readyRead ≠ 一条消息：一帧切 3 次 write（半包）、2 帧塞 1 次 write（粘包），
          并排打印「服务端 readyRead 次数」；
          长度前缀的解帧循环（缓冲区 + 已解析偏移）；
          坏帧防御：长度字段是对端说的，声称超大的帧必须拒并断开
    诚实标注：本讲只在 127.0.0.1 上跑，不连公网；出错顺序依赖本机协议栈，以打印为准；
              21 项硬账本机连跑全绿，其中「死端口到底给不给 errorOccurred」那条
              按实测改成「给几次就记几次」，不假设协议栈的行为

  04_download_and_progress.cpp    ← 下载任务的状态机
    重点：downloadProgress(bytesReceived, bytesTotal) 三分支：
          >0 正常算百分比；== -1 长度未知（服务器没发 Content-Length，用关闭连接划线），
          别拿它做除法；== 0 是空文件，当 100% 完成而不是除零事故；
          ★ 上面那句要加两条本机（Qt 6.12.0 / MinGW / Windows）实测出来的前提：
          ① 进度信号**被节流到最快每 100ms 一次**（qnetworkreply.cpp:15
             `progressSignalInterval = 100`，发送点 qnetworkreplyhttpimpl.cpp:1252~1257）。
             回环上 1 MiB 一坨到手、100ms 内就结束 → **中途那次 -1 根本不存在**，
             demo 的服务器必须按 10ms/64KiB 限速滴发，`-1` 分支才真的走得到；
          ② **收尾那一帧 Qt 会改口**：qnetworkreplyhttpimpl.cpp:2240~2244，
             totalSize==-1 时发 `downloadProgress(received, received)`，
             最后一帧的 total 等于已收，不是 -1。UI 不能死等 -1，也不能指望它保持 -1；
          熟头 header(ContentTypeHeader/ContentLengthHeader/LastModifiedHeader) 与
          生头 rawHeader/rawHeaderList/rawHeaderPairs 的分工——
          Content-Range / Accept-Ranges 只在生头里；
          ★ rawHeaderList() 吐出来的名字是**全小写**（`content-range`），
            hasRawHeader()/rawHeader() 却大小写不敏感——按驼峰 contains 一定 false；
          断点续传 = Range 头 + 206 + Content-Range 回显，两段拼起来与整段逐字节比对；
          边下边写：readyRead → reply->read(chunk) → QSaveFile::write，绝不 readAll 攒内存；
          ★ 但实测给这句加了限定：**边下边写 ≠ 一定省内存**。/file 的 1 MiB 在回环上
            一次进齐，第一次 readyRead 时 bytesAvailable() 峰值 = 1048576 = 正文全长；
            同一套 while 排空写法换成限速的 8 MiB 流，峰值只有 65536。
            压住驻留靠「数据来得慢」或 setReadBufferSize，不靠 read() 这个动作本身。
          ★ 一次 readyRead 只 read(64KiB) 会**把下载截断**（1 MiB 只落 65536 字节）：
            Qt 在数据到手结束时只发一次 readyRead 就转 finished，
            必须 while 读到空、finished 之后再兜底排空一次；
          取消用 abort()（abort 后仍会 finished，注意区分「被取消」与「失败」）；
          ★ 错误码是 **OperationCanceledError = 5**，不是常被引用的 41
            （qnetworkreply.h:33~48 数出来的：1~99 网络层 / 101~ 代理 / 201~ 内容 /
             301~ 协议 / 401~ 服务器，41 这个号不存在）；
          ★ **abort() 是同步的**：errorOccurred → finished → 你的收尾槽全部在它返回
            之前跑完。所以状态要「先记账再 abort」，用返回值往外传必然读到旧值；
            reply 已经不 running 时 abort() 是**空操作**，取消按钮点了个寂寞还不报错
    诚实标注：本讲对着本机回环上的静态服务端做，可控制慢速与断流；
              真实 CDN 的分段与压缩行为不在承诺范围；
              44 项硬账本机 4 连跑全绿，限速相关读数会随机器抖动，判定都留了余量

  05_dns_and_ssl.cpp              ← 名字与加密
    重点：QHostInfo 同步（fromName，堵线程）与异步（lookupHost + lookupId 对结果）两套；
          ★ 两者都没有超时参数（qhostinfo.h 第 56~92 行），期限要自己加；
          「查不到」是常态不是异常：用保留域名 .invalid 演示，
          并证明「我们的期限一定生效」（因为期限是我们自己写的）；
          QHostAddress 五个事实：isLoopback / isNull / IPv4 与 IPv6 / LocalHost 与 Any；
          TLS 探测：supportsSsl()、后端名、可用密码套件、系统根证书数量；
          QSslSocket 的握手也不给你期限 → QTimer 看门狗
    诚实标注：DNS 结果与 TLS 能力**依赖本机安装**；本讲把「查不到」和「不支持 TLS」
              都当成正常分支来演示，断网也能跑完
```

## 四、一张时序图（一次请求的一生）

```text
你的代码                NAM                    reply                事件循环
  │  get(req)            │                       │                     │
  ├─────────────────────>│  内部排队、建连接       │                     │
  │  立刻返回            ├──────────────────────>│  (新对象)            │
  │                      │                       │                     │
  │  connect(reply, finished, ...)               │                     │
  │                      │          ◄────────────┤ readyRead  （正文分段到）│
  │                      │          ◄────────────┤ downloadProgress      │
  │                      │          ◄────────────┤ finished  （结论齐了） │
  │  <────────────────────────────────────────────┘                     │
  │  读 error() / attribute(状态码) / readAll()，然后 deleteLater()       │
```

三条要点：

1. **结果只在 `finished` 之后完整**；
2. **正文可以随时读**（`readyRead` 来了就读走，别攒）；
3. **reply 必须还**（`deleteLater()`，或由 parent 收）。

## 五、常见误区 → 正解

| 误区 | 正解 |
|---|---|
| 「每个请求 new 一个 NAM」 | 一个应用一个。连接池、Cookie、缓存都在它身上 |
| 「在 GUI 线程 `waitForFinished()` 也行」 | 事件循环停了。要么异步、要么放到专门线程 |
| 「`error() == NoError` 就是成功」 | 还要看 `HttpStatusCodeAttribute`。500 也是 NoError |
| 「`finished` 之前也能读状态码」 | 读到的是「还没发生」。结论都等 `finished` |
| 「`readyRead` 来一次就是一条消息」 | 那是 TCP。HTTP 有边界，TCP 没有 |
| 「`setTransferTimeout` 是总时长」 | 它是**静默超时**：只要还有数据在动，就不会触发 |
| 「进度总量 -1 就当 0 处理」 | -1 是「长度未知」。做除法会得到荒谬的百分比 |
| 「取消就 `deleteLater()` reply」 | 先 `abort()`，再等 `finished`，最后 `deleteLater()` |
| 「TLS 一定可用」 | 先 `supportsSsl()`。有些 Qt 构建没编 OpenSSL 后端 |
| 「证书错误加个忽略就行」 | 那正是中间人的入口。要修的是证书链，不是校验 |

## 六、练习

1. **半包/粘包**：把第 3 讲的长度前缀去掉，改成「按行分帧」，然后用一条正文里带换行的消息去喂它，观察解析错位。
2. **坏帧**：把帧头长度改成 `0xFFFFFFFF`，确认服务端拒绝并断开，且它前面那条好帧没有丢。
3. **进度三分支**：让回环服务端分别用「带 Content-Length」「不带」「空文件」三种方式响应，把三个分支都打出来。
4. **断点续传**：下到一半 `abort()`，记下已写字节数，用 Range 从该处续，拼起来与整段逐字节比对。
5. **超时**：把 `setTransferTimeout` 调到一个很小的值，让服务端每隔一会儿发一个字节，观察超时**不会**触发——这就是「静默超时」。
6. **TLS 探测**：在你的机器上打印 `supportsSsl()` 和后端名。如果没有，试着装 OpenSSL 再跑一次，对照差异。

## 七、验收标准

- [ ] 你的网络代码里找不到 `waitForFinished()`（除非在专门的后台线程）
- [ ] 每个 reply 都有明确的收尸路径
- [ ] 失败被分成三类，用户看到的提示能区分「服务器拒绝」与「网络不通」
- [ ] 你能画出自己协议的帧格式，并说出坏帧怎么防
- [ ] 下载任务的内存占用不随文件大小增长
- [ ] 所有网络等待都有期限，且能说出「静默超时」与「总时长」的区别

## 八、下一站

网络让程序会说话。第拾阶段「工程质量」要让它**经得起别人改、经得起机器验**：QtTest 的单元与界面测试、分层架构、所有权与生命周期、错误处理、性能测量、百万行视图优化。你会发现前面九阶段学的东西，在那一步才真正开始互相咬合。

想看动画复盘，随时回来翻 [图解 · 异步网络](../visuals/network_async.html) 与 [图解 · 事件循环](../visuals/event_loop.html)。加油，徒弟！
