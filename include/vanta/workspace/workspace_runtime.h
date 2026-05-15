#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include "vanta/agent/agent_tool_registry.h"
#include "vanta/agent/agent_context.h"
#include "vanta/agent/agent_operation.h"
#include "vanta/execution/build_service.h"
#include "vanta/execution/job_service.h"
#include "vanta/workspace/change_set_service.h"
#include "vanta/workspace/capability_service.h"
#include "vanta/language/code_intelligence_service.h"
#include "vanta/workspace/diagnostic_service.h"
#include "vanta/workspace/document_service.h"
#include "vanta/workspace/editor.h"
#include "vanta/workspace/workspace.h"
#include "vanta/execution/execution_service.h"
#include "vanta/builtin/git/git_client.h"
#include "vanta/workspace/ide_event.h"
#include "vanta/workspace/command_palette.h"
#include "vanta/builtin/cpp/cpp_index.h"
#include "vanta/language/document_language_sync.h"
#include "vanta/language/language_service.h"
#include "vanta/language/language_request_pipeline.h"
#include "vanta/platform/async.h"
#include "vanta/plugin/contribution_registry.h"
#include "vanta/plugin/approval_service.h"
#include "vanta/workspace/index_service.h"
#include "vanta/workspace/initialization.h"
#include "vanta/workspace/workspace_context.h"
#include "vanta/workspace/workspace_services.h"
#include "vanta/project/project.h"
#include "vanta/project/project_manager.h"
#include "vanta/project/project_state_store.h"
#include "vanta/project/project_template.h"
#include "vanta/execution/run_configuration.h"
#include "vanta/workspace/settings_service.h"
#include "vanta/workspace/search_service.h"
#include "vanta/workspace/layout_state_store.h"
#include "vanta/workspace/ui_state_store.h"
#include "vanta/vfs/file_watcher.h"
#include "vanta/vfs/virtual_file_system.h"

namespace vanta {

class WorkspaceRuntime {
public:
    WorkspaceRuntime(VirtualFileSystem& vfs, AsyncRuntime& async);
    WorkspaceRuntime(const WorkspaceRuntime&) = delete;
    WorkspaceRuntime& operator=(const WorkspaceRuntime&) = delete;
    ~WorkspaceRuntime();

    bool open(const std::filesystem::path& workspacePath, std::string* errorMessage = nullptr);
    void close();
    bool isOpen() const;

    void refreshProject();
    void startDocumentSync();
    void stopDocumentSync();
    bool startFileWatcher(std::string* errorMessage = nullptr);
    void stopFileWatcher();
    void bindComponent(std::unique_ptr<Component> component);
    bool unbindComponent(const std::string& id);
    void addComponentContribution(ComponentContribution contribution);
    bool removeComponentContribution(const std::string& id);
    std::vector<ComponentContribution> componentContributions() const;
    ProjectManager& projectManager();
    const ProjectManager& projectManager() const;
    Workspace& workspace();
    const Workspace& workspace() const;
    Project& project();
    const Project& project() const;
    DocumentService& documents();
    const DocumentService& documents() const;
    EditorWorkspace& editor();
    const EditorWorkspace& editor() const;
    BuildService& build();
    const BuildService& build() const;
    AgentToolRegistry& agent();
    const AgentToolRegistry& agent() const;
    AgentContextCollector& agentContext();
    AgentOperationService& agentOperations();
    const AgentOperationService& agentOperations() const;
    AgentOperationJournal& agentOperationJournal();
    const AgentOperationJournal& agentOperationJournal() const;
    ChangeSetService& changes();
    ExecutionService& execution();
    const ExecutionService& execution() const;
    GitClient& git();
    const GitClient& git() const;
    RunConfigurationService& runConfigurations();
    const RunConfigurationService& runConfigurations() const;
    DefaultLanguageRegistry& languages();
    const DefaultLanguageRegistry& languages() const;
    LanguageRequestPipeline& languageRequests();
    CodeIntelligenceService& codeIntelligence();
    DiagnosticService& diagnostics();
    const DiagnosticService& diagnostics() const;
    JobService& jobs();
    const JobService& jobs() const;
    DefaultCommandRegistry& commands();
    const DefaultCommandRegistry& commands() const;
    KeybindingRegistry& keybindings();
    CommandPalette& commandPalette();
    CppSemanticIndex& cppIndex();
    SearchService& search();
    const SearchService& search() const;
    IndexCoordinator& indexes();
    const IndexCoordinator& indexes() const;
    CapabilityRegistry& capabilities();
    const CapabilityRegistry& capabilities() const;
    WorkspaceInitializationPipeline& initialization();
    const WorkspaceInitializationPipeline& initialization() const;
    ProjectTemplateService& projectTemplates();
    const ProjectTemplateService& projectTemplates() const;
    ScratchFileService& scratchFiles();
    const ScratchFileService& scratchFiles() const;
    ApprovalService& approvals();
    const ApprovalService& approvals() const;
    SettingsService& workspaceSettings();
    PluginStorageService& pluginStorage();
    ContributionRegistry& contributions();
    AsyncRuntime& async();
    UiStateStore& ui();
    const UiStateStore& ui() const;
    WorkspaceContext& context();
    const WorkspaceContext& context() const;

    std::uint64_t onEvent(IdeEventBus::Listener listener);
    void removeEventListener(std::uint64_t listenerId);
    IdeEventBus& events();
    void publish(IdeEvent event);

private:
    Workspace workspace_;
    Project project_;
    DocumentService documents_;
    EditorWorkspace editor_;
    DefaultBuildService build_;
    AgentToolRegistry agent_;
    AgentContextCollector agentContext_;
    AgentOperationService agentOperations_;
    AgentOperationJournal agentOperationJournal_;
    ChangeSetService changes_;
    ExecutionService execution_;
    GitClient git_;
    RunConfigurationService runConfigurations_;
    DefaultLanguageRegistry languages_;
    LanguageRequestPipeline languageRequests_;
    CodeIntelligenceService codeIntelligence_;
    DiagnosticService diagnostics_;
    JobService jobs_;
    DefaultCommandRegistry commands_;
    KeybindingRegistry keybindings_;
    CommandPalette commandPalette_;
    CppSemanticIndex cppIndex_;
    SearchService search_;
    IndexCoordinator indexes_;
    CapabilityRegistry capabilities_;
    WorkspaceInitializationPipeline initialization_;
    ProjectTemplateService projectTemplates_;
    ScratchFileService scratchFiles_;
    ApprovalService approvals_;
    SettingsService workspaceSettings_;
    PluginStorageService pluginStorage_;
    ContributionRegistry contributions_;
    UiStateStore ui_;

    LayoutStateStore& layoutComponent();
    const LayoutStateStore& layoutComponent() const;
    void bindBuiltinComponents();
    void refreshIndexes(std::string title);
    void updateCoreCapabilities();
    void reconcileComponentContributions();
    void connectEventRelays();
    void disconnectEventRelays();
    void handleFileChange(const VirtualFileChangeEvent& event);
    void publishDocumentEvent(const DocumentChangeEvent& event);
    void publishJobEvent(const JobChangeEvent& event);

    VirtualFileSystem& vfs_;
    AsyncRuntime& async_;
    IdeEventBus events_;
    ProjectManager projectManager_;
    ProjectStateStore projectStateStore_;
    ProjectState projectState_;
    ComponentContributionRegistry componentContributions_;
    std::map<std::string, bool> activeContributedComponents_;
    WorkspaceContext context_;
    std::unique_ptr<FileWatcher> fileWatcher_;
    std::unique_ptr<DocumentLanguageSynchronizer> documentSync_;
    std::uint64_t documentListener_ = 0;
    std::uint64_t diagnosticListener_ = 0;
    std::uint64_t jobListener_ = 0;
    std::map<JobId, JobStatus> jobStatuses_;
    bool open_ = false;
};

}
