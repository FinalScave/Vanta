#pragma once

#include <map>
#include <string>

#include "vanta/workspace/command_registry.h"

namespace vanta::internal {

class CommandRegistryImpl final : public CommandRegistry {
public:
    using CommandRegistry::RegisterCommand;

    RegistrationHandle RegisterCommand(CommandDescriptor descriptor, CommandHandler handler) override;
    std::optional<Value> Execute(const std::string& id, const Value& arguments) const override;
    std::vector<std::string> List() const override;
    std::vector<CommandDescriptor> Commands() const override;

private:
    struct Entry {
        CommandDescriptor descriptor;
        CommandHandler handler;
    };

    void Unregister(const std::string& id);

    std::map<std::string, Entry> commands_;
};

}
