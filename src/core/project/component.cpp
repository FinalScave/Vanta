#include "vanta/project/component.h"

#include <algorithm>

#include "vanta/project/project.h"

namespace vanta {
namespace {

bool ContainsString(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

}

bool ProjectComponentMatch::Matches(const ProjectModel& model) const {
    if (all_projects) {
        return true;
    }
    if (!project_types.empty()) {
        const std::string primary_type = PrimaryProjectType(model);
        if (ContainsString(project_types, primary_type) || ContainsString(project_types, ToString(model.origin))) {
            return true;
        }
    }
    for (const std::string& facet : facets) {
        if (model.HasFacet(facet)) {
            return true;
        }
    }
    return false;
}

void Component::OnAttach(WorkspaceContext& context) {
    (void)context;
}

void Component::RestoreState(const Value& state) {
    (void)state;
}

void Component::OnOpenProject(Project& project) {
    (void)project;
}

void Component::OnProjectChanged(Project& project) {
    (void)project;
}

Value Component::SaveState() const {
    return Value::ObjectValue();
}

void Component::OnCloseProject(Project& project) {
    (void)project;
}

void Component::OnDetach() {}

}
