#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "vanta/core/cancellation.h"
#include "vanta/language/code_intelligence_service.h"

namespace vanta::internal {

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
    std::uint64_t document_version = 0;
    std::chrono::milliseconds timeout = std::chrono::milliseconds(3000);
    CancellationToken cancellation;
};

struct LanguagePipelineResult {
    std::uint64_t request_id = 0;
    LanguageRequestKind kind = LanguageRequestKind::Completion;
    Uri document_uri;
    std::uint64_t requested_version = 0;
    std::uint64_t current_version = 0;
    bool ok = false;
    bool cancelled = false;
    bool stale = false;
    bool timed_out = false;
    std::string error;
    CodeIntelligencePayload payload;
    std::chrono::milliseconds elapsed = std::chrono::milliseconds(0);
};

class LanguageRequestPipeline {
public:
    LanguagePipelineResult Execute(
        const LanguageRequest& request,
        const DocumentService& documents,
        LanguageRegistry& languages);

private:
    std::atomic_uint64_t next_request_id_ = 1;
};

std::string ToString(LanguageRequestKind kind);

}
