# Rolang and Rolange

Rolang is a small, dynamically typed, Python-inspired language. Rolange is its compiler toolchain, implemented in C++17 (with C-compatible concepts where useful): an indentation-aware lexer, recursive-descent/Pratt parser, AST, bytecode compiler, and stack virtual machine.

This is an evolving language implementation, not Python itself and not yet a production optimizing or native-code compiler. C++ is used for the compiler/runtime implementation; Rolang source programs use `.rol` files.

## Build and use

Requirements: a C++17 compiler (`g++` or `clang++`) and Make.

```sh
make
./build/rolang check examples/hello.rol
./build/rolang tokens examples/hello.rol
./build/rolang ast examples/hello.rol
./build/rolang compile examples/hello.rol -o hello.rbc
./build/rolang run examples/hello.rol
make test
```

`compile` writes a readable, versioned bytecode listing. `run` currently compiles source in memory and executes it in the Rolange VM; loading serialized bytecode files and native machine-code generation are future backend work.

## Example

```python
# Indentation defines blocks; semicolons are not required.
def factorial(n):
    if n <= 1:
        return 1
    return n * factorial(n - 1)

print("factorial:", factorial(5))
```

The supported initial grammar includes function definitions and positional parameters, `return`, `if`/`else`, `while`, `pass`, name assignment, calls, numeric and string literals, `True`, `False`, `None`, arithmetic (`+ - * / // % **`), comparisons, boolean operators, unary operators, and parentheses. The built-in `print` accepts multiple values. Lists, `for`, imports, classes, comprehensions, exceptions, and full Python compatibility are not implemented.

## Compiler architecture

- `src/frontend.cpp`: source-position-aware lexer with `NEWLINE`, `INDENT`, and `DEDENT`; AST and parser.
- `src/frontend.hpp`: tokens and AST interface.
- `src/backend.cpp`: AST-to-bytecode compiler, readable bytecode emitter, and bytecode VM.
- `src/backend.hpp`: bytecode and runtime interface.
- `src/main.cpp`: CLI driver (`tokens`, `ast`, `check`, `compile`, `run`).

The existing `src/*.c` and `rolange.py` are early prototypes retained for history/reference; the Makefile builds the C++17 implementation above. Run the focused smoke tests with `make test`.
