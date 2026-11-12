import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


COMPILER = Path(sys.argv.pop(1)).resolve()


class CppCompilerTests(unittest.TestCase):
    def run_source(self, source, command="run"):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "program.rol"
            path.write_text(source, encoding="utf-8")
            return subprocess.run(
                [str(COMPILER), command, str(path)],
                text=True,
                capture_output=True,
                check=False,
            )

    def assert_runs(self, source, expected):
        result = self.run_source(source)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(), expected)

    def assert_fails(self, source, message, command="run"):
        result = self.run_source(source, command)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(message, result.stderr)

    def test_calls_nested_calls_recursion_and_argument_order(self):
        self.assert_runs(
            """\
def combine(a, b):
    return a * 10 + b
def factorial(n):
    if n <= 1:
        return 1
    return n * factorial(n - 1)
print(combine(combine(1, 2), combine(3, 4)))
print(factorial(5))
""",
            ["154", "120"],
        )

    def test_multiline_call_and_trailing_commas(self):
        self.assert_runs(
            """\
def add(a, b,):
    return a + b
print(
    add(2, 3,),
)
""",
            ["5"],
        )

    def test_function_frames_keep_locals_separate_from_globals(self):
        self.assert_runs(
            """\
value = 10
def increment(value):
    value = value + 1
    return value
print(increment(2))
print(value)
""",
            ["3", "10"],
        )

    def test_calls_inside_branches_and_loops_keep_jump_targets_valid(self):
        self.assert_runs(
            """\
def positive(value):
    return value > 0
def identity(value):
    return value
count = 2
while positive(identity(count)):
    if positive(identity(count - 1)):
        print(count)
    count = count - 1
""",
            ["2"],
        )

    def test_normal_deep_and_infinite_recursion(self):
        self.assert_runs(
            """\
def recurse(n):
    if n == 0:
        return 0
    return recurse(n - 1)
print(recurse(20))
""",
            ["0"],
        )
        self.assert_fails(
            "def recurse(n):\n    return recurse(n - 1)\nrecurse(500)\n",
            "maximum call depth exceeded",
        )
        self.assert_fails(
            "def recurse():\n    return recurse()\nrecurse()\n",
            "maximum call depth exceeded",
        )

    def test_instruction_limit_still_stops_non_recursive_loops(self):
        self.assert_fails("while True:\n    pass\n", "execution limit exceeded")

    def test_short_circuit_skips_unselected_operand(self):
        self.assert_runs(
            """\
print(False and missing_name)
print(True or missing_name)
print(0 and missing_name)
print(1 or missing_name)
""",
            ["False", "True", "False", "True"],
        )

    def test_and_or_short_circuit_and_return_booleans(self):
        self.assert_runs(
            """\
print(0 and missing_name)
print(1 or missing_name)
print("left" and "right")
print("" or "fallback")
""",
            ["False", "True", "True", "True"],
        )

    def test_mixed_boolean_and_comparison_precedence(self):
        self.assert_runs(
            """\
print(not 1 == 2 and 3 < 4 or False)
print(not (1 < 2) or 3 == 3 and False)
""",
            ["True", "False"],
        )

    def test_print_marker_collision_and_function_shadowing(self):
        self.assert_runs(
            """\
print("__rolang_builtin_print__")
def print(value):
    return value + 1
print(print(4))
""",
            ["__rolang_builtin_print__"],
        )
        self.assert_fails(
            'ordinary = "__rolang_builtin_print__"\nordinary()\n',
            "object is not callable",
        )

    def test_return_is_only_valid_inside_functions(self):
        self.assert_fails("return 1\n", "return outside a function", "check")
        self.assert_fails("if True:\n    return 1\n", "return outside a function", "check")
        self.assert_runs(
            """\
def choose():
    if True:
        return 7
    return 0
print(choose())
""",
            ["7"],
        )

    def test_check_reports_semantic_errors_with_source_locations(self):
        result = self.run_source("print(missing)\n", "check")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertRegex(
            result.stderr,
            r"program\.rol:1:7: semantic error\[undefined-name\]: undefined name 'missing'",
        )

        result = self.run_source(
            "def add(a, b):\n    return a + b\nadd(1)\n",
            "check",
        )
        self.assertEqual(result.returncode, 1)
        self.assertRegex(
            result.stderr,
            r"program\.rol:3:1: semantic error\[argument-count\]: "
            r"function 'add' expects 2 arguments, got 1",
        )
        self.assertRegex(result.stderr, r"program\.rol:1:1: note: function declared here")

    def test_check_accepts_nested_functions_and_valid_variable_usage(self):
        result = self.run_source(
            "value = 3\n"
            "def outer(parameter):\n"
            "    def inner(argument):\n"
            "        return argument\n"
            "    return inner(parameter)\n"
            "print(outer(value))\n",
            "check",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.endswith("/program.rol: syntax OK\n"))

    def test_semantic_check_does_not_gate_run_or_compile(self):
        source = "def add(a, b):\n    return a + b\nadd(1)\n"
        checked = self.run_source(source, "check")
        self.assertNotEqual(checked.returncode, 0)

        executed = self.run_source(source)
        self.assertNotEqual(executed.returncode, 0)
        self.assertIn("takes 2 argument(s), got 1", executed.stderr)

        compiled = self.run_source(source, "compile")
        self.assertEqual(compiled.returncode, 0, compiled.stderr)
        self.assertIn("compiled ", compiled.stdout)

    def test_not_has_lower_precedence_than_comparisons(self):
        self.assert_runs(
            """\
print(not 1 == 2)
print(not 1 < 2)
print(-2 ** 2)
""",
            ["True", "False", "-4"],
        )

    def test_equality_compares_values_without_stringification(self):
        self.assert_runs(
            """\
print(None == "None")
print(1 == "1")
print(True == "True")
print(True == 1)
""",
            ["False", "False", "False", "True"],
        )

    def test_supported_string_escapes(self):
        self.assert_runs(
            r'''print("quote: \" slash: \\")''' + "\n",
            ['quote: " slash: \\'],
        )
        self.assert_runs(
            r'''print("line one\nline two\ttab")''' + "\n",
            ["line one", "line two\ttab"],
        )

    def test_tabs_are_rejected_in_indentation(self):
        self.assert_fails("if True:\n\tpass\n", "tabs are not allowed in indentation", "check")

    def test_inconsistent_indentation_and_mixed_tabs_and_spaces(self):
        self.assert_fails(
            "if True:\n    pass\n  pass\n",
            "inconsistent indentation",
            "check",
        )
        self.assert_fails(
            "if True:\n \tpass\n",
            "tabs are not allowed in indentation",
            "check",
        )

    def test_delimiters_and_malformed_literals_report_locations(self):
        self.assert_fails("print(1))\n", "unmatched ')'", "check")
        self.assert_fails("print(1\n", "unclosed '('", "check")
        self.assert_fails("print(1.2.3)\n", "malformed numeric literal", "check")
        self.assert_fails("print(1e)\n", "malformed numeric literal", "check")
        self.assert_fails("print(1e+)\n", "malformed numeric literal", "check")
        self.assert_fails('print("bad\\q")\n', "invalid escape sequence", "check")
        self.assert_fails('print("incomplete\\\n', "incomplete escape sequence", "check")
        self.assert_fails('print("unterminated)\n', "unterminated string literal", "check")
        self.assert_fails("def duplicate(value, value):\n    pass\n", "duplicate parameter", "check")

    def test_parser_reports_unexpected_token_and_empty_block(self):
        self.assert_fails("print(,)\n", "expected expression", "check")
        self.assert_fails("if True:\n", "expected an indented block", "check")
        result = self.run_source("print(,)\n", "check")
        self.assertRegex(result.stderr, r"1:7: expected expression")
        result = self.run_source("print(1)\nprint(,)\n", "check")
        self.assertRegex(result.stderr, r"2:7: expected expression")

    def test_runtime_errors_include_source_line(self):
        self.assert_fails("print(1 / 0)\n", "line 1:9: division by zero")
        self.assert_fails("def add(a, b):\n    return a + b\nadd(1)\n", "takes 2 argument(s), got 1")
        self.assert_fails("print(1 % 0)\n", "modulo by zero")
        result = self.run_source("print(1)\nprint(2 / 0)\n")
        self.assertRegex(result.stderr, r"line 2:9: division by zero")

    def test_non_finite_arithmetic_results_are_errors(self):
        for expression in (
            "1e308 + 1e308",
            "-1e308 - 1e308",
            "1e308 * 1e308",
            "1 / 1e-320",
            "(-1) ** 0.5",
            "10 ** 10000",
        ):
            with self.subTest(expression=expression):
                self.assert_fails(
                    f"print({expression})\n",
                    "arithmetic result is not finite",
                )


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
