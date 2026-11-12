#include "diagnostic_engine.hpp"

#include <algorithm>
#include <utility>

namespace rolang::semantics {

bool AnalysisResult::has_errors() const {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
}

const char* diagnostic_code_name(DiagnosticCode code) {
    switch (code) {
        case DiagnosticCode::UndefinedName: return "undefined-name";
        case DiagnosticCode::ArgumentCount: return "argument-count";
        case DiagnosticCode::InvalidReturn: return "invalid-return";
        case DiagnosticCode::InvalidOperator: return "invalid-operator";
    }
    return "unknown";
}

void DiagnosticEngine::report(Diagnostic diagnostic) {
    diagnostics_.push_back(std::move(diagnostic));
}

void DiagnosticEngine::error(DiagnosticCode code, SourceLocation at, std::string message) {
    report({DiagnosticSeverity::Error, code, std::move(at), std::move(message), {}});
}

void DiagnosticEngine::error_with_related(
    DiagnosticCode code, SourceLocation at, std::string message, RelatedDiagnostic related) {
    report({DiagnosticSeverity::Error, code, std::move(at), std::move(message), {std::move(related)}});
}

bool DiagnosticEngine::has_errors() const {
    return std::any_of(diagnostics_.begin(), diagnostics_.end(), [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
}

const std::vector<Diagnostic>& DiagnosticEngine::diagnostics() const {
    return diagnostics_;
}

std::vector<Diagnostic> DiagnosticEngine::take_diagnostics() {
    std::stable_sort(diagnostics_.begin(), diagnostics_.end(), [](const Diagnostic& left, const Diagnostic& right) {
        if (left.location.line != right.location.line) return left.location.line < right.location.line;
        return left.location.column < right.location.column;
    });
    return std::move(diagnostics_);
}

} // namespace rolang::semantics
