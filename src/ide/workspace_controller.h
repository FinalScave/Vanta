#pragma once

#include <memory>
#include <optional>

#include <QString>
#include <QVector>

#include "mornox/ide/ide_application.h"
#include "ui/ui_state_store.h"

namespace mornox {
class WorkspaceContext;
}

namespace mornox::ide {

class WorkspaceController {
public:
    bool OpenWorkspace(const QString& path, QString* error_message = nullptr);
    void Shutdown();
    void DrainMainTasks();

    bool HasWorkspace() const;
    WorkspaceContext* Context();
    const WorkspaceContext* Context() const;
    const UiState* State() const;

    QVector<VirtualFile> RootFiles() const;
    std::optional<QString> ReadFileText(const VirtualFile& file) const;
    EditorTab* OpenFile(const VirtualFile& file);
    bool SaveFileText(const VirtualFile& file, const QString& text, QString* error_message = nullptr);

private:
    IdeApplication app_;
    std::unique_ptr<UiStateStore> ui_state_;
};

QString ToQString(const std::string& value);
QString ToQString(const std::filesystem::path& path);

}
