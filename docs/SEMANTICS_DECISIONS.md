# Rolang Semantics Decisions

This document separates observable behavior in the current C++ compiler from
language rules that still need review. The recommendations below are proposals
only. No runtime or compiler behavior is changed by this document.

## 1. Assignment inside functions

- **Question:** Does assigning a name in a function always create/update a
  local, or should assignment update an existing global unless explicitly
  declared local?
- **Current behavior:** Every assignment compiled in a function writes its
  local frame, whether the name is a parameter, an existing local, or matches
  a global. Module assignments, including assignments in module-level blocks,
  write globals.
- **Possible choices:** Keep function-local assignment; update an existing
  global by default; or add explicit `global`/`nonlocal` declarations while
  retaining local-by-default assignment.
- **Recommendation:** Keep local-by-default assignment and add an explicit
  declaration only if mutation of globals is needed. This makes local
  assignment independent of the current global environment.
- **Why it matters:**

  ```rol
  count = 10
  def increment():
      count = count + 1
  ```

  Today this reads the global `count`, stores `11` in a new local, and leaves
  the global at `10`. A static local-by-default rule would need an explicit
  global reference or a parameter for the read.

## 2. Reading globals and nested functions

- **Question:** Should functions read module globals dynamically, capture
  lexical environments, or be restricted to explicit captures?
- **Current behavior:** A function resolves its own locals first, then the
  live module-global mapping. Nested functions do not capture enclosing
  function locals; only their own locals and globals are visible.
- **Possible choices:** Keep live global lookup and disallow closures; capture
  enclosing lexical scopes; or require explicit capture/global declarations.
- **Recommendation:** Adopt lexical closures for nested functions while
  retaining module globals as the outermost scope, if nested functions are
  intended as a supported feature. Otherwise reject nested function
  definitions, rather than silently accepting definitions without closures.
- **Why it matters:**

  ```rol
  def outer():
      value = 5
      def inner():
          return value
      return inner()
  ```

  `inner` cannot currently read `outer`'s local `value`.

## 3. Parameters and return values

- **Question:** Should function calls and returns remain dynamically typed
  positional values, or gain defaults, keyword arguments, or type
  declarations?
- **Current behavior:** Parameters must be distinct names. Calls require an
  exact positional argument count; there are no defaults or keyword
  arguments. Any runtime value may be passed or returned. Bare `return` and
  reaching the end of a function both produce `None`.
- **Possible choices:** Keep exact positional arity and dynamic values; add
  default/keyword parameters; add optional type declarations; or require a
  return expression on every path.
- **Recommendation:** Keep dynamic values, exact positional arity, and
  implicit `None` for now; specify defaults and keyword calls separately
  before implementation.
- **Why it matters:**

  ```rol
  def label(value):
      if value:
          return "yes"
  print(label(False))
  ```

  Current output is `None`; requiring a return on all paths or declaring a
  return type would change this program's behavior.

## 4. Type conversion

- **Question:** Which implicit or explicit conversions should the language
  permit?
- **Current behavior:** There are no explicit cast functions. Booleans convert
  to `1`/`0` only when consumed by numeric operations. Strings do not convert
  to numbers. String concatenation requires two strings.
- **Possible choices:** Keep this narrow rule; reject Booleans in arithmetic;
  add explicit conversion built-ins; or add broader implicit conversions.
- **Recommendation:** Avoid broad implicit conversions. If conversion is
  needed, provide explicit built-ins and specify their failure behavior.
- **Why it matters:**

  ```rol
  print(True + 2)
  print("2" + 2)
  ```

  The first currently prints `3`; the second raises a type error.

## 5. Equality across different types

- **Question:** Should equality coerce values, use strict type identity, or
  compare values only within compatible types?
- **Current behavior:** A number and Boolean compare numerically (`True == 1`
  is true). Other different-type values compare unequal. Same-type strings,
  numbers, Booleans, and `None` compare by value; functions compare by
  identity.
- **Possible choices:** Keep the current numeric/Boolean exception; make all
  cross-type equality false; or explicitly define more coercions.
- **Recommendation:** Make equality strict by type, with numeric values
  compared by value and no Boolean/number equivalence. This avoids
  surprising results in conditions and containers if they are added later.
- **Why it matters:**

  ```rol
  print(True == 1)
  ```

  Current output is `True`; strict equality would produce `False`.

## 6. Truthiness

- **Question:** Should conditions accept any value or require Boolean values?
- **Current behavior:** `None`, `False`, zero, and the empty string are
  falsey. Nonzero numbers, non-empty strings, and function values are truthy.
  `if`, `while`, `not`, `and`, and `or` use this rule. `and`/`or` short-circuit
  and return Boolean values.
- **Possible choices:** Keep truthiness; require a Boolean condition; or use
  an explicitly defined `bool` conversion operation.
- **Recommendation:** Keep the current truthiness rule for this small dynamic
  language, but document it normatively and add a cast only if useful.
- **Why it matters:**

  ```rol
  if "ready":
      print("runs")
  ```

  The body currently executes because a non-empty string is truthy.

## 7. Integer versus floating-point arithmetic

- **Question:** Should numeric values be all floating point, or should
  integer literals and operations preserve integer values?
- **Current behavior:** Every numeric literal and arithmetic result is a
  `double`; there is no integer type. Display uses a precision-limited
  conversion.
- **Possible choices:** Keep a single double type; add arbitrary-precision
  integers; or add bounded integers with overflow rules.
- **Recommendation:** Keep one floating-point numeric type until integer
  semantics are deliberately designed. If integers are added, define literal
  parsing, overflow, mixed arithmetic, and display together.
- **Why it matters:**

  ```rol
  print(9007199254740993 == 9007199254740992)
  ```

  The current double representation cannot distinguish these adjacent
  integers.

## 8. Division and modulo

- **Question:** Should `//` and `%` follow floor-division identity rules, or
  truncation/remainder rules?
- **Current behavior:** `/` performs floating division; `//` floors the
  quotient; `%` uses `fmod` and follows the dividend's sign. Consequently
  negative operands do not follow the common floor-modulo identity.
- **Possible choices:** Keep the current combination; use truncation and
  remainder together; or use floor division and a modulo whose sign follows
  the divisor.
- **Recommendation:** Pair floor division with modulo so
  `a == (a // b) * b + (a % b)` (for nonzero `b`), with modulo sign following
  the divisor; document that `/` remains floating-point.
- **Why it matters:**

  ```rol
  print(-5 // 2)
  print(-5 % 2)
  ```

  Current results are `-3` and `-1`; a floor-modulo rule would produce
  `-3` and `1`.

## 9. Loop control

- **Question:** Should loops remain `while` plus `return`, or gain `break`,
  `continue`, and iteration constructs?
- **Current behavior:** Only `while` exists. `pass` does nothing; `return`
  exits the containing function, including when executed inside a loop.
  `break`, `continue`, and `for` are unsupported.
- **Possible choices:** Keep the minimal set; add `break`/`continue` to
  `while`; or add `for` after defining iterable values and iteration.
- **Recommendation:** Add `break` and `continue` only with precisely defined
  nesting and interaction with function returns. Defer `for` until iterable
  types exist.
- **Why it matters:**

  ```rol
  done = True
  while True:
      if done:
          break
  ```

  `break` is not a keyword, so it is parsed as an expression name and normally
  raises an undefined-name error if reached. It does not exit the loop; only a
  condition change, `return`, or instruction-limit failure can end such a
  loop.

## 10. Error handling

- **Question:** Should runtime errors remain fatal, or become catchable
  language values/exceptions?
- **Current behavior:** Runtime failures stop execution and the CLI returns a
  nonzero status with a source location. The language has no `try`, `except`,
  or error values.
- **Possible choices:** Keep fail-fast execution; add a structured result
  convention; or add catchable exceptions with defined stack-unwinding rules.
- **Recommendation:** Keep runtime errors fatal until error categories and
  stack-unwinding semantics are designed; continue improving source locations.
- **Why it matters:**

  ```rol
  print(1 / 0)
  print("still runs")
  ```

  The second statement is not executed because division by zero terminates
  the run.

## 11. Chained comparisons and precedence edge cases

- **Question:** Should `a < b < c` mean a chained comparison or ordinary
  left-associated binary operations?
- **Current behavior:** The parser produces left-associated binary operations.
  There is no chain-specific evaluation or single-evaluation rule for the
  middle operand.
- **Possible choices:** Keep ordinary binary grouping; implement Python-like
  chained comparisons with one evaluation per operand; or reject chains.
- **Recommendation:** Implement explicit chained comparisons only if desired;
  otherwise reject repeated comparison operators to avoid misleading results.
- **Why it matters:**

  ```rol
  print(3 < 2 < 1)
  ```

  The current parse is `(3 < 2) < 1`; because `False` is numeric in ordering,
  this evaluates to `True`, rather than the result of a three-value range
  test.
