#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "vanta/agent/agent_tool_registry.h"
#include "vanta/execution/problem_matcher.h"
#include "vanta/execution/build_service.h"
#include "vanta/builtin/cmake/cmake_project_model.h"
#include "vanta/workspace/editor.h"
#include "vanta/workspace/workspace.h"
#include "vanta/builtin/git/git_client.h"
#include "vanta/workspace/ide_application.h"
#include "vanta/workspace/ide_environment.h"
#include "vanta/workspace/workspace_runtime.h"
#include "vanta/language/lsp_client.h"
#include "vanta/plugin/plugin_manager.h"

namespace {

struct AppState {
    vanta::IdeApplication app;
    std::vector<vanta::Diagnostic> lastDiagnostics;
    std::vector<std::string> changeSetIds;
};

vanta::WorkspaceRuntime& ide(AppState& state) {
    return state.app.runtime();
}

vanta::PluginManager& plugins(AppState& state) {
    return state.app.plugins();
}

vanta::LayoutStateStore& layout(AppState& state) {
    return *ide(state).project().getComponent<vanta::LayoutStateStore>(vanta::LayoutStateStore::componentId);
}

std::vector<std::string> splitCommand(const std::string& line) {
    std::istringstream stream(line);
    std::vector<std::string> parts;
    std::string part;
    while (stream >> part) {
        parts.push_back(part);
    }
    return parts;
}

std::filesystem::path argumentPath(const std::vector<std::string>& args, std::size_t index, const std::filesystem::path& fallback) {
    if (args.size() <= index) {
        return fallback;
    }
    return args[index];
}

void printHelp() {
    std::cout
        << "Commands:\n"
        << "  help\n"
        << "  tree\n"
        << "  tabs\n"
        << "  open <file>\n"
        << "  plugins\n"
        << "  plugins.reload [id]\n"
        << "  plugins.unload <id>\n"
        << "  contributions\n"
        << "  project.graph\n"
        << "  ui.state\n"
        << "  layout.state\n"
        << "  components\n"
        << "  palette [query]\n"
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
        << "  agent.run <file> <replacement-file>\n"
        << "  agent.approve <index>\n"
        << "  agent.apply <index>\n"
        << "  lsp.start\n"
        << "  lsp.completion <file> <line> <character>\n"
        << "  lsp.hover <file> <line> <character>\n"
        << "  quit\n";
}

vanta::Json contributionToJson(const vanta::Contribution& contribution) {
    return vanta::Json::object({
        {"kind", vanta::Json(vanta::toString(contribution.kind))},
        {"id", vanta::Json(contribution.id)},
        {"title", vanta::Json(contribution.title)},
        {"pluginId", vanta::Json(contribution.pluginId)},
    });
}

vanta::Json searchHitsToJson(const std::vector<vanta::SearchHit>& hits) {
    vanta::Json::Array values;
    for (const vanta::SearchHit& hit : hits) {
        values.push_back(vanta::Json::object({
            {"file", vanta::Json(hit.file.toUri().string())},
            {"line", vanta::Json(static_cast<std::int64_t>(hit.line))},
            {"column", vanta::Json(static_cast<std::int64_t>(hit.column))},
            {"preview", vanta::Json(hit.preview)},
            {"score", vanta::Json(static_cast<std::int64_t>(hit.score))},
        }));
    }
    return vanta::Json::array(std::move(values));
}

vanta::Json paletteItemsToJson(const std::vector<vanta::CommandPaletteItem>& items) {
    vanta::Json::Array values;
    for (const vanta::CommandPaletteItem& item : items) {
        values.push_back(vanta::Json::object({
            {"id", vanta::Json(item.id)},
            {"title", vanta::Json(item.title)},
            {"keybinding", vanta::Json(item.keybinding)},
            {"source", vanta::Json(item.source)},
            {"enabled", vanta::Json(item.enabled)},
        }));
    }
    return vanta::Json::array(std::move(values));
}

vanta::Json runConfigurationsToJson(const std::vector<vanta::RunConfiguration>& configurations) {
    vanta::Json::Array values;
    for (const vanta::RunConfiguration& configuration : configurations) {
        values.push_back(vanta::toJson(configuration));
    }
    return vanta::Json::array(std::move(values));
}

vanta::Json executionTargetsToJson(const std::vector<vanta::ExecutionTarget>& targets) {
    vanta::Json::Array values;
    for (const vanta::ExecutionTarget& target : targets) {
        values.push_back(vanta::toJson(target));
    }
    return vanta::Json::array(std::move(values));
}

vanta::Json stringsToJson(const std::vector<std::string>& values) {
    vanta::Json::Array result;
    for (const std::string& value : values) {
        result.push_back(vanta::Json(value));
    }
    return vanta::Json::array(std::move(result));
}

vanta::Json layoutStateToJsonSummary(const vanta::LayoutState& layout) {
    vanta::Json::Array tabs;
    for (const vanta::Uri& uri : layout.openTabs) {
        tabs.push_back(vanta::Json(uri.string()));
    }
    return vanta::Json::object({
        {"openTabs", vanta::Json::array(std::move(tabs))},
        {"activeFile", vanta::Json(layout.activeFile.string())},
        {"fileTreeVisible", vanta::Json(layout.fileTreeVisible)},
        {"problemsVisible", vanta::Json(layout.problemsVisible)},
        {"buildPanelVisible", vanta::Json(layout.buildPanelVisible)},
        {"agentPanelVisible", vanta::Json(layout.agentPanelVisible)},
        {"gitPanelVisible", vanta::Json(layout.gitPanelVisible)},
        {"lastBuildTarget", vanta::Json(layout.lastBuildTarget)},
    });
}

vanta::LanguageRequestKind languageKindFromCommand(const std::string& command) {
    if (command == "lsp.hover") {
        return vanta::LanguageRequestKind::Hover;
    }
    return vanta::LanguageRequestKind::Completion;
}

std::filesystem::path activeBuildDirectory(AppState& state) {
    const auto* cmake = ide(state).project().model().attachment<vanta::CMakeProjectModel>(vanta::CMakeProjectModel::attachmentId);
    if (cmake != nullptr && !cmake->buildDirectory.empty()) {
        return cmake->buildDirectory;
    }
    return ide(state).workspace().info().rootPath / "build";
}

void registerBuiltInCommands(AppState& state) {
    ide(state).keybindings().bind({.commandId = "editor.open", .key = "Cmd+O", .when = "workspaceOpen"});
    ide(state).keybindings().bind({.commandId = "command.palette", .key = "Cmd+Shift+P", .when = "workspaceOpen"});
    ide(state).keybindings().bind({.commandId = "cmake.build", .key = "Cmd+B", .when = "project == cmake"});

    ide(state).commands().add("workspace.tree", [&](const vanta::Json&) {
        return vanta::Json(vanta::renderFileTree(ide(state).workspace().fileTree()));
    });

    ide(state).commands().add("editor.open", [&](const vanta::Json& input) {
        const std::string file = input.stringValue("file").value_or("");
        const vanta::VirtualFile virtualFile = ide(state).workspace().file(file);
        ide(state).documents().openDocument(virtualFile);
        vanta::EditorTab& tab = ide(state).editor().openFile(virtualFile);
        ide(state).ui().refresh();
        return vanta::Json::object({
            {"id", vanta::Json(static_cast<std::int64_t>(tab.id))},
            {"title", vanta::Json(tab.title)},
            {"placeholderEditor", vanta::Json(tab.placeholderEditor)},
        });
    });

    ide(state).commands().add("contributions.list", [&](const vanta::Json&) {
        vanta::Json::Array values;
        for (const vanta::Contribution& contribution : ide(state).contributions().list()) {
            values.push_back(contributionToJson(contribution));
        }
        return vanta::Json::array(std::move(values));
    });

    ide(state).commands().add("plugins.reload", [&](const vanta::Json&) {
        return stringsToJson(state.app.reloadPlugins());
    });

    ide(state).commands().add("plugin.reload", [&](const vanta::Json& input) {
        const std::string pluginId = input.stringValue("id").value_or("");
        return stringsToJson(state.app.reloadPlugin(pluginId));
    });

    ide(state).commands().add("plugin.unload", [&](const vanta::Json& input) {
        const std::string pluginId = input.stringValue("id").value_or("");
        std::string message;
        const bool ok = state.app.unloadPlugin(pluginId, &message);
        return vanta::Json::object({
            {"ok", vanta::Json(ok)},
            {"message", vanta::Json(message)},
        });
    });

    ide(state).commands().add("project.graph", [&](const vanta::Json&) {
        const auto* cmake = ide(state).project().model().attachment<vanta::CMakeProjectModel>(vanta::CMakeProjectModel::attachmentId);
        return cmake == nullptr ? vanta::Json::object() : vanta::toJson(cmake->graph);
    });

    ide(state).commands().add("ui.state", [&](const vanta::Json&) {
        const vanta::UiState& ui = ide(state).ui().state();
        return vanta::Json::object({
            {"workspaceOpen", vanta::Json(ui.workspaceOpen)},
            {"workspace", vanta::Json(ui.workspace.rootPath.string())},
            {"project", vanta::Json(vanta::primaryProjectType(ui.project))},
            {"tabs", vanta::Json(static_cast<std::int64_t>(ui.tabs.size()))},
            {"problems", vanta::Json(static_cast<std::int64_t>(ui.problems.size()))},
            {"jobs", vanta::Json(static_cast<std::int64_t>(ui.jobs.size()))},
            {"contributions", vanta::Json(static_cast<std::int64_t>(ui.contributions.size()))},
            {"version", vanta::Json(static_cast<std::int64_t>(ui.version))},
        });
    });

    ide(state).commands().add("layout.state", [&](const vanta::Json&) {
        layout(state).capture(ide(state).ui().state());
        return layoutStateToJsonSummary(layout(state).state());
    });

    ide(state).commands().add("search.files", [&](const vanta::Json& input) {
        const std::string query = input.stringValue("query").value_or("");
        return searchHitsToJson(ide(state).search().searchFiles(query));
    });

    ide(state).commands().add("search.text", [&](const vanta::Json& input) {
        const std::string query = input.stringValue("query").value_or("");
        return searchHitsToJson(ide(state).search().searchText(query));
    });

    ide(state).commands().add("command.palette", [&](const vanta::Json& input) {
        const std::string query = input.stringValue("query").value_or("");
        auto items = ide(state).commandPalette().items(ide(state).commands(), ide(state).contributions(), ide(state).keybindings());
        return paletteItemsToJson(ide(state).commandPalette().filter(items, query));
    });

    ide(state).commands().add("run.configurations", [&](const vanta::Json& input) {
        std::vector<vanta::RunConfiguration> configurations = ide(state).runConfigurations().configurations(true);
        vanta::VirtualFile focusFile;
        if (auto file = input.stringValue("file")) {
            focusFile = ide(state).workspace().file(*file);
        }
        std::vector<vanta::RunConfiguration> produced = ide(state).runConfigurations().produce(ide(state).context(), focusFile);
        configurations.insert(configurations.end(), produced.begin(), produced.end());
        return runConfigurationsToJson(configurations);
    });

    ide(state).commands().add("run.targets", [&](const vanta::Json&) {
        return executionTargetsToJson(ide(state).execution().targets(ide(state).context()));
    });

    ide(state).agent().addTool({
        .id = "vanta.readFile",
        .description = "Read a text file from the current workspace.",
        .inputSchema = vanta::Json::object({
            {"type", vanta::Json("object")},
            {"required", vanta::Json::array({vanta::Json("file")})},
        }),
        .handler = [&](const vanta::Json& input) {
            const std::string file = input.stringValue("file").value_or("");
            const vanta::VirtualFile virtualFile = ide(state).workspace().file(file);
            const auto snapshot = ide(state).documents().readSnapshot(virtualFile);
            return vanta::Json::object({
                {"ok", vanta::Json(snapshot.has_value())},
                {"uri", vanta::Json(virtualFile.toUri().string())},
                {"open", vanta::Json(snapshot ? snapshot->open : false)},
                {"dirty", vanta::Json(snapshot ? snapshot->dirty : false)},
                {"version", vanta::Json(static_cast<std::int64_t>(snapshot ? snapshot->version : 0))},
                {"text", vanta::Json(snapshot ? snapshot->text : "")},
            });
        },
    });

    ide(state).commands().add("agent.context", [&](const vanta::Json& input) {
        vanta::AgentContextRequest request;
        request.goal = input.stringValue("goal").value_or("");
        if (auto file = input.stringValue("file")) {
            request.focusFile = ide(state).workspace().file(*file);
        }
        return vanta::toJson(ide(state).agentContext().collect(request, ide(state).context()));
    });
}

void printStartupSummary(AppState& state) {
    const vanta::ProjectModel& project = ide(state).project().model();
    const auto* cmake = project.attachment<vanta::CMakeProjectModel>(vanta::CMakeProjectModel::attachmentId);
    const auto* cpp = project.attachment<vanta::CppCompilationDatabase>(vanta::CppCompilationDatabase::attachmentId);
    std::cout << "Vanta workspace: " << ide(state).workspace().info().rootPath.string() << '\n';
    std::cout << "Project: " << vanta::primaryProjectType(project) << '\n';
    std::cout << "CMakeLists.txt: " << (cmake != nullptr && cmake->cmakeListsFile.valid() ? "yes" : "no") << '\n';
    std::cout << "compile_commands.json: " << (cpp != nullptr && cpp->file.valid() ? cpp->file.toUri().string() : "no") << '\n';
    std::cout << "project graph: " << (cmake == nullptr ? 0 : cmake->graph.sourceFiles.size()) << " source files, "
              << (cmake == nullptr ? 0 : cmake->graph.includeDirectories.size()) << " include dirs\n";
    std::cout << "plugins: " << plugins(state).manifests().size() << '\n';
    std::cout << "active core plugins: " << plugins(state).activePluginIds().size() << '\n';
    std::cout << "editor: placeholder panels enabled\n";
}

void printTabs(const vanta::EditorWorkspace& editor) {
    if (editor.tabs().empty()) {
        std::cout << "No open tabs\n";
        return;
    }
    for (const vanta::EditorTab& tab : editor.tabs()) {
        std::cout << (tab.active ? "* " : "  ") << tab.id << " " << tab.title;
        if (tab.placeholderEditor) {
            std::cout << " [placeholder]";
        }
        std::cout << '\n';
    }
}

void runBuildCommand(AppState& state, const std::vector<std::string>& args, vanta::BuildTaskKind kind) {
    vanta::BuildTask task;
    task.kind = kind;
    task.buildDirectory = activeBuildDirectory(state);
    if (args.size() > 1) {
        task.target = args[1];
        layout(state).rememberBuildTarget(args[1]);
    }

    const vanta::JobKind jobKind = kind == vanta::BuildTaskKind::Build ? vanta::JobKind::Build : vanta::JobKind::Test;
    task.jobId = ide(state).jobs().start(jobKind, vanta::toString(kind));
    vanta::BuildResult result = ide(state).context().run(ide(state).workspace().info().rootPath, task);
    ide(state).refreshProject();
    state.lastDiagnostics = result.diagnostics;
    ide(state).diagnostics().publish("build", result.diagnostics);
    ide(state).ui().refresh();
    std::cout << result.output;
    std::cout << "\nexit: " << result.exitCode << ", diagnostics: " << result.diagnostics.size() << '\n';
    for (std::size_t i = 0; i < result.diagnostics.size(); ++i) {
        const vanta::Diagnostic& diagnostic = result.diagnostics[i];
        std::cout << i << ": " << diagnostic.location.file.toUri().string() << ':' << diagnostic.location.line << ':'
                  << diagnostic.location.column << " " << vanta::toString(diagnostic.severity) << " "
                  << diagnostic.message << '\n';
        ide(state).editor().openDiagnostic(diagnostic);
    }
}

void printRunResult(AppState& state, const vanta::RunResult& result) {
    if (!result.output.empty()) {
        std::cout << result.output;
    }
    std::cout << "\nexit: " << result.exitCode << ", diagnostics: " << result.diagnostics.size() << '\n';
    if (!result.diagnostics.empty()) {
        state.lastDiagnostics = result.diagnostics;
        ide(state).diagnostics().publish("run", result.diagnostics);
        for (std::size_t i = 0; i < result.diagnostics.size(); ++i) {
            const vanta::Diagnostic& diagnostic = result.diagnostics[i];
            std::cout << i << ": " << diagnostic.location.file.toUri().string() << ':' << diagnostic.location.line << ':'
                      << diagnostic.location.column << " " << vanta::toString(diagnostic.severity) << " "
                      << diagnostic.message << '\n';
            ide(state).editor().openDiagnostic(diagnostic);
        }
    }
    ide(state).ui().refresh();
}

void runConfiguration(AppState& state, const std::string& configurationId, const std::string& targetId = {}) {
    const vanta::RunResult result = ide(state).runConfigurations().run(ide(state).context(), configurationId, targetId);
    printRunResult(state, result);
}

void runFileCommand(AppState& state, const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cout << "Usage: run.file <file>\n";
        return;
    }
    const vanta::VirtualFile file = ide(state).workspace().file(args[1]);
    std::vector<vanta::RunConfiguration> configurations = ide(state).runConfigurations().produce(ide(state).context(), file);
    if (configurations.empty()) {
        std::cout << "No run configuration producer matched " << file.toUri().string() << '\n';
        return;
    }
    vanta::RunConfiguration configuration = std::move(configurations.front());
    const std::string id = configuration.id;
    ide(state).runConfigurations().addConfiguration(std::move(configuration));
    runConfiguration(state, id);
}

void handleAgentPropose(AppState& state, const std::vector<std::string>& args) {
    if (args.size() < 3) {
        std::cout << "Usage: agent.propose <file> <replacement-file>\n";
        return;
    }

    const vanta::VirtualFile originalFile = ide(state).workspace().file(args[1]);
    const vanta::VirtualFile replacementFile = ide(state).workspace().file(args[2]);
    auto original = originalFile.readText();
    auto replacement = replacementFile.readText();
    if (!original || !replacement) {
        std::cout << "Could not read proposal input files\n";
        return;
    }

    vanta::ChangeSet changeSet = ide(state).changes().createFileReplacement(originalFile, "agent", "Agent change set", *original, *replacement);
    state.changeSetIds.push_back(changeSet.id);
    ide(state).publish({
        .kind = vanta::IdeEventKind::ChangeSetProposed,
        .file = originalFile,
        .message = changeSet.title,
    });
    std::cout << "change set " << (state.changeSetIds.size() - 1) << " (" << changeSet.id << ")\n";
    std::cout << changeSet.unifiedDiff;
}

void handleAgentRun(AppState& state, const std::vector<std::string>& args) {
    if (args.size() < 3) {
        std::cout << "Usage: agent.run <file> <replacement-file>\n";
        return;
    }

    const vanta::VirtualFile originalFile = ide(state).workspace().file(args[1]);
    const vanta::VirtualFile replacementFile = ide(state).workspace().file(args[2]);
    auto replacement = replacementFile.readText();
    if (!replacement) {
        std::cout << "Could not read replacement file\n";
        return;
    }

    vanta::AgentRunRequest request;
    request.goal = "Agent edit";
    request.focusFile = originalFile;
    request.targetFile = originalFile;
    request.replacementText = *replacement;
    const vanta::AgentRun run = ide(state).agentOperations().startRun(ide(state).context(), std::move(request));
    if (!run.changeSetId.empty()) {
        state.changeSetIds.push_back(run.changeSetId);
    }
    std::cout << vanta::toJson(run).dump() << '\n';
}

void handleCommand(AppState& state, const std::vector<std::string>& args) {
    if (args.empty()) {
        return;
    }

    const std::string& command = args[0];
    if (command == "help") {
        printHelp();
    } else if (command == "tree") {
        std::cout << vanta::renderFileTree(ide(state).workspace().fileTree());
    } else if (command == "tabs") {
        printTabs(ide(state).editor());
    } else if (command == "open") {
        const auto file = argumentPath(args, 1, {});
        if (file.empty()) {
            std::cout << "Usage: open <file>\n";
            return;
        }
        const vanta::VirtualFile virtualFile = ide(state).workspace().file(file);
        ide(state).documents().openDocument(virtualFile);
        const vanta::EditorTab& tab = ide(state).editor().openFile(virtualFile);
        ide(state).ui().refresh();
        std::cout << "opened tab " << tab.id << " for " << tab.file.toUri().string() << '\n';
    } else if (command == "plugins") {
        const auto activePlugins = plugins(state).activePluginIds();
        for (const vanta::PluginManifest& manifest : plugins(state).manifests()) {
            const bool active = std::find(activePlugins.begin(), activePlugins.end(), manifest.extension.id) != activePlugins.end();
            std::cout << manifest.extension.id << " " << manifest.extension.version << " at "
                      << manifest.extension.location.string() << (active ? " [active]" : "") << '\n';
        }
    } else if (command == "plugins.reload") {
        auto result = args.size() > 1
            ? ide(state).commands().execute("plugin.reload", vanta::Json::object({{"id", vanta::Json(args[1])}}))
            : ide(state).commands().execute("plugins.reload", vanta::Json::object());
        std::cout << (result ? result->dump() : "plugin reload is not available") << '\n';
    } else if (command == "plugins.unload") {
        if (args.size() < 2) {
            std::cout << "Usage: plugins.unload <id>\n";
            return;
        }
        auto result = ide(state).commands().execute("plugin.unload", vanta::Json::object({{"id", vanta::Json(args[1])}}));
        std::cout << (result ? result->dump() : "plugin unload is not available") << '\n';
    } else if (command == "contributions") {
        auto result = ide(state).commands().execute("contributions.list", vanta::Json::object());
        std::cout << (result ? result->dump() : "contribution registry is not available") << '\n';
    } else if (command == "project.graph") {
        auto result = ide(state).commands().execute("project.graph", vanta::Json::object());
        std::cout << (result ? result->dump() : "project graph is not available") << '\n';
    } else if (command == "ui.state") {
        auto result = ide(state).commands().execute("ui.state", vanta::Json::object());
        std::cout << (result ? result->dump() : "ui state is not available") << '\n';
    } else if (command == "layout.state") {
        auto result = ide(state).commands().execute("layout.state", vanta::Json::object());
        std::cout << (result ? result->dump() : "layout state is not available") << '\n';
    } else if (command == "components") {
        for (const std::string& id : ide(state).project().components().ids()) {
            std::cout << id << '\n';
        }
    } else if (command == "palette") {
        const std::string query = args.size() > 1 ? args[1] : "";
        auto result = ide(state).commands().execute("command.palette", vanta::Json::object({{"query", vanta::Json(query)}}));
        std::cout << (result ? result->dump() : "command palette is not available") << '\n';
    } else if (command == "search.files" || command == "search.text") {
        if (args.size() < 2) {
            std::cout << "Usage: " << command << " <query>\n";
            return;
        }
        auto result = ide(state).commands().execute(command, vanta::Json::object({{"query", vanta::Json(args[1])}}));
        std::cout << (result ? result->dump() : "search is not available") << '\n';
    } else if (command == "commands") {
        for (const std::string& id : ide(state).commands().list()) {
            std::cout << id << '\n';
        }
    } else if (command == "build") {
        runBuildCommand(state, args, vanta::BuildTaskKind::Build);
    } else if (command == "test") {
        runBuildCommand(state, args, vanta::BuildTaskKind::Test);
    } else if (command == "run.configs") {
        vanta::Json input = vanta::Json::object();
        if (args.size() > 1) {
            input = vanta::Json::object({{"file", vanta::Json(args[1])}});
        }
        auto result = ide(state).commands().execute("run.configurations", input);
        std::cout << (result ? result->dump() : "run configurations are not available") << '\n';
    } else if (command == "run.targets") {
        auto result = ide(state).commands().execute("run.targets", vanta::Json::object());
        std::cout << (result ? result->dump() : "run targets are not available") << '\n';
    } else if (command == "run.file") {
        runFileCommand(state, args);
    } else if (command == "run") {
        if (args.size() < 2) {
            std::cout << "Usage: run <config-id> [target-id]\n";
            return;
        }
        runConfiguration(state, args[1], args.size() > 2 ? args[2] : "");
    } else if (command == "git.diff") {
        auto result = ide(state).commands().execute("git.diff", vanta::Json::object());
        if (result && result->contains("text")) {
            std::cout << (*result)["text"].asString();
        } else {
            std::cout << "git.diff command is not available\n";
        }
    } else if (command == "agent.read") {
        if (args.size() < 2) {
            std::cout << "Usage: agent.read <file>\n";
            return;
        }
        auto result = ide(state).agent().callTool("vanta.readFile", vanta::Json::object({{"file", vanta::Json(args[1])}}));
        std::cout << (result ? result->dump() : "tool not found") << '\n';
    } else if (command == "agent.context") {
        vanta::Json input = vanta::Json::object();
        if (args.size() > 1) {
            input = vanta::Json::object({{"file", vanta::Json(args[1])}});
        }
        auto result = ide(state).commands().execute("agent.context", input);
        std::cout << (result ? result->dump() : "agent context is not available") << '\n';
    } else if (command == "agent.propose") {
        handleAgentPropose(state, args);
    } else if (command == "agent.run") {
        handleAgentRun(state, args);
    } else if (command == "agent.approve") {
        if (args.size() < 2) {
            std::cout << "Usage: agent.approve <index>\n";
            return;
        }
        const auto index = static_cast<std::size_t>(std::stoul(args[1]));
        if (index < state.changeSetIds.size()) {
            const auto result = ide(state).changes().approve(state.changeSetIds[index]);
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
        if (index < state.changeSetIds.size()) {
            const auto result = ide(state).changes().applyApproved(ide(state).workspace(), ide(state).documents(), state.changeSetIds[index], {.saveAfterApply = true});
            ide(state).publish({
                .kind = vanta::IdeEventKind::ChangeSetApplied,
                .message = state.changeSetIds[index],
            });
            ide(state).ui().refresh();
            std::cout << result.message << '\n';
        }
    } else if (command == "lsp.start") {
        auto result = ide(state).commands().execute("clice.start", vanta::Json::object());
        const bool ok = result && result->contains("ok") && (*result)["ok"].asBool();
        if (ok) {
            std::cout << "clice started\n";
        } else {
            const std::string error = result && result->contains("error") ? (*result)["error"].asString() : "command is not available";
            std::cout << "clice start failed: " << error << '\n';
        }
    } else if (command == "lsp.completion" || command == "lsp.hover") {
        if (args.size() < 4) {
            std::cout << "Usage: " << command << " <file> <line> <character>\n";
            return;
        }
        const vanta::VirtualFile file = ide(state).workspace().file(args[1]);
        std::string error;
        vanta::TextDocument* document = ide(state).documents().document(file);
        if (document == nullptr) {
            document = ide(state).documents().openDocument(file, &error);
        }

        vanta::LanguageRequest request;
        request.kind = languageKindFromCommand(command);
        request.document.file = file;
        request.document.languageId = "cpp";
        request.documentVersion = document == nullptr ? 0 : document->version;
        request.position = {
            .line = std::stoi(args[2]),
            .character = std::stoi(args[3]),
        };
        const auto result = ide(state).languageRequests().execute(request, ide(state).documents(), ide(state).languages());
        std::cout << vanta::languagePipelineResultToJson(result).dump() << '\n';
    } else {
        std::cout << "Unknown command: " << command << '\n';
    }
}

}

int main(int argc, char** argv) {
    std::filesystem::path workspacePath = std::filesystem::current_path();
    std::filesystem::path clicePath;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--workspace" && i + 1 < argc) {
            workspacePath = argv[++i];
        } else if (arg == "--clice" && i + 1 < argc) {
            clicePath = argv[++i];
        } else if (arg == "--help") {
            printHelp();
            return 0;
        }
    }

    AppState state;
    std::string error;
    if (!state.app.openWorkspace(workspacePath, clicePath, &error)) {
        std::cerr << error << '\n';
        return 1;
    }

    registerBuiltInCommands(state);

    printStartupSummary(state);
    printHelp();

    std::string line;
    while (state.app.services().async.drainMain(), std::cout << "vanta> " && std::getline(std::cin, line)) {
        const auto args = splitCommand(line);
        if (!args.empty() && (args[0] == "quit" || args[0] == "exit")) {
            break;
        }
        handleCommand(state, args);
    }

    state.app.shutdown();
    return 0;
}
