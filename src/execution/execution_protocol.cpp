#include "vanta/execution/execution_protocol.h"

#include <cstdint>
#include <utility>

namespace vanta {

std::string toString(ExecutionEventKind kind) {
    switch (kind) {
    case ExecutionEventKind::Started:
        return "started";
    case ExecutionEventKind::Stdout:
        return "stdout";
    case ExecutionEventKind::Stderr:
        return "stderr";
    case ExecutionEventKind::Progress:
        return "progress";
    case ExecutionEventKind::Finished:
        return "finished";
    }
    return "started";
}

Json toJson(const ExecutionEvent& event) {
    return Json::object({
        {"kind", Json(toString(event.kind))},
        {"jobId", Json(static_cast<std::int64_t>(event.jobId))},
        {"executorId", Json(event.executorId)},
        {"targetId", Json(event.targetId)},
        {"text", Json(event.text)},
        {"progress", Json(event.progress)},
        {"exitCode", Json(static_cast<std::int64_t>(event.exitCode))},
        {"metadata", event.metadata},
    });
}

Json toJson(const std::vector<ExecutionEvent>& events) {
    Json::Array values;
    for (const ExecutionEvent& event : events) {
        values.push_back(toJson(event));
    }
    return Json::array(std::move(values));
}

}
