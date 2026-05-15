#include "vanta/plugin/plugin_process_host.h"

#include <chrono>
#include <sstream>
#include <thread>

namespace vanta {

bool PluginProcessHost::start(const PluginManifest& manifest, const std::filesystem::path& workspaceRoot, std::string* errorMessage) {
    if (manifest.runtimeKind != "process") {
        if (errorMessage != nullptr) {
            *errorMessage = "Plugin manifest does not describe a process plugin";
        }
        return false;
    }

    const std::filesystem::path executable = manifest.extension.location / manifest.entry;
    return process_.start({
        .executable = executable.string(),
        .arguments = {},
        .workingDirectory = workspaceRoot,
    }, errorMessage);
}

bool PluginProcessHost::running() const {
    return process_.running();
}

void PluginProcessHost::stop() {
    process_.terminate();
}

std::optional<PluginRpcResponse> PluginProcessHost::activate(const PluginManifest& manifest, const std::filesystem::path& workspaceRoot) {
    Json::Array permissions;
    for (const std::string& permission : manifest.permissions) {
        permissions.push_back(Json(permission));
    }
    return sendRequest("plugin.activate", Json::object({
        {"id", Json(manifest.extension.id)},
        {"name", Json(manifest.extension.name)},
        {"workspaceRoot", Json(workspaceRoot.string())},
        {"permissions", Json::array(std::move(permissions))},
    }));
}

std::optional<PluginRpcResponse> PluginProcessHost::deactivate(const std::string& pluginId) {
    return sendRequest("plugin.deactivate", Json::object({
        {"id", Json(pluginId)},
    }));
}

std::optional<PluginRpcResponse> PluginProcessHost::sendRequest(std::string method, Json params) {
    PluginRpcRequest request;
    request.id = nextRequestId_++;
    request.method = std::move(method);
    request.params = std::move(params);

    const std::string body = toJson(request).dump();
    std::ostringstream frame;
    frame << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    if (!process_.writeStdin(frame.str())) {
        return std::nullopt;
    }

    std::string response;
    for (int attempt = 0; attempt < 50; ++attempt) {
        response += process_.readStdoutAvailable();
        if (response.find("\r\n\r\n") != std::string::npos) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const std::string separator = "\r\n\r\n";
    const std::size_t bodyStart = response.find(separator);
    if (bodyStart == std::string::npos) {
        return std::nullopt;
    }
    return parsePluginRpcResponse(Json::parse(response.substr(bodyStart + separator.size())));
}

}
