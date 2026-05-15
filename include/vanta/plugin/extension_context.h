#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vanta/workspace/workspace_context.h"
#include "vanta/platform/json.h"
#include "vanta/plugin/permissions.h"

namespace vanta {

struct ExtensionInfo {
    std::string id;
    std::string name;
    std::string version;
    std::string publisher;
    std::filesystem::path location;
};

class Logger {
public:
    virtual ~Logger() = default;
    virtual void info(const std::string& message) = 0;
    virtual void warn(const std::string& message) = 0;
    virtual void error(const std::string& message) = 0;
};

class ExtensionContext {
public:
    virtual ~ExtensionContext() = default;

    virtual const ExtensionInfo& extension() const = 0;
    virtual const WorkspaceInfo& workspace() const = 0;
    virtual Logger& logger() = 0;
    virtual WorkspaceContext& workspaceContext() = 0;
    virtual const PermissionSet& permissions() const = 0;

    CommandRegistry& commands();
    WorkspaceEvents& events();
    WorkspaceFiles& workspaceFiles();
    LanguageRegistry& languages();
    BuildService& build();
    ProjectComponents& components();
    ProjectModelProviderRegistry& projectModels();
    AgentTools& agentTools();
    AgentContextProviders& agentContext();
    AgentOperationService& agentOperations();
    AgentOperationJournal& agentOperationJournal();
    ExecutionHost& execution();
    CodeIntelligenceService& codeIntelligence();
    JobService& jobs();
    RunConfigurationRegistry& runConfigurations();
    GitReader& git();
    IndexCoordinator& indexes();
    CapabilityRegistry& capabilities();
    WorkspaceInitializationPipeline& initialization();
    ProjectTemplateService& projectTemplates();
    ScratchFileService& scratchFiles();
    ApprovalService& approvals();
    SettingsService& settings();
};

}
