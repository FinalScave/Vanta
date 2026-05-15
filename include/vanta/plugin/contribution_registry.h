#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vanta/platform/json.h"

namespace vanta {

enum class ContributionKind {
    Command,
    View,
    Menu,
    LanguageService,
    FileSystemProvider,
    AgentTool,
    AgentContextProvider,
    RunConfiguration,
    DiagnosticProvider,
    Component,
};

struct Contribution {
    ContributionKind kind = ContributionKind::Command;
    std::string id;
    std::string title;
    std::string pluginId;
    Json metadata;
};

class ContributionRegistry {
public:
    void add(Contribution contribution);
    void remove(const std::string& id);
    void removePlugin(const std::string& pluginId);
    void clear();

    std::optional<Contribution> contribution(const std::string& id) const;
    std::vector<Contribution> list() const;
    std::vector<Contribution> list(ContributionKind kind) const;
    std::vector<Contribution> listByPlugin(const std::string& pluginId) const;

private:
    std::map<std::string, Contribution> contributions_;
};

std::string toString(ContributionKind kind);
std::optional<ContributionKind> contributionKindFromString(const std::string& value);

}
