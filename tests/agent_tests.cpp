#include "test_support.h"

namespace vanta::tests {

void TestAgentRuntimeAndModelService() {
    const auto root = MakeTempRoot();
    WriteFile(root / "main.cpp", "int main() { return 0; }\n");

    vanta::VirtualFileSystem vfs;
    vanta::WorkspaceRuntime session(vfs, vanta::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));
    session.Context().Models().RegisterProvider(std::make_unique<FakeModelProvider>());

    int events = 0;
    vanta::AgentSessionRequest request;
    request.goal = "Improve main";
    request.model_id = "test-model";
    request.focus_file = session.Context().CurrentWorkspace().File("main.cpp");
    const vanta::AgentSession result = session.Context().Agents().StartSession(session.Context(), request, [&](const vanta::AgentRuntimeEvent&) {
        ++events;
    });

    REQUIRE(result.status == vanta::AgentSessionStatus::Completed);
    REQUIRE(result.model_response == "Plan generated");
    REQUIRE(events >= 3);
    REQUIRE(session.Context().Models().Model("test-model").has_value());
    REQUIRE(session.Context().Agents().Session(result.id).has_value());
    session.Close();
}

void TestAgentRuntimeUsesOperationProtocol() {
    const auto root = MakeTempRoot();
    WriteFile(root / "main.cpp", "int main() { return 0; }\n");

    vanta::VirtualFileSystem vfs;
    vanta::WorkspaceRuntime session(vfs, vanta::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));
    session.Context().Models().RegisterProvider(std::make_unique<ToolCallingModelProvider>());

    vanta::AgentSessionRequest request;
    request.goal = "Use a tool";
    request.model_id = "tool-model";
    const vanta::AgentSession result = session.Context().Agents().StartSession(session.Context(), request);

    REQUIRE(result.status == vanta::AgentSessionStatus::Completed);
    REQUIRE(result.operation_ids.size() == 1);
    const auto records = session.Context().AgentOperations().Records();
    REQUIRE(records.size() == 1);
    REQUIRE(records.front().kind == vanta::AgentOperationKind::ReadFile);
    REQUIRE(records.front().ok);
    session.Close();
}

void TestAgentRuntimeStartsSessionsAsynchronously() {
    const auto root = MakeTempRoot();
    WriteFile(root / "main.cpp", "int main() { return 0; }\n");

    vanta::AsyncRuntime async_runtime(1);
    vanta::VirtualFileSystem vfs;
    vanta::WorkspaceRuntime session(vfs, vanta::AsyncJobDispatcher(async_runtime));
    std::string error;
    REQUIRE(session.Open(root, &error));
    session.Context().Models().RegisterProvider(std::make_unique<FakeModelProvider>());

    int events = 0;
    vanta::AgentSessionRequest request;
    request.goal = "Improve main";
    request.model_id = "test-model";
    const vanta::AgentSession started = session.Context().Agents().StartSession(session.Context(), request, [&](const vanta::AgentRuntimeEvent&) {
        ++events;
    });

    REQUIRE(!started.id.empty());
    REQUIRE(started.job_id != 0);
    session.Context().Jobs().Wait(started.job_id);
    const auto completed = session.Context().Agents().Session(started.id);
    REQUIRE(completed.has_value());
    REQUIRE(completed->status == vanta::AgentSessionStatus::Completed);
    REQUIRE(completed->model_response == "Plan generated");
    REQUIRE(events >= 3);
    session.Close();
}

void TestAgentRuntimeCollaborationState() {
    const auto root = MakeTempRoot();
    WriteFile(root / "main.cpp", "int main() { return 0; }\n");

    vanta::VirtualFileSystem vfs;
    vanta::WorkspaceRuntime session(vfs, vanta::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));
    session.Context().Models().RegisterProvider(std::make_unique<FakeModelProvider>());

    vanta::AgentSessionRequest request;
    request.work_kind = vanta::AgentWorkKind::Review;
    request.goal = "Review: Check main";
    request.model_id = "test-model";
    request.participants = {{
        .id = "reviewer",
        .display_name = "Reviewer",
        .model_id = "test-model",
        .role = "review",
    }};
    request.ownership_scopes = {{
        .kind = vanta::OwnershipScopeKind::File,
        .file = session.Context().CurrentWorkspace().File("main.cpp"),
        .description = "Review main.cpp",
    }};

    const vanta::AgentSession started = session.Context().Agents().StartSession(session.Context(), request);
    REQUIRE(started.status == vanta::AgentSessionStatus::Completed);
    REQUIRE(started.request.work_kind == vanta::AgentWorkKind::Review);
    REQUIRE(started.request.goal == "Review: Check main");
    REQUIRE(started.request.participants.size() == 1);
    REQUIRE(started.request.ownership_scopes.size() == 1);

    const std::string finding_id = session.Context().Agents().AddFinding({
        .session_id = started.id,
        .category = vanta::AgentWorkKind::Review,
        .severity = vanta::AgentFindingSeverity::Warning,
        .title = "Return value",
        .message = "The return value should be checked.",
        .file = session.Context().CurrentWorkspace().File("main.cpp"),
    });
    REQUIRE(!finding_id.empty());

    const std::string proposal_id = session.Context().Agents().AddProposal({
        .session_id = started.id,
        .title = "Update return value",
        .summary = "Change the return value.",
        .finding_ids = {finding_id},
    });
    REQUIRE(!proposal_id.empty());

    const auto snapshot = session.Context().Agents().Session(started.id);
    REQUIRE(snapshot.has_value());
    REQUIRE(snapshot->finding_ids.size() == 1);
    REQUIRE(snapshot->proposal_ids.size() == 1);
    REQUIRE(session.Context().Agents().FindingsForSession(started.id).size() == 1);
    REQUIRE(session.Context().Agents().ProposalsForSession(started.id).size() == 1);
    REQUIRE(vanta::ToString(vanta::AgentWorkKind::Security) == "security");

    session.Close();
}

void TestAgentContextAndOperationService() {
    const auto root = MakeTempRoot();
    WriteFile(root / "main.cpp", "int main() { return 0; }\n");

    vanta::VirtualFileSystem vfs;
    vanta::WorkspaceRuntime session(vfs, vanta::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));
    const vanta::VirtualFile main_file = session.Context().CurrentWorkspace().File("main.cpp");
    REQUIRE(session.Context().Documents().OpenDocument(main_file, &error) != nullptr);

    vanta::Diagnostic diagnostic;
    diagnostic.location.file = main_file;
    diagnostic.source = "test";
    diagnostic.message = "sample";
    session.Context().Diagnostics().Publish("test", {diagnostic});

    vanta::AgentContextRequest context_request;
    context_request.goal = "explain";
    context_request.focus_file = main_file;
    const vanta::AgentContext context = session.Context().AgentContext().Collect(context_request, session.Context());
    REQUIRE(!context.items.empty());

    const auto snapshot = session.Context().Documents().ReadSnapshot(main_file);
    vanta::AgentOperationRequest operation;
    operation.kind = vanta::AgentOperationKind::ProposeFileReplacement;
    operation.file = main_file;
    operation.source = "agent";
    operation.title = "Change return value";
    operation.replacement_text = "int main() { return 2; }\n";
    operation.expected_document_version = snapshot ? snapshot->version : 0;
    const vanta::AgentOperationResult result = session.Context().AgentOperations().Execute(session.Context(), operation);
    REQUIRE(result.ok);
    REQUIRE(!result.change_set_id.empty());
    REQUIRE(session.Context().Changes().Approve(result.change_set_id).ok);
    REQUIRE(session.Context().Changes().ApplyApproved(session.Context().CurrentWorkspace(), session.Context().Documents(), result.change_set_id, {.save_after_apply = true}).ok);
    REQUIRE(session.Context().CurrentWorkspace().ReadTextFile("main.cpp")->find("return 2") != std::string::npos);
    session.Close();
}

void TestAgentOperationService() {
    const auto root = MakeTempRoot();
    WriteFile(root / "main.cpp", "int main() { return 0; }\n");

    vanta::VirtualFileSystem vfs;
    vanta::WorkspaceRuntime session(vfs, vanta::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));
    const vanta::VirtualFile main_file = session.Context().CurrentWorkspace().File("main.cpp");

    int events = 0;
    const auto record_event = [&](const vanta::AgentOperationEvent& event) {
        REQUIRE(!vanta::ToString(event.status).empty());
        ++events;
    };

    vanta::AgentOperationRequest read_request;
    read_request.id = "read-main";
    read_request.kind = vanta::AgentOperationKind::ReadFile;
    read_request.file = main_file;
    vanta::AgentOperationResult read = session.Context().AgentOperations().Execute(session.Context(), read_request, record_event);
    REQUIRE(read.ok);
    REQUIRE(read.text.find("return 0") != std::string::npos);

    vanta::AgentOperationRequest search_request;
    search_request.id = "search-main";
    search_request.kind = vanta::AgentOperationKind::SearchText;
    search_request.query = "return 0";
    vanta::AgentOperationResult search = session.Context().AgentOperations().Execute(session.Context(), search_request, record_event);
    REQUIRE(search.ok);
    REQUIRE(search.search_hits.size() == 1);

    vanta::AgentOperationRequest change_request;
    change_request.id = "change-main";
    change_request.kind = vanta::AgentOperationKind::ProposeFileReplacement;
    change_request.file = main_file;
    change_request.title = "Change return value";
    change_request.replacement_text = "int main() { return 3; }\n";
    vanta::AgentOperationResult change = session.Context().AgentOperations().Execute(session.Context(), change_request, record_event);
    REQUIRE(change.ok);
    REQUIRE(!change.change_set_id.empty());
    REQUIRE(session.Context().Changes().Get(change.change_set_id).has_value());
    auto change_record = session.Context().AgentOperations().Record("change-main");
    REQUIRE(change_record.has_value());
    REQUIRE(change_record->ok);
    REQUIRE(change_record->change_set_id == change.change_set_id);

    session.Context().AgentTools().RegisterTool({
        .id = "test.echo",
        .description = "Echo input",
        .handler = [](const vanta::Value& input) {
            return input;
        },
    });
    vanta::AgentOperationRequest tool_request;
    tool_request.id = "tool-echo";
    tool_request.kind = vanta::AgentOperationKind::CallTool;
    tool_request.tool_id = "test.echo";
    tool_request.input = vanta::Value(vanta::Value::ObjectValue({{"value", vanta::Value("ok")}}));
    vanta::AgentOperationResult tool = session.Context().AgentOperations().Execute(session.Context(), tool_request, record_event);
    REQUIRE(tool.ok);
    REQUIRE(tool.payload.has_value());
    REQUIRE(tool.payload->StringValue("value").value_or("") == "ok");
    REQUIRE(session.Context().AgentOperations().Records().size() >= 4);
    const auto jobs = session.Context().Jobs().Jobs();
    REQUIRE(std::any_of(jobs.begin(), jobs.end(), [](const vanta::JobRecord& job) {
        return job.kind == vanta::JobKind::Agent && job.status == vanta::JobStatus::Succeeded;
    }));

    session.Context().Approvals().SetAutoApprove(false);
    vanta::AgentOperationRequest denied_request;
    denied_request.id = "denied-change";
    denied_request.kind = vanta::AgentOperationKind::ProposeFileReplacement;
    denied_request.file = main_file;
    denied_request.replacement_text = "int main() { return 4; }\n";
    const vanta::AgentOperationResult denied = session.Context().AgentOperations().Execute(session.Context(), denied_request, record_event);
    REQUIRE(!denied.ok);
    REQUIRE(denied.error.find("denied") != std::string::npos);
    REQUIRE(!session.Context().Approvals().History().empty());
    const auto denied_record = session.Context().AgentOperations().Record("denied-change");
    REQUIRE(denied_record.has_value());
    REQUIRE(!denied_record->ok);
    REQUIRE(events >= 8);
    session.Close();
}

void TestAgentToolRegistryReplacesToolsSafely() {
    vanta::AgentToolRegistry registry;
    vanta::RegistrationHandle first = registry.RegisterTool({
        .id = "test.echo",
        .description = "First echo",
        .handler = [](const vanta::Value&) {
            return vanta::Value("first");
        },
    });
    vanta::RegistrationHandle second = registry.RegisterTool({
        .id = "test.echo",
        .description = "Second echo",
        .handler = [](const vanta::Value&) {
            return vanta::Value("second");
        },
    });

    REQUIRE(registry.Tools().size() == 1);
    REQUIRE(registry.CallTool("test.echo", vanta::Value::ObjectValue())->AsString() == "second");
    first.Unregister();
    REQUIRE(registry.CallTool("test.echo", vanta::Value::ObjectValue())->AsString() == "second");
    second.Unregister();
    REQUIRE(!registry.CallTool("test.echo", vanta::Value::ObjectValue()).has_value());
}

}

TEST_CASE("Agent runtime and model service", "[agent]") {
    vanta::tests::TestAgentRuntimeAndModelService();
}

TEST_CASE("Agent runtime uses operation protocol", "[agent]") {
    vanta::tests::TestAgentRuntimeUsesOperationProtocol();
}

TEST_CASE("Agent runtime starts sessions asynchronously", "[agent]") {
    vanta::tests::TestAgentRuntimeStartsSessionsAsynchronously();
}

TEST_CASE("Agent runtime collaboration state", "[agent]") {
    vanta::tests::TestAgentRuntimeCollaborationState();
}

TEST_CASE("Agent context and operation service", "[agent]") {
    vanta::tests::TestAgentContextAndOperationService();
}

TEST_CASE("Agent operation service", "[agent]") {
    vanta::tests::TestAgentOperationService();
}

TEST_CASE("Agent tool registry replaces tools safely", "[agent]") {
    vanta::tests::TestAgentToolRegistryReplacesToolsSafely();
}
