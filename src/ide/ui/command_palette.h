#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vanta/workspace/command_registry.h"

namespace vanta {

struct CommandPaletteItem {
    std::string id;
    std::string title;
    std::string keybinding;
    std::string source;
    bool enabled = true;
};

enum class KeyModifier : std::uint32_t {
    None = 0,
    Ctrl = 1u << 0,
    Shift = 1u << 1,
    Alt = 1u << 2,
    Meta = 1u << 3,
};

constexpr std::uint32_t KeyModifierMask(KeyModifier modifier) {
    return static_cast<std::uint32_t>(modifier);
}

constexpr std::uint32_t operator|(KeyModifier left, KeyModifier right) {
    return KeyModifierMask(left) | KeyModifierMask(right);
}

constexpr std::uint32_t operator|(std::uint32_t left, KeyModifier right) {
    return left | KeyModifierMask(right);
}

constexpr bool HasKeyModifier(std::uint32_t modifiers, KeyModifier modifier) {
    return (modifiers & KeyModifierMask(modifier)) != 0;
}

struct KeyChord {
    std::uint32_t modifiers = 0;
    std::string key;
};

struct Keybinding {
    std::string command_id;
    KeyChord first;
    std::optional<KeyChord> second;
    std::string when;
};

class KeybindingRegistry {
public:
    void Bind(Keybinding binding);
    void Unbind(const std::string& command_id);
    std::optional<Keybinding> BindingForCommand(const std::string& command_id) const;
    std::vector<Keybinding> List() const;

private:
    std::map<std::string, Keybinding> bindings_;
};

bool SameKeyChord(const KeyChord& left, const KeyChord& right);
std::string FormatKeyChord(const KeyChord& chord);
std::string FormatKeybinding(const Keybinding& binding);
std::vector<CommandPaletteItem> CommandPaletteItems(
    const CommandRegistry& commands,
    const KeybindingRegistry& keybindings);
std::vector<CommandPaletteItem> FilterCommandPaletteItems(const std::vector<CommandPaletteItem>& items, const std::string& query);

}
