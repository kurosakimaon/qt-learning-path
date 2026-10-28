// ============================================================================
//  todo_desktop · 主窗口
// ----------------------------------------------------------------------------
//  这个窗口把前十一阶段的东西拧在一起：
//    - QMainWindow / QToolBar / 状态栏 / QSettings 记住窗口大小   （第贰 / 捌阶段）
//    - QTableView + 自写模型 + 代理 + 委托 + 选择模型             （第陆阶段）
//    - 拖入 .json 文件即导入                                     （第叁阶段拖放）
//    - 统计在后台线程算、结果走信号回来                           （第柒阶段）
//    - 每次改动立刻落库（SQLite）                                 （第捌阶段）
//
//  数据流向只有一条：用户操作 → 模型改数据 → 发信号 → 视图重画 → 仓储落库。
//  没有「先改界面再想办法同步数据」这种路，这是模型/视图阶段讲的第一条规矩。
// ============================================================================
#pragma once

#include "core/task.h"
#include "core/taskrepository.h"

#include <QMainWindow>
#include <QString>

class QAction;
class QCloseEvent;
class QComboBox;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QLineEdit;
class QTableView;
class StatsService;
class TaskFilterProxy;
class TaskModel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString& databasePath, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void buildUi();
    void buildActions();
    void loadFromDatabase();
    void seedSampleTasks();

    void addTask();
    void editSelected();
    void deleteSelected();
    void toggleSelected();
    void removeCompleted();
    void importFromFile(const QString& path);
    void exportToFile();

    void persistTask(const Task& task);
    void refreshStats();
    void updateCounts(int total, int done);
    void updateActions();

    int selectedSourceRow() const;

    TaskRepository m_repository;

    TaskModel* m_model = nullptr;
    TaskFilterProxy* m_proxy = nullptr;
    StatsService* m_stats = nullptr;

    QTableView* m_view = nullptr;
    QLineEdit* m_search = nullptr;
    QComboBox* m_filter = nullptr;
    QLabel* m_statsLabel = nullptr;

    QAction* m_editAction = nullptr;
    QAction* m_deleteAction = nullptr;
    QAction* m_toggleAction = nullptr;
    QAction* m_clearDoneAction = nullptr;
};
