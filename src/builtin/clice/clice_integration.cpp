#include "vanta/builtin/clice/clice_integration.h"

#include <utility>

#include "vanta/language/lsp_language_service.h"

namespace vanta {

bool CliceIntegration::configure(std::filesystem::path clicePath, std::filesystem::path workspaceRoot) {
    clicePath_ = std::move(clicePath);
    workspaceRoot_ = std::move(workspaceRoot);
    return !clicePath_.empty() && std::filesystem::exists(clicePath_);
}

std::unique_ptr<LanguageService> CliceIntegration::createLanguageService() const {
    return std::make_unique<LspLanguageService>(clicePath_, workspaceRoot_);
}

const std::filesystem::path& CliceIntegration::clicePath() const {
    return clicePath_;
}

const std::filesystem::path& CliceIntegration::workspaceRoot() const {
    return workspaceRoot_;
}

}
