#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "vanta/platform/json.h"
#include "vanta/vfs/virtual_file.h"

namespace vanta {

struct CMakeTarget {
    std::string name;
    std::string kind;
    std::vector<VirtualFile> sourceFiles;
    std::vector<VirtualFile> includeDirectories;
    std::vector<std::string> defines;
    std::vector<std::string> compileArguments;
};

struct CMakeProjectGraph {
    std::vector<CMakeTarget> targets;
    std::vector<VirtualFile> sourceFiles;
    std::vector<VirtualFile> includeDirectories;
    std::vector<VirtualFile> generatedFiles;
    std::vector<std::string> defines;
    std::vector<std::string> compileArguments;
};

struct CMakeProjectModel {
    static constexpr const char* attachmentId = "vanta.cmake.project";
    static constexpr const char* attachmentKind = "cmake.project";

    bool detected = false;
    VirtualFile cmakeListsFile;
    VirtualFile compileCommandsFile;
    std::filesystem::path buildDirectory;
    CMakeProjectGraph graph;
};

Json toJson(const CMakeProjectGraph& graph);
Json toJson(const CMakeProjectModel& model);

}
