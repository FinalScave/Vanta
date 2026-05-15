#include "vanta/workspace/workspace_runtime.h"

#include <cstdint>
#include <filesystem>
#include <utility>

namespace vanta {
namespace {

bool isTerminalStatus(JobStatus status) {
    return status == JobStatus::Succeeded || status == JobStatus::Failed || status == JobStatus::Cancelled;
}

IdeEventKind kindFromDocumentChange(DocumentChangeKind kind) {
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

Json stringsToJson(const std::vector<std::string>& values) {
    Json::Array items;
    for (const std::string& value : values) {
        items.push_back(Json(value));
    }
    return Json::array(std::move(items));
}

}

WorkspaceRuntime::WorkspaceRuntime(VirtualFileSystem& vfs, AsyncRuntime& async)
    : vfs_(vfs), async_(async), context_(*this) {
    agentOperations_.setJournal(&agentOperationJournal_);
    registerDefaultIndexProviders(indexes_, search_, cppIndex_);
    registerDefaultProjectTemplates(projectTemplates_);
    registerDefaultSettings(workspaceSettings_);
    initialization_.reset();
}

WorkspaceRuntime::~WorkspaceRuntime() {
    close();
}

bool WorkspaceRuntime::open(const std::filesystem::path& workspacePath, std::string* errorMessage) {
    initialization_.reset();
    initialization_.start(WorkspaceInitializationStage::WorkspaceOpened, "Opening workspace");
    capabilities_.clear();
    indexes_.clearSnapshots();
    jobs_.clear();
    agentOperationJournal_.clear();
    const JobId openJob = jobs_.start(JobKind::Initialization, "Open workspace");
    workspace_.bindFileSystem(vfs_);
    std::error_code statusError;
    const bool openAsFile = std::filesystem::is_regular_file(workspacePath, statusError);
    std::filesystem::path rootPath = openAsFile ? workspacePath.parent_path() : workspacePath;
    if (rootPath.empty()) {
        rootPath = std::filesystem::current_path();
    }
    if (!workspace_.open(rootPath, errorMessage)) {
        initialization_.fail(WorkspaceInitializationStage::WorkspaceOpened, errorMessage == nullptr ? "Workspace open failed" : *errorMessage);
        jobs_.complete(openJob, false, "Workspace open failed");
        return false;
    }
    initialization_.complete(WorkspaceInitializationStage::WorkspaceOpened, "Workspace opened", Json::object({
        {"root", Json(workspace_.info().rootPath.string())},
        {"singleFile", Json(openAsFile)},
    }));
    jobs_.updateProgress(openJob, 0.3, "Workspace opened");
    if (openAsFile) {
        projectManager_.setSingleFile(workspace_.file(workspacePath));
    } else {
        projectManager_.clearSingleFile();
    }

    workspaceSettings_.load({.kind = SettingScopeKind::Workspace, .qualifier = workspace_.info().rootPath.string()}, workspace_.info().rootPath / ".vanta" / "settings.json");
    projectStateStore_.load(workspace_.info().rootPath / ".vanta" / "state.json", &projectState_);
    pluginStorage_.setRoot(workspace_.info().rootPath / ".vanta" / "plugin-storage");
    registerDefaultExecutionProviders(execution_);
    registerDefaultRunConfigurationProviders(runConfigurations_);
    bindBuiltinComponents();
    context_.setProject(&project_);
    project_.components().attachAll(context_);
    project_.components().restoreAll(projectState_);
    initialization_.complete(WorkspaceInitializationStage::ComponentsReady, "Components attached");
    jobs_.updateProgress(openJob, 0.5, "Components attached");
    refreshIndexes("Initial workspace index");
    agentContext_.clearProviders();
    registerDefaultAgentContextProviders(agentContext_);
    initialization_.complete(WorkspaceInitializationStage::AgentContextReady, "Agent context providers ready");
    initialization_.complete(WorkspaceInitializationStage::LanguageServicesReady, "Language registry ready", Json::object({
        {"languages", Json(static_cast<std::int64_t>(languages_.languageIds().size()))},
    }));
    initialization_.complete(WorkspaceInitializationStage::BuildModelReady, "Build providers ready", Json::object({
        {"providers", Json(static_cast<std::int64_t>(build_.buildProviderIds().size()))},
    }));
    connectEventRelays();
    open_ = true;
    ui_.attach(*this);
    updateCoreCapabilities();
    jobs_.complete(openJob, true, "Workspace open completed");
    publish({
        .kind = IdeEventKind::WorkspaceOpened,
        .file = workspace_.rootFile(),
    });
    return true;
}

void WorkspaceRuntime::close() {
    if (!open_) {
        return;
    }
    stopFileWatcher();
    stopDocumentSync();
    const VirtualFile root = workspace_.rootFile();
    project_.components().closeProject(project_);
    layoutComponent().capture(ui_.state());
    projectState_ = project_.components().saveAll(projectState_);
    workspaceSettings_.save({.kind = SettingScopeKind::Workspace, .qualifier = workspace_.info().rootPath.string()}, workspace_.info().rootPath / ".vanta" / "settings.json");
    projectStateStore_.save(workspace_.info().rootPath / ".vanta" / "state.json", projectState_);
    open_ = false;
    publish({
        .kind = IdeEventKind::WorkspaceClosed,
        .file = root,
    });
    ui_.detach();
    disconnectEventRelays();
    project_.components().detachAll();
    context_.setProject(nullptr);
    indexes_.clearSnapshots();
    capabilities_.clear();
    jobs_.clear();
}

bool WorkspaceRuntime::isOpen() const {
    return open_;
}

void WorkspaceRuntime::refreshProject() {
    const JobId job = jobs_.start(JobKind::Initialization, "Refresh project");
    initialization_.start(WorkspaceInitializationStage::ProjectModelResolved, "Refreshing project model");
    project_.setModel(projectManager_.refresh(context_));
    initialization_.complete(WorkspaceInitializationStage::ProjectModelResolved, "Project model ready", Json::object({
        {"type", Json(primaryProjectType(project_.model()))},
        {"origin", Json(toString(project_.model().origin))},
    }));
    jobs_.updateProgress(job, 0.4, "Project model ready");
    refreshIndexes("Refresh project index");
    reconcileComponentContributions();
    project_.components().openProject(project_);
    initialization_.complete(WorkspaceInitializationStage::ComponentsReady, "Project components ready");
    initialization_.complete(WorkspaceInitializationStage::BuildModelReady, "Build model ready", Json::object({
        {"providers", Json(static_cast<std::int64_t>(build_.buildProviderIds().size()))},
    }));
    updateCoreCapabilities();
    jobs_.complete(job, true, "Project refresh completed");
    publish({
        .kind = IdeEventKind::ProjectChanged,
        .file = workspace_.rootFile(),
    });
}

void WorkspaceRuntime::startDocumentSync() {
    if (documentSync_ != nullptr) {
        return;
    }
    documentSync_ = std::make_unique<DocumentLanguageSynchronizer>(documents_, languages_);
    documentSync_->start();
}

void WorkspaceRuntime::stopDocumentSync() {
    if (documentSync_ == nullptr) {
        return;
    }
    documentSync_->stop();
    documentSync_.reset();
}

bool WorkspaceRuntime::startFileWatcher(std::string* errorMessage) {
    if (fileWatcher_ != nullptr && fileWatcher_->running()) {
        return true;
    }
    fileWatcher_ = createPlatformFileWatcher(vfs_);
    return fileWatcher_->start(workspace_.rootFile(), [this](const VirtualFileChangeEvent& event) {
        async_.postMain([this, event] {
            handleFileChange(event);
        });
    }, errorMessage);
}

void WorkspaceRuntime::stopFileWatcher() {
    if (fileWatcher_ == nullptr) {
        return;
    }
    fileWatcher_->stop();
    fileWatcher_.reset();
}

void WorkspaceRuntime::bindComponent(std::unique_ptr<Component> component) {
    project_.components().rememberState(projectState_);
    project_.components().bind(std::move(component));
}

bool WorkspaceRuntime::unbindComponent(const std::string& id) {
    if (Component* component = project_.components().get(id)) {
        try {
            projectState_.componentStates[id] = component->saveState();
        } catch (...) {
        }
    }
    return project_.components().unbind(id);
}

void WorkspaceRuntime::addComponentContribution(ComponentContribution contribution) {
    componentContributions_.add(std::move(contribution));
    if (open_) {
        reconcileComponentContributions();
    }
}

bool WorkspaceRuntime::removeComponentContribution(const std::string& id) {
    const bool removed = componentContributions_.remove(id);
    if (removed && open_) {
        reconcileComponentContributions();
    }
    return removed;
}

std::vector<ComponentContribution> WorkspaceRuntime::componentContributions() const {
    return componentContributions_.list();
}

ProjectManager& WorkspaceRuntime::projectManager() {
    return projectManager_;
}

const ProjectManager& WorkspaceRuntime::projectManager() const {
    return projectManager_;
}

Workspace& WorkspaceRuntime::workspace() {
    return workspace_;
}

const Workspace& WorkspaceRuntime::workspace() const {
    return workspace_;
}

Project& WorkspaceRuntime::project() {
    return project_;
}

const Project& WorkspaceRuntime::project() const {
    return project_;
}

DocumentService& WorkspaceRuntime::documents() {
    return documents_;
}

const DocumentService& WorkspaceRuntime::documents() const {
    return documents_;
}

EditorWorkspace& WorkspaceRuntime::editor() {
    return editor_;
}

const EditorWorkspace& WorkspaceRuntime::editor() const {
    return editor_;
}

BuildService& WorkspaceRuntime::build() {
    return build_;
}

const BuildService& WorkspaceRuntime::build() const {
    return build_;
}

AgentToolRegistry& WorkspaceRuntime::agent() {
    return agent_;
}

const AgentToolRegistry& WorkspaceRuntime::agent() const {
    return agent_;
}

AgentContextCollector& WorkspaceRuntime::agentContext() {
    return agentContext_;
}

AgentOperationService& WorkspaceRuntime::agentOperations() {
    return agentOperations_;
}

const AgentOperationService& WorkspaceRuntime::agentOperations() const {
    return agentOperations_;
}

AgentOperationJournal& WorkspaceRuntime::agentOperationJournal() {
    return agentOperationJournal_;
}

const AgentOperationJournal& WorkspaceRuntime::agentOperationJournal() const {
    return agentOperationJournal_;
}

ChangeSetService& WorkspaceRuntime::changes() {
    return changes_;
}

ExecutionService& WorkspaceRuntime::execution() {
    return execution_;
}

const ExecutionService& WorkspaceRuntime::execution() const {
    return execution_;
}

GitClient& WorkspaceRuntime::git() {
    return git_;
}

const GitClient& WorkspaceRuntime::git() const {
    return git_;
}

RunConfigurationService& WorkspaceRuntime::runConfigurations() {
    return runConfigurations_;
}

const RunConfigurationService& WorkspaceRuntime::runConfigurations() const {
    return runConfigurations_;
}

DefaultLanguageRegistry& WorkspaceRuntime::languages() {
    return languages_;
}

const DefaultLanguageRegistry& WorkspaceRuntime::languages() const {
    return languages_;
}

LanguageRequestPipeline& WorkspaceRuntime::languageRequests() {
    return languageRequests_;
}

CodeIntelligenceService& WorkspaceRuntime::codeIntelligence() {
    return codeIntelligence_;
}

DiagnosticService& WorkspaceRuntime::diagnostics() {
    return diagnostics_;
}

const DiagnosticService& WorkspaceRuntime::diagnostics() const {
    return diagnostics_;
}

JobService& WorkspaceRuntime::jobs() {
    return jobs_;
}

const JobService& WorkspaceRuntime::jobs() const {
    return jobs_;
}

DefaultCommandRegistry& WorkspaceRuntime::commands() {
    return commands_;
}

const DefaultCommandRegistry& WorkspaceRuntime::commands() const {
    return commands_;
}

KeybindingRegistry& WorkspaceRuntime::keybindings() {
    return keybindings_;
}

CommandPalette& WorkspaceRuntime::commandPalette() {
    return commandPalette_;
}

CppSemanticIndex& WorkspaceRuntime::cppIndex() {
    return cppIndex_;
}

SearchService& WorkspaceRuntime::search() {
    return search_;
}

const SearchService& WorkspaceRuntime::search() const {
    return search_;
}

IndexCoordinator& WorkspaceRuntime::indexes() {
    return indexes_;
}

const IndexCoordinator& WorkspaceRuntime::indexes() const {
    return indexes_;
}

CapabilityRegistry& WorkspaceRuntime::capabilities() {
    return capabilities_;
}

const CapabilityRegistry& WorkspaceRuntime::capabilities() const {
    return capabilities_;
}

WorkspaceInitializationPipeline& WorkspaceRuntime::initialization() {
    return initialization_;
}

const WorkspaceInitializationPipeline& WorkspaceRuntime::initialization() const {
    return initialization_;
}

ProjectTemplateService& WorkspaceRuntime::projectTemplates() {
    return projectTemplates_;
}

const ProjectTemplateService& WorkspaceRuntime::projectTemplates() const {
    return projectTemplates_;
}

ScratchFileService& WorkspaceRuntime::scratchFiles() {
    return scratchFiles_;
}

const ScratchFileService& WorkspaceRuntime::scratchFiles() const {
    return scratchFiles_;
}

ApprovalService& WorkspaceRuntime::approvals() {
    return approvals_;
}

const ApprovalService& WorkspaceRuntime::approvals() const {
    return approvals_;
}

SettingsService& WorkspaceRuntime::workspaceSettings() {
    return workspaceSettings_;
}

PluginStorageService& WorkspaceRuntime::pluginStorage() {
    return pluginStorage_;
}

ContributionRegistry& WorkspaceRuntime::contributions() {
    return contributions_;
}

AsyncRuntime& WorkspaceRuntime::async() {
    return async_;
}

UiStateStore& WorkspaceRuntime::ui() {
    return ui_;
}

const UiStateStore& WorkspaceRuntime::ui() const {
    return ui_;
}

WorkspaceContext& WorkspaceRuntime::context() {
    return context_;
}

const WorkspaceContext& WorkspaceRuntime::context() const {
    return context_;
}

std::uint64_t WorkspaceRuntime::onEvent(IdeEventBus::Listener listener) {
    return events_.subscribe(std::move(listener));
}

void WorkspaceRuntime::removeEventListener(std::uint64_t listenerId) {
    events_.unsubscribe(listenerId);
}

IdeEventBus& WorkspaceRuntime::events() {
    return events_;
}

void WorkspaceRuntime::publish(IdeEvent event) {
    events_.publish(event);
}

LayoutStateStore& WorkspaceRuntime::layoutComponent() {
    auto* layout = project_.getComponent<LayoutStateStore>(LayoutStateStore::componentId);
    if (layout == nullptr) {
        project_.components().bind(std::make_unique<LayoutStateStore>());
        layout = project_.getComponent<LayoutStateStore>(LayoutStateStore::componentId);
    }
    return *layout;
}

const LayoutStateStore& WorkspaceRuntime::layoutComponent() const {
    const auto* layout = project_.getComponent<LayoutStateStore>(LayoutStateStore::componentId);
    return *layout;
}

void WorkspaceRuntime::bindBuiltinComponents() {
    if (project_.getComponent(LayoutStateStore::componentId) == nullptr) {
        project_.components().bind(std::make_unique<LayoutStateStore>());
    }
    if (project_.getComponent(RunConfigurationComponent::componentId) == nullptr) {
        project_.components().bind(std::make_unique<RunConfigurationComponent>());
    }
}

void WorkspaceRuntime::refreshIndexes(std::string title) {
    initialization_.start(WorkspaceInitializationStage::FileIndexReady, "Refreshing indexes");
    const JobId jobId = indexes_.refresh(context_, jobs_, async_, std::move(title));
    jobs_.wait(jobId);
    initialization_.complete(WorkspaceInitializationStage::FileIndexReady, "File index ready", Json::object({
        {"snapshots", toJson(indexes_.snapshots())},
    }));
    updateCoreCapabilities();
}

void WorkspaceRuntime::updateCoreCapabilities() {
    capabilities_.set({
        .id = "workspace.open",
        .title = "Workspace Open",
        .providerId = "vanta.core",
        .status = open_ ? CapabilityStatus::Available : CapabilityStatus::Unavailable,
        .message = open_ ? "Workspace is open" : "Workspace is closed",
        .data = Json::object({
            {"root", Json(workspace_.info().rootPath.string())},
        }),
    });
    capabilities_.set({
        .id = "project.model",
        .title = "Project Model",
        .providerId = "vanta.core",
        .status = projectManager_.hasProject() ? CapabilityStatus::Available : CapabilityStatus::Degraded,
        .message = projectManager_.hasProject() ? "Project model has facets or attachments" : "Generic project model is available",
        .data = Json::object({
            {"type", Json(primaryProjectType(project_.model()))},
            {"origin", Json(toString(project_.model().origin))},
        }),
    });
    capabilities_.set({
        .id = "index.workspace",
        .title = "Workspace Index",
        .providerId = "vanta.core",
        .status = indexes_.snapshots().empty() ? CapabilityStatus::Unavailable : CapabilityStatus::Available,
        .message = indexes_.snapshots().empty() ? "No index snapshots are available" : "Index snapshots are ready",
        .data = Json::object({
            {"snapshots", toJson(indexes_.snapshots())},
        }),
    });
    capabilities_.set({
        .id = "language.registry",
        .title = "Language Registry",
        .providerId = "vanta.core",
        .status = CapabilityStatus::Available,
        .message = "Language registry is available",
        .data = Json::object({
            {"languageIds", stringsToJson(languages_.languageIds())},
        }),
    });
    capabilities_.set({
        .id = "build.providers",
        .title = "Build Providers",
        .providerId = "vanta.core",
        .status = build_.buildProviderIds().empty() ? CapabilityStatus::Degraded : CapabilityStatus::Available,
        .message = build_.buildProviderIds().empty() ? "No build provider is registered" : "Build providers are registered",
        .data = Json::object({
            {"providerIds", stringsToJson(build_.buildProviderIds())},
        }),
    });
    capabilities_.set({
        .id = "agent.operations",
        .title = "Agent Operations",
        .providerId = "vanta.core",
        .status = CapabilityStatus::Available,
        .message = "Agent operation protocol is available",
        .data = Json::object({
            {"records", Json(static_cast<std::int64_t>(agentOperationJournal_.records().size()))},
        }),
    });
}

void WorkspaceRuntime::reconcileComponentContributions() {
    const ProjectModel& model = project_.model();
    for (auto it = activeContributedComponents_.begin(); it != activeContributedComponents_.end();) {
        const auto contribution = componentContributions_.contribution(it->first);
        if (!contribution || !contribution->match.matches(model)) {
            unbindComponent(it->first);
            it = activeContributedComponents_.erase(it);
        } else {
            ++it;
        }
    }

    for (const ComponentContribution& contribution : componentContributions_.matching(model)) {
        if (activeContributedComponents_.contains(contribution.id) || project_.getComponent(contribution.id) != nullptr) {
            continue;
        }
        std::unique_ptr<Component> component = contribution.factory ? contribution.factory() : nullptr;
        if (component == nullptr) {
            continue;
        }
        bindComponent(std::move(component));
        activeContributedComponents_[contribution.id] = true;
    }
}

void WorkspaceRuntime::connectEventRelays() {
    if (documentListener_ == 0) {
        documentListener_ = documents_.onDidChangeDocument([this](const DocumentChangeEvent& event) {
            publishDocumentEvent(event);
        });
    }
    if (diagnosticListener_ == 0) {
        diagnosticListener_ = diagnostics_.onDidChangeDiagnostics([this](const DiagnosticChangeEvent& event) {
            publish({
                .kind = IdeEventKind::DiagnosticsChanged,
                .source = event.source,
            });
        });
    }
    if (jobListener_ == 0) {
        jobListener_ = jobs_.onDidChangeJob([this](const JobChangeEvent& event) {
            publishJobEvent(event);
        });
    }
}

void WorkspaceRuntime::disconnectEventRelays() {
    if (documentListener_ != 0) {
        documents_.removeDocumentListener(documentListener_);
        documentListener_ = 0;
    }
    if (diagnosticListener_ != 0) {
        diagnostics_.removeDiagnosticsListener(diagnosticListener_);
        diagnosticListener_ = 0;
    }
    if (jobListener_ != 0) {
        jobs_.removeJobListener(jobListener_);
        jobListener_ = 0;
    }
    jobStatuses_.clear();
}

void WorkspaceRuntime::handleFileChange(const VirtualFileChangeEvent& event) {
    if (!open_) {
        return;
    }
    workspace_.refreshFileTree();
    publish({
        .kind = ideEventKindFromFileChange(event.kind),
        .file = event.file,
        .message = toString(event.kind),
    });
    refreshProject();
}

void WorkspaceRuntime::publishDocumentEvent(const DocumentChangeEvent& event) {
    publish({
        .kind = kindFromDocumentChange(event.kind),
        .file = event.file,
    });
}

void WorkspaceRuntime::publishJobEvent(const JobChangeEvent& event) {
    const auto previous = jobStatuses_.find(event.job.id);
    const bool firstStatus = previous == jobStatuses_.end();
    const JobStatus oldStatus = firstStatus ? JobStatus::Pending : previous->second;
    jobStatuses_[event.job.id] = event.job.status;

    if (event.job.status == JobStatus::Running && oldStatus != JobStatus::Running) {
        publish({
            .kind = IdeEventKind::JobStarted,
            .source = toString(event.job.kind),
            .message = event.job.title,
            .jobId = event.job.id,
        });
    } else if (isTerminalStatus(event.job.status) && oldStatus != event.job.status) {
        publish({
            .kind = IdeEventKind::JobCompleted,
            .source = toString(event.job.kind),
            .message = event.job.title,
            .jobId = event.job.id,
        });
    }
}

}
