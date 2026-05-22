#include "vanta/execution/problem_matcher.h"

#include <regex>
#include <sstream>

namespace vanta {
namespace {

std::filesystem::path NormalizedExistingPath(const std::filesystem::path& path) {
    std::error_code error;
    const auto normalized = std::filesystem::weakly_canonical(path, error);
    return error ? path : normalized;
}

DiagnosticSeverity SeverityFromString(const std::string& severity) {
    if (severity == "error") {
        return DiagnosticSeverity::Error;
    }
    if (severity == "warning") {
        return DiagnosticSeverity::Warning;
    }
    if (severity == "note") {
        return DiagnosticSeverity::Note;
    }
    return DiagnosticSeverity::Note;
}

bool LooksLikeUri(const std::string& value) {
    return value.find("://") != std::string::npos;
}

}

std::vector<ProblemMatch> ProblemMatcher::MatchCompilerOutput(const std::string& output) const {
    std::vector<ProblemMatch> matches;
    const std::regex pattern(R"(([^:\n]+):(\d+):(\d+):\s+(warning|error|note):\s+(.+))");
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        std::smatch match;
        if (!std::regex_search(line, match, pattern)) {
            continue;
        }

        ProblemMatch problem;
        problem.file_path = match[1].str();
        problem.line = std::stoi(match[2].str());
        problem.column = std::stoi(match[3].str());
        problem.severity = SeverityFromString(match[4].str());
        problem.source = "build";
        problem.message = match[5].str();
        matches.push_back(std::move(problem));
    }
    return matches;
}

std::vector<Diagnostic> DiagnosticResolver::Resolve(
    const std::vector<ProblemMatch>& matches,
    const Workspace& workspace,
    const std::filesystem::path& build_directory) const {
    std::vector<Diagnostic> diagnostics;
    diagnostics.reserve(matches.size());
    for (const ProblemMatch& match : matches) {
        Diagnostic diagnostic;
        diagnostic.location.file = ResolveFile(match, workspace, build_directory);
        diagnostic.location.line = match.line;
        diagnostic.location.column = match.column;
        diagnostic.severity = match.severity;
        diagnostic.source = match.source;
        diagnostic.message = match.message;
        diagnostics.push_back(std::move(diagnostic));
    }
    return diagnostics;
}

VirtualFile DiagnosticResolver::ResolveFile(
    const ProblemMatch& match,
    const Workspace& workspace,
    const std::filesystem::path& build_directory) const {
    if (LooksLikeUri(match.file_path)) {
        return VirtualFile(Uri::Parse(match.file_path), nullptr);
    }

    const std::filesystem::path raw_path(match.file_path);
    if (raw_path.is_absolute()) {
        return workspace.File(NormalizedExistingPath(raw_path));
    }

    const std::filesystem::path from_workspace = workspace.Resolve(raw_path);
    if (std::filesystem::exists(from_workspace)) {
        return workspace.File(NormalizedExistingPath(from_workspace));
    }

    if (!build_directory.empty()) {
        const std::filesystem::path absolute_build_directory = workspace.Resolve(build_directory);
        const std::filesystem::path from_build = absolute_build_directory / raw_path;
        if (std::filesystem::exists(from_build)) {
            return workspace.File(NormalizedExistingPath(from_build));
        }
    }

    return workspace.File(from_workspace);
}

}
