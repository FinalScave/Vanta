#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vanta/workspace/document_service.h"
#include "vanta/workspace/workspace.h"

namespace vanta {

enum class WorkspaceEditOperationKind {
    ReplaceFile,
    EditText,
    CreateFile,
    DeleteFile,
    RenameFile,
};

struct WorkspaceEditOperation {
    WorkspaceEditOperationKind kind = WorkspaceEditOperationKind::ReplaceFile;
    VirtualFile file;
    VirtualFile targetFile;
    std::string originalText;
    std::string replacementText;
    std::vector<TextEdit> textEdits;
    std::uint64_t expectedDocumentVersion = 0;
};

struct WorkspaceEdit {
    std::vector<WorkspaceEditOperation> operations;
};

struct WorkspaceEditConflict {
    WorkspaceEditOperationKind operationKind = WorkspaceEditOperationKind::ReplaceFile;
    VirtualFile file;
    VirtualFile targetFile;
    std::string message;
};

struct WorkspaceEditPreflight {
    bool ok = true;
    std::vector<WorkspaceEditConflict> conflicts;
};

enum class ChangeSetStatus {
    Pending,
    Approved,
    Rejected,
    Applied,
};

struct ChangeSet {
    std::string id;
    std::string source;
    std::string title;
    WorkspaceEdit edit;
    std::string unifiedDiff;
    ChangeSetStatus status = ChangeSetStatus::Pending;
};

struct ChangeSetApplyOptions {
    bool saveAfterApply = false;
    bool preflight = true;
};

struct ChangeSetResult {
    bool ok = false;
    std::string message;
};

class ChangeSetService {
public:
    ChangeSet create(
        std::string source,
        std::string title,
        WorkspaceEdit edit);

    ChangeSet createFileReplacement(
        VirtualFile file,
        std::string source,
        std::string title,
        std::string originalText,
        std::string replacementText,
        std::uint64_t expectedDocumentVersion = 0);
    ChangeSet createFileCreation(
        VirtualFile file,
        std::string source,
        std::string title,
        std::string text);
    ChangeSet createFileDeletion(
        VirtualFile file,
        std::string source,
        std::string title,
        std::string originalText);
    ChangeSet createFileRename(
        VirtualFile file,
        VirtualFile targetFile,
        std::string source,
        std::string title);

    std::optional<ChangeSet> changeSet(const std::string& id) const;
    std::vector<ChangeSet> changeSets() const;
    WorkspaceEditPreflight preflight(
        Workspace& workspace,
        DocumentService& documents,
        const std::string& id) const;
    ChangeSetResult approve(const std::string& id);
    ChangeSetResult reject(const std::string& id);
    ChangeSetResult applyApproved(
        Workspace& workspace,
        DocumentService& documents,
        const std::string& id,
        ChangeSetApplyOptions options = {});

private:
    ChangeSet* mutableChangeSet(const std::string& id);
    WorkspaceEditPreflight preflightOperation(DocumentService& documents, const WorkspaceEditOperation& operation) const;
    ChangeSetResult applyOperation(DocumentService& documents, const WorkspaceEditOperation& operation, ChangeSetApplyOptions options) const;

    std::map<std::string, ChangeSet> changeSets_;
    std::uint64_t nextChangeSetId_ = 1;
};

std::string toString(WorkspaceEditOperationKind kind);
std::string toString(ChangeSetStatus status);
std::string createUnifiedDiff(
    const VirtualFile& file,
    const std::string& originalText,
    const std::string& replacementText);

}
