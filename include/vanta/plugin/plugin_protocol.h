#pragma once

#include <optional>
#include <string>

#include "vanta/platform/json.h"

namespace vanta {

struct PluginRpcRequest {
    int id = 0;
    std::string method;
    Json params;
};

struct PluginRpcResponse {
    int id = 0;
    bool ok = false;
    Json result;
    std::string error;
};

enum class PluginRegistrationKind {
    Command,
    AgentTool,
    BuildProvider,
    LanguageService,
};

struct PluginRegistration {
    PluginRegistrationKind kind = PluginRegistrationKind::Command;
    std::string id;
    std::string title;
    Json metadata;
};

Json toJson(const PluginRpcRequest& request);
Json toJson(const PluginRpcResponse& response);
Json toJson(const PluginRegistration& registration);
std::optional<PluginRpcResponse> parsePluginRpcResponse(const Json& json);
std::optional<PluginRegistration> parsePluginRegistration(const Json& json);
std::string toString(PluginRegistrationKind kind);

}
