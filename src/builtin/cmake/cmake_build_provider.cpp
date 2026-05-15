#include "vanta/builtin/cmake/cmake_build_provider.h"

#include <utility>

namespace vanta {
namespace {

std::filesystem::path findCompileCommands(const std::filesystem::path& workspaceRoot) {
    const auto rootDatabase = workspaceRoot / "compile_commands.json";
    if (std::filesystem::exists(rootDatabase)) {
        return rootDatabase;
    }
    const auto buildDatabase = workspaceRoot / "build" / "compile_commands.json";
    if (std::filesystem::exists(buildDatabase)) {
        return buildDatabase;
    }
    return {};
}

ExecutionRequest executionRequestForTask(const std::filesystem::path& workspaceRoot, const BuildTask& task) {
    ExecutionRequest request;
    request.workingDirectory = workspaceRoot;
    if (task.kind == BuildTaskKind::Build) {
        request.executable = "cmake";
        request.arguments = {"--build", task.buildDirectory.empty() ? "build" : task.buildDirectory.string()};
        if (!task.target.empty()) {
            request.arguments.push_back("--target");
            request.arguments.push_back(task.target);
        }
    } else {
        request.executable = "ctest";
        request.arguments = {"--test-dir", task.buildDirectory.empty() ? "build" : task.buildDirectory.string(), "--output-on-failure"};
    }
    return request;
}

}

std::string CMakeBuildProvider::id() const {
    return "cmake";
}

BuildEnvironment CMakeBuildProvider::detect(const std::filesystem::path& workspaceRoot) const {
    BuildEnvironment environment;
    environment.providerId = id();
    const auto cmakeListsPath = workspaceRoot / "CMakeLists.txt";
    const auto compileCommandsPath = findCompileCommands(workspaceRoot);
    const bool hasCMakeLists = std::filesystem::exists(cmakeListsPath);
    const bool hasCompileCommands = !compileCommandsPath.empty();
    environment.detected = hasCMakeLists || hasCompileCommands;
    environment.buildDirectory = hasCompileCommands ? compileCommandsPath.parent_path() : workspaceRoot / "build";
    environment.metadata = Json::object({
        {"hasCMakeLists", Json(hasCMakeLists)},
        {"hasCompileCommands", Json(hasCompileCommands)},
        {"cmakeListsPath", Json(cmakeListsPath.string())},
        {"compileCommandsPath", Json(compileCommandsPath.string())},
    });
    return environment;
}

BuildPlan CMakeBuildProvider::plan(const std::filesystem::path& workspaceRoot, const BuildTask& task) const {
    return {
        .providerId = id(),
        .title = toString(task.kind),
        .steps = {{
            .title = toString(task.kind),
            .request = executionRequestForTask(workspaceRoot, task),
            .parseDiagnostics = task.kind == BuildTaskKind::Build,
        }},
        .metadata = Json::object({
            {"buildDirectory", Json((task.buildDirectory.empty() ? std::filesystem::path("build") : task.buildDirectory).string())},
            {"target", Json(task.target)},
        }),
    };
}

}
