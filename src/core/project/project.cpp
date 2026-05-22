#include "vanta/project/project.h"

#include <algorithm>
#include <map>
#include <utility>

#include "vanta/workspace/workspace_context.h"

namespace vanta {

bool ProjectModel::HasFacet(const std::string& type) const {
    for (const ProjectFacet& facet : facets) {
        if (facet.type == type) {
            return true;
        }
    }
    return false;
}

bool ProjectModel::HasAttachment(const std::string& id) const {
    return Attachment(id) != nullptr;
}

const ProjectAttachment* ProjectModel::Attachment(const std::string& id) const {
    for (const auto& attachment : attachments) {
        if (attachment != nullptr && attachment->Id() == id) {
            return attachment.get();
        }
    }
    return nullptr;
}

std::vector<const ProjectAttachment*> ProjectModel::Attachments() const {
    std::vector<const ProjectAttachment*> result;
    for (const auto& attachment : attachments) {
        if (attachment != nullptr) {
            result.push_back(attachment.get());
        }
    }
    return result;
}

Value ProjectAttachment::Projection() const {
    return Value::ObjectValue();
}

std::string SingleFileModel::Id() const {
    return kAttachmentId;
}

std::string SingleFileModel::Kind() const {
    return kAttachmentKind;
}

std::string SingleFileModel::Title() const {
    return "Single File";
}

Value SingleFileModel::Projection() const {
    return Value::ObjectValue({
        {"file", Value(file.ToUri().ToString())},
        {"languageId", Value(language_id)},
        {"workingDirectory", Value(working_directory.string())},
    });
}

ProjectModelBuilder::ProjectModelBuilder(ProjectOrigin origin, VirtualFile root) {
    model_.origin = origin;
    model_.root = std::move(root);
}

ProjectOrigin ProjectModelBuilder::Origin() const {
    return model_.origin;
}

const VirtualFile& ProjectModelBuilder::Root() const {
    return model_.root;
}

bool ProjectModelBuilder::Empty() const {
    return model_.modules.empty() && model_.facets.empty() && model_.attachments.empty();
}

void ProjectModelBuilder::AddModule(ProjectModule module) {
    model_.modules.push_back(std::move(module));
}

void ProjectModelBuilder::AddFacet(ProjectFacet facet) {
    if (facet.id.empty() && !facet.type.empty()) {
        facet.id = facet.type;
    }
    model_.facets.push_back(std::move(facet));
}

void ProjectModelBuilder::AddFacetToPrimaryModule(ProjectFacet facet) {
    if (model_.modules.empty()) {
        return;
    }
    if (facet.id.empty() && !facet.type.empty()) {
        facet.id = facet.type;
    }
    model_.modules.front().facets.push_back(std::move(facet));
}

void ProjectModelBuilder::AddAttachment(std::unique_ptr<ProjectAttachment> attachment) {
    if (attachment == nullptr || attachment->Id().empty()) {
        return;
    }
    const std::string id = attachment->Id();
    for (auto& existing : model_.attachments) {
        if (existing != nullptr && existing->Id() == id) {
            existing = std::move(attachment);
            return;
        }
    }
    model_.attachments.push_back(std::move(attachment));
}

const ProjectModel& ProjectModelBuilder::Preview() const {
    return model_;
}

ProjectModel ProjectModelBuilder::Build() {
    return std::move(model_);
}

struct Project::Components {
    struct Entry {
        std::unique_ptr<Component> component;
        bool attached = false;
        bool restored = false;
        bool project_opened = false;
    };

    void Bind(std::unique_ptr<Component> component);
    bool Unbind(const std::string& id);
    Component* Get(const std::string& id);
    const Component* Get(const std::string& id) const;
    std::vector<std::string> Ids() const;
    void AttachAll(WorkspaceContext& context);
    void RestoreAll(const ProjectState& state);
    void OpenProject(Project& project);
    void ProjectChanged(Project& project);
    ProjectState SaveAll(const ProjectState& previous = {}) const;
    void CloseProject(Project& project);
    void DetachAll();
    void ReconcileProviders(const ProjectModel& model, const std::vector<ProjectComponentProvider>& providers);

private:
    bool AttachEntry(const std::string& id, Entry& entry);
    void RestoreEntry(const std::string& id, Entry& entry, const ProjectState& state);
    void OpenEntry(Entry& entry, Project& project);
    void ProjectChangedEntry(Entry& entry, Project& project);
    void CloseEntry(Entry& entry, Project& project);
    void DetachEntry(const std::string& id, Entry& entry);

    std::map<std::string, Entry> components_;
    std::map<std::string, bool> active_provider_components_;
    WorkspaceContext* context_ = nullptr;
    ProjectState restored_state_;
    bool has_restored_state_ = false;
    Project* open_project_ = nullptr;
    bool project_open_ = false;
};

Project::Project()
    : components_(std::make_unique<Components>()) {}

Project::Project(Project&&) noexcept = default;

Project& Project::operator=(Project&&) noexcept = default;

Project::~Project() = default;

const ProjectModel& Project::Model() const {
    return model_;
}

void Project::ReplaceModel(ProjectModel model) {
    model_ = std::move(model);
}

void Project::BindComponent(std::unique_ptr<Component> component) {
    components_->Bind(std::move(component));
}

bool Project::UnbindComponent(const std::string& id) {
    return components_->Unbind(id);
}

Component* Project::GetComponent(const std::string& id) {
    return components_->Get(id);
}

const Component* Project::GetComponent(const std::string& id) const {
    return components_->Get(id);
}

std::vector<std::string> Project::ComponentIds() const {
    return components_->Ids();
}

void Project::AttachComponents(WorkspaceContext& context) {
    components_->AttachAll(context);
}

void Project::RestoreComponents(const ProjectState& state) {
    components_->RestoreAll(state);
}

void Project::ReconcileComponents(const std::vector<ProjectComponentProvider>& providers) {
    components_->ReconcileProviders(model_, providers);
}

void Project::OpenComponents() {
    components_->OpenProject(*this);
}

ProjectState Project::SaveComponents(const ProjectState& previous) const {
    return components_->SaveAll(previous);
}

void Project::CloseComponents() {
    components_->CloseProject(*this);
}

void Project::DetachComponents() {
    components_->DetachAll();
}

void Project::Components::Bind(std::unique_ptr<Component> component) {
    if (component == nullptr || component->Id().empty()) {
        return;
    }
    const std::string id = component->Id();
    Unbind(id);
    auto [it, inserted] = components_.emplace(id, Entry{.component = std::move(component)});
    (void)inserted;
    if (context_ != nullptr && !AttachEntry(id, it->second)) {
        components_.erase(it);
        return;
    }
    if (has_restored_state_) {
        RestoreEntry(id, it->second, restored_state_);
    }
    if (project_open_ && open_project_ != nullptr) {
        OpenEntry(it->second, *open_project_);
    }
}

bool Project::Components::Unbind(const std::string& id) {
    auto it = components_.find(id);
    if (it == components_.end()) {
        return false;
    }
    if (has_restored_state_ && it->second.component != nullptr) {
        try {
            restored_state_.component_states[id] = it->second.component->SaveState();
        } catch (...) {
        }
    }
    if (it->second.project_opened && open_project_ != nullptr) {
        CloseEntry(it->second, *open_project_);
    }
    DetachEntry(id, it->second);
    components_.erase(it);
    return true;
}

Component* Project::Components::Get(const std::string& id) {
    auto it = components_.find(id);
    return it == components_.end() ? nullptr : it->second.component.get();
}

const Component* Project::Components::Get(const std::string& id) const {
    auto it = components_.find(id);
    return it == components_.end() ? nullptr : it->second.component.get();
}

std::vector<std::string> Project::Components::Ids() const {
    std::vector<std::string> result;
    for (const auto& [id, entry] : components_) {
        (void)entry;
        result.push_back(id);
    }
    return result;
}

void Project::Components::AttachAll(WorkspaceContext& context) {
    context_ = &context;
    for (auto& [id, entry] : components_) {
        if (!entry.attached) {
            AttachEntry(id, entry);
        }
    }
}

void Project::Components::RestoreAll(const ProjectState& state) {
    restored_state_ = state;
    has_restored_state_ = true;
    for (auto& [id, entry] : components_) {
        if (!entry.restored) {
            RestoreEntry(id, entry, restored_state_);
        }
    }
}

void Project::Components::OpenProject(Project& project) {
    if (project_open_) {
        ProjectChanged(project);
        return;
    }
    project_open_ = true;
    open_project_ = &project;
    for (auto& [id, entry] : components_) {
        (void)id;
        OpenEntry(entry, project);
    }
}

void Project::Components::ProjectChanged(Project& project) {
    for (auto& [id, entry] : components_) {
        (void)id;
        ProjectChangedEntry(entry, project);
    }
}

ProjectState Project::Components::SaveAll(const ProjectState& previous) const {
    ProjectState state = has_restored_state_ ? restored_state_ : previous;
    for (const auto& [id, value] : previous.component_states) {
        if (!state.component_states.contains(id)) {
            state.component_states[id] = value;
        }
    }
    state.schema_version = 1;
    for (const auto& [id, entry] : components_) {
        try {
            state.component_states[id] = entry.component->SaveState();
        } catch (...) {
        }
    }
    return state;
}

void Project::Components::CloseProject(Project& project) {
    if (!project_open_) {
        return;
    }
    for (auto& [id, entry] : components_) {
        (void)id;
        CloseEntry(entry, project);
    }
    project_open_ = false;
    open_project_ = nullptr;
}

void Project::Components::DetachAll() {
    for (auto& [id, entry] : components_) {
        DetachEntry(id, entry);
    }
    context_ = nullptr;
}

void Project::Components::ReconcileProviders(const ProjectModel& model, const std::vector<ProjectComponentProvider>& providers) {
    for (auto it = active_provider_components_.begin(); it != active_provider_components_.end();) {
        const auto provider = std::find_if(providers.begin(), providers.end(), [&](const ProjectComponentProvider& value) {
            return value.id == it->first;
        });
        if (provider == providers.end() || !provider->match.Matches(model)) {
            Unbind(it->first);
            it = active_provider_components_.erase(it);
        } else {
            ++it;
        }
    }

    for (const ProjectComponentProvider& provider : providers) {
        if (!provider.match.Matches(model)) {
            continue;
        }
        if (active_provider_components_.contains(provider.id) || Get(provider.id) != nullptr) {
            continue;
        }
        std::unique_ptr<Component> component = provider.factory ? provider.factory() : nullptr;
        if (component == nullptr || component->Id() != provider.id) {
            continue;
        }
        Bind(std::move(component));
        active_provider_components_[provider.id] = true;
    }
}

bool Project::Components::AttachEntry(const std::string& id, Entry& entry) {
    if (entry.component == nullptr || entry.attached || context_ == nullptr) {
        return entry.attached;
    }
    try {
        entry.component->OnAttach(*context_);
        entry.attached = true;
    } catch (...) {
        if (context_ != nullptr) {
            context_->RemoveEventSubscriptions(id);
        }
    }
    return entry.attached;
}

void Project::Components::RestoreEntry(const std::string& id, Entry& entry, const ProjectState& state) {
    if (entry.component == nullptr || entry.restored || !entry.attached) {
        return;
    }
    entry.restored = true;
    auto found = state.component_states.find(id);
    if (found == state.component_states.end()) {
        return;
    }
    try {
        entry.component->RestoreState(found->second);
    } catch (...) {
    }
}

void Project::Components::OpenEntry(Entry& entry, Project& project) {
    if (entry.component == nullptr || entry.project_opened || !entry.attached) {
        return;
    }
    try {
        entry.component->OnOpenProject(project);
        entry.project_opened = true;
    } catch (...) {
    }
}

void Project::Components::ProjectChangedEntry(Entry& entry, Project& project) {
    if (entry.component == nullptr || !entry.project_opened) {
        return;
    }
    try {
        entry.component->OnProjectChanged(project);
    } catch (...) {
    }
}

void Project::Components::CloseEntry(Entry& entry, Project& project) {
    if (entry.component == nullptr || !entry.project_opened) {
        return;
    }
    try {
        entry.component->OnCloseProject(project);
    } catch (...) {
    }
    entry.project_opened = false;
}

void Project::Components::DetachEntry(const std::string& id, Entry& entry) {
    if (entry.component != nullptr && entry.attached) {
        try {
            entry.component->OnDetach();
        } catch (...) {
        }
        entry.attached = false;
    }
    if (context_ != nullptr) {
        context_->RemoveEventSubscriptions(id);
    }
}

}
