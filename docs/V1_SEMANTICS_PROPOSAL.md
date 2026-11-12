# Rolang Version 1 Semantics Proposal

**Status: proposal for review; not an approved language contract.**

This document proposes Version 1 semantics based on the requested design
preferences and the active C++17 implementation. It does not change or silently
redefine compiler behavior. The [current implementation specification](LANGUAGE_SPEC.md)
remains the factual description of what the compiler does today. Decisions
marked **Approval required** need explicit agreement before implementation.
The [decision register](SEMANTICS_DECISIONS.md) summarizes those gates.

The target remains the existing C++17 lexer, parser, AST, bytecode compiler,
and VM. Version 1 work must not replace the compiler with Python, add
unnecessary dependencies, remove existing functionality without an approved
compatibility plan, or start IR or native-code generation.
Where this proposal does not state a change, the current C++ language features
and syntax are intended to carry forward; consult the current-behavior
specification for their exact behavior.

## Proposed decisions

### 1. Function locals and explicit global writes — **Approval required**

**Proposal.** Assignments in a function bind local names by default. A
function may read module globals when the name is not a local binding.
Modifying a module global requires a `global name, ...` declaration in that
function. The declaration applies to the whole function, including assignments
inside its nested `if` and `while` suites. It may appear anywhere in the
function body; duplicate names and names also used as parameters are errors.
At module scope, assignments continue to create or replace globals.

Names assigned anywhere in a function are local throughout that function;
assignment in a nested function body does not bind a name in its enclosing
function.
Reading such a local before its first assignment is an unbound-local runtime
error, not a fallback read of a same-named global. A name declared `global`
always resolves to the module mapping for reads and writes. Function
definitions bind their names under the same local/global rule.

```rol
rate = 3
def scaled(value):
    local = value * rate
    return local
print(scaled(4))

count = 10
def increment():
    global count
    count = count + 1
increment()
print(count)
```

The first function reads a global and writes only a local; the second
explicitly modifies a global.

**Invalid:**

```rol
count = 10
def increment():
    count = count + 1
    return count
```

Here `count` is local because the function assigns it, so reading it before
assignment raises an unbound-local error. Use `global count` to modify the
global, or a differently named local/parameter.

```rol
global count
```

`global` is only valid inside a function.

**Reasoning.** Local-by-default assignment avoids accidental global mutation;
allowing reads keeps constants and shared configuration convenient. Making
local binding independent of runtime assignment order avoids a surprising
global-read-then-local-write split.

**Implementation changes.** Reserve and parse `global`; collect declarations
and local bindings per function, including nested suites; reject declaration
conflicts; add local-uninitialized diagnostics and a global-store operation or
equivalent in bytecode execution. Preserve module-level storage behavior.

**Compatibility risk.** Code that currently reads a same-named global before
its first local assignment will instead fail with an unbound-local error.
Functions that intentionally mutate globals without declarations will become
local assignments. A semantic analysis pass must also avoid changing current
behavior for unrelated name lookup.

**Necessary tests.** Global reads; local shadowing; local read before
assignment with and without a same-named global; declared global read/write;
declarations nested in branches/loops; parameter/duplicate conflicts;
module-scope declarations rejected; module assignment and global function
definition behavior.

### 2. Nested functions and closures — **Approval required**

**Proposal.** Keep nested `def` syntax, but do not capture enclosing function
locals in Version 1. A nested function can read its own locals and module
globals, and its own assignments follow Decision 1. Creating it inside a
function does not extend the lifetime or visibility of that function's local
frame.

```rol
rate = 2
def outer():
    def inner(value):
        return value * rate
    return inner(3)
print(outer())
```

This is valid and reads the module global `rate`.

**Invalid at runtime:**

```rol
def outer():
    value = 5
    def inner():
        return value
    return inner()
```

`inner` cannot capture `outer`'s local `value`.

**Reasoning.** This matches current runtime behavior and avoids adding closure
cell lifetime, mutation, and capture rules to the first language version.

**Implementation changes.** No closure machinery is proposed. Ensure the
function-scope analysis in Decision 1 does not accidentally treat outer
function locals as module globals or captured locals.

**Compatibility risk.** None intended relative to current C++ behavior.
Rejecting or changing nested definitions would be a larger compatibility
change and is not proposed.

**Necessary tests.** Nested function reading a global; nested function reading
an outer local fails; nested function's own local and parameter work; recursive
nested function behavior; nested function `global` affects the module mapping.

### 3. Dynamic typing and a future static-typing path

**Proposal.** Version 1 is dynamically typed. Variables and parameters have no
declared type, and values are checked by operations at runtime. No type
annotation or static type-checking syntax is included in Version 1. A later
version may add optional annotations and an opt-in static checker while
preserving dynamic execution for unannotated programs.

```rol
def identity(value):
    return value
print(identity(2))
print(identity("two"))
```

Both calls are valid.

**Invalid in Version 1:**

```rol
def identity(value: int):
    return value
```

Type annotations are not part of the Version 1 grammar.

**Reasoning.** Dynamic typing fits the current value model and keeps the first
semantics compact. Keeping annotations out until their syntax and checking
mode are designed avoids implying static guarantees the VM cannot enforce.

**Implementation changes.** Preserve runtime operand checks. Do not add a
type checker or annotation syntax in the initial semantics implementation.
Future typing should be an additive parser/checker/runtime-boundary design.

**Compatibility risk.** Version 1 introduces no intended change to existing
unannotated source. Reserving annotation syntax later could conflict with
programs that currently use `:` in other contexts, so the extension must be
specified before adoption.

**Necessary tests.** Existing values can flow through parameters and returns
without static restrictions; incompatible operations fail at runtime;
annotations are rejected by `check`; existing unannotated programs continue
to parse and run.

### 4. Boolean operators and truthiness

**Proposal.** `and` and `or` short-circuit and always return Booleans.
`and` evaluates its right operand only when the left is truthy; `or` evaluates
it only when the left is falsey. `not` returns a Boolean. Conditions accept
any value using the existing truthiness rule: `None`, `False`, numeric zero,
and the empty string are falsey; other numbers, non-empty strings, and
functions are truthy.

```rol
print(0 and missing_name)
print(1 or missing_name)
print("x" and "y")
```

This prints `False`, `True`, `True` without looking up `missing_name`.

**Invalid:**

```rol
if True && False:
    pass
```

`&&` is not a Rolang operator; use `and`.

**Reasoning.** Boolean results make control-flow composition predictable and
are explicitly preferred over operand-returning behavior. Retaining
truthiness avoids needlessly restricting a dynamic language's conditions.

**Implementation changes.** Preserve the existing short-circuit bytecode
control flow and Boolean conversion. Expand truthiness coverage rather than
changing it.

**Compatibility risk.** Existing C++ behavior already returns Booleans and
short-circuits. Programs relying on truthy non-Booleans in conditions remain
valid. The retained Python prototype is not a source of C++ semantics.

**Necessary tests.** Both skipped and evaluated right operands; Boolean result
for falsey/truthy combinations; `None`, both Booleans, positive/negative/zero
numbers, empty/non-empty strings, and function values in conditions and
Boolean expressions.

### 5. Integer and floating-point values — **Approval required**

**Proposal.** Integers and floating-point values are distinct runtime types.
A numeric literal without a decimal point or exponent is an integer; one with
a decimal point or exponent is a float (`3`, `3.0`, and `3e0` therefore have
different types). Integer operations should preserve integer results where
defined and detect overflow. `/` produces a float. Float operations use
finite IEEE-754 binary64 values; non-finite results remain runtime errors.

The preferred integer representation is signed 64-bit, but the width,
overflow behavior, mixed integer/float arithmetic, and integer power rules
require approval before they become normative. The proposal is checked
overflow for signed 64-bit integer operations and promotion of mixed
integer/float arithmetic to float. These choices must not be inferred from
the current all-`double` implementation. If signed 64-bit is approved, the
minimum value `-9223372036854775808` is representable; its magnitude is
accepted only as the operand of unary minus.

Integer floor division and modulo are specified separately in Decision 11.

```rol
whole = 3
fraction = 3.0
print(whole + 2)
print(fraction + 0.5)
```

**Invalid under the proposed signed 64-bit literal range:**

```rol
too_large = 9223372036854775808
```

If signed 64-bit integers are approved, this literal is rejected rather than
rounded or wrapped.

**Reasoning.** Distinct types prevent large integer values from silently
losing precision and let integer-oriented programs retain exact results.
Explicit rules are required because the current implementation converts all
numeric literals and results to `double`.

**Implementation changes.** Preserve numeric token spelling or add a
numeric-kind tag; parse and range-check integer literals separately; extend
AST/runtime values with an integer alternative; define arithmetic,
comparison, formatting, and overflow behavior without changing C++17 or
adding dependencies.

**Compatibility risk.** Existing integer-looking literals currently produce
`double`; integer division, formatting, overflow, and comparisons may change.
Programs relying on floating behavior from integer-looking literals or
Boolean numeric coercion are especially at risk. Boolean arithmetic is
proposed to be rejected; this also requires approval.

**Necessary tests.** Literal classification including decimal/exponent
forms; min/max and out-of-range literals; checked overflow; integer and float
result types; `/`; approved mixed numeric rules; Boolean arithmetic;
formatting; signed zero and non-finite float results.

### 6. Type-aware equality — **Approval required**

**Proposal.** Equality is type-aware and does not coerce values. Values of
different types compare unequal, including `True` and `1`, and integer `1`
and float `1.0`. Values of the same type compare by value for integers,
floats, Booleans, strings, and `None`; function values compare by identity.
`!=` is the negation of `==`.

```rol
print(True == 1)
print(1 == 1.0)
print("1" == 1)
```

All three comparisons are valid and print `False`.

**Invalid:**

```rol
print(1 === 1.0)
```

`===` is not a Rolang operator.

**Reasoning.** Strict type-aware equality avoids implicit equivalence between
values whose types differ and aligns equality with the proposed distinct
integer and float types.

**Implementation changes.** Replace the existing Boolean/number equality
special case with a type-dispatched equality operation, adding the integer
case and preserving function identity semantics.

**Compatibility risk.** `True == 1` currently evaluates to `True`; it will
become `False`. `1 == 1.0` currently compares two doubles and is `True`; it
will become `False` after integer support. Equality results can alter branches
and loop termination.

**Necessary tests.** Same-type values and values of every cross-type pair;
integer versus float; Boolean versus integer/float; `None`; string contents;
function identity; `!=`; and equality-driven control flow.

### 7. Exact function arity; defer defaults

**Proposal.** User-defined calls must supply exactly the declared number of
positional arguments. Parameters remain dynamically typed. Default arguments,
keyword arguments, and implicit argument omission are deferred.

```rol
def add(left, right):
    return left + right
print(add(2, 3))
```

**Invalid at runtime:**

```rol
add(2)
```

**Reasoning.** Exact arity is already simple to explain and implement; defaults
raise questions about evaluation time, ordering, and mutable default values.

**Implementation changes.** Preserve runtime arity checks and source-located
errors. No default-value metadata or call syntax is needed in Version 1.

**Compatibility risk.** Current C++ calls already require exact arity. No
change is intended. Adding defaults later must be additive and separately
specified.

**Necessary tests.** Zero-parameter and multi-parameter exact calls; too few
and too many arguments; nested calls; built-in `print` remains variadic;
trailing commas remain accepted.

### 8. `break` and `continue`

**Proposal.** Support both statements in `while` loops. `break` exits the
innermost enclosing loop. `continue` starts the next iteration by reevaluating
that loop's condition. Neither statement exits a function; `return` continues
to exit the current function. Loop control cannot cross a function boundary.

```rol
value = 0
while value < 5:
    value = value + 1
    if value == 2:
        continue
    if value == 4:
        break
    print(value)
```

This prints `1` and `3`.

**Invalid:**

```rol
break
```

`break` outside a loop is a syntax/semantic error. The same rule applies to
`continue`.

**Reasoning.** These statements provide bounded loop control without
introducing iterable types or `for` semantics.

**Implementation changes.** Reserve both keywords; add AST statement kinds;
track the innermost loop's break and continue targets during bytecode
generation; reset loop context when compiling a nested function; validate
their placement before execution.

**Compatibility risk.** `break` and `continue` currently tokenize as names.
Programs using either as a variable or function name will become invalid.
This intentional keyword change must be called out in release notes.

**Necessary tests.** Break and continue in `while`; nested-loop targeting;
continue reaches condition evaluation; break skips loop `else` (none exists);
interaction with `return`; placement errors at module scope and inside a
function but outside a loop; use inside a nested function defined within a
loop.

### 9. Recursion and runtime limits

**Proposal.** Recursion is supported. Preserve the current safety limits:
at most 128 simultaneously active user-function calls and 1,000,000 executed
bytecode instructions per `VM::run`. The module is not a user-function frame;
the instruction budget is shared across module and function execution and
resets for each run. Exceeding either limit is a fatal runtime error with a
source location.

```rol
def factorial(value):
    if value <= 1:
        return 1
    return value * factorial(value - 1)
print(factorial(5))
```

**Invalid at runtime:**

```rol
def recurse():
    return recurse()
recurse()
```

This exceeds the call-depth limit.

**Reasoning.** Recursion is useful and already implemented; explicit call and
instruction budgets prevent unbounded stack growth and instruction execution.

**Implementation changes.** Retain and document both VM counters/limits when
adding loop control and new value types. Ensure runtime errors preserve source
locations and each run resets the instruction counter.

**Compatibility risk.** Existing programs already encounter these limits.
Programs that rely on a particular numeric limit have no portability promise;
changing a limit later requires a compatibility note and updated tests.

**Necessary tests.** Recursion under the limit; the exact depth boundary and
overflow; infinite recursion; instruction exhaustion in a non-recursive
loop; counter reset across runs; nested loops and calls share the budget.

### 10. Structured runtime exceptions — deferred

**Proposal.** Version 1 runtime errors remain fatal, non-catchable errors
reported by the CLI with source locations. Structured language exceptions,
`try`/`except`, and stack unwinding are future features, not Version 1
constructs.

```rol
print(1 + 2)
```

**Invalid in Version 1:**

```rol
try:
    print(1 / 0)
except:
    print("recovered")
```

**Reasoning.** Catchable errors need an agreed exception taxonomy, handler
matching, stack unwinding, and interaction with VM limits. Deferring them
avoids presenting host runtime failures as a stable language API.

**Implementation changes.** Keep explicit runtime failures and CLI
diagnostics; do not add exception opcodes, handler tables, or catch syntax
now. A future design must use structured error data internally without
silently converting failures to successful values.

**Compatibility risk.** Existing code cannot catch runtime errors. Adding
exceptions later needs a precise definition of catchable categories and
unwind behavior; existing fatal failures should not silently become
catchable.

**Necessary tests.** Runtime failures stop subsequent statements and include
the originating source location; parse failures remain distinct; limits and
operand errors remain fatal.

### 11. Division and modulo — **Approval required**

**Proposal.** `/` always produces a float. Integer `//` rounds toward negative
infinity, and integer `%` satisfies
`a == (a // b) * b + (a % b)` for nonzero `b`, with the remainder's sign
following the divisor. For floating operands, preserve the current C++
behavior for Version 1: `//` floors the quotient and `%` uses `fmod`, so the
remainder follows the dividend's sign. Mixed operands follow the approved
numeric promotion rule in Decision 5.

```rol
print(-5 // 2)
print(-5 % 2)
```

With integer operands this prints `-3` and `1`.

**Invalid at runtime:**

```rol
print(5 // 0)
```

Both `//` and `%` reject a zero divisor.

**Reasoning.** A paired floor-division/modulo rule for integers preserves the
quotient/remainder identity, while keeping existing float behavior avoids
changing current programs without approval.

**Implementation changes.** Add separate integer and float operator paths;
implement integer floor division and divisor-sign modulo without signed
overflow, and preserve the existing floating-point path and zero checks.

**Compatibility risk.** Integer-looking literals currently become doubles,
so changing `%` behavior for those operands can change program results. The
float `%` behavior is explicitly retained in this proposal; changing it
requires separate approval.

**Necessary tests.** Positive and negative dividends and divisors; exact
division and non-exact division; integer identity and remainder-sign rules;
floating `//` and `%` behavior for negative operands; mixed numeric operands
if approved; zero divisors; overflow corner cases.

## Additional semantic details to approve

These are not settled by the requested preferences and must be agreed before
the corresponding implementation:

1. **Numeric model:** signed 64-bit versus another integer range; checked
   overflow; Boolean arithmetic; mixed int/float operators; integer
   exponentiation; and integer/float ordering.
2. **Division and modulo:** whether integer `//` rounds toward negative
   infinity or truncates, and which operand determines the sign of `%`.
   Preserve and explicitly test the current float `//` and `%` behavior or
   approve a change.
3. **Function binding:** the proposal's whole-function local classification
   and unbound-local error, versus preserving runtime fallback to a global
   until a local is assigned.
4. **Nested scope:** retain no-closure behavior or approve lexical closures
   with their lifetime and mutation semantics.
5. **Truthiness:** approve retaining the current falsey set, including
   `None`, zero, and empty string.
6. **Limit stability:** approve documenting current numeric limits as V1
   guarantees or as implementation limits subject to change.

## Prioritized implementation roadmap

This is a proposal only; implementation should begin after approval. Keep each
step in the C++17 codebase and preserve unrelated existing functionality.

1. **Approve semantic gates.** Resolve the six items above; confirm local
   binding/global declaration rules, integer representation and arithmetic,
   equality, and whether current runtime limits become V1 guarantees.
2. **Specify and implement name binding.** Add `global` syntax, function-level
   binding analysis, local-before-assignment errors, and explicit global
   stores. Add focused parser/compiler/VM regressions.
3. **Add integer values.** Extend tokens, AST, bytecode constants, runtime
   values, arithmetic, formatting, comparisons, and tests according to the
   approved numeric rules. Preserve float behavior unless explicitly changed.
4. **Add loop control.** Parse and compile `break`/`continue` with innermost
   loop targets and function-boundary validation; test nested loops and
   returns.
5. **Lock down V1 conformance.** Expand tests for equality, truthiness,
   arity, recursion limits, diagnostics, and compatibility. Update
   `LANGUAGE_SPEC.md` from observed behavior only after implementation passes
   the approved conformance tests.
6. **Defer optional static typing and exceptions.** Design these as separate
   future proposals. Do not start IR or native code generation as part of this
   semantics work.
