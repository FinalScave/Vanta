#include "ui/command_palette.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace vanta {

namespace {

std::string Lowercase(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool Matches(const CommandPaletteItem& item, const std::string& query) {
    if (query.empty()) {
        return true;
    }
    const std::string needle = Lowercase(query);
    return Lowercase(item.id).find(needle) != std::string::npos ||
           Lowercase(item.title).find(needle) != std::string::npos ||
           Lowercase(item.source).find(needle) != std::string::npos;
}

void AppendPart(std::ostringstream& stream, const std::string& part, bool& first) {
    if (!first) {
        stream << '+';
    }
    stream << part;
    first = false;
}

}

bool SameKeyChord(const KeyChord& left, const KeyChord& right) {
    return left.modifiers == right.modifiers && left.key == right.key;
}

std::string FormatKeyChord(const KeyChord& chord) {
    std::ostringstream stream;
    bool first = true;
    if (HasKeyModifier(chord.modifiers, KeyModifier::Ctrl)) {
        AppendPart(stream, "Ctrl", first);
    }
    if (HasKeyModifier(chord.modifiers, KeyModifier::Shift)) {
        AppendPart(stream, "Shift", first);
    }
    if (HasKeyModifier(chord.modifiers, KeyModifier::Alt)) {
        AppendPart(stream, "Alt", first);
    }
    if (HasKeyModifier(chord.modifiers, KeyModifier::Meta)) {
        AppendPart(stream, "Cmd", first);
    }
    if (!chord.key.empty()) {
        AppendPart(stream, chord.key, first);
    }
    return stream.str();
}

std::string FormatKeybinding(const Keybinding& binding) {
    std::string label = FormatKeyChord(binding.first);
    if (binding.second.has_value()) {
        const std::string second = FormatKeyChord(*binding.second);
        if (!second.empty()) {
            if (!label.empty()) {
                label += ' ';
            }
            label += second;
        }
    }
    return label;
}

void KeybindingRegistry::Bind(Keybinding binding) {
    if (binding.command_id.empty()) {
        return;
    }
    bindings_[binding.command_id] = std::move(binding);
}

void KeybindingRegistry::Unbind(const std::string& command_id) {
    bindings_.erase(command_id);
}

std::optional<Keybinding> KeybindingRegistry::BindingForCommand(const std::string& command_id) const {
    auto it = bindings_.find(command_id);
    return it == bindings_.end() ? std::nullopt : std::optional<Keybinding>(it->second);
}

std::vector<Keybinding> KeybindingRegistry::List() const {
    std::vector<Keybinding> result;
    for (const auto& [id, binding] : bindings_) {
        (void)id;
        result.push_back(binding);
    }
    return result;
}

std::vector<CommandPaletteItem> CommandPaletteItems(
    const CommandRegistry& commands,
    const KeybindingRegistry& keybindings) {
    std::vector<CommandPaletteItem> result;

    for (const CommandDescriptor& command : commands.Commands()) {
        CommandPaletteItem item;
        item.id = command.id;
        item.title = command.title.empty() ? command.id : command.title;
        item.source = command.source;
        if (auto binding = keybindings.BindingForCommand(item.id)) {
            item.keybinding = FormatKeybinding(*binding);
        }
        result.push_back(std::move(item));
    }

    std::sort(result.begin(), result.end(), [](const CommandPaletteItem& left, const CommandPaletteItem& right) {
        return left.title < right.title;
    });
    return result;
}

std::vector<CommandPaletteItem> FilterCommandPaletteItems(const std::vector<CommandPaletteItem>& items, const std::string& query) {
    std::vector<CommandPaletteItem> result;
    for (const CommandPaletteItem& item : items) {
        if (Matches(item, query)) {
            result.push_back(item);
        }
    }
    return result;
}

}
