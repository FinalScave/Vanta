#include "vanta/workspace/ui_state_store.h"

#include "vanta/workspace/workspace_runtime.h"

namespace vanta {

void UiStateStore::attach(WorkspaceRuntime& runtime) {
    detach();
    runtime_ = &runtime;
    listenerId_ = runtime.onEvent([this](const IdeEvent& event) {
        handleEvent(event);
    });
    refresh();
}

void UiStateStore::detach() {
    if (runtime_ != nullptr && listenerId_ != 0) {
        runtime_->removeEventListener(listenerId_);
    }
    runtime_ = nullptr;
    listenerId_ = 0;
}

void UiStateStore::refresh() {
    if (runtime_ == nullptr || !runtime_->isOpen()) {
        state_.workspaceOpen = false;
        ++state_.version;
        return;
    }

    state_.workspaceOpen = true;
    state_.workspace = runtime_->workspace().info();
    state_.fileTree = runtime_->workspace().fileTree();
    state_.project = runtime_->project().model();
    state_.tabs = runtime_->editor().tabs();
    state_.problems = runtime_->diagnostics().allDiagnostics();
    state_.jobs = runtime_->jobs().jobs();
    state_.contributions = runtime_->contributions().list();
    ++state_.version;
}

const UiState& UiStateStore::state() const {
    return state_;
}

void UiStateStore::handleEvent(const IdeEvent& event) {
    state_.lastEvent = event;
    refresh();
    state_.lastEvent = event;
}

}
