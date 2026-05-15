#include "vanta/workspace/change_set_service.h"

#include <filesystem>
#include <sstream>

namespace vanta {
namespace {

std::vector<std::string> splitLines(const std::string& text) {
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

std::string buildUnifiedDiff(const WorkspaceEdit& edit) {
    std::string diff;
    for (const WorkspaceEditOperation& operation : edit.operations) {
        if (operation.kind == WorkspaceEditOperationKind::ReplaceFile || operation.kind == WorkspaceEditOperationKind::CreateFile || operation.kind == WorkspaceEditOperationKind::DeleteFile) {
            diff += createUnifiedDiff(operation.file, operation.originalText, operation.replacementText);
        } else if (operation.kind == WorkspaceEditOperationKind::RenameFile) {
            diff += "rename from " + operation.file.toUri().string() + '\n';
            diff += "rename to " + operation.targetFile.toUri().string() + '\n';
        }
    }
    return diff;
}

ChangeSetResult removeLocalFile(const VirtualFile& file) {
    const auto path = file.localPath();
    if (!path) {
        return {false, "Delete operation requires a local file"};
    }
    std::error_code error;
    if (!std::filesystem::remove(*path, error) || error) {
        return {false, error ? error.message() : "Could not delete file"};
    }
    return {true, "File deleted"};
}

ChangeSetResult renameLocalFile(const VirtualFile& file, const VirtualFile& targetFile) {
    const auto source = file.localPath();
    const auto target = targetFile.localPath();
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

WorkspaceEditPreflight conflict(WorkspaceEditOperationKind kind, VirtualFile file, std::string message, VirtualFile targetFile = {}) {
    return {
        .ok = false,
        .conflicts = {{
            .operationKind = kind,
            .file = std::move(file),
            .targetFile = std::move(targetFile),
            .message = std::move(message),
        }},
    };
}

void appendConflicts(WorkspaceEditPreflight& target, WorkspaceEditPreflight source) {
    if (!source.ok) {
        target.ok = false;
        target.conflicts.insert(target.conflicts.end(), source.conflicts.begin(), source.conflicts.end());
    }
}

std::string preflightMessage(const WorkspaceEditPreflight& preflight) {
    std::ostringstream stream;
    stream << "Change set has " << preflight.conflicts.size() << " conflict";
    if (preflight.conflicts.size() != 1) {
        stream << 's';
    }
    for (const WorkspaceEditConflict& conflict : preflight.conflicts) {
        stream << "\n- " << toString(conflict.operationKind) << ' ' << conflict.file.toUri().string() << ": " << conflict.message;
    }
    return stream.str();
}

}

ChangeSet ChangeSetService::create(
    std::string source,
    std::string title,
    WorkspaceEdit edit) {
    ChangeSet changeSet;
    changeSet.id = "change-" + std::to_string(nextChangeSetId_++);
    changeSet.source = std::move(source);
    changeSet.title = std::move(title);
    changeSet.edit = std::move(edit);
    changeSet.unifiedDiff = buildUnifiedDiff(changeSet.edit);

    changeSets_[changeSet.id] = changeSet;
    return changeSet;
}

ChangeSet ChangeSetService::createFileReplacement(
    VirtualFile file,
    std::string source,
    std::string title,
    std::string originalText,
    std::string replacementText,
    std::uint64_t expectedDocumentVersion) {
    WorkspaceEdit edit;
    edit.operations.push_back({
        .kind = WorkspaceEditOperationKind::ReplaceFile,
        .file = std::move(file),
        .originalText = std::move(originalText),
        .replacementText = std::move(replacementText),
        .textEdits = {},
        .expectedDocumentVersion = expectedDocumentVersion,
    });
    return create(std::move(source), std::move(title), std::move(edit));
}

ChangeSet ChangeSetService::createFileCreation(
    VirtualFile file,
    std::string source,
    std::string title,
    std::string text) {
    WorkspaceEdit edit;
    edit.operations.push_back({
        .kind = WorkspaceEditOperationKind::CreateFile,
        .file = std::move(file),
        .replacementText = std::move(text),
    });
    return create(std::move(source), std::move(title), std::move(edit));
}

ChangeSet ChangeSetService::createFileDeletion(
    VirtualFile file,
    std::string source,
    std::string title,
    std::string originalText) {
    WorkspaceEdit edit;
    edit.operations.push_back({
        .kind = WorkspaceEditOperationKind::DeleteFile,
        .file = std::move(file),
        .originalText = std::move(originalText),
    });
    return create(std::move(source), std::move(title), std::move(edit));
}

ChangeSet ChangeSetService::createFileRename(
    VirtualFile file,
    VirtualFile targetFile,
    std::string source,
    std::string title) {
    WorkspaceEdit edit;
    edit.operations.push_back({
        .kind = WorkspaceEditOperationKind::RenameFile,
        .file = std::move(file),
        .targetFile = std::move(targetFile),
    });
    return create(std::move(source), std::move(title), std::move(edit));
}

std::optional<ChangeSet> ChangeSetService::changeSet(const std::string& id) const {
    auto it = changeSets_.find(id);
    return it == changeSets_.end() ? std::nullopt : std::optional<ChangeSet>(it->second);
}

std::vector<ChangeSet> ChangeSetService::changeSets() const {
    std::vector<ChangeSet> result;
    for (const auto& [id, changeSet] : changeSets_) {
        (void)id;
        result.push_back(changeSet);
    }
    return result;
}

WorkspaceEditPreflight ChangeSetService::preflight(
    Workspace& workspace,
    DocumentService& documents,
    const std::string& id) const {
    (void)workspace;
    const auto found = changeSet(id);
    if (!found) {
        return conflict(WorkspaceEditOperationKind::ReplaceFile, {}, "Change set was not found");
    }

    WorkspaceEditPreflight result;
    for (const WorkspaceEditOperation& operation : found->edit.operations) {
        appendConflicts(result, preflightOperation(documents, operation));
    }
    return result;
}

ChangeSetResult ChangeSetService::approve(const std::string& id) {
    ChangeSet* found = mutableChangeSet(id);
    if (found == nullptr) {
        return {false, "Change set was not found"};
    }
    found->status = ChangeSetStatus::Approved;
    return {true, "Change set approved"};
}

ChangeSetResult ChangeSetService::reject(const std::string& id) {
    ChangeSet* found = mutableChangeSet(id);
    if (found == nullptr) {
        return {false, "Change set was not found"};
    }
    found->status = ChangeSetStatus::Rejected;
    return {true, "Change set rejected"};
}

ChangeSetResult ChangeSetService::applyApproved(
    Workspace& workspace,
    DocumentService& documents,
    const std::string& id,
    ChangeSetApplyOptions options) {
    (void)workspace;
    ChangeSet* found = mutableChangeSet(id);
    if (found == nullptr) {
        return {false, "Change set was not found"};
    }
    if (found->status != ChangeSetStatus::Approved) {
        return {false, "Change set has not been approved"};
    }

    if (options.preflight) {
        const WorkspaceEditPreflight preflightResult = preflight(workspace, documents, id);
        if (!preflightResult.ok) {
            return {false, preflightMessage(preflightResult)};
        }
    }

    for (const WorkspaceEditOperation& operation : found->edit.operations) {
        ChangeSetResult result = applyOperation(documents, operation, options);
        if (!result.ok) {
            return result;
        }
    }

    found->status = ChangeSetStatus::Applied;
    return {true, "Change set applied"};
}

ChangeSet* ChangeSetService::mutableChangeSet(const std::string& id) {
    auto it = changeSets_.find(id);
    return it == changeSets_.end() ? nullptr : &it->second;
}

WorkspaceEditPreflight ChangeSetService::preflightOperation(
    DocumentService& documents,
    const WorkspaceEditOperation& operation) const {
    if (operation.kind == WorkspaceEditOperationKind::CreateFile) {
        if (operation.file.exists()) {
            return conflict(operation.kind, operation.file, "Target file already exists");
        }
        return {};
    }

    if (operation.kind == WorkspaceEditOperationKind::DeleteFile) {
        if (TextDocument* document = documents.document(operation.file)) {
            if (document->text != operation.originalText) {
                return conflict(operation.kind, operation.file, "Document text changed after the change set was created");
            }
            return {};
        }
        auto text = operation.file.readText();
        if (!text) {
            return conflict(operation.kind, operation.file, "Target file is not readable");
        }
        if (*text != operation.originalText) {
            return conflict(operation.kind, operation.file, "Target file changed after the change set was created");
        }
        return {};
    }

    if (operation.kind == WorkspaceEditOperationKind::RenameFile) {
        if (!operation.file.exists()) {
            return conflict(operation.kind, operation.file, "Rename source does not exist", operation.targetFile);
        }
        if (operation.targetFile.exists()) {
            return conflict(operation.kind, operation.file, "Rename target already exists", operation.targetFile);
        }
        return {};
    }

    if (operation.kind == WorkspaceEditOperationKind::EditText) {
        const TextDocument* document = documents.document(operation.file);
        if (document == nullptr) {
            auto text = operation.file.readText();
            if (!text) {
                return conflict(operation.kind, operation.file, "Document is not readable");
            }
            return {};
        }
        if (operation.expectedDocumentVersion != 0 && document->version != operation.expectedDocumentVersion) {
            return conflict(operation.kind, operation.file, "Document version changed after the change set was created");
        }
        return {};
    }

    if (TextDocument* document = documents.document(operation.file)) {
        if (operation.expectedDocumentVersion != 0 && document->version != operation.expectedDocumentVersion) {
            return conflict(operation.kind, operation.file, "Document version changed after the change set was created");
        }
        if (document->text != operation.originalText) {
            return conflict(operation.kind, operation.file, "Document text changed after the change set was created");
        }
    } else {
        auto text = operation.file.readText();
        if (!text) {
            return conflict(operation.kind, operation.file, "Target file is not readable");
        }
        if (*text != operation.originalText) {
            return conflict(operation.kind, operation.file, "Target file changed after the change set was created");
        }
    }
    return {};
}

ChangeSetResult ChangeSetService::applyOperation(
    DocumentService& documents,
    const WorkspaceEditOperation& operation,
    ChangeSetApplyOptions options) const {
    if (operation.kind == WorkspaceEditOperationKind::CreateFile) {
        if (operation.file.exists()) {
            return {false, "Target file already exists"};
        }
        std::string error;
        if (!operation.file.writeText(operation.replacementText, &error)) {
            return {false, error};
        }
        return {true, "File created"};
    }

    if (operation.kind == WorkspaceEditOperationKind::DeleteFile) {
        if (TextDocument* document = documents.document(operation.file)) {
            if (document->text != operation.originalText) {
                return {false, "Document text changed after the change set was created"};
            }
        } else {
            auto text = operation.file.readText();
            if (!text || *text != operation.originalText) {
                return {false, "Target file changed after the change set was created"};
            }
        }
        ChangeSetResult result = removeLocalFile(operation.file);
        if (result.ok) {
            documents.closeDocument(operation.file);
        }
        return result;
    }

    if (operation.kind == WorkspaceEditOperationKind::RenameFile) {
        if (!operation.file.exists()) {
            return {false, "Rename source does not exist"};
        }
        if (operation.targetFile.exists()) {
            return {false, "Rename target already exists"};
        }
        ChangeSetResult result = renameLocalFile(operation.file, operation.targetFile);
        if (result.ok) {
            documents.closeDocument(operation.file);
        }
        return result;
    }

    if (operation.kind == WorkspaceEditOperationKind::EditText) {
        TextDocument* document = documents.document(operation.file);
        if (document == nullptr) {
            std::string error;
            document = documents.openDocument(operation.file, &error);
            if (document == nullptr) {
                return {false, error};
            }
        }
        if (operation.expectedDocumentVersion != 0 && document->version != operation.expectedDocumentVersion) {
            return {false, "Document version changed after the change set was created"};
        }
        std::string error;
        if (!documents.applyEdits(operation.file, operation.textEdits, document->version, &error)) {
            return {false, error};
        }
        if (options.saveAfterApply && !documents.saveDocument(operation.file, &error)) {
            return {false, error};
        }
        return {true, "Text edits applied"};
    }

    if (TextDocument* document = documents.document(operation.file)) {
        if (operation.expectedDocumentVersion != 0 && document->version != operation.expectedDocumentVersion) {
            return {false, "Document version changed after the change set was created"};
        }
        if (document->text != operation.originalText) {
            return {false, "Document text changed after the change set was created"};
        }
        std::string error;
        if (!documents.setText(operation.file, operation.replacementText, document->version, &error)) {
            return {false, error};
        }
        if (options.saveAfterApply && !documents.saveDocument(operation.file, &error)) {
            return {false, error};
        }
    } else {
        auto text = operation.file.readText();
        if (!text || *text != operation.originalText) {
            return {false, "Target file changed after the change set was created"};
        }
        std::string error;
        if (!operation.file.writeText(operation.replacementText, &error)) {
            return {false, error};
        }
    }
    return {true, "File replacement applied"};
}

std::string toString(WorkspaceEditOperationKind kind) {
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

std::string toString(ChangeSetStatus status) {
    switch (status) {
    case ChangeSetStatus::Pending:
        return "pending";
    case ChangeSetStatus::Approved:
        return "approved";
    case ChangeSetStatus::Rejected:
        return "rejected";
    case ChangeSetStatus::Applied:
        return "applied";
    }
    return "pending";
}

std::string createUnifiedDiff(
    const VirtualFile& file,
    const std::string& originalText,
    const std::string& replacementText) {
    std::ostringstream diff;
    diff << "--- a/" << file.toUri().string() << '\n';
    diff << "+++ b/" << file.toUri().string() << '\n';
    diff << "@@ -1,";
    diff << splitLines(originalText).size();
    diff << " +1,";
    diff << splitLines(replacementText).size();
    diff << " @@\n";

    for (const std::string& line : splitLines(originalText)) {
        diff << '-' << line << '\n';
    }
    for (const std::string& line : splitLines(replacementText)) {
        diff << '+' << line << '\n';
    }
    return diff.str();
}

}
