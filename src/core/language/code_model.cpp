#include "vanta/language/code_model.h"

namespace vanta {

std::string ToString(SymbolKind kind) {
    switch (kind) {
    case SymbolKind::Unknown:
        return "unknown";
    case SymbolKind::File:
        return "file";
    case SymbolKind::Module:
        return "module";
    case SymbolKind::Namespace:
        return "namespace";
    case SymbolKind::Package:
        return "package";
    case SymbolKind::Class:
        return "class";
    case SymbolKind::Method:
        return "method";
    case SymbolKind::Property:
        return "property";
    case SymbolKind::Field:
        return "field";
    case SymbolKind::Constructor:
        return "constructor";
    case SymbolKind::Enum:
        return "enum";
    case SymbolKind::Interface:
        return "interface";
    case SymbolKind::Function:
        return "function";
    case SymbolKind::Variable:
        return "variable";
    case SymbolKind::Constant:
        return "constant";
    case SymbolKind::String:
        return "string";
    case SymbolKind::Number:
        return "number";
    case SymbolKind::Boolean:
        return "boolean";
    case SymbolKind::Array:
        return "array";
    case SymbolKind::Object:
        return "object";
    case SymbolKind::Key:
        return "key";
    case SymbolKind::Null:
        return "null";
    case SymbolKind::EnumMember:
        return "enumMember";
    case SymbolKind::Struct:
        return "struct";
    case SymbolKind::Event:
        return "event";
    case SymbolKind::Operator:
        return "operator";
    case SymbolKind::TypeParameter:
        return "typeParameter";
    case SymbolKind::Macro:
        return "macro";
    }
    return "unknown";
}

std::string ToString(SymbolReferenceKind kind) {
    switch (kind) {
    case SymbolReferenceKind::Unknown:
        return "unknown";
    case SymbolReferenceKind::Declaration:
        return "declaration";
    case SymbolReferenceKind::Definition:
        return "definition";
    case SymbolReferenceKind::Read:
        return "read";
    case SymbolReferenceKind::Write:
        return "write";
    case SymbolReferenceKind::Call:
        return "call";
    case SymbolReferenceKind::Inheritance:
        return "inheritance";
    case SymbolReferenceKind::Import:
        return "import";
    case SymbolReferenceKind::Override:
        return "override";
    case SymbolReferenceKind::TypeUse:
        return "typeUse";
    }
    return "unknown";
}

std::string ToString(CodeGraphEdgeKind kind) {
    switch (kind) {
    case CodeGraphEdgeKind::Unknown:
        return "unknown";
    case CodeGraphEdgeKind::Contains:
        return "contains";
    case CodeGraphEdgeKind::References:
        return "references";
    case CodeGraphEdgeKind::Calls:
        return "calls";
    case CodeGraphEdgeKind::Overrides:
        return "overrides";
    case CodeGraphEdgeKind::Implements:
        return "implements";
    case CodeGraphEdgeKind::Inherits:
        return "inherits";
    case CodeGraphEdgeKind::UsesType:
        return "usesType";
    case CodeGraphEdgeKind::Includes:
        return "includes";
    }
    return "unknown";
}

}
