#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "vanta/core/registration.h"
#include "vanta/core/value.h"

namespace vanta {

class Component;
class Project;
class WorkspaceContext;
struct ProjectModel;

struct ProjectComponentMatch {
    bool all_projects = false;
    std::vector<std::string> project_types;
    std::vector<std::string> facets;

    bool Matches(const ProjectModel& model) const;
};

struct ProjectComponentProvider {
    std::string id;
    ProjectComponentMatch match;
    std::function<std::unique_ptr<Component>()> factory;
};

struct ProjectState {
    int schema_version = 1;
    std::map<std::string, Value> component_states;
};

class Component {
public:
    virtual ~Component() = default;

    virtual std::string Id() const = 0;
    virtual void OnAttach(WorkspaceContext& context);
    virtual void RestoreState(const Value& state);
    virtual void OnOpenProject(Project& project);
    virtual void OnProjectChanged(Project& project);
    virtual Value SaveState() const;
    virtual void OnCloseProject(Project& project);
    virtual void OnDetach();
};

}
