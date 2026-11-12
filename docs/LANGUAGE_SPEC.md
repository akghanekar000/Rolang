# Rolang Language Specification (Current C++ Implementation)

This document describes the language accepted and executed by the active
C++17 compiler and VM in `src/frontend.cpp` and `src/backend.cpp`. It records
the implementation as it exists; it does not approve or settle the open
design questions in [SEMANTICS_DECISIONS.md](SEMANTICS_DECISIONS.md) or the proposed
Version 1 contract in [V1_SEMANTICS_PROPOSAL.md](V1_SEMANTICS_PROPOSAL.md).
It is a current-behavior reference, not the normative Version 1 specification.
Rolang is not Python. The C-compatible `src/*.c` code and `rolange.py` are
retained prototypes, not the implementation built by the Makefile. The Python
tests exercise that separate prototype; they do not establish C++ Rolang
semantics.

## Status key

- **Implemented and tested**: current C++ behavior exercised by the existing
  smoke/regression tests or by the focused string-escape regression added with
  this document.
- **Implemented, insufficiently tested**: behavior is present in the C++
  implementation but has little or no direct regression coverage.
- **Partially implemented**: some forms work, but related language behavior is
  incomplete.
- **Unsupported**: the current C++ frontend/runtime has no implementation.
- **Design unresolved**: behavior is observable, but it should not be treated as
  a settled language rule until the design question is reviewed.

## Source form and statements

The source file is a sequence of newline-terminated statements. Leading spaces
define blocks. A greater indentation starts a block; a lesser indentation
closes one or more blocks and must match a prior indentation level. Tabs in
leading indentation are rejected. Blank lines and lines whose first
non-whitespace character is `#` are ignored. A `#` outside a string starts a
comment. Parentheses may span lines; newlines and indentation within an open
parenthesis do not end a logical line or open a block. Semicolons are not
statement separators. **Implemented and tested** for block parsing and
indentation diagnostics; multiline parenthesized input and comment edge cases
are **implemented, insufficiently tested**.

```rol
if True:
    print("inside")
else:
    pass
```

An indented suite may not be empty; use `pass`. The current compound statements
are `def`, `if` with optional `else`, and `while`. `elif` is not a keyword or
grammar form. `pass` is a no-op. Simple statements are assignments, expression
statements, and `return` inside a function. **Implemented and tested**.

```rol
count = 2
while count > 0:
    print(count)
    count = count - 1
```

**Invalid**:

```rol
if True:
print("not indented")  # missing indented suite
```

```rol
if True:
    # comments do not make a non-empty suite
```

## Names, variables, and assignment

An identifier starts with a letter or `_` and continues with letters, digits,
or `_`. `def`, `return`, `if`, `else`, `while`, `pass`, `and`, `or`, `not`,
`True`, `False`, and `None` are reserved words. Assignment has the form
`name = expression`; only a bare name can be assigned. Assignment is a
statement, not an expression, and there is no separate declaration syntax.
It evaluates the right-hand expression before storing its value.

At module execution, assignment stores or replaces a module-global name,
including assignment in a module-level `if` or `while` suite. In a function,
assignment always stores in that invocation's local frame: it updates a
parameter or an existing local, or creates a new local. It does not update a
same-named global. **Implemented and tested** for parameters/local frame
isolation; the remaining assignment rules are implemented but have
insufficient direct tests. Their design remains open (see decisions).

```rol
value = 10
def increment(value):
    value = value + 1
    return value
print(increment(2))
print(value)
```

Output:

```text
3
10
```

**Invalid**:

```rol
items[0] = 1  # indexing and non-name assignment targets are unsupported
```

## Name lookup and scope

When a function reads a name, lookup checks, in order:

1. The current function's parameters and locals.
2. The current module-global mapping.
3. The built-in `print`, if no local or global named `print` was found.

There are no enclosing-function closure scopes. A function created while
executing another function does not capture that function's local frame; it
can access its own locals and module globals. Function code resolves globals
against the live module mapping at call time. At module level, names resolve
from the globals mapping and then the built-in `print`. An unresolved name
raises a runtime error. A local or global may shadow `print`. **Implemented,
insufficiently tested** for global reads from functions and block-defined
functions. Nested function definitions are accepted, but their enclosing
locals are not captured: **partially implemented**. Lookup details are current
behavior, not settled design.

```rol
rate = 3
def scaled(value):
    return value * rate
print(scaled(4))
```

Output: `12`.

**Invalid at runtime**:

```rol
print(missing_name)
```

## Functions, calls, parameters, and returns

Function definition syntax is `def name(parameter, ...):` followed by an
indented, non-empty suite. Parameters are positional names, must be unique,
and may have a trailing comma. Calls use `callee(argument, ...)`; arguments
are positional expressions, and a trailing comma is allowed. The compiler
evaluates the callee and then arguments from left to right before calling.
The number of supplied arguments must exactly equal the parameter count.
There are no default, keyword-only, or named-call arguments. The built-in
`print` accepts zero or more arguments and emits their displayed forms
separated by one space, followed by a newline.

Each user-function call gets a fresh local mapping populated from its
parameters. Calls may be nested or recursive; functions are values and can be
shadowed like other names. **Implemented and tested** for positional arity,
nested calls, argument order, recursion, and `print` shadowing. Parameter and
return type policies remain a design question.

```rol
def combine(a, b,):
    return a * 10 + b
print(combine(combine(1, 2), combine(3, 4)))
```

Output: `154`.

`return` is syntactically valid only within a function definition. It exits
the current invocation immediately. `return expression` returns the evaluated
value without a declared type restriction; bare `return` returns `None`.
Reaching the end of a function also returns `None`. **Implemented and tested**
for returns in branches and rejection of module-level returns; fall-through
and bare-return coverage is insufficient.

**Invalid**:

```rol
def add(a, b):
    return a + b
add(1)  # runtime arity error
```

```rol
return 1  # syntax error: return outside a function
```

## Runtime limits

The VM allows at most **128 simultaneously active user-function calls**.
The module is not a function frame. A call that would enter a 129th active
function fails with `maximum call depth exceeded`. **Implemented and tested**
for successful recursion and depth-limit failure.

Each `VM::run` has a **1,000,000 instruction execution limit** shared by the
module and all function invocations. Each executed bytecode instruction
increments the count. Exceeding it raises `execution limit exceeded`; the
counter resets for the next run. This bounds loops as well as recursive work.
**Implemented and tested** for an infinite non-recursive loop.

## Values and type behavior

The active runtime uses tagged `Int64`, `Float64`, Boolean, String, Null,
Function, and built-in-function values:

| Source value | Runtime representation |
| --- | --- |
| Integer spelling without `.` or exponent | signed 64-bit `Int64` |
| Decimal-point or exponent spelling | finite `Float64` (`double`) |
| `True`, `False` | Boolean |
| `None` | Null |
| Quoted literal | String |
| User-defined function | Function value |
| `print` when not shadowed | Built-in function value |

The source spelling `-9223372036854775808` is accepted as the minimum Int64;
positive integer literals above `9223372036854775807` are rejected. Boolean
values remain accepted by numeric operators as `True = 1` and `False = 0`.
Without a Float64 operand, Booleans participate as Int64 values in numeric
arithmetic; with a Float64 operand they are promoted to Float64.
No explicit conversion functions exist. `print` formats values for display;
it does not convert values for subsequent operations.

Int64 `+`, `-`, and `*` preserve Int64 and report `integer overflow` rather
than overflowing. An operation with a Float64 operand promotes numeric
operands to Float64. A string left operand to `+` requires a string right
operand and concatenates the two strings. Other string/numeric mixes fail.
Non-finite Float64 results fail. These rules are **implemented and tested**.

Equality preserves the established numeric compatibility: Int64, Float64, and
Boolean values compare numerically, so `True == 1` and `1 == 1.0` are true.
Int64-to-Float64 equality uses the Float64 conversion and can therefore lose
precision for large integers. Non-numeric different types compare unequal;
same-type strings and Null compare by value, and functions by identity.
Ordering accepts numeric operands (including Booleans). These semantics are
**implemented and tested**, but remain subject to the language decision
register rather than constituting approval of a normative V1 contract.

**Invalid at runtime**:

```rol
print("value: " + 3)  # string concatenation requires two strings
```

## Arithmetic, comparisons, and evaluation

Supported arithmetic operators are `+`, `-`, `*`, `/`, `//`, `%`, and `**`.
Unary `+` and `-` accept numeric operands (including Booleans). `/` produces
a Float64 quotient. Int64/Int64 `//` produces an Int64 floor quotient and
Int64/Int64 `%` produces an Int64 remainder whose sign follows the divisor.
If either operand is Float64, `//` produces the floating-point floor and `%`
retains `fmod` behavior (remainder sign follows the dividend). Both division
operators and `%` reject a zero divisor. Int64 `**` with a nonnegative Int64
exponent returns checked Int64; a negative Int64 exponent or any Float64
operand returns Float64. Unary integer negation and Int64 arithmetic are
checked. Non-finite Float64 results raise a runtime error. These choices are
**implemented and tested**; see [SEMANTICS_DECISIONS.md](SEMANTICS_DECISIONS.md)
for choices that remain subject to language-design approval.

Comparisons are `==`, `!=`, `<`, `<=`, `>`, and `>=`. Each comparison produces
a Boolean. Repeated comparison operators parse as ordinary left-associated
binary operators; there is no dedicated chained-comparison evaluation.
**Implemented; comparison operators and selected equality cases are tested**.
Whether to add chained-comparison semantics is unresolved.

For ordinary binary operators, operands evaluate left to right. `and` and
`or` are short-circuit operators and return Boolean values:

- `left and right` evaluates `right` only if `left` is truthy; its result is
  `True` exactly when both operands are truthy.
- `left or right` evaluates `right` only if `left` is falsey; its result is
  `True` exactly when at least one operand is truthy.

They do **not** return either operand. `not expression` returns the negation
of the expression's truth value. **Implemented and tested**, including skipped
undefined names, Boolean results, and mixed precedence.

### Truthiness used by conditions and Boolean operators

`None`, `False`, numeric zero (including negative zero), and the empty string
are falsey. `True`, nonzero numbers, non-empty strings, user functions, and
the built-in `print` are truthy. Conditions do not require a Boolean. This is
**implemented and tested** for the runtime value classes.

### Operator precedence and associativity

Higher rows bind more tightly. Parentheses may override grouping.

| Precedence | Operators | Associativity / binding |
| ---: | --- | --- |
| 9 | Function call `(...)` | Left-to-right call chaining |
| 8 | `**` | Right-associative |
| 7 | Prefix unary `+`, `-` | Prefix; nests right-to-left |
| 6 | `*`, `/`, `//`, `%` | Left-associative |
| 5 | `+`, `-` | Left-associative |
| 4 | `<`, `<=`, `>`, `>=` | Left-associative |
| 3 | `==`, `!=`, prefix `not` | Infix equality is left-associative. `not` consumes equality and tighter operators in its operand. |
| 2 | `and` | Left-associative; short-circuit |
| 1 | `or` | Left-associative; short-circuit |

Thus `-2 ** 2` parses as `-(2 ** 2)`, `2 ** 3 ** 2` parses as
`2 ** (3 ** 2)`, and `not 1 == 2` parses as `not (1 == 2)`.
**Implemented and tested** for the displayed unary and boolean/comparison
grouping; exponent associativity is implemented but insufficiently tested.

## Conditions and loops

`if expression:` executes its suite when the expression is truthy; an optional
`else:` suite runs otherwise. `while expression:` reevaluates its condition
before every iteration and repeats while truthy. `pass` does nothing.
`return` inside a loop still immediately returns from the enclosing function.
There are no `break` or `continue` statements and no `for` loop. `break` and
`continue` are not reserved keywords; a bare use is parsed as a name expression
and normally fails at runtime with an undefined-name error, rather than
controlling the loop. **Implemented and tested** for `if`/`else` and normal
`while` flow; truthiness and control-flow corner cases are insufficiently
tested. Loop-control policy is open.

## Strings and escapes

Single- and double-quoted strings are supported. Strings cannot span physical
lines. The recognized escapes are `\n`, `\t`, `\r`, `\\`, `\'`, and `\"`.
Any other escape, a trailing backslash, or a missing closing quote is a
lexical error. String concatenation is supported only with `+` between two
strings. String ordering is unsupported.

```rol
print("first\nsecond\tcolumn")
print('quote: \'')
```

The escape spellings, unterminated strings, and invalid escapes are
**implemented**; the newly added C++ test covers escaped quotes, backslashes,
newlines, and tabs. Other string operation combinations are insufficiently
tested.

## Errors and diagnostics

Lexical and parse errors include a source line and column. The CLI reports
them on standard error with an `error:` prefix and exits nonzero. Runtime
errors include the executing instruction's source line and column. Examples
include undefined names, non-callable values, wrong arity, invalid operand
types, division/modulo by zero, non-finite arithmetic, call-depth overflow,
and instruction-limit overflow. Errors are not catchable in the language.

`check` performs lexing and parsing, then runs the conservative Phase 1
semantic analyzer. It reports source-located undefined names, known direct
function-call arity mismatches, and invalid operators when both operand types
are statically known. Semantic errors are written to standard error and make
`check` exit nonzero. The analyzer uses possible bindings collected from the
module and current function (including conditional/loop suites); it does not
perform definite-assignment analysis, infer variable or parameter types, or
capture enclosing-function locals. Calls whose target is dynamic or not
unambiguously established remain runtime checks. `return` outside a function
is still rejected by the parser before semantic analysis can run.

The semantic pass is read-only and independent of bytecode generation.
`run` still compiles and executes directly, while `compile` still writes a
readable bytecode listing rather than a bytecode file the VM can load. Thus
semantic findings change `check` only; they do not move errors earlier for
`run` or `compile`. **Implemented and tested** for syntax and semantic
locations, known arity, selected known operand incompatibilities, runtime
errors, malformed literals, and execution limits; exact diagnostic stability
and any future catchable-error model remain unresolved.

## Unsupported features and incomplete areas

The active C++ implementation does not support declarations with `let`/`var`,
lists, indexing, `for`, `break`, `continue`, imports, classes, comprehensions,
exceptions, lambdas, closures, keyword arguments, default parameters,
multiline string literals, or full Python compatibility. `src/rolang.h`,
`src/parser.c`, and `src/vm.c` describe an older C prototype with a different
grammar; `rolange.py` and `tests/test_rolange.py` describe another retained
prototype. Neither defines the behavior specified here.
