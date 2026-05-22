#include "test_support.h"

namespace vanta::tests {

void TestProjectManager() {
    const auto root = MakeTempRoot();
    WriteFile(root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\nproject(Sample)\n");
    WriteFile(root / "src" / "main.cpp", "int main() { return 0; }\n");
    WriteFile(root / "plugins" / "cmake" / "vanta.plugin.json", R"({
      "id": "vanta.cmake",
      "name": "CMake Support",
      "version": "0.1.0",
      "publisher": "Vanta",
      "runtime": {"kind": "core", "entry": "builtin:cmake"}
    })");
    std::filesystem::create_directories(root / "include");
    WriteFile(root / "build" / "compile_commands.json", std::string(R"([
      {
        "directory": ")" + root.string() + R"(",
        "file": ")" + (root / "src" / "main.cpp").string() + R"(",
        "arguments": ["c++", "-Iinclude", "-DVANTA_TEST=1", "-c", "src/main.cpp"]
      }
    ])"));

    vanta::VirtualFileSystem vfs;
    vanta::WorkspaceRuntime session(vfs, vanta::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));
    vanta::ConsoleLogger logger;
    vanta::PluginManager manager;
    vanta::CorePluginRegistry registry = vanta::CreateDefaultCorePluginRegistry();
    manager.Scan(root / "plugins");
    manager.ActivateCorePlugins(registry, logger, session.Context());
    session.RefreshProject();

    const vanta::ProjectModel& project = session.Context().RequireProject().Model();
    REQUIRE(project.HasFacet("cmake"));
    REQUIRE(project.HasFacet("cpp"));
    const auto* cmake = project.Attachment<vanta::CMakeProjectModel>(vanta::CMakeProjectModel::kAttachmentId);
    const auto* cpp = project.Attachment<vanta::CppCompilationDatabase>(vanta::CppCompilationDatabase::kAttachmentId);
    REQUIRE(cmake != nullptr);
    REQUIRE(cpp != nullptr);
    const vanta::Value cmake_projection = cmake->Projection();
    const vanta::Value cpp_projection = cpp->Projection();
    REQUIRE(cmake_projection.Contains("graph"));
    REQUIRE(cpp_projection.Contains("translationUnits"));
    REQUIRE(cpp_projection["translationUnits"].IsArray());
    REQUIRE(cmake->cmake_lists_file.ToUri() == session.Context().CurrentWorkspace().File("CMakeLists.txt").ToUri());
    REQUIRE(cmake->compile_commands_file.ToUri() == session.Context().CurrentWorkspace().File("build/compile_commands.json").ToUri());
    REQUIRE(cpp->translation_units.size() == 1);
    REQUIRE(cpp->translation_units[0].source_file.ToUri() == session.Context().CurrentWorkspace().File("src/main.cpp").ToUri());
    REQUIRE(cmake->graph.targets.size() == 1);
    REQUIRE(cmake->graph.source_files.size() == 1);
    REQUIRE(cmake->graph.include_directories.size() == 1);
    REQUIRE(cmake->graph.include_directories[0].ToUri() == session.Context().CurrentWorkspace().File("include").ToUri());
    REQUIRE(cmake->graph.defines.size() == 1);
    REQUIRE(cmake->graph.defines[0] == "VANTA_TEST=1");

    auto project_views = session.Context().Projects().Views(session.Context());
    REQUIRE(std::any_of(project_views.begin(), project_views.end(), [](const vanta::ProjectView& view) {
        return view.id == "vanta.files";
    }));
    REQUIRE(std::any_of(project_views.begin(), project_views.end(), [](const vanta::ProjectView& view) {
        return view.id == "vanta.cmake";
    }));

    auto file_roots = session.Context().Projects().TopLevelNodes(session.Context(), "vanta.files");
    REQUIRE(!file_roots.empty());
    auto file_root_children = session.Context().Projects().Children(session.Context(), "vanta.files", file_roots.front());
    REQUIRE(std::any_of(file_root_children.begin(), file_root_children.end(), [](const vanta::ProjectViewNode& node) {
        return node.label == "src" && node.has_children;
    }));

    auto cmake_roots = session.Context().Projects().TopLevelNodes(session.Context(), "vanta.cmake");
    REQUIRE(std::any_of(cmake_roots.begin(), cmake_roots.end(), [](const vanta::ProjectViewNode& node) {
        return node.id == "vanta.cmake.targets";
    }));
    auto source_group = std::find_if(cmake_roots.begin(), cmake_roots.end(), [](const vanta::ProjectViewNode& node) {
        return node.id == "vanta.cmake.sources";
    });
    REQUIRE(source_group != cmake_roots.end());
    auto cmake_sources = session.Context().Projects().Children(session.Context(), "vanta.cmake", *source_group);
    REQUIRE(std::any_of(cmake_sources.begin(), cmake_sources.end(), [](const vanta::ProjectViewNode& node) {
        return node.label == "main.cpp" && node.has_file;
    }));
    manager.DeactivateAll();
    session.Close();
}

void TestRunConfigurationsAndSingleFileProject() {
    const auto root = MakeTempRoot();
    WriteFile(root / "script.py", "print('ok')\n");
    WriteFile(root / "plugins" / "languages" / "vanta.plugin.json", R"({
      "id": "vanta.languages",
      "name": "Core Languages",
      "version": "0.1.0",
      "publisher": "Vanta",
      "runtime": {"kind": "core", "entry": "builtin:languages"}
    })");
    WriteFile(root / "plugins" / "python" / "vanta.plugin.json", R"({
      "id": "vanta.python",
      "name": "Python Platform Support",
      "version": "0.1.0",
      "publisher": "Vanta",
      "runtime": {"kind": "core", "entry": "builtin:python"},
      "activationEvents": ["onLanguage:python"]
    })");

    vanta::VirtualFileSystem vfs;
    vanta::WorkspaceRuntime session(vfs, vanta::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root / "script.py", &error));
    vanta::ConsoleLogger logger;
    vanta::PluginManager manager;
    vanta::CorePluginRegistry registry = vanta::CreateDefaultCorePluginRegistry();
    manager.Scan(root / "plugins");
    manager.ActivateCorePlugins(registry, logger, session.Context());
    session.RefreshProject();

    REQUIRE(session.Context().RequireProject().Model().origin == vanta::ProjectOrigin::kSingleFile);
    const auto* single_file = session.Context().RequireProject().Model().Attachment<vanta::SingleFileModel>(vanta::SingleFileModel::kAttachmentId);
    REQUIRE(single_file != nullptr);
    REQUIRE(single_file->language_id == "python");
    REQUIRE(single_file->Projection().StringValue("languageId").value_or("") == "python");
    auto* project_runs = session.Context().RequireProject().GetComponent<vanta::ProjectRunConfigurations>(vanta::ProjectRunConfigurations::kComponentId);
    REQUIRE(project_runs != nullptr);

    const auto targets = session.Context().Execution().Targets(session.Context());
    REQUIRE(targets.size() == 1);
    REQUIRE(targets.front().id == "local.default");
    REQUIRE(targets.front().executor_id == "vanta.localExecutor");

    const auto discovered = session.Context().RunConfigurations().Discover(session.Context(), {});
    REQUIRE(discovered.size() == 1);
    REQUIRE(discovered.front().provider_id == "python.script");

    vanta::RunConfiguration configuration =
        session.Context().RunConfigurations().Create(session.Context(), "custom.command", {}, "Echo");
    configuration.id = "custom.echo";
    configuration.temporary = true;
    auto* provider = session.Context().RunConfigurations().Provider("custom.command");
    REQUIRE(provider != nullptr);
    const auto fields = provider->Fields(session.Context(), configuration);
    REQUIRE(std::any_of(fields.begin(), fields.end(), [](const vanta::RunConfigurationField& field) {
        return field.id == "executable" && field.kind == "string" && field.default_value.IsString();
    }));
    REQUIRE(provider->SetFieldValue(*configuration.data, "executable", vanta::Value("/bin/echo")));
    REQUIRE(provider->SetFieldValue(*configuration.data, "arguments", vanta::Value::ArrayValue({vanta::Value("ok")})));
    REQUIRE(provider->SetFieldValue(*configuration.data, "workingDirectory", vanta::Value(root.string())));
    REQUIRE(provider->GetFieldValue(*configuration.data, "executable").AsString() == "/bin/echo");
    project_runs->AddConfiguration(std::move(configuration));
    REQUIRE(project_runs->Configuration("custom.echo").has_value());
    const vanta::RunResult result = session.Context().RunConfigurations().RunSaved(session.Context(), "custom.echo");
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.output.find("ok") != std::string::npos);
    REQUIRE(result.job_id != 0);
    manager.DeactivateAll();
    session.Close();
}

void TestProjectComponentStatePersistence() {
    const auto root = MakeTempRoot();
    WriteFile(root / "main.cpp", "int main() { return 0; }\n");

    {
        vanta::VirtualFileSystem vfs;
        vanta::WorkspaceRuntime session(vfs, vanta::InlineJobDispatcher());
        std::string error;
        REQUIRE(session.Open(root, &error));
        vanta::UiStateStore ui(session.Context());
        auto* layout = session.Context().RequireProject().GetComponent<vanta::LayoutStateStore>(vanta::LayoutStateStore::kComponentId);
        REQUIRE(layout != nullptr);
        ui.OpenFile(session.Context().CurrentWorkspace().File("main.cpp"));
        layout->Capture(ui.State());
        layout->RememberBuildTarget("persisted-target");
        ui.Detach();
        session.Close();
    }

    {
        vanta::VirtualFileSystem vfs;
        vanta::WorkspaceRuntime session(vfs, vanta::InlineJobDispatcher());
        std::string error;
        REQUIRE(session.Open(root, &error));
        vanta::UiStateStore ui(session.Context());
        const auto* layout = session.Context().RequireProject().GetComponent<vanta::LayoutStateStore>(vanta::LayoutStateStore::kComponentId);
        REQUIRE(layout != nullptr);
        REQUIRE(layout->State().open_tabs.size() == 1);
        REQUIRE(layout->State().last_build_target == "persisted-target");
        session.Close();
    }
}

void TestComponentLifecycleAndEventCleanup() {
    const auto root = MakeTempRoot();
    WriteFile(root / "main.cpp", "int main() { return 0; }\n");

    vanta::VirtualFileSystem vfs;
    vanta::WorkspaceRuntime session(vfs, vanta::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));

    ComponentTestStats stats;
    stats.saved_value = 7;
    session.Context().Projects().BindComponent(
        session.Context().RequireProject(),
        std::make_unique<TestComponent>("sample.component", stats));
    REQUIRE(stats.attached == 1);

    session.RefreshProject();
    REQUIRE(stats.opened == 1);
    const int events_before_publish = stats.events;
    session.Context().Publish({.kind = vanta::IdeEventKind::ProjectChanged, .file = session.Context().CurrentWorkspace().RootFile()});
    REQUIRE(stats.events == events_before_publish + 1);

    REQUIRE(session.Context().Projects().UnbindComponent(session.Context().RequireProject(), "sample.component"));
    REQUIRE(stats.saved == 1);
    REQUIRE(stats.closed == 1);
    REQUIRE(stats.detached == 1);
    const int events_after_unbind = stats.events;
    session.Context().Publish({.kind = vanta::IdeEventKind::ProjectChanged, .file = session.Context().CurrentWorkspace().RootFile()});
    REQUIRE(stats.events == events_after_unbind);
    session.Close();
}

void TestComponentStateIsolation() {
    const auto root = MakeTempRoot();
    WriteFile(root / "main.cpp", "int main() { return 0; }\n");
    WriteFile(root / ".vanta" / "state.json", R"({
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
        vanta::WorkspaceRuntime session(vfs, vanta::InlineJobDispatcher());
        session.Context().Projects().BindComponent(
            session.Context().RequireProject(),
            std::make_unique<TestComponent>("bad.state", stats, true, true));
        std::string error;
        REQUIRE(session.Open(root, &error));
        REQUIRE(stats.attached == 1);
        REQUIRE(stats.restored == 1);
        session.Close();
    }

    vanta::ProjectState restored;
    vanta::ProjectStateStore store;
    REQUIRE(store.Load(root / ".vanta" / "state.json", &restored));
    REQUIRE(restored.component_states.contains("bad.state"));
    REQUIRE(restored.component_states.contains("unknown.state"));
    REQUIRE(restored.component_states["bad.state"]["value"].AsInt() == 42);
    REQUIRE(restored.component_states["unknown.state"]["kept"].AsBool());
}

}

TEST_CASE("Project manager", "[project]") {
    vanta::tests::TestProjectManager();
}

TEST_CASE("Run configurations and single-file projects", "[execution][project]") {
    vanta::tests::TestRunConfigurationsAndSingleFileProject();
}

TEST_CASE("Project component state persistence", "[project]") {
    vanta::tests::TestProjectComponentStatePersistence();
}

TEST_CASE("Component lifecycle and event cleanup", "[project]") {
    vanta::tests::TestComponentLifecycleAndEventCleanup();
}

TEST_CASE("Component state isolation", "[project]") {
    vanta::tests::TestComponentStateIsolation();
}
