// ============================================================================
// 第 4 讲 · Qt 资源系统：qrc 是怎么变成你程序里的一块只读内存的
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   前三讲（01 QSettings / 02 文件 IO / 03 JSON）都在跟「磁盘上的文件」打交道。
//   但图标、qss、翻译、示例配置这些东西**不该依赖安装目录**，也不该被用户改坏——
//   它们该躺在可执行文件里。这一讲把这条链讲透：
//     1) **路径是怎么拼出来的**：资源路径 = ":/" + <qresource prefix> + 条目路径（或 alias）。
//        配套清单见 storage/resource_demo.qrc（我在那个文件里把 prefix/file/alias 三条规则
//        都写在注释里了）。本讲用同一张 logo.svg 挂两个不同路径，把「alias 改路径不改内容、
//        但字节真的存了两份」这件事打印出来。
//     2) **AUTORCC 做了什么**（仓库根 CMakeLists.txt 第 34 行已开 set(CMAKE_AUTORCC ON)，
//        第 216 行把 resource_demo.qrc 写进本目标的源文件列表，我不重复叙述开关本身，只说机制）：
//        构建系统对每个 .qrc 调 rcc 生成一个 C++ 文件（Qt 的 CMake 层里能看到产物名：
//        普通 AUTORCC 走 ${CMAKE_CURRENT_BINARY_DIR}/qrc_<名字>.cpp，Qt6CoreMacros.cmake 第 472 行；
//        qt_add_resources 走 .qt/rcc/qrc_<名字>.cpp，同文件第 2673~2675 行），
//        里面是三张静态数组（数据/名字/树）+ 一个 `qInitResources_<名字>()` + 一句
//        `Q_CONSTRUCTOR_FUNCTION(...)`——**在 main 之前就把资源挂进全局注册表**。
//        Qt 的 CMake 还会自己解析 qrc（_qt6_parse_qrc_file，第 319/375~376 行）好让
//        「改了图 → 重跑 rcc」这条依赖成立，并把该文件的 SKIP_AUTORCC 置 ON 避免重复生成。
//        压缩：Qt6 的 CMake 层默认让 AUTORCC 用 zstd，除非本机 Qt 没编 zstd 或你设了
//        QT_NO_AUTORCC_ZSTD（Qt6CoreMacros.cmake 第 698~711 行给目标追加 "--no-zstd"）。
//     3) **QResource 到底能读到什么**——这里要先把三个「听起来很该有」的名字钉死：
//        Qt 6.12 的 QtCore/qresource.h 我通读了（全文件只有 71 行），公共可读项是
//        fileName / absoluteFilePath / locale / isValid / compressionAlgorithm /
//        size / data / uncompressedSize / uncompressedData / lastModified + 两个静态
//        register/unregisterResource。**没有 alias()、没有 lang()（6.x 是 locale()）、
//        没有 compressedSize()、也没有 registeredResourcePaths()**。
//        「compressedSize」这件事由 size() 承担（存进包里的字节数），配合 uncompressedSize()
//        一起看才知道压没压；「alias」在运行时**不是**一个可查询字段，它只是拼路径的原料；
//        「列出所有已注册的根」这种 API 不存在——想看全貌就自己去走 QDir(":/")/QDirIterator。
//        另外 `isDir()/isFile()/children()` 在这个版本是 **protected**（第 54~60 行），
//        外面调不到，别照着老教程写。
//     4) **Q_INIT_RESOURCE 的边界**（这是被写错最多的一条）：
//        宏在 QtCore/qtresource.h 第 15~17 行，展开成「extern 声明 + 调用 qInitResources_<name>()」。
//        可执行程序里的 .qrc 由 AUTORCC 生成的构造函数**自动注册**，你再手写一遍纯属多余；
//        真正需要它的是**静态库**（链接器把没人引用的那个目标文件整块丢掉 → 构造函数进不了
//        可执行文件）和**插件/动态库被 dlopen 的场景**。写错的代价不是崩，而是
//        「注册计数与注销计数不配对」——生成的清理函数 qCleanupResources_<name>() 是按调用次数
//        配对的（计数细节我在本机只能看到宏本身，**待复核**：以「别乱加」为纪律就安全）。
//     5) **资源是只读的**：QFile 能把 ":/x" 打开读，但改不了（本讲打印 isWritable 与写打开的失败）。
//        正确姿势是 QFile::copy 把资源里的东西释放到 TempLocation，再改磁盘副本，
//        并且**回头再读一次资源本体**证明它一根汗毛没动。
//     6) QIcon / QPixmap 从资源加载，以及主题图标（QIcon::fromTheme）在这台机器上的真实处境。
//   谨慎陈述的部分：网上流传的「Qt6 删了 rcc 的 -binary 格式」这类历史，我在本机头文件与
//   CMake 模块里**核对不到**（本机 share/qt6/bin/rcc.exe 存在，但我跑 `rcc -help`/`--help`/
//   `--version` 三个都是空输出，拿不到它的选项表），所以本讲不写这种结论；能写的事实只有上面
//   那条 Compression 枚举（qresource.h 第 23~27 行：NoCompression / ZlibCompression / ZstdCompression）
//   和 CMake 那两条。
//
// 对应构建目标名：storage_04_resource_system（仓库根 CMakeLists.txt 第 216 行，照抄不改）
//   set(CMAKE_AUTORCC ON)                        # 第 34 行，本目标的 .qrc 靠它
//   qt_add_demo(storage storage_04_resource_system storage/04_resource_system.cpp storage/resource_demo.qrc)
//   展开后等价于：
//     add_executable(storage_04_resource_system 04_resource_system.cpp resource_demo.qrc)
//     target_link_libraries(... Qt6::Widgets Qt6::Sql Qt6::Xml Threads::Threads)   # 第 212 行的 QT_DEMO_LIBS
//   ⚠ 一处如实记录的缺口：本讲用了 **QSvgRenderer（Qt6::Svg）**，而第 212 行 storage 组的
//     QT_DEMO_LIBS 只有 Widgets/Sql/Xml，**没有 Qt6::Svg**；按要求我不改 CMakeLists.txt，
//     所以链接期需要维护者把 Svg 补进这一组（或者把本讲挪到 painting 那组）。
//     校验门 _tools/check_syntax.sh 的 include 路径里**是**带 QtSvg 的（脚本第 18 行），
//     所以本文件的 API 用法能被编译期核到；缺的只是链接。
//
// 运行前须知：
//   · 无人值守：QTimer::singleShot 串步骤 → qInfo() 打印证据 → 清理临时目录 → quit()。
//     **没有 QDialog::exec()、没有模态对话框**，只用 processEvents() 催一次绘制。
//   · 可以无头跑：`storage_04_resource_system -platform offscreen`（CMakeLists 第 88 行的
//     CTest 就是这么设的）。需要 QApplication：QPixmap/QIcon/QPainter 属于 QtGui，
//     没有 QGuiApplication 实例时它们一律给空对象，那会让本讲的像素证据失真。
//   · 资源来自本目标的 resource_demo.qrc：`:storage/demo/hello.txt` 与 `:storage/demo/logo.svg`、
//     `:brand/qt-style-logo.svg` 三个入口。**如果换了个目标编译本 .cpp 而没带 qrc，
//     所有 :/ 路径都会打不开**——本讲每个证据都带了「打不开就打印原因并跳过」的分支。
//   · SVG 有两条独立路径：① QSvgRenderer（Qt6::Svg，本讲主路径）；
//     ② QImageReader 认 "svg" 格式（那是 imageformats 里的 qsvg 插件）。
//     本机 QtSql/Svg 头文件在，插件不保证在，所以②是先探测再决定要不要演示（打印原因，不崩）。
//   · 落盘约定：所有临时文件写在 QStandardPaths::TempLocation 下的 qtc_storage04 小目录，
//     结尾 removeRecursively 删掉并把结果打印出来。
//   · 未实测声明：**本文件未在本机实测编译运行**；只做的是 moc + g++ -fsyntax-only 核对
//     （-std=c++17 -fPIC -Wall -Wextra，0 error / 0 warning），未链接、未运行。
//     文中所有「预期」后面是推理，不是实测数字。
// ============================================================================

#include <QApplication>              // QtWidgets/qapplication.h（本讲要画像素，得建 GUI 应用对象）
#include <QCoreApplication>          // QtCore/qcoreapplication.h（quit/processEvents）
#include <QTimer>                    // QtCore/qtimer.h（串演示步骤）
#include <QDebug>                    // qInfo()
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QFile>                     // QtCore/qfile.h（第 250 行 static bool copy(src,dst)）
#include <QFileInfo>                 // QtCore/qfileinfo.h
#include <QDir>                      // QtCore/qdir.h（第 24~47 行 Filter 枚举）
#include <QDirIterator>              // QtCore/qdiriterator.h（第 26~33 行三个重载）
#include <QResource>                 // QtCore/qresource.h（全文 71 行，第一节逐行抄）
#include <QStandardPaths>            // QtCore/qstandardpaths.h（第 29 行 TempLocation / 第 48 行 writableLocation）
#include <QLocale>                   // QResource 的 locale 构造参数
#include <QDateTime>                 // lastModified() 的返回类型
#include <QImage>                    // QtGui/qimage.h
#include <QPainter>                  // QtGui/qpainter.h（把 SVG 画进 QImage）
#include <QColor>                    // QtGui/qcolor.h（qRed/qGreen/qBlue/qAlpha 这几个函数在这儿）
#include <QEventLoop>                // QtCore/qeventloop.h（processEvents 的 AllEvents）
#include <QPixmap>                   // QtGui/qpixmap.h
#include <QIcon>                     // QtGui/qicon.h（第 243~258 行 fromTheme/themeName）
#include <QImageReader>              // QtGui/qimagereader.h（supportedImageFormats：查 svg 插件）
#include <QtGlobal>                  // QT_VERSION_STR、qRed/qGreen/qBlue/qAlpha 在 qrgb.h（已被 QtGui 带进来）
#include <QGuiApplication>           // platformName()

#include <QtSvg/QSvgRenderer>        // QtSvg/qsvgrenderer.h（第 37/44/73/76 行，见第一节）

// ============================================================================
// 一、本机头文件逐行核对（C:\msys64\ucrt64\include\qt6，Qt 6.12.0）
// ============================================================================
// QtCore/qresource.h（整份只有 71 行，这是本讲最重要的一节）：
//   第 20 行  class Q_CORE_EXPORT QResource
//   第 23~27 行 enum Compression { NoCompression, ZlibCompression, ZstdCompression };
//   第 29 行  QResource(const QString &file = QString(), const QLocale &locale = QLocale());
//             ↑ 第二个参数就是 qrc 里 lang= 的那套机制在 Qt6 的样子：**locale**，不是 lang 字符串
//   第 32/33/34 行 setFileName / fileName / absoluteFilePath
//   第 36/37 行 setLocale / locale
//   第 39 行  bool isValid() const;
//   第 41 行  Compression compressionAlgorithm() const;
//   第 42 行  qint64 size() const;                 ← 存进包里的那份字节数（压缩后）
//   第 43 行  const uchar *data() const;           ← 可能是压缩缓冲，别当成「文件明文」用
//   第 44 行  qint64 uncompressedSize() const;     ← 解开之后
//   第 45 行  QByteArray uncompressedData() const; ← 想要明文用这个（或老老实实用 QFile）
//   第 46 行  QDateTime lastModified() const;
//   第 48~52 行 static bool registerResource(const QString &rccFilename, const QString &resourceRoot = QString());
//             static bool unregisterResource(同上);
//             static bool registerResource(const uchar *rccData, ...);   ← 内存里的 .rcc（编译期产物）
//   第 54~60 行 protected: isDir() / isFile() / children()              ← **外部调不到**
//   ⚠ 通读全文的结论：**没有** alias()、lang()、compressedSize()、registeredResourcePaths()、
//     也没有 isCompressed()/resourceForKey() 之类。网上「QResource::registeredResourcePaths()」
//     那种写法在 Qt6 编不过（本讲不写它，用 QDir 走目录代替，见证据①）。
// QtCore/qtresource.h（宏，不是类）：
//   第 15~17 行 #define Q_INIT_RESOURCE(name) do { extern int QT_MANGLE_NAMESPACE(qInitResources_##name)();
//                                              QT_MANGLE_NAMESPACE(qInitResources_##name)(); } while (false)
//   第 18 行起 #define Q_CLEANUP_RESOURCE(name) 对称的那个
// QtCore/qfile.h：
//   第 250 行 static bool copy(const QString &fileName, const QString &newName);
//   （第 246 行是三参数版，带 QFile::CopyFlags；第 255/264 行是 std::filesystem::path 版）
// QtCore/qdir.h：第 24 行 Dirs=0x001、第 47 行 NoDotAndDotDot = NoDot | NoDotDot（无弃用标记）
// QtCore/qdiriterator.h：第 26~33 行 (path, Filters, IteratorFlags) 三个重载
// QtGui/qicon.h：第 243/244 行 fromTheme(name[, fallback])、245 hasThemeIcon、257 themeName、258 setThemeName
// QtSvg/qsvgrenderer.h：第 37 行 bool isValid()、第 44 行 QSize defaultSize()、
//                      第 73/74/75 行 load(filename)/load(QByteArray)/load(QXmlStreamReader*)、
//                      第 76/77 行 render(QPainter*)/render(QPainter*, const QRectF&)
//                      ⚠ **没有 imageSize()**（那是 QSvgItem/QSvgWidget 上的），本讲用 defaultSize()。
// ============================================================================

namespace {

// ── 临时目录：QStandardPaths::TempLocation 下自建一个小窝，收尾连窝端 ──────────
QString demoTempDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return QDir(base).filePath(QStringLiteral("qtc_storage04"));
}

// 把资源路径写成可读的短名（证据里反复要用到这三条路径）
const QString kHelloPath = QStringLiteral(":/storage/demo/hello.txt");
const QString kLogoPath    = QStringLiteral(":/storage/demo/logo.svg");
const QString kLogoAlias   = QStringLiteral(":/brand/qt-style-logo.svg");

// Compression 枚举翻译成人话（枚举值本机核对：qresource.h 第 23~27 行）
QString compressionName(QResource::Compression c)
{
    switch (c) {
    case QResource::NoCompression:   return QStringLiteral("NoCompression（原样存）");
    case QResource::ZlibCompression: return QStringLiteral("ZlibCompression");
    case QResource::ZstdCompression: return QStringLiteral("ZstdCompression");
    }
    return QStringLiteral("未知枚举值 %1").arg(int(c));   // 走到这里说明枚举扩了，回来更新本节
}

// 只读地把一个资源读成 QByteArray；失败时把原因写进 *why
QByteArray slurpResource(const QString &path, QString *why)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (why)
            *why = f.errorString();
        return QByteArray();
    }
    const QByteArray raw = f.readAll();
    f.close();
    if (why)
        why->clear();
    return raw;
}

void nudgeEvents()
{
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

} // namespace

// ============================================================================
// 二、证据①：路径是从哪儿拼出来的（qrc → :/ 树）
// ============================================================================
static void evidenceResourceTree()
{
    qInfo() << "【证据①】资源树：\":/\" 下面是 prefix 的集合，不是磁盘目录";

    // 没有 registeredResourcePaths() 这种 API（第一节已核对），想看全貌只能自己走目录。
    QDir root(QStringLiteral(":/"));
    qInfo() << "  QDir(\":/\").exists() =" << root.exists()
            << "｜顶层条目（含 . / ..）=" << root.entryList(QDir::AllEntries | QDir::AllDirs).size()
            << "｜只列目录且去掉 . 和 .. =" << root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)
            << "← 这两个就是我 qrc 里写的 /storage 与 /brand";

    // 递归把整棵资源树打出来：条数就是 qrc 里 <file> 的条数（同一份字节挂两处 = 计两次）
    int seen = 0;
    QDirIterator it(QStringLiteral(":/"), QDir::Files, QDirIterator::Subdirectories);
    QStringList paths;
    while (it.hasNext()) {
        paths << it.next();
        ++seen;
    }
    paths.sort();
    qInfo() << "  资源里的文件总数 =" << seen << "（预期 3：hello.txt 一次、logo.svg 两次）";
    for (const QString &p : std::as_const(paths))
        qInfo() << "    -" << p << "｜QFileInfo.size() =" << QFileInfo(p).size();

    qInfo() << "  同一张 logo 的两个入口，字节数相同 =" << (QFileInfo(kLogoPath).size() == QFileInfo(kLogoAlias).size())
            << "｜路径不同但内容是同一份：**rcc 存了两份字节**（alias 只改路径，不省空间）";
    qInfo() << "  对照：磁盘上的文件路径" << QDir::currentPath() << "里根本没有 \":/\"，"
               "这套前缀只存在于 Qt 的文件抽象层（QFileDevice → QFileEngine → 资源引擎）。";
}

// ============================================================================
// 三、证据②：QFile 读资源 + 「资源是只读的」
// ============================================================================
static void evidenceReadResource()
{
    qInfo() << "【证据②】从 :/ 读文本：字节数、首行、以及只读这件事";

    QString why;
    const QByteArray raw = slurpResource(kHelloPath, &why);
    if (raw.isEmpty()) {
        qInfo() << "  ⚠ 读不到" << kHelloPath << "，原因：" << why;
        qInfo() << "     → 十有八九是本 .cpp 被编进了**没有带 resource_demo.qrc 的目标**。"
                   "按 CMakeLists 第 216 行的目标名编译就没有这个问题。跳过本条证据。";
        return;
    }
    const QString text = QString::fromUtf8(raw);
    const QString firstLine = text.section(QLatin1Char('\n'), 0, 0).remove(QLatin1Char('\r'));

    qInfo() << "  " << kHelloPath << "：QFile::readAll() 字节数 =" << raw.size()
            << "｜QFileInfo.size() =" << QFileInfo(kHelloPath).size()
            << "（两个数应当相等：前者是读到的，后者是包里的账）";
    qInfo() << "  首行 =" << firstLine;
    qInfo() << "  UTF-8 字节数" << raw.size() << "比字符数" << text.size() << "多出来的那部分"
                                                               "就是中文（一个汉字 3 字节）——资源里放中文文本时，"
                                                               "记得**自己指定编码**（这里用 fromUtf8），别指望系统 locale。";

    QFileInfo fi(kHelloPath);
    qInfo() << "  只读性质：isFile =" << fi.isFile() << "｜exists =" << fi.exists()
            << "｜isWritable =" << fi.isWritable()
            << "｜isSymLink =" << fi.isSymLink();
    QFile w(kHelloPath);
    const bool openedWrite = w.open(QIODevice::ReadWrite);
    qInfo() << "  试着以 ReadWrite 打开资源本体 =" << openedWrite
            << "，errorString =" << (openedWrite ? QStringLiteral("（居然开了，待复核）") : w.errorString());
    if (openedWrite)
        w.close();
    qInfo() << "  → 结论：**别想着往 :/ 里写**。要改就先 copy 到磁盘（证据⑤）。";
}

// ============================================================================
// 四、证据③：QResource 能读到什么（把「不存在的那几个名字」也钉出来）
// ============================================================================
static void evidenceQResource()
{
    qInfo() << "【证据③】QResource 的可用字段（6.12 通读全文的结果）";

    QResource res(kHelloPath);
    qInfo() << "  fileName() =" << res.fileName()
            << "｜absoluteFilePath() =" << res.absoluteFilePath()
            << "（两个都还带 \":\"，它不是磁盘路径；语义以实测为准，此处标「待复核」）";
    qInfo() << "  isValid() =" << res.isValid() << "（无效 = 这个路径没注册，多半是 qrc 没编进本目标）";
    qInfo() << "  locale() =" << res.locale().name()
            << "｜qrc 里的 lang= 机制在 Qt6 就叫 locale，**不叫 lang**（没有 lang() 这个方法）";
    qInfo() << "  compressionAlgorithm() =" << compressionName(res.compressionAlgorithm());
    const qint64 stored = res.size();
    const qint64 plain  = res.uncompressedSize();
    qInfo() << "  size() =" << stored << "｜uncompressedSize() =" << plain
            << "｜比值 =" << (plain > 0 ? double(stored) / double(plain) : 0.0)
            << "← 「compressedSize()」这个名字**不存在**，要看的这对：存进去的 / 解出来的";
    const QByteArray plainData = res.uncompressedData();
    qInfo() << "  uncompressedData().size() =" << plainData.size()
            << "｜头两个字节 =" << (plainData.size() > 1
                                       ? plainData.left(2).toHex()
                                       : QStringLiteral("(空)"))
            << "｜data() 指针是否非空 =" << (res.data() != nullptr)
            << "（data() 返回的是**包里那一坨**，压缩时不是明文，别直接当内容用）";
    const QDateTime lm = res.lastModified();
    qInfo() << "  lastModified() =" << lm << "｜isValid =" << lm.isValid()
            << "（时间戳不是 qrc 格式的必备字段，拿到无效值属正常，别拿它做逻辑判断）";

    // alias 不是可查询字段：能核对的是「alias 拼出来的那条路径存不存在、内容是否一致」
    QResource logoA(kLogoPath), logoB(kLogoAlias);
    qInfo() << "  alias 的运行时真相：两个路径各自 isValid =" << logoA.isValid() << logoB.isValid()
            << "｜字节数（size）=" << logoA.size() << logoB.size()
            << "｜明文长度 =" << logoA.uncompressedData().size() << logoB.uncompressedData().size()
            << "→ 没有 alias() 可以问「这是不是个别名」，只能看到「两条路径、两份相同字节」";

    // lang/locale 那条路径：qrc 没给德语版，所以带德语 locale 去问应落到默认那份
    QResource de(kHelloPath, QLocale(QLocale::German));
    qInfo() << "  用德语 locale 问同一个路径：isValid =" << de.isValid()
            << "｜locale =" << de.locale().name()
            << "｜size 仍 =" << de.size()
            << "（qrc 里没写 lang=\"de\" 的条目 → 回落到无语言的那份，这就是 lang 机制的行为）";
    qInfo() << "  ⚠ 这一版 QResource 的 isDir()/isFile()/children() 在 **protected**（第 54~60 行），"
               "外面调不到；想列目录请走 QDir/QDirIterator（证据①）。老教程里 `res.children()` 那种写法编不过。";
}

// ============================================================================
// 五、证据④：SVG → QImage → 像素；QIcon/QPixmap 与主题图标
// ============================================================================
static void evidenceSvgAndIcon()
{
    qInfo() << "【证据④】资源里的 SVG 真能渲染出像素（这是「资源可用」最硬的证据）";

    QString why;
    const QByteArray svg = slurpResource(kLogoPath, &why);
    if (svg.isEmpty()) {
        qInfo() << "  ⚠ 读不到" << kLogoPath << "：" << why << "，跳过本条。";
        return;
    }
    qInfo() << "  SVG 源字节数 =" << svg.size() << "｜前 5 字节 =" << svg.left(5).toHex()
            << "（\"<?xml\" 的十六进制，说明 qrc 没动内容）";

    QSvgRenderer renderer;
    const bool loaded = renderer.load(svg);
    qInfo() << "  QSvgRenderer::load(QByteArray) =" << loaded << "｜isValid() =" << renderer.isValid()
            << "｜defaultSize() =" << renderer.defaultSize()
            << "（这张 svg 里写了 width/height=128，所以预期 defaultSize 是 128×128）";

    QImage img(96, 96, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        renderer.render(&p);                 // 渲染失败也不崩，会得到一张全透明的图（下面用像素判定）
    }
    const QRgb center = img.pixel(48, 48);
    const QRgb corner = img.pixel(2, 2);
    qInfo() << "  渲染结果：尺寸 =" << img.size() << "｜格式 =" << int(img.format())
            << "｜中心像素 (48,48) RGBA ="
            << qRed(center) << qGreen(center) << qBlue(center) << qAlpha(center);
    qInfo() << "  角落像素 (2,2) RGBA =" << qRed(corner) << qGreen(corner) << qBlue(corner) << qAlpha(corner)
            << "← 中心应当是「亮的绿、alpha 满」，角落是「深绿底、alpha 满」；"
               "两者 alpha 若都是 255，说明资源里那张图**真的被解出来并画了**";

    QPixmap pm = QPixmap::fromImage(img);
    qInfo() << "  QPixmap::fromImage 后 size =" << pm.size() << "｜isNull =" << pm.isNull();
    QIcon icon;
    icon.addPixmap(pm);
    qInfo() << "  QIcon 用渲染结果构造：isNull =" << icon.isNull()
            << "｜availableSizes 个数 =" << icon.availableSizes().size()
            << "｜pixmap(32×32) 尺寸 =" << icon.pixmap(QSize(32, 32), QIcon::Normal).size()
            << "（图标从资源来是常态，但**位图尺寸要备齐**，否则放大靠插值，糊的是用户界面）";

    // 直连资源路径的加载依赖 imageformats 里的 svg 插件——先探测，不可用就说清楚并跳过
    const QList<QByteArray> formats = QImageReader::supportedImageFormats();
    const bool svgViaPlugin = formats.contains("svg");
    qInfo() << "  QImageReader 支持的格式个数 =" << formats.size() << "｜其中含 \"svg\" =" << svgViaPlugin
            << "（这是 qsvg **imageformats 插件**在不在，跟链没链 Qt6::Svg 是两回事）";
    if (svgViaPlugin) {
        QPixmap direct(kLogoAlias);
        qInfo() << "  直接 QPixmap(\":/brand/qt-style-logo.svg\")：isNull =" << direct.isNull()
                << "｜size =" << direct.size();
    } else {
        qInfo() << "  → 本机没有 svg 图像格式插件：**跳过** QPixmap 直载 svg 的写法。"
                   "工程上这是「装了 Qt 但没装插件」最常见的坑，报「Invalid image data」就是它。";
    }
    qInfo() << "  主题图标：QIcon::themeName() =" << QIcon::themeName()
            << "｜hasThemeIcon(\"application-exit\") =" << QIcon::hasThemeIcon(QStringLiteral("application-exit"))
            << "（Windows/MSYS2 上一般没有 icon theme，themeName 为空是**正常**的；"
               "跨平台产品不能假设 fromTheme 有货，必须留回落到资源的份儿）";
    const QIcon themed = QIcon::fromTheme(QStringLiteral("application-exit"), icon);
    qInfo() << "  fromTheme(name, fallback) 拿到的是回落图标吗 =" << (themed.isNull() ? QStringLiteral("还是空")
                                                                                       : QStringLiteral("非空（来自回落）"));
}

// ============================================================================
// 六、证据⑤：资源只读 → copy 到 TempLocation → 改副本 → 回头验证资源没动
// ============================================================================
static void evidenceCopyToDiskAndEdit()
{
    qInfo() << "【证据⑤】把资源释放到磁盘再改（唯一正解），并证明资源本体不变";

    QString why;
    const QByteArray original = slurpResource(kHelloPath, &why);
    if (original.isEmpty()) {
        qInfo() << "  ⚠ 资源没读到：" << why << "，跳过。";
        return;
    }
    const qint64 sizeInResource = QFileInfo(kHelloPath).size();

    QDir().mkpath(demoTempDir());
    const QString dst = QDir(demoTempDir()).filePath(QStringLiteral("hello.txt"));
    QFile::remove(dst);                                  // 上次跑剩的就地清掉，保证 copy 是干净的
    const bool copied = QFile::copy(kHelloPath, dst);
    qInfo() << "  QFile::copy(:/storage/demo/hello.txt →" << dst << ") =" << copied
            << "｜磁盘副本字节数 =" << QFileInfo(dst).size()
            << "｜资源里那份字节数 =" << sizeInResource
            << "（预期相等）";
    if (!copied) {
        QFile probe(dst);                            // Qt6 的 errorString() 是 QIODevice 的成员，不是静态的
        const bool probeOk = probe.open(QIODevice::ReadOnly);   // open 带 nodiscard，必须接住返回值
        qInfo() << "  ⚠ copy 失败；再单独打开目标文件：" << (probeOk ? QStringLiteral("居然能开") : QStringLiteral("也开不了"))
                << "｜errorString =" << probe.errorString()
                << "→ 通常是 TempLocation 不可写或目录创建失败，后面的比对全部跳过。";
        if (probeOk)
            probe.close();
        return;
    }
    qInfo() << "  磁盘副本 isWritable =" << QFileInfo(dst).isWritable()
            << "｜资源本体 isWritable =" << QFileInfo(kHelloPath).isWritable()
            << "← 这一对比就是「为什么要先释放再改」的全部理由";

    // 追加一行到磁盘副本（追加=写开、seek 到尾、write）
    {
        QFile app(dst);
        const bool ok = app.open(QIODevice::Append);
        qInfo() << "  以 Append 打开磁盘副本 =" << ok;
        if (ok) {
            const QByteArray added = "\n这行是演示程序追加的，资源里没有。\n";
            const qint64 written = app.write(added);
            app.close();
            qInfo() << "  追加字节 =" << written << "（应为" << 34 << "字节：中文一字符 3 字节，所以别用字符数算长度）";
        }
    }
    QFile f(dst);
    const bool backOk = f.open(QIODevice::ReadOnly);   // nodiscard：接住
    const QByteArray back = backOk ? f.readAll() : QByteArray();
    f.close();
    qInfo() << "  读回磁盘副本：打开成功 =" << backOk
            << "｜字节数 =" << back.size()
            << "｜前" << original.size() << "字节与资源原文完全相同 ="
            << (back.left(original.size()) == original)
            << "｜副本是否变长 =" << (back.size() > original.size());

    const QByteArray stillSame = slurpResource(kHelloPath, &why);
    qInfo() << "  **再读一次资源本体**：字节数 =" << stillSame.size()
            << "｜与最初那份相同 =" << (stillSame == original)
            << "｜与磁盘副本相同 =" << (stillSame == back)
            << "← 期望是「与最初相同、与副本不同」：改的是副本，资源是只读的、也不可能是只改一半的";
}

// ============================================================================
// 七、证据⑥：注册/注销的失败路径（把 API 的形状摆出来，不假装能成功）
// ============================================================================
static void evidenceRegisterApi()
{
    qInfo() << "【证据⑥】QResource::registerResource：什么时候用得上、失败长什么样";

    const QString fakeRcc = QDir(demoTempDir()).filePath(QStringLiteral("nope.rcc"));
    const bool okFile = QResource::registerResource(fakeRcc);
    qInfo() << "  registerResource(不存在的 .rcc 文件" << fakeRcc << ") =" << okFile
            << "｜unregisterResource(同一个) =" << QResource::unregisterResource(fakeRcc);
    qInfo() << "  → 返回 false 是**正常的失败**（文件不存在/不是 rcc 格式）。工程上你只有两种场景"
               "会调它：① 加载编译期产物 .rcc（registerResource(const uchar *data,...) 那份，第 51 行）；"
               "② 运行时热插一包资源（带 resourceRoot 参数指定挂到哪个前缀下）。"
               "**普通 .qrc 不该用这条路**——那是 AUTORCC 的活。";
    qInfo() << "  Q_INIT_RESOURCE 的边界（背下来）：可执行程序 = AUTORCC 自动注册，别手写；"
               "静态库/需要防止链接器丢目标文件的场合 = 在库外显式调一次 Q_INIT_RESOURCE(名字)，"
               "名字是 .qrc 去掉后缀的文件名（本讲即 resource_demo）。"
               "写错的典型后果是「重复注册 / 注册与清理次数不配对」，而不是当场崩——"
               "这类 bug 最难查，所以纪律是**不该写就不写**。";
    nudgeEvents();
}

// ============================================================================
// 八、main：QTimer::singleShot 串起来，末尾清理并退出
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("storage_04_resource_system"));

    qInfo() << "================================================================";
    qInfo() << "storage 第 4 讲 · Qt 资源系统（Qt =" << QT_VERSION_STR
            << "，平台 =" << QGuiApplication::platformName() << "）";
    qInfo() << "================================================================";
    qInfo() << "  临时目录（结尾会删） =" << demoTempDir();

    QTimer::singleShot(0, [&app]() {
        Q_UNUSED(app);
        evidenceResourceTree();
    });
    QTimer::singleShot(30, []() {
        evidenceReadResource();
    });
    QTimer::singleShot(60, []() {
        evidenceQResource();
    });
    QTimer::singleShot(90, []() {
        evidenceSvgAndIcon();
    });
    QTimer::singleShot(120, []() {
        evidenceCopyToDiskAndEdit();
    });
    QTimer::singleShot(150, []() {
        evidenceRegisterApi();
    });

    // 收尾：删临时目录并打印清理结果，然后退出事件循环
    QTimer::singleShot(180, []() {
        const QString dir = demoTempDir();
        QFile::remove(QDir(dir).filePath(QStringLiteral("hello.txt")));
        const bool fileGone = !QFileInfo::exists(QDir(dir).filePath(QStringLiteral("hello.txt")));
        QDir d(dir);
        const bool dirGone = d.exists() ? d.removeRecursively() : true;
        qInfo() << "【收尾】清理：" << dir
                << "｜hello.txt 已删 =" << fileGone
                << "｜整个目录 removeRecursively 成功 =" << dirGone
                << "｜目录还在吗 =" << QFileInfo::exists(dir);
        qInfo() << "【收尾】这一讲四句话："
                   "① 资源路径是 \":/\" + prefix + 条目名/alias 拼出来的，跟磁盘无关；"
                   "② QResource 只有第一节列那十来个成员，alias()/lang()/compressedSize()/registeredResourcePaths() "
                   "在 Qt 6.12 都不存在；"
                   "③ Q_INIT_RESOURCE 只在静态库/特殊场合用，可执行文件里 AUTORCC 已经注册好了；"
                   "④ 要改就先 QFile::copy 到磁盘改副本，资源本体永远只读。";
        QCoreApplication::quit();
    });

    const int rc = app.exec();
    qInfo() << "（事件循环正常退出，rc =" << rc << "）";
    return rc;
}

// ============================================================================
// 九、速查表与纪律
// ============================================================================
/*
 * 一、qrc 三件套 → 路径的推导（背下来就不用猜）
 *   <qresource prefix="/storage">  +  <file>demo/hello.txt</file>
 *        → ":/storage/demo/hello.txt"          （也可写 ":storage/demo/hello.txt"，QFile 都吃）
 *   <qresource prefix="/brand">    +  <file alias="qt-style-logo.svg">demo/logo.svg</file>
 *        → ":/brand/qt-style-logo.svg"         （alias 顶掉的是 prefix 下面那一整截路径）
 *   prefix="/"  → 直接挂根上；同 prefix 想分组就再开一个 <qresource>。
 *   <file> 里的相对路径 = 相对**这个 .qrc 文件所在目录**，不是相对工程根、不是相对 build 目录。
 *   这条是新手第一大坑：qrc 里写 "storage/demo/logo.svg" 而 .qrc 本身就在 storage/ → 找不到文件。
 *
 * 二、CMake 侧到底谁在跑 rcc（本机 Qt6CoreMacros.cmake 看到的）
 *   · 顶层 set(CMAKE_AUTORCC ON)（第 34 行）+ 目标源文件列表里带 .qrc（第 216 行）
 *     → CMake 的 AUTORCC 规则调 rcc，生成 ${CMAKE_CURRENT_BINARY_DIR}/qrc_<名字>.cpp（Qt 侧同名单在第 472 行）
 *   · qt_add_resources(...) 那条路产物是 .qt/rcc/qrc_<名字>.cpp（第 2673~2675 行），
 *     并且 Qt 的宏会 SKIP_AUTORCC ON + 自己解析 qrc（第 319、375~376 行）来挂文件级依赖
 *   · zstd：默认 AUTORCC 用 zstd；本机 Qt 没 zstd 或设了 QT_NO_AUTORCC_ZSTD 时，
 *     Qt 给目标追加 AUTORCC_OPTIONS "--no-zstd"（第 698~711 行）
 *   · 生成的 .cpp 里那个 qInitResources_<名字>() 由 Q_CONSTRUCTOR_FUNCTION 在 main 之前调用，
 *     这就是「可执行程序不用手写 Q_INIT_RESOURCE」的根据（宏本体见 QtCore/qtresource.h 第 15~17 行）
 *
 * 三、想读资源元信息，能读的只有这些（qresource.h，6.12）
 *   fileName / absoluteFilePath / locale / isValid / compressionAlgorithm /
 *   size / uncompressedSize / data / uncompressedData / lastModified
 *   + static registerResource / unregisterResource（QString 版与 const uchar* 版各一对）
 *   读不到：alias、lang、compressedSize、isCompressed、"已注册路径列表"、children（protected）
 *
 * 四、六条纪律
 *   ① 资源里的中文/文本 → 明确按 UTF-8 解（QString::fromUtf8），别依赖 locale。
 *   ② 想改资源内容 → QFile::copy 到可写位置再改；别在 :/ 上开写模式（证据②打了 isWritable=false）。
 *   ③ 图标优先「资源里的位图/SVG + QSvgRenderer」，fromTheme 只当增强，永远留回落。
 *   ④ 大文件进资源要算账：包体变大、启动时不加载（按需解压），但 alias 重复挂一份就是双份字节。
 *   ⑤ 静态库里的资源：库外用 Q_INIT_RESOURCE(名字)；库内构造函数会被链接器丢掉。
 *   ⑥ 不要在资源里放需要**运行期写入**的东西（配置、缓存、下载）——那是 01/02 讲的 AppDataLocation/TempLocation。
 *
 * ——— 老师傅的话 ———
 *   徒弟，资源系统这玩意儿看着小，坑全在「名字」上。你在网上搜一圈，能搜出
 *   QResource::lang()、compressedSize()、registeredResourcePaths() 一大把写法——那些是
 *   Qt4/Qt5 时代的账本，或者是**根本没人核对过头文件的想象**。我这文件第一节把
 *   qresource.h 全文 71 行逐条抄给你了，公共面就那么大；想列资源就走 QDir，想问压缩后
 *   字节就用 size()/uncompressedSize() 这一对，想按语言挑就用 locale。
 *   我为什么这么较真：这类「听起来完全合理」的 API 名，编译器拦得住，代码评审拦不住——
 *   因为评审的人也想当然。**记 API 记到能报出它在哪个头文件第几行**，这是这一行手艺的门票。
 *
 *   第二件事：Q_INIT_RESOURCE 是「知道什么时候该用」比「会用」更重要的东西。
 *   我见过太多工程在可执行文件里手写它，理由十有八九是「某篇博客那么写」。它不会崩，
 *   只会让你的注册与清理计数对不上、让你在项目重构成静态库的时候彻底糊涂。
 *   我的规矩：**可执行文件一律不写；哪天资源进了静态库，再在库的使用方显式调一次**，
 *   而且调的那个 name 要跟 .qrc 文件名一致，写错了链接期就报未定义符号——那反倒是好事。
 *
 *   第三件事：别把资源当"能改的文件"。证据②里我以 ReadWrite 打开过一次资源本体，
 *   拿到的是失败；这就是设计。你要是遇到「产品要求运行时改图标/改 qss」，
 *   正确答案是证据⑤那条：释放到 AppDataLocation/TempLocation，改副本，程序只读副本。
 *   硬要往 :/ 写，你会去搜索「QFile 写资源」，然后掉进自定义 QFileEngineHandler 的深坑里，
 *   那是一个为了插件化文件系统而生的机制，不是给你改图标用的。
 *
 *   最后一句关于 SVG：它漂亮、体积小、随 DPI 放大不糊，但**它靠插件**。
 *   我在证据④里把两件事分开验证了：链了 Qt6::Svg 就有 QSvgRenderer；
 *   QPixmap/QImageReader 能不能直接吃 svg，看的是 imageformats 里那个 qsvg 插件在不在。
 *   发布包里忘了带插件，开发机上跑得好、客户机上黑屏，就是这么来的。
 *   （本讲还有一条如实记录的缺口：第 212 行 storage 组没链 Qt6::Svg，我在文件头写明了。）
 *
 * ——— 小练习 ———
 *   1. 把 resource_demo.qrc 里的 <file alias="qt-style-logo.svg"> 那一条删掉，重跑本程序：
 *      看证据①的文件总数从 3 变 2、证据③里 logoB.isValid() 变 false，并解释
 *      「alias 只影响路径，不影响磁盘文件」。再加回一条 <qresource prefix="/storage/demo">
 *      看看 prefix 分组与 <file> 路径怎么配。
 *   2. 给 qrc 加一个 <file compressed="false"> 的条目（可以复用 hello.txt 换个路径挂），
 *      把证据③里两个条目的 compressionAlgorithm() 与 size()/uncompressedSize() 比值并排打出来，
 *      解释为什么小文件有时「压不动甚至变大」（提示：rcc 有低于阈值原样存的策略，
 *      并去 Qt6CoreMacros.cmake 第 698~711 行确认 zstd 与 --no-zstd 的关系）。
 *   3. 故意把本 .cpp 编进一个**不带 qrc** 的目标（或临时改一下 qrc 里的 prefix），
 *      观察每条证据的「打不开就打印原因并跳过」分支有没有如期生效；
 *      然后回答：为什么程序不能因为资源缺失而崩（离线打包缺文件的现场就是这个场景）。
 *   4. 把证据⑤改成「释放到 AppDataLocation 而不是 TempLocation」，
 *      想一想配置文件该放哪、临时导出文件该放哪，两条线什么时候必须分开
 *      （对照 01 讲 QSettings 与 02 讲文件 IO 的落盘约定）。
 */
// ============================================================================

// 本文件没有 Q_OBJECT 类（证据函数全是静态自由函数，没有任何自定义信号/槽），
// 所以末尾不需要 #include "04_resource_system.moc"。
