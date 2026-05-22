#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "vanta/core/cancellation.h"
#include "vanta/core/registration.h"
#include "vanta/core/value.h"
#include "vanta/language/language_service.h"

namespace vanta {

class WorkspaceContext;

enum class CodeIntelligenceKind {
    Completion,
    InlineCompletion,
    Hover,
    Definition,
    SemanticTokens,
};

using CodeIntelligencePayload = std::variant<
    std::monostate,
    CompletionList,
    HoverResult,
    LocationResult,
    SemanticTokens>;

struct CodeIntelligenceRequest {
    CodeIntelligenceKind kind = CodeIntelligenceKind::Completion;
    TextDocumentIdentifier document;
    TextPosition position;
    std::uint64_t document_version = 0;
    std::chrono::milliseconds timeout = std::chrono::milliseconds(3000);
    CancellationToken cancellation;
    std::string intent;
};

struct CodeIntelligenceResult {
    CodeIntelligenceKind kind = CodeIntelligenceKind::Completion;
    bool ok = false;
    bool stale = false;
    bool cancelled = false;
    bool timed_out = false;
    std::string error;
    Uri document_uri;
    std::uint64_t requested_version = 0;
    std::uint64_t current_version = 0;
    CodeIntelligencePayload payload;
};

enum class CodeCompletionMode {
    Explicit,
    Inline,
};

struct CodeCompletionRequest {
    CodeCompletionMode mode = CodeCompletionMode::Explicit;
    TextDocumentIdentifier document;
    TextPosition position;
    std::uint64_t document_version = 0;
    std::chrono::milliseconds timeout = std::chrono::milliseconds(3000);
    CancellationToken cancellation;
    std::string intent;
};

struct CodeCompletionItem {
    std::string label;
    std::string insert_text;
    TextRange replace_range;
    std::string detail;
    std::string documentation;
    std::string source;
    double score = 0.0;
};

struct CodeCompletionResult {
    CodeCompletionMode mode = CodeCompletionMode::Explicit;
    bool ok = false;
    bool stale = false;
    bool cancelled = false;
    bool timed_out = false;
    std::string error;
    std::vector<CodeCompletionItem> items;
};

class CodeCompletionProvider {
public:
    virtual ~CodeCompletionProvider() = default;

    virtual std::string Id() const = 0;
    virtual CodeCompletionResult Complete(WorkspaceContext& context, const CodeCompletionRequest& request) const = 0;
};

class CodeIntelligenceService {
public:
    static constexpr const char* kServiceId = "vanta.codeIntelligence";

    CodeIntelligenceService();
    ~CodeIntelligenceService();

    RegistrationHandle RegisterCompletionProvider(std::unique_ptr<CodeCompletionProvider> provider);
    void RemoveCompletionProvider(const std::string& provider_id);
    std::vector<std::string> CompletionProviderIds() const;
    RegistrationHandle RegisterInlineCompletionProvider(std::unique_ptr<CodeCompletionProvider> provider);
    void RemoveInlineCompletionProvider(const std::string& provider_id);
    std::vector<std::string> InlineCompletionProviderIds() const;
    CodeCompletionResult Complete(WorkspaceContext& context, const CodeCompletionRequest& request);
    CodeIntelligenceResult Query(WorkspaceContext& context, const CodeIntelligenceRequest& request);

private:
    struct Impl;

    CodeCompletionResult LanguageCompletion(WorkspaceContext& context, const CodeCompletionRequest& request);

    std::unique_ptr<Impl> impl_;
    std::map<std::string, std::unique_ptr<CodeCompletionProvider>> completion_providers_;
    std::map<std::string, std::unique_ptr<CodeCompletionProvider>> inline_completion_providers_;
};

std::string ToString(CodeCompletionMode mode);
std::string ToString(CodeIntelligenceKind kind);

}
