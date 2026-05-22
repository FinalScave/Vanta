#include "cmake_project_model.h"

#include <utility>

namespace vanta::internal {
namespace {

Value StringsProjection(const std::vector<std::string>& values) {
    Value::Array result;
    for (const std::string& value : values) {
        result.push_back(Value(value));
    }
    return Value::ArrayValue(std::move(result));
}

Value VirtualFilesProjection(const std::vector<VirtualFile>& files) {
    Value::Array values;
    for (const VirtualFile& file : files) {
        values.push_back(Value(file.ToUri().ToString()));
    }
    return Value::ArrayValue(std::move(values));
}

}

Value CMakeProjectGraphProjection(const CMakeProjectGraph& graph) {
    Value::Array targets;
    for (const CMakeTarget& target : graph.targets) {
        targets.push_back(Value::ObjectValue({
            {"name", Value(target.name)},
            {"kind", Value(target.kind)},
            {"sourceFiles", VirtualFilesProjection(target.source_files)},
            {"includeDirectories", VirtualFilesProjection(target.include_directories)},
            {"defines", StringsProjection(target.defines)},
            {"compileArguments", StringsProjection(target.compile_arguments)},
        }));
    }

    return Value::ObjectValue({
        {"targets", Value::ArrayValue(std::move(targets))},
        {"sourceFiles", VirtualFilesProjection(graph.source_files)},
        {"includeDirectories", VirtualFilesProjection(graph.include_directories)},
        {"generatedFiles", VirtualFilesProjection(graph.generated_files)},
        {"defines", StringsProjection(graph.defines)},
        {"compileArguments", StringsProjection(graph.compile_arguments)},
    });
}

}

namespace vanta {

std::string CMakeProjectModel::Id() const {
    return kAttachmentId;
}

std::string CMakeProjectModel::Kind() const {
    return kAttachmentKind;
}

std::string CMakeProjectModel::Title() const {
    return "CMake Project";
}

Value CMakeProjectModel::Projection() const {
    return Value::ObjectValue({
        {"detected", Value(detected)},
        {"cmakeListsFile", Value(cmake_lists_file.ToUri().ToString())},
        {"compileCommandsFile", Value(compile_commands_file.ToUri().ToString())},
        {"buildDirectory", Value(build_directory.string())},
        {"graph", internal::CMakeProjectGraphProjection(graph)},
    });
}

}
