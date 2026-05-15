#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "vanta/core/registration.h"
#include "vanta/language/language_request_pipeline.h"
#include "vanta/platform/async.h"
#include "vanta/platform/json.h"

namespace vanta {

class WorkspaceContext;

enum class CodeIntelligenceKind {
    Completion,
    InlineCompletion,
    Hover,
    Definition,
    SemanticTokens,
};

struct CodeIntelligenceRequest {
    CodeIntelligenceKind kind = CodeIntelligenceKind::Completion;
    TextDocumentIdentifier document;
    TextPosition position;
    std::uint64_t documentVersion = 0;
    std::chrono::milliseconds timeout = std::chrono::milliseconds(3000);
    CancellationToken cancellation;
    std::string intent;
};

struct CodeIntelligenceResult {
    CodeIntelligenceKind kind = CodeIntelligenceKind::Completion;
    bool ok = false;
    bool stale = false;
    bool cancelled = false;
    bool timedOut = false;
    std::string error;
    LanguagePipelineResult language;
    Json data;
};

enum class CodeCompletionMode {
    Explicit,
    Inline,
};

struct CodeCompletionRequest {
    CodeCompletionMode mode = CodeCompletionMode::Explicit;
    TextDocumentIdentifier document;
    TextPosition position;
    std::uint64_t documentVersion = 0;
    std::chrono::milliseconds timeout = std::chrono::milliseconds(3000);
    CancellationToken cancellation;
    std::string intent;
};

struct CodeCompletionItem {
    std::string label;
    std::string insertText;
    TextRange replaceRange;
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
    bool timedOut = false;
    std::string error;
    std::vector<CodeCompletionItem> items;
    LanguagePipelineResult language;
    Json data;
};

class CodeCompletionProvider {
public:
    virtual ~CodeCompletionProvider() = default;

    virtual std::string id() const = 0;
    virtual CodeCompletionResult complete(WorkspaceContext& context, const CodeCompletionRequest& request) const = 0;
};

class CodeIntelligenceService {
public:
    void addCompletionProvider(std::unique_ptr<CodeCompletionProvider> provider);
    RegistrationHandle registerCompletionProvider(std::unique_ptr<CodeCompletionProvider> provider);
    void removeCompletionProvider(const std::string& providerId);
    std::vector<std::string> completionProviderIds() const;
    CodeCompletionResult complete(WorkspaceContext& context, const CodeCompletionRequest& request);
    CodeIntelligenceResult query(WorkspaceContext& context, const CodeIntelligenceRequest& request);

private:
    CodeCompletionResult languageCompletion(WorkspaceContext& context, const CodeCompletionRequest& request);

    std::map<std::string, std::unique_ptr<CodeCompletionProvider>> completionProviders_;
};

std::string toString(CodeCompletionMode mode);
std::string toString(CodeIntelligenceKind kind);
Json toJson(const CodeCompletionItem& item);
Json toJson(const CodeCompletionResult& result);
Json toJson(const CodeIntelligenceResult& result);

}
