// ============================================================================
// 第 7 讲 · 拖放（drag and drop）
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   拖放是事件系统里唯一「要跨进程说话」的一段，也是最容易写成「在我机器上好好的」的
//   一段。本讲把五件事讲清，并且**全程不用鼠标**：
//     1) QMimeData 是唯一的载体：setText/setUrls/setData + hasText/hasFormat/formats/data；
//     2) dragEnterEvent 里**必须 acceptProposedAction()**（或 setDropAction），
//        只调 accept() 不够——光标反馈与最终 dropAction 走的是另一条路；
//     3) dropEvent 里怎么把数据接住（含「自定义 MIME 类型」这一条内部拖放特权）；
//     4) 真正的拖动用 QDrag::exec()，启动阈值是
//        **mouseMoveEvent 里 manhattanLength() > QApplication::startDragDistance()**；
//        exec() 内部自己开嵌套事件循环（回指第 1 讲）；
//     5) 内部拖放 vs 跨程序拖放的真实差别（序列化、可辨认的 MIME 名、光标/来源判定）。
//
// 对应构建目标名：events_07_drag_and_drop
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(events_07_drag_and_drop 07_drag_and_drop.cpp)
//   target_link_libraries(events_07_drag_and_drop PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)   # 本讲示例不需要 Q_OBJECT（拖放 handler 全是虚函数继承）
//
// 运行前须知：
//   · 无人值守的诀窍：**在进程内直接构造合成事件投递**——
//     QMimeData 摆在栈上，QDragEnterEvent / QDropEvent 用 qevent.h 里核对过的构造
//     手工做出来，再 QApplication::sendEvent() 投给目标控件，
//     于是「没有鼠标也能把数据接住」这件事变成可打印的证据。
//   · 真实拖动（QDrag::exec）需要指针按下与移动，本示例**不自动跑它**，
//     只把正确的写法完整列出并解释阈值；想亲手验，请按注释里那句「手动路径」用鼠标拖一次。
//   · 本机当前无法编译链接 Qt 运行时，本示例**未在本机实测编译**；
//     所有签名都对着 C:\msys64\ucrt64\include\qt6 的真实头文件核对（注释给了文件与行号）。
// ============================================================================

#include <QApplication>
#include <QCoreApplication>
#include <QWidget>
#include <QEvent>
#include <QMimeData>              // QtCore/qmimedata.h：数据载体在 **QtCore**，不在 QtGui
#include <QUrl>                   // QtCore/qurl.h
#include <QDrag>                  // QtGui/qdrag.h
#include <QDragEnterEvent>        // QtGui/qevent.h
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QPainter>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QList>
#include <QDebug>

// ============================================================================
// 一、核对签名（本讲全部依据）
// ============================================================================
// QtCore/qmimedata.h（第 18 行起）
//   20  QMimeData();                 21  ~QMimeData();
//   23  QList<QUrl> urls() const;    24  void setUrls(const QList<QUrl> &urls);  25  bool hasUrls() const;
//   27  QString text() const;        28  void setText(const QString &text);      29  bool hasText() const;
//   31  QString html() const;        32  void setHtml(const QString &html);      33  bool hasHtml() const;
//   35  QVariant imageData() const;  36  void setImageData(const QVariant &);    37  bool hasImage() const;
//   43  QByteArray data(const QString &mimetype) const;
//   44  void setData(const QString &mimetype, const QByteArray &data);
//   45  void removeFormat(const QString &mimetype);
//   47  virtual bool hasFormat(const QString &mimetype) const;
//   48  virtual QStringList formats() const;
//   50  void clear();
//   53  【protected】virtual QVariant retrieveData(const QString &mimetype,
//                                                  **QMetaType** preferredType) const;
//        ← Qt5 这个函数第二个参数是 QVariant::Type；Qt6 换成 QMetaType。重写它的老代码必改。
//        ← 另外 QMimeData::Q_DISABLE_COPY（第 56 行）：**不可拷贝**，只能传指针、谈所有权。
// QtGui/qevent.h
//   710 class QDropEvent : public QEvent
//   714 QDropEvent(const QPointF &pos, Qt::DropActions actions, const QMimeData *data,
//                  Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers,
//                  Type type = Drop);
//        · position() → QPointF（19 行）；pos()/posF()/mouseButtons()/keyboardModifiers()
//          均 QT_DEPRECATED_VERSION_X_6_0（10~16 行）
//        · possibleActions()（23）/ proposedAction()（24）/ dropAction()（27）/ setDropAction()（28）
//        · **acceptProposedAction()（25 行，内联）**：
//              { m_dropAction = m_defaultAction; accept(); }
//          ← 一句话讲清它的本质：**accept() 只说「我要这个事件」，
//             acceptProposedAction() 才说「我按对方提议的动作接收这次拖放」**。
//        · mimeData()（31 行）返回 const QMimeData*
//        · 749 行 protected 段里 `friend class QApplication;` ——
//          所以 **source() 只能由 Qt 自己填**（731 行 QObject* source() const），
//          手工合成的事件里它是 **nullptr**，你的代码必须判空。
//   754 class QDragMoveEvent : public QDropEvent（多一个 reply()）
//   779 class QDragEnterEvent : public QDragMoveEvent
//        · 785 行 QDragEnterEvent(QPointF pos, Qt::DropActions actions, const QMimeData *data,
//                                 Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers);
//        · 上面那个收 QPoint 的重载在本机 6.12 已标 **QT_DEPRECATED_VERSION_X_6_16**
//   793 class QDragLeaveEvent : public QEvent（无数据）
// QtGui/qdrag.h（第 22 行起）
//   27  explicit QDrag(QObject *dragSource);     ← **Qt6 收 QObject\***；Qt5 是 QWidget*
//   30  void setMimeData(QMimeData *data);       ← 所有权交给 QDrag（文档语义），别再自己 delete
//   31  QMimeData *mimeData() const;
//   33  void setPixmap(const QPixmap &);         36  void setHotSpot(const QPoint &);
//   42  Qt::DropAction exec(Qt::DropActions supportedActions = Qt::MoveAction);
//   43  Qt::DropAction exec(Qt::DropActions supportedActions, Qt::DropAction defaultAction);
//   45  void setDragCursor(const QPixmap &, Qt::DropAction);
//   51  static void cancel();
//   54  Q_SIGNAL void actionChanged(Qt::DropAction action);
// QtWidgets/qapplication.h
//   111 static int startDragTime();      113 static int startDragDistance();
//   （Qt6 里**查不到** autoStartDragDistance / autoStartDragTime 这两个 Qt5 名字，
//     本讲只用 startDragDistance()/startDragTime()）
// QtCore/qnamespace.h
//   1500 enum DropAction { CopyAction=0x1, MoveAction=0x2, LinkAction=0x4, ActionMask=0xff,
//                          TargetMoveAction=0x8002, IgnoreAction=0x0 }
//   1508 Q_DECLARE_FLAGS(DropActions, DropAction)
//   68   enum MouseButton（LeftButton=0x1…）；107 MouseButtons 为它的 flags
//   1369 enum FocusReason / 1077 enum KeyboardModifier（合成事件里要填 modifiers）
// QtWidgets/qwidget.h（拖放四件套全在 protected 段，第 658 行起）
//   689 dragEnterEvent(QDragEnterEvent*)   690 dragMoveEvent(QDragMoveEvent*)
//   691 dragLeaveEvent(QDragLeaveEvent*)   692 dropEvent(QDropEvent*)
//   558 setAcceptDrops(bool)               336 setMouseTracking(bool)

// ============================================================================
// 二、接住拖放的控件
// ============================================================================
class DropBox : public QWidget
{
public:
    int enterCount = 0;
    int moveCount = 0;
    int leaveCount = 0;
    int dropCount = 0;
    int rejectedCount = 0;
    QString lastText;
    QStringList lastFormats;
    QString lastSource = QStringLiteral("<无>");
    int lastDropAction = -1;

    explicit DropBox(const QString &name, bool acceptDrops = true)
    {
        setObjectName(name);
        setFixedSize(200, 110);
        setAcceptDrops(acceptDrops);           // qwidget.h 558 行 —— **不设就收不到拖放事件**
    }

protected:
    // 第 1 步：进入。这里唯一的任务是「表态」：这次拖放我接不接受、按哪个动作接。
    void dragEnterEvent(QDragEnterEvent *e) override
    {
        ++enterCount;
        lastFormats = e->mimeData() ? e->mimeData()->formats() : QStringList();
        qInfo().noquote() << QStringLiteral("    %1 dragEnter：位置=(%2,%3) possibleActions=0x%4 "
                                            "proposedAction=0x%5 formats=%6")
                                 .arg(objectName())
                                 .arg(e->position().x()).arg(e->position().y())
                                 .arg(int(e->possibleActions()), 0, 16)
                                 .arg(int(e->proposedAction()), 0, 16)
                                 .arg(lastFormats.join(QStringLiteral(", ")));

        const bool canTake = e->mimeData() && (e->mimeData()->hasText()
                            || e->mimeData()->hasFormat(QStringLiteral("application/x-qt-learning-item")));
        if (canTake) {
            // ★ 关键一行：acceptProposedAction()（qevent.h 25 行的内联实现）
            //   只写 e->accept() 的话事件是被「接住」了，但 dropAction 仍是 IgnoreAction，
            //   光标/最终动作判定会不对——这就是「界面显示禁止符号」的根因。
            e->acceptProposedAction();
            qInfo() << "      → acceptProposedAction() 已调用，dropAction 现在是 0x"
                    << QString::number(int(e->dropAction()), 16);
        } else {
            ++rejectedCount;
            e->ignore();                       // qcoreevent.h 314 行
            qInfo() << "      → 数据格式不认识，ignore()";
        }
    }

    void dragMoveEvent(QDragMoveEvent *e) override
    {
        ++moveCount;
        // 拖动过程中位置在变，可能要重新表态（比如从「可放区」移到「不可放区」）
        if (e->mimeData() && e->mimeData()->hasText())
            e->acceptProposedAction();
        else
            e->ignore();
    }

    void dragLeaveEvent(QDragLeaveEvent *) override
    {
        ++leaveCount;
        qInfo() << "    " << objectName() << " dragLeave（把高亮/插入线撤掉的地方）";
    }

    // 第 2 步：真正取数据
    void dropEvent(QDropEvent *e) override
    {
        ++dropCount;
        lastDropAction = int(e->dropAction());
        const QMimeData *md = e->mimeData();
        if (md && md->hasText())
            lastText = md->text();             // qmimedata.h 27 行

        if (md && md->hasFormat(QStringLiteral("application/x-qt-learning-item"))) {
            const QByteArray raw = md->data(QStringLiteral("application/x-qt-learning-item"));  // 43 行
            qInfo() << "    " << objectName() << " 取到自定义 MIME 数据，长度 =" << raw.size()
                    << "内容 =" << QString::fromUtf8(raw);
        }
        if (md && md->hasUrls()) {
            const QList<QUrl> us = md->urls();                 // 23 行
            QStringList strs;
            for (const QUrl &u : us)
                strs << (u.isLocalFile() ? u.toLocalFile() : u.toString());
            qInfo() << "    " << objectName() << " 取到" << us.size() << "个 URL：" << strs;
        }
        // source() 由 Qt 内部填（qevent.h 731 行 + protected 段 friend class QApplication），
        // 合成的事件里它是 nullptr —— 判空是**义务**，不是可选项。
        QObject *src = e->source();
        lastSource = src ? src->objectName() : QStringLiteral("<nullptr：合成事件/跨程序拖动都可能是空>");
        qInfo().noquote() << QStringLiteral("    %1 dropEvent：text=\"%2\" dropAction=0x%3 source=%4")
                                 .arg(objectName()).arg(lastText)
                                 .arg(lastDropAction, 0, 16).arg(lastSource);

        e->acceptProposedAction();
        update();                              // qwidget.h 470 行：重绘请求，别在 drop 里直接画
    }

    // 顺手把「接住了什么」画出来，肉眼可核（绘制永远在 paintEvent 里，第 1 讲的规矩）
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setPen(Qt::black);
        p.drawRect(rect().adjusted(1, 1, -2, -2));
        const QString label = lastText.isEmpty()
                ? QStringLiteral("%1\n(acceptDrops=%2)").arg(objectName())
                        .arg(testAttribute(Qt::WA_TransparentForMouseEvents) ? "-" : "开")
                : QStringLiteral("%1\n收到: %2").arg(objectName(), lastText);
        p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, label);
    }
};

// ============================================================================
// 三、发起拖动的控件（写法示范；自动流程里不真的调用 exec）
// ============================================================================
class DragSource : public QWidget
{
public:
    Qt::DropAction lastExecResult = Qt::IgnoreAction;
    int dragStarted = 0;
    QPoint m_pressPos;                            // 按下起点：mouseMoveEvent 里的拖动阈值要跟它比

    explicit DragSource(const QString &name)
    {
        setObjectName(name);
        setFixedSize(200, 110);
    }

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton)
            m_pressPos = e->position().toPoint();      // 记下起点，阈值要跟它比
        QWidget::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!(e->buttons() & Qt::LeftButton))
            return;                                    // 没有按键按着：这是「悬停移动」
        // ★ 拖动阈值：Qt6 用 QPoint::manhattanLength()，阈值来自 QApplication::startDragDistance()
        const QPoint delta = e->position().toPoint() - m_pressPos;
        if (delta.manhattanLength() < QApplication::startDragDistance())
            return;                                    // 还没到「算拖动」的距离，别急着起拖

        ++dragStarted;
        startDrag();
        m_pressPos = QPoint();                          // 一次按下只起一次拖
    }

    void startDrag()
    {
        auto *md = new QMimeData;                       // 所有权交给 QDrag（qdrag.h 30 行）
        md->setText(QStringLiteral("从 %1 拖出来的文本").arg(objectName()));
        md->setData(QStringLiteral("application/x-qt-learning-item"),
                    QByteArrayLiteral("payload-from-DragSource"));
        md->setUrls({QUrl::fromLocalFile(QStringLiteral("C:/Temp/示例文件.txt")),
                     QUrl(QStringLiteral("https://doc.qt.io/qt-6/drag-and-drop.html"))});

        QPixmap pm(80, 40);
        pm.fill(Qt::darkGreen);
        {
            QPainter p(&pm);
            p.setPen(Qt::white);
            p.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("拖动我"));
        }

        auto *drag = new QDrag(this);                   // **Qt6：QDrag(QObject*)**
        drag->setMimeData(md);
        drag->setPixmap(pm);
        drag->setHotSpot(QPoint(40, 20));               // 指针在 pixmap 上的落点

        // exec() **阻塞**在这里，内部开一个嵌套事件循环（第 1 讲的证据⑤ 就是它）：
        // 拖动期间界面还活着，但你的代码在栈里等结果 → 重入风险按第 1 讲的规矩处理。
        const Qt::DropActions supported = Qt::CopyAction | Qt::MoveAction;
        lastExecResult = drag->exec(supported, Qt::CopyAction);   // 第二个参数是默认动作
        qInfo() << "    QDrag::exec() 返回动作 = 0x"
                << QString::number(int(lastExecResult), 16)
                << "（Qt::IgnoreAction 表示没人接）";
        delete drag;                                    // drag 由你 new，也由你 delete
    }
};

// ============================================================================
// 四、合成一次「进入 + 落下」
// ============================================================================
static void synthDrop(QWidget *target, const QPointF &where, QMimeData *md,
                      Qt::DropActions possible = Qt::CopyAction | Qt::MoveAction,
                      Qt::DropAction proposed = Qt::CopyAction)
{
    // 第 1 击：DragEnter（核对 qevent.h 785 行；注意第一个参数是 **QPointF**）
    QDragEnterEvent enter(where, possible, md, Qt::LeftButton, Qt::NoModifier);
    qInfo() << "  投递 QDragEnterEvent →";
    QCoreApplication::sendEvent(target, &enter);
    qInfo() << "    enter 之后 isAccepted =" << enter.isAccepted()
            << "dropAction = 0x" << QString::number(int(enter.dropAction()), 16);

    // 第 2 击：Drop（核对 qevent.h 714 行：
    //   QDropEvent(const QPointF&, Qt::DropActions, const QMimeData*,
    //              Qt::MouseButtons, Qt::KeyboardModifiers, Type type = Drop)）
    // ⚠ 只带 buttons/modifiers 的 QPoint 版重载在 6.16 起已弃用（QDragEnterEvent 里核到）。
    QDropEvent drop(where, possible, md, Qt::LeftButton, Qt::NoModifier, QEvent::Drop);
    drop.setDropAction(proposed);                    // qevent.h 28 行：设定「提议的动作」
    qInfo() << "  投递 QDropEvent →";
    QCoreApplication::sendEvent(target, &drop);      // 同步：返回时 dropEvent() 已跑完
    qInfo() << "    drop 之后 isAccepted =" << drop.isAccepted()
            << "dropAction = 0x" << QString::number(int(drop.dropAction()), 16);
}

// ============================================================================
// 五、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle(QStringLiteral("第 7 讲 · 拖放"));
    window.resize(640, 300);

    auto *box = new DropBox(QStringLiteral("接收盒"));
    box->move(20, 20);
    auto *boxNoAccept = new DropBox(QStringLiteral("没开acceptDrops"), /*acceptDrops=*/false);
    boxNoAccept->move(240, 20);
    auto *source = new DragSource(QStringLiteral("拖动源"));
    source->move(460, 20);
    window.show();

    // ── 事实① 阈值与系统的建议值
    qInfo() << "事实① 拖动阈值：QApplication::startDragDistance() ="
            << QApplication::startDragDistance() << "px，startDragTime() ="
            << QApplication::startDragTime() << "ms（qapplication.h 111/113 行）"
            << "→ 这两个值来自系统/样式，**别在代码里写死 10 像素**，用 API 取。";

    // ── 事实② QMimeData 能力自检（数据格式列表是拖放双方的「合同」）
    QMimeData probe;
    probe.setText(QStringLiteral("hello"));
    probe.setHtml(QStringLiteral("<b>hello</b>"));
    probe.setData(QStringLiteral("application/x-qt-learning-item"), QByteArrayLiteral("k=1"));
    probe.setUrls({QUrl::fromLocalFile(QStringLiteral("C:/Temp/a.txt"))});
    qInfo() << "事实② 一个 QMimeData 可以同时装多种格式：formats() =" << probe.formats()
            << "hasText =" << probe.hasText() << "hasUrls =" << probe.hasUrls()
            << "hasHtml =" << probe.hasHtml()
            << "hasFormat(自定义) =" << probe.hasFormat(QStringLiteral("application/x-qt-learning-item"))
            << "自定义 data 长度 =" << probe.data(QStringLiteral("application/x-qt-learning-item")).size();
    // 注意：setText 之后 formats() 里出现的是 text/plain（以及 Qt 顺手给的
    // text/uri-list / text/html 等衍生格式）；**具体列表由 Qt 决定，本讲不背它**，
    // 让程序自己打印才是可靠的做法。

    // ── 演示① 合成拖放：数据被接住（这是本讲「无人值守」的核心）
    QTimer::singleShot(400, [&] {
        qInfo() << "\n=== 演示① 进程内合成一次拖放（没有鼠标）===";
        QMimeData md;
        md.setText(QStringLiteral("第 7 讲的载荷"));
        md.setData(QStringLiteral("application/x-qt-learning-item"),
                   QByteArrayLiteral("内部拖放特权：随意的 MIME 名与任意字节"));
        md.setUrls({QUrl::fromLocalFile(QStringLiteral("C:/Temp/示例文件.txt")),
                    QUrl(QStringLiteral("https://doc.qt.io"))});

        const int before = box->dropCount;
        synthDrop(box, QPointF(30, 30), &md);
        qInfo() << "演示① 结果：接收盒 enter" << box->enterCount << "次、drop"
                << (box->dropCount - before) << "次；接到的文本 =\"" << box->lastText
                << "\"来源 =" << box->lastSource
                << "（source 为空是**预期**：合成事件没人填它，见 qevent.h 731 行的 source()）";
        // ⚠ 生命周期：sendEvent 不接管事件、也不接管 mimeData（QDropEvent 只存指针）。
        //    md 在这里是**栈对象**，必须活得比 sendEvent 这次调用久——上面正是这样写的。
        //    若换成 postEvent，就必须把 md 也放到堆上并自己安排好删除时机。
    });

    // ── 演示② 没开 setAcceptDrops 的控件收不到拖放事件
    QTimer::singleShot(800, [&] {
        qInfo() << "\n=== 演示② setAcceptDrops(false) 的控件 ===";
        QMimeData md;
        md.setText(QStringLiteral("投给没开开关的盒子"));
        const int before = boxNoAccept->enterCount;
        synthDrop(boxNoAccept, QPointF(30, 30), &md);
        qInfo() << "演示② 结果：「没开acceptDrops」的 dragEnter 计数增量 ="
                << (boxNoAccept->enterCount - before) << "、drop 计数 =" << boxNoAccept->dropCount
                << "→ 期望全 0。**这是新手最常见的「我的 dropEvent 怎么不进」的第一因**，"
                << "第二因是数据格式不匹配（走的是 ignore() 分支），第三因是父控件把事件吞了。";
    });

    // ── 演示③ accept() vs acceptProposedAction() 的差别（光标反馈的真相）
    QTimer::singleShot(1200, [&] {
        qInfo() << "\n=== 演示③ 只 accept() 不 acceptProposedAction() 会怎样 ===";
        QMimeData md;
        md.setText(QStringLiteral("只 accept"));
        QDragEnterEvent enter(QPointF(20, 20), Qt::CopyAction | Qt::MoveAction, &md,
                              Qt::LeftButton, Qt::NoModifier);
        enter.accept();                    // ← 只把 isAccepted 置真
        qInfo() << "  只调 accept() 之后：isAccepted =" << enter.isAccepted()
                << "dropAction = 0x" << QString::number(int(enter.dropAction()), 16)
                << "（0 = Qt::IgnoreAction）";
        QDragEnterEvent enter2(QPointF(20, 20), Qt::CopyAction | Qt::MoveAction, &md,
                               Qt::LeftButton, Qt::NoModifier);
        enter2.acceptProposedAction();     // ← 内联实现：m_dropAction = m_defaultAction; accept();
        qInfo() << "  调 acceptProposedAction() 之后：isAccepted =" << enter2.isAccepted()
                << "dropAction = 0x" << QString::number(int(enter2.dropAction()), 16)
                << "→ 差别就在这一个字段上。拖放里**表态的是 dropAction，不是 isAccepted**，"
                << "光标（禁止/复制/移动/链接）按它来画。";
    });

    // ── 演示④ 数据格式不对 → 走 ignore 分支（接收端要有失败路径）
    QTimer::singleShot(1600, [&] {
        qInfo() << "\n=== 演示④ 只给一个我们不认识的数据类型 ===";
        QMimeData md;
        md.setData(QStringLiteral("application/vnd.something-else"), QByteArrayLiteral("x"));
        const int rejBefore = box->rejectedCount;
        QDragEnterEvent enter(QPointF(30, 30), Qt::CopyAction, &md, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(box, &enter);
        qInfo() << "  接收盒拒绝次数增量 =" << (box->rejectedCount - rejBefore)
                << "enter.isAccepted =" << enter.isAccepted()
                << "→ 之后用户真落下也不会有 dropEvent（拖动方拿到 IgnoreAction）。"
                << "结论：**dragEnter 里的判断要尽量便宜**，别在这里做 I/O 或解析大对象，"
                << "因为拖动过程中它会反复被调（dragMove 更频繁）。";
    });

    // ── 事实③ 内部拖放 vs 跨程序拖放（写在日志里，方便对着代码回想）
    QTimer::singleShot(2000, [] {
        qInfo() << "\n=== 事实③ 两种拖放的差别 ===";
        qInfo() << "  内部（同进程）：QMimeData **对象指针**直接递过去，数据不序列化，"
                << "自定义 MIME 名随便用，source() 是真实的 QObject*，还能传只在本进程有意义的复杂对象；"
                << "拖动过程可以很快（没有编解码）。";
        qInfo() << "  跨程序：数据要经 QPA 走平台的机制（Windows 是 OLE 拖放，"
                << "X11/Wayland 是各自的 DnD 协议），于是——"
                << "① MIME 名必须是对方认得的（text/plain、text/uri-list 这类标准名）；"
                << "② 数据要能变成 QByteArray，即 retrieveData(mimetype, QMetaType) 得给得出"
                << "（qmimedata.h 53 行；Qt5 这里是 QVariant::Type，重写它的老代码必改）；"
                << "③ 对方是另一个进程，**你无法在它 drop 之前问它要不要**，"
                << "所以 acceptProposedAction 的表态尤其重要；"
                << "④ 大对象请用 URL/文件路径传，别把整个字节流塞进拖放（会卡）。";
    });

    // ── 结论
    QTimer::singleShot(2400, [&box, &boxNoAccept, &source] {
        qInfo() << "\n=== 结论 ===";
        qInfo() << "接收盒：enter" << box->enterCount << "move" << box->moveCount
                << "leave" << box->leaveCount << "drop" << box->dropCount
                << "拒绝" << box->rejectedCount << "最后一次 dropAction = 0x"
                << QString::number(box->lastDropAction, 16);
        qInfo() << "没开开关的盒子：enter" << boxNoAccept->enterCount << "drop" << boxNoAccept->dropCount;
        qInfo() << "拖动源：本次自动流程起拖次数 =" << source->dragStarted
                << "（0 是**预期**：合成事件走的是接收端，QDrag::exec 需要真实指针按住+移动；"
                << "要验真拖动，请手动从「拖动源」拖到「接收盒」，一次即可，无需脚本）";
        qInfo() << "一句话：**QMimeData 谈格式、dragEnter 谈态度（acceptProposedAction）、"
                << "dropEvent 谈收获、startDragDistance 谈时机。**";
        QCoreApplication::quit();
    });

    return app.exec();
}

// ============================================================================
// 附：拖放的最小正确骨架（照抄这个，别自己发明）
// ============================================================================
/*
 *  接收端（target）
 *    setAcceptDrops(true);                                    // 忘了它，一切免谈
 *    void dragEnterEvent(QDragEnterEvent *e) {
 *        if (e->mimeData()->hasFormat(我的类型)) e->acceptProposedAction();   // 表态
 *        else e->ignore();
 *    }
 *    void dragMoveEvent(QDragMoveEvent *e)   { 同上，判断可以更快 }
 *    void dropEvent(QDropEvent *e) {
 *        const QMimeData *md = e->mimeData();
 *        ... 取数据 ...
 *        e->setDropAction(Qt::MoveAction);   // 需要指定动作时才写
 *        e->acceptProposedAction();
 *    }
 *
 *  发起端（source）
 *    void mousePressEvent(QMouseEvent *e)   { if (e->button()==Qt::LeftButton) m_pos=e->position().toPoint(); }
 *    void mouseMoveEvent(QMouseEvent *e)   {
 *        if (!(e->buttons() & Qt::LeftButton)) return;
 *        if ((e->position().toPoint()-m_pos).manhattanLength() < QApplication::startDragDistance()) return;
 *        auto *drag = new QDrag(this);                       // Qt6：this 可以是任意 QObject
 *        auto *md = new QMimeData; md->setText(...); md->setData(我的类型, 字节);
 *        drag->setMimeData(md);                              // 所有权交给 drag
 *        drag->setPixmap(缩略图); drag->setHotSpot(指针在图上的位置);
 *        if (drag->exec(Qt::CopyAction|Qt::MoveAction, Qt::CopyAction) == Qt::MoveAction) 删掉原数据;
 *    }
 *
 * 四条纪律：
 *  ① **acceptProposedAction 不是可选项**。它同时做两件事：置 dropAction、accept()。
 *     只 accept() 的话，Qt 认为你接了事件但没接动作，光标与最终动作都会不对。
 *  ② dragEnter/dragMove 每次移动都会跑，里面**只做便宜判断**；解析、读文件、
 *     申请内存这些活全留到 dropEvent。
 *  ③ 自定义 MIME 名是内部拖放的福利、跨程序的雷：起名叫 `应用名/语义`
 *     （本讲用 application/x-qt-learning-item），并且永远同时准备一份标准格式
 *     （text/plain）作为对外兼容。
 *  ④ **QDrag::exec 里嵌着一个事件循环**：它在你的 mouseMoveEvent 栈上一路阻塞，
 *     这期间任何事件都可能进来（第 1 讲的重入清单在此完全适用）。
 *     别在起拖之前改状态又指望它保持不变。
 *
 * ——— 老师傅的话 ———
 *   徒弟，拖放这个功能，代码量小，坑全是「看不见」的那一类。为什么？
 *   因为你平时测试是**用鼠标拖的**，鼠标一拖，进-移-落三步全走通，你只看到了「成功的那条路」。
 *   而用户的失败发生在另外三条路上：格式不对、开关没开、光标没反馈。
 *
 *   所以师傅教你本讲这一招——**在进程内自己造事件投给自己**。
 *   QMimeData 摆栈上，QDragEnterEvent / QDropEvent 按 qevent.h 里核对过的构造做出来，
 *   sendEvent 一投，接收端的行为立刻可见、可计数、可断言。这样你能把
 *   「只有 text/plain 时接不接」「只有自定义类型时拒不拒」「没开 acceptDrops 时到底进不进」
 *   每一种情况都跑一遍，还能写进 CI。
 *   记住这句：**能被程序复现的交互，才叫被测过的交互。**
 *
 *   再说跨进程那件事。你在自己程序里传个 QByteArray 叫「自定义类型」，
 *   出了你的进程它就叫「没人认的数据」。真正的资深做法是：**同时给多份表示**，
 *   文本给 text/plain、文件给 text/uri-list、自己的结构体给自定义类型，
 *   让对方按标准名去挑。这么写一次，你的程序就能和整个桌面环境互通；
 *   偷懒只给自定义类型，程序就只能跟自己说话。
 *
 * ——— 小练习 ———
 *   1. 给 DropBox 再加一个格式判断：只接受「urls 里全部是 .txt」的拖放，
 *      其他一律 ignore()。然后用 synthDrop 分别投「1 个 txt」「1 个 exe」「txt+exe 混合」
 *      三种 QMimeData，把三次的 enter/drop 计数与 dropAction 打表。
 *   2. 把演示③ 的 `enter.accept()` 那行改成 `enter.setDropAction(Qt::MoveAction); enter.accept();`
 *      （qevent.h 28/310 行），对照打印 dropAction，说出「setDropAction + accept」
 *      与「acceptProposedAction」的等价性与差别。
 *   3. 手动路径：把 DragSource::startDrag() 里的 exec 换成
 *      `exec(Qt::CopyAction)`（只支持复制），用鼠标从「拖动源」拖到「接收盒」，
 *      观察光标形状与打印出的 exec 返回值；再拖到窗口**外面**松手，看返回什么动作。
 *      最后回答：为什么这条演示无法用合成事件代替？（提示：grab 与平台 DnD 会话。）
 */
// ============================================================================
