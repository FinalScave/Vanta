#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "vanta/core/localization.h"
#include "vanta/agent/agent_tool_registry.h"
#include "vanta/agent/agent_context.h"
#include "vanta/agent/agent_operation.h"
#include "vanta/agent/agent_runtime.h"
#include "vanta/agent/model_service.h"
#include "vanta/debug/debug_service.h"
#include "vanta/execution/build_service.h"
#include "vanta/execution/job_service.h"
#include "vanta/workspace/change_set_service.h"
#include "vanta/workspace/capability_service.h"
#include "vanta/language/code_intelligence_service.h"
#include "vanta/workspace/diagnostic_service.h"
#include "vanta/workspace/document_service.h"
#include "vanta/workspace/workspace.h"
#include "vanta/execution/execution_service.h"
#include "vanta/workspace/git_service.h"
#include "vanta/workspace/ide_event.h"
#include "vanta/workspace/command_registry.h"
#include "vanta/language/document_language_sync.h"
#include "vanta/language/language_service.h"
#include "vanta/workspace/approval_service.h"
#include "vanta/plugin/plugin_storage.h"
#include "vanta/language/refactoring_service.h"
#include "vanta/workspace/index_service.h"
#include "vanta/workspace/workspace_context.h"
#include "vanta/project/project.h"
#include "vanta/project/project_manager.h"
#include "vanta/project/project_template.h"
#include "vanta/execution/run_configuration.h"
#include "vanta/workspace/settings_service.h"
#include "vanta/workspace/workspace_trust.h"
#include "vanta/vfs/file_watcher.h"
#include "vanta/vfs/virtual_file_system.h"

namespace vanta {

class ProjectStateStore;

class WorkspaceRuntime {
public:
    WorkspaceRuntime(VirtualFileSystem& vfs, JobDispatcher job_dispatcher, JobDispatch main_dispatcher = {});
    WorkspaceRuntime(const WorkspaceRuntime&) = delete;
    WorkspaceRuntime& operator=(const WorkspaceRuntime&) = delete;
    ~WorkspaceRuntime();

    bool Open(const std::filesystem::path& workspace_path, std::string* error_message = nullptr, bool initialize = true);
    void InitializeWorkspace();
    void Close();
    bool IsOpen() const;

    void RefreshProject();
    void StartDocumentSync();
    void StopDocumentSync();
    bool StartFileWatcher(std::string* error_message = nullptr);
    void StopFileWatcher();
    WorkspaceContext& Context();
    const WorkspaceContext& Context() const;

private:
    friend class WorkspaceContext;

    ProjectManager& Projects();
    const ProjectManager& Projects() const;
    Workspace& WorkspaceValue();
    const Workspace& WorkspaceValue() const;
    Project& ProjectValue();
    const Project& ProjectValue() const;
    DocumentService& Documents();
    const DocumentService& Documents() const;
    BuildService& Build();
    const BuildService& Build() const;
    AgentToolRegistry& AgentTools();
    const AgentToolRegistry& AgentTools() const;
    AgentContextCollector& AgentContext();
    AgentOperationService& AgentOperations();
    const AgentOperationService& AgentOperations() const;
    ModelService& Models();
    const ModelService& Models() const;
    AgentRuntime& AgentRuntimeValue();
    const AgentRuntime& AgentRuntimeValue() const;
    ChangeSetService& Changes();
    RefactoringService& Refactorings();
    const RefactoringService& Refactorings() const;
    DebugService& Debug();
    const DebugService& Debug() const;
    ExecutionService& Execution();
    const ExecutionService& Execution() const;
    GitService& Git();
    const GitService& Git() const;
    RunConfigurationService& RunConfigurations();
    const RunConfigurationService& RunConfigurations() const;
    LanguageRegistry& Languages();
    const LanguageRegistry& Languages() const;
    CodeIntelligenceService& CodeIntelligence();
    DiagnosticService& Diagnostics();
    const DiagnosticService& Diagnostics() const;
    JobService& Jobs();
    const JobService& Jobs() const;
    CommandRegistry& Commands();
    const CommandRegistry& Commands() const;
    IndexService& Indexes();
    const IndexService& Indexes() const;
    CapabilityRegistry& Capabilities();
    const CapabilityRegistry& Capabilities() const;
    ProjectTemplateService& ProjectTemplates();
    const ProjectTemplateService& ProjectTemplates() const;
    ScratchFileService& ScratchFiles();
    const ScratchFileService& ScratchFiles() const;
    ApprovalService& Approvals();
    const ApprovalService& Approvals() const;
    WorkspaceTrustService& WorkspaceTrust();
    const WorkspaceTrustService& WorkspaceTrust() const;
    SettingsService& WorkspaceSettings();
    LocalizationRegistry& Localization();
    const LocalizationRegistry& Localization() const;
    PluginStorageService& PluginStorage();
    VirtualFileSystem& FileSystems();
    const VirtualFileSystem& FileSystems() const;

    std::uint64_t OnEvent(IdeEventBus::Listener listener);
    void RemoveEventListener(std::uint64_t listener_id);
    IdeEventBus& EventsValue();
    void Publish(IdeEvent event);

    Workspace workspace_;
    Project project_;
    DocumentService documents_;
    std::unique_ptr<BuildService> build_;
    AgentToolRegistry agent_;
    AgentContextCollector agent_context_;
    AgentOperationService agent_operations_;
    ModelService model_service_;
    AgentRuntime agent_runtime_;
    ChangeSetService changes_;
    RefactoringService refactorings_;
    DebugService debug_;
    ExecutionService execution_;
    std::unique_ptr<GitService> git_;
    std::unique_ptr<RunConfigurationService> run_configurations_;
    std::unique_ptr<LanguageRegistry> languages_;
    CodeIntelligenceService code_intelligence_;
    DiagnosticService diagnostics_;
    JobService jobs_;
    std::unique_ptr<CommandRegistry> commands_;
    IndexService indexes_;
    CapabilityRegistry capabilities_;
    ProjectTemplateService project_templates_;
    ScratchFileService scratch_files_;
    WorkspaceTrustService workspace_trust_;
    ApprovalService approvals_;
    SettingsService workspace_settings_;
    LocalizationRegistry localization_;
    PluginStorageService plugin_storage_;
    void RefreshIndexes(std::string title);
    void UpdateCoreCapabilities();
    void WaitForActiveJobs(std::chrono::milliseconds timeout);
    void ConnectEventRelays();
    void DisconnectEventRelays();
    void DispatchMain(JobTask task);
    void HandleFileChange(const VirtualFileChangeEvent& event);
    void PublishDocumentEvent(const DocumentChangeEvent& event);
    void PublishJobEvent(const JobChangeEvent& event);

    VirtualFileSystem& vfs_;
    JobDispatch main_dispatcher_;
    IdeEventBus events_;
    ProjectManager project_manager_;
    std::unique_ptr<ProjectStateStore> project_state_store_;
    ProjectState project_state_;
    WorkspaceContext context_;
    std::unique_ptr<FileWatcher> file_watcher_;
    std::unique_ptr<DocumentLanguageSynchronizer> document_sync_;
    std::uint64_t document_listener_ = 0;
    std::uint64_t diagnostic_listener_ = 0;
    std::uint64_t index_listener_ = 0;
    std::uint64_t job_listener_ = 0;
    std::map<JobId, JobStatus> job_statuses_;
    bool open_ = false;
    bool initialized_ = false;
};

}
