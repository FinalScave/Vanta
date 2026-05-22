#pragma once

#include <algorithm>
#include <catch2/catch_amalgamated.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <variant>

#include "vanta/agent/agent_context.h"
#include "vanta/agent/agent_operation.h"
#include "vanta/agent/agent_runtime.h"
#include "vanta/agent/model_service.h"
#include "vanta/agent/agent_tool_registry.h"
#include "cpp_index.h"
#include "vanta/debug/debug_service.h"
#include "vanta/execution/build_service.h"
#include "vanta/execution/problem_matcher.h"
#include "vanta/workspace/change_set_service.h"
#include "cmake_project_model.h"
#include "vanta/language/code_intelligence_service.h"
#include "vanta/workspace/diagnostic_service.h"
#include "vanta/workspace/document_service.h"
#include "vanta/workspace/workspace.h"
#include "vanta/language/document_language_sync.h"
#include "vanta/workspace/workspace_runtime.h"
#include "vanta/language/lsp_client.h"
#include "vanta/plugin/core_plugin.h"
#include "vanta/plugin/plugin_protocol.h"
#include "vanta/plugin/plugin_manager.h"
#include "vanta/platform/async.h"
#include "vanta/platform/async_job_dispatcher.h"
#include "vanta/platform/process.h"
#include "vanta/core/json_codec.h"
#include "vanta/core/result.h"
#include "vanta/project/project.h"
#include "project/project_state_store.h"
#include "vanta/workspace/approval_service.h"
#include "ui/command_palette.h"
#include "ui/layout_state_store.h"
#include "ui/ui_state_store.h"
#include "vanta/workspace/settings_service.h"
#include "vanta/core/value.h"
#include "vanta/vfs/virtual_file_system.h"
#include "vanta/workspace/workspace_trust.h"


namespace vanta::tests {

inline void WaitForJobs(vanta::WorkspaceContext& context, vanta::JobKind kind) {
    for (const vanta::JobRecord& job : context.Jobs().Jobs()) {
        if (job.kind == kind) {
            context.Jobs().Wait(job.id);
        }
    }
}

class FakeLanguageService final : public vanta::LanguageService {
public:
    bool Start(std::string*) override { return true; }
    bool Running() const override { return true; }
    void Stop() override {}

    void DidOpen(const vanta::TextDocument&) override { ++opened; }
    void DidChange(const vanta::TextDocument&) override { ++changed; }
    void DidSave(const vanta::TextDocument&) override { ++saved; }
    void DidClose(const vanta::VirtualFile&) override { ++closed; }

    vanta::CompletionList Completion(const vanta::TextDocumentPosition&) override {
        return {
            .ok = true,
            .items = {{
                .label = "sample",
                .insert_text = "sample",
                .detail = "Fake completion",
            }},
            .trace = {.method = "completion"},
        };
    }
    vanta::HoverResult Hover(const vanta::TextDocumentPosition&) override {
        return {.ok = true, .contents = "Fake hover", .trace = {.method = "hover"}};
    }
    vanta::LocationResult Definition(const vanta::TextDocumentPosition&) override {
        return {.ok = true, .trace = {.method = "definition"}};
    }
    vanta::SemanticTokens SemanticTokensFull(const vanta::TextDocumentIdentifier&) override {
        return {.ok = true, .trace = {.method = "semantic_tokens/full"}};
    }
    vanta::ReferenceResult References(const vanta::ReferenceRequest& request) override {
        return {
            .ok = true,
            .references = {{
                .symbol_id = "main",
                .name = "main",
                .location = {
                    .file = request.position.document.file,
                    .range = {
                        .start = {.line = 0, .character = 4},
                        .end = {.line = 0, .character = 8},
                    },
                },
                .kind = vanta::SymbolReferenceKind::Definition,
            }},
            .trace = {.method = "references"},
        };
    }
    vanta::DocumentSymbolResult DocumentSymbols(const vanta::TextDocumentIdentifier& document) override {
        return {
            .ok = true,
            .symbols = {{
                .id = "main",
                .name = "main",
                .qualified_name = "main",
                .kind = vanta::SymbolKind::Function,
                .location = {
                    .file = document.file,
                    .range = {
                        .start = {.line = 0, .character = 4},
                        .end = {.line = 0, .character = 8},
                    },
                },
                .language_id = document.language_id,
            }},
            .trace = {.method = "document_symbols"},
        };
    }
    vanta::RenamePrepareResult PrepareRename(const vanta::TextDocumentPosition&) override {
        return {
            .ok = true,
            .range = {
                .start = {.line = 0, .character = 4},
                .end = {.line = 0, .character = 8},
            },
            .placeholder = "main",
            .trace = {.method = "prepare_rename"},
        };
    }

    int opened = 0;
    int changed = 0;
    int saved = 0;
    int closed = 0;
};

inline vanta::Language FakeCppLanguage(vanta::LanguageService* service = nullptr, int priority = 0) {
    return {
        .id = "cpp",
        .definition = {
            .display_name = "C++",
        },
        .association = {
            .extensions = {".cpp", ".h"},
        },
        .service = service,
        .priority = priority,
    };
}

class FakeBuildProvider final : public vanta::BuildProvider {
public:
    std::string Id() const override {
        return "test.build";
    }

    vanta::BuildEnvironment Detect(vanta::WorkspaceContext& context, const vanta::ProjectModel&) const override {
        return {
            .provider_id = Id(),
            .detected = true,
            .build_directory = context.CurrentWorkspace().Info().root_path / "build",
        };
    }

    vanta::BuildPlan Plan(vanta::WorkspaceContext& context, const vanta::BuildRequest&) const override {
        return {
            .provider_id = Id(),
            .title = "Fake build",
            .steps = {{
                .title = "Fake build",
                .request = {
                    .executable = "/bin/sh",
                    .arguments = {"-c", "printf 'built\\n'"},
                    .working_directory = context.CurrentWorkspace().Info().root_path,
                },
                .parse_diagnostics = false,
            }},
        };
    }
};

class DiagnosticBuildProvider final : public vanta::BuildProvider {
public:
    std::string Id() const override {
        return "test.diagnostics";
    }

    vanta::BuildEnvironment Detect(vanta::WorkspaceContext& context, const vanta::ProjectModel&) const override {
        return {
            .provider_id = Id(),
            .detected = true,
            .build_directory = context.CurrentWorkspace().Info().root_path / "build",
        };
    }

    vanta::BuildPlan Plan(vanta::WorkspaceContext& context, const vanta::BuildRequest& request) const override {
        return {
            .provider_id = Id(),
            .title = "Diagnostic build",
            .steps = {{
                .title = "Diagnostic build",
                .request = {
                    .executable = "/bin/sh",
                    .arguments = {"-c", "printf 'src/main.cpp:2:10: error: expected expression\\n'"},
                    .working_directory = context.CurrentWorkspace().Info().root_path,
                },
                .parse_diagnostics = request.kind == vanta::BuildRequestKind::Build,
                .diagnostic_base_directory = request.build_directory_override,
            }},
        };
    }
};

class FakeInlineCompletionProvider final : public vanta::CodeCompletionProvider {
public:
    std::string Id() const override {
        return "test.inline";
    }

    vanta::CodeCompletionResult Complete(vanta::WorkspaceContext&, const vanta::CodeCompletionRequest& request) const override {
        if (request.mode != vanta::CodeCompletionMode::Inline) {
            return {.mode = request.mode, .ok = true};
        }
        return {
            .mode = request.mode,
            .ok = true,
            .items = {{
                .label = "return 0;",
                .insert_text = "return 0;",
                .source = Id(),
                .score = 1.0,
            }},
        };
    }
};

class FakeModelProvider final : public vanta::ModelProvider {
public:
    std::string Id() const override {
        return "test.model";
    }

    std::vector<vanta::ModelInfo> Models() const override {
        return {{
            .id = "test-model",
            .provider_id = Id(),
            .display_name = "Test Model",
            .capabilities = {"chat", "toolCall"},
        }};
    }

    vanta::ModelResponse Complete(const vanta::ModelRequest& request, vanta::ModelStreamCallback on_event = {}) const override {
        if (on_event) {
            on_event({
                .kind = vanta::ModelStreamEventKind::Delta,
                .text = "Planning",
            });
        }
        return {
            .ok = true,
            .model_id = request.model_id.empty() ? "test-model" : request.model_id,
            .provider_id = Id(),
            .content = "Plan generated",
            .payload = vanta::Value::ObjectValue({{"ok", vanta::Value(true)}}),
        };
    }
};

class ToolCallingModelProvider final : public vanta::ModelProvider {
public:
    std::string Id() const override {
        return "test.tool-model";
    }

    std::vector<vanta::ModelInfo> Models() const override {
        return {{
            .id = "tool-model",
            .provider_id = Id(),
            .display_name = "Tool Model",
            .capabilities = {"chat", "toolCall"},
        }};
    }

    vanta::ModelResponse Complete(const vanta::ModelRequest& request, vanta::ModelStreamCallback = {}) const override {
        return {
            .ok = true,
            .model_id = request.model_id,
            .provider_id = Id(),
            .content = "Use tool",
            .tool_calls = {{
                .id = "call-1",
                .tool_id = "vanta.readFile",
                .input = vanta::Value(vanta::Value::ObjectValue({{"file", vanta::Value("main.cpp")}})),
            }},
            .payload = vanta::Value::ObjectValue({{"tool", vanta::Value(true)}}),
        };
    }
};

class FakeDebugProvider final : public vanta::DebugProvider {
public:
    std::string Id() const override {
        return "test.debug";
    }

    std::vector<std::string> ConfigurationTypes() const override {
        return {"native"};
    }

    vanta::DebugSession Start(
        vanta::WorkspaceContext&,
        vanta::DebugSessionId session_id,
        const vanta::DebugConfiguration& configuration,
        vanta::DebugEventCallback on_event = {}) override {
        if (on_event) {
            on_event({
                .session_id = session_id,
                .kind = vanta::DebugEventKind::Started,
                .message = "Debug started",
            });
        }
        return {
            .id = session_id,
            .provider_id = Id(),
            .configuration = configuration,
            .status = vanta::DebugSessionStatus::Running,
            .message = "Debug started",
        };
    }

    bool Stop(vanta::DebugSessionId) override {
        return true;
    }

    bool ContinueSession(vanta::DebugSessionId) override {
        return true;
    }

    bool Pause(vanta::DebugSessionId) override {
        return true;
    }

    vanta::DebugEvaluationResult Evaluate(vanta::DebugSessionId, const std::string& expression, std::uint64_t) override {
        return {
            .ok = true,
            .value = expression,
            .type = "string",
        };
    }

    std::vector<vanta::StackFrame> StackTrace(vanta::DebugSessionId) const override {
        return {{
            .id = 1,
            .name = "main",
        }};
    }

    std::vector<vanta::DebugVariable> Variables(vanta::DebugSessionId, std::uint64_t) const override {
        return {{
            .name = "value",
            .value = "1",
            .type = "int",
        }};
    }
};

struct ComponentTestStats {
    int attached = 0;
    int restored = 0;
    int opened = 0;
    int changed = 0;
    int closed = 0;
    int detached = 0;
    int events = 0;
    int saved = 0;
    int restored_value = 0;
    int saved_value = 0;
};

class TestComponent final : public vanta::Component {
public:
    TestComponent(std::string id, ComponentTestStats& stats, bool throw_on_restore = false, bool throw_on_save = false)
        : id_(std::move(id)), stats_(stats), throw_on_restore_(throw_on_restore), throw_on_save_(throw_on_save) {}

    std::string Id() const override {
        return id_;
    }

    void OnAttach(vanta::WorkspaceContext& context) override {
        ++stats_.attached;
        context.OnEvent(*this, vanta::IdeEventKind::ProjectChanged, [this](const vanta::IdeEvent&) {
            ++stats_.events;
        });
    }

    void RestoreState(const vanta::Value& state) override {
        ++stats_.restored;
        if (throw_on_restore_) {
            throw std::runtime_error("restore failed");
        }
        if (state.Contains("value") && state["value"].IsInt()) {
            stats_.restored_value = static_cast<int>(state["value"].AsInt());
        }
    }

    void OnOpenProject(vanta::Project&) override {
        ++stats_.opened;
    }

    void OnProjectChanged(vanta::Project&) override {
        ++stats_.changed;
    }

    vanta::Value SaveState() const override {
        ++stats_.saved;
        if (throw_on_save_) {
            throw std::runtime_error("save failed");
        }
        return vanta::Value::ObjectValue({
            {"value", vanta::Value(static_cast<std::int64_t>(stats_.saved_value))},
        });
    }

    void OnCloseProject(vanta::Project&) override {
        ++stats_.closed;
    }

    void OnDetach() override {
        ++stats_.detached;
    }

private:
    std::string id_;
    ComponentTestStats& stats_;
    bool throw_on_restore_ = false;
    bool throw_on_save_ = false;
};

class RuntimeComponentExtension final : public vanta::CoreExtension {
public:
    explicit RuntimeComponentExtension(ComponentTestStats& stats) : stats_(stats) {}

    void Activate(vanta::ExtensionContext& context) override {
        context.Track(context.Context().Projects().RegisterComponentProvider({
            .id = "sample.runtime",
            .match = {.all_projects = true},
            .factory = [this] {
                return std::make_unique<TestComponent>("sample.runtime", stats_);
            },
        }));
        context.Track(context.Context().Projects().RegisterComponentProvider({
            .id = "sample.cpp.only",
            .match = {.facets = {"cpp"}},
            .factory = [this] {
                return std::make_unique<TestComponent>("sample.cpp.only", stats_);
            },
        }));
    }

private:
    ComponentTestStats& stats_;
};

inline std::filesystem::path MakeTempRoot() {
    const auto root = std::filesystem::temp_directory_path() / "vanta-tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

inline void WriteFile(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    output << text;
}

}
