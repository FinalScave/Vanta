#include "vanta/workspace/ide_event.h"

namespace vanta {

std::string ToString(IdeEventKind kind) {
    switch (kind) {
    case IdeEventKind::WorkspaceOpened:
        return "workspace.opened";
    case IdeEventKind::WorkspaceClosed:
        return "workspace.closed";
    case IdeEventKind::FileChanged:
        return "file.changed";
    case IdeEventKind::FileCreated:
        return "file.created";
    case IdeEventKind::FileDeleted:
        return "file.deleted";
    case IdeEventKind::ProjectChanged:
        return "project.changed";
    case IdeEventKind::DocumentOpened:
        return "document.opened";
    case IdeEventKind::DocumentChanged:
        return "document.changed";
    case IdeEventKind::DocumentSaved:
        return "document.saved";
    case IdeEventKind::DocumentClosed:
        return "document.closed";
    case IdeEventKind::DiagnosticsChanged:
        return "diagnostics.changed";
    case IdeEventKind::IndexChanged:
        return "index.changed";
    case IdeEventKind::JobChanged:
        return "job.changed";
    case IdeEventKind::JobStarted:
        return "job.started";
    case IdeEventKind::JobCompleted:
        return "job.completed";
    case IdeEventKind::ChangeSetProposed:
        return "changeSet.proposed";
    case IdeEventKind::ChangeSetApplied:
        return "changeSet.applied";
    }
    return "workspace.opened";
}

IdeEventKind IdeEventKindFromFileChange(VirtualFileChangeKind kind) {
    switch (kind) {
    case VirtualFileChangeKind::Created:
        return IdeEventKind::FileCreated;
    case VirtualFileChangeKind::Modified:
        return IdeEventKind::FileChanged;
    case VirtualFileChangeKind::Deleted:
        return IdeEventKind::FileDeleted;
    }
    return IdeEventKind::FileChanged;
}

}
