#include "test_support.h"

#include "execution/build_service_impl.h"

namespace mornox::tests {

void TestJobService() {
    mornox::JobService jobs(mornox::InlineJobDispatcher());
    const mornox::JobId id = jobs.Start(mornox::JobKind::Agent, "Agent job");
    jobs.AppendOutput(id, "hello");
    jobs.Complete(id, true);
    const auto job = jobs.Job(id);
    REQUIRE(job.has_value());
    REQUIRE(job->status == mornox::JobStatus::Succeeded);
    REQUIRE(job->output == "hello");

    const mornox::JobHandle handle = jobs.Submit({
        .kind = mornox::JobKind::Plugin,
        .title = "Posted job",
        .cancellable = true,
    }, [](mornox::JobContext& context) {
        context.AppendOutput("posted");
        context.Report(0.5, "half");
        return mornox::JobResult{.success = true, .message = "done"};
    });
    REQUIRE(handle.Valid());
    for (int attempt = 0; attempt < 50; ++attempt) {
        const auto posted = jobs.Job(handle.Id());
        if (posted && posted->status == mornox::JobStatus::Succeeded) {
            REQUIRE(posted->output.find("posted") != std::string::npos);
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(false);
}

void TestJobTermination() {
    mornox::JobService jobs(mornox::InlineJobDispatcher());
    const mornox::JobId id = jobs.Start(mornox::JobKind::Generic, "Terminable job");
    bool handler_called = false;
    jobs.SetCancelHandler(id, [&] {
        handler_called = true;
    });

    REQUIRE(jobs.Terminate(id, "stop now"));
    REQUIRE(handler_called);
    const auto record = jobs.Wait(id, std::chrono::milliseconds(1));
    REQUIRE(record.has_value());
    REQUIRE(record->status == mornox::JobStatus::Cancelled);
    REQUIRE(record->message == "stop now");
}

void TestProcessRealtimeCallbacks() {
    int stdout_chunks = 0;
    int stderr_chunks = 0;
    const mornox::CommandResult result = mornox::RunCommand(TestStdoutStderrCommand("out", "err"), {
        .on_stdout = [&](const std::string& chunk) {
            if (!chunk.empty()) {
                ++stdout_chunks;
            }
        },
        .on_stderr = [&](const std::string& chunk) {
            if (!chunk.empty()) {
                ++stderr_chunks;
            }
        },
    });
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.standard_output == "out");
    REQUIRE(result.standard_error == "err");
    REQUIRE(stdout_chunks > 0);
    REQUIRE(stderr_chunks > 0);
}

void TestBuildHandle() {
    const auto root = MakeTempRoot();
    WriteFile(root / "src" / "main.cpp", "int main() {\n  return ;\n}\n");
    mornox::VirtualFileSystem vfs;
    mornox::WorkspaceRuntime session(vfs, mornox::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));

    mornox::internal::BuildServiceImpl service;
    service.RegisterProvider(std::make_unique<FakeBuildProvider>());
    std::vector<mornox::ExecutionEvent> events;
    mornox::BuildHandle handle = service.Start(session.Context(), {
        .kind = mornox::BuildRequestKind::Build,
        .provider_id = "test.build",
        .job_id = 42,
    }, [&](const mornox::ExecutionEvent& event) {
        events.push_back(event);
    });
    REQUIRE(handle.Valid());
    const mornox::BuildResult result = handle.Wait();
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.output == "built\n");
    REQUIRE(handle.Status() == mornox::BuildStatus::Succeeded);
    REQUIRE(events.size() == 3);
    REQUIRE(handle.EventsValue().size() == 3);

    session.Context().Build().RegisterProvider(std::make_unique<FakeBuildProvider>());
    const mornox::JobId job_id = session.Context().Jobs().Start(mornox::JobKind::Build, "Tracked build");
    mornox::BuildHandle tracked = session.Context().Build().Start(session.Context(), {
        .kind = mornox::BuildRequestKind::Build,
        .provider_id = "test.build",
        .job_id = job_id,
    });
    REQUIRE(tracked.Wait().exit_code == 0);
    const auto job = session.Context().Jobs().Job(job_id);
    REQUIRE(job.has_value());
    REQUIRE(job->status == mornox::JobStatus::Succeeded);
    REQUIRE(job->output == "built\n");

    session.Context().Build().RegisterProvider(std::make_unique<DiagnosticBuildProvider>());
    const mornox::BuildResult diagnostic_build = session.Context().Build().Run(session.Context(), {
        .kind = mornox::BuildRequestKind::Build,
        .provider_id = "test.diagnostics",
        .build_directory_override = root / "build",
    });
    REQUIRE(diagnostic_build.diagnostics.size() == 1);
    REQUIRE(diagnostic_build.diagnostics[0].location.file.ToUri() == session.Context().CurrentWorkspace().File("src/main.cpp").ToUri());

    const mornox::BuildResult diagnostic_test = session.Context().Build().Run(session.Context(), {
        .kind = mornox::BuildRequestKind::Test,
        .provider_id = "test.diagnostics",
        .build_directory_override = root / "build",
    });
    REQUIRE(diagnostic_test.diagnostics.empty());
    session.Close();
}

void TestExecutionHandle() {
    const auto root = MakeTempRoot();
    mornox::VirtualFileSystem vfs;
    mornox::WorkspaceRuntime session(vfs, mornox::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));

    const auto targets = session.Context().Execution().Targets(session.Context());
    REQUIRE(!targets.empty());
    int events = 0;
    mornox::ExecutionHandle handle = session.Context().Execution().Start(session.Context(), TestExecutionRequest(TestStdoutCommand("ok", root)), targets.front(), [&](const mornox::ExecutionEvent&) {
        ++events;
    });
    REQUIRE(handle.Valid());
    const mornox::ExecutionResult result = handle.Wait();
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.output == "ok");
    REQUIRE(events >= 2);
    REQUIRE(handle.Status() == mornox::ExecutionStatus::Succeeded);

    const mornox::JobId job_id = session.Context().Jobs().Start(mornox::JobKind::Run, "Tracked run");
    mornox::ExecutionHandle tracked = session.Context().Execution().Start(session.Context(), TestExecutionRequest(TestStdoutCommand("tracked", root), job_id), targets.front(), [&](const mornox::ExecutionEvent& event) {
        mornox::ApplyExecutionEventToJob(session.Context().Jobs(), event);
    });
    REQUIRE(tracked.Wait().exit_code == 0);
    const auto job = session.Context().Jobs().Job(job_id);
    REQUIRE(job.has_value());
    REQUIRE(job->status == mornox::JobStatus::Succeeded);
    REQUIRE(job->output == "tracked");

    const mornox::JobId cancel_job_id = session.Context().Jobs().Start(mornox::JobKind::Run, "Cancelable run");
    mornox::ExecutionHandle tracked_cancel = session.Context().Execution().Start(session.Context(), TestExecutionRequest(TestDelayedStdoutCommand(std::chrono::milliseconds(1000), "late", root), cancel_job_id), targets.front(), [&](const mornox::ExecutionEvent& event) {
        mornox::ApplyExecutionEventToJob(session.Context().Jobs(), event);
    });
    REQUIRE(tracked_cancel.Valid());
    session.Context().Jobs().SetCancelHandler(cancel_job_id, [tracked_cancel]() mutable {
        tracked_cancel.Cancel();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(session.Context().Jobs().RequestCancel(cancel_job_id));
    const mornox::ExecutionResult tracked_cancel_result = tracked_cancel.Wait();
    REQUIRE(tracked_cancel_result.exit_code != 0);
    const auto cancelled_job = session.Context().Jobs().Job(cancel_job_id);
    REQUIRE(cancelled_job.has_value());
    REQUIRE(cancelled_job->status == mornox::JobStatus::Cancelled);

    mornox::ExecutionHandle cancelled = session.Context().Execution().Start(
        session.Context(),
        TestExecutionRequest(TestDelayedStdoutCommand(std::chrono::milliseconds(1000), "late", root)),
        targets.front());
    REQUIRE(cancelled.Valid());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    cancelled.Cancel();
    const mornox::ExecutionResult cancelled_result = cancelled.Wait();
    REQUIRE(cancelled_result.exit_code != 0);
    REQUIRE(cancelled.Status() == mornox::ExecutionStatus::Cancelled);
    session.Close();
}

}

TEST_CASE("Job service", "[execution]") {
    mornox::tests::TestJobService();
}

TEST_CASE("Job termination", "[execution]") {
    mornox::tests::TestJobTermination();
}

TEST_CASE("Process realtime callbacks", "[execution]") {
    mornox::tests::TestProcessRealtimeCallbacks();
}

TEST_CASE("Build handle", "[build][execution]") {
    mornox::tests::TestBuildHandle();
}

TEST_CASE("Execution handle", "[execution]") {
    mornox::tests::TestExecutionHandle();
}
