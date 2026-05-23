#include "test_support.h"

#include "mornox/ide/ui_service.h"
#include "ui_service_impl.h"

namespace mornox::tests {

void TestWorkspace() {
    const auto root = MakeTempRoot();
    WriteFile(root / "src" / "main.cpp", "int main() { return 0; }\n");

    mornox::Workspace workspace;
    mornox::VirtualFileSystem vfs;
    workspace.BindFileSystem(vfs);
    std::string error;
    REQUIRE(workspace.Open(root, &error));
    REQUIRE(workspace.Info().name == "mornox-tests");
    REQUIRE(workspace.ReadTextFile("src/main.cpp").has_value());
    const auto root_children = workspace.RootFile().ListChildren();
    const auto src = std::find_if(root_children.begin(), root_children.end(), [](const mornox::VirtualFile& file) {
        return file.DisplayName() == "src";
    });
    REQUIRE(src != root_children.end());
    const auto source_children = src->ListChildren();
    REQUIRE(std::any_of(source_children.begin(), source_children.end(), [](const mornox::VirtualFile& file) {
        return file.DisplayName() == "main.cpp";
    }));
}

void TestWorkspaceRuntimeEvents() {
    const auto root = MakeTempRoot();
    WriteFile(root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\nproject(Sample)\n");
    WriteFile(root / "main.cpp", "int main() { return 0; }\n");
    WriteFile(root / "plugins" / "cmake" / "mornox.plugin.json", R"({
      "id": "mornox.cmake",
      "name": "CMake Support",
      "version": "0.1.0",
      "publisher": "Mornox",
      "runtime": {"kind": "core", "entry": "builtin:cmake"}
    })");

    mornox::VirtualFileSystem vfs;
    mornox::WorkspaceRuntime session(vfs, mornox::InlineJobDispatcher());

    std::vector<mornox::IdeEventKind> events;
    session.Context().Events().Subscribe([&](const mornox::IdeEvent& event) {
        events.push_back(event.kind);
    });

    std::string error;
    REQUIRE(session.Open(root, &error));
    mornox::ConsoleLogger logger;
    mornox::PluginManager manager;
    mornox::CorePluginRegistry registry = mornox::CreateDefaultCorePluginRegistry();
    manager.Scan(root / "plugins");
    manager.ActivateCorePlugins(registry, logger, session.Context());
    session.RefreshProject();
    mornox::UiStateStore ui(session.Context());

    const mornox::VirtualFile main_file = session.Context().CurrentWorkspace().File("main.cpp");
    REQUIRE(session.Context().Documents().OpenDocument(main_file, &error) != nullptr);

    mornox::Diagnostic diagnostic;
    diagnostic.location.file = main_file;
    diagnostic.source = "test";
    diagnostic.message = "sample";
    session.Context().Diagnostics().Publish("test", {diagnostic});

    const mornox::JobId job = session.Context().Jobs().Start(mornox::JobKind::Build, "build");
    session.Context().Jobs().Complete(job, true);
    ui.Refresh();
    REQUIRE(ui.State().workspace_open);
    REQUIRE(ui.State().project.HasFacet("cmake"));
    REQUIRE(ui.State().problems.size() == 1);
    REQUIRE(std::any_of(ui.State().jobs.begin(), ui.State().jobs.end(), [&](const mornox::JobRecord& record) {
        return record.id == job && record.status == mornox::JobStatus::Succeeded;
    }));
    manager.DeactivateAll();
    session.Close();
    REQUIRE(!ui.State().workspace_open);

    REQUIRE(std::find(events.begin(), events.end(), mornox::IdeEventKind::WorkspaceOpened) != events.end());
    REQUIRE(std::find(events.begin(), events.end(), mornox::IdeEventKind::ProjectChanged) != events.end());
    REQUIRE(std::find(events.begin(), events.end(), mornox::IdeEventKind::DocumentOpened) != events.end());
    REQUIRE(std::find(events.begin(), events.end(), mornox::IdeEventKind::DiagnosticsChanged) != events.end());
    REQUIRE(std::find(events.begin(), events.end(), mornox::IdeEventKind::JobChanged) != events.end());
    REQUIRE(std::find(events.begin(), events.end(), mornox::IdeEventKind::JobStarted) != events.end());
    REQUIRE(std::find(events.begin(), events.end(), mornox::IdeEventKind::JobCompleted) != events.end());
    REQUIRE(std::find(events.begin(), events.end(), mornox::IdeEventKind::WorkspaceClosed) != events.end());
}

void TestWorkspacePlatformServices() {
    const auto root = MakeTempRoot();
    WriteFile(root / "main.cpp", "int main() { return 0; }\n");
    WriteFile(root / "plugins" / "languages" / "mornox.plugin.json", R"({
      "id": "mornox.languages",
      "name": "Core Languages",
      "version": "0.1.0",
      "publisher": "Mornox",
      "runtime": {"kind": "core", "entry": "builtin:languages"},
      "activationEvents": ["onStartup"]
    })");
    WriteFile(root / "plugins" / "cpp" / "mornox.plugin.json", R"({
      "id": "mornox.cpp",
      "name": "C++ Platform Support",
      "version": "0.1.0",
      "publisher": "Mornox",
      "runtime": {"kind": "core", "entry": "builtin:cpp"},
      "activationEvents": ["onLanguage:cpp"]
    })");
    WriteFile(root / "plugins" / "python" / "mornox.plugin.json", R"({
      "id": "mornox.python",
      "name": "Python Platform Support",
      "version": "0.1.0",
      "publisher": "Mornox",
      "runtime": {"kind": "core", "entry": "builtin:python"},
      "activationEvents": ["onLanguage:python"]
    })");

    mornox::VirtualFileSystem vfs;
    mornox::WorkspaceRuntime session(vfs, mornox::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));
    mornox::ConsoleLogger logger;
    mornox::PluginManager manager;
    mornox::CorePluginRegistry registry = mornox::CreateDefaultCorePluginRegistry();
    manager.Scan(root / "plugins");
    manager.ActivateCorePlugins(registry, logger, session.Context());
    WaitForJobs(session.Context(), mornox::JobKind::Index);

    REQUIRE(session.Context().GetService<mornox::CommandRegistry>() == &session.Context().Commands());
    REQUIRE(session.Context().GetService<mornox::BuildService>() == &session.Context().Build());
    REQUIRE(session.Context().GetService<mornox::LanguageRegistry>() == &session.Context().Languages());
    REQUIRE(session.Context().Capabilities().Available("workspace.open"));
    REQUIRE(session.Context().Capabilities().Get("index.workspace").has_value());
    REQUIRE(session.Context().Capabilities().Get("agent.operations").has_value());
    REQUIRE(!session.Context().Jobs().Jobs().empty());

    const auto snapshots = session.Context().Indexes().Snapshots();
    REQUIRE(!snapshots.empty());
    const auto search_snapshot = session.Context().Indexes().Snapshot("mornox.index.search");
    REQUIRE(search_snapshot.has_value());
    REQUIRE(search_snapshot->status == mornox::IndexStatus::Ready);
    REQUIRE(search_snapshot->item_count > 0);

    const auto categories = session.Context().ProjectTemplates().Categories();
    REQUIRE(std::any_of(categories.begin(), categories.end(), [](const mornox::ProjectTemplateCategory& category) {
        return category.id == "cpp";
    }));
    REQUIRE(std::any_of(categories.begin(), categories.end(), [](const mornox::ProjectTemplateCategory& category) {
        return category.id == "python";
    }));
    REQUIRE(std::none_of(categories.begin(), categories.end(), [](const mornox::ProjectTemplateCategory& category) {
        return category.id == "android";
    }));

    const auto created = session.Context().ProjectTemplates().CreateProject("cpp.console.cmake", root / "generated");
    REQUIRE(created.ok);
    REQUIRE(std::filesystem::exists(root / "generated" / "CMakeLists.txt"));

    const auto scratch = session.Context().ScratchFiles().CreateScratchFile(session.Context(), {
        .language_id = "python",
        .file_name = "note.py",
        .contents = "print('scratch')\n",
    });
    REQUIRE(scratch.ok);
    REQUIRE(scratch.file.Exists());
    REQUIRE(scratch.file.ReadText()->find("scratch") != std::string::npos);
    REQUIRE(!session.Context().Jobs().Jobs().empty());
    manager.DeactivateAll();
    session.Close();
}

void TestLayoutAndCommandPalette() {
    const auto root = MakeTempRoot();
    WriteFile(root / "main.cpp", "int main() { return 0; }\n");

    mornox::VirtualFileSystem vfs;
    mornox::WorkspaceRuntime session(vfs, mornox::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));
    mornox::UiStateStore ui(session.Context());

    const mornox::VirtualFile main_file = session.Context().CurrentWorkspace().File("main.cpp");
    ui.OpenFile(main_file);
    auto* layout = session.Context().RequireProject().GetComponent<mornox::LayoutStateStore>(mornox::LayoutStateStore::kComponentId);
    REQUIRE(layout != nullptr);
    layout->Capture(ui.State());
    layout->RememberBuildTarget("mornox_tests");
    REQUIRE(layout->State().open_tabs.size() == 1);
    REQUIRE(layout->State().last_build_target == "mornox_tests");

    session.Context().Commands().RegisterCommand({
        .id = "sample.run",
        .title = "Sample: Run",
        .source = "sample.plugin",
    }, [](const mornox::Value&) {
        return mornox::Value::ObjectValue();
    });
    ui.Keybindings().Bind({
        .command_id = "sample.run",
        .first = {.modifiers = mornox::KeyModifier::Meta | mornox::KeyModifier::Shift, .key = "R"},
        .second = mornox::KeyChord{.modifiers = mornox::KeyModifierMask(mornox::KeyModifier::Ctrl), .key = "Enter"},
        .when = "workspaceOpen",
    });

    const auto items = mornox::CommandPaletteItems(session.Context().Commands(), ui.Keybindings());
    const auto filtered = mornox::FilterCommandPaletteItems(items, "sample");
    REQUIRE(filtered.size() == 1);
    REQUIRE(filtered[0].keybinding == "Shift+Cmd+R Ctrl+Enter");
    session.Close();
}

void TestUiServiceInjectionAndProviders() {
    class PanelProvider final : public mornox::UiPanelProvider {
    public:
        std::string Id() const override {
            return "sample.panels";
        }

        std::vector<mornox::UiPanelDescriptor> Panels(mornox::WorkspaceContext&) const override {
            return {
                {
                    .id = "sample.output",
                    .title = "Output",
                    .location = mornox::UiLocations::kBottomToolWindow,
                    .sort_order = 20,
                },
                {
                    .id = "sample.agent",
                    .title = "Agent",
                    .location = mornox::UiLocations::kRightToolWindow,
                    .sort_order = 10,
                },
            };
        }
    };

    class ActionProvider final : public mornox::UiActionProvider {
    public:
        std::string Id() const override {
            return "sample.actions";
        }

        std::vector<mornox::UiActionDescriptor> Actions(mornox::WorkspaceContext&) const override {
            return {{
                .id = "sample.run",
                .title = "Run",
                .location = mornox::UiLocations::kToolbar,
                .command_id = "sample.run",
            }};
        }
    };

    class SettingsProvider final : public mornox::UiSettingsPageProvider {
    public:
        std::string Id() const override {
            return "sample.settings";
        }

        std::vector<mornox::UiSettingsPageDescriptor> SettingsPages(mornox::WorkspaceContext&) const override {
            return {{
                .id = "sample.settings",
                .title = "Sample",
                .parent_id = "tools",
                .command_id = "sample.settings.open",
            }};
        }
    };

    const auto root = MakeTempRoot();
    WriteFile(root / "main.cpp", "int main() { return 0; }\n");

    mornox::VirtualFileSystem vfs;
    mornox::WorkspaceRuntime session(vfs, mornox::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));
    mornox::internal::UiServiceImpl ui_service;
    session.Context().RegisterService(mornox::UiService::kServiceId, &ui_service);
    auto* ui = session.Context().GetService<mornox::UiService>();
    REQUIRE(ui == &ui_service);

    PanelProvider panels;
    ActionProvider actions;
    SettingsProvider settings;
    auto panel_registration = ui->RegisterPanelProvider(&panels);
    auto action_registration = ui->RegisterActionProvider(&actions);
    auto settings_registration = ui->RegisterSettingsPageProvider(&settings);

    REQUIRE(panel_registration.Registered());
    REQUIRE(action_registration.Registered());
    REQUIRE(settings_registration.Registered());
    REQUIRE(ui->PanelProviderIds().size() == 1);
    REQUIRE(ui->Panels(session.Context()).size() == 2);
    REQUIRE(ui->Panels(session.Context())[0].id == "sample.agent");
    REQUIRE(ui->Actions(session.Context()).size() == 1);
    REQUIRE(ui->SettingsPages(session.Context()).size() == 1);

    panel_registration.Unregister();
    REQUIRE(ui->Panels(session.Context()).empty());
    session.Context().UnregisterService(mornox::UiService::kServiceId, &ui_service);
    session.Close();
}

}

TEST_CASE("Workspace basics", "[workspace]") {
    mornox::tests::TestWorkspace();
}

TEST_CASE("Workspace runtime events", "[workspace]") {
    mornox::tests::TestWorkspaceRuntimeEvents();
}

TEST_CASE("Workspace platform services", "[workspace]") {
    mornox::tests::TestWorkspacePlatformServices();
}

TEST_CASE("Layout and command palette", "[workspace]") {
    mornox::tests::TestLayoutAndCommandPalette();
}

TEST_CASE("UI service injection and providers", "[workspace]") {
    mornox::tests::TestUiServiceInjectionAndProviders();
}
