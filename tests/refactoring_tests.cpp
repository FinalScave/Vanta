#include "test_support.h"

#include "vanta/language/refactoring_service.h"

namespace vanta::tests {

class FakeRefactoringProvider final : public vanta::RefactoringProvider {
public:
    std::string Id() const override {
        return "test.refactor";
    }

    bool Supports(vanta::RefactoringKind kind) const override {
        return kind == vanta::RefactoringKind::RenameSymbol;
    }

    vanta::RefactoringPrepareResult Prepare(vanta::WorkspaceContext&, const vanta::RefactoringRequest& request) const override {
        return {
            .ok = true,
            .title = request.title.empty() ? "Rename symbol" : request.title,
            .affected_symbols = {{
                .id = "main",
                .name = "main",
                .kind = vanta::SymbolKind::Function,
            }},
        };
    }

    vanta::RefactoringPlan Plan(vanta::WorkspaceContext&, const vanta::RefactoringRequest& request) const override {
        vanta::WorkspaceEdit edit;
        edit.operations.push_back({
            .kind = vanta::WorkspaceEditOperationKind::EditText,
            .file = request.document.file,
            .text_edits = {{
                .range = {
                    .start = {.line = 0, .character = 4},
                    .end = {.line = 0, .character = 8},
                },
                .replacement_text = "entry",
            }},
        });
        return {
            .ok = true,
            .title = request.title.empty() ? "Rename symbol" : request.title,
            .edit = std::move(edit),
        };
    }
};

void TestRefactoringServiceCreatesChangeSet() {
    const auto root = MakeTempRoot();
    WriteFile(root / "main.cpp", "int main() { return 0; }\n");

    vanta::VirtualFileSystem vfs;
    vanta::WorkspaceRuntime session(vfs, vanta::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));
    const vanta::VirtualFile main_file = session.Context().CurrentWorkspace().File("main.cpp");
    REQUIRE(session.Context().Documents().OpenDocument(main_file, &error) != nullptr);

    auto registration = session.Context().Refactorings().RegisterProvider(std::make_unique<FakeRefactoringProvider>());
    REQUIRE(registration.Registered());
    REQUIRE(session.Context().Refactorings().ProviderIds().size() == 1);

    vanta::RefactoringRequest request;
    request.kind = vanta::RefactoringKind::RenameSymbol;
    request.document.file = main_file;
    request.document.language_id = "cpp";
    request.position = {.line = 0, .character = 4};
    request.title = "Rename main";
    request.params = vanta::RenameSymbolParams{.new_name = "entry"};

    const vanta::RefactoringPrepareResult prepared = session.Context().Refactorings().Prepare(session.Context(), request);
    REQUIRE(prepared.ok);
    REQUIRE(prepared.affected_symbols.size() == 1);

    const vanta::RefactoringPlan plan = session.Context().Refactorings().Plan(session.Context(), request);
    REQUIRE(plan.ok);
    const std::optional<vanta::ChangeSet> change_set = session.Context().Refactorings().CreateChangeSet(session.Context(), plan, "test.refactor");
    REQUIRE(change_set.has_value());
    REQUIRE(session.Context().Changes().Approve(change_set->id).ok);
    REQUIRE(session.Context().Changes().ApplyApproved(session.Context().CurrentWorkspace(), session.Context().Documents(), change_set->id).ok);

    const auto text = session.Context().Documents().ReadText(main_file);
    REQUIRE(text.has_value());
    REQUIRE(text->find("entry") != std::string::npos);
    registration.Unregister();
    REQUIRE(session.Context().Refactorings().ProviderIds().empty());
    session.Close();
}

}

TEST_CASE("Refactoring service creates change set", "[refactoring]") {
    vanta::tests::TestRefactoringServiceCreatesChangeSet();
}
