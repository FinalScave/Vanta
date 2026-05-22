#include "internal/projection.h"

#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>

#include "vanta/core/diagnostic.h"

namespace vanta::internal {
namespace {

Value StringsProjection(const std::vector<std::string>& values) {
    Value::Array result;
    for (const std::string& value : values) {
        result.push_back(Value(value));
    }
    return Value::ArrayValue(std::move(result));
}

Value AgentContextItemProjection(const AgentContextItem& item) {
    return Value::ObjectValue({
        {"providerId", Value(item.provider_id)},
        {"kind", Value(item.kind)},
        {"title", Value(item.title)},
        {"file", Value(item.file.ToUri().ToString())},
        {"text", Value(item.text)},
        {"data", item.payload.value_or(Value::ObjectValue())},
    });
}

Value LanguageTraceProjection(const LanguageRequestTrace& trace) {
    return Value::ObjectValue({
        {"id", Value(static_cast<std::int64_t>(trace.id))},
        {"method", Value(trace.method)},
        {"rawRequest", Value(trace.raw_request)},
        {"rawResponse", Value(trace.raw_response)},
    });
}

Value CompletionItemsProjection(const std::vector<CompletionItem>& items) {
    Value::Array values;
    for (const CompletionItem& item : items) {
        values.push_back(Value::ObjectValue({
            {"label", Value(item.label)},
            {"insertText", Value(item.insert_text)},
            {"detail", Value(item.detail)},
            {"documentation", Value(item.documentation)},
        }));
    }
    return Value::ArrayValue(std::move(values));
}

Value LocationsProjection(const std::vector<Location>& locations) {
    Value::Array values;
    for (const Location& location : locations) {
        values.push_back(Value::ObjectValue({
            {"file", Value(location.file.ToUri().ToString())},
            {"range", Value::ObjectValue({
                {"start", Value::ObjectValue({
                    {"line", Value(static_cast<std::int64_t>(location.range.start.line))},
                    {"character", Value(static_cast<std::int64_t>(location.range.start.character))},
                })},
                {"end", Value::ObjectValue({
                    {"line", Value(static_cast<std::int64_t>(location.range.end.line))},
                    {"character", Value(static_cast<std::int64_t>(location.range.end.character))},
                })},
            })},
        }));
    }
    return Value::ArrayValue(std::move(values));
}

Value TokenDataProjection(const std::vector<std::int64_t>& data) {
    Value::Array values;
    for (std::int64_t value : data) {
        values.push_back(Value(value));
    }
    return Value::ArrayValue(std::move(values));
}

Value LanguagePayloadProjection(const CodeIntelligencePayload& payload) {
    return std::visit([](const auto& value) -> Value {
        using Payload = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Payload, std::monostate>) {
            return Value(nullptr);
        } else if constexpr (std::is_same_v<Payload, CompletionList>) {
            return LanguageCompletionProjection(value);
        } else if constexpr (std::is_same_v<Payload, HoverResult>) {
            return LanguageHoverProjection(value);
        } else if constexpr (std::is_same_v<Payload, LocationResult>) {
            return LanguageLocationProjection(value);
        } else {
            return LanguageSemanticTokensProjection(value);
        }
    }, payload);
}

}

Value DiagnosticProjection(const Diagnostic& diagnostic) {
    return Value::ObjectValue({
        {"file", Value(diagnostic.location.file.ToUri().ToString())},
        {"line", Value(static_cast<std::int64_t>(diagnostic.location.line))},
        {"column", Value(static_cast<std::int64_t>(diagnostic.location.column))},
        {"severity", Value(ToString(diagnostic.severity))},
        {"source", Value(diagnostic.source)},
        {"message", Value(diagnostic.message)},
    });
}

Value DiagnosticsProjection(const std::vector<Diagnostic>& diagnostics) {
    Value::Array values;
    for (const Diagnostic& diagnostic : diagnostics) {
        values.push_back(DiagnosticProjection(diagnostic));
    }
    return Value::ArrayValue(std::move(values));
}

Value IndexHitProjection(const IndexHit& hit) {
    return Value::ObjectValue({
        {"file", Value(hit.file.ToUri().ToString())},
        {"startLine", Value(static_cast<std::int64_t>(hit.range.start.line))},
        {"startCharacter", Value(static_cast<std::int64_t>(hit.range.start.character))},
        {"endLine", Value(static_cast<std::int64_t>(hit.range.end.line))},
        {"endCharacter", Value(static_cast<std::int64_t>(hit.range.end.character))},
        {"title", Value(hit.title)},
        {"preview", Value(hit.preview)},
        {"providerId", Value(hit.provider_id)},
        {"score", Value(static_cast<std::int64_t>(hit.score))},
    });
}

Value IndexHitsProjection(const std::vector<IndexHit>& hits) {
    Value::Array values;
    for (const IndexHit& hit : hits) {
        values.push_back(IndexHitProjection(hit));
    }
    return Value::ArrayValue(std::move(values));
}

Value BuildResultProjection(const BuildResult& result) {
    return Value::ObjectValue({
        {"exitCode", Value(static_cast<std::int64_t>(result.exit_code))},
        {"output", Value(result.output)},
        {"diagnostics", DiagnosticsProjection(result.diagnostics)},
        {"events", ExecutionEventsProjection(result.events)},
    });
}

Value AgentContextProjection(const AgentContext& context) {
    Value::Array items;
    for (const AgentContextItem& item : context.items) {
        items.push_back(AgentContextItemProjection(item));
    }
    return Value::ObjectValue({
        {"items", Value::ArrayValue(std::move(items))},
    });
}

Value AgentOperationEventProjection(const AgentOperationEvent& event) {
    return Value::ObjectValue({
        {"operationId", Value(event.operation_id)},
        {"kind", Value(ToString(event.kind))},
        {"status", Value(ToString(event.status))},
        {"message", Value(event.message)},
        {"data", event.payload.value_or(Value::ObjectValue())},
    });
}

Value AgentOperationResultProjection(const AgentOperationResult& result) {
    return Value::ObjectValue({
        {"ok", Value(result.ok)},
        {"kind", Value(ToString(result.kind))},
        {"error", Value(result.error)},
        {"message", Value(result.message)},
        {"text", Value(result.text)},
        {"searchHits", IndexHitsProjection(result.search_hits)},
        {"changeSetId", Value(result.change_set_id)},
        {"buildResult", BuildResultProjection(result.build_result)},
        {"data", result.payload.value_or(Value::ObjectValue())},
    });
}

Value ExecutionTargetProjection(const ExecutionTarget& target) {
    return Value::ObjectValue({
        {"id", Value(target.id)},
        {"executorId", Value(target.executor_id)},
        {"name", Value(target.name)},
        {"kind", Value(ToString(target.kind))},
        {"capabilities", StringsProjection(target.capabilities)},
    });
}

Value ExecutionEventProjection(const ExecutionEvent& event) {
    return Value::ObjectValue({
        {"kind", Value(ToString(event.kind))},
        {"jobId", Value(static_cast<std::int64_t>(event.job_id))},
        {"executorId", Value(event.executor_id)},
        {"targetId", Value(event.target_id)},
        {"text", Value(event.text)},
        {"progress", Value(event.progress)},
        {"exitCode", Value(static_cast<std::int64_t>(event.exit_code))},
    });
}

Value ExecutionEventsProjection(const std::vector<ExecutionEvent>& events) {
    Value::Array values;
    for (const ExecutionEvent& event : events) {
        values.push_back(ExecutionEventProjection(event));
    }
    return Value::ArrayValue(std::move(values));
}

Value RunConfigurationProjection(const RunConfiguration& configuration) {
    return Value::ObjectValue({
        {"id", Value(configuration.id)},
        {"name", Value(configuration.name)},
        {"providerId", Value(configuration.provider_id)},
        {"targetId", Value(configuration.target_id)},
        {"data", Value::ObjectValue()},
        {"temporary", Value(configuration.temporary)},
    });
}

Value RunResultProjection(const RunResult& result) {
    return Value::ObjectValue({
        {"exitCode", Value(static_cast<std::int64_t>(result.exit_code))},
        {"output", Value(result.output)},
        {"diagnostics", DiagnosticsProjection(result.diagnostics)},
        {"jobId", Value(static_cast<std::int64_t>(result.job_id))},
    });
}

Value LanguageCompletionProjection(const CompletionList& result) {
    return Value::ObjectValue({
        {"ok", Value(result.ok)},
        {"error", Value(result.error)},
        {"incomplete", Value(result.incomplete)},
        {"items", CompletionItemsProjection(result.items)},
        {"trace", LanguageTraceProjection(result.trace)},
    });
}

Value LanguageHoverProjection(const HoverResult& result) {
    return Value::ObjectValue({
        {"ok", Value(result.ok)},
        {"error", Value(result.error)},
        {"contents", Value(result.contents)},
        {"trace", LanguageTraceProjection(result.trace)},
    });
}

Value LanguageLocationProjection(const LocationResult& result) {
    return Value::ObjectValue({
        {"ok", Value(result.ok)},
        {"error", Value(result.error)},
        {"locations", LocationsProjection(result.locations)},
        {"trace", LanguageTraceProjection(result.trace)},
    });
}

Value LanguageSemanticTokensProjection(const SemanticTokens& result) {
    return Value::ObjectValue({
        {"ok", Value(result.ok)},
        {"error", Value(result.error)},
        {"data", TokenDataProjection(result.data)},
        {"trace", LanguageTraceProjection(result.trace)},
    });
}

Value LanguageErrorProjection(const std::string& error) {
    return Value::ObjectValue({
        {"ok", Value(false)},
        {"error", Value(error)},
    });
}

Value CodeIntelligenceResultProjection(const CodeIntelligenceResult& result) {
    return Value::ObjectValue({
        {"kind", Value(ToString(result.kind))},
        {"document", Value(result.document_uri.ToString())},
        {"requestedVersion", Value(static_cast<std::int64_t>(result.requested_version))},
        {"currentVersion", Value(static_cast<std::int64_t>(result.current_version))},
        {"ok", Value(result.ok)},
        {"cancelled", Value(result.cancelled)},
        {"stale", Value(result.stale)},
        {"timedOut", Value(result.timed_out)},
        {"error", Value(result.error)},
        {"result", LanguagePayloadProjection(result.payload)},
    });
}

}
