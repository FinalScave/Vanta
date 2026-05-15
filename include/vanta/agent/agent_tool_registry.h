#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "vanta/core/diagnostic.h"
#include "vanta/platform/json.h"
#include "vanta/vfs/virtual_file.h"

namespace vanta {

struct AgentToolDefinition {
    std::string id;
    std::string description;
    Json inputSchema;
    std::function<Json(const Json&)> handler;
};

class AgentToolRegistry {
public:
    void addTool(AgentToolDefinition definition);
    void removeTool(const std::string& id);
    std::optional<Json> callTool(const std::string& id, const Json& input) const;
    std::vector<AgentToolDefinition> tools() const;

    std::optional<std::string> readCode(const VirtualFile& file) const;
    std::string explainDiagnostic(const Diagnostic& diagnostic) const;

private:
    std::vector<AgentToolDefinition> tools_;
};

}
