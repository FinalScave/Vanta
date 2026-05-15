#pragma once

#include <filesystem>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vanta/plugin/contribution_registry.h"
#include "vanta/plugin/core_plugin.h"
#include "vanta/plugin/extension_context.h"
#include "vanta/plugin/approval_service.h"

namespace vanta {

class PluginProcessHost;

struct PluginProcessHostDeleter {
    void operator()(PluginProcessHost* host) const;
};

struct PluginManifest {
    ExtensionInfo extension;
    std::string runtimeKind;
    std::string entry;
    std::vector<std::string> permissions;
    std::vector<std::string> activationEvents;
};

class ConsoleLogger final : public Logger {
public:
    void info(const std::string& message) override;
    void warn(const std::string& message) override;
    void error(const std::string& message) override;
};

enum class PluginLifecycleState {
    Discovered,
    Activating,
    Active,
    Deactivating,
    Inactive,
    Failed,
};

struct PluginLifecycleInfo {
    std::string id;
    PluginLifecycleState state = PluginLifecycleState::Discovered;
    bool unloadable = false;
    std::string runtimeKind;
    std::string error;
    std::size_t registrationCount = 0;
};

struct PluginActivationState {
    std::vector<RegistrationHandle> registrations;

    void track(RegistrationHandle registration);
    void clear();
    std::size_t registrationCount() const;
};

class BasicExtensionContext final : public ExtensionContext {
public:
    BasicExtensionContext(
        ExtensionInfo extension,
        std::shared_ptr<PluginActivationState> activationState,
        PermissionSet permissions,
        ApprovalService& approvals,
        Logger& logger,
        WorkspaceContext& workspace);

    const ExtensionInfo& extension() const override;
    const WorkspaceInfo& workspace() const override;
    Logger& logger() override;
    WorkspaceContext& workspaceContext() override;
    const PermissionSet& permissions() const override;

private:
    ExtensionInfo extension_;
    std::shared_ptr<PluginActivationState> activationState_;
    PermissionSet permissions_;
    ApprovalService& approvals_;
    Logger& logger_;
    std::unique_ptr<WorkspaceContext> workspaceContext_;
};

class PluginManager {
public:
    ~PluginManager();

    std::vector<PluginManifest> scan(const std::filesystem::path& pluginsRoot);
    std::optional<PluginManifest> loadManifest(const std::filesystem::path& manifestPath) const;
    std::vector<std::string> activateCorePlugins(
        const CorePluginRegistry& registry,
        Logger& logger,
        WorkspaceContext& workspace,
        ApprovalService& approvals);
    std::vector<std::string> reloadCorePlugins(
        const CorePluginRegistry& registry,
        Logger& logger,
        WorkspaceContext& workspace,
        ApprovalService& approvals);
    std::vector<std::string> activateExternalPlugins(
        Logger& logger,
        WorkspaceContext& workspace,
        ApprovalService& approvals);
    bool unloadPlugin(const std::string& pluginId, std::string* message = nullptr);
    std::vector<std::string> reloadPlugin(
        const std::string& pluginId,
        Logger& logger,
        WorkspaceContext& workspace,
        ApprovalService& approvals);
    void deactivateAll();
    const std::vector<PluginManifest>& manifests() const;
    const ContributionRegistry& contributions() const;
    std::vector<std::string> activePluginIds() const;
    std::vector<PluginLifecycleInfo> pluginLifecycle() const;

private:
    struct ActivePluginSession {
        PluginManifest manifest;
        std::shared_ptr<PluginActivationState> activationState;
        std::unique_ptr<CoreExtension> extension;
        std::unique_ptr<PluginProcessHost, PluginProcessHostDeleter> processHost;
        std::unique_ptr<ExtensionContext> context;
        bool unloadable = false;
        PluginLifecycleState state = PluginLifecycleState::Active;
        std::string lastError;
    };

    std::string activateExternalPlugin(
        const PluginManifest& manifest,
        Logger& logger,
        WorkspaceContext& workspace,
        ApprovalService& approvals);
    const PluginManifest* manifestById(const std::string& pluginId) const;
    void updateLifecycle(
        const PluginManifest& manifest,
        PluginLifecycleState state,
        bool unloadable,
        std::string error = {},
        std::size_t registrationCount = 0);

    std::vector<PluginManifest> manifests_;
    std::vector<ActivePluginSession> activeSessions_;
    ContributionRegistry contributions_;
    std::filesystem::path pluginsRoot_;
    std::map<std::string, PluginLifecycleInfo> lifecycle_;
};

std::string toString(PluginLifecycleState state);

}
