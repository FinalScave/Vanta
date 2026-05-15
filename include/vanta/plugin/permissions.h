#pragma once

#include <set>
#include <string>
#include <vector>

namespace vanta {

enum class Permission {
    WorkspaceRead,
    WorkspaceWrite,
    ProcessExecute,
    NetworkAccess,
    GitRead,
    GitWrite,
    AgentTool,
    LanguageService,
    BuildProvider,
};

class PermissionSet {
public:
    static PermissionSet fromStrings(const std::vector<std::string>& permissions);

    void add(Permission permission);
    bool contains(Permission permission) const;

private:
    std::set<Permission> permissions_;
};

std::string toString(Permission permission);

}
