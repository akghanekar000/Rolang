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
count = 2
while positive(count):
    if positive(count - 1):
        print(count)
    count = count - 1
""",
            ["2"],
        )

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

    def test_tabs_are_rejected_in_indentation(self):
        self.assert_fails("if True:\n\tpass\n", "tabs are not allowed in indentation", "check")

    def test_delimiters_and_malformed_literals_report_locations(self):
        self.assert_fails("print(1))\n", "unmatched ')'", "check")
        self.assert_fails("print(1\n", "unclosed '('", "check")
        self.assert_fails("print(1.2.3)\n", "malformed numeric literal", "check")
        self.assert_fails('print("bad\\q")\n', "invalid escape sequence", "check")
        self.assert_fails('print("unterminated)\n', "unterminated string literal", "check")

    def test_parser_reports_unexpected_token_and_empty_block(self):
        self.assert_fails("print(,)\n", "expected expression", "check")
        self.assert_fails("if True:\n", "expected an indented block", "check")
        result = self.run_source("print(,)\n", "check")
        self.assertRegex(result.stderr, r"1:\d+: expected expression")

    def test_runtime_errors_include_source_line(self):
        self.assert_fails("print(1 / 0)\n", "line 1: division by zero")
        self.assert_fails("def add(a, b):\n    return a + b\nadd(1)\n", "takes 2 argument(s), got 1")
        self.assert_fails("print(1 % 0)\n", "modulo by zero")


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
