#include "vanta/workspace/ide_application.h"

#include <cassert>

namespace vanta {

bool IdeApplication::openWorkspace(const std::filesystem::path& workspacePath, const std::filesystem::path& clicePath, std::string* errorMessage) {
    shutdown();
    services_.async.start();

    runtime_ = std::make_unique<WorkspaceRuntime>(services_.vfs, services_.async);
    if (!runtime_->open(workspacePath, errorMessage)) {
        runtime_.reset();
        return false;
    }

    plugins_.scan(runtime_->workspace().info().rootPath / "plugins");
    runtime_->contributions() = plugins_.contributions();
    runtime_->contributions().add({
        .kind = ContributionKind::Component,
        .id = LayoutStateStore::componentId,
        .title = "Layout State",
        .pluginId = "vanta.core",
    });
    corePlugins_ = createDefaultCorePluginRegistry({
        .clicePath = clicePath,
        .workspaceRoot = runtime_->workspace().info().rootPath,
    });
    wireServices();
    activatePlugins();
    runtime_->refreshProject();
    runtime_->startDocumentSync();

    std::string watcherError;
    if (!runtime_->startFileWatcher(&watcherError) && !watcherError.empty()) {
        logger_.warn("File watcher is not active: " + watcherError);
    }
    return true;
}

void IdeApplication::shutdown() {
    if (runtime_ != nullptr) {
        runtime_->close();
    }
    plugins_.deactivateAll();
    runtime_.reset();
    services_.async.stop();
}

std::vector<std::string> IdeApplication::reloadPlugins() {
    assert(runtime_ != nullptr);
    std::vector<std::string> messages = plugins_.reloadCorePlugins(
        corePlugins_,
        logger_,
        runtime_->context(),
        services_.approvals);
    std::vector<std::string> externalMessages = plugins_.activateExternalPlugins(
        logger_,
        runtime_->context(),
        services_.approvals);
    messages.insert(messages.end(), externalMessages.begin(), externalMessages.end());
    runtime_->refreshProject();
    runtime_->ui().refresh();
    return messages;
}

std::vector<std::string> IdeApplication::reloadPlugin(const std::string& pluginId) {
    assert(runtime_ != nullptr);
    std::vector<std::string> messages = plugins_.reloadPlugin(
        pluginId,
        logger_,
        runtime_->context(),
        services_.approvals);
    runtime_->refreshProject();
    runtime_->ui().refresh();
    return messages;
}

bool IdeApplication::unloadPlugin(const std::string& pluginId, std::string* message) {
    const bool ok = plugins_.unloadPlugin(pluginId, message);
    if (runtime_ != nullptr) {
        runtime_->refreshProject();
        runtime_->ui().refresh();
    }
    return ok;
}

IdeEnvironment& IdeApplication::services() {
    return services_;
}

const IdeEnvironment& IdeApplication::services() const {
    return services_;
}

WorkspaceRuntime& IdeApplication::runtime() {
    assert(runtime_ != nullptr);
    return *runtime_;
}

const WorkspaceRuntime& IdeApplication::runtime() const {
    assert(runtime_ != nullptr);
    return *runtime_;
}

WorkspaceContext& IdeApplication::context() {
    return runtime().context();
}

const WorkspaceContext& IdeApplication::context() const {
    return runtime().context();
}

PluginManager& IdeApplication::plugins() {
    return plugins_;
}

const PluginManager& IdeApplication::plugins() const {
    return plugins_;
}

Logger& IdeApplication::logger() {
    return logger_;
}

void IdeApplication::wireServices() {
    assert(runtime_ != nullptr);
    runtime_->agentContext().addProvider(createGitDiffAgentContextProvider(runtime_->git()));
}

void IdeApplication::activatePlugins() {
    assert(runtime_ != nullptr);
    for (const std::string& message : plugins_.activateCorePlugins(
             corePlugins_,
             logger_,
             runtime_->context(),
             services_.approvals)) {
        logger_.info(message);
    }
    for (const std::string& message : plugins_.activateExternalPlugins(
             logger_,
             runtime_->context(),
             services_.approvals)) {
        logger_.info(message);
    }
}

}
