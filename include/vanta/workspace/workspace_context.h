#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "vanta/core/localization.h"
#include "vanta/core/registration.h"
#include "vanta/workspace/command_registry.h"
#include "vanta/workspace/ide_event.h"

namespace vanta {

class AgentToolRegistry;
class AgentContextCollector;
class AgentOperationService;
class AgentRuntime;
class ApprovalService;
class BuildService;
class CapabilityRegistry;
class ChangeSetService;
class CodeIntelligenceService;
class Component;
class DebugService;
class DiagnosticService;
class DocumentService;
class ExecutionService;
class GitService;
class IndexService;
class JobService;
class LanguageRegistry;
class ModelService;
class PluginStorageService;
class Project;
class ProjectManager;
class ProjectTemplateService;
class RefactoringService;
class RunConfigurationService;
class ScratchFileService;
class SettingsService;
class VirtualFileSystem;
class Workspace;
class WorkspaceRuntime;
class WorkspaceTrustService;

class WorkspaceContext {
public:
    explicit WorkspaceContext(WorkspaceRuntime& runtime);

    void SetProject(Project* project);
    void RegisterService(std::string service_id, void* service);
    bool UnregisterService(std::string_view service_id, const void* service = nullptr);
    void* GetService(std::string_view service_id) const;

    template <typename T>
    T* GetService() const {
        return static_cast<T*>(GetService(T::kServiceId));
    }

    Workspace& CurrentWorkspace();
    const Workspace& CurrentWorkspace() const;
    bool WorkspaceOpen() const;
    Project* CurrentProject();
    const Project* CurrentProject() const;
    Project& RequireProject();
    const Project& RequireProject() const;

    CommandRegistry& Commands();
    DocumentService& Documents();
    VirtualFileSystem& FileSystems();
    LanguageRegistry& Languages();
    CodeIntelligenceService& CodeIntelligence();
    DiagnosticService& Diagnostics();
    BuildService& Build();
    ExecutionService& Execution();
    RunConfigurationService& RunConfigurations();
    const RunConfigurationService& RunConfigurations() const;
    JobService& Jobs();
    IndexService& Indexes();
    ProjectManager& Projects();
    AgentToolRegistry& AgentTools();
    AgentContextCollector& AgentContext();
    AgentOperationService& AgentOperations();
    ModelService& Models();
    AgentRuntime& Agents();
    ChangeSetService& Changes();
    RefactoringService& Refactorings();
    DebugService& Debug();
    GitService& Git();
    CapabilityRegistry& Capabilities();
    ProjectTemplateService& ProjectTemplates();
    ScratchFileService& ScratchFiles();
    ApprovalService& Approvals();
    PluginStorageService& PluginStorage();
    WorkspaceTrustService& WorkspaceTrust();
    SettingsService& Settings();
    LocalizationRegistry& Localization();
    const LocalizationRegistry& Localization() const;
    Localizer LocalizerFor(std::string owner_id) const;
    IdeEventBus& Events();
    const IdeEventBus& Events() const;
    void Publish(IdeEvent event);
    void RefreshProject();

    std::uint64_t OnEvent(const Component& owner, IdeEventBus::Listener listener);
    std::uint64_t OnEvent(const Component& owner, IdeEventKind kind, IdeEventBus::Listener listener);
    void RemoveEventSubscriptions(const std::string& component_id);

private:
    WorkspaceRuntime* runtime_ = nullptr;
    Project* project_ = nullptr;
    std::map<std::string, void*> services_;
    std::map<std::string, std::vector<std::uint64_t>> event_subscriptions_;
};

}
