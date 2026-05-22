#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vanta/core/registration.h"
#include "vanta/core/value.h"
#include "vanta/vfs/virtual_file.h"

namespace vanta {

class WorkspaceContext;

struct ProjectTemplateCategory {
    std::string id;
    std::string title;
    int sort_order = 0;
};

struct ProjectTemplateFile {
    std::filesystem::path relative_path;
    std::string contents;
    bool executable = false;
};

struct ProjectTemplate {
    std::string id;
    std::string category_id;
    std::string title;
    std::string description;
    std::vector<ProjectTemplateFile> files;
    std::string language_id;
};

struct ProjectTemplateResult {
    bool ok = false;
    std::string message;
    std::filesystem::path root;
};

class ProjectTemplateService {
public:
    static constexpr const char* kServiceId = "vanta.projectTemplates";

    void AddCategory(ProjectTemplateCategory category);
    RegistrationHandle RegisterCategory(ProjectTemplateCategory category);
    void RemoveCategory(const std::string& id);
    void AddTemplate(ProjectTemplate value);
    RegistrationHandle RegisterTemplate(ProjectTemplate value);
    void RemoveTemplate(const std::string& id);
    std::optional<ProjectTemplateCategory> Category(const std::string& id) const;
    std::optional<ProjectTemplate> Template(const std::string& id) const;
    std::vector<ProjectTemplateCategory> Categories() const;
    std::vector<ProjectTemplate> Templates(const std::string& category_id = {}) const;
    ProjectTemplateResult CreateProject(const std::string& template_id, const std::filesystem::path& root) const;

private:
    std::map<std::string, ProjectTemplateCategory> categories_;
    std::map<std::string, ProjectTemplate> templates_;
};

struct ScratchFileRequest {
    std::string language_id;
    std::string file_name;
    std::string contents;
};

struct ScratchFileResult {
    bool ok = false;
    std::string message;
    VirtualFile file;
};

class ScratchFileService {
public:
    static constexpr const char* kServiceId = "vanta.scratchFiles";

    ScratchFileResult CreateScratchFile(WorkspaceContext& context, ScratchFileRequest request) const;
};

void RegisterDefaultProjectTemplates(ProjectTemplateService& service);
}
