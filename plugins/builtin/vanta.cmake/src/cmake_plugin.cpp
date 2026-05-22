#include "core_plugin_factories.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "cmake_build_provider.h"
#include "cmake_project_model.h"
#include "cpp_index.h"
#include "vanta/agent/agent_tool_registry.h"
#include "internal/projection.h"
#include "vanta/project/project.h"
#include "vanta/project/project_manager.h"
#include "vanta/workspace/settings_service.h"
#include "vanta/workspace/workspace_context.h"

namespace vanta::builtin {
namespace {

Value BuildEnvironmentProjection(const BuildEnvironment& environment) {
    return Value::ObjectValue({
        {"providerId", Value(environment.provider_id)},
        {"detected", Value(environment.detected)},
        {"buildDirectory", Value(environment.build_directory.string())},
    });
}

BuildRequest BuildRequestFromValue(const Value& input, BuildRequestKind kind) {
    BuildRequest request;
    request.kind = kind;
    request.target_id = input.StringValue("target").value_or(input.StringValue("targetId").value_or(""));
    if (auto build_directory = input.StringValue("buildDirectory")) {
        request.build_directory_override = *build_directory;
    }
    if (input.Contains("parameters") && input["parameters"].IsObject()) {
        if (auto build_directory = input["parameters"].StringValue("buildDirectory")) {
            request.build_directory_override = *build_directory;
        }
    }
    return request;
}

std::string NormalizePathString(const std::filesystem::path& path) {
    std::error_code error;
    const auto normalized = std::filesystem::weakly_canonical(path, error);
    return (error ? path : normalized).string();
}

std::filesystem::path DefaultBuildDirectory(const WorkspaceInfo& workspace) {
    return workspace.root_path / "build";
}

std::filesystem::path CompileCommandsPath(const std::filesystem::path& workspace_root) {
    const auto root_database = workspace_root / "compile_commands.json";
    if (std::filesystem::exists(root_database)) {
        return root_database;
    }
    const auto build_database = workspace_root / "build" / "compile_commands.json";
    if (std::filesystem::exists(build_database)) {
        return build_database;
    }
    return {};
}

std::filesystem::path InferBuildDirectory(const WorkspaceInfo& workspace, const std::filesystem::path& database_path) {
    if (!database_path.empty()) {
        return database_path.parent_path();
    }
    return DefaultBuildDirectory(workspace);
}

void AddUniqueString(std::vector<std::string>& values, std::set<std::string>& seen, std::string value) {
    if (value.empty() || !seen.insert(value).second) {
        return;
    }
    values.push_back(std::move(value));
}

void AddUniqueFile(std::vector<VirtualFile>& values, std::set<Uri>& seen, VirtualFile file) {
    if (!file.Valid() || !seen.insert(file.ToUri()).second) {
        return;
    }
    values.push_back(std::move(file));
}

std::string SanitizeId(std::string value) {
    for (char& character : value) {
        const bool allowed = std::isalnum(static_cast<unsigned char>(character)) ||
            character == '-' || character == '_' || character == '.';
        if (!allowed) {
            character = '_';
        }
    }
    return value.empty() ? "file" : value;
}

CMakeProjectGraph BuildCMakeGraph(const Workspace& workspace, const CppCompilationDatabase& database) {
    CMakeProjectGraph graph;
    CMakeTarget target;
    target.name = workspace.Info().name.empty() ? "workspace" : workspace.Info().name;
    target.kind = "compileCommands";

    std::set<Uri> source_uris;
    std::set<Uri> target_source_uris;
    std::set<Uri> include_uris;
    std::set<Uri> target_include_uris;
    std::set<std::string> define_values;
    std::set<std::string> target_define_values;
    std::set<std::string> argument_values;

    for (const CppTranslationUnit& unit : database.translation_units) {
        AddUniqueFile(graph.source_files, source_uris, unit.source_file);
        AddUniqueFile(target.source_files, target_source_uris, unit.source_file);

        for (const VirtualFile& include_directory : unit.include_directories) {
            AddUniqueFile(graph.include_directories, include_uris, include_directory);
            AddUniqueFile(target.include_directories, target_include_uris, include_directory);
        }
        for (const std::string& define : unit.defines) {
            AddUniqueString(graph.defines, define_values, define);
            AddUniqueString(target.defines, target_define_values, define);
        }
        for (const std::string& argument : unit.compile_arguments) {
            AddUniqueString(graph.compile_arguments, argument_values, argument);
            target.compile_arguments.push_back(argument);
        }
    }

    if (!target.source_files.empty() || !target.include_directories.empty() || !target.defines.empty()) {
        graph.targets.push_back(std::move(target));
    }
    return graph;
}

ProjectViewNode CMakeGroupNode(std::string id, std::string label, std::string description = {}) {
    return {
        .id = std::move(id),
        .label = std::move(label),
        .description = std::move(description),
        .kind = std::string(ProjectViewNodeKind::kGroup),
        .icon = "folder",
        .has_children = true,
        .synthetic = true,
    };
}

ProjectViewNode CMakeFileNode(const VirtualFile& file) {
    const bool directory = file.Valid() && file.Stat().kind == VirtualFileKind::Directory;
    return {
        .id = file.ToUri().ToString(),
        .label = file.DisplayName(),
        .kind = directory ? std::string(ProjectViewNodeKind::kDirectory) : std::string(ProjectViewNodeKind::kFile),
        .icon = directory ? "folder" : "file",
        .file = file,
        .has_file = file.Valid(),
        .has_children = false,
    };
}

std::vector<ProjectViewNode> CMakeFileNodes(const std::vector<VirtualFile>& files) {
    std::vector<ProjectViewNode> nodes;
    for (const VirtualFile& file : files) {
        nodes.push_back(CMakeFileNode(file));
    }
    return nodes;
}

std::string CMakeTargetNodeId(const CMakeTarget& target) {
    return "vanta.cmake.target:" + SanitizeId(target.name);
}

const CMakeTarget* FindCMakeTarget(const CMakeProjectModel& model, const std::string& node_id) {
    for (const CMakeTarget& target : model.graph.targets) {
        if (CMakeTargetNodeId(target) == node_id) {
            return &target;
        }
    }
    return nullptr;
}

class CMakeProjectModelProvider final : public ProjectModelProvider {
public:
    std::string Id() const override {
        return "vanta.cmake.projectProvider";
    }

    void Contribute(WorkspaceContext& context, ProjectModelBuilder& builder) const override {
        Workspace& workspace = context.CurrentWorkspace();
        const auto cmake_lists_path = workspace.Info().root_path / "CMakeLists.txt";
        const bool has_cmake_lists = std::filesystem::exists(cmake_lists_path);
        const auto database_path = CompileCommandsPath(workspace.Info().root_path);
        const bool has_compile_commands = !database_path.empty();
        if (!has_cmake_lists && !has_compile_commands) {
            return;
        }

        CMakeProjectModel cmake;
        cmake.detected = true;
        cmake.build_directory = InferBuildDirectory(workspace.Info(), database_path);
        if (has_cmake_lists) {
            cmake.cmake_lists_file = workspace.File(cmake_lists_path);
            ProjectFacet facet{
                .id = "cmake",
                .type = "cmake",
                .title = "CMake",
            };
            builder.AddFacet(facet);
            builder.AddFacetToPrimaryModule(std::move(facet));
        }

        if (has_compile_commands) {
            auto database = std::make_unique<CppCompilationDatabase>(LoadCppCompilationDatabase(workspace, database_path));
            cmake.compile_commands_file = database->file;
            cmake.graph = BuildCMakeGraph(workspace, *database);
            ProjectFacet facet{
                .id = "cpp",
                .type = "cpp",
                .title = "C++",
            };
            builder.AddFacet(facet);
            builder.AddFacetToPrimaryModule(std::move(facet));
            builder.AddAttachment(std::move(database));
        }

        builder.AddAttachment(std::make_unique<CMakeProjectModel>(std::move(cmake)));
    }
};

class CMakeProjectViewProvider final : public ProjectViewProvider {
public:
    std::string Id() const override {
        return "vanta.cmake.projectViewProvider";
    }

    std::vector<ProjectView> Views(WorkspaceContext& context) const override {
        if (context.CurrentProject() == nullptr) {
            return {};
        }
        const auto* model = context.CurrentProject()->Model().Attachment<CMakeProjectModel>(CMakeProjectModel::kAttachmentId);
        if (model == nullptr || !model->detected) {
            return {};
        }
        return {{
            .id = "vanta.cmake",
            .title = "CMake",
            .icon = "cmake",
            .priority = 20,
        }};
    }

    std::vector<ProjectViewNode> TopLevelNodes(WorkspaceContext& context, const ProjectView&) override {
        const auto* model = context.RequireProject().Model().Attachment<CMakeProjectModel>(CMakeProjectModel::kAttachmentId);
        if (model == nullptr) {
            return {};
        }

        std::vector<ProjectViewNode> nodes;
        if (!model->graph.targets.empty()) {
            nodes.push_back(CMakeGroupNode("vanta.cmake.targets", "Targets"));
        }
        if (!model->graph.source_files.empty()) {
            nodes.push_back(CMakeGroupNode("vanta.cmake.sources", "Source Files"));
        }
        if (!model->graph.include_directories.empty()) {
            nodes.push_back(CMakeGroupNode("vanta.cmake.includes", "Include Directories"));
        }
        if (!model->graph.generated_files.empty()) {
            nodes.push_back(CMakeGroupNode("vanta.cmake.generated", "Generated Files"));
        }
        return nodes;
    }

    std::vector<ProjectViewNode> Children(WorkspaceContext& context, const ProjectView&, const ProjectViewNode& parent) override {
        const auto* model = context.RequireProject().Model().Attachment<CMakeProjectModel>(CMakeProjectModel::kAttachmentId);
        if (model == nullptr) {
            return {};
        }
        if (parent.id == "vanta.cmake.sources") {
            return CMakeFileNodes(model->graph.source_files);
        }
        if (parent.id == "vanta.cmake.includes") {
            return CMakeFileNodes(model->graph.include_directories);
        }
        if (parent.id == "vanta.cmake.generated") {
            return CMakeFileNodes(model->graph.generated_files);
        }
        if (parent.id == "vanta.cmake.targets") {
            std::vector<ProjectViewNode> nodes;
            for (const CMakeTarget& target : model->graph.targets) {
                nodes.push_back({
                    .id = CMakeTargetNodeId(target),
                    .label = target.name,
                    .description = target.kind,
                    .kind = "vanta.cmake.target",
                    .icon = "target",
                    .has_children = !target.source_files.empty() || !target.include_directories.empty() || !target.defines.empty(),
                    .synthetic = true,
                });
            }
            return nodes;
        }

        for (const CMakeTarget& target : model->graph.targets) {
            const std::string target_id = CMakeTargetNodeId(target);
            if (parent.id == target_id + ".sources") {
                return CMakeFileNodes(target.source_files);
            }
            if (parent.id == target_id + ".includes") {
                return CMakeFileNodes(target.include_directories);
            }
            if (parent.id == target_id + ".defines") {
                std::vector<ProjectViewNode> nodes;
                for (const std::string& define : target.defines) {
                    nodes.push_back({
                        .id = target_id + ".define:" + SanitizeId(define),
                        .label = define,
                        .kind = "vanta.cmake.define",
                        .icon = "symbol",
                        .synthetic = true,
                    });
                }
                return nodes;
            }
        }

        const CMakeTarget* target = FindCMakeTarget(*model, parent.id);
        if (target == nullptr) {
            return {};
        }
        std::vector<ProjectViewNode> nodes;
        if (!target->source_files.empty()) {
            ProjectViewNode group = CMakeGroupNode(parent.id + ".sources", "Source Files");
            group.has_children = true;
            nodes.push_back(std::move(group));
        }
        if (!target->include_directories.empty()) {
            ProjectViewNode group = CMakeGroupNode(parent.id + ".includes", "Include Directories");
            group.has_children = true;
            nodes.push_back(std::move(group));
        }
        if (!target->defines.empty()) {
            ProjectViewNode group = CMakeGroupNode(parent.id + ".defines", "Defines");
            group.has_children = true;
            nodes.push_back(std::move(group));
        }
        return nodes;
    }
};

class CMakeCoreExtension final : public CoreExtension {
public:
    void Activate(ExtensionContext& context) override {
        WorkspaceContext& workspace = context.Context();
        build_ = &workspace.Build();
        workspace_context_ = &context.Context();

        context.Track(workspace.Build().RegisterProvider(std::make_unique<CMakeBuildProvider>()));
        context.Track(workspace.Projects().RegisterModelProvider(std::make_unique<CMakeProjectModelProvider>()));
        context.Track(workspace.Projects().RegisterViewProvider(std::make_unique<CMakeProjectViewProvider>()));
        workspace.Settings().RegisterNode({.id = "build.cmake", .parent_id = "build", .owner_id = "vanta.cmake", .title = "CMake", .order = 10});
        workspace.Settings().RegisterSetting({
            .id = "cmake.build_directory",
            .owner_id = "vanta.cmake",
            .node_id = "build.cmake",
            .title = "Build Directory",
            .description = "Default CMake build directory.",
            .type = SettingValueType::Path,
            .default_value = SettingValue::PathValue("build"),
            .supported_scopes = {SettingScopeKind::Workspace, SettingScopeKind::Project},
            .resolution_order = {SettingScopeKind::Project, SettingScopeKind::Workspace},
            .tags = {"cmake", "build", "directory"},
            .aliases = {"cmake build dir"},
            .order = 10,
        });

        context.Track(workspace.Commands().RegisterCommand("cmake.detect", [this](const Value&) {
            return BuildEnvironmentProjection(build_->Detect(*workspace_context_, workspace_context_->RequireProject().Model()));
        }));

        context.Track(workspace.Commands().RegisterCommand("cmake.build", [this](const Value& input) {
            return internal::BuildResultProjection(build_->Run(*workspace_context_, BuildRequestFromValue(input, BuildRequestKind::Build)));
        }));

        context.Track(workspace.Commands().RegisterCommand("cmake.test", [this](const Value& input) {
            return internal::BuildResultProjection(build_->Run(*workspace_context_, BuildRequestFromValue(input, BuildRequestKind::Test)));
        }));

        context.Track(workspace.AgentTools().RegisterTool({
            .id = "cmake.findOwningTarget",
            .description = "Find compile command information for a source file in the current CMake workspace.",
            .input_schema = Value::ObjectValue({
                {"type", Value("object")},
                {"required", Value::ArrayValue({Value("file")})},
            }),
            .handler = [this](const Value& input) {
                const std::string file = input.StringValue("file").value_or("");
                const std::string target_path = NormalizePathString(file);
                const auto* database = workspace_context_->RequireProject().Model().Attachment<CppCompilationDatabase>(CppCompilationDatabase::kAttachmentId);
                if (database == nullptr) {
                    return Value::ObjectValue({
                        {"found", Value(false)},
                        {"file", Value(file)},
                    });
                }
                for (const CppCompileCommand& command : database->commands) {
                    if (NormalizePathString(command.file) == target_path) {
                        return Value::ObjectValue({
                            {"found", Value(true)},
                            {"file", Value(command.file.string())},
                            {"directory", Value(command.directory.string())},
                            {"command", Value(command.command)},
                        });
                    }
                }
                return Value::ObjectValue({
                    {"found", Value(false)},
                    {"file", Value(file)},
                });
            },
        }));

        context.Log().Info("Activated CMake core plugin");
    }

    void Deactivate() override {
        build_ = nullptr;
        workspace_context_ = nullptr;
    }

private:
    BuildService* build_ = nullptr;
    WorkspaceContext* workspace_context_ = nullptr;
};

}

std::unique_ptr<CoreExtension> CreateCMakeCoreExtension() {
    return std::make_unique<CMakeCoreExtension>();
}

}
