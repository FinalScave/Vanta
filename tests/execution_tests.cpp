#include "test_support.h"

#include "execution/build_service_impl.h"

namespace vanta::tests {

void TestJobService() {
    vanta::JobService jobs(vanta::InlineJobDispatcher());
    const vanta::JobId id = jobs.Start(vanta::JobKind::Agent, "Agent job");
    jobs.AppendOutput(id, "hello");
    jobs.Complete(id, true);
    const auto job = jobs.Job(id);
    REQUIRE(job.has_value());
    REQUIRE(job->status == vanta::JobStatus::Succeeded);
    REQUIRE(job->output == "hello");

    const vanta::JobHandle handle = jobs.Submit({
        .kind = vanta::JobKind::Plugin,
        .title = "Posted job",
        .cancellable = true,
    }, [](vanta::JobContext& context) {
        context.AppendOutput("posted");
        context.Report(0.5, "half");
        return vanta::JobResult{.success = true, .message = "done"};
    });
    REQUIRE(handle.Valid());
    for (int attempt = 0; attempt < 50; ++attempt) {
        const auto posted = jobs.Job(handle.Id());
        if (posted && posted->status == vanta::JobStatus::Succeeded) {
            REQUIRE(posted->output.find("posted") != std::string::npos);
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(false);
}

void TestJobTermination() {
    vanta::JobService jobs(vanta::InlineJobDispatcher());
    const vanta::JobId id = jobs.Start(vanta::JobKind::Generic, "Terminable job");
    bool handler_called = false;
    jobs.SetCancelHandler(id, [&] {
        handler_called = true;
    });

    REQUIRE(jobs.Terminate(id, "stop now"));
    REQUIRE(handler_called);
    const auto record = jobs.Wait(id, std::chrono::milliseconds(1));
    REQUIRE(record.has_value());
    REQUIRE(record->status == vanta::JobStatus::Cancelled);
    REQUIRE(record->message == "stop now");
}

void TestProcessRealtimeCallbacks() {
    int stdout_chunks = 0;
    int stderr_chunks = 0;
    const vanta::CommandResult result = vanta::RunCommand({
        .executable = "/bin/sh",
        .arguments = {"-c", "printf out; printf err >&2"},
    }, {
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
    vanta::VirtualFileSystem vfs;
    vanta::WorkspaceRuntime session(vfs, vanta::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));

    vanta::internal::BuildServiceImpl service;
    service.RegisterProvider(std::make_unique<FakeBuildProvider>());
    std::vector<vanta::ExecutionEvent> events;
    vanta::BuildHandle handle = service.Start(session.Context(), {
        .kind = vanta::BuildRequestKind::Build,
        .provider_id = "test.build",
        .job_id = 42,
    }, [&](const vanta::ExecutionEvent& event) {
        events.push_back(event);
    });
    REQUIRE(handle.Valid());
    const vanta::BuildResult result = handle.Wait();
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.output == "built\n");
    REQUIRE(handle.Status() == vanta::BuildStatus::Succeeded);
    REQUIRE(events.size() == 3);
    REQUIRE(handle.EventsValue().size() == 3);

    session.Context().Build().RegisterProvider(std::make_unique<FakeBuildProvider>());
    const vanta::JobId job_id = session.Context().Jobs().Start(vanta::JobKind::Build, "Tracked build");
    vanta::BuildHandle tracked = session.Context().Build().Start(session.Context(), {
        .kind = vanta::BuildRequestKind::Build,
        .provider_id = "test.build",
        .job_id = job_id,
    });
    REQUIRE(tracked.Wait().exit_code == 0);
    const auto job = session.Context().Jobs().Job(job_id);
    REQUIRE(job.has_value());
    REQUIRE(job->status == vanta::JobStatus::Succeeded);
    REQUIRE(job->output == "built\n");

    session.Context().Build().RegisterProvider(std::make_unique<DiagnosticBuildProvider>());
    const vanta::BuildResult diagnostic_build = session.Context().Build().Run(session.Context(), {
        .kind = vanta::BuildRequestKind::Build,
        .provider_id = "test.diagnostics",
        .build_directory_override = root / "build",
    });
    REQUIRE(diagnostic_build.diagnostics.size() == 1);
    REQUIRE(diagnostic_build.diagnostics[0].location.file.ToUri() == session.Context().CurrentWorkspace().File("src/main.cpp").ToUri());

    const vanta::BuildResult diagnostic_test = session.Context().Build().Run(session.Context(), {
        .kind = vanta::BuildRequestKind::Test,
        .provider_id = "test.diagnostics",
        .build_directory_override = root / "build",
    });
    REQUIRE(diagnostic_test.diagnostics.empty());
    session.Close();
}

void TestExecutionHandle() {
    const auto root = MakeTempRoot();
    vanta::VirtualFileSystem vfs;
    vanta::WorkspaceRuntime session(vfs, vanta::InlineJobDispatcher());
    std::string error;
    REQUIRE(session.Open(root, &error));

    const auto targets = session.Context().Execution().Targets(session.Context());
    REQUIRE(!targets.empty());
    int events = 0;
    vanta::ExecutionHandle handle = session.Context().Execution().Start(session.Context(), {
        .executable = "/bin/sh",
        .arguments = {"-c", "printf ok"},
        .working_directory = root,
    }, targets.front(), [&](const vanta::ExecutionEvent&) {
        ++events;
    });
    REQUIRE(handle.Valid());
    const vanta::ExecutionResult result = handle.Wait();
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.output == "ok");
    REQUIRE(events >= 2);
    REQUIRE(handle.Status() == vanta::ExecutionStatus::Succeeded);

    const vanta::JobId job_id = session.Context().Jobs().Start(vanta::JobKind::Run, "Tracked run");
    vanta::ExecutionHandle tracked = session.Context().Execution().Start(session.Context(), {
        .executable = "/bin/sh",
        .arguments = {"-c", "printf tracked"},
        .working_directory = root,
        .job_id = job_id,
    }, targets.front(), [&](const vanta::ExecutionEvent& event) {
        vanta::ApplyExecutionEventToJob(session.Context().Jobs(), event);
    });
    REQUIRE(tracked.Wait().exit_code == 0);
    const auto job = session.Context().Jobs().Job(job_id);
    REQUIRE(job.has_value());
    REQUIRE(job->status == vanta::JobStatus::Succeeded);
    REQUIRE(job->output == "tracked");

    const vanta::JobId cancel_job_id = session.Context().Jobs().Start(vanta::JobKind::Run, "Cancelable run");
    vanta::ExecutionHandle tracked_cancel = session.Context().Execution().Start(session.Context(), {
        .executable = "/bin/sh",
        .arguments = {"-c", "sleep 1; printf late"},
        .working_directory = root,
        .job_id = cancel_job_id,
    }, targets.front(), [&](const vanta::ExecutionEvent& event) {
        vanta::ApplyExecutionEventToJob(session.Context().Jobs(), event);
    });
    REQUIRE(tracked_cancel.Valid());
    session.Context().Jobs().SetCancelHandler(cancel_job_id, [tracked_cancel]() mutable {
        tracked_cancel.Cancel();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(session.Context().Jobs().RequestCancel(cancel_job_id));
    const vanta::ExecutionResult tracked_cancel_result = tracked_cancel.Wait();
    REQUIRE(tracked_cancel_result.exit_code != 0);
    const auto cancelled_job = session.Context().Jobs().Job(cancel_job_id);
    REQUIRE(cancelled_job.has_value());
    REQUIRE(cancelled_job->status == vanta::JobStatus::Cancelled);

    vanta::ExecutionHandle cancelled = session.Context().Execution().Start(session.Context(), {
        .executable = "/bin/sh",
        .arguments = {"-c", "sleep 1; printf late"},
        .working_directory = root,
    }, targets.front());
    REQUIRE(cancelled.Valid());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    cancelled.Cancel();
    const vanta::ExecutionResult cancelled_result = cancelled.Wait();
    REQUIRE(cancelled_result.exit_code != 0);
    REQUIRE(cancelled.Status() == vanta::ExecutionStatus::Cancelled);
    session.Close();
}

}

TEST_CASE("Job service", "[execution]") {
    vanta::tests::TestJobService();
}

TEST_CASE("Job termination", "[execution]") {
    vanta::tests::TestJobTermination();
}

TEST_CASE("Process realtime callbacks", "[execution]") {
    vanta::tests::TestProcessRealtimeCallbacks();
}

TEST_CASE("Build handle", "[build][execution]") {
    vanta::tests::TestBuildHandle();
}

TEST_CASE("Execution handle", "[execution]") {
    vanta::tests::TestExecutionHandle();
}
