#include "vanta/agent/agent_tool_registry.h"

#include <utility>

namespace vanta {

RegistrationHandle AgentToolRegistry::RegisterTool(AgentToolDefinition definition) {
    if (definition.id.empty() || !definition.handler) {
        return {};
    }
    const std::string id = definition.id;
    const std::uint64_t registration_id = next_registration_id_++;
    tools_[id] = {
        .definition = std::move(definition),
        .registration_id = registration_id,
    };
    return RegistrationHandle([this, id, registration_id] {
        RemoveToolRegistration(id, registration_id);
    });
}

void AgentToolRegistry::RemoveTool(const std::string& id) {
    tools_.erase(id);
}

void AgentToolRegistry::RemoveToolRegistration(const std::string& id, std::uint64_t registration_id) {
    auto it = tools_.find(id);
    if (it != tools_.end() && it->second.registration_id == registration_id) {
        tools_.erase(it);
    }
}

std::optional<Value> AgentToolRegistry::CallTool(const std::string& id, const Value& input) const {
    auto it = tools_.find(id);
    if (it != tools_.end()) {
        return it->second.definition.handler(input);
    }
    return std::nullopt;
}

std::vector<AgentToolDefinition> AgentToolRegistry::Tools() const {
    std::vector<AgentToolDefinition> result;
    for (const auto& [id, tool] : tools_) {
        (void)id;
        result.push_back(tool.definition);
    }
    return result;
}

}
