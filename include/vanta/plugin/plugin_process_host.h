#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "vanta/platform/process.h"
#include "vanta/plugin/plugin_manager.h"
#include "vanta/plugin/plugin_protocol.h"

namespace vanta {

class PluginProcessHost {
public:
    bool start(const PluginManifest& manifest, const std::filesystem::path& workspaceRoot, std::string* errorMessage = nullptr);
    bool running() const;
    void stop();
    std::optional<PluginRpcResponse> activate(const PluginManifest& manifest, const std::filesystem::path& workspaceRoot);
    std::optional<PluginRpcResponse> deactivate(const std::string& pluginId);
    std::optional<PluginRpcResponse> sendRequest(std::string method, Json params);

private:
    int nextRequestId_ = 1;
    ChildProcess process_;
};

}
