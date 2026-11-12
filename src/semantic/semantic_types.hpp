#pragma once

#include "frontend.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rolang::semantics {

using ScopeId = std::uint32_t;
using SymbolId = std::uint32_t;

struct SourceLocation {
    std::string source_name;
    int line = 1;
    int column = 1;
};

enum class TypeKind { Dynamic, Error, None, Bool, Number, String, Function };

struct Type {
    TypeKind kind = TypeKind::Dynamic;
    std::optional<SymbolId> function_symbol;
};

enum class SymbolKind { Variable, Parameter, Function, Builtin };
enum class ScopeKind { Module, Function, Builtin };

struct FunctionSignature {
    std::vector<std::string> parameter_names;
    bool variadic = false;
};

struct Symbol {
    SymbolId id = 0;
    std::string name;
    SymbolKind kind = SymbolKind::Variable;
    ScopeId owner = 0;
    SourceLocation declaration;
    Type type;
    std::optional<FunctionSignature> signature;
};

enum class DiagnosticSeverity { Error, Warning, Note };
enum class DiagnosticCode {
    UndefinedName,
    ArgumentCount,
    InvalidReturn,
    InvalidOperator
};

struct RelatedDiagnostic {
    SourceLocation location;
    std::string message;
};

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    DiagnosticCode code = DiagnosticCode::UndefinedName;
    SourceLocation location;
    std::string message;
    std::vector<RelatedDiagnostic> related;
};

struct AnalysisOptions {
    std::string source_name = "<source>";
};

struct AnalysisResult {
    std::vector<Diagnostic> diagnostics;
    std::unordered_map<const Node*, SymbolId> resolved_names;
    std::unordered_map<const Node*, SymbolId> known_call_targets;
    std::unordered_map<const Node*, Type> inferred_types;

    bool has_errors() const;
};

const char* diagnostic_code_name(DiagnosticCode code);

} // namespace rolang::semantics
