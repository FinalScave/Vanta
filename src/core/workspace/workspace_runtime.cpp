#include "vanta/workspace/workspace_runtime.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <utility>

#include "execution/build_service_impl.h"
#include "execution/run_configuration_service_impl.h"
#include "language/language_registry_impl.h"
#include "project/project_state_store.h"
#include "workspace/command_registry_impl.h"
#include "workspace/git_service_impl.h"

namespace vanta {
namespace {

bool IsTerminalStatus(JobStatus status) {
    return status == JobStatus::Succeeded || status == JobStatus::Failed || status == JobStatus::Cancelled;
}

IdeEventKind KindFromDocumentChange(DocumentChangeKind kind) {
    switch (kind) {
    case DocumentChangeKind::Opened:
        return IdeEventKind::DocumentOpened;
    case DocumentChangeKind::Changed:
        return IdeEventKind::DocumentChanged;
    case DocumentChangeKind::Saved:
        return IdeEventKind::DocumentSaved;
    case DocumentChangeKind::Closed:
        return IdeEventKind::DocumentClosed;
    }
    return IdeEventKind::DocumentChanged;
}

std::string JoinStrings(const std::vector<std::string>& values) {
    std::string result;
    for (const std::string& value : values) {
        if (!result.empty()) {
            result += ", ";
        }
        result += value;
    }
    return result;
}

}

WorkspaceRuntime::WorkspaceRuntime(VirtualFileSystem& vfs, JobDispatcher job_dispatcher, JobDispatch main_dispatcher)
    : build_(std::make_unique<internal::BuildServiceImpl>()),
      git_(std::make_unique<internal::GitServiceImpl>()),
      run_configurations_(std::make_unique<internal::RunConfigurationServiceImpl>()),
      languages_(std::make_unique<internal::LanguageRegistryImpl>()),
      jobs_(std::move(job_dispatcher)),
      commands_(std::make_unique<internal::CommandRegistryImpl>()),
      approvals_(workspace_trust_),
      vfs_(vfs),
      main_dispatcher_(std::move(main_dispatcher)),
      project_state_store_(std::make_unique<ProjectStateStore>()),
      context_(*this) {
    context_.RegisterService(AgentToolRegistry::kServiceId, &agent_);
    context_.RegisterService(AgentContextCollector::kServiceId, &agent_context_);
    context_.RegisterService(AgentOperationService::kServiceId, &agent_operations_);
    context_.RegisterService(AgentRuntime::kServiceId, &agent_runtime_);
    context_.RegisterService(ApprovalService::kServiceId, &approvals_);
    context_.RegisterService(BuildService::kServiceId, build_.get());
    context_.RegisterService(CapabilityRegistry::kServiceId, &capabilities_);
    context_.RegisterService(ChangeSetService::kServiceId, &changes_);
    context_.RegisterService(CodeIntelligenceService::kServiceId, &code_intelligence_);
    context_.RegisterService(CommandRegistry::kServiceId, commands_.get());
    context_.RegisterService(DebugService::kServiceId, &debug_);
    context_.RegisterService(DiagnosticService::kServiceId, &diagnostics_);
    context_.RegisterService(DocumentService::kServiceId, &documents_);
    context_.RegisterService(ExecutionService::kServiceId, &execution_);
    context_.RegisterService(GitService::kServiceId, git_.get());
    context_.RegisterService(IndexService::kServiceId, &indexes_);
    context_.RegisterService(JobService::kServiceId, &jobs_);
    context_.RegisterService(LanguageRegistry::kServiceId, languages_.get());
    context_.RegisterService(LocalizationRegistry::kServiceId, &localization_);
    context_.RegisterService(ModelService::kServiceId, &model_service_);
    context_.RegisterService(PluginStorageService::kServiceId, &plugin_storage_);
    context_.RegisterService(ProjectManager::kServiceId, &project_manager_);
    context_.RegisterService(ProjectTemplateService::kServiceId, &project_templates_);
    context_.RegisterService(RefactoringService::kServiceId, &refactorings_);
    context_.RegisterService(RunConfigurationService::kServiceId, run_configurations_.get());
    context_.RegisterService(ScratchFileService::kServiceId, &scratch_files_);
    context_.RegisterService(SettingsService::kServiceId, &workspace_settings_);
    context_.RegisterService(VirtualFileSystem::kServiceId, &vfs_);
    context_.RegisterService(WorkspaceTrustService::kServiceId, &workspace_trust_);
    RegisterDefaultIndexProviders(indexes_);
    RegisterDefaultProjectTemplates(project_templates_);
    RegisterDefaultSettings(workspace_settings_);
}

WorkspaceRuntime::~WorkspaceRuntime() {
    Close();
}

bool WorkspaceRuntime::Open(const std::filesystem::path& workspace_path, std::string* error_message, bool initialize) {
    capabilities_.Clear();
    indexes_.ClearSnapshots();
    jobs_.Clear();
    agent_operations_.Clear();
    agent_runtime_.Clear();
    initialized_ = false;
    const JobId open_job = jobs_.Start(JobKind::Initialization, "Open workspace");
    workspace_.BindFileSystem(vfs_);
    std::error_code status_error;
    const bool open_as_file = std::filesystem::is_regular_file(workspace_path, status_error);
    std::filesystem::path root_path = open_as_file ? workspace_path.parent_path() : workspace_path;
    if (root_path.empty()) {
        root_path = std::filesystem::current_path();
    }
    if (!workspace_.Open(root_path, error_message)) {
        jobs_.Complete(open_job, false, "Workspace open failed");
        return false;
    }
    static_cast<internal::GitServiceImpl&>(*git_).SetWorkspaceRoot(workspace_.Info().root_path);
    jobs_.UpdateProgress(open_job, 0.3, "Workspace opened");
    if (open_as_file) {
        project_manager_.SetSingleFile(workspace_.File(workspace_path));
    } else {
        project_manager_.ClearSingleFile();
    }

    workspace_settings_.Load({.kind = SettingScopeKind::Workspace, .qualifier = workspace_.Info().root_path.string()}, workspace_.Info().root_path / ".vanta" / "settings.json");
    project_state_store_->Load(workspace_.Info().root_path / ".vanta" / "state.json", &project_state_);
    plugin_storage_.SetRoot(workspace_.Info().root_path / ".vanta" / "plugin-storage");
    open_ = true;
    jobs_.Complete(open_job, true, "Workspace open completed");
    if (initialize) {
        InitializeWorkspace();
    }
    return true;
}

void WorkspaceRuntime::InitializeWorkspace() {
    if (!open_ || initialized_) {
        return;
    }
    const JobId initialize_job = jobs_.Start(JobKind::Initialization, "Initialize workspace");
    RegisterDefaultExecutionProviders(execution_);
    RegisterDefaultRunConfigurations(*run_configurations_);
    project_manager_.BindDefaultComponents(project_);
    context_.SetProject(&project_);
    project_manager_.AttachProject(context_, project_);
    project_manager_.RestoreProject(project_, project_state_);
    jobs_.UpdateProgress(initialize_job, 0.4, "Components attached");
    RegisterDefaultAgentContextProviders(agent_context_);
    jobs_.UpdateProgress(initialize_job, 0.5, "Agent context providers Ready");
    ConnectEventRelays();
    RefreshProject();
    initialized_ = true;
    UpdateCoreCapabilities();
    jobs_.Complete(initialize_job, true, "Workspace initialization completed");
    Publish({
        .kind = IdeEventKind::WorkspaceOpened,
        .file = workspace_.RootFile(),
    });
}

void WorkspaceRuntime::Close() {
    if (!open_) {
        return;
    }
    StopFileWatcher();
    StopDocumentSync();
    jobs_.RequestCancelAll();
    WaitForActiveJobs(std::chrono::milliseconds(500));
    jobs_.TerminateAll("Workspace is closing");
    WaitForActiveJobs(std::chrono::milliseconds(500));
    const VirtualFile root = workspace_.RootFile();
    project_manager_.CloseProject(project_);
    project_state_ = project_manager_.SaveProject(project_, project_state_);
    workspace_settings_.Save({.kind = SettingScopeKind::Workspace, .qualifier = workspace_.Info().root_path.string()}, workspace_.Info().root_path / ".vanta" / "settings.json");
    project_state_store_->Save(workspace_.Info().root_path / ".vanta" / "state.json", project_state_);
    open_ = false;
    initialized_ = false;
    Publish({
        .kind = IdeEventKind::WorkspaceClosed,
        .file = root,
    });
    DisconnectEventRelays();
    project_manager_.DetachProject(project_);
    context_.SetProject(nullptr);
    indexes_.ClearSnapshots();
    capabilities_.Clear();
    jobs_.Clear();
    agent_runtime_.Clear();
}

bool WorkspaceRuntime::IsOpen() const {
    return open_;
}

void WorkspaceRuntime::RefreshProject() {
    const JobId job = jobs_.Start(JobKind::Initialization, "Refresh project");
    project_manager_.Refresh(context_, project_);
    jobs_.UpdateProgress(job, 0.4, "Project model Ready");
    RefreshIndexes("Refresh project index");
    UpdateCoreCapabilities();
    project_manager_.InvalidateViews({});
    jobs_.Complete(job, true, "Project refresh completed");
    Publish({
        .kind = IdeEventKind::ProjectChanged,
        .file = workspace_.RootFile(),
    });
}

void WorkspaceRuntime::StartDocumentSync() {
    if (document_sync_ != nullptr) {
        return;
    }
    document_sync_ = std::make_unique<DocumentLanguageSynchronizer>(documents_, *languages_);
    document_sync_->Start();
}

void WorkspaceRuntime::StopDocumentSync() {
    if (document_sync_ == nullptr) {
        return;
    }
    document_sync_->Stop();
    document_sync_.reset();
}

bool WorkspaceRuntime::StartFileWatcher(std::string* error_message) {
    if (file_watcher_ != nullptr && file_watcher_->Running()) {
        return true;
    }
    file_watcher_ = CreatePlatformFileWatcher(vfs_);
    return file_watcher_->Start(workspace_.RootFile(), [this](const VirtualFileChangeEvent& event) {
        DispatchMain([this, event] {
            HandleFileChange(event);
        });
    }, error_message);
}

void WorkspaceRuntime::StopFileWatcher() {
    if (file_watcher_ == nullptr) {
        return;
    }
    file_watcher_->Stop();
    file_watcher_.reset();
}

ProjectManager& WorkspaceRuntime::Projects() {
    return project_manager_;
}

const ProjectManager& WorkspaceRuntime::Projects() const {
    return project_manager_;
}

Workspace& WorkspaceRuntime::WorkspaceValue() {
    return workspace_;
}

const Workspace& WorkspaceRuntime::WorkspaceValue() const {
    return workspace_;
}

Project& WorkspaceRuntime::ProjectValue() {
    return project_;
}

const Project& WorkspaceRuntime::ProjectValue() const {
    return project_;
}

DocumentService& WorkspaceRuntime::Documents() {
    return documents_;
}

const DocumentService& WorkspaceRuntime::Documents() const {
    return documents_;
}

BuildService& WorkspaceRuntime::Build() {
    return *build_;
}

const BuildService& WorkspaceRuntime::Build() const {
    return *build_;
}

AgentToolRegistry& WorkspaceRuntime::AgentTools() {
    return agent_;
}

const AgentToolRegistry& WorkspaceRuntime::AgentTools() const {
    return agent_;
}

AgentContextCollector& WorkspaceRuntime::AgentContext() {
    return agent_context_;
}

AgentOperationService& WorkspaceRuntime::AgentOperations() {
    return agent_operations_;
}

const AgentOperationService& WorkspaceRuntime::AgentOperations() const {
    return agent_operations_;
}

ModelService& WorkspaceRuntime::Models() {
    return model_service_;
}

const ModelService& WorkspaceRuntime::Models() const {
    return model_service_;
}

AgentRuntime& WorkspaceRuntime::AgentRuntimeValue() {
    return agent_runtime_;
}

const AgentRuntime& WorkspaceRuntime::AgentRuntimeValue() const {
    return agent_runtime_;
}

ChangeSetService& WorkspaceRuntime::Changes() {
    return changes_;
}

RefactoringService& WorkspaceRuntime::Refactorings() {
    return refactorings_;
}

const RefactoringService& WorkspaceRuntime::Refactorings() const {
    return refactorings_;
}

DebugService& WorkspaceRuntime::Debug() {
    return debug_;
}

const DebugService& WorkspaceRuntime::Debug() const {
    return debug_;
}

ExecutionService& WorkspaceRuntime::Execution() {
    return execution_;
}

const ExecutionService& WorkspaceRuntime::Execution() const {
    return execution_;
}

GitService& WorkspaceRuntime::Git() {
    return *git_;
}

const GitService& WorkspaceRuntime::Git() const {
    return *git_;
}

RunConfigurationService& WorkspaceRuntime::RunConfigurations() {
    return *run_configurations_;
}

const RunConfigurationService& WorkspaceRuntime::RunConfigurations() const {
    return *run_configurations_;
}

LanguageRegistry& WorkspaceRuntime::Languages() {
    return *languages_;
}

const LanguageRegistry& WorkspaceRuntime::Languages() const {
    return *languages_;
}

CodeIntelligenceService& WorkspaceRuntime::CodeIntelligence() {
    return code_intelligence_;
}

DiagnosticService& WorkspaceRuntime::Diagnostics() {
    return diagnostics_;
}

const DiagnosticService& WorkspaceRuntime::Diagnostics() const {
    return diagnostics_;
}

JobService& WorkspaceRuntime::Jobs() {
    return jobs_;
}

const JobService& WorkspaceRuntime::Jobs() const {
    return jobs_;
}

CommandRegistry& WorkspaceRuntime::Commands() {
    return *commands_;
}

const CommandRegistry& WorkspaceRuntime::Commands() const {
    return *commands_;
}

IndexService& WorkspaceRuntime::Indexes() {
    return indexes_;
}

const IndexService& WorkspaceRuntime::Indexes() const {
    return indexes_;
}

CapabilityRegistry& WorkspaceRuntime::Capabilities() {
    return capabilities_;
}

const CapabilityRegistry& WorkspaceRuntime::Capabilities() const {
    return capabilities_;
}

ProjectTemplateService& WorkspaceRuntime::ProjectTemplates() {
    return project_templates_;
}

const ProjectTemplateService& WorkspaceRuntime::ProjectTemplates() const {
    return project_templates_;
}

ScratchFileService& WorkspaceRuntime::ScratchFiles() {
    return scratch_files_;
}

const ScratchFileService& WorkspaceRuntime::ScratchFiles() const {
    return scratch_files_;
}

ApprovalService& WorkspaceRuntime::Approvals() {
    return approvals_;
}

const ApprovalService& WorkspaceRuntime::Approvals() const {
    return approvals_;
}

WorkspaceTrustService& WorkspaceRuntime::WorkspaceTrust() {
    return workspace_trust_;
}

const WorkspaceTrustService& WorkspaceRuntime::WorkspaceTrust() const {
    return workspace_trust_;
}

SettingsService& WorkspaceRuntime::WorkspaceSettings() {
    return workspace_settings_;
}

LocalizationRegistry& WorkspaceRuntime::Localization() {
    return localization_;
}

const LocalizationRegistry& WorkspaceRuntime::Localization() const {
    return localization_;
}

PluginStorageService& WorkspaceRuntime::PluginStorage() {
    return plugin_storage_;
}

VirtualFileSystem& WorkspaceRuntime::FileSystems() {
    return vfs_;
}

const VirtualFileSystem& WorkspaceRuntime::FileSystems() const {
    return vfs_;
}

WorkspaceContext& WorkspaceRuntime::Context() {
    return context_;
}

const WorkspaceContext& WorkspaceRuntime::Context() const {
    return context_;
}

std::uint64_t WorkspaceRuntime::OnEvent(IdeEventBus::Listener listener) {
    return events_.Subscribe(std::move(listener));
}

void WorkspaceRuntime::RemoveEventListener(std::uint64_t listener_id) {
    events_.Unsubscribe(listener_id);
}

IdeEventBus& WorkspaceRuntime::EventsValue() {
    return events_;
}

void WorkspaceRuntime::Publish(IdeEvent event) {
    events_.Publish(event);
}

void WorkspaceRuntime::DispatchMain(JobTask task) {
    if (main_dispatcher_) {
        main_dispatcher_(std::move(task));
        return;
    }
    task();
}

void WorkspaceRuntime::RefreshIndexes(std::string title) {
    indexes_.Refresh(context_, std::move(title));
    UpdateCoreCapabilities();
}

void WorkspaceRuntime::UpdateCoreCapabilities() {
    capabilities_.Set({
        .id = "workspace.open",
        .title = "Workspace Open",
        .provider_id = "vanta.core",
        .status = open_ ? CapabilityStatus::Available : CapabilityStatus::Unavailable,
        .message = open_ ? "Workspace is open" : "Workspace is closed",
        .details = {{"root", workspace_.Info().root_path.string()}},
    });
    capabilities_.Set({
        .id = "project.model",
        .title = "Project Model",
        .provider_id = "vanta.core",
        .status = project_.Model().facets.empty() && project_.Model().attachments.empty() ? CapabilityStatus::Degraded : CapabilityStatus::Available,
        .message = project_.Model().facets.empty() && project_.Model().attachments.empty() ? "Generic project model is available" : "Project model has facets or attachments",
        .details = {
            {"type", PrimaryProjectType(project_.Model())},
            {"origin", ToString(project_.Model().origin)},
        },
    });
    capabilities_.Set({
        .id = "index.workspace",
        .title = "Workspace Index",
        .provider_id = "vanta.core",
        .status = indexes_.Snapshots().empty() ? CapabilityStatus::Unavailable : CapabilityStatus::Available,
        .message = indexes_.Snapshots().empty() ? "No index snapshots are available" : "Index snapshots are Ready",
        .details = {{"snapshots", std::to_string(indexes_.Snapshots().size())}},
    });
    capabilities_.Set({
        .id = "language.registry",
        .title = "Language Registry",
        .provider_id = "vanta.core",
        .status = CapabilityStatus::Available,
        .message = "Language registry is available",
        .details = {{"languageIds", JoinStrings(languages_->LanguageIds())}},
    });
    capabilities_.Set({
        .id = "build.providers",
        .title = "Build Providers",
        .provider_id = "vanta.core",
        .status = build_->BuildProviderIds().empty() ? CapabilityStatus::Degraded : CapabilityStatus::Available,
        .message = build_->BuildProviderIds().empty() ? "No build provider is registered" : "Build providers are registered",
        .details = {{"providerIds", JoinStrings(build_->BuildProviderIds())}},
    });
    capabilities_.Set({
        .id = "agent.operations",
        .title = "Agent Operations",
        .provider_id = "vanta.core",
        .status = CapabilityStatus::Available,
        .message = "Agent operation protocol is available",
        .details = {{"records", std::to_string(agent_operations_.Records().size())}},
    });
}

void WorkspaceRuntime::WaitForActiveJobs(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (const JobRecord& job : jobs_.Jobs()) {
        if (!IsTerminalStatus(job.status)) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                return;
            }
            jobs_.Wait(job.id, std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now));
        }
    }
}

void WorkspaceRuntime::ConnectEventRelays() {
    if (document_listener_ == 0) {
        document_listener_ = documents_.OnDidChangeDocument([this](const DocumentChangeEvent& event) {
            DispatchMain([this, event] {
                PublishDocumentEvent(event);
            });
        });
    }
    if (diagnostic_listener_ == 0) {
        diagnostic_listener_ = diagnostics_.OnDidChangeDiagnostics([this](const DiagnosticChangeEvent& event) {
            DispatchMain([this, event] {
                Publish({
                    .kind = IdeEventKind::DiagnosticsChanged,
                    .source = event.source,
                });
            });
        });
    }
    if (index_listener_ == 0) {
        index_listener_ = indexes_.OnDidChangeIndex([this](const IndexChangeEvent&) {
            DispatchMain([this] {
                UpdateCoreCapabilities();
                Publish({
                    .kind = IdeEventKind::IndexChanged,
                    .source = "vanta.index",
                });
            });
        });
    }
    if (job_listener_ == 0) {
        job_listener_ = jobs_.OnDidChangeJob([this](const JobChangeEvent& event) {
            DispatchMain([this, event] {
                PublishJobEvent(event);
            });
        });
    }
}

void WorkspaceRuntime::DisconnectEventRelays() {
    if (document_listener_ != 0) {
        documents_.RemoveDocumentListener(document_listener_);
        document_listener_ = 0;
    }
    if (diagnostic_listener_ != 0) {
        diagnostics_.RemoveDiagnosticsListener(diagnostic_listener_);
        diagnostic_listener_ = 0;
    }
    if (index_listener_ != 0) {
        indexes_.RemoveIndexListener(index_listener_);
        index_listener_ = 0;
    }
    if (job_listener_ != 0) {
        jobs_.RemoveJobListener(job_listener_);
        job_listener_ = 0;
    }
    job_statuses_.clear();
}

void WorkspaceRuntime::HandleFileChange(const VirtualFileChangeEvent& event) {
    if (!open_) {
        return;
    }
    Publish({
        .kind = IdeEventKindFromFileChange(event.kind),
        .file = event.file,
        .message = ToString(event.kind),
    });
    RefreshProject();
}

void WorkspaceRuntime::PublishDocumentEvent(const DocumentChangeEvent& event) {
    Publish({
        .kind = KindFromDocumentChange(event.kind),
        .file = event.file,
    });
}

void WorkspaceRuntime::PublishJobEvent(const JobChangeEvent& event) {
    const auto previous = job_statuses_.find(event.job.id);
    const bool first_status = previous == job_statuses_.end();
    const JobStatus old_status = first_status ? JobStatus::Pending : previous->second;
    job_statuses_[event.job.id] = event.job.status;

    Publish({
        .kind = IdeEventKind::JobChanged,
        .source = ToString(event.job.kind),
        .message = event.job.title,
        .job_id = event.job.id,
    });

    if (event.job.status == JobStatus::Running && old_status != JobStatus::Running) {
        Publish({
            .kind = IdeEventKind::JobStarted,
            .source = ToString(event.job.kind),
            .message = event.job.title,
            .job_id = event.job.id,
        });
    } else if (IsTerminalStatus(event.job.status) && old_status != event.job.status) {
        Publish({
            .kind = IdeEventKind::JobCompleted,
            .source = ToString(event.job.kind),
            .message = event.job.title,
            .job_id = event.job.id,
        });
    }
}

}
