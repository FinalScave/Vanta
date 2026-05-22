#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vanta/core/registration.h"
#include "vanta/core/value.h"

namespace vanta {

enum class ModelMessageRole {
    System,
    User,
    Assistant,
    Tool,
};

struct ModelMessage {
    ModelMessageRole role = ModelMessageRole::User;
    std::string content;
    std::string name;
};

struct ModelToolDefinition {
    std::string id;
    std::string description;
    Value input_schema = Value::ObjectValue();
};

struct ModelToolCall {
    std::string id;
    std::string tool_id;
    Value input = Value::ObjectValue();
};

struct ModelInfo {
    std::string id;
    std::string provider_id;
    std::string display_name;
    std::vector<std::string> capabilities;
};

struct ModelRequest {
    std::string model_id;
    std::vector<ModelMessage> messages;
    std::vector<ModelToolDefinition> tools;
    double temperature = 0.0;
    int max_output_tokens = 0;
};

struct ModelResponse {
    bool ok = false;
    std::string model_id;
    std::string provider_id;
    std::string content;
    std::vector<ModelToolCall> tool_calls;
    std::string error;
    std::optional<Value> payload;
};

enum class ModelStreamEventKind {
    Started,
    Delta,
    ToolCall,
    Completed,
    Failed,
};

struct ModelStreamEvent {
    ModelStreamEventKind kind = ModelStreamEventKind::Started;
    std::string text;
    ModelToolCall tool_call;
    std::string error;
    std::optional<Value> payload;
};

using ModelStreamCallback = std::function<void(const ModelStreamEvent&)>;

class ModelProvider {
public:
    virtual ~ModelProvider() = default;

    virtual std::string Id() const = 0;
    virtual std::vector<ModelInfo> Models() const = 0;
    virtual ModelResponse Complete(const ModelRequest& request, ModelStreamCallback on_event = {}) const = 0;
};

class ModelService {
public:
    static constexpr const char* kServiceId = "vanta.agent.models";

    RegistrationHandle RegisterProvider(std::unique_ptr<ModelProvider> provider);
    void RemoveProvider(const std::string& provider_id);
    std::vector<std::string> ProviderIds() const;
    std::vector<ModelInfo> Models() const;
    std::optional<ModelInfo> Model(const std::string& model_id) const;
    ModelResponse Complete(const ModelRequest& request, ModelStreamCallback on_event = {}) const;

private:
    const ModelProvider* ProviderFor(const std::string& model_id) const;

    std::map<std::string, std::unique_ptr<ModelProvider>> providers_;
};

std::string ToString(ModelMessageRole role);
std::string ToString(ModelStreamEventKind kind);

}
