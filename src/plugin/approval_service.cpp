#include "vanta/plugin/approval_service.h"

namespace vanta {

void ApprovalService::setAutoApprove(bool autoApprove) {
    autoApprove_ = autoApprove;
}

ApprovalDecision ApprovalService::requestApproval(const ApprovalRequest& request) {
    history_.push_back(request);
    return autoApprove_ ? ApprovalDecision::Allow : ApprovalDecision::Deny;
}

const std::vector<ApprovalRequest>& ApprovalService::history() const {
    return history_;
}

}
