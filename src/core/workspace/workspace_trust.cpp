#include "vanta/workspace/workspace_trust.h"

#include <algorithm>
#include <utility>

namespace vanta {
namespace {

bool ContainsAccess(const std::vector<AccessKind>& access_list, AccessKind access) {
    return std::find(access_list.begin(), access_list.end(), access) != access_list.end();
}

}

WorkspaceTrustService::WorkspaceTrustService() {
    ApplyDefaultPolicy();
}

void WorkspaceTrustService::SetLevel(WorkspaceTrustLevel level) {
    if (policy_.level == level) {
        return;
    }
    policy_.level = level;
    ApplyDefaultPolicy();
    Publish();
}

WorkspaceTrustLevel WorkspaceTrustService::Level() const {
    return policy_.level;
}

WorkspaceTrustPolicy WorkspaceTrustService::Policy() const {
    return policy_;
}

bool WorkspaceTrustService::Trusted() const {
    return policy_.level == WorkspaceTrustLevel::Trusted;
}

bool WorkspaceTrustService::Allows(AccessKind access, bool high_risk) const {
    if (ContainsAccess(policy_.blocked_access, access)) {
        return false;
    }
    return !high_risk || !ContainsAccess(policy_.high_risk_blocked_access, access);
}

bool WorkspaceTrustService::CanRememberApprovals() const {
    return policy_.allow_remembered_approvals;
}

std::string WorkspaceTrustService::DenialReason(AccessKind access, bool high_risk) const {
    if (Allows(access, high_risk)) {
        return {};
    }
    return "Access is blocked by workspace trust policy: " + ToString(access);
}

std::uint64_t WorkspaceTrustService::OnDidChangeTrust(EventBus<WorkspaceTrustChangeEvent>::Listener listener) {
    return on_did_change_.Subscribe(std::move(listener));
}

void WorkspaceTrustService::RemoveTrustListener(std::uint64_t listener_id) {
    on_did_change_.Unsubscribe(listener_id);
}

void WorkspaceTrustService::ApplyDefaultPolicy() {
    policy_.blocked_access.clear();
    policy_.high_risk_blocked_access.clear();
    switch (policy_.level) {
    case WorkspaceTrustLevel::Trusted:
        policy_.allow_remembered_approvals = true;
        break;
    case WorkspaceTrustLevel::Restricted:
        policy_.allow_remembered_approvals = false;
        policy_.blocked_access = {AccessKind::NetworkAccess};
        policy_.high_risk_blocked_access = {
            AccessKind::WorkspaceWrite,
            AccessKind::ProcessExecute,
            AccessKind::GitWrite,
        };
        break;
    case WorkspaceTrustLevel::Untrusted:
        policy_.allow_remembered_approvals = false;
        policy_.blocked_access = {
            AccessKind::WorkspaceWrite,
            AccessKind::ProcessExecute,
            AccessKind::NetworkAccess,
            AccessKind::GitWrite,
        };
        break;
    }
}

void WorkspaceTrustService::Publish() {
    on_did_change_.Publish({
        .level = policy_.level,
        .policy = policy_,
    });
}

std::string ToString(WorkspaceTrustLevel level) {
    switch (level) {
    case WorkspaceTrustLevel::Trusted:
        return "trusted";
    case WorkspaceTrustLevel::Restricted:
        return "restricted";
    case WorkspaceTrustLevel::Untrusted:
        return "untrusted";
    }
    return "trusted";
}

std::optional<WorkspaceTrustLevel> WorkspaceTrustLevelFromString(const std::string& value) {
    if (value == "trusted") {
        return WorkspaceTrustLevel::Trusted;
    }
    if (value == "restricted") {
        return WorkspaceTrustLevel::Restricted;
    }
    if (value == "untrusted") {
        return WorkspaceTrustLevel::Untrusted;
    }
    return std::nullopt;
}

}
