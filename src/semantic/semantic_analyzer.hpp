#pragma once

#include "semantic_types.hpp"

namespace rolang::semantics {

class SemanticAnalyzer {
public:
    AnalysisResult analyze(const Node& root, const AnalysisOptions& options = {}) const;
};

} // namespace rolang::semantics
