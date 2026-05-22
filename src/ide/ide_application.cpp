#include "vanta/ide/ide_application.h"

#include <cassert>
#include <utility>

#include "ide/ui_service_impl.h"
#include "vanta/platform/async_job_dispatcher.h"
#include "vanta/workspace/workspace_runtime.h"

namespace vanta {

IdeApplication::IdeApplication()
    : ui_service_(std::make_unique<internal::UiServiceImpl>()) {
}

IdeApplication::~IdeApplication() = default;

bool IdeApplication::OpenWorkspace(const std::filesystem::path& workspace_path, CorePluginDependencies core_plugin_dependencies, std::string* error_message) {
    Shutdown();
    async_.Start();

    runtime_ = std::make_unique<WorkspaceRuntime>(
        vfs_,
        AsyncJobDispatcher(async_),
        [this](JobTask task) {
            async_.PostMain(std::move(task));
        });
    if (!runtime_->Open(workspace_path, error_message, false)) {
        runtime_.reset();
        async_.Stop();
        return false;
    }
    runtime_->Context().RegisterService(UiService::kServiceId, ui_service_.get());

    plugins_.Scan(runtime_->Context().CurrentWorkspace().Info().root_path / "plugins");
    core_plugins_ = CreateDefaultCorePluginRegistry(std::move(core_plugin_dependencies));
    ActivatePlugins();
    runtime_->InitializeWorkspace();
    runtime_->StartDocumentSync();

    std::string watcher_error;
    if (!runtime_->StartFileWatcher(&watcher_error) && !watcher_error.empty()) {
        logger_.Warn("File watcher is not active: " + watcher_error);
    }
    return true;
}

void IdeApplication::Shutdown() {
    if (runtime_ != nullptr) {
        runtime_->Close();
    }
    plugins_.DeactivateAll();
    if (runtime_ != nullptr) {
        runtime_->Context().UnregisterService(UiService::kServiceId, ui_service_.get());
    }
    runtime_.reset();
    async_.Stop();
}

std::vector<std::string> IdeApplication::ReloadPlugins() {
    assert(runtime_ != nullptr);
    std::vector<std::string> messages = plugins_.ReloadCorePlugins(
        core_plugins_,
        logger_,
        runtime_->Context());
    std::vector<std::string> external_messages = plugins_.ActivateExternalPlugins(
        logger_,
        runtime_->Context());
    messages.insert(messages.end(), external_messages.begin(), external_messages.end());
    runtime_->RefreshProject();
    return messages;
}

std::vector<std::string> IdeApplication::ReloadPlugin(const std::string& plugin_id) {
    assert(runtime_ != nullptr);
    std::vector<std::string> messages = plugins_.ReloadPlugin(
        plugin_id,
        logger_,
        runtime_->Context());
    runtime_->RefreshProject();
    return messages;
}

bool IdeApplication::UnloadPlugin(const std::string& plugin_id, std::string* message) {
    const bool ok = plugins_.UnloadPlugin(plugin_id, message);
    if (runtime_ != nullptr) {
        runtime_->RefreshProject();
    }
    return ok;
}

WorkspaceContext& IdeApplication::Context() {
    assert(runtime_ != nullptr);
    return runtime_->Context();
}

const WorkspaceContext& IdeApplication::Context() const {
    assert(runtime_ != nullptr);
    return runtime_->Context();
}

UiService& IdeApplication::Ui() {
    return *ui_service_;
}

const UiService& IdeApplication::Ui() const {
    return *ui_service_;
}

PluginManager& IdeApplication::Plugins() {
    return plugins_;
}

const PluginManager& IdeApplication::Plugins() const {
    return plugins_;
}

Logger& IdeApplication::LoggerValue() {
    return logger_;
}

std::size_t IdeApplication::DrainMainTasks() {
    return async_.DrainMain();
}

void IdeApplication::ActivatePlugins() {
    assert(runtime_ != nullptr);
    for (const std::string& message : plugins_.ActivateCorePlugins(
             core_plugins_,
             logger_,
             runtime_->Context())) {
        logger_.Info(message);
    }
    for (const std::string& message : plugins_.ActivateExternalPlugins(
             logger_,
             runtime_->Context())) {
        logger_.Info(message);
    }
}

}
