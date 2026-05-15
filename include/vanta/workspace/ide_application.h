#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "vanta/workspace/ide_environment.h"
#include "vanta/workspace/workspace_runtime.h"
#include "vanta/plugin/core_plugin.h"
#include "vanta/plugin/plugin_manager.h"

namespace vanta {

class IdeApplication {
public:
    bool openWorkspace(const std::filesystem::path& workspacePath, const std::filesystem::path& clicePath, std::string* errorMessage = nullptr);
    void shutdown();
    std::vector<std::string> reloadPlugins();
    std::vector<std::string> reloadPlugin(const std::string& pluginId);
    bool unloadPlugin(const std::string& pluginId, std::string* message = nullptr);

    IdeEnvironment& services();
    const IdeEnvironment& services() const;
    WorkspaceRuntime& runtime();
    const WorkspaceRuntime& runtime() const;
    WorkspaceContext& context();
    const WorkspaceContext& context() const;
    PluginManager& plugins();
    const PluginManager& plugins() const;
    Logger& logger();

private:
    void wireServices();
    void activatePlugins();

    IdeEnvironment services_;
    std::unique_ptr<WorkspaceRuntime> runtime_;
    ConsoleLogger logger_;
    PluginManager plugins_;
    CorePluginRegistry corePlugins_;
};

}
