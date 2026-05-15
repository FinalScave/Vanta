#include "vanta/project/project_template.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <system_error>
#include <utility>

#include "vanta/workspace/workspace_context.h"

namespace vanta {
namespace {

std::string defaultScratchName(const ScratchFileRequest& request) {
    if (!request.fileName.empty()) {
        return request.fileName;
    }
    if (request.languageId == "python") {
        return "scratch.py";
    }
    if (request.languageId == "cpp") {
        return "scratch.cpp";
    }
    return "scratch.txt";
}

bool writeProjectFile(const std::filesystem::path& root, const ProjectTemplateFile& file, std::string* errorMessage) {
    const std::filesystem::path path = root / file.relativePath;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        if (errorMessage != nullptr) {
            *errorMessage = error.message();
        }
        return false;
    }

    std::ofstream output(path);
    if (!output) {
        if (errorMessage != nullptr) {
            *errorMessage = "Could not create " + path.string();
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

Json templateFilesToJson(const std::vector<ProjectTemplateFile>& files) {
    Json::Array values;
    for (const ProjectTemplateFile& file : files) {
        values.push_back(Json::object({
            {"path", Json(file.relativePath.string())},
            {"executable", Json(file.executable)},
        }));
    }
    return Json::array(std::move(values));
}

}

void ProjectTemplateService::addCategory(ProjectTemplateCategory category) {
    if (category.id.empty()) {
        return;
    }
    categories_[category.id] = std::move(category);
}

void ProjectTemplateService::addTemplate(ProjectTemplate value) {
    if (value.id.empty()) {
        return;
    }
    if (!value.metadata.isObject()) {
        value.metadata = Json::object();
    }
    templates_[value.id] = std::move(value);
}

std::optional<ProjectTemplateCategory> ProjectTemplateService::category(const std::string& id) const {
    auto it = categories_.find(id);
    return it == categories_.end() ? std::nullopt : std::optional<ProjectTemplateCategory>(it->second);
}

std::optional<ProjectTemplate> ProjectTemplateService::projectTemplate(const std::string& id) const {
    auto it = templates_.find(id);
    return it == templates_.end() ? std::nullopt : std::optional<ProjectTemplate>(it->second);
}

std::vector<ProjectTemplateCategory> ProjectTemplateService::categories() const {
    std::vector<ProjectTemplateCategory> values;
    for (const auto& [id, category] : categories_) {
        (void)id;
        values.push_back(category);
    }
    std::sort(values.begin(), values.end(), [](const ProjectTemplateCategory& left, const ProjectTemplateCategory& right) {
        if (left.sortOrder != right.sortOrder) {
            return left.sortOrder < right.sortOrder;
        }
        return left.id < right.id;
    });
    return values;
}

std::vector<ProjectTemplate> ProjectTemplateService::templates(const std::string& categoryId) const {
    std::vector<ProjectTemplate> values;
    for (const auto& [id, value] : templates_) {
        (void)id;
        if (categoryId.empty() || value.categoryId == categoryId) {
            values.push_back(value);
        }
    }
    std::sort(values.begin(), values.end(), [](const ProjectTemplate& left, const ProjectTemplate& right) {
        return left.id < right.id;
    });
    return values;
}

ProjectTemplateResult ProjectTemplateService::createProject(const std::string& templateId, const std::filesystem::path& root) const {
    const auto value = projectTemplate(templateId);
    if (!value) {
        return {.ok = false, .message = "Project template not found", .root = root};
    }
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error) {
        return {.ok = false, .message = error.message(), .root = root};
    }
    for (const ProjectTemplateFile& file : value->files) {
        std::string fileError;
        if (!writeProjectFile(root, file, &fileError)) {
            return {.ok = false, .message = fileError, .root = root};
        }
    }
    return {.ok = true, .message = "Project created", .root = root};
}

ScratchFileResult ScratchFileService::createScratchFile(WorkspaceContext& context, ScratchFileRequest request) const {
    const std::filesystem::path directory = context.workspace().info().rootPath / ".vanta" / "scratch";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return {.ok = false, .message = error.message()};
    }

    const std::string name = defaultScratchName(request);
    const VirtualFile file = context.workspace().file(directory / name);
    std::string writeError;
    if (!file.writeText(request.contents, &writeError)) {
        return {.ok = false, .message = writeError, .file = file};
    }
    return {.ok = true, .message = "Scratch file created", .file = file};
}

void registerDefaultProjectTemplates(ProjectTemplateService& service) {
    service.addCategory({.id = "cpp", .title = "C++", .sortOrder = 10});
    service.addCategory({.id = "python", .title = "Python", .sortOrder = 20});
    service.addCategory({.id = "android", .title = "Android", .sortOrder = 30});

    service.addTemplate({
        .id = "cpp.console.cmake",
        .categoryId = "cpp",
        .title = "C++ Console",
        .description = "A minimal CMake console project.",
        .files = {
            {.relativePath = "CMakeLists.txt", .contents = "cmake_minimum_required(VERSION 3.20)\nproject(VantaApp LANGUAGES CXX)\n\nset(CMAKE_CXX_STANDARD 20)\nadd_executable(vanta_app src/main.cpp)\n"},
            {.relativePath = "src/main.cpp", .contents = "#include <iostream>\n\nint main() {\n    std::cout << \"Hello from Vanta\" << std::endl;\n    return 0;\n}\n"},
        },
        .metadata = Json::object({{"languageId", Json("cpp")}}),
    });
    service.addTemplate({
        .id = "python.script",
        .categoryId = "python",
        .title = "Python Script",
        .description = "A single Python entry point.",
        .files = {
            {.relativePath = "main.py", .contents = "def main():\n    print(\"Hello from Vanta\")\n\nif __name__ == \"__main__\":\n    main()\n"},
        },
        .metadata = Json::object({{"languageId", Json("python")}}),
    });
}

Json toJson(const ProjectTemplateCategory& category) {
    return Json::object({
        {"id", Json(category.id)},
        {"title", Json(category.title)},
        {"sortOrder", Json(static_cast<std::int64_t>(category.sortOrder))},
    });
}

Json toJson(const ProjectTemplate& value) {
    return Json::object({
        {"id", Json(value.id)},
        {"categoryId", Json(value.categoryId)},
        {"title", Json(value.title)},
        {"description", Json(value.description)},
        {"files", templateFilesToJson(value.files)},
        {"metadata", value.metadata},
    });
}

}
