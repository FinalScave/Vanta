#pragma once

#include <string>

#include "vanta/vfs/virtual_file.h"

namespace vanta {

struct SourceLocation {
    VirtualFile file;
    int line = 0;
    int column = 0;
};

enum class DiagnosticSeverity {
    Note,
    Warning,
    Error,
};

struct Diagnostic {
    SourceLocation location;
    DiagnosticSeverity severity = DiagnosticSeverity::Note;
    std::string message;
    std::string source;
};

std::string toString(DiagnosticSeverity severity);

}
