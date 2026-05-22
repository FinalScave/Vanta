#include "vanta/agent/agent_runtime.h"

#include <sstream>
#include <utility>

#include "internal/projection.h"
#include "vanta/agent/agent_operation.h"
#include "vanta/agent/agent_tool_registry.h"
#include "vanta/core/json_codec.h"
#include "vanta/workspace/workspace_context.h"

namespace vanta {
namespace {

bool ContainsTool(const std::vector<ModelToolDefinition>& tools, const std::string& id) {
    for (const ModelToolDefinition& tool : tools) {
        if (tool.id == id) {
            return true;
        }
    }
    return false;
}

std::string FormatAgentContext(const AgentContext& context) {
    std::ostringstream stream;
    for (const AgentContextItem& item : context.items) {
        stream << '[' << item.kind << "] " << item.title << '\n';
        if (item.file.Valid()) {
            stream << "file: " << item.file.ToUri().ToString() << '\n';
        }
        if (!item.text.empty()) {
            stream << item.text << '\n';
        } else if (item.payload) {
            stream << ValueToJsonText(*item.payload) << '\n';
        }
        stream << '\n';
    }
    return stream.str();
}

}

AgentSession AgentRuntime::StartSession(WorkspaceContext& context, AgentSessionRequest request, AgentRuntimeCallback on_event) {
    AgentSession session = CreateSession(std::move(request));
    const std::string session_id = session.id;
    const JobId job_id = context.Jobs().Create(JobKind::Agent, "Agent session: " + session.request.goal);
    context.Jobs().SetCancellable(job_id, true);
    context.Jobs().SetCancelHandler(job_id, [this, session_id] {
        CancelSession(session_id);
    });
    SetSessionJobId(session_id, job_id);
    context.Jobs().Submit(job_id, [this, &context, session_id, on_event = std::move(on_event)](JobContext& job) mutable {
        return RunSession(context, session_id, std::move(on_event), job);
    });
    return Session(session_id).value_or(std::move(session));
}

JobResult AgentRuntime::RunSession(WorkspaceContext& context, const std::string& session_id, AgentRuntimeCallback on_event, JobContext& job) {
    if (job.CancellationRequested() || SessionCancelled(session_id)) {
        Update(session_id, AgentSessionStatus::Cancelled, "Agent session cancelled", on_event);
        return {.success = false, .message = "Agent session cancelled"};
    }
    std::optional<AgentSession> snapshot = Session(session_id);
    if (!snapshot) {
        return {.success = false, .message = "Agent session not found"};
    }
    Update(session_id, AgentSessionStatus::CollectingContext, "Collecting context", on_event);

    AgentContextRequest context_request;
    context_request.goal = snapshot->request.goal;
    context_request.focus_file = snapshot->request.focus_file;
    context_request.diagnostics = snapshot->request.diagnostics;
    AgentContext collected_context = context.AgentContext().Collect(context_request, context);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            it->second.context = collected_context;
        }
    }
    Update(session_id, AgentSessionStatus::Planning, "Requesting model plan", on_event, Value::ObjectValue({
        {"contextItems", Value(static_cast<std::int64_t>(collected_context.items.size()))},
    }));

    ModelRequest model_request;
    model_request.model_id = snapshot->request.model_id;
    model_request.tools = AgentOperationToolDefinitions();
    for (const AgentToolDefinition& tool : context.AgentTools().Tools()) {
        if (!ContainsTool(model_request.tools, tool.id)) {
            model_request.tools.push_back({
                .id = tool.id,
                .description = tool.description,
                .input_schema = tool.input_schema,
            });
        }
    }
    std::string user_message = "Goal:\n" + snapshot->request.goal;
    const std::string context_text = FormatAgentContext(collected_context);
    if (!context_text.empty()) {
        user_message += "\n\nIDE context:\n" + context_text;
    }
    model_request.messages = {
        {
            .role = ModelMessageRole::System,
            .content = "You are Vanta's coding agent. Use IDE operation tools for workspace reads, searches, builds, tests, and proposed edits.",
        },
        {
            .role = ModelMessageRole::User,
            .content = std::move(user_message),
        },
    };

    ModelResponse response = context.Models().Complete(model_request, [&](const ModelStreamEvent& event) {
        if (event.kind == ModelStreamEventKind::Delta && !event.text.empty()) {
            Update(session_id, AgentSessionStatus::Running, event.text, on_event, Value::ObjectValue({
                {"modelEvent", Value(ToString(event.kind))},
            }));
        }
    });
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            it->second.model_response = response.content;
        }
    }
    if (!response.ok) {
        const std::string error = response.error.empty() ? "Model request failed" : response.error;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = sessions_.find(session_id);
            if (it != sessions_.end()) {
                it->second.error = error;
            }
        }
        Update(session_id, AgentSessionStatus::Failed, error, on_event);
        return {.success = false, .message = error};
    }

    if (job.CancellationRequested() || SessionCancelled(session_id)) {
        Update(session_id, AgentSessionStatus::Cancelled, "Agent session cancelled", on_event);
        return {.success = false, .message = "Agent session cancelled"};
    }

    ExecuteToolCalls(context, session_id, response, on_event);
    snapshot = Session(session_id);
    if (snapshot && snapshot->status == AgentSessionStatus::Failed) {
        return {.success = false, .message = snapshot->error};
    }
    if (snapshot && snapshot->status == AgentSessionStatus::Cancelled) {
        return {.success = false, .message = "Agent session cancelled"};
    }
    Update(session_id, AgentSessionStatus::Completed, "Agent session completed", on_event);
    return {.success = true, .message = "Agent session completed"};
}

bool AgentRuntime::CancelSession(const std::string& session_id) {
    AgentSession snapshot;
    AgentRuntimeEvent event;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) {
            return false;
        }
        AgentSession& session = it->second;
        if (session.status == AgentSessionStatus::Completed ||
            session.status == AgentSessionStatus::Failed ||
            session.status == AgentSessionStatus::Cancelled) {
            return false;
        }
        session.status = AgentSessionStatus::Cancelled;
        event = {
            .session_id = session.id,
            .status = AgentSessionStatus::Cancelled,
            .message = "Agent session cancelled",
        };
        session.events.push_back(event);
        snapshot = session;
    }
    changed_.notify_all();
    on_did_change_.Publish({.session = snapshot, .event = event});
    return true;
}

std::optional<AgentSession> AgentRuntime::Session(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    return it == sessions_.end() ? std::nullopt : std::optional<AgentSession>(it->second);
}

std::vector<AgentSession> AgentRuntime::Sessions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AgentSession> result;
    for (const auto& [id, session] : sessions_) {
        (void)id;
        result.push_back(session);
    }
    return result;
}

std::string AgentRuntime::AddFinding(AgentFinding finding) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (finding.id.empty()) {
        finding.id = "agent-finding-" + std::to_string(next_finding_id_++);
    }
    const std::string id = finding.id;
    if (!finding.session_id.empty()) {
        auto session = sessions_.find(finding.session_id);
        if (session != sessions_.end()) {
            session->second.finding_ids.push_back(id);
        }
    }
    findings_[id] = std::move(finding);
    changed_.notify_all();
    return id;
}

std::vector<AgentFinding> AgentRuntime::Findings() const {
    std::vector<AgentFinding> result;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, finding] : findings_) {
        (void)id;
        result.push_back(finding);
    }
    return result;
}

std::vector<AgentFinding> AgentRuntime::FindingsForSession(const std::string& session_id) const {
    std::vector<AgentFinding> result;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, finding] : findings_) {
        (void)id;
        if (finding.session_id == session_id) {
            result.push_back(finding);
        }
    }
    return result;
}

std::string AgentRuntime::AddProposal(AgentProposal proposal) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (proposal.id.empty()) {
        proposal.id = "agent-proposal-" + std::to_string(next_proposal_id_++);
    }
    const std::string id = proposal.id;
    if (!proposal.session_id.empty()) {
        auto session = sessions_.find(proposal.session_id);
        if (session != sessions_.end()) {
            session->second.proposal_ids.push_back(id);
        }
    }
    proposals_[id] = std::move(proposal);
    changed_.notify_all();
    return id;
}

std::vector<AgentProposal> AgentRuntime::Proposals() const {
    std::vector<AgentProposal> result;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, proposal] : proposals_) {
        (void)id;
        result.push_back(proposal);
    }
    return result;
}

std::vector<AgentProposal> AgentRuntime::ProposalsForSession(const std::string& session_id) const {
    std::vector<AgentProposal> result;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, proposal] : proposals_) {
        (void)id;
        if (proposal.session_id == session_id) {
            result.push_back(proposal);
        }
    }
    return result;
}

std::uint64_t AgentRuntime::OnDidChangeSession(EventBus<AgentSessionChangeEvent>::Listener listener) {
    return on_did_change_.Subscribe(std::move(listener));
}

void AgentRuntime::RemoveSessionListener(std::uint64_t listener_id) {
    on_did_change_.Unsubscribe(listener_id);
}

void AgentRuntime::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.clear();
    findings_.clear();
    proposals_.clear();
    changed_.notify_all();
}

AgentSession AgentRuntime::CreateSession(AgentSessionRequest request) {
    AgentSession session;
    session.request = std::move(request);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        session.id = "agent-session-" + std::to_string(next_session_id_++);
        sessions_[session.id] = session;
    }
    changed_.notify_all();
    return session;
}

void AgentRuntime::SetSessionJobId(const std::string& session_id, JobId job_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        it->second.job_id = job_id;
    }
}

bool AgentRuntime::SessionCancelled(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    return it != sessions_.end() && it->second.status == AgentSessionStatus::Cancelled;
}

void AgentRuntime::AddOperationId(const std::string& session_id, std::string operation_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        it->second.operation_ids.push_back(std::move(operation_id));
    }
}

void AgentRuntime::Update(const std::string& session_id, AgentSessionStatus status, std::string message, AgentRuntimeCallback& on_event, std::optional<Value> payload) {
    AgentSession snapshot;
    AgentRuntimeEvent event;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) {
            return;
        }
        AgentSession& session = it->second;
        if (session.status == AgentSessionStatus::Cancelled && status != AgentSessionStatus::Cancelled) {
            return;
        }
        session.status = status;
        event = {
            .session_id = session.id,
            .status = status,
            .message = std::move(message),
            .payload = std::move(payload),
        };
        session.events.push_back(event);
        snapshot = session;
    }
    changed_.notify_all();
    on_did_change_.Publish({.session = snapshot, .event = event});
    if (on_event) {
        on_event(event);
    }
}

void AgentRuntime::ExecuteToolCalls(WorkspaceContext& context, const std::string& session_id, const ModelResponse& response, AgentRuntimeCallback& on_event) {
    int index = 0;
    for (const ModelToolCall& call : response.tool_calls) {
        if (SessionCancelled(session_id)) {
            return;
        }
        ++index;
        Update(session_id, AgentSessionStatus::Running, "Calling agent tool " + call.tool_id, on_event, Value::ObjectValue({
            {"toolId", Value(call.tool_id)},
        }));
        AgentOperationRequest operation = AgentOperationRequestFromToolCall(context, call);
        operation.id = session_id + ".tool." + std::to_string(index);
        const AgentOperationResult result = context.AgentOperations().Execute(context, operation, [&](const AgentOperationEvent& event) {
            Update(session_id, AgentSessionStatus::Running, event.message, on_event, internal::AgentOperationEventProjection(event));
        });
        AddOperationId(session_id, operation.id);
        if (!result.ok) {
            const std::string error = result.error.empty() ? result.message : result.error;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = sessions_.find(session_id);
                if (it != sessions_.end()) {
                    it->second.error = error;
                }
            }
            Update(session_id, AgentSessionStatus::Failed, error, on_event);
            return;
        }
    }
}

std::string ToString(AgentSessionStatus status) {
    switch (status) {
    case AgentSessionStatus::Pending:
        return "pending";
    case AgentSessionStatus::CollectingContext:
        return "collectingContext";
    case AgentSessionStatus::Planning:
        return "planning";
    case AgentSessionStatus::Running:
        return "running";
    case AgentSessionStatus::Completed:
        return "completed";
    case AgentSessionStatus::Failed:
        return "failed";
    case AgentSessionStatus::Cancelled:
        return "cancelled";
    }
    return "pending";
}

std::string ToString(AgentWorkKind kind) {
    switch (kind) {
    case AgentWorkKind::General:
        return "general";
    case AgentWorkKind::Code:
        return "code";
    case AgentWorkKind::Review:
        return "review";
    case AgentWorkKind::Documentation:
        return "documentation";
    case AgentWorkKind::Security:
        return "security";
    case AgentWorkKind::Performance:
        return "performance";
    }
    return "general";
}

std::string ToString(OwnershipScopeKind kind) {
    switch (kind) {
    case OwnershipScopeKind::Workspace:
        return "workspace";
    case OwnershipScopeKind::Project:
        return "project";
    case OwnershipScopeKind::Directory:
        return "directory";
    case OwnershipScopeKind::File:
        return "file";
    case OwnershipScopeKind::Symbol:
        return "symbol";
    case OwnershipScopeKind::ChangeSet:
        return "changeSet";
    }
    return "workspace";
}

std::string ToString(AgentFindingSeverity severity) {
    switch (severity) {
    case AgentFindingSeverity::Info:
        return "info";
    case AgentFindingSeverity::Warning:
        return "warning";
    case AgentFindingSeverity::Error:
        return "error";
    }
    return "info";
}

std::string ToString(AgentProposalStatus status) {
    switch (status) {
    case AgentProposalStatus::Pending:
        return "pending";
    case AgentProposalStatus::Accepted:
        return "accepted";
    case AgentProposalStatus::Rejected:
        return "rejected";
    case AgentProposalStatus::Applied:
        return "applied";
    case AgentProposalStatus::Superseded:
        return "superseded";
    }
    return "pending";
}

}
