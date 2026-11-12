#pragma once

#include "diagnostic_engine.hpp"

#include <string>

namespace rolang::semantics {

class TypeChecker {
public:
    TypeChecker(DiagnosticEngine& diagnostics, std::string source_name);

    Type type_of_literal(const Node& literal) const;
    Type check_unary(const Node& operation, Type operand);
    Type check_binary(const Node& operation, Type left, Type right);
    Type check_call_result(const Node& call, Type callee);

private:
    DiagnosticEngine& diagnostics_;
    std::string source_name_;
};

} // namespace rolang::semantics
