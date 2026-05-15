#include "vanta/language/language_service.h"

#include <algorithm>
#include <utility>

#include "vanta/project/project_manager.h"

namespace vanta {
namespace {

bool containsString(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

bool extensionMatches(const std::string& registeredExtension, const std::string& fileExtension) {
    if (registeredExtension == fileExtension) {
        return true;
    }
    if (!registeredExtension.empty() && registeredExtension.front() != '.') {
        return "." + registeredExtension == fileExtension;
    }
    return false;
}

bool globMatches(const std::string& pattern, const std::string& value) {
    std::size_t patternIndex = 0;
    std::size_t valueIndex = 0;
    std::size_t starIndex = std::string::npos;
    std::size_t matchedIndex = 0;

    while (valueIndex < value.size()) {
        if (patternIndex < pattern.size() && (pattern[patternIndex] == '?' || pattern[patternIndex] == value[valueIndex])) {
            ++patternIndex;
            ++valueIndex;
            continue;
        }
        if (patternIndex < pattern.size() && pattern[patternIndex] == '*') {
            starIndex = patternIndex++;
            matchedIndex = valueIndex;
            continue;
        }
        if (starIndex != std::string::npos) {
            patternIndex = starIndex + 1;
            valueIndex = ++matchedIndex;
            continue;
        }
        return false;
    }

    while (patternIndex < pattern.size() && pattern[patternIndex] == '*') {
        ++patternIndex;
    }
    return patternIndex == pattern.size();
}

int matchScore(const LanguageAssociation& association, const VirtualFile& file) {
    const std::string filename = file.displayName();
    const std::string extension = file.extension();
    const std::string uri = file.toUri().string();

    if (containsString(association.filenames, filename)) {
        return 300;
    }
    for (const std::string& pattern : association.globPatterns) {
        if (globMatches(pattern, filename) || globMatches(pattern, uri)) {
            return 200;
        }
    }
    for (const std::string& registeredExtension : association.extensions) {
        if (!extension.empty() && extensionMatches(registeredExtension, extension)) {
            return 100;
        }
    }
    return -1;
}

bool selectorMatches(const LanguageSelector& selector, const LanguageResolutionContext& context) {
    if (!selector.projectFacets.empty()) {
        if (context.project == nullptr) {
            return false;
        }
        for (const std::string& facet : selector.projectFacets) {
            if (!context.project->hasFacet(facet)) {
                return false;
            }
        }
    }
    if (!selector.capabilities.empty()) {
        if (context.capability.empty()) {
            return false;
        }
        if (!containsString(selector.capabilities, context.capability)) {
            return false;
        }
    }
    return true;
}

int selectorScore(const LanguageSelector& selector) {
    return static_cast<int>(selector.projectFacets.size() * 40 + selector.capabilities.size() * 20);
}

bool betterLanguageMatch(int score, int priority, std::uint64_t order, int bestScore, int bestPriority, std::uint64_t bestOrder) {
    if (score != bestScore) {
        return score > bestScore;
    }
    if (priority != bestPriority) {
        return priority > bestPriority;
    }
    return order > bestOrder;
}

std::vector<BracketPair> commonBrackets() {
    return {
        {.open = "{", .close = "}"},
        {.open = "[", .close = "]"},
        {.open = "(", .close = ")"},
    };
}

std::vector<BracketPair> commonSurroundingPairs() {
    return {
        {.open = "{", .close = "}"},
        {.open = "[", .close = "]"},
        {.open = "(", .close = ")"},
        {.open = "\"", .close = "\""},
        {.open = "'", .close = "'"},
    };
}

LanguageConfiguration cStyleConfiguration() {
    return {
        .lineComment = "//",
        .blockComment = {"/*", "*/"},
        .brackets = commonBrackets(),
        .autoClosingPairs = commonSurroundingPairs(),
        .surroundingPairs = commonSurroundingPairs(),
    };
}

LanguageConfiguration pythonConfiguration() {
    return {
        .lineComment = "#",
        .brackets = commonBrackets(),
        .autoClosingPairs = commonSurroundingPairs(),
        .surroundingPairs = commonSurroundingPairs(),
    };
}

}

void LanguageService::didOpen(const TextDocument& document) {
    (void)document;
}

void LanguageService::didChange(const TextDocument& document) {
    (void)document;
}

void LanguageService::didSave(const TextDocument& document) {
    (void)document;
}

void LanguageService::didClose(const VirtualFile& file) {
    (void)file;
}

DefaultLanguageRegistry::DefaultLanguageRegistry() = default;

void DefaultLanguageRegistry::addLanguage(Language language) {
    addRegistration(std::move(language));
}

RegistrationHandle DefaultLanguageRegistry::registerLanguage(Language language) {
    const std::uint64_t registrationId = addRegistration(std::move(language));
    if (registrationId == 0) {
        return {};
    }
    return RegistrationHandle([this, registrationId] {
        removeRegistration(registrationId);
    });
}

std::vector<Language> DefaultLanguageRegistry::languages() const {
    std::vector<Language> result;
    for (const RegisteredLanguage& registered : languages_) {
        result.push_back(registered.language);
    }
    return result;
}

const Language* DefaultLanguageRegistry::languageForFile(const VirtualFile& file) const {
    return languageForFile(file, {});
}

const Language* DefaultLanguageRegistry::languageForFile(const VirtualFile& file, const LanguageResolutionContext& context) const {
    const RegisteredLanguage* best = nullptr;
    int bestScore = -1;
    int bestPriority = 0;
    std::uint64_t bestOrder = 0;

    for (const RegisteredLanguage& registered : languages_) {
        if (!selectorMatches(registered.language.selector, context)) {
            continue;
        }
        const int score = matchScore(registered.language.association, file);
        if (score < 0) {
            continue;
        }
        const int totalScore = score + selectorScore(registered.language.selector);
        if (best == nullptr || betterLanguageMatch(totalScore, registered.language.priority, registered.order, bestScore, bestPriority, bestOrder)) {
            best = &registered;
            bestScore = totalScore;
            bestPriority = registered.language.priority;
            bestOrder = registered.order;
        }
    }
    return best == nullptr ? nullptr : &best->language;
}

const Language* DefaultLanguageRegistry::languageForId(const std::string& languageId) const {
    return languageForId(languageId, {});
}

const Language* DefaultLanguageRegistry::languageForId(const std::string& languageId, const LanguageResolutionContext& context) const {
    const RegisteredLanguage* best = nullptr;
    int bestPriority = 0;
    int bestSelectorScore = -1;
    std::uint64_t bestOrder = 0;

    for (const RegisteredLanguage& registered : languages_) {
        if (registered.language.id != languageId) {
            continue;
        }
        if (!selectorMatches(registered.language.selector, context)) {
            continue;
        }
        const int currentSelectorScore = selectorScore(registered.language.selector);
        if (best == nullptr ||
            registered.language.priority > bestPriority ||
            (registered.language.priority == bestPriority && currentSelectorScore > bestSelectorScore) ||
            (registered.language.priority == bestPriority && currentSelectorScore == bestSelectorScore && registered.order > bestOrder)) {
            best = &registered;
            bestPriority = registered.language.priority;
            bestSelectorScore = currentSelectorScore;
            bestOrder = registered.order;
        }
    }
    return best == nullptr ? nullptr : &best->language;
}

LanguageService* DefaultLanguageRegistry::serviceForLanguage(const std::string& languageId) const {
    return serviceForLanguage(languageId, {});
}

LanguageService* DefaultLanguageRegistry::serviceForLanguage(const std::string& languageId, const LanguageResolutionContext& context) const {
    const Language* language = languageForId(languageId, context);
    return language == nullptr ? nullptr : language->service;
}

LanguageService* DefaultLanguageRegistry::serviceForDocument(const VirtualFile& file) const {
    return serviceForDocument(file, {});
}

LanguageService* DefaultLanguageRegistry::serviceForDocument(const VirtualFile& file, const LanguageResolutionContext& context) const {
    const Language* language = languageForFile(file, context);
    return language == nullptr ? nullptr : language->service;
}

std::string DefaultLanguageRegistry::languageIdForFile(const VirtualFile& file) const {
    return languageIdForFile(file, {});
}

std::string DefaultLanguageRegistry::languageIdForFile(const VirtualFile& file, const LanguageResolutionContext& context) const {
    const Language* language = languageForFile(file, context);
    return language == nullptr ? "" : language->id;
}

std::vector<std::string> DefaultLanguageRegistry::languageIds() const {
    std::vector<std::string> result;
    for (const RegisteredLanguage& registered : languages_) {
        if (!registered.language.id.empty() && !containsString(result, registered.language.id)) {
            result.push_back(registered.language.id);
        }
    }
    return result;
}

std::uint64_t DefaultLanguageRegistry::addRegistration(Language language) {
    if (language.id.empty()) {
        return 0;
    }
    const std::uint64_t registrationId = nextRegistrationId_++;
    languages_.push_back({
        .registrationId = registrationId,
        .language = std::move(language),
        .order = nextOrder_++,
    });
    return registrationId;
}

void DefaultLanguageRegistry::removeRegistration(std::uint64_t registrationId) {
    auto it = std::remove_if(languages_.begin(), languages_.end(), [registrationId](const RegisteredLanguage& language) {
        return language.registrationId == registrationId;
    });
    languages_.erase(it, languages_.end());
}

namespace {

Language language(std::string id, std::string displayName, std::vector<std::string> extensions, LanguageConfiguration configuration) {
    return {
        .id = std::move(id),
        .definition = {
            .displayName = std::move(displayName),
        },
        .association = {
            .extensions = std::move(extensions),
        },
        .configuration = std::move(configuration),
    };
}

}

namespace {

Json traceToJson(const LanguageRequestTrace& trace) {
    return Json::object({
        {"id", Json(static_cast<std::int64_t>(trace.id))},
        {"method", Json(trace.method)},
        {"rawRequest", Json(trace.rawRequest)},
        {"rawResponse", Json(trace.rawResponse)},
    });
}

Json completionItemsToJson(const std::vector<CompletionItem>& items) {
    Json::Array values;
    for (const CompletionItem& item : items) {
        values.push_back(Json::object({
            {"label", Json(item.label)},
            {"insertText", Json(item.insertText)},
            {"detail", Json(item.detail)},
            {"documentation", Json(item.documentation)},
        }));
    }
    return Json::array(std::move(values));
}

Json locationsToJson(const std::vector<Location>& locations) {
    Json::Array values;
    for (const Location& location : locations) {
        values.push_back(Json::object({
            {"file", Json(location.file.toUri().string())},
            {"range", Json::object({
                {"start", Json::object({
                    {"line", Json(static_cast<std::int64_t>(location.range.start.line))},
                    {"character", Json(static_cast<std::int64_t>(location.range.start.character))},
                })},
                {"end", Json::object({
                    {"line", Json(static_cast<std::int64_t>(location.range.end.line))},
                    {"character", Json(static_cast<std::int64_t>(location.range.end.character))},
                })},
            })},
        }));
    }
    return Json::array(std::move(values));
}

Json tokenDataToJson(const std::vector<std::int64_t>& data) {
    Json::Array values;
    for (std::int64_t value : data) {
        values.push_back(Json(value));
    }
    return Json::array(std::move(values));
}

}

Json languageResultToJson(const CompletionList& result) {
    return Json::object({
        {"ok", Json(result.ok)},
        {"error", Json(result.error)},
        {"incomplete", Json(result.incomplete)},
        {"items", completionItemsToJson(result.items)},
        {"raw", result.raw},
        {"trace", traceToJson(result.trace)},
    });
}

Json languageResultToJson(const HoverResult& result) {
    return Json::object({
        {"ok", Json(result.ok)},
        {"error", Json(result.error)},
        {"contents", Json(result.contents)},
        {"raw", result.raw},
        {"trace", traceToJson(result.trace)},
    });
}

Json languageResultToJson(const LocationResult& result) {
    return Json::object({
        {"ok", Json(result.ok)},
        {"error", Json(result.error)},
        {"locations", locationsToJson(result.locations)},
        {"raw", result.raw},
        {"trace", traceToJson(result.trace)},
    });
}

Json languageResultToJson(const SemanticTokens& result) {
    return Json::object({
        {"ok", Json(result.ok)},
        {"error", Json(result.error)},
        {"data", tokenDataToJson(result.data)},
        {"raw", result.raw},
        {"trace", traceToJson(result.trace)},
    });
}

Json languageErrorToJson(const std::string& error) {
    return Json::object({
        {"ok", Json(false)},
        {"error", Json(error)},
    });
}

std::vector<Language> defaultLanguages() {
    std::vector<Language> languages;
    languages.push_back(language("cpp", "C++", {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}, cStyleConfiguration()));
    languages.back().definition.aliases = {"c++", "cpp"};
    languages.push_back(language("python", "Python", {".py", ".pyw"}, pythonConfiguration()));
    languages.back().definition.aliases = {"py"};
    languages.push_back(language("java", "Java", {".java"}, cStyleConfiguration()));
    languages.push_back(language("kotlin", "Kotlin", {".kt", ".kts"}, cStyleConfiguration()));
    languages.push_back(language("javascript", "JavaScript", {".js", ".mjs", ".cjs"}, cStyleConfiguration()));
    languages.back().definition.aliases = {"js"};
    languages.push_back(language("typescript", "TypeScript", {".ts", ".tsx"}, cStyleConfiguration()));
    languages.back().definition.aliases = {"ts"};
    languages.push_back(language("rust", "Rust", {".rs"}, cStyleConfiguration()));
    return languages;
}

void registerDefaultLanguages(LanguageRegistry& registry) {
    for (Language language : defaultLanguages()) {
        registry.addLanguage(std::move(language));
    }
}

}
