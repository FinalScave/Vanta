#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vanta/core/diagnostic.h"
#include "vanta/core/registration.h"
#include "vanta/execution/execution_service.h"
#include "vanta/execution/execution_protocol.h"

namespace vanta {

struct BuildState;
class WorkspaceContext;
struct ProjectModel;

struct BuildEnvironment {
    std::string provider_id;
    bool detected = false;
    std::filesystem::path build_directory;
};

enum class BuildRequestKind {
    Build,
    Test,
};

struct BuildRequest {
    BuildRequestKind kind = BuildRequestKind::Build;
    std::string provider_id;
    std::string profile_id;
    std::string target_id;
    std::string execution_target_id;
    std::filesystem::path build_directory_override;
    JobId job_id = 0;
};

struct BuildStep {
    std::string title;
    ExecutionRequest request;
    bool parse_diagnostics = true;
    std::filesystem::path diagnostic_base_directory;
};

struct BuildPlan {
    std::string provider_id;
    std::string title;
    std::vector<BuildStep> steps;
};

struct BuildResult {
    int exit_code = -1;
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

    JobId JobIdValue() const;
    JobHandle JobHandleValue() const;
    BuildStatus Status() const;
    bool Running() const;
    void Cancel();
    void Terminate();
    BuildResult Wait();
    std::optional<BuildResult> ResultValue() const;
    std::vector<ExecutionEvent> EventsValue() const;
    bool Valid() const;

private:
    JobHandle job_;
    std::shared_ptr<BuildState> state_;
};

class BuildProvider {
public:
    virtual ~BuildProvider() = default;

    virtual std::string Id() const = 0;
    virtual BuildEnvironment Detect(WorkspaceContext& context, const ProjectModel& project) const = 0;
    virtual BuildPlan Plan(WorkspaceContext& context, const BuildRequest& request) const = 0;
};

class BuildService {
public:
    static constexpr const char* kServiceId = "vanta.build";

    virtual ~BuildService() = default;

    virtual RegistrationHandle RegisterProvider(std::unique_ptr<BuildProvider> provider) = 0;
    virtual void RemoveProvider(const std::string& provider_id) = 0;
    virtual std::vector<std::string> BuildProviderIds() const = 0;
    virtual BuildEnvironment Detect(WorkspaceContext& context, const ProjectModel& project) const = 0;
    virtual BuildHandle Start(
        WorkspaceContext& context,
        const BuildRequest& request,
        ExecutionEventCallback on_event = {}) const = 0;
    virtual BuildResult Run(
        WorkspaceContext& context,
        const BuildRequest& request,
        ExecutionEventCallback on_event = {}) const = 0;
};

std::string ToString(BuildRequestKind kind);
std::string ToString(BuildStatus status);

}
