#include "ui/layout_state_store.h"

#include "vanta/core/value.h"
#include "ui/ui_state_store.h"

namespace vanta {
namespace {

bool BoolValue(const Value& object, const std::string& key, bool fallback) {
    return object.BoolValue(key).value_or(fallback);
}

Value LayoutStateProjection(const LayoutState& state) {
    Value::Array open_tabs;
    for (const Uri& uri : state.open_tabs) {
        open_tabs.push_back(Value(uri.ToString()));
    }

    Value::Object plugin_state;
    for (const auto& [key, value] : state.plugin_state) {
        plugin_state[key] = value;
    }

    return Value::ObjectValue({
        {"openTabs", Value::ArrayValue(std::move(open_tabs))},
        {"activeFile", Value(state.active_file.ToString())},
        {"projectViewVisible", Value(state.project_view_visible)},
        {"problemsVisible", Value(state.problems_visible)},
        {"buildPanelVisible", Value(state.build_panel_visible)},
        {"agentPanelVisible", Value(state.agent_panel_visible)},
        {"gitPanelVisible", Value(state.git_panel_visible)},
        {"lastBuildTarget", Value(state.last_build_target)},
        {"pluginState", Value::ObjectValue(std::move(plugin_state))},
    });
}

LayoutState LayoutStateFromValue(const Value& value) {
    LayoutState state;
    if (!value.IsObject()) {
        return state;
    }

    if (value.Contains("openTabs") && value["openTabs"].IsArray()) {
        for (const Value& item : value["openTabs"].AsArray()) {
            if (item.IsString()) {
                state.open_tabs.push_back(Uri::Parse(item.AsString()));
            }
        }
    }
    if (auto active_file = value.StringValue("activeFile")) {
        state.active_file = Uri::Parse(*active_file);
    }
    state.project_view_visible = BoolValue(value, "projectViewVisible", state.project_view_visible);
    state.problems_visible = BoolValue(value, "problemsVisible", state.problems_visible);
    state.build_panel_visible = BoolValue(value, "buildPanelVisible", state.build_panel_visible);
    state.agent_panel_visible = BoolValue(value, "agentPanelVisible", state.agent_panel_visible);
    state.git_panel_visible = BoolValue(value, "gitPanelVisible", state.git_panel_visible);
    state.last_build_target = value.StringValue("lastBuildTarget").value_or("");

    if (value.Contains("pluginState") && value["pluginState"].IsObject()) {
        for (const auto& [key, plugin_value] : value["pluginState"].AsObject()) {
            state.plugin_state[key] = plugin_value;
        }
    }
    return state;
}

}

std::string LayoutStateStore::Id() const {
    return kComponentId;
}

void LayoutStateStore::RestoreState(const Value& state) {
    state_ = LayoutStateFromValue(state);
}

Value LayoutStateStore::SaveState() const {
    return LayoutStateProjection(state_);
}

void LayoutStateStore::Capture(const UiState& ui) {
    state_.open_tabs.clear();
    for (const EditorTab& tab : ui.tabs) {
        state_.open_tabs.push_back(tab.file.ToUri());
        if (tab.active) {
            state_.active_file = tab.file.ToUri();
        }
    }
}

void LayoutStateStore::RememberBuildTarget(std::string target) {
    state_.last_build_target = std::move(target);
}

const LayoutState& LayoutStateStore::State() const {
    return state_;
}

LayoutState& LayoutStateStore::State() {
    return state_;
}

}
