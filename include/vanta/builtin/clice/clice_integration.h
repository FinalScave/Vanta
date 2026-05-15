#pragma once

#include <filesystem>
#include <memory>

#include "vanta/language/language_service.h"

namespace vanta {

class CliceIntegration {
public:
    bool configure(std::filesystem::path clicePath, std::filesystem::path workspaceRoot);
    std::unique_ptr<LanguageService> createLanguageService() const;
    const std::filesystem::path& clicePath() const;
    const std::filesystem::path& workspaceRoot() const;

private:
    std::filesystem::path clicePath_;
    std::filesystem::path workspaceRoot_;
};

}
