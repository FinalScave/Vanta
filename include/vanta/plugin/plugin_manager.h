#pragma once

#include <filesystem>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vanta/core/registration.h"
#include "vanta/language/language_service.h"
#include "vanta/plugin/core_plugin.h"
#include "vanta/plugin/extension_context.h"

namespace vanta {

class PluginProcessHost;

struct PluginProcessHostDeleter {
    void operator()(PluginProcessHost* host) const;
};

struct PluginManifest {
    ExtensionInfo extension;
    std::string runtime_kind;
    std::string entry;
    std::string min_api_version;
    std::string target_api_version;
    std::vector<std::string> capabilities;
    std::vector<std::string> activation_events;
};

class ConsoleLogger final : public Logger {
public:
    void Info(const std::string& message) override;
    void Warn(const std::string& message) override;
    void Error(const std::string& message) override;
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
    std::string runtime_kind;
    std::string error;
    std::size_t registration_count = 0;
    bool process_running = false;
    bool process_responsive = true;
    int failed_requests = 0;
    int crash_count = 0;
    std::optional<int> process_exit_code;
    std::string process_error;
};

struct PluginActivationState {
    std::vector<RegistrationHandle> registrations;
    std::vector<std::unique_ptr<LanguageService>> language_services;

    void Track(RegistrationHandle registration);
    void Clear();
    std::size_t RegistrationCount() const;
};

class BasicExtensionContext final : public ExtensionContext {
public:
    BasicExtensionContext(
        ExtensionInfo extension,
        std::shared_ptr<PluginActivationState> activation_state,
        Logger& logger,
        WorkspaceContext& workspace);

    const ExtensionInfo& Extension() const override;
    const WorkspaceInfo& Workspace() const override;
    Logger& Log() override;
    WorkspaceContext& Context() override;
    Localizer LocalizerValue() const override;
    PluginStorageService& Storage() override;
    void Track(RegistrationHandle registration) override;

private:
    ExtensionInfo extension_;
    std::shared_ptr<PluginActivationState> activation_state_;
    Logger& logger_;
    WorkspaceContext& workspace_;
};

class PluginManager {
public:
    ~PluginManager();

    std::vector<PluginManifest> Scan(const std::filesystem::path& plugins_root);
    std::optional<PluginManifest> LoadManifest(const std::filesystem::path& manifest_path) const;
    std::vector<std::string> ActivateCorePlugins(
        const CorePluginRegistry& registry,
        Logger& logger,
        WorkspaceContext& workspace);
    std::vector<std::string> ReloadCorePlugins(
        const CorePluginRegistry& registry,
        Logger& logger,
        WorkspaceContext& workspace);
    std::vector<std::string> ActivateExternalPlugins(
        Logger& logger,
        WorkspaceContext& workspace);
    bool UnloadPlugin(const std::string& plugin_id, std::string* message = nullptr);
    std::vector<std::string> ReloadPlugin(
        const std::string& plugin_id,
        Logger& logger,
        WorkspaceContext& workspace);
    void DeactivateAll();
    const std::vector<PluginManifest>& Manifests() const;
    std::vector<std::string> ActivePluginIds() const;
    void ReconcileProcessHealth();
    std::vector<PluginLifecycleInfo> PluginLifecycle();

private:
    struct ActivePluginSession {
        PluginManifest manifest;
        std::shared_ptr<PluginActivationState> activation_state;
        std::unique_ptr<CoreExtension> extension;
        std::unique_ptr<PluginProcessHost, PluginProcessHostDeleter> process_host;
        std::unique_ptr<ExtensionContext> context;
        bool unloadable = false;
        PluginLifecycleState state = PluginLifecycleState::Active;
        std::string last_error;
    };

    std::string ActivateExternalPlugin(
        const PluginManifest& manifest,
        Logger& logger,
        WorkspaceContext& workspace);
    const PluginManifest* ManifestById(const std::string& plugin_id) const;
    bool IsActive(const std::string& plugin_id) const;
    bool ShouldActivate(const PluginManifest& manifest, WorkspaceContext& workspace) const;
    std::string CompatibilityError(const PluginManifest& manifest) const;
    void RegisterExternalPluginCapabilities(
        const PluginManifest& manifest,
        PluginProcessHost& host,
        PluginActivationState& activation_state,
        WorkspaceContext& workspace,
        const Value& result);
    void UpdateLifecycle(
        const PluginManifest& manifest,
        PluginLifecycleState state,
        bool unloadable,
        std::string error = {},
        std::size_t registration_count = 0);

    std::vector<PluginManifest> manifests_;
    std::vector<ActivePluginSession> active_sessions_;
    std::filesystem::path plugins_root_;
    std::map<std::string, PluginLifecycleInfo> lifecycle_;
};

std::string ToString(PluginLifecycleState state);

}
