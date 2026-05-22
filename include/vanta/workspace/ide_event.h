#pragma once

#include <string>

#include "vanta/core/diagnostic.h"
#include "vanta/core/event.h"
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
    IndexChanged,
    JobChanged,
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
    JobId job_id = 0;
};

using IdeEventBus = EventBus<IdeEvent>;

std::string ToString(IdeEventKind kind);
IdeEventKind IdeEventKindFromFileChange(VirtualFileChangeKind kind);

}
