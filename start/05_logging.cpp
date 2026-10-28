// ============================================================================
// 第 5 讲 · 调试输出
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   Qt 自带一套「分级 + 可格式化 + 可分类开关」的日志，别再用 std::cout 手搓。
//   落地四件事：
//     1) 五个级别 qDebug/qInfo/qWarning/qCritical/qFatal 的分工与后果
//     2) qSetMessagePattern 的占位符：让每条日志自带时间/文件/行号/函数/类型
//     3) qDebug().noquote().nospace() 这类**链式**接口到底改了什么
//     4) QLoggingCategory 分类日志 + QT_LOGGING_RULES（及程序内 setFilterRules）
//        按类别开/关，不靠重新编译
//
// 对应构建目标名：start_05_logging
//   add_executable(start_05_logging 05_logging.cpp)
//   target_link_libraries(start_05_logging PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)
//
// 运行前须知：
//   · 依赖 Qt6::Widgets（用一个隐形 QApplication 让 qInfo 走标准输出即可；
//     日志本身属 QtCore/QtGui，Widgets 只是为了和整套课程构建方式一致）。
//   · qFatal 会**直接终止进程**（abort），本示例默认不调用它，只在注释里讲。
//   · 本机当前无法编译链接 Qt，本示例**未在本机实测编译**；API 均对本机
//     QtCore/qlogging.h、qloggingcategory.h、qdebug.h 头文件核对过。
//   · 无人值守：QTimer 驱动，结束自动 quit()。
// ============================================================================

#include <QApplication>
#include <QTimer>
#include <QCoreApplication>
#include <QDebug>             // QtCore/qdebug.h：QDebug 与链式接口
// qInfo/qDebug/... 宏、QtMsgType、qSetMessagePattern 都在 QtCore/qlogging.h，
// 它被 qglobal.h 全局带入，随便 include 一个 QtCore 头就能用（已核对）。
#include <QLoggingCategory>   // QtCore/qloggingcategory.h：分类日志
#include <QString>

// ── 定义一个「分类」──────────────────────────────────────────────────
// 核对：Q_LOGGING_CATEGORY(name, "点.分.类.名", 默认级别) —— qloggingcategory.h 第 147 行。
// 第三参数是 QtMsgType：设为 QtDebugMsg 表示 debug 级别默认就开；
// 若设成 QtWarningMsg，则这个类的 debug/info 默认**被静音**，只剩 warning 及以上。
// 这正是「分类日志」的意义：粒度开关，不看编译模式。
Q_LOGGING_CATEGORY(lcDemo, "app.start05", QtDebugMsg)

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // ── 2. 消息格式：qSetMessagePattern ──────────────────────────────
    // 核对：qSetMessagePattern(const QString&) —— qlogging.h 第 200 行。
    // 占位符（写在字符串里，用 %{...} 包裹；解析在 QtCore 内部，不在公开头文件，
    // 但都是 Qt 官方文档里长期稳定的这几项，我只列有把握的）：
    //   %{time}        时间（可加格式：%{time hh:mm:ss.zzz}）
    //   %{type}        级别标签 debug/info/warning/critical/fatal
    //   %{file}        源文件名（仅路径末段）   %{line} 行号   %{function} 函数名
    //   %{category}    分类名（配合 QLoggingCategory）
    //   %{message}     你 << 进去的正文
    //   %{appname}     应用名（applicationName()）  %{threadid} 线程 id
    // 不设 pattern 时，Qt 默认只输出 %{message}（不带文件行号），
    // 调试期一般设成「带 type/file/line」。
    qSetMessagePattern(QStringLiteral(
        "[%{time hh:mm:ss.zzz}] [%{type}] %{file}:%{line} %{function} :: %{message}"));

    qInfo() << "—— 1) 五个级别：轻重不同，去向与后果也不同 ——";
    // qDebug  ：开发期细节，默认在 release 里也可由 QT_NO_DEBUG_OUTPUT 关掉。
    qDebug()   << "debug：啰嗦的调试细节，追踪变量状态用";
    // qInfo   ：正常里程碑/状态播报，比 debug 高一级，默认都会显示。
    qInfo()    << "info：程序走到了一个值得记录的节点";
    // qWarning：能用但不正常，程序继续跑；这是「该修但还没崩」的信号。
    qWarning() << "warning：不对劲，但还能继续";
    // qCritical：严重错误，通常紧跟一次失败恢复；比 warning 更显眼。
    qCritical()<< "critical：出了影响功能的错误";
    // qFatal   ：致命错误——**打印后立即调用 abort() 终止进程**，绝不返回。
    // 所以本示例**不会真的执行 qFatal**（跑起来就 abort 了），只说明其用途：
    // 用在「继续下去只会造成数据损坏」的场合。示例：
    //   qFatal("配置文件损坏，无法安全启动");   // ← 一旦执行，进程当场结束

    // ── 3. 链式接口：noquote / nospace / quote ───────────────────────
    qInfo() << "—— 3) 链式接口改了输出格式 ——";
    // 核对：qdebug.h 第 130 行 QDebug &noquote()、第 116 行 nospace()、
    //       第 129 行 quote()。
    // 【纠一个常见口误】网上常有写成 .quoted() 的——Qt6 的 QDebug **没有 quoted()
    // 这个方法**。开/关引号的方法叫 quote()/noquote()（默认 QString 输出带引号，
    // noquote() 去掉引号）。这里照真实 API 写。
    QString s = QStringLiteral("你好，Qt");
    qDebug().noquote() << "noquote()：" << s;              // 不带引号
    qDebug().nospace() << "nospace()：A" << 1 << "B" << 2; // 取消自动空格
    qDebug().quote()   << "quote() 后：" << s;             // 显式恢复引号（默认即是）

    // ── 4. 分类日志 + 规则开关 ──────────────────────────────────────
    qInfo() << "—— 4) 分类日志 QLoggingCategory ——";
    // 核对：qCDebug/qCInfo/qCWarning/qCCritical 宏 —— qloggingcategory.h 第 162~165 行。
    qCDebug(lcDemo)   << "这是 app.start05 分类下的 debug 信息";
    qCInfo(lcDemo)    << "这是 app.start05 分类下的 info 信息";
    qCWarning(lcDemo) << "这是 app.start05 分类下的 warning 信息";

    // 程序内开关（等价于环境变量，但不用重启进程）：
    // 核对：static void QLoggingCategory::setFilterRules(const QString&) —— 第 39 行。
    // 规则形如 "<类别>.<级别>=<true/false>"，'*' 可通配。
    // 下面这条把 app.start05 的 debug 关掉：之后再 qCDebug(lcDemo) 就静默了。
    QLoggingCategory::setFilterRules(QStringLiteral("app.start05.debug=false"));
    qCDebug(lcDemo) << "关过 debug 之后，这条 debug 不该再出现（类别已静音）";
    qCInfo(lcDemo)  << "但 info 仍然可见（只关了 debug）";

    // 【环境变量：QT_LOGGING_RULES】不改代码、不重启规则，也能开关分类——
    //   Windows(cmd) :  set QT_LOGGING_RULES=app.start05.debug=false
    //   PowerShell   :  $env:QT_LOGGING_RULES="app.start05.debug=false"
    //   MSYS2/POSIX  :  export QT_LOGGING_RULES="app.start05.debug=false"
    // 也支持通配与多规则用分号隔开：QT_LOGGING_RULES="*.debug=false;app.*=true"
    // 它和 setFilterRules 语法完全一致，区别只是「在进程外设置」vs「在代码里设置」。
    // 注意：环境变量优先级低于代码里的 setFilterRules（代码后设的会覆盖）。

    // 让程序「像个 GUI 一样活着」，但主要输出走控制台日志；
    // 用 QTimer 把日志排进事件循环，演示异步触发点，最后自动退出。
    QTimer::singleShot(300, [] {
        qCInfo(lcDemo) << "300ms：由事件循环（定时器）发出的分类日志，带上前面的 pattern";
    });
    QTimer::singleShot(600, [] {
        qInfo() << "600ms：日志演示结束，退出";
        QCoreApplication::quit();
    });

    return app.exec();
}

// ——— 老师傅的话 ———
/*
 * 徒弟，这一讲最容易被误解的一件事：**日志「级别」不是「颜色」，而是「开关的
 * 层级」和「后果的严重度」。** 很多人以为 qWarning 只是比 qDebug「红一点」，
 * 于是想当然地乱用级别，结果线上出问题时，真正严重的和噪音混在一起没法过滤。
 * 正确的分层心智是这样的：debug 是「只有开发者要看的过程细节」，release 里
 * 就该用 QT_LOGGING_RULES 或 QT_NO_DEBUG_OUTPUT 一键静音；info 是「运维想看的
 * 里程碑」；warning 是「不正常但还能跑」；critical 是「功能已经受损」；而 fatal
 * 是「我拒绝继续，直接 abort」——它不是「很重要的一条日志」，它是「程序给自己
 * 上吊」。所以判断标准永远是问自己：**「没有这条信息，我排查问题会不会抓瞎？
 * 出现这个情况，我还能不能安全地继续往下跑？」** 前者定级别，后者定是不是 fatal。
 * 顺带记住那对占位符 %{category} 和 QLoggingCategory：分类名要用**点分层级**
 * (app.start05)，这样一句 `app.*.debug=false` 就能静音整棵子树——这就是不重编译
 * 动态调日志量的关键，别把分类名写成随意一个字符串。
 */

// ——— 小练习 ———
/*
 *   1. 把 qSetMessagePattern 换成只留 "%{message}"，跑一遍看输出「裸」成什么样，
 *      再调成带 %{appname} 和 %{threadid}，观察这两项打印出了什么。
 *   2. 新增一个 Q_LOGGING_CATEGORY(lcNet, "app.start05.network", QtWarningMsg)，
 *      给它发 qCDebug/qCWarning 各一条，验证：因为默认级别设成 QtWarningMsg，
 *      它的 debug 一开头就是静音的；再用 setFilterRules("app.start05.network.debug=true")
 *      把它打开。
 *   3. 不改代码，用环境变量启动一次本程序（PowerShell 里
 *      $env:QT_LOGGING_RULES="app.start05.debug=false"，再运行），看是不是
 *      第一条 qCDebug 就没了——把「环境变量」和「setFilterRules」两条路都走通。
 */
// ============================================================================
