#include "type_checker.hpp"

#include <stdexcept>

namespace rolang::semantics {
namespace {

Type type(TypeKind kind) { return {kind, {}}; }

bool is_numeric(TypeKind kind) {
    return kind == TypeKind::Number || kind == TypeKind::Bool;
}

const char* type_name(TypeKind kind) {
    switch (kind) {
        case TypeKind::Dynamic: return "Dynamic";
        case TypeKind::Error: return "Error";
        case TypeKind::None: return "None";
        case TypeKind::Bool: return "Bool";
        case TypeKind::Number: return "Number";
        case TypeKind::String: return "String";
        case TypeKind::Function: return "Function";
    }
    throw std::logic_error("unknown semantic type");
}

SourceLocation location(const std::string& source_name, const Node& node) {
    return {source_name, node.line, node.column};
}

} // namespace

TypeChecker::TypeChecker(DiagnosticEngine& diagnostics, std::string source_name)
    : diagnostics_(diagnostics), source_name_(std::move(source_name)) {}

Type TypeChecker::type_of_literal(const Node& literal) const {
    if (literal.string_literal) return type(TypeKind::String);
    if (literal.text == "True" || literal.text == "False") return type(TypeKind::Bool);
    if (literal.text == "None") return type(TypeKind::None);
    if (literal.text.empty()) return type(TypeKind::Number);
    throw std::logic_error("malformed literal node");
}

Type TypeChecker::check_unary(const Node& operation, Type operand) {
    if (operand.kind == TypeKind::Error) return type(TypeKind::Error);
    if (operation.text == "not") return type(TypeKind::Bool);
    if (operation.text != "+" && operation.text != "-")
        throw std::logic_error("unknown unary operator in AST");
    if (operand.kind == TypeKind::Dynamic) return type(TypeKind::Dynamic);
    if (is_numeric(operand.kind)) return type(TypeKind::Number);

    SourceLocation at = location(source_name_, operation);
    diagnostics_.error(
        DiagnosticCode::InvalidOperator, std::move(at),
        "operator '" + operation.text + "' does not accept " + type_name(operand.kind));
    return type(TypeKind::Error);
}

Type TypeChecker::check_binary(const Node& operation, Type left, Type right) {
    if (left.kind == TypeKind::Error || right.kind == TypeKind::Error) return type(TypeKind::Error);
    if (operation.text == "and" || operation.text == "or") return type(TypeKind::Bool);
    if (operation.text == "==" || operation.text == "!=") return type(TypeKind::Bool);

    const bool dynamic = left.kind == TypeKind::Dynamic || right.kind == TypeKind::Dynamic;
    if (operation.text == "+") {
        if (dynamic) return type(TypeKind::Dynamic);
        if (left.kind == TypeKind::String && right.kind == TypeKind::String) return type(TypeKind::String);
        if (is_numeric(left.kind) && is_numeric(right.kind)) return type(TypeKind::Number);
    } else if (operation.text == "-" || operation.text == "*" || operation.text == "/" ||
               operation.text == "//" || operation.text == "%" || operation.text == "**") {
        if (dynamic) return type(TypeKind::Dynamic);
        if (is_numeric(left.kind) && is_numeric(right.kind)) return type(TypeKind::Number);
    } else if (operation.text == "<" || operation.text == "<=" ||
               operation.text == ">" || operation.text == ">=") {
        if (dynamic) return type(TypeKind::Dynamic);
        if (is_numeric(left.kind) && is_numeric(right.kind)) return type(TypeKind::Bool);
    } else {
        throw std::logic_error("unknown binary operator in AST");
    }

    SourceLocation at = location(source_name_, operation);
    diagnostics_.error(
        DiagnosticCode::InvalidOperator, std::move(at),
        "operator '" + operation.text + "' does not accept " + type_name(left.kind) +
            " and " + type_name(right.kind));
    return type(TypeKind::Error);
}

Type TypeChecker::check_call_result(const Node&, Type callee) {
    if (callee.kind == TypeKind::Error) return type(TypeKind::Error);
    return type(TypeKind::Dynamic);
}

} // namespace rolang::semantics
