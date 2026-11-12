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

The runtime value variants are:

| Source value | Runtime representation |
| --- | --- |
| Numeric literal | `double` |
| `True`, `False` | Boolean |
| `None` | `None` value |
| Quoted literal | String |
| User-defined function | Function value |
| `print` when not shadowed | Built-in function value |

There is no distinct integer type. Numeric literals accept decimal and
exponent notation and are represented as finite double-precision values.
Boolean values are accepted by numeric operators as `True = 1` and
`False = 0`. No explicit conversion functions exist. `print` formats values
for display; it does not convert values for subsequent operations.

`+` adds numeric operands, except that a string left operand requires a
string right operand and concatenates the two strings. Other string/numeric
mixes fail. Numeric operations that produce a non-finite result fail.
**Implemented, insufficiently tested** for most value/operator combinations;
boolean/numeric equality and several error cases are tested.

Equality compares numeric values across `double` and Boolean (`True == 1` is
true); same-type values compare by value for numbers, booleans, strings, and
`None`. Other different-type pairs compare unequal. Function values compare
by function-object identity. `!=` negates equality. Ordering operators accept
numeric operands (including Booleans) and reject other types at runtime.
**Implemented and tested** for the selected cross-type equality cases;
remaining combinations are insufficiently tested and the cross-type policy is
open to design review.

**Invalid at runtime**:

```rol
print("value: " + 3)  # string concatenation requires two strings
```

## Arithmetic, comparisons, and evaluation

Supported arithmetic operators are `+`, `-`, `*`, `/`, `//`, `%`, and `**`.
Unary `+` and `-` accept numeric operands (including Booleans). `/` produces
a floating-point quotient. `//` applies floor to the quotient. `%` uses
`fmod`, so its remainder follows the dividend's sign and may be fractional.
Both division operators and `%` reject a zero divisor. `**` uses floating
exponentiation. A non-finite arithmetic result raises a runtime error.
**Implemented and tested** for ordinary arithmetic, division/modulo by zero,
and non-finite results; negative/fractional division and modulo cases need
more tests and their policy is unresolved.

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
**implemented and insufficiently tested** for every runtime value class; the
truthiness policy is an unresolved design decision.

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

`check` performs lexing and parsing; it does not execute a program or catch
runtime errors. `run` compiles and executes the source in memory. The `compile`
command writes a readable bytecode listing, not a bytecode file that the VM
can load. **Implemented and tested** for syntax and runtime error locations,
arity, arithmetic errors, malformed literals, and execution limits; exact
diagnostic stability and any future catchable-error model are unresolved.

## Unsupported features and incomplete areas

The active C++ implementation does not support declarations with `let`/`var`,
lists, indexing, `for`, `break`, `continue`, imports, classes, comprehensions,
exceptions, lambdas, closures, keyword arguments, default parameters,
multiline string literals, or full Python compatibility. `src/rolang.h`,
`src/parser.c`, and `src/vm.c` describe an older C prototype with a different
grammar; `rolange.py` and `tests/test_rolange.py` describe another retained
prototype. Neither defines the behavior specified here.
