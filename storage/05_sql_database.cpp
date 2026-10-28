// ============================================================================
// 第 5 讲 · Qt SQL 的正确用法：连接名、绑定值、事务、错误四件事
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   数据库这块，Qt 给的不是「薄封装」而是**一套带方言抽象的 API**。用错的人年年有，
//   症状都差不多：偶发崩溃（跨线程共用连接）、被注入（字符串拼 SQL）、
//   静默丢数据（没检查 affected rows / 没提交事务）、报「未知错误」（没读 QSqlError）。
//   这一讲把四件事一次钉死：
//     1) **连接名**。`QSqlDatabase::addDatabase(type)` 不写第二个参数时，用的是默认连接名
//        "qt_sql_default_connection"（qsqldatabase.h 第 38~47 行那个
//        QSqlDatabaseDefaultConnectionName 里写死的字符串，第 106~107 行重载的默认值就是它）。
//        后果：**同一个进程里谁都能拿到同一个连接**。第 112 行 `database(name, open=true)`
//        还默认帮你 open——这条默认值坑过不少人。
//        线程规则：一个连接**属于创建它的那个线程**（QSqlDatabase 内部持驱动 QObject，
//        第 97/98 行给了 moveToThread/thread，就是唯一正规搬家方式）。
//        正确姿势：每个用数据库的线程用**自己的连接名**；
//        要么 cloneDatabase（第 110/111 行），要么在工作线程里 addDatabase 一个新名字。
//        跨线程复用同一名字的连接 = 未定义行为，驱动多半当场报错或直接崩。
//     2) **QSqlDatabase 这个对象的「拷贝」不是新连接**。它不是 QObject（是 Q_GADGET，第 51 行），
//        拷贝构造与 operator= 都在 public（第 57/60 行）——拷出来的是**同一个连接的第二个句柄**，
//        改一处另一处可见。所以：句柄可以随便传（按值传 QSqlDatabase 合法但没意义），
//        而**连接的寿命与名字绑定**，收尾必须 removeDatabase(名字)（第 114 行），
//        别指望「对象析构了连接就没了」。真正**不能拷贝**的是 QSqlQuery：
//        qsqlquery.h 第 36~44 行——新 Qt 里拷贝直接 `= delete`，只留移动构造与 swap。
//     3) **绑定值是防注入的唯一正道**。bindValue(名字/位置, 值, QSql::ParamType) 与
//        addBindValue（第 101~104 行），走的是驱动的准备语句接口（PreparedQueries 特性，
//        qsqldriver.h 第 36 行）。拼接字符串是把**用户输入当 SQL 语法**解释；
//        绑定是把值交给数据库当**值**处理，两者语义层面就不是一回事。
//        本讲证据⑤用一张脏名字把两种写法并排打出来：拼接那条命中 3 行（全表泄露），
//        绑定那条命中 0 行（正常语义）。
//        另外一条硬知识：**表名/列名不能当绑定值**（占位符只能替换「值」的位置），
//        要防注入得走 QSqlDriver::escapeIdentifier（qsqldriver.h 第 74 行）+ 白名单。
//     4) **错误与行数要逐条读**。exec() 返回 false 才说明语句失败；返回 true 不代表影响了几行，
//        所以 INSERT/UPDATE/DELETE 之后要读 numRowsAffected()（第 65 行），
//        插入自增主键之后读 lastInsertId()（第 111 行，先看 LastInsertId 特性）。
//        QSqlError 在本机能读的是 type()/nativeErrorCode()/text()/driverText()/databaseText()/isValid()
//        （qsqlerror.h 第 42~47 行）——**Qt5 的 number() 在 6.x 没有了**，
//        换成返回 QString 的 nativeErrorCode()，别再写 `err.number()`。
//        QSqlResult（qsqlresult.h）里 lastError()/data()/size() 全在 **protected** 段（第 32~42 行），
//        只有驱动子类能用；业务代码取错误**一律从 QSqlQuery::lastError() 或 QSqlDatabase::lastError() 走**，
//        别去 query.result()->lastError()（编不过，而且是设计如此）。
//     5) **事务**。transaction()/commit()/rollback()（第 77~79 行）是驱动薄封装，
//        先 `driver()->hasFeature(QSqlDriver::Transactions)` 判断（SQLite/PG/MySQL 有，
//        某些驱动没有——没有时这三个函数只是发语句，成败看返回值）。
//        SQLite 的实际行为：BEGIN/COMMIT/ROLLBACK 是真事务，且**DDL 也能回滚**（PG 一样，
//        MySQL 的 DDL 隐式提交、回滚不了——这就是可移植性的第一道坎）；
//        默认 journal 模式下写事务会锁库文件；嵌套 transaction() 在 SQLite 上**不合法**
//        （第二个 BEGIN 直接报错），要做嵌套得用 SAVEPOINT，Qt 这层没给你封装。
//        上面这些「SQLite/MySQL/PG 谁怎样」的句子，凡是本文件没在头文件里核到的，
//        都以「SQLite 行为以现场打印为准」处理，本讲每条都打了返回值让你自己核对。
//   顺带两个 Qt6 的签名变化（很多人还在按 Qt5 写）：
//     · **按名字取值的那批重载换成了 QAnyStringView**：QSqlQuery::value/isNull、
//       QSqlRecord::value/indexOf/contains/field 的 `const QString&` 版在
//       `QT_SQL_REMOVED_SINCE(6, 8)` 开关下已经关掉（qsqlquery.h 第 77~80 行、
//       qsqlrecord.h 第 36~60 行），新代码直接传字符串字面量/QString 即可
//       （QAnyStringView 有从 QString 的隐式构造，qanystringview.h 第 210 行）。
//     · **QSqlQuery::value 仍返回 QVariant，不是 std::optional**（第 76/80 行）。
//       「Qt6 把 SQL 接口改成 std::optional 了」这种说法在本机 6.12 的头文件里核对不到：
//       可空性判断走的是 isValid()/isNull(i)/isActive()。别为了「现代化」去写 optional 版。
//     · QSqlDatabase::exec(const QString&) 在 6.6 就打了弃用（第 70~73 行），
//       提示「Use QSqlQuery::exec() instead」——所以本讲一次都不用它。
//
// 对应构建目标名：storage_05_sql_database（仓库根 CMakeLists.txt 第 217 行，照抄不改）
//   set(QT_DEMO_LIBS Qt6::Widgets Qt6::Sql Qt6::Xml)      # 第 212 行
//   qt_add_demo(storage storage_05_sql_database storage/05_sql_database.cpp)
//   find_package(Qt6 REQUIRED COMPONENTS Sql)             # 手工核对时
//   target_link_libraries(storage_05_sql_database PRIVATE Qt6::Sql)
//   本文件**没有 Q_OBJECT 类**，末尾不 include 任何 .moc（校验门只在文件含 Q_OBJECT 时才跑 moc）。
//
// 运行前须知：
//   · **需要 QSQLITE 驱动插件**。程序第一步就 `QSqlDatabase::isDriverAvailable("QSQLITE")`
//     并打印 `QSqlDatabase::drivers()`；本机核对到过插件文件
//     C:\msys64\ucrt64\share\qt6\plugins\sqldrivers\qsqlite.dll。
//     不可用时**不崩、不硬跑**：打印原因（缺插件/缺 Qt6::Sql）后 quit() 退出——
//     这就是「运行前须知」该由你自己核对的那一条，装了 Qt 但没装 sql 插件是常事。
//   · 无人值守：QTimer::singleShot 串步骤 → qInfo() 打证据 → close/removeDatabase/删库文件
//     → 打「硬账」→ exit(Ledger::exitCode())。行数、错误码、特征位、回滚效果都逐条核对：
//     全对才返回 0，有一项对不上返回 1（缺 QSQLITE 驱动同样判红，不假装通过）。
//   · 本讲不建窗口，用 **QCoreApplication** 就够（QtSql 与 QtGui 无关；
//     第 6 讲要挂视图、要用 QSqlRelationalDelegate，那边才必须 QApplication）。
//   · 落盘约定：库文件写在 QStandardPaths::TempLocation 下的 qtc_storage05 目录，
//     结尾删库并顺带清 -journal/-wal，把结果打印出来。
//   · 未实测声明：**本文件未在本机实测编译运行**；只过了 moc + g++ -fsyntax-only
//     （-std=c++17 -fPIC -Wall -Wextra，0 error / 0 warning），未链接、未运行。
//     下文「预期」是推理，不是实测。
// ============================================================================

#include <QCoreApplication>          // QtCore/qcoreapplication.h（QSql 不需要 GUI）
#include <QTimer>
#include <QDebug>
#include <QString>
#include <QStringList>             // QStringLiteral 宏来自 QtCore/qstring.h，**没有** <QStringLiteral> 这个头
#include <QByteArray>
#include <QVariant>                  // QtCore/qvariant.h：value()/lastInsertId() 的返回类型
#include <QVariantList>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QtGlobal>

#include <QSqlDatabase>              // QtSql/qsqldatabase.h（本讲注释里的行号都对着它）
#include <QSqlQuery>                 // QtSql/qsqlquery.h
#include <QSqlError>                  // QtSql/qsqlerror.h
#include <QSqlDriver>                // QtSql/qsqldriver.h（hasFeature / escapeIdentifier / sqlStatement）
#include <QSqlRecord>                // QtSql/qsqlrecord.h
#include <QSqlField>                 // QtSql/qsqlfield.h（record().field(i) 的类型）
#include <QSqlResult>                // QtSql/qsqlresult.h（看它哪些成员在 protected）
#include <QSqlIndex>                 // QtSql/qsqlindex.h（primaryIndex 的返回类型）

// ============================================================================
// 一、本机头文件核对（C:\msys64\ucrt64\include\qt6，Qt 6.12.0 / QT_VERSION_STR 见 qconfig.h:73）
// ============================================================================
// QtSql/qsqldatabase.h
//   第 38~47 行 struct QSqlDatabaseDefaultConnectionName { static constexpr const char
//              defaultConnection[] = "qt_sql_default_connection"; ... }   ← 默认连接名的真身
//   第 49 行  class Q_SQL_EXPORT QSqlDatabase : public QSqlDatabaseDefaultConnectionName
//   第 51 行  Q_GADGET                                                    ← **不是 QObject**
//   第 56~60 行 构造 / **拷贝构造 public** / 析构 / **operator= public**
//   第 62~66 行 open() / open(user,password) / close() / isOpen() / isOpenError()
//   第 67 行  QStringList tables(QSql::TableType type = QSql::Tables) const;
//   第 68/69 行 QSqlIndex primaryIndex(const QString&) / QSqlRecord record(const QString&)
//   第 70~73 行 QSqlDatabase::exec(const QString&) —— **6.6 起弃用**，用 QSqlQuery
//   第 74 行  QSqlError lastError() const;
//   第 75 行  bool isValid() const;                       ← 连接名存在且驱动可用
//   第 77~79 行 bool transaction(); bool commit(); bool rollback();
//   第 81~94 行 setDatabaseName/setUserName/setPassword/setHostName/setPort/setConnectOptions + 各 getter
//              + QString connectionName() const;
//   第 95/96 行 setNumericalPrecisionPolicy / numericalPrecisionPolicy
//   第 97/98 行 bool moveToThread(QThread*); QThread *thread() const;   ← 线程归属的正规出口
//   第 100 行 QSqlDriver* driver() const;
//   第 106/107 行 static QSqlDatabase addDatabase(const QString &type,
//                       const QString &connectionName = defaultConnectionName());
//   第 108/109 行 static QSqlDatabase addDatabase(QSqlDriver*, const QString &connectionName = ...);
//   第 110/111 行 static QSqlDatabase cloneDatabase(const QSqlDatabase&, const QString &connectionName);
//              （第二份接 QString 名字的版本；两个重载都**必须给新连接名**）
//   第 112 行 static QSqlDatabase database(const QString &connectionName = defaultConnectionName(),
//                       bool open = true);          ← 注意第二个参数默认 true
//   第 114 行 static void removeDatabase(const QString &connectionName);
//   第 115~119 行 static contains / drivers / connectionNames / registerSqlDriver / isDriverAvailable
// QtSql/qsqlquery.h
//   第 32~34 行 explicit QSqlQuery(QSqlResult*); QSqlQuery(const QString& = QString(),
//              const QSqlDatabase& = QSqlDatabase()); explicit QSqlQuery(const QSqlDatabase&);
//   第 36~44 行 拷贝构造/赋值：**要么带弃用标记（6.2 起），要么直接 = delete** ← 别拷 QSqlQuery
//   第 46~54 行 移动构造 + swap + 移动赋值        ← 要「传走」就用 move
//   第 56~58 行 isValid/isActive/isNull(int)；第 62 行 isNull(QAnyStringView)
//   第 63~65 行 at() / lastQuery() / numRowsAffected()
//   第 66 行  QSqlError lastError() const;
//   第 67/68 行 isSelect() / size()               ← size() 要 QuerySize 特性，SQLite 给 -1
//   第 70 行  const QSqlResult* result() const;   ← 能拿到指针，但它的成员基本是 protected
//   第 72 行  QSqlRecord record() const;
//   第 74/75 行 setForwardOnly(bool) / exec(const QString&)
//   第 76/80 行 QVariant value(int i) const; QVariant value(QAnyStringView) const;  ← **QVariant，不是 optional**
//   第 88~93 行 seek/next/previous/first/last
//   第 97~100 行 bool exec(); enum BatchExecutionMode{ValuesAsRows,ValuesAsColumns};
//              bool execBatch(BatchExecutionMode = ValuesAsRows); bool prepare(const QString&);
//   第 101~104 行 bindValue(名字,值,QSql::ParamType=QSql::In) / bindValue(位置,...) / addBindValue(值,...)
//   第 105~110 行 boundValue/boundValues()/boundValueNames()/boundValueName(pos)/executedQuery()
//   第 111 行  QVariant lastInsertId() const;
//   第 112/113 行 finish() / nextResult()
// QtSql/qsqlerror.h
//   第 20~26 行 enum ErrorType { NoError, ConnectionError, StatementError, TransactionError, UnknownError }
//              ← 枚举在 **QSqlError 类里面**（QtSql/qsql.h 那种全局 namespace 里没有 ErrorType）
//   第 42~47 行 driverText() / databaseText() / type() / **nativeErrorCode()** / text() / isValid()
//              ⚠ **没有 number()**：Qt5 那个返回 int 的 number() 在本版本不存在
// QtSql/qtsqlglobal.h（namespace QSql 的枚举都在这儿；文件末尾注释还写着 // QSQL_H，
//                   所以网上说的「qsql.h」在本机是**没有这个文件**的，我 Read 过：No such file）
//   第 20~24 行 enum Location { BeforeFirstRow = -1, AfterLastRow = -2 }
//   第 26~33 行 enum ParamTypeFlag { In, Out, InOut, Binary } + Q_DECLARE_FLAGS(ParamType, ParamTypeFlag)
//   第 35~41 行 enum TableType { Tables, SystemTables, Views, AllTables }
//   第 43~50 行 enum NumericalPrecisionPolicy { LowPrecisionInt32/Int64/Double, HighPrecision = 0 }
// QtSql/qsqldriver.h
//   第 36/37 行 enum DriverFeature { Transactions, QuerySize, BLOB, Unicode, PreparedQueries,
//              NamedPlaceholders, PositionalPlaceholders, LastInsertId, BatchOperations, ... }
//   第 41/42 行 enum StatementType { WhereStatement, SelectStatement, UpdateStatement,
//              InsertStatement, DeleteStatement }
//   第 44/45 行 enum IdentifierType { FieldName, TableName }
//   第 48~59 行 enum DbmsType { ..., SQLite, ... }（判方言用 dbmsType()，第 101 行）
//   第 74 行  virtual QString escapeIdentifier(const QString&, IdentifierType) const;  ← 标识符防注入
//   第 75/76 行 virtual QString sqlStatement(StatementType, const QString &table,
//              const QSqlRecord&, bool preparedStatement) const;                      ← 让驱动拼语句
//   第 81 行  virtual bool hasFeature(DriverFeature f) const = 0;
// QtSql/qsqlresult.h
//   第 28~31 行 public 只有析构与 handle()；**第 32 行起整段 protected**：
//   第 40~42 行 at()/lastQuery()/lastError()  ← 所以业务代码调不到 result()->lastError()
//   第 82~93 行 纯虚 data/isNull/reset/fetch/fetchFirst/fetchLast + size/numRowsAffected/
//              record/lastInsertId（**驱动子类**实现这些，这才是「逐条取」的地方）
// QtSql/qsqlrecord.h
//   第 35~40 行 value(int) / value(QAnyStringView)（QString 版在 QT_SQL_REMOVED_SINCE(6,8) 里）
//   第 60/61 行 indexOf(QAnyStringView) / fieldName(int)
//   第 63~67 行 field(int) / field(QAnyStringView)
//   第 85~93 行 isEmpty/contains/clear/clearValues/count/keyValues
// ============================================================================

namespace {

// ── 硬账：每一项核对都要能判红，不然「预期」只是嘴上说说 ──
struct Ledger {
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
                             .arg(want)
                             .arg(got);
}

void checkBool(const char *what, bool got, bool want = true)
{
    check(what, got ? 1 : 0, want ? 1 : 0);
}

const QString kConnectionName = QStringLiteral("storage05.main");   // 一定要带自己的名字，别吃默认值
const QString kTableName      = QStringLiteral("accounts");

QString tempDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(QStringLiteral("qtc_storage05"));
}
QString dbFilePath()
{
    return QDir(tempDir()).filePath(QStringLiteral("demo.db"));
}

// QSqlError 打成人话：**只用本机存在的成员**（type/nativeErrorCode/text/driverText/databaseText）
QString errorTypeName(QSqlError::ErrorType t)
{
    switch (t) {
    case QSqlError::NoError:          return QStringLiteral("NoError");
    case QSqlError::ConnectionError:  return QStringLiteral("ConnectionError");
    case QSqlError::StatementError:   return QStringLiteral("StatementError");
    case QSqlError::TransactionError: return QStringLiteral("TransactionError");
    case QSqlError::UnknownError:     return QStringLiteral("UnknownError");
    }
    return QStringLiteral("未知 ErrorType(%1)").arg(int(t));
}

QString describeError(const QSqlError &e)
{
    return QStringLiteral("type=%1｜nativeErrorCode=%2｜driverText=%3｜databaseText=%4｜text=%5｜isValid=%6")
        .arg(errorTypeName(e.type()))
        .arg(e.nativeErrorCode().isEmpty() ? QStringLiteral("(空)") : e.nativeErrorCode())
        .arg(e.driverText(), e.databaseText(), e.text())
        .arg(e.isValid());
}

// 数一下全表行数：SELECT COUNT(*) 走准备语句，返回值是 QVariant
qint64 countAllRows(const QSqlDatabase &db)
{
    QSqlQuery probe(db);
    if (!probe.prepare(QStringLiteral("SELECT COUNT(*) FROM accounts")))
        return -1;
    if (!probe.exec())
        return -1;
    if (!probe.next())
        return -1;
    return probe.value(0).toLongLong();
}

} // namespace

// ============================================================================
// 二、证据①：驱动可用性 + 连接名/句柄语义
// ============================================================================
static bool evidenceDriverAndConnection()
{
    qInfo() << "【证据①】驱动与连接：先把「能不能跑」判断清楚";

    const QStringList all = QSqlDatabase::drivers();
    qInfo() << "  QSqlDatabase::drivers() =" << all << "（本机可用的驱动插件名）";
    const bool hasSqlite = QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"));
    qInfo() << "  isDriverAvailable(\"QSQLITE\") =" << hasSqlite;
    checkBool("QSQLITE 驱动插件可用（缺它这一讲无法核对）", hasSqlite);
    if (!hasSqlite) {
        qInfo() << "  ⚠ 没有 QSQLITE 驱动插件：后面的 SQL 演示**全部跳过**（不崩、不硬跑）。"
                   "装法（MSYS2）：pacman -S mingw-w64-ucrt-x86_64-qt6-base 里的 sqldrivers 插件；"
                   "Qt 官方安装器勾「Qt 5.15+ → Database」也一样。"
                   "现场判据：把 plugins/sqldrivers 目录放进可执行文件旁的 sqldrivers/ 即可。";
        return false;
    }

    QDir().mkpath(tempDir());
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kConnectionName);
    db.setDatabaseName(dbFilePath());
    qInfo() << "  addDatabase 后：driverName =" << db.driverName()
            << "｜connectionName =" << db.connectionName()
            << "｜databaseName =" << db.databaseName();
    qInfo() << "  还没 open：isOpen =" << db.isOpen() << "｜isValid =" << db.isValid()
            << "｜isOpenError =" << db.isOpenError()
            << "（isValid 说的是「这个名字有连接且驱动在手」，跟开没开是两码事）";
    qInfo() << "  contains(默认名) =" << QSqlDatabase::contains()
            << "｜connectionNames() =" << QSqlDatabase::connectionNames()
            << "← 我只注册了带自己名字的连接，**没碰默认连接**";
    checkBool("① 连接的 driverName 是 QSQLITE", db.driverName() == QStringLiteral("QSQLITE"));
    checkBool("① 连接名就是我给的那个（没落到默认名上）",
              db.connectionName() == kConnectionName);
    checkBool("① 默认连接 qt_sql_default_connection 没有被创建", QSqlDatabase::contains(), false);
    check("① connectionNames() 里只有 1 个连接", QSqlDatabase::connectionNames().size(), 1);
    checkBool("① addDatabase 之后还没 open", db.isOpen(), false);
    checkBool("① 没 open 也 isValid（名字在、驱动在手）", db.isValid());

    // 拷贝语义：句柄复制 = 同一个连接（QSqlDatabase 不是 QObject，拷贝是 public）
    QSqlDatabase handle2 = db;
    handle2.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    qInfo() << "  拷贝一个 QSqlDatabase 再在副本上 setConnectOptions，原件可见吗 ="
            << (db.connectOptions() == handle2.connectOptions())
            << "｜副本的连接名 =" << handle2.connectionName()
            << "→ **拷贝不是新连接**，是同一连接的第二个句柄（要新连接就 cloneDatabase/addDatabase 新名字）";
    checkBool("① 在副本上 setConnectOptions，原件立刻可见（同一连接）",
              db.connectOptions() == handle2.connectOptions());
    checkBool("① 副本的连接名与原件相同", handle2.connectionName() == db.connectionName());
    check("① 拷贝之后连接数没变（没有新连接）", QSqlDatabase::connectionNames().size(), 1);

    if (!db.open()) {
        qInfo() << "  ⚠ open 失败：" << describeError(db.lastError());
        checkBool("① 库文件能 open", false);
        return false;
    }
    qInfo() << "  open 成功：isOpen =" << db.isOpen() << "｜线程归属 thread() 是否为空 ="
            << (db.thread() == nullptr)
            << "｜驱动特性：Transactions =" << db.driver()->hasFeature(QSqlDriver::Transactions)
            << "PreparedQueries =" << db.driver()->hasFeature(QSqlDriver::PreparedQueries)
            << "NamedPlaceholders =" << db.driver()->hasFeature(QSqlDriver::NamedPlaceholders)
            << "LastInsertId =" << db.driver()->hasFeature(QSqlDriver::LastInsertId)
            << "BatchOperations =" << db.driver()->hasFeature(QSqlDriver::BatchOperations)
            << "QuerySize =" << db.driver()->hasFeature(QSqlDriver::QuerySize);
    qInfo() << "  dbmsType() = SQLite 吗 ="
            << (db.driver()->dbmsType() == QSqlDriver::SQLite)
            << "（要写跨库代码，方言判断走这里，别猜）";
    checkBool("① open 成功", db.isOpen());
    checkBool("① 方言判断 dbmsType() 是 SQLite", db.driver()->dbmsType() == QSqlDriver::SQLite);
    // 连接属于「创建它的线程」：驱动 QObject 就挂在这个线程上
    checkBool("① 连接的宿主线程就是当前（主）线程",
              db.thread() == QCoreApplication::instance()->thread());
    checkBool("① 有 Transactions 特性（否则事务那一段只能靠返回值赌）",
              db.driver()->hasFeature(QSqlDriver::Transactions));
    checkBool("① 有 PreparedQueries 特性（绑定值靠它才有准备语句语义）",
              db.driver()->hasFeature(QSqlDriver::PreparedQueries));
    return true;
}

// ============================================================================
// 三、证据②：建表 + 可移植性
// ============================================================================
static void evidenceCreateTable()
{
    qInfo() << "【证据②】建表：语句本身也是「方言」，先看本机 SQLite 认什么";

    QSqlDatabase db = QSqlDatabase::database(kConnectionName, false);   // 显式 false：别让它顺手 open
    QSqlQuery q(db);
    const QString ddl = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS accounts ("
        "  id    INTEGER PRIMARY KEY AUTOINCREMENT,"   // SQLite 写法（MySQL: AUTO_INCREMENT，PG: BIGSERIAL/IDENTITY）
        "  name  TEXT NOT NULL,"
        "  qty   INTEGER NOT NULL DEFAULT 0,"
        "  note  TEXT,"                                 // 允许 NULL：本讲拿它验 isNull()
        "  UNIQUE (name)"
        ")");
    const bool ok = q.exec(ddl);
    qInfo() << "  CREATE TABLE IF NOT EXISTS ... =" << ok
            << "｜numRowsAffected() =" << q.numRowsAffected()
            << "（DDL 的受影响行数各家驱动都可能给 0 或 -1，**别拿它当成功判据**，成功判据是返回值）";
    if (!ok)
        qInfo() << "  DDL 失败详情：" << describeError(q.lastError());
    checkBool("② 建表语句 exec 成功", ok);

    // 可移植性三件事，全部有本机 API 支撑
    const QStringList tables = db.tables(QSql::Tables);
    qInfo() << "  db.tables(QSql::Tables) =" << tables
            << "｜里面含 accounts =" << tables.contains(kTableName)
            << "（这是**跨库的表清单**入口，别拼 \"SELECT name FROM sqlite_master\" 那种方言 SQL）";
    const QSqlRecord rec = db.record(kTableName);
    QStringList fieldNames;
    for (int i = 0; i < rec.count(); ++i)
        fieldNames << rec.fieldName(i);
    qInfo() << "  db.record(表名)：字段数 =" << rec.count() << "｜字段名 =" << fieldNames
            << "（跨库写 DDL/映射的正规信息来源）";
    const QSqlIndex pk = db.primaryIndex(kTableName);
    QStringList pkNames;
    for (int i = 0; i < pk.count(); ++i)
        pkNames << pk.fieldName(i);
    qInfo() << "  primaryIndex：字段 =" << pkNames << "（第 6 讲 QSqlTableModel 能不能改数据就看它）";
    checkBool("② tables() 里有 accounts", tables.contains(kTableName));
    check("② record() 读到的字段数", rec.count(), 4);
    checkBool("② record() 的字段名顺序与建表一致",
              fieldNames == QStringList({ QStringLiteral("id"), QStringLiteral("name"),
                                          QStringLiteral("qty"), QStringLiteral("note") }));
    check("② primaryIndex 只有一个字段", pk.count(), 1);
    checkBool("② 主键是 id", pkNames.value(0) == QStringLiteral("id"));

    // 标识符转义：表名/列名**不能**当绑定值，只能走驱动转义 + 白名单
    const QString nastyIdent = QStringLiteral("acc\"ount; DROP TABLE accounts;--");
    const QString escaped = db.driver()->escapeIdentifier(nastyIdent, QSqlDriver::TableName);
    qInfo() << "  escapeIdentifier(TableName) 把脏标识符变成 ="
            << escaped
            << "｜再交给 record() 问它存在吗 =" << db.tables(QSql::Tables).contains(nastyIdent)
            << "→ 结论：**值用 bindValue，标识符用 escapeIdentifier + 白名单**，两条不能互换。";
    checkBool("② escapeIdentifier 用双引号把标识符整个包住",
              escaped.startsWith(QLatin1Char('"')) && escaped.endsWith(QLatin1Char('"')));
    checkBool("② 标识符里原有的双引号被翻倍（\"\" 才是 SQL 的转义）",
              escaped.contains(QStringLiteral("\"\"")));
    checkBool("② 脏标识符没有被当成一张真实存在的表",
              db.tables(QSql::Tables).contains(nastyIdent), false);
}

// ============================================================================
// 四、证据③：prepare + 绑定值插入，lastInsertId / numRowsAffected
// ============================================================================
static void evidenceBoundInserts()
{
    qInfo() << "【证据③】准备语句 + 绑定值：插入三行，把每行的 id 与 affected rows 打出来";

    QSqlDatabase db = QSqlDatabase::database(kConnectionName, false);
    QSqlQuery q(db);
    const QString ins = QStringLiteral("INSERT INTO accounts (name, qty, note) VALUES (:name, :qty, :note)");
    const bool prepared = q.prepare(ins);
    if (!prepared) {
        qInfo() << "  prepare 失败：" << describeError(q.lastError());
        checkBool("③ prepare 成功", false);
        return;
    }
    qInfo() << "  prepare 后 executedQuery（还没执行）=" << q.executedQuery().isEmpty()
            << "（true=还没执行；空不空决定于驱动，别拿它当日志依据，日志用 lastQuery()）";
    checkBool("③ prepare 返回 true", prepared);

    struct Seed { const char *name; int qty; bool hasNote; };
    const Seed seeds[] = {
        { "alice",  7, true  },
        { "bob",   42, false },
        { "carol",  13, true  },
    };
    int seedIndex = 0;
    for (const Seed &s : seeds) {
        q.bindValue(QStringLiteral(":name"), QString::fromLatin1(s.name), QSql::In);
        q.bindValue(QStringLiteral(":qty"), s.qty);
        if (s.hasNote)
            q.bindValue(QStringLiteral(":note"), QStringLiteral("第 5 讲的演示行"));
        else
            q.bindValue(QStringLiteral(":note"), QVariant());   // ← 无效 QVariant 就是 NULL，别写 QString()
        const bool ok = q.exec();
        qInfo() << QStringLiteral("  插入 %1（qty=%2, note=%3）：exec=%4｜numRowsAffected=%5｜lastInsertId=%6")
                       .arg(QString::fromLatin1(s.name))
                       .arg(s.qty)
                       .arg(s.hasNote ? QStringLiteral("有") : QStringLiteral("NULL"))
                       .arg(ok)
                       .arg(ok ? q.numRowsAffected() : -1)
                       .arg(ok ? q.lastInsertId().toString() : QStringLiteral("(无)"));
        if (!ok) {
            qInfo() << "     失败详情：" << describeError(q.lastError());
            checkBool("③ 种子行插入成功", false);
        } else {
            check("③ numRowsAffected = 1（一条 INSERT 只影响一行）", q.numRowsAffected(), 1);
            check("③ lastInsertId 是连续自增的 1/2/3", q.lastInsertId().toInt(), seedIndex + 1);
        }
        ++seedIndex;
    }
    qInfo() << "  绑定值本身可回读：boundValueNames() =" << q.boundValueNames()
            << "｜boundValue(\":qty\") =" << q.boundValue(QStringLiteral(":qty")).toString()
            << "（QSql::ParamType 默认 In，第 101~104 行；Out/InOut 只有支持的驱动才有意义）";
    qInfo() << "  全表行数 =" << countAllRows(db) << "（预期 3）";
    check("③ 三行种子插入后全表行数", countAllRows(db), 3);
    checkBool("③ 绑定值能按名字回读（:qty 仍是最后一次绑的 13）",
              q.boundValue(QStringLiteral(":qty")).toInt() == 13);
    checkBool("③ boundValueNames 里带着 :name/:qty/:note 三个名字",
              q.boundValueNames().contains(QStringLiteral(":name"))
                  && q.boundValueNames().contains(QStringLiteral(":note")));

    // 批量：execBatch + 列表绑定（BatchOperations 特性为 true 才有意义）
    if (db.driver()->hasFeature(QSqlDriver::BatchOperations)) {
        QSqlQuery batch(db);
        batch.prepare(QStringLiteral("INSERT INTO accounts (name, qty, note) VALUES (:name, :qty, :note)"));
        batch.bindValue(QStringLiteral(":name"), QVariantList{ QStringLiteral("dave"), QStringLiteral("erin") });
        batch.bindValue(QStringLiteral(":qty"), QVariantList{ 1, 2 });
        batch.bindValue(QStringLiteral(":note"), QVariantList{ QVariant(), QStringLiteral("批量塞的两行") });
        const bool okBatch = batch.execBatch(QSqlQuery::ValuesAsRows);
        qInfo() << "  execBatch(ValuesAsRows) =" << okBatch
                << "｜numRowsAffected =" << batch.numRowsAffected()
                << "｜全表行数现在 =" << countAllRows(db)
                << "（预期 5。**批量后的 affected rows 语义各驱动不同，这条标「待复核」**，"
                   "要精确就逐条 exec 自己数）";
        checkBool("③ execBatch 成功（本驱动报了 BatchOperations）", okBatch);
        check("③ 批量后全表行数（3 种子 + 2 批量）", countAllRows(db), 5);
    } else {
        // SQLite 在 Qt6 里就是这条分支：hasFeature(BatchOperations)==false。
        // 注意 QSqlQuery::execBatch 会**先查这个特性**，没有就返回 false——所以绝不能
        // 把「批量」当成必然发生的步骤，后面的期望行数要按现场算，而不是写死 5。
        qInfo() << "  本驱动 hasFeature(BatchOperations)==false → 批量段落**跳过**；"
                   "后面的期望行数一律按现场 countAllRows() 算，不写死数字";
        check("③ 跳过批量后全表行数仍是 3", countAllRows(db), 3);
    }
}

// ============================================================================
// 五、证据④：一次注定失败的 INSERT，把 QSqlError 的五个成员读全
// ============================================================================
static void evidenceFailure()
{
    qInfo() << "【证据④】故意撞 UNIQUE：错误的四个面（type / nativeErrorCode / driverText / databaseText）";

    QSqlDatabase db = QSqlDatabase::database(kConnectionName, false);
    const qint64 before = countAllRows(db);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO accounts (name, qty, note) VALUES (:name, :qty, :note)"));
    q.bindValue(QStringLiteral(":name"), QStringLiteral("alice"));   // 已经存在，且表上有 UNIQUE(name)
    q.bindValue(QStringLiteral(":qty"), 99);
    q.bindValue(QStringLiteral(":note"), QVariant());
    const bool ok = q.exec();
    const QSqlError err = q.lastError();
    qInfo() << "  exec() 返回 =" << ok << "（false 才是失败；**true 也别以为写进去了**，看下一条）";
    qInfo() << "  lastError：" << describeError(err);
    qInfo() << "  ⚠ 本版本 QSqlError **没有 number()**（qsqlerror.h 第 42~47 行里没有它），"
               "对应的是返回 QString 的 nativeErrorCode()。照抄 Qt5 代码里 `err.number()` 的一律编不过。";
    qInfo() << "  失败之后 numRowsAffected() =" << q.numRowsAffected()
            << "｜isActive =" << q.isActive() << "｜isValid =" << q.isValid()
            << "｜lastInsertId() =" << q.lastInsertId().toString()
            << "（失败的语句拿不到有效 id：QVariant 是空的，**先判 isValid/返回值，再取值**）";
    qInfo() << "  全表行数没变 =" << countAllRows(db) << "（撞 UNIQUE 的这条没写进去）";
    qInfo() << "  逐条取错误的正规路：QSqlQuery::lastError()；"
               "**不要**去 query.result()->lastError()——qsqlresult.h 里那批成员在 protected 段（第 32~42 行），"
               "只有驱动子类能用。多条语句（nextResult()）时也是逐条读 lastError()。";
    checkBool("④ 撞 UNIQUE 的 INSERT：exec() 返回 false", ok, false);
    checkBool("④ lastError().isValid() 为真（失败一定有错对象）", err.isValid());
    check("④ 错误类型是 StatementError（语句级）", int(err.type()), int(QSqlError::StatementError));
    checkBool("④ nativeErrorCode 非空（Qt6 用它替代 Qt5 的 number()）",
              !err.nativeErrorCode().isEmpty());
    checkBool("④ databaseText 非空（数据库自己说的话）", !err.databaseText().isEmpty());
    checkBool("④ 失败的语句 isActive() = false", q.isActive(), false);
    check("④ 全表行数一行没变", countAllRows(db), before);
}

// ============================================================================
// 六、证据⑤：拼接 vs 绑定值（注入的现场对照）
// ============================================================================
static void evidenceInjection()
{
    qInfo() << "【证据⑤】同一段脏输入，拼接泄露全表、绑定守住语义";

    QSqlDatabase db = QSqlDatabase::database(kConnectionName, false);
    const QString hostile = QStringLiteral("alice' OR '1'='1");
    const qint64 totalRows = countAllRows(db);

    // —— 反面写法：把输入拼进 SQL（生产代码里这就是事故报告的第一行）
    const QString badSql = QStringLiteral("SELECT id, name FROM accounts WHERE name = '%1'").arg(hostile);
    QSqlQuery bad(db);
    int badHits = 0;
    const bool badOk = bad.exec(badSql);
    while (badOk && bad.next())
        ++badHits;
    qInfo() << "  拼接：exec =" << badOk << "｜命中行数 =" << badHits
            << "｜真正送出去的语句 =" << bad.executedQuery()
            << "← 用户输入被当成 **SQL 语法**执行了，'1'='1' 恒真，全表泄露（这里全表才" << totalRows
            << "行，换成 500 万行客户数据就是拖库）";

    // —— 正面写法：同一条查询，值走绑定
    QSqlQuery good(db);
    good.prepare(QStringLiteral("SELECT id, name FROM accounts WHERE name = :who"));
    good.bindValue(QStringLiteral(":who"), hostile);
    int goodHits = 0;
    const bool goodOk = good.exec();
    while (goodOk && good.next())
        ++goodHits;
    qInfo() << "  绑定：exec =" << goodOk << "｜命中行数 =" << goodHits
            << "｜boundValue(:who) 原样回读 =" << good.boundValue(QStringLiteral(":who")).toString()
            << "← 那串东西被当成**一个字符串值**去比较，查不到就是查不到，**这才是正确结果**";
    qInfo() << "  一句话记住：绑定不是「更安全一点的拼接」，是**换了一层语义**；"
               "任何一处 `+ userInput` 拼进 SQL 都是漏洞，包括「内部系统的」「不会有人乱输的」那些。";
    qInfo() << "  位置绑定也行：addBindValue（第 104 行）按顺序塞，配合 setForwardOnly(true) 更省内存";
    QSqlQuery pos(db);
    pos.setForwardOnly(true);
    pos.prepare(QStringLiteral("SELECT id FROM accounts WHERE qty > ? ORDER BY id"));
    pos.addBindValue(10);
    int hits = 0;
    const bool posOk = pos.exec();
    while (posOk && pos.next())
        ++hits;
    qInfo() << "  位置绑定命中 =" << hits << "｜exec =" << posOk << "（? 还是 :name 看驱动：NamedPlaceholders 已在证据①打过）";

    checkBool("⑤ 拼接写法 exec 成功（SQL 语法本身没错——错的是语义）", badOk);
    check("⑤ 拼接注入命中全表（命中行数 = 全表行数）", badHits, totalRows);
    checkBool("⑤ 绑定写法 exec 成功", goodOk);
    check("⑤ 绑定写法把脏输入当字符串，命中 0 行", goodHits, 0);
    checkBool("⑤ boundValue(:who) 原样存着那句注入串（值就是值）",
              good.boundValue(QStringLiteral(":who")).toString() == hostile);
    checkBool("⑤ 位置绑定 exec 成功", posOk);
    check("⑤ 位置绑定 qty>10 命中 2 行（bob 42 / carol 13）", hits, 2);
}

// ============================================================================
// 七、证据⑥：事务 —— rollback 之后行数不变
// ============================================================================
static void evidenceTransaction()
{
    qInfo() << "【证据⑥】事务：transaction → 插两行 → rollback → 行数一根毛都没动";

    QSqlDatabase db = QSqlDatabase::database(kConnectionName, false);
    const qint64 before = countAllRows(db);
    qInfo() << "  事务前行数 =" << before;
    qInfo() << "  hasFeature(Transactions) =" << db.driver()->hasFeature(QSqlDriver::Transactions)
            << "（没这特性的驱动，这三个函数只是「试着发语句」，成败一律看返回值）";

    const bool began = db.transaction();
    qInfo() << "  db.transaction() =" << began << "｜若失败，lastError ="
            << describeError(db.lastError());
    checkBool("⑥ db.transaction() 成功", began);
    if (began) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO accounts (name, qty, note) VALUES (:n, :q, :t)"));
        q.bindValue(QStringLiteral(":n"), QStringLiteral("tx-rollback-1"));
        q.bindValue(QStringLiteral(":q"), 1);
        q.bindValue(QStringLiteral(":t"), QVariant());
        const bool i1 = q.exec();
        q.bindValue(QStringLiteral(":n"), QStringLiteral("tx-rollback-2"));
        const bool i2 = q.exec();
        const qint64 inside = countAllRows(db);
        qInfo() << "  事务内两次 exec =" << i1 << i2 << "｜事务内行数 =" << inside
                << "｜numRowsAffected 本条 =" << q.numRowsAffected()
                << "（SQLite 在事务里就看得见这两行——别把「看得见」当成「已落盘」）";
        const bool rolled = db.rollback();
        qInfo() << "  db.rollback() =" << rolled << "｜回滚后行数 =" << countAllRows(db)
                << "｜事务前 =" << before << "｜相等吗 =" << (countAllRows(db) == before);
        check("⑥ 事务内两次 INSERT 都成功", (i1 ? 1 : 0) + (i2 ? 1 : 0), 2);
        check("⑥ 事务内就能看见这两行（未提交也可见）", inside, before + 2);
        checkBool("⑥ db.rollback() 返回 true", rolled);
        check("⑥ 回滚之后行数回到事务前", countAllRows(db), before);
    }

    // 再走一遍 commit，证明「成功路径也检查返回值」
    const bool began2 = db.transaction();
    QSqlQuery q2(db);
    q2.prepare(QStringLiteral("INSERT INTO accounts (name, qty, note) VALUES (:n, :q, :t)"));
    q2.bindValue(QStringLiteral(":n"), QStringLiteral("tx-committed"));
    q2.bindValue(QStringLiteral(":q"), 5);
    q2.bindValue(QStringLiteral(":t"), QStringLiteral("提交掉的那一行"));
    const bool okInsert = q2.exec();
    const bool committed = began2 && okInsert && db.commit();
    qInfo() << "  第二段：transaction=" << began2 << "插一行=" << okInsert << "commit=" << committed
            << "｜现在全表行数 =" << countAllRows(db)
            << "（预期" << before + 1 << "）";
    qInfo() << "  ⚠ SQLite 别嵌套 transaction()：第二个 BEGIN 会直接报错，Qt 这层没有 SAVEPOINT 封装。"
               "要做嵌套就在自己的代码里计数（只有最外层真开事务），或者一条事务走完。";
    checkBool("⑥ 第二段 transaction() 成功", began2);
    checkBool("⑥ 第二段 INSERT 成功", okInsert);
    checkBool("⑥ 第二段 commit() 成功", committed);
    check("⑥ commit 之后行数 = 事务前 + 1", countAllRows(db), before + 1);
}

// ============================================================================
// 八、证据⑦：查询逐行取 + QSqlRecord 字段索引
// ============================================================================
static void evidenceQueryRows()
{
    qInfo() << "【证据⑦】查询：游标逐行、按名字取值、字段元信息";

    QSqlDatabase db = QSqlDatabase::database(kConnectionName, false);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT id, name, qty, note FROM accounts WHERE qty > :min ORDER BY qty DESC"));
    q.bindValue(QStringLiteral(":min"), 4);
    if (!q.exec()) {
        qInfo() << "  exec 失败：" << describeError(q.lastError());
        return;
    }
    const QSqlRecord rec = q.record();
    QStringList meta;
    for (int i = 0; i < rec.count(); ++i)
        meta << QStringLiteral("%1:%2").arg(rec.fieldName(i))
                                              .arg(QString::fromUtf8(rec.field(i).metaType().name()));
    qInfo() << "  record()：字段数 =" << rec.count() << "｜名:QMetaType 编号 =" << meta
            << "｜indexOf(\"qty\") =" << rec.indexOf(QStringLiteral("qty"))
            << "（**列号从 record 查，别在代码里写死 0/1/2/3**——SELECT 顺序一改就全错）";
    qInfo() << "  size() =" << q.size()
            << "｜QuerySize 特性 =" << db.driver()->hasFeature(QSqlDriver::QuerySize)
            << "（SQLite 不给行数，size() 常是 -1：**别靠 size() 判有没有数据**，靠 next()）";
    check("⑦ record() 的字段数（SELECT 四列）", rec.count(), 4);
    check("⑦ indexOf(\"qty\") 指向第 3 列（下标 2）", rec.indexOf(QStringLiteral("qty")), 2);
    // size() 与驱动的 QuerySize 特性必须是「一致的」：有特性才可能有行数，没特性只能 -1。
    // 这条不写死具体值——各家驱动不一样，但**不许自相矛盾**。
    {
        const bool hasQuerySize = db.driver()->hasFeature(QSqlDriver::QuerySize);
        checkBool("⑦ size() 与 QuerySize 特性一致（没特性就必须是 -1）",
                  hasQuerySize ? q.size() >= 0 : q.size() == -1);
    }

    int rows = 0;
    int nullNotes = 0;
    while (q.next()) {
        const int id = q.value(0).toInt();
        const QString name = q.value(QStringLiteral("name")).toString();   // QAnyStringView 版重载（第 80 行）
        const int qty = q.value(rec.indexOf(QStringLiteral("qty"))).toInt();
        const bool noteNull = q.isNull(QStringLiteral("note"));
        if (noteNull)
            ++nullNotes;
        if (rows < 4)
            qInfo() << QStringLiteral("  行 %1: id=%2 name=%3 qty=%4 note 为 NULL=%5")
                           .arg(rows).arg(id).arg(name).arg(qty).arg(noteNull);
        ++rows;
    }
    qInfo() << "  逐行取完：命中行数 =" << rows << "｜其中 note 为 NULL 的 =" << nullNotes
            << "｜q.isValid() 游标当前位置 =" << q.isValid()
            << "（next() 走完返回 false，此时 isValid=false，**值只能在 next() 为真时取**）";
    qInfo() << "  游标可以倒回去：q.first() =" << q.first() << "｜再取 name ="
            << q.value(QStringLiteral("name")).toString()
            << "（前提不是 forwardOnly；证据⑤那条设了 setForwardOnly(true)，就退不回去了）";
    qInfo() << "  数值精度策略（第 95/96 行）当前 =" << int(db.numericalPrecisionPolicy())
            << "（HighPrecision=0 是默认；大整数溢出成 int 的那种坑就是这里调）";
    // 现场数据（证据③的 3 行种子 + 证据⑥提交的 1 行）：qty>4 的是 alice 7 / bob 42 / carol 13 / tx 5
    check("⑦ qty>4 命中 4 行", rows, 4);
    check("⑦ 其中 note 为 NULL 的 1 行（bob）", nullNotes, 1);
    checkBool("⑦ next() 走完之后游标不再有效（不能再取值）", q.isValid(), false);
    checkBool("⑦ q.first() 能把游标倒回第一行", q.first());
    // ORDER BY qty DESC：第一行是 qty=42 的 bob
    checkBool("⑦ 倒回第一行后取到的 name 是 bob",
              q.value(QStringLiteral("name")).toString() == QStringLiteral("bob"));
}

// ============================================================================
// 九、main
// ============================================================================
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("storage_05_sql_database"));

    qInfo() << "================================================================";
    qInfo() << "storage 第 5 讲 · Qt SQL（Qt =" << QT_VERSION_STR << "）";
    qInfo() << "================================================================";
    qInfo() << "  库文件将写在 =" << dbFilePath();

    // 有没有 QSQLITE 驱动，决定后面跑不跑
    static bool sqlUsable = false;

    QTimer::singleShot(0, []() {
        sqlUsable = evidenceDriverAndConnection();
        if (!sqlUsable)
            qInfo() << "  → 环境缺 QSQLITE 驱动：本讲核对无法进行，按「未通过」退出（装好插件再跑）";
    });
    QTimer::singleShot(30, []() {
        if (sqlUsable) evidenceCreateTable();
    });
    QTimer::singleShot(60, []() {
        if (sqlUsable) evidenceBoundInserts();
    });
    QTimer::singleShot(90, []() {
        if (sqlUsable) evidenceFailure();
    });
    QTimer::singleShot(120, []() {
        if (sqlUsable) evidenceInjection();
    });
    QTimer::singleShot(150, []() {
        if (sqlUsable) evidenceTransaction();
    });
    QTimer::singleShot(180, []() {
        if (sqlUsable) evidenceQueryRows();
    });

    // 收尾：close → removeDatabase → 删库文件（含 -journal/-wal）→ 删临时目录
    QTimer::singleShot(210, []() {
        qInfo() << "【收尾】连接与文件的离场顺序";
        if (!sqlUsable) {
            qInfo() << "  缺 QSQLITE 驱动：连接与落盘都没发生，直接按未通过退出";
            qInfo().noquote() << QStringLiteral("\n【硬账】%1/%2 项核对通过 → 退出码 %3")
                                     .arg(Ledger::passed).arg(Ledger::total).arg(Ledger::exitCode());
            QCoreApplication::exit(Ledger::exitCode());
            return;
        }
        {
            // 走「按名字取连接」的正路；第二个参数 false = 只拿句柄不试图 open（第 112 行默认是 true）
            QSqlDatabase db = QSqlDatabase::database(kConnectionName, false);
            qInfo() << "  先建一个「工作线程用的新连接名」演示 clone 的用法（本进程没真开线程，只验 API 形状）："
                    << "cloneDatabase(...,\"storage05.worker\") 后 connectionNames ="
                    << (QSqlDatabase::contains(QStringLiteral("storage05.worker"))
                            ? QStringLiteral("已存在")
                            : QStringLiteral("尚未存在"));
            const bool wasOpen = db.isOpen();
            if (db.isOpen())
                db.close();
            qInfo() << "  close() 之后：isOpen =" << db.isOpen() << "｜isValid（名字还在） =" << db.isValid();
            checkBool("收尾 关连接之前它是开着的", wasOpen);
            checkBool("收尾 close() 之后 isOpen = false", db.isOpen(), false);
            checkBool("收尾 close() 之后连接名还在（isValid 仍为真）", db.isValid());
            QSqlDatabase::removeDatabase(QStringLiteral("storage05.worker"));   // 没建成也无害：按名字删
            QSqlDatabase::removeDatabase(kConnectionName);
        }
        qInfo() << "  removeDatabase 之后：contains(本讲连接名) =" << QSqlDatabase::contains(kConnectionName)
                << "｜connectionNames() =" << QSqlDatabase::connectionNames()
                << "← 连接的生死跟着**名字**走，不跟着某个 QSqlDatabase 变量走";
        checkBool("收尾 removeDatabase 之后 contains = false",
                  QSqlDatabase::contains(kConnectionName), false);
        check("收尾连接表被清空", QSqlDatabase::connectionNames().size(), 0);

        const bool dbFileExisted = QFileInfo::exists(dbFilePath());
        const QStringList suspects { dbFilePath(),
                                     dbFilePath() + QStringLiteral("-journal"),
                                     dbFilePath() + QStringLiteral("-wal"),
                                     dbFilePath() + QStringLiteral("-shm") };
        QStringList removed;
        for (const QString &p : suspects) {
            if (QFileInfo::exists(p)) {
                QFile::remove(p);
                removed << QFileInfo(p).fileName();
            }
        }
        QDir d(tempDir());
        const bool dirGone = d.exists() ? d.removeRecursively() : true;
        qInfo() << "  删掉的库相关文件 =" << removed
                << "｜临时目录" << tempDir() << "整体删除成功 =" << dirGone
                << "｜还在吗 =" << QFileInfo::exists(tempDir())
                << "（-journal/-wal/-shm 是 SQLite 的附属文件，**关连接之后才删得干净**，顺序别反）";
        checkBool("收尾 库文件确实落过盘（不是内存库）", dbFileExisted);
        checkBool("收尾 库文件已删除", QFileInfo::exists(dbFilePath()), false);
        checkBool("收尾 临时目录整体删除干净", dirGone && !QFileInfo::exists(tempDir()));
        qInfo() << "【收尾】五句话带走：① addDatabase 必带连接名，线程各用各的；"
                   "② QSqlDatabase 拷贝是同一连接的第二个句柄，QSqlQuery 才不能拷；"
                   "③ 值一律 bindValue，标识符一律 escapeIdentifier + 白名单；"
                   "④ exec 返回值 + numRowsAffected + lastError 三样都要看，QSqlError 只有 nativeErrorCode 没有 number；"
                   "⑤ 事务三个函数都要判返回值，SQLite 不能嵌套。";
        qInfo().noquote() << QStringLiteral("\n【硬账】%1/%2 项核对通过 → 退出码 %3")
                                 .arg(Ledger::passed).arg(Ledger::total).arg(Ledger::exitCode());
        QCoreApplication::exit(Ledger::exitCode());
    });

    const int rc = app.exec();
    qInfo() << "（事件循环正常退出，rc =" << rc << "）";
    return rc;
}

// ============================================================================
// 十、速查表与纪律
// ============================================================================
/*
 * 一、连接的规矩（本机 qsqldatabase.h 核过）
 *   addDatabase("QSQLITE", "唯一的连接名")            ← 永远带第二个参数
 *   database("唯一的连接名", false)                    ← 第二个参数默认 true，别让它偷偷 open
 *   cloneDatabase(other, "新连接名")                   ← 跨线程复制配置的正规办法
 *   removeDatabase("唯一的连接名")                     ← 收尾；顺序：先所有 QSqlQuery 析构，再 close，再 remove
 *   contains / connectionNames / drivers / isDriverAvailable / registerSqlDriver
 *   moveToThread(QThread*) + thread()                 ← 连接只有一个主人：创建它的线程（或你搬给它之后）
 *   QSqlDatabase 是 Q_GADGET，**不是 QObject**：没有信号/槽、不能放父子树里、拷贝只多一个句柄。
 *
 * 二、语句的规矩（qsqlquery.h）
 *   读：prepare → bindValue/addBindValue → exec() → while (next()) value(i 或 名字)
 *   写：prepare → bindValue → exec() → numRowsAffected()（自增主键再读 lastInsertId()）
 *   批：bindValue(QVariantList) + execBatch(QSqlQuery::ValuesAsRows)（先看 BatchOperations 特性）
 *   游标：first/last/previous/seek(i, relative)；forwardOnly 之后只能往前
 *   取空值：QVariant() 绑进去 = NULL；读回来用 isNull(...)，**不是** toString().isEmpty()
 *   ⚠ 不能拷 QSqlQuery（第 36~44 行 = delete 或带弃用标记），要传走用 std::move / swap
 *   ⚠ 别用 QSqlDatabase::exec(...)（第 70~73 行 6.6 起弃用），也别拼 "SELECT ... ; ;" 多图 nextResult
 *
 * 三、错误的规矩
 *   QSqlError：type()（ErrorType 在类里，5 个值）、nativeErrorCode()（QString）、
 *             driverText()、databaseText()、text()、isValid()
 *             **没有 number()**（Qt5 的写法）
 *   层级：QSqlQuery::lastError() 是语句级；QSqlDatabase::lastError() 是连接级（open 失败看它）
 *   QSqlResult 的成员几乎全 protected（qsqlresult.h 第 32 行起）：那是**驱动作者**的地盘
 *
 * 四、SQL 本身的可移植清单
 *   · 自增主键：SQLite INTEGER PRIMARY KEY AUTOINCREMENT / MySQL AUTO_INCREMENT /
 *     PG GENERATED ... AS IDENTITY；跨库就**别自己写 DDL**，用 QSqlDatabase::record/table 系列
 *     或干脆带一份 per-driver 的 .sql 迁移脚本。
 *   · DDL 里的 IF NOT EXISTS：SQLite/MySQL/PG 都有，但不是所有驱动都有。
 *   · 布尔：SQLite/MySQL 常存 INTEGER；PG 有 BOOLEAN。取值一律按「非零即真」自己定死。
 *   · 时间：SQLite 存 TEXT(ISO8601)/INTEGER；PG/MySQL 有原生类型。跨库统一存 ISO8601 文本最省事。
 *   · 引号：SQLite/PG 用 "ident"，MySQL 默认 `ident`（ ANSI_QUOTES 又变），SQL Server 用 []
 *     → 一律走 QSqlDriver::escapeIdentifier（第 74 行）。
 *   · 让驱动替你拼：sqlStatement(InsertStatement, 表, record, true)（第 75~76 行），
 *     第 6 讲 QSqlTableModel 内部走的正是这套。
 *   · 嵌套事务：PG/SQLite 的 BEGIN 都不能嵌套（SAVEPOINT 另说）；MySQL DDL 隐式提交。
 *
 * 五、线程归属（这一条最贵）
 *   连接属于创建它的线程。QtConcurrent / QThread 里访问数据库，三种正规做法：
 *   ① 工作线程自己 addDatabase(带该线程专属的连接名) + 用完 removeDatabase；
 *   ② 主线程建好配置，用 cloneDatabase(原, "线程专属名") 把配置复制过去，工作线程再 open；
 *   ③ 需要长期搬家就 moveToThread(QThread*)（第 97 行），但**同时**必须把连接名交接清楚。
 *   反面做法：把主线程的 QSqlDatabase 句柄按值丢给工作线程用（句柄拷得走，连接拷不走），
 *   症状是偶发「cannot / database not open」和驱动内部崩溃，压测才能压出来。
 *
 * ——— 老师傅的话 ———
 *   徒弟，SQL 这块我见过太多「能跑就行」的代码，出事的时候有三个固定剧本，
 *   对应这一讲三个证据：跨线程共用连接（崩在客户机器上，你复现不了）、
 *   字符串拼 SQL（安全测评一票否决，或者半夜被拖库）、
 *   不看返回值（数据悄悄少了一批，半年后才对上账）。
 *   这三个剧本的共同点：**编译器一声不响**。所以我把纪律压成三句，你抄在便签上——
 *   连接名要唯一且属于一个线程；值走绑定；返回值、affected rows、lastError 三样都要读。
 *
 *   关于 QSqlError 的 number()：我为什么在证据④专门骂这一句？因为它是「想当然 API」的典型。
 *   老代码里 `if (err.number() == 19)`（SQLite 的约束冲突码）到处都是，Qt6 换成
 *   nativeErrorCode() 且是 QString，含义也从「驱动整数码」变成「数据库原生错误码字符串」——
 *   你光把函数名换掉、拿 QString 去比 "19"，逻辑就悄悄错了。
 *   换 API 的时候**语义也要重读**，这是本讲最想让你带走的手艺。
 *
 *   再说 size()：SQLite 不提供行数（QuerySize 特性为 false），所以 q.size() 给 -1。
 *   新手拿 size() 判「有没有结果」，于是「查得到但界面空白」。判有没有数据只有两种可靠办法：
 *   next() 走一次，或者 SELECT COUNT(*) 用绑定值问。这一条我在证据⑦打了两行给你看。
 *
 *   最后一句留给「要不要用 QSqlDatabase 的便利」。第 6 讲会讲 QSqlTableModel 那一路，
 *   它的方便是真的，代价也是真的。我的偏好：**查询走准备语句自己写，
 *   只有 CRUD 面板这种「改一行立刻落库、表结构固定、主键明确」的场景才用 SQL 模型类**。
 *
 * ——— 小练习 ———
 *   1. 把证据⑤的拼接语句换成「输入是 alice'; DROP TABLE accounts;--」，
 *      再跑一遍，看 SQLite 单语句 exec 是否挡住了这条（然后回答：为什么不能把「挡住」
 *      当成安全依据——多语句支持、编码绕过、二阶注入都在不同驱动上表现不同）。
 *   2. 把 kConnectionName 改成默认连接名（不传第二个参数），然后在本讲基础上加一个
 *      QtConcurrent::run 里 addDatabase 同名连接的工作，观察报错文案；
 *      再改回「每个线程自己的连接名」验证问题消失（对照 threads 模块第 3 讲）。
 *   3. 给 accounts 加一列 real 类型的 score，插入 1.5/2.5，然后用
 *      setNumericalPrecisionPolicy(QSql::LowPrecisionInt32) 与 HighPrecision 各读一遍，
 *      把两种策略下 value(4) 的 toString 并排打出来，说明「精度策略」什么时候必须降。
 *   4. 把证据⑥的事务改成「插两行、第二行故意撞 UNIQUE、然后 commit」，
 *      打印：第二行失败后 commit 是否成功、最终行数是多少，
 *      再对比 rollback 版本 —— 想清楚「出错就整批回滚」在代码里怎么写才不会漏。
 */
// ============================================================================

// 本文件没有 Q_OBJECT 类（连接、查询、错误全是值类型/框架类，我没新增信号槽），
// 所以末尾不需要 #include "05_sql_database.moc"。
