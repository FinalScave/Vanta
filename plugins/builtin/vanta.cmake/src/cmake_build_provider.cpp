#include "cmake_build_provider.h"

#include <utility>

#include "vanta/project/project.h"
#include "vanta/workspace/workspace.h"
#include "vanta/workspace/workspace_context.h"

namespace vanta {
namespace {

std::filesystem::path FindCompileCommands(const std::filesystem::path& workspace_root) {
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

std::filesystem::path BuildDirectoryFor(const WorkspaceContext& context, const BuildRequest& request) {
    if (!request.build_directory_override.empty()) {
        return request.build_directory_override;
    }
    const std::filesystem::path compile_commands_path = FindCompileCommands(context.CurrentWorkspace().Info().root_path);
    if (!compile_commands_path.empty()) {
        return compile_commands_path.parent_path();
    }
    return context.CurrentWorkspace().Info().root_path / "build";
}

ExecutionRequest ExecutionRequestForBuild(const WorkspaceContext& context, const BuildRequest& build_request, const std::filesystem::path& build_directory) {
    ExecutionRequest request;
    request.working_directory = context.CurrentWorkspace().Info().root_path;
    if (build_request.kind == BuildRequestKind::Build) {
        request.executable = "cmake";
        request.arguments = {"--build", build_directory.string()};
        if (!build_request.target_id.empty()) {
            request.arguments.push_back("--target");
            request.arguments.push_back(build_request.target_id);
        }
    } else {
        request.executable = "ctest";
        request.arguments = {"--test-dir", build_directory.string(), "--output-on-failure"};
    }
    return request;
}

}

std::string CMakeBuildProvider::Id() const {
    return "cmake";
}

BuildEnvironment CMakeBuildProvider::Detect(WorkspaceContext& context, const ProjectModel& project) const {
    const std::filesystem::path workspace_root = context.CurrentWorkspace().Info().root_path;
    BuildEnvironment environment;
    environment.provider_id = Id();
    const auto cmake_lists_path = workspace_root / "CMakeLists.txt";
    const auto compile_commands_path = FindCompileCommands(workspace_root);
    const bool has_cmake_lists = std::filesystem::exists(cmake_lists_path);
    const bool has_compile_commands = !compile_commands_path.empty();
    environment.detected = project.HasFacet("cmake") || has_cmake_lists || has_compile_commands;
    const std::filesystem::path build_directory = has_compile_commands ? compile_commands_path.parent_path() : workspace_root / "build";
    environment.build_directory = build_directory;
    return environment;
}

BuildPlan CMakeBuildProvider::Plan(WorkspaceContext& context, const BuildRequest& request) const {
    const std::filesystem::path build_directory = BuildDirectoryFor(context, request);
    return {
        .provider_id = Id(),
        .title = ToString(request.kind),
        .steps = {{
            .title = ToString(request.kind),
            .request = ExecutionRequestForBuild(context, request, build_directory),
            .parse_diagnostics = request.kind == BuildRequestKind::Build,
            .diagnostic_base_directory = build_directory,
        }},
    };
}

}
