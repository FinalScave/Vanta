#include "vanta/workspace/workspace_context.h"

#include <utility>

#include "vanta/workspace/workspace_runtime.h"
#include "vanta/project/project.h"

namespace vanta {

WorkspaceContext::WorkspaceContext(WorkspaceRuntime& runtime)
    : runtime_(&runtime), project_(&runtime.ProjectValue()) {
}

void WorkspaceContext::SetProject(Project* project) {
    project_ = project;
}

void WorkspaceContext::RegisterService(std::string service_id, void* service) {
    if (service_id.empty()) {
        return;
    }
    if (service == nullptr) {
        services_.erase(service_id);
        return;
    }
    services_[std::move(service_id)] = service;
}

bool WorkspaceContext::UnregisterService(std::string_view service_id, const void* service) {
    auto it = services_.find(std::string(service_id));
    if (it == services_.end()) {
        return false;
    }
    if (service != nullptr && it->second != service) {
        return false;
    }
    services_.erase(it);
    return true;
}

void* WorkspaceContext::GetService(std::string_view service_id) const {
    auto it = services_.find(std::string(service_id));
    return it == services_.end() ? nullptr : it->second;
}

Workspace& WorkspaceContext::CurrentWorkspace() {
    if (runtime_ == nullptr) {
        throw std::runtime_error("Workspace runtime is not attached");
    }
    return runtime_->WorkspaceValue();
}

const Workspace& WorkspaceContext::CurrentWorkspace() const {
    if (runtime_ == nullptr) {
        throw std::runtime_error("Workspace runtime is not attached");
    }
    return runtime_->WorkspaceValue();
}

bool WorkspaceContext::WorkspaceOpen() const {
    return runtime_ != nullptr && runtime_->IsOpen();
}

Project* WorkspaceContext::CurrentProject() {
    return project_;
}

const Project* WorkspaceContext::CurrentProject() const {
    return project_;
}

Project& WorkspaceContext::RequireProject() {
    if (project_ == nullptr) {
        throw std::runtime_error("Project is not attached");
    }
    return *project_;
}

const Project& WorkspaceContext::RequireProject() const {
    if (project_ == nullptr) {
        throw std::runtime_error("Project is not attached");
    }
    return *project_;
}

CommandRegistry& WorkspaceContext::Commands() {
    return runtime_->Commands();
}

DocumentService& WorkspaceContext::Documents() {
    return runtime_->Documents();
}

VirtualFileSystem& WorkspaceContext::FileSystems() {
    return runtime_->FileSystems();
}

LanguageRegistry& WorkspaceContext::Languages() {
    return runtime_->Languages();
}

CodeIntelligenceService& WorkspaceContext::CodeIntelligence() {
    return runtime_->CodeIntelligence();
}

DiagnosticService& WorkspaceContext::Diagnostics() {
    return runtime_->Diagnostics();
}

BuildService& WorkspaceContext::Build() {
    return runtime_->Build();
}

ExecutionService& WorkspaceContext::Execution() {
    return runtime_->Execution();
}

RunConfigurationService& WorkspaceContext::RunConfigurations() {
    return runtime_->RunConfigurations();
}

const RunConfigurationService& WorkspaceContext::RunConfigurations() const {
    return runtime_->RunConfigurations();
}

JobService& WorkspaceContext::Jobs() {
    return runtime_->Jobs();
}

IndexService& WorkspaceContext::Indexes() {
    return runtime_->Indexes();
}

ProjectManager& WorkspaceContext::Projects() {
    return runtime_->Projects();
}

AgentToolRegistry& WorkspaceContext::AgentTools() {
    return runtime_->AgentTools();
}

AgentContextCollector& WorkspaceContext::AgentContext() {
    return runtime_->AgentContext();
}

AgentOperationService& WorkspaceContext::AgentOperations() {
    return runtime_->AgentOperations();
}

ModelService& WorkspaceContext::Models() {
    return runtime_->Models();
}

AgentRuntime& WorkspaceContext::Agents() {
    return runtime_->AgentRuntimeValue();
}

ChangeSetService& WorkspaceContext::Changes() {
    return runtime_->Changes();
}

RefactoringService& WorkspaceContext::Refactorings() {
    return runtime_->Refactorings();
}

DebugService& WorkspaceContext::Debug() {
    return runtime_->Debug();
}

GitService& WorkspaceContext::Git() {
    return runtime_->Git();
}

CapabilityRegistry& WorkspaceContext::Capabilities() {
    return runtime_->Capabilities();
}

ProjectTemplateService& WorkspaceContext::ProjectTemplates() {
    return runtime_->ProjectTemplates();
}

ScratchFileService& WorkspaceContext::ScratchFiles() {
    return runtime_->ScratchFiles();
}

ApprovalService& WorkspaceContext::Approvals() {
    return runtime_->Approvals();
}

PluginStorageService& WorkspaceContext::PluginStorage() {
    return runtime_->PluginStorage();
}

WorkspaceTrustService& WorkspaceContext::WorkspaceTrust() {
    return runtime_->WorkspaceTrust();
}

SettingsService& WorkspaceContext::Settings() {
    return runtime_->WorkspaceSettings();
}

LocalizationRegistry& WorkspaceContext::Localization() {
    return runtime_->Localization();
}

const LocalizationRegistry& WorkspaceContext::Localization() const {
    return runtime_->Localization();
}

Localizer WorkspaceContext::LocalizerFor(std::string owner_id) const {
    return runtime_->Localization().LocalizerForOwner(std::move(owner_id));
}

IdeEventBus& WorkspaceContext::Events() {
    return runtime_->EventsValue();
}

const IdeEventBus& WorkspaceContext::Events() const {
    return runtime_->EventsValue();
}

void WorkspaceContext::Publish(IdeEvent event) {
    runtime_->Publish(std::move(event));
}

void WorkspaceContext::RefreshProject() {
    runtime_->RefreshProject();
}

std::uint64_t WorkspaceContext::OnEvent(const Component& owner, IdeEventBus::Listener listener) {
    const std::uint64_t id = Events().Subscribe(std::move(listener));
    event_subscriptions_[owner.Id()].push_back(id);
    return id;
}

std::uint64_t WorkspaceContext::OnEvent(const Component& owner, IdeEventKind kind, IdeEventBus::Listener listener) {
    return OnEvent(owner, [kind, listener = std::move(listener)](const IdeEvent& event) {
        if (event.kind == kind) {
            listener(event);
        }
    });
}

void WorkspaceContext::RemoveEventSubscriptions(const std::string& component_id) {
    auto it = event_subscriptions_.find(component_id);
    if (it == event_subscriptions_.end() || runtime_ == nullptr) {
        return;
    }
    for (std::uint64_t listener_id : it->second) {
        runtime_->EventsValue().Unsubscribe(listener_id);
    }
    event_subscriptions_.erase(it);
}

}
