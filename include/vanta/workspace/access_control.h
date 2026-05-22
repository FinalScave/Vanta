#pragma once

#include <optional>
#include <string>

namespace vanta {

enum class AccessKind {
    WorkspaceRead,
    WorkspaceWrite,
    ProcessExecute,
    NetworkAccess,
    GitRead,
    GitWrite,
    SecretRead,
    DeviceAccess,
    RemoteExecute,
};

std::string ToString(AccessKind kind);
std::optional<AccessKind> AccessKindFromString(const std::string& value);

}
