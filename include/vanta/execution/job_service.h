#pragma once

#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "vanta/core/event.h"
#include "vanta/core/value.h"

namespace vanta {

using JobId = std::uint64_t;

class JobService;

enum class JobKind {
    Generic,
    Initialization,
    Index,
    Build,
    Test,
    Run,
    Agent,
    Plugin,
};

enum class JobStatus {
    Pending,
    Running,
    Succeeded,
    Failed,
    Cancelled,
};

struct JobRecord {
    JobId id = 0;
    JobKind kind = JobKind::Generic;
    JobStatus status = JobStatus::Pending;
    std::string title;
    std::string message;
    std::string output;
    double progress = -1.0;
    bool cancellable = false;
    bool cancellation_requested = false;
    std::vector<JobId> dependencies;
    std::optional<Value> payload;
};

struct JobRequest {
    JobKind kind = JobKind::Generic;
    std::string title;
    std::vector<JobId> dependencies;
    bool cancellable = false;
    std::optional<Value> payload;
};

struct JobResult {
    bool success = true;
    std::string message;
    std::optional<Value> payload;
};

enum class JobThread {
    Worker,
    Main,
};

using JobTask = std::function<void()>;
using JobDispatch = std::function<void(JobTask)>;

struct JobDispatcher {
    JobDispatch worker;
    JobDispatch main;

    void Dispatch(JobThread thread, JobTask task) const;
};

struct JobChangeEvent {
    JobRecord job;
};

class JobContext {
public:
    JobContext(JobService& service, JobId id);

    JobId Id() const;
    bool CancellationRequested() const;
    void Report(double progress, std::string message = {}) const;
    void AppendOutput(const std::string& output) const;
    void SetPayload(Value payload) const;

private:
    JobService* service_ = nullptr;
    JobId id_ = 0;
};

using JobFunction = std::function<JobResult(JobContext&)>;

class JobHandle {
public:
    JobHandle() = default;
    JobHandle(JobService& service, JobId id);

    JobId Id() const;
    bool Valid() const;
    bool Cancel();
    bool Terminate();
    std::optional<JobRecord> Record() const;
    std::optional<JobRecord> Wait() const;
    std::optional<JobRecord> Wait(std::chrono::milliseconds timeout) const;

private:
    JobService* service_ = nullptr;
    JobId id_ = 0;
};

class JobService {
public:
    static constexpr const char* kServiceId = "vanta.jobs";

    explicit JobService(JobDispatcher dispatcher);

    JobId Create(JobKind kind, std::string title, std::vector<JobId> dependencies = {});
    JobId Start(JobKind kind, std::string title, std::vector<JobId> dependencies = {});
    JobHandle Submit(JobRequest request, JobFunction function, JobThread thread = JobThread::Worker);
    JobHandle Submit(JobId id, JobFunction function, JobThread thread = JobThread::Worker);
    void MarkRunning(JobId id, std::string message = {});
    void UpdateProgress(JobId id, double progress, std::string message = {});
    void AppendOutput(JobId id, const std::string& output);
    void SetCancellable(JobId id, bool cancellable);
    void SetCancelHandler(JobId id, std::function<void()> handler);
    void SetPayload(JobId id, Value payload);
    void Complete(JobId id, bool success, std::string message = {});
    void Cancel(JobId id, std::string message = {});
    bool RequestCancel(JobId id);
    bool RequestCancelAll();
    bool Terminate(JobId id, std::string message = {});
    void TerminateAll(std::string message = {});
    bool CancellationRequested(JobId id) const;
    bool IsTerminal(JobId id) const;
    bool Ready(JobId id) const;
    std::optional<JobRecord> Job(JobId id) const;
    std::optional<JobRecord> Wait(JobId id) const;
    std::optional<JobRecord> Wait(JobId id, std::chrono::milliseconds timeout) const;
    std::vector<JobRecord> Jobs() const;
    void Clear();

    std::uint64_t OnDidChangeJob(EventBus<JobChangeEvent>::Listener listener);
    void RemoveJobListener(std::uint64_t listener_id);

private:
    void Publish(const JobRecord& job);
    bool Update(JobId id, const std::function<void(JobRecord&)>& update);

    JobId next_job_id_ = 1;
    std::map<JobId, JobRecord> jobs_;
    std::map<JobId, std::function<void()>> cancel_handlers_;
    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    EventBus<JobChangeEvent> on_did_change_;
    JobDispatcher dispatcher_;
};

JobDispatcher InlineJobDispatcher();

std::string ToString(JobKind kind);
std::string ToString(JobStatus status);

}
