#include "vanta/agent/agent_operation.h"

#include <cstdint>
#include <exception>
#include <future>
#include <memory>
#include <optional>
#include <utility>

#include "vanta/agent/agent_tool_registry.h"
#include "vanta/execution/job_service.h"
#include "vanta/plugin/approval_service.h"
#include "vanta/workspace/capability_service.h"
#include "vanta/workspace/workspace_context.h"
#include "vanta/workspace/workspace_runtime.h"

namespace vanta {
namespace {

void emitEvent(const AgentOperationRequest &request,
               AgentOperationStatus status, std::string message,
               AgentOperationJournal *journal, AgentOperationCallback &onEvent,
               Json data = Json::object()) {
  AgentOperationEvent event{
      .operationId = request.id,
      .kind = request.kind,
      .status = status,
      .message = std::move(message),
      .data = std::move(data),
  };
  if (journal != nullptr) {
    journal->recordEvent(event);
  }
  if (onEvent) {
    onEvent(event);
  }
}

Json diagnosticToJson(const Diagnostic &diagnostic) {
  return Json::object({
      {"file", Json(diagnostic.location.file.toUri().string())},
      {"line", Json(static_cast<std::int64_t>(diagnostic.location.line))},
      {"column", Json(static_cast<std::int64_t>(diagnostic.location.column))},
      {"severity", Json(toString(diagnostic.severity))},
      {"source", Json(diagnostic.source)},
      {"message", Json(diagnostic.message)},
  });
}

Json searchHitToJson(const SearchHit &hit) {
  return Json::object({
      {"file", Json(hit.file.toUri().string())},
      {"line", Json(static_cast<std::int64_t>(hit.line))},
      {"column", Json(static_cast<std::int64_t>(hit.column))},
      {"preview", Json(hit.preview)},
      {"score", Json(static_cast<std::int64_t>(hit.score))},
  });
}

Json searchHitsToJson(const std::vector<SearchHit> &hits) {
  Json::Array values;
  for (const SearchHit &hit : hits) {
    values.push_back(searchHitToJson(hit));
  }
  return Json::array(std::move(values));
}

Json buildResultToJson(const BuildResult &result) {
  Json::Array diagnostics;
  for (const Diagnostic &diagnostic : result.diagnostics) {
    diagnostics.push_back(diagnosticToJson(diagnostic));
  }
  return Json::object({
      {"exitCode", Json(static_cast<std::int64_t>(result.exitCode))},
      {"output", Json(result.output)},
      {"diagnostics", Json::array(std::move(diagnostics))},
      {"events", toJson(result.events)},
  });
}

AgentOperationResult failedResult(AgentOperationKind kind, std::string error) {
  return {
      .ok = false,
      .kind = kind,
      .error = std::move(error),
  };
}

std::string requestSummary(const AgentOperationRequest &request) {
  switch (request.kind) {
  case AgentOperationKind::ReadFile:
  case AgentOperationKind::ProposeFileReplacement:
    return request.file.toUri().string();
  case AgentOperationKind::SearchFiles:
  case AgentOperationKind::SearchText:
    return request.query;
  case AgentOperationKind::ExplainDiagnostic:
    return request.diagnostic.message;
  case AgentOperationKind::RunBuild:
  case AgentOperationKind::RunTest:
    return request.buildTask.target;
  case AgentOperationKind::CallTool:
    return request.toolId;
  }
  return {};
}

std::string resultSummary(const AgentOperationResult &result) {
  if (!result.ok) {
    return result.error;
  }
  if (!result.changeSetId.empty()) {
    return result.changeSetId;
  }
  if (!result.searchHits.empty()) {
    return std::to_string(result.searchHits.size()) + " hits";
  }
  if (!result.text.empty()) {
    return std::to_string(result.text.size()) + " bytes";
  }
  return result.message;
}

Permission permissionForOperation(AgentOperationKind kind) {
  switch (kind) {
  case AgentOperationKind::ReadFile:
  case AgentOperationKind::SearchFiles:
  case AgentOperationKind::SearchText:
  case AgentOperationKind::ExplainDiagnostic:
    return Permission::WorkspaceRead;
  case AgentOperationKind::ProposeFileReplacement:
    return Permission::WorkspaceWrite;
  case AgentOperationKind::RunBuild:
  case AgentOperationKind::RunTest:
    return Permission::ProcessExecute;
  case AgentOperationKind::CallTool:
    return Permission::AgentTool;
  }
  return Permission::WorkspaceRead;
}

bool highRiskOperation(AgentOperationKind kind) {
  return kind == AgentOperationKind::ProposeFileReplacement ||
         kind == AgentOperationKind::RunBuild ||
         kind == AgentOperationKind::RunTest;
}

std::string actionForRequest(const AgentOperationRequest &request) {
  switch (request.kind) {
  case AgentOperationKind::ReadFile:
    return "read " + request.file.toUri().string();
  case AgentOperationKind::SearchFiles:
    return "search files for " + request.query;
  case AgentOperationKind::SearchText:
    return "search text for " + request.query;
  case AgentOperationKind::ExplainDiagnostic:
    return "explain diagnostic";
  case AgentOperationKind::ProposeFileReplacement:
    return "propose replacement for " + request.file.toUri().string();
  case AgentOperationKind::RunBuild:
    return "run build";
  case AgentOperationKind::RunTest:
    return "run test";
  case AgentOperationKind::CallTool:
    return "call agent tool " + request.toolId;
  }
  return toString(request.kind);
}

} // namespace

void AgentOperationJournal::recordStart(const AgentOperationRequest &request) {
  AgentOperationRecord &value = ensure(request.id, request.kind);
  value.status = AgentOperationStatus::Started;
  value.inputSummary = requestSummary(request);
  value.outputSummary.clear();
  value.error.clear();
  value.changeSetId.clear();
  value.ok = false;
  value.events.clear();
}

void AgentOperationJournal::recordEvent(const AgentOperationEvent &event) {
  AgentOperationRecord &value = ensure(event.operationId, event.kind);
  value.status = event.status;
  value.events.push_back(event);
}

void AgentOperationJournal::recordResult(const std::string &operationId,
                                         const AgentOperationResult &result) {
  AgentOperationRecord &value = ensure(operationId, result.kind);
  value.status = result.ok ? AgentOperationStatus::Completed
                           : AgentOperationStatus::Failed;
  value.outputSummary = resultSummary(result);
  value.error = result.error;
  value.changeSetId = result.changeSetId;
  value.ok = result.ok;
}

std::optional<AgentOperationRecord>
AgentOperationJournal::record(const std::string &operationId) const {
  auto it = records_.find(operationId);
  return it == records_.end() ? std::nullopt
                              : std::optional<AgentOperationRecord>(it->second);
}

std::vector<AgentOperationRecord> AgentOperationJournal::records() const {
  std::vector<AgentOperationRecord> values;
  for (const std::string &id : order_) {
    auto it = records_.find(id);
    if (it != records_.end()) {
      values.push_back(it->second);
    }
  }
  return values;
}

void AgentOperationJournal::clear() {
  records_.clear();
  order_.clear();
}

AgentOperationRecord &
AgentOperationJournal::ensure(const std::string &operationId,
                              AgentOperationKind kind) {
  AgentOperationRecord &value = records_[operationId];
  if (value.id.empty()) {
    value.id = operationId;
    value.kind = kind;
    order_.push_back(operationId);
  }
  return value;
}

void AgentOperationService::setJournal(AgentOperationJournal *journal) {
  journal_ = journal;
}

AgentOperationJournal *AgentOperationService::journal() const {
  return journal_;
}

AgentRun AgentOperationService::startRun(WorkspaceContext &context,
                                         AgentRunRequest request,
                                         AgentOperationCallback onEvent) {
  AgentRun run;
  run.id = "agent-run-" + std::to_string(nextRunId_++);
  run.request = std::move(request);
  run.status = AgentRunStatus::CollectingContext;
  appendLog(run, "Collecting IDE context");

  AgentContextRequest contextRequest;
  contextRequest.goal = run.request.goal;
  contextRequest.focusFile = run.request.focusFile;
  contextRequest.diagnostics = run.request.diagnostics;
  run.context = context.agentContext().collect(contextRequest);
  appendLog(run, "Collected " + std::to_string(run.context.items.size()) +
                     " context items");

  if (run.request.targetFile.valid() && !run.request.replacementText.empty()) {
    run.status = AgentRunStatus::Running;
    const auto snapshot =
        context.documents().readSnapshot(run.request.targetFile);
    AgentOperationRequest operation;
    operation.id = run.id + ".replace";
    operation.kind = AgentOperationKind::ProposeFileReplacement;
    operation.file = run.request.targetFile;
    operation.source = "agent";
    operation.title =
        run.request.goal.empty() ? "Agent change set" : run.request.goal;
    operation.replacementText = run.request.replacementText;
    operation.expectedDocumentVersion = snapshot ? snapshot->version : 0;
    AgentOperationResult result =
        execute(context, operation, std::move(onEvent));
    if (!result.ok) {
      run.status = AgentRunStatus::Failed;
      run.error = result.error.empty() ? result.message : result.error;
      appendLog(run, run.error);
    } else {
      run.changeSetId = result.changeSetId;
      run.status = AgentRunStatus::WaitingForApproval;
      appendLog(run, "Created change set " + result.changeSetId);
    }
  } else {
    run.status = AgentRunStatus::Completed;
    appendLog(run, "Run completed without edits");
  }

  runs_[run.id] = run;
  return run;
}

bool AgentOperationService::cancelRun(const std::string &id) {
  AgentRun *found = mutableRun(id);
  if (found == nullptr || found->status == AgentRunStatus::Completed ||
      found->status == AgentRunStatus::Failed ||
      found->status == AgentRunStatus::Cancelled) {
    return false;
  }
  found->status = AgentRunStatus::Cancelled;
  appendLog(*found, "Run cancelled");
  return true;
}

std::optional<AgentRun>
AgentOperationService::run(const std::string &id) const {
  auto found = runs_.find(id);
  return found == runs_.end() ? std::nullopt
                              : std::optional<AgentRun>(found->second);
}

std::vector<AgentRun> AgentOperationService::runs() const {
  std::vector<AgentRun> result;
  for (const auto &[id, run] : runs_) {
    (void)id;
    result.push_back(run);
  }
  return result;
}

AgentOperationResult
AgentOperationService::execute(WorkspaceContext &context,
                               const AgentOperationRequest &request,
                               AgentOperationCallback onEvent) const {
  AgentOperationRequest effective = request;
  if (effective.id.empty()) {
    effective.id = nextOperationId();
  }
  if (journal_ != nullptr) {
    journal_->recordStart(effective);
  }
  auto resultPromise = std::make_shared<std::promise<AgentOperationResult>>();
  std::future<AgentOperationResult> resultFuture = resultPromise->get_future();
  JobHandle handle = context.jobs().submit(
      context.runtime()->async(),
      {
          .kind = JobKind::Agent,
          .title = "Agent operation: " + toString(effective.kind),
      },
      [this, &context, effective, onEvent = std::move(onEvent),
       resultPromise](JobContext &job) mutable {
        (void)job;
        auto finish = [&](AgentOperationResult value) {
          if (journal_ != nullptr) {
            journal_->recordResult(effective.id, value);
          }
          context.capabilities().set({
              .id = "agent.operations",
              .title = "Agent Operations",
              .providerId = "vanta.core",
              .status = CapabilityStatus::Available,
              .message = "Agent operation protocol is available",
              .data = Json::object({
                  {"records",
                   Json(static_cast<std::int64_t>(
                       context.agentOperationJournal().records().size()))},
              }),
          });
          const AgentOperationResult result = std::move(value);
          resultPromise->set_value(result);
          return JobResult{
              .success = result.ok,
              .message = result.ok ? result.message
                                   : (result.error.empty() ? result.message
                                                           : result.error),
              .data = toJson(result),
          };
        };

        try {
          emitEvent(effective, AgentOperationStatus::Started,
                    toString(effective.kind), journal_, onEvent);
          if (context.approvals().requestApproval({
                  .subject = "agent.operation",
                  .permission = permissionForOperation(effective.kind),
                  .action = actionForRequest(effective),
                  .highRisk = highRiskOperation(effective.kind),
              }) == ApprovalDecision::Deny) {
            const std::string error = "Agent operation was denied";
            emitEvent(effective, AgentOperationStatus::Failed, error, journal_,
                      onEvent);
            return finish(failedResult(effective.kind, error));
          }

          switch (effective.kind) {
          case AgentOperationKind::ReadFile: {
            auto text = context.workspaceFiles().readTextFile(effective.file);
            if (!text) {
              const std::string error = "Unable to read file";
              emitEvent(effective, AgentOperationStatus::Failed, error,
                        journal_, onEvent);
              return finish(failedResult(effective.kind, error));
            }
            emitEvent(effective, AgentOperationStatus::Completed, "Read file",
                      journal_, onEvent);
            return finish({
                .ok = true,
                .kind = effective.kind,
                .message = "Read file",
                .text = std::move(*text),
            });
          }
          case AgentOperationKind::SearchFiles: {
            std::vector<SearchHit> hits =
                context.search().searchFiles(effective.query, effective.limit);
            emitEvent(effective, AgentOperationStatus::Completed,
                      "Searched files", journal_, onEvent,
                      searchHitsToJson(hits));
            return finish({
                .ok = true,
                .kind = effective.kind,
                .message = "Searched files",
                .searchHits = std::move(hits),
            });
          }
          case AgentOperationKind::SearchText: {
            std::vector<SearchHit> hits =
                context.search().searchText(effective.query, effective.limit);
            emitEvent(effective, AgentOperationStatus::Completed,
                      "Searched text", journal_, onEvent,
                      searchHitsToJson(hits));
            return finish({
                .ok = true,
                .kind = effective.kind,
                .message = "Searched text",
                .searchHits = std::move(hits),
            });
          }
          case AgentOperationKind::ExplainDiagnostic: {
            std::string explanation =
                context.agent().explainDiagnostic(effective.diagnostic);
            emitEvent(effective, AgentOperationStatus::Completed,
                      "Explained diagnostic", journal_, onEvent,
                      diagnosticToJson(effective.diagnostic));
            return finish({
                .ok = true,
                .kind = effective.kind,
                .message = "Explained diagnostic",
                .text = std::move(explanation),
            });
          }
          case AgentOperationKind::ProposeFileReplacement: {
            auto originalText =
                context.workspaceFiles().readTextFile(effective.file);
            if (!originalText) {
              const std::string error =
                  "Unable to read file before creating change set";
              emitEvent(effective, AgentOperationStatus::Failed, error,
                        journal_, onEvent);
              return finish(failedResult(effective.kind, error));
            }
            const ChangeSet changeSet = context.changes().createFileReplacement(
                effective.file, effective.source,
                effective.title.empty() ? "Agent change" : effective.title,
                std::move(*originalText), effective.replacementText,
                effective.expectedDocumentVersion);
            context.publish({
                .kind = IdeEventKind::ChangeSetProposed,
                .file = effective.file,
                .message = changeSet.title,
            });
            emitEvent(effective, AgentOperationStatus::Completed,
                      "Created change set", journal_, onEvent,
                      Json::object({{"changeSetId", Json(changeSet.id)}}));
            return finish({
                .ok = true,
                .kind = effective.kind,
                .message = "Created change set",
                .changeSetId = changeSet.id,
                .data = Json::object({{"diff", Json(changeSet.unifiedDiff)}}),
            });
          }
          case AgentOperationKind::RunBuild:
          case AgentOperationKind::RunTest: {
            BuildTask task = effective.buildTask;
            task.kind = effective.kind == AgentOperationKind::RunBuild
                            ? BuildTaskKind::Build
                            : BuildTaskKind::Test;
            BuildResult result = context.build().run(
                context, context.workspace().info().rootPath, task,
                [&](const ExecutionEvent &event) {
                  emitEvent(effective, AgentOperationStatus::Progress,
                            event.text, journal_, onEvent, toJson(event));
                });
            const bool ok = result.exitCode == 0;
            emitEvent(effective,
                      ok ? AgentOperationStatus::Completed
                         : AgentOperationStatus::Failed,
                      ok ? "Build operation completed"
                         : "Build operation failed",
                      journal_, onEvent, buildResultToJson(result));
            return finish({
                .ok = ok,
                .kind = effective.kind,
                .error = ok ? "" : result.output,
                .message =
                    ok ? "Build operation completed" : "Build operation failed",
                .buildResult = std::move(result),
            });
          }
          case AgentOperationKind::CallTool: {
            std::optional<Json> output =
                context.agent().callTool(effective.toolId, effective.input);
            if (!output) {
              const std::string error =
                  "Agent tool is not registered: " + effective.toolId;
              emitEvent(effective, AgentOperationStatus::Failed, error,
                        journal_, onEvent);
              return finish(failedResult(effective.kind, error));
            }
            emitEvent(effective, AgentOperationStatus::Completed,
                      "Called agent tool", journal_, onEvent, *output);
            return finish({
                .ok = true,
                .kind = effective.kind,
                .message = "Called agent tool",
                .data = std::move(*output),
            });
          }
          }

          const std::string error = "Unsupported agent operation";
          emitEvent(effective, AgentOperationStatus::Failed, error, journal_,
                    onEvent);
          return finish(failedResult(effective.kind, error));
        } catch (const std::exception &error) {
          emitEvent(effective, AgentOperationStatus::Failed, error.what(),
                    journal_, onEvent);
          return finish(failedResult(effective.kind, error.what()));
        } catch (...) {
          const std::string error = "Agent operation failed";
          emitEvent(effective, AgentOperationStatus::Failed, error, journal_,
                    onEvent);
          return finish(failedResult(effective.kind, error));
        }
      });

  AgentOperationResult result = resultFuture.get();
  handle.wait();
  return result;
}

AgentRun *AgentOperationService::mutableRun(const std::string &id) {
  auto found = runs_.find(id);
  return found == runs_.end() ? nullptr : &found->second;
}

void AgentOperationService::appendLog(AgentRun &run,
                                      std::string message) const {
  run.log.push_back({.message = std::move(message)});
}

std::string AgentOperationService::nextOperationId() const {
  return "operation-" + std::to_string(nextOperationId_++);
}

std::string toString(AgentRunStatus status) {
  switch (status) {
  case AgentRunStatus::Pending:
    return "pending";
  case AgentRunStatus::CollectingContext:
    return "collectingContext";
  case AgentRunStatus::Running:
    return "running";
  case AgentRunStatus::WaitingForApproval:
    return "waitingForApproval";
  case AgentRunStatus::Completed:
    return "completed";
  case AgentRunStatus::Failed:
    return "failed";
  case AgentRunStatus::Cancelled:
    return "cancelled";
  }
  return "pending";
}

std::string toString(AgentOperationKind kind) {
  switch (kind) {
  case AgentOperationKind::ReadFile:
    return "readFile";
  case AgentOperationKind::SearchFiles:
    return "searchFiles";
  case AgentOperationKind::SearchText:
    return "searchText";
  case AgentOperationKind::ExplainDiagnostic:
    return "explainDiagnostic";
  case AgentOperationKind::ProposeFileReplacement:
    return "proposeFileReplacement";
  case AgentOperationKind::RunBuild:
    return "runBuild";
  case AgentOperationKind::RunTest:
    return "runTest";
  case AgentOperationKind::CallTool:
    return "callTool";
  }
  return "readFile";
}

std::string toString(AgentOperationStatus status) {
  switch (status) {
  case AgentOperationStatus::Started:
    return "started";
  case AgentOperationStatus::Progress:
    return "progress";
  case AgentOperationStatus::Completed:
    return "completed";
  case AgentOperationStatus::Failed:
    return "failed";
  }
  return "started";
}

Json toJson(const AgentOperationEvent &event) {
  return Json::object({
      {"operationId", Json(event.operationId)},
      {"kind", Json(toString(event.kind))},
      {"status", Json(toString(event.status))},
      {"message", Json(event.message)},
      {"data", event.data},
  });
}

Json toJson(const AgentRun &run) {
  Json::Array log;
  for (const AgentRunLogEntry &entry : run.log) {
    log.push_back(Json(entry.message));
  }
  return Json::object({
      {"id", Json(run.id)},
      {"goal", Json(run.request.goal)},
      {"status", Json(toString(run.status))},
      {"changeSetId", Json(run.changeSetId)},
      {"error", Json(run.error)},
      {"context", toJson(run.context)},
      {"log", Json::array(std::move(log))},
  });
}

Json toJson(const AgentOperationRecord &record) {
  Json::Array events;
  for (const AgentOperationEvent &event : record.events) {
    events.push_back(toJson(event));
  }
  return Json::object({
      {"id", Json(record.id)},
      {"kind", Json(toString(record.kind))},
      {"status", Json(toString(record.status))},
      {"inputSummary", Json(record.inputSummary)},
      {"outputSummary", Json(record.outputSummary)},
      {"error", Json(record.error)},
      {"changeSetId", Json(record.changeSetId)},
      {"ok", Json(record.ok)},
      {"events", Json::array(std::move(events))},
  });
}

Json toJson(const AgentOperationResult &result) {
  return Json::object({
      {"ok", Json(result.ok)},
      {"kind", Json(toString(result.kind))},
      {"error", Json(result.error)},
      {"message", Json(result.message)},
      {"text", Json(result.text)},
      {"searchHits", searchHitsToJson(result.searchHits)},
      {"changeSetId", Json(result.changeSetId)},
      {"buildResult", buildResultToJson(result.buildResult)},
      {"data", result.data},
  });
}

} // namespace vanta
