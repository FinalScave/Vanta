#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "vanta/core/diagnostic.h"
#include "vanta/workspace/document_service.h"
#include "vanta/core/registration.h"
#include "vanta/core/text.h"
#include "vanta/platform/json.h"

namespace vanta {

struct ProjectModel;

struct TextDocumentIdentifier {
    VirtualFile file;
    std::string languageId;
};

struct TextDocumentPosition {
    TextDocumentIdentifier document;
    TextPosition position;
};

class LanguageService;

struct BracketPair {
    std::string open;
    std::string close;
};

struct LanguageDefinition {
    std::string displayName;
    std::vector<std::string> aliases;
    Json metadata;
};

struct LanguageAssociation {
    std::vector<std::string> extensions;
    std::vector<std::string> filenames;
    std::vector<std::string> globPatterns;
};

struct LanguageConfiguration {
    std::string lineComment;
    std::vector<std::string> blockComment;
    std::vector<BracketPair> brackets;
    std::vector<BracketPair> autoClosingPairs;
    std::vector<BracketPair> surroundingPairs;
    std::vector<std::string> wordPattern;
};

struct LanguageSelector {
    std::vector<std::string> projectFacets;
    std::vector<std::string> capabilities;
    Json metadata;
};

struct LanguageResolutionContext {
    const ProjectModel* project = nullptr;
    std::string capability;
    Json metadata;
};

struct Language {
    std::string id;
    LanguageDefinition definition;
    LanguageAssociation association;
    LanguageConfiguration configuration;
    LanguageSelector selector;
    LanguageService* service = nullptr;
    int priority = 0;
    Json metadata;
};

struct CompletionItem {
    std::string label;
    std::string insertText;
    std::string detail;
    std::string documentation;
};

struct LanguageRequestTrace {
    int id = 0;
    std::string method;
    std::string rawRequest;
    std::string rawResponse;
};

struct CompletionList {
    bool ok = false;
    std::string error;
    bool incomplete = false;
    std::vector<CompletionItem> items;
    Json raw;
    LanguageRequestTrace trace;
};

struct HoverResult {
    bool ok = false;
    std::string error;
    std::string contents;
    Json raw;
    LanguageRequestTrace trace;
};

struct Location {
    VirtualFile file;
    TextRange range;
};

struct LocationResult {
    bool ok = false;
    std::string error;
    std::vector<Location> locations;
    Json raw;
    LanguageRequestTrace trace;
};

struct SemanticTokens {
    bool ok = false;
    std::string error;
    std::vector<std::int64_t> data;
    Json raw;
    LanguageRequestTrace trace;
};

class LanguageService {
public:
    virtual ~LanguageService() = default;

    virtual bool start(std::string* errorMessage = nullptr) = 0;
    virtual bool running() const = 0;
    virtual void stop() = 0;

    virtual void didOpen(const TextDocument& document);
    virtual void didChange(const TextDocument& document);
    virtual void didSave(const TextDocument& document);
    virtual void didClose(const VirtualFile& file);

    virtual CompletionList completion(const TextDocumentPosition& request) = 0;
    virtual HoverResult hover(const TextDocumentPosition& request) = 0;
    virtual LocationResult definition(const TextDocumentPosition& request) = 0;
    virtual SemanticTokens semanticTokensFull(const TextDocumentIdentifier& document) = 0;
};

class LanguageRegistry {
public:
    virtual ~LanguageRegistry() = default;

    virtual void addLanguage(Language language) = 0;
    virtual RegistrationHandle registerLanguage(Language language) = 0;
    virtual std::vector<Language> languages() const = 0;
    virtual const Language* languageForFile(const VirtualFile& file) const = 0;
    virtual const Language* languageForFile(const VirtualFile& file, const LanguageResolutionContext& context) const = 0;
    virtual const Language* languageForId(const std::string& languageId) const = 0;
    virtual const Language* languageForId(const std::string& languageId, const LanguageResolutionContext& context) const = 0;
    virtual LanguageService* serviceForLanguage(const std::string& languageId) const = 0;
    virtual LanguageService* serviceForLanguage(const std::string& languageId, const LanguageResolutionContext& context) const = 0;
    virtual LanguageService* serviceForDocument(const VirtualFile& file) const = 0;
    virtual LanguageService* serviceForDocument(const VirtualFile& file, const LanguageResolutionContext& context) const = 0;
    virtual std::string languageIdForFile(const VirtualFile& file) const = 0;
    virtual std::string languageIdForFile(const VirtualFile& file, const LanguageResolutionContext& context) const = 0;
    virtual std::vector<std::string> languageIds() const = 0;
};

class DefaultLanguageRegistry final : public LanguageRegistry {
public:
    DefaultLanguageRegistry();

    void addLanguage(Language language) override;
    RegistrationHandle registerLanguage(Language language) override;
    std::vector<Language> languages() const override;
    const Language* languageForFile(const VirtualFile& file) const override;
    const Language* languageForFile(const VirtualFile& file, const LanguageResolutionContext& context) const override;
    const Language* languageForId(const std::string& languageId) const override;
    const Language* languageForId(const std::string& languageId, const LanguageResolutionContext& context) const override;
    LanguageService* serviceForLanguage(const std::string& languageId) const override;
    LanguageService* serviceForLanguage(const std::string& languageId, const LanguageResolutionContext& context) const override;
    LanguageService* serviceForDocument(const VirtualFile& file) const override;
    LanguageService* serviceForDocument(const VirtualFile& file, const LanguageResolutionContext& context) const override;
    std::string languageIdForFile(const VirtualFile& file) const override;
    std::string languageIdForFile(const VirtualFile& file, const LanguageResolutionContext& context) const override;
    std::vector<std::string> languageIds() const override;

private:
    struct RegisteredLanguage {
        std::uint64_t registrationId = 0;
        Language language;
        std::uint64_t order = 0;
    };

    std::uint64_t addRegistration(Language language);
    void removeRegistration(std::uint64_t registrationId);

    std::vector<RegisteredLanguage> languages_;
    std::uint64_t nextRegistrationId_ = 1;
    std::uint64_t nextOrder_ = 1;
};

Json languageResultToJson(const CompletionList& result);
Json languageResultToJson(const HoverResult& result);
Json languageResultToJson(const LocationResult& result);
Json languageResultToJson(const SemanticTokens& result);
Json languageErrorToJson(const std::string& error);
std::vector<Language> defaultLanguages();
void registerDefaultLanguages(LanguageRegistry& languages);

}
