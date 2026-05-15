#include "vanta/project/project.h"

namespace vanta {

const ProjectModel& Project::model() const {
    return model_;
}

void Project::setModel(ProjectModel model) {
    model_ = std::move(model);
}

ComponentRegistry& Project::components() {
    return components_;
}

const ComponentRegistry& Project::components() const {
    return components_;
}

Component* Project::getComponent(const std::string& id) {
    return components_.get(id);
}

const Component* Project::getComponent(const std::string& id) const {
    return components_.get(id);
}

}
