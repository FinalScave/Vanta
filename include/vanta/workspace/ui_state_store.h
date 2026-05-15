#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "vanta/core/diagnostic.h"
#include "vanta/workspace/editor.h"
#include "vanta/workspace/workspace.h"
#include "vanta/workspace/ide_event.h"
#include "vanta/plugin/contribution_registry.h"
#include "vanta/project/project_manager.h"
#include "vanta/execution/job_service.h"

namespace vanta {

class WorkspaceRuntime;

struct UiState {
    bool workspaceOpen = false;
    WorkspaceInfo workspace;
    FileTreeNode fileTree;
    ProjectModel project;
    std::vector<EditorTab> tabs;
    std::vector<Diagnostic> problems;
    std::vector<JobRecord> jobs;
    std::vector<Contribution> contributions;
    std::optional<IdeEvent> lastEvent;
    std::uint64_t version = 0;
};

class UiStateStore {
public:
    void attach(WorkspaceRuntime& runtime);
    void detach();
    void refresh();

    const UiState& state() const;

private:
    void handleEvent(const IdeEvent& event);

    WorkspaceRuntime* runtime_ = nullptr;
    std::uint64_t listenerId_ = 0;
    UiState state_;
};

}
