// ============================================================================
// 第 6 讲 · 数据库接上界面：模型是缓存，视图是投影，中间还隔着一层代理
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   第 5 讲把 SQL 讲到了「能用」：连接名、绑定值、事务、错误。这一讲要把它接上界面。
//   新手接界面时几乎必然踩同三个坑：
//     · 「我 setData 了，可数据库里没变」 —— 编辑策略（editStrategy）在替你踩刹车；
//     · 「我直接 UPDATE 了表，界面不动」 —— 模型是**缓存**，它不知道库变了；
//     · 「我 setFilter 之后，用户的输入没了」 —— 重新查询会把未提交的改动丢掉。
//   三个坑是同一件事的三张脸：**模型不是数据库，视图不是模型**。
//   中间每多一层，就多一个「什么时候同步」的问题。
//
//   八节地图（每一节都可运行、可打印、硬账进退出码）：
//     1) 两种模型的分工：QSqlQueryModel 是**只读快照**（它连 setData 都没声明，
//        qsqlquerymodel.h 里根本没有这个名字），QSqlTableModel 是**可编辑的行集合**
//        （qsqltablemodel.h 第 42 行 setData、第 37 行 flags 都在）。
//        这一节用「同一个问题问两个模型」把它们钉开。
//     2) 三种编辑策略：OnFieldChange / OnRowChange / OnManualSubmit
//        （qsqltablemodel.h 第 29 行那个枚举，顺序都写在头文件里）。
//        「我改了为什么库里没变」的答案就在这一节的实测数字里。
//     3) setFilter / setSort 会重新查询。重新查询和「未提交的改动」怎么相处？
//        ★ 这一节我把「会不会丢」记 Soft —— 文档与历史行为都说会丢，但那条策略在
//          .cpp 里，我读不到；硬账只记「显式提交之后一定不丢」。
//     4) 外键：QSqlRelationalTableModel 让 dept_id 显示成「研发部」。
//        必须 setRelation 之后再 select()：这一条有确定的语义，记硬账。
//     5) 接上 QTableView：模型归谁、选择怎么读、列怎么藏、表头怎么改，
//        以及「某列只读」要子类化 flags() —— 这是唯一正确的做法。
//     6) 代理模型：排序与过滤发生在**视图和模型之间**，数据库一点都不知道。
//        这一节证明「代理不复制数据」：改代理就是改源模型。
//     7) 陈旧：拿 QSqlQuery 直接 UPDATE，模型不知道。这一节量化「模型是缓存」。
//     8) 大表：fetchMore / canFetchMore（qsqlquerymodel.h 第 60/61 行）。
//        「初始 rowCount 是多少」记 Obs（驱动决定怎么分批），
//        但「一直 fetchMore 到底，总数一定对」是**两条分支同结论**，记硬账。
//
//   ★ 运行前须知：
//     · 用 QSQLITE 内存库（":memory:"），一个文件都不写；每个连接有自己的库。
//     · 每种「库里的值」都用一条独立 SELECT 去核，不靠模型自己汇报 ——
//       模型说「我改了」和库说「我改了」是两件事，这一讲从头到尾在区分它们。
//     · 本讲**没有事件循环**。这是刻意的：模型/视图的交互全是同步的，
//       说明了 MVC 不是并发机制，它只是「一份数据，多个投影」的分工。
//     · 没有 Q_OBJECT 类 —— 本讲没有新增信号槽，全是框架类的组合。
// ============================================================================

// ============================================================================
// 一、头文件地图
// ----------------------------------------------------------------------------
// QtSql/qsqlquerymodel.h
//   第 30/31 行 int rowCount(...) / int columnCount(...)   override
//   第 32/33 行 QSqlRecord record(int row) / record()
//   第 35 行 QVariant data(const QModelIndex &, int role) override
//   第 38 行 bool setHeaderData(int, Qt::Orientation, const QVariant &, int)
//              ← ★ QSqlTableModel 没有自己声明 setHeaderData，它就地继承这一个。
//   第 46~49 行 setQuery 三个重载（QSqlQuery 左值 / 右值 / SQL 字符串 + db）
//   第 52 行 QSqlQuery query() const
//   第 56 行 virtual void clear()
//   第 58 行 QSqlError lastError() const
//   第 60/61 行 fetchMore / canFetchMore   override      ← 【8】的主角
//   ★★ 注意这里**没有** setData，也没有 flags。只读不是因为它拒绝了写，
//      而是因为这台机器上根本没有那个齿轮 —— 写请求落到 QAbstractItemModel
//      的默认实现，它返回 false（默认 flags 只有 ItemIsEnabled|ItemIsSelectable）。
//
// QtSql/qsqltablemodel.h
//   第 29 行 enum EditStrategy { OnFieldChange, OnRowChange, OnManualSubmit };
//   第 34/35 行 virtual setTable / tableName
//   第 37 行 Qt::ItemFlags flags(...) override               ← 可编辑的入口
//   第 39/40 行 record() / record(int row)
//   第 41 行 QVariant data(...) override
//   第 42 行 bool setData(...) override                      ← 可写的入口
//   第 47/48 行 bool isDirty() / isDirty(const QModelIndex &)
//              ← ★ 「有没有还没写回去的改动」，是这一讲最重要的一个查询
//   第 52/53 行 setEditStrategy / editStrategy
//   第 55 行 QSqlIndex primaryKey() const
//              ← ★ 没有主键的表，编辑行为无从落地（【2】末尾实测给结论）
//   第 59/60 行 sort / setSort
//   第 62/63 行 filter / setFilter
//   第 65 行 rowCount(...) override
//   第 68/69 行 removeRows / insertRows
//   第 71/72 行 insertRecord / setRecord
//   第 74 行 revertRow(int)
//   第 77/78 行 select() / selectRow(int)
//   第 80/81 行 submit() / revert()          ← 只影响「当前行」
//   第 83/84 行 submitAll() / revertAll()    ← 影响全部脏行
//
// QtSql/qsqlrelationaltablemodel.h
//   第 53 行 enum JoinMode（InnerJoin / LeftJoin）
//   第 67 行 select() override          ← ★ setRelation 之后要重新 select 才生效
//   第 70/71 行 setRelation(int column, const QSqlRelation &) / relation(int)
//   第 72 行 QSqlTableModel *relationModel(int column) const  ← 那张「查表用的表」
//   第 73 行 setJoinMode(...)
//
// QtCore/qsortfilterproxymodel.h
//   第 55 行 setSourceModel(QAbstractItemModel *) override
//   第 57/58 行 mapToSource / mapFromSource        ← ★ 代理的全部秘密就这两行
//   第 67 行 setFilterKeyColumn(int)
//   第 90 行 setSortRole(int)
//   第 121/122 行 setFilterRegularExpression（Qt6 的正名）
//   第 124 行 setFilterFixedString                 ← 子串匹配，不是正则
//   第 160 行 rowCount(...) override
//   第 190 行 sort(int, Qt::SortOrder) override
//
// 跨讲链接：
//   · storage 第 5 讲：连接名、绑定值、事务、QSqlError。本讲假定你已经会用。
//   · model_view 模块：QAbstractItemModel 的 index/role/flags 三条基本规则。
//   · threads 第 3 讲：为什么一个模型不许跨线程用（视图只在主线程碰它）。
// ============================================================================

// ============================================================================
// 二、口径与账本
// ----------------------------------------------------------------------------
//   Hard —— 「模型说了」和「库说了」分开核：本讲绝大多数硬账都是拿一条独立 SQL
//           去问数据库本人，而不是问模型。这样「模型在撒谎」这种情况才会被抓住。
//   Soft —— 实现策略不在头文件里、读不到源码的，一律 Soft（比如【3】的丢改动）。
//   Obs  —— 只报数值（初始行数、取了几批、EditRole 里到底是什么）。
// ============================================================================

#include <QApplication>          // 本讲要建 QTableView，所以是 QApplication 不是 QCoreApplication
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QSqlIndex>
#include <QSqlQueryModel>
#include <QSqlTableModel>
#include <QSqlRelationalTableModel>
#include <QSqlRelation>
#include <QTableView>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QSortFilterProxyModel>
#include <QVariant>
#include <QString>
#include <QStringList>
#include <QDebug>

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

static void checkStr(const QString &label, const QString &got, const QString &want)
{
    verdict(Hard, label, got == want,
            QStringLiteral("「%1」（期望「%2」）").arg(got, want));
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

// ============================================================================
// 三、连接与会话：为什么必须「先扔掉句柄，再 removeDatabase」
// ----------------------------------------------------------------------------
// QSqlDatabase 是一个**句柄**（Q_GADGET，不是 QObject）。removeDatabase(name) 的
// 前提是「这个连接的所有句柄都已经销毁」，否则 Qt 会打一行
// 「connection 'x' is still in use」然后什么也不做。
// 最常见的漏法是：成员变量 m_db 还活着就去 removeDatabase —— 析构函数里顺序错了。
// 所以这里的 ~DbSession 先把 m_db 赋成空句柄，再删连接。
// ============================================================================

class DbSession
{
public:
    explicit DbSession(const QString &tag)
        : m_name(QStringLiteral("s06_%1").arg(tag))
    {
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name);
        m_db.setDatabaseName(QStringLiteral(":memory:"));
    }

    ~DbSession()
    {
        m_db.close();
        m_db = QSqlDatabase();          // ★ 先扔掉句柄
        QSqlDatabase::removeDatabase(m_name);   // ★ 再删连接
    }

    QSqlDatabase db() const { return m_db; }
    QString name() const { return m_name; }

private:
    QString m_name;
    QSqlDatabase m_db;
};

static bool exec(QSqlDatabase db, const QString &sql)
{
    QSqlQuery q(db);
    if (!q.exec(sql)) {
        qWarning().noquote() << QStringLiteral("SQL 失败：%1\n  语句：%2")
                                    .arg(q.lastError().text(), sql);
        return false;
    }
    return true;
}

// 直接问数据库本人：这一列的 SELECt 出来是什么。
static QVariant scalar(QSqlDatabase db, const QString &sql)
{
    QSqlQuery q(db);
    if (!q.exec(sql) || !q.next())
        return QVariant();
    return q.value(0);
}

// ============================================================================
// 【1】两种模型的分工
// ============================================================================
static void phase1_twoKindsOfModels(QSqlDatabase db)
{
    section(QStringLiteral("【1】QSqlQueryModel（只读快照） vs QSqlTableModel（可编辑的行）"));

    QSqlQueryModel snapshot;
    snapshot.setQuery(QStringLiteral("SELECT id, name, salary FROM emp ORDER BY id"), db);
    if (!premise(QStringLiteral("【1】快照模型查出了 5 行"),
                 snapshot.rowCount() == 5,
                 QStringLiteral("rowCount = %1，lastError = %2")
                     .arg(snapshot.rowCount()).arg(snapshot.lastError().text())))
        return;

    checkNum(QStringLiteral("【1A】快照模型列数"), snapshot.columnCount(), 3);
    checkStr(QStringLiteral("【1B】第 1 行第 2 列"),
             snapshot.data(snapshot.index(0, 1)).toString(), QStringLiteral("张三"));
    const QModelIndex cell = snapshot.index(0, 1);
    checkBool(QStringLiteral("【1C】快照模型的 flags 里没有 ItemIsEditable"),
              (snapshot.flags(cell) & Qt::ItemIsEditable) != 0, false);
    checkBool(QStringLiteral("【1D】对快照模型 setData 会被拒"),
              snapshot.setData(cell, QStringLiteral("张三丰")), false);

    // ★ 为什么这两条是硬账：qsqlquerymodel.h 里**没有** setData、**没有** flags。
    //   所以这两个调用落到 QAbstractItemModel 的默认实现：setData 返回 false，
    //   flags 只有 Enabled|Selectable。这不是「它拒绝了写」，是「它没有写的齿轮」。

    QSqlTableModel table(nullptr, db);
    table.setTable(QStringLiteral("emp"));
    if (!premise(QStringLiteral("【1】表模型 select() 成功"),
                 table.select(),
                 QStringLiteral("lastError = %1").arg(table.lastError().text())))
        return;

    checkNum(QStringLiteral("【1E】表模型行数（和快照一样）"), table.rowCount(), 5);
    checkBool(QStringLiteral("【1F】刚查完不属于「脏」"), table.isDirty(), false);
    checkBool(QStringLiteral("【1G】表模型的 flags 里有 ItemIsEditable"),
              (table.flags(table.index(0, 1)) & Qt::ItemIsEditable) != 0, true);

    // 默认编辑策略是 OnRowChange；这里先不依赖它，把策略摆到最可控的 OnManualSubmit。
    table.setEditStrategy(QSqlTableModel::OnManualSubmit);
    checkBool(QStringLiteral("【1H】setData 成功返回 true"),
              table.setData(table.index(0, 1), QStringLiteral("张三丰")), true);
    checkBool(QStringLiteral("【1I】这时候模型是脏的"), table.isDirty(), true);
    checkNum(QStringLiteral("【1J】★ 但数据库里还没变（问库本人）"),
             scalar(db, QStringLiteral("SELECT COUNT(*) FROM emp WHERE name='张三丰'")).toInt(), 0);
    checkStr(QStringLiteral("【1K】模型自己报告的已经是新值"),
             table.data(table.index(0, 1)).toString(), QStringLiteral("张三丰"));

    qInfo().noquote() << QStringLiteral(
        "  → 这一对数字（【1J】=0 与【1K】=张三丰）就是「模型是缓存」的第一份证据："
        "\n     用户看到的是缓存，库里还是旧值。谁负责把它们对齐？下一节的策略。");

    if (!premise(QStringLiteral("【1】submitAll() 成功"),
                 table.submitAll(),
                 QStringLiteral("lastError = %1").arg(table.lastError().text())))
        return;
    checkNum(QStringLiteral("【1L】提交之后库里有了"),
             scalar(db, QStringLiteral("SELECT COUNT(*) FROM emp WHERE name='张三丰'")).toInt(), 1);
    checkBool(QStringLiteral("【1M】提交之后不脏了"), table.isDirty(), false);

    table.setData(table.index(0, 1), QStringLiteral("张四"));
    table.revertAll();
    checkStr(QStringLiteral("【1N】revertAll() 把改动退回库里的值"),
             table.data(table.index(0, 1)).toString(), QStringLiteral("张三丰"));

    // 恢复成初始数据，免得影响后面的节
    table.submitAll();
    QSqlQuery fix(db);
    fix.exec(QStringLiteral("UPDATE emp SET name='张三' WHERE id=1"));
}

// ============================================================================
// 【2】三种编辑策略
// ============================================================================
static void phase2_editStrategies(QSqlDatabase db)
{
    section(QStringLiteral("【2】OnFieldChange / OnRowChange / OnManualSubmit：改在哪一刻落地"));

    QSqlTableModel table(nullptr, db);
    table.setTable(QStringLiteral("emp"));
    if (!premise(QStringLiteral("【2】表模型查表成功"), table.select(),
                 QStringLiteral("lastError = %1").arg(table.lastError().text())))
        return;

    verdict(Obs, QStringLiteral("【2】默认编辑策略"), true,
            QStringLiteral("%1（枚举序：OnFieldChange=0, OnRowChange=1, OnManualSubmit=2）")
                .arg(int(table.editStrategy())));

    // ① OnFieldChange：改一个字段就写一次库。
    table.setEditStrategy(QSqlTableModel::OnFieldChange);
    table.setData(table.index(1, 2), 11111);
    checkNum(QStringLiteral("【2A】OnFieldChange：setData 之后库里立刻变了"),
             scalar(db, QStringLiteral("SELECT salary FROM emp WHERE id=2")).toInt(), 11111);

    // ② OnRowChange：改的时候只在缓存里；离开这一行时才提交。
    table.setEditStrategy(QSqlTableModel::OnRowChange);
    table.setData(table.index(2, 2), 22222);
    checkNum(QStringLiteral("【2B】OnRowChange：还在这一行里，库里没变"),
             scalar(db, QStringLiteral("SELECT salary FROM emp WHERE id=3")).toInt(), 0);
    // ★ 这一条的依据是策略名字的字面语义（qsqltablemodel.h 第 29 行的枚举）：
    //   「换到另一行时提交」。所以改完第 3 行、再改第 4 行，第 3 行就应该落库了。
    table.setData(table.index(3, 2), 33333);
    checkNum(QStringLiteral("【2C】OnRowChange：换行之后，上一行的改动落库"),
             scalar(db, QStringLiteral("SELECT salary FROM emp WHERE id=3")).toInt(), 22222);

    // ③ OnManualSubmit：只有 submitAll()/submit() 才写。
    table.setEditStrategy(QSqlTableModel::OnManualSubmit);
    table.setData(table.index(4, 2), 44444);
    checkNum(QStringLiteral("【2D】OnManualSubmit：setData 之后库里没变"),
             scalar(db, QStringLiteral("SELECT salary FROM emp WHERE id=4")).toInt(), 0);
    checkBool(QStringLiteral("【2E】OnManualSubmit：模型是脏的"), table.isDirty(), true);
    table.submitAll();
    checkNum(QStringLiteral("【2F】OnManualSubmit：submitAll 之后落库"),
             scalar(db, QStringLiteral("SELECT salary FROM emp WHERE id=4")).toInt(), 44444);

    // 主键：没有主键的表怎么编辑？
    checkNum(QStringLiteral("【2G】emp 的主键列数"), table.primaryKey().count(), 1);
    verdict(Obs, QStringLiteral("【2】主键是哪一列"), true,
            table.primaryKey().fieldName(0));

    if (exec(db, QStringLiteral("CREATE TABLE nopk (v TEXT)"))
        && exec(db, QStringLiteral("INSERT INTO nopk VALUES ('a')"))) {
        QSqlTableModel noKey(nullptr, db);
        noKey.setTable(QStringLiteral("nopk"));
        noKey.setEditStrategy(QSqlTableModel::OnManualSubmit);
        noKey.select();
        const bool accepted = noKey.setData(noKey.index(0, 0), QStringLiteral("b"));
        const bool submitted = noKey.submitAll();
        // ★ 这一条记 Soft：主键缺失时 QSqlTableModel 内部怎么拒绝，源码在 .cpp 里，
        //   头文件只告诉你 primaryKey() 存在（第 55 行）。「应当拒绝提交」是文档的说法，
        //   但拒绝发生在 setData 还是 submitAll，我没法从头文件里证明，所以不记硬账。
        verdict(Soft, QStringLiteral("【2H】无主键表：setData 被接受但 submitAll 应当失败"),
                accepted && !submitted,
                QStringLiteral("setData=%1，submitAll=%2（两种情况都合理：拒绝可能发生在任一步）")
                    .arg(accepted ? QStringLiteral("true") : QStringLiteral("false"),
                         submitted ? QStringLiteral("true") : QStringLiteral("false")));
        verdict(Obs, QStringLiteral("【2】无主键表：库里最终是什么"), true,
                scalar(db, QStringLiteral("SELECT v FROM nopk")).toString());
    }

    // 复原
    if (exec(db, QStringLiteral("UPDATE emp SET salary=0")))
        qInfo().noquote() << QStringLiteral("  → 已把 salary 复原成 0，后面的节从干净状态开始。");

    qInfo().noquote() << QStringLiteral(
        "  → 三种策略怎么选，只有一个判断标准：**用户能不能容忍「刚填的东西突然没了」**。"
        "\n     · 单个开关 / 单个数字：OnFieldChange，改完就走，简单；"
        "\n     · 一个表单（多字段要一起成立）：OnManualSubmit + 一个「保存」按钮；"
        "\n     · 表格里逐行编辑：OnRowChange（离开这一行才算改完）。"
        "\n     混合策略是灾难 —— 所以 setEditStrategy 应该在 setTable 之后立刻定下来，"
        "\n     别在事件处理里来回改。");
}

// ============================================================================
// 【3】重新查询与未提交的改动
// ============================================================================
static void phase3_reselectDiscards(QSqlDatabase db)
{
    section(QStringLiteral("【3】setFilter / setSort 会重新查询：未提交的改动跟它怎么相处"));

    QSqlTableModel table(nullptr, db);
    table.setTable(QStringLiteral("emp"));
    table.setEditStrategy(QSqlTableModel::OnManualSubmit);
    if (!premise(QStringLiteral("【3】表模型查表成功"), table.select(),
                 QStringLiteral("lastError = %1").arg(table.lastError().text())))
        return;

    const int original = table.data(table.index(0, 1)).toString().size();

    // 造一个未提交的改动，然后在「没提交」的状态下重新查询。
    table.setData(table.index(0, 1), QStringLiteral("半截输入没保存"));
    const bool dirtyBefore = table.isDirty();
    table.setFilter(QStringLiteral("salary >= 0"));

    // ★ Soft：select() 会把未提交的改动丢掉，这是文档和历史行为一致的结论，
    //   但那条策略在 qsqltablemodel.cpp 里，头文件看不到。所以不记硬账 ——
    //   万一某个版本改成「保留」，这里只会打印一行「这次没命中」，不会误红。
    verdict(Soft, QStringLiteral("【3A】重新查询之前是脏的"), dirtyBefore,
            QStringLiteral("isDirty = %1").arg(dirtyBefore));
    verdict(Soft, QStringLiteral("【3B】setFilter 之后 isDirty 应当已经清零"),
            !table.isDirty(), QStringLiteral("isDirty = %1").arg(table.isDirty()));
    verdict(Soft, QStringLiteral("【3C】那一行的值应当回到库里的旧值"),
            table.data(table.index(0, 1)).toString() != QStringLiteral("半截输入没保存"),
            QStringLiteral("现在是「%1」").arg(table.data(table.index(0, 1)).toString()));
    verdict(Obs, QStringLiteral("【3】旧值的长度（对照用）"), true,
            QStringLiteral("%1 字符").arg(original));

    // ★ 硬账：先提交、再过滤 —— 这条无论实现怎么变都成立，因为值是**已经写进库**的。
    table.setFilter(QString());
    table.select();
    table.setData(table.index(1, 1), QStringLiteral("已提交的名字"));
    const bool submitted = table.submitAll();
    checkBool(QStringLiteral("【3D】显式提交成功"), submitted, true);
    table.setFilter(QStringLiteral("salary >= 0"));
    checkStr(QStringLiteral("【3E】先提交再过滤：改动一定还在（模型里）"),
             table.data(table.index(1, 1)).toString(), QStringLiteral("已提交的名字"));
    checkNum(QStringLiteral("【3F】先提交再过滤：改动一定还在（库里）"),
             scalar(db, QStringLiteral("SELECT COUNT(*) FROM emp WHERE name='已提交的名字'")).toInt(), 1);

    // 排序同理。
    table.setSort(2, Qt::DescendingOrder);
    table.select();
    checkNum(QStringLiteral("【3G】重新排序之后，已提交的改动仍然在库里"),
             scalar(db, QStringLiteral("SELECT COUNT(*) FROM emp WHERE name='已提交的名字'")).toInt(), 1);

    QSqlQuery fix(db);
    fix.exec(QStringLiteral("UPDATE emp SET name='李四' WHERE id=2"));
    table.setFilter(QString());
    table.select();

    qInfo().noquote() << QStringLiteral(
        "  → 把这条写成规矩：**任何会触发重新查询的动作（setFilter / setSort / select），"
        "\n     前面都要有一句 submitAll() 或者一个明确的「放弃修改」确认框。**"
        "\n     用户的半截输入不该被一次排序默默吃掉。"
        "\n     更稳的做法：把表单字段和模型分开 —— 表单是表单，模型是模型，"
        "\n     用户点「保存」时一次性构造 SQL 写下去。");
}

// ============================================================================
// 【4】外键：把 dept_id 显示成「研发部」
// ============================================================================
static void phase4_relational(QSqlDatabase db)
{
    section(QStringLiteral("【4】QSqlRelationalTableModel：外键显示成人看得懂的名字"));

    QSqlTableModel plain(nullptr, db);
    plain.setTable(QStringLiteral("emp"));
    if (!premise(QStringLiteral("【4】先看一眼「没有关系表的模型」长什么样"), plain.select(),
                 QStringLiteral("lastError = %1").arg(plain.lastError().text())))
        return;
    checkStr(QStringLiteral("【4A】没有关系表时，第 2 列就是那个整数"),
             plain.data(plain.index(0, 2)).toString(), QStringLiteral("1"));

    QSqlRelationalTableModel rel(nullptr, db);
    rel.setTable(QStringLiteral("emp"));
    // ★ 顺序是要紧的：setRelation 必须在 select 之前，否则已经查出来的数据不会重建。
    //   setRelation 自己会调用 select()（文档语义），但为了不吃惊，这里显式再查一次。
    rel.setRelation(2, QSqlRelation(QStringLiteral("dept"), QStringLiteral("id"),
                                    QStringLiteral("name")));
    if (!premise(QStringLiteral("【4】关系表模型 select() 成功"), rel.select(),
                 QStringLiteral("lastError = %1").arg(rel.lastError().text())))
        return;

    checkStr(QStringLiteral("【4B】DisplayRole 变成了部门名"),
             rel.data(rel.index(0, 2)).toString(), QStringLiteral("研发部"));
    checkBool(QStringLiteral("【4C】relationModel(2) 拿得到那张部门表"),
              rel.relationModel(2) != nullptr, true);
    if (rel.relationModel(2))
        checkNum(QStringLiteral("【4D】部门表里有 3 个部门"),
                 rel.relationModel(2)->rowCount(), 3);

    // EditRole 里到底是什么？这是「给编辑器看的值」和「给人看的值」的分界线。
    // 结论落在 .cpp 里，所以我只报实测，两种都合理。
    verdict(Obs, QStringLiteral("【4】同一个格子里 EditRole 的值"), true,
            rel.data(rel.index(0, 2), Qt::EditRole).toString());

    // 关系列能不能直接写？写进去的应该是 id 还是名字？—— 这是最常见的误用。
    rel.setData(rel.index(0, 2), QStringLiteral("2"));
    const QVariant afterWrite = scalar(db, QStringLiteral("SELECT dept_id FROM emp WHERE id=1"));
    verdict(Obs, QStringLiteral("【4】往关系列写入 \"2\" 之后，库里的 dept_id"), true,
            afterWrite.toString());
    QSqlQuery fix(db);
    fix.exec(QStringLiteral("UPDATE emp SET dept_id=1 WHERE id=1"));
    rel.select();

    checkStr(QStringLiteral("【4E】复原之后又显示回部门名"),
             rel.data(rel.index(0, 2)).toString(), QStringLiteral("研发部"));

    qInfo().noquote() << QStringLiteral(
        "  → 这一节只解决「显示」。要在界面上**编辑**外键，还得给这一列装一个"
        "\n     QSqlRelationalDelegate（视图侧的事），它会把关系表变成一个下拉框。"
        "\n     没有它，用户只会看到「研发部」却改不动 —— 这不是 bug，是分工。"
        "\n     要按部门名过滤？用 setFilter 只能过滤本表的列；跨表过滤请写视图（JOIN）。");
}

// ============================================================================
// 【5】接上 QTableView
// ============================================================================
// 只读列的**正确**做法：子类化并改 flags()。
// ★ 很多人想的是「不显示编辑器」或者「在 setData 里判断」—— 前者能被 Tab 键绕过，
//   后者是防线放错了位置。flags() 是模型对视图说的唯一一句话：
//   「这个格子能不能编辑」。改这一句，所有视图（表格、树、列表、代理）同时生效。
//   这个类没有 Q_OBJECT：它不加信号槽，只改了一个虚函数。
// ============================================================================
class ReadOnlyIdModel : public QSqlTableModel
{
public:
    using QSqlTableModel::QSqlTableModel;

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        Qt::ItemFlags f = QSqlTableModel::flags(index);
        if (index.isValid() && index.column() == 0)
            f &= ~Qt::ItemIsEditable;
        return f;
    }
};

static void phase5_view(QSqlDatabase db)
{
    section(QStringLiteral("【5】QTableView：模型归谁、选择怎么读、列怎么藏、表头怎么改"));

    ReadOnlyIdModel model(nullptr, db);
    model.setTable(QStringLiteral("emp"));
    model.setEditStrategy(QSqlTableModel::OnManualSubmit);
    if (!premise(QStringLiteral("【5】模型查表成功"), model.select(),
                 QStringLiteral("lastError = %1").arg(model.lastError().text())))
        return;

    QTableView view;
    view.setModel(&model);
    checkBool(QStringLiteral("【5A】视图记住了我们给的模型"), view.model() == &model, true);
    checkBool(QStringLiteral("【5B】setModel 顺手建了一个选择模型"),
              view.selectionModel() != nullptr, true);
    checkNum(QStringLiteral("【5C】视图看到 5 行"), view.model()->rowCount(), 5);

    // 选择：让人选中第 2 行（0 基是 1）。
    view.setSelectionBehavior(QAbstractItemView::SelectRows);
    view.selectRow(1);
    const QModelIndexList picked = view.selectionModel()->selectedRows();
    checkNum(QStringLiteral("【5D】选中行的个数"), picked.size(), 1);
    if (!picked.isEmpty())
        checkNum(QStringLiteral("【5E】选中的是第几行"), picked.first().row(), 1);

    // 藏列：id 列对用户没意义。
    view.setColumnHidden(0, true);
    checkBool(QStringLiteral("【5F】第 0 列被藏起来了"), view.isColumnHidden(0), true);

    // 表头：QSqlTableModel 自己没有 setHeaderData，用的是 QSqlQueryModel 第 38 行那个。
    const bool headerOk = model.setHeaderData(1, Qt::Horizontal, QStringLiteral("姓名"));
    checkBool(QStringLiteral("【5G】setHeaderData 成功"), headerOk, true);
    checkStr(QStringLiteral("【5H】表头读回来是新的"),
             model.headerData(1, Qt::Horizontal).toString(), QStringLiteral("姓名"));
    checkStr(QStringLiteral("【5I】视图的表头跟着变（它只是转发给模型）"),
             view.model()->headerData(1, Qt::Horizontal).toString(), QStringLiteral("姓名"));

    // 只读列：id 列的 flags 里没有 Editable，其它列有。
    checkBool(QStringLiteral("【5J】id 列不可编辑"),
              (model.flags(model.index(0, 0)) & Qt::ItemIsEditable) != 0, false);
    checkBool(QStringLiteral("【5K】name 列仍然可编辑"),
              (model.flags(model.index(0, 1)) & Qt::ItemIsEditable) != 0, true);
    // 再往下一层：setData 会不会自己尊重 flags？源码在 .cpp，只报实测。
    const bool idWrite = model.setData(model.index(0, 0), 99);
    verdict(Obs, QStringLiteral("【5】对只读列 setData 的返回值"), true,
            idWrite ? QStringLiteral("true") : QStringLiteral("false"));
    checkNum(QStringLiteral("【5L】不管返回值如何，id 没有被改掉"),
             scalar(db, QStringLiteral("SELECT id FROM emp ORDER BY id LIMIT 1")).toInt(), 1);

    qInfo().noquote() << QStringLiteral(
        "  → 视图从来不保存数据，它只**问**模型。所以："
        "\n     · 两个 QTableView 可以挂同一个模型 —— 一份数据两处显示，改一处两处都变；"
        "\n     · 但一个模型**不能**同时挂两个线程的视图（模型有线程归属，见 threads 第 3 讲）；"
        "\n     · 想让某个视图只读，别去禁用它，套一个代理或者改 flags —— 后者最干净。");
}

// ============================================================================
// 【6】代理模型：排序与过滤只影响视图看到的东西
// ============================================================================
static void phase6_proxy(QSqlDatabase db)
{
    section(QStringLiteral("【6】QSortFilterProxyModel：一层只改变「投影」的玻璃"));

    QSqlTableModel model(nullptr, db);
    model.setTable(QStringLiteral("emp"));
    model.setEditStrategy(QSqlTableModel::OnFieldChange);   // 让写入立刻落库，好核对
    if (!premise(QStringLiteral("【6】模型查表成功"), model.select(),
                 QStringLiteral("lastError = %1").arg(model.lastError().text())))
        return;
    if (!premise(QStringLiteral("【6】源模型有 5 行"), model.rowCount() == 5,
                 QStringLiteral("rowCount = %1").arg(model.rowCount())))
        return;

    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    checkNum(QStringLiteral("【6A】不设过滤时，代理看到全部 5 行"), proxy.rowCount(), 5);

    // 过滤：数一下源模型里有几行名字带「张」，不写死期望值。
    int expectZhang = 0;
    for (int r = 0; r < model.rowCount(); ++r) {
        if (model.data(model.index(r, 1)).toString().contains(QStringLiteral("张")))
            ++expectZhang;
    }
    proxy.setFilterKeyColumn(1);
    proxy.setFilterFixedString(QStringLiteral("张"));
    checkNum(QStringLiteral("【6B】代理只留下名字带「张」的行"), proxy.rowCount(), expectZhang);
    checkNum(QStringLiteral("【6C】源模型一行没少"), model.rowCount(), 5);

    // 排序：仍然由代理负责，源模型的物理顺序不变。
    proxy.setFilterFixedString(QString());
    proxy.sort(2, Qt::DescendingOrder);
    int maxSalary = 0;
    for (int r = 0; r < model.rowCount(); ++r)
        maxSalary = qMax(maxSalary, model.data(model.index(r, 2)).toInt());
    checkNum(QStringLiteral("【6D】降序排列后第一行的 salary 是最大值"),
             proxy.data(proxy.index(0, 2)).toInt(), maxSalary);
    checkStr(QStringLiteral("【6E】源模型的第一行还是原来的第一行"),
             model.data(model.index(0, 1)).toString(), QStringLiteral("张三"));

    // 代理不是复制品：通过它写，落到的就是源模型。
    const QModelIndex proxyCell = proxy.index(0, 1);
    const QModelIndex sourceCell = proxy.mapToSource(proxyCell);
    checkBool(QStringLiteral("【6F】mapToSource 指回源模型的一个有效格子"),
              sourceCell.isValid(), true);
    checkBool(QStringLiteral("【6G】通过代理 setData 成功"), proxy.setData(proxyCell, QStringLiteral("排第一的")), true);
    checkStr(QStringLiteral("【6H】源模型那一格也变了（同一个格子）"),
             model.data(sourceCell).toString(), QStringLiteral("排第一的"));
    checkNum(QStringLiteral("【6I】OnFieldChange 下，库里也变了"),
             scalar(db, QStringLiteral("SELECT COUNT(*) FROM emp WHERE name='排第一的'")).toInt(), 1);

    QSqlQuery fix(db);
    fix.exec(QStringLiteral("UPDATE emp SET name='王五' WHERE salary=5000 AND id<>1"));
    model.select();

    qInfo().noquote() << QStringLiteral(
        "  → 代理的全部秘密就是 mapToSource / mapFromSource 两行（qsortfilterproxymodel.h 57/58）。"
        "\n     它不存数据，它存一个「代理第 i 行 → 源第 j 行」的映射。"
        "\n     所以：排序/过滤可以叠很多层，每层只加一次映射，数据永远只有一份。"
        "\n     代价是每取一个格子都要走一遍映射 —— 上百万行 + 多层代理时，这就是性能瓶颈。");
}

// ============================================================================
// 【7】陈旧：直接改库，模型不知道
// ============================================================================
static void phase7_stale(QSqlDatabase db)
{
    section(QStringLiteral("【7】绕过模型直接 UPDATE：模型为什么会「不动」"));

    QSqlTableModel model(nullptr, db);
    model.setTable(QStringLiteral("emp"));
    if (!premise(QStringLiteral("【7】模型查表成功"), model.select(),
                 QStringLiteral("lastError = %1").arg(model.lastError().text())))
        return;

    const QString before = model.data(model.index(0, 1)).toString();

    // 绕过模型，直接用 SQL 改库。
    if (!premise(QStringLiteral("【7】绕过模型直接 UPDATE 成功"),
                 exec(db, QStringLiteral("UPDATE emp SET name='库里偷偷改的' WHERE id=1")),
                 QStringLiteral("id=1")))
        return;

    checkStr(QStringLiteral("【7A】模型里还是旧值 —— 它不是数据库的实时视图"),
             model.data(model.index(0, 1)).toString(), before);
    checkStr(QStringLiteral("【7B】但库里的值已经变了"),
             scalar(db, QStringLiteral("SELECT name FROM emp WHERE id=1")).toString(),
             QStringLiteral("库里偷偷改的"));

    model.select();
    checkStr(QStringLiteral("【7C】select() 之后模型才跟上"),
             model.data(model.index(0, 1)).toString(), QStringLiteral("库里偷偷改的"));

    QSqlQuery fix(db);
    fix.exec(QStringLiteral("UPDATE emp SET name='张三' WHERE id=1"));
    model.select();

    qInfo().noquote() << QStringLiteral(
        "  → 这不是缺陷，是分工。模型只保证「它查过的那一次」和界面一致。"
        "\n     所以跨进程写同一个库（两台机器、两个用户、一个后台服务）时，你必须自己安排刷新："
        "\n       · 定时 select() —— 最土，但从来不骗人；"
        "\n       · 让写的那一方发通知（本地套接字广播，见 threads 第 8 讲），收到再 select()；"
        "\n       · 用 SQLite 的 WAL + 一个「版本号表」，每次读先比版本号。"
        "\n     最不该做的是「假设没人会绕过我改库」。");
}

// ============================================================================
// 【8】大表：fetchMore / canFetchMore
// ============================================================================
static void phase8_fetchMore(QSqlDatabase db)
{
    section(QStringLiteral("【8】200 行的大表：模型是分批取的，视图滚动时才要下一批"));

    if (!premise(QStringLiteral("【8】造一张 200 行的表"),
                 exec(db, QStringLiteral("CREATE TABLE big (id INTEGER PRIMARY KEY, v TEXT)"))
                     && exec(db, QStringLiteral("BEGIN"))
                     && [&]() {
                            QSqlQuery q(db);
                            q.prepare(QStringLiteral("INSERT INTO big (id, v) VALUES (?, ?)"));
                            for (int i = 1; i <= 200; ++i) {
                                q.addBindValue(i);
                                q.addBindValue(QStringLiteral("row-%1").arg(i));
                                if (!q.exec())
                                    return false;
                            }
                            return true;
                        }()
                     && exec(db, QStringLiteral("COMMIT")),
                 QStringLiteral("建表 + 插 200 行")))
        return;

    QSqlQueryModel snapshot;
    snapshot.setQuery(QStringLiteral("SELECT id, v FROM big ORDER BY id"), db);
    if (!premise(QStringLiteral("【8】查询成功"), snapshot.lastError().isValid() == false,
                 QStringLiteral("lastError = %1").arg(snapshot.lastError().text())))
        return;

    // ★ 初始能拿到多少行由驱动决定：SQLite 一次就能全给你，别的驱动可能只给前 N 行。
    //   所以这里只报实测，不承诺具体数字。
    verdict(Obs, QStringLiteral("【8】刚查询完就看到的行数"), true,
            QStringLiteral("%1（canFetchMore = %2）")
                .arg(snapshot.rowCount())
                .arg(snapshot.canFetchMore() ? QStringLiteral("true") : QStringLiteral("false")));

    int batches = 0;
    while (snapshot.canFetchMore()) {
        snapshot.fetchMore();
        ++batches;
        if (batches > 1000) {                 // 防御：驱动说「还有」却永远给不完
            qWarning().noquote() << QStringLiteral("【8】canFetchMore 迟迟不结束，停止拉取");
            break;
        }
    }

    // ★ 这一条是硬账，而且理由是「两条分支同结论」：
    //   如果一开始就全给完了，循环一次都不进，行数就是 200；
    //   如果分批，循环把它拉完，行数还是 200。不管走哪条，结论一样。
    checkNum(QStringLiteral("【8A】一直拉到底之后，总行数一项不差"), snapshot.rowCount(), 200);
    checkBool(QStringLiteral("【8B】到底之后 canFetchMore 为假"), snapshot.canFetchMore(), false);
    checkStr(QStringLiteral("【8C】第 1 行的内容仍然正确"),
             snapshot.data(snapshot.index(0, 1)).toString(), QStringLiteral("row-1"));
    checkStr(QStringLiteral("【8D】最后一行的内容仍然正确"),
             snapshot.data(snapshot.index(199, 1)).toString(), QStringLiteral("row-200"));
    verdict(Obs, QStringLiteral("【8】额外调了几次 fetchMore"), true, QString::number(batches));

    qInfo().noquote() << QStringLiteral(
        "  → QTableView 滚动到底部时会自己调 fetchMore，所以「表格滚动时越来越长」"
        "\n     是设计行为，不是 bug（qsqlquerymodel.h 第 60/61 行就是给它的两个齿轮）。"
        "\n     只读的大表可以开 setForwardOnly(true)（QSqlQuery 上）省内存，"
        "\n     代价是不能往回滚、rowCount 也不可靠 —— 鱼与熊掌。");
}

// ============================================================================
// 九、合账
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
// 十、main：建库、跑八节、合账。全程没有 exec()
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("storage_06_sql_model_view"));

    qInfo() << "================================================================";
    qInfo() << "storage 第 6 讲 · SQL 接界面（Qt =" << QString::fromLatin1(QT_VERSION_STR) << "）";
    qInfo() << "================================================================";

    if (!premise(QStringLiteral("【0】本机有 QSQLITE 驱动"),
                 QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")),
                 QStringLiteral("可用驱动：%1")
                     .arg(QSqlDatabase::drivers().join(QStringLiteral(", ")))))
    {
        reconcile();
        return gLedger.exitCode();
    }

    DbSession session(QStringLiteral("main"));
    QSqlDatabase db = session.db();
    if (!premise(QStringLiteral("【0】内存库打开成功"), db.open(),
                 QStringLiteral("lastError = %1").arg(db.lastError().text())))
    {
        reconcile();
        return gLedger.exitCode();
    }

    // 建表：emp 有主键（能被 QSqlTableModel 编辑的前提），dept 是外键的目标。
    if (!premise(QStringLiteral("【0】建表"),
                 exec(db, QStringLiteral("CREATE TABLE dept (id INTEGER PRIMARY KEY, name TEXT NOT NULL)"))
                     && exec(db, QStringLiteral("CREATE TABLE emp (id INTEGER PRIMARY KEY, "
                                                "name TEXT NOT NULL, dept_id INTEGER, salary INTEGER)"))
                     && exec(db, QStringLiteral("INSERT INTO dept (id, name) VALUES "
                                                "(1,'研发部'),(2,'市场部'),(3,'财务部')"))
                     && exec(db, QStringLiteral("INSERT INTO emp (id, name, dept_id, salary) VALUES "
                                                "(1,'张三',1,3000),"
                                                "(2,'李四',1,4000),"
                                                "(3,'王五',2,5000),"
                                                "(4,'赵六',3,6000),"
                                                "(5,'钱七',2,7000)")),
                 QStringLiteral("dept/emp + 3 个部门 + 5 个员工")))
    {
        reconcile();
        return gLedger.exitCode();
    }

    phase1_twoKindsOfModels(db);
    phase2_editStrategies(db);
    phase3_reselectDiscards(db);
    phase4_relational(db);
    phase5_view(db);
    phase6_proxy(db);
    phase7_stale(db);
    phase8_fetchMore(db);

    qInfo().noquote() << QStringLiteral(
        "\n  ★ 本讲从头到尾没进过事件循环。这不是省略 —— 模型/视图的通信是同步的："
        "\n    setData 立刻改缓存，dataChanged 立刻通知视图，视图立刻重画那一格。"
        "\n    「一份数据、多个投影」是**结构**上的分工，不是并发。");

    reconcile();
    const int rc = gLedger.exitCode();
    qInfo().noquote() << QStringLiteral("进程退出码 = %1").arg(rc);
    return rc;
}

// ============================================================================
// 十一、速查
// ============================================================================
/*
 * 选哪个模型：
 *   只读展示一条 SQL 的结果            QSqlQueryModel
 *   要能增删改一个表                    QSqlTableModel（表必须有主键）
 *   外键列要显示成人话                  QSqlRelationalTableModel + setRelation
 *   只读超大数据集、不想全进内存        QSqlQueryModel + setForwardOnly + 分批
 *
 * 三种编辑策略（qsqltablemodel.h 第 29 行）：
 *   OnFieldChange    改一个字段写一次库     单个开关/数字，改完就走
 *   OnRowChange      离开这一行时提交       表格逐行编辑（默认）
 *   OnManualSubmit   只有 submitAll 才写    表单，或需要「一起成立」的多字段
 *
 * 三对容易混的方法：
 *   submit() / revert()        只作用于**当前行**
 *   submitAll() / revertAll()  作用于**所有脏行**
 *   select()                   重新查；未提交的改动会被丢掉（Soft，实测为准）
 *
 * 五个「不是」：
 *   · 模型不是数据库 —— 它是缓存，绕过它改库要自己 select()
 *   · 视图不是模型   —— 它只问不存；多个视图可以挂同一个模型
 *   · 代理不是拷贝   —— 它只有映射，改代理就是改源
 *   · flags() 不是建议 —— 它是模型对视图说的唯一一句「能不能编辑」
 *   · rowCount 不是一次到位 —— canFetchMore/fetchMore 管着后续批次
 *
 * 丢数据的三条路（背下来）：
 *   ① 未提交时 setFilter / setSort / select
 *   ② 编辑策略是 OnManualSubmit 而用户直接关窗口
 *   ③ 直接改库没有通知模型（看到旧值，用户以为白改了）
 */

// ============================================================================
// 十二、练习
// ----------------------------------------------------------------------------
/*
 * 1. 把【2】的 OnFieldChange 换成 OnRowChange 重跑 ①，确认【2A】那条硬账会红。
 *    然后回答：为什么「策略」这种设计一定要做成枚举 + 一个 setter，
 *    而不是让调用方自己决定「什么时候 submit」？
 *
 * 2. 给【5】的 ReadOnlyIdModel 加一条规则：salary 列只接受 0..100000 的值。
 *    实现方式必须仍然是 flags() —— 提示：值域检查不能放在 flags 里，
 *    那你就得换一个扩展点。想清楚之后回答：
 *    **Qt 的模型层给了哪几个「扩展点」，各自该放什么逻辑？（flags / setData / 代理）**
 *
 * 3. 给【6】加第二层代理（先过滤再排序，套两层），确认【6B】【6D】仍然成立。
 *    然后写一句你能贴进代码评审的话，解释「为什么代理层数不是越多越好」。
 *
 * 4. 用【8】的表写一个「分页表格」：每页 50 行，用 setFilter 做 WHERE id BETWEEN ...
 *    而不是靠 fetchMore。比较两种做法的：
 *      (a) 内存占用；(b) 翻到第 100 页要多久；(c) 中途有人改了数据会怎样。
 *    第 (c) 问是本讲没讲的，答案在【7】。
 *
 * 5. 故意把【4】的 setRelation 放到 select() 之后，重跑，看【4B】红不红。
 *    然后回答：为什么这个顺序问题在 QSqlRelationalTableModel 上必然存在，
 *    而在 QSortFilterProxyModel 上不存在？（提示：一个改的是「SQL 语句」，一个改的是「映射」。）
 *
 * 6. 资深题：写一个 QAbstractTableModel 子类，背后是一个 std::vector<Row>，
 *    要求：可编辑、排序在内存里做、支持「撤销上一步」。
 *    然后把【3】的坑在它身上重演一遍：setFilter 换成你的「重新加载」，
 *    确认未保存的编辑会不会丢。写清楚你的设计怎么保证不丢。
 */
// ============================================================================

// 本文件没有 Q_OBJECT 类（ReadOnlyIdModel 只改了一个虚函数，没有新增信号槽），
// 所以末尾不需要 #include "06_sql_model_view.moc"。
