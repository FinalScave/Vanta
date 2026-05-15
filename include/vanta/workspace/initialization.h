#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vanta/platform/async.h"
#include "vanta/platform/json.h"

namespace vanta {

enum class WorkspaceInitializationStage {
    WorkspaceOpened,
    ProjectModelResolved,
    ComponentsReady,
    FileIndexReady,
    LanguageServicesReady,
    BuildModelReady,
    AgentContextReady,
};

enum class WorkspaceInitializationStatus {
    Pending,
    Running,
    Completed,
    Failed,
};

struct WorkspaceInitializationStep {
    WorkspaceInitializationStage stage = WorkspaceInitializationStage::WorkspaceOpened;
    WorkspaceInitializationStatus status = WorkspaceInitializationStatus::Pending;
    std::string message;
    Json data;
};

struct WorkspaceInitializationChangeEvent {
    WorkspaceInitializationStep step;
};

class WorkspaceInitializationPipeline {
public:
    void reset();
    void start(WorkspaceInitializationStage stage, std::string message = {});
    void complete(WorkspaceInitializationStage stage, std::string message = {}, Json data = Json::object());
    void fail(WorkspaceInitializationStage stage, std::string message, Json data = Json::object());
    std::optional<WorkspaceInitializationStep> step(WorkspaceInitializationStage stage) const;
    std::vector<WorkspaceInitializationStep> steps() const;
    bool completed(WorkspaceInitializationStage stage) const;
    std::uint64_t onDidChangeStep(EventBus<WorkspaceInitializationChangeEvent>::Listener listener);
    void removeStepListener(std::uint64_t listenerId);

private:
    WorkspaceInitializationStep& ensure(WorkspaceInitializationStage stage);
    void publish(const WorkspaceInitializationStep& step);

    std::map<WorkspaceInitializationStage, WorkspaceInitializationStep> steps_;
    EventBus<WorkspaceInitializationChangeEvent> onDidChange_;
};

std::string toString(WorkspaceInitializationStage stage);
std::string toString(WorkspaceInitializationStatus status);
Json toJson(const WorkspaceInitializationStep& step);
Json toJson(const std::vector<WorkspaceInitializationStep>& steps);

}
