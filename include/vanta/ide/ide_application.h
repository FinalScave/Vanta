#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "vanta/ide/ui_service.h"
#include "vanta/platform/async.h"
#include "vanta/plugin/core_plugin.h"
#include "vanta/plugin/plugin_manager.h"
#include "vanta/vfs/virtual_file_system.h"

namespace vanta {

class WorkspaceContext;
class WorkspaceRuntime;

class IdeApplication {
public:
    IdeApplication();
    ~IdeApplication();

    bool OpenWorkspace(const std::filesystem::path& workspace_path, CorePluginDependencies core_plugin_dependencies = {}, std::string* error_message = nullptr);
    void Shutdown();
    std::vector<std::string> ReloadPlugins();
    std::vector<std::string> ReloadPlugin(const std::string& plugin_id);
    bool UnloadPlugin(const std::string& plugin_id, std::string* message = nullptr);

    WorkspaceContext& Context();
    const WorkspaceContext& Context() const;
    UiService& Ui();
    const UiService& Ui() const;
    PluginManager& Plugins();
    const PluginManager& Plugins() const;
    Logger& LoggerValue();
    std::size_t DrainMainTasks();

private:
    void ActivatePlugins();

    AsyncRuntime async_;
    VirtualFileSystem vfs_;
    std::unique_ptr<WorkspaceRuntime> runtime_;
    std::unique_ptr<UiService> ui_service_;
    ConsoleLogger logger_;
    PluginManager plugins_;
    CorePluginRegistry core_plugins_;
};

}
