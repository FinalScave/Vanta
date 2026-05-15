#pragma once

#include <cstdint>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "vanta/platform/async.h"
#include "vanta/platform/json.h"

namespace vanta {

using JobId = std::uint64_t;

struct ExecutionEvent;
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
    bool cancellationRequested = false;
    std::vector<JobId> dependencies;
    Json data;
};

struct JobRequest {
    JobKind kind = JobKind::Generic;
    std::string title;
    std::vector<JobId> dependencies;
    bool cancellable = false;
    Json data;
};

struct JobResult {
    bool success = true;
    std::string message;
    Json data;
};

enum class JobThread {
    Worker,
    Main,
};

struct JobChangeEvent {
    JobRecord job;
};

class JobContext {
public:
    JobContext(JobService& service, JobId id);

    JobId id() const;
    bool cancellationRequested() const;
    void report(double progress, std::string message = {}) const;
    void appendOutput(const std::string& output) const;
    void setData(Json data) const;

private:
    JobService* service_ = nullptr;
    JobId id_ = 0;
};

using JobFunction = std::function<JobResult(JobContext&)>;

class JobHandle {
public:
    JobHandle() = default;
    JobHandle(JobService& service, JobId id);

    JobId id() const;
    bool valid() const;
    bool cancel();
    std::optional<JobRecord> record() const;
    std::optional<JobRecord> wait() const;

private:
    JobService* service_ = nullptr;
    JobId id_ = 0;
};

class JobService {
public:
    JobId create(JobKind kind, std::string title, std::vector<JobId> dependencies = {});
    JobId start(JobKind kind, std::string title, std::vector<JobId> dependencies = {});
    JobHandle submit(AsyncRuntime& runtime, JobRequest request, JobFunction function, JobThread thread = JobThread::Worker);
    JobHandle submit(AsyncRuntime& runtime, JobId id, JobFunction function, JobThread thread = JobThread::Worker);
    void markRunning(JobId id, std::string message = {});
    void updateProgress(JobId id, double progress, std::string message = {});
    void appendOutput(JobId id, const std::string& output);
    void setCancellable(JobId id, bool cancellable);
    void setCancelHandler(JobId id, std::function<void()> handler);
    void setData(JobId id, Json data);
    void complete(JobId id, bool success, std::string message = {});
    void cancel(JobId id, std::string message = {});
    bool requestCancel(JobId id);
    bool cancellationRequested(JobId id) const;
    bool isTerminal(JobId id) const;
    bool ready(JobId id) const;
    void applyExecutionEvent(const ExecutionEvent& event);
    void applyExecutionEvents(const std::vector<ExecutionEvent>& events);

    std::optional<JobRecord> job(JobId id) const;
    std::optional<JobRecord> wait(JobId id) const;
    std::vector<JobRecord> jobs() const;
    void clear();

    std::uint64_t onDidChangeJob(EventBus<JobChangeEvent>::Listener listener);
    void removeJobListener(std::uint64_t listenerId);

private:
    void publish(const JobRecord& job);
    bool update(JobId id, const std::function<void(JobRecord&)>& update);

    JobId nextJobId_ = 1;
    std::map<JobId, JobRecord> jobs_;
    std::map<JobId, std::function<void()>> cancelHandlers_;
    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    EventBus<JobChangeEvent> onDidChange_;
};

std::string toString(JobKind kind);
std::string toString(JobStatus status);
Json toJson(const JobRecord& job);
Json toJson(const std::vector<JobRecord>& jobs);

}
