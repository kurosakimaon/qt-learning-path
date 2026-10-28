#include "ui/mainwindow.h"

#include "core/jsoncodec.h"
#include "core/statsservice.h"
#include "core/taskfilterproxy.h"
#include "core/taskmodel.h"
#include "ui/prioritydelegate.h"
#include "ui/taskdialog.h"

#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableView>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

MainWindow::MainWindow(const QString& databasePath, QWidget* parent)
    : QMainWindow(parent)
    , m_repository(QStringLiteral("todo_desktop_main"))
{
    m_model = new TaskModel(this);
    m_proxy = new TaskFilterProxy(this);
    m_proxy->setSourceModel(m_model);
    m_stats = new StatsService(this);

    buildUi();
    buildActions();

    // 先建界面再开库：即使数据库打不开，窗口也能显示，只是改动不落盘。
    QString error;
    if (!m_repository.open(databasePath, &error)) {
        statusBar()->showMessage(QStringLiteral("数据库打不开：%1").arg(error), 0);
    }

    loadFromDatabase();
    refreshStats();
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("待办清单 · Qt 学习路径实战"));
    setAcceptDrops(true);                 // 允许把 .json 拖进来

    auto* central = new QWidget(this);
    auto* outer = new QVBoxLayout(central);
    outer->setContentsMargins(12, 10, 12, 10);
    outer->setSpacing(8);

    auto* top = new QHBoxLayout;
    m_search = new QLineEdit(central);
    m_search->setPlaceholderText(QStringLiteral("搜索标题或备注…"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumWidth(240);

    m_filter = new QComboBox(central);
    m_filter->addItem(QStringLiteral("全部任务"));
    m_filter->addItem(QStringLiteral("只看未完成"));

    top->addWidget(new QLabel(QStringLiteral("搜索"), central));
    top->addWidget(m_search, 1);
    top->addSpacing(8);
    top->addWidget(m_filter);

    m_view = new QTableView(central);
    m_view->setModel(m_proxy);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setAlternatingRowColors(true);
    m_view->setSortingEnabled(true);
    m_view->setItemDelegateForColumn(TaskModel::PriorityColumn, new PriorityDelegate(this));
    m_view->verticalHeader()->hide();
    m_view->horizontalHeader()->setStretchLastSection(false);
    m_view->horizontalHeader()->setSectionResizeMode(TaskModel::TitleColumn,
                                                     QHeaderView::Stretch);
    m_view->horizontalHeader()->setSectionResizeMode(TaskModel::PriorityColumn,
                                                     QHeaderView::ResizeToContents);
    m_view->horizontalHeader()->setSectionResizeMode(TaskModel::DueColumn,
                                                     QHeaderView::ResizeToContents);
    m_view->horizontalHeader()->setSectionResizeMode(TaskModel::DoneColumn,
                                                     QHeaderView::ResizeToContents);
    m_view->sortByColumn(TaskModel::DueColumn, Qt::AscendingOrder);

    outer->addLayout(top);
    outer->addWidget(m_view, 1);
    setCentralWidget(central);

    m_statsLabel = new QLabel(QStringLiteral("统计准备中…"), this);
    m_statsLabel->setContentsMargins(0, 0, 8, 0);
    statusBar()->addPermanentWidget(m_statsLabel);
    statusBar()->showMessage(QStringLiteral("就绪：双击编辑，勾选完成，也可以把 .json 拖进来"));

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_proxy->setQueryText(text);
        updateActions();
        statusBar()->showMessage(
            QStringLiteral("已显示 %1 条（共 %2 条）")
                .arg(m_proxy->rowCount())
                .arg(m_model->rowCount()));
    });

    connect(m_filter, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_proxy->setOnlyOpen(index == 1);
        updateActions();
    });

    connect(m_view, &QTableView::doubleClicked, this, &MainWindow::editSelected);
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::updateActions);

    connect(m_model, &TaskModel::countsChanged, this, &MainWindow::updateCounts);

    connect(m_stats, &StatsService::statsReady, this,
            [this](const TaskStats& stats, qint64 elapsedMs) {
                m_statsLabel->setText(
                    QStringLiteral("共 %1 · 未完成 %2 · 逾期 %3 · 低/普通/高 %4/%5/%6 · 统计 %7 ms（后台线程）")
                        .arg(stats.total)
                        .arg(stats.open)
                        .arg(stats.overdue)
                        .arg(stats.low)
                        .arg(stats.normal)
                        .arg(stats.high)
                        .arg(elapsedMs));
            });

    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    const QByteArray headerState =
        settings.value(QStringLiteral("window/header")).toByteArray();
    if (!headerState.isEmpty())
        m_view->horizontalHeader()->restoreState(headerState);
}

void MainWindow::buildActions()
{
    auto* toolbar = addToolBar(QStringLiteral("任务"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    QAction* newAction = toolbar->addAction(QStringLiteral("新增"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::addTask);

    m_editAction = toolbar->addAction(QStringLiteral("编辑"));
    connect(m_editAction, &QAction::triggered, this, &MainWindow::editSelected);

    m_deleteAction = toolbar->addAction(QStringLiteral("删除"));
    m_deleteAction->setShortcut(QKeySequence::Delete);
    connect(m_deleteAction, &QAction::triggered, this, &MainWindow::deleteSelected);

    m_toggleAction = toolbar->addAction(QStringLiteral("切换完成"));
    connect(m_toggleAction, &QAction::triggered, this, &MainWindow::toggleSelected);

    toolbar->addSeparator();

    QAction* importAction = toolbar->addAction(QStringLiteral("导入 JSON"));
    importAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    connect(importAction, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("导入任务"), QString(),
            QStringLiteral("JSON 文件 (*.json);;所有文件 (*)"));
        if (!path.isEmpty())
            importFromFile(path);
    });

    QAction* exportAction = toolbar->addAction(QStringLiteral("导出 JSON"));
    exportAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    connect(exportAction, &QAction::triggered, this, &MainWindow::exportToFile);

    m_clearDoneAction = toolbar->addAction(QStringLiteral("清除已完成"));
    connect(m_clearDoneAction, &QAction::triggered, this, &MainWindow::removeCompleted);

    updateActions();
}

void MainWindow::loadFromDatabase()
{
    QString error;
    const QVector<Task> tasks = m_repository.loadAll(&error);
    if (!error.isEmpty())
        statusBar()->showMessage(QStringLiteral("读取失败：%1").arg(error), 8000);

    m_model->setTasks(tasks);
    if (m_model->rowCount() == 0)
        seedSampleTasks();
}

void MainWindow::seedSampleTasks()
{
    // 第一次运行给三条样例：一条逾期、一条高优先级、一条普通——打开就能看出颜色与排序。
    const QDate today = QDate::currentDate();

    Task overdue;
    overdue.title = QStringLiteral("把第柒阶段的线程归属示例跑一遍");
    overdue.priority = Priority::High;
    overdue.due = today.addDays(-1);
    overdue.notes = QStringLiteral("注意：只有 run() 跑在新线程里");

    Task upcoming;
    upcoming.title = QStringLiteral("手写一个 QAbstractTableModel");
    upcoming.priority = Priority::Normal;
    upcoming.due = today.addDays(3);
    upcoming.notes = QStringLiteral("五个纯虚函数 + flags/setData/dataChanged");

    Task someday;
    someday.title = QStringLiteral("给窗口加一个 QSettings 记住大小");
    someday.priority = Priority::Low;
    someday.notes = QStringLiteral("closeEvent 里 saveGeometry");

    for (Task task : {overdue, upcoming, someday}) {
        task.id = m_model->addTask(task);
        persistTask(task);
    }
    statusBar()->showMessage(QStringLiteral("数据库是空的，已放入 3 条样例任务"));
}

void MainWindow::addTask()
{
    TaskDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    Task task = dialog.task();
    task.id = m_model->addTask(task);
    persistTask(task);
    refreshStats();
    m_view->scrollToBottom();
}

int MainWindow::selectedSourceRow() const
{
    const QModelIndex proxyIndex = m_view->currentIndex();
    if (!proxyIndex.isValid())
        return -1;
    // 代理的 index 不能直接拿去问源模型，必须先映射；这就是第陆阶段那道坎。
    const QModelIndex sourceIndex = m_proxy->mapToSource(proxyIndex);
    return sourceIndex.isValid() ? sourceIndex.row() : -1;
}

void MainWindow::editSelected()
{
    const int row = selectedSourceRow();
    if (row < 0)
        return;

    TaskDialog dialog(this);
    dialog.setTask(m_model->taskAt(row));
    if (dialog.exec() != QDialog::Accepted)
        return;

    m_model->updateTask(row, dialog.task());
    persistTask(m_model->taskAt(row));
    refreshStats();
}

void MainWindow::deleteSelected()
{
    const int row = selectedSourceRow();
    if (row < 0)
        return;

    const Task task = m_model->taskAt(row);
    const auto answer = QMessageBox::question(
        this, QStringLiteral("删除任务"),
        QStringLiteral("确定删除「%1」？").arg(task.title),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    m_model->removeRows(row, 1);

    QString error;
    if (!m_repository.remove(task.id, &error))
        statusBar()->showMessage(QStringLiteral("删除失败：%1").arg(error), 8000);

    refreshStats();
}

void MainWindow::toggleSelected()
{
    const int row = selectedSourceRow();
    if (row < 0)
        return;

    const Task task = m_model->taskAt(row);
    if (!m_model->setDone(row, !task.done))
        return;

    persistTask(m_model->taskAt(row));
    refreshStats();
}

void MainWindow::removeCompleted()
{
    int done = 0;
    for (const Task& task : m_model->tasks()) {
        if (task.done)
            ++done;
    }
    if (done == 0) {
        statusBar()->showMessage(QStringLiteral("没有已完成的任务"), 4000);
        return;
    }

    const auto answer = QMessageBox::question(
        this, QStringLiteral("清除已完成"),
        QStringLiteral("确定清除 %1 条已完成任务？").arg(done),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    m_model->removeCompleted();

    QString error;
    if (!m_repository.removeCompleted(&error))
        statusBar()->showMessage(QStringLiteral("清除失败：%1").arg(error), 8000);

    refreshStats();
}

void MainWindow::importFromFile(const QString& path)
{
    QVector<Task> incoming;
    QString error;
    if (!JsonCodec::load(path, &incoming, &error)) {
        QMessageBox::warning(this, QStringLiteral("导入失败"), error);
        return;
    }

    int imported = 0;
    for (Task task : incoming) {
        task.id = 0;                       // 导入一律分配新 id，避免覆盖现有任务
        task.id = m_model->addTask(task);
        persistTask(task);
        ++imported;
    }

    refreshStats();
    statusBar()->showMessage(
        QStringLiteral("已从 %1 导入 %2 条任务（追加模式，原有任务保留）")
            .arg(QFileInfo(path).fileName())
            .arg(imported),
        8000);
}

void MainWindow::exportToFile()
{
    const QString suggestion = QDir(QStandardPaths::writableLocation(
                                        QStandardPaths::DocumentsLocation))
                                   .filePath(QStringLiteral("todo-tasks.json"));
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出任务"), suggestion,
        QStringLiteral("JSON 文件 (*.json)"));
    if (path.isEmpty())
        return;

    QString error;
    if (!JsonCodec::save(path, m_model->tasks(), &error)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), error);
        return;
    }
    statusBar()->showMessage(QStringLiteral("已导出 %1 条任务到 %2")
                                 .arg(m_model->rowCount())
                                 .arg(path),
                             8000);
}

void MainWindow::persistTask(const Task& task)
{
    if (!m_repository.isOpen())
        return;

    QString error;
    if (!m_repository.upsert(task, &error))
        statusBar()->showMessage(QStringLiteral("保存失败：%1").arg(error), 8000);
}

void MainWindow::refreshStats()
{
    // 统计在后台线程算；这里只把「当前快照」丢过去，绝不让工作线程碰 m_model。
    m_stats->request(m_model->tasks());
}

void MainWindow::updateCounts(int total, int done)
{
    m_statsLabel->setText(QStringLiteral("共 %1 · 已完成 %2（统计中…）").arg(total).arg(done));
    m_clearDoneAction->setEnabled(done > 0);
}

void MainWindow::updateActions()
{
    const bool hasSelection = selectedSourceRow() >= 0;
    m_editAction->setEnabled(hasSelection);
    m_deleteAction->setEnabled(hasSelection);
    m_toggleAction->setEnabled(hasSelection);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/header"), m_view->horizontalHeader()->saveState());
    QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    const QMimeData* mime = event->mimeData();
    if (!mime->hasUrls())
        return;

    const QList<QUrl> urls = mime->urls();
    if (!urls.isEmpty()
        && urls.first().toLocalFile().endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const QMimeData* mime = event->mimeData();
    if (!mime->hasUrls())
        return;

    const QList<QUrl> urls = mime->urls();
    if (urls.isEmpty())
        return;

    const QString path = urls.first().toLocalFile();
    if (!path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
        return;

    event->acceptProposedAction();
    importFromFile(path);
}
