#include "vanta/plugin/permissions.h"

#include <map>

namespace vanta {
namespace {

const std::map<std::string, Permission>& permissionMap() {
    static const std::map<std::string, Permission> permissions = {
        {"workspace.read", Permission::WorkspaceRead},
        {"workspace.write", Permission::WorkspaceWrite},
        {"process.execute", Permission::ProcessExecute},
        {"network.access", Permission::NetworkAccess},
        {"git.read", Permission::GitRead},
        {"git.write", Permission::GitWrite},
        {"agent.tool", Permission::AgentTool},
        {"language.service", Permission::LanguageService},
        {"build.provider", Permission::BuildProvider},
    };
    return permissions;
}

}

PermissionSet PermissionSet::fromStrings(const std::vector<std::string>& permissions) {
    PermissionSet result;
    for (const std::string& permission : permissions) {
        auto it = permissionMap().find(permission);
        if (it != permissionMap().end()) {
            result.add(it->second);
        }
    }
    return result;
}

void PermissionSet::add(Permission permission) {
    permissions_.insert(permission);
}

bool PermissionSet::contains(Permission permission) const {
    return permissions_.contains(permission);
}

std::string toString(Permission permission) {
    for (const auto& [name, value] : permissionMap()) {
        if (value == permission) {
            return name;
        }
    }
    return "";
}

}
