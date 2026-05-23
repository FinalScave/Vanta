#include "mornox/plugin/plugin_process_host.h"

#include <chrono>
#include <cstddef>
#include <optional>
#include <sstream>
#include <thread>

#include "mornox/core/value.h"
#include "mornox/core/json_codec.h"

#include "plugin/plugin_process_command.h"

namespace mornox {
namespace {

std::optional<std::size_t> ContentLength(const std::string& headers) {
    const std::string key = "Content-Length:";
    const std::size_t start = headers.find(key);
    if (start == std::string::npos) {
        return std::nullopt;
    }
    const std::size_t value_start = start + key.size();
    const std::size_t value_end = headers.find_first_of("\r\n", value_start);
    try {
        return static_cast<std::size_t>(std::stoull(headers.substr(value_start, value_end - value_start)));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::size_t> HeaderEndOffset(const std::string& response) {
    if (const std::size_t value = response.find("\r\n\r\n"); value != std::string::npos) {
        return value + 4;
    }
    if (const std::size_t value = response.find("\r\r\n\r\r\n"); value != std::string::npos) {
        return value + 6;
    }
    if (const std::size_t value = response.find("\n\n"); value != std::string::npos) {
        return value + 2;
    }
    return std::nullopt;
}

}

bool PluginProcessHost::Start(const PluginManifest& manifest, const std::filesystem::path& workspace_root, std::string* error_message) {
    if (manifest.runtime_kind != "process") {
        if (error_message != nullptr) {
            *error_message = "Plugin manifest does not describe a process plugin";
        }
        return false;
    }

    health_ = {};
    const bool started = process_.Start(internal::ResolvePluginProcessCommand(manifest, workspace_root), error_message);
    health_.running = started;
    health_.responsive = started;
    if (!started && error_message != nullptr) {
        health_.last_error = *error_message;
    }
    return started;
}

bool PluginProcessHost::Running() const {
    return process_.Running();
}

void PluginProcessHost::Stop() {
    process_.Terminate();
    health_.running = false;
    health_.exit_code = process_.ExitCode();
}

std::optional<PluginRpcResponse> PluginProcessHost::Activate(const PluginManifest& manifest, const std::filesystem::path& workspace_root) {
    Value::Array capabilities;
    for (const std::string& capability : manifest.capabilities) {
        capabilities.push_back(Value(capability));
    }
    return SendRequest("plugin.activate", ValueToJsonText(Value::ObjectValue({
        {"id", Value(manifest.extension.id)},
        {"name", Value(manifest.extension.name)},
        {"apiVersion", Value(manifest.target_api_version)},
        {"workspaceRoot", Value(workspace_root.string())},
        {"capabilities", Value::ArrayValue(std::move(capabilities))},
    })));
}

std::optional<PluginRpcResponse> PluginProcessHost::Deactivate(const std::string& plugin_id) {
    return SendRequest("plugin.deactivate", ValueToJsonText(Value::ObjectValue({
        {"id", Value(plugin_id)},
    })));
}

std::optional<PluginRpcResponse> PluginProcessHost::SendRequest(std::string method, std::string params_json) {
    std::lock_guard<std::mutex> lock(rpc_mutex_);
    PluginRpcRequest request;
    request.id = next_request_id_++;
    request.method = std::move(method);
    request.params_json = std::move(params_json);

    const std::string body = FormatPluginRpcRequestText(request);
    std::ostringstream frame;
    frame << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    if (!process_.WriteStdin(frame.str())) {
        RecordFailure("Plugin process is not writable");
        health_.running = process_.Running();
        health_.exit_code = process_.ExitCode();
        return std::nullopt;
    }

    std::string response;
    std::optional<std::size_t> expected_body_size;
    std::optional<std::size_t> body_start;
    for (int attempt = 0; attempt < 300; ++attempt) {
        response += process_.ReadStdoutAvailable();
        if (!body_start) {
            if (const auto header_end = HeaderEndOffset(response)) {
                expected_body_size = ContentLength(response.substr(0, *header_end));
                body_start = *header_end;
            }
        }
        if (body_start && expected_body_size && response.size() >= *body_start + *expected_body_size) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!body_start || !expected_body_size || response.size() < *body_start + *expected_body_size) {
        RecordFailure("Plugin RPC timed out");
        health_.running = process_.Running();
        health_.exit_code = process_.ExitCode();
        return std::nullopt;
    }
    auto parsed = ParsePluginRpcResponseText(response.substr(*body_start, *expected_body_size));
    if (!parsed) {
        RecordFailure("Plugin RPC response was not valid JSON");
        health_.running = process_.Running();
        health_.exit_code = process_.ExitCode();
        return std::nullopt;
    }
    RecordSuccess();
    health_.running = process_.Running();
    health_.exit_code = process_.ExitCode();
    return parsed;
}

PluginProcessHealth PluginProcessHost::Health() {
    if (auto exit_code = process_.TryWait()) {
        if (health_.running || !health_.exit_code.has_value()) {
            ++health_.crash_count;
        }
        health_.running = false;
        health_.responsive = false;
        health_.exit_code = *exit_code;
        if (health_.last_error.empty()) {
            health_.last_error = "Plugin process exited";
        }
    } else {
        health_.running = process_.Running();
        health_.exit_code = process_.ExitCode();
    }
    return health_;
}

void PluginProcessHost::RecordFailure(std::string error) {
    ++health_.failed_requests;
    health_.responsive = false;
    health_.last_error = std::move(error);
    if (!process_.Running()) {
        ++health_.crash_count;
    }
}

void PluginProcessHost::RecordSuccess() {
    health_.responsive = true;
    health_.last_error.clear();
}

}
