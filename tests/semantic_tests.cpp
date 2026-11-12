#include "frontend.hpp"
#include "semantic/semantic_analyzer.hpp"

#include <cassert>
#include <string>

using namespace rolang;
using namespace rolang::semantics;

namespace {

AnalysisResult analyze(const std::string& source) {
    Node tree = parse(tokenize(source));
    AnalysisOptions options;
    options.source_name = "semantic_test.rol";
    return SemanticAnalyzer{}.analyze(tree, options);
}

void assert_location(const SourceLocation& location, const std::string& source,
                     int line, int column) {
    assert(location.source_name == source);
    assert(location.line == line);
    assert(location.column == column);
}

void test_valid_programs_and_dynamic_parameters() {
    const AnalysisResult result = analyze(
        "value = 3\n"
        "def identity(parameter):\n"
        "    return parameter\n"
        "print(identity(\"text\"))\n");
    assert(!result.has_errors());
    assert(result.diagnostics.empty());

    const AnalysisResult nested = analyze(
        "def outer(value):\n"
        "    def inner(parameter):\n"
        "        return parameter\n"
        "    return inner(value)\n"
        "print(outer(3))\n");
    assert(!nested.has_errors());
    assert(nested.diagnostics.empty());
}

void test_undefined_name_location() {
    const AnalysisResult result = analyze("print(missing)\n");
    assert(result.diagnostics.size() == 1);
    const Diagnostic& diagnostic = result.diagnostics.front();
    assert(diagnostic.code == DiagnosticCode::UndefinedName);
    assert(diagnostic.severity == DiagnosticSeverity::Error);
    assert(diagnostic.message == "undefined name 'missing'");
    assert_location(diagnostic.location, "semantic_test.rol", 1, 7);
    assert(diagnostic.related.empty());
}

void test_unknown_function() {
    const AnalysisResult result = analyze("missing(1)\n");
    assert(result.diagnostics.size() == 1);
    const Diagnostic& diagnostic = result.diagnostics.front();
    assert(diagnostic.code == DiagnosticCode::UndefinedName);
    assert(diagnostic.message == "undefined name 'missing'");
    assert_location(diagnostic.location, "semantic_test.rol", 1, 1);
    assert(diagnostic.related.empty());
}

void test_known_function_argument_count() {
    const AnalysisResult result = analyze(
        "def add(a, b):\n"
        "    return a + b\n"
        "add(1)\n");
    assert(result.diagnostics.size() == 1);
    const Diagnostic& diagnostic = result.diagnostics.front();
    assert(diagnostic.code == DiagnosticCode::ArgumentCount);
    assert(diagnostic.message == "function 'add' expects 2 arguments, got 1");
    assert_location(diagnostic.location, "semantic_test.rol", 3, 1);
    assert(diagnostic.related.size() == 1);
    assert(diagnostic.related[0].message == "function declared here");
    assert_location(diagnostic.related[0].location, "semantic_test.rol", 1, 1);
}

void test_recursive_function_argument_count() {
    const AnalysisResult result = analyze(
        "def recurse(value):\n"
        "    return recurse()\n");
    assert(result.diagnostics.size() == 1);
    const Diagnostic& diagnostic = result.diagnostics.front();
    assert(diagnostic.code == DiagnosticCode::ArgumentCount);
    assert(diagnostic.message == "function 'recurse' expects 1 arguments, got 0");
    assert_location(diagnostic.location, "semantic_test.rol", 2, 12);
    assert(diagnostic.related.size() == 1);
    assert_location(diagnostic.related[0].location, "semantic_test.rol", 1, 1);
}

void test_known_invalid_operation_location() {
    const AnalysisResult result = analyze("print(\"x\" - 1)\n");
    assert(result.diagnostics.size() == 1);
    const Diagnostic& diagnostic = result.diagnostics.front();
    assert(diagnostic.code == DiagnosticCode::InvalidOperator);
    assert(diagnostic.message == "operator '-' does not accept String and Number");
    assert_location(diagnostic.location, "semantic_test.rol", 1, 11);
    assert(diagnostic.related.empty());
}

void test_return_outside_function_on_constructed_ast() {
    Node root{NodeKind::Program, 1};
    Node statement{NodeKind::Return, 2};
    statement.column = 3;
    root.children.push_back(statement);

    AnalysisOptions options;
    options.source_name = "semantic_test.rol";
    const AnalysisResult result = SemanticAnalyzer{}.analyze(root, options);
    assert(result.diagnostics.size() == 1);
    const Diagnostic& diagnostic = result.diagnostics.front();
    assert(diagnostic.code == DiagnosticCode::InvalidReturn);
    assert(diagnostic.message == "return outside a function");
    assert_location(diagnostic.location, "semantic_test.rol", 2, 3);
}

void test_candidates_and_no_closure_capture() {
    const AnalysisResult candidate = analyze(
        "def read_later():\n"
        "    return later\n"
        "if True:\n"
        "    later = 1\n");
    assert(candidate.diagnostics.empty());

    const AnalysisResult nested = analyze(
        "def outer():\n"
        "    value = 1\n"
        "    def inner():\n"
        "        return value\n"
        "    return inner()\n");
    assert(nested.diagnostics.size() == 1);
    assert(nested.diagnostics.front().code == DiagnosticCode::UndefinedName);
    assert(nested.diagnostics.front().message == "undefined name 'value'");
    assert_location(nested.diagnostics.front().location, "semantic_test.rol", 4, 16);
}

void test_established_runtime_operator_rules_and_multiple_errors() {
    const AnalysisResult supported = analyze(
        "print(True + 1)\n"
        "print(1 == \"1\")\n"
        "def identity(value):\n"
        "    return value + \"s\"\n"
        "identity(1)\n");
    assert(supported.diagnostics.empty());

    const AnalysisResult multiple = analyze(
        "print(missing)\n"
        "print(\"x\" - 1)\n"
        "unknown()\n");
    assert(multiple.diagnostics.size() == 3);
    assert(multiple.diagnostics[0].code == DiagnosticCode::UndefinedName);
    assert_location(multiple.diagnostics[0].location, "semantic_test.rol", 1, 7);
    assert(multiple.diagnostics[1].code == DiagnosticCode::InvalidOperator);
    assert_location(multiple.diagnostics[1].location, "semantic_test.rol", 2, 11);
    assert(multiple.diagnostics[2].code == DiagnosticCode::UndefinedName);
    assert_location(multiple.diagnostics[2].location, "semantic_test.rol", 3, 1);
}

} // namespace

int main() {
    test_valid_programs_and_dynamic_parameters();
    test_undefined_name_location();
    test_unknown_function();
    test_known_function_argument_count();
    test_recursive_function_argument_count();
    test_known_invalid_operation_location();
    test_return_outside_function_on_constructed_ast();
    test_candidates_and_no_closure_capture();
    test_established_runtime_operator_rules_and_multiple_errors();
}
