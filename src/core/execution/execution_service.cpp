#include "vanta/execution/execution_service.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include "vanta/execution/job_service.h"
#include "vanta/platform/process.h"
#include "vanta/workspace/workspace_context.h"

namespace vanta {
namespace {

std::atomic_uint64_t next_execution_id = 1;

}

struct ExecutionState {
    ~ExecutionState() {
        if (worker.joinable()) {
            if (std::this_thread::get_id() == worker.get_id()) {
                worker.detach();
            } else {
                worker.join();
            }
        }
    }

    std::uint64_t id = next_execution_id++;
    JobId job_id = 0;
    mutable std::mutex mutex;
    std::condition_variable completed;
    ExecutionStatus status = ExecutionStatus::Pending;
    bool cancel_requested = false;
    std::optional<ExecutionResult> result;
    std::vector<ExecutionEvent> events;
    std::string output;
    std::unique_ptr<ChildProcess> process;
    std::thread worker;
};

namespace {

void EmitEvent(const std::shared_ptr<ExecutionState>& state, ExecutionEventCallback& on_event, ExecutionEvent event) {
    bool completed = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (event.kind == ExecutionEventKind::Started) {
            state->status = ExecutionStatus::Running;
        } else if (event.kind == ExecutionEventKind::Stdout || event.kind == ExecutionEventKind::Stderr) {
            state->output += event.text;
        } else if (event.kind == ExecutionEventKind::Finished) {
            state->status = state->cancel_requested ? ExecutionStatus::Cancelled : (event.exit_code == 0 ? ExecutionStatus::Succeeded : ExecutionStatus::Failed);
            completed = true;
        }
        state->events.push_back(event);
        if (completed) {
            state->result = ExecutionResult{
                .exit_code = event.exit_code,
                .output = state->output,
                .job_id = state->job_id,
                .events = state->events,
            };
        }
    }
    if (on_event) {
        on_event(event);
    }
    if (completed) {
        state->completed.notify_all();
    }
}

ExecutionHandle CompletedHandle(ExecutionResult result, ExecutionStatus status, ExecutionEventCallback on_event = {}) {
    auto state = std::make_shared<ExecutionState>();
    state->job_id = result.job_id;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->status = status;
        state->events = result.events;
        state->output = result.output;
        state->result = std::move(result);
    }
    if (on_event) {
        for (const ExecutionEvent& event : state->events) {
            on_event(event);
        }
    }
    state->completed.notify_all();
    return ExecutionHandle(std::move(state));
}

class LocalExecutionProvider final : public ExecutionProvider {
public:
    std::string Id() const override {
        return "vanta.localExecutor";
    }

    std::vector<ExecutionTarget> Targets(WorkspaceContext& context) const override {
        (void)context;
        return {{
            .id = "local.default",
            .executor_id = Id(),
            .name = "Local Machine",
            .kind = ExecutionTargetKind::Local,
            .capabilities = {"run", "debug"},
        }};
    }

    ExecutionHandle Start(
        WorkspaceContext& context,
        const ExecutionRequest& request,
        const ExecutionTarget& target,
        ExecutionEventCallback on_event = {}) const override {
        (void)context;
        auto state = std::make_shared<ExecutionState>();
        state->job_id = request.job_id;

        state->worker = std::thread([state, request, target, on_event = std::move(on_event)]() mutable {
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->process = std::make_unique<ChildProcess>();
            }

            EmitEvent(state, on_event, {
                .kind = ExecutionEventKind::Started,
                .job_id = request.job_id,
                .executor_id = target.executor_id,
                .target_id = target.id,
                .progress = 0.0,
            });

            std::string error;
            bool started = false;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                started = state->process->Start({
                    .executable = request.executable,
                    .arguments = request.arguments,
                    .working_directory = request.working_directory,
                }, &error);
            }
            if (!started) {
                EmitEvent(state, on_event, {
                    .kind = ExecutionEventKind::Stderr,
                    .job_id = request.job_id,
                    .executor_id = target.executor_id,
                    .target_id = target.id,
                    .text = error.empty() ? "Failed to start process\n" : error + "\n",
                });
                EmitEvent(state, on_event, {
                    .kind = ExecutionEventKind::Finished,
                    .job_id = request.job_id,
                    .executor_id = target.executor_id,
                    .target_id = target.id,
                    .progress = 1.0,
                    .exit_code = -1,
                });
                return;
            }

            int exit_code = -1;
            while (true) {
                std::string stdout_chunk;
                std::string stderr_chunk;
                std::optional<int> finished;
                bool should_cancel = false;
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    if (state->process != nullptr) {
                        stdout_chunk = state->process->ReadStdoutAvailable();
                        stderr_chunk = state->process->ReadStderrAvailable();
                        should_cancel = state->cancel_requested;
                        if (should_cancel) {
                            state->process->Terminate();
                            finished = state->process->ExitCode().value_or(130);
                        } else {
                            finished = state->process->TryWait();
                        }
                    }
                }

                if (!stdout_chunk.empty()) {
                    EmitEvent(state, on_event, {
                        .kind = ExecutionEventKind::Stdout,
                        .job_id = request.job_id,
                        .executor_id = target.executor_id,
                        .target_id = target.id,
                        .text = stdout_chunk,
                    });
                }
                if (!stderr_chunk.empty()) {
                    EmitEvent(state, on_event, {
                        .kind = ExecutionEventKind::Stderr,
                        .job_id = request.job_id,
                        .executor_id = target.executor_id,
                        .target_id = target.id,
                        .text = stderr_chunk,
                    });
                }
                if (finished) {
                    exit_code = *finished;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->process.reset();
            }
            EmitEvent(state, on_event, {
                .kind = ExecutionEventKind::Finished,
                .job_id = request.job_id,
                .executor_id = target.executor_id,
                .target_id = target.id,
                .progress = 1.0,
                .exit_code = exit_code,
            });
        });

        return ExecutionHandle(std::move(state));
    }
};

}

ExecutionHandle::ExecutionHandle(std::shared_ptr<ExecutionState> state)
    : state_(std::move(state)) {}

std::uint64_t ExecutionHandle::Id() const {
    return state_ == nullptr ? 0 : state_->id;
}

JobId ExecutionHandle::JobIdValue() const {
    return state_ == nullptr ? 0 : state_->job_id;
}

ExecutionStatus ExecutionHandle::Status() const {
    if (state_ == nullptr) {
        return ExecutionStatus::Failed;
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->status;
}

bool ExecutionHandle::Running() const {
    const ExecutionStatus current = Status();
    return current == ExecutionStatus::Pending || current == ExecutionStatus::Running;
}

void ExecutionHandle::Cancel() {
    if (state_ == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->cancel_requested = true;
    if (state_->status == ExecutionStatus::Pending || state_->status == ExecutionStatus::Running) {
        state_->status = ExecutionStatus::Cancelled;
    }
    if (state_->process != nullptr) {
        state_->process->Terminate();
    }
}

void ExecutionHandle::Terminate() {
    Cancel();
}

ExecutionResult ExecutionHandle::Wait() {
    if (state_ == nullptr) {
        return {.exit_code = -1, .output = "Execution handle is not valid\n"};
    }
    if (state_->worker.joinable() && std::this_thread::get_id() != state_->worker.get_id()) {
        state_->worker.join();
    }
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->completed.wait(lock, [this] {
        return state_->result.has_value();
    });
    return *state_->result;
}

std::optional<ExecutionResult> ExecutionHandle::ResultValue() const {
    if (state_ == nullptr) {
        return std::nullopt;
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->result;
}

std::vector<ExecutionEvent> ExecutionHandle::EventsValue() const {
    if (state_ == nullptr) {
        return {};
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->events;
}

bool ExecutionHandle::Valid() const {
    return state_ != nullptr;
}

ExecutionResult ExecutionProvider::Execute(
    WorkspaceContext& context,
    const ExecutionRequest& request,
    const ExecutionTarget& target,
    ExecutionEventCallback on_event) const {
    return Start(context, request, target, std::move(on_event)).Wait();
}

RegistrationHandle ExecutionService::RegisterProvider(std::unique_ptr<ExecutionProvider> provider) {
    if (provider == nullptr || provider->Id().empty()) {
        return {};
    }
    const std::string provider_id = provider->Id();
    providers_[provider_id] = std::move(provider);
    return RegistrationHandle([this, provider_id] {
        RemoveProvider(provider_id);
    });
}

void ExecutionService::RemoveProvider(const std::string& provider_id) {
    providers_.erase(provider_id);
}

std::vector<std::string> ExecutionService::ProviderIds() const {
    std::vector<std::string> ids;
    for (const auto& [id, provider] : providers_) {
        (void)provider;
        ids.push_back(id);
    }
    return ids;
}

std::vector<ExecutionTarget> ExecutionService::Targets(WorkspaceContext& context) const {
    std::vector<ExecutionTarget> values;
    for (const auto& [id, provider] : providers_) {
        (void)id;
        std::vector<ExecutionTarget> provided = provider->Targets(context);
        values.insert(values.end(), provided.begin(), provided.end());
    }
    return values;
}

ExecutionResult ExecutionService::Execute(
    WorkspaceContext& context,
    const ExecutionRequest& request,
    const ExecutionTarget& target,
    ExecutionEventCallback on_event) const {
    return Start(context, request, target, std::move(on_event)).Wait();
}

ExecutionHandle ExecutionService::Start(
    WorkspaceContext& context,
    const ExecutionRequest& request,
    const ExecutionTarget& target,
    ExecutionEventCallback on_event) const {
    const ExecutionProvider* provider = ProviderFor(request, target);
    if (provider == nullptr) {
        ExecutionEvent event{
            .kind = ExecutionEventKind::Finished,
            .job_id = request.job_id,
            .executor_id = target.executor_id,
            .target_id = target.id,
            .text = "Execution provider not found\n",
            .progress = 1.0,
            .exit_code = -1,
        };
        ExecutionResult result{
            .exit_code = -1,
            .output = "Execution provider not found\n",
            .job_id = request.job_id,
            .events = {event},
        };
        return CompletedHandle(std::move(result), ExecutionStatus::Failed, std::move(on_event));
    }
    return provider->Start(context, request, target, std::move(on_event));
}

const ExecutionProvider* ExecutionService::ProviderFor(const ExecutionRequest& request, const ExecutionTarget& target) const {
    (void)request;
    auto it = providers_.find(target.executor_id);
    return it == providers_.end() ? nullptr : it->second.get();
}

void RegisterDefaultExecutionProviders(ExecutionService& service) {
    service.RegisterProvider(std::make_unique<LocalExecutionProvider>());
}

void ApplyExecutionEventToJob(JobService& jobs, const ExecutionEvent& event) {
    if (event.job_id == 0) {
        return;
    }
    switch (event.kind) {
    case ExecutionEventKind::Started:
        jobs.MarkRunning(event.job_id);
        if (event.progress >= 0.0) {
            jobs.UpdateProgress(event.job_id, event.progress);
        }
        return;
    case ExecutionEventKind::Stdout:
    case ExecutionEventKind::Stderr:
        jobs.AppendOutput(event.job_id, event.text);
        return;
    case ExecutionEventKind::Progress:
        jobs.UpdateProgress(event.job_id, event.progress, event.text);
        return;
    case ExecutionEventKind::Finished:
        if (!event.text.empty()) {
            jobs.AppendOutput(event.job_id, event.text);
        }
        if (jobs.CancellationRequested(event.job_id)) {
            jobs.Cancel(event.job_id, "Job cancelled");
        } else {
            jobs.Complete(event.job_id, event.exit_code == 0);
        }
        return;
    }
}

std::string ToString(ExecutionTargetKind kind) {
    switch (kind) {
    case ExecutionTargetKind::Local:
        return "local";
    case ExecutionTargetKind::Device:
        return "device";
    case ExecutionTargetKind::Remote:
        return "remote";
    case ExecutionTargetKind::Container:
        return "container";
    case ExecutionTargetKind::Custom:
        return "custom";
    }
    return "custom";
}

std::string ToString(ExecutionStatus status) {
    switch (status) {
    case ExecutionStatus::Pending:
        return "pending";
    case ExecutionStatus::Running:
        return "running";
    case ExecutionStatus::Succeeded:
        return "succeeded";
    case ExecutionStatus::Failed:
        return "failed";
    case ExecutionStatus::Cancelled:
        return "cancelled";
    }
    return "pending";
}

}
