#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "vanta/agent/agent_tool_registry.h"
#include "vanta/agent/agent_context.h"
#include "cpp_index.h"
#include "vanta/execution/problem_matcher.h"
#include "vanta/execution/build_service.h"
#include "vanta/execution/run_configuration.h"
#include "cmake_project_model.h"
#include "internal/projection.h"
#include "vanta/platform/async.h"
#include "vanta/platform/async_job_dispatcher.h"
#include "vanta/plugin/core_plugin.h"
#include "vanta/workspace/workspace.h"
#include "vanta/workspace/workspace_context.h"
#include "vanta/workspace/workspace_runtime.h"
#include "vanta/language/lsp_client.h"
#include "vanta/core/json_codec.h"
#include "vanta/plugin/plugin_manager.h"
#include "vanta/project/project.h"
#include "vanta/project/project_manager.h"
#include "vanta/vfs/virtual_file_system.h"

namespace {

struct AppState {
    vanta::AsyncRuntime async;
    vanta::VirtualFileSystem vfs;
    std::unique_ptr<vanta::WorkspaceRuntime> runtime;
    vanta::ConsoleLogger logger;
    vanta::PluginManager plugins;
    vanta::CorePluginRegistry core_plugins;
    std::vector<vanta::Diagnostic> last_diagnostics;
    std::vector<std::string> change_set_ids;
};

vanta::WorkspaceContext& Ide(AppState& state) {
    return state.runtime->Context();
}

vanta::PluginManager& Plugins(AppState& state) {
    return state.plugins;
}

vanta::ProjectRunConfigurations* ProjectRuns(AppState& state) {
    return Ide(state).RequireProject().GetComponent<vanta::ProjectRunConfigurations>(vanta::ProjectRunConfigurations::kComponentId);
}

std::vector<std::string> SplitCommand(const std::string& line) {
    std::istringstream stream(line);
    std::vector<std::string> parts;
    std::string part;
    while (stream >> part) {
        parts.push_back(part);
    }
    return parts;
}

std::filesystem::path ArgumentPath(const std::vector<std::string>& args, std::size_t index, const std::filesystem::path& fallback) {
    if (args.size() <= index) {
        return fallback;
    }
    return args[index];
}

void PrintHelp() {
    std::cout
        << "Commands:\n"
        << "  help\n"
        << "  tree\n"
        << "  open <file>\n"
        << "  plugins\n"
        << "  plugins.reload [id]\n"
        << "  plugins.unload <id>\n"
        << "  project.graph\n"
        << "  components\n"
        << "  search.files <query>\n"
        << "  search.text <query>\n"
        << "  commands\n"
        << "  build [target]\n"
        << "  test\n"
        << "  run.configs [file]\n"
        << "  run.targets\n"
        << "  run.file <file>\n"
        << "  run <config-id> [target-id]\n"
        << "  git.diff\n"
        << "  agent.read <file>\n"
        << "  agent.context [file]\n"
        << "  agent.propose <file> <replacement-file>\n"
        << "  agent.operation <file> <replacement-file>\n"
        << "  agent.Approve <index>\n"
        << "  agent.apply <index>\n"
        << "  lsp.start\n"
        << "  lsp.completion <file> <line> <character>\n"
        << "  lsp.hover <file> <line> <character>\n"
        << "  quit\n";
}

vanta::Value IndexHitsProjection(const std::vector<vanta::IndexHit>& hits) {
    vanta::Value::Array values;
    for (const vanta::IndexHit& hit : hits) {
        values.push_back(vanta::Value::ObjectValue({
            {"file", vanta::Value(hit.file.ToUri().ToString())},
            {"line", vanta::Value(static_cast<std::int64_t>(hit.range.start.line + 1))},
            {"column", vanta::Value(static_cast<std::int64_t>(hit.range.start.character + 1))},
            {"title", vanta::Value(hit.title)},
            {"preview", vanta::Value(hit.preview)},
            {"providerId", vanta::Value(hit.provider_id)},
            {"score", vanta::Value(static_cast<std::int64_t>(hit.score))},
        }));
    }
    return vanta::Value::ArrayValue(std::move(values));
}

vanta::Value RunConfigurationsProjection(const std::vector<vanta::RunConfiguration>& configurations) {
    vanta::Value::Array values;
    for (const vanta::RunConfiguration& configuration : configurations) {
        values.push_back(vanta::internal::RunConfigurationProjection(configuration));
    }
    return vanta::Value::ArrayValue(std::move(values));
}

vanta::Value ExecutionTargetsProjection(const std::vector<vanta::ExecutionTarget>& targets) {
    vanta::Value::Array values;
    for (const vanta::ExecutionTarget& target : targets) {
        values.push_back(vanta::internal::ExecutionTargetProjection(target));
    }
    return vanta::Value::ArrayValue(std::move(values));
}

vanta::Value StringsProjection(const std::vector<std::string>& values) {
    vanta::Value::Array result;
    for (const std::string& value : values) {
        result.push_back(vanta::Value(value));
    }
    return vanta::Value::ArrayValue(std::move(result));
}

std::string ProjectViewIndent(std::size_t depth) {
    return std::string(depth * 2, ' ');
}

void RenderProjectViewNode(
    vanta::WorkspaceContext& context,
    const std::string& view_id,
    const vanta::ProjectViewNode& node,
    std::ostringstream& stream,
    std::size_t depth,
    std::size_t max_depth) {
    const bool folder_like = node.has_children || node.kind == std::string(vanta::ProjectViewNodeKind::kDirectory);
    stream << ProjectViewIndent(depth) << (folder_like ? "[D] " : "[F] ") << node.label << '\n';
    if (!node.has_children || depth >= max_depth) {
        return;
    }
    for (const vanta::ProjectViewNode& child : context.Projects().Children(context, view_id, node)) {
        RenderProjectViewNode(context, view_id, child, stream, depth + 1, max_depth);
    }
}

std::string RenderProjectView(vanta::WorkspaceContext& context, const std::string& view_id, std::size_t max_depth = 4) {
    std::ostringstream stream;
    for (const vanta::ProjectViewNode& node : context.Projects().TopLevelNodes(context, view_id)) {
        RenderProjectViewNode(context, view_id, node, stream, 0, max_depth);
    }
    return stream.str();
}

vanta::CodeIntelligenceKind CodeIntelligenceKindFromCommand(const std::string& command) {
    if (command == "lsp.hover") {
        return vanta::CodeIntelligenceKind::Hover;
    }
    return vanta::CodeIntelligenceKind::Completion;
}

std::filesystem::path ActiveBuildDirectory(AppState& state) {
    const auto* cmake = Ide(state).RequireProject().Model().Attachment<vanta::CMakeProjectModel>(vanta::CMakeProjectModel::kAttachmentId);
    if (cmake != nullptr && !cmake->build_directory.empty()) {
        return cmake->build_directory;
    }
    return Ide(state).CurrentWorkspace().Info().root_path / "build";
}

void ShutdownCli(AppState& state) {
    if (state.runtime != nullptr) {
        state.runtime->Close();
    }
    state.plugins.DeactivateAll();
    state.runtime.reset();
    state.async.Stop();
}

bool OpenCliWorkspace(
    AppState& state,
    const std::filesystem::path& workspace_path,
    vanta::CorePluginDependencies dependencies,
    std::string* error_message) {
    ShutdownCli(state);
    state.async.Start();

    state.runtime = std::make_unique<vanta::WorkspaceRuntime>(
        state.vfs,
        vanta::AsyncJobDispatcher(state.async),
        [&async = state.async](vanta::JobTask task) {
            async.PostMain(std::move(task));
        });
    if (!state.runtime->Open(workspace_path, error_message, false)) {
        state.runtime.reset();
        state.async.Stop();
        return false;
    }

    vanta::WorkspaceContext& context = state.runtime->Context();
    state.plugins.Scan(context.CurrentWorkspace().Info().root_path / "plugins");
    state.core_plugins = vanta::CreateDefaultCorePluginRegistry(std::move(dependencies));
    for (const std::string& message : state.plugins.ActivateCorePlugins(state.core_plugins, state.logger, context)) {
        state.logger.Info(message);
    }
    for (const std::string& message : state.plugins.ActivateExternalPlugins(state.logger, context)) {
        state.logger.Info(message);
    }
    state.runtime->InitializeWorkspace();
    state.runtime->StartDocumentSync();
    return true;
}

std::vector<std::string> ReloadCliPlugins(AppState& state) {
    std::vector<std::string> messages = state.plugins.ReloadCorePlugins(state.core_plugins, state.logger, Ide(state));
    std::vector<std::string> external_messages = state.plugins.ActivateExternalPlugins(state.logger, Ide(state));
    messages.insert(messages.end(), external_messages.begin(), external_messages.end());
    Ide(state).RefreshProject();
    return messages;
}

std::vector<std::string> ReloadCliPlugin(AppState& state, const std::string& plugin_id) {
    std::vector<std::string> messages = state.plugins.ReloadPlugin(plugin_id, state.logger, Ide(state));
    Ide(state).RefreshProject();
    return messages;
}

bool UnloadCliPlugin(AppState& state, const std::string& plugin_id, std::string* message) {
    const bool ok = state.plugins.UnloadPlugin(plugin_id, message);
    Ide(state).RefreshProject();
    return ok;
}

void RegisterBuiltInCommands(AppState& state) {
    Ide(state).Commands().RegisterCommand("workspace.tree", [&](const vanta::Value&) {
        return vanta::Value(RenderProjectView(Ide(state), "vanta.files"));
    });

    Ide(state).Commands().RegisterCommand("editor.open", [&](const vanta::Value& input) {
        const std::string file = input.StringValue("file").value_or("");
        const vanta::VirtualFile virtual_file = Ide(state).CurrentWorkspace().File(file);
        std::string error;
        vanta::TextDocument* document = Ide(state).Documents().OpenDocument(virtual_file, &error);
        return vanta::Value::ObjectValue({
            {"ok", vanta::Value(document != nullptr)},
            {"uri", vanta::Value(virtual_file.ToUri().ToString())},
            {"version", vanta::Value(static_cast<std::int64_t>(document == nullptr ? 0 : document->version))},
            {"error", vanta::Value(error)},
        });
    });

    Ide(state).Commands().RegisterCommand("plugins.reload", [&](const vanta::Value&) {
        return StringsProjection(ReloadCliPlugins(state));
    });

    Ide(state).Commands().RegisterCommand("plugin.reload", [&](const vanta::Value& input) {
        const std::string plugin_id = input.StringValue("id").value_or("");
        return StringsProjection(ReloadCliPlugin(state, plugin_id));
    });

    Ide(state).Commands().RegisterCommand("plugin.unload", [&](const vanta::Value& input) {
        const std::string plugin_id = input.StringValue("id").value_or("");
        std::string message;
        const bool ok = UnloadCliPlugin(state, plugin_id, &message);
        return vanta::Value::ObjectValue({
            {"ok", vanta::Value(ok)},
            {"message", vanta::Value(message)},
        });
    });

    Ide(state).Commands().RegisterCommand("project.graph", [&](const vanta::Value&) {
        const auto* cmake = Ide(state).RequireProject().Model().Attachment<vanta::CMakeProjectModel>(vanta::CMakeProjectModel::kAttachmentId);
        return cmake == nullptr ? vanta::Value::ObjectValue() : vanta::internal::CMakeProjectGraphProjection(cmake->graph);
    });

    Ide(state).Commands().RegisterCommand("search.files", [&](const vanta::Value& input) {
        const std::string query = input.StringValue("query").value_or("");
        return IndexHitsProjection(Ide(state).Indexes().Query(Ide(state), {
            .kind = vanta::IndexQueryKind::Files,
            .query = query,
        }).hits);
    });

    Ide(state).Commands().RegisterCommand("search.text", [&](const vanta::Value& input) {
        const std::string query = input.StringValue("query").value_or("");
        return IndexHitsProjection(Ide(state).Indexes().Query(Ide(state), {
            .kind = vanta::IndexQueryKind::Text,
            .query = query,
        }).hits);
    });

    Ide(state).Commands().RegisterCommand("run.configurations", [&](const vanta::Value& input) {
        vanta::ProjectRunConfigurations* project_configurations = ProjectRuns(state);
        std::vector<vanta::RunConfiguration> configurations =
            project_configurations == nullptr ? std::vector<vanta::RunConfiguration>() : project_configurations->Configurations(true);
        vanta::VirtualFile focus_file;
        if (auto file = input.StringValue("file")) {
            focus_file = Ide(state).CurrentWorkspace().File(*file);
        }
        std::vector<vanta::RunConfiguration> discovered = Ide(state).RunConfigurations().Discover(Ide(state), focus_file);
        configurations.insert(configurations.end(), discovered.begin(), discovered.end());
        return RunConfigurationsProjection(configurations);
    });

    Ide(state).Commands().RegisterCommand("run.targets", [&](const vanta::Value&) {
        return ExecutionTargetsProjection(Ide(state).Execution().Targets(Ide(state)));
    });

    Ide(state).Commands().RegisterCommand("agent.context", [&](const vanta::Value& input) {
        vanta::AgentContextRequest request;
        request.goal = input.StringValue("goal").value_or("");
        if (auto file = input.StringValue("file")) {
            request.focus_file = Ide(state).CurrentWorkspace().File(*file);
        }
        return vanta::internal::AgentContextProjection(Ide(state).AgentContext().Collect(request, Ide(state)));
    });
}

void PrintStartupSummary(AppState& state) {
    const vanta::ProjectModel& project = Ide(state).RequireProject().Model();
    const auto* cmake = project.Attachment<vanta::CMakeProjectModel>(vanta::CMakeProjectModel::kAttachmentId);
    const auto* cpp = project.Attachment<vanta::CppCompilationDatabase>(vanta::CppCompilationDatabase::kAttachmentId);
    std::cout << "Vanta workspace: " << Ide(state).CurrentWorkspace().Info().root_path.string() << '\n';
    std::cout << "Project: " << vanta::PrimaryProjectType(project) << '\n';
    std::cout << "CMakeLists.txt: " << (cmake != nullptr && cmake->cmake_lists_file.Valid() ? "yes" : "no") << '\n';
    std::cout << "compile_commands.json: " << (cpp != nullptr && cpp->file.Valid() ? cpp->file.ToUri().ToString() : "no") << '\n';
    std::cout << "project graph: " << (cmake == nullptr ? 0 : cmake->graph.source_files.size()) << " source files, "
              << (cmake == nullptr ? 0 : cmake->graph.include_directories.size()) << " include dirs\n";
    std::cout << "plugins: " << Plugins(state).Manifests().size() << '\n';
    std::cout << "active core plugins: " << Plugins(state).ActivePluginIds().size() << '\n';
}

void RunBuildCommand(AppState& state, const std::vector<std::string>& args, vanta::BuildRequestKind kind) {
    vanta::BuildRequest request;
    request.kind = kind;
    request.build_directory_override = ActiveBuildDirectory(state);
    if (args.size() > 1) {
        request.target_id = args[1];
    }

    const vanta::JobKind job_kind = kind == vanta::BuildRequestKind::Build ? vanta::JobKind::Build : vanta::JobKind::Test;
    request.job_id = Ide(state).Jobs().Start(job_kind, vanta::ToString(kind));
    vanta::BuildResult result = Ide(state).Build().Run(Ide(state), request);
    Ide(state).RefreshProject();
    state.last_diagnostics = result.diagnostics;
    Ide(state).Diagnostics().Publish("build", result.diagnostics);
    std::cout << result.output;
    std::cout << "\nexit: " << result.exit_code << ", diagnostics: " << result.diagnostics.size() << '\n';
    for (std::size_t i = 0; i < result.diagnostics.size(); ++i) {
        const vanta::Diagnostic& diagnostic = result.diagnostics[i];
        std::cout << i << ": " << diagnostic.location.file.ToUri().ToString() << ':' << diagnostic.location.line << ':'
                  << diagnostic.location.column << " " << vanta::ToString(diagnostic.severity) << " "
                  << diagnostic.message << '\n';
    }
}

void PrintRunResult(AppState& state, const vanta::RunResult& result) {
    if (!result.output.empty()) {
        std::cout << result.output;
    }
    std::cout << "\nexit: " << result.exit_code << ", diagnostics: " << result.diagnostics.size() << '\n';
    if (!result.diagnostics.empty()) {
        state.last_diagnostics = result.diagnostics;
        Ide(state).Diagnostics().Publish("run", result.diagnostics);
        for (std::size_t i = 0; i < result.diagnostics.size(); ++i) {
            const vanta::Diagnostic& diagnostic = result.diagnostics[i];
            std::cout << i << ": " << diagnostic.location.file.ToUri().ToString() << ':' << diagnostic.location.line << ':'
                      << diagnostic.location.column << " " << vanta::ToString(diagnostic.severity) << " "
                      << diagnostic.message << '\n';
        }
    }
}

void RunConfigurationCommand(AppState& state, const std::string& configuration_id, const std::string& target_id = {}) {
    const vanta::RunResult result = Ide(state).RunConfigurations().RunSaved(Ide(state), configuration_id, target_id);
    PrintRunResult(state, result);
}

void RunFileCommand(AppState& state, const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cout << "Usage: run.file <file>\n";
        return;
    }
    const vanta::VirtualFile file = Ide(state).CurrentWorkspace().File(args[1]);
    std::vector<vanta::RunConfiguration> configurations = Ide(state).RunConfigurations().Discover(Ide(state), file);
    if (configurations.empty()) {
        std::cout << "No run configuration provider matched " << file.ToUri().ToString() << '\n';
        return;
    }
    const vanta::RunResult result = Ide(state).RunConfigurations().Run(Ide(state), configurations.front());
    PrintRunResult(state, result);
}

void HandleAgentPropose(AppState& state, const std::vector<std::string>& args) {
    if (args.size() < 3) {
        std::cout << "Usage: agent.propose <file> <replacement-file>\n";
        return;
    }

    const vanta::VirtualFile original_file = Ide(state).CurrentWorkspace().File(args[1]);
    const vanta::VirtualFile replacement_file = Ide(state).CurrentWorkspace().File(args[2]);
    auto original = original_file.ReadText();
    auto replacement = replacement_file.ReadText();
    if (!original || !replacement) {
        std::cout << "Could not read proposal input files\n";
        return;
    }

    vanta::ChangeSet change_set = Ide(state).Changes().CreateFileReplacement(original_file, "agent", "Agent change set", *original, *replacement);
    state.change_set_ids.push_back(change_set.id);
    Ide(state).Publish({
        .kind = vanta::IdeEventKind::ChangeSetProposed,
        .file = original_file,
        .message = change_set.title,
    });
    std::cout << "change set " << (state.change_set_ids.size() - 1) << " (" << change_set.id << ")\n";
    std::cout << change_set.unified_diff;
}

void HandleAgentOperation(AppState& state, const std::vector<std::string>& args) {
    if (args.size() < 3) {
        std::cout << "Usage: agent.operation <file> <replacement-file>\n";
        return;
    }

    const vanta::VirtualFile original_file = Ide(state).CurrentWorkspace().File(args[1]);
    const vanta::VirtualFile replacement_file = Ide(state).CurrentWorkspace().File(args[2]);
    auto replacement = replacement_file.ReadText();
    if (!replacement) {
        std::cout << "Could not read replacement file\n";
        return;
    }

    const auto snapshot = Ide(state).Documents().ReadSnapshot(original_file);
    vanta::AgentOperationRequest request;
    request.kind = vanta::AgentOperationKind::ProposeFileReplacement;
    request.file = original_file;
    request.source = "agent";
    request.title = "Agent edit";
    request.replacement_text = *replacement;
    request.expected_document_version = snapshot ? snapshot->version : 0;
    const vanta::AgentOperationResult result = Ide(state).AgentOperations().Execute(Ide(state), std::move(request));
    if (!result.change_set_id.empty()) {
        state.change_set_ids.push_back(result.change_set_id);
    }
    std::cout << vanta::ValueToJsonText(vanta::internal::AgentOperationResultProjection(result)) << '\n';
}

void HandleCommand(AppState& state, const std::vector<std::string>& args) {
    if (args.empty()) {
        return;
    }

    const std::string& command = args[0];
    if (command == "help") {
        PrintHelp();
    } else if (command == "tree") {
        std::cout << RenderProjectView(Ide(state), "vanta.files");
    } else if (command == "open") {
        const auto file = ArgumentPath(args, 1, {});
        if (file.empty()) {
            std::cout << "Usage: open <file>\n";
            return;
        }
        const vanta::VirtualFile virtual_file = Ide(state).CurrentWorkspace().File(file);
        std::string error;
        vanta::TextDocument* document = Ide(state).Documents().OpenDocument(virtual_file, &error);
        if (document == nullptr) {
            std::cout << "open failed: " << error << '\n';
        } else {
            std::cout << "opened " << virtual_file.ToUri().ToString() << " version " << document->version << '\n';
        }
    } else if (command == "plugins") {
        const auto active_plugins = Plugins(state).ActivePluginIds();
        for (const vanta::PluginManifest& manifest : Plugins(state).Manifests()) {
            const bool active = std::find(active_plugins.begin(), active_plugins.end(), manifest.extension.id) != active_plugins.end();
            std::cout << manifest.extension.id << " " << manifest.extension.version << " at "
                      << manifest.extension.location.string() << (active ? " [active]" : "") << '\n';
        }
    } else if (command == "plugins.reload") {
        auto result = args.size() > 1
            ? Ide(state).Commands().Execute("plugin.reload", vanta::Value::ObjectValue({{"id", vanta::Value(args[1])}}))
            : Ide(state).Commands().Execute("plugins.reload", vanta::Value::ObjectValue());
        std::cout << (result ? vanta::ValueToJsonText(*result) : "plugin reload is not available") << '\n';
    } else if (command == "plugins.unload") {
        if (args.size() < 2) {
            std::cout << "Usage: plugins.unload <id>\n";
            return;
        }
        auto result = Ide(state).Commands().Execute("plugin.unload", vanta::Value::ObjectValue({{"id", vanta::Value(args[1])}}));
        std::cout << (result ? vanta::ValueToJsonText(*result) : "plugin unload is not available") << '\n';
    } else if (command == "project.graph") {
        auto result = Ide(state).Commands().Execute("project.graph", vanta::Value::ObjectValue());
        std::cout << (result ? vanta::ValueToJsonText(*result) : "project graph is not available") << '\n';
    } else if (command == "components") {
        for (const std::string& id : Ide(state).RequireProject().ComponentIds()) {
            std::cout << id << '\n';
        }
    } else if (command == "search.files" || command == "search.text") {
        if (args.size() < 2) {
            std::cout << "Usage: " << command << " <query>\n";
            return;
        }
        auto result = Ide(state).Commands().Execute(command, vanta::Value::ObjectValue({{"query", vanta::Value(args[1])}}));
        std::cout << (result ? vanta::ValueToJsonText(*result) : "search is not available") << '\n';
    } else if (command == "commands") {
        for (const vanta::CommandDescriptor& descriptor : Ide(state).Commands().Commands()) {
            std::cout << descriptor.id << " - " << descriptor.title << " (" << descriptor.source << ")\n";
        }
    } else if (command == "build") {
        RunBuildCommand(state, args, vanta::BuildRequestKind::Build);
    } else if (command == "test") {
        RunBuildCommand(state, args, vanta::BuildRequestKind::Test);
    } else if (command == "run.configs") {
        vanta::Value input = vanta::Value::ObjectValue();
        if (args.size() > 1) {
            input = vanta::Value::ObjectValue({{"file", vanta::Value(args[1])}});
        }
        auto result = Ide(state).Commands().Execute("run.configurations", input);
        std::cout << (result ? vanta::ValueToJsonText(*result) : "run configurations are not available") << '\n';
    } else if (command == "run.targets") {
        auto result = Ide(state).Commands().Execute("run.targets", vanta::Value::ObjectValue());
        std::cout << (result ? vanta::ValueToJsonText(*result) : "run targets are not available") << '\n';
    } else if (command == "run.file") {
        RunFileCommand(state, args);
    } else if (command == "run") {
        if (args.size() < 2) {
            std::cout << "Usage: run <config-id> [target-id]\n";
            return;
        }
        RunConfigurationCommand(state, args[1], args.size() > 2 ? args[2] : "");
    } else if (command == "git.diff") {
        auto result = Ide(state).Commands().Execute("git.diff", vanta::Value::ObjectValue());
        if (result && result->Contains("text")) {
            std::cout << (*result)["text"].AsString();
        } else {
            std::cout << "git.diff command is not available\n";
        }
    } else if (command == "agent.read") {
        if (args.size() < 2) {
            std::cout << "Usage: agent.read <file>\n";
            return;
        }
        vanta::AgentOperationRequest request;
        request.kind = vanta::AgentOperationKind::ReadFile;
        request.file = Ide(state).CurrentWorkspace().File(args[1]);
        const vanta::AgentOperationResult result = Ide(state).AgentOperations().Execute(Ide(state), request);
        std::cout << vanta::ValueToJsonText(vanta::internal::AgentOperationResultProjection(result)) << '\n';
    } else if (command == "agent.context") {
        vanta::Value input = vanta::Value::ObjectValue();
        if (args.size() > 1) {
            input = vanta::Value::ObjectValue({{"file", vanta::Value(args[1])}});
        }
        auto result = Ide(state).Commands().Execute("agent.context", input);
        std::cout << (result ? vanta::ValueToJsonText(*result) : "agent context is not available") << '\n';
    } else if (command == "agent.propose") {
        HandleAgentPropose(state, args);
    } else if (command == "agent.operation") {
        HandleAgentOperation(state, args);
    } else if (command == "agent.Approve") {
        if (args.size() < 2) {
            std::cout << "Usage: agent.Approve <index>\n";
            return;
        }
        const auto index = static_cast<std::size_t>(std::stoul(args[1]));
        if (index < state.change_set_ids.size()) {
            const auto result = Ide(state).Changes().Approve(state.change_set_ids[index]);
            std::cout << "approved change set " << index << '\n';
            if (!result.ok) {
                std::cout << result.message << '\n';
            }
        }
    } else if (command == "agent.apply") {
        if (args.size() < 2) {
            std::cout << "Usage: agent.apply <index>\n";
            return;
        }
        const auto index = static_cast<std::size_t>(std::stoul(args[1]));
        if (index < state.change_set_ids.size()) {
            const auto result = Ide(state).Changes().ApplyApproved(Ide(state).CurrentWorkspace(), Ide(state).Documents(), state.change_set_ids[index], {.save_after_apply = true});
            Ide(state).Publish({
                .kind = vanta::IdeEventKind::ChangeSetApplied,
                .message = state.change_set_ids[index],
            });
            std::cout << result.message << '\n';
        }
    } else if (command == "lsp.start") {
        auto result = Ide(state).Commands().Execute("clice.start", vanta::Value::ObjectValue());
        const bool ok = result && result->Contains("ok") && (*result)["ok"].AsBool();
        if (ok) {
            std::cout << "clice started\n";
        } else {
            const std::string error = result && result->Contains("error") ? (*result)["error"].AsString() : "command is not available";
            std::cout << "clice start failed: " << error << '\n';
        }
    } else if (command == "lsp.completion" || command == "lsp.hover") {
        if (args.size() < 4) {
            std::cout << "Usage: " << command << " <file> <line> <character>\n";
            return;
        }
        const vanta::VirtualFile file = Ide(state).CurrentWorkspace().File(args[1]);
        std::string error;
        vanta::TextDocument* document = Ide(state).Documents().Document(file);
        if (document == nullptr) {
            document = Ide(state).Documents().OpenDocument(file, &error);
        }

        vanta::CodeIntelligenceRequest request;
        request.kind = CodeIntelligenceKindFromCommand(command);
        request.document.file = file;
        request.document.language_id = "cpp";
        request.document_version = document == nullptr ? 0 : document->version;
        request.position = {
            .line = std::stoi(args[2]),
            .character = std::stoi(args[3]),
        };
        const auto result = Ide(state).CodeIntelligence().Query(Ide(state), request);
        std::cout << vanta::ValueToJsonText(vanta::internal::CodeIntelligenceResultProjection(result)) << '\n';
    } else {
        std::cout << "Unknown command: " << command << '\n';
    }
}

}

int main(int argc, char** argv) {
    std::filesystem::path workspace_path = std::filesystem::current_path();
    std::filesystem::path clice_path;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--workspace" && i + 1 < argc) {
            workspace_path = argv[++i];
        } else if (arg == "--clice" && i + 1 < argc) {
            clice_path = argv[++i];
        } else if (arg == "--help") {
            PrintHelp();
            return 0;
        }
    }

    AppState state;
    vanta::CorePluginDependencies core_plugin_dependencies;
    if (!clice_path.empty()) {
        core_plugin_dependencies.clice.server_path = clice_path;
    }
    std::string error;
    if (!OpenCliWorkspace(state, workspace_path, std::move(core_plugin_dependencies), &error)) {
        std::cerr << error << '\n';
        return 1;
    }

    RegisterBuiltInCommands(state);

    PrintStartupSummary(state);
    PrintHelp();

    std::string line;
    while (state.async.DrainMain(), std::cout << "vanta> " && std::getline(std::cin, line)) {
        const auto args = SplitCommand(line);
        if (!args.empty() && (args[0] == "quit" || args[0] == "exit")) {
            break;
        }
        HandleCommand(state, args);
    }

    ShutdownCli(state);
    return 0;
}
