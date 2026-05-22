#include "language/language_registry_impl.h"

#include <algorithm>
#include <utility>

#include "vanta/project/project.h"

namespace vanta {
namespace {

bool ContainsString(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

bool ExtensionMatches(const std::string& registered_extension, const std::string& file_extension) {
    if (registered_extension == file_extension) {
        return true;
    }
    if (!registered_extension.empty() && registered_extension.front() != '.') {
        return "." + registered_extension == file_extension;
    }
    return false;
}

bool GlobMatches(const std::string& pattern, const std::string& value) {
    std::size_t pattern_index = 0;
    std::size_t value_index = 0;
    std::size_t star_index = std::string::npos;
    std::size_t matched_index = 0;

    while (value_index < value.size()) {
        if (pattern_index < pattern.size() && (pattern[pattern_index] == '?' || pattern[pattern_index] == value[value_index])) {
            ++pattern_index;
            ++value_index;
            continue;
        }
        if (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
            star_index = pattern_index++;
            matched_index = value_index;
            continue;
        }
        if (star_index != std::string::npos) {
            pattern_index = star_index + 1;
            value_index = ++matched_index;
            continue;
        }
        return false;
    }

    while (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
        ++pattern_index;
    }
    return pattern_index == pattern.size();
}

int MatchScore(const LanguageAssociation& association, const VirtualFile& file) {
    const std::string filename = file.DisplayName();
    const std::string extension = file.Extension();
    const std::string uri = file.ToUri().ToString();

    if (ContainsString(association.filenames, filename)) {
        return 300;
    }
    for (const std::string& pattern : association.glob_patterns) {
        if (GlobMatches(pattern, filename) || GlobMatches(pattern, uri)) {
            return 200;
        }
    }
    for (const std::string& registered_extension : association.extensions) {
        if (!extension.empty() && ExtensionMatches(registered_extension, extension)) {
            return 100;
        }
    }
    return -1;
}

bool SelectorMatches(const LanguageSelector& selector, const LanguageResolutionContext& context) {
    if (!selector.project_facets.empty()) {
        if (context.project == nullptr) {
            return false;
        }
        for (const std::string& facet : selector.project_facets) {
            if (!context.project->HasFacet(facet)) {
                return false;
            }
        }
    }
    if (!selector.capabilities.empty()) {
        if (context.capability.empty()) {
            return false;
        }
        if (!ContainsString(selector.capabilities, context.capability)) {
            return false;
        }
    }
    return true;
}

int SelectorScore(const LanguageSelector& selector) {
    return static_cast<int>(selector.project_facets.size() * 40 + selector.capabilities.size() * 20);
}

bool BetterLanguageMatch(int score, int priority, std::uint64_t order, int best_score, int best_priority, std::uint64_t best_order) {
    if (score != best_score) {
        return score > best_score;
    }
    if (priority != best_priority) {
        return priority > best_priority;
    }
    return order > best_order;
}

std::vector<BracketPair> CommonBrackets() {
    return {
        {.open = "{", .close = "}"},
        {.open = "[", .close = "]"},
        {.open = "(", .close = ")"},
    };
}

std::vector<BracketPair> CommonSurroundingPairs() {
    return {
        {.open = "{", .close = "}"},
        {.open = "[", .close = "]"},
        {.open = "(", .close = ")"},
        {.open = "\"", .close = "\""},
        {.open = "'", .close = "'"},
    };
}

LanguageConfiguration CStyleConfiguration() {
    return {
        .line_comment = "//",
        .block_comment = {"/*", "*/"},
        .brackets = CommonBrackets(),
        .auto_closing_pairs = CommonSurroundingPairs(),
        .surrounding_pairs = CommonSurroundingPairs(),
    };
}

LanguageConfiguration PythonConfiguration() {
    return {
        .line_comment = "#",
        .brackets = CommonBrackets(),
        .auto_closing_pairs = CommonSurroundingPairs(),
        .surrounding_pairs = CommonSurroundingPairs(),
    };
}

}

void LanguageService::DidOpen(const TextDocument& document) {
    (void)document;
}

void LanguageService::DidChange(const TextDocument& document) {
    (void)document;
}

void LanguageService::DidSave(const TextDocument& document) {
    (void)document;
}

void LanguageService::DidClose(const VirtualFile& file) {
    (void)file;
}

ReferenceResult LanguageService::References(const ReferenceRequest& request) {
    (void)request;
    return {
        .ok = false,
        .error = "Language service does not support references",
    };
}

LocationResult LanguageService::Implementation(const TextDocumentPosition& request) {
    (void)request;
    return {
        .ok = false,
        .error = "Language service does not support implementation lookup",
    };
}

DocumentSymbolResult LanguageService::DocumentSymbols(const TextDocumentIdentifier& document) {
    (void)document;
    return {
        .ok = false,
        .error = "Language service does not support document symbols",
    };
}

RenamePrepareResult LanguageService::PrepareRename(const TextDocumentPosition& request) {
    (void)request;
    return {
        .ok = false,
        .error = "Language service does not support rename preparation",
    };
}

CodeActionResult LanguageService::CodeActions(const CodeActionRequest& request) {
    (void)request;
    return {
        .ok = false,
        .error = "Language service does not support code actions",
    };
}

CallHierarchyPrepareResult LanguageService::PrepareCallHierarchy(const TextDocumentPosition& request) {
    (void)request;
    return {
        .ok = false,
        .error = "Language service does not support call hierarchy",
    };
}

CallHierarchyResult LanguageService::IncomingCalls(const CodeSymbol& item) {
    (void)item;
    return {
        .ok = false,
        .error = "Language service does not support incoming calls",
    };
}

CallHierarchyResult LanguageService::OutgoingCalls(const CodeSymbol& item) {
    (void)item;
    return {
        .ok = false,
        .error = "Language service does not support outgoing calls",
    };
}

TypeHierarchyPrepareResult LanguageService::PrepareTypeHierarchy(const TextDocumentPosition& request) {
    (void)request;
    return {
        .ok = false,
        .error = "Language service does not support type hierarchy",
    };
}

TypeHierarchyResult LanguageService::Supertypes(const CodeSymbol& item) {
    (void)item;
    return {
        .ok = false,
        .error = "Language service does not support supertypes",
    };
}

TypeHierarchyResult LanguageService::Subtypes(const CodeSymbol& item) {
    (void)item;
    return {
        .ok = false,
        .error = "Language service does not support subtypes",
    };
}

internal::LanguageRegistryImpl::LanguageRegistryImpl() = default;

RegistrationHandle internal::LanguageRegistryImpl::RegisterLanguage(Language language) {
    const std::uint64_t registration_id = AddRegistration(std::move(language));
    if (registration_id == 0) {
        return {};
    }
    return RegistrationHandle([this, registration_id] {
        RemoveRegistration(registration_id);
    });
}

std::vector<Language> internal::LanguageRegistryImpl::Languages() const {
    std::vector<Language> result;
    for (const RegisteredLanguage& registered : languages_) {
        result.push_back(registered.language);
    }
    return result;
}

const Language* internal::LanguageRegistryImpl::LanguageForFile(const VirtualFile& file) const {
    return LanguageForFile(file, {});
}

const Language* internal::LanguageRegistryImpl::LanguageForFile(const VirtualFile& file, const LanguageResolutionContext& context) const {
    const RegisteredLanguage* best = nullptr;
    int best_score = -1;
    int best_priority = 0;
    std::uint64_t best_order = 0;

    for (const RegisteredLanguage& registered : languages_) {
        if (!SelectorMatches(registered.language.selector, context)) {
            continue;
        }
        const int score = MatchScore(registered.language.association, file);
        if (score < 0) {
            continue;
        }
        const int total_score = score + SelectorScore(registered.language.selector);
        if (best == nullptr || BetterLanguageMatch(total_score, registered.language.priority, registered.order, best_score, best_priority, best_order)) {
            best = &registered;
            best_score = total_score;
            best_priority = registered.language.priority;
            best_order = registered.order;
        }
    }
    return best == nullptr ? nullptr : &best->language;
}

const Language* internal::LanguageRegistryImpl::LanguageForId(const std::string& language_id) const {
    return LanguageForId(language_id, {});
}

const Language* internal::LanguageRegistryImpl::LanguageForId(const std::string& language_id, const LanguageResolutionContext& context) const {
    const RegisteredLanguage* best = nullptr;
    int best_priority = 0;
    int best_selector_score = -1;
    std::uint64_t best_order = 0;

    for (const RegisteredLanguage& registered : languages_) {
        if (registered.language.id != language_id) {
            continue;
        }
        if (!SelectorMatches(registered.language.selector, context)) {
            continue;
        }
        const int current_selector_score = SelectorScore(registered.language.selector);
        if (best == nullptr ||
            registered.language.priority > best_priority ||
            (registered.language.priority == best_priority && current_selector_score > best_selector_score) ||
            (registered.language.priority == best_priority && current_selector_score == best_selector_score && registered.order > best_order)) {
            best = &registered;
            best_priority = registered.language.priority;
            best_selector_score = current_selector_score;
            best_order = registered.order;
        }
    }
    return best == nullptr ? nullptr : &best->language;
}

LanguageService* internal::LanguageRegistryImpl::ServiceForLanguage(const std::string& language_id) const {
    return ServiceForLanguage(language_id, {});
}

LanguageService* internal::LanguageRegistryImpl::ServiceForLanguage(const std::string& language_id, const LanguageResolutionContext& context) const {
    const Language* language = LanguageForId(language_id, context);
    return language == nullptr ? nullptr : language->service;
}

LanguageService* internal::LanguageRegistryImpl::ServiceForDocument(const VirtualFile& file) const {
    return ServiceForDocument(file, {});
}

LanguageService* internal::LanguageRegistryImpl::ServiceForDocument(const VirtualFile& file, const LanguageResolutionContext& context) const {
    const Language* language = LanguageForFile(file, context);
    return language == nullptr ? nullptr : language->service;
}

std::string internal::LanguageRegistryImpl::LanguageIdForFile(const VirtualFile& file) const {
    return LanguageIdForFile(file, {});
}

std::string internal::LanguageRegistryImpl::LanguageIdForFile(const VirtualFile& file, const LanguageResolutionContext& context) const {
    const Language* language = LanguageForFile(file, context);
    return language == nullptr ? "" : language->id;
}

std::vector<std::string> internal::LanguageRegistryImpl::LanguageIds() const {
    std::vector<std::string> result;
    for (const RegisteredLanguage& registered : languages_) {
        if (!registered.language.id.empty() && !ContainsString(result, registered.language.id)) {
            result.push_back(registered.language.id);
        }
    }
    return result;
}

std::uint64_t internal::LanguageRegistryImpl::AddRegistration(Language language) {
    if (language.id.empty()) {
        return 0;
    }
    const std::uint64_t registration_id = next_registration_id_++;
    languages_.push_back({
        .registration_id = registration_id,
        .language = std::move(language),
        .order = next_order_++,
    });
    return registration_id;
}

void internal::LanguageRegistryImpl::RemoveRegistration(std::uint64_t registration_id) {
    auto it = std::remove_if(languages_.begin(), languages_.end(), [registration_id](const RegisteredLanguage& language) {
        return language.registration_id == registration_id;
    });
    languages_.erase(it, languages_.end());
}

namespace {

Language MakeLanguage(std::string id, std::string display_name, std::vector<std::string> extensions, LanguageConfiguration configuration) {
    return {
        .id = std::move(id),
        .definition = {
            .display_name = std::move(display_name),
        },
        .association = {
            .extensions = std::move(extensions),
        },
        .configuration = std::move(configuration),
    };
}

}

std::vector<Language> DefaultLanguages() {
    std::vector<Language> languages;
    languages.push_back(MakeLanguage("cpp", "C++", {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}, CStyleConfiguration()));
    languages.back().definition.aliases = {"c++", "cpp"};
    languages.push_back(MakeLanguage("python", "Python", {".py", ".pyw"}, PythonConfiguration()));
    languages.back().definition.aliases = {"py"};
    languages.push_back(MakeLanguage("java", "Java", {".java"}, CStyleConfiguration()));
    languages.push_back(MakeLanguage("kotlin", "Kotlin", {".kt", ".kts"}, CStyleConfiguration()));
    languages.push_back(MakeLanguage("javascript", "JavaScript", {".js", ".mjs", ".cjs"}, CStyleConfiguration()));
    languages.back().definition.aliases = {"js"};
    languages.push_back(MakeLanguage("typescript", "TypeScript", {".ts", ".tsx"}, CStyleConfiguration()));
    languages.back().definition.aliases = {"ts"};
    languages.push_back(MakeLanguage("rust", "Rust", {".rs"}, CStyleConfiguration()));
    return languages;
}

void RegisterDefaultLanguages(LanguageRegistry& registry) {
    for (Language language : DefaultLanguages()) {
        registry.RegisterLanguage(std::move(language));
    }
}

std::string ToString(CodeActionKind kind) {
    switch (kind) {
    case CodeActionKind::QuickFix:
        return "quickFix";
    case CodeActionKind::Refactor:
        return "refactor";
    case CodeActionKind::RefactorExtract:
        return "refactor.extract";
    case CodeActionKind::RefactorInline:
        return "refactor.inline";
    case CodeActionKind::RefactorRewrite:
        return "refactor.rewrite";
    case CodeActionKind::Source:
        return "source";
    case CodeActionKind::SourceOrganizeImports:
        return "source.organizeImports";
    case CodeActionKind::SourceFixAll:
        return "source.fixAll";
    }
    return "quickFix";
}

}
