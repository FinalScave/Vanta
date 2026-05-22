#include "vanta/workspace/change_set_service.h"

#include <filesystem>
#include <sstream>

namespace vanta {
namespace {

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    if (!text.empty() && text.back() == '\n') {
        lines.push_back("");
    }
    return lines;
}

std::string BuildUnifiedDiff(const WorkspaceEdit& edit) {
    std::string diff;
    for (const WorkspaceEditOperation& operation : edit.operations) {
        if (operation.kind == WorkspaceEditOperationKind::ReplaceFile || operation.kind == WorkspaceEditOperationKind::CreateFile || operation.kind == WorkspaceEditOperationKind::DeleteFile) {
            diff += CreateUnifiedDiff(operation.file, operation.original_text, operation.replacement_text);
        } else if (operation.kind == WorkspaceEditOperationKind::RenameFile) {
            diff += "rename from " + operation.file.ToUri().ToString() + '\n';
            diff += "rename to " + operation.target_file.ToUri().ToString() + '\n';
        }
    }
    return diff;
}

ChangeSetResult RemoveLocalFile(const VirtualFile& file) {
    const auto path = file.LocalPath();
    if (!path) {
        return {false, "Delete operation requires a local file"};
    }
    std::error_code error;
    if (!std::filesystem::remove(*path, error) || error) {
        return {false, error ? error.message() : "Could not delete file"};
    }
    return {true, "File deleted"};
}

ChangeSetResult RenameLocalFile(const VirtualFile& file, const VirtualFile& target_file) {
    const auto source = file.LocalPath();
    const auto target = target_file.LocalPath();
    if (!source || !target) {
        return {false, "Rename operation requires local files"};
    }
    if (std::filesystem::exists(*target)) {
        return {false, "Rename target already exists"};
    }
    std::error_code error;
    std::filesystem::create_directories(target->parent_path(), error);
    if (error) {
        return {false, error.message()};
    }
    std::filesystem::rename(*source, *target, error);
    if (error) {
        return {false, error.message()};
    }
    return {true, "File renamed"};
}

WorkspaceEditPreflight Conflict(WorkspaceEditOperationKind kind, VirtualFile file, std::string message, VirtualFile target_file = {}) {
    return {
        .ok = false,
        .conflicts = {{
            .operation_kind = kind,
            .file = std::move(file),
            .target_file = std::move(target_file),
            .message = std::move(message),
        }},
    };
}

void AppendConflicts(WorkspaceEditPreflight& target, WorkspaceEditPreflight source) {
    if (!source.ok) {
        target.ok = false;
        target.conflicts.insert(target.conflicts.end(), source.conflicts.begin(), source.conflicts.end());
    }
}

std::string PreflightMessage(const WorkspaceEditPreflight& preflight) {
    std::ostringstream stream;
    stream << "Change set has " << preflight.conflicts.size() << " conflict";
    if (preflight.conflicts.size() != 1) {
        stream << 's';
    }
    for (const WorkspaceEditConflict& conflict : preflight.conflicts) {
        stream << "\n- " << ToString(conflict.operation_kind) << ' ' << conflict.file.ToUri().ToString() << ": " << conflict.message;
    }
    return stream.str();
}

}

ChangeSet ChangeSetService::Create(
    std::string source,
    std::string title,
    WorkspaceEdit edit) {
    ChangeSet change_set;
    change_set.id = "change-" + std::to_string(next_change_set_id_++);
    change_set.source = std::move(source);
    change_set.title = std::move(title);
    change_set.edit = std::move(edit);
    change_set.unified_diff = BuildUnifiedDiff(change_set.edit);

    change_sets_[change_set.id] = change_set;
    return change_set;
}

ChangeSet ChangeSetService::CreateFileReplacement(
    VirtualFile file,
    std::string source,
    std::string title,
    std::string original_text,
    std::string replacement_text,
    std::uint64_t expected_document_version) {
    WorkspaceEdit edit;
    edit.operations.push_back({
        .kind = WorkspaceEditOperationKind::ReplaceFile,
        .file = std::move(file),
        .original_text = std::move(original_text),
        .replacement_text = std::move(replacement_text),
        .text_edits = {},
        .expected_document_version = expected_document_version,
    });
    return Create(std::move(source), std::move(title), std::move(edit));
}

ChangeSet ChangeSetService::CreateFileCreation(
    VirtualFile file,
    std::string source,
    std::string title,
    std::string text) {
    WorkspaceEdit edit;
    edit.operations.push_back({
        .kind = WorkspaceEditOperationKind::CreateFile,
        .file = std::move(file),
        .replacement_text = std::move(text),
    });
    return Create(std::move(source), std::move(title), std::move(edit));
}

ChangeSet ChangeSetService::CreateFileDeletion(
    VirtualFile file,
    std::string source,
    std::string title,
    std::string original_text) {
    WorkspaceEdit edit;
    edit.operations.push_back({
        .kind = WorkspaceEditOperationKind::DeleteFile,
        .file = std::move(file),
        .original_text = std::move(original_text),
    });
    return Create(std::move(source), std::move(title), std::move(edit));
}

ChangeSet ChangeSetService::CreateFileRename(
    VirtualFile file,
    VirtualFile target_file,
    std::string source,
    std::string title) {
    WorkspaceEdit edit;
    edit.operations.push_back({
        .kind = WorkspaceEditOperationKind::RenameFile,
        .file = std::move(file),
        .target_file = std::move(target_file),
    });
    return Create(std::move(source), std::move(title), std::move(edit));
}

std::optional<ChangeSet> ChangeSetService::Get(const std::string& id) const {
    auto it = change_sets_.find(id);
    return it == change_sets_.end() ? std::nullopt : std::optional<ChangeSet>(it->second);
}

std::vector<ChangeSet> ChangeSetService::List() const {
    std::vector<ChangeSet> result;
    for (const auto& [id, change_set] : change_sets_) {
        (void)id;
        result.push_back(change_set);
    }
    return result;
}

WorkspaceEditPreflight ChangeSetService::Preflight(
    Workspace& workspace,
    DocumentService& documents,
    const std::string& id) const {
    (void)workspace;
    const auto found = Get(id);
    if (!found) {
        return Conflict(WorkspaceEditOperationKind::ReplaceFile, {}, "Change set was not found");
    }

    WorkspaceEditPreflight result;
    for (const WorkspaceEditOperation& operation : found->edit.operations) {
        AppendConflicts(result, PreflightOperation(documents, operation));
    }
    return result;
}

ChangeSetResult ChangeSetService::Approve(const std::string& id) {
    ChangeSet* found = MutableChangeSet(id);
    if (found == nullptr) {
        return {false, "Change set was not found"};
    }
    found->status = ChangeSetStatus::Approved;
    return {true, "Change set approved"};
}

ChangeSetResult ChangeSetService::Reject(const std::string& id) {
    ChangeSet* found = MutableChangeSet(id);
    if (found == nullptr) {
        return {false, "Change set was not found"};
    }
    found->status = ChangeSetStatus::Rejected;
    return {true, "Change set rejected"};
}

ChangeSetResult ChangeSetService::ApplyApproved(
    Workspace& workspace,
    DocumentService& documents,
    const std::string& id,
    ChangeSetApplyOptions options) {
    (void)workspace;
    ChangeSet* found = MutableChangeSet(id);
    if (found == nullptr) {
        return {false, "Change set was not found"};
    }
    if (found->status != ChangeSetStatus::Approved) {
        return {false, "Change set has not been approved"};
    }

    if (options.preflight) {
        const WorkspaceEditPreflight preflight_result = Preflight(workspace, documents, id);
        if (!preflight_result.ok) {
            return {false, PreflightMessage(preflight_result)};
        }
    }

    WorkspaceEdit inverse_edit = CreateInverseEdit(documents, found->edit);
    for (const WorkspaceEditOperation& operation : found->edit.operations) {
        ChangeSetResult result = ApplyOperation(documents, operation, options);
        if (!result.ok) {
            return result;
        }
    }

    found->inverse_edit = std::move(inverse_edit);
    found->undo_token = found->id + ".undo";
    found->status = ChangeSetStatus::Applied;
    return {true, "Change set applied"};
}

ChangeSetResult ChangeSetService::UndoApplied(
    Workspace& workspace,
    DocumentService& documents,
    const std::string& id,
    ChangeSetApplyOptions options) {
    (void)workspace;
    ChangeSet* found = MutableChangeSet(id);
    if (found == nullptr) {
        return {false, "Change set was not found"};
    }
    if (found->status != ChangeSetStatus::Applied) {
        return {false, "Change set has not been applied"};
    }
    if (found->inverse_edit.operations.empty()) {
        return {false, "Change set has no undo edit"};
    }

    if (options.preflight) {
        WorkspaceEditPreflight result;
        for (const WorkspaceEditOperation& operation : found->inverse_edit.operations) {
            AppendConflicts(result, PreflightOperation(documents, operation));
        }
        if (!result.ok) {
            return {false, PreflightMessage(result)};
        }
    }

    for (const WorkspaceEditOperation& operation : found->inverse_edit.operations) {
        ChangeSetResult result = ApplyOperation(documents, operation, options);
        if (!result.ok) {
            return result;
        }
    }

    found->status = ChangeSetStatus::Undone;
    return {true, "Change set undone"};
}

ChangeSet* ChangeSetService::MutableChangeSet(const std::string& id) {
    auto it = change_sets_.find(id);
    return it == change_sets_.end() ? nullptr : &it->second;
}

WorkspaceEdit ChangeSetService::CreateInverseEdit(DocumentService& documents, const WorkspaceEdit& edit) const {
    WorkspaceEdit inverse;
    for (auto it = edit.operations.rbegin(); it != edit.operations.rend(); ++it) {
        const WorkspaceEditOperation& operation = *it;
        if (operation.kind == WorkspaceEditOperationKind::CreateFile) {
            inverse.operations.push_back({
                .kind = WorkspaceEditOperationKind::DeleteFile,
                .file = operation.file,
                .original_text = operation.replacement_text,
            });
            continue;
        }
        if (operation.kind == WorkspaceEditOperationKind::DeleteFile) {
            inverse.operations.push_back({
                .kind = WorkspaceEditOperationKind::CreateFile,
                .file = operation.file,
                .replacement_text = operation.original_text,
            });
            continue;
        }
        if (operation.kind == WorkspaceEditOperationKind::RenameFile) {
            inverse.operations.push_back({
                .kind = WorkspaceEditOperationKind::RenameFile,
                .file = operation.target_file,
                .target_file = operation.file,
            });
            continue;
        }
        if (operation.kind == WorkspaceEditOperationKind::EditText) {
            auto snapshot = documents.ReadSnapshot(operation.file);
            std::string before = snapshot ? snapshot->text : "";
            std::string after = before;
            for (auto edit_it = operation.text_edits.rbegin(); edit_it != operation.text_edits.rend(); ++edit_it) {
                after = ApplyTextEdit(after, *edit_it);
            }
            inverse.operations.push_back({
                .kind = WorkspaceEditOperationKind::ReplaceFile,
                .file = operation.file,
                .original_text = std::move(after),
                .replacement_text = std::move(before),
            });
            continue;
        }
        inverse.operations.push_back({
            .kind = WorkspaceEditOperationKind::ReplaceFile,
            .file = operation.file,
            .original_text = operation.replacement_text,
            .replacement_text = operation.original_text,
        });
    }
    return inverse;
}

WorkspaceEditPreflight ChangeSetService::PreflightOperation(
    DocumentService& documents,
    const WorkspaceEditOperation& operation) const {
    if (operation.kind == WorkspaceEditOperationKind::CreateFile) {
        if (operation.file.Exists()) {
            return Conflict(operation.kind, operation.file, "Target file already exists");
        }
        return {};
    }

    if (operation.kind == WorkspaceEditOperationKind::DeleteFile) {
        if (TextDocument* document = documents.Document(operation.file)) {
            if (document->text != operation.original_text) {
                return Conflict(operation.kind, operation.file, "Document text changed after the change set was created");
            }
            return {};
        }
        auto text = operation.file.ReadText();
        if (!text) {
            return Conflict(operation.kind, operation.file, "Target file is not readable");
        }
        if (*text != operation.original_text) {
            return Conflict(operation.kind, operation.file, "Target file changed after the change set was created");
        }
        return {};
    }

    if (operation.kind == WorkspaceEditOperationKind::RenameFile) {
        if (!operation.file.Exists()) {
            return Conflict(operation.kind, operation.file, "Rename source does not exist", operation.target_file);
        }
        if (operation.target_file.Exists()) {
            return Conflict(operation.kind, operation.file, "Rename target already exists", operation.target_file);
        }
        return {};
    }

    if (operation.kind == WorkspaceEditOperationKind::EditText) {
        const TextDocument* document = documents.Document(operation.file);
        if (document == nullptr) {
            auto text = operation.file.ReadText();
            if (!text) {
                return Conflict(operation.kind, operation.file, "Document is not readable");
            }
            return {};
        }
        if (operation.expected_document_version != 0 && document->version != operation.expected_document_version) {
            return Conflict(operation.kind, operation.file, "Document version changed after the change set was created");
        }
        return {};
    }

    if (TextDocument* document = documents.Document(operation.file)) {
        if (operation.expected_document_version != 0 && document->version != operation.expected_document_version) {
            return Conflict(operation.kind, operation.file, "Document version changed after the change set was created");
        }
        if (document->text != operation.original_text) {
            return Conflict(operation.kind, operation.file, "Document text changed after the change set was created");
        }
    } else {
        auto text = operation.file.ReadText();
        if (!text) {
            return Conflict(operation.kind, operation.file, "Target file is not readable");
        }
        if (*text != operation.original_text) {
            return Conflict(operation.kind, operation.file, "Target file changed after the change set was created");
        }
    }
    return {};
}

ChangeSetResult ChangeSetService::ApplyOperation(
    DocumentService& documents,
    const WorkspaceEditOperation& operation,
    ChangeSetApplyOptions options) const {
    if (operation.kind == WorkspaceEditOperationKind::CreateFile) {
        if (operation.file.Exists()) {
            return {false, "Target file already exists"};
        }
        std::string error;
        if (!operation.file.WriteText(operation.replacement_text, &error)) {
            return {false, error};
        }
        return {true, "File created"};
    }

    if (operation.kind == WorkspaceEditOperationKind::DeleteFile) {
        if (TextDocument* document = documents.Document(operation.file)) {
            if (document->text != operation.original_text) {
                return {false, "Document text changed after the change set was created"};
            }
        } else {
            auto text = operation.file.ReadText();
            if (!text || *text != operation.original_text) {
                return {false, "Target file changed after the change set was created"};
            }
        }
        ChangeSetResult result = RemoveLocalFile(operation.file);
        if (result.ok) {
            documents.CloseDocument(operation.file);
        }
        return result;
    }

    if (operation.kind == WorkspaceEditOperationKind::RenameFile) {
        if (!operation.file.Exists()) {
            return {false, "Rename source does not exist"};
        }
        if (operation.target_file.Exists()) {
            return {false, "Rename target already exists"};
        }
        ChangeSetResult result = RenameLocalFile(operation.file, operation.target_file);
        if (result.ok) {
            documents.CloseDocument(operation.file);
        }
        return result;
    }

    if (operation.kind == WorkspaceEditOperationKind::EditText) {
        TextDocument* document = documents.Document(operation.file);
        if (document == nullptr) {
            std::string error;
            document = documents.OpenDocument(operation.file, &error);
            if (document == nullptr) {
                return {false, error};
            }
        }
        if (operation.expected_document_version != 0 && document->version != operation.expected_document_version) {
            return {false, "Document version changed after the change set was created"};
        }
        std::string error;
        if (!documents.ApplyEdits(operation.file, operation.text_edits, document->version, &error)) {
            return {false, error};
        }
        if (options.save_after_apply && !documents.SaveDocument(operation.file, &error)) {
            return {false, error};
        }
        return {true, "Text edits applied"};
    }

    if (TextDocument* document = documents.Document(operation.file)) {
        if (operation.expected_document_version != 0 && document->version != operation.expected_document_version) {
            return {false, "Document version changed after the change set was created"};
        }
        if (document->text != operation.original_text) {
            return {false, "Document text changed after the change set was created"};
        }
        std::string error;
        if (!documents.SetText(operation.file, operation.replacement_text, document->version, &error)) {
            return {false, error};
        }
        if (options.save_after_apply && !documents.SaveDocument(operation.file, &error)) {
            return {false, error};
        }
    } else {
        auto text = operation.file.ReadText();
        if (!text || *text != operation.original_text) {
            return {false, "Target file changed after the change set was created"};
        }
        std::string error;
        if (!operation.file.WriteText(operation.replacement_text, &error)) {
            return {false, error};
        }
    }
    return {true, "File replacement applied"};
}

std::string ToString(WorkspaceEditOperationKind kind) {
    switch (kind) {
    case WorkspaceEditOperationKind::ReplaceFile:
        return "replaceFile";
    case WorkspaceEditOperationKind::EditText:
        return "editText";
    case WorkspaceEditOperationKind::CreateFile:
        return "createFile";
    case WorkspaceEditOperationKind::DeleteFile:
        return "deleteFile";
    case WorkspaceEditOperationKind::RenameFile:
        return "renameFile";
    }
    return "replaceFile";
}

std::string ToString(ChangeSetStatus status) {
    switch (status) {
    case ChangeSetStatus::Pending:
        return "pending";
    case ChangeSetStatus::Approved:
        return "approved";
    case ChangeSetStatus::Rejected:
        return "rejected";
    case ChangeSetStatus::Applied:
        return "applied";
    case ChangeSetStatus::Undone:
        return "undone";
    }
    return "pending";
}

std::string CreateUnifiedDiff(
    const VirtualFile& file,
    const std::string& original_text,
    const std::string& replacement_text) {
    std::ostringstream diff;
    diff << "--- a/" << file.ToUri().ToString() << '\n';
    diff << "+++ b/" << file.ToUri().ToString() << '\n';
    diff << "@@ -1,";
    diff << SplitLines(original_text).size();
    diff << " +1,";
    diff << SplitLines(replacement_text).size();
    diff << " @@\n";

    for (const std::string& line : SplitLines(original_text)) {
        diff << '-' << line << '\n';
    }
    for (const std::string& line : SplitLines(replacement_text)) {
        diff << '+' << line << '\n';
    }
    return diff.str();
}

}
