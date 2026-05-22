#include "test_support.h"

namespace vanta::tests {

class FakeCodeGraphIndexProvider final : public vanta::IndexProvider {
public:
    std::string Id() const override {
        return "test.graph";
    }

    std::string Kind() const override {
        return "codeGraph";
    }

    vanta::IndexSnapshot Refresh(vanta::WorkspaceContext&, vanta::JobContext&) override {
        return {
            .id = Id(),
            .kind = Kind(),
            .status = vanta::IndexStatus::Ready,
            .item_count = 3,
        };
    }

    vanta::CodeGraphSnapshot CodeGraph(vanta::WorkspaceContext& context) const override {
        const vanta::VirtualFile file = context.CurrentWorkspace().File("src/main.cpp");
        return {
            .symbols = {MainSymbol(file)},
            .references = {MainReference(file)},
            .edges = {MainEdge(file)},
        };
    }

    vanta::SymbolQueryResult Symbols(vanta::WorkspaceContext& context, const vanta::SymbolQuery&) const override {
        return {
            .symbols = {MainSymbol(context.CurrentWorkspace().File("src/main.cpp"))},
        };
    }

    vanta::ReferenceQueryResult References(vanta::WorkspaceContext& context, const vanta::ReferenceQuery&) const override {
        return {
            .references = {MainReference(context.CurrentWorkspace().File("src/main.cpp"))},
        };
    }

    vanta::CodeGraphQueryResult GraphEdges(vanta::WorkspaceContext& context, const vanta::CodeGraphQuery&) const override {
        return {
            .edges = {MainEdge(context.CurrentWorkspace().File("src/main.cpp"))},
        };
    }

private:
    static vanta::CodeSymbol MainSymbol(const vanta::VirtualFile& file) {
        return {
            .id = "main",
            .name = "main",
            .qualified_name = "main",
            .kind = vanta::SymbolKind::Function,
            .location = {
                .file = file,
                .range = {
                    .start = {.line = 0, .character = 4},
                    .end = {.line = 0, .character = 8},
                },
            },
            .language_id = "cpp",
        };
    }

    static vanta::SymbolReference MainReference(const vanta::VirtualFile& file) {
        return {
            .symbol_id = "main",
            .name = "main",
            .location = {
                .file = file,
                .range = {
                    .start = {.line = 0, .character = 4},
                    .end = {.line = 0, .character = 8},
                },
            },
            .kind = vanta::SymbolReferenceKind::Definition,
        };
    }

    static vanta::CodeGraphEdge MainEdge(const vanta::VirtualFile& file) {
        return {
            .id = "call-main",
            .kind = vanta::CodeGraphEdgeKind::Calls,
            .from_symbol_id = "main",
            .to_symbol_id = "main",
            .location = {
                .file = file,
                .range = {
                    .start = {.line = 0, .character = 4},
                    .end = {.line = 0, .character = 8},
                },
            },
            .provider_id = "test.graph",
        };
    }
};

void TestIndexServiceSearch() {
    const auto root = MakeTempRoot();
    WriteFile(root / "src" / "main.cpp", "int main() {\n  return 0;\n}\n");
    WriteFile(root / "include" / "main.hpp", "int answer();\n");

    vanta::VirtualFileSystem vfs;
    vanta::WorkspaceRuntime session(vfs, vanta::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));
    WaitForJobs(session.Context(), vanta::JobKind::Index);

    const auto snapshot = session.Context().Indexes().Snapshot("vanta.index.search");
    REQUIRE(snapshot.has_value());
    REQUIRE(snapshot->item_count > 0);
    const auto file_result = session.Context().Indexes().Query(session.Context(), {
        .kind = vanta::IndexQueryKind::Files,
        .query = "main",
    });
    REQUIRE(file_result.ok);
    REQUIRE(!file_result.hits.empty());
    const auto text_result = session.Context().Indexes().Query(session.Context(), {
        .kind = vanta::IndexQueryKind::Text,
        .query = "return 0",
    });
    REQUIRE(text_result.ok);
    REQUIRE(text_result.hits.size() == 1);
    REQUIRE(text_result.hits[0].file.ToUri() == session.Context().CurrentWorkspace().File("src/main.cpp").ToUri());
    session.Close();
}

void TestCppCompilationDatabaseIndexProvider() {
    const auto root = MakeTempRoot();
    WriteFile(root / "src" / "main.cpp", "int main() { return 0; }\n");
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
    session.Context().Indexes().RegisterProvider(vanta::CreateCppCompilationDatabaseIndexProvider());
    const vanta::JobId job_id = session.Context().Indexes().Refresh(session.Context(), "Refresh C++ index");
    session.Context().Jobs().Wait(job_id);

    const auto snapshot = session.Context().Indexes().Snapshot("vanta.index.cpp.compilationDatabase");
    REQUIRE(snapshot.has_value());
    REQUIRE(snapshot->status == vanta::IndexStatus::Ready);
    REQUIRE(snapshot->item_count == 1);

    const auto includes = session.Context().Indexes().Query(session.Context(), {
        .kind = vanta::IndexQueryKind::Includes,
        .query = "main.cpp",
        .provider_id = "vanta.index.cpp.compilationDatabase",
    });
    REQUIRE(includes.ok);
    REQUIRE(includes.hits.size() == 1);
    REQUIRE(includes.hits[0].file.ToUri() == session.Context().CurrentWorkspace().File("include").ToUri());

    session.Close();
}

void TestIndexServiceCodeGraph() {
    const auto root = MakeTempRoot();
    WriteFile(root / "src" / "main.cpp", "int main() { return 0; }\n");

    vanta::VirtualFileSystem vfs;
    vanta::WorkspaceRuntime session(vfs, vanta::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));
    session.Context().Indexes().RegisterProvider(std::make_unique<FakeCodeGraphIndexProvider>());

    const vanta::CodeGraphSnapshot graph = session.Context().Indexes().CodeGraph(session.Context(), "test.graph");
    REQUIRE(graph.ok);
    REQUIRE(graph.symbols.size() == 1);
    REQUIRE(graph.references.size() == 1);
    REQUIRE(graph.edges.size() == 1);

    const vanta::SymbolQueryResult symbols = session.Context().Indexes().Symbols(session.Context(), {
        .query = "main",
        .provider_id = "test.graph",
    });
    REQUIRE(symbols.ok);
    REQUIRE(symbols.symbols.size() == 1);

    const vanta::ReferenceQueryResult references = session.Context().Indexes().References(session.Context(), {
        .symbol_id = "main",
        .provider_id = "test.graph",
    });
    REQUIRE(references.ok);
    REQUIRE(references.references.size() == 1);

    const vanta::CodeGraphQueryResult edges = session.Context().Indexes().GraphEdges(session.Context(), {
        .symbol_id = "main",
        .edge_kind = vanta::CodeGraphEdgeKind::Calls,
        .provider_id = "test.graph",
    });
    REQUIRE(edges.ok);
    REQUIRE(edges.edges.size() == 1);
    REQUIRE(vanta::ToString(edges.edges.front().kind) == "calls");

    const vanta::SymbolQueryResult missing = session.Context().Indexes().Symbols(session.Context(), {
        .provider_id = "missing.graph",
    });
    REQUIRE(!missing.ok);
    REQUIRE(!missing.error.empty());
    session.Close();
}

}

TEST_CASE("Index service search", "[index]") {
    vanta::tests::TestIndexServiceSearch();
}

TEST_CASE("C++ compilation database index provider", "[index][cpp]") {
    vanta::tests::TestCppCompilationDatabaseIndexProvider();
}

TEST_CASE("Index service code graph", "[index]") {
    vanta::tests::TestIndexServiceCodeGraph();
}
