#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vanta/core/event.h"
#include "vanta/core/registration.h"
#include "vanta/project/component.h"
#include "vanta/project/project.h"

namespace vanta {

class WorkspaceContext;
class Project;

class ProjectModelProvider {
public:
    virtual ~ProjectModelProvider() = default;

    virtual std::string Id() const = 0;
    virtual void Contribute(WorkspaceContext& context, ProjectModelBuilder& builder) const = 0;
};

struct ProjectViewChangeEvent {
    std::string provider_id;
    std::string view_id;
    std::string node_id;
};

class ProjectViewProvider {
public:
    virtual ~ProjectViewProvider() = default;

    virtual std::string Id() const = 0;
    virtual std::vector<ProjectView> Views(WorkspaceContext& context) const = 0;
    virtual std::vector<ProjectViewNode> TopLevelNodes(WorkspaceContext& context, const ProjectView& view) = 0;
    virtual std::vector<ProjectViewNode> Children(WorkspaceContext& context, const ProjectView& view, const ProjectViewNode& parent) = 0;
};

class ProjectManager {
public:
    static constexpr const char* kServiceId = "vanta.projects";

    ProjectManager();

    RegistrationHandle RegisterModelProvider(std::unique_ptr<ProjectModelProvider> provider);
    void RemoveModelProvider(const std::string& provider_id);
    std::vector<std::string> ModelProviderIds() const;
    RegistrationHandle RegisterViewProvider(std::unique_ptr<ProjectViewProvider> provider);
    void RemoveViewProvider(const std::string& provider_id);
    std::vector<std::string> ViewProviderIds() const;
    void SetSingleFile(VirtualFile file, std::string language_id = {});
    void ClearSingleFile();
    void BindComponent(Project& project, std::unique_ptr<Component> component) const;
    bool UnbindComponent(Project& project, const std::string& id) const;
    RegistrationHandle RegisterComponentProvider(ProjectComponentProvider provider);

    void BindDefaultComponents(Project& project) const;
    void AttachProject(WorkspaceContext& context, Project& project);
    void RestoreProject(Project& project, const ProjectState& state) const;
    const ProjectModel& Refresh(WorkspaceContext& context, Project& project);
    ProjectState SaveProject(const Project& project, const ProjectState& previous) const;
    void CloseProject(Project& project) const;
    void DetachProject(Project& project);
    std::vector<ProjectView> Views(WorkspaceContext& context) const;
    std::vector<ProjectViewNode> TopLevelNodes(WorkspaceContext& context, const std::string& view_id);
    std::vector<ProjectViewNode> Children(WorkspaceContext& context, const std::string& view_id, const ProjectViewNode& parent);
    std::uint64_t OnDidChangeViews(EventBus<ProjectViewChangeEvent>::Listener listener);
    void RemoveViewListener(std::uint64_t listener_id);
    void InvalidateViews(ProjectViewChangeEvent event = {});

private:
    std::vector<ProjectComponentProvider> ComponentProviders() const;
    void RemoveComponentProvider(const std::string& id, std::uint64_t registration_id);
    void ReconcileActiveProject();

    std::map<std::string, std::unique_ptr<ProjectModelProvider>> model_providers_;
    std::map<std::string, std::unique_ptr<ProjectViewProvider>> view_providers_;
    std::map<std::string, ProjectComponentProvider> component_providers_;
    std::map<std::string, std::uint64_t> component_provider_registrations_;
    EventBus<ProjectViewChangeEvent> view_events_;
    std::optional<SingleFileModel> single_file_;
    std::uint64_t next_component_provider_registration_ = 1;
    Project* active_project_ = nullptr;
};

}
