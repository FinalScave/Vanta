#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vanta/core/registration.h"
#include "vanta/core/result.h"
#include "vanta/core/value.h"

namespace vanta {

struct LocalizedText {
    std::string owner_id;
    std::string key;
    std::vector<Value> args;
};

struct LocalizationCatalog {
    std::string owner_id;
    std::string locale;
    std::map<std::string, std::string> messages;
};

class LocalizationRegistry;

class Localizer {
public:
    Localizer() = default;
    Localizer(const LocalizationRegistry& registry, std::string owner_id);

    LocalizedText Text(std::string key, std::vector<Value> args = {}) const;
    std::string Resolve(std::string key, std::vector<Value> args = {}, std::string locale = {}) const;
    std::string Resolve(const LocalizedText& text, std::string locale = {}) const;

private:
    const LocalizationRegistry* registry_ = nullptr;
    std::string owner_id_;
};

class LocalizationRegistry {
public:
    static constexpr const char* kServiceId = "vanta.localization";

    explicit LocalizationRegistry(std::string default_locale = "en-US");

    const std::string& DefaultLocale() const;
    void SetDefaultLocale(std::string locale);

    RegistrationHandle RegisterCatalog(LocalizationCatalog catalog);
    Localizer LocalizerForOwner(std::string owner_id) const;

    std::optional<std::string> Message(const std::string& owner_id, const std::string& locale, const std::string& key) const;
    std::string Resolve(const LocalizedText& text, const std::string& locale = {}) const;

private:
    void RemoveCatalog(const std::string& owner_id, const std::string& locale);

    std::string default_locale_ = "en-US";
    std::map<std::string, std::map<std::string, std::map<std::string, std::string>>> catalogs_;
};

Result<LocalizationCatalog> ParseLocalizationProperties(
    std::string owner_id,
    std::string locale,
    const std::string& text);

Result<LocalizationCatalog> ReadLocalizationProperties(
    std::string owner_id,
    std::string locale,
    const std::filesystem::path& path);

std::string InterpolateLocalizedPattern(const std::string& pattern, const std::vector<Value>& args);

}
