#!/usr/bin/env python3
"""Rolange: compiler front-end and reference VM for the Rolang language."""
from __future__ import annotations

import argparse
import json
import math
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


class RolangeError(Exception):
    """A source, compile, or runtime error with a user-facing message."""


@dataclass
class Token:
    kind: str
    value: Any
    line: int
    column: int


TOKEN_RE = re.compile(
    r"(?P<SPACE>[ \t\r]+)|(?P<NEWLINE>\n)|(?P<COMMENT>\#.*|//[^\n]*)|"
    r"(?P<NUMBER>\d+(?:\.\d+)?)|(?P<STRING>\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*')|"
    r"(?P<IDENT>[A-Za-z_][A-Za-z0-9_]*)|(?P<OP>==|!=|<=|>=|&&|\|\||[+*/%<>=!()-{};,])"
)
KEYWORDS = {"let", "var", "fn", "if", "else", "while", "return", "true", "false", "null", "and", "or", "not"}


def tokenize(source: str) -> list[Token]:
    tokens: list[Token] = []
    pos, line, col = 0, 1, 1
    while pos < len(source):
        match = TOKEN_RE.match(source, pos)
        if not match:
            raise RolangeError(f"{line}:{col}: unexpected character {source[pos]!r}")
        kind, text = match.lastgroup, match.group()
        if kind == "NEWLINE":
            line, col = line + 1, 1
        elif kind in ("SPACE", "COMMENT"):
            col += len(text)
        else:
            if kind == "IDENT" and text in KEYWORDS:
                kind = text.upper()
            if kind == "NUMBER":
                value = float(text) if "." in text else int(text)
            elif kind == "STRING":
                try:
                    value = bytes(text[1:-1], "utf-8").decode("unicode_escape")
                except UnicodeDecodeError as exc:
                    raise RolangeError(f"{line}:{col}: invalid string escape") from exc
            else:
                value = text
            tokens.append(Token(kind, value, line, col))
            col += len(text)
        pos = match.end()
    tokens.append(Token("EOF", None, line, col))
    return tokens


class Parser:
    def __init__(self, tokens: list[Token]):
        self.tokens, self.current = tokens, 0

    def peek(self) -> Token:
        return self.tokens[self.current]

    def advance(self) -> Token:
        token = self.peek()
        if token.kind != "EOF":
            self.current += 1
        return token

    def check(self, *kinds: str) -> bool:
        return self.peek().kind in kinds

    def match(self, *kinds: str) -> bool:
        if self.check(*kinds):
            self.advance()
            return True
        return False

    def expect(self, kind: str, message: str | None = None) -> Token:
        if self.check(kind):
            return self.advance()
        token = self.peek()
        raise RolangeError(f"{token.line}:{token.column}: {message or f'expected {kind.lower()}, got {token.kind.lower()}'}")

    def node(self, kind: str, token: Token, **fields: Any) -> dict[str, Any]:
        return {"type": kind, "line": token.line, **fields}

    def parse(self) -> dict[str, Any]:
        start = self.peek()
        statements = []
        while not self.check("EOF"):
            statements.append(self.statement())
        return self.node("program", start, statements=statements)

    def statement(self) -> dict[str, Any]:
        if self.match("LET", "VAR"):
            token = self.tokens[self.current - 1]
            name = self.expect("IDENT", "expected a variable name")
            self.expect("OP", "expected '=' after variable name")
            if self.tokens[self.current - 1].value != "=":
                raise RolangeError(f"{self.peek().line}:{self.peek().column}: expected '=' after variable name")
            value = self.expression()
            self.expect("OP", "expected ';' after declaration")
            if self.tokens[self.current - 1].value != ";":
                raise RolangeError(f"{self.peek().line}:{self.peek().column}: expected ';' after declaration")
            return self.node("declare", token, name=name.value, value=value)
        if self.match("FN"):
            token = self.tokens[self.current - 1]
            name = self.expect("IDENT", "expected a function name")
            self.expect("OP", "expected '(' after function name")
            if self.tokens[self.current - 1].value != "(":
                raise RolangeError(f"{name.line}:{name.column}: expected '(' after function name")
            params = []
            if not self.is_op(")"):
                while True:
                    params.append(self.expect("IDENT", "expected a parameter name").value)
                    if not self.match_op(","):
                        break
            self.expect_value(")")
            return self.node("function", token, name=name.value, params=params, body=self.block())
        if self.match("IF"):
            token = self.tokens[self.current - 1]
            condition = self.parenthesized_expression()
            then = self.block()
            otherwise = self.block() if self.match("ELSE") else None
            return self.node("if", token, condition=condition, then=then, otherwise=otherwise)
        if self.match("WHILE"):
            token = self.tokens[self.current - 1]
            return self.node("while", token, condition=self.parenthesized_expression(), body=self.block())
        if self.match("RETURN"):
            token = self.tokens[self.current - 1]
            value = None if self.is_op(";") else self.expression()
            self.expect_value(";")
            return self.node("return", token, value=value)
        if self.match_op("{"):
            self.current -= 1
            return self.block()
        if self.check("IDENT") and self.current + 1 < len(self.tokens) and self.tokens[self.current + 1].kind == "OP" and self.tokens[self.current + 1].value == "=":
            name = self.advance()
            self.advance()
            value = self.expression()
            self.expect_value(";")
            return self.node("assign", name, name=name.value, value=value)
        expr = self.expression()
        self.expect_value(";")
        return self.node("expression", self.tokens[self.current - 1], expression=expr)

    def block(self) -> dict[str, Any]:
        start = self.expect_value("{")
        statements = []
        while not self.is_op("}") and not self.check("EOF"):
            statements.append(self.statement())
        self.expect_value("}")
        return self.node("block", start, statements=statements)

    def parenthesized_expression(self) -> dict[str, Any]:
        self.expect_value("(")
        expr = self.expression()
        self.expect_value(")")
        return expr

    def expression(self) -> dict[str, Any]:
        return self.or_expr()

    def binary(self, next_parser, operators: tuple[str, ...]) -> dict[str, Any]:
        expr = next_parser()
        while self.peek().value in operators:
            op = self.advance()
            expr = self.node("binary", op, operator=op.value, left=expr, right=next_parser())
        return expr

    def or_expr(self):
        return self.binary(self.and_expr, ("or", "||"))

    def and_expr(self):
        return self.binary(self.equality, ("and", "&&"))

    def equality(self):
        return self.binary(self.comparison, ("==", "!="))

    def comparison(self):
        return self.binary(self.term, ("<", "<=", ">", ">="))

    def term(self):
        return self.binary(self.factor, ("+", "-"))

    def factor(self):
        return self.binary(self.unary, ("*", "/", "%"))

    def unary(self):
        if self.peek().value in ("!", "not", "-"):
            op = self.advance()
            return self.node("unary", op, operator=op.value, operand=self.unary())
        return self.call()

    def call(self):
        expr = self.primary()
        while self.match_op("("):
            args = []
            if not self.is_op(")"):
                while True:
                    args.append(self.expression())
                    if not self.match_op(","):
                        break
            self.expect_value(")")
            expr = self.node("call", self.tokens[self.current - 1], callee=expr, args=args)
        return expr

    def primary(self):
        token = self.advance()
        if token.kind in ("NUMBER", "STRING"):
            return self.node("literal", token, value=token.value)
        if token.kind == "TRUE":
            return self.node("literal", token, value=True)
        if token.kind == "FALSE":
            return self.node("literal", token, value=False)
        if token.kind == "NULL":
            return self.node("literal", token, value=None)
        if token.kind == "IDENT":
            return self.node("variable", token, name=token.value)
        if token.kind == "OP" and token.value == "(":
            expr = self.expression()
            self.expect_value(")")
            return expr
        raise RolangeError(f"{token.line}:{token.column}: expected an expression")

    def is_op(self, value: str) -> bool:
        return self.peek().kind == "OP" and self.peek().value == value

    def match_op(self, value: str) -> bool:
        if self.is_op(value):
            self.advance()
            return True
        return False

    def expect_value(self, value: str) -> Token:
        token = self.peek()
        if token.kind == "OP" and token.value == value:
            return self.advance()
        raise RolangeError(f"{token.line}:{token.column}: expected {value!r}")


def compile_source(source: str) -> dict[str, Any]:
    """Compile source into Rolange's portable JSON intermediate representation."""
    return Parser(tokenize(source)).parse()


class Environment:
    def __init__(self, parent: Environment | None = None):
        self.values: dict[str, Any] = {}
        self.parent = parent

    def get(self, name: str) -> Any:
        if name in self.values:
            return self.values[name]
        if self.parent:
            return self.parent.get(name)
        raise RolangeError(f"undefined variable '{name}'")

    def assign(self, name: str, value: Any) -> None:
        if name in self.values:
            self.values[name] = value
        elif self.parent:
            self.parent.assign(name, value)
        else:
            raise RolangeError(f"undefined variable '{name}'")


@dataclass
class UserFunction:
    params: list[str]
    body: dict[str, Any]
    closure: Environment


class ReturnSignal(Exception):
    def __init__(self, value: Any):
        self.value = value


class VM:
    def __init__(self, output=print):
        self.output = output
        self.globals = Environment()
        self.globals.values["print"] = "<builtin print>"

    def run(self, program: dict[str, Any]) -> Any:
        try:
            self.execute(program, self.globals)
        except ReturnSignal:
            raise RolangeError("return used outside a function")
        return None

    def execute(self, stmt: dict[str, Any], env: Environment) -> None:
        kind = stmt["type"]
        try:
            if kind == "program" or kind == "block":
                scope = Environment(env) if kind == "block" else env
                for child in stmt["statements"]:
                    self.execute(child, scope)
            elif kind == "declare":
                if stmt["name"] in env.values:
                    raise RolangeError(f"variable '{stmt['name']}' is already declared")
                env.values[stmt["name"]] = self.evaluate(stmt["value"], env)
            elif kind == "assign":
                env.assign(stmt["name"], self.evaluate(stmt["value"], env))
            elif kind == "function":
                if stmt["name"] in env.values:
                    raise RolangeError(f"'{stmt['name']}' is already declared")
                env.values[stmt["name"]] = UserFunction(stmt["params"], stmt["body"], env)
            elif kind == "expression":
                self.evaluate(stmt["expression"], env)
            elif kind == "if":
                branch = stmt["then"] if self.truthy(self.evaluate(stmt["condition"], env)) else stmt["otherwise"]
                if branch:
                    self.execute(branch, env)
            elif kind == "while":
                while self.truthy(self.evaluate(stmt["condition"], env)):
                    self.execute(stmt["body"], env)
            elif kind == "return":
                raise ReturnSignal(self.evaluate(stmt["value"], env) if stmt["value"] else None)
            else:
                raise RolangeError(f"unknown statement '{kind}'")
        except RolangeError as exc:
            if str(exc).startswith("line "):
                raise
            raise RolangeError(f"line {stmt.get('line', '?')}: {exc}") from exc

    def evaluate(self, expr: dict[str, Any], env: Environment) -> Any:
        kind = expr["type"]
        try:
            if kind == "literal":
                return expr["value"]
            if kind == "variable":
                return env.get(expr["name"])
            if kind == "unary":
                value = self.evaluate(expr["operand"], env)
                if expr["operator"] in ("!", "not"):
                    return not self.truthy(value)
                return -self.number(value)
            if kind == "binary":
                op = expr["operator"]
                left = self.evaluate(expr["left"], env)
                if op in ("and", "&&"):
                    return self.truthy(left) and self.truthy(self.evaluate(expr["right"], env))
                if op in ("or", "||"):
                    return self.truthy(left) or self.truthy(self.evaluate(expr["right"], env))
                right = self.evaluate(expr["right"], env)
                if op == "+":
                    if isinstance(left, str) and isinstance(right, str):
                        return left + right
                    return self.number(left) + self.number(right)
                if op == "-": return self.number(left) - self.number(right)
                if op == "*": return self.number(left) * self.number(right)
                if op == "/":
                    if self.number(right) == 0: raise RolangeError("division by zero")
                    return self.number(left) / self.number(right)
                if op == "%": return self.number(left) % self.number(right)
                if op == "==": return left == right
                if op == "!=": return left != right
                if op == "<": return left < right
                if op == "<=": return left <= right
                if op == ">": return left > right
                if op == ">=": return left >= right
            if kind == "call":
                callee = self.evaluate(expr["callee"], env)
                args = [self.evaluate(arg, env) for arg in expr["args"]]
                if callee == "<builtin print>":
                    if len(args) != 1: raise RolangeError("print expects exactly one argument")
                    self.output(self.display(args[0]))
                    return None
                if not isinstance(callee, UserFunction):
                    raise RolangeError("can only call functions")
                if len(args) != len(callee.params):
                    raise RolangeError(f"expected {len(callee.params)} arguments, got {len(args)}")
                local = Environment(callee.closure)
                local.values.update(zip(callee.params, args))
                try:
                    self.execute(callee.body, local)
                except ReturnSignal as signal:
                    return signal.value
                return None
            raise RolangeError(f"unknown expression '{kind}'")
        except RolangeError as exc:
            if str(exc).startswith("line "):
                raise
            raise RolangeError(f"line {expr.get('line', '?')}: {exc}") from exc
        except (TypeError, ValueError, ZeroDivisionError) as exc:
            raise RolangeError(f"line {expr.get('line', '?')}: invalid operation: {exc}") from exc

    @staticmethod
    def number(value: Any) -> int | float:
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise RolangeError("expected a number")
        return value

    @staticmethod
    def truthy(value: Any) -> bool:
        return value is not None and value is not False and value != 0 and value != ""

    @staticmethod
    def display(value: Any) -> str:
        if value is True: return "true"
        if value is False: return "false"
        if value is None: return "null"
        if isinstance(value, float) and value.is_integer(): return str(int(value))
        return str(value)


def load_program(path: Path) -> dict[str, Any]:
    text = path.read_text(encoding="utf-8")
    if path.suffix == ".rbc":
        try:
            program = json.loads(text)
        except json.JSONDecodeError as exc:
            raise RolangeError(f"invalid compiled file: {exc}") from exc
        if not isinstance(program, dict) or program.get("type") != "program":
            raise RolangeError("invalid compiled file: expected a program")
        return program
    return compile_source(text)


def repl() -> int:
    vm = VM()
    print("Rolange REPL — enter an expression or statement; Ctrl-D to exit")
    while True:
        try:
            line = input("rolange> ")
        except EOFError:
            print()
            return 0
        if not line.strip():
            continue
        try:
            program = compile_source(line)
            vm.run(program)
        except RolangeError as exc:
            print(f"error: {exc}", file=sys.stderr)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="rolange", description="Compile and run Rolang programs.")
    commands = parser.add_subparsers(dest="command")
    compile_cmd = commands.add_parser("compile", help="compile .rol source to portable .rbc IR")
    compile_cmd.add_argument("source", type=Path)
    compile_cmd.add_argument("-o", "--output", type=Path)
    run_cmd = commands.add_parser("run", help="run a .rol source or compiled .rbc program")
    run_cmd.add_argument("source", type=Path)
    check_cmd = commands.add_parser("check", help="check source syntax without running it")
    check_cmd.add_argument("source", type=Path)
    commands.add_parser("repl", help="start the interactive interpreter")
    args = parser.parse_args(argv)
    try:
        if args.command == "repl" or args.command is None:
            return repl()
        if args.command == "compile":
            program = compile_source(args.source.read_text(encoding="utf-8"))
            output = args.output or args.source.with_suffix(".rbc")
            output.write_text(json.dumps(program, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
            print(f"compiled {args.source} -> {output}")
        elif args.command == "check":
            compile_source(args.source.read_text(encoding="utf-8"))
            print(f"{args.source}: syntax OK")
        elif args.command == "run":
            VM().run(load_program(args.source))
        return 0
    except (RolangeError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
