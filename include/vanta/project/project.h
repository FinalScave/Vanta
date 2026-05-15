#pragma once

#include <string>

#include "vanta/project/component.h"
#include "vanta/project/project_manager.h"

namespace vanta {

class Project {
public:
    const ProjectModel& model() const;
    void setModel(ProjectModel model);

    ComponentRegistry& components();
    const ComponentRegistry& components() const;
    Component* getComponent(const std::string& id);
    const Component* getComponent(const std::string& id) const;

    template <class T>
    T* getComponent(const std::string& id) {
        return components_.get<T>(id);
    }

    template <class T>
    const T* getComponent(const std::string& id) const {
        return components_.get<T>(id);
    }

private:
    ProjectModel model_;
    ComponentRegistry components_;
};

}
