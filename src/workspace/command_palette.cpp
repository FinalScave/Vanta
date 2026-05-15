#include "vanta/workspace/command_palette.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace vanta {
namespace {

std::string lowercase(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool matches(const CommandPaletteItem& item, const std::string& query) {
    if (query.empty()) {
        return true;
    }
    const std::string needle = lowercase(query);
    return lowercase(item.id).find(needle) != std::string::npos ||
           lowercase(item.title).find(needle) != std::string::npos ||
           lowercase(item.source).find(needle) != std::string::npos;
}

}

void KeybindingRegistry::bind(Keybinding binding) {
    if (binding.commandId.empty()) {
        return;
    }
    bindings_[binding.commandId] = std::move(binding);
}

void KeybindingRegistry::unbind(const std::string& commandId) {
    bindings_.erase(commandId);
}

std::optional<Keybinding> KeybindingRegistry::bindingForCommand(const std::string& commandId) const {
    auto it = bindings_.find(commandId);
    return it == bindings_.end() ? std::nullopt : std::optional<Keybinding>(it->second);
}

std::vector<Keybinding> KeybindingRegistry::list() const {
    std::vector<Keybinding> result;
    for (const auto& [id, binding] : bindings_) {
        (void)id;
        result.push_back(binding);
    }
    return result;
}

std::vector<CommandPaletteItem> CommandPalette::items(
    const CommandRegistry& commands,
    const ContributionRegistry& contributions,
    const KeybindingRegistry& keybindings) const {
    std::vector<CommandPaletteItem> result;
    std::set<std::string> seen;

    for (const Contribution& contribution : contributions.list(ContributionKind::Command)) {
        CommandPaletteItem item;
        item.id = contribution.id;
        item.title = contribution.title.empty() ? contribution.id : contribution.title;
        item.source = contribution.pluginId;
        if (auto binding = keybindings.bindingForCommand(item.id)) {
            item.keybinding = binding->key;
        }
        result.push_back(std::move(item));
        seen.insert(contribution.id);
    }

    for (const std::string& id : commands.list()) {
        if (seen.contains(id)) {
            continue;
        }
        CommandPaletteItem item;
        item.id = id;
        item.title = id;
        item.source = "runtime";
        if (auto binding = keybindings.bindingForCommand(item.id)) {
            item.keybinding = binding->key;
        }
        result.push_back(std::move(item));
    }

    std::sort(result.begin(), result.end(), [](const CommandPaletteItem& left, const CommandPaletteItem& right) {
        return left.title < right.title;
    });
    return result;
}

std::vector<CommandPaletteItem> CommandPalette::filter(const std::vector<CommandPaletteItem>& items, const std::string& query) const {
    std::vector<CommandPaletteItem> result;
    for (const CommandPaletteItem& item : items) {
        if (matches(item, query)) {
            result.push_back(item);
        }
    }
    return result;
}

}
