#include "test_support.h"

namespace mornox::tests {

class FakeCodeGraphIndexProvider final : public mornox::IndexProvider {
public:
    std::string Id() const override {
        return "test.graph";
    }

    std::string Kind() const override {
        return "codeGraph";
    }

    mornox::IndexSnapshot Refresh(mornox::WorkspaceContext&, mornox::JobContext&) override {
        return {
            .id = Id(),
            .kind = Kind(),
            .status = mornox::IndexStatus::Ready,
            .item_count = 3,
        };
    }

    mornox::CodeGraphSnapshot CodeGraph(mornox::WorkspaceContext& context) const override {
        const mornox::VirtualFile file = context.CurrentWorkspace().File("src/main.cpp");
        return {
            .symbols = {MainSymbol(file)},
            .references = {MainReference(file)},
            .edges = {MainEdge(file)},
        };
    }

    mornox::SymbolQueryResult Symbols(mornox::WorkspaceContext& context, const mornox::SymbolQuery&) const override {
        return {
            .symbols = {MainSymbol(context.CurrentWorkspace().File("src/main.cpp"))},
        };
    }

    mornox::ReferenceQueryResult References(mornox::WorkspaceContext& context, const mornox::ReferenceQuery&) const override {
        return {
            .references = {MainReference(context.CurrentWorkspace().File("src/main.cpp"))},
        };
    }

    mornox::CodeGraphQueryResult GraphEdges(mornox::WorkspaceContext& context, const mornox::CodeGraphQuery&) const override {
        return {
            .edges = {MainEdge(context.CurrentWorkspace().File("src/main.cpp"))},
        };
    }

private:
    static mornox::CodeSymbol MainSymbol(const mornox::VirtualFile& file) {
        return {
            .id = "main",
            .name = "main",
            .qualified_name = "main",
            .kind = mornox::SymbolKind::Function,
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

    static mornox::SymbolReference MainReference(const mornox::VirtualFile& file) {
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
            .kind = mornox::SymbolReferenceKind::Definition,
        };
    }

    static mornox::CodeGraphEdge MainEdge(const mornox::VirtualFile& file) {
        return {
            .id = "call-main",
            .kind = mornox::CodeGraphEdgeKind::Calls,
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

    mornox::VirtualFileSystem vfs;
    mornox::WorkspaceRuntime session(vfs, mornox::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));
    WaitForJobs(session.Context(), mornox::JobKind::Index);

    const auto snapshot = session.Context().Indexes().Snapshot("mornox.index.search");
    REQUIRE(snapshot.has_value());
    REQUIRE(snapshot->item_count > 0);
    const auto file_result = session.Context().Indexes().Query(session.Context(), {
        .kind = mornox::IndexQueryKind::Files,
        .query = "main",
    });
    REQUIRE(file_result.ok);
    REQUIRE(!file_result.hits.empty());
    const auto text_result = session.Context().Indexes().Query(session.Context(), {
        .kind = mornox::IndexQueryKind::Text,
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
    WriteFile(
        root / "build" / "compile_commands.json",
        std::string("[\n")
            + "  {\n"
            + "    \"directory\": " + JsonPath(root) + ",\n"
            + "    \"file\": " + JsonPath(root / "src" / "main.cpp") + ",\n"
            + "    \"arguments\": [\"c++\", \"-Iinclude\", \"-DMORNOX_TEST=1\", \"-c\", \"src/main.cpp\"]\n"
            + "  }\n"
            + "]\n");

    mornox::VirtualFileSystem vfs;
    mornox::WorkspaceRuntime session(vfs, mornox::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));
    session.Context().Indexes().RegisterProvider(mornox::CreateCppCompilationDatabaseIndexProvider());
    const mornox::JobId job_id = session.Context().Indexes().Refresh(session.Context(), "Refresh C++ index");
    session.Context().Jobs().Wait(job_id);

    const auto snapshot = session.Context().Indexes().Snapshot("mornox.index.cpp.compilationDatabase");
    REQUIRE(snapshot.has_value());
    REQUIRE(snapshot->status == mornox::IndexStatus::Ready);
    REQUIRE(snapshot->item_count == 1);

    const auto includes = session.Context().Indexes().Query(session.Context(), {
        .kind = mornox::IndexQueryKind::Includes,
        .query = "main.cpp",
        .provider_id = "mornox.index.cpp.compilationDatabase",
    });
    REQUIRE(includes.ok);
    REQUIRE(includes.hits.size() == 1);
    REQUIRE(includes.hits[0].file.ToUri() == session.Context().CurrentWorkspace().File("include").ToUri());

    session.Close();
}

void TestIndexServiceCodeGraph() {
    const auto root = MakeTempRoot();
    WriteFile(root / "src" / "main.cpp", "int main() { return 0; }\n");

    mornox::VirtualFileSystem vfs;
    mornox::WorkspaceRuntime session(vfs, mornox::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));
    session.Context().Indexes().RegisterProvider(std::make_unique<FakeCodeGraphIndexProvider>());

    const mornox::CodeGraphSnapshot graph = session.Context().Indexes().CodeGraph(session.Context(), "test.graph");
    REQUIRE(graph.ok);
    REQUIRE(graph.symbols.size() == 1);
    REQUIRE(graph.references.size() == 1);
    REQUIRE(graph.edges.size() == 1);

    const mornox::SymbolQueryResult symbols = session.Context().Indexes().Symbols(session.Context(), {
        .query = "main",
        .provider_id = "test.graph",
    });
    REQUIRE(symbols.ok);
    REQUIRE(symbols.symbols.size() == 1);

    const mornox::ReferenceQueryResult references = session.Context().Indexes().References(session.Context(), {
        .symbol_id = "main",
        .provider_id = "test.graph",
    });
    REQUIRE(references.ok);
    REQUIRE(references.references.size() == 1);

    const mornox::CodeGraphQueryResult edges = session.Context().Indexes().GraphEdges(session.Context(), {
        .symbol_id = "main",
        .edge_kind = mornox::CodeGraphEdgeKind::Calls,
        .provider_id = "test.graph",
    });
    REQUIRE(edges.ok);
    REQUIRE(edges.edges.size() == 1);
    REQUIRE(mornox::ToString(edges.edges.front().kind) == "calls");

    const mornox::SymbolQueryResult missing = session.Context().Indexes().Symbols(session.Context(), {
        .provider_id = "missing.graph",
    });
    REQUIRE(!missing.ok);
    REQUIRE(!missing.error.empty());
    session.Close();
}

}

TEST_CASE("Index service search", "[index]") {
    mornox::tests::TestIndexServiceSearch();
}

TEST_CASE("C++ compilation database index provider", "[index][cpp]") {
    mornox::tests::TestCppCompilationDatabaseIndexProvider();
}

TEST_CASE("Index service code graph", "[index]") {
    mornox::tests::TestIndexServiceCodeGraph();
}
