#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <variant>

#include "vanta/language/language_service.h"
#include "vanta/platform/async.h"

namespace vanta {

enum class LanguageRequestKind {
    Completion,
    Hover,
    Definition,
    SemanticTokensFull,
};

struct LanguageRequest {
    LanguageRequestKind kind = LanguageRequestKind::Completion;
    TextDocumentIdentifier document;
    TextPosition position;
    std::uint64_t documentVersion = 0;
    std::chrono::milliseconds timeout = std::chrono::milliseconds(3000);
    CancellationToken cancellation;
};

using LanguagePipelinePayload = std::variant<std::monostate, CompletionList, HoverResult, LocationResult, SemanticTokens>;

struct LanguagePipelineResult {
    std::uint64_t requestId = 0;
    LanguageRequestKind kind = LanguageRequestKind::Completion;
    Uri documentUri;
    std::uint64_t requestedVersion = 0;
    std::uint64_t currentVersion = 0;
    bool ok = false;
    bool cancelled = false;
    bool stale = false;
    bool timedOut = false;
    std::string error;
    LanguagePipelinePayload payload;
    std::chrono::milliseconds elapsed = std::chrono::milliseconds(0);
};

class LanguageRequestPipeline {
public:
    LanguagePipelineResult execute(
        const LanguageRequest& request,
        const DocumentService& documents,
        LanguageRegistry& languages);

private:
    std::atomic_uint64_t nextRequestId_ = 1;
};

std::string toString(LanguageRequestKind kind);
Json languagePipelineResultToJson(const LanguagePipelineResult& result);

}
