#pragma once

#include <cstdint>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "vanta/agent/agent_context.h"
#include "vanta/agent/model_service.h"
#include "vanta/core/event.h"
#include "vanta/core/registration.h"
#include "vanta/core/text.h"
#include "vanta/core/value.h"
#include "vanta/execution/job_service.h"

namespace vanta {

class WorkspaceContext;

enum class AgentSessionStatus {
    Pending,
    CollectingContext,
    Planning,
    Running,
    Completed,
    Failed,
    Cancelled,
};

enum class AgentWorkKind {
    General,
    Code,
    Review,
    Documentation,
    Security,
    Performance,
};

enum class OwnershipScopeKind {
    Workspace,
    Project,
    Directory,
    File,
    Symbol,
    ChangeSet,
};

struct OwnershipScope {
    OwnershipScopeKind kind = OwnershipScopeKind::Workspace;
    VirtualFile file;
    std::string symbol_id;
    std::string change_set_id;
    std::string description;
};

struct AgentParticipant {
    std::string id;
    std::string display_name;
    std::string model_id;
    std::string role;
    std::vector<OwnershipScope> ownership_scopes;
};

enum class AgentFindingSeverity {
    Info,
    Warning,
    Error,
};

struct AgentFinding {
    std::string id;
    std::string session_id;
    AgentWorkKind category = AgentWorkKind::General;
    AgentFindingSeverity severity = AgentFindingSeverity::Info;
    std::string title;
    std::string message;
    VirtualFile file;
    TextRange range;
    std::vector<std::string> tags;
};

enum class AgentProposalStatus {
    Pending,
    Accepted,
    Rejected,
    Applied,
    Superseded,
};

struct AgentProposal {
    std::string id;
    std::string session_id;
    std::string title;
    std::string summary;
    std::string change_set_id;
    AgentProposalStatus status = AgentProposalStatus::Pending;
    std::vector<OwnershipScope> affected_scopes;
    std::vector<std::string> finding_ids;
};

struct AgentSessionRequest {
    std::string goal;
    std::string model_id;
    AgentWorkKind work_kind = AgentWorkKind::General;
    VirtualFile focus_file;
    std::vector<Diagnostic> diagnostics;
    std::vector<AgentParticipant> participants;
    std::vector<OwnershipScope> ownership_scopes;
};

struct AgentRuntimeEvent {
    std::string session_id;
    AgentSessionStatus status = AgentSessionStatus::Pending;
    std::string message;
    std::optional<Value> payload;
};

struct AgentSession {
    std::string id;
    AgentSessionRequest request;
    AgentSessionStatus status = AgentSessionStatus::Pending;
    AgentContext context;
    std::vector<AgentRuntimeEvent> events;
    std::vector<std::string> operation_ids;
    std::vector<std::string> finding_ids;
    std::vector<std::string> proposal_ids;
    std::string model_response;
    std::string change_set_id;
    std::string error;
    JobId job_id = 0;
};

using AgentRuntimeCallback = std::function<void(const AgentRuntimeEvent&)>;

struct AgentSessionChangeEvent {
    AgentSession session;
    AgentRuntimeEvent event;
};

class AgentRuntime {
public:
    static constexpr const char* kServiceId = "vanta.agent.runtime";

    AgentSession StartSession(
        WorkspaceContext& context,
        AgentSessionRequest request,
        AgentRuntimeCallback on_event = {});
    bool CancelSession(const std::string& session_id);
    std::optional<AgentSession> Session(const std::string& session_id) const;
    std::vector<AgentSession> Sessions() const;
    std::string AddFinding(AgentFinding finding);
    std::vector<AgentFinding> Findings() const;
    std::vector<AgentFinding> FindingsForSession(const std::string& session_id) const;
    std::string AddProposal(AgentProposal proposal);
    std::vector<AgentProposal> Proposals() const;
    std::vector<AgentProposal> ProposalsForSession(const std::string& session_id) const;
    std::uint64_t OnDidChangeSession(EventBus<AgentSessionChangeEvent>::Listener listener);
    void RemoveSessionListener(std::uint64_t listener_id);
    void Clear();

private:
    AgentSession CreateSession(AgentSessionRequest request);
    JobResult RunSession(WorkspaceContext& context, const std::string& session_id, AgentRuntimeCallback on_event, JobContext& job);
    void SetSessionJobId(const std::string& session_id, JobId job_id);
    bool SessionCancelled(const std::string& session_id) const;
    void AddOperationId(const std::string& session_id, std::string operation_id);
    void Update(const std::string& session_id, AgentSessionStatus status, std::string message, AgentRuntimeCallback& on_event, std::optional<Value> payload = std::nullopt);
    void ExecuteToolCalls(WorkspaceContext& context, const std::string& session_id, const ModelResponse& response, AgentRuntimeCallback& on_event);

    std::uint64_t next_session_id_ = 1;
    std::uint64_t next_finding_id_ = 1;
    std::uint64_t next_proposal_id_ = 1;
    std::map<std::string, AgentSession> sessions_;
    std::map<std::string, AgentFinding> findings_;
    std::map<std::string, AgentProposal> proposals_;
    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    EventBus<AgentSessionChangeEvent> on_did_change_;
};

std::string ToString(AgentSessionStatus status);
std::string ToString(AgentWorkKind kind);
std::string ToString(OwnershipScopeKind kind);
std::string ToString(AgentFindingSeverity severity);
std::string ToString(AgentProposalStatus status);

}
