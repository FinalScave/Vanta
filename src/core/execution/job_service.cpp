#include "vanta/execution/job_service.h"

#include <exception>
#include <utility>

#include "vanta/core/json_codec.h"

namespace vanta {
namespace {

bool IsTerminalStatus(JobStatus status) {
    return status == JobStatus::Succeeded || status == JobStatus::Failed || status == JobStatus::Cancelled;
}

}

void JobDispatcher::Dispatch(JobThread thread, JobTask task) const {
    const JobDispatch& dispatch = thread == JobThread::Main ? main : worker;
    if (dispatch) {
        dispatch(std::move(task));
        return;
    }
    if (thread == JobThread::Main && worker) {
        worker(std::move(task));
        return;
    }
    task();
}

JobContext::JobContext(JobService& service, JobId id)
    : service_(&service), id_(id) {}

JobId JobContext::Id() const {
    return id_;
}

bool JobContext::CancellationRequested() const {
    return service_ != nullptr && service_->CancellationRequested(id_);
}

void JobContext::Report(double progress, std::string message) const {
    if (service_ != nullptr) {
        service_->UpdateProgress(id_, progress, std::move(message));
    }
}

void JobContext::AppendOutput(const std::string& output) const {
    if (service_ != nullptr) {
        service_->AppendOutput(id_, output);
    }
}

void JobContext::SetPayload(Value payload) const {
    if (service_ != nullptr) {
        service_->SetPayload(id_, std::move(payload));
    }
}

JobHandle::JobHandle(JobService& service, JobId id)
    : service_(&service), id_(id) {}

JobId JobHandle::Id() const {
    return id_;
}

bool JobHandle::Valid() const {
    return service_ != nullptr && id_ != 0;
}

bool JobHandle::Cancel() {
    return service_ != nullptr && service_->RequestCancel(id_);
}

bool JobHandle::Terminate() {
    return service_ != nullptr && service_->Terminate(id_);
}

std::optional<JobRecord> JobHandle::Record() const {
    return service_ == nullptr ? std::nullopt : service_->Job(id_);
}

std::optional<JobRecord> JobHandle::Wait() const {
    return service_ == nullptr ? std::nullopt : service_->Wait(id_);
}

std::optional<JobRecord> JobHandle::Wait(std::chrono::milliseconds timeout) const {
    return service_ == nullptr ? std::nullopt : service_->Wait(id_, timeout);
}

JobService::JobService(JobDispatcher dispatcher)
    : dispatcher_(std::move(dispatcher)) {}

JobId JobService::Create(JobKind kind, std::string title, std::vector<JobId> dependencies) {
    JobRecord record;
    record.kind = kind;
    record.title = std::move(title);
    record.dependencies = std::move(dependencies);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        record.id = next_job_id_++;
        jobs_[record.id] = record;
        record = jobs_.at(record.id);
    }
    changed_.notify_all();
    Publish(record);
    return record.id;
}

JobId JobService::Start(JobKind kind, std::string title, std::vector<JobId> dependencies) {
    const JobId id = Create(kind, std::move(title), std::move(dependencies));
    MarkRunning(id);
    return id;
}

JobHandle JobService::Submit(JobRequest request, JobFunction function, JobThread thread) {
    const JobId id = Create(request.kind, std::move(request.title), std::move(request.dependencies));
    SetCancellable(id, request.cancellable);
    if (request.payload.has_value()) {
        SetPayload(id, std::move(*request.payload));
    }

    return Submit(id, std::move(function), thread);
}

JobHandle JobService::Submit(JobId id, JobFunction function, JobThread thread) {
    if (!Job(id)) {
        return {};
    }

    auto task = [this, id, function = std::move(function)]() mutable {
        if (!Ready(id)) {
            Complete(id, false, "Job dependencies are not ready");
            return;
        }
        MarkRunning(id);
        JobContext context(*this, id);
        try {
            JobResult result = function(context);
            if (result.payload.has_value()) {
                SetPayload(id, std::move(*result.payload));
            }
            if (!IsTerminal(id)) {
                if (CancellationRequested(id)) {
                    Cancel(id, result.message.empty() ? "Job cancelled" : result.message);
                } else {
                    Complete(id, result.success, std::move(result.message));
                }
            }
        } catch (const std::exception& error) {
            if (!IsTerminal(id)) {
                Complete(id, false, error.what());
            }
        } catch (...) {
            if (!IsTerminal(id)) {
                Complete(id, false, "Job failed");
            }
        }
    };

    dispatcher_.Dispatch(thread, std::move(task));
    return JobHandle(*this, id);
}

void JobService::MarkRunning(JobId id, std::string message) {
    Update(id, [&](JobRecord& record) {
        record.status = JobStatus::Running;
        record.message = std::move(message);
    });
}

void JobService::UpdateProgress(JobId id, double progress, std::string message) {
    Update(id, [&](JobRecord& record) {
        record.progress = progress;
        if (!message.empty()) {
            record.message = message;
            record.output += message;
            if (message.back() != '\n') {
                record.output += '\n';
            }
        }
    });
}

void JobService::AppendOutput(JobId id, const std::string& output) {
    Update(id, [&](JobRecord& record) {
        record.output += output;
    });
}

void JobService::SetCancellable(JobId id, bool cancellable) {
    Update(id, [&](JobRecord& record) {
        record.cancellable = cancellable;
    });
}

void JobService::SetCancelHandler(JobId id, std::function<void()> handler) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto job_it = jobs_.find(id);
        if (job_it == jobs_.end() || IsTerminalStatus(job_it->second.status)) {
            return;
        }
        if (handler) {
            cancel_handlers_[id] = std::move(handler);
        } else {
            cancel_handlers_.erase(id);
        }
    }
    SetCancellable(id, handler != nullptr);
}

void JobService::SetPayload(JobId id, Value payload) {
    Update(id, [&](JobRecord& record) {
        record.payload = std::move(payload);
    });
}

void JobService::Complete(JobId id, bool success, std::string message) {
    Update(id, [&](JobRecord& record) {
        record.status = success ? JobStatus::Succeeded : JobStatus::Failed;
        record.progress = 1.0;
        record.message = std::move(message);
    });
    std::lock_guard<std::mutex> lock(mutex_);
    cancel_handlers_.erase(id);
}

void JobService::Cancel(JobId id, std::string message) {
    Update(id, [&](JobRecord& record) {
        record.status = JobStatus::Cancelled;
        record.progress = 1.0;
        record.message = std::move(message);
    });
    std::lock_guard<std::mutex> lock(mutex_);
    cancel_handlers_.erase(id);
}

bool JobService::RequestCancel(JobId id) {
    JobRecord snapshot;
    std::function<void()> handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = jobs_.find(id);
        if (it == jobs_.end()) {
            return false;
        }
        JobRecord& record = it->second;
        if (!record.cancellable || IsTerminalStatus(record.status)) {
            return false;
        }
        record.cancellation_requested = true;
        auto handler_it = cancel_handlers_.find(id);
        if (handler_it != cancel_handlers_.end()) {
            handler = handler_it->second;
        }
        snapshot = record;
    }
    if (handler) {
        handler();
    }
    Publish(snapshot);
    return true;
}

bool JobService::RequestCancelAll() {
    bool requested = false;
    for (const JobRecord& job : Jobs()) {
        if (!IsTerminalStatus(job.status)) {
            requested = RequestCancel(job.id) || requested;
        }
    }
    return requested;
}

bool JobService::Terminate(JobId id, std::string message) {
    JobRecord snapshot;
    std::function<void()> handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = jobs_.find(id);
        if (it == jobs_.end() || IsTerminalStatus(it->second.status)) {
            return false;
        }
        JobRecord& record = it->second;
        record.cancellation_requested = true;
        record.status = JobStatus::Cancelled;
        record.progress = 1.0;
        record.message = message.empty() ? "Job terminated" : std::move(message);
        auto handler_it = cancel_handlers_.find(id);
        if (handler_it != cancel_handlers_.end()) {
            handler = handler_it->second;
            cancel_handlers_.erase(handler_it);
        }
        snapshot = record;
    }
    if (handler) {
        handler();
    }
    changed_.notify_all();
    Publish(snapshot);
    return true;
}

void JobService::TerminateAll(std::string message) {
    for (const JobRecord& job : Jobs()) {
        if (!IsTerminalStatus(job.status)) {
            Terminate(job.id, message);
        }
    }
}

bool JobService::CancellationRequested(JobId id) const {
    auto found = Job(id);
    return found && found->cancellation_requested;
}

bool JobService::IsTerminal(JobId id) const {
    auto found = Job(id);
    return !found || IsTerminalStatus(found->status);
}

bool JobService::Ready(JobId id) const {
    auto found = Job(id);
    if (!found) {
        return false;
    }
    for (JobId dependency : found->dependencies) {
        auto dependency_job = Job(dependency);
        if (!dependency_job || dependency_job->status != JobStatus::Succeeded) {
            return false;
        }
    }
    return true;
}

std::optional<JobRecord> JobService::Job(JobId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = jobs_.find(id);
    return it == jobs_.end() ? std::nullopt : std::optional<JobRecord>(it->second);
}

std::optional<JobRecord> JobService::Wait(JobId id) const {
    std::unique_lock<std::mutex> lock(mutex_);
    changed_.wait(lock, [this, id] {
        auto it = jobs_.find(id);
        return it == jobs_.end() || IsTerminalStatus(it->second.status);
    });
    auto it = jobs_.find(id);
    return it == jobs_.end() ? std::nullopt : std::optional<JobRecord>(it->second);
}

std::optional<JobRecord> JobService::Wait(JobId id, std::chrono::milliseconds timeout) const {
    std::unique_lock<std::mutex> lock(mutex_);
    const bool completed = changed_.wait_for(lock, timeout, [this, id] {
        auto it = jobs_.find(id);
        return it == jobs_.end() || IsTerminalStatus(it->second.status);
    });
    if (!completed) {
        return std::nullopt;
    }
    auto it = jobs_.find(id);
    return it == jobs_.end() ? std::nullopt : std::optional<JobRecord>(it->second);
}

std::vector<JobRecord> JobService::Jobs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<JobRecord> result;
    for (const auto& [id, job] : jobs_) {
        (void)id;
        result.push_back(job);
    }
    return result;
}

void JobService::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    jobs_.clear();
    cancel_handlers_.clear();
    changed_.notify_all();
}

std::uint64_t JobService::OnDidChangeJob(EventBus<JobChangeEvent>::Listener listener) {
    return on_did_change_.Subscribe(std::move(listener));
}

void JobService::RemoveJobListener(std::uint64_t listener_id) {
    on_did_change_.Unsubscribe(listener_id);
}

void JobService::Publish(const JobRecord& job) {
    on_did_change_.Publish({.job = job});
}

bool JobService::Update(JobId id, const std::function<void(JobRecord&)>& update) {
    JobRecord snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = jobs_.find(id);
        if (it == jobs_.end()) {
            return false;
        }
        update(it->second);
        snapshot = it->second;
    }
    changed_.notify_all();
    Publish(snapshot);
    return true;
}

JobDispatcher InlineJobDispatcher() {
    return {
        .worker = [](JobTask task) {
            task();
        },
        .main = [](JobTask task) {
            task();
        },
    };
}

std::string ToString(JobKind kind) {
    switch (kind) {
    case JobKind::Generic:
        return "generic";
    case JobKind::Initialization:
        return "initialization";
    case JobKind::Index:
        return "index";
    case JobKind::Build:
        return "build";
    case JobKind::Test:
        return "test";
    case JobKind::Run:
        return "run";
    case JobKind::Agent:
        return "agent";
    case JobKind::Plugin:
        return "plugin";
    }
    return "generic";
}

std::string ToString(JobStatus status) {
    switch (status) {
    case JobStatus::Pending:
        return "pending";
    case JobStatus::Running:
        return "running";
    case JobStatus::Succeeded:
        return "succeeded";
    case JobStatus::Failed:
        return "failed";
    case JobStatus::Cancelled:
        return "cancelled";
    }
    return "pending";
}

}
