#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "vanta/core/diagnostic.h"
#include "vanta/builtin/git/git_client.h"
#include "vanta/platform/json.h"
#include "vanta/vfs/virtual_file.h"

namespace vanta {

class WorkspaceContext;

enum class AgentContextItemKind {
    Text,
    Document,
    Diagnostics,
    Project,
    Job,
    SearchIndex,
    GitDiff,
};

struct AgentContextRequest {
    std::string goal;
    VirtualFile focusFile;
    std::vector<Diagnostic> diagnostics;
    std::size_t maxItems = 32;
};

struct AgentContextItem {
    std::string providerId;
    AgentContextItemKind kind = AgentContextItemKind::Text;
    std::string title;
    VirtualFile file;
    std::string text;
    Json data;
};

struct AgentContext {
    std::vector<AgentContextItem> items;
};

class AgentContextProvider {
public:
    virtual ~AgentContextProvider() = default;

    virtual std::string id() const = 0;
    virtual std::vector<AgentContextItem> collect(const AgentContextRequest& request, WorkspaceContext& context) const = 0;
};

class AgentContextCollector {
public:
    void addProvider(std::unique_ptr<AgentContextProvider> provider);
    void removeProvider(const std::string& providerId);
    void clearProviders();
    std::vector<std::string> providerIds() const;
    AgentContext collect(const AgentContextRequest& request, WorkspaceContext& context) const;

private:
    std::map<std::string, std::unique_ptr<AgentContextProvider>> providers_;
};

void registerDefaultAgentContextProviders(AgentContextCollector& service);
std::unique_ptr<AgentContextProvider> createGitDiffAgentContextProvider(const GitClient& git);
std::string toString(AgentContextItemKind kind);
Json toJson(const AgentContextItem& item);
Json toJson(const AgentContext& context);

}
