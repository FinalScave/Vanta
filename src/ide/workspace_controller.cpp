#include "workspace_controller.h"

#include "mornox/workspace/document_service.h"
#include "mornox/workspace/workspace_context.h"

namespace mornox::ide {

bool WorkspaceController::OpenWorkspace(const QString& path, QString* error_message) {
    std::string error;
    const std::filesystem::path workspace_path(path.toStdWString());
    if (!app_.OpenWorkspace(workspace_path, {}, &error)) {
        if (error_message != nullptr) {
            *error_message = ToQString(error);
        }
        return false;
    }
    ui_state_ = std::make_unique<UiStateStore>(app_.Context(), "qt");
    return true;
}

void WorkspaceController::Shutdown() {
    ui_state_.reset();
    app_.Shutdown();
}

void WorkspaceController::DrainMainTasks() {
    app_.DrainMainTasks();
    if (ui_state_ != nullptr) {
        ui_state_->Refresh();
    }
}

bool WorkspaceController::HasWorkspace() const {
    return ui_state_ != nullptr && ui_state_->State().workspace_open;
}

WorkspaceContext* WorkspaceController::Context() {
    return HasWorkspace() ? &app_.Context() : nullptr;
}

const WorkspaceContext* WorkspaceController::Context() const {
    return HasWorkspace() ? &app_.Context() : nullptr;
}

const UiState* WorkspaceController::State() const {
    return ui_state_ == nullptr ? nullptr : &ui_state_->State();
}

QVector<VirtualFile> WorkspaceController::RootFiles() const {
    QVector<VirtualFile> files;
    const WorkspaceContext* context = Context();
    if (context == nullptr) {
        return files;
    }
    for (const VirtualFile& child : context->CurrentWorkspace().RootFile().ListChildren()) {
        files.push_back(child);
    }
    return files;
}

std::optional<QString> WorkspaceController::ReadFileText(const VirtualFile& file) const {
    if (const auto text = file.ReadText()) {
        return ToQString(*text);
    }
    return std::nullopt;
}

EditorTab* WorkspaceController::OpenFile(const VirtualFile& file) {
    return ui_state_ == nullptr ? nullptr : &ui_state_->OpenFile(file);
}

bool WorkspaceController::SaveFileText(const VirtualFile& file, const QString& text, QString* error_message) {
    WorkspaceContext* context = Context();
    if (context == nullptr) {
        if (error_message != nullptr) {
            *error_message = "No workspace is open.";
        }
        return false;
    }

    std::string error;
    TextDocument* document = context->Documents().Document(file);
    if (document == nullptr) {
        document = context->Documents().OpenDocument(file, &error);
    }
    if (document == nullptr) {
        if (error_message != nullptr) {
            *error_message = ToQString(error.empty() ? "Document is not readable." : error);
        }
        return false;
    }

    const std::string next_text = text.toStdString();
    if (document->text != next_text &&
        !context->Documents().SetText(file, next_text, document->version, &error)) {
        if (error_message != nullptr) {
            *error_message = ToQString(error.empty() ? "Failed to update document." : error);
        }
        return false;
    }
    if (!context->Documents().SaveDocument(file, &error)) {
        if (error_message != nullptr) {
            *error_message = ToQString(error.empty() ? "Failed to save document." : error);
        }
        return false;
    }
    if (ui_state_ != nullptr) {
        ui_state_->Refresh();
    }
    return true;
}

QString ToQString(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QString ToQString(const std::filesystem::path& path) {
    return QString::fromStdWString(path.wstring());
}

}
