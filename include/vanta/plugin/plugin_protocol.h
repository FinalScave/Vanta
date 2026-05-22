#pragma once

#include <optional>
#include <string>

#include "vanta/core/value.h"

namespace vanta {

struct PluginRpcRequest {
    int id = 0;
    std::string method;
    std::string params_json = "{}";
};

struct PluginRpcResponse {
    int id = 0;
    bool ok = false;
    std::string result_json = "{}";
    std::string error;
};

enum class PluginCapabilityKind {
    Command,
    AgentTool,
    BuildProvider,
    LanguageService,
    ModelProvider,
    DebugProvider,
};

struct PluginCapabilityRegistration {
    PluginCapabilityKind kind = PluginCapabilityKind::Command;
    std::string id;
    std::string title;
    Value metadata = Value::ObjectValue();
};

std::optional<PluginRpcResponse> ParsePluginRpcResponse(const Value& json);
std::optional<PluginRpcResponse> ParsePluginRpcResponseText(const std::string& json_text);
std::optional<PluginCapabilityRegistration> ParsePluginCapabilityRegistration(const Value& json);
std::string FormatPluginRpcRequestText(const PluginRpcRequest& request);
std::string FormatPluginRpcResponseText(const PluginRpcResponse& response);
std::string ToString(PluginCapabilityKind kind);

}
