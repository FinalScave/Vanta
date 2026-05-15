#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vanta/workspace/workspace_services.h"
#include "vanta/plugin/contribution_registry.h"

namespace vanta {

struct CommandPaletteItem {
    std::string id;
    std::string title;
    std::string keybinding;
    std::string source;
    bool enabled = true;
};

struct Keybinding {
    std::string commandId;
    std::string key;
    std::string when;
};

class KeybindingRegistry {
public:
    void bind(Keybinding binding);
    void unbind(const std::string& commandId);
    std::optional<Keybinding> bindingForCommand(const std::string& commandId) const;
    std::vector<Keybinding> list() const;

private:
    std::map<std::string, Keybinding> bindings_;
};

class CommandPalette {
public:
    std::vector<CommandPaletteItem> items(const CommandRegistry& commands, const ContributionRegistry& contributions, const KeybindingRegistry& keybindings) const;
    std::vector<CommandPaletteItem> filter(const std::vector<CommandPaletteItem>& items, const std::string& query) const;
};

}
