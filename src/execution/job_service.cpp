#include "vanta/execution/job_service.h"

#include <exception>
#include <utility>

#include "vanta/execution/execution_protocol.h"

namespace vanta {
namespace {

bool terminal(JobStatus status) {
    return status == JobStatus::Succeeded || status == JobStatus::Failed || status == JobStatus::Cancelled;
}

}

JobContext::JobContext(JobService& service, JobId id)
    : service_(&service), id_(id) {}

JobId JobContext::id() const {
    return id_;
}

bool JobContext::cancellationRequested() const {
    return service_ != nullptr && service_->cancellationRequested(id_);
}

void JobContext::report(double progress, std::string message) const {
    if (service_ != nullptr) {
        service_->updateProgress(id_, progress, std::move(message));
    }
}

void JobContext::appendOutput(const std::string& output) const {
    if (service_ != nullptr) {
        service_->appendOutput(id_, output);
    }
}

void JobContext::setData(Json data) const {
    if (service_ != nullptr) {
        service_->setData(id_, std::move(data));
    }
}

JobHandle::JobHandle(JobService& service, JobId id)
    : service_(&service), id_(id) {}

JobId JobHandle::id() const {
    return id_;
}

bool JobHandle::valid() const {
    return service_ != nullptr && id_ != 0;
}

bool JobHandle::cancel() {
    return service_ != nullptr && service_->requestCancel(id_);
}

std::optional<JobRecord> JobHandle::record() const {
    return service_ == nullptr ? std::nullopt : service_->job(id_);
}

std::optional<JobRecord> JobHandle::wait() const {
    return service_ == nullptr ? std::nullopt : service_->wait(id_);
}

JobId JobService::create(JobKind kind, std::string title, std::vector<JobId> dependencies) {
    JobRecord record;
    record.kind = kind;
    record.title = std::move(title);
    record.dependencies = std::move(dependencies);
    record.data = Json::object();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        record.id = nextJobId_++;
        jobs_[record.id] = record;
        record = jobs_.at(record.id);
    }
    changed_.notify_all();
    publish(record);
    return record.id;
}

JobId JobService::start(JobKind kind, std::string title, std::vector<JobId> dependencies) {
    const JobId id = create(kind, std::move(title), std::move(dependencies));
    markRunning(id);
    return id;
}

JobHandle JobService::submit(AsyncRuntime& runtime, JobRequest request, JobFunction function, JobThread thread) {
    const JobId id = create(request.kind, std::move(request.title), std::move(request.dependencies));
    setCancellable(id, request.cancellable);
    if (!request.data.isNull()) {
        setData(id, std::move(request.data));
    }

    return submit(runtime, id, std::move(function), thread);
}

JobHandle JobService::submit(AsyncRuntime& runtime, JobId id, JobFunction function, JobThread thread) {
    if (!job(id)) {
        return {};
    }

    auto task = [this, id, function = std::move(function)]() mutable {
        if (!ready(id)) {
            complete(id, false, "Job dependencies are not ready");
            return;
        }
        markRunning(id);
        JobContext context(*this, id);
        try {
            JobResult result = function(context);
            if (!result.data.isNull()) {
                setData(id, std::move(result.data));
            }
            if (!isTerminal(id)) {
                if (cancellationRequested(id)) {
                    cancel(id, result.message.empty() ? "Job cancelled" : result.message);
                } else {
                    complete(id, result.success, std::move(result.message));
                }
            }
        } catch (const std::exception& error) {
            if (!isTerminal(id)) {
                complete(id, false, error.what());
            }
        } catch (...) {
            if (!isTerminal(id)) {
                complete(id, false, "Job failed");
            }
        }
    };

    if (thread == JobThread::Main) {
        runtime.postMain(std::move(task));
    } else {
        runtime.postWorker(std::move(task));
    }
    return JobHandle(*this, id);
}

void JobService::markRunning(JobId id, std::string message) {
    update(id, [&](JobRecord& record) {
        record.status = JobStatus::Running;
        record.message = std::move(message);
    });
}

void JobService::updateProgress(JobId id, double progress, std::string message) {
    update(id, [&](JobRecord& record) {
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

void JobService::appendOutput(JobId id, const std::string& output) {
    update(id, [&](JobRecord& record) {
        record.output += output;
    });
}

void JobService::setCancellable(JobId id, bool cancellable) {
    update(id, [&](JobRecord& record) {
        record.cancellable = cancellable;
    });
}

void JobService::setCancelHandler(JobId id, std::function<void()> handler) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto jobIt = jobs_.find(id);
        if (jobIt == jobs_.end() || terminal(jobIt->second.status)) {
            return;
        }
        if (handler) {
            cancelHandlers_[id] = std::move(handler);
        } else {
            cancelHandlers_.erase(id);
        }
    }
    setCancellable(id, handler != nullptr);
}

void JobService::setData(JobId id, Json data) {
    update(id, [&](JobRecord& record) {
        record.data = std::move(data);
    });
}

void JobService::complete(JobId id, bool success, std::string message) {
    update(id, [&](JobRecord& record) {
        record.status = success ? JobStatus::Succeeded : JobStatus::Failed;
        record.progress = 1.0;
        record.message = std::move(message);
    });
    std::lock_guard<std::mutex> lock(mutex_);
    cancelHandlers_.erase(id);
}

void JobService::cancel(JobId id, std::string message) {
    update(id, [&](JobRecord& record) {
        record.status = JobStatus::Cancelled;
        record.progress = 1.0;
        record.message = std::move(message);
    });
    std::lock_guard<std::mutex> lock(mutex_);
    cancelHandlers_.erase(id);
}

bool JobService::requestCancel(JobId id) {
    JobRecord snapshot;
    std::function<void()> handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = jobs_.find(id);
        if (it == jobs_.end()) {
            return false;
        }
        JobRecord& record = it->second;
        if (!record.cancellable || terminal(record.status)) {
            return false;
        }
        record.cancellationRequested = true;
        auto handlerIt = cancelHandlers_.find(id);
        if (handlerIt != cancelHandlers_.end()) {
            handler = handlerIt->second;
        }
        snapshot = record;
    }
    if (handler) {
        handler();
    }
    publish(snapshot);
    return true;
}

bool JobService::cancellationRequested(JobId id) const {
    auto found = job(id);
    return found && found->cancellationRequested;
}

bool JobService::isTerminal(JobId id) const {
    auto found = job(id);
    return !found || terminal(found->status);
}

bool JobService::ready(JobId id) const {
    auto found = job(id);
    if (!found) {
        return false;
    }
    for (JobId dependency : found->dependencies) {
        auto dependencyJob = job(dependency);
        if (!dependencyJob || dependencyJob->status != JobStatus::Succeeded) {
            return false;
        }
    }
    return true;
}

void JobService::applyExecutionEvent(const ExecutionEvent& event) {
    if (event.jobId == 0) {
        return;
    }
    switch (event.kind) {
    case ExecutionEventKind::Started:
        markRunning(event.jobId);
        if (event.progress >= 0.0) {
            updateProgress(event.jobId, event.progress);
        }
        return;
    case ExecutionEventKind::Stdout:
    case ExecutionEventKind::Stderr:
        appendOutput(event.jobId, event.text);
        return;
    case ExecutionEventKind::Progress:
        updateProgress(event.jobId, event.progress, event.text);
        return;
    case ExecutionEventKind::Finished:
        if (!event.text.empty()) {
            appendOutput(event.jobId, event.text);
        }
        if (cancellationRequested(event.jobId)) {
            cancel(event.jobId, "Job cancelled");
        } else {
            complete(event.jobId, event.exitCode == 0);
        }
        return;
    }
}

void JobService::applyExecutionEvents(const std::vector<ExecutionEvent>& events) {
    for (const ExecutionEvent& event : events) {
        applyExecutionEvent(event);
    }
}

std::optional<JobRecord> JobService::job(JobId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = jobs_.find(id);
    return it == jobs_.end() ? std::nullopt : std::optional<JobRecord>(it->second);
}

std::optional<JobRecord> JobService::wait(JobId id) const {
    std::unique_lock<std::mutex> lock(mutex_);
    changed_.wait(lock, [this, id] {
        auto it = jobs_.find(id);
        return it == jobs_.end() || terminal(it->second.status);
    });
    auto it = jobs_.find(id);
    return it == jobs_.end() ? std::nullopt : std::optional<JobRecord>(it->second);
}

std::vector<JobRecord> JobService::jobs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<JobRecord> result;
    for (const auto& [id, job] : jobs_) {
        (void)id;
        result.push_back(job);
    }
    return result;
}

void JobService::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    jobs_.clear();
    cancelHandlers_.clear();
    changed_.notify_all();
}

std::uint64_t JobService::onDidChangeJob(EventBus<JobChangeEvent>::Listener listener) {
    return onDidChange_.subscribe(std::move(listener));
}

void JobService::removeJobListener(std::uint64_t listenerId) {
    onDidChange_.unsubscribe(listenerId);
}

void JobService::publish(const JobRecord& job) {
    onDidChange_.publish({.job = job});
}

bool JobService::update(JobId id, const std::function<void(JobRecord&)>& update) {
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
    publish(snapshot);
    return true;
}

std::string toString(JobKind kind) {
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

std::string toString(JobStatus status) {
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

Json toJson(const JobRecord& job) {
    Json::Array dependencies;
    for (JobId dependency : job.dependencies) {
        dependencies.push_back(Json(static_cast<std::int64_t>(dependency)));
    }
    return Json::object({
        {"id", Json(static_cast<std::int64_t>(job.id))},
        {"kind", Json(toString(job.kind))},
        {"status", Json(toString(job.status))},
        {"title", Json(job.title)},
        {"message", Json(job.message)},
        {"output", Json(job.output)},
        {"progress", Json(job.progress)},
        {"cancellable", Json(job.cancellable)},
        {"cancellationRequested", Json(job.cancellationRequested)},
        {"dependencies", Json::array(std::move(dependencies))},
        {"data", job.data},
    });
}

Json toJson(const std::vector<JobRecord>& jobs) {
    Json::Array values;
    for (const JobRecord& job : jobs) {
        values.push_back(toJson(job));
    }
    return Json::array(std::move(values));
}

}
