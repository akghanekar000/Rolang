# Rolang Semantic Analyzer Implementation Plan

**Status: implementation blueprint only.** This document proposes the first
working C++17 semantic-analysis engine. It does not implement the engine,
change the compiler, or approve any unresolved language rule. The current
behavior remains described by [LANGUAGE_SPEC.md](LANGUAGE_SPEC.md); design
questions remain in [SEMANTIC_ANALYSIS_ARCHITECTURE.md](SEMANTIC_ANALYSIS_ARCHITECTURE.md),
[V1_TYPE_SYSTEM_DESIGN.md](V1_TYPE_SYSTEM_DESIGN.md),
[V1_SEMANTICS_PROPOSAL.md](V1_SEMANTICS_PROPOSAL.md), and
[SEMANTICS_DECISIONS.md](SEMANTICS_DECISIONS.md).

## 1. Scope and compatibility boundary

The first engine is a read-only pass over the existing C++17 AST. It reports
structured findings and may return temporary per-node analysis facts. It does
not generate bytecode, execute the VM, modify nodes, add IR, or generate native
code. The initial implementation must not change the existing `check`, `run`,
or `compile` command behavior.

The following are observed facts about the current C++ implementation, not
automatic approval to move errors to compile time:

- The parser rejects duplicate parameters and `return` outside a function.
- At runtime, a function reads its invocation's current locals, then module
  globals, then the built-in `print`; nested functions do not capture enclosing
  locals.
- Function calls enforce exact positional arity at runtime. `print` accepts
  any argument count. Function and variable names can be overwritten.
- Values are dynamic. Current operator dispatch accepts Booleans as numeric
  operands; `+` also accepts two strings. Equality accepts all value pairs.
- `and` and `or` return Booleans, and dynamic calls and operations retain
  runtime checks.

**Pending approval before compiler integration:** whether undefined names,
known arity mismatches, and invalid known-type operations become compile-time
errors; name visibility and binding rules; declaration ordering; shadowing and
redeclaration; builtin shadowing; closure policy as a normative rule; and
which operator/type rules are normative. In particular, a successful analysis
must never be treated as proof that a dynamic program cannot fail at runtime.

## 2. Dependency graph

```text
frontend.hpp: Node, NodeKind, line/column
                  |
                  v
semantic_analyzer.hpp/.cpp
  |               |                  |                   |
  v               v                  v                   v
scope_manager   type_checker    diagnostic_engine   AnalysisResult
  |               |                  ^                   ^
  v               +------------------+                   |
symbol_table ------------------------+-------------------+

main.cpp (future, only after approval): parse -> analyze -> optional gate
                                                   -> existing compile -> VM

backend.hpp/.cpp: unchanged; no dependency from semantic modules
```

`SymbolTable` stores declaration records and per-scope name indexes.
`ScopeManager` owns scope relationships and performs parent-chain lookup.
`SemanticAnalyzer` coordinates traversal and supplies context to the services.
`TypeChecker` reasons only about language-level types and operator policy.
`DiagnosticEngine` owns diagnostic records, not CLI rendering.

## 3. Proposed file tree

```text
src/
  frontend.hpp                         # Existing AST; no first-phase edits
  frontend.cpp                         # Existing lexer/parser; unchanged
  semantic/
    semantic.hpp                       # Public include for the semantic API
    semantic_types.hpp                 # Location, IDs, Type, Symbol, results
    symbol_table.hpp
    symbol_table.cpp
    scope_manager.hpp
    scope_manager.cpp
    diagnostic_engine.hpp
    diagnostic_engine.cpp
    type_checker.hpp
    type_checker.cpp
    semantic_analyzer.hpp
    semantic_analyzer.cpp
  backend.hpp                          # Existing backend; unchanged
  backend.cpp                          # Existing backend; unchanged
tests/
  semantic_tests.cpp                   # Direct C++ API tests, no new framework
```

These are proposed boundaries, not a requirement for many independent
frameworks. Keep each service small; `semantic.hpp` should only aggregate
headers. Add semantic sources to the Makefile only when implementation starts.
Use the existing compiler and standard library; do not add a package dependency.

## 4. AST integration

### Existing nodes and child layout

The analyzer can visit every current construct through `NodeKind`, `text`,
`number`, `string_literal`, `params`, and `children`:

| Node kind | Existing representation and children |
| --- | --- |
| `Program`, `Block` | `children` are statements in source order. |
| `Function` | `text` is the function name, `params` are positional parameter names, and `children[0]` is the body `Block`. |
| `If` | `children[0]` condition, `[1]` then-block, optional `[2]` else-block. |
| `While` | `children[0]` condition, `[1]` body block. |
| `Return` | No child for bare return; otherwise `children[0]` is the result expression. |
| `Assign` | `text` is the assigned name; `children[0]` is the value expression. |
| `Expression` | `children[0]` is the expression. |
| `Literal` | A string has `string_literal == true` and its decoded value in `text`; `True`, `False`, and `None` are represented in `text`; a numeric literal uses `number` and normally empty `text`. |
| `Name` | `text` is the identifier. |
| `Unary`, `Binary` | `text` is the operator; children are operand(s), in order. |
| `Call` | `children[0]` is the callee; remaining children are positional arguments. |
| `Pass` | No children. |

Preserve and validate these child-count assumptions at the analyzer boundary.
The parser produces these shapes, but a public analysis API can also receive a
manually built or malformed `Node`; reject malformed AST structure explicitly
(for example, with `std::logic_error`) rather than turning it into a
user-facing semantic diagnostic or silently ignoring it.

### Available source locations

`Token` and `Node` each expose `int line` and `int column`, initialized to
1-based defaults. The parser copies locations onto statement and expression
nodes. Important location choices from the actual parser:

- A `Name` node points at the identifier token.
- A `Binary` or `Unary` node points at its operator token.
- A `Call` node points at the opening parenthesis; its callee child retains
  the callee's location.
- Parenthesized expressions return the inner expression node, so they do not
  acquire a separate pair-of-parentheses location.

Use the operator node for invalid-operator diagnostics and the callee `Name`
node for an undefined name or known-call arity error. Attach function
declaration locations as related information for arity errors. The AST has no
source filename; pass it via analysis options. It has no end positions, so
initial diagnostics should be point-located and must not invent source ranges.

### Reuse and eventual AST changes

The first implementation should inspect `const Node&` and must not mutate it.
Do not add declaration nodes for assignments: assignment is currently a
statement that writes a name and not a distinct declaration syntax. Keep
derived information in `AnalysisResult` side tables keyed by `const Node*`;
this is safe only while the same immutable tree remains alive and unchanged.
Do not serialize those pointers, cache them across parses, or retain them past
the AST lifetime.

If later consumers need persistent/cross-pass facts, add a stable `NodeId` and
prefer source spans with end positions in a separate, deliberate frontend
change. Preserve literal spelling/kind only if a future language decision
requires distinctions the current `double` AST field cannot express (for
example, separate integer and floating-point values). Such AST changes are
not prerequisites for the initial dynamic analyzer and are not part of this
blueprint's implementation.

## 5. Proposed C++17 interfaces

The following concrete declarations show the intended service contracts. They
are sketches for implementation, not code added by this task. Keep types
language-level: do not include `backend.hpp`, use a VM `Value`, infer semantics
from `std::variant` indices, or depend on bytecode opcodes.

### `semantic_types.hpp`

```cpp
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

enum class TypeKind {
    Dynamic, Error, None, Bool, Number, String, Function
};

struct Type {
    TypeKind kind = TypeKind::Dynamic;
    std::optional<SymbolId> function_symbol;
};

enum class SymbolKind { Variable, Parameter, Function, Builtin };
enum class ScopeKind { Module, Function };

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
    InvalidOperator,
    DuplicateDeclaration
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
    // Future compiler integration must select the policy explicitly after
    // language and diagnostic-phase decisions are approved.
};

struct AnalysisResult {
    std::vector<Diagnostic> diagnostics;
    // Valid only while the analyzed const Node tree is alive and unchanged.
    std::unordered_map<const Node*, SymbolId> resolved_names;
    std::unordered_map<const Node*, SymbolId> known_call_targets;
    std::unordered_map<const Node*, Type> inferred_types;

    bool has_errors() const;
};

} // namespace rolang::semantics
```

`Number` represents the current C++ runtime's single numeric type for
operator-compatibility analysis; it does not settle a future Int/Float split.
`Dynamic` means the analyzer cannot determine a value's type. `Error` is an
internal recovery type to prevent cascaded operator errors, not a runtime
value. The minimal initial signature needs names/count and variadic status;
typed parameter and return types are not in the current AST.

### `SymbolTable` and `ScopeManager`

```cpp
// symbol_table.hpp
namespace rolang::semantics {

struct InsertResult {
    std::optional<SymbolId> inserted;
    std::optional<SymbolId> existing;
};

class SymbolTable {
public:
    InsertResult insert(Symbol symbol);
    std::optional<SymbolId> find_local(
        ScopeId scope, const std::string& name) const;
    const Symbol* get(SymbolId id) const;
    void create_scope_index(ScopeId scope);
};

} // namespace rolang::semantics

// scope_manager.hpp
namespace rolang::semantics {

struct Scope {
    ScopeId id = 0;
    std::optional<ScopeId> parent;
    ScopeKind kind = ScopeKind::Module;
    std::optional<SymbolId> owning_function;
};

class ScopeManager {
public:
    explicit ScopeManager(SymbolTable& symbols);
    ScopeId module_scope() const;
    ScopeId create_scope(
        ScopeKind kind, std::optional<SymbolId> owning_function = std::nullopt);
    void enter(ScopeId scope);
    void leave();
    ScopeId current_scope() const;
    std::optional<SymbolId> lookup(const std::string& name) const;
    std::optional<SymbolId> lookup_local(const std::string& name) const;
    const Scope& get_scope(ScopeId id) const;
};

} // namespace rolang::semantics
```

`SymbolTable` owns stable symbol records and indexes them by scope/name;
`ScopeManager` owns the scope records and current traversal stack. Reserve
symbol/scope ID zero as invalid and never reuse IDs during one analysis.
Insertion reports an existing symbol rather than deciding whether redeclaring
or overwriting a binding is legal. The analyzer applies only the selected
language policy.

For the first compatibility-oriented analyzer, create module and function
scopes only. `If` and `While` blocks do not get independent scope records
unless block scope is approved. A scope-parent chain is a service capability,
not authorization to capture enclosing-function locals. Under the observed
runtime-compatible profile, lookup from a function checks its own scope and
then the module scope, skipping any enclosing function scopes; builtin lookup
is a final, separate fallback after user scopes. Builtins should be
represented distinctly from user symbols so that the analyzer can defer
shadowing behavior rather than hard-code it into lookup.

### `DiagnosticEngine`

```cpp
// diagnostic_engine.hpp
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
};

} // namespace rolang::semantics
```

The engine stores diagnostic data only. CLI formatting, source excerpts,
carets, exit codes, and stream selection belong to a future consumer. Return
diagnostics in deterministic source order (line, column, then stable emission
order); related declaration locations are attached to the primary error.
Deduplication should use code and primary node/location, not message text.
Do not catch and discard allocation failures or malformed internal state.

### `TypeChecker`

```cpp
// type_checker.hpp
namespace rolang::semantics {

class TypeChecker {
public:
    explicit TypeChecker(DiagnosticEngine& diagnostics);

    Type type_of_literal(const Node& literal) const;
    Type check_unary(const Node& operation, Type operand);
    Type check_binary(const Node& operation, Type left, Type right);
    Type check_call_result(const Node& call, Type callee);
};

} // namespace rolang::semantics
```

The analyzer recursively determines operand types and passes them here. The
checker returns `Dynamic` for unknown facts and propagates `Error` to avoid
cascades. First-phase inference is intentionally shallow: literal types and
known function symbols only. Variable types remain `Dynamic` because a
binding can hold different values over time. `check_call_result` returns
`None` for the known built-in `print` and `Dynamic` for user functions because
the language has no return annotations; it must not infer a permanent result
type from one return expression. Unknown callees also remain `Dynamic`.

Operator validation is constrained to combinations directly established by
the current C++ runtime, subject to approval before reporting them as
compile-time errors:

- Numeric operators accept `Number` and `Bool` operands at runtime; `Bool`
  therefore participates in the compatibility check as numeric, without
  deciding whether that rule should remain normative.
- `+` accepts two strings or two numeric operands; it rejects a known
  string/non-string pair.
- Ordering operators require numeric operands. Equality and inequality accept
  any pair. `and`, `or`, and `not` accept dynamic truthy/falsey values.
- Division by zero and non-finite arithmetic are value/runtime conditions,
  not type incompatibilities. They remain VM checks; constant folding is out
  of scope.

Every invalid-operator diagnostic must point to `operation.line` and
`operation.column`. If operator policy is not approved, `TypeChecker` may
record an analysis fact but must not emit a blocking diagnostic.

### `SemanticAnalyzer`

```cpp
// semantic_analyzer.hpp
namespace rolang::semantics {

class SemanticAnalyzer {
public:
    AnalysisResult analyze(
        const Node& root, const AnalysisOptions& options = {}) const;
};

} // namespace rolang::semantics
```

The implementation creates one per-call context containing the symbol table,
scope manager, type checker, diagnostics, return/function context, and
temporary declaration candidate indexes. No mutable analysis state is shared
between calls. Before ordinary recursion it validates AST shape and gathers
candidate bindings/signatures without changing the AST. It then analyzes
statements and expressions, records only resolved facts, and returns
diagnostics plus side tables. Invalid AST structure is an internal API misuse
and should fail explicitly; it is not a language semantic diagnostic.

Do not claim declaration pre-registration implements forward-reference
semantics. Candidate collection is solely for conservative existence
analysis; whether a declaration is visible before execution remains pending.
Likewise, a call target is "known" only where the binding is unambiguous
under the selected policy. Indirect calls, reassigned names, and unresolved
targets remain dynamic and are left to runtime.

For the conservative existence index, collect module assignments and
functions from module-level suites, including nested `if`/`while` blocks, but
not from function bodies. For each function, collect its parameters and its
own assignments/nested function bindings, but do not merge a nested function's
body into its parent's candidate set. This index means only "a binding might
exist"; it does not claim definite assignment, visibility before execution,
or a stable function target.

## 6. First implementation phase

Implement the service skeleton and rules below without connecting it to the
CLI. Add semantic tests through the public analysis API.

| Check | First implementation behavior | Rule status / gate |
| --- | --- | --- |
| Undefined name | Report a name only when no possible binding exists in the name's current function parameters/assignments, accessible module bindings, or builtin environment. Do not flag names with a possible assignment in a branch, loop, or later declaration merely because definite assignment was not proven. Do not count enclosing-function locals as candidates for a nested function. | Runtime lookup precedence is observable; static error timing, visibility, closures, and name rules are **pending approval**. This deliberately conservative subset needs an approved compile-time diagnostic policy before CLI activation. |
| Function existence | A direct call through a `Name` may resolve to a user function only when the target is unambiguous under the selected binding policy. An entirely unknown callee name is an undefined-name finding. A non-name/dynamic callee is not statically classified as non-callable. | Function values, reassignment, builtin shadowing, and forward-reference treatment are **pending approval**. |
| Known arity | Compare argument count with `FunctionSignature::parameter_names.size()` only for a known user-function target. `print` is variadic; unknown/dynamic callees are unchecked. Attach the declaration location as related information. | Exact positional count is current runtime behavior and a V1 proposal direction; moving the error earlier and declaration visibility are **pending approval**. |
| Return context | Carry `inside_function` through traversal. A `Return` node outside a function reports `invalid-return`; one inside a function is valid, whether bare or valued. Do not require all paths to return or check return types. | Parser currently rejects source-level out-of-function return before analysis. This analyzer defense applies to direct/malformed AST input; do not reclassify parser errors. Implicit `None` and typed returns are unchanged. |
| Known invalid operations | Check only operand-type combinations from the current runtime table listed above, and only when operand types are statically known. Dynamic operands produce no static incompatibility claim and retain VM checking. | Promoting the finding to a compile-time error and normative operator rules are **pending approval**. Integer/float distinction, strict equality, conversions, Boolean arithmetic, truthiness, and division/modulo design remain pending. |
| Source locations | Copy node line/column and caller-supplied source name into every diagnostic. | Available now; end spans and source text excerpts require future frontend/consumer support. |

Duplicate function/variable declaration errors, definite assignment, whole
function local classification, all-path returns, typed parameter/result
checks, block scopes, closures, and flow-sensitive type narrowing are not
first-phase features. Duplicate parameters remain parser errors.

## 7. Compiler integration (future, gated)

The eventual placement is after parsing and before existing code generation:

```text
source -> tokenize -> parse -> const AST
                                  |
                                  v
                         semantic analyze
                           | diagnostics
                           | side tables (optional, later)
                           v
                      existing compile(tree)
                           |
                           v
                      existing VM / listing
```

Do not wire this pass into `main.cpp` in the first implementation. A direct
API and its tests can work without changing user-visible behavior. When
semantic phase policy is approved, use one caller-side helper to analyze,
format diagnostics, and gate code generation; keep the analyzer independent
of `main.cpp` and the backend.

Eventually:

- **`check`** should parse and then analyze, but continue to avoid bytecode
  generation and execution. This intentionally changes its current
  syntax-only contract and requires approval of which semantic findings fail
  the command.
- **`run`** should parse, analyze, stop on approved blocking errors, then call
  the same `compile(tree)` and VM path as today. The VM's runtime name, call,
  type, arithmetic, and resource checks stay in place for dynamic cases.
- **`compile`** should parse, analyze, stop on approved blocking errors, then
  produce the same readable bytecode listing through the existing backend.
  Semantic side tables must not change code generation in this first engine.

The eventual helper must leave `tokens` and `ast` behavior unchanged unless
separately approved. Diagnostic text and CLI error codes should be implemented
at the CLI boundary. An analysis result is not passed into `compile` until a
separate requirement justifies a backend consumer. Do not alter VM or backend
interfaces, runtime checks, or output as part of this semantic work.

## 8. C++ regression test plan

Add a standalone `tests/semantic_tests.cpp` executable using the standard
library and existing frontend/analyzer API, without a new test framework. It
should parse source and inspect structured diagnostics; do not assert parser
error strings as semantic results. The helper below illustrates the intended
assertions:

```cpp
#include "frontend.hpp"
#include "semantic/semantic_analyzer.hpp"

#include <cassert>
#include <memory>
#include <string>
#include <utility>

using namespace rolang;
using namespace rolang::semantics;

struct AnalyzedSource {
    std::unique_ptr<Node> tree;
    AnalysisResult result;
};

static AnalyzedSource analyze_source(const std::string& source) {
    auto tree = std::make_unique<Node>(parse(tokenize(source)));
    AnalysisOptions options;
    options.source_name = "semantic_test.rol";
    AnalysisResult result = SemanticAnalyzer{}.analyze(*tree, options);
    return {std::move(tree), std::move(result)};
}

static const Diagnostic& only_diagnostic(const AnalysisResult& result) {
    assert(result.diagnostics.size() == 1);
    return result.diagnostics.front();
}
```

The helper keeps the AST alive alongside the result because side-table keys
are node addresses. The initial cases and expected structured output are:

| Case | Exact Rolang input / setup | Expected result |
| --- | --- | --- |
| Resolved module name and builtin | `value = 3\nprint(value)\n` | No diagnostics. |
| Valid known function call with dynamic argument | `def identity(x):\n    return x\nidentity("text")\n` | No diagnostics; parameter values remain dynamic. |
| Valid current numeric operation | `print(True + 1)\n` | No diagnostic under the current-runtime compatibility profile; do not silently apply the proposed Bool-is-not-numeric rule. |
| Definite absent name | `print(missing)\n` | Exactly one `UndefinedName` at `semantic_test.rol:1:7`, message `undefined name 'missing'`, no related locations. CLI promotion remains gated. |
| Wrong count for a known function | `def add(a, b):\n    return a + b\nadd(1)\n` | Exactly one `ArgumentCount` at `semantic_test.rol:3:1`, message `function 'add' expects 2 arguments, got 1`; one related location at `semantic_test.rol:1:1`, message `function declared here`. |
| Known invalid operand types | `print("x" - 1)\n` | Exactly one `InvalidOperator` at `semantic_test.rol:1:11`, message `operator '-' does not accept String and Number`. |
| Return inside a function | `def f():\n    return 1\n` | No diagnostics. |
| Return outside a function | Construct `Node root{NodeKind::Program, 1}; Node ret{NodeKind::Return, 2}; ret.column = 3; root.children.push_back(ret);` and analyze the root. | Exactly one `InvalidReturn` at `semantic_test.rol:2:3`, message `return outside a function`. The source parser's current rejection is not part of this analyzer unit test. |

For the negative cases, assert the diagnostic code, severity, source name,
line, column, exact message, related-location count, and related location
where shown. Also assert no extra/cascade diagnostics. Add these focused
guard cases:

1. `def f(x):\n    return x + "s"\nf(1)\n` must not infer a permanent
   parameter type or reject the argument; only an operation whose operands
   are known may be diagnosed.
2. `print(1 == "1")\n` must not be flagged as an invalid operator: equality
   accepts cross-type values under the current runtime.
3. `def f():\n    print(later)\nlater = 1\n` and a branch/loop assignment
   candidate must not be reported as definitely absent merely because the
   analyzer cannot prove execution order or definite assignment.
4. `def identity(x):\n    return x\ncallee = identity\ncallee(1)\n` must not
   get an arity diagnostic unless the callee is known under the selected
   policy.
5. A nested function reference to an enclosing function's local must not be
   resolved through accidental closure lookup.
6. Preserve CLI/backend regressions by running the existing `make test`;
   before explicit CLI integration, its expected output and exit statuses
   remain unchanged.

The new test executable should be added to `make test` only when the analyzer
exists. Continue to run the existing Python-driven C++ compiler tests as the
runtime regression baseline; do not treat the Python Rolang prototype tests
as semantic analyzer coverage.

## 9. Risks and mitigations

| Risk | Mitigation |
| --- | --- |
| Earlier diagnostics change observable behavior. | Keep the API out of the CLI initially; require an explicit phase-policy decision before any command rejects a program. |
| Runtime lookup differs from a simple lexical symbol table. | Separate possible-name candidates from definite bindings; keep dynamic and control-flow-dependent cases unresolved; do not impose whole-function locals or closures. |
| Function declarations can be overwritten or conditional. | Check arity only for a proven known target; leave indirect, shadowed, conditionally available, and reassigned calls to runtime. |
| Type design proposals conflict with current VM behavior. | Start with `Dynamic`, `Error`, and current runtime-level types; track approval-required rules explicitly; keep VM validation authoritative. |
| Parser already rejects return context and duplicate parameters. | Do not move checks across phases; test analyzer return defense with constructed AST and retain parser tests. |
| AST has no stable identity or full spans. | Keep side tables per-analysis and keyed by node address only; do not persist or cache them; add IDs/spans only when required. |
| New pass accidentally couples to backend or affects code generation. | Keep semantic headers backend-free; analyze `const Node&`; do not pass metadata into `compile` in phase one. |
| Malformed AST causes crashes or success-shaped defaults. | Validate node arity/kind contracts and fail explicitly as an internal API error; never silently skip malformed nodes or report them as a source-language diagnostic. |

## 10. Implementation sequence

1. **Approve phase and language gates:** undefined-name timing, declaration
   visibility/forward references, scope and global lookup, closures, function
   shadowing/reassignment, exact-arity promotion, operator compatibility,
   diagnostic categories/messages, and blocking behavior per CLI command.
2. **Freeze the small API contract:** source locations, stable-per-analysis
   IDs, diagnostic ordering, `TypeKind`, symbol/signature representation,
   malformed-AST handling, and pointer-key lifetime.
3. **Add semantic data types and diagnostics:** create the headers, structured
   diagnostic engine, deterministic ordering, and direct C++ unit-test
   scaffolding; keep the compiler pipeline untouched.
4. **Implement symbol storage and scope traversal:** module/function scopes,
   owner relationships, parameter symbols, candidate declarations, and
   approved builtin lookup. Do not add block scopes or duplicate-declaration
   policy without approval.
5. **Implement conservative name and call resolution:** source-located
   undefined-name findings, unambiguous direct user-function targets, related
   declaration locations, and known arity checks; retain dynamic cases for
   runtime.
6. **Implement AST validation and statement context:** verify child shapes,
   track function context, accept in-function returns, and test out-of-function
   return on a constructed AST without changing parser behavior.
7. **Implement shallow expression typing:** literal inference, dynamic
   propagation, current-runtime operator compatibility, and cascade
   suppression; leave all unresolved type decisions pending.
8. **Complete focused semantic tests and review false-positive boundaries:**
   run new tests plus `make test`; preserve existing runtime and CLI outputs.
9. **Only after approval, integrate `check`, `run`, and `compile`:** add
   caller-side analysis and formatting/gating while leaving backend and VM
   behavior intact for accepted programs and dynamic runtime checks.
10. **Document approved behavior separately:** update the language spec and
    decision register only after semantic choices and compiler integration
    have been approved and tested. Keep IR and native generation out of scope.

## Validation performed for this blueprint

The requested `make test` was run before any file changes. It completed with
exit code **0**:

```text
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -Isrc src/main.cpp src/frontend.cpp src/backend.cpp -o build/rolang
sh ./tests/smoke.sh ./build/rolang
examples/hello.rol: syntax OK
Hello from Rolang
factorial(5) = 120
3
2
1
compiled examples/hello.rol -> build/hello.rbc
python3 tests/test_cpp.py ./build/rolang
....................
----------------------------------------------------------------------
Ran 20 tests in 0.180s

OK
```

This is a documentation-only change; the compiler was not rebuilt after the
document was added, and no compiler behavior was modified.
