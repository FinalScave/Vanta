#include "vanta/workspace/layout_state_store.h"

#include <fstream>
#include <sstream>

namespace vanta {
namespace {

bool boolValue(const Json& object, const std::string& key, bool fallback) {
    if (!object.contains(key) || !object[key].isBool()) {
        return fallback;
    }
    return object[key].asBool();
}

}

std::string LayoutStateStore::id() const {
    return componentId;
}

void LayoutStateStore::restoreState(const Json& state) {
    state_ = layoutStateFromJson(state);
}

Json LayoutStateStore::saveState() const {
    return toJson(state_);
}

bool LayoutStateStore::load(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        return false;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    state_ = layoutStateFromJson(Json::parse(stream.str()));
    return true;
}

bool LayoutStateStore::save(const std::filesystem::path& path) const {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }

    std::ofstream output(path);
    if (!output) {
        return false;
    }
    output << toJson(state_).dump();
    return true;
}

void LayoutStateStore::capture(const UiState& ui) {
    state_.openTabs.clear();
    for (const EditorTab& tab : ui.tabs) {
        state_.openTabs.push_back(tab.file.toUri());
        if (tab.active) {
            state_.activeFile = tab.file.toUri();
        }
    }
}

void LayoutStateStore::rememberBuildTarget(std::string target) {
    state_.lastBuildTarget = std::move(target);
}

const LayoutState& LayoutStateStore::state() const {
    return state_;
}

LayoutState& LayoutStateStore::state() {
    return state_;
}

Json toJson(const LayoutState& state) {
    Json::Array openTabs;
    for (const Uri& uri : state.openTabs) {
        openTabs.push_back(Json(uri.string()));
    }

    Json::Object pluginState;
    for (const auto& [key, value] : state.pluginState) {
        pluginState[key] = value;
    }

    return Json::object({
        {"openTabs", Json::array(std::move(openTabs))},
        {"activeFile", Json(state.activeFile.string())},
        {"fileTreeVisible", Json(state.fileTreeVisible)},
        {"problemsVisible", Json(state.problemsVisible)},
        {"buildPanelVisible", Json(state.buildPanelVisible)},
        {"agentPanelVisible", Json(state.agentPanelVisible)},
        {"gitPanelVisible", Json(state.gitPanelVisible)},
        {"lastBuildTarget", Json(state.lastBuildTarget)},
        {"pluginState", Json::object(std::move(pluginState))},
    });
}

LayoutState layoutStateFromJson(const Json& json) {
    LayoutState state;
    if (!json.isObject()) {
        return state;
    }

    if (json.contains("openTabs") && json["openTabs"].isArray()) {
        for (const Json& item : json["openTabs"].asArray()) {
            if (item.isString()) {
                state.openTabs.push_back(Uri::parse(item.asString()));
            }
        }
    }
    if (auto activeFile = json.stringValue("activeFile")) {
        state.activeFile = Uri::parse(*activeFile);
    }
    state.fileTreeVisible = boolValue(json, "fileTreeVisible", state.fileTreeVisible);
    state.problemsVisible = boolValue(json, "problemsVisible", state.problemsVisible);
    state.buildPanelVisible = boolValue(json, "buildPanelVisible", state.buildPanelVisible);
    state.agentPanelVisible = boolValue(json, "agentPanelVisible", state.agentPanelVisible);
    state.gitPanelVisible = boolValue(json, "gitPanelVisible", state.gitPanelVisible);
    state.lastBuildTarget = json.stringValue("lastBuildTarget").value_or("");

    if (json.contains("pluginState") && json["pluginState"].isObject()) {
        for (const auto& [key, value] : json["pluginState"].asObject()) {
            state.pluginState[key] = value;
        }
    }
    return state;
}

}
