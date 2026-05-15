#include "vanta/language/code_intelligence_service.h"

#include <cstdint>
#include <type_traits>
#include <utility>

#include "vanta/workspace/workspace_context.h"

namespace vanta {
namespace {

LanguageRequestKind toLanguageRequestKind(CodeIntelligenceKind kind) {
    switch (kind) {
    case CodeIntelligenceKind::Completion:
    case CodeIntelligenceKind::InlineCompletion:
        return LanguageRequestKind::Completion;
    case CodeIntelligenceKind::Hover:
        return LanguageRequestKind::Hover;
    case CodeIntelligenceKind::Definition:
        return LanguageRequestKind::Definition;
    case CodeIntelligenceKind::SemanticTokens:
        return LanguageRequestKind::SemanticTokensFull;
    }
    return LanguageRequestKind::Completion;
}

LanguageRequest toLanguageRequest(const CodeCompletionRequest& request) {
    LanguageRequest languageRequest;
    languageRequest.kind = LanguageRequestKind::Completion;
    languageRequest.document = request.document;
    languageRequest.position = request.position;
    languageRequest.documentVersion = request.documentVersion;
    languageRequest.timeout = request.timeout;
    languageRequest.cancellation = request.cancellation;
    return languageRequest;
}

std::vector<CodeCompletionItem> itemsFromLanguageResult(const LanguagePipelineResult& result) {
    std::vector<CodeCompletionItem> items;
    const auto* completionList = std::get_if<CompletionList>(&result.payload);
    if (completionList == nullptr) {
        return items;
    }
    for (const CompletionItem& item : completionList->items) {
        CodeCompletionItem completion;
        completion.label = item.label;
        completion.insertText = item.insertText.empty() ? item.label : item.insertText;
        completion.detail = item.detail;
        completion.documentation = item.documentation;
        completion.source = "language";
        if (!completion.label.empty() || !completion.insertText.empty()) {
            items.push_back(std::move(completion));
        }
    }
    return items;
}

Json dataFromLanguageResult(const LanguagePipelineResult& result) {
    return std::visit([](const auto& value) -> Json {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, std::monostate>) {
            return Json(nullptr);
        } else {
            return value.raw;
        }
    }, result.payload);
}

}

void CodeIntelligenceService::addCompletionProvider(std::unique_ptr<CodeCompletionProvider> provider) {
    if (provider == nullptr || provider->id().empty()) {
        return;
    }
    completionProviders_[provider->id()] = std::move(provider);
}

RegistrationHandle CodeIntelligenceService::registerCompletionProvider(std::unique_ptr<CodeCompletionProvider> provider) {
    if (provider == nullptr || provider->id().empty()) {
        return {};
    }
    const std::string providerId = provider->id();
    addCompletionProvider(std::move(provider));
    return RegistrationHandle([this, providerId] {
        removeCompletionProvider(providerId);
    });
}

void CodeIntelligenceService::removeCompletionProvider(const std::string& providerId) {
    completionProviders_.erase(providerId);
}

std::vector<std::string> CodeIntelligenceService::completionProviderIds() const {
    std::vector<std::string> ids;
    for (const auto& [id, provider] : completionProviders_) {
        (void)provider;
        ids.push_back(id);
    }
    return ids;
}

CodeCompletionResult CodeIntelligenceService::complete(WorkspaceContext& context, const CodeCompletionRequest& request) {
    CodeCompletionResult result = languageCompletion(context, request);
    for (const auto& [id, provider] : completionProviders_) {
        (void)id;
        CodeCompletionResult provided = provider->complete(context, request);
        if (provided.ok) {
            result.ok = true;
        }
        if (!provided.error.empty() && result.error.empty()) {
            result.error = provided.error;
        }
        result.items.insert(result.items.end(), provided.items.begin(), provided.items.end());
    }
    return result;
}

CodeIntelligenceResult CodeIntelligenceService::query(WorkspaceContext& context, const CodeIntelligenceRequest& request) {
    LanguageRequest languageRequest;
    languageRequest.kind = toLanguageRequestKind(request.kind);
    languageRequest.document = request.document;
    languageRequest.position = request.position;
    languageRequest.documentVersion = request.documentVersion;
    languageRequest.timeout = request.timeout;
    languageRequest.cancellation = request.cancellation;

    CodeIntelligenceResult result;
    result.kind = request.kind;
    result.language = context.languageRequests().execute(languageRequest, context.documents(), context.languages());
    result.ok = result.language.ok;
    result.stale = result.language.stale;
    result.cancelled = result.language.cancelled;
    result.timedOut = result.language.timedOut;
    result.error = result.language.error;
    result.data = dataFromLanguageResult(result.language);
    return result;
}

CodeCompletionResult CodeIntelligenceService::languageCompletion(WorkspaceContext& context, const CodeCompletionRequest& request) {
    CodeCompletionResult result;
    result.mode = request.mode;
    result.language = context.languageRequests().execute(toLanguageRequest(request), context.documents(), context.languages());
    result.ok = result.language.ok;
    result.stale = result.language.stale;
    result.cancelled = result.language.cancelled;
    result.timedOut = result.language.timedOut;
    result.error = result.language.error;
    result.data = dataFromLanguageResult(result.language);
    result.items = itemsFromLanguageResult(result.language);
    return result;
}

std::string toString(CodeCompletionMode mode) {
    switch (mode) {
    case CodeCompletionMode::Explicit:
        return "explicit";
    case CodeCompletionMode::Inline:
        return "inline";
    }
    return "explicit";
}

std::string toString(CodeIntelligenceKind kind) {
    switch (kind) {
    case CodeIntelligenceKind::Completion:
        return "completion";
    case CodeIntelligenceKind::InlineCompletion:
        return "inlineCompletion";
    case CodeIntelligenceKind::Hover:
        return "hover";
    case CodeIntelligenceKind::Definition:
        return "definition";
    case CodeIntelligenceKind::SemanticTokens:
        return "semanticTokens";
    }
    return "completion";
}

Json toJson(const CodeCompletionItem& item) {
    return Json::object({
        {"label", Json(item.label)},
        {"insertText", Json(item.insertText)},
        {"detail", Json(item.detail)},
        {"documentation", Json(item.documentation)},
        {"source", Json(item.source)},
        {"score", Json(item.score)},
    });
}

Json toJson(const CodeCompletionResult& result) {
    Json::Array items;
    for (const CodeCompletionItem& item : result.items) {
        items.push_back(toJson(item));
    }
    return Json::object({
        {"mode", Json(toString(result.mode))},
        {"ok", Json(result.ok)},
        {"stale", Json(result.stale)},
        {"cancelled", Json(result.cancelled)},
        {"timedOut", Json(result.timedOut)},
        {"error", Json(result.error)},
        {"items", Json::array(std::move(items))},
        {"language", languagePipelineResultToJson(result.language)},
        {"data", result.data},
    });
}

Json toJson(const CodeIntelligenceResult& result) {
    return Json::object({
        {"kind", Json(toString(result.kind))},
        {"ok", Json(result.ok)},
        {"stale", Json(result.stale)},
        {"cancelled", Json(result.cancelled)},
        {"timedOut", Json(result.timedOut)},
        {"error", Json(result.error)},
        {"language", languagePipelineResultToJson(result.language)},
        {"data", result.data},
    });
}

}
