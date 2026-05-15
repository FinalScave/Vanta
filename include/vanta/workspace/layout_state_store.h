#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "vanta/platform/json.h"
#include "vanta/project/component.h"
#include "vanta/workspace/ui_state_store.h"
#include "vanta/vfs/uri.h"

namespace vanta {

struct LayoutState {
    std::vector<Uri> openTabs;
    Uri activeFile;
    bool fileTreeVisible = true;
    bool problemsVisible = true;
    bool buildPanelVisible = true;
    bool agentPanelVisible = true;
    bool gitPanelVisible = true;
    std::string lastBuildTarget;
    std::map<std::string, Json> pluginState;
};

class LayoutStateStore final : public Component {
public:
    static constexpr const char* componentId = "vanta.ui.layout";

    std::string id() const override;
    void restoreState(const Json& state) override;
    Json saveState() const override;

    bool load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;
    void capture(const UiState& ui);
    void rememberBuildTarget(std::string target);

    const LayoutState& state() const;
    LayoutState& state();

private:
    LayoutState state_;
};

Json toJson(const LayoutState& state);
LayoutState layoutStateFromJson(const Json& json);

}
