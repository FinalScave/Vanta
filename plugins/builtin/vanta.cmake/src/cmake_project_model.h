#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "vanta/core/value.h"
#include "vanta/project/project.h"
#include "vanta/vfs/virtual_file.h"

namespace vanta {

struct CMakeTarget {
    std::string name;
    std::string kind;
    std::vector<VirtualFile> source_files;
    std::vector<VirtualFile> include_directories;
    std::vector<std::string> defines;
    std::vector<std::string> compile_arguments;
};

struct CMakeProjectGraph {
    std::vector<CMakeTarget> targets;
    std::vector<VirtualFile> source_files;
    std::vector<VirtualFile> include_directories;
    std::vector<VirtualFile> generated_files;
    std::vector<std::string> defines;
    std::vector<std::string> compile_arguments;
};

struct CMakeProjectModel final : public ProjectAttachment {
    static constexpr const char* kAttachmentId = "vanta.cmake.project";
    static constexpr const char* kAttachmentKind = "cmake.project";

    std::string Id() const override;
    std::string Kind() const override;
    std::string Title() const override;
    Value Projection() const override;

    bool detected = false;
    VirtualFile cmake_lists_file;
    VirtualFile compile_commands_file;
    std::filesystem::path build_directory;
    CMakeProjectGraph graph;
};

namespace internal {

Value CMakeProjectGraphProjection(const CMakeProjectGraph& graph);

}

}
