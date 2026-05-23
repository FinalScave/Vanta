#pragma once

#include <QMainWindow>

#include "workspace_controller.h"

class QTreeWidget;
class QTreeWidgetItem;
class QTimer;

namespace mornox::ide {

class InspectorPanel;
class MissionBar;
class MissionMapPanel;
class OperationLedger;
class TaskSurface;
class WorkspaceRail;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void BuildUi();
    void BuildMenus();
    void OpenWorkspaceDialog();
    bool SaveCurrentEditor();
    bool ConfirmCloseEditor(const QString& key, const QString& title, bool dirty);
    bool SaveEditorByKey(const QString& key);
    void PopulateWorkspaceTree();
    void AddFileNode(QTreeWidgetItem* parent, const VirtualFile& file, int depth);
    void OpenTreeItem(QTreeWidgetItem* item, int column);
    void OpenDiagnostic(const Diagnostic& diagnostic);
    void OpenFileInEditor(const VirtualFile& file, std::optional<SourceLocation> location = std::nullopt);
    void OpenJob(JobId job_id);
    void RefreshUi();
    void UpdateStatus();
    VirtualFile FileFromKey(const QString& key);

    void closeEvent(QCloseEvent* event) override;

    WorkspaceController controller_;
    MissionBar* mission_bar_ = nullptr;
    WorkspaceRail* workspace_rail_ = nullptr;
    MissionMapPanel* mission_map_ = nullptr;
    QTreeWidget* file_tree_ = nullptr;
    TaskSurface* task_surface_ = nullptr;
    InspectorPanel* inspector_ = nullptr;
    OperationLedger* ledger_ = nullptr;
    QTimer* refresh_timer_ = nullptr;
};

}
