#pragma once

#include <string>
#include <vector>

#include "vanta/core/text.h"
#include "vanta/vfs/virtual_file.h"

namespace vanta {

struct Location {
    VirtualFile file;
    TextRange range;
};

enum class SymbolKind {
    Unknown,
    File,
    Module,
    Namespace,
    Package,
    Class,
    Method,
    Property,
    Field,
    Constructor,
    Enum,
    Interface,
    Function,
    Variable,
    Constant,
    String,
    Number,
    Boolean,
    Array,
    Object,
    Key,
    Null,
    EnumMember,
    Struct,
    Event,
    Operator,
    TypeParameter,
    Macro,
};

struct CodeSymbol {
    std::string id;
    std::string name;
    std::string qualified_name;
    SymbolKind kind = SymbolKind::Unknown;
    Location location;
    Location declaration;
    TextRange selection_range;
    std::string container_id;
    std::string language_id;
    std::string detail;
};

enum class SymbolReferenceKind {
    Unknown,
    Declaration,
    Definition,
    Read,
    Write,
    Call,
    Inheritance,
    Import,
    Override,
    TypeUse,
};

struct SymbolReference {
    std::string symbol_id;
    std::string name;
    Location location;
    SymbolReferenceKind kind = SymbolReferenceKind::Unknown;
};

enum class CodeGraphEdgeKind {
    Unknown,
    Contains,
    References,
    Calls,
    Overrides,
    Implements,
    Inherits,
    UsesType,
    Includes,
};

struct CodeGraphEdge {
    std::string id;
    CodeGraphEdgeKind kind = CodeGraphEdgeKind::Unknown;
    std::string from_symbol_id;
    std::string to_symbol_id;
    Location location;
    std::string provider_id;
};

std::string ToString(SymbolKind kind);
std::string ToString(SymbolReferenceKind kind);
std::string ToString(CodeGraphEdgeKind kind);

}
