#pragma once

#include <string>

#include "vanta/core/diagnostic.h"
#include "vanta/platform/async.h"
#include "vanta/execution/job_service.h"
#include "vanta/vfs/file_watcher.h"

namespace vanta {

enum class IdeEventKind {
    WorkspaceOpened,
    WorkspaceClosed,
    FileChanged,
    FileCreated,
    FileDeleted,
    ProjectChanged,
    DocumentOpened,
    DocumentChanged,
    DocumentSaved,
    DocumentClosed,
    DiagnosticsChanged,
    JobStarted,
    JobCompleted,
    ChangeSetProposed,
    ChangeSetApplied,
};

struct IdeEvent {
    IdeEventKind kind = IdeEventKind::WorkspaceOpened;
    VirtualFile file;
    std::string source;
    std::string message;
    JobId jobId = 0;
};

using IdeEventBus = EventBus<IdeEvent>;

std::string toString(IdeEventKind kind);
IdeEventKind ideEventKindFromFileChange(VirtualFileChangeKind kind);

}
