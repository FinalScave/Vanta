#include "workspace/command_registry_impl.h"

#include <utility>

namespace vanta {

RegistrationHandle internal::CommandRegistryImpl::RegisterCommand(CommandDescriptor descriptor, CommandHandler handler) {
    if (descriptor.id.empty()) {
        return {};
    }
    if (descriptor.title.empty()) {
        descriptor.title = descriptor.id;
    }
    if (descriptor.source.empty()) {
        descriptor.source = "runtime";
    }
    const std::string id = descriptor.id;
    commands_[id] = Entry{
        .descriptor = std::move(descriptor),
        .handler = std::move(handler),
    };
    return RegistrationHandle([this, id] {
        Unregister(id);
    });
}

std::optional<Value> internal::CommandRegistryImpl::Execute(const std::string& id, const Value& arguments) const {
    auto it = commands_.find(id);
    if (it == commands_.end()) {
        return std::nullopt;
    }
    return it->second.handler(arguments);
}

std::vector<std::string> internal::CommandRegistryImpl::List() const {
    std::vector<std::string> result;
    for (const auto& [id, entry] : commands_) {
        (void)entry;
        result.push_back(id);
    }
    return result;
}

std::vector<CommandDescriptor> internal::CommandRegistryImpl::Commands() const {
    std::vector<CommandDescriptor> result;
    for (const auto& [id, entry] : commands_) {
        (void)id;
        result.push_back(entry.descriptor);
    }
    return result;
}

void internal::CommandRegistryImpl::Unregister(const std::string& id) {
    commands_.erase(id);
}

}
