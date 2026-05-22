#include "core_plugin_factories.h"

#include <memory>
#include <string>
#include <utility>

#include "clice_integration.h"
#include "vanta/agent/agent_tool_registry.h"
#include "internal/projection.h"
#include "vanta/language/language_service.h"
#include "vanta/workspace/approval_service.h"
#include "vanta/workspace/workspace_context.h"

namespace vanta::builtin {
namespace {

TextPosition PositionFromValue(const Value& input) {
    TextPosition position;
    if (input.Contains("line") && input["line"].IsInt()) {
        position.line = static_cast<int>(input["line"].AsInt());
    }
    if (input.Contains("character") && input["character"].IsInt()) {
        position.character = static_cast<int>(input["character"].AsInt());
    }
    return position;
}

class CliceCoreExtension final : public CoreExtension {
public:
    explicit CliceCoreExtension(CorePluginDependencies dependencies) : dependencies_(std::move(dependencies)) {}

    void Activate(ExtensionContext& context) override {
        WorkspaceContext& workspace = context.Context();
        languages_ = &workspace.Languages();
        approvals_ = &workspace.Approvals();
        subject_ = context.Extension().id;

        context.Track(workspace.Commands().RegisterCommand("clice.start", [this](const Value&) {
            if (approvals_ != nullptr && approvals_->RequestApproval({
                .actor = {.kind = ApprovalActorKind::Plugin, .id = subject_},
                .access = AccessKind::ProcessExecute,
                .action = "start clice language server",
                .high_risk = true,
            }) == ApprovalDecision::Deny) {
                return Value::ObjectValue({
                    {"ok", Value(false)},
                    {"running", Value(false)},
                    {"error", Value("process execution was denied")},
                });
            }
            LanguageService* service = languages_->ServiceForLanguage("cpp");
            std::string error;
            const bool ok = service != nullptr && service->Start(&error);
            return Value::ObjectValue({
                {"ok", Value(ok)},
                {"running", Value(service != nullptr && service->Running())},
                {"error", Value(service == nullptr ? "C++ language service is not registered" : error)},
            });
        }));

        context.Track(workspace.Commands().RegisterCommand("clice.hover", [this](const Value& input) {
            const std::string file = input.StringValue("file").value_or("");
            return CallDocumentPosition("hover", VirtualFile(Uri::Parse(file), nullptr), PositionFromValue(input));
        }));

        context.Track(workspace.Commands().RegisterCommand("clice.completion", [this](const Value& input) {
            const std::string file = input.StringValue("file").value_or("");
            return CallDocumentPosition("completion", VirtualFile(Uri::Parse(file), nullptr), PositionFromValue(input));
        }));

        context.Track(workspace.Commands().RegisterCommand("clice.semantic_tokens", [this](const Value& input) {
            const std::string file = input.StringValue("file").value_or("");
            const VirtualFile virtual_file(Uri::Parse(file), nullptr);
            LanguageService* service = languages_->ServiceForDocument(virtual_file);
            if (service == nullptr) {
                return internal::LanguageErrorProjection("No language service is registered for document");
            }
            return internal::LanguageSemanticTokensProjection(service->SemanticTokensFull({.file = virtual_file, .language_id = "cpp"}));
        }));

        context.Track(workspace.AgentTools().RegisterTool({
            .id = "clice.findSymbol",
            .description = "Ask clice for the definition location near a source position.",
            .input_schema = Value::ObjectValue({
                {"type", Value("object")},
                {"required", Value::ArrayValue({Value("file"), Value("line"), Value("character")})},
            }),
            .handler = [this](const Value& input) {
                const std::string file = input.StringValue("file").value_or("");
                return CallDocumentPosition("definition", VirtualFile(Uri::Parse(file), nullptr), PositionFromValue(input));
            },
        }));

        clice_.Configure(dependencies_.clice.server_path, context.Workspace().root_path);
        language_service_ = clice_.CreateLanguageService();
        Language language;
        for (Language candidate : DefaultLanguages()) {
            if (candidate.id == "cpp") {
                language = std::move(candidate);
                break;
            }
        }
        if (language.id.empty()) {
            language.id = "cpp";
            language.association.extensions = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"};
        }
        language.service = language_service_.get();
        language.priority = 100;
        context.Track(workspace.Languages().RegisterLanguage(std::move(language)));

        context.Log().Info("Activated clice core plugin");
    }

    void Deactivate() override {
        languages_ = nullptr;
        approvals_ = nullptr;
        subject_.clear();
    }

private:
    Value CallDocumentPosition(const std::string& operation, const VirtualFile& file, TextPosition position) const {
        LanguageService* service = languages_->ServiceForDocument(file);
        if (service == nullptr) {
            return internal::LanguageErrorProjection("No language service is registered for document");
        }

        TextDocumentPosition request;
        request.document.file = file;
        request.document.language_id = "cpp";
        request.position = position;

        if (operation == "completion") {
            return internal::LanguageCompletionProjection(service->Completion(request));
        }
        if (operation == "hover") {
            return internal::LanguageHoverProjection(service->Hover(request));
        }
        return internal::LanguageLocationProjection(service->Definition(request));
    }

    CorePluginDependencies dependencies_;
    CliceIntegration clice_;
    std::unique_ptr<LanguageService> language_service_;
    LanguageRegistry* languages_ = nullptr;
    ApprovalService* approvals_ = nullptr;
    std::string subject_;
};

}

std::unique_ptr<CoreExtension> CreateCliceCoreExtension(CorePluginDependencies dependencies) {
    return std::make_unique<CliceCoreExtension>(std::move(dependencies));
}

}
