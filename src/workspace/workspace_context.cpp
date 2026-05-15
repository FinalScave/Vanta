#include "vanta/workspace/workspace_context.h"

#include <utility>

#include "vanta/builtin/cmake/cmake_project_model.h"
#include "vanta/workspace/workspace_runtime.h"
#include "vanta/project/project.h"
#include "vanta/project/project_manager.h"

namespace vanta {
namespace {

JobKind jobKindForBuildTask(BuildTaskKind kind) {
    return kind == BuildTaskKind::Build ? JobKind::Build : JobKind::Test;
}

std::string buildJobTitle(const BuildTask& task) {
    std::string title = toString(task.kind);
    if (!task.target.empty()) {
        title += ": " + task.target;
    }
    return title;
}

std::string executionJobTitle(const ExecutionRequest& request) {
    return request.executable.empty() ? "Run" : "Run " + request.executable;
}

}

WorkspaceContext::WorkspaceContext(WorkspaceRuntime& runtime)
    : runtime_(&runtime), project_(&runtime.project()) {
    registerServices();
}

void WorkspaceContext::setProject(Project* project) {
    project_ = project;
}

WorkspaceRuntime* WorkspaceContext::runtime() {
    return runtime_;
}

const WorkspaceRuntime* WorkspaceContext::runtime() const {
    return runtime_;
}

Workspace& WorkspaceContext::workspace() {
    if (runtime_ == nullptr) {
        throw std::runtime_error("Workspace runtime is not attached");
    }
    return runtime_->workspace();
}

const Workspace& WorkspaceContext::workspace() const {
    if (runtime_ == nullptr) {
        throw std::runtime_error("Workspace runtime is not attached");
    }
    return runtime_->workspace();
}

Project* WorkspaceContext::project() {
    return project_;
}

const Project* WorkspaceContext::project() const {
    return project_;
}

Project& WorkspaceContext::requireProject() {
    if (project_ == nullptr) {
        throw std::runtime_error("Project is not attached");
    }
    return *project_;
}

const Project& WorkspaceContext::requireProject() const {
    if (project_ == nullptr) {
        throw std::runtime_error("Project is not attached");
    }
    return *project_;
}

DocumentService& WorkspaceContext::documents() {
    return runtime_->documents();
}

BuildService& WorkspaceContext::build() {
    return *this;
}

SearchService& WorkspaceContext::search() {
    return runtime_->search();
}

CppSemanticIndex& WorkspaceContext::cppIndex() {
    return runtime_->cppIndex();
}

LanguageRequestPipeline& WorkspaceContext::languageRequests() {
    return runtime_->languageRequests();
}

CodeIntelligenceService& WorkspaceContext::codeIntelligence() {
    return runtime_->codeIntelligence();
}

LanguageRegistry& WorkspaceContext::languages() {
    return *this;
}

DiagnosticService& WorkspaceContext::diagnostics() {
    return runtime_->diagnostics();
}

JobService& WorkspaceContext::jobs() {
    return runtime_->jobs();
}

CommandRegistry& WorkspaceContext::commands() {
    return *this;
}

ContributionRegistry& WorkspaceContext::contributions() {
    return runtime_->contributions();
}

AgentToolRegistry& WorkspaceContext::agent() {
    return runtime_->agent();
}

AgentContextProviders& WorkspaceContext::agentContext() {
    return *this;
}

AgentOperationService& WorkspaceContext::agentOperations() {
    return runtime_->agentOperations();
}

AgentOperationJournal& WorkspaceContext::agentOperationJournal() {
    return runtime_->agentOperationJournal();
}

ChangeSetService& WorkspaceContext::changes() {
    return runtime_->changes();
}

ExecutionHost& WorkspaceContext::execution() {
    return *this;
}

RunConfigurationService& WorkspaceContext::runConfigurations() {
    return runtime_->runConfigurations();
}

RunConfigurationRegistry& WorkspaceContext::runConfigurationRegistry() {
    return *this;
}

IndexCoordinator& WorkspaceContext::indexes() {
    return runtime_->indexes();
}

CapabilityRegistry& WorkspaceContext::capabilities() {
    return runtime_->capabilities();
}

WorkspaceInitializationPipeline& WorkspaceContext::initialization() {
    return runtime_->initialization();
}

ProjectTemplateService& WorkspaceContext::projectTemplates() {
    return runtime_->projectTemplates();
}

ScratchFileService& WorkspaceContext::scratchFiles() {
    return runtime_->scratchFiles();
}

ApprovalService& WorkspaceContext::approvals() {
    return runtime_->approvals();
}

SettingsService& WorkspaceContext::settings() {
    return runtime_->workspaceSettings();
}

IdeEventBus& WorkspaceContext::eventBus() {
    return runtime_->events();
}

void WorkspaceContext::publish(IdeEvent event) {
    runtime_->publish(std::move(event));
}

RegistrationHandle WorkspaceContext::onEvent(IdeEventBus::Listener listener) {
    const std::uint64_t id = runtime_->onEvent(std::move(listener));
    WorkspaceRuntime* runtime = runtime_;
    return RegistrationHandle([runtime, id] {
        runtime->removeEventListener(id);
    });
}

RegistrationHandle WorkspaceContext::onEvent(IdeEventKind kind, IdeEventBus::Listener listener) {
    return onEvent([kind, listener = std::move(listener)](const IdeEvent& event) {
        if (event.kind == kind) {
            listener(event);
        }
    });
}

std::uint64_t WorkspaceContext::onEvent(const Component& owner, IdeEventBus::Listener listener) {
    const std::uint64_t id = eventBus().subscribe(std::move(listener));
    eventSubscriptions_[owner.id()].push_back(id);
    return id;
}

std::uint64_t WorkspaceContext::onEvent(const Component& owner, IdeEventKind kind, IdeEventBus::Listener listener) {
    return onEvent(owner, [kind, listener = std::move(listener)](const IdeEvent& event) {
        if (event.kind == kind) {
            listener(event);
        }
    });
}

void WorkspaceContext::removeEventSubscriptions(const std::string& componentId) {
    auto it = eventSubscriptions_.find(componentId);
    if (it == eventSubscriptions_.end() || runtime_ == nullptr) {
        return;
    }
    for (std::uint64_t listenerId : it->second) {
        runtime_->events().unsubscribe(listenerId);
    }
    eventSubscriptions_.erase(it);
}

WorkspaceEvents& WorkspaceContext::events() {
    return *this;
}

WorkspaceFiles& WorkspaceContext::workspaceFiles() {
    return *this;
}

ProjectComponents& WorkspaceContext::components() {
    return *this;
}

ProjectModelProviderRegistry& WorkspaceContext::projectModels() {
    return *this;
}

AgentTools& WorkspaceContext::agentTools() {
    return *this;
}

GitReader& WorkspaceContext::git() {
    return *this;
}

void WorkspaceContext::add(const std::string& id, CommandHandler handler) {
    runtime_->commands().add(id, std::move(handler));
}

RegistrationHandle WorkspaceContext::registerCommand(const std::string& id, CommandHandler handler) {
    return runtime_->commands().registerCommand(id, std::move(handler));
}

std::optional<Json> WorkspaceContext::execute(const std::string& id, const Json& arguments) const {
    return runtime_->commands().execute(id, arguments);
}

std::vector<std::string> WorkspaceContext::list() const {
    return runtime_->commands().list();
}

RegistrationHandle WorkspaceContext::subscribe(IdeEventBus::Listener listener) {
    return onEvent(std::move(listener));
}

RegistrationHandle WorkspaceContext::subscribe(IdeEventKind kind, IdeEventBus::Listener listener) {
    return onEvent(kind, std::move(listener));
}

const WorkspaceInfo& WorkspaceContext::info() const {
    return workspace().info();
}

std::optional<std::string> WorkspaceContext::readTextFile(const VirtualFile& file) const {
    return file.readText();
}

bool WorkspaceContext::writeTextFile(const VirtualFile& file, const std::string& text, std::string* errorMessage) {
    const bool ok = file.writeText(text, errorMessage);
    if (ok) {
        runtime_->workspace().refreshFileTree();
    }
    return ok;
}

void WorkspaceContext::addLanguage(Language language) {
    runtime_->languages().addLanguage(std::move(language));
}

RegistrationHandle WorkspaceContext::registerLanguage(Language language) {
    return runtime_->languages().registerLanguage(std::move(language));
}

std::vector<Language> WorkspaceContext::languages() const {
    return runtime_->languages().languages();
}

const Language* WorkspaceContext::languageForFile(const VirtualFile& file) const {
    return runtime_->languages().languageForFile(file);
}

const Language* WorkspaceContext::languageForFile(const VirtualFile& file, const LanguageResolutionContext& context) const {
    return runtime_->languages().languageForFile(file, context);
}

const Language* WorkspaceContext::languageForId(const std::string& languageId) const {
    return runtime_->languages().languageForId(languageId);
}

const Language* WorkspaceContext::languageForId(const std::string& languageId, const LanguageResolutionContext& context) const {
    return runtime_->languages().languageForId(languageId, context);
}

LanguageService* WorkspaceContext::serviceForLanguage(const std::string& languageId) const {
    return runtime_->languages().serviceForLanguage(languageId);
}

LanguageService* WorkspaceContext::serviceForLanguage(const std::string& languageId, const LanguageResolutionContext& context) const {
    return runtime_->languages().serviceForLanguage(languageId, context);
}

LanguageService* WorkspaceContext::serviceForDocument(const VirtualFile& file) const {
    return runtime_->languages().serviceForDocument(file);
}

LanguageService* WorkspaceContext::serviceForDocument(const VirtualFile& file, const LanguageResolutionContext& context) const {
    return runtime_->languages().serviceForDocument(file, context);
}

std::string WorkspaceContext::languageIdForFile(const VirtualFile& file) const {
    return runtime_->languages().languageIdForFile(file);
}

std::string WorkspaceContext::languageIdForFile(const VirtualFile& file, const LanguageResolutionContext& context) const {
    return runtime_->languages().languageIdForFile(file, context);
}

std::vector<std::string> WorkspaceContext::languageIds() const {
    return runtime_->languages().languageIds();
}

void WorkspaceContext::addProvider(std::unique_ptr<BuildProvider> provider) {
    runtime_->build().addProvider(std::move(provider));
}

RegistrationHandle WorkspaceContext::registerProvider(std::unique_ptr<BuildProvider> provider) {
    return runtime_->build().registerProvider(std::move(provider));
}

void WorkspaceContext::removeProvider(const std::string& providerId) {
    runtime_->build().removeProvider(providerId);
}

std::vector<std::string> WorkspaceContext::buildProviderIds() const {
    return runtime_->build().buildProviderIds();
}

BuildEnvironment WorkspaceContext::detect(const std::filesystem::path& workspaceRoot) const {
    BuildEnvironment environment = runtime_->build().detect(workspaceRoot);
    if (environment.buildDirectory.empty()) {
        if (const auto* cmake = runtime_->project().model().attachment<CMakeProjectModel>(CMakeProjectModel::attachmentId)) {
            environment.buildDirectory = cmake->buildDirectory;
        }
    }
    return environment;
}

BuildHandle WorkspaceContext::start(WorkspaceContext& context, const std::filesystem::path& workspaceRoot, const BuildTask& task, ExecutionEventCallback onEvent) const {
    BuildTask resolvedTask = resolveBuildTask(task);
    if (resolvedTask.jobId == 0) {
        resolvedTask.jobId = context.jobs().start(jobKindForBuildTask(resolvedTask.kind), buildJobTitle(resolvedTask));
    } else {
        context.jobs().markRunning(resolvedTask.jobId);
    }
    BuildHandle handle = runtime_->build().start(context, workspaceRoot, resolvedTask, std::move(onEvent));
    if (!handle.valid()) {
        context.jobs().complete(resolvedTask.jobId, false, "Build did not start");
    }
    if (auto result = handle.result(); result && result->events.empty()) {
        if (!result->output.empty()) {
            context.jobs().appendOutput(resolvedTask.jobId, result->output);
        }
        context.jobs().complete(resolvedTask.jobId, result->exitCode == 0);
    }
    return handle;
}

BuildResult WorkspaceContext::run(WorkspaceContext& context, const std::filesystem::path& workspaceRoot, const BuildTask& task, ExecutionEventCallback onEvent) const {
    BuildHandle handle = start(context, workspaceRoot, task, std::move(onEvent));
    return handle.wait();
}

BuildHandle WorkspaceContext::start(const std::filesystem::path& workspaceRoot, const BuildTask& task, ExecutionEventCallback onEvent) const {
    return start(const_cast<WorkspaceContext&>(*this), workspaceRoot, task, std::move(onEvent));
}

BuildResult WorkspaceContext::run(const std::filesystem::path& workspaceRoot, const BuildTask& task, ExecutionEventCallback onEvent) const {
    return run(const_cast<WorkspaceContext&>(*this), workspaceRoot, task, std::move(onEvent));
}

void WorkspaceContext::contribute(ComponentContribution contribution) {
    runtime_->addComponentContribution(std::move(contribution));
}

RegistrationHandle WorkspaceContext::registerContribution(ComponentContribution contribution) {
    if (contribution.id.empty()) {
        return {};
    }
    const std::string id = contribution.id;
    runtime_->addComponentContribution(std::move(contribution));
    return RegistrationHandle([this, id] {
        runtime_->removeComponentContribution(id);
    });
}

std::vector<ComponentContribution> WorkspaceContext::contributions() const {
    return runtime_->componentContributions();
}

void WorkspaceContext::addProvider(std::unique_ptr<ProjectModelProvider> provider) {
    runtime_->projectManager().addProvider(std::move(provider));
}

RegistrationHandle WorkspaceContext::registerProvider(std::unique_ptr<ProjectModelProvider> provider) {
    if (provider == nullptr || provider->id().empty()) {
        return {};
    }
    const std::string id = provider->id();
    runtime_->projectManager().addProvider(std::move(provider));
    return RegistrationHandle([this, id] {
        runtime_->projectManager().removeProvider(id);
    });
}

std::vector<std::string> WorkspaceContext::modelProviderIds() const {
    return runtime_->projectManager().providerIds();
}

void WorkspaceContext::addTool(AgentToolDefinition definition) {
    runtime_->agent().addTool(std::move(definition));
}

RegistrationHandle WorkspaceContext::registerTool(AgentToolDefinition definition) {
    const std::string id = definition.id;
    runtime_->agent().addTool(std::move(definition));
    return RegistrationHandle([this, id] {
        runtime_->agent().removeTool(id);
    });
}

void WorkspaceContext::addProvider(std::unique_ptr<AgentContextProvider> provider) {
    runtime_->agentContext().addProvider(std::move(provider));
}

RegistrationHandle WorkspaceContext::registerProvider(std::unique_ptr<AgentContextProvider> provider) {
    if (provider == nullptr || provider->id().empty()) {
        return {};
    }
    const std::string id = provider->id();
    runtime_->agentContext().addProvider(std::move(provider));
    return RegistrationHandle([this, id] {
        runtime_->agentContext().removeProvider(id);
    });
}

AgentContext WorkspaceContext::collect(const AgentContextRequest& request) const {
    return runtime_->agentContext().collect(request, runtime_->context());
}

void WorkspaceContext::addProvider(std::unique_ptr<ExecutionProvider> provider) {
    runtime_->execution().addProvider(std::move(provider));
}

RegistrationHandle WorkspaceContext::registerProvider(std::unique_ptr<ExecutionProvider> provider) {
    if (provider == nullptr || provider->id().empty()) {
        return {};
    }
    const std::string id = provider->id();
    runtime_->execution().addProvider(std::move(provider));
    return RegistrationHandle([this, id] {
        runtime_->execution().removeProvider(id);
    });
}

std::vector<std::string> WorkspaceContext::providerIds() const {
    return runtime_->execution().providerIds();
}

std::vector<ExecutionTarget> WorkspaceContext::targets() const {
    return runtime_->execution().targets(runtime_->context());
}

ExecutionHandle WorkspaceContext::start(const ExecutionRequest& request, const ExecutionTarget& target, ExecutionEventCallback onEvent) const {
    ExecutionRequest resolvedRequest = request;
    if (resolvedRequest.jobId == 0) {
        resolvedRequest.jobId = runtime_->jobs().start(JobKind::Run, executionJobTitle(resolvedRequest));
    } else {
        runtime_->jobs().markRunning(resolvedRequest.jobId);
    }
    ExecutionHandle handle = runtime_->execution().start(runtime_->context(), resolvedRequest, target, [this, onEvent = std::move(onEvent)](const ExecutionEvent& event) {
        runtime_->jobs().applyExecutionEvent(event);
        if (onEvent) {
            onEvent(event);
        }
    });
    if (!handle.valid()) {
        runtime_->jobs().complete(resolvedRequest.jobId, false, "Execution did not start");
    } else {
        runtime_->jobs().setCancelHandler(resolvedRequest.jobId, [handle]() mutable {
            handle.cancel();
        });
    }
    if (auto result = handle.result(); result && result->events.empty()) {
        if (!result->output.empty()) {
            runtime_->jobs().appendOutput(resolvedRequest.jobId, result->output);
        }
        runtime_->jobs().complete(resolvedRequest.jobId, result->exitCode == 0);
    }
    return handle;
}

ExecutionResult WorkspaceContext::execute(const ExecutionRequest& request, const ExecutionTarget& target, ExecutionEventCallback onEvent) const {
    ExecutionRequest resolvedRequest = request;
    if (resolvedRequest.jobId == 0) {
        resolvedRequest.jobId = runtime_->jobs().start(JobKind::Run, executionJobTitle(resolvedRequest));
    } else {
        runtime_->jobs().markRunning(resolvedRequest.jobId);
    }
    return start(resolvedRequest, target, std::move(onEvent)).wait();
}

void WorkspaceContext::addType(std::unique_ptr<RunConfigurationType> type) {
    runtime_->runConfigurations().addType(std::move(type));
}

RegistrationHandle WorkspaceContext::registerType(std::unique_ptr<RunConfigurationType> type) {
    if (type == nullptr || type->id().empty()) {
        return {};
    }
    const std::string id = type->id();
    runtime_->runConfigurations().addType(std::move(type));
    return RegistrationHandle([this, id] {
        runtime_->runConfigurations().removeType(id);
    });
}

void WorkspaceContext::addProducer(std::unique_ptr<RunConfigurationProducer> producer) {
    runtime_->runConfigurations().addProducer(std::move(producer));
}

RegistrationHandle WorkspaceContext::registerProducer(std::unique_ptr<RunConfigurationProducer> producer) {
    if (producer == nullptr || producer->id().empty()) {
        return {};
    }
    const std::string id = producer->id();
    runtime_->runConfigurations().addProducer(std::move(producer));
    return RegistrationHandle([this, id] {
        runtime_->runConfigurations().removeProducer(id);
    });
}

void WorkspaceContext::addConfiguration(RunConfiguration configuration) {
    runtime_->runConfigurations().addConfiguration(std::move(configuration));
}

RegistrationHandle WorkspaceContext::registerConfiguration(RunConfiguration configuration) {
    if (configuration.id.empty()) {
        return {};
    }
    const std::string id = configuration.id;
    runtime_->runConfigurations().addConfiguration(std::move(configuration));
    return RegistrationHandle([this, id] {
        runtime_->runConfigurations().removeConfiguration(id);
    });
}

std::vector<RunConfiguration> WorkspaceContext::configurations(bool includeTemporary) const {
    return runtime_->runConfigurations().configurations(includeTemporary);
}

GitDiff WorkspaceContext::diff() const {
    return runtime_->git().diff(workspace().info().rootPath);
}

void WorkspaceContext::registerServices() {
    registry_ = {};
    registry_.add<CommandRegistry>(*this);
    registry_.add<WorkspaceEvents>(*this);
    registry_.add<WorkspaceFiles>(*this);
    registry_.add<LanguageRegistry>(*this);
    registry_.add<BuildService>(*this);
    registry_.add<ProjectComponents>(*this);
    registry_.add<ProjectModelProviderRegistry>(*this);
    registry_.add<AgentTools>(*this);
    registry_.add<AgentContextProviders>(*this);
    registry_.add<AgentOperationService>(runtime_->agentOperations());
    registry_.add<AgentOperationJournal>(runtime_->agentOperationJournal());
    registry_.add<ExecutionHost>(*this);
    registry_.add<CodeIntelligenceService>(runtime_->codeIntelligence());
    registry_.add<RunConfigurationRegistry>(*this);
    registry_.add<GitReader>(*this);
    registry_.add<IndexCoordinator>(runtime_->indexes());
    registry_.add<CapabilityRegistry>(runtime_->capabilities());
    registry_.add<WorkspaceInitializationPipeline>(runtime_->initialization());
    registry_.add<ProjectTemplateService>(runtime_->projectTemplates());
    registry_.add<ScratchFileService>(runtime_->scratchFiles());
    registry_.add<JobService>(runtime_->jobs());
    registry_.add<ApprovalService>(runtime_->approvals());
    registry_.add<SettingsService>(runtime_->workspaceSettings());
}

BuildTask WorkspaceContext::resolveBuildTask(const BuildTask& task) const {
    BuildTask resolvedTask = task;
    if (!resolvedTask.buildDirectory.empty()) {
        return resolvedTask;
    }
    if (const auto* cmake = runtime_->project().model().attachment<CMakeProjectModel>(CMakeProjectModel::attachmentId)) {
        resolvedTask.buildDirectory = cmake->buildDirectory;
    }
    if (resolvedTask.buildDirectory.empty()) {
        resolvedTask.buildDirectory = runtime_->workspace().info().rootPath / "build";
    }
    return resolvedTask;
}

}
