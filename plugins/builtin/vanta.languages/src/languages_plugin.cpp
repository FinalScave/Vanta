#include "core_plugin_factories.h"

#include <utility>

#include "vanta/language/language_service.h"
#include "vanta/plugin/extension_context.h"
#include "vanta/workspace/workspace_context.h"

namespace vanta::builtin {
namespace {

class LanguagesCoreExtension final : public CoreExtension {
public:
    void Activate(ExtensionContext& context) override {
        WorkspaceContext& workspace = context.Context();
        for (Language language : DefaultLanguages()) {
            context.Track(workspace.Languages().RegisterLanguage(std::move(language)));
        }
        context.Log().Info("Activated languages core plugin");
    }
};

}

std::unique_ptr<CoreExtension> CreateLanguagesCoreExtension() {
    return std::make_unique<LanguagesCoreExtension>();
}

}
