#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vanta/agent/agent_context.h"
#include "vanta/agent/agent_tool_registry.h"
#include "vanta/execution/build_service.h"
#include "vanta/language/code_intelligence_service.h"
#include "vanta/core/registration.h"
#include "vanta/workspace/workspace.h"
#include "vanta/execution/execution_service.h"
#include "vanta/builtin/git/git_client.h"
#include "vanta/workspace/ide_event.h"
#include "vanta/language/language_service.h"
#include "vanta/platform/json.h"
#include "vanta/execution/run_configuration.h"

namespace vanta {

class Component;
class ProjectModelProvider;

using CommandHandler = std::function<Json(const Json&)>;

class CommandRegistry {
public:
    virtual ~CommandRegistry() = default;
    virtual void add(const std::string& id, CommandHandler handler) = 0;
    virtual RegistrationHandle registerCommand(const std::string& id, CommandHandler handler) = 0;
    virtual std::optional<Json> execute(const std::string& id, const Json& arguments) const = 0;
    virtual std::vector<std::string> list() const = 0;
};

class DefaultCommandRegistry final : public CommandRegistry {
public:
    void add(const std::string& id, CommandHandler handler) override;
    RegistrationHandle registerCommand(const std::string& id, CommandHandler handler) override;
    std::optional<Json> execute(const std::string& id, const Json& arguments) const override;
    std::vector<std::string> list() const override;
    void unregister(const std::string& id);

private:
    std::map<std::string, CommandHandler> handlers_;
};

class WorkspaceFiles {
public:
    virtual ~WorkspaceFiles() = default;
    virtual const WorkspaceInfo& info() const = 0;
    virtual std::optional<std::string> readTextFile(const VirtualFile& file) const = 0;
    virtual bool writeTextFile(const VirtualFile& file, const std::string& text, std::string* errorMessage = nullptr) = 0;
};

class WorkspaceEvents {
public:
    virtual ~WorkspaceEvents() = default;

    virtual RegistrationHandle subscribe(IdeEventBus::Listener listener) = 0;
    virtual RegistrationHandle subscribe(IdeEventKind kind, IdeEventBus::Listener listener) = 0;
    virtual void publish(IdeEvent event) = 0;
};

class ProjectComponents {
public:
    virtual ~ProjectComponents() = default;

    virtual void contribute(ComponentContribution contribution) = 0;
    virtual RegistrationHandle registerContribution(ComponentContribution contribution) = 0;
    virtual std::vector<ComponentContribution> contributions() const = 0;
};

class ProjectModelProviderRegistry {
public:
    virtual ~ProjectModelProviderRegistry() = default;

    virtual void addProvider(std::unique_ptr<ProjectModelProvider> provider) = 0;
    virtual RegistrationHandle registerProvider(std::unique_ptr<ProjectModelProvider> provider) = 0;
    virtual std::vector<std::string> modelProviderIds() const = 0;
};

class AgentTools {
public:
    virtual ~AgentTools() = default;
    virtual void addTool(AgentToolDefinition definition) = 0;
    virtual RegistrationHandle registerTool(AgentToolDefinition definition) = 0;
};

class AgentContextProviders {
public:
    virtual ~AgentContextProviders() = default;

    virtual void addProvider(std::unique_ptr<AgentContextProvider> provider) = 0;
    virtual RegistrationHandle registerProvider(std::unique_ptr<AgentContextProvider> provider) = 0;
    virtual AgentContext collect(const AgentContextRequest& request) const = 0;
};

class RunConfigurationRegistry {
public:
    virtual ~RunConfigurationRegistry() = default;

    virtual void addType(std::unique_ptr<RunConfigurationType> type) = 0;
    virtual RegistrationHandle registerType(std::unique_ptr<RunConfigurationType> type) = 0;
    virtual void addProducer(std::unique_ptr<RunConfigurationProducer> producer) = 0;
    virtual RegistrationHandle registerProducer(std::unique_ptr<RunConfigurationProducer> producer) = 0;
    virtual void addConfiguration(RunConfiguration configuration) = 0;
    virtual RegistrationHandle registerConfiguration(RunConfiguration configuration) = 0;
    virtual std::vector<RunConfiguration> configurations(bool includeTemporary = false) const = 0;
};

class ExecutionHost {
public:
    virtual ~ExecutionHost() = default;

    virtual void addProvider(std::unique_ptr<ExecutionProvider> provider) = 0;
    virtual RegistrationHandle registerProvider(std::unique_ptr<ExecutionProvider> provider) = 0;
    virtual std::vector<std::string> providerIds() const = 0;
    virtual std::vector<ExecutionTarget> targets() const = 0;
    virtual ExecutionHandle start(
        const ExecutionRequest& request,
        const ExecutionTarget& target,
        ExecutionEventCallback onEvent = {}) const = 0;
    virtual ExecutionResult execute(
        const ExecutionRequest& request,
        const ExecutionTarget& target,
        ExecutionEventCallback onEvent = {}) const = 0;
};

class GitReader {
public:
    virtual ~GitReader() = default;

    virtual GitDiff diff() const = 0;
};

}
