import json
import tempfile
import unittest
from pathlib import Path

import rolange


class RolangeTests(unittest.TestCase):
    def execute(self, source):
        output = []
        program = rolange.compile_source(source)
        rolange.VM(output.append).run(program)
        return output

    def test_arithmetic_and_output(self):
        self.assertEqual(self.execute('let x = 2 + 3 * 4; print(x);'), ['14'])

    def test_function_and_recursion(self):
        source = '''
        fn factorial(n) {
          if (n <= 1) { return 1; }
          return n * factorial(n - 1);
        }
        print(factorial(5));
        '''
        self.assertEqual(self.execute(source), ['120'])

    def test_loop_and_assignment(self):
        self.assertEqual(self.execute('let n = 3; while (n > 0) { print(n); n = n - 1; }'), ['3', '2', '1'])

    def test_compiled_ir_round_trip(self):
        source = 'print("compiled");'
        program = rolange.compile_source(source)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'program.rbc'
            path.write_text(json.dumps(program), encoding='utf-8')
            output = []
            rolange.VM(output.append).run(rolange.load_program(path))
        self.assertEqual(output, ['compiled'])

    def test_undefined_assignment_is_error(self):
        with self.assertRaisesRegex(rolange.RolangeError, "undefined variable 'x'"):
            self.execute('x = 1;')

    def test_syntax_error_has_location(self):
        with self.assertRaisesRegex(rolange.RolangeError, r'1:.*expected'):
            rolange.compile_source('let x = ;')


if __name__ == '__main__':
    unittest.main()
