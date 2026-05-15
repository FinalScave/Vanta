#include "vanta/execution/problem_matcher.h"

#include <regex>
#include <sstream>

namespace vanta {
namespace {

std::filesystem::path normalizedExistingPath(const std::filesystem::path& path) {
    std::error_code error;
    const auto normalized = std::filesystem::weakly_canonical(path, error);
    return error ? path : normalized;
}

DiagnosticSeverity severityFromString(const std::string& severity) {
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

bool looksLikeUri(const std::string& value) {
    return value.find("://") != std::string::npos;
}

}

std::vector<ProblemMatch> ProblemMatcher::matchCompilerOutput(const std::string& output) const {
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
        problem.filePath = match[1].str();
        problem.line = std::stoi(match[2].str());
        problem.column = std::stoi(match[3].str());
        problem.severity = severityFromString(match[4].str());
        problem.source = "build";
        problem.message = match[5].str();
        matches.push_back(std::move(problem));
    }
    return matches;
}

std::vector<Diagnostic> DiagnosticResolver::resolve(
    const std::vector<ProblemMatch>& matches,
    const Workspace& workspace,
    const std::filesystem::path& buildDirectory) const {
    std::vector<Diagnostic> diagnostics;
    diagnostics.reserve(matches.size());
    for (const ProblemMatch& match : matches) {
        Diagnostic diagnostic;
        diagnostic.location.file = resolveFile(match, workspace, buildDirectory);
        diagnostic.location.line = match.line;
        diagnostic.location.column = match.column;
        diagnostic.severity = match.severity;
        diagnostic.source = match.source;
        diagnostic.message = match.message;
        diagnostics.push_back(std::move(diagnostic));
    }
    return diagnostics;
}

VirtualFile DiagnosticResolver::resolveFile(
    const ProblemMatch& match,
    const Workspace& workspace,
    const std::filesystem::path& buildDirectory) const {
    if (looksLikeUri(match.filePath)) {
        return VirtualFile(Uri::parse(match.filePath), nullptr);
    }

    const std::filesystem::path rawPath(match.filePath);
    if (rawPath.is_absolute()) {
        return workspace.file(normalizedExistingPath(rawPath));
    }

    const std::filesystem::path fromWorkspace = workspace.resolve(rawPath);
    if (std::filesystem::exists(fromWorkspace)) {
        return workspace.file(normalizedExistingPath(fromWorkspace));
    }

    if (!buildDirectory.empty()) {
        const std::filesystem::path absoluteBuildDirectory = workspace.resolve(buildDirectory);
        const std::filesystem::path fromBuild = absoluteBuildDirectory / rawPath;
        if (std::filesystem::exists(fromBuild)) {
            return workspace.file(normalizedExistingPath(fromBuild));
        }
    }

    return workspace.file(fromWorkspace);
}

}
