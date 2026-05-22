#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "vanta/core/value.h"
#include "vanta/project/component.h"
#include "vanta/vfs/virtual_file.h"

namespace vanta {

class ProjectModelBuilder;
class WorkspaceContext;

enum class ProjectOrigin {
    kWorkspace,
    kSingleFile,
    kScratch,
};

struct ProjectFacet {
    std::string id;
    std::string type;
    std::string title;
};

struct ProjectModule {
    std::string id;
    std::string name;
    VirtualFile content_root;
    std::vector<VirtualFile> source_roots;
    std::vector<VirtualFile> excluded_roots;
    std::vector<ProjectFacet> facets;
};

class ProjectAttachment {
public:
    virtual ~ProjectAttachment() = default;

    virtual std::string Id() const = 0;
    virtual std::string Kind() const = 0;
    virtual std::string Title() const = 0;
    virtual Value Projection() const;
};

struct ProjectModel {
    ProjectOrigin origin = ProjectOrigin::kWorkspace;
    VirtualFile root;
    std::vector<ProjectModule> modules;
    std::vector<ProjectFacet> facets;
    std::vector<std::unique_ptr<ProjectAttachment>> attachments;

    bool HasFacet(const std::string& type) const;
    bool HasAttachment(const std::string& id) const;
    const ProjectAttachment* Attachment(const std::string& id) const;
    std::vector<const ProjectAttachment*> Attachments() const;

    template <class T>
    const T* Attachment(const std::string& id) const {
        return dynamic_cast<const T*>(Attachment(id));
    }
};

struct SingleFileModel final : public ProjectAttachment {
    static constexpr const char* kAttachmentId = "vanta.singleFile";
    static constexpr const char* kAttachmentKind = "singleFile";

    std::string Id() const override;
    std::string Kind() const override;
    std::string Title() const override;
    Value Projection() const override;

    VirtualFile file;
    std::string language_id;
    std::filesystem::path working_directory;
};

class ProjectModelBuilder {
public:
    ProjectModelBuilder(ProjectOrigin origin, VirtualFile root);

    ProjectOrigin Origin() const;
    const VirtualFile& Root() const;
    bool Empty() const;

    void AddModule(ProjectModule module);
    void AddFacet(ProjectFacet facet);
    void AddFacetToPrimaryModule(ProjectFacet facet);
    void AddAttachment(std::unique_ptr<ProjectAttachment> attachment);

    const ProjectModel& Preview() const;
    ProjectModel Build();

private:
    ProjectModel model_;
};

struct ProjectView {
    std::string id;
    std::string title;
    std::string icon;
    int priority = 0;
};

namespace ProjectViewNodeKind {
inline constexpr std::string_view kGroup = "vanta.group";
inline constexpr std::string_view kFile = "vanta.file";
inline constexpr std::string_view kDirectory = "vanta.directory";
inline constexpr std::string_view kModule = "vanta.module";
}

struct ProjectViewNode {
    std::string id;
    std::string label;
    std::string description;
    std::string kind;
    std::string icon;
    VirtualFile file;
    bool has_file = false;
    bool has_children = false;
    bool synthetic = false;
};

class Project {
public:
    Project();
    Project(const Project&) = delete;
    Project& operator=(const Project&) = delete;
    Project(Project&&) noexcept;
    Project& operator=(Project&&) noexcept;
    ~Project();

    const ProjectModel& Model() const;

    Component* GetComponent(const std::string& id);
    const Component* GetComponent(const std::string& id) const;
    std::vector<std::string> ComponentIds() const;

    template <class T>
    T* GetComponent(const std::string& id) {
        return dynamic_cast<T*>(GetComponent(id));
    }

    template <class T>
    const T* GetComponent(const std::string& id) const {
        return dynamic_cast<const T*>(GetComponent(id));
    }

private:
    friend class ProjectManager;
    struct Components;

    void ReplaceModel(ProjectModel model);
    void BindComponent(std::unique_ptr<Component> component);
    bool UnbindComponent(const std::string& id);
    void AttachComponents(WorkspaceContext& context);
    void RestoreComponents(const ProjectState& state);
    void ReconcileComponents(const std::vector<ProjectComponentProvider>& providers);
    void OpenComponents();
    ProjectState SaveComponents(const ProjectState& previous = {}) const;
    void CloseComponents();
    void DetachComponents();

    ProjectModel model_;
    std::unique_ptr<Components> components_;
};

std::string ToString(ProjectOrigin origin);
std::string PrimaryProjectType(const ProjectModel& model);

}
