#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vanta/core/registration.h"
#include "vanta/core/text.h"
#include "vanta/core/value.h"
#include "vanta/execution/execution_service.h"
#include "vanta/vfs/virtual_file.h"

namespace vanta {

class WorkspaceContext;

using DebugSessionId = std::uint64_t;
using BreakpointId = std::uint64_t;

enum class DebugSessionStatus {
    Pending,
    Starting,
    Running,
    Paused,
    Stopped,
    Failed,
};

enum class BreakpointKind {
    Line,
    Function,
    Exception,
};

struct Breakpoint {
    BreakpointId id = 0;
    BreakpointKind kind = BreakpointKind::Line;
    VirtualFile file;
    int line = 0;
    std::string expression;
    bool enabled = true;
};

struct StackFrame {
    std::uint64_t id = 0;
    std::string name;
    VirtualFile file;
    TextRange range;
};

struct DebugVariable {
    std::string name;
    std::string value;
    std::string type;
    bool expandable = false;
};

struct DebugConfiguration {
    std::string id;
    std::string name;
    std::string type;
    ExecutionTarget target;
    std::optional<Value> payload;
};

struct DebugSession {
    DebugSessionId id = 0;
    std::string provider_id;
    DebugConfiguration configuration;
    DebugSessionStatus status = DebugSessionStatus::Pending;
    std::string message;
    std::optional<Value> payload;
};

enum class DebugEventKind {
    Started,
    Paused,
    Continued,
    Stopped,
    Output,
    Failed,
};

struct DebugEvent {
    DebugSessionId session_id = 0;
    DebugEventKind kind = DebugEventKind::Started;
    std::string message;
    std::optional<Value> payload;
};

struct DebugEvaluationResult {
    bool ok = false;
    std::string value;
    std::string type;
    std::string error;
    std::optional<Value> payload;
};

using DebugEventCallback = std::function<void(const DebugEvent&)>;

class DebugProvider {
public:
    virtual ~DebugProvider() = default;

    virtual std::string Id() const = 0;
    virtual std::vector<std::string> ConfigurationTypes() const = 0;
    virtual DebugSession Start(
        WorkspaceContext& context,
        DebugSessionId session_id,
        const DebugConfiguration& configuration,
        DebugEventCallback on_event = {}) = 0;
    virtual bool Stop(DebugSessionId session_id) = 0;
    virtual bool ContinueSession(DebugSessionId session_id) = 0;
    virtual bool Pause(DebugSessionId session_id) = 0;
    virtual DebugEvaluationResult Evaluate(DebugSessionId session_id, const std::string& expression, std::uint64_t frame_id = 0) = 0;
    virtual std::vector<StackFrame> StackTrace(DebugSessionId session_id) const = 0;
    virtual std::vector<DebugVariable> Variables(DebugSessionId session_id, std::uint64_t frame_id) const = 0;
};

class DebugService {
public:
    static constexpr const char* kServiceId = "vanta.debug";

    RegistrationHandle RegisterProvider(std::unique_ptr<DebugProvider> provider);
    void RemoveProvider(const std::string& provider_id);
    std::vector<std::string> ProviderIds() const;

    BreakpointId AddBreakpoint(Breakpoint breakpoint);
    bool RemoveBreakpoint(BreakpointId id);
    bool SetBreakpointEnabled(BreakpointId id, bool enabled);
    std::vector<Breakpoint> Breakpoints() const;
    std::vector<Breakpoint> BreakpointsForFile(const VirtualFile& file) const;

    DebugSession Start(
        WorkspaceContext& context,
        DebugConfiguration configuration,
        DebugEventCallback on_event = {});
    bool Stop(DebugSessionId session_id);
    bool ContinueSession(DebugSessionId session_id);
    bool Pause(DebugSessionId session_id);
    DebugEvaluationResult Evaluate(DebugSessionId session_id, const std::string& expression, std::uint64_t frame_id = 0);
    std::vector<StackFrame> StackTrace(DebugSessionId session_id) const;
    std::vector<DebugVariable> Variables(DebugSessionId session_id, std::uint64_t frame_id) const;

    std::optional<DebugSession> Session(DebugSessionId session_id) const;
    std::vector<DebugSession> Sessions() const;

private:
    DebugProvider* ProviderFor(const DebugConfiguration& configuration) const;
    DebugProvider* ProviderFor(DebugSessionId session_id) const;
    void RememberEvent(const DebugEvent& event);

    std::map<std::string, std::unique_ptr<DebugProvider>> providers_;
    std::map<BreakpointId, Breakpoint> breakpoints_;
    std::map<DebugSessionId, DebugSession> sessions_;
    std::map<DebugSessionId, std::string> session_providers_;
    BreakpointId next_breakpoint_id_ = 1;
    DebugSessionId next_session_id_ = 1;
};

}
