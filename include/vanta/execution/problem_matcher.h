#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "vanta/core/diagnostic.h"
#include "vanta/workspace/workspace.h"

namespace vanta {

struct ProblemMatch {
    std::string filePath;
    int line = 0;
    int column = 0;
    DiagnosticSeverity severity = DiagnosticSeverity::Note;
    std::string source;
    std::string message;
};

class ProblemMatcher {
public:
    std::vector<ProblemMatch> matchCompilerOutput(const std::string& output) const;
};

class DiagnosticResolver {
public:
    std::vector<Diagnostic> resolve(
        const std::vector<ProblemMatch>& matches,
        const Workspace& workspace,
        const std::filesystem::path& buildDirectory = {}) const;

private:
    VirtualFile resolveFile(
        const ProblemMatch& match,
        const Workspace& workspace,
        const std::filesystem::path& buildDirectory) const;
};

}
