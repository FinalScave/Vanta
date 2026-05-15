#pragma once

#include <any>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vanta/workspace/workspace.h"
#include "vanta/platform/json.h"
#include "vanta/vfs/virtual_file.h"

namespace vanta {

class WorkspaceContext;
class ProjectModelBuilder;

enum class ProjectOrigin {
    Workspace,
    SingleFile,
    Scratch,
};

struct ProjectFacet {
    std::string id;
    std::string type;
    std::string title;
    Json metadata;
};

struct ProjectModule {
    std::string id;
    std::string name;
    VirtualFile contentRoot;
    std::vector<VirtualFile> sourceRoots;
    std::vector<VirtualFile> excludedRoots;
    std::vector<ProjectFacet> facets;
};

struct ProjectAttachmentInfo {
    std::string id;
    std::string kind;
    std::string title;
    Json summary;
};

struct ProjectModel {
    ProjectOrigin origin = ProjectOrigin::Workspace;
    VirtualFile root;
    std::vector<ProjectModule> modules;
    std::vector<ProjectFacet> facets;
    std::vector<ProjectAttachmentInfo> attachmentInfos;
    std::map<std::string, std::any> attachments;

    bool hasFacet(const std::string& type) const;
    bool hasAttachment(const std::string& id) const;
    std::optional<ProjectAttachmentInfo> attachmentInfo(const std::string& id) const;

    template <class T>
    void setAttachment(ProjectAttachmentInfo info, T attachment) {
        if (info.id.empty()) {
            return;
        }
        attachments[info.id] = std::move(attachment);
        for (ProjectAttachmentInfo& existing : attachmentInfos) {
            if (existing.id == info.id) {
                existing = std::move(info);
                return;
            }
        }
        attachmentInfos.push_back(std::move(info));
    }

    template <class T>
    const T* attachment(const std::string& id) const {
        auto found = attachments.find(id);
        if (found == attachments.end()) {
            return nullptr;
        }
        return std::any_cast<T>(&found->second);
    }
};

struct SingleFileModel {
    static constexpr const char* attachmentId = "vanta.singleFile";
    static constexpr const char* attachmentKind = "singleFile";

    VirtualFile file;
    std::string languageId;
    std::filesystem::path workingDirectory;
    Json metadata;
};

class ProjectModelBuilder {
public:
    ProjectModelBuilder(ProjectOrigin origin, VirtualFile root);

    ProjectOrigin origin() const;
    const VirtualFile& root() const;
    bool empty() const;

    void addModule(ProjectModule module);
    void addFacet(ProjectFacet facet);
    void addFacetToPrimaryModule(ProjectFacet facet);

    template <class T>
    void setAttachment(ProjectAttachmentInfo info, T attachment) {
        model_.setAttachment(std::move(info), std::move(attachment));
    }

    const ProjectModel& preview() const;
    ProjectModel build();

private:
    ProjectModel model_;
};

class ProjectModelProvider {
public:
    virtual ~ProjectModelProvider() = default;

    virtual std::string id() const = 0;
    virtual void contribute(WorkspaceContext& context, ProjectModelBuilder& builder) const = 0;
};

class ProjectManager {
public:
    void addProvider(std::unique_ptr<ProjectModelProvider> provider);
    void removeProvider(const std::string& providerId);
    std::vector<std::string> providerIds() const;
    void setSingleFile(VirtualFile file, std::string languageId = {});
    void clearSingleFile();

    const ProjectModel& refresh(WorkspaceContext& context);
    const ProjectModel& current() const;
    bool hasProject() const;

private:
    ProjectModel model_;
    std::map<std::string, std::unique_ptr<ProjectModelProvider>> providers_;
    std::optional<SingleFileModel> singleFile_;
};

std::string toString(ProjectOrigin origin);
std::string primaryProjectType(const ProjectModel& model);
Json toJson(const SingleFileModel& model);

}
