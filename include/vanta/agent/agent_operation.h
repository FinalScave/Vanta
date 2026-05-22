#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vanta/core/diagnostic.h"
#include "vanta/execution/build_service.h"
#include "vanta/core/value.h"
#include "vanta/agent/model_service.h"
#include "vanta/vfs/virtual_file.h"
#include "vanta/workspace/change_set_service.h"
#include "vanta/workspace/index_service.h"

namespace vanta {

class WorkspaceContext;

enum class AgentOperationKind {
    ReadFile,
    SearchFiles,
    SearchText,
    ExplainDiagnostic,
    ProposeFileReplacement,
    RunBuild,
    RunTest,
    CallTool,
};

enum class AgentOperationStatus {
    Started,
    Progress,
    Completed,
    Failed,
};

struct AgentOperationRequest {
    std::string id;
    AgentOperationKind kind = AgentOperationKind::ReadFile;
    VirtualFile file;
    std::string query;
    std::size_t limit = 50;
    Diagnostic diagnostic;
    std::string source = "agent";
    std::string title;
    std::string replacement_text;
    std::uint64_t expected_document_version = 0;
    BuildRequest build_request;
    std::string tool_id;
    Value input = Value::ObjectValue();
};

struct AgentOperationEvent {
    std::string operation_id;
    AgentOperationKind kind = AgentOperationKind::ReadFile;
    AgentOperationStatus status = AgentOperationStatus::Started;
    std::string message;
    std::optional<Value> payload;
};

struct AgentOperationResult {
    bool ok = false;
    AgentOperationKind kind = AgentOperationKind::ReadFile;
    std::string error;
    std::string message;
    std::string text;
    std::vector<IndexHit> search_hits;
    std::string change_set_id;
    BuildResult build_result;
    std::optional<Value> payload;
};

using AgentOperationCallback = std::function<void(const AgentOperationEvent&)>;

struct AgentOperationRecord {
    std::string id;
    AgentOperationKind kind = AgentOperationKind::ReadFile;
    AgentOperationStatus status = AgentOperationStatus::Started;
    std::string input_summary;
    std::string output_summary;
    std::string error;
    std::string change_set_id;
    bool ok = false;
    std::vector<AgentOperationEvent> events;
};

class AgentOperationService {
public:
    static constexpr const char* kServiceId = "vanta.agent.operations";

    std::optional<AgentOperationRecord> Record(const std::string& operation_id) const;
    std::vector<AgentOperationRecord> Records() const;
    void Clear();

    AgentOperationResult Execute(
        WorkspaceContext& context,
        const AgentOperationRequest& request,
        AgentOperationCallback on_event = {});

private:
    void RecordStart(const AgentOperationRequest& request);
    void RecordEvent(const AgentOperationEvent& event);
    void RecordResult(const std::string& operation_id, const AgentOperationResult& result);
    AgentOperationRecord& Ensure(const std::string& operation_id, AgentOperationKind kind);
    std::string NextOperationId();

    std::map<std::string, AgentOperationRecord> records_;
    std::vector<std::string> order_;
    std::uint64_t next_operation_id_ = 1;
};

std::string ToString(AgentOperationKind kind);
std::string ToString(AgentOperationStatus status);
std::vector<ModelToolDefinition> AgentOperationToolDefinitions();
AgentOperationRequest AgentOperationRequestFromToolCall(WorkspaceContext& context, const ModelToolCall& call);

}
