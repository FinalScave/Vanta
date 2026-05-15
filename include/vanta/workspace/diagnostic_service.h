#pragma once

#include <map>
#include <string>
#include <vector>

#include "vanta/core/diagnostic.h"
#include "vanta/platform/async.h"

namespace vanta {

struct DiagnosticChangeEvent {
    std::string source;
    std::vector<Diagnostic> diagnostics;
};

class DiagnosticService {
public:
    void publish(std::string source, std::vector<Diagnostic> diagnostics);
    void clear(const std::string& source);
    std::vector<Diagnostic> diagnosticsForFile(const VirtualFile& file) const;
    std::vector<Diagnostic> allDiagnostics() const;

    std::uint64_t onDidChangeDiagnostics(EventBus<DiagnosticChangeEvent>::Listener listener);
    void removeDiagnosticsListener(std::uint64_t listenerId);

private:
    std::map<std::string, std::vector<Diagnostic>> diagnosticsBySource_;
    EventBus<DiagnosticChangeEvent> onDidChange_;
};

}
