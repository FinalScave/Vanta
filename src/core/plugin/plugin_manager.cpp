#include "vanta/plugin/plugin_manager.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <utility>

#include "internal/projection.h"
#include "vanta/agent/model_service.h"
#include "vanta/core/localization.h"
#include "vanta/debug/debug_service.h"
#include "vanta/project/project.h"
#include "vanta/plugin/plugin_process_host.h"
#include "vanta/core/json_codec.h"
#include "vanta/workspace/workspace_runtime.h"

namespace vanta {
namespace {

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::ostringstream stream;
    stream << input.rdbuf();
    return stream.str();
}

std::vector<std::string> ParseStringArray(const Value& object, const std::string& key) {
    std::vector<std::string> result;
    if (!object.Contains(key) || !object[key].IsArray()) {
        return result;
    }
    for (const Value& item : object[key].AsArray()) {
        if (item.IsString()) {
            result.push_back(item.AsString());
        }
    }
    return result;
}

const std::string& SupportedPluginApiVersion() {
    static const std::string version = "0.1.0";
    return version;
}

std::optional<PluginRpcResponse> SendPluginRequest(PluginProcessHost& host, std::string method, const Value& params) {
    return host.SendRequest(std::move(method), ValueToJsonText(params));
}

Value ResponseValue(const PluginRpcResponse& response) {
    Result<Value> parsed = ValueFromJsonText(response.result_json);
    return parsed ? parsed.Value() : Value::ObjectValue();
}

std::vector<int> ParseVersion(const std::string& value) {
    std::vector<int> parts;
    std::string current;
    for (char character : value) {
        if (character == '.') {
            parts.push_back(current.empty() ? 0 : std::stoi(current));
            current.clear();
        } else if (std::isdigit(static_cast<unsigned char>(character))) {
            current.push_back(character);
        } else {
            break;
        }
    }
    parts.push_back(current.empty() ? 0 : std::stoi(current));
    while (parts.size() < 3) {
        parts.push_back(0);
    }
    return parts;
}

int CompareVersion(const std::string& left, const std::string& right) {
    const std::vector<int> left_parts = ParseVersion(left);
    const std::vector<int> right_parts = ParseVersion(right);
    const std::size_t count = std::max(left_parts.size(), right_parts.size());
    for (std::size_t index = 0; index < count; ++index) {
        const int left_value = index < left_parts.size() ? left_parts[index] : 0;
        const int right_value = index < right_parts.size() ? right_parts[index] : 0;
        if (left_value != right_value) {
            return left_value < right_value ? -1 : 1;
        }
    }
    return 0;
}

bool SupportedPluginCapability(const std::string& capability) {
    static const std::set<std::string> capabilities = {
        "command",
        "agentTool",
        "buildProvider",
        "languageService",
        "debugProvider",
        "modelProvider",
        "workspaceRead",
        "workspaceWrite",
        "processExecute",
    };
    return capabilities.contains(capability);
}

bool StartsWith(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

std::string LocaleFromPropertiesPath(const std::filesystem::path& path) {
    return path.stem().string();
}

void RegisterPluginLocalizationCatalogs(
    const PluginManifest& manifest,
    WorkspaceContext& workspace,
    Logger& logger,
    PluginActivationState& activation_state) {
    const std::filesystem::path i18n_directory = manifest.extension.location / "i18n";
    std::error_code error;
    if (!std::filesystem::exists(i18n_directory, error) || !std::filesystem::is_directory(i18n_directory, error)) {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(i18n_directory, error)) {
        if (error) {
            logger.Warn("Failed to Scan localization catalogs for " + manifest.extension.id + ": " + error.message());
            return;
        }
        if (!entry.is_regular_file() || entry.path().extension() != ".properties") {
            continue;
        }
        Result<LocalizationCatalog> catalog = ReadLocalizationProperties(
            manifest.extension.id,
            LocaleFromPropertiesPath(entry.path()),
            entry.path());
        if (!catalog) {
            logger.Warn("Failed to load localization catalog " + entry.path().string() + ": " + catalog.ErrorValue().message);
            continue;
        }
        activation_state.Track(workspace.Localization().RegisterCatalog(std::move(catalog.Value())));
    }
}

bool ValueBool(const Value& object, const std::string& key, bool fallback = false) {
    if (!object.IsObject() || !object.Contains(key) || !object[key].IsBool()) {
        return fallback;
    }
    return object[key].AsBool();
}

int JsonIntValue(const Value& object, const std::string& key, int fallback = 0) {
    if (!object.IsObject() || !object.Contains(key) || !object[key].IsInt()) {
        return fallback;
    }
    return static_cast<int>(object[key].AsInt());
}

Value BuildRequestProjection(const BuildRequest& request) {
    return Value::ObjectValue({
        {"kind", Value(ToString(request.kind))},
        {"providerId", Value(request.provider_id)},
        {"profileId", Value(request.profile_id)},
        {"targetId", Value(request.target_id)},
        {"executionTargetId", Value(request.execution_target_id)},
        {"buildDirectory", Value(request.build_directory_override.string())},
        {"jobId", Value(static_cast<std::int64_t>(request.job_id))},
    });
}

ExecutionRequest ExecutionRequestFromValue(const Value& json) {
    ExecutionRequest request;
    if (!json.IsObject()) {
        return request;
    }
    request.executable = json.StringValue("executable").value_or("");
    request.arguments = ParseStringArray(json, "arguments");
    if (auto working_directory = json.StringValue("workingDirectory")) {
        request.working_directory = *working_directory;
    }
    if (json.Contains("jobId") && json["jobId"].IsInt()) {
        request.job_id = static_cast<JobId>(json["jobId"].AsInt());
    }
    return request;
}

BuildEnvironment BuildEnvironmentFromValue(const Value& json, const std::string& provider_id) {
    BuildEnvironment environment;
    if (!json.IsObject()) {
        return environment;
    }
    environment.provider_id = json.StringValue("providerId").value_or(provider_id);
    environment.detected = ValueBool(json, "detected");
    if (auto build_directory = json.StringValue("buildDirectory")) {
        environment.build_directory = *build_directory;
    }
    return environment;
}

BuildPlan BuildPlanFromValue(const Value& json, const std::string& provider_id) {
    BuildPlan plan;
    if (!json.IsObject()) {
        return plan;
    }
    plan.provider_id = json.StringValue("providerId").value_or(provider_id);
    plan.title = json.StringValue("title").value_or("");
    if (json.Contains("steps") && json["steps"].IsArray()) {
        for (const Value& item : json["steps"].AsArray()) {
            if (!item.IsObject()) {
                continue;
            }
            BuildStep step;
            step.title = item.StringValue("title").value_or("");
            if (item.Contains("request")) {
                step.request = ExecutionRequestFromValue(item["request"]);
            }
            step.parse_diagnostics = ValueBool(item, "parseDiagnostics", true);
            plan.steps.push_back(std::move(step));
        }
    }
    return plan;
}

Value ModelMessageProjection(const ModelMessage& message) {
    return Value::ObjectValue({
        {"role", Value(ToString(message.role))},
        {"content", Value(message.content)},
        {"name", Value(message.name)},
    });
}

Value ModelToolDefinitionProjection(const ModelToolDefinition& tool) {
    return Value::ObjectValue({
        {"id", Value(tool.id)},
        {"description", Value(tool.description)},
        {"inputSchema", tool.input_schema},
    });
}

Value ModelRequestProjection(const ModelRequest& request) {
    Value::Array messages;
    for (const ModelMessage& message : request.messages) {
        messages.push_back(ModelMessageProjection(message));
    }
    Value::Array tools;
    for (const ModelToolDefinition& tool : request.tools) {
        tools.push_back(ModelToolDefinitionProjection(tool));
    }
    return Value::ObjectValue({
        {"modelId", Value(request.model_id)},
        {"messages", Value::ArrayValue(std::move(messages))},
        {"tools", Value::ArrayValue(std::move(tools))},
        {"temperature", Value(request.temperature)},
        {"maxOutputTokens", Value(static_cast<std::int64_t>(request.max_output_tokens))},
    });
}

ModelToolCall ModelToolCallFromValue(const Value& json) {
    ModelToolCall call;
    if (!json.IsObject()) {
        return call;
    }
    call.id = json.StringValue("id").value_or("");
    call.tool_id = json.StringValue("toolId").value_or("");
    if (json.Contains("input")) {
        call.input = json["input"];
    }
    return call;
}

ModelInfo ModelInfoFromValue(const Value& json, const std::string& provider_id) {
    ModelInfo info;
    if (!json.IsObject()) {
        return info;
    }
    info.id = json.StringValue("id").value_or("");
    info.provider_id = json.StringValue("providerId").value_or(provider_id);
    info.display_name = json.StringValue("displayName").value_or(info.id);
    info.capabilities = ParseStringArray(json, "capabilities");
    return info;
}

ModelResponse ModelResponseFromValue(const Value& json, const std::string& provider_id, const std::string& model_id) {
    ModelResponse response;
    if (!json.IsObject()) {
        response.error = "Model response was invalid";
        return response;
    }
    response.ok = ValueBool(json, "ok", true);
    response.model_id = json.StringValue("modelId").value_or(model_id);
    response.provider_id = json.StringValue("providerId").value_or(provider_id);
    response.content = json.StringValue("content").value_or("");
    response.error = json.StringValue("error").value_or("");
    response.payload = json;
    if (json.Contains("toolCalls") && json["toolCalls"].IsArray()) {
        for (const Value& item : json["toolCalls"].AsArray()) {
            ModelToolCall call = ModelToolCallFromValue(item);
            if (!call.tool_id.empty()) {
                response.tool_calls.push_back(std::move(call));
            }
        }
    }
    return response;
}

Value DebugConfigurationProjection(const DebugConfiguration& configuration) {
    return Value::ObjectValue({
        {"id", Value(configuration.id)},
        {"name", Value(configuration.name)},
        {"type", Value(configuration.type)},
        {"target", internal::ExecutionTargetProjection(configuration.target)},
        {"data", configuration.payload.value_or(Value::ObjectValue())},
    });
}

DebugSessionStatus DebugSessionStatusFromString(const std::string& value) {
    if (value == "starting") {
        return DebugSessionStatus::Starting;
    }
    if (value == "running") {
        return DebugSessionStatus::Running;
    }
    if (value == "paused") {
        return DebugSessionStatus::Paused;
    }
    if (value == "stopped") {
        return DebugSessionStatus::Stopped;
    }
    if (value == "failed") {
        return DebugSessionStatus::Failed;
    }
    return DebugSessionStatus::Pending;
}

DebugSession DebugSessionFromValue(const Value& json, DebugSessionId session_id, const std::string& provider_id, const DebugConfiguration& configuration) {
    DebugSession session;
    session.id = session_id;
    session.provider_id = provider_id;
    session.configuration = configuration;
    session.status = DebugSessionStatus::Running;
    if (!json.IsObject()) {
        return session;
    }
    if (json.Contains("id") && json["id"].IsInt()) {
        session.id = static_cast<DebugSessionId>(json["id"].AsInt());
    }
    session.provider_id = json.StringValue("providerId").value_or(provider_id);
    session.message = json.StringValue("message").value_or("");
    session.status = DebugSessionStatusFromString(json.StringValue("status").value_or("running"));
    if (json.Contains("data")) {
        session.payload = json["data"];
    }
    return session;
}

DebugEvaluationResult DebugEvaluationFromValue(const std::optional<PluginRpcResponse>& response) {
    if (!response) {
        return {.ok = false, .error = "Plugin did not respond"};
    }
    if (!response->ok) {
        return {.ok = false, .error = response->error};
    }
    const Value json = ResponseValue(*response);
    if (!json.IsObject()) {
        return {.ok = false, .error = "Debug evaluation response was invalid"};
    }
    DebugEvaluationResult result;
    result.ok = ValueBool(json, "ok", true);
    result.value = json.StringValue("value").value_or("");
    result.type = json.StringValue("type").value_or("");
    result.error = json.StringValue("error").value_or("");
    if (json.Contains("data")) {
        result.payload = json["data"];
    }
    return result;
}

Value TextPositionProjection(TextPosition position) {
    return Value::ObjectValue({
        {"line", Value(static_cast<std::int64_t>(position.line))},
        {"character", Value(static_cast<std::int64_t>(position.character))},
    });
}

TextPosition TextPositionFromValue(const Value& json) {
    TextPosition position;
    position.line = JsonIntValue(json, "line");
    position.character = JsonIntValue(json, "character");
    return position;
}

TextRange TextRangeFromValue(const Value& json) {
    TextRange range;
    if (json.IsObject() && json.Contains("start")) {
        range.start = TextPositionFromValue(json["start"]);
    }
    if (json.IsObject() && json.Contains("end")) {
        range.end = TextPositionFromValue(json["end"]);
    }
    return range;
}

StackFrame StackFrameFromValue(const Value& json) {
    StackFrame frame;
    if (!json.IsObject()) {
        return frame;
    }
    if (json.Contains("id") && json["id"].IsInt()) {
        frame.id = static_cast<std::uint64_t>(json["id"].AsInt());
    }
    frame.name = json.StringValue("name").value_or("");
    const std::string uri = json.StringValue("uri").value_or(json.StringValue("file").value_or(""));
    if (!uri.empty()) {
        frame.file = VirtualFile(Uri::Parse(uri), nullptr);
    }
    if (json.Contains("range")) {
        frame.range = TextRangeFromValue(json["range"]);
    }
    return frame;
}

DebugVariable DebugVariableFromValue(const Value& json) {
    DebugVariable variable;
    if (!json.IsObject()) {
        return variable;
    }
    variable.name = json.StringValue("name").value_or("");
    variable.value = json.StringValue("value").value_or("");
    variable.type = json.StringValue("type").value_or("");
    variable.expandable = ValueBool(json, "expandable");
    return variable;
}

Value DocumentIdentifierProjection(const TextDocumentIdentifier& document) {
    return Value::ObjectValue({
        {"uri", Value(document.file.ToUri().ToString())},
        {"languageId", Value(document.language_id)},
    });
}

Value DocumentPositionProjection(const TextDocumentPosition& request) {
    return Value::ObjectValue({
        {"document", DocumentIdentifierProjection(request.document)},
        {"position", TextPositionProjection(request.position)},
    });
}

CompletionList CompletionListFromPluginResponse(const std::optional<PluginRpcResponse>& response) {
    CompletionList result;
    result.ok = response && response->ok;
    if (!response) {
        result.error = "Plugin did not respond";
        return result;
    }
    if (!response->ok) {
        result.error = response->error;
        return result;
    }
    const Value json = ResponseValue(*response);
    result.incomplete = ValueBool(json, "incomplete");
    if (json.Contains("items") && json["items"].IsArray()) {
        for (const Value& item : json["items"].AsArray()) {
            if (!item.IsObject()) {
                continue;
            }
            result.items.push_back({
                .label = item.StringValue("label").value_or(""),
                .insert_text = item.StringValue("insertText").value_or(""),
                .detail = item.StringValue("detail").value_or(""),
                .documentation = item.StringValue("documentation").value_or(""),
            });
        }
    }
    return result;
}

HoverResult HoverResultFromPluginResponse(const std::optional<PluginRpcResponse>& response) {
    HoverResult result;
    result.ok = response && response->ok;
    if (!response) {
        result.error = "Plugin did not respond";
        return result;
    }
    if (!response->ok) {
        result.error = response->error;
        return result;
    }
    const Value json = ResponseValue(*response);
    result.contents = json.StringValue("contents").value_or("");
    return result;
}

LocationResult LocationResultFromPluginResponse(const std::optional<PluginRpcResponse>& response) {
    LocationResult result;
    result.ok = response && response->ok;
    if (!response) {
        result.error = "Plugin did not respond";
        return result;
    }
    if (!response->ok) {
        result.error = response->error;
        return result;
    }
    const Value json = ResponseValue(*response);
    if (json.Contains("locations") && json["locations"].IsArray()) {
        for (const Value& item : json["locations"].AsArray()) {
            if (!item.IsObject()) {
                continue;
            }
            const std::string uri = item.StringValue("uri").value_or(item.StringValue("file").value_or(""));
            if (uri.empty()) {
                continue;
            }
            Location location;
            location.file = VirtualFile(Uri::Parse(uri), nullptr);
            if (item.Contains("range")) {
                location.range = TextRangeFromValue(item["range"]);
            }
            result.locations.push_back(std::move(location));
        }
    }
    return result;
}

SemanticTokens SemanticTokensFromPluginResponse(const std::optional<PluginRpcResponse>& response) {
    SemanticTokens result;
    result.ok = response && response->ok;
    if (!response) {
        result.error = "Plugin did not respond";
        return result;
    }
    if (!response->ok) {
        result.error = response->error;
        return result;
    }
    const Value json = ResponseValue(*response);
    if (json.Contains("data") && json["data"].IsArray()) {
        for (const Value& item : json["data"].AsArray()) {
            if (item.IsInt()) {
                result.data.push_back(item.AsInt());
            }
        }
    }
    return result;
}

class ExternalBuildProvider final : public BuildProvider {
public:
    ExternalBuildProvider(PluginProcessHost& host, std::string plugin_id, std::string provider_id)
        : host_(&host), plugin_id_(std::move(plugin_id)), provider_id_(std::move(provider_id)) {}

    std::string Id() const override {
        return provider_id_;
    }

    BuildEnvironment Detect(WorkspaceContext& context, const ProjectModel& project) const override {
        auto response = SendPluginRequest(*host_, "build.detect", Value::ObjectValue({
            {"pluginId", Value(plugin_id_)},
            {"id", Value(provider_id_)},
            {"workspaceRoot", Value(context.CurrentWorkspace().Info().root_path.string())},
            {"projectType", Value(PrimaryProjectType(project))},
        }));
        if (!response || !response->ok) {
            return {};
        }
        return BuildEnvironmentFromValue(ResponseValue(*response), provider_id_);
    }

    BuildPlan Plan(WorkspaceContext& context, const BuildRequest& request) const override {
        auto response = SendPluginRequest(*host_, "build.plan", Value::ObjectValue({
            {"pluginId", Value(plugin_id_)},
            {"id", Value(provider_id_)},
            {"workspaceRoot", Value(context.CurrentWorkspace().Info().root_path.string())},
            {"request", BuildRequestProjection(request)},
        }));
        if (!response || !response->ok) {
            return {};
        }
        return BuildPlanFromValue(ResponseValue(*response), provider_id_);
    }

private:
    PluginProcessHost* host_ = nullptr;
    std::string plugin_id_;
    std::string provider_id_;
};

class ExternalModelProvider final : public ModelProvider {
public:
    ExternalModelProvider(PluginProcessHost& host, std::string plugin_id, std::string provider_id, Value metadata)
        : host_(&host), plugin_id_(std::move(plugin_id)), provider_id_(std::move(provider_id)), metadata_(std::move(metadata)) {}

    std::string Id() const override {
        return provider_id_;
    }

    std::vector<ModelInfo> Models() const override {
        auto response = SendPluginRequest(*host_, "model.models", Value::ObjectValue({
            {"pluginId", Value(plugin_id_)},
            {"id", Value(provider_id_)},
        }));
        const Value json = response && response->ok ? ResponseValue(*response) : Value::ObjectValue();
        if (response && response->ok && json.Contains("models") && json["models"].IsArray()) {
            std::vector<ModelInfo> result;
            for (const Value& item : json["models"].AsArray()) {
                ModelInfo info = ModelInfoFromValue(item, provider_id_);
                if (!info.id.empty()) {
                    result.push_back(std::move(info));
                }
            }
            return result;
        }
        if (metadata_.Contains("models") && metadata_["models"].IsArray()) {
            std::vector<ModelInfo> result;
            for (const Value& item : metadata_["models"].AsArray()) {
                ModelInfo info = ModelInfoFromValue(item, provider_id_);
                if (!info.id.empty()) {
                    result.push_back(std::move(info));
                }
            }
            return result;
        }
        return {};
    }

    ModelResponse Complete(const ModelRequest& request, ModelStreamCallback on_event = {}) const override {
        if (on_event) {
            on_event({.kind = ModelStreamEventKind::Started});
        }
        auto response = SendPluginRequest(*host_, "model.complete", Value::ObjectValue({
            {"pluginId", Value(plugin_id_)},
            {"id", Value(provider_id_)},
            {"request", ModelRequestProjection(request)},
        }));
        if (!response) {
            if (on_event) {
                on_event({.kind = ModelStreamEventKind::Failed, .error = "Plugin did not respond"});
            }
            return {
                .ok = false,
                .model_id = request.model_id,
                .provider_id = provider_id_,
                .error = "Plugin did not respond",
            };
        }
        if (!response->ok) {
            if (on_event) {
                on_event({.kind = ModelStreamEventKind::Failed, .error = response->error});
            }
            return {
                .ok = false,
                .model_id = request.model_id,
                .provider_id = provider_id_,
                .error = response->error,
            };
        }
        ModelResponse result = ModelResponseFromValue(ResponseValue(*response), provider_id_, request.model_id);
        if (on_event) {
            on_event({
                .kind = result.ok ? ModelStreamEventKind::Completed : ModelStreamEventKind::Failed,
                .text = result.content,
                .error = result.error,
                .payload = result.payload,
            });
        }
        return result;
    }

private:
    PluginProcessHost* host_ = nullptr;
    std::string plugin_id_;
    std::string provider_id_;
    Value metadata_;
};

class ExternalDebugProvider final : public DebugProvider {
public:
    ExternalDebugProvider(PluginProcessHost& host, std::string plugin_id, std::string provider_id, Value metadata)
        : host_(&host), plugin_id_(std::move(plugin_id)), provider_id_(std::move(provider_id)), metadata_(std::move(metadata)) {}

    std::string Id() const override {
        return provider_id_;
    }

    std::vector<std::string> ConfigurationTypes() const override {
        std::vector<std::string> types = ParseStringArray(metadata_, "configurationTypes");
        if (types.empty()) {
            types.push_back(provider_id_);
        }
        return types;
    }

    DebugSession Start(
        WorkspaceContext&,
        DebugSessionId session_id,
        const DebugConfiguration& configuration,
        DebugEventCallback on_event = {}) override {
        auto response = SendPluginRequest(*host_, "debug.start", Value::ObjectValue({
            {"pluginId", Value(plugin_id_)},
            {"id", Value(provider_id_)},
            {"sessionId", Value(static_cast<std::int64_t>(session_id))},
            {"configuration", DebugConfigurationProjection(configuration)},
        }));
        if (!response || !response->ok) {
            DebugSession failed;
            failed.id = session_id;
            failed.provider_id = provider_id_;
            failed.configuration = configuration;
            failed.status = DebugSessionStatus::Failed;
            failed.message = response ? response->error : "Plugin did not respond";
            if (on_event) {
                on_event({
                    .session_id = session_id,
                    .kind = DebugEventKind::Failed,
                    .message = failed.message,
                });
            }
            return failed;
        }
        DebugSession session = DebugSessionFromValue(ResponseValue(*response), session_id, provider_id_, configuration);
        if (on_event) {
            on_event({
                .session_id = session.id,
                .kind = session.status == DebugSessionStatus::Failed ? DebugEventKind::Failed : DebugEventKind::Started,
                .message = session.message,
                .payload = session.payload,
            });
        }
        return session;
    }

    bool Stop(DebugSessionId session_id) override {
        return BoolResponse("debug.stop", session_id);
    }

    bool ContinueSession(DebugSessionId session_id) override {
        return BoolResponse("debug.continue", session_id);
    }

    bool Pause(DebugSessionId session_id) override {
        return BoolResponse("debug.pause", session_id);
    }

    DebugEvaluationResult Evaluate(DebugSessionId session_id, const std::string& expression, std::uint64_t frame_id = 0) override {
        return DebugEvaluationFromValue(SendPluginRequest(*host_, "debug.evaluate", Value::ObjectValue({
            {"pluginId", Value(plugin_id_)},
            {"id", Value(provider_id_)},
            {"sessionId", Value(static_cast<std::int64_t>(session_id))},
            {"expression", Value(expression)},
            {"frameId", Value(static_cast<std::int64_t>(frame_id))},
        })));
    }

    std::vector<StackFrame> StackTrace(DebugSessionId session_id) const override {
        auto response = SendPluginRequest(*host_, "debug.stackTrace", Value::ObjectValue({
            {"pluginId", Value(plugin_id_)},
            {"id", Value(provider_id_)},
            {"sessionId", Value(static_cast<std::int64_t>(session_id))},
        }));
        const Value json = response && response->ok ? ResponseValue(*response) : Value::ObjectValue();
        if (!response || !response->ok || !json.Contains("frames") || !json["frames"].IsArray()) {
            return {};
        }
        std::vector<StackFrame> frames;
        for (const Value& item : json["frames"].AsArray()) {
            StackFrame frame = StackFrameFromValue(item);
            if (frame.id != 0 || !frame.name.empty()) {
                frames.push_back(std::move(frame));
            }
        }
        return frames;
    }

    std::vector<DebugVariable> Variables(DebugSessionId session_id, std::uint64_t frame_id) const override {
        auto response = SendPluginRequest(*host_, "debug.variables", Value::ObjectValue({
            {"pluginId", Value(plugin_id_)},
            {"id", Value(provider_id_)},
            {"sessionId", Value(static_cast<std::int64_t>(session_id))},
            {"frameId", Value(static_cast<std::int64_t>(frame_id))},
        }));
        const Value json = response && response->ok ? ResponseValue(*response) : Value::ObjectValue();
        if (!response || !response->ok || !json.Contains("variables") || !json["variables"].IsArray()) {
            return {};
        }
        std::vector<DebugVariable> values;
        for (const Value& item : json["variables"].AsArray()) {
            DebugVariable variable = DebugVariableFromValue(item);
            if (!variable.name.empty()) {
                values.push_back(std::move(variable));
            }
        }
        return values;
    }

private:
    bool BoolResponse(const std::string& method, DebugSessionId session_id) {
        auto response = SendPluginRequest(*host_, method, Value::ObjectValue({
            {"pluginId", Value(plugin_id_)},
            {"id", Value(provider_id_)},
            {"sessionId", Value(static_cast<std::int64_t>(session_id))},
        }));
        if (!response || !response->ok) {
            return false;
        }
        const Value json = ResponseValue(*response);
        if (json.IsObject() && json.Contains("ok") && json["ok"].IsBool()) {
            return json["ok"].AsBool();
        }
        return true;
    }

    PluginProcessHost* host_ = nullptr;
    std::string plugin_id_;
    std::string provider_id_;
    Value metadata_;
};

class ExternalLanguageService final : public LanguageService {
public:
    ExternalLanguageService(PluginProcessHost& host, std::string plugin_id, std::string service_id)
        : host_(&host), plugin_id_(std::move(plugin_id)), service_id_(std::move(service_id)) {}

    bool Start(std::string* error_message = nullptr) override {
        auto response = SendPluginRequest(*host_, "language.start", Value::ObjectValue({
            {"pluginId", Value(plugin_id_)},
            {"id", Value(service_id_)},
        }));
        running_ = response && response->ok;
        if (!running_ && error_message != nullptr) {
            *error_message = response ? response->error : "Plugin did not respond";
        }
        return running_;
    }

    bool Running() const override {
        return running_;
    }

    void Stop() override {
        SendPluginRequest(*host_, "language.stop", Value::ObjectValue({
            {"pluginId", Value(plugin_id_)},
            {"id", Value(service_id_)},
        }));
        running_ = false;
    }

    CompletionList Completion(const TextDocumentPosition& request) override {
        return CompletionListFromPluginResponse(SendDocumentPosition("language.completion", request));
    }

    HoverResult Hover(const TextDocumentPosition& request) override {
        return HoverResultFromPluginResponse(SendDocumentPosition("language.hover", request));
    }

    LocationResult Definition(const TextDocumentPosition& request) override {
        return LocationResultFromPluginResponse(SendDocumentPosition("language.definition", request));
    }

    SemanticTokens SemanticTokensFull(const TextDocumentIdentifier& document) override {
        auto response = SendPluginRequest(*host_, "language.semanticTokensFull", Value::ObjectValue({
            {"pluginId", Value(plugin_id_)},
            {"id", Value(service_id_)},
            {"document", DocumentIdentifierProjection(document)},
        }));
        return SemanticTokensFromPluginResponse(response);
    }

private:
    std::optional<PluginRpcResponse> SendDocumentPosition(const std::string& method, const TextDocumentPosition& request) const {
        return SendPluginRequest(*host_, method, Value::ObjectValue({
            {"pluginId", Value(plugin_id_)},
            {"id", Value(service_id_)},
            {"request", DocumentPositionProjection(request)},
        }));
    }

    PluginProcessHost* host_ = nullptr;
    std::string plugin_id_;
    std::string service_id_;
    bool running_ = false;
};

}

void ConsoleLogger::Info(const std::string& message) {
    std::cout << "[info] " << message << '\n';
}

void ConsoleLogger::Warn(const std::string& message) {
    std::cout << "[warn] " << message << '\n';
}

void ConsoleLogger::Error(const std::string& message) {
    std::cerr << "[error] " << message << '\n';
}

void PluginActivationState::Track(RegistrationHandle registration) {
    if (registration.Registered()) {
        registrations.push_back(std::move(registration));
    }
}

void PluginActivationState::Clear() {
    for (RegistrationHandle& registration : registrations) {
        registration.Unregister();
    }
    registrations.clear();
    language_services.clear();
}

std::size_t PluginActivationState::RegistrationCount() const {
    return registrations.size();
}

BasicExtensionContext::BasicExtensionContext(
    ExtensionInfo extension,
    std::shared_ptr<PluginActivationState> activation_state,
    Logger& logger,
    WorkspaceContext& workspace)
    : extension_(std::move(extension)),
      activation_state_(std::move(activation_state)),
      logger_(logger),
      workspace_(workspace) {
}

const ExtensionInfo& BasicExtensionContext::Extension() const {
    return extension_;
}

const WorkspaceInfo& BasicExtensionContext::Workspace() const {
    return workspace_.CurrentWorkspace().Info();
}

Logger& BasicExtensionContext::Log() {
    return logger_;
}

WorkspaceContext& BasicExtensionContext::Context() {
    return workspace_;
}

Localizer BasicExtensionContext::LocalizerValue() const {
    return workspace_.LocalizerFor(extension_.id);
}

PluginStorageService& BasicExtensionContext::Storage() {
    return workspace_.PluginStorage();
}

void BasicExtensionContext::Track(RegistrationHandle registration) {
    activation_state_->Track(std::move(registration));
}

void PluginProcessHostDeleter::operator()(PluginProcessHost* host) const {
    delete host;
}

std::string ToString(PluginLifecycleState state) {
    switch (state) {
    case PluginLifecycleState::Discovered:
        return "discovered";
    case PluginLifecycleState::Activating:
        return "activating";
    case PluginLifecycleState::Active:
        return "active";
    case PluginLifecycleState::Deactivating:
        return "deactivating";
    case PluginLifecycleState::Inactive:
        return "inactive";
    case PluginLifecycleState::Failed:
        return "failed";
    }
    return "discovered";
}

PluginManager::~PluginManager() {
    DeactivateAll();
}

std::vector<PluginManifest> PluginManager::Scan(const std::filesystem::path& plugins_root) {
    plugins_root_ = plugins_root;
    manifests_.clear();
    lifecycle_.clear();
    if (!std::filesystem::exists(plugins_root)) {
        return manifests_;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(plugins_root)) {
        if (!entry.is_regular_file() || entry.path().filename() != "vanta.plugin.json") {
            continue;
        }
        if (auto manifest = LoadManifest(entry.path())) {
            UpdateLifecycle(*manifest, PluginLifecycleState::Discovered, manifest->runtime_kind == "process");
            manifests_.push_back(std::move(*manifest));
        }
    }
    return manifests_;
}

std::optional<PluginManifest> PluginManager::LoadManifest(const std::filesystem::path& manifest_path) const {
    Result<Value> parsed = ValueFromJsonText(ReadFile(manifest_path));
    if (!parsed) {
        return std::nullopt;
    }
    const Value& root = parsed.Value();
    if (!root.IsObject()) {
        return std::nullopt;
    }

    PluginManifest manifest;
    manifest.extension.id = root.StringValue("id").value_or("");
    manifest.extension.name = root.StringValue("name").value_or(manifest.extension.id);
    manifest.extension.version = root.StringValue("version").value_or("0.0.0");
    manifest.extension.publisher = root.StringValue("publisher").value_or("");
    manifest.extension.location = manifest_path.parent_path();

    if (root.Contains("runtime") && root["runtime"].IsObject()) {
        const Value& runtime = root["runtime"];
        manifest.runtime_kind = runtime.StringValue("kind").value_or("");
        manifest.entry = runtime.StringValue("entry").value_or("");
    }

    manifest.min_api_version = root.StringValue("minApiVersion").value_or("");
    manifest.target_api_version = root.StringValue("targetApiVersion").value_or("");
    manifest.capabilities = ParseStringArray(root, "capabilities");
    manifest.activation_events = ParseStringArray(root, "activationEvents");

    if (manifest.extension.id.empty()) {
        return std::nullopt;
    }
    return manifest;
}

std::vector<std::string> PluginManager::ActivateCorePlugins(
    const CorePluginRegistry& registry,
    Logger& logger,
    WorkspaceContext& workspace) {
    std::vector<std::string> messages;
    std::set<std::string> attempted;
    bool activated = false;

    do {
        activated = false;
        for (const PluginManifest& manifest : manifests_) {
            if (manifest.runtime_kind != "core" ||
                IsActive(manifest.extension.id) ||
                attempted.contains(manifest.extension.id) ||
                !ShouldActivate(manifest, workspace)) {
                continue;
            }
            attempted.insert(manifest.extension.id);

            const std::string compatibility = CompatibilityError(manifest);
            if (!compatibility.empty()) {
                UpdateLifecycle(manifest, PluginLifecycleState::Failed, false, compatibility);
                messages.push_back("Failed to Activate " + manifest.extension.id + ": " + compatibility);
                continue;
            }

            std::unique_ptr<CoreExtension> extension = registry.Create(manifest.entry);
            if (!extension) {
                UpdateLifecycle(manifest, PluginLifecycleState::Failed, false, "No core plugin registered for " + manifest.entry);
                messages.push_back("No core plugin registered for " + manifest.entry);
                continue;
            }

            UpdateLifecycle(manifest, PluginLifecycleState::Activating, false);
            auto activation_state = std::make_shared<PluginActivationState>();
            RegisterPluginLocalizationCatalogs(manifest, workspace, logger, *activation_state);
            auto context = std::make_unique<BasicExtensionContext>(
                manifest.extension,
                activation_state,
                logger,
                workspace);

            try {
                extension->Activate(*context);
                active_sessions_.push_back({
                    .manifest = manifest,
                    .activation_state = std::move(activation_state),
                    .extension = std::move(extension),
                    .process_host = nullptr,
                    .context = std::move(context),
                    .unloadable = false,
                    .state = PluginLifecycleState::Active,
                });
                UpdateLifecycle(manifest, PluginLifecycleState::Active, false, {}, active_sessions_.back().activation_state->RegistrationCount());
                messages.push_back("Activated " + manifest.extension.id);
                activated = true;
            } catch (const std::exception& exception) {
                activation_state->Clear();
                UpdateLifecycle(manifest, PluginLifecycleState::Failed, false, exception.what());
                messages.push_back("Failed to Activate " + manifest.extension.id + ": " + exception.what());
            }
        }
    } while (activated);

    return messages;
}

std::vector<std::string> PluginManager::ReloadCorePlugins(
    const CorePluginRegistry& registry,
    Logger& logger,
    WorkspaceContext& workspace) {
    DeactivateAll();
    if (!plugins_root_.empty()) {
        Scan(plugins_root_);
    }
    return ActivateCorePlugins(registry, logger, workspace);
}

std::vector<std::string> PluginManager::ActivateExternalPlugins(
    Logger& logger,
    WorkspaceContext& workspace) {
    ReconcileProcessHealth();
    std::vector<std::string> messages;
    for (const PluginManifest& manifest : manifests_) {
        if (manifest.runtime_kind != "process" ||
            IsActive(manifest.extension.id) ||
            !ShouldActivate(manifest, workspace)) {
            continue;
        }
        const std::string compatibility = CompatibilityError(manifest);
        if (!compatibility.empty()) {
            UpdateLifecycle(manifest, PluginLifecycleState::Failed, true, compatibility);
            messages.push_back("Failed to Activate " + manifest.extension.id + ": " + compatibility);
            continue;
        }
        std::string message = ActivateExternalPlugin(manifest, logger, workspace);
        if (StartsWith(message, "Failed")) {
            logger.Warn(message);
        }
        messages.push_back(std::move(message));
    }
    return messages;
}

bool PluginManager::UnloadPlugin(const std::string& plugin_id, std::string* message) {
    for (auto it = active_sessions_.begin(); it != active_sessions_.end(); ++it) {
        if (it->manifest.extension.id != plugin_id) {
            continue;
        }
        if (!it->unloadable) {
            if (message != nullptr) {
                *message = "Built-in plugin cannot be unloaded: " + plugin_id;
            }
            return false;
        }
        it->state = PluginLifecycleState::Deactivating;
        UpdateLifecycle(it->manifest, PluginLifecycleState::Deactivating, it->unloadable, {}, it->activation_state ? it->activation_state->RegistrationCount() : 0);
        if (it->activation_state) {
            it->activation_state->Clear();
        }
        if (it->process_host) {
            it->process_host->Deactivate(plugin_id);
            it->process_host->Stop();
        }
        if (it->extension) {
            it->extension->Deactivate();
        }
        UpdateLifecycle(it->manifest, PluginLifecycleState::Inactive, it->unloadable);
        active_sessions_.erase(it);
        if (message != nullptr) {
            *message = "Unloaded " + plugin_id;
        }
        return true;
    }
    if (message != nullptr) {
        *message = "Plugin is not active: " + plugin_id;
    }
    return false;
}

std::vector<std::string> PluginManager::ReloadPlugin(
    const std::string& plugin_id,
    Logger& logger,
    WorkspaceContext& workspace) {
    ReconcileProcessHealth();
    std::vector<std::string> messages;
    const PluginManifest* manifest = ManifestById(plugin_id);
    if (manifest == nullptr && !plugins_root_.empty()) {
        Scan(plugins_root_);
        manifest = ManifestById(plugin_id);
    }
    if (manifest == nullptr) {
        messages.push_back("Plugin manifest not found: " + plugin_id);
        return messages;
    }
    const std::string compatibility = CompatibilityError(*manifest);
    if (!compatibility.empty()) {
        UpdateLifecycle(*manifest, PluginLifecycleState::Failed, manifest->runtime_kind == "process", compatibility);
        messages.push_back("Failed to Activate " + manifest->extension.id + ": " + compatibility);
        return messages;
    }
    if (manifest->runtime_kind == "core") {
        messages.push_back("Built-in plugin cannot be reloaded individually: " + plugin_id);
        return messages;
    }

    std::string unload_message;
    UnloadPlugin(plugin_id, &unload_message);
    if (!unload_message.empty()) {
        messages.push_back(unload_message);
    }
    messages.push_back(ActivateExternalPlugin(*manifest, logger, workspace));
    return messages;
}

void PluginManager::DeactivateAll() {
    for (auto it = active_sessions_.rbegin(); it != active_sessions_.rend(); ++it) {
        it->state = PluginLifecycleState::Deactivating;
        UpdateLifecycle(it->manifest, PluginLifecycleState::Deactivating, it->unloadable, {}, it->activation_state ? it->activation_state->RegistrationCount() : 0);
        if (it->activation_state) {
            it->activation_state->Clear();
        }
        if (it->process_host) {
            it->process_host->Deactivate(it->manifest.extension.id);
            it->process_host->Stop();
        }
        if (it->extension) {
            it->extension->Deactivate();
        }
        UpdateLifecycle(it->manifest, PluginLifecycleState::Inactive, it->unloadable);
    }
    active_sessions_.clear();
}

const std::vector<PluginManifest>& PluginManager::Manifests() const {
    return manifests_;
}

std::vector<std::string> PluginManager::ActivePluginIds() const {
    std::vector<std::string> result;
    for (const ActivePluginSession& session : active_sessions_) {
        result.push_back(session.manifest.extension.id);
    }
    return result;
}

void PluginManager::ReconcileProcessHealth() {
    for (auto it = active_sessions_.begin(); it != active_sessions_.end();) {
        if (!it->process_host) {
            ++it;
            continue;
        }
        const PluginProcessHealth Health = it->process_host->Health();
        PluginLifecycleInfo& info = lifecycle_[it->manifest.extension.id];
        info.process_running = Health.running;
        info.process_responsive = Health.responsive;
        info.failed_requests = Health.failed_requests;
        info.crash_count = Health.crash_count;
        info.process_exit_code = Health.exit_code;
        info.process_error = Health.last_error;
        if (Health.running) {
            ++it;
            continue;
        }
        if (it->activation_state) {
            it->activation_state->Clear();
        }
        it->process_host->Stop();
        UpdateLifecycle(
            it->manifest,
            PluginLifecycleState::Failed,
            it->unloadable,
            Health.last_error.empty() ? "Plugin process stopped" : Health.last_error,
            0);
        PluginLifecycleInfo& failed = lifecycle_[it->manifest.extension.id];
        failed.process_running = false;
        failed.process_responsive = false;
        failed.failed_requests = Health.failed_requests;
        failed.crash_count = Health.crash_count;
        failed.process_exit_code = Health.exit_code;
        failed.process_error = Health.last_error;
        it = active_sessions_.erase(it);
    }
}

std::vector<PluginLifecycleInfo> PluginManager::PluginLifecycle() {
    ReconcileProcessHealth();
    std::vector<PluginLifecycleInfo> result;
    for (const auto& [id, info] : lifecycle_) {
        (void)id;
        result.push_back(info);
    }
    return result;
}

std::string PluginManager::ActivateExternalPlugin(
    const PluginManifest& manifest,
    Logger& logger,
    WorkspaceContext& workspace) {
    if (manifest.runtime_kind != "process") {
        UpdateLifecycle(manifest, PluginLifecycleState::Failed, false, "Plugin is not an external process plugin");
        return "Plugin is not an external process plugin: " + manifest.extension.id;
    }
    for (const ActivePluginSession& session : active_sessions_) {
        if (session.manifest.extension.id == manifest.extension.id) {
            return "Plugin is already active: " + manifest.extension.id;
        }
    }

    UpdateLifecycle(manifest, PluginLifecycleState::Activating, true);
    std::unique_ptr<PluginProcessHost, PluginProcessHostDeleter> host(new PluginProcessHost());
    std::string error;
    if (!host->Start(manifest, workspace.CurrentWorkspace().Info().root_path, &error)) {
        UpdateLifecycle(manifest, PluginLifecycleState::Failed, true, error);
        return "Failed to start " + manifest.extension.id + ": " + error;
    }
    const auto response = host->Activate(manifest, workspace.CurrentWorkspace().Info().root_path);
    if (!response || !response->ok) {
        const std::string response_error = response ? response->error : "No activation response";
        host->Stop();
        UpdateLifecycle(manifest, PluginLifecycleState::Failed, true, response_error);
        return "Failed to Activate " + manifest.extension.id + ": " + response_error;
    }

    auto activation_state = std::make_shared<PluginActivationState>();
    RegisterPluginLocalizationCatalogs(manifest, workspace, logger, *activation_state);
    RegisterExternalPluginCapabilities(manifest, *host, *activation_state, workspace, ResponseValue(*response));
    if (activation_state->RegistrationCount() > 0) {
        logger.Info("Registered " + std::to_string(activation_state->RegistrationCount()) + " capabilities for " + manifest.extension.id);
    }
    active_sessions_.push_back({
        .manifest = manifest,
        .activation_state = std::move(activation_state),
        .extension = nullptr,
        .process_host = std::move(host),
        .context = nullptr,
        .unloadable = true,
        .state = PluginLifecycleState::Active,
    });
    UpdateLifecycle(manifest, PluginLifecycleState::Active, true, {}, active_sessions_.back().activation_state->RegistrationCount());
    const PluginProcessHealth Health = active_sessions_.back().process_host->Health();
    PluginLifecycleInfo& info = lifecycle_[manifest.extension.id];
    info.process_running = Health.running;
    info.process_responsive = Health.responsive;
    info.failed_requests = Health.failed_requests;
    info.crash_count = Health.crash_count;
    info.process_exit_code = Health.exit_code;
    info.process_error = Health.last_error;
    return "Activated " + manifest.extension.id;
}

const PluginManifest* PluginManager::ManifestById(const std::string& plugin_id) const {
    for (const PluginManifest& manifest : manifests_) {
        if (manifest.extension.id == plugin_id) {
            return &manifest;
        }
    }
    return nullptr;
}

bool PluginManager::IsActive(const std::string& plugin_id) const {
    for (const ActivePluginSession& session : active_sessions_) {
        if (session.manifest.extension.id == plugin_id) {
            return true;
        }
    }
    return false;
}

bool PluginManager::ShouldActivate(const PluginManifest& manifest, WorkspaceContext& workspace) const {
    if (manifest.activation_events.empty()) {
        return true;
    }
    for (const std::string& event : manifest.activation_events) {
        if (event == "onStartup" || event == "onWorkspaceOpened") {
            return true;
        }
        if (StartsWith(event, "onWorkspaceContains:")) {
            const std::string relative_path = event.substr(std::string("onWorkspaceContains:").size());
            if (!relative_path.empty() && std::filesystem::exists(workspace.CurrentWorkspace().Info().root_path / relative_path)) {
                return true;
            }
            continue;
        }
        if (StartsWith(event, "onLanguage:")) {
            const std::string language_id = event.substr(std::string("onLanguage:").size());
            if (!language_id.empty() && workspace.Languages().LanguageForId(language_id) != nullptr) {
                return true;
            }
            continue;
        }
        if (StartsWith(event, "onCommand:")) {
            const std::string command_id = event.substr(std::string("onCommand:").size());
            const std::vector<std::string> commands = workspace.Commands().List();
            if (std::find(commands.begin(), commands.end(), command_id) != commands.end()) {
                return true;
            }
        }
    }
    return false;
}

std::string PluginManager::CompatibilityError(const PluginManifest& manifest) const {
    if (!manifest.min_api_version.empty() && CompareVersion(manifest.min_api_version, SupportedPluginApiVersion()) > 0) {
        return "Plugin requires Vanta API " + manifest.min_api_version + ", but this runtime supports " + SupportedPluginApiVersion();
    }
    for (const std::string& capability : manifest.capabilities) {
        if (!SupportedPluginCapability(capability)) {
            return "Plugin capability is not supported: " + capability;
        }
    }
    return {};
}

void PluginManager::RegisterExternalPluginCapabilities(
    const PluginManifest& manifest,
    PluginProcessHost& host,
    PluginActivationState& activation_state,
    WorkspaceContext& workspace,
    const Value& result) {
    if (!result.IsObject() || !result.Contains("registrations") || !result["registrations"].IsArray()) {
        return;
    }

    for (const Value& item : result["registrations"].AsArray()) {
        const auto registration = ParsePluginCapabilityRegistration(item);
        if (!registration || registration->id.empty()) {
            continue;
        }

        const Value metadata = registration->metadata;

        switch (registration->kind) {
        case PluginCapabilityKind::Command:
            activation_state.Track(workspace.Commands().RegisterCommand({
                .id = registration->id,
                .title = registration->title.empty() ? registration->id : registration->title,
                .source = manifest.extension.id,
            }, [&host, plugin_id = manifest.extension.id, command_id = registration->id](const Value& arguments) {
                auto response = SendPluginRequest(host, "command.execute", Value::ObjectValue({
                    {"pluginId", Value(plugin_id)},
                    {"id", Value(command_id)},
                    {"arguments", arguments},
                }));
                if (response && response->ok) {
                    return ResponseValue(*response);
                }
                return Value::ObjectValue({
                    {"ok", Value(false)},
                    {"error", Value(response ? response->error : "Plugin did not respond")},
                });
            }));
            break;
        case PluginCapabilityKind::AgentTool:
            activation_state.Track(workspace.AgentTools().RegisterTool({
                .id = registration->id,
                .description = registration->title,
                .input_schema = metadata.Contains("inputSchema") ? metadata["inputSchema"] : Value::ObjectValue(),
                .handler = [&host, plugin_id = manifest.extension.id, tool_id = registration->id](const Value& input) {
                    auto response = SendPluginRequest(host, "agentTool.call", Value::ObjectValue({
                        {"pluginId", Value(plugin_id)},
                        {"id", Value(tool_id)},
                        {"input", input},
                    }));
                    if (response && response->ok) {
                        return ResponseValue(*response);
                    }
                    return Value::ObjectValue({
                        {"ok", Value(false)},
                        {"error", Value(response ? response->error : "Plugin did not respond")},
                    });
                },
            }));
            break;
        case PluginCapabilityKind::BuildProvider:
            activation_state.Track(workspace.Build().RegisterProvider(std::make_unique<ExternalBuildProvider>(
                host,
                manifest.extension.id,
                registration->id)));
            break;
        case PluginCapabilityKind::ModelProvider:
            activation_state.Track(workspace.Models().RegisterProvider(std::make_unique<ExternalModelProvider>(
                host,
                manifest.extension.id,
                registration->id,
                metadata)));
            break;
        case PluginCapabilityKind::DebugProvider:
            activation_state.Track(workspace.Debug().RegisterProvider(std::make_unique<ExternalDebugProvider>(
                host,
                manifest.extension.id,
                registration->id,
                metadata)));
            break;
        case PluginCapabilityKind::LanguageService: {
            auto service = std::make_unique<ExternalLanguageService>(host, manifest.extension.id, registration->id);
            Language language;
            language.id = metadata.StringValue("languageId").value_or(registration->id);
            language.definition.display_name = registration->title.empty() ? language.id : registration->title;
            language.association.extensions = ParseStringArray(metadata, "extensions");
            language.association.filenames = ParseStringArray(metadata, "filenames");
            language.association.glob_patterns = ParseStringArray(metadata, "globPatterns");
            language.priority = JsonIntValue(metadata, "priority");
            language.service = service.get();
            activation_state.Track(workspace.Languages().RegisterLanguage(std::move(language)));
            activation_state.language_services.push_back(std::move(service));
            break;
        }
        }
    }
}

void PluginManager::UpdateLifecycle(
    const PluginManifest& manifest,
    PluginLifecycleState state,
    bool unloadable,
    std::string error,
    std::size_t registration_count) {
    PluginLifecycleInfo& info = lifecycle_[manifest.extension.id];
    info.id = manifest.extension.id;
    info.state = state;
    info.unloadable = unloadable;
    info.runtime_kind = manifest.runtime_kind;
    info.error = std::move(error);
    info.registration_count = registration_count;
}

}
