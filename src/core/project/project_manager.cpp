#include "vanta/project/project_manager.h"

#include <algorithm>
#include <set>
#include <utility>

#include "vanta/execution/run_configuration.h"
#include "vanta/language/language_service.h"
#include "vanta/project/project.h"
#include "vanta/workspace/workspace.h"
#include "vanta/workspace/workspace_context.h"

namespace vanta {
namespace {

bool ShouldSkipDirectory(const VirtualFile& file) {
    const std::string name = file.DisplayName();
    return name == ".git" || name == ".vanta" || name == "build" || name == ".cache";
}

bool IsDirectory(const VirtualFile& file) {
    return file.Valid() && file.Stat().kind == VirtualFileKind::Directory;
}

ProjectViewNode FileNode(const VirtualFile& file) {
    const bool directory = IsDirectory(file);
    std::string label = file.DisplayName();
    if (label.empty()) {
        label = file.ToUri().ToString();
    }
    return {
        .id = file.ToUri().ToString(),
        .label = std::move(label),
        .kind = directory ? std::string(ProjectViewNodeKind::kDirectory) : std::string(ProjectViewNodeKind::kFile),
        .icon = directory ? "folder" : "file",
        .file = file,
        .has_file = file.Valid(),
        .has_children = directory,
        .synthetic = false,
    };
}

std::vector<VirtualFile> SortedVisibleChildren(const VirtualFile& directory) {
    std::vector<VirtualFile> children;
    for (const VirtualFile& child : directory.ListChildren()) {
        if (IsDirectory(child) && ShouldSkipDirectory(child)) {
            continue;
        }
        children.push_back(child);
    }
    std::sort(children.begin(), children.end(), [](const VirtualFile& left, const VirtualFile& right) {
        const bool left_directory = IsDirectory(left);
        const bool right_directory = IsDirectory(right);
        if (left_directory != right_directory) {
            return left_directory > right_directory;
        }
        return left.DisplayName() < right.DisplayName();
    });
    return children;
}

class FilesProjectViewProvider final : public ProjectViewProvider {
public:
    std::string Id() const override {
        return "vanta.files.projectViewProvider";
    }

    std::vector<ProjectView> Views(WorkspaceContext& context) const override {
        if (!context.WorkspaceOpen()) {
            return {};
        }
        return {{
            .id = "vanta.files",
            .title = "Files",
            .icon = "files",
            .priority = 0,
        }};
    }

    std::vector<ProjectViewNode> TopLevelNodes(WorkspaceContext& context, const ProjectView&) override {
        std::vector<ProjectViewNode> nodes;
        std::set<std::string> seen;
        const Project* project = context.CurrentProject();
        if (project != nullptr) {
            for (const ProjectModule& module : project->Model().modules) {
                if (!module.content_root.Valid() || !seen.insert(module.content_root.ToUri().ToString()).second) {
                    continue;
                }
                nodes.push_back(FileNode(module.content_root));
            }
        }
        if (nodes.empty() && context.CurrentWorkspace().RootFile().Valid()) {
            nodes.push_back(FileNode(context.CurrentWorkspace().RootFile()));
        }
        return nodes;
    }

    std::vector<ProjectViewNode> Children(WorkspaceContext&, const ProjectView&, const ProjectViewNode& parent) override {
        if (!parent.has_file || !IsDirectory(parent.file)) {
            return {};
        }
        std::vector<ProjectViewNode> nodes;
        for (const VirtualFile& child : SortedVisibleChildren(parent.file)) {
            nodes.push_back(FileNode(child));
        }
        return nodes;
    }
};

}

ProjectManager::ProjectManager() {
    RegisterViewProvider(std::make_unique<FilesProjectViewProvider>());
}

RegistrationHandle ProjectManager::RegisterModelProvider(std::unique_ptr<ProjectModelProvider> provider) {
    if (provider == nullptr || provider->Id().empty()) {
        return {};
    }
    const std::string id = provider->Id();
    model_providers_[id] = std::move(provider);
    return RegistrationHandle([this, id] {
        RemoveModelProvider(id);
    });
}

void ProjectManager::RemoveModelProvider(const std::string& provider_id) {
    model_providers_.erase(provider_id);
}

std::vector<std::string> ProjectManager::ModelProviderIds() const {
    std::vector<std::string> ids;
    for (const auto& [id, provider] : model_providers_) {
        (void)provider;
        ids.push_back(id);
    }
    return ids;
}

RegistrationHandle ProjectManager::RegisterViewProvider(std::unique_ptr<ProjectViewProvider> provider) {
    if (provider == nullptr || provider->Id().empty()) {
        return {};
    }
    const std::string id = provider->Id();
    view_providers_[id] = std::move(provider);
    InvalidateViews({.provider_id = id});
    return RegistrationHandle([this, id] {
        RemoveViewProvider(id);
    });
}

void ProjectManager::RemoveViewProvider(const std::string& provider_id) {
    const bool removed = view_providers_.erase(provider_id) > 0;
    if (removed) {
        InvalidateViews({.provider_id = provider_id});
    }
}

std::vector<std::string> ProjectManager::ViewProviderIds() const {
    std::vector<std::string> ids;
    for (const auto& [id, provider] : view_providers_) {
        (void)provider;
        ids.push_back(id);
    }
    return ids;
}

void ProjectManager::SetSingleFile(VirtualFile file, std::string language_id) {
    if (!file.Valid()) {
        ClearSingleFile();
        return;
    }
    std::filesystem::path working_directory;
    if (const auto local_path = file.LocalPath()) {
        working_directory = local_path->parent_path();
    }
    SingleFileModel model;
    model.file = std::move(file);
    model.language_id = std::move(language_id);
    model.working_directory = std::move(working_directory);
    single_file_ = std::move(model);
}

void ProjectManager::ClearSingleFile() {
    single_file_.reset();
}

void ProjectManager::BindComponent(Project& project, std::unique_ptr<Component> component) const {
    project.BindComponent(std::move(component));
}

bool ProjectManager::UnbindComponent(Project& project, const std::string& id) const {
    return project.UnbindComponent(id);
}

RegistrationHandle ProjectManager::RegisterComponentProvider(ProjectComponentProvider provider) {
    if (provider.id.empty() || !provider.factory) {
        return {};
    }
    const std::string id = provider.id;
    const std::uint64_t registration_id = next_component_provider_registration_++;
    component_providers_[id] = std::move(provider);
    component_provider_registrations_[id] = registration_id;
    ReconcileActiveProject();
    return RegistrationHandle([this, id, registration_id] {
        RemoveComponentProvider(id, registration_id);
    });
}

void ProjectManager::BindDefaultComponents(Project& project) const {
    if (project.GetComponent(ProjectRunConfigurations::kComponentId) == nullptr) {
        project.BindComponent(std::make_unique<ProjectRunConfigurations>());
    }
}

void ProjectManager::AttachProject(WorkspaceContext& context, Project& project) {
    active_project_ = &project;
    project.AttachComponents(context);
}

void ProjectManager::RestoreProject(Project& project, const ProjectState& state) const {
    project.RestoreComponents(state);
}

const ProjectModel& ProjectManager::Refresh(WorkspaceContext& context, Project& project) {
    Workspace& workspace = context.CurrentWorkspace();
    ProjectModelBuilder builder(
        single_file_ ? ProjectOrigin::kSingleFile : ProjectOrigin::kWorkspace,
        workspace.RootFile());
    if (single_file_) {
        if (single_file_->language_id.empty()) {
            single_file_->language_id = context.Languages().LanguageIdForFile(single_file_->file);
        }
        ProjectFacet facet{
            .id = single_file_->language_id,
            .type = single_file_->language_id,
            .title = single_file_->language_id,
        };
        builder.AddFacet(facet);
        builder.AddModule({
            .id = "single-file",
            .name = single_file_->file.DisplayName(),
            .content_root = workspace.RootFile(),
            .source_roots = {single_file_->file},
            .excluded_roots = {},
            .facets = {std::move(facet)},
        });
        builder.AddAttachment(std::make_unique<SingleFileModel>(*single_file_));
    } else {
        builder.AddModule({
            .id = "workspace",
            .name = workspace.Info().name,
            .content_root = workspace.RootFile(),
            .source_roots = {workspace.RootFile()},
            .excluded_roots = {},
            .facets = {},
        });
    }

    if (!single_file_) {
        for (const auto& [id, provider] : model_providers_) {
            (void)id;
            provider->Contribute(context, builder);
        }
    }

    project.ReplaceModel(builder.Build());
    project.ReconcileComponents(ComponentProviders());
    project.OpenComponents();
    return project.Model();
}

ProjectState ProjectManager::SaveProject(const Project& project, const ProjectState& previous) const {
    return project.SaveComponents(previous);
}

void ProjectManager::CloseProject(Project& project) const {
    project.CloseComponents();
}

void ProjectManager::DetachProject(Project& project) {
    project.DetachComponents();
    if (active_project_ == &project) {
        active_project_ = nullptr;
    }
}

std::vector<ProjectComponentProvider> ProjectManager::ComponentProviders() const {
    std::vector<ProjectComponentProvider> values;
    for (const auto& [id, provider] : component_providers_) {
        (void)id;
        values.push_back(provider);
    }
    return values;
}

void ProjectManager::RemoveComponentProvider(const std::string& id, std::uint64_t registration_id) {
    auto registration = component_provider_registrations_.find(id);
    if (registration == component_provider_registrations_.end() || registration->second != registration_id) {
        return;
    }
    component_provider_registrations_.erase(registration);
    component_providers_.erase(id);
    ReconcileActiveProject();
}

void ProjectManager::ReconcileActiveProject() {
    if (active_project_ != nullptr) {
        active_project_->ReconcileComponents(ComponentProviders());
    }
}

std::vector<ProjectView> ProjectManager::Views(WorkspaceContext& context) const {
    std::vector<ProjectView> result;
    std::set<std::string> seen;
    for (const auto& [id, provider] : view_providers_) {
        (void)id;
        for (ProjectView view : provider->Views(context)) {
            if (view.id.empty() || !seen.insert(view.id).second) {
                continue;
            }
            result.push_back(std::move(view));
        }
    }
    std::sort(result.begin(), result.end(), [](const ProjectView& left, const ProjectView& right) {
        if (left.priority != right.priority) {
            return left.priority < right.priority;
        }
        if (left.title != right.title) {
            return left.title < right.title;
        }
        return left.id < right.id;
    });
    return result;
}

std::vector<ProjectViewNode> ProjectManager::TopLevelNodes(WorkspaceContext& context, const std::string& view_id) {
    for (const auto& [id, provider] : view_providers_) {
        (void)id;
        for (const ProjectView& view : provider->Views(context)) {
            if (view.id == view_id) {
                return provider->TopLevelNodes(context, view);
            }
        }
    }
    return {};
}

std::vector<ProjectViewNode> ProjectManager::Children(WorkspaceContext& context, const std::string& view_id, const ProjectViewNode& parent) {
    for (const auto& [id, provider] : view_providers_) {
        (void)id;
        for (const ProjectView& view : provider->Views(context)) {
            if (view.id == view_id) {
                return provider->Children(context, view, parent);
            }
        }
    }
    return {};
}

std::uint64_t ProjectManager::OnDidChangeViews(EventBus<ProjectViewChangeEvent>::Listener listener) {
    return view_events_.Subscribe(std::move(listener));
}

void ProjectManager::RemoveViewListener(std::uint64_t listener_id) {
    view_events_.Unsubscribe(listener_id);
}

void ProjectManager::InvalidateViews(ProjectViewChangeEvent event) {
    view_events_.Publish(event);
}

std::string ToString(ProjectOrigin origin) {
    switch (origin) {
    case ProjectOrigin::kWorkspace:
        return "workspace";
    case ProjectOrigin::kSingleFile:
        return "singleFile";
    case ProjectOrigin::kScratch:
        return "scratch";
    }
    return "workspace";
}

std::string PrimaryProjectType(const ProjectModel& model) {
    if (model.origin == ProjectOrigin::kSingleFile) {
        return "singleFile";
    }
    if (!model.facets.empty()) {
        return model.facets.front().type;
    }
    if (!model.modules.empty()) {
        return "generic";
    }
    return "unknown";
}

}
