# Rolang / Rolange

Rolang is a small, dynamically typed programming language. **Rolange** is its compiler front-end and reference virtual machine, implemented using only Python 3's standard library.

## Quick start

```sh
python3 rolange.py run examples/hello.rol
python3 rolange.py check examples/hello.rol
python3 rolange.py compile examples/hello.rol -o hello.rbc
python3 rolange.py run hello.rbc
python3 rolange.py repl
```

`compile` parses source into a portable JSON intermediate representation (`.rbc`); `run` executes source directly or that compiled representation. This is an intentionally small initial compiler/runtime, not a native machine-code compiler.

## Language tour

```rolang
# Variables, arithmetic, strings, and output
let greeting = "Hello, Rolang!";
print(greeting);

fn factorial(n) {
  if (n <= 1) {
    return 1;
  }
  return n * factorial(n - 1);
}

let count = 3;
while (count > 0) {
  print(factorial(count));
  count = count - 1;
}
```

### Syntax and behavior

- Declarations: `let name = expression;` (or `var`)
- Assignment: `name = expression;` (variables must already exist)
- Functions: `fn name(parameter, ...) { ... }`; `return expression;`
- Control flow: `if (condition) { ... } else { ... }`, `while (condition) { ... }`
- Values: numbers, strings, `true`, `false`, and `null`
- Operators: `+ - * / %`, comparisons, `== !=`, `!`/`not`, `and`/`or` (also `&&`/`||`)
- Built-in: `print(value);`
- Comments: `# comment` or `// comment`
- Statements end with `;`; blocks use braces.

Blocks have their own variable scope. Functions capture the environment where they are declared. Values are dynamically typed; arithmetic requires numbers, and `+` also concatenates two strings. Only `false`, `null`, `0`, and the empty string are false-like.

## Development

Run the standard-library tests with:

```sh
python3 -m unittest discover -s tests -v
```

See `examples/` for runnable programs.
