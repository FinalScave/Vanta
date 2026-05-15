#include "vanta/project/project_manager.h"

#include <utility>

#include "vanta/workspace/workspace_context.h"

namespace vanta {
namespace {

Json attachmentSummary(const SingleFileModel& model) {
    return Json::object({
        {"file", Json(model.file.toUri().string())},
        {"languageId", Json(model.languageId)},
        {"workingDirectory", Json(model.workingDirectory.string())},
    });
}

}

bool ProjectModel::hasFacet(const std::string& type) const {
    for (const ProjectFacet& facet : facets) {
        if (facet.type == type) {
            return true;
        }
    }
    return false;
}

bool ProjectModel::hasAttachment(const std::string& id) const {
    return attachments.contains(id);
}

std::optional<ProjectAttachmentInfo> ProjectModel::attachmentInfo(const std::string& id) const {
    for (const ProjectAttachmentInfo& info : attachmentInfos) {
        if (info.id == id) {
            return info;
        }
    }
    return std::nullopt;
}

ProjectModelBuilder::ProjectModelBuilder(ProjectOrigin origin, VirtualFile root) {
    model_.origin = origin;
    model_.root = std::move(root);
}

ProjectOrigin ProjectModelBuilder::origin() const {
    return model_.origin;
}

const VirtualFile& ProjectModelBuilder::root() const {
    return model_.root;
}

bool ProjectModelBuilder::empty() const {
    return model_.modules.empty() && model_.facets.empty() && model_.attachmentInfos.empty();
}

void ProjectModelBuilder::addModule(ProjectModule module) {
    model_.modules.push_back(std::move(module));
}

void ProjectModelBuilder::addFacet(ProjectFacet facet) {
    if (facet.id.empty() && !facet.type.empty()) {
        facet.id = facet.type;
    }
    model_.facets.push_back(std::move(facet));
}

void ProjectModelBuilder::addFacetToPrimaryModule(ProjectFacet facet) {
    if (model_.modules.empty()) {
        return;
    }
    if (facet.id.empty() && !facet.type.empty()) {
        facet.id = facet.type;
    }
    model_.modules.front().facets.push_back(std::move(facet));
}

const ProjectModel& ProjectModelBuilder::preview() const {
    return model_;
}

ProjectModel ProjectModelBuilder::build() {
    return std::move(model_);
}

void ProjectManager::addProvider(std::unique_ptr<ProjectModelProvider> provider) {
    if (provider == nullptr || provider->id().empty()) {
        return;
    }
    providers_[provider->id()] = std::move(provider);
}

void ProjectManager::removeProvider(const std::string& providerId) {
    providers_.erase(providerId);
}

std::vector<std::string> ProjectManager::providerIds() const {
    std::vector<std::string> ids;
    for (const auto& [id, provider] : providers_) {
        (void)provider;
        ids.push_back(id);
    }
    return ids;
}

void ProjectManager::setSingleFile(VirtualFile file, std::string languageId) {
    if (!file.valid()) {
        clearSingleFile();
        return;
    }
    std::filesystem::path workingDirectory;
    if (const auto localPath = file.localPath()) {
        workingDirectory = localPath->parent_path();
    }
    singleFile_ = SingleFileModel{
        .file = std::move(file),
        .languageId = std::move(languageId),
        .workingDirectory = std::move(workingDirectory),
        .metadata = Json::object(),
    };
}

void ProjectManager::clearSingleFile() {
    singleFile_.reset();
}

const ProjectModel& ProjectManager::refresh(WorkspaceContext& context) {
    Workspace& workspace = context.workspace();
    ProjectModelBuilder builder(
        singleFile_ ? ProjectOrigin::SingleFile : ProjectOrigin::Workspace,
        workspace.rootFile());
    if (singleFile_) {
        if (singleFile_->languageId.empty()) {
            singleFile_->languageId = context.languages().languageIdForFile(singleFile_->file);
        }
        ProjectFacet facet{
            .id = singleFile_->languageId,
            .type = singleFile_->languageId,
            .title = singleFile_->languageId,
            .metadata = Json::object(),
        };
        builder.addFacet(facet);
        builder.addModule({
            .id = "single-file",
            .name = singleFile_->file.displayName(),
            .contentRoot = workspace.rootFile(),
            .sourceRoots = {singleFile_->file},
            .excludedRoots = {},
            .facets = {std::move(facet)},
        });
        builder.setAttachment({
            .id = SingleFileModel::attachmentId,
            .kind = SingleFileModel::attachmentKind,
            .title = "Single File",
            .summary = attachmentSummary(*singleFile_),
        }, *singleFile_);
    } else {
        builder.addModule({
            .id = "workspace",
            .name = workspace.info().name,
            .contentRoot = workspace.rootFile(),
            .sourceRoots = {workspace.rootFile()},
            .excludedRoots = {},
            .facets = {},
        });
    }

    if (!singleFile_) {
        for (const auto& [id, provider] : providers_) {
            (void)id;
            provider->contribute(context, builder);
        }
    }

    model_ = builder.build();
    return model_;
}

const ProjectModel& ProjectManager::current() const {
    return model_;
}

bool ProjectManager::hasProject() const {
    return !model_.facets.empty() || !model_.attachmentInfos.empty();
}

std::string toString(ProjectOrigin origin) {
    switch (origin) {
    case ProjectOrigin::Workspace:
        return "workspace";
    case ProjectOrigin::SingleFile:
        return "singleFile";
    case ProjectOrigin::Scratch:
        return "scratch";
    }
    return "workspace";
}

std::string primaryProjectType(const ProjectModel& model) {
    if (model.origin == ProjectOrigin::SingleFile) {
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

Json toJson(const SingleFileModel& model) {
    return Json::object({
        {"file", Json(model.file.toUri().string())},
        {"languageId", Json(model.languageId)},
        {"workingDirectory", Json(model.workingDirectory.string())},
        {"metadata", model.metadata},
    });
}

}
