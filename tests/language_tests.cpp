#include "test_support.h"

#include "language/language_registry_impl.h"

namespace vanta::tests {

void TestCodeIntelligenceStaleRequest() {
    const auto root = MakeTempRoot();
    WriteFile(root / "main.cpp", "int main() { return 0; }\n");

    vanta::VirtualFileSystem vfs;
    vanta::WorkspaceRuntime session(vfs, vanta::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));

    const vanta::VirtualFile main_file = session.Context().CurrentWorkspace().File("main.cpp");
    vanta::TextDocument* document = session.Context().Documents().OpenDocument(main_file, &error);
    REQUIRE(document != nullptr);

    auto service = std::make_unique<FakeLanguageService>();
    session.Context().Languages().RegisterLanguage(FakeCppLanguage(service.get()));

    vanta::CodeIntelligenceRequest request;
    request.kind = vanta::CodeIntelligenceKind::Completion;
    request.document.file = main_file;
    request.document.language_id = "cpp";
    request.document_version = document->version;
    request.position = {.line = 0, .character = 4};

    const auto result = session.Context().CodeIntelligence().Query(session.Context(), request);
    REQUIRE(result.ok);
    REQUIRE(!result.stale);

    REQUIRE(session.Context().Documents().SetText(main_file, "int main() { return 1; }\n", document->version, &error));
    const auto stale = session.Context().CodeIntelligence().Query(session.Context(), request);
    REQUIRE(!stale.ok);
    REQUIRE(stale.stale);
    session.Close();
}

void TestLanguageRegistryAtomicResolution() {
    const auto root = MakeTempRoot();
    WriteFile(root / "Main.java", "class Main {}\n");

    vanta::Workspace workspace;
    vanta::VirtualFileSystem vfs;
    workspace.BindFileSystem(vfs);
    std::string error;
    REQUIRE(workspace.Open(root, &error));

    const vanta::VirtualFile main_file = workspace.File("Main.java");
    vanta::internal::LanguageRegistryImpl languages;
    languages.RegisterLanguage({
        .id = "java",
        .definition = {
            .display_name = "Java Base",
        },
        .association = {
            .extensions = {".java"},
        },
        .priority = 0,
    });

    auto service = std::make_unique<FakeLanguageService>();
    vanta::RegistrationHandle registration = languages.RegisterLanguage({
        .id = "java",
        .definition = {
            .display_name = "Java Plugin",
        },
        .association = {
            .extensions = {".java"},
        },
        .service = service.get(),
        .priority = 10,
    });

    const vanta::Language* selected = languages.LanguageForFile(main_file);
    REQUIRE(selected != nullptr);
    REQUIRE(selected->definition.display_name == "Java Plugin");
    REQUIRE(languages.ServiceForDocument(main_file) == service.get());
    registration.Unregister();

    selected = languages.LanguageForFile(main_file);
    REQUIRE(selected != nullptr);
    REQUIRE(selected->definition.display_name == "Java Base");
    REQUIRE(languages.ServiceForDocument(main_file) == nullptr);
}

void TestLanguageRegistryProjectContextResolution() {
    const auto root = MakeTempRoot();
    WriteFile(root / "Main.java", "class Main {}\n");

    vanta::Workspace workspace;
    vanta::VirtualFileSystem vfs;
    workspace.BindFileSystem(vfs);
    std::string error;
    REQUIRE(workspace.Open(root, &error));

    const vanta::VirtualFile main_file = workspace.File("Main.java");
    vanta::internal::LanguageRegistryImpl languages;
    languages.RegisterLanguage({
        .id = "java",
        .definition = {
            .display_name = "Java Base",
        },
        .association = {
            .extensions = {".java"},
        },
    });
    languages.RegisterLanguage({
        .id = "java",
        .definition = {
            .display_name = "Android Java",
        },
        .association = {
            .extensions = {".java"},
        },
        .selector = {
            .project_facets = {"android"},
        },
    });

    const vanta::Language* selected = languages.LanguageForFile(main_file);
    REQUIRE(selected != nullptr);
    REQUIRE(selected->definition.display_name == "Java Base");

    vanta::ProjectModel model;
    model.facets.push_back({
        .id = "android",
        .type = "android",
        .title = "Android",
    });
    selected = languages.LanguageForFile(main_file, {.project = &model});
    REQUIRE(selected != nullptr);
    REQUIRE(selected->definition.display_name == "Android Java");
}

void TestCodeIntelligenceService() {
    const auto root = MakeTempRoot();
    WriteFile(root / "main.cpp", "int main() { return 0; }\n");

    vanta::VirtualFileSystem vfs;
    vanta::WorkspaceRuntime session(vfs, vanta::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));
    const vanta::VirtualFile main_file = session.Context().CurrentWorkspace().File("main.cpp");
    vanta::TextDocument* document = session.Context().Documents().OpenDocument(main_file, &error);
    REQUIRE(document != nullptr);
    auto service = std::make_unique<FakeLanguageService>();
    session.Context().Languages().RegisterLanguage(FakeCppLanguage(service.get()));

    vanta::CodeIntelligenceRequest request;
    request.kind = vanta::CodeIntelligenceKind::InlineCompletion;
    request.document.file = main_file;
    request.document.language_id = "cpp";
    request.document_version = document->version;
    request.position = {.line = 0, .character = 4};
    request.intent = "complete current expression";

    const vanta::CodeIntelligenceResult result = session.Context().CodeIntelligence().Query(session.Context(), request);
    REQUIRE(result.ok);
    REQUIRE(std::get_if<vanta::CompletionList>(&result.payload) != nullptr);

    vanta::CodeCompletionRequest completion_request;
    completion_request.mode = vanta::CodeCompletionMode::Inline;
    completion_request.document = request.document;
    completion_request.document_version = request.document_version;
    completion_request.position = request.position;
    auto registration = session.Context().CodeIntelligence().RegisterInlineCompletionProvider(std::make_unique<FakeInlineCompletionProvider>());
    REQUIRE(registration.Registered());
    const vanta::CodeCompletionResult completion = session.Context().CodeIntelligence().Complete(session.Context(), completion_request);
    REQUIRE(completion.ok);
    REQUIRE(!completion.items.empty());
    REQUIRE(completion.items.back().source == "test.inline");
    registration.Unregister();
    REQUIRE(session.Context().CodeIntelligence().InlineCompletionProviderIds().empty());
    session.Close();
}

void TestLanguageSemanticApis() {
    const auto root = MakeTempRoot();
    WriteFile(root / "main.cpp", "int main() { return 0; }\n");

    vanta::VirtualFileSystem vfs;
    vanta::WorkspaceRuntime session(vfs, vanta::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));
    const vanta::VirtualFile main_file = session.Context().CurrentWorkspace().File("main.cpp");
    vanta::TextDocument* document = session.Context().Documents().OpenDocument(main_file, &error);
    REQUIRE(document != nullptr);
    auto service = std::make_unique<FakeLanguageService>();
    session.Context().Languages().RegisterLanguage(FakeCppLanguage(service.get()));

    vanta::TextDocumentPosition position;
    position.document.file = main_file;
    position.document.language_id = "cpp";
    position.position = {.line = 0, .character = 4};

    const vanta::ReferenceResult references = service->References({
        .position = position,
    });
    REQUIRE(references.ok);
    REQUIRE(references.references.size() == 1);

    const vanta::DocumentSymbolResult symbols = service->DocumentSymbols(position.document);
    REQUIRE(symbols.ok);
    REQUIRE(symbols.symbols.front().kind == vanta::SymbolKind::Function);

    const vanta::RenamePrepareResult rename = service->PrepareRename(position);
    REQUIRE(rename.ok);
    REQUIRE(rename.placeholder == "main");
    session.Close();
}

void TestCliceRegistersLanguageService() {
    const auto root = MakeTempRoot();
    WriteFile(root / "plugins" / "clice" / "vanta.plugin.json", R"({
      "id": "vanta.clice",
      "name": "clice Language Intelligence",
      "version": "0.1.0",
      "publisher": "Vanta",
      "runtime": {"kind": "core", "entry": "builtin:clice"}
    })");

    vanta::VirtualFileSystem vfs;
    vanta::WorkspaceRuntime session(vfs, vanta::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));
    vanta::ConsoleLogger logger;
    vanta::PluginManager manager;
    vanta::CorePluginRegistry registry = vanta::CreateDefaultCorePluginRegistry();

    manager.Scan(root / "plugins");
    manager.ActivateCorePlugins(
        registry,
        logger,
        session.Context());

    REQUIRE(session.Context().Languages().ServiceForLanguage("cpp") != nullptr);
    REQUIRE(session.Context().Commands().Execute("clice.start", vanta::Value::ObjectValue()).has_value());
    manager.DeactivateAll();
    REQUIRE(session.Context().Languages().ServiceForLanguage("cpp") == nullptr);
    session.Close();
}

}

TEST_CASE("Code intelligence stale request", "[language]") {
    vanta::tests::TestCodeIntelligenceStaleRequest();
}

TEST_CASE("Language registry atomic resolution", "[language]") {
    vanta::tests::TestLanguageRegistryAtomicResolution();
}

TEST_CASE("Language registry project context resolution", "[language]") {
    vanta::tests::TestLanguageRegistryProjectContextResolution();
}

TEST_CASE("Code intelligence service", "[language]") {
    vanta::tests::TestCodeIntelligenceService();
}

TEST_CASE("Language semantic APIs", "[language]") {
    vanta::tests::TestLanguageSemanticApis();
}

TEST_CASE("Clice registers language service", "[language][clice]") {
    vanta::tests::TestCliceRegistersLanguageService();
}
