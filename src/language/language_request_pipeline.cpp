#include "vanta/language/language_request_pipeline.h"

#include <type_traits>
#include <utility>

namespace vanta {
namespace {

std::uint64_t currentDocumentVersion(const DocumentService& documents, const VirtualFile& file) {
    auto snapshot = documents.readSnapshot(file);
    return snapshot ? snapshot->version : 0;
}

LanguagePipelinePayload callService(LanguageService& service, const LanguageRequest& request) {
    if (request.kind == LanguageRequestKind::SemanticTokensFull) {
        return service.semanticTokensFull(request.document);
    }

    TextDocumentPosition position;
    position.document = request.document;
    position.position = request.position;
    switch (request.kind) {
    case LanguageRequestKind::Completion:
        return service.completion(position);
    case LanguageRequestKind::Hover:
        return service.hover(position);
    case LanguageRequestKind::Definition:
        return service.definition(position);
    case LanguageRequestKind::SemanticTokensFull:
        break;
    }
    return std::monostate{};
}

bool payloadOk(const LanguagePipelinePayload& payload) {
    return std::visit([](const auto& value) -> bool {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, std::monostate>) {
            return false;
        } else {
            return value.ok;
        }
    }, payload);
}

std::string payloadError(const LanguagePipelinePayload& payload) {
    return std::visit([](const auto& value) -> std::string {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, std::monostate>) {
            return "";
        } else {
            return value.error;
        }
    }, payload);
}

Json payloadToJson(const LanguagePipelinePayload& payload) {
    return std::visit([](const auto& value) -> Json {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, std::monostate>) {
            return Json(nullptr);
        } else {
            return languageResultToJson(value);
        }
    }, payload);
}

}

LanguagePipelineResult LanguageRequestPipeline::execute(
    const LanguageRequest& request,
    const DocumentService& documents,
    LanguageRegistry& languages) {
    LanguagePipelineResult result;
    result.requestId = nextRequestId_++;
    result.kind = request.kind;
    result.documentUri = request.document.file.toUri();
    result.requestedVersion = request.documentVersion;

    if (request.cancellation.cancelled()) {
        result.cancelled = true;
        result.error = "Language request was cancelled";
        return result;
    }

    LanguageService* service = languages.serviceForDocument(request.document.file);
    if (service == nullptr) {
        result.error = "No language service is registered for document";
        return result;
    }

    const std::uint64_t versionBefore = currentDocumentVersion(documents, request.document.file);
    const std::uint64_t expectedVersion = request.documentVersion == 0 ? versionBefore : request.documentVersion;
    result.requestedVersion = expectedVersion;
    if (expectedVersion != 0 && versionBefore != 0 && versionBefore != expectedVersion) {
        result.stale = true;
        result.currentVersion = versionBefore;
        result.error = "Document version changed before request";
        return result;
    }

    const auto started = std::chrono::steady_clock::now();
    result.payload = callService(*service, request);
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    result.currentVersion = currentDocumentVersion(documents, request.document.file);

    result.cancelled = request.cancellation.cancelled();
    result.timedOut = request.timeout.count() > 0 && result.elapsed > request.timeout;
    result.stale = expectedVersion != 0 && result.currentVersion != 0 && result.currentVersion != expectedVersion;

    if (result.cancelled) {
        result.error = "Language request was cancelled";
    } else if (result.timedOut) {
        result.error = "Language request timed out";
    } else if (result.stale) {
        result.error = "Language result is stale";
    } else {
        result.error = payloadError(result.payload);
    }
    result.ok = payloadOk(result.payload) && !result.cancelled && !result.timedOut && !result.stale;
    return result;
}

std::string toString(LanguageRequestKind kind) {
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

Json languagePipelineResultToJson(const LanguagePipelineResult& result) {
    return Json::object({
        {"requestId", Json(static_cast<std::int64_t>(result.requestId))},
        {"kind", Json(toString(result.kind))},
        {"document", Json(result.documentUri.string())},
        {"requestedVersion", Json(static_cast<std::int64_t>(result.requestedVersion))},
        {"currentVersion", Json(static_cast<std::int64_t>(result.currentVersion))},
        {"ok", Json(result.ok)},
        {"cancelled", Json(result.cancelled)},
        {"stale", Json(result.stale)},
        {"timedOut", Json(result.timedOut)},
        {"elapsedMs", Json(static_cast<std::int64_t>(result.elapsed.count()))},
        {"error", Json(result.error)},
        {"result", payloadToJson(result.payload)},
    });
}

}
