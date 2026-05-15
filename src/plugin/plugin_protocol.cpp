#include "vanta/plugin/plugin_protocol.h"

namespace vanta {

Json toJson(const PluginRpcRequest& request) {
    return Json::object({
        {"jsonrpc", Json("2.0")},
        {"id", Json(static_cast<std::int64_t>(request.id))},
        {"method", Json(request.method)},
        {"params", request.params},
    });
}

Json toJson(const PluginRpcResponse& response) {
    Json::Object object;
    object["jsonrpc"] = Json("2.0");
    object["id"] = Json(static_cast<std::int64_t>(response.id));
    if (response.ok) {
        object["result"] = response.result;
    } else {
        object["error"] = Json::object({
            {"message", Json(response.error)},
        });
    }
    return Json::object(std::move(object));
}

Json toJson(const PluginRegistration& registration) {
    return Json::object({
        {"kind", Json(toString(registration.kind))},
        {"id", Json(registration.id)},
        {"title", Json(registration.title)},
        {"metadata", registration.metadata},
    });
}

std::optional<PluginRpcResponse> parsePluginRpcResponse(const Json& json) {
    if (!json.isObject() || !json.contains("id") || !json["id"].isInt()) {
        return std::nullopt;
    }

    PluginRpcResponse response;
    response.id = static_cast<int>(json["id"].asInt());
    if (json.contains("result")) {
        response.ok = true;
        response.result = json["result"];
    } else if (json.contains("error")) {
        response.ok = false;
        if (json["error"].isObject()) {
            response.error = json["error"].stringValue("message").value_or("Plugin RPC error");
        } else {
            response.error = "Plugin RPC error";
        }
    }
    return response;
}

std::optional<PluginRegistration> parsePluginRegistration(const Json& json) {
    if (!json.isObject()) {
        return std::nullopt;
    }
    PluginRegistration registration;
    const std::string kind = json.stringValue("kind").value_or("");
    if (kind == "command") {
        registration.kind = PluginRegistrationKind::Command;
    } else if (kind == "agentTool") {
        registration.kind = PluginRegistrationKind::AgentTool;
    } else if (kind == "buildProvider") {
        registration.kind = PluginRegistrationKind::BuildProvider;
    } else if (kind == "languageService") {
        registration.kind = PluginRegistrationKind::LanguageService;
    } else {
        return std::nullopt;
    }
    registration.id = json.stringValue("id").value_or("");
    registration.title = json.stringValue("title").value_or("");
    if (json.contains("metadata")) {
        registration.metadata = json["metadata"];
    }
    return registration;
}

std::string toString(PluginRegistrationKind kind) {
    switch (kind) {
    case PluginRegistrationKind::Command:
        return "command";
    case PluginRegistrationKind::AgentTool:
        return "agentTool";
    case PluginRegistrationKind::BuildProvider:
        return "buildProvider";
    case PluginRegistrationKind::LanguageService:
        return "languageService";
    }
    return "command";
}

}
