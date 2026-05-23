#pragma once

#include <algorithm>
#include <catch2/catch_amalgamated.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "mornox/agent/agent_context.h"
#include "mornox/agent/agent_operation.h"
#include "mornox/agent/agent_runtime.h"
#include "mornox/agent/model_service.h"
#include "mornox/agent/agent_tool_registry.h"
#include "cpp_index.h"
#include "mornox/debug/debug_service.h"
#include "mornox/execution/build_service.h"
#include "mornox/execution/execution_service.h"
#include "mornox/execution/problem_matcher.h"
#include "mornox/workspace/change_set_service.h"
#include "cmake_project_model.h"
#include "mornox/language/code_intelligence_service.h"
#include "mornox/workspace/diagnostic_service.h"
#include "mornox/workspace/document_service.h"
#include "mornox/workspace/workspace.h"
#include "mornox/language/document_language_sync.h"
#include "mornox/workspace/workspace_runtime.h"
#include "mornox/language/lsp_client.h"
#include "mornox/plugin/core_plugin.h"
#include "mornox/plugin/plugin_protocol.h"
#include "mornox/plugin/plugin_manager.h"
#include "mornox/platform/async.h"
#include "mornox/platform/async_job_dispatcher.h"
#include "mornox/platform/executable.h"
#include "mornox/platform/process.h"
#include "mornox/core/json_codec.h"
#include "mornox/core/result.h"
#include "mornox/project/project.h"
#include "project/project_state_store.h"
#include "mornox/workspace/approval_service.h"
#include "ui/command_palette.h"
#include "ui/layout_state_store.h"
#include "ui/ui_state_store.h"
#include "mornox/workspace/settings_service.h"
#include "mornox/core/value.h"
#include "mornox/vfs/virtual_file_system.h"
#include "mornox/workspace/workspace_trust.h"


namespace mornox::tests {

inline std::string JsonString(std::string_view value) {
    return mornox::ValueToJsonText(mornox::Value(std::string(value)));
}

inline std::string JsonPath(const std::filesystem::path& path) {
    return JsonString(path.string());
}

inline mornox::Value StringArrayValue(const std::vector<std::string>& values) {
    mornox::Value::Array array;
    for (const std::string& value : values) {
        array.push_back(mornox::Value(value));
    }
    return mornox::Value::ArrayValue(std::move(array));
}

inline std::string PythonBytes(std::string_view value) {
    std::string result = "b'";
    for (char character : value) {
        switch (character) {
        case '\\':
            result += "\\\\";
            break;
        case '\'':
            result += "\\'";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        default:
            result.push_back(character);
            break;
        }
    }
    result += "'";
    return result;
}

inline std::filesystem::path TestPythonExecutable() {
#if defined(_WIN32)
    const std::vector<std::string> candidates = {"python", "py"};
    return mornox::FindFirstExecutableOnPath(candidates).value_or(std::filesystem::path("python"));
#else
    const std::vector<std::string> candidates = {
        "/opt/local/bin/python3.11",
        "/opt/homebrew/bin/python3",
        "/usr/local/bin/python3",
        "python3",
        "python",
    };
    return mornox::FindFirstExecutableOnPath(candidates).value_or(std::filesystem::path("python3"));
#endif
}

inline mornox::CommandSpec TestPythonCommand(std::string code, std::filesystem::path working_directory = {}) {
    return {
        .executable = TestPythonExecutable().string(),
        .arguments = {"-c", std::move(code)},
        .working_directory = std::move(working_directory),
    };
}

inline mornox::CommandSpec TestStdoutCommand(std::string text, std::filesystem::path working_directory = {}) {
    return TestPythonCommand("import sys; sys.stdout.buffer.write(" + PythonBytes(text) + ")", std::move(working_directory));
}

inline mornox::CommandSpec TestStdoutStderrCommand(
    std::string stdout_text,
    std::string stderr_text,
    std::filesystem::path working_directory = {}) {
    return TestPythonCommand(
        "import sys; sys.stdout.buffer.write(" + PythonBytes(stdout_text) + "); sys.stderr.buffer.write(" + PythonBytes(stderr_text) + ")",
        std::move(working_directory));
}

inline mornox::CommandSpec TestDelayedStdoutCommand(
    std::chrono::milliseconds delay,
    std::string text,
    std::filesystem::path working_directory = {}) {
    return TestPythonCommand(
        "import sys, time; time.sleep(" + std::to_string(delay.count() / 1000.0) + "); sys.stdout.buffer.write(" + PythonBytes(text) + ")",
        std::move(working_directory));
}

inline mornox::ExecutionRequest TestExecutionRequest(mornox::CommandSpec command, mornox::JobId job_id = 0) {
    return {
        .executable = std::move(command.executable),
        .arguments = std::move(command.arguments),
        .working_directory = std::move(command.working_directory),
        .job_id = job_id,
    };
}

inline void WaitForJobs(mornox::WorkspaceContext& context, mornox::JobKind kind) {
    for (const mornox::JobRecord& job : context.Jobs().Jobs()) {
        if (job.kind == kind) {
            context.Jobs().Wait(job.id);
        }
    }
}

class FakeLanguageService final : public mornox::LanguageService {
public:
    bool Start(std::string*) override { return true; }
    bool Running() const override { return true; }
    void Stop() override {}

    void DidOpen(const mornox::TextDocument&) override { ++opened; }
    void DidChange(const mornox::TextDocument&) override { ++changed; }
    void DidSave(const mornox::TextDocument&) override { ++saved; }
    void DidClose(const mornox::VirtualFile&) override { ++closed; }

    mornox::CompletionList Completion(const mornox::TextDocumentPosition&) override {
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
    mornox::HoverResult Hover(const mornox::TextDocumentPosition&) override {
        return {.ok = true, .contents = "Fake hover", .trace = {.method = "hover"}};
    }
    mornox::LocationResult Definition(const mornox::TextDocumentPosition&) override {
        return {.ok = true, .trace = {.method = "definition"}};
    }
    mornox::SemanticTokens SemanticTokensFull(const mornox::TextDocumentIdentifier&) override {
        return {.ok = true, .trace = {.method = "semantic_tokens/full"}};
    }
    mornox::ReferenceResult References(const mornox::ReferenceRequest& request) override {
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
                .kind = mornox::SymbolReferenceKind::Definition,
            }},
            .trace = {.method = "references"},
        };
    }
    mornox::DocumentSymbolResult DocumentSymbols(const mornox::TextDocumentIdentifier& document) override {
        return {
            .ok = true,
            .symbols = {{
                .id = "main",
                .name = "main",
                .qualified_name = "main",
                .kind = mornox::SymbolKind::Function,
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
    mornox::RenamePrepareResult PrepareRename(const mornox::TextDocumentPosition&) override {
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

inline mornox::Language FakeCppLanguage(mornox::LanguageService* service = nullptr, int priority = 0) {
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

class FakeBuildProvider final : public mornox::BuildProvider {
public:
    std::string Id() const override {
        return "test.build";
    }

    mornox::BuildEnvironment Detect(mornox::WorkspaceContext& context, const mornox::ProjectModel&) const override {
        return {
            .provider_id = Id(),
            .detected = true,
            .build_directory = context.CurrentWorkspace().Info().root_path / "build",
        };
    }

    mornox::BuildPlan Plan(mornox::WorkspaceContext& context, const mornox::BuildRequest&) const override {
        return {
            .provider_id = Id(),
            .title = "Fake build",
            .steps = {{
                .title = "Fake build",
                .request = TestExecutionRequest(TestStdoutCommand("built\n", context.CurrentWorkspace().Info().root_path)),
                .parse_diagnostics = false,
            }},
        };
    }
};

class DiagnosticBuildProvider final : public mornox::BuildProvider {
public:
    std::string Id() const override {
        return "test.diagnostics";
    }

    mornox::BuildEnvironment Detect(mornox::WorkspaceContext& context, const mornox::ProjectModel&) const override {
        return {
            .provider_id = Id(),
            .detected = true,
            .build_directory = context.CurrentWorkspace().Info().root_path / "build",
        };
    }

    mornox::BuildPlan Plan(mornox::WorkspaceContext& context, const mornox::BuildRequest& request) const override {
        return {
            .provider_id = Id(),
            .title = "Diagnostic build",
            .steps = {{
                .title = "Diagnostic build",
                .request = TestExecutionRequest(TestStdoutCommand(
                    "src/main.cpp:2:10: error: expected expression\n",
                    context.CurrentWorkspace().Info().root_path)),
                .parse_diagnostics = request.kind == mornox::BuildRequestKind::Build,
                .diagnostic_base_directory = request.build_directory_override,
            }},
        };
    }
};

class FakeInlineCompletionProvider final : public mornox::CodeCompletionProvider {
public:
    std::string Id() const override {
        return "test.inline";
    }

    mornox::CodeCompletionResult Complete(mornox::WorkspaceContext&, const mornox::CodeCompletionRequest& request) const override {
        if (request.mode != mornox::CodeCompletionMode::Inline) {
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

class FakeModelProvider final : public mornox::ModelProvider {
public:
    std::string Id() const override {
        return "test.model";
    }

    std::vector<mornox::ModelInfo> Models() const override {
        return {{
            .id = "test-model",
            .provider_id = Id(),
            .display_name = "Test Model",
            .capabilities = {"chat", "toolCall"},
        }};
    }

    mornox::ModelResponse Complete(const mornox::ModelRequest& request, mornox::ModelStreamCallback on_event = {}) const override {
        if (on_event) {
            on_event({
                .kind = mornox::ModelStreamEventKind::Delta,
                .text = "Planning",
            });
        }
        return {
            .ok = true,
            .model_id = request.model_id.empty() ? "test-model" : request.model_id,
            .provider_id = Id(),
            .content = "Plan generated",
            .payload = mornox::Value::ObjectValue({{"ok", mornox::Value(true)}}),
        };
    }
};

class ToolCallingModelProvider final : public mornox::ModelProvider {
public:
    std::string Id() const override {
        return "test.tool-model";
    }

    std::vector<mornox::ModelInfo> Models() const override {
        return {{
            .id = "tool-model",
            .provider_id = Id(),
            .display_name = "Tool Model",
            .capabilities = {"chat", "toolCall"},
        }};
    }

    mornox::ModelResponse Complete(const mornox::ModelRequest& request, mornox::ModelStreamCallback = {}) const override {
        return {
            .ok = true,
            .model_id = request.model_id,
            .provider_id = Id(),
            .content = "Use tool",
            .tool_calls = {{
                .id = "call-1",
                .tool_id = "mornox.readFile",
                .input = mornox::Value(mornox::Value::ObjectValue({{"file", mornox::Value("main.cpp")}})),
            }},
            .payload = mornox::Value::ObjectValue({{"tool", mornox::Value(true)}}),
        };
    }
};

class FakeDebugProvider final : public mornox::DebugProvider {
public:
    std::string Id() const override {
        return "test.debug";
    }

    std::vector<std::string> ConfigurationTypes() const override {
        return {"native"};
    }

    mornox::DebugSession Start(
        mornox::WorkspaceContext&,
        mornox::DebugSessionId session_id,
        const mornox::DebugConfiguration& configuration,
        mornox::DebugEventCallback on_event = {}) override {
        if (on_event) {
            on_event({
                .session_id = session_id,
                .kind = mornox::DebugEventKind::Started,
                .message = "Debug started",
            });
        }
        return {
            .id = session_id,
            .provider_id = Id(),
            .configuration = configuration,
            .status = mornox::DebugSessionStatus::Running,
            .message = "Debug started",
        };
    }

    bool Stop(mornox::DebugSessionId) override {
        return true;
    }

    bool ContinueSession(mornox::DebugSessionId) override {
        return true;
    }

    bool Pause(mornox::DebugSessionId) override {
        return true;
    }

    mornox::DebugEvaluationResult Evaluate(mornox::DebugSessionId, const std::string& expression, std::uint64_t) override {
        return {
            .ok = true,
            .value = expression,
            .type = "string",
        };
    }

    std::vector<mornox::StackFrame> StackTrace(mornox::DebugSessionId) const override {
        return {{
            .id = 1,
            .name = "main",
        }};
    }

    std::vector<mornox::DebugVariable> Variables(mornox::DebugSessionId, std::uint64_t) const override {
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

class TestComponent final : public mornox::Component {
public:
    TestComponent(std::string id, ComponentTestStats& stats, bool throw_on_restore = false, bool throw_on_save = false)
        : id_(std::move(id)), stats_(stats), throw_on_restore_(throw_on_restore), throw_on_save_(throw_on_save) {}

    std::string Id() const override {
        return id_;
    }

    void OnAttach(mornox::WorkspaceContext& context) override {
        ++stats_.attached;
        context.OnEvent(*this, mornox::IdeEventKind::ProjectChanged, [this](const mornox::IdeEvent&) {
            ++stats_.events;
        });
    }

    void RestoreState(const mornox::Value& state) override {
        ++stats_.restored;
        if (throw_on_restore_) {
            throw std::runtime_error("restore failed");
        }
        if (state.Contains("value") && state["value"].IsInt()) {
            stats_.restored_value = static_cast<int>(state["value"].AsInt());
        }
    }

    void OnOpenProject(mornox::Project&) override {
        ++stats_.opened;
    }

    void OnProjectChanged(mornox::Project&) override {
        ++stats_.changed;
    }

    mornox::Value SaveState() const override {
        ++stats_.saved;
        if (throw_on_save_) {
            throw std::runtime_error("save failed");
        }
        return mornox::Value::ObjectValue({
            {"value", mornox::Value(static_cast<std::int64_t>(stats_.saved_value))},
        });
    }

    void OnCloseProject(mornox::Project&) override {
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

class RuntimeComponentExtension final : public mornox::CoreExtension {
public:
    explicit RuntimeComponentExtension(ComponentTestStats& stats) : stats_(stats) {}

    void Activate(mornox::ExtensionContext& context) override {
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
    const auto root = std::filesystem::temp_directory_path() / "mornox-tests";
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
