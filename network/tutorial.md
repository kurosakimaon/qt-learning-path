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

> 环境事实：Qt 6.12.0、Windows、MinGW-w64（MSYS2 ucrt64，gcc 15.2/16.2）、CMake ≥ 3.21 + Ninja、C++17。目标名 `network_0N_xxx`，已在顶层 `CMakeLists.txt` 登记，本组额外链 `Qt6::Network`。
>
> 诚实标注：**本机当前无法编译链接 Qt 程序**，5 个示例**都未在本机实测运行**。每个类、函数、枚举、重载都对着本机 `C:\msys64\ucrt64\include\qt6` 里真实头文件核对过（注释里给了文件名与行号），核对不上的 API 一律不用。真实网络的对方行为不可预知，文中凡是「服务器会怎么答」都写成条件句。
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
          不写地址就是监听所有网卡；connectToHost 返回 void（第 136/137 行），
          连不上只能等 errorOccurred；
          一次 readyRead ≠ 一条消息：一帧切 3 次 write（半包）、2 帧塞 1 次 write（粘包），
          并排打印「服务端 readyRead 次数」；
          长度前缀的解帧循环（缓冲区 + 已解析偏移）；
          坏帧防御：长度字段是对端说的，声称超大的帧必须拒并断开
    诚实标注：本讲只在 127.0.0.1 上跑，不连公网；出错顺序依赖本机协议栈，以打印为准

  04_download_and_progress.cpp    ← 下载任务的状态机
    重点：downloadProgress(bytesReceived, bytesTotal) 三分支：
          >0 正常算百分比；== -1 长度未知（服务器没发 Content-Length，用关闭连接划线），
          别拿它做除法；== 0 是空文件，当 100% 完成而不是除零事故；
          熟头 header(ContentTypeHeader/ContentLengthHeader/LastModifiedHeader) 与
          生头 rawHeader/rawHeaderList/rawHeaderPairs 的分工——
          Content-Range / Accept-Ranges 只在生头里；
          断点续传 = Range 头 + 206 + Content-Range 回显，两段拼起来与整段逐字节比对；
          边下边写：readyRead → reply->read(chunk) → QSaveFile::write，绝不 readAll 攒内存；
          取消用 abort()（abort 后仍会 finished，注意区分「被取消」与「失败」）
    诚实标注：本讲对着本机回环上的静态服务端做，可控制慢速与断流；
              真实 CDN 的分段与压缩行为不在承诺范围

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
