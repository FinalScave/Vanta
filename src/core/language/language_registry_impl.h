#pragma once

#include <cstdint>
#include <vector>

#include "vanta/language/language_service.h"

namespace vanta::internal {

class LanguageRegistryImpl final : public LanguageRegistry {
public:
    LanguageRegistryImpl();

    RegistrationHandle RegisterLanguage(Language language) override;
    std::vector<Language> Languages() const override;
    const Language* LanguageForFile(const VirtualFile& file) const override;
    const Language* LanguageForFile(const VirtualFile& file, const LanguageResolutionContext& context) const override;
    const Language* LanguageForId(const std::string& language_id) const override;
    const Language* LanguageForId(const std::string& language_id, const LanguageResolutionContext& context) const override;
    LanguageService* ServiceForLanguage(const std::string& language_id) const override;
    LanguageService* ServiceForLanguage(const std::string& language_id, const LanguageResolutionContext& context) const override;
    LanguageService* ServiceForDocument(const VirtualFile& file) const override;
    LanguageService* ServiceForDocument(const VirtualFile& file, const LanguageResolutionContext& context) const override;
    std::string LanguageIdForFile(const VirtualFile& file) const override;
    std::string LanguageIdForFile(const VirtualFile& file, const LanguageResolutionContext& context) const override;
    std::vector<std::string> LanguageIds() const override;

private:
    struct RegisteredLanguage {
        std::uint64_t registration_id = 0;
        Language language;
        std::uint64_t order = 0;
    };

    std::uint64_t AddRegistration(Language language);
    void RemoveRegistration(std::uint64_t registration_id);

    std::vector<RegisteredLanguage> languages_;
    std::uint64_t next_registration_id_ = 1;
    std::uint64_t next_order_ = 1;
};

}
