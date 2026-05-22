#include "vanta/workspace/access_control.h"

namespace vanta {

std::string ToString(AccessKind kind) {
    switch (kind) {
    case AccessKind::WorkspaceRead:
        return "workspace.read";
    case AccessKind::WorkspaceWrite:
        return "workspace.write";
    case AccessKind::ProcessExecute:
        return "process.execute";
    case AccessKind::NetworkAccess:
        return "network.access";
    case AccessKind::GitRead:
        return "git.read";
    case AccessKind::GitWrite:
        return "git.write";
    case AccessKind::SecretRead:
        return "secret.read";
    case AccessKind::DeviceAccess:
        return "device.access";
    case AccessKind::RemoteExecute:
        return "remote.execute";
    }
    return "workspace.read";
}

std::optional<AccessKind> AccessKindFromString(const std::string& value) {
    if (value == "workspace.read") {
        return AccessKind::WorkspaceRead;
    }
    if (value == "workspace.write") {
        return AccessKind::WorkspaceWrite;
    }
    if (value == "process.execute") {
        return AccessKind::ProcessExecute;
    }
    if (value == "network.access") {
        return AccessKind::NetworkAccess;
    }
    if (value == "git.read") {
        return AccessKind::GitRead;
    }
    if (value == "git.write") {
        return AccessKind::GitWrite;
    }
    if (value == "secret.read") {
        return AccessKind::SecretRead;
    }
    if (value == "device.access") {
        return AccessKind::DeviceAccess;
    }
    if (value == "remote.execute") {
        return AccessKind::RemoteExecute;
    }
    return std::nullopt;
}

}
