#include "vanta/agent/agent_tool_registry.h"

#include <algorithm>
#include <sstream>

namespace vanta {

void AgentToolRegistry::addTool(AgentToolDefinition definition) {
    tools_.push_back(std::move(definition));
}

void AgentToolRegistry::removeTool(const std::string& id) {
    tools_.erase(
        std::remove_if(tools_.begin(), tools_.end(), [&](const AgentToolDefinition& tool) {
            return tool.id == id;
        }),
        tools_.end());
}

std::optional<Json> AgentToolRegistry::callTool(const std::string& id, const Json& input) const {
    for (const AgentToolDefinition& tool : tools_) {
        if (tool.id == id) {
            return tool.handler(input);
        }
    }
    return std::nullopt;
}

std::vector<AgentToolDefinition> AgentToolRegistry::tools() const {
    return tools_;
}

std::optional<std::string> AgentToolRegistry::readCode(const VirtualFile& file) const {
    return file.readText();
}

std::string AgentToolRegistry::explainDiagnostic(const Diagnostic& diagnostic) const {
    std::ostringstream stream;
    stream << diagnostic.location.file.toUri().string() << ':' << diagnostic.location.line << ':' << diagnostic.location.column;
    stream << " reports " << toString(diagnostic.severity) << " from " << diagnostic.source << ". ";
    stream << diagnostic.message;
    return stream.str();
}

}
