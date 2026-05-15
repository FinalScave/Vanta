#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vanta/execution/execution_protocol.h"
#include "vanta/platform/json.h"

namespace vanta {

class WorkspaceContext;

struct ExecutionTarget {
    std::string id;
    std::string executorId;
    std::string name;
    std::vector<std::string> capabilities;
    Json metadata;
};

struct ExecutionRequest {
    std::string executable;
    std::vector<std::string> arguments;
    std::filesystem::path workingDirectory;
    Json metadata;
    JobId jobId = 0;
};

struct ExecutionResult {
    int exitCode = -1;
    std::string output;
    JobId jobId = 0;
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

    std::uint64_t id() const;
    JobId jobId() const;
    ExecutionStatus status() const;
    bool running() const;
    void cancel();
    ExecutionResult wait();
    std::optional<ExecutionResult> result() const;
    std::vector<ExecutionEvent> events() const;
    bool valid() const;

private:
    std::shared_ptr<ExecutionState> state_;
};

class ExecutionProvider {
public:
    virtual ~ExecutionProvider() = default;

    virtual std::string id() const = 0;
    virtual std::vector<ExecutionTarget> targets(WorkspaceContext& context) const = 0;
    virtual ExecutionHandle start(
        WorkspaceContext& context,
        const ExecutionRequest& request,
        const ExecutionTarget& target,
        ExecutionEventCallback onEvent = {}) const = 0;
    virtual ExecutionResult execute(
        WorkspaceContext& context,
        const ExecutionRequest& request,
        const ExecutionTarget& target,
        ExecutionEventCallback onEvent = {}) const;
};

class ExecutionService {
public:
    void addProvider(std::unique_ptr<ExecutionProvider> provider);
    void removeProvider(const std::string& providerId);
    std::vector<std::string> providerIds() const;
    std::vector<ExecutionTarget> targets(WorkspaceContext& context) const;
    ExecutionHandle start(
        WorkspaceContext& context,
        const ExecutionRequest& request,
        const ExecutionTarget& target,
        ExecutionEventCallback onEvent = {}) const;
    ExecutionResult execute(
        WorkspaceContext& context,
        const ExecutionRequest& request,
        const ExecutionTarget& target,
        ExecutionEventCallback onEvent = {}) const;

private:
    const ExecutionProvider* providerFor(const ExecutionRequest& request, const ExecutionTarget& target) const;

    std::map<std::string, std::unique_ptr<ExecutionProvider>> providers_;
};

void registerDefaultExecutionProviders(ExecutionService& service);
std::string toString(ExecutionStatus status);
Json toJson(const ExecutionTarget& target);
Json toJson(const ExecutionResult& result);

}
