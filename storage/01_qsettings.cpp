// ============================================================================
// 第 1 讲 · QSettings：把配置写下去，并且还能读回来
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   「设置」是桌面程序里最小的一块持久化，也是最容易被写坏的一块。QSettings 帮你把
//   「键值对 → 磁盘上的一个 ini / 注册表项」这条翻译做完了，但它的坑全在**时机**和
//   **类型**这两件事上。本讲正面讲六条：
//     1) 组织名/应用名是 QSettings 的**构造期快照**，不是运行期查询——
//        为什么 setOrganizationName()/setApplicationName() 必须排在 new QSettings 之前，
//        本讲用两个对象各自的 fileName() 打成左右对照给你看。
//     2) 三组构造函数与三种作用域的落盘位置差别（本机 qsettings.h 第 94~102 行一共
//        六条重载，去掉 QT_NO_QOBJECT 那半份，实际可用的是这五类形态）：
//          · QSettings(Format, Scope, 组织, 应用)  ← 最灵活，能用 setPath 改道
//          · QSettings(Scope, 组织, 应用) / QSettings(组织, 应用) ← ★ **写死 NativeFormat**，
//            setDefaultFormat() 对它俩**无效**（qsettings.cpp 第 2536~2540 行、第 2561~2563 行
//            两份构造的第一个参数都是硬编码的 NativeFormat；步骤② 实测打在这两条上）
//          · QSettings(fileName, Format)          ← **唯一能把文件写到你指定地点的那组**
//          · QSettings(QObject *parent) / QSettings(Scope, parent) ← 全靠全局名字，
//            这两条才真的吃 defaultFormat()（.cpp 第 2661~2683 行：默认构造转手给
//            QSettings(UserScope, parent)，那条用 globalDefaultFormat）
//        本讲把 IniFormat + UserScope 用 QSettings::setPath() 改道到系统临时目录，
//        **不写注册表、不碰真实用户配置目录**（详见下面「运行前须知」）。
//     3) 层级 key 用 `/` 分隔、beginGroup/endGroup 的配对、group()/allKeys()/
//        childKeys()/childGroups() 各回答什么问题（第 137~139 行：三个都是 const）。
//     4) value(key, 默认值) 的**默认值只在「键不存在」时出场**——这是 Qt6 相对 Qt5
//        改掉的一处，也是最反直觉的一处：Qt6 的 QSettings **不再做任何类型转换**
//        （qsettings.cpp 第 3361~3372 行：get() 命中就原样把存的 QVariant 返回，
//          只有取不到才 return *defaultValue；Qt5 那份签名里的 requiredType/convert
//          已经整段删掉了）。所以第二个参数**不指定期望类型**，你 Qt5 时代的那套理解
//        作废。转换是后面你自己调的 QVariant::toInt()/toString() 在做的，
//        它转不动就默默给 0 / 空串，一声不响（存 "abc" 读 int → 你拿到的是 0，**不是** 7）。
//        本讲四个证据全打上。
//     5) setValue 之后 sync() 的时机：QSettings 是「改内存 + 择机刷盘」，析构也会刷。
//        本讲在 sync 前后各读一次磁盘上的 ini 原文，把字节数打出来。
//     6) **QSettings 没有任何信号**（本机 qsettings.h 第 27~185 行整份读完：
//        有 Q_OBJECT、有 protected event()，但一个 Q_SIGNALS 段都没有）。
//        老代码里说的「监听 QSettingsEvent」在本机 6.12 查无此名：qsettings.cpp 全文 grep   name-ok:QSettingsEvent 有意复述旧错名，本机 0 处
//        `QSettingsEvent` 是 **0 处**，真实机制朴素得多——第 330 行   name-ok:QSettingsEvent 同上，复述旧错名
//        `QCoreApplication::postEvent(q, new QEvent(QEvent::UpdateRequest));`，
//        第 3320 行的 `event()` 自己把它接住。既没有私有事件类，也没有公开的专用 QEvent::Type：
//          · 本讲装了一个 eventFilter 去**实测**能收到什么，把事件类型分布打出来；
//          · 结论给得保守：**要靠 QSettings 自己通知你配置变了，此路不通**；
//            正经做法是自己包一层 setter（本讲给 ConfigStore），或者老实轮询/比对哈希。
//        标 待复核 的地方是我不能从公共头文件确认的部分，别当结论用。
//
// 对应构建目标名：storage_01_qsettings
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(storage_01_qsettings 01_qsettings.cpp)
//   target_link_libraries(storage_01_qsettings PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)     # 本文件有 Q_OBJECT 类（ConfigStore / SettingsProbe）
//   # 手工核对：moc 01_qsettings.cpp -o 01_qsettings.moc，末尾那句 include 就是给它的钩子
//
// 运行前须知：
//   · 无人值守自动演示：QTimer::singleShot 串 9 步，末尾 qInfo() 打证据后
//     QCoreApplication::exit(Ledger::exitCode())：核对全过才返回 0，有一项对不上返回 1，
//     ctest 直接判红。**没有任何模态对话框、没有 exec() 等人点。**
//   · 全程只在系统临时目录下自己建的一个小目录里写文件：
//       QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/qt_storage_01"
//     并且开讲第一件事就是 `QSettings::setDefaultFormat(QSettings::IniFormat)`
//     加 `QSettings::setPath(IniFormat, UserScope, 那个目录)` 把 IniFormat 的落盘位置
//     整个改道过去——**一个字节都不写注册表、不写真实的用户配置目录**，
//     收尾用 QDir::removeRecursively() 删干净并打印删除结果。
//   · NativeFormat 在 Windows 上就是注册表（HKEY_CURRENT_USER\Software\组织\应用），
//     本讲**只说明、不构造写它的对象**，理由见上一条。
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 storage_01_qsettings）。本机这一轮：【硬账】56/56 项核对通过。
//     这只证明「编得过、跑得起来、上面这行为绿」——换 Qt 版本 / 换平台 / 换 CPU 核数
//     之后，正文里带具体数字的「预期」请以你自己那次运行输出为准
//     （重跑：bash _tools/run_all.sh）。
// ============================================================================

#include <QApplication>              // QtWidgets/qapplication.h
#include <QCoreApplication>          // QtCore/qcoreapplication.h：org/app 名字、quit()
#include <QSettings>                 // QtCore/qsettings.h（转发头）
#include <QStandardPaths>            // QtCore/qstandardpaths.h：writableLocation(TempLocation)
#include <QDir>                      // QtCore/qdir.h：mkpath / filePath / removeRecursively
#include <QFile>                     // QtCore/qfile.h：读 ini 原文当证据
#include <QFileInfo>                 // QtCore/qfileinfo.h：size() / lastModified()
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QVariant>                  // QtCore/qvariant.h：metaType() / typeId()
#include <QMetaType>                 // QMetaType::Int 之类
#include <QObject>
#include <QEvent>                    // QtCore/qcoreevent.h：int QEvent::type()
#include <QEventLoop>                // QtCore/qeventloop.h：processEvents 的 flags
#include <QHash>
#include <QTimer>
#include <QDebug>

// ============================================================================
// 零、硬账：退出码 = 结论
// ----------------------------------------------------------------------------
//   本讲不是「打印一堆数字给人看」就完事：每一项能对上的结论都登记进 Ledger，
//   收尾时如果 checksPassed != checksTotal，进程以 1 退出，ctest 直接判红。
//   规则同 threads/06、network/03：一项都没核（total == 0）也算失败。
// ============================================================================
namespace {

struct Ledger
{
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
                             .arg(want).arg(got);
}

void checkBool(const char *what, bool got, bool want = true)
{
    check(what, got ? 1 : 0, want ? 1 : 0);
}

} // namespace

// ============================================================================
// 一、核对签名（本机 D:\Qt\6.12.0\mingw_64\include\QtCore\qsettings.h，Qt 6.12.0）
// ============================================================================
// 类声明（第 26~30 行）：
//   #ifndef QT_NO_QOBJECT
//       class Q_CORE_EXPORT QSettings : public QObject      ← 第 27 行：**是 QObject**
//   #else
//       class Q_CORE_EXPORT QSettings                        ← 无 QObject 形态
//   #endif
//   第 33 行 Q_OBJECT；第 180 行 protected: bool event(QEvent *event) override;
//   ★ 整份文件搜不到 Q_SIGNALS / signals: —— **QSettings 一个信号都没有**。
//     第 184 行 `Q_DISABLE_COPY(QSettings)`：不能拷贝，只能一个作用域一个对象。
//
// 三个枚举（都是 Q_ENUM，所以能被元对象认识，但打印还是要自己转名字）：
//   第 40~44 行 enum Status { NoError = 0, AccessError, FormatError };
//   第 49~80 行 enum Format { NativeFormat = 0, IniFormat = 1,
//             [Windows] Registry32Format = 2, Registry64Format = 3,
//             [WASM] WebLocalStorageFormat = 4, WebIndexedDBFormat = 5,
//             InvalidFormat = 16, CustomFormat1..CustomFormat16 };
//   第 85~88 行 enum Scope { UserScope, SystemScope };
//   ← 注意 Qt6 **没有** QSettings::InvalidFormat 之外的老别名，也别假设 Registry*
//     在所有平台都存在：它是 `#if defined(Q_OS_WIN)` 才编进去的，跨平台代码里
//     写 Registry32Format 在 Linux 上直接编不过。
//
// 六条构造函数（第 93~102 行，QT_NO_QOBJECT = 0 的那一侧）：
//   第 94 行 explicit QSettings(const QString &organization,
//                              const QString &application = QString(), QObject *parent = nullptr);
//   第 96 行 QSettings(Scope scope, const QString &organization,
//                      const QString &application = QString(), QObject *parent = nullptr);
//   第 98 行 QSettings(Format format, Scope scope, const QString &organization,
//                      const QString &application = QString(), QObject *parent = nullptr);
//   第 100 行 QSettings(const QString &fileName, Format format, QObject *parent = nullptr);
//   第 101 行 explicit QSettings(QObject *parent = nullptr);
//   第 102 行 explicit QSettings(Scope scope, QObject *parent = nullptr);
//   ★ 第 100 行那个「传文件路径」的重载是唯一能把配置**写到指定地点**的正规入口，
//     也是「导出成一份 ini 给人拷走」这种需求的正确做法。它的第二个参数**没有默认值**：
//     你必须显式说 Format，`QSettings s("a.conf")` 这种写法在 Qt6 里编不过。
//
// 读写与层级（★ 本讲用到全部的签名，注意第 121~156 行那些 QAnyStringView 重载）：
//   第 122~126 行：beginGroup 有两份——QString 那份在 `#if QT_CORE_REMOVED_SINCE(6, 4)`
//                 里（为兼容性留的符号），**你写的代码会解析到第 124 行的
//                 QAnyStringView 那份**。endGroup() 第 125 行，group() 第 126 行 const。
//   第 132~135 行：int beginReadArray(QAnyStringView)、void beginWriteArray(QAnyStringView,
//                 int size = -1)、endArray()、setArrayIndex(int)  ← 数组语法糖，本讲只在
//                 速查表里提一句（它的 value 语义和「层级 key + 手工编号」等价）。
//   第 137 行 QStringList allKeys() const;      ← **全树、带 `/` 的完整键**，不受 group 影响
//   第 138 行 QStringList childKeys() const;    ← 只看当前 group 下的「叶子键」
//   第 139 行 QStringList childGroups() const;  ← 只看当前 group 下的「下一层目录名」
//   第 140 行 bool isWritable() const;
//   第 147 行 void setValue(QAnyStringView key, const QVariant &value);
//   第 148 行 QVariant value(QAnyStringView key, const QVariant &defaultValue) const;
//   第 149 行 QVariant value(QAnyStringView key) const;     ← 单参版：找不到就是无效 QVariant
//   第 155 行 void remove(QAnyStringView key);   第 156 行 bool contains(QAnyStringView key) const;
//   第 115 行 void clear();          ← 清**整个**对象能碰到的范围，不是清当前 group
//   第 116 行 void sync();           ← 立刻写盘；写完要再读 status() 才知道成没成
//   第 117 行 Status status() const; 第 118/119 行 isAtomicSyncRequired()/setAtomicSyncRequired()
//   第 158/159 行 setFallbacksEnabled()/fallbacksEnabled()
//   第 161 行 QString fileName() const;   第 162 行 Format format() const;
//   第 163 行 Scope scope() const;        第 164/165 行 organizationName()/applicationName()
//   静态工具（第 167~169 行）：
//   第 167/168 行 static void setDefaultFormat(Format) / static Format defaultFormat();
//   第 169 行 static void setPath(Format format, Scope scope, const QString &path);
//                    ↑ **本讲靠它把 IniFormat/UserScope 改道到临时目录**，
//                      这是「不污染用户目录」的关键一招。必须在构造 QSettings 之前调用。
//   第 171 行 typedef QMap<QString, QVariant> SettingsMap;  第 175 行 registerFormat(...)
//                    ← 想自定义文件格式（比如把配置塞进二进制里）走这两个，本讲不展开。
//
// QCoreApplication 那一侧（QtCore/qcoreapplication.h）：
//   第 84 行 static void setOrganizationName(const QString &orgName);
//   第 85 行 static QString organizationName();
//   第 86 行 static void setApplicationName(const QString &application);
//   第 87 行 static QString applicationName();
//   第 88 行 static void setApplicationVersion(const QString &version);
//   第 205 行 static void quit();
//   ← 它们都是 **static**，所以和 QApplication 的构造顺序无关，只和「谁先执行」有关。
//     QApplication 自己**不填**这两个名字：默认都是空串，得你自己 set。
//
// 事件类型区间（QtCore/qcoreevent.h，步骤⑧ 的 eventFilter 要用）：
//   第 56 行 enum Type { … }；第 102 行 MetaCall = 43；
//   第 300 行 User = 1000（第一个自定义事件号）；第 301 行 MaxUser = 65535（最后一个）。
//   ★ 本机 Qt 6.12 里**没有** QEvent::MaximumType 这个枚举值，只有 QEvent::MaxUser。
//     老资料/老代码里写 `QEvent::MaximumType` 的，在这份头文件上直接编不过（本讲踩过，
//     已改成 MaxUser）；判断「是不是自定义事件」请用 User..MaxUser 这个闭区间。
// QVariant 那一侧（QtCore/qvariant.h）：
//   第 573 行 int typeId() const；第 587 行 QMetaType metaType() const（.name() 给可读名字）
//   第 605 行 bool isValid() const；第 606 行 bool isNull() const
//   第 613 行 int toInt(bool *ok = nullptr) const     ← **带 ok 出参**，别只看返回值
//   第 617 行 bool toBool() const；第 618 行 double toDouble(bool *ok = nullptr) const
//   第 623 行 QString toString() const；第 624 行 QStringList toStringList() const
//   第 598/601 行 canConvert(int targetTypeId) / convert(int)（Qt6 主推 QMetaType 那两份，
//                 第 589/591 行）——判断「能不能转」请用 canConvert，别靠试 toXxx 猜。
// Qt6 相对 Qt5 的改名/删除，本讲避开的：
//   · QVariant::type() 返回 QVariant::Type 的那份在 Qt6 已弃用 → 用 typeId()/metaType()
//     （第 676 行附近能看到 QT_DEPRECATED_VERSION_X_6_0("Use typeId() or metaType().")）。
//   · QSettings::status() 的枚举名 Qt6 仍是 Status{NoError,AccessError,FormatError}，没改。
//   · beginGroup/value/setValue/remove/contains 的参数在 Qt6 后期变成 QAnyStringView，
//     传 QString 照样能编（隐式转换），但**传裸 char* 要看 QT_NO_CAST_FROM_ASCII**。
// ============================================================================

// ============================================================================
// 二、SettingsProbe：把「QSettings 到底会不会发通知」这件事实测出来
// ============================================================================
// eventFilter 是虚函数，本类不需要信号槽，所以**不写 Q_OBJECT**（少一份元对象负担）。
// 它挂在 QSettings 对象上，把收到的事件类型计数记录下来。
class SettingsProbe : public QObject
{
public:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        Q_UNUSED(watched)
        ++total;
        const int t = int(event->type());     // qcoreevent.h：int QEvent::type() const
        hits[QEvent::Type(t)] += 1;
        if (t >= int(QEvent::User) && t <= int(QEvent::MaxUser))
            ++userRangeHits;                  // 私有事件多半落在 User 这一段
        return false;                         // **不吃掉事件**，交回 QSettings 自己处理
    }

    QString histogram() const
    {
        if (total == 0)
            return QStringLiteral("（一个事件都没收到）");
        QStringList parts;
        for (auto it = hits.cbegin(); it != hits.cend(); ++it)
            parts << QStringLiteral("%1:%2").arg(int(it.key())).arg(it.value());
        return parts.join(QStringLiteral("  "));
    }

    int total = 0;
    int userRangeHits = 0;
    QHash<int, int> hits;                     // QEvent::type() 的数值 → 次数
};

// ============================================================================
// 三、ConfigStore：QSettings 没有信号，那就自己包一层（这是本讲的正解）
// ============================================================================
// 规矩：**业务代码永远不许直接摸 QSettings**，一律经过这个门。
// 于是「配置变了」这件事变成我们自己发的信号，而不是指望 QSettings。
class ConfigStore : public QObject
{
    Q_OBJECT

public:
    explicit ConfigStore(QSettings *settings, QObject *parent = nullptr)
        : QObject(parent), m_settings(settings)
    {
    }

    // 写：值真的变了才发信号（避免「写同一个值 → 界面重画 100 次」）
    void set(const QString &key, const QVariant &value)
    {
        const QVariant oldValue = m_settings->value(key);
        const bool sameKind = oldValue.userType() == value.userType();
        const bool same = sameKind && oldValue == value;
        m_settings->setValue(key, value);
        ++m_writeCount;
        if (!same)
            emit changed(key);                // ← 变更通知是**我们自己**发的
    }

    QVariant get(const QString &key, const QVariant &fallback) const
    {
        return m_settings->value(key, fallback);
    }

    // 一次性刷盘；QSettings 自己不报错，得回来读 status()
    bool flush()
    {
        m_settings->sync();
        m_syncCount += 1;
        return m_settings->status() == QSettings::NoError;
    }

    int writeCount() const { return m_writeCount; }
    int syncCount() const { return m_syncCount; }

Q_SIGNALS:
    void changed(const QString &key);

private:
    QSettings *m_settings = nullptr;          // 不持有所有权：外面 new 的，本讲活到 main 结束
    int m_writeCount = 0;
    int m_syncCount = 0;
};

// ============================================================================
// 四、小工具
// ============================================================================
static QString vdesc(const QVariant &v)
{
    // 把「一个 QVariant 到底是什么类型」打印成人类可读：类型名 + typeId + 值
    if (!v.isValid())
        return QStringLiteral("<无效 QVariant（isValid()=false）>");
    return QStringLiteral("%1(id=%2) = %3")
        .arg(QString::fromUtf8(v.metaType().name()))
        .arg(v.typeId())
        .arg(v.toString());
}

static QString statusName(QSettings::Status st)
{
    switch (st) {
    case QSettings::NoError:     return QStringLiteral("NoError");
    case QSettings::AccessError: return QStringLiteral("AccessError");
    case QSettings::FormatError: return QStringLiteral("FormatError");
    }
    return QStringLiteral("未知(%1)").arg(int(st));   // 待复核：三个值以外的情况
}

// 直接读磁盘上的原文（绕过 QSettings），用来证明 sync 前后落盘与否
static QByteArray readRaw(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();                  // 打不开就返回空，调用方按字节数判断
    QByteArray data = f.readAll();
    f.close();
    return data;
}

static QString humanPath(const QString &p)
{
    return QDir::toNativeSeparators(p);
}

// ============================================================================
// 五、main：九步自动演示
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // ── 落盘约定：一切写进行程临时目录下的自己的小目录 ─────────────────
    const QString tmpRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString workDir = QDir(tmpRoot).filePath(QStringLiteral("qt_storage_01"));
    QDir().mkpath(workDir);
    const QString mainIni = QDir(workDir).filePath(QStringLiteral("app.ini"));
    const QString exportIni = QDir(workDir).filePath(QStringLiteral("backup.ini"));

    QTimer::singleShot(0, [&workDir, &mainIni, &exportIni]() {
        qInfo() << "============================================================";
        qInfo() << "storage 第 1 讲 · QSettings（Qt =" << QT_VERSION_STR << "）";
        qInfo() << "工作目录 =" << humanPath(workDir);
        qInfo() << "============================================================";

        // ────────────────────────────────────────────────────────────
        // 步骤① 名字必须在构造之前定：QSettings 拿的是构造那一刻的快照
        // ────────────────────────────────────────────────────────────
        qInfo() << "\n【步骤①】组织名/应用名 = 构造期快照";
        // 先把格式与路径钉死在临时目录，保证后面任何一个 QSettings 都不会碰注册表。
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, workDir);
        qInfo() << "  已执行 setDefaultFormat(IniFormat) 与 setPath(IniFormat,UserScope,临时目录)";
        qInfo() << "  defaultFormat() 现在 =" << int(QSettings::defaultFormat())
                << "（0=NativeFormat, 1=IniFormat, 2/3=Registry*, 16=InvalidFormat）";
        qInfo() << "  此刻全局名字：organizationName =[" << QCoreApplication::organizationName()
                << "] applicationName =[" << QCoreApplication::applicationName() << "]";

        // 用显式名字构造：它和后面的全局改名**无关**
        auto *snapA = new QSettings(QStringLiteral("QtCourseOrg"), QStringLiteral("SnapA"));
        const QString snapAFileBefore = snapA->fileName();
        qInfo() << "  先构造 snapA(QSettings(\"QtCourseOrg\",\"SnapA\"))：fileName =["
                << humanPath(snapA->fileName()) << "]";

        // 现在才改全局名字
        QCoreApplication::setOrganizationName(QStringLiteral("QtCourseOrg"));
        QCoreApplication::setApplicationName(QStringLiteral("MyApp"));
        qInfo() << "  改完全局名字：organizationName =[" << QCoreApplication::organizationName()
                << "] applicationName =[" << QCoreApplication::applicationName() << "]";
        qInfo() << "  snapA 的 fileName 变了吗？还是 [" << humanPath(snapA->fileName())
                << "] ← **没变**：组织/应用名在构造时就抄进对象里了。";

        auto *snapB = new QSettings();      // 默认构造：读全局名字（此刻是 MyApp）
        qInfo() << "  之后默认构造的 snapB：fileName =[" << humanPath(snapB->fileName())
                << "] org =" << snapB->organizationName() << "app =" << snapB->applicationName()
                << "→ 这才是「谁先 set 谁后构造」的区别：**先定名字，再 new QSettings**。";
        checkBool("snapA 的 fileName 在全局改名后不变", snapA->fileName() == snapAFileBefore);
        checkBool("snapB 抓到的是改名后的组织名",
                  snapB->organizationName() == QStringLiteral("QtCourseOrg"));
        checkBool("snapB 抓到的是改名后的应用名",
                  snapB->applicationName() == QStringLiteral("MyApp"));
        qInfo() << "  反面教材（本讲不实测，标 待复核）：如果在 setOrganizationName 之前"
                   "就默认构造 QSettings，Qt 会用一个「Unknown Organization」的兜底名并打警告，"
                   "路径也跟着错——这是「配置莫名其妙丢了」的头号原因。";

        // ────────────────────────────────────────────────────────────
        // 步骤② 三种构造形态的落盘位置对照
        // ────────────────────────────────────────────────────────────
        qInfo() << "\n【步骤②】三组构造 × 三种作用域：落点各在哪";
        auto *sFs = new QSettings(QSettings::IniFormat, QSettings::UserScope,
                                  QStringLiteral("QtCourseOrg"), QStringLiteral("MyApp"));
        qInfo() << "  (a) QSettings(Format,Scope,org,app)：fileName =[" << humanPath(sFs->fileName())
                << "] format =" << int(sFs->format()) << " scope =" << int(sFs->scope())
                << " isWritable =" << sFs->isWritable()
                << "← 位置是被 setPath(IniFormat,UserScope) 改道过的临时目录";

        auto *sFile = new QSettings(mainIni, QSettings::IniFormat);
        qInfo() << "  (b) QSettings(fileName, Format)     ：fileName =[" << humanPath(sFile->fileName())
                << "] format =" << int(sFile->format()) << " scope =" << int(sFile->scope())
                << " isWritable =" << sFile->isWritable()
                << "← **本讲的主战场**：路径你说了算，导出/便携版就用这条";
        qInfo() << "     它的 scope() 仍然是" << int(sFile->scope())
                << "（0=UserScope）：构造时传了路径，scope 只是个标签，别拿它当落点依据。";

        auto *sOrg = new QSettings(QStringLiteral("QtCourseOrg"), QStringLiteral("MyApp"));
        qInfo() << "  (c) QSettings(org, app)             ：fileName =[" << humanPath(sOrg->fileName())
                << "] format =" << int(sOrg->format())
                << "｜此刻 defaultFormat() =" << int(QSettings::defaultFormat())
                << " ← ★ 它**不**跟随 defaultFormat：这条构造在 qsettings.cpp 第 2536~2540 行"
                   "把格式**写死成 NativeFormat**（=0，所以 fileName 是注册表路径），"
                   "文档第 2531 行原话：calling setDefaultFormat() before calling this constructor "
                   "has no effect。同理 QSettings(Scope,org,app)（第 2561~2563 行）也写死 NativeFormat；"
                   "真正吃 defaultFormat() 的只有默认构造/QSettings(Scope, parent) 那两条（第 2661/2674 行）。";
        check("sFs 用 IniFormat", int(sFs->format()), int(QSettings::IniFormat));
        check("sFs 是 UserScope", int(sFs->scope()), int(QSettings::UserScope));
        check("sFile 用 IniFormat", int(sFile->format()), int(QSettings::IniFormat));
        // ★ 原预测「sOrg 跟随 defaultFormat」错在把这条构造想成了 (Format,Scope,org,app) 的糖；
        //   实测 format=NativeFormat(0)，而此刻 defaultFormat()=IniFormat(1)：两条硬账分开打。
        check("sOrg(org,app) 恒为 NativeFormat（.cpp 第 2537 行写死）",
              int(sOrg->format()), int(QSettings::NativeFormat));
        checkBool("sOrg 与 defaultFormat() 不一致 → setDefaultFormat 对它无效",
                  int(sOrg->format()) != int(QSettings::defaultFormat()));

        qInfo() << "  (d) NativeFormat 在 Windows 上 = 注册表 HKEY_CURRENT_USER\\Software\\<org>\\<app>；"
                   "在 Linux/macOS 上 = ini/XDG。**本讲不构造写它的对象**，免得真写注册表。";
        qInfo() << "     SystemScope 需要管理员权限，普通安装请用 UserScope。";

        // ────────────────────────────────────────────────────────────
        // 步骤③ 写入若干类型，逐项读回并核对类型
        // ────────────────────────────────────────────────────────────
        qInfo() << "\n【步骤③】写六种类型 → 读回，看类型名变没变";
        sFile->setValue(QStringLiteral("窗口/宽度"), 1280);                     // int
        sFile->setValue(QStringLiteral("窗口/透明度"), 0.85);                   // double
        sFile->setValue(QStringLiteral("界面/深色模式"), true);                 // bool
        sFile->setValue(QStringLiteral("界面/语言"), QStringLiteral("zh_CN"));  // QString
        sFile->setValue(QStringLiteral("界面/最近目录"),
                        QStringList{ QStringLiteral("D:/工程甲"),
                                     QStringLiteral("E:/资料/手册"),
                                     QStringLiteral("临时") });                // QStringList
        sFile->setValue(QStringLiteral("服务/端口"), 18080);
        sFile->sync();
        qInfo() << "  写完并 sync()：status =" << statusName(sFile->status());

        const QStringList probeKeys {
            QStringLiteral("窗口/宽度"), QStringLiteral("窗口/透明度"),
            QStringLiteral("界面/深色模式"), QStringLiteral("界面/语言"),
            QStringLiteral("界面/最近目录"), QStringLiteral("服务/端口")
        };
        for (const QString &k : probeKeys) {
            const QVariant v = sFile->value(k);
            qInfo() << QStringLiteral("  %1 → %2").arg(k, -14) << vdesc(v);
        }
        qInfo() << "  ★ 重点看「窗口/透明度」：ini 里存的是文本，读回来却是 "
                   "QVariant(double)——**QSettings 按格式自己做了类型推断**，"
                   "但这套推断只保证「它自己写出去的文本」，不保证你手改的 ini。";
        check("窗口/宽度 读回 typeId = Int",
              sFile->value(QStringLiteral("窗口/宽度")).typeId(), QMetaType::Int);
        check("窗口/透明度 读回 typeId = Double",
              sFile->value(QStringLiteral("窗口/透明度")).typeId(), QMetaType::Double);
        check("界面/深色模式 读回 typeId = Bool",
              sFile->value(QStringLiteral("界面/深色模式")).typeId(), QMetaType::Bool);
        check("界面/语言 读回 typeId = QString",
              sFile->value(QStringLiteral("界面/语言")).typeId(), QMetaType::QString);

        // ────────────────────────────────────────────────────────────
        // 步骤④ 存的类型 ≠ 读的类型：Qt6 的 QSettings **不做转换**，默认值只在缺键时出场
        // ────────────────────────────────────────────────────────────
        qInfo() << "\n【步骤④】类型不一致时到底发生什么";
        sFile->setValue(QStringLiteral("实验/字符串数字"), QStringLiteral("42"));
        sFile->setValue(QStringLiteral("实验/字符串乱码"), QStringLiteral("abc"));
        sFile->setValue(QStringLiteral("实验/整数"), 42);
        sFile->sync();

        const QVariant asStr = sFile->value(QStringLiteral("实验/整数"),
                                            QStringLiteral("<默认串>"));
        qInfo() << "  存 int 42，用 QString 默认值去读 →" << vdesc(asStr)
                << "（★ 返回的还是**存的那个 int**，QSettings 没转成 QString；"
                   "上面那句 \"42\" 是代码里 toString() 转的。默认值更没出场——它只在**键不存在**时才回来）";

        const QVariant asIntOk = sFile->value(QStringLiteral("实验/字符串数字"), 7);
        qInfo() << "  存 \"42\"，用 int 默认值 7 去读 →" << vdesc(asIntOk)
                << "（同上：给的还是存的 QString，7 没用上；toInt() 是 QVariant 自己在转）";

        const QVariant asIntBad = sFile->value(QStringLiteral("实验/字符串乱码"), 7);
        bool okToInt = false;
        const int rawToInt = sFile->value(QStringLiteral("实验/字符串乱码")).toInt(&okToInt);
        qInfo() << "  存 \"abc\"，用 int 默认值 7 去读 →" << vdesc(asIntBad)
                << "← ★ 拿到的**不是 7**，而是存着的那串 \"abc\"：Qt6 的 value() 里根本没有"
                   "「按默认值的类型去 convert」这一步（qsettings.cpp 第 3361~3372 行：命中键就 "
                   "原样 return，只有取不到才 return *defaultValue）。"
                   "默认值掩盖不了任何事，因为它压根没参与。";
        qInfo() << "     再 toInt() 才是 0 —— 静默失败发生在你自己调的 QVariant::toInt() 上，"
                   "不是 QSettings 上（原预测「给默认值 7」是把 Qt5 的 requiredType/convert 当成"
                   "现在的行为了，那份签名在 Qt6 已整段删除）。";
        qInfo() << "     同一份数据单参读回来再 toInt(&ok)：值 =" << rawToInt
                << " ok =" << okToInt
                << "（第 613 行的 bool* 出参才是「转换成没成」的真相，**默认值掩盖它**）";
        checkBool("int 42 用 QString 默认值读回 \"42\"",
                  asStr.toString() == QStringLiteral("42"));
        checkBool("★ 存 int 读出来仍是 int（QSettings 不转换）",
                  asStr.typeId() == QMetaType::Int);
        bool okGood = false;
        const int goodInt = sFile->value(QStringLiteral("实验/字符串数字")).toInt(&okGood);
        check("字符串 \"42\" 用 int 默认值读回 42", asIntOk.toInt(), 42);
        // ★ 原先这条预测的是 7（Qt5 语义：默认值兼作期望类型，转不动就回默认值）。
        //   Qt 6.12 实测：默认值不参与，返回存的 QString；toInt() 自己转不动 → 0。
        check("字符串 \"abc\" 读回来仍是存的 QString（默认值 7 没出场）",
              asIntBad.typeId(), QMetaType::QString);
        check("字符串 \"abc\" 转不动 → toInt() 默默给 0（**不是** 7）", asIntBad.toInt(), 0);
        check("裸 toInt(&ok) 对 \"42\" 给出 42", goodInt, 42);
        checkBool("裸 toInt(&ok) 对 \"42\" 的 ok 为 true", okGood);
        check("裸 toInt(&ok) 对 \"abc\" 给 0", rawToInt, 0);
        checkBool("裸 toInt(&ok) 对 \"abc\" 的 ok 为 false", okToInt, false);

        const QVariant asList = sFile->value(QStringLiteral("实验/整数"));
        qInfo() << "  存 int 42，toStringList() 硬转 →" << asList.toStringList()
                << "｜canConvert(QStringList) =" << asList.canConvert<QStringList>()
                << "（想读列表先 canConvert，别 toXxx 猜）";
        qInfo() << "  → 纪律：**每个配置项固定一种读法**（写 int 就永远按 int 读），"
                   "并把默认值的类型写成和存储类型一致。★ 但别再指望 QSettings 替你兜底："
                   "Qt6 的 value() 不做类型转换，键在就原样给你存的 QVariant，"
                   "「看起来合理」的 0 / 空串是**你自己那句 toInt()/toString() 造出来的**，"
                   "一句错误都不报——这才是最难查的那类 bug。";

        // ────────────────────────────────────────────────────────────
        // 步骤⑤ 缺失项：单参 vs 双参 value()
        // ────────────────────────────────────────────────────────────
        qInfo() << "\n【步骤⑤】配置里没有这一项的时候";
        const QString ghost = QStringLiteral("界面/一个从没写过的键");
        const QVariant oneArg = sFile->value(ghost);
        const QVariant twoArg = sFile->value(ghost, 96);
        qInfo() << "  contains(ghost) =" << sFile->contains(ghost)
                << "｜value(ghost) 单参版 →" << vdesc(oneArg)
                << "｜value(ghost,96) 双参版 →" << vdesc(twoArg);
        qInfo() << "  所以「用户从没设过 → 走首次运行引导」这种判断，"
                   "要用**单参版 + isValid()**，或者 contains()；用双参版你永远分不清"
                   "「是存着的默认值」还是「我给的兜底值」。";
        checkBool("缺失键 contains 为 false", sFile->contains(ghost), false);
        checkBool("缺失键单参 value 无效", oneArg.isValid(), false);
        check("缺失键双参 value 拿到兜底 96", twoArg.toInt(), 96);

        // ────────────────────────────────────────────────────────────
        // 步骤⑥ 层级：/ 、beginGroup/endGroup、group/allKeys/childKeys/childGroups
        // ────────────────────────────────────────────────────────────
        qInfo() << "\n【步骤⑥】层级 key 与 group";
        sFile->beginGroup(QStringLiteral("窗口"));
        checkBool("beginGroup 后 group 为「窗口」",
                  sFile->group() == QStringLiteral("窗口"));
        qInfo() << "  beginGroup(\"窗口\") 之后 group() =[" << sFile->group() << "]";
        sFile->setValue(QStringLiteral("宽度"), 1920);       // ← 等价于写 "窗口/宽度"
        sFile->beginGroup(QStringLiteral("停靠"));
        checkBool("嵌套 beginGroup 后 group 为「窗口/停靠」",
                  sFile->group() == QStringLiteral("窗口/停靠"));
        qInfo() << "  嵌套 beginGroup(\"停靠\") 之后 group() =[" << sFile->group()
                << "] ← **group 是会叠加的**，所以这里是 窗口/停靠";
        sFile->setValue(QStringLiteral("左"), true);
        sFile->endGroup();
        const QString groupAfterFirstEnd = sFile->group();
        qInfo() << "  endGroup() 回到 group() =[" << groupAfterFirstEnd << "]";
        sFile->endGroup();
        const QString groupAfterSecondEnd = sFile->group();
        qInfo() << "  再 endGroup() 回到 group() =[" << groupAfterSecondEnd
                << "]（空串 = 根）";
        sFile->sync();
        checkBool("第一次 endGroup 回到「窗口」", groupAfterFirstEnd == QStringLiteral("窗口"));
        checkBool("第二次 endGroup 回到根（空串）", groupAfterSecondEnd.isEmpty());

        const int widthInWindow = sFile->value(QStringLiteral("窗口/宽度")).toInt();
        qInfo() << "  「窗口/宽度」现在读回来 =" << widthInWindow
                << "← beginGroup 里写的「宽度」和根上写的「窗口/宽度」是**同一个键**";
        check("group 里写的宽度覆盖了根上那个 1280", widthInWindow, 1920);

        const QStringList rootGroups = sFile->childGroups();
        qInfo() << "  根上 childGroups() =" << rootGroups;
        checkBool("根上 childGroups 含「窗口」", rootGroups.contains(QStringLiteral("窗口")));

        sFile->beginGroup(QStringLiteral("窗口"));
        const QStringList winKeys = sFile->childKeys();
        const QStringList winGroups = sFile->childGroups();
        qInfo() << "  进到「窗口」里：childKeys() =" << winKeys
                << "｜childGroups() =" << winGroups;
        sFile->endGroup();
        checkBool("「窗口」的 childKeys 含 宽度", winKeys.contains(QStringLiteral("宽度")));
        checkBool("「窗口」的 childGroups 含 停靠", winGroups.contains(QStringLiteral("停靠")));

        const QStringList all = sFile->allKeys();
        qInfo() << "  allKeys() 共" << all.size() << "项：";
        for (const QString &k : all)
            qInfo() << "     " << k;
        checkBool("allKeys 含 窗口/宽度", all.contains(QStringLiteral("窗口/宽度")));
        checkBool("allKeys 含 窗口/停靠/左", all.contains(QStringLiteral("窗口/停靠/左")));
        checkBool("allKeys 含 界面/语言", all.contains(QStringLiteral("界面/语言")));
        checkBool("allKeys 不含 beginGroup 里的短名「宽度」",
                  all.contains(QStringLiteral("宽度")), false);
        qInfo() << "  ★ allKeys() 永远给你**完整的、带 / 的键**，不受当前 group 影响；"
                   "childKeys/childGroups 才看当前 group。begin/end 必须配对——"
                   "中间提前 return 就把 group 漏在里层了，用 RAII 包一层最稳。";

        // ────────────────────────────────────────────────────────────
        // 步骤⑦ sync 的时机：改内存 vs 落磁盘
        // ────────────────────────────────────────────────────────────
        qInfo() << "\n【步骤⑦】sync() 之前磁盘上是什么";
        // 键名故意用 ASCII：ini 里非 ASCII 的组名/键名可能被写成 \x 转义，
        // 拿中文去原文里 grep 会「明明写了却搜不到」，那是比对方法的坑，不是没落盘。
        const QString probeKey = QStringLiteral("probe/only_in_memory");
        const qint64 sizeBefore = QFileInfo(mainIni).size();
        sFile->setValue(probeKey, 1);
        const qint64 sizeAfterWrite = QFileInfo(mainIni).size();
        const QByteArray rawAfterWrite = readRaw(mainIni);
        const QByteArray markerBytes = QStringLiteral("only_in_memory").toUtf8();
        sFile->sync();
        const QByteArray rawAfterSync = readRaw(mainIni);
        qInfo() << QString::asprintf("  磁盘字节数：开讲时 %lld → 写了新键之后 %lld → sync() 之后 %lld",
                                     static_cast<long long>(sizeBefore),
                                     static_cast<long long>(sizeAfterWrite),
                                     static_cast<long long>(rawAfterSync.size()));
        qInfo() << "  文件原文里搜 only_in_memory：sync 前 ="
                << (rawAfterWrite.contains(markerBytes) ? QStringLiteral("搜到") : QStringLiteral("没搜到"))
                << "｜sync 后 ="
                << (rawAfterSync.contains(markerBytes) ? QStringLiteral("搜到") : QStringLiteral("没搜到"))
                << "（readAll 共" << rawAfterSync.size() << "字节）";
        qInfo() << "  内存侧永远立刻可见：value(probe/only_in_memory) ="
                << sFile->value(probeKey).toInt()
                << "→ 写进去就能读，磁盘上要等 sync()";
        checkBool("sync 前磁盘原文搜不到 only_in_memory",
                  rawAfterWrite.contains(markerBytes), false);
        checkBool("sync 后磁盘原文出现 only_in_memory", rawAfterSync.contains(markerBytes));
        checkBool("sync 前文件大小没被改动（证明“择机刷盘”）",
                  sizeAfterWrite == sizeBefore);
        {
            QSettings reread(mainIni, QSettings::IniFormat);   // 另开一个对象，只认磁盘
            check("另开的 QSettings 从磁盘读回 probe 键", reread.value(probeKey).toInt(), 1);
        }
        qInfo() << "  isAtomicSyncRequired =" << sFile->isAtomicSyncRequired()
                << "（第 118 行；true 时 sync 走「写临时文件再改名」，**断电不容易写坏**）";
        qInfo() << "  → 三条时机：① 用户点「应用」→ 立刻 sync()；② 程序退出前 → sync()；"
                   "③ 崩溃也要保住的东西 → setAtomicSyncRequired(true) + 频繁 sync()。"
                   "别指望「对象析构时自动刷」——它只在**正常析构**时刷。";

        // ────────────────────────────────────────────────────────────
        // 步骤⑧ 变更通知：实测 eventFilter 收到什么 + 自己包一层
        // ────────────────────────────────────────────────────────────
        qInfo() << "\n【步骤⑧】QSettings 会不会告诉你「配置变了」？";
        auto *probe = new SettingsProbe();
        sFile->installEventFilter(probe);
        sFile->setValue(QStringLiteral("通知测试"), 1);
        sFile->setValue(QStringLiteral("通知测试"), 2);
        sFile->sync();
        QCoreApplication::processEvents();
        qInfo() << "  在 QSettings 上装了 eventFilter，做了 2 次 setValue + 1 次 sync 之后："
                << "收到事件总数 =" << probe->total
                << "｜其中 User 段（QEvent::User=1000 .. QEvent::MaxUser=65535）=" << probe->userRangeHits;
        qInfo() << "  事件类型分布（type 枚举值:次数）：" << probe->histogram();
        qInfo() << "  结论（保守）：qsettings.h 里**没有任何 Q_SIGNALS**，第 180 行只有 "
                   "protected event()。老资料里的 QSettingsEvent 在本机 6.12 查无此名："
                   "qsettings.cpp 全文 grep 0 处，真实机制是第 330 行 postEvent 一个普通的 "
                   "QEvent(QEvent::UpdateRequest)、再由第 3320 行 event() 自己接住 —— "
                   "公共头文件里既没有它的类型值也没有它的声明，所以**不能当成可依赖的 API**"
                   "（本讲实测到的分布只是佐证，别写成产品逻辑）。";
        qInfo() << "     → 真要变更通知，两条正路：① **自己包一层 setter**（下面的 ConfigStore，"
                   "值真变了才 emit）；② 轮询：定期把 allKeys()+value() 拼个串比哈希。"
                   "②有延迟、①要求所有写入都走你的门——这就是纪律，不是技术。";
        sFile->removeEventFilter(probe);

        auto *owned = new QSettings(mainIni, QSettings::IniFormat);
        auto *store = new ConfigStore(owned);
        int changedCount = 0;
        QObject::connect(store, &ConfigStore::changed, [&changedCount](const QString &k) {
            ++changedCount;
            qInfo() << "     [ConfigStore::changed] 键 =[" << k << "] 真的变了";
        });
        store->set(QStringLiteral("服务/端口"), 18080);      // 与磁盘上现值相同 → 不该发信号
        store->set(QStringLiteral("服务/端口"), 19000);      // 变了 → 发一次
        store->set(QStringLiteral("界面/语言"), QStringLiteral("en_US"));
        const int writesBeforeFlush = store->writeCount();
        const int syncsBeforeFlush = store->syncCount();
        qInfo() << "  ConfigStore：写了" << writesBeforeFlush << "次、sync 了"
                << syncsBeforeFlush << "次，changed 响了" << changedCount << "次";
        const bool flushed = store->flush();
        qInfo() << "  flush() 返回 =" << flushed << "｜status =" << statusName(owned->status());
        check("写同值不发信号，两次真变化 → changed 共 2 次", changedCount, 2);
        check("ConfigStore::set 调了 3 次", writesBeforeFlush, 3);
        check("flush 之前没有隐式 sync", syncsBeforeFlush, 0);
        checkBool("flush() 成功", flushed);
        check("flush 之后 syncCount = 1", store->syncCount(), 1);

        // ────────────────────────────────────────────────────────────
        // 步骤⑨ 导出 / 导入：两种做法各有边界
        // ────────────────────────────────────────────────────────────
        qInfo() << "\n【步骤⑨】导出配置、导入配置";
        // 做法一：键级搬运（可控、可过滤、可加版本号；跨 Format 也能搬）
        QSettings *src = sFile;
        auto *dst = new QSettings(exportIni, QSettings::IniFormat);
        const QStringList keys = src->allKeys();
        int copied = 0;
        for (const QString &k : keys) {
            dst->setValue(k, src->value(k));
            ++copied;
        }
        dst->setValue(QStringLiteral("元信息/导出时间戳版本"), 1);
        dst->sync();
        src->sync();
        qInfo() << "  做法一（键级搬运）：搬了" << copied << "个键 →"
                << humanPath(dst->fileName()) << "｜字节数 =" << QFileInfo(dst->fileName()).size();

        // 做法二：整份文件直接拷（原样、含注释？不——QSettings 不保留注释，这点要讲清）
        QByteArray rawIni = readRaw(src->fileName());
        QFile out(dst->fileName() + QStringLiteral(".copy"));
        const bool opened = out.open(QIODevice::WriteOnly | QIODevice::Truncate);
        const qint64 written = opened ? out.write(rawIni) : -1;
        out.close();
        qInfo() << "  做法二（原文拷贝）：读" << rawIni.size() << "字节 → 写" << written
                << "字节 →" << humanPath(dst->fileName() + QStringLiteral(".copy"))
                << "｜两边字节数相等 =" << (written == rawIni.size());
        checkBool("源 ini 原文非空", !rawIni.isEmpty());
        checkBool("键级搬运的键数 = 源 allKeys 条数", copied == keys.size());
        check("原文拷贝写满源文件字节数", written, rawIni.size());

        // 从导出件读回，逐项核对
        auto *imported = new QSettings(exportIni, QSettings::IniFormat);
        const QStringList inKeys = imported->allKeys();
        int mismatch = 0;
        for (const QString &k : inKeys) {
            if (k == QStringLiteral("元信息/导出时间戳版本"))
                continue;
            if (imported->value(k) != src->value(k))
                ++mismatch;
        }
        qInfo() << "  导入核对：导出件里" << inKeys.size() << "个键，与源不一致的有"
                << mismatch << "个（0 才算过）｜imported->status() ="
                << statusName(imported->status());
        check("导出件键数 = 源键数 + 1（多了元信息）", inKeys.size(), keys.size() + 1);
        check("导出件与源逐键比对，不一致数为 0", mismatch, 0);

        // remove / clear 的边界
        const int keysBeforeClear = imported->allKeys().size();
        imported->remove(QStringLiteral("服务/端口"));
        const bool stillThere = imported->contains(QStringLiteral("服务/端口"));
        const int keysAfterRemove = imported->allKeys().size();
        qInfo() << "  remove(\"服务/端口\") 之后 contains =" << stillThere
                << "｜clear() 之前 allKeys =" << keysAfterRemove;
        imported->clear();
        imported->sync();
        const int keysAfterClear = imported->allKeys().size();
        qInfo() << "  clear() 之后 allKeys =" << keysAfterClear
                << "｜status =" << statusName(imported->status())
                << "｜isWritable =" << imported->isWritable()
                << "← clear 是「清空这个对象能看到的整棵树」，**不是清当前 group**；"
                   "用户点「恢复默认」想清干净就得用它，但得先问用户一句。";
        checkBool("remove 之后 contains 为 false", stillThere, false);
        check("remove 只少一个键", keysAfterRemove, keysBeforeClear - 1);
        check("clear 之后 allKeys 为 0", keysAfterClear, 0);

        // ────────────────────────────────────────────────────────────
        // 收尾：清理临时目录
        // ────────────────────────────────────────────────────────────
        delete probe;
        delete store;
        delete imported;
        delete dst;
        delete owned;
        delete snapA;
        delete snapB;
        delete sOrg;
        delete sFs;
        delete sFile;                  // 析构也会 sync 一次；上面已经显式 sync 过了

        const QStringList filesLeft = QDir(workDir).entryList(QDir::Files);
        QDir cleanup(workDir);
        const bool removed = cleanup.removeRecursively();
        const bool dirStillThere = QDir(workDir).exists();
        qInfo() << "\n【清理】removeRecursively() =" << removed
                << "｜目录还在吗 =" << dirStillThere
                << "（临时目录里删掉" << filesLeft.size() << "个文件：" << filesLeft << "）";
        checkBool("清理前临时目录里有文件", !filesLeft.isEmpty());
        checkBool("removeRecursively() 成功", removed);
        checkBool("清理后目录不存在", dirStillThere, false);

        qInfo() << "\n【本讲可核对的证据】";
        qInfo() << "  ① snapA 的 fileName 在全局改名后不变、snapB 用新名字 → 构造期快照";
        qInfo() << "  ② 三种构造的 fileName/format/scope 已逐项打出（IniFormat=1, UserScope=0）";
        qInfo() << "  ③ 六个类型写回读回的类型名与值一致；\"abc\" 读成 int 时 Qt6 **不给默认值 7**，"
                   "给的是存的 QString，toInt() 之后才是 0（默认值只在缺键时出场）";
        qInfo() << "  ④ allKeys() 的条数与层级前缀、childKeys/childGroups 的分工已对照";
        qInfo() << "  ⑤ sync 前后磁盘字节数、eventFilter 事件计数、导出件字节数、清理结果";
        qInfo().noquote() << QStringLiteral("\n【硬账】%1/%2 项核对通过 → 退出码 %3")
                                 .arg(Ledger::passed)
                                 .arg(Ledger::total)
                                 .arg(Ledger::exitCode());
        QCoreApplication::exit(Ledger::exitCode());
    });

    return app.exec();
}

// ============================================================================
// 附：QSettings 速查与纪律
// ============================================================================
/*
 * 一、构造函数怎么选（本机 qsettings.h 第 94~102 行）
 *   需求                              选哪条                              为什么
 *   正常桌面应用（跟着安装走）        QSettings() 默认构造 + 先 set 名字   简单，位置由平台决定
 *   想区分「本机/系统级」             QSettings(Scope, org, app)          第 102 行的 Scope 版
 *   强制 ini（Linux 上也想要 ini）    QSettings(IniFormat, Scope, ...)    第 98 行
 *   便携版 / 配置放程序目录 / 导出件  QSettings(fileName, IniFormat)      第 100 行，**唯一路径可控**
 *   要把配置塞进自定义二进制/加密     registerFormat + SettingsMap        第 171~175 行
 *   两条纪律：
 *   ① 第 100 行的 Format **没有默认值**，`QSettings s("x.ini")` 编不过（Qt6 里它是必填）。
 *   ② 一旦用 fileName 版，`scope()/organizationName()/applicationName()` 只是标签，
 *      别拿它们反推落盘位置——只有 fileName() 是真的。
 *
 * 二、层级 key 的写法对照（三种写法等价）
 *   setValue("窗口/停靠/左", true)
 *   beginGroup("窗口"); beginGroup("停靠"); setValue("左", true); endGroup(); endGroup();
 *   beginGroup("窗口/停靠"); setValue("左", true); endGroup();
 *   注意：
 *   ① **key 里出现的第一个 / 才是分隔符**；组名本身不能含 /（会被切成两层）。
 *   ② allKeys() 给全路径；childKeys() 给当前层叶子；childGroups() 给当前层的下一层目录名。
 *   ③ begin/end 必须配对，中间有 return/异常就把 group 漏在里层——
 *      正经项目请写一个 RAII 小类，析构里 endGroup()。
 *   ④ 数组语法糖 beginWriteArray/beginReadArray + setArrayIndex（第 132~135 行）
 *      等价于 "前缀/0/键"、"前缀/1/键"；beginReadArray 返回元素个数，0 表示没有。
 *
 * 三、value() 的两个重载 = 两套语义（第 148/149 行）
 *   value(key)          找不到 → **无效 QVariant**（isValid()==false）→ 用来判断「有没有存过」
 *   value(key, 默认值)  ★ Qt6：默认值**只在「键不存在」时**返回（qsettings.cpp 第 3361~3372 行）。
 *                        键在就原样返回存的 QVariant，类型也是它原来的类型。
 *   存与读类型不一致：  Qt6 **不再转换**——Qt5 那份 value(key, requiredType, defaultValue)
 *                        里的 convert 步骤已整段删除，所以 int 42 用 QString 默认值读回来
 *                        仍然是 int、"abc" 用 int 默认值读回来仍然是 QString。
 *                        能转（"42"→42）是你自己调 toInt() 的功劳，不能转（"abc"→0）也是。
 *   想知道「转换到底成没成」：单参版读回来 + toInt(&ok) / toDouble(&ok) / canConvert<T>()。
 *
 * 四、什么时候必须 sync()
 *   ① 点了「确定/应用」；② 退出前；③ 写完后立刻要被**另一个进程**读；
 *   ④ 崩溃保护：setAtomicSyncRequired(true)（第 119 行）。
 *   sync() 之后**一定要读 status()**：AccessError（只读介质/权限）与 FormatError
 *   （文件被改坏）都不抛异常、不打崩溃，只有 status() 知道。
 *
 * 五、变更监听（本讲的实测结论）
 *   QSettings **没有信号**（qsettings.h 整份无 Q_SIGNALS）。可选路数只有三条：
 *   ① 自己包一层 setter，值真变了才 emit —— 唯一可靠的，见 ConfigStore；
 *   ② 轮询 allKeys()+value() 拼串比哈希；
 *   ③ eventFilter 接 QEvent::UpdateRequest —— 本机 6.12 的真实机制就这一条：qsettings.cpp
 *      第 330 行 postEvent 一个 **QEvent(UpdateRequest)**，第 3320 行的 event() 自己接住。
 *      既没有私有事件类，也没有公开的专用 QEvent::Type，所以它只告诉你「该重读配置了」，
 *      不告诉你是哪个键变的——**别写进产品代码**，要通知就自己包 setter（见 ConfigStore）。
 *
 * 六、导出/导入的两条路
 *   键级搬运：读 allKeys() → 逐项 setValue 到另一个 QSettings。能过滤、能加版本号、
 *             能跨 Format（registry → ini）。缺点：**ini 里的注释不保留**（QSettings
 *             本来就不存注释）。
 *   原文拷贝：直接搬 ini 字节。快、原样。缺点：只对 ini 形态有效，注册表形态没法这么玩。
 *   配置里带上 schemaVersion（本讲导出件里那个「元信息/导出时间戳版本」就是这个角色），
 *   下次读的时候按版本做迁移，否则改配置结构就只能「用户自己删配置文件」。
 *
 * ——— 老师傅的话 ———
 *   徒弟，配置这块东西，写的时候越方便，出事的时候越难查。我的三条规矩：
 *
 *   第一条：**默认值和读取类型必须绑死**。★ Qt6 里默认值**不再**替你定类型（见速查三），
 *   所以这个绑死只能靠你自己：`value("超时", 0).toInt()` 和 `value("超时", 5000).toInt()`
 *   只差兜底数字，类型全凭你后面调的那个 toXxx()。别嫌烦，我给每一项配置都留一行注释：
 *   键名、类型、默认值、谁写的。三年后接手的人是你自己。
 *
 *   第二条：**程序里不许出现裸的 QSettings**。所有读写走 ConfigStore 这种门。
 *   理由不是洁癖，是步骤⑧ 那条实测：QSettings 一声不响，值变了没人告诉你。
 *   门在你手里，信号才在你手里；顺便还能做校验（端口 0 存进去，界面当场抽风）。
 *
 *   第三条：**status() 是必修课**。配置文件被用户手改坏、U 盘拔了、目录只读，
 *   QSettings 都不会崩，读不回来的键它就当不存在，默默给你默认值，你以为程序「记住」了，
 *   其实什么都没写进去。
 *   我在步骤⑦ 把磁盘字节数一行行打出来，就是让你习惯「拿证据说话」这个毛病。
 *   做桌面的人，最后输给你的往往不是算法，是这些「读回来到底是多少」。
 *
 * ——— 小练习 ———
 *   1. 给 ConfigStore 加一个 `template <class T> T getChecked(key, 默认值, 校验lambda)`：
 *      读回来先用 canConvert<T>() 判类型、再用 lambda 判范围（端口必须在 1..65535），
 *      不合格就返回默认值并 qWarning() 一行。把你造的那个「坏 ini」路径打出来。
 *   2. 把步骤⑥ 的 beginGroup/endGroup 改成一个 RAII 类（GroupGuard，构造 beginGroup、
 *      析构 endGroup），然后在中间塞一条 `return;`，对比改前改后的 allKeys() 输出，
 *      亲眼看一次「group 漏在里层」长什么样。
 *   3. 把 defaultFormat 换成 NativeFormat（**只在 Linux/macOS 上做**，Windows 会写注册表），
 *      重跑步骤②，把 (a)(c) 两条的 fileName() 抄回注释里，说明为什么同一份代码
 *      在两个平台上落点不同，以及便携版为什么必须走 fileName 那条构造。
 *   4. 步骤⑨ 的导出件加上 schemaVersion 迁移：写一段「从版本 1 迁到版本 2」的假迁移
 *      （把 服务/端口 改名成 网络/监听端口），再拿旧 ini 读一遍，把迁移前后的 allKeys()
 *      都打出来。
 */
// ============================================================================

// 本文件在 .cpp 里定义了 Q_OBJECT 类（ConfigStore）：AUTOMOC 生成同名 .moc，必须在类定义之后包含。
#include "01_qsettings.moc"
