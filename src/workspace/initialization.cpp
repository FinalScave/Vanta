#include "vanta/workspace/initialization.h"

#include <cstdint>
#include <utility>

namespace vanta {
namespace {

std::vector<WorkspaceInitializationStage> orderedStages() {
    return {
        WorkspaceInitializationStage::WorkspaceOpened,
        WorkspaceInitializationStage::ProjectModelResolved,
        WorkspaceInitializationStage::ComponentsReady,
        WorkspaceInitializationStage::FileIndexReady,
        WorkspaceInitializationStage::LanguageServicesReady,
        WorkspaceInitializationStage::BuildModelReady,
        WorkspaceInitializationStage::AgentContextReady,
    };
}

}

void WorkspaceInitializationPipeline::reset() {
    steps_.clear();
    for (WorkspaceInitializationStage stage : orderedStages()) {
        ensure(stage);
    }
}

void WorkspaceInitializationPipeline::start(WorkspaceInitializationStage stage, std::string message) {
    WorkspaceInitializationStep& value = ensure(stage);
    value.status = WorkspaceInitializationStatus::Running;
    value.message = std::move(message);
    value.data = Json::object();
    publish(value);
}

void WorkspaceInitializationPipeline::complete(WorkspaceInitializationStage stage, std::string message, Json data) {
    WorkspaceInitializationStep& value = ensure(stage);
    value.status = WorkspaceInitializationStatus::Completed;
    value.message = std::move(message);
    value.data = std::move(data);
    publish(value);
}

void WorkspaceInitializationPipeline::fail(WorkspaceInitializationStage stage, std::string message, Json data) {
    WorkspaceInitializationStep& value = ensure(stage);
    value.status = WorkspaceInitializationStatus::Failed;
    value.message = std::move(message);
    value.data = std::move(data);
    publish(value);
}

std::optional<WorkspaceInitializationStep> WorkspaceInitializationPipeline::step(WorkspaceInitializationStage stage) const {
    auto it = steps_.find(stage);
    return it == steps_.end() ? std::nullopt : std::optional<WorkspaceInitializationStep>(it->second);
}

std::vector<WorkspaceInitializationStep> WorkspaceInitializationPipeline::steps() const {
    std::vector<WorkspaceInitializationStep> values;
    for (WorkspaceInitializationStage stage : orderedStages()) {
        auto it = steps_.find(stage);
        if (it != steps_.end()) {
            values.push_back(it->second);
        }
    }
    return values;
}

bool WorkspaceInitializationPipeline::completed(WorkspaceInitializationStage stage) const {
    auto value = step(stage);
    return value && value->status == WorkspaceInitializationStatus::Completed;
}

std::uint64_t WorkspaceInitializationPipeline::onDidChangeStep(EventBus<WorkspaceInitializationChangeEvent>::Listener listener) {
    return onDidChange_.subscribe(std::move(listener));
}

void WorkspaceInitializationPipeline::removeStepListener(std::uint64_t listenerId) {
    onDidChange_.unsubscribe(listenerId);
}

WorkspaceInitializationStep& WorkspaceInitializationPipeline::ensure(WorkspaceInitializationStage stage) {
    WorkspaceInitializationStep& value = steps_[stage];
    value.stage = stage;
    if (!value.data.isObject()) {
        value.data = Json::object();
    }
    return value;
}

void WorkspaceInitializationPipeline::publish(const WorkspaceInitializationStep& step) {
    onDidChange_.publish({.step = step});
}

std::string toString(WorkspaceInitializationStage stage) {
    switch (stage) {
    case WorkspaceInitializationStage::WorkspaceOpened:
        return "workspaceOpened";
    case WorkspaceInitializationStage::ProjectModelResolved:
        return "projectModelResolved";
    case WorkspaceInitializationStage::ComponentsReady:
        return "componentsReady";
    case WorkspaceInitializationStage::FileIndexReady:
        return "fileIndexReady";
    case WorkspaceInitializationStage::LanguageServicesReady:
        return "languageServicesReady";
    case WorkspaceInitializationStage::BuildModelReady:
        return "buildModelReady";
    case WorkspaceInitializationStage::AgentContextReady:
        return "agentContextReady";
    }
    return "workspaceOpened";
}

std::string toString(WorkspaceInitializationStatus status) {
    switch (status) {
    case WorkspaceInitializationStatus::Pending:
        return "pending";
    case WorkspaceInitializationStatus::Running:
        return "running";
    case WorkspaceInitializationStatus::Completed:
        return "completed";
    case WorkspaceInitializationStatus::Failed:
        return "failed";
    }
    return "pending";
}

Json toJson(const WorkspaceInitializationStep& step) {
    return Json::object({
        {"stage", Json(toString(step.stage))},
        {"status", Json(toString(step.status))},
        {"message", Json(step.message)},
        {"data", step.data},
    });
}

Json toJson(const std::vector<WorkspaceInitializationStep>& steps) {
    Json::Array values;
    for (const WorkspaceInitializationStep& step : steps) {
        values.push_back(toJson(step));
    }
    return Json::array(std::move(values));
}

}
