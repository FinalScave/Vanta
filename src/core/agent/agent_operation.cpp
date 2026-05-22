#include "vanta/agent/agent_operation.h"

#include <cstdint>
#include <exception>
#include <future>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>

#include "internal/projection.h"
#include "vanta/agent/agent_tool_registry.h"
#include "vanta/execution/job_service.h"
#include "vanta/workspace/approval_service.h"
#include "vanta/workspace/capability_service.h"
#include "vanta/workspace/workspace_context.h"

namespace vanta {
namespace {

constexpr const char* kReadFileTool = "vanta.readFile";
constexpr const char* kSearchFilesTool = "vanta.searchFiles";
constexpr const char* kSearchTextTool = "vanta.searchText";
constexpr const char* kProposeFileReplacementTool = "vanta.proposeFileReplacement";
constexpr const char* kRunBuildTool = "vanta.runBuild";
constexpr const char* kRunTestTool = "vanta.runTest";

AgentOperationEvent CreateEvent(const AgentOperationRequest &request,
                                AgentOperationStatus status,
                                std::string message,
                                std::optional<Value> payload = std::nullopt) {
  return {
      .operation_id = request.id,
      .kind = request.kind,
      .status = status,
      .message = std::move(message),
      .payload = std::move(payload),
  };
}

std::string DiagnosticExplanation(const Diagnostic &diagnostic) {
  std::ostringstream stream;
  stream << diagnostic.location.file.ToUri().ToString() << ':' << diagnostic.location.line << ':' << diagnostic.location.column;
  stream << " reports " << ToString(diagnostic.severity) << " from " << diagnostic.source << ". ";
  stream << diagnostic.message;
  return stream.str();
}

Value ObjectSchema(std::vector<std::string> required) {
  Value::Array required_values;
  for (const std::string &name : required) {
    required_values.push_back(Value(name));
  }
  return Value::ObjectValue({
      {"type", Value("object")},
      {"required", Value::ArrayValue(std::move(required_values))},
  });
}

AgentOperationResult FailedResult(AgentOperationKind kind, std::string error) {
  return {
      .ok = false,
      .kind = kind,
      .error = std::move(error),
  };
}

std::string RequestSummary(const AgentOperationRequest &request) {
  switch (request.kind) {
  case AgentOperationKind::ReadFile:
  case AgentOperationKind::ProposeFileReplacement:
    return request.file.ToUri().ToString();
  case AgentOperationKind::SearchFiles:
  case AgentOperationKind::SearchText:
    return request.query;
  case AgentOperationKind::ExplainDiagnostic:
    return request.diagnostic.message;
  case AgentOperationKind::RunBuild:
  case AgentOperationKind::RunTest:
    return request.build_request.target_id;
  case AgentOperationKind::CallTool:
    return request.tool_id;
  }
  return {};
}

std::string ResultSummary(const AgentOperationResult &result) {
  if (!result.ok) {
    return result.error;
  }
  if (!result.change_set_id.empty()) {
    return result.change_set_id;
  }
  if (!result.search_hits.empty()) {
    return std::to_string(result.search_hits.size()) + " hits";
  }
  if (!result.text.empty()) {
    return std::to_string(result.text.size()) + " bytes";
  }
  return result.message;
}

AccessKind AccessForOperation(AgentOperationKind kind) {
  switch (kind) {
  case AgentOperationKind::ReadFile:
  case AgentOperationKind::SearchFiles:
  case AgentOperationKind::SearchText:
  case AgentOperationKind::ExplainDiagnostic:
    return AccessKind::WorkspaceRead;
  case AgentOperationKind::ProposeFileReplacement:
    return AccessKind::WorkspaceWrite;
  case AgentOperationKind::RunBuild:
  case AgentOperationKind::RunTest:
    return AccessKind::ProcessExecute;
  case AgentOperationKind::CallTool:
    return AccessKind::WorkspaceRead;
  }
  return AccessKind::WorkspaceRead;
}

bool HighRiskOperation(AgentOperationKind kind) {
  return kind == AgentOperationKind::ProposeFileReplacement ||
         kind == AgentOperationKind::RunBuild ||
         kind == AgentOperationKind::RunTest;
}

std::string ActionForRequest(const AgentOperationRequest &request) {
  switch (request.kind) {
  case AgentOperationKind::ReadFile:
    return "read " + request.file.ToUri().ToString();
  case AgentOperationKind::SearchFiles:
    return "search files for " + request.query;
  case AgentOperationKind::SearchText:
    return "search text for " + request.query;
  case AgentOperationKind::ExplainDiagnostic:
    return "explain diagnostic";
  case AgentOperationKind::ProposeFileReplacement:
    return "propose replacement for " + request.file.ToUri().ToString();
  case AgentOperationKind::RunBuild:
    return "run build";
  case AgentOperationKind::RunTest:
    return "run test";
  case AgentOperationKind::CallTool:
    return "call agent tool " + request.tool_id;
  }
  return ToString(request.kind);
}

VirtualFile FileFromInput(WorkspaceContext &context, const Value &input) {
  const std::string file = input.StringValue("file").value_or("");
  return file.empty() ? VirtualFile() : context.CurrentWorkspace().File(file);
}

std::size_t LimitFromInput(const Value &input) {
  const std::int64_t limit = input.IntValue("limit").value_or(50);
  return limit > 0 ? static_cast<std::size_t>(limit) : 50;
}

BuildRequest BuildRequestFromInput(const Value &input, BuildRequestKind kind) {
  BuildRequest request;
  request.kind = kind;
  request.provider_id = input.StringValue("providerId").value_or("");
  request.profile_id = input.StringValue("profileId").value_or("");
  request.target_id = input.StringValue("targetId").value_or("");
  request.execution_target_id = input.StringValue("executionTargetId").value_or("");
  if (auto build_directory = input.StringValue("buildDirectory")) {
    request.build_directory_override = *build_directory;
  }
  return request;
}

} // namespace

void AgentOperationService::RecordStart(const AgentOperationRequest &request) {
  AgentOperationRecord &value = Ensure(request.id, request.kind);
  value.status = AgentOperationStatus::Started;
  value.input_summary = RequestSummary(request);
  value.output_summary.clear();
  value.error.clear();
  value.change_set_id.clear();
  value.ok = false;
  value.events.clear();
}

void AgentOperationService::RecordEvent(const AgentOperationEvent &event) {
  AgentOperationRecord &value = Ensure(event.operation_id, event.kind);
  value.status = event.status;
  value.events.push_back(event);
}

void AgentOperationService::RecordResult(const std::string &operation_id,
                                         const AgentOperationResult &result) {
  AgentOperationRecord &value = Ensure(operation_id, result.kind);
  value.status = result.ok ? AgentOperationStatus::Completed
                           : AgentOperationStatus::Failed;
  value.output_summary = ResultSummary(result);
  value.error = result.error;
  value.change_set_id = result.change_set_id;
  value.ok = result.ok;
}

std::optional<AgentOperationRecord>
AgentOperationService::Record(const std::string &operation_id) const {
  auto it = records_.find(operation_id);
  return it == records_.end() ? std::nullopt
                              : std::optional<AgentOperationRecord>(it->second);
}

std::vector<AgentOperationRecord> AgentOperationService::Records() const {
  std::vector<AgentOperationRecord> values;
  for (const std::string &id : order_) {
    auto it = records_.find(id);
    if (it != records_.end()) {
      values.push_back(it->second);
    }
  }
  return values;
}

void AgentOperationService::Clear() {
  records_.clear();
  order_.clear();
}

AgentOperationRecord &
AgentOperationService::Ensure(const std::string &operation_id,
                              AgentOperationKind kind) {
  AgentOperationRecord &value = records_[operation_id];
  if (value.id.empty()) {
    value.id = operation_id;
    value.kind = kind;
    order_.push_back(operation_id);
  }
  return value;
}

AgentOperationResult
AgentOperationService::Execute(WorkspaceContext &context,
                               const AgentOperationRequest &request,
                               AgentOperationCallback on_event) {
  AgentOperationRequest effective = request;
  if (effective.id.empty()) {
    effective.id = NextOperationId();
  }
  RecordStart(effective);
  auto result_promise = std::make_shared<std::promise<AgentOperationResult>>();
  std::future<AgentOperationResult> result_future = result_promise->get_future();
  JobHandle handle = context.Jobs().Submit(
      {
          .kind = JobKind::Agent,
          .title = "Agent operation: " + ToString(effective.kind),
      },
      [this, &context, effective, on_event = std::move(on_event),
       result_promise](JobContext &job) mutable {
        (void)job;
        auto emit_event = [&](AgentOperationStatus status, std::string message, std::optional<Value> payload = std::nullopt) {
          AgentOperationEvent event = CreateEvent(effective, status, std::move(message), std::move(payload));
          RecordEvent(event);
          if (on_event) {
            on_event(event);
          }
        };
        auto finish = [&](AgentOperationResult value) {
          RecordResult(effective.id, value);
          context.Capabilities().Set({
              .id = "agent.operations",
              .title = "Agent Operations",
              .provider_id = "vanta.core",
              .status = CapabilityStatus::Available,
              .message = "Agent operation protocol is available",
              .details = {{"records", std::to_string(Records().size())}},
          });
          const AgentOperationResult result = std::move(value);
          result_promise->set_value(result);
          return JobResult{
              .success = result.ok,
              .message = result.ok ? result.message
                                   : (result.error.empty() ? result.message
                                                           : result.error),
              .payload = internal::AgentOperationResultProjection(result),
          };
        };

        try {
          emit_event(AgentOperationStatus::Started, ToString(effective.kind));
          if (HighRiskOperation(effective.kind) && context.Approvals().RequestApproval({
                  .actor = {.kind = ApprovalActorKind::Agent, .id = "agent.operation"},
                  .access = AccessForOperation(effective.kind),
                  .action = ActionForRequest(effective),
                  .high_risk = HighRiskOperation(effective.kind),
              }) == ApprovalDecision::Deny) {
            const std::string error = "Agent operation was denied";
            emit_event(AgentOperationStatus::Failed, error);
            return finish(FailedResult(effective.kind, error));
          }

          switch (effective.kind) {
          case AgentOperationKind::ReadFile: {
            auto text = effective.file.ReadText();
            if (!text) {
              const std::string error = "Unable to read file";
              emit_event(AgentOperationStatus::Failed, error);
              return finish(FailedResult(effective.kind, error));
            }
            emit_event(AgentOperationStatus::Completed, "Read file");
            return finish({
                .ok = true,
                .kind = effective.kind,
                .message = "Read file",
                .text = std::move(*text),
            });
          }
          case AgentOperationKind::SearchFiles: {
            IndexQueryResult query = context.Indexes().Query(context, {
                .kind = IndexQueryKind::Files,
                .query = effective.query,
                .limit = effective.limit,
            });
            if (!query.ok) {
              const std::string error = query.error.empty() ? "Unable to search files" : query.error;
              emit_event(AgentOperationStatus::Failed, error);
              return finish(FailedResult(effective.kind, error));
            }
            emit_event(AgentOperationStatus::Completed,
                      "Searched files",
                      internal::IndexHitsProjection(query.hits));
            return finish({
                .ok = true,
                .kind = effective.kind,
                .message = "Searched files",
                .search_hits = std::move(query.hits),
            });
          }
          case AgentOperationKind::SearchText: {
            IndexQueryResult query = context.Indexes().Query(context, {
                .kind = IndexQueryKind::Text,
                .query = effective.query,
                .limit = effective.limit,
            });
            if (!query.ok) {
              const std::string error = query.error.empty() ? "Unable to search text" : query.error;
              emit_event(AgentOperationStatus::Failed, error);
              return finish(FailedResult(effective.kind, error));
            }
            emit_event(AgentOperationStatus::Completed,
                      "Searched text",
                      internal::IndexHitsProjection(query.hits));
            return finish({
                .ok = true,
                .kind = effective.kind,
                .message = "Searched text",
                .search_hits = std::move(query.hits),
            });
          }
          case AgentOperationKind::ExplainDiagnostic: {
            std::string explanation =
                DiagnosticExplanation(effective.diagnostic);
            emit_event(AgentOperationStatus::Completed,
                      "Explained diagnostic",
                      internal::DiagnosticProjection(effective.diagnostic));
            return finish({
                .ok = true,
                .kind = effective.kind,
                .message = "Explained diagnostic",
                .text = std::move(explanation),
            });
          }
          case AgentOperationKind::ProposeFileReplacement: {
            auto original_text =
                effective.file.ReadText();
            if (!original_text) {
              const std::string error =
                  "Unable to read file before creating change set";
              emit_event(AgentOperationStatus::Failed, error);
              return finish(FailedResult(effective.kind, error));
            }
            const ChangeSet change_set = context.Changes().CreateFileReplacement(
                effective.file, effective.source,
                effective.title.empty() ? "Agent change" : effective.title,
                std::move(*original_text), effective.replacement_text,
                effective.expected_document_version);
            context.Events().Publish({
                .kind = IdeEventKind::ChangeSetProposed,
                .file = effective.file,
                .message = change_set.title,
            });
            emit_event(AgentOperationStatus::Completed,
                      "Created change set",
                      Value::ObjectValue({{"changeSetId", Value(change_set.id)}}));
            return finish({
                .ok = true,
                .kind = effective.kind,
                .message = "Created change set",
                .change_set_id = change_set.id,
                .payload = Value::ObjectValue({{"diff", Value(change_set.unified_diff)}}),
            });
          }
          case AgentOperationKind::RunBuild:
          case AgentOperationKind::RunTest: {
            BuildRequest request = effective.build_request;
            request.kind = effective.kind == AgentOperationKind::RunBuild
                            ? BuildRequestKind::Build
                            : BuildRequestKind::Test;
            BuildResult result = context.Build().Run(
                context, request,
                [&](const ExecutionEvent &event) {
                  emit_event(AgentOperationStatus::Progress,
                            event.text, internal::ExecutionEventProjection(event));
                });
            const bool ok = result.exit_code == 0;
            emit_event(
                      ok ? AgentOperationStatus::Completed
                         : AgentOperationStatus::Failed,
                      ok ? "Build operation completed"
                         : "Build operation failed",
                      internal::BuildResultProjection(result));
            return finish({
                .ok = ok,
                .kind = effective.kind,
                .error = ok ? "" : result.output,
                .message =
                    ok ? "Build operation completed" : "Build operation failed",
                .build_result = std::move(result),
            });
          }
          case AgentOperationKind::CallTool: {
            std::optional<Value> output =
                context.AgentTools().CallTool(effective.tool_id, effective.input);
            if (!output) {
              const std::string error =
                  "Agent tool is not registered: " + effective.tool_id;
              emit_event(AgentOperationStatus::Failed, error);
              return finish(FailedResult(effective.kind, error));
            }
            Value output_payload = *output;
            emit_event(AgentOperationStatus::Completed,
                      "Called agent tool", output_payload);
            return finish({
                .ok = true,
                .kind = effective.kind,
                .message = "Called agent tool",
                .payload = output_payload,
            });
          }
          }

          const std::string error = "Unsupported agent operation";
          emit_event(AgentOperationStatus::Failed, error);
          return finish(FailedResult(effective.kind, error));
        } catch (const std::exception &error) {
          emit_event(AgentOperationStatus::Failed, error.what());
          return finish(FailedResult(effective.kind, error.what()));
        } catch (...) {
          const std::string error = "Agent operation failed";
          emit_event(AgentOperationStatus::Failed, error);
          return finish(FailedResult(effective.kind, error));
        }
      });

  AgentOperationResult result = result_future.get();
  handle.Wait();
  return result;
}

std::string AgentOperationService::NextOperationId() {
  return "operation-" + std::to_string(next_operation_id_++);
}

std::string ToString(AgentOperationKind kind) {
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

std::string ToString(AgentOperationStatus status) {
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

std::vector<ModelToolDefinition> AgentOperationToolDefinitions() {
  return {
      {
          .id = kReadFileTool,
          .description = "Read a text file from the current workspace.",
          .input_schema = ObjectSchema({"file"}),
      },
      {
          .id = kSearchFilesTool,
          .description = "Search indexed workspace files by path or title.",
          .input_schema = ObjectSchema({"query"}),
      },
      {
          .id = kSearchTextTool,
          .description = "Search indexed workspace text.",
          .input_schema = ObjectSchema({"query"}),
      },
      {
          .id = kProposeFileReplacementTool,
          .description = "Create an auditable replacement diff for a workspace file.",
          .input_schema = ObjectSchema({"file", "replacementText"}),
      },
      {
          .id = kRunBuildTool,
          .description = "Run the active build provider.",
          .input_schema = ObjectSchema({}),
      },
      {
          .id = kRunTestTool,
          .description = "Run tests through the active build provider.",
          .input_schema = ObjectSchema({}),
      },
  };
}

AgentOperationRequest AgentOperationRequestFromToolCall(WorkspaceContext &context,
                                                        const ModelToolCall &call) {
  AgentOperationRequest operation;
  if (call.tool_id == kReadFileTool) {
    operation.kind = AgentOperationKind::ReadFile;
    operation.file = FileFromInput(context, call.input);
    return operation;
  }
  if (call.tool_id == kSearchFilesTool) {
    operation.kind = AgentOperationKind::SearchFiles;
    operation.query = call.input.StringValue("query").value_or("");
    operation.limit = LimitFromInput(call.input);
    return operation;
  }
  if (call.tool_id == kSearchTextTool) {
    operation.kind = AgentOperationKind::SearchText;
    operation.query = call.input.StringValue("query").value_or("");
    operation.limit = LimitFromInput(call.input);
    return operation;
  }
  if (call.tool_id == kProposeFileReplacementTool) {
    operation.kind = AgentOperationKind::ProposeFileReplacement;
    operation.file = FileFromInput(context, call.input);
    operation.source = "agent";
    operation.title = call.input.StringValue("title").value_or("Agent change");
    operation.replacement_text = call.input.StringValue("replacementText").value_or("");
    operation.expected_document_version = static_cast<std::uint64_t>(call.input.IntValue("expectedDocumentVersion").value_or(0));
    return operation;
  }
  if (call.tool_id == kRunBuildTool) {
    operation.kind = AgentOperationKind::RunBuild;
    operation.build_request = BuildRequestFromInput(call.input, BuildRequestKind::Build);
    return operation;
  }
  if (call.tool_id == kRunTestTool) {
    operation.kind = AgentOperationKind::RunTest;
    operation.build_request = BuildRequestFromInput(call.input, BuildRequestKind::Test);
    return operation;
  }
  operation.kind = AgentOperationKind::CallTool;
  operation.tool_id = call.tool_id;
  operation.input = call.input;
  return operation;
}

} // namespace vanta
