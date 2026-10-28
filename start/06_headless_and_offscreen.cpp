// ============================================================================
// 第 6 讲 · 无头运行与自动化
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   GUI 程序怎么在没有显示器、没有人点鼠标的服务器 / CI（持续集成）里跑起来
//   并「自我验证」？讲清四件事，并把本示例本身写成一个能脚本化跑完的自测：
//     1) QT_QPA_PLATFORM 选 offscreen 还是 minimal，区别在哪；
//     2) 为什么 CI 能测 GUI（因为 QPA 把「怎么画」抽象成了可替换的插件）；
//     3) grab() 把窗口渲染成 QPixmap → QImage，再 save 成 PNG；
//     4) resize/sizeHint 的**断言式自检**，最后用退出码告诉脚本「过没过」。
//   还顺带说清 platforms 插件目录与 windeployqt 的关系（打包时别漏插件）。
//
// 对应构建目标名：start_06_headless_offscreen
//   add_executable(start_06_headless_offscreen 06_headless_and_offscreen.cpp)
//   target_link_libraries(start_06_headless_offscreen PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)
//
// 运行前须知（怎么无头跑它）：
//   · Linux/macOS/POSIX 控制台：
//         QT_QPA_PLATFORM=offscreen ./start_06_headless_offscreen
//   · Windows（cmd）： set QT_QPA_PLATFORM=offscreen && start_06_headless_offscreen.exe
//   · Windows（PowerShell）： $env:QT_QPA_PLATFORM="offscreen"; .\start_06_headless_offscreen.exe
//     不设该环境变量时，在 Windows 上会走默认的 windows 平台，弹出真窗口——
//     本示例同样能跑完（它不依赖「必须无头」，只是无头才适合 CI）。
//   · 本机 Qt6 的 platforms 插件目录实测存在这些插件（本讲据此而写）：
//         C:\msys64\ucrt64\plugins\platforms\  下有 qwindows.dll、qoffscreen.dll、
//         qminimal.dll、qdirect2d.dll —— 所以 offscreen / minimal 在这台机器上都能选到。
//   · 本机当前无法编译链接 Qt，本示例**未在本机实测编译**；API 均对本机头文件核对过。
// ============================================================================

#include <QApplication>
#include <QGuiApplication>   // platformName() / primaryScreen()
#include <QCoreApplication>  // exit(code)
#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QTimer>
#include <QPixmap>           // grab() 的返回类型；toImage()/save()
#include <QImage>            // 转成 QImage 后读 width()/height()
#include <QSize>
#include <QDebug>
#include <QString>

// 全局累计一个「所有断言是否都通过」的标志，最后据此决定退出码。
static bool allChecksPassed = true;

// 断言小工具：打印一条 PASS/FAIL，并在失败时把总标志拧成 false。
static void check(const QString &name, bool condition)
{
    if (condition) {
        qInfo().noquote() << QStringLiteral("  [PASS]") << name;
    } else {
        allChecksPassed = false;
        qWarning().noquote() << QStringLiteral("  [FAIL]") << name;
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // ── 1) 打印当前用的是哪个 QPA 插件 ───────────────────────────────
    // 核对：static QString platformName() —— qguiapplication.h 第 74 行。
    const QString platform = QGuiApplication::platformName();
    qInfo() << "本次运行的 QPA 平台插件是：" << platform;
    // offscreen ：完整的「能画」的平台——支持渲染到 QImage/QPixmap、支持字体度量、
    //             支持布局，只是窗口不往任何物理屏幕显示。适合「要截图/要像素」的测试。
    // minimal   ：一个「几乎什么都不做」的极简平台——能建窗口对象、能收事件循环，
    //             但不提供屏幕渲染等能力。适合「我只想让逻辑在无显示环境里跑起来，
    //             不关心也不生成像素」的场合。要截图就别用它，要用 offscreen。

    // ── 2) 为什么 CI 能测 GUI ────────────────────────────────────────
    // 关键在于 QPA（Qt Platform Abstraction）：Qt 把「开窗、取输入、画到屏幕」
    // 这些**每个操作系统都不一样**的事，抽成一套平台插件接口。widgets 只跟接口打交道。
    // 于是换到 offscreen/minimal 插件时，上层代码一行不改，底下变成「画进内存 /
    // 什么都不画」。这就是同一份 GUI 程序能在没有显卡、没有登录会话的 CI 容器里跑的依据。
    // primaryScreen() 核对：qguiapplication.h 第 81 行 static QScreen *primaryScreen()。
    // offscreen 插件会提供一个虚拟屏幕，所以这里通常非空；minimal 平台则可能为空。
    qInfo() << "primaryScreen 是否可用：" << (QGuiApplication::primaryScreen() != nullptr);

    // ── 3) 造一个有内容的窗口，准备截图 ──────────────────────────────
    QWidget window;
    window.setWindowTitle(QStringLiteral("第 6 讲 · 无头自测"));
    auto *layout = new QVBoxLayout(&window);
    layout->addWidget(new QLabel(QStringLiteral("Hello, headless Qt!"), &window));

    // ── 4) sizeHint / resize 的断言式自检 ────────────────────────────
    // 核对签名（qwidget.h）：
    //   第 522 行 virtual QSize sizeHint() const;        建议尺寸
    //   第 523 行 virtual QSize minimumSizeHint() const; 最小建议尺寸
    //   第 503 行 void resize(int w, int h);
    //   第 258 行 QSize size() const;
    // 先让布局算出建议尺寸（show 之前 sizeHint 也能问，但真正按建议展开要靠事件循环，
    // 这里显式 resize 定一个已知尺寸，好让「截图尺寸」可断言）。
    const QSize hint = window.sizeHint();
    qInfo() << "sizeHint() 建议尺寸 =" << hint;
    check(QStringLiteral("sizeHint 宽/高都大于 0"), hint.width() > 0 && hint.height() > 0);

    window.resize(320, 180);
    // resize 之后 size() 不一定立刻精确等于请求值（可能有最小尺寸约束），
    // 但在 offscreen + 无约束的 QWidget 上，我们断言它不小于 0 且方向正确。
    const QSize sz = window.size();
    qInfo() << "resize(320,180) 之后 size() =" << sz;
    check(QStringLiteral("resize 后宽高均 > 0"), sz.width() > 0 && sz.height() > 0);

    // ── 把窗口显示出来，稍后再截图（截图要等事件循环转起来才有内容）──
    window.show();

    QTimer::singleShot(300, [&window] {
        // ── 5) grab()：把窗口渲染成 QPixmap，再 save 成 PNG ────────────
        // 核对签名：
        //   QWidget::grab  —— qwidget.h 第 356 行
        //     Q_INVOKABLE QPixmap grab(const QRect &rectangle = QRect(QPoint(0,0), QSize(-1,-1)));
        //     不传参数即抓整个控件区域。
        //   QPixmap::toImage —— qpixmap.h 第 89 行 QImage toImage() const;
        //   QImage::save    —— qimage.h 第 268 行 bool save(const QString&, const char* fmt=nullptr, int q=-1);
        const QPixmap shot = window.grab();
        check(QStringLiteral("grab() 返回的 pixmap 非空"), !shot.isNull());
        qInfo() << "grab() 得到 pixmap：" << shot.size();

        // save 时按扩展名自动推断格式（.png → PNG）；返回 bool 表示是否写盘成功。
        const QString pngName = QStringLiteral("start_06_grab.png");
        const bool saved = shot.save(pngName);
        check(QStringLiteral("截图已保存为 PNG（save 返回 true）"), saved);

        // 转成 QImage 再读尺寸，演示 QPixmap→QImage 这步（做像素级断言时会用到）。
        const QImage img = shot.toImage();
        check(QStringLiteral("toImage() 后可读尺寸且与 pixmap 一致"),
              img.width() == shot.width() && img.height() == shot.height());
        qInfo().noquote() << QStringLiteral("  → 截图文件：") << pngName
                          << QStringLiteral("（放在程序的当前工作目录）");
    });

    // ── 6) 汇总并按退出码交差 ────────────────────────────────────────
    QTimer::singleShot(900, [] {
        qInfo() << "—— 自测结论 ——";
        if (allChecksPassed) {
            qInfo() << "全部断言通过，以退出码 0 结束（CI 判定为 PASS）";
            // exit(code) 核对：qcoreapplication.h 第 206 行 static void exit(int retcode = 0);
            QCoreApplication::exit(0);
        } else {
            qWarning() << "有断言失败，以退出码 1 结束（CI 判定为 FAIL）";
            QCoreApplication::exit(1);
        }
    });

    // exec() 的返回值 == 上面 exit() 传进去的 code，main 直接把它当进程退出码 return，
    // 这样 shell 里 `... ; echo $?` 或 CI 的 step 就能直接读到过/没过。
    return app.exec();
}

// ============================================================================
// 附：platforms 插件目录与 windeployqt 的关系（打包必看）
// ============================================================================
/*
 * 无头运行能成立的前提，是运行时**找得到**对应的平台插件：offscreen 就找
 * platforms/qoffscreen.dll，minimal 就找 qminimal.dll。Qt 默认相对可执行文件
 * 找 platforms/ 子目录（也可用 -platformpluginpath 或 QT_QPA_PLATFORM_PLUGIN_PATH
 * 指路，见第 1 讲的命令行选项）。
 *
 * 你在开发机上能跑，是因为插件都在 C:\msys64\ucrt64\plugins 下、PATH 也对。
 * 但把 exe 单独拷到别的机器 / CI 镜像里，就会报经典的
 *     "This application failed to start because no Qt platform plugin could be
 *      initialized"  /  "could not find/load the Qt platform plugin 'windows'"
 * ——99% 是 platforms 目录没跟着拷。
 *
 * 正确做法：Windows 上用随 Qt 提供的 windeployqt（本机在
 * C:\msys64\ucrt64\bin\windeployqt.exe）对每个 exe 跑一次：
 *     windeployqt start_06_headless_offscreen.exe
 * 它会分析 exe 依赖，把 Qt6*.dll、platforms\*.dll（含你要的 qwindows/qoffscreen）、
 * styles\、imageformats\ 等**按需要的插件**拷到 exe 旁边。CI 里通常再配合
 * cmake --install 把 windeployqt 拷好的产物打成发布包。
 *
 * 【Qt5→Qt6 差异提醒】Qt6 把光栅渲染后端拆成了独立平台插件 qdirect2d（Windows），
 * 并且不再有 Qt5 的 -platform windows:direct2d 那种混合写法那么随意；另外 Qt6 的
 * windeployqt 是 windeployqt.exe（对应 QtCore 版本），迁移工程时注意别混用 Qt5 的
 * windeployqt 去拷 Qt6 的 dll，否则版本对不上直接起不来。
 */

// ——— 老师傅的话 ———
/*
 * 徒弟，这一讲最容易被误解的一件事：**「无头」不是「不能测 GUI」，而是「把 GUI
 * 的显示这一步换成了内存渲染」——被测的代码路径几乎没变，变的只是最底下那个平台
 * 插件。** 很多新手一听「CI 里跑不了 GUI」就去把界面逻辑全删了只测纯函数，那是
 * 舍近求远。真相是：只要你的程序不硬依赖「窗口真出现在物理屏幕上」，把
 * QT_QPA_PLATFORM 设成 offscreen，同一份代码就能在没有任何显示器的容器里跑完，
 * 还能 grab() 出像素来做「长对了没」的断言。要记住的只有一条分寸：
 * **要生成/比对像素，就用 offscreen；minimal 只够让事件循环空转，别拿它截图。**
 * 还有退出码这个细节别小看——GUI 程序在 CI 里最怕「卡住不退出」，所以本讲把每步
 * 自检结果都汇进一个 bool，最后用 exit(0/1) 交出去，脚本一眼就知道成没成。这才是
 * 「自动化」两个字落到实处的样子：不但要能无人值守跑完，还要把结论用退出码说清楚。
 */

// ——— 小练习 ———
/*
 *   1. 分别用 QT_QPA_PLATFORM=windows（默认弹出真窗口）和 =offscreen 各跑一次本
 *      程序，比较 platformName 打印、窗口有没有出现、grab 出来的 PNG 有没有区别。
 *   2. 把第 4 步的 window.resize(320,180) 改成 resize(1,1)，再看 size() 和
 *      grab 得到的尺寸是否真变成 1x1（体会「resize 请求」和「实际生效尺寸」之间
 *      可能隔着最小尺寸约束）——把这条 check 改成你会怎么断言。
 *   3. 故意把 platforms/qoffscreen.dll 改名后，用 QT_QPA_PLATFORM=offscreen 运行，
 *      亲眼读一次「no Qt platform plugin could be initialized」的报错原文；然后用
 *      windeployqt 重新拷贝插件修好它。这一步能让你以后打包心里有底。
 */
// ============================================================================
