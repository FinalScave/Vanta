#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vanta/platform/json.h"
#include "vanta/vfs/virtual_file.h"

namespace vanta {

class WorkspaceContext;

struct ProjectTemplateCategory {
    std::string id;
    std::string title;
    int sortOrder = 0;
};

struct ProjectTemplateFile {
    std::filesystem::path relativePath;
    std::string contents;
    bool executable = false;
};

struct ProjectTemplate {
    std::string id;
    std::string categoryId;
    std::string title;
    std::string description;
    std::vector<ProjectTemplateFile> files;
    Json metadata;
};

struct ProjectTemplateResult {
    bool ok = false;
    std::string message;
    std::filesystem::path root;
};

class ProjectTemplateService {
public:
    void addCategory(ProjectTemplateCategory category);
    void addTemplate(ProjectTemplate value);
    std::optional<ProjectTemplateCategory> category(const std::string& id) const;
    std::optional<ProjectTemplate> projectTemplate(const std::string& id) const;
    std::vector<ProjectTemplateCategory> categories() const;
    std::vector<ProjectTemplate> templates(const std::string& categoryId = {}) const;
    ProjectTemplateResult createProject(const std::string& templateId, const std::filesystem::path& root) const;

private:
    std::map<std::string, ProjectTemplateCategory> categories_;
    std::map<std::string, ProjectTemplate> templates_;
};

struct ScratchFileRequest {
    std::string languageId;
    std::string fileName;
    std::string contents;
};

struct ScratchFileResult {
    bool ok = false;
    std::string message;
    VirtualFile file;
};

class ScratchFileService {
public:
    ScratchFileResult createScratchFile(WorkspaceContext& context, ScratchFileRequest request) const;
};

void registerDefaultProjectTemplates(ProjectTemplateService& service);
Json toJson(const ProjectTemplateCategory& category);
Json toJson(const ProjectTemplate& value);

}
