#include "project/project_state_store.h"

#include <fstream>
#include <sstream>

#include "vanta/core/value.h"
#include "vanta/core/json_codec.h"

namespace vanta {
namespace {

Value ProjectStateProjection(const ProjectState& state) {
    Value::Object components;
    for (const auto& [id, component_state] : state.component_states) {
        components[id] = component_state;
    }
    return Value::ObjectValue({
        {"schemaVersion", Value(static_cast<std::int64_t>(state.schema_version))},
        {"project", Value::ObjectValue({
            {"components", Value::ObjectValue(std::move(components))},
        })},
    });
}

ProjectState ProjectStateFromValue(const Value& json) {
    if (!json.IsObject()) {
        return {};
    }

    if (!json.Contains("schemaVersion")) {
        return {};
    }

    ProjectState state;
    if (json["schemaVersion"].IsInt()) {
        state.schema_version = static_cast<int>(json["schemaVersion"].AsInt());
    }

    if (!json.Contains("project") || !json["project"].IsObject()) {
        return state;
    }
    const Value& project = json["project"];
    if (!project.Contains("components") || !project["components"].IsObject()) {
        return state;
    }

    for (const auto& [id, component_state] : project["components"].AsObject()) {
        state.component_states[id] = component_state;
    }
    return state;
}

}

bool ProjectStateStore::Load(const std::filesystem::path& path, ProjectState* state, std::string* error_message) const {
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
    Result<Value> parsed = ValueFromJsonText(stream.str());
    if (!parsed) {
        *state = {};
        if (error_message != nullptr) {
            *error_message = parsed.ErrorValue().message;
        }
        return false;
    }
    *state = ProjectStateFromValue(parsed.Value());
    return true;
}

bool ProjectStateStore::Save(const std::filesystem::path& path, const ProjectState& state, std::string* error_message) const {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        if (error_message != nullptr) {
            *error_message = "Failed to create state directory";
        }
        return false;
    }

    std::ofstream output(path);
    if (!output) {
        if (error_message != nullptr) {
            *error_message = "Failed to open state file";
        }
        return false;
    }
    output << ValueToJsonText(ProjectStateProjection(state));
    return true;
}

}
