#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "vanta/core/event.h"
#include "vanta/core/diagnostic.h"
#include "vanta/execution/job_service.h"
#include "vanta/project/project.h"
#include "ui/command_palette.h"
#include "ui/editor.h"
#include "vanta/workspace/ide_event.h"
#include "vanta/workspace/workspace.h"

namespace vanta {

class WorkspaceContext;

struct UiState {
    bool workspace_open = false;
    WorkspaceInfo workspace;
    std::vector<ProjectView> project_views;
    struct ProjectAttachmentState {
        std::string id;
        std::string kind;
        std::string title;
        Value data;
    };
    struct ProjectState {
        ProjectOrigin origin = ProjectOrigin::kWorkspace;
        VirtualFile root;
        std::vector<ProjectModule> modules;
        std::vector<ProjectFacet> facets;
        std::vector<ProjectAttachmentState> attachments;

        bool HasFacet(const std::string& type) const;
        bool HasAttachment(const std::string& id) const;
    };
    ProjectState project;
    std::vector<EditorTab> tabs;
    std::vector<Diagnostic> problems;
    std::vector<JobRecord> jobs;
    std::optional<IdeEvent> last_event;
    std::uint64_t version = 0;
};

struct UiStateChangeEvent {
    UiState state;
    std::optional<IdeEvent> source_event;
};

class UiStateStore {
public:
    UiStateStore() = default;
    explicit UiStateStore(WorkspaceContext& context, std::string client_id = "default");
    UiStateStore(const UiStateStore&) = delete;
    UiStateStore& operator=(const UiStateStore&) = delete;
    ~UiStateStore();

    void Attach(WorkspaceContext& context);
    void Detach();
    void Refresh();

    EditorWorkspace& Editor();
    const EditorWorkspace& Editor() const;
    KeybindingRegistry& Keybindings();
    const KeybindingRegistry& Keybindings() const;

    const UiState& State() const;

    EditorTab& OpenFile(const VirtualFile& file);
    bool CloseTab(std::uint64_t id);
    bool ActivateTab(std::uint64_t id);
    EditorTab* OpenDiagnostic(const Diagnostic& diagnostic);

    std::uint64_t OnDidChange(EventBus<UiStateChangeEvent>::Listener listener);
    void RemoveListener(std::uint64_t listener_id);

private:
    void HandleEvent(const IdeEvent& event);
    void EnsureLayoutState();
    void Publish(std::optional<IdeEvent> source_event = std::nullopt);

    WorkspaceContext* context_ = nullptr;
    std::string client_id_ = "default";
    std::uint64_t listener_id_ = 0;
    std::uint64_t project_view_listener_id_ = 0;
    EditorWorkspace editor_;
    KeybindingRegistry keybindings_;
    UiState state_;
    EventBus<UiStateChangeEvent> on_did_change_;
};

}
