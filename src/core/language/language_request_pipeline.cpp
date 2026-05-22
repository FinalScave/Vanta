#include "language/language_request_pipeline.h"

#include <type_traits>
#include <utility>

namespace vanta::internal {
namespace {

std::uint64_t CurrentDocumentVersion(const DocumentService& documents, const VirtualFile& file) {
    auto snapshot = documents.ReadSnapshot(file);
    return snapshot ? snapshot->version : 0;
}

CodeIntelligencePayload CallService(LanguageService& service, const LanguageRequest& request) {
    if (request.kind == LanguageRequestKind::SemanticTokensFull) {
        return service.SemanticTokensFull(request.document);
    }

    TextDocumentPosition position;
    position.document = request.document;
    position.position = request.position;
    switch (request.kind) {
    case LanguageRequestKind::Completion:
        return service.Completion(position);
    case LanguageRequestKind::Hover:
        return service.Hover(position);
    case LanguageRequestKind::Definition:
        return service.Definition(position);
    case LanguageRequestKind::SemanticTokensFull:
        break;
    }
    return std::monostate{};
}

bool PayloadOk(const CodeIntelligencePayload& payload) {
    return std::visit([](const auto& value) -> bool {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, std::monostate>) {
            return false;
        } else {
            return value.ok;
        }
    }, payload);
}

std::string PayloadError(const CodeIntelligencePayload& payload) {
    return std::visit([](const auto& value) -> std::string {
        using Payload = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Payload, std::monostate>) {
            return "";
        } else {
            return value.error;
        }
    }, payload);
}

}

LanguagePipelineResult LanguageRequestPipeline::Execute(
    const LanguageRequest& request,
    const DocumentService& documents,
    LanguageRegistry& languages) {
    LanguagePipelineResult result;
    result.request_id = next_request_id_++;
    result.kind = request.kind;
    result.document_uri = request.document.file.ToUri();
    result.requested_version = request.document_version;

    if (request.cancellation.Cancelled()) {
        result.cancelled = true;
        result.error = "Language request was cancelled";
        return result;
    }

    LanguageService* service = nullptr;
    if (!request.document.language_id.empty() && !request.document.file.Valid()) {
        service = languages.ServiceForLanguage(request.document.language_id);
    }
    if (service == nullptr) {
        service = languages.ServiceForDocument(request.document.file);
    }
    if (service == nullptr && !request.document.language_id.empty()) {
        service = languages.ServiceForLanguage(request.document.language_id);
    }
    if (service == nullptr) {
        result.error = "No language service is registered for document";
        return result;
    }

    const std::uint64_t version_before = CurrentDocumentVersion(documents, request.document.file);
    const std::uint64_t expected_version = request.document_version == 0 ? version_before : request.document_version;
    result.requested_version = expected_version;
    if (expected_version != 0 && version_before != 0 && version_before != expected_version) {
        result.stale = true;
        result.current_version = version_before;
        result.error = "Document version changed before request";
        return result;
    }

    const auto started = std::chrono::steady_clock::now();
    result.payload = CallService(*service, request);
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    result.current_version = CurrentDocumentVersion(documents, request.document.file);

    result.cancelled = request.cancellation.Cancelled();
    result.timed_out = request.timeout.count() > 0 && result.elapsed > request.timeout;
    result.stale = expected_version != 0 && result.current_version != 0 && result.current_version != expected_version;

    if (result.cancelled) {
        result.error = "Language request was cancelled";
    } else if (result.timed_out) {
        result.error = "Language request timed out";
    } else if (result.stale) {
        result.error = "Language result is stale";
    } else {
        result.error = PayloadError(result.payload);
    }
    result.ok = PayloadOk(result.payload) && !result.cancelled && !result.timed_out && !result.stale;
    return result;
}

std::string ToString(LanguageRequestKind kind) {
    switch (kind) {
    case LanguageRequestKind::Completion:
        return "completion";
    case LanguageRequestKind::Hover:
        return "hover";
    case LanguageRequestKind::Definition:
        return "definition";
    case LanguageRequestKind::SemanticTokensFull:
        return "semanticTokensFull";
    }
    return "completion";
}

}
