// ============================================================================
// main.cpp · 翻译链路的运行期那一半
// ----------------------------------------------------------------------------
// 本工程解决什么问题：
//   构建期（CMake）负责把 .ts 变成 .qm 并塞进资源；运行期要做的是三件事：
//     1) 找到 .qm（这里是 ":/i18n/translations_app_en.qm"）
//     2) QTranslator::load() 装进来
//     3) QCoreApplication::installTranslator() 让它生效
//   然后，tr() 才会去查翻译表；查不到就回落到源码里的中文——这是**优雅降级**，
//   不是错误。
//
// 关于 %n（复数）：
//   tr("当前有 %n 个窗口", nullptr, n) 会按 n 选复数形式。中文没有复数变化，
//   英文有（1 window / 2 windows）。.ts 里对应的条目会带 <numerusform>。
//
// 关于 #include "main.moc"：
//   Greeter 的 Q_OBJECT 写在 .cpp 里，AUTOMOC 不会为 .cpp 生成 moc_main.cpp，
//   约定的钩子就是在类定义之后 include "<本文件名>.moc"。
//
// ★★ 这一版为什么整段重写：原版会**打印一句假话** ★★
//   第一版的第 81 行是 `qInfo() << "翻译是否生效：" << installed;`，而 `installed`
//   量的只是 `load()` + `installTranslator()` 都成功。本机实测这条链是这样断的：
//     · qt_add_translations 在**首次 configure** 时只生成一个**空 .ts**——文件里除了
//       XML 头就只有一句注释："Run the update_translations CMake target to populate
//       the source strings in this file."（.ts 是它建的，源串是 lupdate 抽的，两件事
//       不在同一个 target 里，构建时不会自动跑 lupdate）；
//     · 空 .ts 过 lrelease → "Generated 0 translation(s) (0 finished and 0 unfinished)"
//       + "Ignored 2 untranslated source text(s)" → 只剩一个 **30 字节**的 .qm；
//     · 这个 30 字节的 .qm **load() 照样返回 true**，installTranslator 照样成功。
//   于是原版在译文一条都没填的情况下，稳稳打印「翻译是否生效： true」，紧跟在三行
//   **没翻译**的中文后面。这本来是该工程最该教的东西，却被原版教反了。
//   → 现在改成真台账：装之前先取基线，装之后逐句比对，不符就把退出码改成 1。
//   → 顺手补一条能程序化分辨「装载成功但表是空的」的判据：QTranslator::isEmpty()
//     （QtCore/qtranslator.h 29 行）。本机用最小探针量到：
//       0 条译文的 .qm → load()=true、isEmpty()=**true**
//       有译文的 .qm   → load()=true、isEmpty()=false
//     **isEmpty 回答「表里有没有东西」；load 只回答「这个文件是不是合法 .qm」。**
//
// 顺带量到的两条 lrelease 事实（写在 .ts 里也有用）：
//   · `<translation type="unfinished">` 只要**填了字**就照样编进 .qm、照样被查中；
//     真正被踢出去的是**空译文**（那行 "Ignored N untranslated source text(s)"）。
//     想让 unfinished 不进包，得显式加 -nounfinished；CI 想在有 unfinished 时红，
//     加 -fail-on-unfinished（本机实测退出码 1）。
//   · 同一 context、同一源文的两条（哪怕注释不同）只要都填了字，lrelease 会打
//     "Warning: dropping duplicate messages" 并且只留第一条。
//   详细过程记在阶段四第 10 讲 widgets/10_internationalization.cpp 的附一。
//
// 怎么让英文真的出现（本机跑通的顺序，缺一不可）：
//     cmake -G "MinGW Makefiles" -B build -S .
//     cmake --build build --target update_translations   # ← 不跑这条，.ts 永远是空的
//     （然后填译文：Qt Linguist 打开 .ts，或直接改 XML）
//     cmake --build build                                # lrelease 编出 .qm 并嵌进 :/i18n/
//   本机实测 update_translations 的输出（第一次 / 第二次）：
//     第一次：Found 2 source text(s) (2 new and 0 already existing)   ← .ts 原本全空
//     第二次：Found 3 source text(s) (1 new and 2 already existing)
//             Same-text heuristic provided 1 translation(s)
//   → **重跑不会冲掉填好的译文**（lupdate 是合并），但它会**抄**：见下面账8。
//   → 别在 .ts 顶部写 XML 注释做说明：qt_add_translations 自己建文件时写的那句
//     "Run the update_translations…" 在第一次 lupdate 之后就被整条删掉了（本机对比过
//     前后两份文件）。lrelease 是能吃 XML 注释的，扛不住的是 lupdate 重写。
//     要给 .ts 写「这份文件怎么来的」，写在讲义里，别写在 .ts 里。
//   当前仓库里这一份 translations_app_en.ts 的状态（跑完两轮之后的真实样子）：
//     Greeter     2 条已填 + 1 条空（账9 故意用的那句没译文的源文）
//     NotGreeter  1 条，译文是同文启发式抄来的，标着 type="unfinished" 但照样生效
//
// 那个多出来的 translations_app_zh_CN.ts 是什么？（第一次看到都会以为写错了）
//   它是 lupdate 的 **plurals 模板文件**，不是要你翻译的 catalog：
//   Qt6LinguistTools/Qt6LinguistToolsMacros.cmake 749~757 行——当 QT_I18N_SOURCE_LANGUAGE
//   （这里是 zh_CN）不在 QT_I18N_TRANSLATED_LANGUAGES 里时，宏自动把「基名_源语言.ts」
//   设成 arg_PLURALS_TS_FILE，用来记录**源语言自己**有几个复数形态。
//   同文件 886 行把它一起塞进 all_ts_files（给 IDE 显示），所以它也跟着走一遍发布链，
//   build 目录里就会出现 translations_app_zh_CN.qm。那个 .qm 里 0 条译文，本程序也从不
//   尝试加载它（下面候选循环第一步就 skip 掉源语言）。不想要它：给 qt_add_translations
//   传 NO_GENERATE_PLURALS_TS_FILE。
//
// 运行前须知：
//   · 无人值守：显示 300 毫秒后自动退出。
//   · 可以无头跑：translations_app -platform offscreen
//   · 台账不符 → 进程退出码 1。本工程是独立 CMake 工程、没接 ctest，
//     但退出码留着：`./build/translations_app; echo $?` 就能验，别只看打印。
//
// 对应构建目标名：translations_app
// ============================================================================

#include <QApplication>
#include <QLabel>
#include <QLocale>
#include <QTranslator>
#include <QDir>
#include <QTimer>
#include <QDebug>

class Greeter : public QObject
{
    Q_OBJECT
public:
    explicit Greeter(QObject *parent = nullptr) : QObject(parent) {}

    // lupdate 会扫这里，把 "Greeter" 作为上下文、把这两句原文收进 .ts。
    QString line() const { return tr("你好，Qt。这句话可以被翻译。"); }
    QString windowCount(int n) const { return tr("当前有 %n 个窗口", nullptr, n); }
};

// 极简台账：一条不符就记一笔，最后参与退出码。
// 为什么本工程也要台账：这个演示**曾经输出过一句明确的假话**（见文件头）。
// 假话只能靠「跑出来的数和期望对一遍」来防——注释里写多少遍「注意 .ts 可能是空的」
// 都防不住，因为写注释的人和跑程序的人不是同一个人。
struct Ledger {
    int ok = 0;
    int bad = 0;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Ledger ledger;
    auto expect = [&ledger](const QString &what, const QString &got, const QString &want) {
        const bool hit = (got == want);
        if (hit) {
            ++ledger.ok;
        } else {
            ++ledger.bad;
        }
        qInfo().noquote() << QStringLiteral("      [%1] %2 → 期望 \"%3\"，实测 \"%4\"")
                                 .arg(hit ? QStringLiteral("对") : QStringLiteral("错"))
                                 .arg(what).arg(want).arg(got);
    };

    qInfo().noquote() << "系统语言：" << QLocale::system().name()
                      << " uiLanguages：" << QLocale::system().uiLanguages();

    // ── 第一步：看看资源里到底有哪些 .qm ───────────────────────────────────
    const QStringList qmFiles = QDir(QStringLiteral(":/i18n")).entryList({QStringLiteral("*.qm")});
    qInfo().noquote() << ":/i18n 里的 qm 文件：" << qmFiles;
    if (qmFiles.count() > 1) {
        qInfo().noquote() << "  （多出来的那个源语言 .qm 是 plurals 模板编出来的，0 条译文，"
                             "下面不会去加载它——理由见文件头那段）";
    }

    // ── 第二步：**先取基线**（此刻查表链上一个 translator 都没有）────────────
    // 这一步是原版缺的那一半：没有「装之前长什么样」，就没有资格说「生效了」。
    Greeter greeter;
    qInfo().noquote() << "[基线] 句子：" << greeter.line();
    qInfo().noquote() << "[基线] 复数 1：" << greeter.windowCount(1);
    qInfo().noquote() << "[基线] 复数 3：" << greeter.windowCount(3);
    expect("账1 基线句子 = 源码里的中文（查表链上没人，必然回落源文）",
           greeter.line(), QStringLiteral("你好，Qt。这句话可以被翻译。"));
    expect("账2 基线复数 n=1（中文只有一个复数形态，%n 仍被 translate 填）",
           greeter.windowCount(1), QStringLiteral("当前有 1 个窗口"));

    // ── 第三步：按优先级尝试装载 ──────────────────────────────────────────
    // 顺序很重要：先系统语言，再英文。第一个**装得上且表非空**的留下。
    //
    // haveCatalog 不是「猜环境」：:/i18n 里有什么是**构建期**由 CMake 编进二进制的，
    // 同一个 exe 跑一百次它都一样。所以后面的期望值按它分两档写，既不会在没装
    // LinguistTools 的机器上把演示判红，也不会在装了的那类机器上放过「译文没填」——
    // 后者正是原版漏掉的那一格。
    const bool haveCatalog = qmFiles.count() > 0;
    const QString wantLine = haveCatalog
        ? QStringLiteral("Hello, Qt. This sentence can be translated.")
        : QStringLiteral("你好，Qt。这句话可以被翻译。");
    const QString wantOne = haveCatalog ? QStringLiteral("There is 1 window")
                                        : QStringLiteral("当前有 1 个窗口");
    const QString wantThree = haveCatalog ? QStringLiteral("There are 3 windows")
                                          : QStringLiteral("当前有 3 个窗口");

    QTranslator translator;
    bool loaded = false;
    bool catalogHasEntries = false;

    QStringList candidates;
    candidates << QLocale::system().name();          // 例如 zh_CN
    candidates << QStringLiteral("en");             // 兜底

    for (const QString &lang : candidates) {
        if (lang.isEmpty() || lang == QLatin1String("zh_CN"))
            continue;                               // 源码语言不需要翻译
        const QString path = QStringLiteral(":/i18n/translations_app_%1.qm").arg(lang);
        if (translator.load(path)) {
            loaded = true;
            // ★ 关键区别：load() 只证明「这是个合法的 .qm」。30 字节的空 catalog
            //   也合法（本机实测 load()=true）。isEmpty()（qtranslator.h 29 行）
            //   才回答「表里到底有没有译文」。
            catalogHasEntries = !translator.isEmpty();
            qInfo().noquote() << "已装载：" << path
                              << "｜isEmpty()=" << translator.isEmpty()
                              << "｜language=" << translator.language();
            break;
        }
        qInfo().noquote() << "没有可用的翻译：" << path << "（回落源码语言）";
    }
    expect("账3 load 返回 true（提醒：这条即使 catalog 是空的也成立，所以它不说明翻译生效）",
           loaded ? QStringLiteral("true") : QStringLiteral("false"),
           haveCatalog ? QStringLiteral("true") : QStringLiteral("false"));
    expect("账4 表里有可用译文（!isEmpty）—— 本机这一档是「有 .qm」，它必须为 true；"
           "如果 .ts 没填过译文，这里就会是 false（原版正是在这一格看不见，才敢打印「生效」）",
           catalogHasEntries ? QStringLiteral("true") : QStringLiteral("false"),
           haveCatalog ? QStringLiteral("true") : QStringLiteral("false"));

    if (catalogHasEntries) {
        app.installTranslator(&translator);
    } else {
        qInfo().noquote() << "  catalog 是空的 → 跳过 installTranslator：装了也查不到任何东西。"
                             "原版就是在这里把「装上了」当成「生效了」，给了人一个假的 true。";
    }

    // ── 第四步：tr() 现在会走翻译表；查不到就回中文 ───────────────────────
    qInfo().noquote() << "[装之后] 句子：" << greeter.line();
    qInfo().noquote() << "[装之后] 复数 1：" << greeter.windowCount(1);
    qInfo().noquote() << "[装之后] 复数 3：" << greeter.windowCount(3);
    expect("账5 单条真的变成英文（原版没做过这次比对）",
           greeter.line(), wantLine);
    // 英文有 one/other 两个形态，所以 .ts 里那条是 numerus + 两个 <numerusform>。
    expect("账6 复数 n=1 走第一个 numerusform",
           greeter.windowCount(1), wantOne);
    expect("账7 复数 n=3 走第二个 numerusform（同一条目，换 n 就换句子）",
           greeter.windowCount(3), wantThree);

    // ★ 这两笔是本工程最有价值的两笔，而且是**跑出来的**，不是我推的。
    //
    // ① 「上下文写错 = 安静没翻译」这条老话，被 lupdate 的同文启发式打了个折。
    //    我在账5 之后故意加了一句 QCoreApplication::translate("NotGreeter", 同一句源文)，
    //    再跑 update_translations，lupdate 打的原文是：
    //        Found 3 source text(s) (1 new and 2 already existing)
    //        Same-text heuristic provided 1 translation(s)
    //    ——它把 Greeter 那条英文**抄给了 NotGreeter**，标成 type="unfinished"。
    //    而 unfinished **只要填了字就照样编进 .qm、照样被查中**（本机探针量过，
    //    详见 widgets/10 附一）。两件事叠起来就是「幽灵翻译」：
    //    上下文明明拼错了，界面却还是英文，还查不出是谁干的。
    //    所以这一笔的期望值是**英文**，不是源文。
    if (haveCatalog) {
        expect("账8 拼错的上下文也被译了 —— lupdate 的同文启发式把译文抄了过去",
               QCoreApplication::translate("NotGreeter", "你好，Qt。这句话可以被翻译。"),
               wantLine);
    } else {
        expect("账8 没有 catalog 的这一档：同一句自然回落源文",
               QCoreApplication::translate("NotGreeter", "你好，Qt。这句话可以被翻译。"),
               QStringLiteral("你好，Qt。这句话可以被翻译。"));
    }
    // ② 真正的「查不中」长这样：目录里没这句源文。安静、不报错、原样还你。
    expect("账9 catalog 里没这句源文 → 回落源文（不报错，这就是拼错最难查的原因）",
           QCoreApplication::translate("Greeter", "这句话仓库里谁都没写过。"),
           QStringLiteral("这句话仓库里谁都没写过。"));
    // ③ context 在**运行时**拼出来，lupdate 扫不到字面量 → 这条进不了 .ts → 永远没翻译。
    //    ★ 这一笔我第一版写错了，错了正好留下一课：当时拼出来的是 "Not" + "Greeter"，
    //      结果它**被译了**，账直接红。原因不是 lupdate 能看见运行时字符串，而是
    //      "NotGreeter" 已经被上面②那笔的同文启发式塞进 .ts 了。
    //      → 换一个目录里确实不存在的上下文，并且跑完 update_translations 后
    //        grep 一下 .ts：找不到 "GreeterRuntime" 这个名字，这才是③的本意。
    const QByteArray runtimeCtx = (QStringLiteral("Greeter") + QStringLiteral("Runtime")).toUtf8();
    expect("账10 上下文运行时才拼出来 → lupdate 扫不到字面量 → catalog 里没这个上下文 → 没译文",
           QCoreApplication::translate(runtimeCtx.constData(), "你好，Qt。这句话可以被翻译。"),
           QStringLiteral("你好，Qt。这句话可以被翻译。"));

    // ── 第五步：remove 之后再问一次 —— install/remove 就是那个开关 ─────────
    if (catalogHasEntries)
        app.removeTranslator(&translator);
    expect("账11 remove 之后回到源文（切语言靠的就是这一开一关 + LanguageChange 事件）",
           greeter.line(), QStringLiteral("你好，Qt。这句话可以被翻译。"));

    QLabel label(greeter.line() + QStringLiteral("\n") + greeter.windowCount(3));
    label.setAlignment(Qt::AlignCenter);
    label.setMinimumSize(520, 120);
    label.show();

    qInfo().noquote() << QStringLiteral("【台账】核对 %1 项，不符 %2 项%3")
                             .arg(ledger.ok + ledger.bad).arg(ledger.bad)
                             .arg(ledger.bad > 0
                                      ? QStringLiteral(" → 退出码会被改成 1")
                                      : QStringLiteral("（全对）"));
    if (qmFiles.isEmpty()) {
        qWarning().noquote() << "  ⚠ :/i18n 里一个 .qm 都没有：说明 Qt6LinguistTools 没找到，"
                                "CMakeLists 里 qt_add_translations 那一段被整段跳过。"
                                "这是本工程允许的降级（configure 不会失败）。";
    }

    QTimer::singleShot(300, &app, &QCoreApplication::quit);
    const int rc = app.exec();
    // 台账不符 → 退出码非 0。`./build/translations_app; echo $?` 就能验，别只看打印。
    return rc != 0 ? rc : (ledger.bad > 0 ? 1 : 0);
}

// 写在 .cpp 里的 Q_OBJECT 类：把 moc 生成的东西展开进来。
#include "main.moc"
