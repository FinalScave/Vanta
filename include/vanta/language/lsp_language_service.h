#pragma once

#include <filesystem>

#include "vanta/language/language_service.h"
#include "vanta/language/lsp_client.h"

namespace vanta {

class LspLanguageService final : public LanguageService {
public:
    LspLanguageService(std::filesystem::path serverPath, std::filesystem::path workspaceRoot);

    bool start(std::string* errorMessage = nullptr) override;
    bool running() const override;
    void stop() override;

    void didOpen(const TextDocument& document) override;
    void didChange(const TextDocument& document) override;
    void didSave(const TextDocument& document) override;
    void didClose(const VirtualFile& file) override;

    CompletionList completion(const TextDocumentPosition& request) override;
    HoverResult hover(const TextDocumentPosition& request) override;
    LocationResult definition(const TextDocumentPosition& request) override;
    SemanticTokens semanticTokensFull(const TextDocumentIdentifier& document) override;

private:
    std::filesystem::path serverPath_;
    std::filesystem::path workspaceRoot_;
    LspClient client_;
};

}
