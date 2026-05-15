#include "vanta/workspace/workspace_services.h"

#include <utility>

namespace vanta {

void DefaultCommandRegistry::add(const std::string& id, CommandHandler handler) {
    handlers_[id] = std::move(handler);
}

RegistrationHandle DefaultCommandRegistry::registerCommand(const std::string& id, CommandHandler handler) {
    add(id, std::move(handler));
    return RegistrationHandle([this, id] {
        unregister(id);
    });
}

std::optional<Json> DefaultCommandRegistry::execute(const std::string& id, const Json& arguments) const {
    auto it = handlers_.find(id);
    if (it == handlers_.end()) {
        return std::nullopt;
    }
    return it->second(arguments);
}

std::vector<std::string> DefaultCommandRegistry::list() const {
    std::vector<std::string> result;
    for (const auto& [id, handler] : handlers_) {
        (void)handler;
        result.push_back(id);
    }
    return result;
}

void DefaultCommandRegistry::unregister(const std::string& id) {
    handlers_.erase(id);
}

}
