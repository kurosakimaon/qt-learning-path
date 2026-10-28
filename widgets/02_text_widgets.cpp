// ============================================================================
// 第 2 讲 · 文本控件（QLineEdit / QPlainTextEdit / QTextEdit）
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   文本控件是「新手最爱用、也最容易被误用」的一类。本讲四块硬货：
//     1) QLineEdit 的三个信号 textChanged / textEdited / editingFinished
//        **到底在什么时候发**——本讲用计数器 + 手工投递 QKeyEvent 把差别逼出来，
//        这是新手误用率最高的一条（拿 textChanged 当「用户改了内容」用）；
//     2) 校验器（validator）三件套 QIntValidator / QDoubleValidator /
//        QRegularExpressionValidator：它们的真正工作面是 validate()/fixup()，
//        并且**状态是三值**的（Invalid / Intermediate / Acceptable）；
//     3) QPlainTextEdit 与 QTextEdit 的取舍：一个是「大文本编辑器」，
//        一个是「富文本渲染器」，性能差别来自内部文档布局，不是外观；
//     4) QTextCursor 是唯一能精准改富文本的手，beginEditBlock/endEditBlock
//        决定 undo 栈里算「一步」还是「一百步」。
//
// 对应构建目标名：widgets_02_text_widgets
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(widgets_02_text_widgets 02_text_widgets.cpp)
//   target_link_libraries(widgets_02_text_widgets PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)   # 本文件里**没有** Q_OBJECT 类，无需 #include "02_....moc"
//
// 运行前须知：
//   · 本机当前无法编译链接 Qt 运行时，本示例**未在本机实测编译**；
//     每个 API 都对着 C:\msys64\ucrt64\include\qt6 下的真实头文件核对（行号见下）。
//   · 全程无人值守：QTimer::singleShot 驱动 setText/insert/del，
//     再用 QCoreApplication::sendEvent 投两个 QKeyEvent 模拟「真实键盘输入」与回车；
//     关键结论用 qInfo() 打印，最后 QCoreApplication::quit()。**没有任何阻塞调用**。
//   · Qt5 → Qt6 差异（本讲重点）：
//       - **QRegExp 整个类在 Qt6 已移除**，QRegExpValidator 也随之没了。
//         本机 QtWidgets/QtGui/QtCore 三处都搜不到 qregexpvalidator.h，
//         校验器头文件只有 QtGui/qvalidator.h 一个文件，里面同时定义
//         QValidator / QIntValidator / QDoubleValidator / QRegularExpressionValidator
//         （第 24/56/89/141 行）。→ 老代码 `new QRegExpValidator(QRegExp("\\d+"))`
//         必须换成 `new QRegularExpressionValidator(QRegularExpression("\\d+"))`。
//       - QValidator / QIntValidator / QDoubleValidator 在 Qt6 位于 **QtGui**（qvalidator.h），
//         不在 QtWidgets；链接 Qt6::Gui 即可，`#include <QtGui/qvalidator.h>` 或直接
//         `#include <QIntValidator>`（转发头）。
//       - QComboBox::setValidator 之类没变，但注意 QLineEdit::setValidator 收的是
//         **const QValidator\***（qlineedit.h 87 行），Qt5 时代也是 const，别自己乱加 const_cast。
// ============================================================================

#include <QApplication>
#include <QCoreApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QLineEdit>            // QtWidgets/qlineedit.h
#include <QLabel>
#include <QPlainTextEdit>       // QtWidgets/qplaintextedit.h
#include <QTextEdit>            // QtWidgets/qtextedit.h
#include <QCompleter>           // QtWidgets/qcompleter.h
#include <QKeySequenceEdit>     // QtWidgets/qkeysequenceedit.h（Qt6 里它仍在 QtWidgets）
#include <QValidator>           // QtGui/qvalidator.h（三个子类同住一个头文件）
#include <QRegularExpression>   // QtCore/qregularexpression.h（Qt6 的正则门面）
#include <QTextCursor>          // QtGui/qtextcursor.h
#include <QTextDocument>        // QtGui/qtextdocument.h
#include <QAbstractItemModel>   // QtCore/qabstractitemmodel.h：读补全模型的行数
#include <QModelIndex>
#include <QKeyEvent>            // QtGui/qevent.h：手工投递「真实按键」
#include <QTimer>
#include <QDebug>
#include <QString>
#include <QStringList>

// ============================================================================
// 一、核对签名（本讲全部依据）
// ============================================================================
// QtWidgets/qlineedit.h
//   第 28 行   class Q_WIDGETS_EXPORT QLineEdit : public QWidget
//   第 36 行   属性 echoMode；第 78~81 行 enum EchoMode { Normal, NoEcho, Password, PasswordEchoOnEdit };
//                Q_ENUM(EchoMode) / echoMode() / setEchoMode(EchoMode)
//   第 48 行   属性 placeholderText；第 67 行 void setPlaceholderText(const QString &)
//   第 50/75 行 clearButtonEnabled 属性 + setClearButtonEnabled(bool)
//   第 84/87/88 行 setReadOnly(bool) / setValidator(const QValidator *) / const QValidator *validator() const
//   第 92 行   void setCompleter(QCompleter *completer)
//   第 52~54 行 enum ActionPosition { LeadingPosition, TrailingPosition }；第 144~145 行 addAction(...)
//   第 62 行   QString text() const；第 149 行 void setText(const QString &)
//   第 99~100 行 cursorPosition()/setCursorPosition(int)
//   第 110~111 行 void backspace(); void del();            ← Qt6 **仍有** del()（第 111 行），别被旧帖误导
//   第 115/116 行 bool isModified() const; void setModified(bool)
//   第 150~161 行 void clear(); void selectAll(); void undo(); void copy() const; void paste(); void deselect();
//   第 162 行  void insert(const QString &)
//   第 167~172 行 Q_SIGNALS: textChanged(const QString&); textEdited(const QString&);
//                cursorPositionChanged(int,int); editingFinished();
//   第 138/139 行 setTextMargins(int,int,int,int) / setTextMargins(const QMargins&)
// QtGui/qvalidator.h
//   第 24 行   class Q_GUI_EXPORT QValidator : public QObject
//   第 31~36 行 enum State { Invalid, Intermediate, Acceptable }; Q_ENUM(State)
//                ↑ **枚举顺序**：Invalid = 0！所以「state 非零」不等于「通过」，必须显式比 Acceptable
//   第 41 行   virtual State validate(QString &, int &) const = 0;   ← 两个参数都是**引用**（可以改写文本！）
//   第 43 行   virtual void fixup(QString &) const;
//   第 56~78 行 class QIntValidator : public QValidator
//                第 64 行 QIntValidator(int bottom, int top, QObject *parent = nullptr);
//                第 72 行 void setRange(int bottom, int top); 第 74/75 行 bottom()/top()
//   第 89~115 行 class QDoubleValidator : public QValidator
//                第 95 行 属性 notation + 第 106 行 Q_ENUM(Notation)（ScientificNotation / StandardNotation）
//                第 110/111 行 void setRange(double bottom, double top, int decimals) / setRange(double,double)
//                第 114 行 setDecimals(int)；第 115 行 setNotation(Notation)
//   第 141~156 行 class QRegularExpressionValidator : public QValidator
//                第 148 行 explicit QRegularExpressionValidator(const QRegularExpression &re, QObject *parent = nullptr);
//                第 151 行 QValidator::State validate(QString &input, int &pos) const override;
//                第 153 行 QRegularExpression regularExpression() const;
// QtWidgets/qcompleter.h
//   第 24 行   class Q_WIDGETS_EXPORT QCompleter : public QObject
//   第 38~42 行 enum CompletionMode { PopupCompletion, UnfilteredPopupCompletion, InlineCompletion }
//   第 44~48 行 enum ModelSorting { UnsortedModel = 0, CaseSensitivelySortedModel, CaseInsensitivelySortedModel }
//   第 51~55 行 ctor：QCompleter(QObject*) / QCompleter(QAbstractItemModel*, QObject*) /
//                QCompleter(const QStringList& completions, QObject*)（第 55 行，受 QT_CONFIG(stringlistmodel) 保护）
//   第 62~63 行 setModel(QAbstractItemModel*) / model()
//   第 65/66 行 setCompletionMode / completionMode      第 74 行 setCaseSensitivity
//   第 77 行   setModelSorting(ModelSorting)            第 89 行 setMaxVisibleItems(int)
//   第 98 行   QAbstractItemModel *completionModel() const;   ← **补全结果自己也是一张 model**
//   第 103 行  void setCompletionPrefix(const QString &)
//   第 115~119 行 信号 activated(QString) / activated(QModelIndex) / highlighted(QString) / highlighted(QModelIndex)
// QtWidgets/qplaintextedit.h
//   第 30 行   class Q_WIDGETS_EXPORT QPlainTextEdit : public QAbstractScrollArea
//   第 40 行   属性 plainText（READ toPlainText WRITE setPlainText NOTIFY textChanged）
//   第 47/94/96 行 maximumBlockCount 属性 + setMaximumBlockCount(int) / maximumBlockCount()
//                （第 94/96 行是 inline 转发到 document()->setMaximumBlockCount(...)）
//   第 52~56 行 enum LineWrapMode { NoWrap, WidgetWidth }
//   第 63 行   QTextDocument *document() const;
//   第 65 行   void setPlaceholderText(const QString &)
//   第 91/121 行 setUndoRedoEnabled(bool) / 第 152 行 int blockCount() const
//   第 100/101 行 lineWrapMode / setLineWrapMode(LineWrapMode)
//   第 112/114 行 bool find(const QString&, QTextDocument::FindFlags = ...) /
//                bool find(const QRegularExpression&, QTextDocument::FindFlags = ...)   ← Qt6 收 QRegularExpression
//   第 117 行  inline QString toPlainText() const { return document()->toPlainText(); }
//   第 140/141 行 cursorWidth / setCursorWidth(int)
//   第 158/166/167/172/174 行 setPlainText / undo / redo / insertPlainText / appendPlainText
//   第 182~192 行 信号 textChanged / undoAvailable(bool) / redoAvailable(bool) / copyAvailable(bool) /
//                selectionChanged / cursorPositionChanged / updateRequest(QRect,int) /
//                blockCountChanged(int) / modificationChanged(bool)
// QtWidgets/qtextedit.h
//   第 29 行   class Q_WIDGETS_EXPORT QTextEdit : public QAbstractScrollArea
//   第 45 行   属性 html（READ toHtml WRITE setHtml NOTIFY textChanged）
//   第 79 行   QTextDocument *document() const;
//   第 125/128 行 setLineWrapMode / setLineWrapColumnOrWidth(int)
//   第 136~139 行 enum LineWrapMode { NoWrap, WidgetWidth, FixedPixelWidth, FixedColumnWidth }
//   第 140 行  QString toHtml() const;                   第 200~207 行 setPlainText / setHtml / setMarkdown / setText
//   第 170 行  void setAcceptRichText(bool accept);      ← 只收纯文本的开关，做「编辑器」时很有用
//   第 190~197 行 setFontPointSize(qreal) / setFontFamily / setFontWeight / setCurrentFont ...
//   第 226 行  void append(const QString &text);         ← 富文本版 append：**按 HTML 解析**
//   第 233~240 行 信号 textChanged / undoAvailable / redoAvailable / currentCharFormatChanged /
//                copyAvailable / selectionChanged / cursorPositionChanged
// QtGui/qtextcursor.h
//   第 29 行   class Q_GUI_EXPORT QTextCursor
//   第 32 行   explicit QTextCursor(QTextDocument *document);
//   第 47~50 行 enum MoveMode { MoveAnchor, KeepAnchor }
//   第 52 行   void setPosition(int pos, MoveMode mode = MoveAnchor);   第 53 行 int position() const;
//   第 58/59 行 void insertText(const QString &text) / (const QString&, const QTextCharFormat&)
//   第 92 行   bool movePosition(MoveOperation op, MoveMode = MoveAnchor, int n = 1);
//   第 103~108 行 enum SelectionType { WordUnderCursor, LineUnderCursor, BlockUnderCursor, Document };
//                void select(SelectionType selection);
//   第 110~118 行 bool hasSelection() const; void removeSelectedText(); void clearSelection();
//                int selectionStart() const; int selectionEnd() const; QString selectedText() const;
//   第 177/179 行 void beginEditBlock(); void endEditBlock();
// QtGui/qtextdocument.h
//   第 67/241 行 blockCount 属性 + int blockCount() const
//   第 90 行   bool isUndoAvailable() const;             第 49/50 行 virtual void undo()/redo()
//   第 111/112 行 QString toHtml() const; void setHtml(const QString &html);
//   第 130 行  void setMarkdown(const QString&, MarkdownFeatures = MarkdownDialectGitHub)
// QtGui/qevent.h
//   第 425~430 行 class Q_GUI_EXPORT QKeyEvent : public QInputEvent
//                QKeyEvent(Type type, int key, Qt::KeyboardModifiers modifiers,
//                          const QString& text = QString(), bool autorep = false, quint16 count = 1);
//                ↑ Qt6 的 QKeyEvent **属于 QtGui**（Qt4 时代在 QtWidgets；本文件 include <QKeyEvent> 即可）

// ============================================================================
// 二、三个信号的计数器（放堆上：接收它们的是排进事件循环的 lambda）
// ============================================================================
struct LineSig
{
    int changed = 0;
    int edited = 0;
    int finished = 0;
    QString lastChanged;
};

static void wireLine(QLineEdit *edit, LineSig *s)
{
    QObject::connect(edit, &QLineEdit::textChanged, [s](const QString &t) {
        ++s->changed; s->lastChanged = t;
    });
    QObject::connect(edit, &QLineEdit::textEdited, [s](const QString &t) {
        ++s->edited; Q_UNUSED(t);
    });
    QObject::connect(edit, &QLineEdit::editingFinished, [s] { ++s->finished; });
}

// 把 State 枚举翻成人话（枚举顺序 Invalid=0/Intermediate=1/Acceptable=2，见 qvalidator.h 31~36 行）
static const char *stateName(QValidator::State st)
{
    switch (st) {
    case QValidator::Acceptable:   return "Acceptable";
    case QValidator::Intermediate: return "Intermediate";
    case QValidator::Invalid:      break;
    }
    return "Invalid";
}

// 投一个「按了某个字符键」的事件：走的是和真人打字同一条 keyPressEvent 路径
static void sendChar(QWidget *w, int key, const QString &text)
{
    // 核对：qevent.h 429 行 ctor；QEvent::KeyPress 见 QtCore/qcoreevent.h
    // sendEvent 见 QtCore/qcoreapplication.h：static void sendEvent(QObject *receiver, QEvent *event)
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
    QCoreApplication::sendEvent(w, &press);
}

// ============================================================================
// 三、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle(QStringLiteral("第 2 讲 · 文本控件"));
    window.resize(640, 520);
    auto *root = new QVBoxLayout(&window);

    // ────────────────────────────────────────────────────────────────
    // 证据 ① 三个信号的分水岭：程序改文本 vs 用户改文本
    // ────────────────────────────────────────────────────────────────
    auto *edit = new QLineEdit;
    edit->setPlaceholderText(QStringLiteral("试试输入姓名（占位文字，不是内容）"));  // qlineedit.h 67 行
    edit->setClearButtonEnabled(true);                                   // 75 行：右侧出现 ✕
    edit->setMaxLength(24);                                              // 70 行
    LineSig *sig = new LineSig;
    wireLine(edit, sig);
    root->addWidget(edit);

    qInfo() << "① 接线之后、什么都没改：changed =" << sig->changed
            << " edited =" << sig->edited << " finished =" << sig->finished << "（全 0）";

    edit->setText(QStringLiteral("程序写进去的"));                        // 149 行
    qInfo() << "① setText() 之后：changed =" << sig->changed
            << " edited =" << sig->edited
            << "→ **setText 只发 textChanged，绝不发 textEdited**（这条是本讲最重要的一句话）";

    edit->insert(QStringLiteral("X"));                                    // 162 行
    qInfo() << "① insert(\"X\") 之后：changed =" << sig->changed
            << " edited =" << sig->edited
            << " → 这一对数字就是「insert 算不算用户编辑」的判决，请把你机器上的打印写回注释";

    edit->del();                                                          // 111 行
    qInfo() << "① del() 之后：changed =" << sig->changed << " edited =" << sig->edited
            << " 当前文本 =" << edit->text();                             // 62 行

    // 【误用图鉴】把「启用/禁用提交按钮」连在 textChanged 上：
    //   程序一 setText(默认值) 按钮就变灰、脏标记就被打开，用户什么都没干已经「改过」了。
    //   正规做法：连 textEdited（只在用户手上发），或者用 isModified()（qlineedit.h 115 行）。
    qInfo() << "① isModified() 现在 =" << edit->isModified()
            << " ← setText 会把它清成 false（「刚刚存过盘」语义），insert/del 则会把它置 true";

    // ────────────────────────────────────────────────────────────────
    // 证据 ② 密码框 + 校验器三件套
    // ────────────────────────────────────────────────────────────────
    auto *pwd = new QLineEdit;
    pwd->setEchoMode(QLineEdit::Password);                                // 81 行 + 78 行枚举四档
    pwd->setPlaceholderText(QStringLiteral("密码：Normal/NoEcho/Password/PasswordEchoOnEdit 的第三档"));
    qInfo() << "② echoMode 枚举值 =" << int(pwd->echoMode())
            << "（Normal=0 NoEcho=1 Password=2 PasswordEchoOnEdit=3，顺序见 qlineedit.h 78 行）";
    root->addWidget(pwd);

    auto *ageEdit = new QLineEdit;
    auto *intVal = new QIntValidator(0, 120, &window);                    // qvalidator.h 64 行
    ageEdit->setValidator(intVal);                                        // qlineedit.h 87 行
    ageEdit->setPlaceholderText(QStringLiteral("年龄 0~120（QIntValidator）"));
    root->addWidget(ageEdit);

    auto *priceEdit = new QLineEdit;
    auto *dblVal = new QDoubleValidator(&window);                          // QDoubleValidator 有 (QObject*) ctor
    dblVal->setRange(0.0, 999.99, 2);                                      // qvalidator.h 110 行
    dblVal->setDecimals(2);                                                // 114 行：显式再写一遍小数位，防人改上面的 setRange
    // 还有一档 setNotation(Notation)（115 行 + 95/106 行的 Q_ENUM(Notation)）：
    // 它决定「1e5 这种科学计数法算不算合法」。本机头文件我只核到属性名与 setter，
    // **没核到枚举成员的拼写**，所以这里不调用它——要用请先在你那份 qvalidator.h 里核对枚举名。
    priceEdit->setValidator(dblVal);
    priceEdit->setPlaceholderText(QStringLiteral("价格 0~999.99，两位小数（QDoubleValidator）"));
    root->addWidget(priceEdit);

    auto *codeEdit = new QLineEdit;
    // ⚠ Qt5→Qt6：QRegExp / QRegExpValidator 在 Qt6 已**整体移除**，这里只能用 QRegularExpression
    const QRegularExpression reCode(QStringLiteral("^[A-Z]{2}-\\d{4}$"));
    auto *reVal = new QRegularExpressionValidator(reCode, &window);        // qvalidator.h 148 行
    codeEdit->setValidator(reVal);
    codeEdit->setPlaceholderText(QStringLiteral("形如 AB-1234（QRegularExpressionValidator）"));
    root->addWidget(codeEdit);

    // 直接问校验器：validate 的两个参数都是引用，它会**就地改写文本**（补零、去空格）
    auto probe = [](QValidator *v, const char *tag, QString text) {
        int pos = text.length();
        const QString before = text;
        const QValidator::State st = v->validate(text, pos);                // qvalidator.h 41 行
        qInfo() << "   " << tag << " 原输入 =" << before << "→ state =" << stateName(st)
                << "，validate 改写后的文本 =" << text << "，pos =" << pos;
        QString toFix = text;
        v->fixup(toFix);                                                    // 43 行：Intermediate 才有救
        qInfo() << "      fixup() 之后 =" << toFix;
    };
    qInfo() << "② 校验器直读（**注意 Invalid = 0**，所以 `if (state)` 这种写法是错的）：";
    probe(intVal, "QIntValidator(0,120)", QStringLiteral("45"));
    probe(intVal, "QIntValidator(0,120)", QStringLiteral("4"));      // Intermediate：还能补成 4x
    probe(intVal, "QIntValidator(0,120)", QStringLiteral("450"));    // Invalid
    probe(dblVal,   "QDoubleValidator",   QStringLiteral("12.345"));
    probe(reVal,    "QRegularExpressionValidator", QStringLiteral("ab-1"));

    // ────────────────────────────────────────────────────────────────
    // 证据 ③ QCompleter：补全结果本身是一张 model
    // ────────────────────────────────────────────────────────────────
    QStringList langs;
    langs << QStringLiteral("Qt Widgets") << QStringLiteral("Qt Quick")
          << QStringLiteral("Qt Network") << QStringLiteral("CMake")
          << QStringLiteral("Conan") << QStringLiteral("qmake");
    auto *completer = new QCompleter(langs, &window);            // qcompleter.h 55 行 QStringList ctor
    completer->setCaseSensitivity(Qt::CaseInsensitive);          // 74 行
    completer->setCompletionMode(QCompleter::PopupCompletion);    // 65 行 + 38~42 行枚举
    completer->setMaxVisibleItems(8);                             // 89 行
    edit->setCompleter(completer);                                // qlineedit.h 92 行
    // ⚠ QCompleter::activated 有 (QString) 与 (QModelIndex) **两个重载**（qcompleter.h 116/117 行），
    //   直接写 &QCompleter::activated 编译器无法决断，必须用 QOverload 挑一个：
    QObject::connect(completer, QOverload<const QString &>::of(&QCompleter::activated),
                     [edit](const QString &text) {
        // 用户从弹窗里选了一项
        edit->setText(text);                                      // 常见写法：回填（注意它发的是 textChanged）
        qInfo() << "③ QCompleter::activated(QString) →" << text;
    });
    // 另一版（想拿 model 行就连它）：
    //   QOverload<const QModelIndex &>::of(&QCompleter::activated)
    completer->setCompletionPrefix(QStringLiteral("Qt"));         // 103 行
    if (QAbstractItemModel *cm = completer->completionModel()) {  // 98 行
        qInfo() << "③ setCompletionPrefix(\"Qt\") 后：completionModel 行数 ="
                << cm->rowCount(QModelIndex())
                << "（**弹窗只在真实按键时出现，但补全计算本身可以立刻读出来当证据**）";
    }
    qInfo() << "③ QCompleter 的源 model 行数 =" << completer->model()->rowCount(QModelIndex())
            << "；自己维护 QStringList 时 Qt 会给你建一个内部 QStringListModel";

    // ────────────────────────────────────────────────────────────────
    // 证据 ④ QKeySequenceEdit：快捷键录入框（可选件）
    // ────────────────────────────────────────────────────────────────
    auto *kse = new QKeySequenceEdit;                              // qkeysequenceedit.h 27 行
    kse->setMaximumSequenceLength(2);                              // 43 行，参数类型 qsizetype
    QObject::connect(kse, &QKeySequenceEdit::keySequenceChanged, [](const QKeySequence &seq) {
        qInfo() << "④ keySequenceChanged 收到一个 QKeySequence（打印它请用 seq.toString()，"
                   "Qt6 的 SequenceFormat 参数已弃用，别传 QKeySequence::PortableV2 之类）";
        (void)seq;
    });
    root->addWidget(kse);
    // 【为什么这里不伪造按键给它】QKeySequenceEdit 的录入要靠**键盘抓取与超时**，
    // 手工投一个 KeyPress 未必走完它的整状态机。想自动化请上 QTest::keyClicks（测试模块）。

    // ────────────────────────────────────────────────────────────────
    // 证据 ⑤ QPlainTextEdit：日志/大文本的正解
    // ────────────────────────────────────────────────────────────────
    auto *log = new QPlainTextEdit;
    log->setReadOnly(true);
    log->setMaximumBlockCount(5);              // qplaintextedit.h 94 行：inline 转发到 document()->setMaximumBlockCount
    log->setLineWrapMode(QPlainTextEdit::NoWrap);   // 52~56 行：长行出横向滚动条，日志一般选 NoWrap
    log->setCursorWidth(0);                    // 141 行：只读日志没必要留光标（0 = 不画）
    QObject::connect(log, &QPlainTextEdit::blockCountChanged, [](int n) {
        qInfo() << "⑤ blockCountChanged →" << n;
    });
    root->addWidget(log);

    // ────────────────────────────────────────────────────────────────
    // 证据 ⑥ QTextEdit：富文本 + QTextCursor 这只「手」
    // ────────────────────────────────────────────────────────────────
    auto *rich = new QTextEdit;
    rich->setHtml(QStringLiteral(
        "<h3>Qt 富文本子集</h3><p>支持 <b>粗</b>、<i>斜</i>、"
        "<span style=\"color:#b36\">色块</span>、<a href=\"#x\">链接</a>、"
        "<ul><li>列表项</li></ul> 与少量 CSS。</p>"));   // qtextedit.h 202 行 setHtml
    qInfo() << "⑥ setHtml 之后 toPlainText 长度 =" << rich->toPlainText().length()
            << "，toHtml 长度 =" << rich->toHtml().length()
            << "（Qt **会把 HTML 规范化重写一遍**，你写进去的字符串不会原样回来——"
               "所以别拿 toHtml() 做 diff，也别指望你的 class/自定义标签还在）";
    root->addWidget(rich);

    // ────────────────────────────────────────────────────────────────
    // 证据 ⑦ 模拟「真人敲键盘」：看三个信号的真实差别
    // ────────────────────────────────────────────────────────────────
    window.show();

    QTimer::singleShot(250, [edit, sig] {
        const int c0 = sig->changed, e0 = sig->edited;
        edit->setFocus();
        sendChar(edit, Qt::Key_A, QStringLiteral("a"));      // 走 QLineEdit::keyPressEvent
        sendChar(edit, Qt::Key_B, QStringLiteral("b"));
        qInfo() << "⑦ 投两个按键事件之后：textChanged +" << (sig->changed - c0)
                << ", textEdited +" << (sig->edited - e0)
                << " 当前文本 =" << edit->text()
                << "（**如果这两个增量都是 +2，就证明按键路径确实算「用户编辑」**；"
                   "拿到的数字请以你机器上的打印为准，我把结论写在注释里而不是替你断言）";
    });

    QTimer::singleShot(500, [edit, sig] {
        // editingFinished 的两个来源：回车（含小键盘 Enter）与「焦点离开且内容变过」
        sendChar(edit, Qt::Key_Return, QString());            // text 为空：这是动作键，不插入字符
        qInfo() << "⑦ 投一个 Key_Return 之后：editingFinished 累计 =" << sig->finished
                << "（**Qt5 时代同名信号叫 acceptedInput，且 Qt5 的 returnPressed 仍在；别混用**）";
    });

    QTimer::singleShot(750, [edit, sig] {
        const int c0 = sig->changed, f0 = sig->finished;
        edit->clear();                                        // qlineedit.h 150 行
        qInfo() << "⑦ clear() 之后：textChanged 增量 =" << (sig->changed - c0)
                << "（clear 等价于 setText(\"\")，**照样发 textChanged、照样不发 textEdited**）；"
                << "editingFinished 增量 =" << (sig->finished - f0)
                << "（预期 0：既没按回车，也没丢焦点）";
    });

    QTimer::singleShot(1000, [log] {
        // 一次「刷 200 行」：maximumBlockCount(5) 会把老的挤出去，这是写日志控件的正确姿势
        for (int i = 1; i <= 200; ++i)
            log->appendPlainText(QStringLiteral("第 %1 行日志").arg(i));   // qplaintextedit.h 174 行
        qInfo() << "⑤ appendPlainText 200 次之后：blockCount =" << log->blockCount()
                << "（152 行）maximumBlockCount =" << log->maximumBlockCount()
                << " → 只留最后 5 行，**内存与重绘代价都被封顶**。"
                << "用 QTextEdit + append 干同样的事，行数和 document()->blockCount() 都会一路涨（152/67 行对比）";
        qInfo() << "⑤ toPlainText 行数（按 \\n 数） ="
                << log->toPlainText().count(QChar(QLatin1Char('\n'))) + 1;
    });

    QTimer::singleShot(1250, [log] {
        log->undo();                                   // qplaintextedit.h 166 行
        qInfo() << "⑤ undo() 之后 blockCount =" << log->blockCount()
                << "；undoAvailable =" << log->isUndoRedoEnabled()
                << "（91 行的 setter；能不能撤销还得看 undoAvailable(bool) 信号，182~185 行）";
        const bool found = log->find(QStringLiteral("第 200 行"));   // 112 行
        qInfo() << "⑤ find(\"第 200 行\") 命中 =" << found
                << "（find 是**移动光标**的操作，命中与否看返回值；配合 findNext 才是循环查找）";
    });

    QTimer::singleShot(1500, [rich] {
        QTextDocument *doc = rich->document();                  // qtextedit.h 79 行
        QTextCursor cur(doc);                                   // qtextcursor.h 32 行
        cur.beginEditBlock();                                   // 177 行：下面这一堆改动在 undo 栈里算**一步**
        cur.setPosition(0);                                   // 52 行：插到文档最前（MoveAnchor 默认）
        cur.insertText(QStringLiteral("〔程序插入〕"));            // 58 行（不带 QTextCharFormat 的那一版）
        cur.insertText(QStringLiteral(" 第二句"));                  // 同 58 行
        // 想真的另起一段请用 insertBlock(const QTextBlockFormat&)（第 145 行）——
        // 它需要一个 QTextBlockFormat 参数（头文件里没有「无参 insertBlock」可核对，我不瞎写）。
        cur.endEditBlock();                                     // 179 行
        cur.select(QTextCursor::Document);                      // 112 行 + 103~107 行的 SelectionType
        qInfo() << "⑥ beginEditBlock/endEditBlock 包了 3 次插入后：position =" << cur.position()
                << "（53 行）hasSelection =" << cur.hasSelection()          // 110 行
                << "selectionStart/End =" << cur.selectionStart() << "/" << cur.selectionEnd()
                << "，selectedText 长度 =" << cur.selectedText().length();   // 118 行
        qInfo() << "⑥ 【为什么必须包 edit block】不包的话，用户按一次 Ctrl+Z 只会看到"
                   "「第二行」被撤掉，〔程序插入〕还留着——**undo 栈按一次插入算一条命令**。"
                   "批量改富文本请一律 begin/endEditBlock 配对，并且中途**不要提前 return**（配对了没关等于没关）。";
        qInfo() << "⑥ 文档 blockCount =" << doc->blockCount() << "（qtextdocument.h 241 行）";
    });

    QTimer::singleShot(1750, [rich] {
        // 造一个「独立游标」→ 选全文 → 打印选区 → 取消选择 → 用 setTextCursor 把这份状态**写回控件**
        // （qtextedit.h 第 84 行 void setTextCursor(const QTextCursor &) 是已核对的入口；
        //   反向的那个 getter 本讲不依赖，读文档一律走 document()，第 79 行）
        QTextCursor cur(rich->document());
        cur.select(QTextCursor::Document);                         // qtextcursor.h 112 行
        qInfo() << "⑥ 选全文之后：selectedText 长度 =" << cur.selectedText().length()
                << " hasSelection =" << cur.hasSelection()          // 110 行
                << " selectionStart/End =" << cur.selectionStart() << "/" << cur.selectionEnd();
        cur.clearSelection();                                      // 113 行
        rich->setTextCursor(cur);                                  // qtextedit.h 84 行
        qInfo() << "⑥ clearSelection + setTextCursor 之后 toPlainText 长度 ="
                << rich->toPlainText().length()
                << "（**内容没动**：removeSelectedText 才会删，本讲不把整篇删掉，留着给你自己试）";
    });

    // 捕获方式说明：sig 是 main 里 new 出来的**堆上裸指针**（LineSig 不是 QObject，
    // 没有 parent、不会被 Qt 回收），所有权归 main，这里按值捕获指针后由本 lambda
    // 负责**唯一一次** delete。按引用捕获反而危险（sig 是 main 的局部变量，2000ms 时
    // 若 main 已提前返回就是悬垂引用）。
    // 顺序：先 delete 计数器，再 quit —— 此时 edit 仍活着，但本讲 750ms 之后
    // 再没有任何代码改动它的文本，所以不会有 textChanged/textEdited 回访这个指针；
    // 若你后续要往这里加改文本的步骤，请先断连（wireLine 的三个连接没有保存
    // QMetaObject::Connection，断不回来），再删。
    QTimer::singleShot(2000, [sig] {
        qInfo() << "\n=== 结论 ===";
        qInfo() << "① textChanged=「内容变了」（程序改也算）；textEdited=「人改了」；editingFinished=回车或焦点离开。";
        qInfo() << "② 「提交按钮是否可用」连 textEdited，「脏标记」用 isModified()/textChanged，别混。";
        qInfo() << "③ QValidator::State 里 Invalid = 0，必须显式比 Acceptable；validate/fixup 会**改写**你的字符串。";
        qInfo() << "④ 大文本、日志、代码 → QPlainTextEdit + setMaximumBlockCount；要富文本才用 QTextEdit。";
        qInfo() << "⑤ 批量改富文本一律 QTextCursor + beginEditBlock/endEditBlock；toHtml() 是「Qt 重排过的 HTML」。";
        qInfo() << "⑥ Qt6 没有 QRegExp：所有正则请用 QRegularExpression / QRegularExpressionValidator。";
        delete sig;
        QCoreApplication::quit();
    });

    return app.exec();
}

// ============================================================================
// 附：QPlainTextEdit vs QTextEdit 的取舍表
// ============================================================================
/*
 *  维度            QPlainTextEdit                     QTextEdit                结论
 *  ------------------------------------------------------------------------------------
 *  内容格式        纯文本（无 HTML 渲染）             HTML/富文本子集          要富文本只能 QTextEdit
 *  性能瓶颈        分块布局 QPlainTextDocumentLayout  整文档布局               长文档差别主要在布局策略
 *                 （第 250 行的类）+ 只画可见块       每次都算全文档           （**具体数字请自己测**）
 *  行数封顶        setMaximumBlockCount(第 94 行)      无同名接口               日志一律用前者
 *  行号/固定宽字体 天然合适（setCursorWidth 等）        字重混排会打乱列对齐      编辑器/终端风：前者
 *  查找            find(QString/QRegularExpression)   同名 + 富文本格式匹配     Qt6 参数是 QRegularExpression
 *  信号            blockCountChanged/updateRequest    currentCharFormatChanged  按需选
 *                 modificationChanged                 （233~240 行）
 *
 * 三条纪律：
 *  ① **别拿 textChanged 当「用户改了」**。它同时也涵盖 setText、补全回填、clear、
 *     甚至校验器的 fixup。做「自动保存草稿」可以连它；做「用户改过所以别覆盖」
 *     必须连 textEdited 或用 isModified()。
 *  ② **别拿 editingFinished 当「失焦」**。它只在「失焦且内容变过」或按回车时发；
 *     真要监听焦点，请重写 focusOutEvent 或连 QApplication::focusChanged
 *     （第贰阶段第 3 讲讲过焦点链）。
 *  ③ **别把 QTextEdit 当日志窗**。append 会走 HTML 解析；日志里出现 "<" 就会被吃掉。
 *     需要日志请用 QPlainTextEdit + appendPlainText，
 *     或者给 QTextEdit setAcceptRichText(false)（qtextedit.h 170 行）。
 *
 * ——— 老师傅的话 ———
 *   徒弟，文本控件这块，我带过的人十有八九栽在同一件事上：
 *   他把「内容变了」和「人变了内容」当成同一个信号。
 *   后果是什么？程序一打开，输入框被填了默认值，textChanged 一路放响，
 *   「未保存」的红点在标题栏亮起，退出时问用户「要保存吗」——
 *   用户一头雾水：我什么都没干啊。
 *   再后果是：搜索框连了 textChanged 就发一次网络请求，
 *   用户按一个退格你打五次接口。正确写法是 textEdited + 一个 300ms 的
 *   单次 QTimer 做防抖（debounce），把「人一直在改」合并成「人改完了一会儿」。
 *   这个防抖的写法就是第 4 讲里 interval=0 定时器合并机制的用武之地。
 *
 *   校验器这块我还想多说一句：QValidator 只有 `validate()` 一个虚函数，
 *   它的返回值是三值的。**Intermediate 是这套 API 里最妙也最被浪费的一档**：
 *   它告诉 QLineEdit「现在还不合法，但再敲两个字符就合法了，别打断用户」。
 *   很多老代码在 Invalid 上做弹窗提示，用户体验非常暴躁；
 *   正确的分层是：Intermediate 什么也不做；Invalid 只画一条红线（第 9 讲样式表）；
 *   真的提交时才弹提示——而且提示是模态的，模态会阻塞，请回看第 5 讲。
 *
 * ——— 小练习 ———
 *   1. 把「防抖搜索」写出来：给 edit 连 textEdited，在里面 `timer->start(300)`
 *      （QTimer + setInterval/start 见第 4 讲），timeout 里打印 edit->text()。
 *      快速敲 10 个字，观察 timeout 只响了一次。
 *   2. 自定义一个 `class SlotValidator : public QValidator`（QtGui/qvalidator.h 24 行），
 *      重写 validate()：接受形如 "09:30" 的时间，"09:3" 返回 Intermediate，
 *      "25:00" 返回 Invalid 并在 fixup() 里把它夹到 23:59。
 *      把它 setValidator 到第 4 个 QLineEdit 上，打印三种输入的 state 字符串。
 *   3. 把证据⑤ 的 setMaximumBlockCount(5) 去掉，改成 appendPlainText 5000 行，
 *      用 QElapsedTimer（events 模块第 4 讲）分别测「插入 5000 行」与「滚动到底」的耗时；
 *      再把同一个测试搬到 QTextEdit::append 上做一遍，把两栏数字写回注释。
 *      这一步做完，你以后选型就不用背表了。
 */
// ============================================================================
