#include "vanta/workspace/approval_service.h"

#include "vanta/workspace/workspace_trust.h"

namespace vanta {

ApprovalService::ApprovalService(WorkspaceTrustService& trust)
    : trust_(trust) {
}

void ApprovalService::SetAutoApprove(bool auto_approve) {
    auto_approve_ = auto_approve;
}

ApprovalDecision ApprovalService::RequestApproval(const ApprovalRequest& request) {
    history_.push_back(request);
    if (!trust_.Allows(request.access, request.high_risk)) {
        return ApprovalDecision::Deny;
    }
    return auto_approve_ ? ApprovalDecision::Allow : ApprovalDecision::Deny;
}

const std::vector<ApprovalRequest>& ApprovalService::History() const {
    return history_;
}

std::string ToString(ApprovalActorKind kind) {
    switch (kind) {
    case ApprovalActorKind::User:
        return "user";
    case ApprovalActorKind::Agent:
        return "agent";
    case ApprovalActorKind::Plugin:
        return "plugin";
    case ApprovalActorKind::Core:
        return "core";
    }
    return "core";
}

}
