// ============================================================================
// 第 5 讲 · 对话框（QDialog / QDialogButtonBox / QFileDialog / QMessageBox /
//           QColorDialog / QFontDialog）
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   对话框是「会杀死事件循环的东西」，也是最容易被写坏的一类。本讲讲清六件事：
//     1) QDialog 的**生命周期与三条出口**：exec()（阻塞 + 嵌套事件循环）、
//        open()（模态但**不**嵌套循环）、show()（非模态）——三者都会走 done(int)
//        → finished(int) → accepted()/rejected()，这条链你必须背下来；
//     2) 模态的三档 Qt::WindowModality（NonModal / WindowModal / ApplicationModal）
//        与 setModal(bool) 的区别：**setModal 只是给 exec()/open() 用的意图声明**，
//        show() 之前调 setModal(true) **不会**阻塞你；
//     3) result() 与 QDialog::Accepted/Rejected：以及 QMessageBox 里 result() 读回来的是
//        **StandardButton 的位值**（Ok = 0x400 = 1024），不是 1——本讲用打印钉死这条；
//     4) QDialogButtonBox：standardButtons 属性 + accepted/rejected/clicked(QAbstractButton*)
//        三个信号 + addButton(text, ButtonRole) 加自定义按钮；
//     5) 自定义对话框的骨架：「表单 + 按钮盒 + 校验通过才 accept()」，
//        校验不过就**留着别关**（这是对话框最基本的礼貌）；
//     6) QFileDialog / QMessageBox / QColorDialog / QFontDialog：
//        **静态方法（getOpenFileName / information / getColor / getFont）一律阻塞**，
//        本资料库把它们只写在注释里；现代写法是「实例 + show()/open() + 信号」。
//
// 对应构建目标名：widgets_05_dialogs
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(widgets_05_dialogs 05_dialogs.cpp)
//   target_link_libraries(widgets_05_dialogs PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)   # 本文件里定义了 Q_OBJECT 类（FormDialog），
//                           # 所以**必须**在文件末尾 #include "05_dialogs.moc"
//
// 运行前须知：
//   · 实测口径：本文件已在本机真实编译、链接、运行（Qt 6.12.0 + MinGW 13.1.0，
//     Windows，QT_QPA_PLATFORM=offscreen；ctest 目标 widgets_05_dialogs，跑到自动退出、退出码 0）。
//     但本讲**没有汇总自检行**，退出码 0 只证明「没崩、走完了」，不证明正文每个数字都对：
//     带具体数字的「预期」请以你自己那次运行输出为准（本机那一轮存档 runlogs/widgets_05_dialogs.log）。
//   · 【铁律】本文件的**主路径里没有任何阻塞调用**：
//       - 不调 QDialog::exec()（qdialog.h 54 行：virtual int exec()，它会起一个嵌套事件循环，
//         没人点按钮就永远停在那儿 → ctest 超时的第一名原因）；
//       - 不调 QMessageBox::information/question/warning/critical（qmessagebox.h 169~264 行的静态版）；
//       - 不调 QFileDialog::getOpenFileName/getSaveFileName/getExistingDirectory/getOpenFileNames
//         （qfiledialog.h 160~201 行的静态版）；
//       - 不调 QColorDialog::getColor（qcolordialog.h 56 行）/ QFontDialog::getFont（qfontdialog.h 59~61 行）。
//     要看模态效果请全部用 show() / open()，并**用 QTimer::singleShot 自动 accept()/reject()/click()**。
//   · 无人值守跑完约 4.5 秒，最后 QCoreApplication::quit()。
//   · Qt5 → Qt6 差异（本讲相关，已核对本机头文件）：
//       - QMessageBox 的静态方法在 Qt6 成对存在：既有返回 StandardButton 的版本，
//         也有返回 int 的版本（qmessagebox.h 第 169/173、179/183、189/193、199/203 行 vs
//         第 221/234/247/260 行）。→ 老代码用 `auto r = QMessageBox::question(...)` 的地方，
//         重载决断一旦落到 int 版，`if (r == QMessageBox::Yes)` 这类比较还能编，
//         但 `QMessageBox::StandardButton r = QMessageBox::question(...)` 会报 narrowing/类型错误。
//       - **QAction 在 Qt6 声明于 QtGui**（QtGui/qaction.h 第 30 行 Q_GUI_EXPORT），
//         对话框里想放「带菜单的按钮」请 include <QtGui/QAction>；QtWidgets/qaction.h 只剩转发。
//         （第 1 讲已核：不少迁移文章把方向写反，以头文件为准。）
//       - QMessageBox::ButtonRole 与 QDialogButtonBox::ButtonRole **是同一套枚举值**，
//         两个头文件里都写着同一句注释「keep this in sync with QPlatformDialogHelper::ButtonRole」
//         （qmessagebox.h 54~66 行 / qdialogbuttonbox.h 28~40 行）→ 你可以把 box 的 role 直接理解成
//         QMessageBox 的 role，跨类混用不是笔误。
//       - QRegExp 时代的过滤器写法在 Qt6 已死：文件对话框的名字过滤器仍是**字符串语法**
//         （"Images (*.png *.xpm *.jpg)"，qfiledialog.h 82 行 setNameFilter），
//         但你在应用内做匹配请用 QRegularExpression，别再找 QRegExp。
// ============================================================================

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>              // QtWidgets/qdialog.h
#include <QDialogButtonBox>     // QtWidgets/qdialogbuttonbox.h
#include <QMessageBox>          // QtWidgets/qmessagebox.h
#include <QFileDialog>          // QtWidgets/qfiledialog.h
#include <QColorDialog>         // QtWidgets/qcolordialog.h
#include <QFontDialog>          // QtWidgets/qfontdialog.h
#include <QVBoxLayout>
#include <QFormLayout>          // QtWidgets/qformlayout.h：addRow(const QString&, QWidget*)
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QAbstractButton>
#include <QIntValidator>        // QtGui/qvalidator.h：表单里那个数量字段用（第 2 讲讲过它住在 QtGui）
// 想做「任务名格式」强校验时再请出 QRegularExpressionValidator（同一头文件第 141~156 行）：
// Qt6 **没有** QRegExpValidator，别按老教程去 #include <QRegExpValidator>。   name-ok:QRegExpValidator 说的就是「它不存在」
#include <QFont>                // QtGui/qfont.h
#include <QColor>               // QtGui/qcolor.h
#include <QTimer>
#include <QDebug>
#include <QString>

// ============================================================================
// 一、核对签名（本讲全部依据）
// ============================================================================
// QtWidgets/qdialog.h
//   第 19 行   class Q_WIDGETS_EXPORT QDialog : public QWidget
//   第 24 行   Q_PROPERTY(bool sizeGripEnabled READ isSizeGripEnabled WRITE setSizeGripEnabled)
//   第 25 行   Q_PROPERTY(bool modal READ isModal WRITE setModal)
//   第 31 行   enum DialogCode { Rejected, Accepted };  Q_ENUM(DialogCode)   ← **Rejected = 0，Accepted = 1**
//   第 34 行   int result() const;
//   第 36 行   void setVisible(bool visible) override;
//   第 41/42 行 void setSizeGripEnabled(bool); bool isSizeGripEnabled() const;
//   第 44/45 行 void setModal(bool modal); void setResult(int r);
//   第 47~50 行 Q_SIGNALS: finished(int result); accepted(); rejected();
//   第 52~57 行 public Q_SLOTS: virtual void open(); virtual int exec();
//                virtual void done(int); virtual void accept(); virtual void reject();
//                ↑ 全是 **public slot**：所以 `dlg->accept()` 这种直调合法，
//                  也正因为 exec() 在这里，「随手写个 exec()」才会静悄悄地把主路径卡死
// QtWidgets/qdialogbuttonbox.h
//   第 20 行   class Q_WIDGETS_EXPORT QDialogButtonBox : public QWidget
//   第 23~25 行 属性 orientation / standardButtons / centerButtons
//   第 28~40 行 enum ButtonRole { InvalidRole = -1, AcceptRole, RejectRole, DestructiveRole,
//                ActionRole, HelpRole, YesRole, NoRole, ResetRole, ApplyRole, NRoles };
//   第 44~68 行 enum StandardButton { NoButton = 0, Ok = 0x400, Save = 0x800, SaveAll = 0x1000,
//                Open = 0x2000, Yes = 0x4000, YesToAll, No, NoToAll, Abort, Retry, Ignore,
//                Close = 0x200000, Cancel = 0x400000, Discard, Help, Apply, Reset, RestoreDefaults };
//                Q_DECLARE_FLAGS(StandardButtons, StandardButton)（第 71 行）
//   第 86~89 行 四个 ctor，其中 explicit QDialogButtonBox(StandardButtons buttons, QWidget *parent = nullptr)
//   第 95~97 行 void addButton(QAbstractButton*, ButtonRole) / QPushButton *addButton(const QString&, ButtonRole)
//                / QPushButton *addButton(StandardButton)
//   第 100~104 行 buttons() / buttonRole(QAbstractButton*) / setStandardButtons / standardButtons()
//                / QPushButton *button(StandardButton which) const;
//   第 106~110 行 setCenterButtons / standardButtonText(StandardButton) / standardButtonShortcut
//   第 117~121 行 Q_SIGNALS: clicked(QAbstractButton *button); accepted(); helpRequested(); rejected();
//                ↑ **注意**：按钮盒自己不关对话框！accepted/rejected 只是「转达意图」，
//                  要么你把它们连到 QDialog::accept/reject（本讲就这么做），要么自己处理
// QtWidgets/qmessagebox.h
//   第 22 行   class Q_WIDGETS_EXPORT QMessageBox : public QDialog
//   第 25~35 行 属性 text / icon / iconPixmap / textFormat / informativeText / textInteractionFlags
//   第 44~52 行 enum Icon { NoIcon = 0, Information = 1, Warning = 2, Critical = 3, Question = 4 };
//   第 54~66 行 enum ButtonRole（与 QDialogButtonBox 同一套，注释互相指认）
//   第 71~100 行 enum StandardButton（位值与 QDialogButtonBox 一致，另有 obsolete 的
//                YesAll/NoAll/Default/Escape —— **新代码别用这四个**）
//   第 120 行  void addButton(QAbstractButton *button, ButtonRole role);
//   第 131 行  void setStandardButtons(StandardButtons buttons);
//   第 134 行  QAbstractButton *button(StandardButton which) const;     ← 拿按钮好 programmatically click
//   第 137/138 行 setDefaultButton(QPushButton*) / setDefaultButton(StandardButton)
//   第 141/142 行 setEscapeButton(QAbstractButton*) / (StandardButton)
//   第 147~161 行 setText / setIcon(Icon) / setIconPixmap / setTextFormat / setTextInteractionFlags /
//                setCheckBox(QCheckBox*)
//   第 169~264 行 八个静态便捷函数（information/question/warning/critical × 两种返回类型）
//                ← **全部阻塞**，本文件只在注释里提它们
//   第 286 行  void setWindowTitle(const QString &title);
// QtWidgets/qfiledialog.h
//   第 28 行   class Q_WIDGETS_EXPORT QFileDialog : public QDialog
//   第 32/33 行 属性 fileMode / acceptMode
//   第 41 行   enum FileMode { AnyFile, ExistingFile, Directory, ExistingFiles };
//   第 43 行   enum AcceptMode { AcceptOpen, AcceptSave };
//   第 45 行   enum DialogLabel { LookIn, FileName, FileType, Accept, Reject };
//   第 48~57 行 enum Option { ShowDirsOnly, DontResolveSymlinks, DontConfirmOverwrite,
//                DontUseNativeDialog, ReadOnly, HideNameFilterDetails, DontUseCustomDirectoryIcons };
//   第 61~63 行 explicit QFileDialog(QWidget *parent = nullptr, const QString &caption = QString(),
//                const QString &directory = QString(), const QString &filter = QString());
//   第 69/73 行 void setDirectory(const QString&) / setDirectoryUrl(const QUrl&)
//   第 82/83 行 void setNameFilter(const QString&) / setNameFilters(const QStringList&)
//   第 87 行   QString selectedNameFilter() const;
//   第 101/104 行 setFileMode(FileMode) / setAcceptMode(AcceptMode)
//   第 107 行  void setSidebarUrls(const QList<QUrl>&)   第 113 行 setDefaultSuffix
//   第 125 行  void setLabelText(DialogLabel, const QString&)   第 132 行 setProxyModel
//   第 136/137 行 setOption(Option, bool on = true) / setOptions(Options)
//   第 145~149 行 Q_SIGNALS: fileSelected(const QString&); filesSelected(const QStringList&);
//                currentChanged(const QString&); directoryEntered(const QString&)
//   第 160~201 行 四个静态 getOpenFileName/getSaveFileName/getExistingDirectory/getOpenFileNames
//                ← **全部阻塞**，本文件不执行
// QtWidgets/qcolordialog.h
//   第 18 行   class Q_WIDGETS_EXPORT QColorDialog : public QDialog
//   第 22/24 行 属性 currentColor / options
//   第 41/42 行 void setCurrentColor(const QColor&) / QColor currentColor() const;
//   第 44 行   QColor selectedColor() const;
//   第 48/49 行 setOptions(ColorDialogOptions) / options()
//   第 56~59 行 static QColor getColor(...)                ← **阻塞**，本文件不执行
//   第 62/64 行 static QColor customColor(int) / standardColor(int)
//   第 69 行   Q_SIGNALS: void colorSelected(const QColor &color);
// QtWidgets/qfontdialog.h
//   第 20 行   class Q_WIDGETS_EXPORT QFontDialog : public QDialog
//   第 24/25 行 属性 currentFont / options
//   第 44/45 行 void setCurrentFont(const QFont&) / QFont currentFont() const;
//   第 47 行   QFont selectedFont() const;
//   第 59~61 行 static QFont getFont(bool *ok, ...)        ← **阻塞**，本文件不执行
//   第 65 行   Q_SIGNALS: void fontSelected(const QFont &font);
// QtGui/qfont.h    第 152/153 行 family()/setFamily()；第 162/163 行 setPointSize(int)/pointSizeF()
// QtGui/qcolor.h   QColor(Qt::GlobalColor) ← 由 qcolordialog.h 第 56 行的默认实参 `Qt::white` 反证存在
// QtWidgets/qformlayout.h 第 80~86 行 addRow(QWidget*,QWidget*) / addRow(const QString&,QWidget*) 等六个重载
// QtWidgets/qwidget.h     第 369 行 setWindowTitle；第 487 行 show；属性 windowModality
// QtCore/qnamespace.h     第 1589~1592 行 enum WindowModality { NonModal, WindowModal, ApplicationModal };
//                          （第 1841 行 Q_ENUM_NS(WindowModality)：所以它能被元对象系统认出名字）

// ============================================================================
// 二、自定义对话框的骨架：表单 + 按钮盒 + 校验通过才 accept()
// ============================================================================
// 【为什么这个类需要 Q_OBJECT】它定义了**自定义信号** validated(const QString&)
// 和一个槽 tryAccept()。有了它们，AUTOMOC 必须为本文件生成 05_dialogs.moc，
// 且必须在**文件末尾**包含（类定义之后）——见本文件最后一行。
class FormDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FormDialog(QWidget *parent = nullptr) : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("新建任务（自定义 QDialog 骨架）"));
        setSizeGripEnabled(true);                 // qdialog.h 41 行：右下角拖拽把手
        qInfo() << "   FormDialog 构造：isSizeGripEnabled =" << isSizeGripEnabled();   // 42 行

        nameEdit = new QLineEdit(this);
        nameEdit->setPlaceholderText(QStringLiteral("至少 3 个字符"));
        qtyEdit = new QLineEdit(this);
        qtyEdit->setValidator(new QIntValidator(1, 99, this));   // 第 2 讲：QtGui/qvalidator.h 64 行

        // 名字校验：用 QRegularExpressionValidator（Qt6 没有 QRegExpValidator，见第 2 讲）   name-ok:QRegExpValidator 同上
        hint = new QLabel(QStringLiteral("提示：任务名只能含中文/字母/数字/空格"), this);

        auto *form = new QFormLayout;             // qformlayout.h 82 行 addRow(const QString &labelText, QWidget *field)
        form->addRow(QStringLiteral("任务名(&T)："), nameEdit);
        form->addRow(QStringLiteral("数量(&Q)："), qtyEdit);
        form->addRow(hint);

        box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        //            ↑ qdialogbuttonbox.h 88 行 ctor：StandardButtons + parent
        box->addButton(QStringLiteral("稍后填"), QDialogButtonBox::RejectRole);  // 96 行：自定义文字 + role
        okButton = box->button(QDialogButtonBox::Ok);                            // 104 行 QPushButton *button(StandardButton)
        qInfo() << "   按钮盒：standardButtons 是否等于 Ok|Cancel ="
                << (box->standardButtons() == (QDialogButtonBox::Ok | QDialogButtonBox::Cancel))   // 103 行
                << "（Ok = 0x400 =" << int(QDialogButtonBox::Ok) << "，Cancel = 0x400000）"
                << "；button(Ok) 拿到了真实按钮 =" << (okButton != nullptr)
                << "→ 拿到它才能无人值守地 click()";

        // 【按钮盒的三条信号，接法必须记住】
        //   accepted()   → QDialog::accept()：这是「按钮盒不关窗、你自己关」的分界线
        //   rejected()   → QDialog::reject()
        //   clicked(QAbstractButton*) → 想知道**具体按了哪个**（含自定义按钮）时才用
        connect(box, &QDialogButtonBox::accepted, this, &FormDialog::tryAccept);   // 走槽，方便打断校验
        connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(box, &QDialogButtonBox::clicked, this, [this](QAbstractButton *b) {
            qInfo() << "   clicked(QAbstractButton*)：" << b->text()
                    << " 它的 ButtonRole =" << int(box->buttonRole(b));            // 101 行
        });

        auto *lay = new QVBoxLayout(this);
        lay->addLayout(form);
        lay->addWidget(box);
    }

    // 供自动化脚本调用：把值「像用户那样」填进去
    void fill(const QString &name, const QString &qty)
    {
        nameEdit->setText(name);                  // qlineedit.h 149 行
        qtyEdit->setText(qty);
    }
    void pressOk()                                // 编程触发按钮盒的 OK：走的就是 accepted() → tryAccept()
    {
        if (okButton) okButton->click();          // qabstractbutton.h 85 行
    }
    QString taskName() const { return nameEdit->text(); }

Q_SIGNALS:
    void validated(const QString &name);          // 自定义信号：只有校验通过才发

private slots:
    // 骨架的**心脏**：校验不过就不 accept，窗子留着
    void tryAccept()
    {
        const QString name = nameEdit->text().trimmed();
        const bool nameOk = name.length() >= 3;
        const bool qtyOk  = !qtyEdit->text().isEmpty();
        // 【回扣第 2 讲】这里**不能**用 qtyEdit->isModified() 当「用户填过了」：
        // 程序 setText() 会把 modified 清成 false（qlineedit.h 115/149 行），
        // 于是自动化脚本、补全回填、默认值三条路都会让它永远为 false。
        qInfo() << "   校验：nameOk =" << nameOk << " qtyOk =" << qtyOk
                << "；顺带读 isModified() =" << qtyEdit->isModified()
                << "（预期 false —— 别拿它当必填判据）"
                << "（**校验失败时绝对不要 accept()**：对话框一关，用户输入就蒸发了）";
        if (!nameOk) {
            hint->setText(QStringLiteral("【失败】 任务名太短，请至少 3 个字符"));   // 就地提示，不弹窗
            hint->setWordWrap(true);
            nameEdit->setFocus();
            return;
        }
        if (!qtyOk) {
            hint->setText(QStringLiteral("【失败】 数量必填"));
            nameEdit->setFocus();
            return;
        }
        Q_EMIT validated(name);                   // 自定义信号，必须靠 moc
        accept();                                 // qdialog.h 56 行 → done(Accepted) → finished(1) + accepted()
    }

private:
    QLineEdit *nameEdit = nullptr;
    QLineEdit *qtyEdit = nullptr;
    QLabel *hint = nullptr;
    QDialogButtonBox *box = nullptr;
    QPushButton *okButton = nullptr;
};

// ============================================================================
// 三、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QWidget window;                              // 主窗：所有对话框的 parent
    window.setWindowTitle(QStringLiteral("第 5 讲 · 对话框"));
    window.resize(420, 220);
    auto *root = new QVBoxLayout(&window);
    root->addWidget(new QLabel(QStringLiteral(
        "本讲的对话框全部用 show()/open() + 定时器自动关闭，<b>没有一处 exec()</b>。")));

    // ────────────────────────────────────────────────────────────────
    // 证据 ① 三条出口 + 信号链（先用一个最朴素的 QDialog 摸清次序）
    // ────────────────────────────────────────────────────────────────
    auto *d1 = new QDialog(&window);
    d1->setWindowTitle(QStringLiteral("① 三条出口"));
    d1->setModal(true);                          // qdialog.h 44 行
    qInfo() << "① setModal(true) 之后 isModal =" << d1->isModal()   // 25 行属性 READ isModal
            << " windowModality 枚举值 =" << int(d1->windowModality())
            << "（**0 = NonModal**：setModal 并不改 windowModality！"
               "它只在 exec()/open() 里被兑现，show() 路线上等于没说话）";
    QObject::connect(d1, &QDialog::finished, [](int r) {
        qInfo() << "① finished(" << r << ") —— **唯一可靠的收尾点**，三个出口都会走到这";
    });
    QObject::connect(d1, &QDialog::accepted, [] { qInfo() << "① accepted()（= finished(1) 的伴生信号）"; });
    QObject::connect(d1, &QDialog::rejected, [] { qInfo() << "① rejected()（= finished(0) 的伴生信号）"; });
    QObject::connect(d1, &QDialog::rejected, [d1] {
        qInfo() << "① reject 之后 result() =" << d1->result()        // 34 行
                << "；DialogCode 第 31 行：Rejected = 0，Accepted = 1";
    });

    // ────────────────────────────────────────────────────────────────
    // 证据 ② 模态三档（qnamespace.h 1589~1592）
    // ────────────────────────────────────────────────────────────────
    auto *d2 = new QDialog(&window);
    d2->setWindowTitle(QStringLiteral("② WindowModal（只挡主窗）"));
    // 核对：QWidget::setWindowModality(Qt::WindowModality)；三档枚举名 NonModal/WindowModal/ApplicationModal
    d2->setWindowModality(Qt::WindowModal);
    auto *d3 = new QDialog(&window);
    d3->setWindowTitle(QStringLiteral("② ApplicationModal（挡整个应用）"));
    d3->setWindowModality(Qt::ApplicationModal);
    qInfo() << "② WindowModal 枚举值 =" << int(d2->windowModality())
            << "，ApplicationModal 枚举值 =" << int(d3->windowModality())
            << " → **setModal(bool) 与 setWindowModality(三档) 是两套开关**："
               "前者是「我要不要当模态用」的意图标记（只在 exec()/open() 里被兑现），"
               "后者是直接对窗口系统下命令。用 show() 还想要真模态，请用 setWindowModality 或改用 open()。";

    // ────────────────────────────────────────────────────────────────
    // 证据 ③ 自定义对话框：先失败、再成功
    // ────────────────────────────────────────────────────────────────
    auto *form = new FormDialog(&window);
    QObject::connect(form, &FormDialog::validated, [](const QString &name) {
        qInfo() << "③ 自定义信号 validated →" << name;
    });
    QObject::connect(form, &QDialog::finished, [](int r) {
        qInfo() << "③ FormDialog finished(" << r << ") → **主流程只该在这里继续，不要在按钮的 clicked 里干活**";
    });

    // ────────────────────────────────────────────────────────────────
    // 证据 ④ QFileDialog：实例 + 非模态 + fileSelected（现代写法）
    // ────────────────────────────────────────────────────────────────
    // 【静态版只在注释里写，绝不执行】真实程序里想「一行拿个文件名」的三种写法：
    //     const QString f = QFileDialog::getOpenFileName(&window, tr("打开"), QDir::homePath(),
    //                                                    tr("图片 (*.png *.jpg)"));   // qfiledialog.h 160 行
    //     const QString d = QFileDialog::getExistingDirectory(&window, tr("选目录"));  // 190 行
    //     const QStringList fs = QFileDialog::getOpenFileNames(&window, tr("打开多个")); // 201 行
    //   它们内部都是「建 dialog + exec()」，**会阻塞直到用户点完**。
    //   本资料库的示例会被注册成无人值守的 ctest 用例，跑一次就死在那儿，所以下面只用实例版。
    auto *fd = new QFileDialog(&window, QStringLiteral("④ 选一个已有文件"),
                               QString(), QStringLiteral("源码 (*.cpp *.h);;所有文件 (*)"));
    fd->setFileMode(QFileDialog::ExistingFile);        // qfiledialog.h 41 + 101 行
    fd->setAcceptMode(QFileDialog::AcceptOpen);        // 43 + 104 行
    fd->setNameFilter(QStringLiteral("源码 (*.cpp *.h)"));  // 82 行
    fd->setLabelText(QFileDialog::FileName, QStringLiteral("文件名："));   // 125 行（DialogLabel 第 45 行）
    qInfo() << "④ FileMode 枚举值（ExistingFile）=" << int(fd->fileMode())
            << "；AcceptMode（AcceptOpen）=" << int(fd->acceptMode())
            << "；DontUseNativeDialog 这个 Option 的位值 =" << int(QFileDialog::DontUseNativeDialog)
            << "（48~57 行：非 0，是位标志）→ 需要「长得一样、可被样式表改」的对话框时才置它";
    QObject::connect(fd, &QFileDialog::fileSelected, [](const QString &file) {
        qInfo() << "④ fileSelected（**现代写法的关键信号**）→" << file;
    });
    QObject::connect(fd, &QFileDialog::directoryEntered, [](const QString &dir) {
        qInfo() << "④ directoryEntered →" << dir;
    });
    QObject::connect(fd, &QFileDialog::currentChanged, [](const QString &path) {
        qInfo() << "④ currentChanged（高亮变了，还没确认）→" << path;
    });
    // 【为什么这里连了 fileSelected 却收不到】它来自**用户在原生/Qt 对话框里点确认**，
    // 无人值守没有手。本讲用 reject() 收尾，把「收到之后该干什么」写在上面的打印里。
    // 想在测试里真收到它：自己往 lineEdit 里 setText 再 sendEvent 一个回车 ——
    // 那是 QtTest 的活，本模块不越界。

    // ────────────────────────────────────────────────────────────────
    // 证据 ⑤ QMessageBox：实例 + show + 自动 click（静态版只在注释里）
    // ────────────────────────────────────────────────────────────────
    // 【真实程序里的写法（**本示例不执行，因为它阻塞**）】
    //     QMessageBox::StandardButton r = QMessageBox::question(
    //         &window, tr("删除"), tr("确定删除 3 个文件？"),
    //         QMessageBox::Yes | QMessageBox::No, QMessageBox::No);   // qmessagebox.h 179 行
    //     if (r == QMessageBox::Yes) { ... }
    //   还有 information / warning / critical（169/189/199 行）与配套的 int 版（221~264 行）。
    //   它们全都「建一个 QMessageBox + exec()」，**没有用户就永远不返回**。
    auto *mb = new QMessageBox(&window);
    mb->setWindowTitle(QStringLiteral("⑤ QMessageBox（实例版）"));   // 286 行
    mb->setIcon(QMessageBox::Warning);                              // 150 行 + 44~52 行 Icon
    mb->setText(QStringLiteral("配置文件已损坏"));                    // 147 行
    mb->setInformativeText(QStringLiteral("将使用默认配置继续。Icon 枚举：NoIcon=0 Information=1 Warning=2 Critical=3 Question=4"));
    mb->setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);   // 131 行
    // 只调用**已核对到的**那个重载：addButton(QAbstractButton*, ButtonRole)（qmessagebox.h 120 行）。
    // （Qt 另有一个 addButton(const QString&, ButtonRole) 版，本文件不用没核对到的签名。）
    auto *logBtn = new QPushButton(QStringLiteral("查看日志"));
    mb->addButton(logBtn, QMessageBox::ActionRole);
    qInfo() << "⑤ 自定义按钮已塞进 QMessageBox，它的 role 是 ActionRole"
            << "（位值 int(QMessageBox::ActionRole) =" << int(QMessageBox::ActionRole)
            << "；ActionRole 的按钮**不参与** result()，点了也不会关窗）";
    mb->setCheckBox(new QCheckBox(QStringLiteral("不再提示此内容"), mb));  // 161 行：QMessageBox 的小惊喜
    qInfo() << "⑤ 静态版我们**不调**；实例版已备好：Icon(Warning) 枚举值 ="
            << int(QMessageBox::Warning)
            << "；Ok 的位值 =" << int(QMessageBox::Ok) << "（= 0x400 = 1024）"
            << "；Cancel 的位值 =" << int(QMessageBox::Cancel);
    QObject::connect(mb, &QDialog::finished, [mb](int r) {
        // 【本讲最容易搞错的一条】QMessageBox 的 finished/result 里装的是 **StandardButton 位值**，
        // 不是 QDialog 的 0/1。判「用户点了确定」要比 QMessageBox::Ok，不能比 Accepted。
        qInfo() << "⑤ QMessageBox finished(" << r << ")，result() =" << mb->result()
                << "；它等于 QMessageBox::Ok(" << int(QMessageBox::Ok) << ") 吗 ="
                << (r == int(QMessageBox::Ok));
    });

    // ────────────────────────────────────────────────────────────────
    // 证据 ⑥ QColorDialog / QFontDialog：同理，实例 + show + 自动 accept
    // ────────────────────────────────────────────────────────────────
    auto *cd = new QColorDialog(&window);
    cd->setCurrentColor(QColor(Qt::darkBlue));      // qcolordialog.h 41 行 + QColor(Qt::GlobalColor)
    qInfo() << "⑥ QColorDialog 静态 getColor（56 行）**不调**；实例版 currentColor ="
            << cd->currentColor().name()            // 42 行 + QColor::name()
            << " → 想拿结果请连 colorSelected（69 行）或 accept 后读 selectedColor()（44 行）";
    QObject::connect(cd, &QColorDialog::colorSelected, [](const QColor &c) {
        qInfo() << "⑥ colorSelected →" << c.name();
    });
    QObject::connect(cd, &QDialog::finished, [cd](int r) {
        qInfo() << "⑥ QColorDialog finished(" << r << ")；selectedColor =" << cd->selectedColor().name();
    });

    auto *fnd = new QFontDialog(&window);
    QFont f;                                        // QtGui/qfont.h：默认 ctor
    f.setPointSize(14);                             // 162 行
    f.setBold(true);                                // 173 行
    fnd->setCurrentFont(f);                         // qfontdialog.h 44 行
    qInfo() << "⑥ QFontDialog 静态 getFont（59~61 行，**第一个参数是 bool \\*ok**）**不调**；"
            << "实例版 currentFont =" << fnd->currentFont().family()
            << " pointSizeF =" << fnd->currentFont().pointSizeF()
            << " bold =" << fnd->currentFont().bold();   // qfont.h 152/163/172 行
    QObject::connect(fnd, &QFontDialog::fontSelected, [](const QFont &sel) {
        qInfo() << "⑥ fontSelected →" << sel.family() << sel.pointSizeF();
    });

    window.show();

    // ── 自动脚本：show / open / accept / reject / click 全排进事件循环 ──
    QTimer::singleShot(200, [d1] {
        d1->open();                                  // qdialog.h 53 行：**模态但不嵌套事件循环**
        qInfo() << "① 走 open()：模态效果有（主窗被挡），但 main 已经往下走了 —— "
                   "这就是 open() 相对 exec() 的全部优势（54 行的 exec 会停在那一行）";
    });
    QTimer::singleShot(500, [d1] {
        d1->accept();                                // 56 行
    });

    QTimer::singleShot(800, [d2, d3] {
        d2->show();
        d3->show();
        qInfo() << "② 两个对话框走 show()：WindowModal / ApplicationModal 的差别要**真人点主窗**才看得出来，"
                   "这里只打印枚举值当证据（WindowModal =" << int(Qt::WindowModal)
                << "，ApplicationModal =" << int(Qt::ApplicationModal) << "）";
    });
    QTimer::singleShot(1100, [d2, d3] {
        d2->reject();
        d3->reject();
    });

    QTimer::singleShot(1400, [form] {
        form->show();                                // 非模态路线：**不会**阻塞
        qInfo() << "③ FormDialog 用 show()（不是 exec）：主循环继续跑，脚本才能往下填值";
    });
    QTimer::singleShot(1700, [form] {
        form->fill(QStringLiteral("ab"), QStringLiteral("5"));   // 故意短：校验必须拦住
        form->pressOk();
        qInfo() << "③ 第一次按 OK 之后 visible =" << form->isVisible()
                << "（**预期 true：校验没过，窗子还开着**）result =" << form->result();
    });
    QTimer::singleShot(2000, [form] {
        form->fill(QStringLiteral("写第 5 讲"), QStringLiteral("3"));
        form->pressOk();
    });
    QTimer::singleShot(2300, [form] {
        qInfo() << "③ 第二次按 OK 之后 visible =" << form->isVisible()
                << "（预期 false）result =" << form->result()
                << "（预期 1 = QDialog::Accepted，qdialog.h 第 31 行）";
        form->deleteLater();
    });

    QTimer::singleShot(2600, [fd] {
        fd->show();                                  // 非模态文件对话框 + 上面三个信号 = 现代写法
        qInfo() << "④ QFileDialog 已 show()（非模态）：真实程序在这之后就可以继续干活，"
                   "文件选好了自然收到 fileSelected";
    });
    QTimer::singleShot(2900, [fd] {
        fd->reject();                                // 无人值守：没人能点「打开」，只能自己收摊
        qInfo() << "④ 用 reject() 收尾（**换成 exec() 这行就是超时**）";
    });

    QTimer::singleShot(3200, [mb] {
        mb->show();
        qInfo() << "⑤ QMessageBox 实例版 show()：静态版同样效果但会阻塞，本讲不用";
    });
    QTimer::singleShot(3500, [mb] {
        QAbstractButton *ok = mb->button(QMessageBox::Ok);        // qmessagebox.h 134 行
        qInfo() << "⑤ 拿到内置 OK 按钮 =" << (ok != nullptr) << "，现在 click() 它";
        if (ok) ok->click();                        // qabstractbutton.h 85 行 → QMessageBox::done(位值)
    });
    QTimer::singleShot(3800, [cd, fnd] {
        cd->show();
        fnd->show();
        qInfo() << "⑥ 颜色/字体对话框 show() 完，接下来程序自动 accept，不劳用户";
    });
    QTimer::singleShot(4100, [cd, fnd] {
        cd->accept();                               // QDialog public slot（56 行）
        fnd->accept();
        qInfo() << "⑥ 两个都 accept 了；selectedColor 的 name() 与 selectedFont 的 family() 见上面 finished 打印";
    });

    QTimer::singleShot(4400, [] {
        qInfo() << "\n=== 结论 ===";
        qInfo() << "① exec() 阻塞 / open() 模态不阻塞 / show() 非模态；三条出口都汇入 done(int) → finished(int)。";
        qInfo() << "② setModal(true) 对 show() 无效，真模态请用 open() 或 setWindowModality(三档)。";
        qInfo() << "③ 收尾逻辑一律连 finished()，别连按钮的 clicked()：Esc、关闭按钮、代码 reject() 都会漏。";
        qInfo() << "④ QMessageBox 的 result() 是 StandardButton 位值（Ok = 1024），不是 1；别拿 Accepted 去比。";
        qInfo() << "⑤ 静态便捷函数（getOpenFileName / information / getColor / getFont）= 建对话框 + exec()，"
                   "在无人值守与自动化测试里是禁地；请用实例 + 信号。";
        QCoreApplication::quit();
    });

    int rc = app.exec();
    qInfo() << "主事件循环退出，rc =" << rc << "（对话框们已经把各自的 finished 打完了）";
    return rc;
}

// ============================================================================
// 附：对话框写法速查
// ============================================================================
/*
 *  想干什么                     阻塞写法（**别在示例里用**）        非阻塞写法（推荐）
 *  ------------------------------------------------------------------------------
 *  拿一个文件名                 QFileDialog::getOpenFileName()      QFileDialog 实例 + show() + fileSelected
 *  拿一个目录                   QFileDialog::getExistingDirectory() setFileMode(QFileDialog::Directory)
 *                               （190 行）                           + setOption(ShowDirsOnly)（48~57 行）
 *  问「确定吗」                 QMessageBox::question()（179 行）    QMessageBox 实例 + button(role) + finished
 *  拿颜色                       QColorDialog::getColor()（56 行）    实例 + show() + colorSelected
 *  拿字体                       QFontDialog::getFont()（59 行）      实例 + show() + fontSelected
 *  模态但仍要主循环活着          ——                                 QDialog::open()（53 行）
 *  真·阻塞拿返回值              QDialog::exec()（54 行）            finished(int) 信号里继续
 *
 * 四条纪律：
 *  ① **对话框的生命周期用 finished 收尾，不用按钮 clicked**。
 *     用户按 Esc、点窗口关闭按钮、你代码里 reject()，都只走 done()/finished()，
 *     不走 accepted()/rejected() 之外的路径——但**不走**你连在按钮上的 clicked。
 *     本讲那个 FormDialog 把 accepted 连到校验槽、把收尾逻辑放在 finished，
 *     就是这个规矩的示范。
 *  ② **对话框的 parent 决定它挡谁**。传 &window → 它是 window 的子/模态对象；
 *     传 nullptr → 它没有相对位置依据、也没法算 WindowModal（WindowModal 必须有 parent）。
 *  ③ **别把 exec() 藏在工具函数里**。`QString askForFile()` 这种签名一旦进了
 *     库代码，将来所有自动化测试都会在那一行挂住，而且很难查——因为它在别人的栈帧里。
 *     需要「同步拿结果」的语义，请用回调/信号，或者显式命名成 xxxBlocking()。
 *  ④ 自定义对话框别用「绝对定位/固定尺寸」，交给布局；
 *     sizeGripEnabled（41 行）只是给你一个把手，**内容会不会跟着缩是布局的事**（第 7、8 讲）。
 *
 * ——— 老师傅的话 ———
 *   徒弟，对话框这块，我给你的不是 API，是一句话：**能阻塞的地方都能不该阻塞**。
 *   很多人觉得「弹个框等用户选」是天经地义的，于是写代码时不假思索就 exec()。
 *   然后他会撞上三件事：
 *     一、界面在长任务里白掉了——因为 exec() 里是嵌套循环，你的进度更新代码排在外面；
 *     二、测试跑不完——ctest 挂了 900 秒然后超时，CI 一片红，最后没人敢给这个模块写测试；
 *     三、用户能在弹窗还开着的时候操作主界面（WindowModal 与 ApplicationModal 的差别），
 *         于是出现「对话框还开着，主窗已经删了数据」的怪状态。
 *   所以本讲把「show + 信号 + 定时器收尾」写成硬规矩。真实项目里，
 *   只有那些「不拿到答案就确实没法往下走」的模态（比如保存文件）才配用 exec()，
 *   而且要用就得接受「它不可自动化」这个代价。多数时候 open() 或 show() 更香。
 *
 *   还有一条我要重复三遍：**QMessageBox 的 result() 是 StandardButton 位值**。
 *   Ok 是 1024，Cancel 是 4194304（qmessagebox.h 第 71~100 行）。
 *   `if (dlg->result() == QDialog::Accepted)` 套在 QMessageBox 上，
 *   永远为假——代码看起来没问题、点了确定就是不执行，
 *   我见过至少五次，每次都花掉半天。
 *
 * ——— 小练习 ———
 *   1. 把 FormDialog 的 OK/Cancel 换成「上一步/下一步/完成」三键向导：
 *      用 addButton(QDialogButtonBox::Back | Forward | Commit) 组合（96~97 行），
 *      连 clicked(QAbstractButton*) + buttonRole（101 行）自己决定何时 accept()。
 *   2. 给证据③ 加一个「非模态 → 模态」开关：加一个复选框控制
 *      d->setWindowModality(Qt::WindowModal / NonModal)，
 *      在 offscreen 平台下打印 windowModality 值，说明为什么肉眼看不出来、只能读值。
 *   3. 用 QTimer 把证据⑤ 改成「三种出口都试一遍」：
 *      (a) click(Ok)、(b) click(Cancel)、(c) 直接 mb->reject()，
 *      分别打印 finished 的整数并写回注释，
 *      再验证：三种情况下 setCheckBox 那个「不再提示」的勾选状态怎么读回来
 *      （getter 已核到：qmessagebox.h 第 162 行 `QCheckBox *checkBox() const;`，
 *        配合 qcheckbox.h 第 37 行的 checkState()/isChecked() 读）。
 */
// ============================================================================

// 本文件在 .cpp 里定义了 Q_OBJECT 类（FormDialog）：AUTOMOC 会生成同名 05_dialogs.moc，
// 必须在**类定义之后**包含，否则链接时报 undefined reference to `FormDialog::staticMetaObject`。
#include "05_dialogs.moc"
