#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "vanta/workspace/workspace_services.h"

namespace vanta {

class AgentToolRegistry;
class AgentContextCollector;
class AgentOperationService;
class AgentOperationJournal;
class ApprovalService;
class BuildService;
class CapabilityRegistry;
class ChangeSetService;
class CodeIntelligenceService;
class CommandRegistry;
class Component;
class ContributionRegistry;
class CppSemanticIndex;
class DiagnosticService;
class DocumentService;
class ExecutionService;
class IndexCoordinator;
class JobService;
class LanguageRequestPipeline;
class DefaultLanguageRegistry;
class Project;
class ProjectTemplateService;
class RunConfigurationService;
class SearchService;
class ScratchFileService;
class SettingsService;
class Workspace;
class WorkspaceInitializationPipeline;
class WorkspaceRuntime;

template <class T>
struct ServiceKey {
    static const void* id() {
        static int value = 0;
        return &value;
    }
};

class ServiceRegistry {
public:
    template <class T>
    void add(T& service) {
        services_[ServiceKey<T>::id()] = &service;
    }

    template <class T>
    T* get() const {
        auto found = services_.find(ServiceKey<T>::id());
        return found == services_.end() ? nullptr : static_cast<T*>(found->second);
    }

    template <class T>
    T& require() const {
        T* service = get<T>();
        if (service == nullptr) {
            throw std::runtime_error("Workspace service is not registered");
        }
        return *service;
    }

private:
    std::map<const void*, void*> services_;
};

class WorkspaceContext
    : public CommandRegistry,
      public WorkspaceEvents,
      public WorkspaceFiles,
      public LanguageRegistry,
      public BuildService,
      public ProjectComponents,
      public ProjectModelProviderRegistry,
      public AgentTools,
      public AgentContextProviders,
      public ExecutionHost,
      public RunConfigurationRegistry,
      public GitReader {
public:
    explicit WorkspaceContext(WorkspaceRuntime& runtime);
    virtual ~WorkspaceContext() = default;

    void setProject(Project* project);
    virtual WorkspaceRuntime* runtime();
    virtual const WorkspaceRuntime* runtime() const;
    virtual Workspace& workspace();
    virtual const Workspace& workspace() const;
    virtual Project* project();
    virtual const Project* project() const;
    virtual Project& requireProject();
    virtual const Project& requireProject() const;
    virtual DocumentService& documents();
    virtual BuildService& build();
    virtual SearchService& search();
    virtual CppSemanticIndex& cppIndex();
    virtual LanguageRequestPipeline& languageRequests();
    virtual CodeIntelligenceService& codeIntelligence();
    virtual LanguageRegistry& languages();
    virtual DiagnosticService& diagnostics();
    virtual JobService& jobs();
    virtual CommandRegistry& commands();
    virtual ContributionRegistry& contributions();
    virtual AgentToolRegistry& agent();
    virtual AgentContextProviders& agentContext();
    virtual AgentOperationService& agentOperations();
    virtual AgentOperationJournal& agentOperationJournal();
    virtual ChangeSetService& changes();
    virtual ExecutionHost& execution();
    virtual RunConfigurationService& runConfigurations();
    virtual RunConfigurationRegistry& runConfigurationRegistry();
    virtual IndexCoordinator& indexes();
    virtual CapabilityRegistry& capabilities();
    virtual WorkspaceInitializationPipeline& initialization();
    virtual ProjectTemplateService& projectTemplates();
    virtual ScratchFileService& scratchFiles();
    virtual ApprovalService& approvals();
    virtual SettingsService& settings();
    virtual IdeEventBus& eventBus();
    void publish(IdeEvent event) override;
    virtual RegistrationHandle onEvent(IdeEventBus::Listener listener);
    virtual RegistrationHandle onEvent(IdeEventKind kind, IdeEventBus::Listener listener);
    std::uint64_t onEvent(const Component& owner, IdeEventBus::Listener listener);
    std::uint64_t onEvent(const Component& owner, IdeEventKind kind, IdeEventBus::Listener listener);
    void removeEventSubscriptions(const std::string& componentId);

    WorkspaceEvents& events();
    WorkspaceFiles& workspaceFiles();
    ProjectComponents& components();
    ProjectModelProviderRegistry& projectModels();
    AgentTools& agentTools();
    GitReader& git();

    void add(const std::string& id, CommandHandler handler) override;
    RegistrationHandle registerCommand(const std::string& id, CommandHandler handler) override;
    std::optional<Json> execute(const std::string& id, const Json& arguments) const override;
    std::vector<std::string> list() const override;

    RegistrationHandle subscribe(IdeEventBus::Listener listener) override;
    RegistrationHandle subscribe(IdeEventKind kind, IdeEventBus::Listener listener) override;

    const WorkspaceInfo& info() const override;
    std::optional<std::string> readTextFile(const VirtualFile& file) const override;
    bool writeTextFile(const VirtualFile& file, const std::string& text, std::string* errorMessage = nullptr) override;

    void addLanguage(Language language) override;
    RegistrationHandle registerLanguage(Language language) override;
    std::vector<Language> languages() const override;
    const Language* languageForFile(const VirtualFile& file) const override;
    const Language* languageForFile(const VirtualFile& file, const LanguageResolutionContext& context) const override;
    const Language* languageForId(const std::string& languageId) const override;
    const Language* languageForId(const std::string& languageId, const LanguageResolutionContext& context) const override;
    LanguageService* serviceForLanguage(const std::string& languageId) const override;
    LanguageService* serviceForLanguage(const std::string& languageId, const LanguageResolutionContext& context) const override;
    LanguageService* serviceForDocument(const VirtualFile& file) const override;
    LanguageService* serviceForDocument(const VirtualFile& file, const LanguageResolutionContext& context) const override;
    std::string languageIdForFile(const VirtualFile& file) const override;
    std::string languageIdForFile(const VirtualFile& file, const LanguageResolutionContext& context) const override;
    std::vector<std::string> languageIds() const override;

    void addProvider(std::unique_ptr<BuildProvider> provider) override;
    RegistrationHandle registerProvider(std::unique_ptr<BuildProvider> provider) override;
    void removeProvider(const std::string& providerId) override;
    std::vector<std::string> buildProviderIds() const override;
    BuildEnvironment detect(const std::filesystem::path& workspaceRoot) const override;
    BuildHandle start(
        WorkspaceContext& context,
        const std::filesystem::path& workspaceRoot,
        const BuildTask& task,
        ExecutionEventCallback onEvent = {}) const override;
    BuildResult run(
        WorkspaceContext& context,
        const std::filesystem::path& workspaceRoot,
        const BuildTask& task,
        ExecutionEventCallback onEvent = {}) const override;
    BuildHandle start(
        const std::filesystem::path& workspaceRoot,
        const BuildTask& task,
        ExecutionEventCallback onEvent = {}) const;
    BuildResult run(
        const std::filesystem::path& workspaceRoot,
        const BuildTask& task,
        ExecutionEventCallback onEvent = {}) const;

    void contribute(ComponentContribution contribution) override;
    RegistrationHandle registerContribution(ComponentContribution contribution) override;
    std::vector<ComponentContribution> contributions() const override;

    void addProvider(std::unique_ptr<ProjectModelProvider> provider) override;
    RegistrationHandle registerProvider(std::unique_ptr<ProjectModelProvider> provider) override;
    std::vector<std::string> modelProviderIds() const override;

    void addTool(AgentToolDefinition definition) override;
    RegistrationHandle registerTool(AgentToolDefinition definition) override;

    void addProvider(std::unique_ptr<AgentContextProvider> provider) override;
    RegistrationHandle registerProvider(std::unique_ptr<AgentContextProvider> provider) override;
    AgentContext collect(const AgentContextRequest& request) const override;

    void addProvider(std::unique_ptr<ExecutionProvider> provider) override;
    RegistrationHandle registerProvider(std::unique_ptr<ExecutionProvider> provider) override;
    std::vector<std::string> providerIds() const override;
    std::vector<ExecutionTarget> targets() const override;
    ExecutionHandle start(
        const ExecutionRequest& request,
        const ExecutionTarget& target,
        ExecutionEventCallback onEvent = {}) const override;
    ExecutionResult execute(
        const ExecutionRequest& request,
        const ExecutionTarget& target,
        ExecutionEventCallback onEvent = {}) const override;

    void addType(std::unique_ptr<RunConfigurationType> type) override;
    RegistrationHandle registerType(std::unique_ptr<RunConfigurationType> type) override;
    void addProducer(std::unique_ptr<RunConfigurationProducer> producer) override;
    RegistrationHandle registerProducer(std::unique_ptr<RunConfigurationProducer> producer) override;
    void addConfiguration(RunConfiguration configuration) override;
    RegistrationHandle registerConfiguration(RunConfiguration configuration) override;
    std::vector<RunConfiguration> configurations(bool includeTemporary = false) const override;

    GitDiff diff() const override;

    template <class T>
    T* getService() const {
        return registry_.get<T>();
    }

    template <class T>
    T& service() const {
        return registry_.require<T>();
    }

protected:
    WorkspaceContext() = default;
    void registerServices();

private:
    BuildTask resolveBuildTask(const BuildTask& task) const;

    WorkspaceRuntime* runtime_ = nullptr;
    Project* project_ = nullptr;
    ServiceRegistry registry_;
    std::map<std::string, std::vector<std::uint64_t>> eventSubscriptions_;
};

}
