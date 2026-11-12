#pragma once

#include "semantic_types.hpp"

namespace rolang::semantics {

class DiagnosticEngine {
public:
    void report(Diagnostic diagnostic);
    void error(DiagnosticCode code, SourceLocation at, std::string message);
    void error_with_related(
        DiagnosticCode code, SourceLocation at, std::string message,
        RelatedDiagnostic related);
    bool has_errors() const;
    const std::vector<Diagnostic>& diagnostics() const;
    std::vector<Diagnostic> take_diagnostics();

private:
    std::vector<Diagnostic> diagnostics_;
};

} // namespace rolang::semantics
