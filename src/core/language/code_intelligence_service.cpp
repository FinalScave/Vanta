#include "vanta/language/code_intelligence_service.h"

#include <cstdint>
#include <utility>

#include "language/language_request_pipeline.h"
#include "vanta/workspace/workspace_context.h"

namespace vanta {

struct CodeIntelligenceService::Impl {
    internal::LanguageRequestPipeline language_requests;
};

namespace {

internal::LanguageRequestKind ToLanguageRequestKind(CodeIntelligenceKind kind) {
    switch (kind) {
    case CodeIntelligenceKind::Completion:
    case CodeIntelligenceKind::InlineCompletion:
        return internal::LanguageRequestKind::Completion;
    case CodeIntelligenceKind::Hover:
        return internal::LanguageRequestKind::Hover;
    case CodeIntelligenceKind::Definition:
        return internal::LanguageRequestKind::Definition;
    case CodeIntelligenceKind::SemanticTokens:
        return internal::LanguageRequestKind::SemanticTokensFull;
    }
    return internal::LanguageRequestKind::Completion;
}

internal::LanguageRequest ToLanguageRequest(const CodeCompletionRequest& request) {
    internal::LanguageRequest language_request;
    language_request.kind = internal::LanguageRequestKind::Completion;
    language_request.document = request.document;
    language_request.position = request.position;
    language_request.document_version = request.document_version;
    language_request.timeout = request.timeout;
    language_request.cancellation = request.cancellation;
    return language_request;
}

std::vector<CodeCompletionItem> ItemsFromLanguageResult(const internal::LanguagePipelineResult& result) {
    std::vector<CodeCompletionItem> items;
    const auto* completion_list = std::get_if<CompletionList>(&result.payload);
    if (completion_list == nullptr) {
        return items;
    }
    for (const CompletionItem& item : completion_list->items) {
        CodeCompletionItem completion;
        completion.label = item.label;
        completion.insert_text = item.insert_text.empty() ? item.label : item.insert_text;
        completion.detail = item.detail;
        completion.documentation = item.documentation;
        completion.source = "language";
        if (!completion.label.empty() || !completion.insert_text.empty()) {
            items.push_back(std::move(completion));
        }
    }
    return items;
}

void CopyPipelineState(const internal::LanguagePipelineResult& pipeline, CodeIntelligenceResult& result) {
    result.ok = pipeline.ok;
    result.stale = pipeline.stale;
    result.cancelled = pipeline.cancelled;
    result.timed_out = pipeline.timed_out;
    result.error = pipeline.error;
    result.document_uri = pipeline.document_uri;
    result.requested_version = pipeline.requested_version;
    result.current_version = pipeline.current_version;
    result.payload = pipeline.payload;
}

void CopyPipelineState(const internal::LanguagePipelineResult& pipeline, CodeCompletionResult& result) {
    result.ok = pipeline.ok;
    result.stale = pipeline.stale;
    result.cancelled = pipeline.cancelled;
    result.timed_out = pipeline.timed_out;
    result.error = pipeline.error;
}

}

CodeIntelligenceService::CodeIntelligenceService()
    : impl_(std::make_unique<Impl>()) {
}

CodeIntelligenceService::~CodeIntelligenceService() = default;

RegistrationHandle CodeIntelligenceService::RegisterCompletionProvider(std::unique_ptr<CodeCompletionProvider> provider) {
    if (provider == nullptr || provider->Id().empty()) {
        return {};
    }
    const std::string provider_id = provider->Id();
    completion_providers_[provider_id] = std::move(provider);
    return RegistrationHandle([this, provider_id] {
        RemoveCompletionProvider(provider_id);
    });
}

void CodeIntelligenceService::RemoveCompletionProvider(const std::string& provider_id) {
    completion_providers_.erase(provider_id);
}

std::vector<std::string> CodeIntelligenceService::CompletionProviderIds() const {
    std::vector<std::string> ids;
    for (const auto& [id, provider] : completion_providers_) {
        (void)provider;
        ids.push_back(id);
    }
    return ids;
}

RegistrationHandle CodeIntelligenceService::RegisterInlineCompletionProvider(std::unique_ptr<CodeCompletionProvider> provider) {
    if (provider == nullptr || provider->Id().empty()) {
        return {};
    }
    const std::string provider_id = provider->Id();
    inline_completion_providers_[provider_id] = std::move(provider);
    return RegistrationHandle([this, provider_id] {
        RemoveInlineCompletionProvider(provider_id);
    });
}

void CodeIntelligenceService::RemoveInlineCompletionProvider(const std::string& provider_id) {
    inline_completion_providers_.erase(provider_id);
}

std::vector<std::string> CodeIntelligenceService::InlineCompletionProviderIds() const {
    std::vector<std::string> ids;
    for (const auto& [id, provider] : inline_completion_providers_) {
        (void)provider;
        ids.push_back(id);
    }
    return ids;
}

CodeCompletionResult CodeIntelligenceService::Complete(WorkspaceContext& context, const CodeCompletionRequest& request) {
    CodeCompletionResult result = request.mode == CodeCompletionMode::Inline ? CodeCompletionResult{.mode = request.mode} : LanguageCompletion(context, request);
    const auto& providers = request.mode == CodeCompletionMode::Inline ? inline_completion_providers_ : completion_providers_;
    for (const auto& [id, provider] : providers) {
        (void)id;
        CodeCompletionResult provided = provider->Complete(context, request);
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

CodeIntelligenceResult CodeIntelligenceService::Query(WorkspaceContext& context, const CodeIntelligenceRequest& request) {
    internal::LanguageRequest language_request;
    language_request.kind = ToLanguageRequestKind(request.kind);
    language_request.document = request.document;
    language_request.position = request.position;
    language_request.document_version = request.document_version;
    language_request.timeout = request.timeout;
    language_request.cancellation = request.cancellation;

    CodeIntelligenceResult result;
    result.kind = request.kind;
    const internal::LanguagePipelineResult pipeline = impl_->language_requests.Execute(language_request, context.Documents(), context.Languages());
    CopyPipelineState(pipeline, result);
    return result;
}

CodeCompletionResult CodeIntelligenceService::LanguageCompletion(WorkspaceContext& context, const CodeCompletionRequest& request) {
    CodeCompletionResult result;
    result.mode = request.mode;
    const internal::LanguagePipelineResult pipeline = impl_->language_requests.Execute(ToLanguageRequest(request), context.Documents(), context.Languages());
    CopyPipelineState(pipeline, result);
    result.items = ItemsFromLanguageResult(pipeline);
    return result;
}

std::string ToString(CodeCompletionMode mode) {
    switch (mode) {
    case CodeCompletionMode::Explicit:
        return "explicit";
    case CodeCompletionMode::Inline:
        return "inline";
    }
    return "explicit";
}

std::string ToString(CodeIntelligenceKind kind) {
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

}
