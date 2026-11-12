# Rolang Phase 2A: Runtime Value and Type Architecture

**Status: architecture record; Phase 2B runtime implementation is complete.**
The initial implementation analysis below records the Phase 2A baseline, and
the recommendations remain proposals rather than normative language approval.
Current behavior is documented in [LANGUAGE_SPEC.md](LANGUAGE_SPEC.md), and
implementation choices still awaiting language approval are tracked in
[SEMANTICS_DECISIONS.md](SEMANTICS_DECISIONS.md).

## 1. Current implementation analysis

The Makefile builds the C++17 implementation in `src/frontend.cpp`,
`src/backend.cpp`, and `src/main.cpp`. The C sources and `rolange.py` are
retained prototypes; their types and behavior do not establish active Rolang
semantics.

### 1.1 Literal path and runtime representation

The lexer emits a `Token` with `text`, a parsed `double number`, and a
1-based line and column. It uses `strtod`, rejects malformed and non-finite
literal results, and retains the original spelling in `Token::text`. It does
not check `errno`, so a very small literal that underflows through `strtod`
may be accepted as a subnormal or zero. The parser copies a numeric token's
`double` into `Node::number`; numeric source spelling is not retained on the
AST node. Integer-looking, decimal, and exponent literals therefore all
become the same runtime `double`.

The active runtime representation in `src/backend.hpp` is:

```cpp
struct Function { std::shared_ptr<Code> code; };
struct BuiltinPrint {};
using Value = std::variant<
    std::monostate, double, bool, std::string,
    std::shared_ptr<Function>, BuiltinPrint>;
```

`std::monostate` is the Rolang `None` value. User functions and built-in
`print` are different runtime alternatives. The `Code` object owns function
parameters and instructions. A function value owns a shared pointer to its
code; code can own nested function code through `Instruction::function_code`.
No environment is captured by a function, and nested functions do not capture
enclosing locals.

`Instruction` also stores a complete `Value` field, although only constant
instructions need a meaningful constant. Constants are copied from the AST
to bytecode and copied onto the VM operand stack. Values are then moved or
copied through stores, argument vectors, and local maps.

### 1.2 Operations that create, inspect, compare, or display values

The active value paths are concentrated in `src/backend.cpp`:

| Operation | Current implementation behavior |
| --- | --- |
| Literal construction | `Generator::expression` emits `double`, `bool`, `std::string`, or `std::monostate` constants. |
| Unary operators | `number()` accepts `double` and `bool`; unary `+`/`-` produce `double`. `not` calls `truthy()` and produces `bool`. |
| Arithmetic | `number()` converts a Boolean to `1.0` or `0.0`. Arithmetic is `double`; `+` additionally concatenates two strings. Results pass through `finite_result()`. |
| Division | `/` checks a zero divisor and returns a finite `double`. `//` floors a `double` quotient and returns a `double`. |
| Modulo and power | `%` uses `std::fmod`, checks zero, and returns a finite `double`; `**` uses `std::pow` and rejects a non-finite result. |
| Equality | `equal_values()` treats `double` and `bool` as one numeric comparison class, so `True == 1` is true. Other different alternatives compare unequal; same alternatives compare by value, except functions compare shared-pointer identity. |
| Ordering | `<`, `<=`, `>`, and `>=` call `number()` for both operands and compare the resulting doubles. Booleans therefore participate as 0/1. |
| Conditions and Boolean operators | `truthy()` makes `None`, false, numeric zero (including signed zero), and empty strings false; other current alternatives are true. `and`/`or` are short-circuit bytecode and return Boolean values. |
| Printing and bytecode listing | `display()` prints `None`, `True`/`False`, doubles with `setprecision(15)`, raw string contents, `<built-in print>`, or `<function>`. `print` joins argument displays with spaces and adds a newline. Display is not a language conversion. |
| Function values and calls | `MakeFunction` allocates `Function` with `make_shared`; calls dispatch `BuiltinPrint` separately or invoke user code. The call checks exact positional arity and creates a fresh local map. |
| Errors and locations | Operations throw `std::runtime_error`; the VM catches standard exceptions at the instruction boundary and prefixes the instruction line and column. Runtime error categories are currently encoded in text, not structured data. |

`Value::index()` is used to distinguish alternatives in equality. That is
valid as an implementation detail for this closed variant, but it couples
language behavior to alternative order and should not become a type identity,
serialized tag, or backend ABI.

There is a useful diagnostic-location detail for later integration: operators
and name loads carry node columns through bytecode emission, while several
literal `Constant` emission calls supply a line but use the instruction's
default column of 1. A migration should preserve and test existing source
locations, and should not assume every current runtime error has a precise
operator or literal column.

### 1.3 Current features and boundaries

Strings own their bytes in `std::string`; concatenation allocates a new
string. Functions are identity-bearing shared objects whose bytecode is
shared. There are no arrays, objects, structures, user-defined type values,
modules, native functions, or closures in the active runtime. The runtime
does not use a garbage collector.

The Phase 1 semantic analyzer is a separate `rolang::semantics` subsystem.
Its `TypeKind` currently describes `Dynamic`, `Error`, `None`, `Bool`,
`Number`, `String`, and `Function`; it does not represent actual VM values or
distinguish Int from Float. `Dynamic` and `Error` are analysis facts, not
runtime alternatives. The analyzer can continue to describe source-level
facts without depending on the runtime `Value` representation.

## 2. Proposed architecture

Use a small runtime value layer independent of the AST and bytecode layout:

```text
frontend tokens / AST
          |
          +--> semantic analysis (language-level facts and diagnostics)
          |
          v
bytecode compiler ---> bytecode constants / operations
                              |
                              v
                    runtime Value + ValueOperations
                              |
                              v
                             VM
```

The frontend owns source spelling and source locations. Semantic analysis
owns static facts and must not infer them from C++ variant alternatives. The
runtime owns actual tagged values, checked access, dynamic operation
semantics, and runtime errors. The bytecode compiler selects instructions and
preserves source locations; it should not duplicate arithmetic or equality
rules. Future IR and native backends should consume language-level operation
semantics, not expose `std::variant` layout.

Recommended initial runtime alternatives are a closed C++17 `std::variant`
for `Null`, `Int64`, `Float64`, `Boolean`, `String`, and `Function`. Wrap
that variant in a `Value` class rather than exposing it throughout the VM.
The wrapper provides named construction, type identification, and safe
inspection. Operators live in `ValueOperations` and `NumericOperations`;
they validate tags explicitly and return a value or throw a structured
`RuntimeError`. This remains dynamic execution: an Int64/Float64 tag is a
runtime fact, not a promise that a variable has a permanently fixed type.

Do not add future alternatives as placeholders now. When arrays, objects,
structures, or other values are approved, add explicit runtime alternatives
or a deliberate heap-object handle and update exhaustive visitors and tests.
Keep `TypeId` / descriptors for user-defined types separate from `ValueType`.

## 3. Type representation

### 3.1 Runtime type tags

The proposed primitive tag set is:

| `ValueType` | Payload | Identity |
| --- | --- | --- |
| `Null` | No payload | One singleton value. |
| `Boolean` | `bool` | `True` and `False`. |
| `Int64` | `std::int64_t` | Exact signed 64-bit value, if approved. |
| `Float64` | `double` | Binary64 floating-point value, if approved. |
| `String` | Owned `std::string` | Immutable Rolang string value. |
| `Function` | Handle to `FunctionValue` | Callable object identity, not code equality. |

Use named enum values or named alternatives to identify a runtime type. Do
not persist enum ordinals or variant indexes as language type identities.
Validate Float64 values at construction and operation boundaries if the
finite-only proposal is approved; a runtime tag alone must not be used to
assume a value is finite.

Future compound type descriptors should have stable identity independent of
their display name. A useful future model is a language-level `TypeId` plus
descriptors for nominal structures and parameterized types, while `ValueType`
continues to identify broad runtime storage categories (for example,
`Array` or `Object`). The type descriptor answers what a value means;
the value tag answers how the runtime can safely access its payload.

### 3.2 Construction, access, and operations

Construction should use named factories (`Value::int64`, `Value::float64`,
`Value::boolean`, `Value::string`, `Value::null`, and `Value::function`).
Avoid ambiguous implicit C++ constructors: `bool`, integer literals, and
floating literals should not accidentally select a different Rolang type.
Factories may enforce internal invariants, such as a non-null function
handle, and should not silently substitute a default value on failure.

Access should be checked. `get_if<T>()`-style non-throwing inspection is
appropriate for dispatch; a required accessor should either return an
explicit optional/reference result or throw a typed runtime error with the
operation's source location. Avoid unchecked `std::get<T>` in user-visible
execution paths.

Every operator should have an explicit accepted-operand table. It must either
produce the specified `Value` or report a `RuntimeError` such as
`TypeMismatch`, `DivisionByZero`, `IntegerOverflow`, or `NonFiniteResult`.
Errors must never become `None`, zero, or another success-shaped value.

### 3.3 Equality and truthiness

**Observed now:** equality compares double and Boolean values after Boolean
coercion to double; all other different value alternatives are unequal;
same-type strings, booleans, doubles, and null compare by value; functions
compare by shared identity. Truthiness is false for null, false, zero
(including negative zero), and empty strings; functions and the built-in
function marker are true.

**Recommended, not approved:** define equality explicitly by language type:
same-type primitive values compare by value, different types compare unequal,
and functions compare by identity. This changes `True == 1` from true to
false. If cross-numeric equality is desired instead, specify it separately
from arithmetic promotion and avoid accidental equality from C++ coercions.

**Recommended, not approved:** retain current scalar truthiness for null,
booleans, numeric zero, empty strings, and functions. Future arrays and
objects need their own approved emptiness/truthiness rule; do not inherit
host-container behavior by accident.

Equality and truthiness should be named operations (`ValueOperations::equal`
and `ValueOperations::truthy`). `and`, `or`, `not`, and conditional bytecode
should call or lower through the same truthiness definition. `and`/`or`
currently return booleans; this is existing behavior and should remain
covered by tests.

### 3.4 String display versus representation

Keep three concepts separate:

1. **Display conversion for `print`:** the current behavior (raw string
   contents, current numeric formatting, etc.) is user-visible.
2. **Debug representation:** an explicitly diagnostic form, such as
   `String("...")`, `Int64(3)`, and `Function(<identity>)`; it need not be
   valid Rolang source.
3. **Language conversion:** a future explicit operation that returns a
   Rolang `String`. No such conversion builtin currently exists.

Do not use `display()` as an equality key, conversion operation, or
serialization format. The existing numeric display uses 15 significant
digits and is not specified as round-trip safe. Any format change requires
compatibility review and tests.

## 4. Numeric semantics

The table separates observed behavior, an implementable proposal, and
decisions still requiring approval. Proposed semantics here are not active
language rules.

| Topic | A. Existing implementation | B. Proposed direction | C. Unresolved approval |
| --- | --- | --- | --- |
| Numeric literal kind | All valid numeric spellings parse to finite `double`; the AST loses original spelling. | An integer spelling without decimal point/exponent becomes Int64; decimal or exponent spelling becomes Float64. Preserve spelling until classification and range checks finish. | Approve the distinction, accepted grammar, radix/underscore policy, signed 64-bit range, and handling of `-9223372036854775808`. |
| Negative values | `-` is a unary operation; the parser places exponentiation tighter than unary sign (`-2 ** 2` is `-(2 ** 2)`). Unary numeric results are doubles. | Preserve parse precedence. Int64 negation remains Int64 and detects `INT64_MIN` overflow; Float64 negation preserves signed zero. | Approve integer minimum literal treatment and whether power/unary precedence remains fixed. |
| Int64 addition, subtraction, multiplication | No Int64 exists; operations use `double`. | Use checked arithmetic; overflow raises `IntegerOverflow`, never C++ signed overflow or wrapping. | Approve signed 64-bit width and checked versus wrapping behavior. |
| Float64 arithmetic | `double` operations; non-finite results rejected by `finite_result`. | Require IEEE-754 binary64; preserve finite-result check. Arithmetic rounding should be specified independently of compiler/backend. | Approve binary64 platform requirement, rounding environment, and exceptional-value policy. |
| Mixed Int64/Float64 | Not applicable; all numeric values are `double`. | A practical proposal is to promote Int64 to Float64 for arithmetic and return Float64, while documenting possible precision loss. | Approve promotion, precision loss, and whether mixed comparison uses exact integer-vs-float comparison rather than rounded promotion. |
| Boolean arithmetic | `number()` accepts Boolean as `1.0`/`0.0`; arithmetic and ordering accept them. | Prefer rejecting Boolean for numeric arithmetic and ordering, while retaining truthiness and Boolean equality as separate operations. | Approval required; rejecting it breaks current valid programs. |
| `/` | Accepts double/Boolean; checks zero; returns finite double. | Return Float64 for all numeric operands; check `+0` and `-0` divisor before division. | Approve accepted operand pairs, including Booleans and mixed values. |
| `//` | Floors a double quotient; zero is an error; result is double. | Int64/Int64 returns an Int64 floor quotient; any Float64 operand returns Float64 after floor. | Approve floor versus truncation, mixed result type, and edge cases such as `INT64_MIN // -1`. |
| `%` | Uses `std::fmod`; rejects zero; remainder follows dividend sign and may be fractional. | Int64/Int64 can use the quotient/remainder identity with divisor-sign remainder; Float64 `%` can retain current `fmod` behavior. | Approval required for integer sign rule and mixed numeric behavior. |
| `**` | Uses `std::pow(double, double)`; non-finite results are errors. | Consider checked Int64 results for nonnegative Int64 exponents; Float64 result for negative exponents or Float64 operands; reject invalid-domain/non-finite results. | Approve exponent result types, integer overflow policy, negative exponents, and real-domain errors. |
| Overflow | No integer overflow distinction. Float result becoming infinity/NaN is rejected. | Raise a specific runtime error on checked Int64 overflow and non-finite Float64 results. | Approve overflow behavior; exact error categories and display belong in the runtime diagnostic contract. |
| Underflow | Finite subnormal and signed-zero results pass `finite_result`; no explicit underflow error is raised. | Prefer IEEE gradual underflow; accept finite subnormal results and underflow to signed zero rather than treating underflow as overflow. | Approve whether underflow is observable, trapped, or allowed; preserve signed zero if observable. |
| Division by zero | `/` and `//` reject a zero `double`; `%` rejects zero. `+0.0` and `-0.0` both compare equal to zero. | Preserve explicit errors before performing division/remainder. | Approve error categories and exact messaging, not the broad prohibition. |
| NaN and infinity | Lexer rejects non-finite numeric literals. Arithmetic rejects non-finite results. They are not ordinary values in the current language. | Keep non-finite values out of ordinary source/runtime values in the first Int64/Float64 design. | Decide future native/foreign ingress, NaN equality/ordering, infinities, and whether constants can ever contain them. |
| Numeric comparison | Converts bools to doubles and compares doubles. | Compare Int64/Int64 exactly; Float64/Float64 per approved IEEE policy; use an exact mixed comparison if mixed ordering is permitted. | Approve Boolean ordering, cross-numeric ordering, NaN policy, and whether comparison may lose precision. |
| Numeric equality | Bool and double share a numeric equality path; `True == 1` is true. | Recommend strict type-aware equality: Int64 differs from Float64, and Boolean differs from both. | Approval required; the current Boolean/double behavior is a compatibility constraint. |
| Conversion | Boolean-to-double coercion happens inside numeric operators. Assignment, calls, and returns do not convert. `print` display is not a conversion. | No implicit conversion for assignment, parameters, returns, or equality. Keep only explicitly approved operator promotion. Add explicit conversion functions only in a separate language proposal. | Approve any Bool/number or Int64/Float64 promotion; decide conversion function names, accepted inputs, loss, range, and errors before adding them. |

The proposed signed-64-bit model should parse integer source text directly
with range checks; first routing it through `strtod` would lose precision.
The lexer currently preserves token spelling but the AST does not. A future
integer implementation therefore needs a deliberate token/AST change (or a
numeric literal kind plus exact integer payload), with separate tests for
minimum/maximum values, overflow, decimal/exponent classification, and
malformed input.

For mixed comparisons, converting a large Int64 to double before comparing
can make distinct mathematical values compare equal. If mixed numeric
ordering is approved, implement and test an exact comparison algorithm or
explicitly accept the precision consequences. Do not reuse the arithmetic
promotion helper as the equality or comparison policy by default.

## 5. Memory management

### 5.1 Alternatives

| Strategy | Advantages | Costs and suitability |
| --- | --- | --- |
| `std::variant` plus RAII | Standard C++17; exhaustively represents a closed set; constructors, moves, and destruction correctly manage strings and handles; no dependency. | Inline alternatives affect `Value` size; adding an alternative requires dispatch updates. This is the recommended immediate-value representation. |
| Manual tagged union | Can tune size/layout and control payload representation. | Requires correct manual construction, destruction, copy/move, and exception safety for every non-trivial payload. Not justified without measured benefit. |
| `unique_ptr` heap box for every value | Uniform handles and small slots. | Adds allocation and indirection to every scalar; obscures value semantics; unnecessary for the current small value set. |
| `shared_ptr` for every value | Easy shared lifetime and identity. | Reference-count overhead for primitives and does not collect cycles. Not recommended as the universal representation. |
| Reference-counted handles for heap objects | Natural identity and aliasing for functions and future mutable compounds. | Cycles leak unless prevented or collected; synchronization/control-block costs. Appropriate selectively, not for every value. |
| Tracing garbage collection | Reclaims cyclic object graphs; can support closure/environment cycles. | Adds roots, safepoints, allocation discipline, collection latency, and substantial correctness work. No current Rolang feature requires it. |

### 5.2 Recommendation for the initial runtime

- Store null, booleans, Int64, Float64, and `std::string` directly in the
  `Value` variant. Use ordinary C++ value semantics and RAII. String copies
  are straightforward and safe; measure before adding interning, copy-on-write,
  or shared strings.
- Use a handle for function identity and shared immutable code. Preserve the
  current useful property that multiple references to one function value
  compare by identity. Ensure code is fully built before publishing a
  const/shared function-code view.
- Keep each VM activation's operand stack and local bindings owned by the
  activation. The current VM uses a `std::vector<Value>` stack and a local
  `unordered_map`; future slot arrays can be considered separately from the
  value semantics.
- Keep constants as values initially. `Instruction`'s embedded `Value` may
  be larger than necessary for non-constant instructions; consider a constant
  pool only as a measured bytecode-layout optimization, not as a prerequisite
  to the type redesign.
- Do not add a tracing collector now. Current values do not form arbitrary
  mutable object graphs, and functions do not capture environments. Selective
  `shared_ptr` handles are sufficient for current function/code lifetime.

If closures are approved later, model captured mutable bindings as shared
cells or an explicit environment, not copies of an entire caller stack frame.
An environment should outlive the defining activation while a closure refers
to its cells. A closure environment that strongly owns a function which in
turn owns that environment can form a `shared_ptr` cycle; similarly, future
mutable arrays/objects can form arbitrary cycles. Before permitting such
graphs, choose a policy: prohibit cycles, use weak edges for specific
backreferences, or introduce a tracing heap. A tracing collector becomes a
justified option when approved language features create reachable cycles that
reference counting cannot reclaim. Keep a future `HeapObject`/handle boundary
so changing allocation strategy does not require changing arithmetic,
equality, or bytecode semantics.

## 6. C++17 interface proposals

The following is header-level design material, not code to add in this
phase. It illustrates the separation among runtime values, operations, and
structured errors. It intentionally does not reuse the semantic analyzer's
`Type` or depend on `backend.hpp` opcodes.

```cpp
#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rolang::runtime {

struct RuntimeLocation {
    std::string source_name;
    int line = 1;
    int column = 1;
};

enum class RuntimeErrorCode {
    TypeMismatch,
    DivisionByZero,
    IntegerOverflow,
    NonFiniteResult,
    InvalidNumericDomain,
    UndefinedName,
    NotCallable,
    ArgumentCount,
    ExecutionLimit,
    InternalRuntime
};

class RuntimeError final : public std::runtime_error {
public:
    RuntimeError(RuntimeErrorCode code, RuntimeLocation location, std::string message)
        : std::runtime_error(std::move(message)),
          code_(code), location_(std::move(location)) {}

    RuntimeErrorCode code() const noexcept { return code_; }
    const RuntimeLocation& location() const noexcept { return location_; }

private:
    RuntimeErrorCode code_;
    RuntimeLocation location_;
};

struct FunctionCode;
struct Environment;
struct FunctionValue;
using FunctionHandle = std::shared_ptr<const FunctionValue>;

enum class ValueType { Null, Boolean, Int64, Float64, String, Function };

class Value {
public:
    static Value null();
    static Value boolean(bool value);
    static Value int64(std::int64_t value);
    static Value float64(double value, const RuntimeLocation& at);
    static Value string(std::string value);
    static Value function(FunctionHandle value);

    ValueType type() const noexcept;

    const bool* get_boolean() const noexcept;
    const std::int64_t* get_int64() const noexcept;
    const double* get_float64() const noexcept;
    const std::string* get_string() const noexcept;
    const FunctionHandle* get_function() const noexcept;

private:
    using Storage = std::variant<
        std::monostate, bool, std::int64_t, double, std::string, FunctionHandle>;

    explicit Value(Storage storage) : storage_(std::move(storage)) {}
    Storage storage_;

    friend class ValueOperations;
    friend class NumericOperations;
};

enum class FunctionKind { User, Native };
using NativeFunctionId = std::uint32_t;

struct FunctionValue {
    FunctionKind kind = FunctionKind::User;
    std::shared_ptr<const FunctionCode> code;
    std::shared_ptr<Environment> closure; // null until closures are approved
    NativeFunctionId native_id = 0;       // dispatched by a runtime registry
};

class ValueOperations {
public:
    static ValueType type_of(const Value& value) noexcept;
    static bool truthy(const Value& value);
    static bool equal(const Value& left, const Value& right,
                      const RuntimeLocation& at);
    static std::string to_display_string(const Value& value,
                                         const RuntimeLocation& at);
    static std::string to_debug_string(const Value& value);
};

class NumericOperations {
public:
    static Value unary_plus(const Value& operand, const RuntimeLocation& at);
    static Value unary_minus(const Value& operand, const RuntimeLocation& at);
    static Value add(const Value& left, const Value& right, const RuntimeLocation& at);
    static Value subtract(const Value& left, const Value& right, const RuntimeLocation& at);
    static Value multiply(const Value& left, const Value& right, const RuntimeLocation& at);
    static Value divide(const Value& left, const Value& right, const RuntimeLocation& at);
    static Value floor_divide(const Value& left, const Value& right, const RuntimeLocation& at);
    static Value modulo(const Value& left, const Value& right, const RuntimeLocation& at);
    static Value power(const Value& left, const Value& right, const RuntimeLocation& at);
    static int compare_numeric(const Value& left, const Value& right,
                               const RuntimeLocation& at);
};

} // namespace rolang::runtime
```

Factories and accessors above show intent; production declarations should
document whether getters return pointers, references, or optionals. A
production `Value::type()` can use named `std::get_if` checks or a
`std::visit` overload set. It must not depend on the variant index. `float64`
should enforce the approved finite-value policy; internal construction from
validated arithmetic may use a private checked helper. Invalid handles and
malformed internal function values are internal/runtime errors, not null
fallbacks.

`FunctionValue` is a stable callable identity. User function code is shared
and immutable after construction. A future native function is represented by
an ID into a runtime-owned registry rather than by a raw host ABI function
pointer in the language `Value`; the VM boundary controls arguments, errors,
and any future foreign-call convention. Closures remain absent until their
scope and lifetime semantics are approved. If introduced, closure cells and
environment ownership require explicit cycle handling.

### 6.1 Alternatives considered

- **Keep the raw current alias and add more alternatives:** smallest diff, but
  makes every backend consumer aware of variant layout and permits accidental
  implicit construction. It also leaves type identity tied to `index()`.
- **Use an enum plus union:** offers direct tag control but adds manual
  lifetime and exception-safety code for `std::string` and future handles.
- **Use a wrapper around `std::variant` (recommended):** keeps C++17 RAII,
  makes construction/access and errors explicit, allows exhaustive internal
  dispatch, and avoids exposing storage details to compiler clients. It does
  not claim that the variant layout is stable across compilers or a native ABI.
- **Use universal heap boxes or a GC now:** adds allocation/runtime
  complexity without current feature requirements; defer until profiling or
  approved cyclic features justify it.

## 7. Runtime and compiler integration

| Area | Design impact and boundary |
| --- | --- |
| Frontend and AST evaluation | No direct AST evaluator exists; the AST is compiled to bytecode. To distinguish Int64 and Float64, retain numeric spelling/kind and exact integer payload in tokens/AST before `strtod` loses information. Keep AST nodes language-level, not `Value` objects. |
| Bytecode generation | `Constant` instructions need an Int64 or Float64 payload according to literal classification. Operator instructions can remain generic initially; compiler must preserve source location. A later constant pool is an independent optimization. |
| VM execution | VM stacks/maps become `runtime::Value`. Loads, stores, stack pop/push, and returns preserve tagged values. Runtime operations centralize checks in `ValueOperations` / `NumericOperations`; dynamic operations retain runtime validation. |
| Function calls | Existing positional argument evaluation/order and arity behavior should remain. A `FunctionValue` unifies user and native callability; dispatch chooses user code or a native ID. The VM owns call frames, locals, depth checks, and error source context. |
| Built-ins | Replace the special `BuiltinPrint` representation only in a later migration with a native-function descriptor/registry entry. Preserve variadic `print`, output spacing/newline, shadowing behavior, and identity/callability rules. |
| Printing | Route `print` through display conversion, not language conversion. Preserve current display output unless an explicit compatibility change is approved. Keep debug and serialization formatting separate. |
| Arithmetic and comparisons | Generic bytecode operators dispatch to named runtime helpers. Keep VM runtime checks even when semantic analysis can prove a case. Do not duplicate numeric promotion, equality, or truthiness in the compiler. |
| Semantic analysis | Keep `rolang::semantics::Type` distinct. Initially map known literal facts to runtime concepts only at an explicit boundary; its current `Number` category is not enough to distinguish Int64 from Float64. Static `Dynamic` / `Error` are not runtime tags. |
| Future IR | Represent language operations, exact type identities, and explicit conversions/guards. Do not encode `std::variant` alternative numbers or C++ object layout in IR. |
| Future x86-64 / ARM64 | Lower approved Int64 and Float64 semantics to target registers/instructions, calling runtime helpers for dynamic operations and errors. Keep overflow, mixed comparisons, NaN, division, and conversion behavior identical across targets. Never pass `Value`'s C++ in-memory layout across generated-code or foreign ABI boundaries. |

The semantic analyzer currently runs for `check`; `run` and `compile` retain
their current paths. Runtime migration must not silently make semantic
analysis authoritative for dynamic execution. A successful static pass is not
a proof that dynamic operations cannot fail.

## 8. Migration plan

Treat language decisions as a gate before changing number behavior. A staged
implementation can be reviewed and rolled back at phase boundaries:

1. **Approve semantics and pin current behavior.** Resolve the approval list
   below. Add/confirm golden runtime cases for existing output, comparison,
   arithmetic, limits, and error locations before editing runtime code.
2. **Add a runtime value API without changing behavior.** Introduce a private
   wrapper around the current alternatives (`Null`, Float64-as-current
   number, Boolean, String, Function, and native print descriptor). Route
   existing construction, type checks, equality, truthiness, and display
   through it. Keep compatibility adapters localized in the backend.
3. **Centralize and test operations.** Move current operator semantics into
   named helpers while preserving current behavior exactly, including
   Boolean numeric coercion and `True == 1`. Add direct C++ operation tests
   where a stable internal API is available. This phase is a rollback point
   before changing numeric meaning.
4. **Preserve numeric source form.** Extend token/AST numeric payloads to
   retain spelling and an explicit numeric kind; parse Int64 with exact
   range checks and Float64 as binary64. Keep the existing behavior behind
   the active policy until behavior changes are approved.
5. **Add Int64 values and bytecode constants.** Add a distinct Int64 runtime
   tag and bytecode literal payload. Update display and disassembly only
   after output compatibility expectations are decided. Keep `run` and
   `compile` using the same source-to-bytecode route.
6. **Implement approved numeric operations.** Add checked integer arithmetic,
   approved mixed numeric rules, division/modulo/power, exact or approved
   comparisons, finite Float64 validation, and structured runtime errors.
   Do not rely on signed overflow or silent conversions.
7. **Migrate functions and built-ins.** Represent user functions and
   `print` through a `FunctionValue`/native registry while preserving function
   identity, shadowing, evaluation order, arity, recursion limits, and
   output. Do not add closures as an incidental consequence.
8. **Extend future types separately.** Add arrays, objects, structures,
   modules, user-defined descriptors, native functions, or closures only
   with their own syntax, ownership, equality, truthiness, mutation, and
   error design. Choose a cycle policy before introducing cyclic graphs.
9. **Consider performance/GC only with evidence.** Benchmark `Value` size,
   string copying, instruction storage, and execution before adding boxing,
   constant pools, interning, or a tracing collector.

### Files expected to change in an implementation phase (not this document task)

- `src/frontend.hpp` and `src/frontend.cpp`: numeric kind/spelling, precise
  AST payload, locations, and literal diagnostics.
- `src/backend.hpp` and `src/backend.cpp`: `Value`, function/native
  representation, bytecode constants, runtime dispatch, operations, display,
  errors, and VM stack/call integration.
- `src/main.cpp`: only if structured runtime errors require improved CLI
  rendering; preserve command contracts.
- `src/semantic/semantic_types.hpp` and `src/semantic/type_checker.cpp`:
  only if a later semantic design needs separate static Int/Float facts.
  Keep the runtime independent; do not conflate static and dynamic types.
- `Makefile`: add any new runtime source/test units if the architecture is
  split into modules.
- `tests/test_cpp.py`, `tests/semantic_tests.cpp`, plus focused C++ runtime
  tests if introduced: assert current compatibility and newly approved rules.
- `docs/LANGUAGE_SPEC.md` and the decision register: update only after
  behavior is approved and implemented.

No compiler or runtime source file is changed by this Phase 2A design.

## 9. Testing strategy

Maintain two separate test goals: a compatibility suite for observed behavior
and a conformance suite for explicitly approved new semantics.

### Compatibility tests to preserve

- Existing calls, nested calls, recursion, argument order, exact arity, and
  built-in `print` variadic behavior.
- `print` shadowing and current behavior of function values.
- Existing numeric samples and behavior for Booleans as numeric operands,
  `True == 1`, string concatenation, mixed-type equality, ordering, floor
  division, `fmod` remainder sign, and powers.
- Zero divisors, non-finite results, malformed/non-finite literals, and
  instruction/call-depth limits.
- `None`, Boolean, zero, signed-zero, empty-string, and function truthiness.
- Exact displayed strings and bytecode listing output where they are
  currently tested or treated as user-visible.
- Source line and column for runtime failures; specifically add coverage for
  literal-origin columns if the frontend/backend location plumbing is
  improved.
- Run all existing C++ smoke/regression tests, not the separate Python
  Rolang prototype suite as evidence for C++ semantics.

### Tests required for approved Int64/Float64 semantics

- Integer/float literal classification (`4`, `4.0`, `4e0`), exact source
  spelling retention, signed 64-bit min/max, out-of-range values, and
  negative-minimum parsing.
- Checked `+`, `-`, `*`, unary negation, `//`, `%`, and integer exponent
  boundaries, including overflow and division edge cases.
- Float64 binary64 assumptions, signed zero, subnormal/underflow behavior,
  overflow to infinity, NaN-producing powers, division by either signed zero,
  and finite-result errors.
- Every approved mixed Int64/Float64 operation and result type; precision
  loss and exact mixed comparison edges near `2^53` and signed 64-bit bounds.
- Boolean arithmetic and comparison policy, type-aware equality across every
  primitive pair, function identity equality, and `!=`.
- Explicit conversion behavior only if such language functions are approved.
- Display, debug representation, and bytecode constant serialization as
  separate outputs.
- Cross-target conformance vectors for any future x86-64 and ARM64 backend.

For `Value` API tests, assert both the runtime tag and payload; for errors,
assert structured code and source location, not only a substring of
`what()`. Keep the VM's dynamic checks enabled when static analysis has facts.

## 10. Unresolved decisions requiring approval

No recommendation in this design silently resolves the existing approval
register. Before changing active behavior, approve at least:

1. **Integer domain:** Int64 or another range; checked overflow versus wrap;
   exact integer literal grammar and out-of-range diagnostics.
2. **Literal classification:** integer versus float spelling; exponent and
   decimal forms; treatment of `-9223372036854775808`.
3. **Boolean numeric behavior:** retain current `True=1` / `False=0` or make
   Boolean non-numeric. This affects arithmetic, unary operators, ordering,
   and mixed equality.
4. **Mixed numeric arithmetic and comparison:** promotion and result types,
   precision loss, and whether comparison must preserve exact mathematical
   ordering.
5. **Division:** `/` result type and behavior, integer and floating `//`,
   signed zero checks, and edge cases.
6. **Modulo:** integer remainder identity/sign and whether float `%` continues
   to use current dividend-sign `fmod`.
7. **Power:** integer result conditions, negative exponent behavior,
   overflow, and invalid Float64 real-domain cases.
8. **Float contract:** required IEEE-754 properties, rounding mode,
   finite-only values, NaN/infinity ingress, underflow, signed zero, and
   formatting guarantees.
9. **Equality:** strict cross-type inequality versus current Boolean/numeric
   equivalence; function identity; future compound identity or structural
   equality.
10. **Truthiness:** approve current scalar truthiness and future empty
    array/object behavior.
11. **Conversions:** whether explicit conversion built-ins exist and their
    names, precision, accepted inputs, range checks, and errors. No conversion
    syntax or builtin is currently implemented.
12. **Function values:** preserve identity semantics; decide how builtin
    shadowing, native functions, and callable metadata evolve.
13. **Closures and cycles:** retain no-closure semantics or add captured
    mutable cells; determine environment ownership and a policy for
    reference-count cycles before mutable compound values are introduced.
14. **Memory/performance:** retain direct strings and `std::variant` until
    measurements justify interning, boxing, a constant pool, or a collector.
15. **Compatibility surface:** numeric display, diagnostics, bytecode listing
    stability, runtime error categories, and whether resource limits remain
    language guarantees or implementation limits.

Until these decisions are approved, the current C++ implementation remains
the authority for observed behavior. This document proposes structure and
migration boundaries only.
