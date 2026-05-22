#include "execution/build_service_impl.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include "vanta/execution/problem_matcher.h"
#include "vanta/workspace/workspace_context.h"
#include "vanta/workspace/workspace_runtime.h"

namespace vanta {
namespace {

BuildStatus BuildStatusFromJob(const std::optional<JobRecord>& job) {
    if (!job) {
        return BuildStatus::Failed;
    }
    switch (job->status) {
    case JobStatus::Pending:
        return BuildStatus::Pending;
    case JobStatus::Running:
        return BuildStatus::Running;
    case JobStatus::Succeeded:
        return BuildStatus::Succeeded;
    case JobStatus::Failed:
        return BuildStatus::Failed;
    case JobStatus::Cancelled:
        return BuildStatus::Cancelled;
    }
    return BuildStatus::Failed;
}

}

struct BuildState {
    mutable std::mutex mutex;
    std::condition_variable completed;
    std::optional<BuildResult> result;
    std::vector<ExecutionEvent> events;
};

namespace {

using BuildCancellationCheck = std::function<bool()>;
using BuildOperation = std::function<BuildResult(ExecutionEventCallback, BuildCancellationCheck)>;

BuildHandle StartBuildOperation(
    JobService& jobs,
    JobId job_id,
    BuildOperation operation,
    ExecutionEventCallback on_event) {
    auto state = std::make_shared<BuildState>();
    JobHandle job_handle = jobs.Submit(job_id, [state, operation = std::move(operation), on_event = std::move(on_event)](JobContext& job) mutable {
        auto emit = [&](const ExecutionEvent& event) {
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->events.push_back(event);
            }
            if (on_event) {
                on_event(event);
            }
        };
        auto cancelled = [&] {
            return job.CancellationRequested();
        };

        BuildResult result;
        try {
            result = operation(emit, cancelled);
        } catch (const std::exception& error) {
            result = {.exit_code = -1, .output = std::string(error.what()) + "\n"};
        } catch (...) {
            result = {.exit_code = -1, .output = "Build failed\n"};
        }
        BuildStatus completed_status = BuildStatus::Failed;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (result.events.empty()) {
                result.events = state->events;
            }
            completed_status = job.CancellationRequested() ? BuildStatus::Cancelled : (result.exit_code == 0 ? BuildStatus::Succeeded : BuildStatus::Failed);
            state->result = std::move(result);
        }
        state->completed.notify_all();
        return JobResult{
            .success = completed_status == BuildStatus::Succeeded,
            .message = completed_status == BuildStatus::Cancelled ? "Build cancelled" : "",
        };
    });
    jobs.SetCancellable(job_id, true);
    return BuildHandle(job_handle, std::move(state));
}

}

BuildHandle::BuildHandle(JobHandle job, std::shared_ptr<BuildState> state)
    : job_(job), state_(std::move(state)) {}

JobId BuildHandle::JobIdValue() const {
    return job_.Id();
}

JobHandle BuildHandle::JobHandleValue() const {
    return job_;
}

BuildStatus BuildHandle::Status() const {
    if (!Valid()) {
        return BuildStatus::Failed;
    }
    return BuildStatusFromJob(job_.Record());
}

bool BuildHandle::Running() const {
    const BuildStatus current = Status();
    return current == BuildStatus::Pending || current == BuildStatus::Running;
}

void BuildHandle::Cancel() {
    job_.Cancel();
}

void BuildHandle::Terminate() {
    job_.Terminate();
}

BuildResult BuildHandle::Wait() {
    if (!Valid()) {
        return {.exit_code = -1, .output = "Build handle is not valid\n"};
    }
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->completed.wait(lock, [this] {
        return state_->result.has_value();
    });
    BuildResult result = *state_->result;
    lock.unlock();
    job_.Wait();
    return result;
}

std::optional<BuildResult> BuildHandle::ResultValue() const {
    if (state_ == nullptr) {
        return std::nullopt;
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->result;
}

std::vector<ExecutionEvent> BuildHandle::EventsValue() const {
    if (state_ == nullptr) {
        return {};
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->events;
}

bool BuildHandle::Valid() const {
    return state_ != nullptr && job_.Valid();
}

RegistrationHandle internal::BuildServiceImpl::RegisterProvider(std::unique_ptr<BuildProvider> provider) {
    if (provider == nullptr || provider->Id().empty()) {
        return {};
    }
    const std::string provider_id = provider->Id();
    providers_[provider_id] = std::move(provider);
    return RegistrationHandle([this, provider_id] {
        RemoveProvider(provider_id);
    });
}

void internal::BuildServiceImpl::RemoveProvider(const std::string& provider_id) {
    providers_.erase(provider_id);
}

std::vector<std::string> internal::BuildServiceImpl::BuildProviderIds() const {
    std::vector<std::string> ids;
    for (const auto& [id, provider] : providers_) {
        (void)provider;
        ids.push_back(id);
    }
    return ids;
}

BuildEnvironment internal::BuildServiceImpl::Detect(WorkspaceContext& context, const ProjectModel& project) const {
    for (const auto& [id, provider] : providers_) {
        (void)id;
        BuildEnvironment environment = provider->Detect(context, project);
        if (environment.detected) {
            return environment;
        }
    }
    return {};
}

BuildHandle internal::BuildServiceImpl::Start(
    WorkspaceContext& context,
    const BuildRequest& request,
    ExecutionEventCallback on_event) const {
    BuildRequest effective_request = request;
    if (effective_request.job_id == 0 || !context.Jobs().Job(effective_request.job_id)) {
        effective_request.job_id = context.Jobs().Start(effective_request.kind == BuildRequestKind::Build ? JobKind::Build : JobKind::Test, ToString(effective_request.kind));
    }
    const BuildProvider* provider = ChooseProvider(context, effective_request);
    BuildHandle handle = StartBuildOperation(context.Jobs(), effective_request.job_id, [&context, build_request = effective_request, provider](ExecutionEventCallback emit, BuildCancellationCheck cancelled) {
        std::vector<ExecutionEvent> events;
        std::string output;
        std::vector<Diagnostic> diagnostics;

        auto emit_direct = [&](ExecutionEvent event) {
            output += event.text;
            events.push_back(event);
            ApplyExecutionEventToJob(context.Jobs(), events.back());
            if (emit) {
                emit(events.back());
            }
        };

        auto fail = [&](std::string message) {
            emit_direct({
                .kind = ExecutionEventKind::Finished,
                .job_id = build_request.job_id,
                .text = message,
                .progress = 1.0,
                .exit_code = -1,
            });
            return BuildResult{.exit_code = -1, .output = output, .diagnostics = diagnostics, .events = events};
        };

        if (provider == nullptr) {
            return fail("No build provider is available for this workspace\n");
        }

        BuildPlan plan = provider->Plan(context, build_request);
        if (plan.steps.empty()) {
            return fail("Build provider did not produce any execution steps\n");
        }

        for (BuildStep& step : plan.steps) {
            if (cancelled()) {
                emit_direct({
                    .kind = ExecutionEventKind::Finished,
                    .job_id = build_request.job_id,
                    .text = "Build cancelled\n",
                    .progress = 1.0,
                    .exit_code = 130,
                });
                return BuildResult{.exit_code = 130, .output = output, .diagnostics = diagnostics, .events = events};
            }

            ExecutionRequest execution_request = step.request;
            execution_request.job_id = build_request.job_id;
            if (execution_request.working_directory.empty()) {
                execution_request.working_directory = context.CurrentWorkspace().Info().root_path;
            }

            std::vector<ExecutionTarget> targets = context.Execution().Targets(context);
            auto target = targets.begin();
            if (!build_request.execution_target_id.empty()) {
                target = std::find_if(targets.begin(), targets.end(), [&](const ExecutionTarget& value) {
                    return value.id == build_request.execution_target_id;
                });
            }
            if (target == targets.end()) {
                return fail("Execution target not found for build step\n");
            }

            ExecutionHandle handle = context.Execution().Start(context, execution_request, *target, [&](const ExecutionEvent& event) {
                events.push_back(event);
                switch (event.kind) {
                case ExecutionEventKind::Started:
                    context.Jobs().MarkRunning(build_request.job_id);
                    if (event.progress >= 0.0) {
                        context.Jobs().UpdateProgress(build_request.job_id, event.progress);
                    }
                    break;
                case ExecutionEventKind::Stdout:
                case ExecutionEventKind::Stderr:
                    context.Jobs().AppendOutput(build_request.job_id, event.text);
                    break;
                case ExecutionEventKind::Progress:
                    context.Jobs().UpdateProgress(build_request.job_id, event.progress, event.text);
                    break;
                case ExecutionEventKind::Finished:
                    if (!event.text.empty()) {
                        context.Jobs().AppendOutput(build_request.job_id, event.text);
                    }
                    if (event.progress >= 0.0) {
                        context.Jobs().UpdateProgress(build_request.job_id, event.progress);
                    }
                    break;
                }
                if (emit) {
                    emit(event);
                }
            });
            if (!handle.Valid()) {
                return fail("Build step did not start\n");
            }
            context.Jobs().SetCancelHandler(build_request.job_id, [handle]() mutable {
                handle.Terminate();
            });

            while (handle.Running()) {
                if (cancelled()) {
                    handle.Terminate();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            ExecutionResult result = handle.Wait();
            context.Jobs().SetCancelHandler(build_request.job_id, {});
            output += result.output;
            if (step.parse_diagnostics) {
                const ProblemMatcher matcher;
                const DiagnosticResolver resolver;
                const std::filesystem::path base_directory = step.diagnostic_base_directory.empty() ? execution_request.working_directory : step.diagnostic_base_directory;
                std::vector<Diagnostic> step_diagnostics = resolver.Resolve(matcher.MatchCompilerOutput(result.output), context.CurrentWorkspace(), base_directory);
                diagnostics.insert(diagnostics.end(), step_diagnostics.begin(), step_diagnostics.end());
            }
            if (result.exit_code != 0) {
                return BuildResult{.exit_code = result.exit_code, .output = output, .diagnostics = diagnostics, .events = events};
            }
        }

        return BuildResult{.exit_code = 0, .output = output, .diagnostics = diagnostics, .events = events};
    }, std::move(on_event));
    return handle;
}

BuildResult internal::BuildServiceImpl::Run(
    WorkspaceContext& context,
    const BuildRequest& request,
    ExecutionEventCallback on_event) const {
    return Start(context, request, std::move(on_event)).Wait();
}

const BuildProvider* internal::BuildServiceImpl::ChooseProvider(WorkspaceContext& context, const BuildRequest& request) const {
    if (!request.provider_id.empty()) {
        auto it = providers_.find(request.provider_id);
        return it == providers_.end() ? nullptr : it->second.get();
    }

    for (const auto& [id, provider] : providers_) {
        (void)id;
        BuildEnvironment environment = provider->Detect(context, context.RequireProject().Model());
        if (environment.detected) {
            return provider.get();
        }
    }
    return nullptr;
}

std::string ToString(BuildRequestKind kind) {
    return kind == BuildRequestKind::Build ? "build" : "test";
}

std::string ToString(BuildStatus status) {
    switch (status) {
    case BuildStatus::Pending:
        return "pending";
    case BuildStatus::Running:
        return "running";
    case BuildStatus::Succeeded:
        return "succeeded";
    case BuildStatus::Failed:
        return "failed";
    case BuildStatus::Cancelled:
        return "cancelled";
    }
    return "pending";
}

}
