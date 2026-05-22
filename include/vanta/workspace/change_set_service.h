#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vanta/workspace/document_service.h"
#include "vanta/workspace/workspace.h"
#include "vanta/workspace/workspace_edit.h"

namespace vanta {

enum class ChangeSetStatus {
    Pending,
    Approved,
    Rejected,
    Applied,
    Undone,
};

struct WorkspaceEditConflict {
    WorkspaceEditOperationKind operation_kind = WorkspaceEditOperationKind::ReplaceFile;
    VirtualFile file;
    VirtualFile target_file;
    std::string message;
};

struct WorkspaceEditPreflight {
    bool ok = true;
    std::vector<WorkspaceEditConflict> conflicts;
};

struct ChangeSet {
    std::string id;
    std::string source;
    std::string title;
    WorkspaceEdit edit;
    WorkspaceEdit inverse_edit;
    std::string unified_diff;
    std::string undo_token;
    ChangeSetStatus status = ChangeSetStatus::Pending;
};

struct ChangeSetApplyOptions {
    bool save_after_apply = false;
    bool preflight = true;
};

struct ChangeSetResult {
    bool ok = false;
    std::string message;
};

class ChangeSetService {
public:
    static constexpr const char* kServiceId = "vanta.changes";

    ChangeSet Create(
        std::string source,
        std::string title,
        WorkspaceEdit edit);

    ChangeSet CreateFileReplacement(
        VirtualFile file,
        std::string source,
        std::string title,
        std::string original_text,
        std::string replacement_text,
        std::uint64_t expected_document_version = 0);
    ChangeSet CreateFileCreation(
        VirtualFile file,
        std::string source,
        std::string title,
        std::string text);
    ChangeSet CreateFileDeletion(
        VirtualFile file,
        std::string source,
        std::string title,
        std::string original_text);
    ChangeSet CreateFileRename(
        VirtualFile file,
        VirtualFile target_file,
        std::string source,
        std::string title);

    std::optional<ChangeSet> Get(const std::string& id) const;
    std::vector<ChangeSet> List() const;
    WorkspaceEditPreflight Preflight(
        Workspace& workspace,
        DocumentService& documents,
        const std::string& id) const;
    ChangeSetResult Approve(const std::string& id);
    ChangeSetResult Reject(const std::string& id);
    ChangeSetResult ApplyApproved(
        Workspace& workspace,
        DocumentService& documents,
        const std::string& id,
        ChangeSetApplyOptions options = {});
    ChangeSetResult UndoApplied(
        Workspace& workspace,
        DocumentService& documents,
        const std::string& id,
        ChangeSetApplyOptions options = {});

private:
    ChangeSet* MutableChangeSet(const std::string& id);
    WorkspaceEdit CreateInverseEdit(DocumentService& documents, const WorkspaceEdit& edit) const;
    WorkspaceEditPreflight PreflightOperation(DocumentService& documents, const WorkspaceEditOperation& operation) const;
    ChangeSetResult ApplyOperation(DocumentService& documents, const WorkspaceEditOperation& operation, ChangeSetApplyOptions options) const;

    std::map<std::string, ChangeSet> change_sets_;
    std::uint64_t next_change_set_id_ = 1;
};

std::string ToString(ChangeSetStatus status);
std::string CreateUnifiedDiff(
    const VirtualFile& file,
    const std::string& original_text,
    const std::string& replacement_text);

}
