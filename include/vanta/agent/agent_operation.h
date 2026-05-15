#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vanta/agent/agent_context.h"
#include "vanta/core/diagnostic.h"
#include "vanta/execution/build_service.h"
#include "vanta/platform/json.h"
#include "vanta/vfs/virtual_file.h"
#include "vanta/workspace/change_set_service.h"
#include "vanta/workspace/search_service.h"

namespace vanta {

class WorkspaceContext;

enum class AgentRunStatus {
    Pending,
    CollectingContext,
    Running,
    WaitingForApproval,
    Completed,
    Failed,
    Cancelled,
};

struct AgentRunRequest {
    std::string goal;
    VirtualFile focusFile;
    std::vector<Diagnostic> diagnostics;
    VirtualFile targetFile;
    std::string replacementText;
};

struct AgentRunLogEntry {
    std::string message;
};

struct AgentRun {
    std::string id;
    AgentRunRequest request;
    AgentRunStatus status = AgentRunStatus::Pending;
    AgentContext context;
    std::vector<AgentRunLogEntry> log;
    std::string changeSetId;
    std::string error;
};

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
    std::string replacementText;
    std::uint64_t expectedDocumentVersion = 0;
    BuildTask buildTask;
    std::string toolId;
    Json input;
};

struct AgentOperationEvent {
    std::string operationId;
    AgentOperationKind kind = AgentOperationKind::ReadFile;
    AgentOperationStatus status = AgentOperationStatus::Started;
    std::string message;
    Json data;
};

struct AgentOperationResult {
    bool ok = false;
    AgentOperationKind kind = AgentOperationKind::ReadFile;
    std::string error;
    std::string message;
    std::string text;
    std::vector<SearchHit> searchHits;
    std::string changeSetId;
    BuildResult buildResult;
    Json data;
};

using AgentOperationCallback = std::function<void(const AgentOperationEvent&)>;

struct AgentOperationRecord {
    std::string id;
    AgentOperationKind kind = AgentOperationKind::ReadFile;
    AgentOperationStatus status = AgentOperationStatus::Started;
    std::string inputSummary;
    std::string outputSummary;
    std::string error;
    std::string changeSetId;
    bool ok = false;
    std::vector<AgentOperationEvent> events;
};

class AgentOperationJournal {
public:
    void recordStart(const AgentOperationRequest& request);
    void recordEvent(const AgentOperationEvent& event);
    void recordResult(const std::string& operationId, const AgentOperationResult& result);
    std::optional<AgentOperationRecord> record(const std::string& operationId) const;
    std::vector<AgentOperationRecord> records() const;
    void clear();

private:
    AgentOperationRecord& ensure(const std::string& operationId, AgentOperationKind kind);

    std::map<std::string, AgentOperationRecord> records_;
    std::vector<std::string> order_;
};

class AgentOperationService {
public:
    void setJournal(AgentOperationJournal* journal);
    AgentOperationJournal* journal() const;

    AgentRun startRun(
        WorkspaceContext& context,
        AgentRunRequest request,
        AgentOperationCallback onEvent = {});
    bool cancelRun(const std::string& id);
    std::optional<AgentRun> run(const std::string& id) const;
    std::vector<AgentRun> runs() const;

    AgentOperationResult execute(
        WorkspaceContext& context,
        const AgentOperationRequest& request,
        AgentOperationCallback onEvent = {}) const;

private:
    AgentRun* mutableRun(const std::string& id);
    void appendLog(AgentRun& run, std::string message) const;
    std::string nextOperationId() const;

    AgentOperationJournal* journal_ = nullptr;
    std::map<std::string, AgentRun> runs_;
    std::uint64_t nextRunId_ = 1;
    mutable std::uint64_t nextOperationId_ = 1;
};

std::string toString(AgentRunStatus status);
std::string toString(AgentOperationKind kind);
std::string toString(AgentOperationStatus status);
Json toJson(const AgentRun& run);
Json toJson(const AgentOperationRecord& record);
Json toJson(const AgentOperationEvent& event);
Json toJson(const AgentOperationResult& result);

}
