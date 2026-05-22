#include "ui/ui_state_store.h"

#include "ui/layout_state_store.h"
#include "vanta/project/project.h"
#include "vanta/project/project_manager.h"
#include "vanta/workspace/diagnostic_service.h"
#include "vanta/workspace/document_service.h"
#include "vanta/workspace/workspace_context.h"

#include <memory>
#include <utility>

namespace vanta {
namespace {

UiState::ProjectState ProjectStateProjection(const ProjectModel& model) {
    UiState::ProjectState state;
    state.origin = model.origin;
    state.root = model.root;
    state.modules = model.modules;
    state.facets = model.facets;
    for (const ProjectAttachment* attachment : model.Attachments()) {
        if (attachment == nullptr) {
            continue;
        }
        state.attachments.push_back({
            .id = attachment->Id(),
            .kind = attachment->Kind(),
            .title = attachment->Title(),
            .data = attachment->Projection(),
        });
    }
    return state;
}

}

bool UiState::ProjectState::HasFacet(const std::string& type) const {
    for (const ProjectFacet& facet : facets) {
        if (facet.type == type) {
            return true;
        }
    }
    return false;
}

bool UiState::ProjectState::HasAttachment(const std::string& id) const {
    for (const ProjectAttachmentState& attachment : attachments) {
        if (attachment.id == id) {
            return true;
        }
    }
    return false;
}

UiStateStore::UiStateStore(WorkspaceContext& context, std::string client_id)
    : client_id_(std::move(client_id)) {
    Attach(context);
}

UiStateStore::~UiStateStore() {
    Detach();
}

void UiStateStore::Attach(WorkspaceContext& context) {
    Detach();
    context_ = &context;
    EnsureLayoutState();
    listener_id_ = context.Events().Subscribe([this](const IdeEvent& event) {
        HandleEvent(event);
    });
    project_view_listener_id_ = context.Projects().OnDidChangeViews([this](const ProjectViewChangeEvent&) {
        Refresh();
    });
    Refresh();
}

void UiStateStore::Detach() {
    if (context_ != nullptr && listener_id_ != 0) {
        context_->Events().Unsubscribe(listener_id_);
    }
    if (context_ != nullptr && project_view_listener_id_ != 0) {
        context_->Projects().RemoveViewListener(project_view_listener_id_);
    }
    context_ = nullptr;
    listener_id_ = 0;
    project_view_listener_id_ = 0;
}

void UiStateStore::Refresh() {
    if (context_ == nullptr || !context_->WorkspaceOpen()) {
        state_.workspace_open = false;
        ++state_.version;
        Publish();
        return;
    }

    state_.workspace_open = true;
    state_.workspace = context_->CurrentWorkspace().Info();
    state_.project_views = context_->Projects().Views(*context_);
    state_.project = ProjectStateProjection(context_->RequireProject().Model());
    state_.tabs = editor_.Tabs();
    state_.problems = context_->Diagnostics().AllDiagnostics();
    state_.jobs = context_->Jobs().Jobs();
    ++state_.version;
    Publish(state_.last_event);
}

EditorWorkspace& UiStateStore::Editor() {
    return editor_;
}

const EditorWorkspace& UiStateStore::Editor() const {
    return editor_;
}

KeybindingRegistry& UiStateStore::Keybindings() {
    return keybindings_;
}

const KeybindingRegistry& UiStateStore::Keybindings() const {
    return keybindings_;
}

const UiState& UiStateStore::State() const {
    return state_;
}

EditorTab& UiStateStore::OpenFile(const VirtualFile& file) {
    if (context_ != nullptr) {
        std::string ignored;
        context_->Documents().OpenDocument(file, &ignored);
    }
    EditorTab& tab = editor_.OpenFile(file);
    Refresh();
    return tab;
}

bool UiStateStore::CloseTab(std::uint64_t id) {
    const bool closed = editor_.CloseTab(id);
    if (closed) {
        Refresh();
    }
    return closed;
}

bool UiStateStore::ActivateTab(std::uint64_t id) {
    const bool activated = editor_.ActivateTab(id);
    if (activated) {
        Refresh();
    }
    return activated;
}

EditorTab* UiStateStore::OpenDiagnostic(const Diagnostic& diagnostic) {
    if (context_ != nullptr) {
        std::string ignored;
        context_->Documents().OpenDocument(diagnostic.location.file, &ignored);
    }
    EditorTab* tab = editor_.OpenDiagnostic(diagnostic);
    Refresh();
    return tab;
}

std::uint64_t UiStateStore::OnDidChange(EventBus<UiStateChangeEvent>::Listener listener) {
    return on_did_change_.Subscribe(std::move(listener));
}

void UiStateStore::RemoveListener(std::uint64_t listener_id) {
    on_did_change_.Unsubscribe(listener_id);
}

void UiStateStore::HandleEvent(const IdeEvent& event) {
    state_.last_event = event;
    Refresh();
    state_.last_event = event;
}

void UiStateStore::EnsureLayoutState() {
    if (context_ == nullptr || !context_->WorkspaceOpen() || context_->CurrentProject() == nullptr) {
        return;
    }
    Project* project = context_->CurrentProject();
    if (project->GetComponent(LayoutStateStore::kComponentId) == nullptr) {
        context_->Projects().BindComponent(*project, std::make_unique<LayoutStateStore>());
    }
}

void UiStateStore::Publish(std::optional<IdeEvent> source_event) {
    on_did_change_.Publish({
        .state = state_,
        .source_event = std::move(source_event),
    });
}

}
