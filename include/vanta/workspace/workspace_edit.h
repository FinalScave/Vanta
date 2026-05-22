#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vanta/core/text.h"
#include "vanta/vfs/virtual_file.h"

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
    VirtualFile target_file;
    std::string original_text;
    std::string replacement_text;
    std::vector<TextEdit> text_edits;
    std::uint64_t expected_document_version = 0;
};

struct WorkspaceEdit {
    std::vector<WorkspaceEditOperation> operations;
};

std::string ToString(WorkspaceEditOperationKind kind);

}
