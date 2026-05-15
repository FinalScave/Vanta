#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vanta/core/diagnostic.h"
#include "vanta/core/registration.h"
#include "vanta/execution/execution_service.h"
#include "vanta/execution/execution_protocol.h"
#include "vanta/platform/json.h"

namespace vanta {

struct BuildState;
class WorkspaceContext;

struct BuildEnvironment {
    std::string providerId;
    bool detected = false;
    std::filesystem::path buildDirectory;
    Json metadata;
};

enum class BuildTaskKind {
    Build,
    Test,
};

struct BuildTask {
    BuildTaskKind kind = BuildTaskKind::Build;
    std::string providerId;
    std::string target;
    std::string executionTargetId;
    std::filesystem::path buildDirectory;
    JobId jobId = 0;
};

struct BuildStep {
    std::string title;
    ExecutionRequest request;
    bool parseDiagnostics = true;
};

struct BuildPlan {
    std::string providerId;
    std::string title;
    std::vector<BuildStep> steps;
    Json metadata;
};

struct BuildResult {
    int exitCode = -1;
    std::string output;
    std::vector<Diagnostic> diagnostics;
    std::vector<ExecutionEvent> events;
};

enum class BuildStatus {
    Pending,
    Running,
    Succeeded,
    Failed,
    Cancelled,
};

class BuildHandle {
public:
    BuildHandle() = default;
    BuildHandle(JobHandle job, std::shared_ptr<BuildState> state);

    JobId jobId() const;
    JobHandle jobHandle() const;
    BuildStatus status() const;
    bool running() const;
    void cancel();
    BuildResult wait();
    std::optional<BuildResult> result() const;
    std::vector<ExecutionEvent> events() const;
    bool valid() const;

private:
    JobHandle job_;
    std::shared_ptr<BuildState> state_;
};

class BuildProvider {
public:
    virtual ~BuildProvider() = default;

    virtual std::string id() const = 0;
    virtual BuildEnvironment detect(const std::filesystem::path& workspaceRoot) const = 0;
    virtual BuildPlan plan(const std::filesystem::path& workspaceRoot, const BuildTask& task) const = 0;
};

class BuildService {
public:
    virtual ~BuildService() = default;

    virtual void addProvider(std::unique_ptr<BuildProvider> provider) = 0;
    virtual RegistrationHandle registerProvider(std::unique_ptr<BuildProvider> provider) = 0;
    virtual void removeProvider(const std::string& providerId) = 0;
    virtual std::vector<std::string> buildProviderIds() const = 0;
    virtual BuildEnvironment detect(const std::filesystem::path& workspaceRoot) const = 0;
    virtual BuildHandle start(
        WorkspaceContext& context,
        const std::filesystem::path& workspaceRoot,
        const BuildTask& task,
        ExecutionEventCallback onEvent = {}) const = 0;
    virtual BuildResult run(
        WorkspaceContext& context,
        const std::filesystem::path& workspaceRoot,
        const BuildTask& task,
        ExecutionEventCallback onEvent = {}) const = 0;
};

class DefaultBuildService final : public BuildService {
public:
    void addProvider(std::unique_ptr<BuildProvider> provider) override;
    RegistrationHandle registerProvider(std::unique_ptr<BuildProvider> provider) override;
    void removeProvider(const std::string& providerId) override;
    std::vector<std::string> buildProviderIds() const override;
    BuildEnvironment detect(const std::filesystem::path& workspaceRoot) const override;
    BuildHandle start(
        WorkspaceContext& context,
        const std::filesystem::path& workspaceRoot,
        const BuildTask& task,
        ExecutionEventCallback onEvent = {}) const override;
    BuildResult run(
        WorkspaceContext& context,
        const std::filesystem::path& workspaceRoot,
        const BuildTask& task,
        ExecutionEventCallback onEvent = {}) const override;

private:
    const BuildProvider* chooseProvider(const std::filesystem::path& workspaceRoot, const BuildTask& task) const;

    std::map<std::string, std::unique_ptr<BuildProvider>> providers_;
};

std::string toString(BuildTaskKind kind);
std::string toString(BuildStatus status);

using BuildCancellationCheck = std::function<bool()>;
class AsyncRuntime;
class JobService;

using BuildOperation = std::function<BuildResult(ExecutionEventCallback, BuildCancellationCheck)>;

BuildHandle startBuildOperation(
    JobService& jobs,
    AsyncRuntime& runtime,
    JobId jobId,
    BuildOperation operation,
    ExecutionEventCallback onEvent = {});

}
