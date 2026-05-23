#include "main_window.h"

#include <QAction>
#include <QCloseEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSizePolicy>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "mornox/workspace/workspace_context.h"
#include "widgets/inspector_panel.h"
#include "widgets/mission_bar.h"
#include "widgets/mission_map_panel.h"
#include "widgets/operation_ledger.h"
#include "widgets/task_surface.h"
#include "widgets/workspace_rail.h"

namespace mornox::ide {
namespace {

constexpr int kMaxTreeDepth = 4;

QString FileKindLabel(const VirtualFile& file) {
    const FileStat stat = file.Stat();
    return stat.kind == VirtualFileKind::Directory ? "dir" : "file";
}

}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowFlag(Qt::FramelessWindowHint);
    BuildMenus();
    BuildUi();
    resize(1440, 900);
    setWindowTitle("Mornox");

    refresh_timer_ = new QTimer(this);
    connect(refresh_timer_, &QTimer::timeout, this, [this] {
        RefreshUi();
    });
    refresh_timer_->start(250);
}

MainWindow::~MainWindow() {
    controller_.Shutdown();
}

void MainWindow::BuildMenus() {
    QMenu* file_menu = menuBar()->addMenu("&File");
    QAction* open_workspace = file_menu->addAction("Open Workspace...");
    open_workspace->setShortcut(QKeySequence::Open);
    addAction(open_workspace);
    connect(open_workspace, &QAction::triggered, this, [this] {
        OpenWorkspaceDialog();
    });
    QAction* save = file_menu->addAction("Save");
    save->setShortcut(QKeySequence::Save);
    addAction(save);
    connect(save, &QAction::triggered, this, [this] {
        SaveCurrentEditor();
    });
    file_menu->addSeparator();
    QAction* quit = file_menu->addAction("Quit");
    addAction(quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);
    menuBar()->hide();
}

void MainWindow::BuildUi() {
    auto* root = new QWidget(this);
    auto* root_layout = new QVBoxLayout(root);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(0);

    mission_bar_ = new MissionBar(root);
    root_layout->addWidget(mission_bar_);

    auto* workbench = new QWidget(root);
    auto* workbench_layout = new QHBoxLayout(workbench);
    workbench_layout->setContentsMargins(0, 0, 0, 0);
    workbench_layout->setSpacing(0);

    auto* left = new QWidget(workbench);
    left->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* left_layout = new QHBoxLayout(left);
    left_layout->setContentsMargins(0, 0, 0, 0);
    left_layout->setSpacing(0);
    workspace_rail_ = new WorkspaceRail(left);
    left_layout->addWidget(workspace_rail_);

    auto* left_content = new QWidget(left);
    left_content->setObjectName("LeftContent");
    auto* left_content_layout = new QVBoxLayout(left_content);
    left_content_layout->setContentsMargins(10, 10, 10, 10);
    left_content_layout->setSpacing(10);
    mission_map_ = new MissionMapPanel(left_content);
    left_content_layout->addWidget(mission_map_, 1);
    auto* files_title = new QLabel("Workspace Objects", left_content);
    files_title->setObjectName("PanelTitle");
    left_content_layout->addWidget(files_title);
    file_tree_ = new QTreeWidget(left_content);
    file_tree_->setObjectName("WorkspaceTree");
    file_tree_->setHeaderLabels({"Workspace", "Kind"});
    file_tree_->header()->setStretchLastSection(false);
    file_tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    file_tree_->setIndentation(16);
    connect(file_tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int column) {
        OpenTreeItem(item, column);
    });
    left_content_layout->addWidget(file_tree_);
    files_title->hide();
    file_tree_->hide();
    left_layout->addWidget(left_content, 1);
    workbench_layout->addWidget(left, 3);

    task_surface_ = new TaskSurface(workbench);
    task_surface_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    task_surface_->SetCloseRequestHandler([this](const QString& key, const QString& title, bool dirty) {
        return ConfirmCloseEditor(key, title, dirty);
    });
    workbench_layout->addWidget(task_surface_, 7);

    inspector_ = new InspectorPanel(workbench);
    inspector_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    inspector_->SetDiagnosticActivatedHandler([this](const Diagnostic& diagnostic) {
        OpenDiagnostic(diagnostic);
    });
    workbench_layout->addWidget(inspector_, 3);
    auto* vertical_workbench = new QSplitter(Qt::Vertical, root);
    vertical_workbench->setChildrenCollapsible(false);
    vertical_workbench->addWidget(workbench);

    ledger_ = new OperationLedger(vertical_workbench);
    ledger_->SetJobActivatedHandler([this](JobId job_id) {
        OpenJob(job_id);
    });
    vertical_workbench->addWidget(ledger_);
    vertical_workbench->setStretchFactor(0, 4);
    vertical_workbench->setStretchFactor(1, 1);
    root_layout->addWidget(vertical_workbench, 1);
    QTimer::singleShot(0, this, [vertical_workbench] {
        vertical_workbench->setSizes({570, 250});
    });

    setCentralWidget(root);
    statusBar()->showMessage("No workspace");
}

void MainWindow::OpenWorkspaceDialog() {
    const QString path = QFileDialog::getExistingDirectory(this, "Open Workspace");
    if (path.isEmpty()) {
        return;
    }

    QString error;
    if (!controller_.OpenWorkspace(path, &error)) {
        QMessageBox::warning(this, "Open Workspace", error.isEmpty() ? "Failed to open workspace." : error);
        return;
    }
    PopulateWorkspaceTree();
    RefreshUi();
}

bool MainWindow::SaveCurrentEditor() {
    const auto key = task_surface_->CurrentFileKey();
    if (!key) {
        return false;
    }
    return SaveEditorByKey(*key);
}

bool MainWindow::ConfirmCloseEditor(const QString& key, const QString& title, bool dirty) {
    if (!dirty) {
        return true;
    }

    const QMessageBox::StandardButton result = QMessageBox::question(
        this,
        "Unsaved Changes",
        QString("Save changes to %1?").arg(title),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (result == QMessageBox::Cancel) {
        return false;
    }
    if (result == QMessageBox::Discard) {
        return true;
    }
    return SaveEditorByKey(key);
}

bool MainWindow::SaveEditorByKey(const QString& key) {
    const auto text = task_surface_->TextForKey(key);
    if (!text) {
        return false;
    }

    QString error;
    if (!controller_.SaveFileText(FileFromKey(key), *text, &error)) {
        QMessageBox::warning(this, "Save File", error.isEmpty() ? "Failed to save file." : error);
        return false;
    }
    task_surface_->MarkSaved(key);
    RefreshUi();
    return true;
}

void MainWindow::PopulateWorkspaceTree() {
    file_tree_->clear();
    WorkspaceContext* context = controller_.Context();
    if (context == nullptr) {
        return;
    }

    const VirtualFile root = context->CurrentWorkspace().RootFile();
    auto* root_item = new QTreeWidgetItem(file_tree_, {ToQString(root.DisplayName()), FileKindLabel(root)});
    root_item->setData(0, Qt::UserRole, ToQString(root.ToUri().ToString()));
    for (const VirtualFile& child : root.ListChildren()) {
        AddFileNode(root_item, child, 0);
    }
    root_item->setExpanded(true);
}

void MainWindow::AddFileNode(QTreeWidgetItem* parent, const VirtualFile& file, int depth) {
    if (depth > kMaxTreeDepth) {
        return;
    }
    auto* item = new QTreeWidgetItem(parent, {ToQString(file.DisplayName()), FileKindLabel(file)});
    item->setData(0, Qt::UserRole, ToQString(file.ToUri().ToString()));
    if (file.Stat().kind == VirtualFileKind::Directory) {
        for (const VirtualFile& child : file.ListChildren()) {
            AddFileNode(item, child, depth + 1);
        }
    }
}

void MainWindow::OpenTreeItem(QTreeWidgetItem* item, int) {
    WorkspaceContext* context = controller_.Context();
    if (context == nullptr || item == nullptr) {
        return;
    }

    const QString uri = item->data(0, Qt::UserRole).toString();
    VirtualFile file(Uri::Parse(uri.toStdString()), &context->FileSystems());
    if (file.Stat().kind != VirtualFileKind::File) {
        return;
    }

    OpenFileInEditor(file);
    RefreshUi();
}

void MainWindow::OpenDiagnostic(const Diagnostic& diagnostic) {
    OpenFileInEditor(diagnostic.location.file, diagnostic.location);
}

void MainWindow::OpenFileInEditor(const VirtualFile& file, std::optional<SourceLocation> location) {
    const auto text = controller_.ReadFileText(file);
    if (!text) {
        return;
    }
    controller_.OpenFile(file);
    const QString key = ToQString(file.ToUri().ToString());
    task_surface_->OpenTextFile(key, ToQString(file.DisplayName()), *text);
    if (location) {
        task_surface_->RevealLocation(key, location->line, location->column);
    }
}

void MainWindow::OpenJob(JobId job_id) {
    WorkspaceContext* context = controller_.Context();
    if (context == nullptr) {
        return;
    }
    const std::optional<JobRecord> job = context->Jobs().Job(job_id);
    if (!job) {
        return;
    }
    task_surface_->OpenJobResult(*job);
}

VirtualFile MainWindow::FileFromKey(const QString& key) {
    WorkspaceContext* context = controller_.Context();
    if (context == nullptr) {
        return {};
    }
    return VirtualFile(Uri::Parse(key.toStdString()), &context->FileSystems());
}

void MainWindow::RefreshUi() {
    controller_.DrainMainTasks();
    const UiState* state = controller_.State();
    mission_map_->SetState(state);
    task_surface_->SetState(state);
    inspector_->SetState(state, controller_.Context());
    ledger_->SetState(state);
    UpdateStatus();
}

void MainWindow::UpdateStatus() {
    const UiState* state = controller_.State();
    if (state == nullptr || !state->workspace_open) {
        statusBar()->showMessage("No workspace");
        return;
    }

    const QString message = QString("%1    %2 problems    %3 jobs    Trusted")
                                .arg(ToQString(state->workspace.name))
                                .arg(state->problems.size())
                                .arg(state->jobs.size());
    statusBar()->showMessage(message);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (task_surface_ != nullptr &&
        !task_surface_->ConfirmAllClosable([this](const QString& key, const QString& title, bool dirty) {
            return ConfirmCloseEditor(key, title, dirty);
        })) {
        event->ignore();
        return;
    }
    event->accept();
}

}
