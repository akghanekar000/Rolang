# Rolang Semantic Analysis Engine Architecture

**Status: architecture proposal only.** This document describes a future
C++17 semantic-analysis pass. It does not approve the proposed V1 language
rules, change compiler behavior, or implement an engine. The current C++
behavior remains described by [LANGUAGE_SPEC.md](LANGUAGE_SPEC.md); proposed
rules and open choices remain in
[V1_SEMANTICS_PROPOSAL.md](V1_SEMANTICS_PROPOSAL.md),
[V1_TYPE_SYSTEM_DESIGN.md](V1_TYPE_SYSTEM_DESIGN.md), and
[SEMANTICS_DECISIONS.md](SEMANTICS_DECISIONS.md).

## Goals and non-goals

The engine should provide a source-located analysis result for the existing
AST, independent of bytecode and VM implementation. It should centralize
name and signature resolution, validate language rules that can be decided
before execution, and leave genuinely dynamic checks to the runtime.

It should:

- Register and resolve symbols through explicit scope relationships.
- Validate function definitions and calls where a signature is known.
- Check statement context, operator rules, and type compatibility to the
  extent allowed by the selected dynamic or future checked mode.
- Produce structured diagnostics, not formatted strings from each check.
- Return side-table information useful to later bytecode, IR, or native
  compilation without making semantic analysis depend on those targets.

It should not:

- Change the parser, VM, backend, CLI, or language contract in this milestone.
- Infer a static type for a dynamic binding and then reject later assignments.
- Claim runtime safety from a successful static analysis.
- Decide unresolved name binding, conversion, numeric, equality, or scope
  rules on behalf of the language.
- Contain bytecode opcodes, VM `Value` alternatives, IR instructions, or native
  ABI details.

## Current integration facts

The active compiler is C++17. `tokenize` and `parse` in `src/frontend.hpp`
produce a `Node` tree whose nodes have `line` and `column`; child order and
payload fields encode constructs. There is no stable node ID, explicit
declaration node for variables, type annotation, or source-span end position.
The parser rejects duplicate parameters and `return` outside a function as
parser errors today.

The existing CLI's `check` command only tokenizes and parses. `run` parses,
compiles directly to the existing backend's bytecode, and executes the VM.
The backend resolves names dynamically at execution and applies runtime
operator and call checks. Module suites share the module globals; function
calls get parameter/local maps; there are no enclosing-function closure
scopes. These facts are observations, not approval of future binding rules.

The existing tests in `tests/test_cpp.py` exercise the C++ compiler. The
separate Python prototype tests do not define C++ or Rolang semantics.

## Proposed architecture

Keep the implementation to a visitor/orchestrator and a few cohesive
services. `ScopeManager` owns and traverses `SymbolTable` instances rather
than introducing a separate object per scope. `FunctionResolver` is a small
resolver over those tables, not a second registry. `TypeChecker` uses a
language-level type model and an explicit policy; it does not inspect VM
values. `DiagnosticEngine` owns diagnostic construction and accumulation.

```text
                         +--------------------+
                         |     AST (Node)     |
                         +---------+----------+
                                   |
                                   v
                         +---------+----------+
                         |  SemanticAnalyzer  |
                         | traversal / phases |
                         +---+----+------+----+
                             |    |      |
               +-------------+    |      +------------------+
               v                  v                         v
       +-------+-------+   +------+-------+         +-------+-------+
       | ScopeManager  |   | Function     |         | TypeChecker   |
       | current/parent|   | Resolver     |         | Type/operation|
       +-------+-------+   +------+-------+         +-------+-------+
               |                  |                         |
               v                  +------------+------------+
       +-------+-------+                       |
       | SymbolTable   |                       v
       | declarations  |              +--------+---------+
       +---------------+              | DiagnosticEngine |
                                      +--------+---------+
                                               |
                                               v
                              AnalysisResult (diagnostics,
                               binding/type/flow side tables)
                                               |
                            +------------------+----------------+
                            v                  v                v
                       Bytecode now     Future language IR   Future native
                       (no coupling)     (no coupling)       backend
```

### Component responsibilities

| Component | Responsibility | Must not do |
| --- | --- | --- |
| `SemanticAnalyzer` | Own analysis phases and AST traversal; establish module/function/branch/loop contexts; coordinate registration, resolution, checking, and diagnostics; return one `AnalysisResult`. | Generate bytecode, execute code, or silently apply an unresolved policy. |
| `SymbolTable` | Store declarations for one lexical environment; index by name; reject duplicate *declarations* only where the language rule requires it; retain declaration identity and source location. | Treat every assignment as a declaration or assume assignments are immutable bindings. |
| `ScopeManager` | Create and leave scopes, identify each scope's kind/owner, provide lexical parent lookup, and distinguish module from function and nested-block scopes. | Invent closure capture or function-local rules. |
| `FunctionResolver` | Resolve callable symbols when statically known; expose parameter names/count, declaration location, and future type signature; distinguish known user functions from dynamic/unknown callees and builtins. | Require all calls to be statically known or impose parameter value types in dynamic mode. |
| `TypeChecker` | Represent language types; infer expression result types to the available precision; apply approved operator, compatibility, and conversion policies; mark unknown/dynamic operations for runtime checks. | Use `std::variant` indices, C++ RTTI, or VM implementation details as Rolang type rules. |
| `DiagnosticEngine` | Construct, collect, and optionally de-duplicate structured diagnostics; attach primary and related locations; provide deterministic ordering for consumers/tests. | Convert errors into default values or swallow internal failures. |

`FunctionResolver` may be a small service or a focused part of the analyzer
until there is enough independent logic to justify its own source module.
Likewise, `TypeChecker` does not require a separate visitor: it can be invoked
by the analyzer for expression nodes. The names denote responsibilities, not
a requirement to create six heavyweight classes.

## Core data model and interfaces

The following C++17 sketches define boundaries, not exact source files or
approved public APIs. They avoid dependencies beyond the standard library.

```cpp
#include "frontend.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rolang::semantics {

struct SourceLocation {
    std::string source_name;
    int line = 1;
    int column = 1;
};

struct SourceSpan {
    SourceLocation begin;
    SourceLocation end; // optional until the frontend provides end positions
};

using ScopeId = std::uint32_t;
using SymbolId = std::uint32_t;

enum class SymbolKind {
    Variable,
    Function,
    Parameter,
    Type
};

enum class TypeKind {
    Dynamic, Error, None, Bool, Int, Float, String, Function, Nominal
};

struct Type {
    TypeKind kind = TypeKind::Dynamic;
    std::uint32_t nominal_id = 0;
    std::vector<Type> arguments;
};

struct FunctionSignature {
    std::vector<Type> parameters;
    Type result;
    bool variadic = false;
};

struct Symbol {
    SymbolId id;
    std::string name;
    SymbolKind kind;
    ScopeId owner;
    SourceSpan declaration;
    Type type; // Dynamic for unannotated V1 bindings/parameters
    std::optional<FunctionSignature> signature;
};

class SymbolTable {
public:
    std::optional<SymbolId> find_local(const std::string& name) const;
    bool insert(Symbol symbol); // duplicate policy supplied by language rules
    const Symbol* get(SymbolId id) const;
};

class ScopeManager {
public:
    ScopeId push_scope(ScopeId owner, std::string name);
    void pop_scope();
    ScopeId current_scope() const;
    std::optional<SymbolId> lookup(const std::string& name) const;
};

enum class DiagnosticCategory {
    UndefinedName,
    DuplicateDeclaration,
    InvalidFunction,
    ArgumentCount,
    InvalidReturn,
    TypeMismatch,
    InvalidOperator,
    InvalidControlFlow
};

struct RelatedLocation {
    SourceSpan span;
    std::string message;
};

struct Diagnostic {
    DiagnosticCategory category;
    SourceSpan primary;
    std::string message;
    std::vector<RelatedLocation> related;
};

struct AnalysisResult {
    std::vector<Diagnostic> diagnostics;
    // Per-analysis side tables: node -> resolved symbol, inferred type,
    // scope, or runtime-check requirement. Stable NodeId is preferred.
};

struct AnalysisOptions {
    // Checked mode is a future extension, not a current language feature.
    bool checked_types = false;
};

AnalysisResult analyze(const Node& root, const AnalysisOptions& options);

} // namespace rolang::semantics
```

These sketches are illustrative API boundaries, not approved public APIs. A
type identity is language-defined and independent of a C++ representation.
Future nominal types need stable declaration identities; future generic types
need a structural descriptor. `Dynamic` means “not statically known,” while
`Error` is an internal recovery type that prevents cascaded diagnostics;
neither is a runtime value.

An eventual `AnalysisResult` can contain:

- A binding from each name-use node to `SymbolId`.
- A scope identity and inferred `Type` for expression nodes.
- A known call target/signature when resolution succeeds.
- Per-operation runtime-check requirements for dynamic operands.
- A control-flow summary where the language defines one.
- Diagnostics in source order.

The current AST has no stable `NodeId`. For an initial implementation, a
side table keyed by `const Node*` is valid only for the lifetime of the
unchanged AST and analysis result. Before AST rewrites, caching, or use across
transformations, add stable node IDs or another explicit identity scheme.
Do not silently treat an address as a persistent identifier.

### Symbol and scope ownership

A symbol records at least its name, kind, owning scope, source location,
language type (initially `Dynamic` for ordinary unannotated bindings), and
function signature when applicable. `Parameter` is kept distinct from
`Variable` so duplicate-parameter and parameter-conflict diagnostics can be
precise. Function symbols carry parameter names and arity even when their
parameter/result types are dynamic. A future type symbol carries a nominal
`TypeId` and declaration identity, not only a spelling.

`ScopeManager` owns a parent chain:

```text
module scope
  ├── function scope (parameters and function bindings)
  │     └── nested function scope
  ├── conditional/loop block scope (only if adopted)
  └── global type declarations (future)
```

Lookup begins in the current scope and walks lexical parents to the module.
Each successful resolution returns the owning `SymbolId`, not a copied
symbol. A name with no lexical binding can then be checked against the
approved builtin namespace. Function scopes are not automatically closure
scopes: lookup must not expose an enclosing function's locals unless closure
semantics are approved. Whether blocks introduce bindings, whether they can
shadow, how a function resolves globals, and whether declarations are visible
before execution all require language decisions; the diagram is a model, not
an adopted rule.

Module/global resolution and function-local assignment are separate
questions. The current runtime's local/global map behavior is not equivalent
to every proposal in the decision register. The engine must not predeclare all
names or classify function assignments as whole-function locals until the
applicable policy is approved. Likewise, a name assigned in an `if` or
`while` must not acquire block ownership merely because the AST has a `Block`
node.

## Analysis phases and execution flow

```text
source
  |
  v
tokenize --------------------------> lexical/syntax diagnostics
  |
  v
parse -> existing AST --------------> parser diagnostics
  |
  v
SemanticAnalyzer
  1. establish module and builtin environment
  2. register declarations according to approved visibility rules
  3. resolve names and function call targets
  4. check parameters, returns, operators, compatibility, and control flow
  5. return diagnostics + optional analysis side tables
  |
  +---- if no blocking diagnostics and enabled ----> existing bytecode compile
  |                                                  (future integration only)
  +---- dynamic values remain guarded -------------> existing runtime checks
```

The suggested phases are not necessarily full separate AST traversals.
Registration before body validation can support recursion and useful
duplicate-declaration locations. However, registering every declaration in a
whole block before checking any expression creates forward visibility that
the current execution model does not provide. Pick source-order registration
or block predeclaration only after specifying definition visibility and
initialization behavior. A safe first implementation can resolve names
against the selected policy and retain unknown/dynamic cases as runtime
lookups rather than silently changing source evaluation order.

Detailed flow:

1. The caller passes a parsed root and an explicit analysis profile.
   The analyzer creates module scope and registers only approved builtins.
2. A registration walk records declarations, function parameters, and
   function signatures. Duplicate function/variable names are diagnosed only
   if the selected namespace and redeclaration rules prohibit them.
3. A validation walk resolves `Name` expressions, creates or updates binding
   metadata as allowed, and asks `FunctionResolver` about calls.
4. `TypeChecker` evaluates expression types conservatively. Dynamic operands
   stay `Dynamic`; known-invalid operations may be diagnosed only in a mode
   whose policy makes that a compile-time error. Unknown cases retain runtime
   checks.
5. Statement checks carry context (inside function, inside loop, return
   signature, reachable/definitely-returned state) only for syntax and
   language constructs that exist and whose rule is approved.
6. `DiagnosticEngine` emits structured findings. The consumer decides whether
   errors block compilation; the CLI presentation layer formats them.

The current `return`-outside-function and duplicate-parameter failures happen
in parsing, so they are syntax/parser diagnostics today. Moving either check
into semantic analysis would be a deliberate phase/API change, not a
requirement to reclassify historical errors.

## Function, parameter, and return validation

The function signature is a symbol-table property. For the existing AST, a
function node provides its name and positional parameter names. No parameter
or result type annotation exists.

- **Definition validation:** parameter names are unique in the current parser.
  Future signature rules (duplicate function names, shadowing, parameter
  annotations, return annotations) are policy-dependent.
- **Call validation:** when a call target resolves to a user function, compare
  supplied argument count with its signature if exact arity is approved. For
  the existing dynamic language, argument values remain unconstrained and
  flow through parameters unchanged. If a target is a dynamic expression,
  leave callability and arity checks to runtime.
- **Builtins:** register builtins in a distinct namespace/environment.
  `print` is currently variadic and can be shadowed by user bindings; whether
  and how builtins participate in lexical shadowing must match approved
  rules.
- **Return validation:** verify function context if the parser has not already
  enforced it. With no declared result type, any expression (or implicit
  `None`) remains dynamic; do not reject returns because branch paths differ.
  Future declared return types can use compatibility checks. A “must return on
  every path” rule is not implied by the current implicit-`None` behavior.

Parameter **count** validation is distinct from parameter **type**
validation. In dynamic mode, a parameter does not constrain the argument's
runtime type. Statically rejecting `f("x")` because a call passes a string is
not valid unless an approved annotation/checking mode gives `f` a typed
signature.

## Type checking and runtime boundary

Use the proposed type-system design as a vocabulary, not a normative rule.
A minimal language-level type model should include `Dynamic`, an internal
`Error`, `NoneType`, `Bool`, `Int`, `Float`, `String`, `Function`, and a
future nominal type identity. `Array<T>`, `Object`, structures, and
function types can be represented as descriptors when those features are
specified; they do not need runtime or parser support in the first analysis
engine.

Dynamic typing and compile-time validation coexist as follows:

1. Assignments, arguments, and returns remain unconstrained in default dynamic
   mode; static inference is descriptive, not a permanent binding constraint.
2. A statically obvious undefined reference or known bad call/operator can be
   diagnosed only if the language chooses that as a compile-time semantic
   error. This impacts current `check` and the timing of `run` failures.
3. When an operand, callee, or binding is `Dynamic`, analysis records an
   unresolved requirement or leaves the operation to the runtime. It must
   not assume success, coerce a value, or emit a success-shaped fallback.
4. The VM retains runtime checks for dynamic values, malformed/foreign input,
   and any path not proven safe. Static diagnostics are earlier feedback, not
   a replacement for runtime enforcement.
5. Optional future checked typing is a separately enabled mode, probably
   introduced alongside approved annotation syntax. A typed operation that
   receives a `Dynamic` value needs a runtime guard or a specified checked
   boundary.

`TypeChecker` should separate type identity, assignment compatibility,
operator compatibility, and conversion. It should consult an explicit
language policy for operations rather than encode unapproved outcomes in
switch statements. For example:

| Expression | Current C++ runtime fact | Proposed rule / status |
| --- | --- | --- |
| `True + 1` | Accepted as numeric addition. | Whether `Bool` is numeric is approval-required. |
| `1 + "x"` | Runtime error. | Numeric/string operator compatibility must be explicit. |
| `True == 1` | True. | Strict type-aware equality is approval-required. |
| `1 == "1"` | False, not an error. | Equality result/type policy needs approval. |
| `1 / 0` | Runtime division-by-zero error. | Keep runtime checks regardless of analysis. |
| `if value:` | Uses runtime truthiness. | Truthiness proposal is not yet approved. |

Do not silently choose numeric promotion, integer width/overflow, Boolean
arithmetic, integer powers, mixed-type ordering, equality, truthiness,
conversion, or integer/float division and remainder behavior from this
architecture. Keep these out of the default `TypeChecker` until approval.

## Diagnostics and error classes

Represent diagnostics as data; formatting belongs to the CLI or IDE adapter.
At minimum include a category, primary source location, clear message, and
zero or more related locations. Preserve the parser's existing
`line:column` reporting convention when adding presentation, but do not
conflate parser exception strings with semantic diagnostic storage.

| Error class | Created by | Example |
| --- | --- | --- |
| Lexical error | Tokenizer | Unexpected character or malformed literal at a line/column. |
| Syntax error | Parser | Missing expression, inconsistent indentation, or `return` outside function under current parser behavior. |
| Semantic error | Analyzer | A name is definitely undefined under the selected binding policy; duplicate declaration where redeclaration is forbidden; known function arity mismatch; invalid return context not already rejected by parser. |
| Runtime error | VM | A dynamic name is absent at execution; a runtime callee is not callable; bad dynamic arity; invalid runtime operand types; zero divisor or resource limit. |

The same underlying issue can move phase only through an explicit integration
decision. For example, undefined names currently fail at runtime; making
`check` reject them is an intentional behavior and timing change. Preserve
source identity through the pipeline so runtime failures continue to point at
the originating expression/instruction.

Example diagnostic presentation (illustrative; exact wording and whether
these are errors require policy approval):

```text
example.rol:4:11: semantic error[undefined-name]: undefined name 'missing'
    print(missing)
          ^^^^^^^
```

```text
example.rol:5:1: semantic error[argument-count]: 'combine' expects 2 arguments, got 1
    combine(3)
    ^^^^^^^
example.rol:1:1: note: function declared here
    def combine(left, right):
    ^^^
```

```text
example.rol:3:12: semantic error[duplicate-declaration]: parameter 'value' is already declared
    def f(value, value):
               ^^^^^
example.rol:3:8: note: first parameter declared here
```

Diagnostics should be deterministic, avoid cascades after an unresolved name
by propagating `Error`/`Dynamic` appropriately, and never suppress an
independent error merely because another one occurred.

## Compiler integration

### Existing AST and frontend

The engine consumes `const Node&` and does not edit the AST. It should use the
current node kinds and child conventions initially, with a small internal
adapter if that makes traversal clearer. Do not extend `Node`, change parsing,
or add stable IDs in this documentation milestone.

Before persistent/cross-pass analysis metadata is needed, extend the frontend
deliberately with stable `NodeId` and preferably source spans (including end
positions). Keep those additions language-neutral. New grammar such as
annotations, `global`, `break`, or `continue` requires its own approved
language change and parser tests.

### Bytecode backend

The future pipeline may call `analyze(tree)` after parsing and before
`compile(tree)`. This is not active in this milestone. The semantic engine
must not include `backend.hpp`, inspect bytecode, rely on `Op`, or call VM
helpers. The bytecode compiler may later consume `AnalysisResult` by const
reference for resolved bindings or guards, but the existing dynamic code path
and runtime validation remain intact unless a separately approved change says
otherwise.

Introducing analysis into `check` or `run` changes visible behavior: `check`
would no longer be syntax-only, and some failures could move from execution
to compilation. Gate such activation on decisions about undefined names,
forward references, scope, and error phases. Keep compilation and execution
separate from diagnostic formatting.

### Future IR and native compilation

The result should express language concepts, not target details: symbol IDs,
language `Type` identities, resolved calls, operator semantics, source spans,
and runtime-check requirements. A future IR can lower those concepts to
explicit conversions, checks, and typed operations. A native backend can then
choose machine representations and runtime helpers while preserving Rolang
semantics.

Neither IR nor native generation is part of this design's implementation
scope. Never use C++ `std::variant` layout, pointer identity, host ABI, or
backend opcode values as a Rolang ABI or semantic identity.

## Control-flow analysis

Track only context required by adopted syntax and rules:

- Whether traversal is inside a function, for return placement.
- Whether traversal is inside a loop, if/when loop-control syntax is approved.
- The current function signature, if result types are introduced.
- Reachability and “returns on all paths” only if a specified rule needs them.

The current AST has `If`, `While`, and `Return`, but no `Break` or `Continue`.
The proposal to add loop-control statements is not a current grammar feature.
Likewise, the parser currently guarantees return placement, and functions
implicitly return `None` at fallthrough. Do not reject partial returns or
empty return paths unless a future declared-return contract requires it.

Definite assignment/unbound-local analysis depends on the decision about
function-wide local classification and global access. It cannot be derived
from the fact that the current backend has local and global maps. Until that
decision is approved, avoid a static unbound-local guarantee.

## Testing strategy

Tests should be organized around a pure analysis API: construct source and
parse it, analyze the AST, then assert diagnostic category, primary location,
and related location when applicable. Keep frontend syntax tests and VM
runtime tests separate so an error's phase is not accidentally asserted
twice. Add CLI integration tests only when the compiler explicitly enables
the semantic pass.

Recommended accepted-rule tests:

| Area | Focused cases |
| --- | --- |
| Undefined variables | Unconditionally used missing name; a declared name resolves; verify location. Keep short-circuit/unreachable cases gated on analysis policy. |
| Duplicate declarations | Duplicate parameters (already parser-tested); future duplicate declarations only after variable/function namespace and redeclaration rules are approved. Reassignment must not be called a duplicate by default. |
| Nested scopes | Resolve local parameter and module name; verify no accidental enclosing-function capture only if no-closure semantics are adopted. Do not assume block scopes. |
| Function calls | Known exact-arity call; wrong arity for known user function; unknown/dynamic callee remains a runtime concern; variadic `print` where builtin shadowing permits. |
| Parameters | Unique parameter symbols and correct owner; count mismatch at known call. Do not assert value-type mismatch in dynamic mode. |
| Returns | Return inside a function; parser remains responsible for its current outside-function syntax error; typed return compatibility only after annotations/result types exist. |
| Invalid operators | Known combinations from each approved operator table; dynamic operands carry a runtime requirement instead of a false static guarantee. |
| Type compatibility | Dynamic assignment/argument/return acceptance; known incompatibility only in an explicitly enabled typed mode. |
| Shadowing | Local/parameter shadowing and builtin shadowing only in accordance with the approved lookup rules. |
| Global access | Module binding resolution and function reads only for approved global/local rules; writes and `global` declarations after specification. |
| Control flow | Return placement; loop-control placement only after grammar and rules are adopted; do not require all-path returns under current implicit-`None` behavior. |

Useful negative assertions include: no duplicate diagnostic for one primary
error, no static type rejection of arbitrary dynamic parameter values, no
enclosing-local lookup when closures are disabled, and no policy-dependent
diagnostic in a mode where that decision is intentionally deferred.

The existing `make test` suite remains the regression baseline for this
documentation-only milestone. It verifies current syntax/runtime behavior,
not an unimplemented semantic engine.

## Trade-offs

- **Separate semantic pass vs checks embedded in parser/backend:** a pass
  keeps grammar, name/type rules, and code generation distinct and supports
  future tools. It introduces a new phase and can move observable failures
  earlier; activation therefore needs compatibility decisions.
- **Symbol IDs and side tables vs AST mutation:** side tables avoid mixing
  derived facts with syntax. Stable IDs require frontend work; node-address
  keys are suitable only for one immutable AST lifetime.
- **Dynamic type vs static rejection:** a conservative `Dynamic` type supports
  compile-time checks when facts are known without pretending all values are
  statically typed. It leaves runtime checks necessary and may produce fewer
  early errors.
- **Lexical parent chain vs runtime maps:** lexical lookup is a clean
  analysis model, but must be reconciled with module execution, function
  locals, dynamic globals, builtin shadowing, and the unresolved closure
  decision.
- **Declaration prepass vs source-order registration:** pre-registration
  enables forward references and recursion analysis, but may differ from
  source execution order. Make visibility explicit before choosing.
- **Narrow C++17 API vs general framework:** a few concrete services and
  enums are sufficient for the current AST. Generic plugin registries,
  configurable rule engines, and external dependencies are not justified.

## Prioritized implementation roadmap

This roadmap is for a later implementation, after the listed semantic gates
are resolved. No step below is performed by this document.

1. **Approve semantic and phase boundaries.** Decide scope/lookup, declaration
   visibility and redeclaration, undefined-name phase, global writes,
   closures, dynamic-vs-checked mode, parameter/return rules, and which
   diagnostics block `check`, `compile`, and `run`.
2. **Specify analysis data contracts.** Define `SourceSpan`, diagnostic
   categories, stable symbol/type identities, `AnalysisResult`, and whether
   the AST needs `NodeId`. Keep these independent of backend types.
3. **Implement scope and symbol registration.** Add module/function scopes,
   parameters, function signatures, approved builtin visibility, nested
   lookup, and related-location duplicate diagnostics. Test shadowing and
   global access only against approved rules.
4. **Implement name and function resolution.** Resolve uses and known call
   targets; validate known arity; preserve dynamic runtime checks for unknown
   callees and builtins. Test forward-reference policy and error locations.
5. **Add dynamic expression/operator analysis.** Introduce `Dynamic` and
   `Error`, infer only facts justified by syntax, and check operators according
   to an approved operation table. Keep unresolved number, equality,
   conversion, and truthiness policies pending until approved.
6. **Add control-flow and typed-return checks.** Implement only adopted
   return/loop/definite-assignment rules. Avoid all-path-return requirements
   unless specified.
7. **Integrate behind an explicit compiler boundary.** Add analysis to
   `check`/`compile`/`run` only after deciding phase behavior and compatibility.
   Keep the bytecode backend unaware of semantic implementation details; pass
   optional result metadata only where justified.
8. **Extend semantic tests and documentation.** Add focused unit and CLI
   regressions; retain VM checks; update language behavior documents from
   approved rules and observed tests.
9. **Only later: typed IR/native consumers.** Reuse language-level identities
   and diagnostics; define target lowering separately. Do not implement IR or
   native code generation as part of the semantic engine foundation.

## Decisions requiring approval

This architecture intentionally does not resolve the following:

1. **Scope model:** which constructs create lexical scopes; block shadowing;
   nested-function lookup; closures versus no closures.
2. **Binding and global rules:** whole-function locals, unbound-local behavior,
   global reads/writes, explicit `global` declarations, and assignment to an
   existing name.
3. **Declaration policy:** duplicate function/variable/type declarations,
   variable redeclaration versus reassignment, namespaces, builtin shadowing,
   and same-scope conflicts.
4. **Visibility and ordering:** source-order declarations versus block
   predeclaration/hoisting; recursion and forward references; when a function
   becomes callable.
5. **Undefined names and phase:** compile-time semantic error or runtime
   lookup failure, including names in short-circuited or otherwise unreachable
   expressions; whether `check` changes from syntax-only.
6. **Dynamic and optional static modes:** whether V1 stays fully dynamic;
   future annotation syntax, checker opt-in, gradual `Dynamic` rules, and
   which static errors block compilation.
7. **Function signatures:** arity rules, parameter/result annotations,
   variadic builtins, function values/indirect calls, and when mismatch is
   static versus runtime. Exact positional arity is proposed, not newly
   approved here.
8. **Return/control-flow rules:** implicit `None`, whether all paths must
   return, typed return compatibility, and future `break`/`continue` context.
9. **Compatibility and conversion:** assignment, argument, return, and
   operator compatibility; implicit promotions; explicit conversion functions.
10. **Numeric semantics:** integer width/range/overflow, `Bool` arithmetic,
    mixed numeric operations and ordering, powers, `/`, `//`, and `%`.
11. **Equality and operators:** strict or coercive equality, string ordering,
    operator operand/result tables, and error behavior.
12. **Truthiness:** approved falsey values and future compound-value rules.
13. **Diagnostic contract:** stable categories/messages, warning/error levels,
    ordering, recovery, CLI formatting, and whether semantic errors prevent
    bytecode generation.
14. **Analysis metadata lifetime:** whether to add stable AST node IDs and
    source-span ends before any consumer needs cross-pass or persistent data.
15. **Future type universe:** generic and nominal type identity, compound
    value semantics, and the point at which those features enter the language.
