#include "vanta/workspace/diagnostic_service.h"

namespace vanta {

void DiagnosticService::publish(std::string source, std::vector<Diagnostic> diagnostics) {
    const std::string sourceId = source;
    diagnosticsBySource_[std::move(source)] = diagnostics;
    onDidChange_.publish({
        .source = sourceId,
        .diagnostics = diagnosticsBySource_.at(sourceId),
    });
}

void DiagnosticService::clear(const std::string& source) {
    diagnosticsBySource_.erase(source);
    onDidChange_.publish({
        .source = source,
        .diagnostics = {},
    });
}

std::vector<Diagnostic> DiagnosticService::diagnosticsForFile(const VirtualFile& file) const {
    std::vector<Diagnostic> result;
    for (const auto& [source, diagnostics] : diagnosticsBySource_) {
        (void)source;
        for (const Diagnostic& diagnostic : diagnostics) {
            if (diagnostic.location.file == file) {
                result.push_back(diagnostic);
            }
        }
    }
    return result;
}

std::vector<Diagnostic> DiagnosticService::allDiagnostics() const {
    std::vector<Diagnostic> result;
    for (const auto& [source, diagnostics] : diagnosticsBySource_) {
        (void)source;
        result.insert(result.end(), diagnostics.begin(), diagnostics.end());
    }
    return result;
}

std::uint64_t DiagnosticService::onDidChangeDiagnostics(EventBus<DiagnosticChangeEvent>::Listener listener) {
    return onDidChange_.subscribe(std::move(listener));
}

void DiagnosticService::removeDiagnosticsListener(std::uint64_t listenerId) {
    onDidChange_.unsubscribe(listenerId);
}

}
