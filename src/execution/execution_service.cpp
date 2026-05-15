#include "vanta/execution/execution_service.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include "vanta/workspace/workspace_context.h"
#include "vanta/platform/process.h"

namespace vanta {
namespace {

std::atomic_uint64_t nextExecutionId = 1;

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

    std::uint64_t id = nextExecutionId++;
    JobId jobId = 0;
    mutable std::mutex mutex;
    std::condition_variable completed;
    ExecutionStatus status = ExecutionStatus::Pending;
    bool cancelRequested = false;
    std::optional<ExecutionResult> result;
    std::vector<ExecutionEvent> events;
    std::string output;
    std::unique_ptr<ChildProcess> process;
    std::thread worker;
};

namespace {

Json stringArrayToJson(const std::vector<std::string>& values) {
    Json::Array array;
    for (const std::string& value : values) {
        array.push_back(Json(value));
    }
    return Json::array(std::move(array));
}

void emitEvent(const std::shared_ptr<ExecutionState>& state, ExecutionEventCallback& onEvent, ExecutionEvent event) {
    bool completed = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (event.kind == ExecutionEventKind::Started) {
            state->status = ExecutionStatus::Running;
        } else if (event.kind == ExecutionEventKind::Stdout || event.kind == ExecutionEventKind::Stderr) {
            state->output += event.text;
        } else if (event.kind == ExecutionEventKind::Finished) {
            state->status = state->cancelRequested ? ExecutionStatus::Cancelled : (event.exitCode == 0 ? ExecutionStatus::Succeeded : ExecutionStatus::Failed);
            completed = true;
        }
        state->events.push_back(event);
        if (completed) {
            state->result = ExecutionResult{
                .exitCode = event.exitCode,
                .output = state->output,
                .jobId = state->jobId,
                .events = state->events,
            };
        }
    }
    if (onEvent) {
        onEvent(event);
    }
    if (completed) {
        state->completed.notify_all();
    }
}

ExecutionHandle completedHandle(ExecutionResult result, ExecutionStatus status, ExecutionEventCallback onEvent = {}) {
    auto state = std::make_shared<ExecutionState>();
    state->jobId = result.jobId;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->status = status;
        state->events = result.events;
        state->output = result.output;
        state->result = std::move(result);
    }
    if (onEvent) {
        for (const ExecutionEvent& event : state->events) {
            onEvent(event);
        }
    }
    state->completed.notify_all();
    return ExecutionHandle(std::move(state));
}

class LocalExecutionProvider final : public ExecutionProvider {
public:
    std::string id() const override {
        return "vanta.localExecutor";
    }

    std::vector<ExecutionTarget> targets(WorkspaceContext& context) const override {
        (void)context;
        return {{
            .id = "local.default",
            .executorId = id(),
            .name = "Local Machine",
            .capabilities = {"run", "debug"},
            .metadata = Json::object({
                {"kind", Json("local")},
            }),
        }};
    }

    ExecutionHandle start(
        WorkspaceContext& context,
        const ExecutionRequest& request,
        const ExecutionTarget& target,
        ExecutionEventCallback onEvent = {}) const override {
        (void)context;
        auto state = std::make_shared<ExecutionState>();
        state->jobId = request.jobId;

        state->worker = std::thread([state, request, target, onEvent = std::move(onEvent)]() mutable {
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->process = std::make_unique<ChildProcess>();
            }

            emitEvent(state, onEvent, {
                .kind = ExecutionEventKind::Started,
                .jobId = request.jobId,
                .executorId = target.executorId,
                .targetId = target.id,
                .progress = 0.0,
            });

            std::string error;
            bool started = false;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                started = state->process->start({
                    .executable = request.executable,
                    .arguments = request.arguments,
                    .workingDirectory = request.workingDirectory,
                }, &error);
            }
            if (!started) {
                emitEvent(state, onEvent, {
                    .kind = ExecutionEventKind::Stderr,
                    .jobId = request.jobId,
                    .executorId = target.executorId,
                    .targetId = target.id,
                    .text = error.empty() ? "Failed to start process\n" : error + "\n",
                });
                emitEvent(state, onEvent, {
                    .kind = ExecutionEventKind::Finished,
                    .jobId = request.jobId,
                    .executorId = target.executorId,
                    .targetId = target.id,
                    .progress = 1.0,
                    .exitCode = -1,
                });
                return;
            }

            int exitCode = -1;
            while (true) {
                std::string stdoutChunk;
                std::string stderrChunk;
                std::optional<int> finished;
                bool shouldCancel = false;
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    if (state->process != nullptr) {
                        stdoutChunk = state->process->readStdoutAvailable();
                        stderrChunk = state->process->readStderrAvailable();
                        shouldCancel = state->cancelRequested;
                        if (shouldCancel) {
                            state->process->terminate();
                            finished = state->process->exitCode().value_or(130);
                        } else {
                            finished = state->process->tryWait();
                        }
                    }
                }

                if (!stdoutChunk.empty()) {
                    emitEvent(state, onEvent, {
                        .kind = ExecutionEventKind::Stdout,
                        .jobId = request.jobId,
                        .executorId = target.executorId,
                        .targetId = target.id,
                        .text = stdoutChunk,
                    });
                }
                if (!stderrChunk.empty()) {
                    emitEvent(state, onEvent, {
                        .kind = ExecutionEventKind::Stderr,
                        .jobId = request.jobId,
                        .executorId = target.executorId,
                        .targetId = target.id,
                        .text = stderrChunk,
                    });
                }
                if (finished) {
                    exitCode = *finished;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->process.reset();
            }
            emitEvent(state, onEvent, {
                .kind = ExecutionEventKind::Finished,
                .jobId = request.jobId,
                .executorId = target.executorId,
                .targetId = target.id,
                .progress = 1.0,
                .exitCode = exitCode,
            });
        });

        return ExecutionHandle(std::move(state));
    }
};

}

ExecutionHandle::ExecutionHandle(std::shared_ptr<ExecutionState> state)
    : state_(std::move(state)) {}

std::uint64_t ExecutionHandle::id() const {
    return state_ == nullptr ? 0 : state_->id;
}

JobId ExecutionHandle::jobId() const {
    return state_ == nullptr ? 0 : state_->jobId;
}

ExecutionStatus ExecutionHandle::status() const {
    if (state_ == nullptr) {
        return ExecutionStatus::Failed;
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->status;
}

bool ExecutionHandle::running() const {
    const ExecutionStatus current = status();
    return current == ExecutionStatus::Pending || current == ExecutionStatus::Running;
}

void ExecutionHandle::cancel() {
    if (state_ == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->cancelRequested = true;
    if (state_->status == ExecutionStatus::Pending || state_->status == ExecutionStatus::Running) {
        state_->status = ExecutionStatus::Cancelled;
    }
    if (state_->process != nullptr) {
        state_->process->terminate();
    }
}

ExecutionResult ExecutionHandle::wait() {
    if (state_ == nullptr) {
        return {.exitCode = -1, .output = "Execution handle is not valid\n"};
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

std::optional<ExecutionResult> ExecutionHandle::result() const {
    if (state_ == nullptr) {
        return std::nullopt;
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->result;
}

std::vector<ExecutionEvent> ExecutionHandle::events() const {
    if (state_ == nullptr) {
        return {};
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->events;
}

bool ExecutionHandle::valid() const {
    return state_ != nullptr;
}

ExecutionResult ExecutionProvider::execute(
    WorkspaceContext& context,
    const ExecutionRequest& request,
    const ExecutionTarget& target,
    ExecutionEventCallback onEvent) const {
    return start(context, request, target, std::move(onEvent)).wait();
}

void ExecutionService::addProvider(std::unique_ptr<ExecutionProvider> provider) {
    if (provider == nullptr || provider->id().empty()) {
        return;
    }
    providers_[provider->id()] = std::move(provider);
}

void ExecutionService::removeProvider(const std::string& providerId) {
    providers_.erase(providerId);
}

std::vector<std::string> ExecutionService::providerIds() const {
    std::vector<std::string> ids;
    for (const auto& [id, provider] : providers_) {
        (void)provider;
        ids.push_back(id);
    }
    return ids;
}

std::vector<ExecutionTarget> ExecutionService::targets(WorkspaceContext& context) const {
    std::vector<ExecutionTarget> values;
    for (const auto& [id, provider] : providers_) {
        (void)id;
        std::vector<ExecutionTarget> provided = provider->targets(context);
        values.insert(values.end(), provided.begin(), provided.end());
    }
    return values;
}

ExecutionResult ExecutionService::execute(
    WorkspaceContext& context,
    const ExecutionRequest& request,
    const ExecutionTarget& target,
    ExecutionEventCallback onEvent) const {
    return start(context, request, target, std::move(onEvent)).wait();
}

ExecutionHandle ExecutionService::start(
    WorkspaceContext& context,
    const ExecutionRequest& request,
    const ExecutionTarget& target,
    ExecutionEventCallback onEvent) const {
    const ExecutionProvider* provider = providerFor(request, target);
    if (provider == nullptr) {
        ExecutionEvent event{
            .kind = ExecutionEventKind::Finished,
            .jobId = request.jobId,
            .executorId = target.executorId,
            .targetId = target.id,
            .text = "Execution provider not found\n",
            .progress = 1.0,
            .exitCode = -1,
        };
        ExecutionResult result{
            .exitCode = -1,
            .output = "Execution provider not found\n",
            .jobId = request.jobId,
            .events = {event},
        };
        return completedHandle(std::move(result), ExecutionStatus::Failed, std::move(onEvent));
    }
    return provider->start(context, request, target, std::move(onEvent));
}

const ExecutionProvider* ExecutionService::providerFor(const ExecutionRequest& request, const ExecutionTarget& target) const {
    (void)request;
    auto it = providers_.find(target.executorId);
    return it == providers_.end() ? nullptr : it->second.get();
}

void registerDefaultExecutionProviders(ExecutionService& service) {
    service.addProvider(std::make_unique<LocalExecutionProvider>());
}

std::string toString(ExecutionStatus status) {
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

Json toJson(const ExecutionTarget& target) {
    return Json::object({
        {"id", Json(target.id)},
        {"executorId", Json(target.executorId)},
        {"name", Json(target.name)},
        {"capabilities", stringArrayToJson(target.capabilities)},
        {"metadata", target.metadata},
    });
}

Json toJson(const ExecutionResult& result) {
    return Json::object({
        {"exitCode", Json(static_cast<std::int64_t>(result.exitCode))},
        {"output", Json(result.output)},
        {"jobId", Json(static_cast<std::int64_t>(result.jobId))},
        {"events", toJson(result.events)},
    });
}

}
