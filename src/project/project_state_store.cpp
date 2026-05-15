#include "vanta/project/project_state_store.h"

#include <fstream>
#include <sstream>

namespace vanta {
namespace {

constexpr int currentSchemaVersion = 1;

ProjectState legacyLayoutState(const Json& json) {
    ProjectState state;
    state.schemaVersion = currentSchemaVersion;
    if (json.isObject()) {
        state.componentStates["vanta.ui.layout"] = json;
    }
    return state;
}

}

bool ProjectStateStore::load(const std::filesystem::path& path, ProjectState* state, std::string* errorMessage) const {
    if (state == nullptr) {
        return false;
    }

    std::ifstream input(path);
    if (!input) {
        *state = {};
        return false;
    }

    std::ostringstream stream;
    stream << input.rdbuf();
    try {
        *state = projectStateFromJson(Json::parse(stream.str()));
    } catch (const JsonError& error) {
        *state = {};
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
        return false;
    }
    return true;
}

bool ProjectStateStore::save(const std::filesystem::path& path, const ProjectState& state, std::string* errorMessage) const {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to create state directory";
        }
        return false;
    }

    std::ofstream output(path);
    if (!output) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to open state file";
        }
        return false;
    }
    output << toJson(state).dump();
    return true;
}

Json toJson(const ProjectState& state) {
    Json::Object components;
    for (const auto& [id, componentState] : state.componentStates) {
        components[id] = componentState;
    }
    return Json::object({
        {"schemaVersion", Json(static_cast<std::int64_t>(state.schemaVersion))},
        {"project", Json::object({
            {"components", Json::object(std::move(components))},
        })},
    });
}

ProjectState projectStateFromJson(const Json& json) {
    if (!json.isObject()) {
        return {};
    }

    if (!json.contains("schemaVersion")) {
        return legacyLayoutState(json);
    }

    ProjectState state;
    if (json["schemaVersion"].isInt()) {
        state.schemaVersion = static_cast<int>(json["schemaVersion"].asInt());
    }

    if (!json.contains("project") || !json["project"].isObject()) {
        return state;
    }
    const Json& project = json["project"];
    if (!project.contains("components") || !project["components"].isObject()) {
        return state;
    }

    for (const auto& [id, componentState] : project["components"].asObject()) {
        state.componentStates[id] = componentState;
    }
    return state;
}

}
