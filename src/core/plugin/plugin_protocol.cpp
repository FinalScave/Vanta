#include "vanta/plugin/plugin_protocol.h"

#include "vanta/core/json_codec.h"

namespace vanta {
namespace {

Value ParseJsonText(const std::string& text) {
    if (text.empty()) {
        return Value::ObjectValue();
    }
    Result<Value> parsed = ValueFromJsonText(text);
    return parsed ? parsed.Value() : Value::ObjectValue();
}

}

Value PluginRpcRequestProjection(const PluginRpcRequest& request) {
    return Value::ObjectValue({
        {"jsonrpc", Value("2.0")},
        {"id", Value(static_cast<std::int64_t>(request.id))},
        {"method", Value(request.method)},
        {"params", ParseJsonText(request.params_json)},
    });
}

Value PluginRpcResponseProjection(const PluginRpcResponse& response) {
    Value::Object object;
    object["jsonrpc"] = Value("2.0");
    object["id"] = Value(static_cast<std::int64_t>(response.id));
    if (response.ok) {
        object["result"] = ParseJsonText(response.result_json);
    } else {
        object["error"] = Value::ObjectValue({
            {"message", Value(response.error)},
        });
    }
    return Value::ObjectValue(std::move(object));
}

std::optional<PluginRpcResponse> ParsePluginRpcResponse(const Value& json) {
    if (!json.IsObject() || !json.Contains("id") || !json["id"].IsInt()) {
        return std::nullopt;
    }

    PluginRpcResponse response;
    response.id = static_cast<int>(json["id"].AsInt());
    if (json.Contains("result")) {
        response.ok = true;
        response.result_json = ValueToJsonText(json["result"]);
    } else if (json.Contains("error")) {
        response.ok = false;
        if (json["error"].IsObject()) {
            response.error = json["error"].StringValue("message").value_or("Plugin RPC error");
        } else {
            response.error = "Plugin RPC error";
        }
    }
    return response;
}

std::optional<PluginRpcResponse> ParsePluginRpcResponseText(const std::string& json_text) {
    Result<Value> parsed = ValueFromJsonText(json_text);
    if (!parsed) {
        return std::nullopt;
    }
    return ParsePluginRpcResponse(parsed.Value());
}

std::optional<PluginCapabilityRegistration> ParsePluginCapabilityRegistration(const Value& json) {
    if (!json.IsObject()) {
        return std::nullopt;
    }
    PluginCapabilityRegistration registration;
    const std::string kind = json.StringValue("kind").value_or("");
    if (kind == "command") {
        registration.kind = PluginCapabilityKind::Command;
    } else if (kind == "agentTool") {
        registration.kind = PluginCapabilityKind::AgentTool;
    } else if (kind == "buildProvider") {
        registration.kind = PluginCapabilityKind::BuildProvider;
    } else if (kind == "languageService") {
        registration.kind = PluginCapabilityKind::LanguageService;
    } else if (kind == "modelProvider") {
        registration.kind = PluginCapabilityKind::ModelProvider;
    } else if (kind == "debugProvider") {
        registration.kind = PluginCapabilityKind::DebugProvider;
    } else {
        return std::nullopt;
    }
    registration.id = json.StringValue("id").value_or("");
    registration.title = json.StringValue("title").value_or("");
    if (json.Contains("metadata")) {
        registration.metadata = json["metadata"];
    }
    return registration;
}

std::string FormatPluginRpcRequestText(const PluginRpcRequest& request) {
    return ValueToJsonText(PluginRpcRequestProjection(request));
}

std::string FormatPluginRpcResponseText(const PluginRpcResponse& response) {
    return ValueToJsonText(PluginRpcResponseProjection(response));
}

std::string ToString(PluginCapabilityKind kind) {
    switch (kind) {
    case PluginCapabilityKind::Command:
        return "command";
    case PluginCapabilityKind::AgentTool:
        return "agentTool";
    case PluginCapabilityKind::BuildProvider:
        return "buildProvider";
    case PluginCapabilityKind::LanguageService:
        return "languageService";
    case PluginCapabilityKind::ModelProvider:
        return "modelProvider";
    case PluginCapabilityKind::DebugProvider:
        return "debugProvider";
    }
    return "command";
}

}
