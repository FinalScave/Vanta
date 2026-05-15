#pragma once

#include <string>
#include <vector>

#include "vanta/plugin/permissions.h"

namespace vanta {

enum class ApprovalDecision {
    Allow,
    Deny,
};

struct ApprovalRequest {
    std::string subject;
    Permission permission = Permission::WorkspaceRead;
    std::string action;
    bool highRisk = false;
};

class ApprovalService {
public:
    void setAutoApprove(bool autoApprove);
    ApprovalDecision requestApproval(const ApprovalRequest& request);
    const std::vector<ApprovalRequest>& history() const;

private:
    bool autoApprove_ = true;
    std::vector<ApprovalRequest> history_;
};

}
