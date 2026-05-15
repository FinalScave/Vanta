#include "vanta/project/component.h"

#include <algorithm>
#include <utility>

#include "vanta/workspace/workspace_context.h"
#include "vanta/workspace/workspace_runtime.h"
#include "vanta/project/project.h"

namespace vanta {
namespace {

bool containsString(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

}

bool ComponentMatch::matches(const ProjectModel& model) const {
    if (allProjects) {
        return true;
    }
    if (!projectTypes.empty()) {
        const std::string primaryType = primaryProjectType(model);
        if (containsString(projectTypes, primaryType) || containsString(projectTypes, toString(model.origin))) {
            return true;
        }
    }
    for (const std::string& facet : facets) {
        if (model.hasFacet(facet)) {
            return true;
        }
    }
    return false;
}

void ComponentContributionRegistry::add(ComponentContribution contribution) {
    if (contribution.id.empty() || !contribution.factory) {
        return;
    }
    contributions_[contribution.id] = std::move(contribution);
}

bool ComponentContributionRegistry::remove(const std::string& id) {
    return contributions_.erase(id) > 0;
}

std::optional<ComponentContribution> ComponentContributionRegistry::contribution(const std::string& id) const {
    auto it = contributions_.find(id);
    return it == contributions_.end() ? std::nullopt : std::optional<ComponentContribution>(it->second);
}

std::vector<ComponentContribution> ComponentContributionRegistry::list() const {
    std::vector<ComponentContribution> values;
    for (const auto& [id, contribution] : contributions_) {
        (void)id;
        values.push_back(contribution);
    }
    return values;
}

std::vector<ComponentContribution> ComponentContributionRegistry::matching(const ProjectModel& model) const {
    std::vector<ComponentContribution> values;
    for (const auto& [id, contribution] : contributions_) {
        (void)id;
        if (contribution.match.matches(model)) {
            values.push_back(contribution);
        }
    }
    return values;
}

void Component::onAttach(WorkspaceContext& context) {
    (void)context;
}

void Component::restoreState(const Json& state) {
    (void)state;
}

void Component::onOpenProject(Project& project) {
    (void)project;
}

void Component::onProjectChanged(Project& project) {
    (void)project;
}

Json Component::saveState() const {
    return Json::object();
}

void Component::onCloseProject(Project& project) {
    (void)project;
}

void Component::onDetach() {}

void ComponentRegistry::bind(std::unique_ptr<Component> component) {
    if (component == nullptr || component->id().empty()) {
        return;
    }
    const std::string id = component->id();
    unbind(id);
    auto [it, inserted] = components_.emplace(id, Entry{.component = std::move(component)});
    (void)inserted;
    if (context_ != nullptr && !attachEntry(id, it->second)) {
        return;
    }
    if (hasRestoredState_) {
        restoreEntry(id, it->second, restoredState_);
    }
    if (projectOpen_ && openProject_ != nullptr) {
        openEntry(it->second, *openProject_);
    }
}

bool ComponentRegistry::unbind(const std::string& id) {
    auto it = components_.find(id);
    if (it == components_.end()) {
        return false;
    }
    if (it->second.projectOpened && openProject_ != nullptr) {
        closeEntry(it->second, *openProject_);
    }
    detachEntry(id, it->second);
    components_.erase(it);
    return true;
}

Component* ComponentRegistry::get(const std::string& id) {
    auto it = components_.find(id);
    return it == components_.end() ? nullptr : it->second.component.get();
}

const Component* ComponentRegistry::get(const std::string& id) const {
    auto it = components_.find(id);
    return it == components_.end() ? nullptr : it->second.component.get();
}

std::vector<std::string> ComponentRegistry::ids() const {
    std::vector<std::string> result;
    for (const auto& [id, entry] : components_) {
        (void)entry;
        result.push_back(id);
    }
    return result;
}

void ComponentRegistry::rememberState(const ProjectState& state) {
    restoredState_ = state;
    hasRestoredState_ = true;
}

void ComponentRegistry::attachAll(WorkspaceContext& context) {
    context_ = &context;
    for (auto& [id, entry] : components_) {
        if (!entry.attached) {
            attachEntry(id, entry);
        }
    }
}

void ComponentRegistry::restoreAll(const ProjectState& state) {
    rememberState(state);
    for (auto& [id, entry] : components_) {
        if (!entry.restored) {
            restoreEntry(id, entry, restoredState_);
        }
    }
}

void ComponentRegistry::openProject(Project& project) {
    if (projectOpen_) {
        projectChanged(project);
        return;
    }
    projectOpen_ = true;
    openProject_ = &project;
    for (auto& [id, entry] : components_) {
        (void)id;
        openEntry(entry, project);
    }
}

void ComponentRegistry::projectChanged(Project& project) {
    for (auto& [id, entry] : components_) {
        (void)id;
        projectChangedEntry(entry, project);
    }
}

ProjectState ComponentRegistry::saveAll(const ProjectState& previous) const {
    ProjectState state = previous;
    state.schemaVersion = 1;
    for (const auto& [id, entry] : components_) {
        try {
            state.componentStates[id] = entry.component->saveState();
        } catch (...) {
        }
    }
    return state;
}

void ComponentRegistry::closeProject(Project& project) {
    if (!projectOpen_) {
        return;
    }
    for (auto& [id, entry] : components_) {
        (void)id;
        closeEntry(entry, project);
    }
    projectOpen_ = false;
    openProject_ = nullptr;
}

void ComponentRegistry::detachAll() {
    for (auto& [id, entry] : components_) {
        detachEntry(id, entry);
    }
    context_ = nullptr;
}

bool ComponentRegistry::attachEntry(const std::string& id, Entry& entry) {
    if (entry.component == nullptr || entry.attached || context_ == nullptr) {
        return entry.attached;
    }
    try {
        entry.component->onAttach(*context_);
        entry.attached = true;
    } catch (...) {
        if (context_ != nullptr) {
            context_->removeEventSubscriptions(id);
        }
    }
    return entry.attached;
}

void ComponentRegistry::restoreEntry(const std::string& id, Entry& entry, const ProjectState& state) {
    if (entry.component == nullptr || entry.restored || !entry.attached) {
        return;
    }
    entry.restored = true;
    auto found = state.componentStates.find(id);
    if (found == state.componentStates.end()) {
        return;
    }
    try {
        entry.component->restoreState(found->second);
    } catch (...) {
    }
}

void ComponentRegistry::openEntry(Entry& entry, Project& project) {
    if (entry.component == nullptr || entry.projectOpened || !entry.attached) {
        return;
    }
    try {
        entry.component->onOpenProject(project);
        entry.projectOpened = true;
    } catch (...) {
    }
}

void ComponentRegistry::projectChangedEntry(Entry& entry, Project& project) {
    if (entry.component == nullptr || !entry.projectOpened) {
        return;
    }
    try {
        entry.component->onProjectChanged(project);
    } catch (...) {
    }
}

void ComponentRegistry::closeEntry(Entry& entry, Project& project) {
    if (entry.component == nullptr || !entry.projectOpened) {
        return;
    }
    try {
        entry.component->onCloseProject(project);
    } catch (...) {
    }
    entry.projectOpened = false;
}

void ComponentRegistry::detachEntry(const std::string& id, Entry& entry) {
    if (entry.component != nullptr && entry.attached) {
        try {
            entry.component->onDetach();
        } catch (...) {
        }
        entry.attached = false;
    }
    if (context_ != nullptr) {
        context_->removeEventSubscriptions(id);
    }
}

}
