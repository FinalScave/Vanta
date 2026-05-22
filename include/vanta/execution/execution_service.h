#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vanta/core/registration.h"
#include "vanta/execution/execution_protocol.h"
#include "vanta/core/value.h"

namespace vanta {

class WorkspaceContext;
class JobService;

enum class ExecutionTargetKind {
    Local,
    Device,
    Remote,
    Container,
    Custom,
};

struct ExecutionTarget {
    std::string id;
    std::string executor_id;
    std::string name;
    ExecutionTargetKind kind = ExecutionTargetKind::Custom;
    std::vector<std::string> capabilities;
};

struct ExecutionRequest {
    std::string executable;
    std::vector<std::string> arguments;
    std::filesystem::path working_directory;
    JobId job_id = 0;
};

struct ExecutionResult {
    int exit_code = -1;
    std::string output;
    JobId job_id = 0;
    std::vector<ExecutionEvent> events;
};

enum class ExecutionStatus {
    Pending,
    Running,
    Succeeded,
    Failed,
    Cancelled,
};

struct ExecutionState;

class ExecutionHandle {
public:
    ExecutionHandle() = default;
    explicit ExecutionHandle(std::shared_ptr<ExecutionState> state);

    std::uint64_t Id() const;
    JobId JobIdValue() const;
    ExecutionStatus Status() const;
    bool Running() const;
    void Cancel();
    void Terminate();
    ExecutionResult Wait();
    std::optional<ExecutionResult> ResultValue() const;
    std::vector<ExecutionEvent> EventsValue() const;
    bool Valid() const;

private:
    std::shared_ptr<ExecutionState> state_;
};

class ExecutionProvider {
public:
    virtual ~ExecutionProvider() = default;

    virtual std::string Id() const = 0;
    virtual std::vector<ExecutionTarget> Targets(WorkspaceContext& context) const = 0;
    virtual ExecutionHandle Start(
        WorkspaceContext& context,
        const ExecutionRequest& request,
        const ExecutionTarget& target,
        ExecutionEventCallback on_event = {}) const = 0;
    virtual ExecutionResult Execute(
        WorkspaceContext& context,
        const ExecutionRequest& request,
        const ExecutionTarget& target,
        ExecutionEventCallback on_event = {}) const;
};

class ExecutionService {
public:
    static constexpr const char* kServiceId = "vanta.execution";

    RegistrationHandle RegisterProvider(std::unique_ptr<ExecutionProvider> provider);
    void RemoveProvider(const std::string& provider_id);
    std::vector<std::string> ProviderIds() const;
    std::vector<ExecutionTarget> Targets(WorkspaceContext& context) const;
    ExecutionHandle Start(
        WorkspaceContext& context,
        const ExecutionRequest& request,
        const ExecutionTarget& target,
        ExecutionEventCallback on_event = {}) const;
    ExecutionResult Execute(
        WorkspaceContext& context,
        const ExecutionRequest& request,
        const ExecutionTarget& target,
        ExecutionEventCallback on_event = {}) const;

private:
    const ExecutionProvider* ProviderFor(const ExecutionRequest& request, const ExecutionTarget& target) const;

    std::map<std::string, std::unique_ptr<ExecutionProvider>> providers_;
};

void RegisterDefaultExecutionProviders(ExecutionService& service);
void ApplyExecutionEventToJob(JobService& jobs, const ExecutionEvent& event);
std::string ToString(ExecutionTargetKind kind);
std::string ToString(ExecutionStatus status);

}
