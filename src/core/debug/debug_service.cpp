#include "vanta/debug/debug_service.h"

#include <algorithm>
#include <utility>

#include "vanta/core/json_codec.h"

namespace vanta {

RegistrationHandle DebugService::RegisterProvider(std::unique_ptr<DebugProvider> provider) {
    if (provider == nullptr || provider->Id().empty()) {
        return {};
    }
    const std::string provider_id = provider->Id();
    providers_[provider_id] = std::move(provider);
    return RegistrationHandle([this, provider_id] {
        RemoveProvider(provider_id);
    });
}

void DebugService::RemoveProvider(const std::string& provider_id) {
    providers_.erase(provider_id);
}

std::vector<std::string> DebugService::ProviderIds() const {
    std::vector<std::string> result;
    for (const auto& [id, provider] : providers_) {
        (void)provider;
        result.push_back(id);
    }
    return result;
}

BreakpointId DebugService::AddBreakpoint(Breakpoint breakpoint) {
    if (breakpoint.id == 0) {
        breakpoint.id = next_breakpoint_id_++;
    }
    const BreakpointId id = breakpoint.id;
    breakpoints_[id] = std::move(breakpoint);
    return id;
}

bool DebugService::RemoveBreakpoint(BreakpointId id) {
    return breakpoints_.erase(id) > 0;
}

bool DebugService::SetBreakpointEnabled(BreakpointId id, bool enabled) {
    auto it = breakpoints_.find(id);
    if (it == breakpoints_.end()) {
        return false;
    }
    it->second.enabled = enabled;
    return true;
}

std::vector<Breakpoint> DebugService::Breakpoints() const {
    std::vector<Breakpoint> result;
    for (const auto& [id, breakpoint] : breakpoints_) {
        (void)id;
        result.push_back(breakpoint);
    }
    return result;
}

std::vector<Breakpoint> DebugService::BreakpointsForFile(const VirtualFile& file) const {
    std::vector<Breakpoint> result;
    for (const auto& [id, breakpoint] : breakpoints_) {
        (void)id;
        if (breakpoint.file.ToUri() == file.ToUri()) {
            result.push_back(breakpoint);
        }
    }
    return result;
}

DebugSession DebugService::Start(WorkspaceContext& context, DebugConfiguration configuration, DebugEventCallback on_event) {
    DebugProvider* provider = ProviderFor(configuration);
    if (provider == nullptr) {
        DebugSession failed;
        failed.id = next_session_id_++;
        failed.configuration = std::move(configuration);
        failed.status = DebugSessionStatus::Failed;
        failed.message = "Debug provider not found";
        sessions_[failed.id] = failed;
        if (on_event) {
            on_event({
                .session_id = failed.id,
                .kind = DebugEventKind::Failed,
                .message = failed.message,
            });
        }
        return failed;
    }

    const DebugSessionId id = next_session_id_++;
    DebugSession session = provider->Start(context, id, configuration, [this, on_event = std::move(on_event)](const DebugEvent& event) {
        RememberEvent(event);
        if (on_event) {
            on_event(event);
        }
    });
    if (session.id == 0) {
        session.id = id;
    }
    if (session.provider_id.empty()) {
        session.provider_id = provider->Id();
    }
    session.configuration = std::move(configuration);
    sessions_[session.id] = session;
    session_providers_[session.id] = provider->Id();
    return session;
}

bool DebugService::Stop(DebugSessionId session_id) {
    DebugProvider* provider = ProviderFor(session_id);
    if (provider == nullptr) {
        return false;
    }
    const bool ok = provider->Stop(session_id);
    if (ok) {
        sessions_[session_id].status = DebugSessionStatus::Stopped;
    }
    return ok;
}

bool DebugService::ContinueSession(DebugSessionId session_id) {
    DebugProvider* provider = ProviderFor(session_id);
    if (provider == nullptr) {
        return false;
    }
    const bool ok = provider->ContinueSession(session_id);
    if (ok) {
        sessions_[session_id].status = DebugSessionStatus::Running;
    }
    return ok;
}

bool DebugService::Pause(DebugSessionId session_id) {
    DebugProvider* provider = ProviderFor(session_id);
    if (provider == nullptr) {
        return false;
    }
    const bool ok = provider->Pause(session_id);
    if (ok) {
        sessions_[session_id].status = DebugSessionStatus::Paused;
    }
    return ok;
}

DebugEvaluationResult DebugService::Evaluate(DebugSessionId session_id, const std::string& expression, std::uint64_t frame_id) {
    DebugProvider* provider = ProviderFor(session_id);
    if (provider == nullptr) {
        return {.ok = false, .error = "Debug provider not found"};
    }
    return provider->Evaluate(session_id, expression, frame_id);
}

std::vector<StackFrame> DebugService::StackTrace(DebugSessionId session_id) const {
    DebugProvider* provider = ProviderFor(session_id);
    return provider == nullptr ? std::vector<StackFrame>() : provider->StackTrace(session_id);
}

std::vector<DebugVariable> DebugService::Variables(DebugSessionId session_id, std::uint64_t frame_id) const {
    DebugProvider* provider = ProviderFor(session_id);
    return provider == nullptr ? std::vector<DebugVariable>() : provider->Variables(session_id, frame_id);
}

std::optional<DebugSession> DebugService::Session(DebugSessionId session_id) const {
    auto it = sessions_.find(session_id);
    return it == sessions_.end() ? std::nullopt : std::optional<DebugSession>(it->second);
}

std::vector<DebugSession> DebugService::Sessions() const {
    std::vector<DebugSession> result;
    for (const auto& [id, session] : sessions_) {
        (void)id;
        result.push_back(session);
    }
    return result;
}

DebugProvider* DebugService::ProviderFor(const DebugConfiguration& configuration) const {
    if (!configuration.type.empty()) {
        for (const auto& [id, provider] : providers_) {
            (void)id;
            const std::vector<std::string> types = provider->ConfigurationTypes();
            if (std::find(types.begin(), types.end(), configuration.type) != types.end()) {
                return provider.get();
            }
        }
        return nullptr;
    }
    return providers_.empty() ? nullptr : providers_.begin()->second.get();
}

DebugProvider* DebugService::ProviderFor(DebugSessionId session_id) const {
    auto session_provider = session_providers_.find(session_id);
    if (session_provider == session_providers_.end()) {
        return nullptr;
    }
    auto provider = providers_.find(session_provider->second);
    return provider == providers_.end() ? nullptr : provider->second.get();
}

void DebugService::RememberEvent(const DebugEvent& event) {
    auto it = sessions_.find(event.session_id);
    if (it == sessions_.end()) {
        return;
    }
    switch (event.kind) {
    case DebugEventKind::Started:
    case DebugEventKind::Continued:
        it->second.status = DebugSessionStatus::Running;
        break;
    case DebugEventKind::Paused:
        it->second.status = DebugSessionStatus::Paused;
        break;
    case DebugEventKind::Stopped:
        it->second.status = DebugSessionStatus::Stopped;
        break;
    case DebugEventKind::Failed:
        it->second.status = DebugSessionStatus::Failed;
        break;
    case DebugEventKind::Output:
        break;
    }
    it->second.message = event.message;
}

}
