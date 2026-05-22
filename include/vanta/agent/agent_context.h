#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vanta/core/registration.h"
#include "vanta/core/diagnostic.h"
#include "vanta/core/value.h"
#include "vanta/vfs/virtual_file.h"

namespace vanta {

class WorkspaceContext;

namespace AgentContextKind {
inline constexpr const char* kText = "text";
inline constexpr const char* kDocument = "document";
inline constexpr const char* kDiagnostics = "diagnostics";
inline constexpr const char* kProject = "project";
inline constexpr const char* kJob = "job";
inline constexpr const char* kSearchIndex = "searchIndex";
inline constexpr const char* kGitDiff = "gitDiff";
}

struct AgentContextRequest {
    std::string goal;
    VirtualFile focus_file;
    std::vector<Diagnostic> diagnostics;
    std::size_t max_items = 32;
};

struct AgentContextItem {
    std::string provider_id;
    std::string kind = AgentContextKind::kText;
    std::string title;
    VirtualFile file;
    std::string text;
    std::optional<Value> payload;
};

struct AgentContext {
    std::vector<AgentContextItem> items;
};

class AgentContextProvider {
public:
    virtual ~AgentContextProvider() = default;

    virtual std::string Id() const = 0;
    virtual std::vector<AgentContextItem> Collect(const AgentContextRequest& request, WorkspaceContext& context) const = 0;
};

class AgentContextCollector {
public:
    static constexpr const char* kServiceId = "vanta.agent.context";

    RegistrationHandle RegisterProvider(std::unique_ptr<AgentContextProvider> provider);
    void RemoveProvider(const std::string& provider_id);
    void ClearProviders();
    std::vector<std::string> ProviderIds() const;
    AgentContext Collect(const AgentContextRequest& request, WorkspaceContext& context) const;

private:
    std::map<std::string, std::unique_ptr<AgentContextProvider>> providers_;
};

void RegisterDefaultAgentContextProviders(AgentContextCollector& service);

}
