// ============================================================================
// 第 3 讲 · QApplication 的职责
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   把 QApplication（连同父类 QGuiApplication / QCoreApplication）这个「应用对象」
//   到底管哪些事，一条条落地演示：
//     1) 应用路径：applicationDirPath() / applicationFilePath()
//     2) 身份：setOrganizationName() + setApplicationName()（和后面 QSettings 呼应）
//        + desktopFileName()
//     3) 外观：setFont() / setPalette() / setStyle()，并用 QStyleFactory::keys()
//        打印本机可用样式
//     4) 生命周期信号：aboutToQuit()
//
// 对应构建目标名：start_03_application_object
//   add_executable(start_03_application_object 03_application_object.cpp)
//   target_link_libraries(start_03_application_object PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)
//
// 运行前须知：
//   · 依赖 Qt6::Widgets。applicationDirPath() 返回的是**可执行文件所在目录**，
//     CMake 构建后一般在 build/start/ 下，运行前请 cd 到该目录或让其被找到。
//   · 本机当前无法编译链接 Qt，本示例**未在本机实测编译**；API 均对本机头文件核对过。
//   · 无人值守：QTimer 驱动，结束自动 quit()。
// ============================================================================

#include <QApplication>       // QtWidgets/qapplication.h
#include <QGuiApplication>    // QtGui/qguiapplication.h
#include <QCoreApplication>   // QtCore/qcoreapplication.h
#include <QStyleFactory>      // QtWidgets/qstylefactory.h
#include <QStyle>             // QtWidgets/qstyle.h（QStyle : public QObject）
#include <QFont>              // QtGui/qfont.h
#include <QPalette>           // QtGui/qpalette.h
#include <QWidget>
#include <QTimer>
#include <QDebug>
#include <QString>
#include <QStringList>

int main(int argc, char *argv[])
{
    // ── 应用对象必须先存在（第 1 讲已讲透，这里直接用）──────────────
    QApplication app(argc, argv);

    // ── 1. 应用路径 ──────────────────────────────────────────────────
    // 核对签名：QtCore/qcoreapplication.h 第 126~127 行
    //   static QString applicationDirPath();
    //   static QString applicationFilePath();
    // 用途：加载和数据文件同目录的资源（图标、翻译、配置模板）时，
    // 不要写死相对路径——那取决于「从哪个目录启动」；用 applicationDirPath()
    // 拼出来的路径才和「可执行文件在哪」绑定，稳。
    qInfo() << "applicationDirPath  =" << QCoreApplication::applicationDirPath();
    qInfo() << "applicationFilePath =" << QCoreApplication::applicationFilePath();

    // ── 2. 应用身份 ──────────────────────────────────────────────────
    // 核对签名：qcoreapplication.h 第 84~87 行
    //   static void setOrganizationName(const QString &);
    //   static void setApplicationName(const QString &);
    // 【和后面 QSettings 呼应，务必现在就设】QSettings 在 Windows 上把配置
    // 写到  HKEY_CURRENT_USER\Software\<组织名>\<应用名>；它「组织名/应用名」
    // 默认从 applicationName() 兜底猜。**如果你不设 setOrganizationName，
    // QSettings 会用一个奇怪的兜底组织名**（常常干脆就是应用名或空），
    // 注册表里就会冒出一堆来历不明的键，将来清理/迁移都头疼。所以：
    // 只要程序用 QSettings，这两行就是「开工第一件事」。
    QCoreApplication::setOrganizationName(QStringLiteral("QtLearnDemo"));
    QCoreApplication::setApplicationName(QStringLiteral("start_03_application_object"));
    qInfo() << "组织名 =" << QCoreApplication::organizationName()
            << "，应用名 =" << QCoreApplication::applicationName();

    // desktopFileName 是 QGuiApplication 的（Linux 下对应 .desktop 文件名，
    // 用于任务栏分组/图标匹配；Windows 上多数场景用不到，但设了不报错）。
    // 核对签名：QtGui/qguiapplication.h 第 64~65 行
    //   static void setDesktopFileName(const QString &);
    //   static QString desktopFileName();
    QGuiApplication::setDesktopFileName(QStringLiteral("org.qtlearn.start03"));
    qInfo() << "desktopFileName =" << QGuiApplication::desktopFileName();

    // ── 3. 外观：字体 / 调色板 / 样式 ────────────────────────────────
    // setFont：核对 qapplication.h 第 67 行 static void setFont(const QFont &, const char* = nullptr)
    //          这里不传 className，表示「全局默认字体」。
    QFont font;                       // QtGui/qfont.h 第 140 行 QFont();
    font.setPointSize(11);            // qfont.h 第 162 行 void setPointSize(int)
    QApplication::setFont(font);

    // setPalette：核对 qapplication.h 第 63 行 static void setPalette(const QPalette &, const char* = nullptr)
    // 这里只取系统当前调色板原样再设回去，演示「接口在哪」；真要自定义颜色
    // 用 QPalette::setColor(...)，本讲不展开以免跑偏。
    QPalette palette;                 // QtGui/qpalette.h 第 23 行 QPalette();
    QApplication::setPalette(palette);

    // setStyle + QStyleFactory::keys：先列出本机装了哪些样式，再挑一个真的设上去。
    // 核对签名：
    //   qstylefactory.h 第 19 行  static QStringList keys();
    //   qapplication.h  第 58 行  static QStyle *setStyle(const QString&);   // 返回新样式，失败返回 nullptr
    const QStringList styleKeys = QStyleFactory::keys();
    qInfo() << "本机可用样式（QStyleFactory::keys）：";
    for (const QString &name : styleKeys)
        qInfo().noquote() << "   -" << name;   // noquote 见第 5 讲

    if (!styleKeys.isEmpty()) {
        const QString pick = styleKeys.first();
        // setStyle 返回指向新样式对象的指针；返回 nullptr 说明这个名字对应的
        // 样式插件没装上。这一步是判断「样式到底换没换成」的关键。
        QStyle *applied = QApplication::setStyle(pick);
        if (applied)
            qInfo() << "已切换样式为：" << pick
                    << "（当前 style()->objectName =" << QApplication::style()->objectName() << "）";
        else
            qWarning() << "样式" << pick << "无法应用（样式插件缺失？）";
    }

    // ── 4. 生命周期信号 aboutToQuit ─────────────────────────────────
    // 核对签名：qcoreapplication.h 第 209 行  void aboutToQuit(QPrivateSignal);
    // 它在事件循环即将退出时发出——是做「收尾落盘、关连接」的地方。
    // 信号带一个 QPrivateSignal 占位参数（防止外部误 emit），连接到一个无参
    // lambda 是允许的：Qt 会自动截断这个私有参数。
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [] {
        qInfo() << "aboutToQuit 触发：事件循环即将退出，这里做收尾（打印/存配置）";
    });

    // ── 5. 一个顶层窗口，让「字体/样式」有地方显示，也让程序「像个 GUI」─
    QWidget window;
    window.setWindowTitle(QStringLiteral("第 3 讲 · QApplication 的职责"));
    window.resize(360, 180);
    window.show();

    // ── 自动演示：跑一会儿就 quit()，aboutToQuit 会在退出前被打印 ──────
    QTimer::singleShot(1200, [] {
        qInfo() << "1.2s：主动退出，演示 aboutToQuit 的触发时机";
        QCoreApplication::quit();
    });

    return app.exec();
}

// ——— 老师傅的话 ———
/*
 * 徒弟，这一讲最容易被误解的一件事：**setOrganizationName / setApplicationName
 * 不是「给界面显示个名字」用的装饰，它是给 QSettings 定位「配置存哪」用的坐标。**
 * 很多人程序里一堆配置要持久化，等到用 QSettings 那天，随手 new 一个无参
 * QSettings()，结果注册表里冒出一个以 exe 名命名的乱键，过两天改了 exe 名字，
 * 老配置全找不回来了——根子就在「身份」没在程序一开头钉死。正确姿势是：
 * main 里 QApplication 一构造好，第一块正事就是 setOrganizationName +
 * setApplicationName，然后才轮到建窗口、加载配置。把这两行当成「身份证登记」，
 * 登记完了，后面 QSettings 才知道往哪个抽屉里塞东西、从哪个抽屉里取东西。
 * 顺带一提 setStyle 返回值：别只看它「没崩」就当成功，一定要检查是不是 nullptr，
 * 样式是插件，插件没装齐时它是会「安静地失败」的。
 */

// ——— 小练习 ———
/*
 *   1. 把 setOrganizationName 那行删掉，改在别处用 QSettings s; 写一个键，
 *      跑完到注册表 HKEY_CURRENT_USER\Software 下找找它把配置塞哪去了，
 *      亲眼看看「不设组织名」的尴尬。然后补回那行。
 *   2. 从 QStyleFactory::keys() 打印出来的名字里挑两个，分别手动
 *      setStyle("...")（写死字符串），比较窗口外观差别；再故意写一个不存在的
 *      名字，验证 setStyle 返回 nullptr 时你那段判空有没有兜住。
 *   3. 把 setFont 的点字号改成 16，再看窗口里文字变大；然后用带 className 的
 *      重载 QApplication::setFont(font, "QPushButton")，体会「只给按钮换字体」
 *      和「全局换字体」的区别。
 */
// ============================================================================
