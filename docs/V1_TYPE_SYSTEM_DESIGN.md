# Rolang V1 Type System and Runtime Value Design

**Status: design proposal; it does not change current compiler or VM behavior.**

This document designs a foundation for Rolang's type system and runtime
values. It is grounded in the active C++17 implementation described by
[LANGUAGE_SPEC.md](LANGUAGE_SPEC.md), the proposed language contract in
[V1_SEMANTICS_PROPOSAL.md](V1_SEMANTICS_PROPOSAL.md), and the open choices in
[SEMANTICS_DECISIONS.md](SEMANTICS_DECISIONS.md). The Makefile builds
`src/frontend.cpp` and `src/backend.cpp`; the C and Python implementations
are retained prototypes and are not normative.

Nothing in this document authorizes a behavior change. In particular, do not
implement the numeric or equality recommendations until the corresponding
approval decisions below are resolved.

## Executive recommendation

1. Keep V1 dynamically typed, but define a language-level `Type` model
   independent from the C++ representation.
2. Make `Int` and `Float` distinct. Recommend signed 64-bit integers with
   checked overflow, finite IEEE-754 binary64 floats, and no implicit
   Boolean-to-number conversion.
3. Use a closed C++17 `std::variant` for the VM's immediate value alternatives
   (`None`, integer, float, Boolean, string, function, and later shared
   handles to compound values). Do not use the variant index as Rolang type
   identity or expose its ABI to native code.
4. Put operator behavior in explicit language-level dispatch helpers, not
   scattered C++ coercions. Keep runtime checks even after optional static
   checking is introduced.
5. Add future static checking as a separate semantic-analysis pass whose
   results annotate stable AST node IDs and inform bytecode/IR generation.
   Dynamic mode remains the default and does not require a compiler rewrite.

## 1. Current implementation boundary

The current frontend parses number spelling into a `double`; its AST numeric
field is a `double`. The backend `Value` is:

```cpp
using Value = std::variant<
    std::monostate, double, bool, std::string,
    std::shared_ptr<Function>, BuiltinPrint>;
```

Consequences of today's implementation (not the recommendation below):

- There is no integer runtime type. Integer-looking literals are doubles.
- Booleans are accepted by numeric operators as 0 and 1.
- `True == 1` is true; other cross-type pairs compare unequal.
- `+` concatenates two strings or adds numeric values. Other operators are
  numeric; invalid operands fail at runtime.
- `/` returns a double, `//` floors a double quotient, and `%` uses
  `std::fmod` (remainder sign follows the dividend).
- Non-finite arithmetic results are runtime errors.
- Truthiness is false for `None`, `False`, numeric zero, and the empty string;
  functions and other non-empty values are true.
- Functions compare by shared object identity. `and` and `or` short-circuit
  and return Booleans.

These are documented so an implementation can be compared against the
proposal; they are not presumed to be the desired Rolang type semantics.

## 2. Type vocabulary and identity

### 2.1 Type identities

The language-level type universe should be modeled explicitly, separately
from C++ classes and `std::variant` indices:

| Rolang type | Identity and intended V1 meaning |
| --- | --- |
| `NoneType` | The single type of the `None` value. |
| `Bool` | `True` and `False`; not a numeric subtype. |
| `Int` | Exact signed integer values in the approved range. |
| `Float` | IEEE-754 binary64 values under the finite-value policy below. |
| `String` | Immutable sequence of the language's specified string units. |
| `Function` | Callable value; signatures are runtime metadata if/when needed. |
| `Array<T>` | Future homogeneous or dynamically checked mutable sequence; see below. |
| `Object` | Future dynamic keyed collection; separate from nominal records. |
| `Struct<Id>` | Future nominal user-defined record type, identified by declaration. |
| `Function<P..., R>` | Future function type identity from parameter and result types. |
| `Dynamic` | Static-analysis type for values not yet known, not a runtime value. |

Primitive type identity is canonical and independent of the current value.
Type equality compares identities, not display names. User-defined types need
a stable declaration identity (`TypeId`) so two declarations with the same
spelling in different scopes do not accidentally become the same type.

For dynamic V1 execution, each value has exactly one runtime type tag.
Variables do not acquire a permanent type: assigning an `Int` and later a
`String` to the same name is permitted unless a future checked annotation
constrains that binding.

### 2.2 Compatibility and conversion are different relations

Do not conflate assignment compatibility with conversion:

- **Type identity**: whether two types denote the same type.
- **Compatibility**: whether a value of one type is accepted in a specified
  context without changing it.
- **Conversion**: an explicit operation that produces a value of another
  type, or fails.

Recommended dynamic V1 policy:

- Assignment, parameter passing, and return do not coerce values.
- A value of any runtime type can be stored, passed, and returned in dynamic
  mode. An operation then checks the actual operand types.
- Operator-specific compatibility is deliberately narrow: arithmetic accepts
  `Int`/`Float` according to the promotion table; `+` additionally accepts
  `String + String`; comparisons accept the stated value pairs.
- There are no implicit conversions for assignment, arguments, or return.
- No general conversion functions are added by this proposal. If approved
  later, conversions such as `int(value)`, `float(value)`, `bool(value)`, and
  `str(value)` must each have specified accepted inputs, precision/loss rules,
  and errors. Displaying a value with `print` is not a conversion.

For a future static checker, compatibility is contextual: `Int` may be
accepted where a `Float` is expected only if the language explicitly approves
a widening conversion there. A value-preserving implicit numeric promotion
for operators does not automatically imply assignment conversion.

## 3. Proposed V1 value and operation rules

The following is the recommended baseline, subject to the approval register.

### 3.1 Primitive values and operations

| Values | Equality | Ordering | Truthiness | Invalid examples |
| --- | --- | --- | --- | --- |
| `NoneType` | `None == None` is true | Not ordered | False | `None + 1`, `None < None` |
| `Bool` | Same-type value equality | Not ordered | `False` is false; `True` is true | `True + 1`, `False * 2` |
| `Int` | Exact integer equality | Numeric ordering | Zero false; otherwise true | `1 + "1"`, `1 / None` |
| `Float` | IEEE numeric equality, with the finite policy below | Numeric ordering | `+0.0` and `-0.0` false; otherwise true | `1.0 + "1"`, `1.0 // None` |
| `String` | Same-type content equality | Not ordered in V1 | Empty false; otherwise true | `"a" + 1`, `"a" < "b"` |
| `Function` | Identity equality | Not ordered | True | `function + function` |

`==` and `!=` should be defined for all operand pairs. Different language
types compare unequal; no conversion is performed for equality. Thus
`True == 1`, `1 == 1.0`, and `"1" == 1` are all false. Equality of functions
is by function-object identity, not source name or body. `None == None` is
true.

Mutable future arrays, objects, and structures should initially use identity
equality. Structural equality has cycle detection, aliasing, recursion-depth,
and mutability implications; it must not appear accidentally because a C++
container happens to provide equality.

`<`, `<=`, `>`, and `>=` are defined for two numeric operands (`Int` and/or
`Float`) and reject all other types. They produce `Bool`. Mixed numeric
ordering must compare the mathematical values without first rounding a large
`Int` to `Float`; binary64 cannot exactly represent every signed 64-bit
integer. The implementation should use a range-aware comparison helper or
another demonstrably exact algorithm.

### 3.2 Numeric promotion and result types

No Boolean is numeric. No string is parsed as a number implicitly.

| Operation / operands | Result and rule |
| --- | --- |
| `Int` with `Int` for `+`, `-`, `*` | `Int`; checked overflow. |
| `Float` with `Float` for `+`, `-`, `*` | `Float`; reject non-finite result. |
| Mixed `Int`/`Float` for arithmetic | Promote the `Int` operand to binary64; result is `Float`; reject non-finite result. Precision loss is possible and is explicit in the result type. |
| `/` with numeric operands | Always `Float`; zero divisor is an error. |
| `Int // Int` | `Int` floor quotient; checked edge cases. |
| Any `//` with a `Float` operand | `Float`; floor the numeric quotient; reject non-finite result. |
| `Int % Int` | `Int`, with floor-division identity and remainder sign following divisor. |
| Any `%` with a `Float` operand | `Float`; recommend preserving current `fmod`/dividend-sign behavior. |
| `Int ** Int` | Recommend `Int` for nonnegative exponent with checked overflow; negative exponent returns `Float`. |
| `**` with a `Float` operand | `Float`; reject non-finite result and invalid real-domain results. |
| Unary `+` / `-` | Preserve `Int` or `Float`; checked negation for `Int`. |

The floor integer quotient/remainder pair is:

```text
q = floor(a / b)
r = a - q*b
a == q*b + r
```

for `b != 0`, with `r` having the divisor's sign or being zero. The
implementation must not calculate `a - q*b` in a way that itself overflows.
The `INT64_MIN // -1` case overflows and must be an error. Integer division by
zero and modulo by zero are errors. Float `//` and `%` follow the approved
float rules independently; do not silently apply integer remainder rules to
floats.

These numeric results are proposals, not inferred from C++: the current
implementation treats every number as `double`, accepts Boolean operands,
and applies floating operations throughout.

### 3.3 Integer model

Recommendation: signed 64-bit two's-complement value domain,
`[-2^63, 2^63-1]`, with checked overflow. The semantic domain is signed
64-bit; the C++ representation should use `std::int64_t` after confirming
the type is available on the target.

- Integer literals are numbers with neither a decimal point nor exponent.
- Decimal-point or exponent syntax creates `Float`: `3`, `3.0`, and `3e0`
  therefore have distinct types.
- Preserve the original numeric token spelling until classification and
  range-checking are complete. Do not first round an integer through
  `strtod`.
- Reject out-of-range literals. The minimum integer literal requires careful
  handling because `9223372036854775808` is not itself an `Int`, while its
  unary-negated form `-9223372036854775808` should be representable if that
  grammar case is approved.
- Addition, subtraction, multiplication, unary negation, integer exponent,
  and floor division use checked operations. Never rely on C++ signed
  overflow (undefined behavior).
- `Int / Int` returns `Float`; integer exactness is not retained by `/`.

The choices for width, overflow and the minimum-literal spelling require
approval. If a different integer width is chosen, update parsing, arithmetic,
formatting, VM constants, tests, and future IR together.

### 3.4 Floating-point model

Recommendation: language `Float` is finite IEEE-754 binary64, round-to-nearest
ties-to-even under the implementation's normal floating environment.
`double` is suitable only on platforms where it satisfies the required
binary64 properties; the build should check `std::numeric_limits<double>`
rather than assuming every C++17 target does. No extra numeric dependency is
needed.

- Source literals that overflow to infinity or are otherwise malformed are
  rejected during lexing.
- Arithmetic producing NaN or either infinity raises a source-located runtime
  arithmetic error. This preserves the current finite-result policy.
- Division by `+0.0` or `-0.0` raises division-by-zero before performing the
  operation.
- Consequently NaN and infinity are not ordinary Rolang values in V1. If
  they are introduced later (for example through native interop), their
  equality and ordering policy must be explicit. The recommended IEEE rules
  are NaN unequal to everything including itself and all ordered comparisons
  with NaN false; infinities compare normally.
- `+0.0 == -0.0` is true, while both are falsey. Float equality is numeric,
  so same-type signed zeros compare equal.
- Formatting must be locale-independent and round-trip safe enough to
  reproduce the same finite float when parsed. Current `setprecision(15)`
  display is not a sufficient normative format guarantee.

### 3.5 Truthiness

Recommend retaining the current scalar truthiness rule:

```text
false: None, False, Int(0), Float(+0.0), Float(-0.0), String("")
true:  all other scalar values, including functions
```

Conditions accept any value. `not` returns `Bool`; `and` and `or` short-circuit
and return `Bool`, not an operand. For future compound values, recommend empty
arrays and empty objects be falsey, and all structures/functions be truthy.
That extension requires approval and explicit tests; it is not currently
implemented.

### 3.6 Explicit conversions and invalid operations

No explicit conversion syntax or builtins currently exist. The design should
not add casts as a side effect of adding numeric types. A future conversion
proposal must distinguish:

- exact conversions (`Int` to `Float` is not always exact);
- checked conversions (finite, integral `Float` to `Int`, range checked);
- formatting (`String(value)`) versus language display (`print(value)`);
- truth conversion (`Bool(value)`) versus condition truthiness.

An operation with invalid operand types is a runtime `TypeError`-class
diagnostic in dynamic mode, including source line and column. Zero divisors,
integer overflow, invalid float results, unbound names, bad calls, and
resource limits should remain distinguishable runtime error categories.
They must not return success-shaped defaults such as `None`.

Examples:

```rol
print(1 + True)       # Type error: Bool is not numeric
print("count=" + 2)   # Type error: String concatenation requires String
print(1 < "2")        # Type error: ordering requires two numeric operands
print(1 == "1")       # Valid; prints False, not a type error
print(1 / 0)          # Division-by-zero error
```

## 4. Runtime representation in C++17

### 4.1 Alternatives considered

| Representation | Advantages | Costs and risks |
| --- | --- | --- |
| Tagged union (`enum` plus C++ union) | Compact when payloads are inline; explicit tag and control of layout; can be tuned for a VM or native ABI. | Manual construction, destruction, copying, and move correctness for strings and handles; every new alternative touches lifetime code; easy to introduce undefined behavior; does not by itself solve recursive compound ownership. |
| `std::variant` | Standard C++17, exhaustive visitation, correct managed lifetimes, simple addition of closed alternatives, readable dispatch; no dependency. | Size is at least the largest alternative plus discriminator/alignment; can become large if strings/compound payloads are inline; visitation and alternative order can tempt accidental use of the index as language type ID; layout is not a stable cross-compiler/native ABI. |
| Custom `Value` structure | Can separate stable `ValueKind` from payload, centralize operations, and evolve toward tagged/boxed layouts or specialized storage. | More code and maintenance than a variant; a hand-written union repeats variant's lifetime risks; abstraction does not automatically improve speed or memory; can become premature universal machinery. |

The current `std::variant` is already a sound starting point for the
interpreter. Replacing it with a custom tagged union solely in anticipation of
native compilation would increase risk without a measured performance case.

### 4.2 Recommendation: variant for the VM, stable type API around it

Keep a closed `std::variant` for ordinary interpreter values, but introduce
small explicit language-level helpers and names. The shape below is
illustrative, not a request to edit implementation now:

```cpp
struct Function;
struct ArrayData;
struct ObjectData;
struct StructData;
struct Builtin;

using Value = std::variant<
    NoneValue,
    std::int64_t,
    double,
    bool,
    std::string,
    std::shared_ptr<const Function>,
    BuiltinId,
    std::shared_ptr<ArrayData>,
    std::shared_ptr<ObjectData>,
    std::shared_ptr<StructData>
>;

enum class TypeKind {
    None, Bool, Int, Float, String, Function,
    Array, Object, Struct, Dynamic
};
```

`NoneValue` can be `std::monostate` if a distinct wrapper is not useful.
Avoid a `BuiltinPrint` singleton alternative as the long-term builtin model:
use a builtin ID or callable descriptor so future builtins do not each require
ad hoc VM cases. Function identity should be a stable object identity, not
equality of code pointers. The illustrative const function handle assumes
function code is immutable; mutable closure environments would need a separate
design.

Important constraints:

- Type tests and dispatch use named helpers (`type_of`, `is_numeric`,
  `truthy`, `equal`, `compare`, `add`, `to_display_string`), never
  `variant::index()` as the language's type identity.
- Exhaustive `std::visit` is preferred where it makes unsupported cases
  explicit. Avoid broad fallback branches that silently accept new variants.
- Keep strings and future containers managed by C++ objects. Use shared
  handles for recursive and mutable compound values; define ownership and
  mutation separately from `Value`.
- Do not add a per-value `Type*` to every primitive. The variant tag already
  distinguishes primitive runtime values; explicit type descriptors are
  needed for parameterized and nominal types, not redundant primitive tags.
- Benchmark before replacing the value layout. Likely memory costs in the
  current design include `std::string`/`shared_ptr` storage and
  `Instruction::value` carrying a full `Value` even for non-constant
  instructions. A later bytecode-constant pool may help more than a custom
  union.

### 4.3 Compound type extension points

Do not implement compounds in this task, but reserve semantic distinctions:

- `Array<T>` is mutable and ordered. Recommend invariant `T` for mutable
  arrays; covariance would allow storing a wider type through a narrower
  alias. Dynamic `Array` may hold mixed runtime values if no element type was
  declared.
- `Object` is an open mapping from string keys to dynamic values. Its type is
  not a user-defined record.
- `Struct<Id>` is nominal, with declared fields and a stable `TypeId`; whether
  fields are mutable is a separate rule.
- Compound values are references/handles at runtime. Assignment aliases the
  same object unless an explicit copy operation is later specified.
- Recommend identity equality and truthiness based on emptiness for arrays
  and objects. Struct equality should default to identity unless a declaration
  explicitly requests value equality in a future feature.
- Recursive and cyclic object graphs must be allowed or rejected explicitly;
  display, equality, and garbage collection/lifetime rules must account for
  cycles.

An open-ended type universe should not be represented by an ever-growing
primitive enum alone. Add type descriptors for `Array<T>`, function
signatures, and nominal user types as those features arrive; keep runtime
value tags closed where practical.

## 5. Static checking without a compiler redesign

Dynamic V1 remains the default. Optional future static checking can be
introduced as a semantic pass between parsing and bytecode generation:

```text
source
  -> lexer
  -> parser / AST
  -> name and type analysis (optional; produces diagnostics + side tables)
  -> bytecode generator
  -> VM
```

The analysis result should be distinct from the AST's source syntax:

```text
AnalysisResult
  NodeId -> inferred Type
  NodeId -> binding / scope information
  NodeId -> selected operator or required runtime guard
  diagnostics with source span and error category
```

The current AST nodes do not have stable IDs, so add a `NodeId` (or use a
stable node address for one analysis lifetime) before relying on side tables.
Prefer IDs: parser and optimizer transformations can change addresses.
Source spans already exist as line and column on tokens/nodes and should be
preserved through diagnostics.

Suggested checker rules:

1. Unannotated bindings begin as `Dynamic`; infer where useful, but do not
   silently turn a dynamic binding into a permanent type constraint.
2. Add annotations and checker mode only after syntax and opt-in behavior are
   approved. A possible gradual type is `Dynamic`/`Any`, with explicit rules
   for calls and assignments.
3. In checked code, reject statically known invalid operations. Where a
   `Dynamic` value reaches a typed operation, emit a runtime guard rather than
   assuming the static checker proved it.
4. Keep VM type and range checks. Static checking is an earlier diagnostic,
   not a replacement for validation of dynamic values, bytecode, or future
   native/foreign inputs.
5. Let the bytecode compiler consume analysis metadata optionally. In dynamic
   mode it compiles as today. In checked mode it may select specialized
   opcodes or add guards, but the language-level operation helpers remain the
   source of truth.

This requires new analysis modules and optional metadata, not a new parser,
runtime, or compiler pipeline. It avoids embedding inferred types directly in
syntax nodes and keeps unannotated programs on the existing dynamic path.

## 6. Integration points

| Component | Foundation needed | Compatibility boundary |
| --- | --- | --- |
| Lexer | Preserve number spelling and source span; classify integer syntax versus float syntax without prematurely converting integer text to `double`; retain malformed/out-of-range diagnostics. | Existing token shape exposes a `double`; changing it is future implementation work, not part of this design document. |
| Parser | Preserve literal kind/spelling in AST. Future annotations should be an additive grammar extension after `:` syntax and checked-mode policy are specified. | Current grammar does not contain type annotations or conversion expressions. |
| AST | Represent integer constants distinctly; retain source spans and add stable `NodeId` before checker side tables. | Do not put C++ RTTI or `std::variant` indices into AST nodes. |
| Semantic analysis | Introduce reusable `Type`, type-identity and compatibility operations; validate statically known operations in optional checked mode. | Dynamic V1 still permits heterogeneous bindings and reports operation failures at runtime. |
| Runtime values | Add `int64_t`, explicit operator helpers, and later compound handles in the chosen `Value` representation. | Preserve explicit errors and source locations; no silent conversion or defaulting. |
| Bytecode | Constants carry explicit `Int`/`Float`; add specialized opcodes only if measured/useful; preserve source locations and validate loaded bytecode. | Existing in-memory `Constant` instructions carry `Value`; serialized listing versioning needs a separate compatibility policy. |
| VM | Centralize promotion, overflow checks, truthiness, equality, ordering, formatting, and error categories. | Keep dynamic checks whether or not optional static checks exist. |
| Future IR | Carry language `TypeId` and explicit conversion/promotion operations; distinguish checked integer arithmetic from wrapping or unchecked machine operations. | IR type identity must be language-defined, not copied from C++ types. |
| Native generation | Lower `Int`/`Float`/`Bool` to target representations while preserving overflow, NaN, conversion and comparison semantics; use runtime helpers for dynamic operations and guards. | Never treat `std::variant` layout or C++ function/object ABI as the Rolang native ABI. |

## 7. Examples of intended behavior

The following examples show recommendations only. They are not expected to
run under the current compiler where integer/float distinction matters.

```rol
whole = 7
fraction = 7.0
print(whole + 2)             # Int(9)
print(fraction + 0.5)        # Float(7.5)
print(whole / 2)             # Float(3.5)
print(whole == fraction)     # False: type-aware equality
print(whole < fraction)      # False: numeric ordering across numeric types
```

```rol
print(-5 // 2)               # Int(-3)
print(-5 % 2)                # Int(1), divisor-sign remainder
print(5 % -2)                # Int(-1)
```

```rol
value = True + 1             # Runtime type error: Bool is not numeric
large = 9223372036854775807 + 1  # Runtime integer overflow
```

For future static checking:

```rol
# Illustrative future syntax only; not V1 syntax.
def scale(value: Float) -> Float:
    return value * 2.0
```

In a checked mode, the known `Float * Float` operation is statically valid.
If `value` instead arrives from a dynamic source, a runtime guard is still
required.

## 8. Unresolved decisions requiring approval

These decisions must be settled before implementation changes make them
normative:

1. **Integer domain and overflow:** approve signed 64-bit range, checked
   overflow, and whether the minimum integer's spelling with unary minus is
   accepted.
2. **Boolean arithmetic:** approve rejecting `Bool` in numeric operations,
   rather than retaining current `True=1`/`False=0` behavior.
3. **Mixed numeric arithmetic and ordering:** approve integer-to-float
   promotion for arithmetic; confirm exact mixed-type ordering and strict
   cross-type equality (`1 == 1.0` false).
4. **Power:** approve `Int ** nonnegative Int -> Int`, checked overflow, and
   negative integer exponent returning `Float`, or select another rule.
5. **Division/remainder:** approve floor integer `//`, divisor-sign integer
   `%`, and retaining current float floor/`fmod` behavior.
6. **Float contract:** approve required binary64 platform properties, finite
   values only, error behavior for NaN/infinity-producing arithmetic, and
   display/round-trip requirements.
7. **Equality:** approve strict cross-type inequality and identity equality
   for functions and future mutable compounds.
8. **Truthiness:** approve current scalar falsey set and decide truthiness of
   future empty arrays/objects.
9. **Conversions:** decide which explicit conversion functions exist, if any,
   and their failure/precision semantics. No conversion is proposed for V1.
10. **Future compounds and typing:** decide when arrays/objects/structures,
    annotations, generic types, and opt-in static checking enter the language.

Until approved, the current implementation remains authoritative for actual
behavior, while this document is a design reference only.

## 9. Step-by-step implementation roadmap

Proceed in small, testable phases after approvals. Do not rewrite the runtime
or change behavior as part of this design deliverable.

1. **Resolve approvals.** Record the accepted numeric, equality, truthiness,
   conversion, and future-type decisions in `SEMANTICS_DECISIONS.md` and the
   normative V1 specification.
2. **Add semantic type primitives.** Introduce `TypeKind`/`TypeId` APIs and
   operator result/compatibility helpers without changing parser or VM
   behavior. Unit-test type identity and exhaustive operation dispatch.
3. **Preserve numeric source form.** Update lexer tokens and AST literals to
   distinguish integer and float spellings and retain source locations.
   Range-check integers before conversion and test bounds and malformed input.
4. **Implement integer runtime values.** Add `int64_t` to VM values and
   constant instructions; implement checked arithmetic, approved promotion,
   division/modulo, power, comparisons, truthiness, equality, and formatting.
   Add focused tests for every boundary and error category.
5. **Consolidate runtime operations.** Centralize type dispatch and
   source-located errors; remove old incidental Boolean coercion only if
   approved. Preserve call semantics, limits, and unrelated VM behavior.
6. **Stabilize bytecode representation.** Define constant serialization,
   versioning, and validation separately from the readable listing. Consider
   constant pools or compact instructions only after measurement.
7. **Add optional static analysis.** Introduce stable AST `NodeId`s, an
   `AnalysisResult`, diagnostics, and a checked-mode entry point. Keep dynamic
   mode as default; preserve runtime guards for `Dynamic`.
8. **Add compounds incrementally.** Design and implement arrays, objects,
   and nominal structures one at a time, defining mutability, aliasing,
   equality, truthiness, display, and cycle/lifetime behavior before each
   runtime variant is added.
9. **Introduce typed IR and native compilation only as separate projects.**
   Reuse language `TypeId` and operation semantics; test interpreter/IR/native
   conformance against the same semantic corpus. Never depend on C++ variant
   layout for generated code.
10. **Lock conformance and document observed behavior.** Run the full C++ test
    suite after each phase, add regression cases for approved semantics, and
    update `LANGUAGE_SPEC.md` only once implementation and tests agree.

## Validation note

This task creates documentation only and intentionally does not alter compiler
behavior. The existing suite was run against the current implementation:

```text
$ make test
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
Ran 20 tests in 0.122s

OK
```

The smoke script passed, and all 20 C++ compiler regression tests passed.
These results validate the unchanged current implementation; they do not
validate proposed semantics that have not yet been implemented.
