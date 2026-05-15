#include <algorithm>
#include <cassert>
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
#include "vanta/agent/agent_tool_registry.h"
#include "vanta/execution/build_service.h"
#include "vanta/execution/problem_matcher.h"
#include "vanta/workspace/change_set_service.h"
#include "vanta/builtin/cmake/cmake_project_model.h"
#include "vanta/language/code_intelligence_service.h"
#include "vanta/workspace/diagnostic_service.h"
#include "vanta/workspace/document_service.h"
#include "vanta/workspace/workspace.h"
#include "vanta/builtin/git/git_client.h"
#include "vanta/language/document_language_sync.h"
#include "vanta/workspace/workspace_runtime.h"
#include "vanta/language/lsp_client.h"
#include "vanta/plugin/core_plugin.h"
#include "vanta/plugin/plugin_protocol.h"
#include "vanta/plugin/plugin_manager.h"
#include "vanta/platform/process.h"
#include "vanta/platform/result.h"
#include "vanta/project/project_manager.h"
#include "vanta/project/project_state_store.h"
#include "vanta/plugin/approval_service.h"
#include "vanta/workspace/settings_service.h"
#include "vanta/platform/json.h"
#include "vanta/vfs/virtual_file_system.h"

namespace {

class FakeLanguageService final : public vanta::LanguageService {
public:
    bool start(std::string*) override { return true; }
    bool running() const override { return true; }
    void stop() override {}

    void didOpen(const vanta::TextDocument&) override { ++opened; }
    void didChange(const vanta::TextDocument&) override { ++changed; }
    void didSave(const vanta::TextDocument&) override { ++saved; }
    void didClose(const vanta::VirtualFile&) override { ++closed; }

    vanta::CompletionList completion(const vanta::TextDocumentPosition&) override {
        return {
            .ok = true,
            .items = {{
                .label = "sample",
                .insertText = "sample",
                .detail = "Fake completion",
            }},
            .raw = vanta::Json::object({{"kind", vanta::Json("completion")}}),
            .trace = {.method = "completion"},
        };
    }
    vanta::HoverResult hover(const vanta::TextDocumentPosition&) override {
        return {.ok = true, .contents = "Fake hover", .raw = vanta::Json::object({{"kind", vanta::Json("hover")}}), .trace = {.method = "hover"}};
    }
    vanta::LocationResult definition(const vanta::TextDocumentPosition&) override {
        return {.ok = true, .raw = vanta::Json::object({{"kind", vanta::Json("definition")}}), .trace = {.method = "definition"}};
    }
    vanta::SemanticTokens semanticTokensFull(const vanta::TextDocumentIdentifier&) override {
        return {.ok = true, .raw = vanta::Json::object({{"kind", vanta::Json("semanticTokens")}}), .trace = {.method = "semanticTokens/full"}};
    }

    int opened = 0;
    int changed = 0;
    int saved = 0;
    int closed = 0;
};

vanta::Language fakeCppLanguage(vanta::LanguageService* service = nullptr, int priority = 0) {
    return {
        .id = "cpp",
        .definition = {
            .displayName = "C++",
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
    std::string id() const override {
        return "test.build";
    }

    vanta::BuildEnvironment detect(const std::filesystem::path& workspaceRoot) const override {
        return {
            .providerId = id(),
            .detected = true,
            .buildDirectory = workspaceRoot / "build",
        };
    }

    vanta::BuildPlan plan(const std::filesystem::path& workspaceRoot, const vanta::BuildTask&) const override {
        return {
            .providerId = id(),
            .title = "Fake build",
            .steps = {{
                .title = "Fake build",
                .request = {
                    .executable = "/bin/sh",
                    .arguments = {"-c", "printf 'built\\n'"},
                    .workingDirectory = workspaceRoot,
                },
                .parseDiagnostics = false,
            }},
        };
    }
};

class DiagnosticBuildProvider final : public vanta::BuildProvider {
public:
    std::string id() const override {
        return "test.diagnostics";
    }

    vanta::BuildEnvironment detect(const std::filesystem::path& workspaceRoot) const override {
        return {
            .providerId = id(),
            .detected = true,
            .buildDirectory = workspaceRoot / "build",
        };
    }

    vanta::BuildPlan plan(const std::filesystem::path& workspaceRoot, const vanta::BuildTask& task) const override {
        return {
            .providerId = id(),
            .title = "Diagnostic build",
            .steps = {{
                .title = "Diagnostic build",
                .request = {
                    .executable = "/bin/sh",
                    .arguments = {"-c", "printf 'src/main.cpp:2:10: error: expected expression\\n'"},
                    .workingDirectory = workspaceRoot,
                },
                .parseDiagnostics = task.kind == vanta::BuildTaskKind::Build,
            }},
        };
    }
};

class FakeInlineCompletionProvider final : public vanta::CodeCompletionProvider {
public:
    std::string id() const override {
        return "test.inline";
    }

    vanta::CodeCompletionResult complete(vanta::WorkspaceContext&, const vanta::CodeCompletionRequest& request) const override {
        if (request.mode != vanta::CodeCompletionMode::Inline) {
            return {.mode = request.mode, .ok = true};
        }
        return {
            .mode = request.mode,
            .ok = true,
            .items = {{
                .label = "return 0;",
                .insertText = "return 0;",
                .source = id(),
                .score = 1.0,
            }},
        };
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
    int restoredValue = 0;
    int savedValue = 0;
};

class TestComponent final : public vanta::Component {
public:
    TestComponent(std::string id, ComponentTestStats& stats, bool throwOnRestore = false, bool throwOnSave = false)
        : id_(std::move(id)), stats_(stats), throwOnRestore_(throwOnRestore), throwOnSave_(throwOnSave) {}

    std::string id() const override {
        return id_;
    }

    void onAttach(vanta::WorkspaceContext& context) override {
        ++stats_.attached;
        context.onEvent(*this, vanta::IdeEventKind::ProjectChanged, [this](const vanta::IdeEvent&) {
            ++stats_.events;
        });
    }

    void restoreState(const vanta::Json& state) override {
        ++stats_.restored;
        if (throwOnRestore_) {
            throw std::runtime_error("restore failed");
        }
        if (state.contains("value") && state["value"].isInt()) {
            stats_.restoredValue = static_cast<int>(state["value"].asInt());
        }
    }

    void onOpenProject(vanta::Project&) override {
        ++stats_.opened;
    }

    void onProjectChanged(vanta::Project&) override {
        ++stats_.changed;
    }

    vanta::Json saveState() const override {
        ++stats_.saved;
        if (throwOnSave_) {
            throw std::runtime_error("save failed");
        }
        return vanta::Json::object({
            {"value", vanta::Json(static_cast<std::int64_t>(stats_.savedValue))},
        });
    }

    void onCloseProject(vanta::Project&) override {
        ++stats_.closed;
    }

    void onDetach() override {
        ++stats_.detached;
    }

private:
    std::string id_;
    ComponentTestStats& stats_;
    bool throwOnRestore_ = false;
    bool throwOnSave_ = false;
};

class RuntimeComponentExtension final : public vanta::CoreExtension {
public:
    explicit RuntimeComponentExtension(ComponentTestStats& stats) : stats_(stats) {}

    void activate(vanta::ExtensionContext& context) override {
        context.components().contribute({
            .id = "sample.runtime",
            .title = "Sample Runtime",
            .pluginId = context.extension().id,
            .match = {.allProjects = true},
            .factory = [this] {
                return std::make_unique<TestComponent>("sample.runtime", stats_);
            },
        });
        context.components().contribute({
            .id = "sample.cpp.only",
            .title = "Sample C++ Only",
            .pluginId = context.extension().id,
            .match = {.facets = {"cpp"}},
            .factory = [this] {
                return std::make_unique<TestComponent>("sample.cpp.only", stats_);
            },
        });
    }

private:
    ComponentTestStats& stats_;
};

std::filesystem::path makeTempRoot() {
    const auto root = std::filesystem::temp_directory_path() / "vanta-tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void writeFile(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    output << text;
}

void testJson() {
    const vanta::Json json = vanta::Json::parse(R"({"name":"Vanta","items":[1,true,null]})");
    assert(json.isObject());
    assert(json["name"].asString() == "Vanta");
    assert(json["items"].asArray().size() == 3);
    assert(json.dump().find("Vanta") != std::string::npos);
}

void testWorkspace() {
    const auto root = makeTempRoot();
    writeFile(root / "src" / "main.cpp", "int main() { return 0; }\n");

    vanta::Workspace workspace;
    vanta::VirtualFileSystem vfs;
    workspace.bindFileSystem(vfs);
    std::string error;
    assert(workspace.open(root, &error));
    assert(workspace.info().name == "vanta-tests");
    assert(workspace.readTextFile("src/main.cpp").has_value());
    assert(vanta::renderFileTree(workspace.fileTree()).find("main.cpp") != std::string::npos);
}

void testProjectManager() {
    const auto root = makeTempRoot();
    writeFile(root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\nproject(Sample)\n");
    writeFile(root / "src" / "main.cpp", "int main() { return 0; }\n");
    writeFile(root / "plugins" / "cmake" / "vanta.plugin.json", R"({
      "id": "vanta.cmake",
      "name": "CMake Support",
      "version": "0.1.0",
      "publisher": "Vanta",
      "runtime": {"kind": "core", "entry": "builtin:cmake"},
      "permissions": ["process.execute", "build.provider", "agent.tool"]
    })");
    std::filesystem::create_directories(root / "include");
    writeFile(root / "build" / "compile_commands.json", std::string(R"([
      {
        "directory": ")" + root.string() + R"(",
        "file": ")" + (root / "src" / "main.cpp").string() + R"(",
        "arguments": ["c++", "-Iinclude", "-DVANTA_TEST=1", "-c", "src/main.cpp"]
      }
    ])"));

    vanta::VirtualFileSystem vfs;
    vanta::AsyncRuntime async(1);
    vanta::WorkspaceRuntime session(vfs, async);
    std::string error;
    assert(session.open(root, &error));
    vanta::ConsoleLogger logger;
    vanta::PluginManager manager;
    vanta::CorePluginRegistry registry = vanta::createDefaultCorePluginRegistry();
    vanta::ApprovalService approvals;
    manager.scan(root / "plugins");
    manager.activateCorePlugins(registry, logger, session.context(), approvals);
    session.refreshProject();

    const vanta::ProjectModel& project = session.project().model();
    assert(project.hasFacet("cmake"));
    assert(project.hasFacet("cpp"));
    const auto* cmake = project.attachment<vanta::CMakeProjectModel>(vanta::CMakeProjectModel::attachmentId);
    const auto* cpp = project.attachment<vanta::CppCompilationDatabase>(vanta::CppCompilationDatabase::attachmentId);
    assert(cmake != nullptr);
    assert(cpp != nullptr);
    assert(cmake->cmakeListsFile.toUri() == session.workspace().file("CMakeLists.txt").toUri());
    assert(cmake->compileCommandsFile.toUri() == session.workspace().file("build/compile_commands.json").toUri());
    assert(cpp->translationUnits.size() == 1);
    assert(cpp->translationUnits[0].sourceFile.toUri() == session.workspace().file("src/main.cpp").toUri());
    assert(cmake->graph.targets.size() == 1);
    assert(cmake->graph.sourceFiles.size() == 1);
    assert(cmake->graph.includeDirectories.size() == 1);
    assert(cmake->graph.includeDirectories[0].toUri() == session.workspace().file("include").toUri());
    assert(cmake->graph.defines.size() == 1);
    assert(cmake->graph.defines[0] == "VANTA_TEST=1");
    manager.deactivateAll();
    session.close();
}

void testWorkspaceRuntimeEvents() {
    const auto root = makeTempRoot();
    writeFile(root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\nproject(Sample)\n");
    writeFile(root / "main.cpp", "int main() { return 0; }\n");
    writeFile(root / "plugins" / "cmake" / "vanta.plugin.json", R"({
      "id": "vanta.cmake",
      "name": "CMake Support",
      "version": "0.1.0",
      "publisher": "Vanta",
      "runtime": {"kind": "core", "entry": "builtin:cmake"},
      "permissions": ["process.execute", "build.provider", "agent.tool"]
    })");

    vanta::VirtualFileSystem vfs;
    vanta::AsyncRuntime async(1);
    vanta::WorkspaceRuntime session(vfs, async);

    std::vector<vanta::IdeEventKind> events;
    session.onEvent([&](const vanta::IdeEvent& event) {
        events.push_back(event.kind);
    });

    std::string error;
    assert(session.open(root, &error));
    vanta::ConsoleLogger logger;
    vanta::PluginManager manager;
    vanta::CorePluginRegistry registry = vanta::createDefaultCorePluginRegistry();
    vanta::ApprovalService approvals;
    manager.scan(root / "plugins");
    manager.activateCorePlugins(registry, logger, session.context(), approvals);
    session.refreshProject();

    const vanta::VirtualFile mainFile = session.workspace().file("main.cpp");
    assert(session.documents().openDocument(mainFile, &error) != nullptr);

    vanta::Diagnostic diagnostic;
    diagnostic.location.file = mainFile;
    diagnostic.source = "test";
    diagnostic.message = "sample";
    session.diagnostics().publish("test", {diagnostic});

    const vanta::JobId job = session.jobs().start(vanta::JobKind::Build, "build");
    session.jobs().complete(job, true);
    session.ui().refresh();
    assert(session.ui().state().workspaceOpen);
    assert(session.ui().state().project.hasFacet("cmake"));
    assert(session.ui().state().problems.size() == 1);
    assert(std::any_of(session.ui().state().jobs.begin(), session.ui().state().jobs.end(), [&](const vanta::JobRecord& record) {
        return record.id == job && record.status == vanta::JobStatus::Succeeded;
    }));
    manager.deactivateAll();
    session.close();
    assert(!session.ui().state().workspaceOpen);

    assert(std::find(events.begin(), events.end(), vanta::IdeEventKind::WorkspaceOpened) != events.end());
    assert(std::find(events.begin(), events.end(), vanta::IdeEventKind::ProjectChanged) != events.end());
    assert(std::find(events.begin(), events.end(), vanta::IdeEventKind::DocumentOpened) != events.end());
    assert(std::find(events.begin(), events.end(), vanta::IdeEventKind::DiagnosticsChanged) != events.end());
    assert(std::find(events.begin(), events.end(), vanta::IdeEventKind::JobStarted) != events.end());
    assert(std::find(events.begin(), events.end(), vanta::IdeEventKind::JobCompleted) != events.end());
    assert(std::find(events.begin(), events.end(), vanta::IdeEventKind::WorkspaceClosed) != events.end());
}

void testRunConfigurationsAndSingleFileProject() {
    const auto root = makeTempRoot();
    writeFile(root / "script.py", "print('ok')\n");
    writeFile(root / "plugins" / "languages" / "vanta.plugin.json", R"({
      "id": "vanta.languages",
      "name": "Core Languages",
      "version": "0.1.0",
      "publisher": "Vanta",
      "runtime": {"kind": "core", "entry": "builtin:languages"},
      "permissions": ["language.service"]
    })");

    vanta::VirtualFileSystem vfs;
    vanta::AsyncRuntime async(1);
    vanta::WorkspaceRuntime session(vfs, async);
    std::string error;
    assert(session.open(root / "script.py", &error));
    vanta::ConsoleLogger logger;
    vanta::PluginManager manager;
    vanta::CorePluginRegistry registry = vanta::createDefaultCorePluginRegistry();
    vanta::ApprovalService approvals;
    manager.scan(root / "plugins");
    manager.activateCorePlugins(registry, logger, session.context(), approvals);
    session.refreshProject();

    assert(session.project().model().origin == vanta::ProjectOrigin::SingleFile);
    const auto* singleFile = session.project().model().attachment<vanta::SingleFileModel>(vanta::SingleFileModel::attachmentId);
    assert(singleFile != nullptr);
    assert(singleFile->languageId == "python");

    const auto targets = session.execution().targets(session.context());
    assert(targets.size() == 1);
    assert(targets.front().id == "local.default");
    assert(targets.front().executorId == "vanta.localExecutor");

    const auto produced = session.runConfigurations().produce(session.context(), {});
    assert(produced.size() == 1);
    assert(produced.front().typeId == "python.script");

    session.runConfigurations().addConfiguration({
        .id = "custom.echo",
        .name = "Echo",
        .typeId = "custom.command",
        .targetId = "local.default",
        .data = vanta::Json::object({
            {"executable", vanta::Json("/bin/echo")},
            {"arguments", vanta::Json::array({vanta::Json("ok")})},
            {"workingDirectory", vanta::Json(root.string())},
        }),
        .temporary = true,
    });
    const vanta::RunResult result = session.runConfigurations().run(session.context(), "custom.echo");
    assert(result.exitCode == 0);
    assert(result.output.find("ok") != std::string::npos);
    assert(result.jobId != 0);
    manager.deactivateAll();
    session.close();
}

void testLanguageRequestPipeline() {
    const auto root = makeTempRoot();
    writeFile(root / "main.cpp", "int main() { return 0; }\n");

    vanta::Workspace workspace;
    vanta::VirtualFileSystem vfs;
    workspace.bindFileSystem(vfs);
    std::string error;
    assert(workspace.open(root, &error));

    vanta::DocumentService documents;
    const vanta::VirtualFile mainFile = workspace.file("main.cpp");
    vanta::TextDocument* document = documents.openDocument(mainFile, &error);
    assert(document != nullptr);

    vanta::DefaultLanguageRegistry languages;
    auto service = std::make_unique<FakeLanguageService>();
    languages.addLanguage(fakeCppLanguage(service.get()));
    vanta::LanguageRequestPipeline pipeline;

    vanta::LanguageRequest request;
    request.kind = vanta::LanguageRequestKind::Completion;
    request.document.file = mainFile;
    request.document.languageId = "cpp";
    request.documentVersion = document->version;
    request.position = {.line = 0, .character = 4};

    const auto result = pipeline.execute(request, documents, languages);
    assert(result.ok);
    assert(!result.stale);
    assert(result.requestId == 1);

    assert(documents.setText(mainFile, "int main() { return 1; }\n", document->version, &error));
    const auto stale = pipeline.execute(request, documents, languages);
    assert(!stale.ok);
    assert(stale.stale);
}

void testLanguageRegistryAtomicResolution() {
    const auto root = makeTempRoot();
    writeFile(root / "Main.java", "class Main {}\n");

    vanta::Workspace workspace;
    vanta::VirtualFileSystem vfs;
    workspace.bindFileSystem(vfs);
    std::string error;
    assert(workspace.open(root, &error));

    const vanta::VirtualFile mainFile = workspace.file("Main.java");
    vanta::DefaultLanguageRegistry languages;
    languages.addLanguage({
        .id = "java",
        .definition = {
            .displayName = "Java Base",
        },
        .association = {
            .extensions = {".java"},
        },
        .priority = 0,
    });

    auto service = std::make_unique<FakeLanguageService>();
    vanta::RegistrationHandle registration = languages.registerLanguage({
        .id = "java",
        .definition = {
            .displayName = "Java Plugin",
        },
        .association = {
            .extensions = {".java"},
        },
        .service = service.get(),
        .priority = 10,
    });

    const vanta::Language* selected = languages.languageForFile(mainFile);
    assert(selected != nullptr);
    assert(selected->definition.displayName == "Java Plugin");
    assert(languages.serviceForDocument(mainFile) == service.get());
    registration.unregister();

    selected = languages.languageForFile(mainFile);
    assert(selected != nullptr);
    assert(selected->definition.displayName == "Java Base");
    assert(languages.serviceForDocument(mainFile) == nullptr);
}

void testLanguageRegistryProjectContextResolution() {
    const auto root = makeTempRoot();
    writeFile(root / "Main.java", "class Main {}\n");

    vanta::Workspace workspace;
    vanta::VirtualFileSystem vfs;
    workspace.bindFileSystem(vfs);
    std::string error;
    assert(workspace.open(root, &error));

    const vanta::VirtualFile mainFile = workspace.file("Main.java");
    vanta::DefaultLanguageRegistry languages;
    languages.addLanguage({
        .id = "java",
        .definition = {
            .displayName = "Java Base",
        },
        .association = {
            .extensions = {".java"},
        },
    });
    languages.addLanguage({
        .id = "java",
        .definition = {
            .displayName = "Android Java",
        },
        .association = {
            .extensions = {".java"},
        },
        .selector = {
            .projectFacets = {"android"},
        },
    });

    const vanta::Language* selected = languages.languageForFile(mainFile);
    assert(selected != nullptr);
    assert(selected->definition.displayName == "Java Base");

    vanta::ProjectModel model;
    model.facets.push_back({
        .id = "android",
        .type = "android",
        .title = "Android",
    });
    selected = languages.languageForFile(mainFile, {.project = &model});
    assert(selected != nullptr);
    assert(selected->definition.displayName == "Android Java");
}

void testCodeIntelligenceService() {
    const auto root = makeTempRoot();
    writeFile(root / "main.cpp", "int main() { return 0; }\n");

    vanta::VirtualFileSystem vfs;
    vanta::AsyncRuntime async(1);
    vanta::WorkspaceRuntime session(vfs, async);
    std::string error;
    assert(session.open(root, &error));
    const vanta::VirtualFile mainFile = session.workspace().file("main.cpp");
    vanta::TextDocument* document = session.documents().openDocument(mainFile, &error);
    assert(document != nullptr);
    auto service = std::make_unique<FakeLanguageService>();
    session.languages().addLanguage(fakeCppLanguage(service.get()));

    vanta::CodeIntelligenceRequest request;
    request.kind = vanta::CodeIntelligenceKind::InlineCompletion;
    request.document.file = mainFile;
    request.document.languageId = "cpp";
    request.documentVersion = document->version;
    request.position = {.line = 0, .character = 4};
    request.intent = "complete current expression";

    const vanta::CodeIntelligenceResult result = session.codeIntelligence().query(session.context(), request);
    assert(result.ok);
    assert(result.language.kind == vanta::LanguageRequestKind::Completion);

    vanta::CodeCompletionRequest completionRequest;
    completionRequest.mode = vanta::CodeCompletionMode::Inline;
    completionRequest.document = request.document;
    completionRequest.documentVersion = request.documentVersion;
    completionRequest.position = request.position;
    auto registration = session.codeIntelligence().registerCompletionProvider(std::make_unique<FakeInlineCompletionProvider>());
    assert(registration.registered());
    const vanta::CodeCompletionResult completion = session.codeIntelligence().complete(session.context(), completionRequest);
    assert(completion.ok);
    assert(!completion.items.empty());
    assert(completion.items.back().source == "test.inline");
    registration.unregister();
    assert(session.codeIntelligence().completionProviderIds().empty());
    session.close();
}

void testSearchService() {
    const auto root = makeTempRoot();
    writeFile(root / "src" / "main.cpp", "int main() {\n  return 0;\n}\n");
    writeFile(root / "include" / "main.hpp", "int answer();\n");

    vanta::Workspace workspace;
    vanta::VirtualFileSystem vfs;
    workspace.bindFileSystem(vfs);
    std::string error;
    assert(workspace.open(root, &error));

    vanta::SearchService search;
    search.rebuild(workspace);
    assert(!search.entries().empty());
    assert(!search.searchFiles("main").empty());
    const auto textHits = search.searchText("return 0");
    assert(textHits.size() == 1);
    assert(textHits[0].file.toUri() == workspace.file("src/main.cpp").toUri());
}

void testWorkspacePlatformServices() {
    const auto root = makeTempRoot();
    writeFile(root / "main.cpp", "int main() { return 0; }\n");

    vanta::VirtualFileSystem vfs;
    vanta::AsyncRuntime async(1);
    vanta::WorkspaceRuntime session(vfs, async);
    std::string error;
    assert(session.open(root, &error));

    assert(session.initialization().completed(vanta::WorkspaceInitializationStage::WorkspaceOpened));
    assert(session.initialization().completed(vanta::WorkspaceInitializationStage::FileIndexReady));
    assert(session.initialization().completed(vanta::WorkspaceInitializationStage::AgentContextReady));
    assert(session.capabilities().available("workspace.open"));
    assert(session.capabilities().capability("index.workspace").has_value());
    assert(!session.jobs().jobs().empty());

    const auto snapshots = session.indexes().snapshots();
    assert(!snapshots.empty());
    const auto searchSnapshot = session.indexes().snapshot("vanta.index.search");
    assert(searchSnapshot.has_value());
    assert(searchSnapshot->status == vanta::IndexStatus::Ready);
    assert(searchSnapshot->itemCount > 0);

    const auto categories = session.projectTemplates().categories();
    assert(std::any_of(categories.begin(), categories.end(), [](const vanta::ProjectTemplateCategory& category) {
        return category.id == "cpp";
    }));
    assert(std::any_of(categories.begin(), categories.end(), [](const vanta::ProjectTemplateCategory& category) {
        return category.id == "android";
    }));

    const auto created = session.projectTemplates().createProject("cpp.console.cmake", root / "generated");
    assert(created.ok);
    assert(std::filesystem::exists(root / "generated" / "CMakeLists.txt"));

    const auto scratch = session.scratchFiles().createScratchFile(session.context(), {
        .languageId = "python",
        .fileName = "note.py",
        .contents = "print('scratch')\n",
    });
    assert(scratch.ok);
    assert(scratch.file.exists());
    assert(scratch.file.readText()->find("scratch") != std::string::npos);
    assert(session.context().service<vanta::JobService>().jobs().size() == session.jobs().jobs().size());
    session.close();
}

void testLayoutAndCommandPalette() {
    const auto root = makeTempRoot();
    writeFile(root / "main.cpp", "int main() { return 0; }\n");

    vanta::VirtualFileSystem vfs;
    vanta::AsyncRuntime async(1);
    vanta::WorkspaceRuntime session(vfs, async);
    std::string error;
    assert(session.open(root, &error));

    const vanta::VirtualFile mainFile = session.workspace().file("main.cpp");
    session.editor().openFile(mainFile);
    session.ui().refresh();
    auto* layout = session.project().getComponent<vanta::LayoutStateStore>(vanta::LayoutStateStore::componentId);
    assert(layout != nullptr);
    layout->capture(session.ui().state());
    layout->rememberBuildTarget("vanta_tests");
    assert(layout->state().openTabs.size() == 1);
    assert(layout->state().lastBuildTarget == "vanta_tests");

    session.commands().add("sample.run", [](const vanta::Json&) {
        return vanta::Json::object();
    });
    session.contributions().add({
        .kind = vanta::ContributionKind::Command,
        .id = "sample.run",
        .title = "Sample: Run",
        .pluginId = "sample.plugin",
    });
    session.keybindings().bind({.commandId = "sample.run", .key = "Cmd+R", .when = "workspaceOpen"});

    const auto items = session.commandPalette().items(session.commands(), session.contributions(), session.keybindings());
    const auto filtered = session.commandPalette().filter(items, "sample");
    assert(filtered.size() == 1);
    assert(filtered[0].keybinding == "Cmd+R");
    session.close();
}

void testProjectComponentStatePersistence() {
    const auto root = makeTempRoot();
    writeFile(root / "main.cpp", "int main() { return 0; }\n");

    {
        vanta::VirtualFileSystem vfs;
        vanta::AsyncRuntime async(1);
        vanta::WorkspaceRuntime session(vfs, async);
        std::string error;
        assert(session.open(root, &error));
        auto* layout = session.project().getComponent<vanta::LayoutStateStore>(vanta::LayoutStateStore::componentId);
        assert(layout != nullptr);
        session.editor().openFile(session.workspace().file("main.cpp"));
        session.ui().refresh();
        layout->capture(session.ui().state());
        layout->rememberBuildTarget("persisted-target");
        session.close();
    }

    {
        vanta::VirtualFileSystem vfs;
        vanta::AsyncRuntime async(1);
        vanta::WorkspaceRuntime session(vfs, async);
        std::string error;
        assert(session.open(root, &error));
        const auto* layout = session.project().getComponent<vanta::LayoutStateStore>(vanta::LayoutStateStore::componentId);
        assert(layout != nullptr);
        assert(layout->state().openTabs.size() == 1);
        assert(layout->state().lastBuildTarget == "persisted-target");
        session.close();
    }
}

void testComponentLifecycleAndEventCleanup() {
    const auto root = makeTempRoot();
    writeFile(root / "main.cpp", "int main() { return 0; }\n");

    vanta::VirtualFileSystem vfs;
    vanta::AsyncRuntime async(1);
    vanta::WorkspaceRuntime session(vfs, async);
    std::string error;
    assert(session.open(root, &error));

    ComponentTestStats stats;
    stats.savedValue = 7;
    session.bindComponent(std::make_unique<TestComponent>("sample.component", stats));
    assert(stats.attached == 1);

    session.refreshProject();
    assert(stats.opened == 1);
    const int eventsBeforePublish = stats.events;
    session.publish({.kind = vanta::IdeEventKind::ProjectChanged, .file = session.workspace().rootFile()});
    assert(stats.events == eventsBeforePublish + 1);

    assert(session.unbindComponent("sample.component"));
    assert(stats.saved == 1);
    assert(stats.closed == 1);
    assert(stats.detached == 1);
    const int eventsAfterUnbind = stats.events;
    session.publish({.kind = vanta::IdeEventKind::ProjectChanged, .file = session.workspace().rootFile()});
    assert(stats.events == eventsAfterUnbind);
    session.close();
}

void testComponentStateIsolation() {
    const auto root = makeTempRoot();
    writeFile(root / "main.cpp", "int main() { return 0; }\n");
    writeFile(root / ".vanta" / "state.json", R"({
      "schemaVersion": 1,
      "project": {
        "components": {
          "bad.state": {"value": 42},
          "unknown.state": {"kept": true}
        }
      }
    })");

    ComponentTestStats stats;
    {
        vanta::VirtualFileSystem vfs;
        vanta::AsyncRuntime async(1);
        vanta::WorkspaceRuntime session(vfs, async);
        session.project().components().bind(std::make_unique<TestComponent>("bad.state", stats, true, true));
        std::string error;
        assert(session.open(root, &error));
        assert(stats.attached == 1);
        assert(stats.restored == 1);
        session.close();
    }

    vanta::ProjectState restored;
    vanta::ProjectStateStore store;
    assert(store.load(root / ".vanta" / "state.json", &restored));
    assert(restored.componentStates.contains("bad.state"));
    assert(restored.componentStates.contains("unknown.state"));
    assert(restored.componentStates["bad.state"]["value"].asInt() == 42);
    assert(restored.componentStates["unknown.state"]["kept"].asBool());
}

void testPluginComponentRegistrationLifecycle() {
    const auto root = makeTempRoot();
    writeFile(root / "plugins" / "sample" / "vanta.plugin.json", R"({
      "id": "sample.component.plugin",
      "name": "Sample Component",
      "version": "0.1.0",
      "publisher": "Vanta",
      "runtime": {"kind": "core", "entry": "builtin:sample-component"},
      "contributes": {
        "components": [{"id": "sample.runtime", "title": "Sample Runtime"}]
      }
    })");

    vanta::VirtualFileSystem vfs;
    vanta::AsyncRuntime async(1);
    vanta::WorkspaceRuntime session(vfs, async);
    std::string error;
    assert(session.open(root, &error));

    ComponentTestStats stats;
    vanta::PluginManager manager;
    vanta::CorePluginRegistry registry;
    registry.add("builtin:sample-component", [&stats] {
        return std::make_unique<RuntimeComponentExtension>(stats);
    });

    vanta::ConsoleLogger logger;
    vanta::ApprovalService approvals;

    manager.scan(root / "plugins");
    auto lifecycle = manager.pluginLifecycle();
    assert(lifecycle.size() == 1);
    assert(lifecycle[0].state == vanta::PluginLifecycleState::Discovered);
    assert(manager.contributions().list(vanta::ContributionKind::Component).empty());
    const auto messages = manager.activateCorePlugins(
        registry,
        logger,
        session.context(),
        approvals);

    assert(messages.size() == 1);
    lifecycle = manager.pluginLifecycle();
    assert(lifecycle.size() == 1);
    assert(lifecycle[0].state == vanta::PluginLifecycleState::Active);
    assert(lifecycle[0].registrationCount == 2);
    assert(session.project().getComponent("sample.runtime") != nullptr);
    assert(session.project().getComponent("sample.cpp.only") == nullptr);
    assert(stats.attached == 1);
    session.refreshProject();
    assert(stats.opened == 1);
    assert(session.project().getComponent("sample.cpp.only") == nullptr);
    const int eventsBeforePublish = stats.events;
    session.publish({.kind = vanta::IdeEventKind::ProjectChanged, .file = session.workspace().rootFile()});
    assert(stats.events == eventsBeforePublish + 1);

    manager.deactivateAll();
    lifecycle = manager.pluginLifecycle();
    assert(lifecycle.size() == 1);
    assert(lifecycle[0].state == vanta::PluginLifecycleState::Inactive);
    assert(session.project().getComponent("sample.runtime") == nullptr);
    assert(stats.closed == 1);
    assert(stats.detached == 1);
    const int eventsAfterDeactivate = stats.events;
    session.publish({.kind = vanta::IdeEventKind::ProjectChanged, .file = session.workspace().rootFile()});
    assert(stats.events == eventsAfterDeactivate);
    session.close();
}

void testVirtualFileSystem() {
    const auto root = makeTempRoot();
    vanta::VirtualFileSystem vfs;
    const vanta::VirtualFile file = vfs.localFile(root / "src" / "main.cpp");
    vanta::VirtualFile copied = file;
    assert(copied.toUri() == file.toUri());

    std::string error;
    assert(file.writeText("int main() { return 0; }\n", &error));
    assert(file.exists());
    assert(file.readText()->find("return 0") != std::string::npos);
    assert(file.localPath().has_value());

    const auto parent = file.parent();
    assert(parent.has_value());
    assert(parent->stat().kind == vanta::VirtualFileKind::Directory);
    assert(!parent->listChildren().empty());
}

void testPluginManifest() {
    const auto root = makeTempRoot();
    const auto pluginDir = root / "plugins" / "sample";
    writeFile(pluginDir / "vanta.plugin.json", R"({
      "id": "sample.plugin",
      "name": "Sample",
      "version": "0.1.0",
      "publisher": "Vanta",
      "runtime": {"kind": "core", "entry": "builtin:sample"},
      "permissions": ["workspace.read"],
      "activationEvents": ["onWorkspaceOpened"],
      "contributes": {
        "commands": [{"id": "sample.run", "title": "Sample: Run"}],
        "views": [{"id": "sample.view", "title": "Sample View"}],
        "menus": [{"id": "sample.menu", "title": "Sample Menu"}],
        "languageServices": [{"id": "sample.cpp", "title": "Sample C++"}],
        "fileSystemProviders": [{"id": "sample.fs", "title": "Sample FS"}],
        "agentTools": [{"id": "sample.tool", "title": "Sample Tool"}],
        "agentContextProviders": [{"id": "sample.context", "title": "Sample Context"}],
        "runConfigurations": [{"id": "sample.runConfiguration", "title": "Sample Run Configuration"}],
        "diagnosticProviders": [{"id": "sample.diagnostics", "title": "Sample Diagnostics"}],
        "components": [{"id": "sample.cache", "title": "Sample Cache"}]
      }
    })");

    vanta::PluginManager manager;
    const auto manifests = manager.scan(root / "plugins");
    assert(manifests.size() == 1);
    assert(manifests[0].extension.id == "sample.plugin");
    assert(manifests[0].permissions.size() == 1);
    assert(manifests[0].activationEvents.size() == 1);
    assert(manager.contributions().list().empty());
}

void testChangeSetDiff() {
    vanta::VirtualFileSystem vfs;
    const std::string diff = vanta::createUnifiedDiff(vfs.localFile("main.cpp"), "int main() { return 0; }\n", "int main() { return 1; }\n");
    assert(diff.find("-int main() { return 0; }") != std::string::npos);
    assert(diff.find("+int main() { return 1; }") != std::string::npos);
}

void testCorePluginActivation() {
    const auto root = makeTempRoot();
    writeFile(root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\nproject(Sample)\n");
    writeFile(root / "plugins" / "cmake" / "vanta.plugin.json", R"({
      "id": "vanta.cmake",
      "name": "CMake Support",
      "version": "0.1.0",
      "publisher": "Vanta",
      "runtime": {"kind": "core", "entry": "builtin:cmake"},
      "permissions": ["process.execute", "build.provider", "agent.tool"]
    })");

    vanta::VirtualFileSystem vfs;
    vanta::AsyncRuntime async(1);
    vanta::WorkspaceRuntime session(vfs, async);
    std::string error;
    assert(session.open(root, &error));
    vanta::ConsoleLogger logger;
    vanta::PluginManager manager;
    vanta::CorePluginRegistry registry = vanta::createDefaultCorePluginRegistry({
        .workspaceRoot = root,
    });
    vanta::ApprovalService approvals;

    manager.scan(root / "plugins");
    const auto messages = manager.activateCorePlugins(
        registry,
        logger,
        session.context(),
        approvals);

    assert(messages.size() == 1);
    assert(manager.activePluginIds().size() == 1);
    assert(session.commands().execute("cmake.detect", vanta::Json::object()).has_value());
    assert(!session.build().buildProviderIds().empty());
    assert(!session.projectManager().providerIds().empty());
    session.refreshProject();
    assert(session.project().model().hasFacet("cmake"));
    const auto reloadMessages = manager.reloadCorePlugins(
        registry,
        logger,
        session.context(),
        approvals);
    assert(reloadMessages.size() == 1);
    assert(manager.activePluginIds().size() == 1);
    assert(session.commands().execute("cmake.detect", vanta::Json::object()).has_value());
    std::string unloadMessage;
    assert(!manager.unloadPlugin("vanta.cmake", &unloadMessage));
    assert(manager.activePluginIds().size() == 1);
    manager.deactivateAll();
    assert(manager.activePluginIds().empty());
    assert(!session.commands().execute("cmake.detect", vanta::Json::object()).has_value());
    assert(session.build().buildProviderIds().empty());
    assert(session.projectManager().providerIds().empty());
    session.close();
}

void testExternalPluginUnloadAndReload() {
    const auto root = makeTempRoot();
    const auto pluginDir = root / "plugins" / "external";
    writeFile(pluginDir / "vanta.plugin.json", R"({
      "id": "sample.external",
      "name": "Sample External",
      "version": "0.1.0",
      "publisher": "Vanta",
      "runtime": {"kind": "process", "entry": "host.sh"},
      "permissions": ["workspace.read"]
    })");
    writeFile(pluginDir / "host.sh", "#!/bin/sh\nprintf 'Content-Length: 1\\r\\n\\r\\n{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}'\nsleep 2\n");
    std::filesystem::permissions(pluginDir / "host.sh", std::filesystem::perms::owner_exec, std::filesystem::perm_options::add);

    vanta::VirtualFileSystem vfs;
    vanta::AsyncRuntime async(1);
    vanta::WorkspaceRuntime session(vfs, async);
    std::string error;
    assert(session.open(root, &error));
    vanta::ConsoleLogger logger;
    vanta::ApprovalService approvals;
    vanta::PluginManager manager;
    manager.scan(root / "plugins");

    const auto activateMessages = manager.activateExternalPlugins(logger, session.context(), approvals);
    assert(activateMessages.size() == 1);
    assert(manager.activePluginIds().size() == 1);
    auto lifecycle = manager.pluginLifecycle();
    assert(lifecycle.size() == 1);
    assert(lifecycle[0].state == vanta::PluginLifecycleState::Active);
    assert(lifecycle[0].unloadable);
    std::string unloadMessage;
    assert(manager.unloadPlugin("sample.external", &unloadMessage));
    assert(manager.activePluginIds().empty());
    lifecycle = manager.pluginLifecycle();
    assert(lifecycle.size() == 1);
    assert(lifecycle[0].state == vanta::PluginLifecycleState::Inactive);
    const auto reloadMessages = manager.reloadPlugin("sample.external", logger, session.context(), approvals);
    assert(!reloadMessages.empty());
    assert(manager.activePluginIds().size() == 1);
    assert(manager.unloadPlugin("sample.external", &unloadMessage));
    session.close();
}

void testCliceRegistersLanguageService() {
    const auto root = makeTempRoot();
    writeFile(root / "plugins" / "clice" / "vanta.plugin.json", R"({
      "id": "vanta.clice",
      "name": "clice Language Intelligence",
      "version": "0.1.0",
      "publisher": "Vanta",
      "runtime": {"kind": "core", "entry": "builtin:clice"},
      "permissions": ["process.execute", "language.service", "agent.tool"]
    })");

    vanta::VirtualFileSystem vfs;
    vanta::AsyncRuntime async(1);
    vanta::WorkspaceRuntime session(vfs, async);
    std::string error;
    assert(session.open(root, &error));
    vanta::ConsoleLogger logger;
    vanta::PluginManager manager;
    vanta::CorePluginRegistry registry = vanta::createDefaultCorePluginRegistry({
        .workspaceRoot = root,
    });
    vanta::ApprovalService approvals;

    manager.scan(root / "plugins");
    manager.activateCorePlugins(
        registry,
        logger,
        session.context(),
        approvals);

    assert(session.languages().serviceForLanguage("cpp") != nullptr);
    assert(session.commands().execute("clice.start", vanta::Json::object()).has_value());
    manager.deactivateAll();
    assert(session.languages().serviceForLanguage("cpp") == nullptr);
    session.close();
}

void testDiagnosticService() {
    vanta::DiagnosticService diagnostics;
    bool changed = false;
    diagnostics.onDidChangeDiagnostics([&](const vanta::DiagnosticChangeEvent& event) {
        changed = event.source == "build";
    });

    vanta::Diagnostic diagnostic;
    vanta::VirtualFileSystem vfs;
    const vanta::VirtualFile mainFile = vfs.localFile("main.cpp");
    diagnostic.location.file = mainFile;
    diagnostic.location.line = 4;
    diagnostic.location.column = 2;
    diagnostic.severity = vanta::DiagnosticSeverity::Error;
    diagnostic.message = "expected expression";
    diagnostics.publish("build", {diagnostic});

    assert(changed);
    assert(diagnostics.allDiagnostics().size() == 1);
    assert(diagnostics.diagnosticsForFile(mainFile).size() == 1);

    diagnostics.clear("build");
    assert(diagnostics.allDiagnostics().empty());
}

void testProblemMatcherResolvesWorkspaceFiles() {
    const auto root = makeTempRoot();
    writeFile(root / "src" / "main.cpp", "int main() {\n  return ;\n}\n");

    vanta::Workspace workspace;
    vanta::VirtualFileSystem vfs;
    workspace.bindFileSystem(vfs);
    std::string error;
    assert(workspace.open(root, &error));

    const vanta::ProblemMatcher matcher;
    const auto matches = matcher.matchCompilerOutput("src/main.cpp:2:10: error: expected expression\n");
    assert(matches.size() == 1);

    const vanta::DiagnosticResolver resolver;
    const auto diagnostics = resolver.resolve(matches, workspace, root / "build");
    assert(diagnostics.size() == 1);
    assert(diagnostics[0].location.file.toUri() == workspace.file("src/main.cpp").toUri());
    assert(diagnostics[0].location.line == 2);
    assert(diagnostics[0].severity == vanta::DiagnosticSeverity::Error);
}

void testDocumentService() {
    const auto root = makeTempRoot();
    writeFile(root / "main.cpp", "int main() {\n  return 0;\n}\n");

    vanta::Workspace workspace;
    vanta::VirtualFileSystem vfs;
    workspace.bindFileSystem(vfs);
    std::string error;
    assert(workspace.open(root, &error));

    vanta::DocumentService documents;
    const vanta::VirtualFile mainFile = workspace.file("main.cpp");
    vanta::TextDocument* document = documents.openDocument(mainFile, &error);
    assert(document != nullptr);
    assert(document->version == 1);

    vanta::TextEdit edit;
    edit.range.start = {.line = 1, .character = 9};
    edit.range.end = {.line = 1, .character = 10};
    edit.replacementText = "1";
    assert(documents.applyEdit(mainFile, edit, document->version, &error));
    assert(documents.document(mainFile)->dirty);
    assert(documents.document(mainFile)->version == 2);
    assert(documents.saveDocument(mainFile, &error));
    assert(workspace.readTextFile("main.cpp")->find("return 1") != std::string::npos);
}

void testDocumentOverlayRead() {
    const auto root = makeTempRoot();
    writeFile(root / "main.cpp", "int main() { return 0; }\n");

    vanta::Workspace workspace;
    vanta::VirtualFileSystem vfs;
    workspace.bindFileSystem(vfs);
    std::string error;
    assert(workspace.open(root, &error));

    vanta::DocumentService documents;
    const vanta::VirtualFile mainFile = workspace.file("main.cpp");
    vanta::TextDocument* document = documents.openDocument(mainFile, &error);
    assert(document != nullptr);
    assert(documents.setText(mainFile, "int main() { return 2; }\n", document->version, &error));

    const auto snapshot = documents.readSnapshot(mainFile);
    assert(snapshot.has_value());
    assert(snapshot->open);
    assert(snapshot->dirty);
    assert(snapshot->text.find("return 2") != std::string::npos);
    assert(mainFile.readText()->find("return 0") != std::string::npos);
}

void testDocumentLanguageSynchronizer() {
    const auto root = makeTempRoot();
    writeFile(root / "main.cpp", "int main() { return 0; }\n");

    vanta::Workspace workspace;
    vanta::VirtualFileSystem vfs;
    workspace.bindFileSystem(vfs);
    std::string error;
    assert(workspace.open(root, &error));

    vanta::DocumentService documents;
    vanta::DefaultLanguageRegistry languages;
    auto service = std::make_unique<FakeLanguageService>();
    FakeLanguageService* rawService = service.get();
    languages.addLanguage(fakeCppLanguage(rawService));

    vanta::DocumentLanguageSynchronizer sync(documents, languages);
    sync.start();

    const vanta::VirtualFile mainFile = workspace.file("main.cpp");
    vanta::TextDocument* document = documents.openDocument(mainFile, &error);
    assert(document != nullptr);
    assert(rawService->opened == 1);
    assert(documents.setText(mainFile, "int main() { return 1; }\n", document->version, &error));
    assert(rawService->changed == 1);
    assert(documents.saveDocument(mainFile, &error));
    assert(rawService->saved == 1);
    assert(documents.closeDocument(mainFile));
    assert(rawService->closed == 1);
}

void testChangeSetService() {
    const auto root = makeTempRoot();
    writeFile(root / "main.cpp", "int main() { return 0; }\n");

    vanta::Workspace workspace;
    vanta::VirtualFileSystem vfs;
    workspace.bindFileSystem(vfs);
    std::string error;
    assert(workspace.open(root, &error));

    vanta::DocumentService documents;
    const vanta::VirtualFile mainFile = workspace.file("main.cpp");
    documents.openDocument(mainFile, &error);

    vanta::ChangeSetService changes;
    const auto changeSet = changes.createFileReplacement(
        mainFile,
        "test",
        "Change return value",
        "int main() { return 0; }\n",
        "int main() { return 1; }\n",
        documents.document(mainFile)->version);
    assert(changes.approve(changeSet.id).ok);
    assert(changes.applyApproved(workspace, documents, changeSet.id, {.saveAfterApply = true}).ok);
    assert(workspace.readTextFile("main.cpp")->find("return 1") != std::string::npos);
}

void testStructuredWorkspaceEdits() {
    const auto root = makeTempRoot();
    writeFile(root / "old.cpp", "int old_value = 1;\n");
    writeFile(root / "delete.cpp", "int delete_me = 1;\n");

    vanta::Workspace workspace;
    vanta::VirtualFileSystem vfs;
    workspace.bindFileSystem(vfs);
    std::string error;
    assert(workspace.open(root, &error));

    vanta::DocumentService documents;
    vanta::ChangeSetService changes;
    const vanta::VirtualFile createdFile = workspace.file("created.cpp");
    const vanta::ChangeSet createSet = changes.createFileCreation(
        createdFile,
        "test",
        "Create file",
        "int created = 1;\n");
    assert(changes.preflight(workspace, documents, createSet.id).ok);
    assert(changes.approve(createSet.id).ok);
    assert(changes.applyApproved(workspace, documents, createSet.id, {.saveAfterApply = true}).ok);
    assert(createdFile.exists());
    assert(createdFile.readText()->find("created") != std::string::npos);

    const vanta::VirtualFile deletedFile = workspace.file("delete.cpp");
    const vanta::ChangeSet deleteSet = changes.createFileDeletion(
        deletedFile,
        "test",
        "Delete file",
        "int delete_me = 1;\n");
    assert(changes.approve(deleteSet.id).ok);
    assert(changes.applyApproved(workspace, documents, deleteSet.id).ok);
    assert(!deletedFile.exists());

    const vanta::VirtualFile oldFile = workspace.file("old.cpp");
    const vanta::VirtualFile newFile = workspace.file("new.cpp");
    const vanta::ChangeSet renameSet = changes.createFileRename(
        oldFile,
        newFile,
        "test",
        "Rename file");
    assert(renameSet.unifiedDiff.find("rename from") != std::string::npos);
    assert(changes.approve(renameSet.id).ok);
    assert(changes.applyApproved(workspace, documents, renameSet.id).ok);
    assert(!oldFile.exists());
    assert(newFile.exists());

    const vanta::ChangeSet conflictSet = changes.createFileCreation(
        newFile,
        "test",
        "Create conflicting file",
        "int conflict = 1;\n");
    const auto preflight = changes.preflight(workspace, documents, conflictSet.id);
    assert(!preflight.ok);
    assert(preflight.conflicts.size() == 1);
    assert(changes.approve(conflictSet.id).ok);
    const auto applyConflict = changes.applyApproved(workspace, documents, conflictSet.id);
    assert(!applyConflict.ok);
    assert(applyConflict.message.find("conflict") != std::string::npos);
}

void testAgentContextAndRunService() {
    const auto root = makeTempRoot();
    writeFile(root / "main.cpp", "int main() { return 0; }\n");

    vanta::VirtualFileSystem vfs;
    vanta::AsyncRuntime async(1);
    vanta::WorkspaceRuntime session(vfs, async);
    std::string error;
    assert(session.open(root, &error));
    const vanta::VirtualFile mainFile = session.workspace().file("main.cpp");
    assert(session.documents().openDocument(mainFile, &error) != nullptr);

    vanta::Diagnostic diagnostic;
    diagnostic.location.file = mainFile;
    diagnostic.source = "test";
    diagnostic.message = "sample";
    session.diagnostics().publish("test", {diagnostic});

    vanta::AgentContextRequest contextRequest;
    contextRequest.goal = "explain";
    contextRequest.focusFile = mainFile;
    const vanta::AgentContext context = session.agentContext().collect(contextRequest, session.context());
    assert(!context.items.empty());

    vanta::AgentRunRequest runRequest;
    runRequest.goal = "Change return value";
    runRequest.focusFile = mainFile;
    runRequest.targetFile = mainFile;
    runRequest.replacementText = "int main() { return 2; }\n";
    const vanta::AgentRun run = session.agentOperations().startRun(session.context(), runRequest);
    assert(run.status == vanta::AgentRunStatus::WaitingForApproval);
    assert(!run.changeSetId.empty());
    assert(session.changes().approve(run.changeSetId).ok);
    assert(session.changes().applyApproved(session.workspace(), session.documents(), run.changeSetId, {.saveAfterApply = true}).ok);
    assert(session.workspace().readTextFile("main.cpp")->find("return 2") != std::string::npos);
    session.close();
}

void testAgentOperationService() {
    const auto root = makeTempRoot();
    writeFile(root / "main.cpp", "int main() { return 0; }\n");

    vanta::VirtualFileSystem vfs;
    vanta::AsyncRuntime async(1);
    vanta::WorkspaceRuntime session(vfs, async);
    std::string error;
    assert(session.open(root, &error));
    const vanta::VirtualFile mainFile = session.workspace().file("main.cpp");

    int events = 0;
    const auto recordEvent = [&](const vanta::AgentOperationEvent& event) {
        assert(!vanta::toString(event.status).empty());
        ++events;
    };

    vanta::AgentOperationRequest readRequest;
    readRequest.id = "read-main";
    readRequest.kind = vanta::AgentOperationKind::ReadFile;
    readRequest.file = mainFile;
    vanta::AgentOperationResult read = session.agentOperations().execute(session.context(), readRequest, recordEvent);
    assert(read.ok);
    assert(read.text.find("return 0") != std::string::npos);

    vanta::AgentOperationRequest searchRequest;
    searchRequest.id = "search-main";
    searchRequest.kind = vanta::AgentOperationKind::SearchText;
    searchRequest.query = "return 0";
    vanta::AgentOperationResult search = session.agentOperations().execute(session.context(), searchRequest, recordEvent);
    assert(search.ok);
    assert(search.searchHits.size() == 1);

    vanta::AgentOperationRequest changeRequest;
    changeRequest.id = "change-main";
    changeRequest.kind = vanta::AgentOperationKind::ProposeFileReplacement;
    changeRequest.file = mainFile;
    changeRequest.title = "Change return value";
    changeRequest.replacementText = "int main() { return 3; }\n";
    vanta::AgentOperationResult change = session.agentOperations().execute(session.context(), changeRequest, recordEvent);
    assert(change.ok);
    assert(!change.changeSetId.empty());
    assert(session.changes().changeSet(change.changeSetId).has_value());
    auto changeRecord = session.agentOperationJournal().record("change-main");
    assert(changeRecord.has_value());
    assert(changeRecord->ok);
    assert(changeRecord->changeSetId == change.changeSetId);

    session.agent().addTool({
        .id = "test.echo",
        .description = "Echo input",
        .handler = [](const vanta::Json& input) {
            return input;
        },
    });
    vanta::AgentOperationRequest toolRequest;
    toolRequest.id = "tool-echo";
    toolRequest.kind = vanta::AgentOperationKind::CallTool;
    toolRequest.toolId = "test.echo";
    toolRequest.input = vanta::Json::object({{"value", vanta::Json("ok")}});
    vanta::AgentOperationResult tool = session.agentOperations().execute(session.context(), toolRequest, recordEvent);
    assert(tool.ok);
    assert(tool.data.stringValue("value").value_or("") == "ok");
    assert(session.agentOperationJournal().records().size() >= 4);
    const auto jobs = session.jobs().jobs();
    assert(std::any_of(jobs.begin(), jobs.end(), [](const vanta::JobRecord& job) {
        return job.kind == vanta::JobKind::Agent && job.status == vanta::JobStatus::Succeeded;
    }));

    session.approvals().setAutoApprove(false);
    vanta::AgentOperationRequest deniedRequest;
    deniedRequest.id = "denied-change";
    deniedRequest.kind = vanta::AgentOperationKind::ProposeFileReplacement;
    deniedRequest.file = mainFile;
    deniedRequest.replacementText = "int main() { return 4; }\n";
    const vanta::AgentOperationResult denied = session.agentOperations().execute(session.context(), deniedRequest, recordEvent);
    assert(!denied.ok);
    assert(denied.error.find("denied") != std::string::npos);
    assert(!session.approvals().history().empty());
    const auto deniedRecord = session.agentOperationJournal().record("denied-change");
    assert(deniedRecord.has_value());
    assert(!deniedRecord->ok);
    assert(events >= 8);
    session.close();
}

void testJobService() {
    vanta::JobService jobs;
    const vanta::JobId id = jobs.start(vanta::JobKind::Agent, "Agent job");
    jobs.appendOutput(id, "hello");
    jobs.complete(id, true);
    const auto job = jobs.job(id);
    assert(job.has_value());
    assert(job->status == vanta::JobStatus::Succeeded);
    assert(job->output == "hello");

    vanta::AsyncRuntime runtime(1);
    const vanta::JobHandle handle = jobs.submit(runtime, {
        .kind = vanta::JobKind::Plugin,
        .title = "Posted job",
        .cancellable = true,
    }, [](vanta::JobContext& context) {
        context.appendOutput("posted");
        context.report(0.5, "half");
        return vanta::JobResult{.success = true, .message = "done"};
    });
    assert(handle.valid());
    for (int attempt = 0; attempt < 50; ++attempt) {
        const auto posted = jobs.job(handle.id());
        if (posted && posted->status == vanta::JobStatus::Succeeded) {
            assert(posted->output.find("posted") != std::string::npos);
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(false);
}

void testProcessRealtimeCallbacks() {
    int stdoutChunks = 0;
    int stderrChunks = 0;
    const vanta::CommandResult result = vanta::runCommand({
        .executable = "/bin/sh",
        .arguments = {"-c", "printf out; printf err >&2"},
    }, {
        .onStdout = [&](const std::string& chunk) {
            if (!chunk.empty()) {
                ++stdoutChunks;
            }
        },
        .onStderr = [&](const std::string& chunk) {
            if (!chunk.empty()) {
                ++stderrChunks;
            }
        },
    });
    assert(result.exitCode == 0);
    assert(result.standardOutput == "out");
    assert(result.standardError == "err");
    assert(stdoutChunks > 0);
    assert(stderrChunks > 0);
}

void testBuildHandle() {
    const auto root = makeTempRoot();
    writeFile(root / "src" / "main.cpp", "int main() {\n  return ;\n}\n");
    vanta::VirtualFileSystem vfs;
    vanta::AsyncRuntime async(1);
    vanta::WorkspaceRuntime session(vfs, async);
    std::string error;
    assert(session.open(root, &error));

    vanta::DefaultBuildService service;
    service.addProvider(std::make_unique<FakeBuildProvider>());
    std::vector<vanta::ExecutionEvent> events;
    vanta::BuildHandle handle = service.start(session.context(), root, {
        .kind = vanta::BuildTaskKind::Build,
        .providerId = "test.build",
        .jobId = 42,
    }, [&](const vanta::ExecutionEvent& event) {
        events.push_back(event);
    });
    assert(handle.valid());
    const vanta::BuildResult result = handle.wait();
    assert(result.exitCode == 0);
    assert(result.output == "built\n");
    assert(handle.status() == vanta::BuildStatus::Succeeded);
    assert(events.size() == 3);
    assert(handle.events().size() == 3);

    session.context().addProvider(std::make_unique<FakeBuildProvider>());
    const vanta::JobId jobId = session.jobs().start(vanta::JobKind::Build, "Tracked build");
    vanta::BuildHandle tracked = session.context().start(root, {
        .kind = vanta::BuildTaskKind::Build,
        .providerId = "test.build",
        .jobId = jobId,
    });
    assert(tracked.wait().exitCode == 0);
    const auto job = session.jobs().job(jobId);
    assert(job.has_value());
    assert(job->status == vanta::JobStatus::Succeeded);
    assert(job->output == "built\n");

    session.context().addProvider(std::make_unique<DiagnosticBuildProvider>());
    const vanta::BuildResult diagnosticBuild = session.context().run(root, {
        .kind = vanta::BuildTaskKind::Build,
        .providerId = "test.diagnostics",
        .buildDirectory = root / "build",
    });
    assert(diagnosticBuild.diagnostics.size() == 1);
    assert(diagnosticBuild.diagnostics[0].location.file.toUri() == session.workspace().file("src/main.cpp").toUri());

    const vanta::BuildResult diagnosticTest = session.context().run(root, {
        .kind = vanta::BuildTaskKind::Test,
        .providerId = "test.diagnostics",
        .buildDirectory = root / "build",
    });
    assert(diagnosticTest.diagnostics.empty());
    session.close();
}

void testExecutionHandle() {
    const auto root = makeTempRoot();
    vanta::VirtualFileSystem vfs;
    vanta::AsyncRuntime async(1);
    vanta::WorkspaceRuntime session(vfs, async);
    std::string error;
    assert(session.open(root, &error));

    const auto targets = session.execution().targets(session.context());
    assert(!targets.empty());
    int events = 0;
    vanta::ExecutionHandle handle = session.execution().start(session.context(), {
        .executable = "/bin/sh",
        .arguments = {"-c", "printf ok"},
        .workingDirectory = root,
    }, targets.front(), [&](const vanta::ExecutionEvent&) {
        ++events;
    });
    assert(handle.valid());
    const vanta::ExecutionResult result = handle.wait();
    assert(result.exitCode == 0);
    assert(result.output == "ok");
    assert(events >= 2);
    assert(handle.status() == vanta::ExecutionStatus::Succeeded);

    const vanta::JobId jobId = session.jobs().start(vanta::JobKind::Run, "Tracked run");
    vanta::ExecutionHandle tracked = session.context().start({
        .executable = "/bin/sh",
        .arguments = {"-c", "printf tracked"},
        .workingDirectory = root,
        .jobId = jobId,
    }, targets.front());
    assert(tracked.wait().exitCode == 0);
    const auto job = session.jobs().job(jobId);
    assert(job.has_value());
    assert(job->status == vanta::JobStatus::Succeeded);
    assert(job->output == "tracked");

    const vanta::JobId cancelJobId = session.jobs().start(vanta::JobKind::Run, "Cancelable run");
    vanta::ExecutionHandle trackedCancel = session.context().start({
        .executable = "/bin/sh",
        .arguments = {"-c", "sleep 1; printf late"},
        .workingDirectory = root,
        .jobId = cancelJobId,
    }, targets.front());
    assert(trackedCancel.valid());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(session.jobs().requestCancel(cancelJobId));
    const vanta::ExecutionResult trackedCancelResult = trackedCancel.wait();
    assert(trackedCancelResult.exitCode != 0);
    const auto cancelledJob = session.jobs().job(cancelJobId);
    assert(cancelledJob.has_value());
    assert(cancelledJob->status == vanta::JobStatus::Cancelled);

    vanta::ExecutionHandle cancelled = session.execution().start(session.context(), {
        .executable = "/bin/sh",
        .arguments = {"-c", "sleep 1; printf late"},
        .workingDirectory = root,
    }, targets.front());
    assert(cancelled.valid());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    cancelled.cancel();
    const vanta::ExecutionResult cancelledResult = cancelled.wait();
    assert(cancelledResult.exitCode != 0);
    assert(cancelled.status() == vanta::ExecutionStatus::Cancelled);
    session.close();
}

void testPluginProtocol() {
    vanta::PluginRpcRequest request;
    request.id = 7;
    request.method = "activate";
    request.params = vanta::Json::object({{"plugin", vanta::Json("sample")}});
    const vanta::Json json = vanta::toJson(request);
    assert(json["id"].asInt() == 7);
    assert(json["method"].asString() == "activate");

    const auto response = vanta::parsePluginRpcResponse(vanta::Json::object({
        {"jsonrpc", vanta::Json("2.0")},
        {"id", vanta::Json(static_cast<std::int64_t>(7))},
        {"result", vanta::Json::object({{"ok", vanta::Json(true)}})},
    }));
    assert(response.has_value());
    assert(response->ok);

    const vanta::PluginRegistration registration{
        .kind = vanta::PluginRegistrationKind::AgentTool,
        .id = "sample.tool",
        .title = "Sample Tool",
        .metadata = vanta::Json::object(),
    };
    const auto parsed = vanta::parsePluginRegistration(vanta::toJson(registration));
    assert(parsed.has_value());
    assert(parsed->kind == vanta::PluginRegistrationKind::AgentTool);
}

void testSettingsAndResult() {
    const auto root = makeTempRoot();
    vanta::SettingsService settings;
    vanta::registerDefaultSettings(settings);
    const vanta::SettingScope ideScope{.kind = vanta::SettingScopeKind::Ide};
    const vanta::SettingScope workspaceScope{.kind = vanta::SettingScopeKind::Workspace, .qualifier = root.string()};
    const vanta::SettingScope languageScope{.kind = vanta::SettingScopeKind::Language, .qualifier = "cpp"};
    int settingEvents = 0;
    settings.onDidChangeSetting([&](const vanta::SettingChangeEvent& event) {
        if (event.id == "editor.formatOnSave") {
            ++settingEvents;
        }
    });
    assert(settings.setValue("editor.fontSize", ideScope, vanta::SettingValue::intValue(16)));
    assert(settings.setValue("editor.formatOnSave", workspaceScope, vanta::SettingValue::boolValue(true)));
    assert(settings.setValue("editor.formatOnSave", languageScope, vanta::SettingValue::boolValue(false)));
    const vanta::SettingResolution resolved = settings.resolve("editor.formatOnSave", {
        .workspaceId = root.string(),
        .languageId = "cpp",
    });
    assert(!resolved.defaulted);
    assert(resolved.source.kind == vanta::SettingScopeKind::Language);
    assert(std::get<bool>(resolved.value.data) == false);
    const auto scopes = settings.scopesFor("editor.formatOnSave", {
        .workspaceId = root.string(),
        .languageId = "cpp",
    });
    assert(scopes.size() == 3);
    assert(std::any_of(scopes.begin(), scopes.end(), [](const vanta::SettingScopeDescriptor& scope) {
        return scope.scope.kind == vanta::SettingScopeKind::Language && scope.effectiveSource;
    }));
    const auto searchResults = settings.search("model");
    assert(!searchResults.empty());
    assert(std::any_of(searchResults.begin(), searchResults.end(), [](const vanta::SettingSearchResult& result) {
        return result.settingId == "ai.agent.model";
    }));
    assert(!settings.children("ai").empty());
    assert(settings.save(workspaceScope, root / ".vanta" / "settings.json"));

    vanta::SettingsService loaded;
    vanta::registerDefaultSettings(loaded);
    assert(loaded.load(workspaceScope, root / ".vanta" / "settings.json"));
    const vanta::SettingResolution loadedValue = loaded.resolve("editor.formatOnSave", {
        .workspaceId = root.string(),
        .languageId = "python",
    });
    assert(std::get<bool>(loadedValue.value.data));
    assert(settingEvents == 2);

    vanta::PluginStorageService storage(root / ".vanta" / "plugin-storage");
    assert(storage.write("sample.plugin", "state", vanta::Json::object({{"ok", vanta::Json(true)}})));
    const auto state = storage.read("sample.plugin", "state");
    assert(state);
    assert(state.value()["ok"].asBool());

    const auto error = vanta::Result<int>::failure("sample", "failed");
    assert(!error);
    assert(error.error().code == "sample");
}

void testAsyncRuntime() {
    vanta::AsyncRuntime runtime(1);
    int value = 0;
    runtime.postWorker([&] {
        runtime.postMain([&] {
            value = 42;
        });
    });
    for (int i = 0; i < 50 && value == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        runtime.drainMain();
    }
    assert(value == 42);
}

void testRuntimeApproval() {
    vanta::ApprovalService approvals;
    approvals.setAutoApprove(false);
    const auto decision = approvals.requestApproval({
        .subject = "sample.plugin",
        .permission = vanta::Permission::WorkspaceWrite,
        .action = "write file",
        .highRisk = true,
    });
    assert(decision == vanta::ApprovalDecision::Deny);
    assert(approvals.history().size() == 1);
}

}

int main() {
    testJson();
    testVirtualFileSystem();
    testWorkspace();
    testProjectManager();
    testWorkspaceRuntimeEvents();
    testRunConfigurationsAndSingleFileProject();
    testLanguageRequestPipeline();
    testLanguageRegistryAtomicResolution();
    testLanguageRegistryProjectContextResolution();
    testCodeIntelligenceService();
    testSearchService();
    testWorkspacePlatformServices();
    testLayoutAndCommandPalette();
    testProjectComponentStatePersistence();
    testComponentLifecycleAndEventCleanup();
    testComponentStateIsolation();
    testPluginComponentRegistrationLifecycle();
    testPluginManifest();
    testChangeSetDiff();
    testCorePluginActivation();
    testExternalPluginUnloadAndReload();
    testCliceRegistersLanguageService();
    testDiagnosticService();
    testProblemMatcherResolvesWorkspaceFiles();
    testDocumentService();
    testDocumentOverlayRead();
    testDocumentLanguageSynchronizer();
    testChangeSetService();
    testStructuredWorkspaceEdits();
    testAgentContextAndRunService();
    testAgentOperationService();
    testJobService();
    testProcessRealtimeCallbacks();
    testBuildHandle();
    testExecutionHandle();
    testPluginProtocol();
    testSettingsAndResult();
    testAsyncRuntime();
    testRuntimeApproval();
    std::cout << "vanta_tests passed\n";
    return 0;
}
