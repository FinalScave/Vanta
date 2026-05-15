#include "vanta/plugin/contribution_registry.h"

namespace vanta {

void ContributionRegistry::add(Contribution contribution) {
    if (contribution.id.empty()) {
        return;
    }
    contributions_[contribution.id] = std::move(contribution);
}

void ContributionRegistry::remove(const std::string& id) {
    contributions_.erase(id);
}

void ContributionRegistry::removePlugin(const std::string& pluginId) {
    for (auto it = contributions_.begin(); it != contributions_.end();) {
        if (it->second.pluginId == pluginId) {
            it = contributions_.erase(it);
        } else {
            ++it;
        }
    }
}

void ContributionRegistry::clear() {
    contributions_.clear();
}

std::optional<Contribution> ContributionRegistry::contribution(const std::string& id) const {
    auto it = contributions_.find(id);
    return it == contributions_.end() ? std::nullopt : std::optional<Contribution>(it->second);
}

std::vector<Contribution> ContributionRegistry::list() const {
    std::vector<Contribution> result;
    for (const auto& [id, contribution] : contributions_) {
        (void)id;
        result.push_back(contribution);
    }
    return result;
}

std::vector<Contribution> ContributionRegistry::list(ContributionKind kind) const {
    std::vector<Contribution> result;
    for (const auto& [id, contribution] : contributions_) {
        (void)id;
        if (contribution.kind == kind) {
            result.push_back(contribution);
        }
    }
    return result;
}

std::vector<Contribution> ContributionRegistry::listByPlugin(const std::string& pluginId) const {
    std::vector<Contribution> result;
    for (const auto& [id, contribution] : contributions_) {
        (void)id;
        if (contribution.pluginId == pluginId) {
            result.push_back(contribution);
        }
    }
    return result;
}

std::string toString(ContributionKind kind) {
    switch (kind) {
    case ContributionKind::Command:
        return "command";
    case ContributionKind::View:
        return "view";
    case ContributionKind::Menu:
        return "menu";
    case ContributionKind::LanguageService:
        return "languageService";
    case ContributionKind::FileSystemProvider:
        return "fileSystemProvider";
    case ContributionKind::AgentTool:
        return "agentTool";
    case ContributionKind::AgentContextProvider:
        return "agentContextProvider";
    case ContributionKind::RunConfiguration:
        return "runConfiguration";
    case ContributionKind::DiagnosticProvider:
        return "diagnosticProvider";
    case ContributionKind::Component:
        return "component";
    }
    return "command";
}

std::optional<ContributionKind> contributionKindFromString(const std::string& value) {
    if (value == "command" || value == "commands") {
        return ContributionKind::Command;
    }
    if (value == "view" || value == "views") {
        return ContributionKind::View;
    }
    if (value == "menu" || value == "menus") {
        return ContributionKind::Menu;
    }
    if (value == "languageService" || value == "languageServices") {
        return ContributionKind::LanguageService;
    }
    if (value == "fileSystemProvider" || value == "fileSystemProviders") {
        return ContributionKind::FileSystemProvider;
    }
    if (value == "agentTool" || value == "agentTools") {
        return ContributionKind::AgentTool;
    }
    if (value == "agentContextProvider" || value == "agentContextProviders") {
        return ContributionKind::AgentContextProvider;
    }
    if (value == "runConfiguration" || value == "runConfigurations") {
        return ContributionKind::RunConfiguration;
    }
    if (value == "diagnosticProvider" || value == "diagnosticProviders") {
        return ContributionKind::DiagnosticProvider;
    }
    if (value == "component" || value == "components") {
        return ContributionKind::Component;
    }
    return std::nullopt;
}

}
