#include "vanta/workspace/diagnostic_service.h"

namespace vanta {

void DiagnosticService::Publish(std::string source, std::vector<Diagnostic> diagnostics) {
    const std::string source_id = source;
    diagnostics_by_source_[std::move(source)] = diagnostics;
    on_did_change_.Publish({
        .source = source_id,
        .diagnostics = diagnostics_by_source_.at(source_id),
    });
}

void DiagnosticService::Clear(const std::string& source) {
    diagnostics_by_source_.erase(source);
    on_did_change_.Publish({
        .source = source,
        .diagnostics = {},
    });
}

std::vector<Diagnostic> DiagnosticService::DiagnosticsForFile(const VirtualFile& file) const {
    std::vector<Diagnostic> result;
    for (const auto& [source, diagnostics] : diagnostics_by_source_) {
        (void)source;
        for (const Diagnostic& diagnostic : diagnostics) {
            if (diagnostic.location.file == file) {
                result.push_back(diagnostic);
            }
        }
    }
    return result;
}

std::vector<Diagnostic> DiagnosticService::AllDiagnostics() const {
    std::vector<Diagnostic> result;
    for (const auto& [source, diagnostics] : diagnostics_by_source_) {
        (void)source;
        result.insert(result.end(), diagnostics.begin(), diagnostics.end());
    }
    return result;
}

std::uint64_t DiagnosticService::OnDidChangeDiagnostics(EventBus<DiagnosticChangeEvent>::Listener listener) {
    return on_did_change_.Subscribe(std::move(listener));
}

void DiagnosticService::RemoveDiagnosticsListener(std::uint64_t listener_id) {
    on_did_change_.Unsubscribe(listener_id);
}

}
