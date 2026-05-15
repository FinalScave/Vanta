#include "vanta/builtin/cmake/cmake_project_model.h"

#include <utility>

namespace vanta {
namespace {

Json stringsToJson(const std::vector<std::string>& values) {
    Json::Array result;
    for (const std::string& value : values) {
        result.push_back(Json(value));
    }
    return Json::array(std::move(result));
}

Json virtualFilesToJson(const std::vector<VirtualFile>& files) {
    Json::Array values;
    for (const VirtualFile& file : files) {
        values.push_back(Json(file.toUri().string()));
    }
    return Json::array(std::move(values));
}

}

Json toJson(const CMakeProjectGraph& graph) {
    Json::Array targets;
    for (const CMakeTarget& target : graph.targets) {
        targets.push_back(Json::object({
            {"name", Json(target.name)},
            {"kind", Json(target.kind)},
            {"sourceFiles", virtualFilesToJson(target.sourceFiles)},
            {"includeDirectories", virtualFilesToJson(target.includeDirectories)},
            {"defines", stringsToJson(target.defines)},
            {"compileArguments", stringsToJson(target.compileArguments)},
        }));
    }

    return Json::object({
        {"targets", Json::array(std::move(targets))},
        {"sourceFiles", virtualFilesToJson(graph.sourceFiles)},
        {"includeDirectories", virtualFilesToJson(graph.includeDirectories)},
        {"generatedFiles", virtualFilesToJson(graph.generatedFiles)},
        {"defines", stringsToJson(graph.defines)},
        {"compileArguments", stringsToJson(graph.compileArguments)},
    });
}

Json toJson(const CMakeProjectModel& model) {
    return Json::object({
        {"detected", Json(model.detected)},
        {"cmakeListsFile", Json(model.cmakeListsFile.toUri().string())},
        {"compileCommandsFile", Json(model.compileCommandsFile.toUri().string())},
        {"buildDirectory", Json(model.buildDirectory.string())},
        {"graph", toJson(model.graph)},
    });
}

}
