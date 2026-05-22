#include "vanta/agent/model_service.h"

#include <utility>

#include "vanta/core/json_codec.h"

namespace vanta {

RegistrationHandle ModelService::RegisterProvider(std::unique_ptr<ModelProvider> provider) {
    if (provider == nullptr || provider->Id().empty()) {
        return {};
    }
    const std::string provider_id = provider->Id();
    providers_[provider_id] = std::move(provider);
    return RegistrationHandle([this, provider_id] {
        RemoveProvider(provider_id);
    });
}

void ModelService::RemoveProvider(const std::string& provider_id) {
    providers_.erase(provider_id);
}

std::vector<std::string> ModelService::ProviderIds() const {
    std::vector<std::string> result;
    for (const auto& [id, provider] : providers_) {
        (void)provider;
        result.push_back(id);
    }
    return result;
}

std::vector<ModelInfo> ModelService::Models() const {
    std::vector<ModelInfo> result;
    for (const auto& [id, provider] : providers_) {
        (void)id;
        std::vector<ModelInfo> provided = provider->Models();
        result.insert(result.end(), provided.begin(), provided.end());
    }
    return result;
}

std::optional<ModelInfo> ModelService::Model(const std::string& model_id) const {
    for (const ModelInfo& info : Models()) {
        if (info.id == model_id) {
            return info;
        }
    }
    return std::nullopt;
}

ModelResponse ModelService::Complete(const ModelRequest& request, ModelStreamCallback on_event) const {
    const ModelProvider* provider = ProviderFor(request.model_id);
    if (provider == nullptr) {
        if (on_event) {
            on_event({
                .kind = ModelStreamEventKind::Failed,
                .error = "Model provider not found",
            });
        }
        return {
            .ok = false,
            .model_id = request.model_id,
            .error = "Model provider not found",
        };
    }
    return provider->Complete(request, std::move(on_event));
}

const ModelProvider* ModelService::ProviderFor(const std::string& model_id) const {
    if (providers_.empty()) {
        return nullptr;
    }
    if (!model_id.empty()) {
        for (const auto& [id, provider] : providers_) {
            (void)id;
            for (const ModelInfo& model : provider->Models()) {
                if (model.id == model_id) {
                    return provider.get();
                }
            }
        }
        return nullptr;
    }
    return providers_.begin()->second.get();
}

std::string ToString(ModelMessageRole role) {
    switch (role) {
    case ModelMessageRole::System:
        return "system";
    case ModelMessageRole::User:
        return "user";
    case ModelMessageRole::Assistant:
        return "assistant";
    case ModelMessageRole::Tool:
        return "tool";
    }
    return "user";
}

std::string ToString(ModelStreamEventKind kind) {
    switch (kind) {
    case ModelStreamEventKind::Started:
        return "started";
    case ModelStreamEventKind::Delta:
        return "delta";
    case ModelStreamEventKind::ToolCall:
        return "toolCall";
    case ModelStreamEventKind::Completed:
        return "completed";
    case ModelStreamEventKind::Failed:
        return "failed";
    }
    return "started";
}

}
