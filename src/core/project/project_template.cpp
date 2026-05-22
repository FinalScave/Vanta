#include "vanta/project/project_template.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <system_error>
#include <utility>

#include "vanta/workspace/workspace_context.h"
#include "vanta/workspace/workspace.h"

namespace vanta {
namespace {

std::string DefaultScratchName(const ScratchFileRequest& request) {
    if (!request.file_name.empty()) {
        return request.file_name;
    }
    if (request.language_id == "python") {
        return "scratch.py";
    }
    if (request.language_id == "cpp") {
        return "scratch.cpp";
    }
    return "scratch.txt";
}

bool WriteProjectFile(const std::filesystem::path& root, const ProjectTemplateFile& file, std::string* error_message) {
    const std::filesystem::path path = root / file.relative_path;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        if (error_message != nullptr) {
            *error_message = error.message();
        }
        return false;
    }

    std::ofstream output(path);
    if (!output) {
        if (error_message != nullptr) {
            *error_message = "Could not create " + path.string();
        }
        return false;
    }
    output << file.contents;
    output.close();

    if (file.executable) {
        std::filesystem::permissions(path, std::filesystem::perms::owner_exec, std::filesystem::perm_options::add, error);
    }
    return true;
}

}

void ProjectTemplateService::AddCategory(ProjectTemplateCategory category) {
    if (category.id.empty()) {
        return;
    }
    categories_[category.id] = std::move(category);
}

RegistrationHandle ProjectTemplateService::RegisterCategory(ProjectTemplateCategory category) {
    if (category.id.empty()) {
        return {};
    }
    const std::string id = category.id;
    AddCategory(std::move(category));
    return RegistrationHandle([this, id] {
        RemoveCategory(id);
    });
}

void ProjectTemplateService::RemoveCategory(const std::string& id) {
    categories_.erase(id);
}

void ProjectTemplateService::AddTemplate(ProjectTemplate value) {
    if (value.id.empty()) {
        return;
    }
    templates_[value.id] = std::move(value);
}

RegistrationHandle ProjectTemplateService::RegisterTemplate(ProjectTemplate value) {
    if (value.id.empty()) {
        return {};
    }
    const std::string id = value.id;
    AddTemplate(std::move(value));
    return RegistrationHandle([this, id] {
        RemoveTemplate(id);
    });
}

void ProjectTemplateService::RemoveTemplate(const std::string& id) {
    templates_.erase(id);
}

std::optional<ProjectTemplateCategory> ProjectTemplateService::Category(const std::string& id) const {
    auto it = categories_.find(id);
    return it == categories_.end() ? std::nullopt : std::optional<ProjectTemplateCategory>(it->second);
}

std::optional<ProjectTemplate> ProjectTemplateService::Template(const std::string& id) const {
    auto it = templates_.find(id);
    return it == templates_.end() ? std::nullopt : std::optional<ProjectTemplate>(it->second);
}

std::vector<ProjectTemplateCategory> ProjectTemplateService::Categories() const {
    std::vector<ProjectTemplateCategory> values;
    for (const auto& [id, category] : categories_) {
        (void)id;
        values.push_back(category);
    }
    std::sort(values.begin(), values.end(), [](const ProjectTemplateCategory& left, const ProjectTemplateCategory& right) {
        if (left.sort_order != right.sort_order) {
            return left.sort_order < right.sort_order;
        }
        return left.id < right.id;
    });
    return values;
}

std::vector<ProjectTemplate> ProjectTemplateService::Templates(const std::string& category_id) const {
    std::vector<ProjectTemplate> values;
    for (const auto& [id, value] : templates_) {
        (void)id;
        if (category_id.empty() || value.category_id == category_id) {
            values.push_back(value);
        }
    }
    std::sort(values.begin(), values.end(), [](const ProjectTemplate& left, const ProjectTemplate& right) {
        return left.id < right.id;
    });
    return values;
}

ProjectTemplateResult ProjectTemplateService::CreateProject(const std::string& template_id, const std::filesystem::path& root) const {
    const auto value = Template(template_id);
    if (!value) {
        return {.ok = false, .message = "Project template not found", .root = root};
    }
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error) {
        return {.ok = false, .message = error.message(), .root = root};
    }
    for (const ProjectTemplateFile& file : value->files) {
        std::string file_error;
        if (!WriteProjectFile(root, file, &file_error)) {
            return {.ok = false, .message = file_error, .root = root};
        }
    }
    return {.ok = true, .message = "Project created", .root = root};
}

ScratchFileResult ScratchFileService::CreateScratchFile(WorkspaceContext& context, ScratchFileRequest request) const {
    const std::filesystem::path directory = context.CurrentWorkspace().Info().root_path / ".vanta" / "scratch";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return {.ok = false, .message = error.message()};
    }

    const std::string name = DefaultScratchName(request);
    const VirtualFile file = context.CurrentWorkspace().File(directory / name);
    std::string write_error;
    if (!file.WriteText(request.contents, &write_error)) {
        return {.ok = false, .message = write_error, .file = file};
    }
    return {.ok = true, .message = "Scratch file created", .file = file};
}

void RegisterDefaultProjectTemplates(ProjectTemplateService& service) {
    (void)service;
}

}
