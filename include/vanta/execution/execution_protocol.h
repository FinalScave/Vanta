#pragma once

#include <functional>
#include <string>
#include <vector>

#include "vanta/execution/job_service.h"
#include "vanta/platform/json.h"

namespace vanta {

enum class ExecutionEventKind {
    Started,
    Stdout,
    Stderr,
    Progress,
    Finished,
};

struct ExecutionEvent {
    ExecutionEventKind kind = ExecutionEventKind::Started;
    JobId jobId = 0;
    std::string executorId;
    std::string targetId;
    std::string text;
    double progress = -1.0;
    int exitCode = 0;
    Json metadata;
};

using ExecutionEventCallback = std::function<void(const ExecutionEvent&)>;

std::string toString(ExecutionEventKind kind);
Json toJson(const ExecutionEvent& event);
Json toJson(const std::vector<ExecutionEvent>& events);

}
