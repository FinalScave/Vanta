#include "core_plugin_factories.h"

#include <cstdint>
#include <memory>

#include "vanta/agent/agent_tool_registry.h"
#include "vanta/workspace/git_service.h"
#include "vanta/workspace/workspace_context.h"

namespace vanta::builtin {
namespace {

class GitCoreExtension final : public CoreExtension {
public:
    void Activate(ExtensionContext& context) override {
        WorkspaceContext& workspace = context.Context();
        git_ = &workspace.Git();

        context.Track(workspace.Commands().RegisterCommand("git.diff", [this](const Value&) {
            const GitDiff diff = git_->Diff();
            return Value::ObjectValue({
                {"exitCode", Value(static_cast<std::int64_t>(diff.exit_code))},
                {"text", Value(diff.text)},
            });
        }));

        context.Track(workspace.AgentTools().RegisterTool({
            .id = "git.diff",
            .description = "Return the current workspace Git diff.",
            .input_schema = Value::ObjectValue({{"type", Value("object")}}),
            .handler = [this](const Value&) {
                const GitDiff diff = git_->Diff();
                return Value::ObjectValue({
                    {"exitCode", Value(static_cast<std::int64_t>(diff.exit_code))},
                    {"text", Value(diff.text)},
                });
            },
        }));

        context.Log().Info("Activated Git core plugin");
    }

    void Deactivate() override {
        git_ = nullptr;
    }

private:
    GitService* git_ = nullptr;
};

}

std::unique_ptr<CoreExtension> CreateGitCoreExtension() {
    return std::make_unique<GitCoreExtension>();
}

}
