#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "vanta/core/registration.h"
#include "vanta/core/value.h"

namespace vanta {

using CommandHandler = std::function<Value(const Value&)>;

struct CommandDescriptor {
    std::string id;
    std::string title;
    std::string source;
};

class CommandRegistry {
public:
    static constexpr const char* kServiceId = "vanta.commands";

    virtual ~CommandRegistry() = default;
    RegistrationHandle RegisterCommand(const std::string& id, CommandHandler handler) {
        return RegisterCommand({.id = id, .title = id, .source = "runtime"}, std::move(handler));
    }
    virtual RegistrationHandle RegisterCommand(CommandDescriptor descriptor, CommandHandler handler) = 0;
    virtual std::optional<Value> Execute(const std::string& id, const Value& arguments) const = 0;
    virtual std::vector<std::string> List() const = 0;
    virtual std::vector<CommandDescriptor> Commands() const = 0;
};

}
