#include "vanta/execution/build_service.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include "vanta/execution/problem_matcher.h"
#include "vanta/workspace/workspace_context.h"
#include "vanta/workspace/workspace_runtime.h"

namespace vanta {
namespace {

BuildStatus buildStatusFromJob(const std::optional<JobRecord>& job) {
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

BuildHandle::BuildHandle(JobHandle job, std::shared_ptr<BuildState> state)
    : job_(job), state_(std::move(state)) {}

JobId BuildHandle::jobId() const {
    return job_.id();
}

JobHandle BuildHandle::jobHandle() const {
    return job_;
}

BuildStatus BuildHandle::status() const {
    if (!valid()) {
        return BuildStatus::Failed;
    }
    return buildStatusFromJob(job_.record());
}

bool BuildHandle::running() const {
    const BuildStatus current = status();
    return current == BuildStatus::Pending || current == BuildStatus::Running;
}

void BuildHandle::cancel() {
    job_.cancel();
}

BuildResult BuildHandle::wait() {
    if (!valid()) {
        return {.exitCode = -1, .output = "Build handle is not valid\n"};
    }
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->completed.wait(lock, [this] {
        return state_->result.has_value();
    });
    BuildResult result = *state_->result;
    lock.unlock();
    job_.wait();
    return result;
}

std::optional<BuildResult> BuildHandle::result() const {
    if (state_ == nullptr) {
        return std::nullopt;
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->result;
}

std::vector<ExecutionEvent> BuildHandle::events() const {
    if (state_ == nullptr) {
        return {};
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->events;
}

bool BuildHandle::valid() const {
    return state_ != nullptr && job_.valid();
}

BuildHandle startBuildOperation(
    JobService& jobs,
    AsyncRuntime& runtime,
    JobId jobId,
    BuildOperation operation,
    ExecutionEventCallback onEvent) {
    auto state = std::make_shared<BuildState>();
    JobHandle jobHandle = jobs.submit(runtime, jobId, [state, operation = std::move(operation), onEvent = std::move(onEvent)](JobContext& job) mutable {
        auto emit = [&](const ExecutionEvent& event) {
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->events.push_back(event);
            }
            if (onEvent) {
                onEvent(event);
            }
        };
        auto cancelled = [&] {
            return job.cancellationRequested();
        };

        BuildResult result;
        try {
            result = operation(emit, cancelled);
        } catch (const std::exception& error) {
            result = {.exitCode = -1, .output = std::string(error.what()) + "\n"};
        } catch (...) {
            result = {.exitCode = -1, .output = "Build failed\n"};
        }
        BuildStatus completedStatus = BuildStatus::Failed;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (result.events.empty()) {
                result.events = state->events;
            }
            completedStatus = job.cancellationRequested() ? BuildStatus::Cancelled : (result.exitCode == 0 ? BuildStatus::Succeeded : BuildStatus::Failed);
            state->result = std::move(result);
        }
        state->completed.notify_all();
        return JobResult{
            .success = completedStatus == BuildStatus::Succeeded,
            .message = completedStatus == BuildStatus::Cancelled ? "Build cancelled" : "",
        };
    });
    jobs.setCancellable(jobId, true);
    return BuildHandle(jobHandle, std::move(state));
}

void DefaultBuildService::addProvider(std::unique_ptr<BuildProvider> provider) {
    if (provider == nullptr) {
        return;
    }
    providers_[provider->id()] = std::move(provider);
}

RegistrationHandle DefaultBuildService::registerProvider(std::unique_ptr<BuildProvider> provider) {
    if (provider == nullptr || provider->id().empty()) {
        return {};
    }
    const std::string providerId = provider->id();
    addProvider(std::move(provider));
    return RegistrationHandle([this, providerId] {
        removeProvider(providerId);
    });
}

void DefaultBuildService::removeProvider(const std::string& providerId) {
    providers_.erase(providerId);
}

std::vector<std::string> DefaultBuildService::buildProviderIds() const {
    std::vector<std::string> ids;
    for (const auto& [id, provider] : providers_) {
        (void)provider;
        ids.push_back(id);
    }
    return ids;
}

BuildEnvironment DefaultBuildService::detect(const std::filesystem::path& workspaceRoot) const {
    for (const auto& [id, provider] : providers_) {
        (void)id;
        BuildEnvironment environment = provider->detect(workspaceRoot);
        if (environment.detected) {
            return environment;
        }
    }
    return {};
}

BuildHandle DefaultBuildService::start(
    WorkspaceContext& context,
    const std::filesystem::path& workspaceRoot,
    const BuildTask& task,
    ExecutionEventCallback onEvent) const {
    BuildTask effectiveTask = task;
    if (effectiveTask.jobId == 0 || !context.jobs().job(effectiveTask.jobId)) {
        effectiveTask.jobId = context.jobs().start(effectiveTask.kind == BuildTaskKind::Build ? JobKind::Build : JobKind::Test, toString(effectiveTask.kind));
    }
    const BuildProvider* provider = chooseProvider(workspaceRoot, task);
    BuildHandle handle = startBuildOperation(context.jobs(), context.runtime()->async(), effectiveTask.jobId, [&context, workspaceRoot, task = effectiveTask, provider](ExecutionEventCallback emit, BuildCancellationCheck cancelled) {
        std::vector<ExecutionEvent> events;
        std::string output;
        std::vector<Diagnostic> diagnostics;

        auto emitDirect = [&](ExecutionEvent event) {
            output += event.text;
            events.push_back(event);
            context.jobs().applyExecutionEvent(events.back());
            if (emit) {
                emit(events.back());
            }
        };

        auto fail = [&](std::string message) {
            emitDirect({
                .kind = ExecutionEventKind::Finished,
                .jobId = task.jobId,
                .text = message,
                .progress = 1.0,
                .exitCode = -1,
            });
            return BuildResult{.exitCode = -1, .output = output, .diagnostics = diagnostics, .events = events};
        };

        if (provider == nullptr) {
            return fail("No build provider is available for this workspace\n");
        }

        BuildPlan plan = provider->plan(workspaceRoot, task);
        if (plan.steps.empty()) {
            return fail("Build provider did not produce any execution steps\n");
        }

        for (BuildStep& step : plan.steps) {
            if (cancelled()) {
                emitDirect({
                    .kind = ExecutionEventKind::Finished,
                    .jobId = task.jobId,
                    .text = "Build cancelled\n",
                    .progress = 1.0,
                    .exitCode = 130,
                });
                return BuildResult{.exitCode = 130, .output = output, .diagnostics = diagnostics, .events = events};
            }

            ExecutionRequest request = step.request;
            request.jobId = task.jobId;
            if (request.workingDirectory.empty()) {
                request.workingDirectory = workspaceRoot;
            }

            std::vector<ExecutionTarget> targets = context.execution().targets();
            auto target = targets.begin();
            if (!task.executionTargetId.empty()) {
                target = std::find_if(targets.begin(), targets.end(), [&](const ExecutionTarget& value) {
                    return value.id == task.executionTargetId;
                });
            }
            if (target == targets.end()) {
                return fail("Execution target not found for build step\n");
            }

            ExecutionHandle handle = context.runtime()->execution().start(context, request, *target, [&](const ExecutionEvent& event) {
                events.push_back(event);
                switch (event.kind) {
                case ExecutionEventKind::Started:
                    context.jobs().markRunning(task.jobId);
                    if (event.progress >= 0.0) {
                        context.jobs().updateProgress(task.jobId, event.progress);
                    }
                    break;
                case ExecutionEventKind::Stdout:
                case ExecutionEventKind::Stderr:
                    context.jobs().appendOutput(task.jobId, event.text);
                    break;
                case ExecutionEventKind::Progress:
                    context.jobs().updateProgress(task.jobId, event.progress, event.text);
                    break;
                case ExecutionEventKind::Finished:
                    if (!event.text.empty()) {
                        context.jobs().appendOutput(task.jobId, event.text);
                    }
                    if (event.progress >= 0.0) {
                        context.jobs().updateProgress(task.jobId, event.progress);
                    }
                    break;
                }
                if (emit) {
                    emit(event);
                }
            });
            if (!handle.valid()) {
                return fail("Build step did not start\n");
            }

            while (handle.running()) {
                if (cancelled()) {
                    handle.cancel();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            ExecutionResult result = handle.wait();
            output += result.output;
            if (step.parseDiagnostics) {
                const ProblemMatcher matcher;
                const DiagnosticResolver resolver;
                const std::filesystem::path baseDirectory = task.buildDirectory.empty() ? request.workingDirectory : task.buildDirectory;
                std::vector<Diagnostic> stepDiagnostics = resolver.resolve(matcher.matchCompilerOutput(result.output), context.workspace(), baseDirectory);
                diagnostics.insert(diagnostics.end(), stepDiagnostics.begin(), stepDiagnostics.end());
            }
            if (result.exitCode != 0) {
                return BuildResult{.exitCode = result.exitCode, .output = output, .diagnostics = diagnostics, .events = events};
            }
        }

        return BuildResult{.exitCode = 0, .output = output, .diagnostics = diagnostics, .events = events};
    }, std::move(onEvent));
    return handle;
}

BuildResult DefaultBuildService::run(
    WorkspaceContext& context,
    const std::filesystem::path& workspaceRoot,
    const BuildTask& task,
    ExecutionEventCallback onEvent) const {
    return start(context, workspaceRoot, task, std::move(onEvent)).wait();
}

const BuildProvider* DefaultBuildService::chooseProvider(const std::filesystem::path& workspaceRoot, const BuildTask& task) const {
    if (!task.providerId.empty()) {
        auto it = providers_.find(task.providerId);
        return it == providers_.end() ? nullptr : it->second.get();
    }

    for (const auto& [id, provider] : providers_) {
        (void)id;
        BuildEnvironment environment = provider->detect(workspaceRoot);
        if (environment.detected) {
            return provider.get();
        }
    }
    return nullptr;
}

std::string toString(BuildTaskKind kind) {
    return kind == BuildTaskKind::Build ? "build" : "test";
}

std::string toString(BuildStatus status) {
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
