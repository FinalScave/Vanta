#pragma once

#include <string>
#include <vector>

#include "vanta/workspace/access_control.h"

namespace vanta {

class WorkspaceTrustService;

enum class ApprovalActorKind {
    User,
    Agent,
    Plugin,
    Core,
};

struct ApprovalActor {
    ApprovalActorKind kind = ApprovalActorKind::Core;
    std::string id;
};

enum class ApprovalDecision {
    Allow,
    Deny,
};

struct ApprovalRequest {
    ApprovalActor actor;
    AccessKind access = AccessKind::WorkspaceRead;
    std::string action;
    bool high_risk = false;
};

class ApprovalService {
public:
    static constexpr const char* kServiceId = "vanta.approvals";

    explicit ApprovalService(WorkspaceTrustService& trust);
    void SetAutoApprove(bool auto_approve);
    ApprovalDecision RequestApproval(const ApprovalRequest& request);
    const std::vector<ApprovalRequest>& History() const;

private:
    WorkspaceTrustService& trust_;
    bool auto_approve_ = true;
    std::vector<ApprovalRequest> history_;
};

std::string ToString(ApprovalActorKind kind);

}
