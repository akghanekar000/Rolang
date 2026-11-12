#include "backend.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::string> run(const std::string& source) {
    const rolang::Node tree = rolang::parse(rolang::tokenize(source));
    const rolang::Code code = rolang::compile(tree);
    rolang::VM vm(false);
    vm.run(code);
    return vm.output();
}

void require_error(const std::string& source, const std::string& expected) {
    try {
        (void)run(source);
    } catch (const std::exception& error) {
        require(std::string(error.what()).find(expected) != std::string::npos,
                "expected error containing '" + expected + "', got '" + error.what() + "'");
        return;
    }
    throw std::runtime_error("expected runtime error containing '" + expected + "'");
}

void test_compiled_literal_tags() {
    const auto code = rolang::compile(rolang::parse(rolang::tokenize(
        "print(4)\nprint(4.0)\nprint(4e0)\n")));
    std::vector<rolang::ValueType> types;
    for (const auto& instruction : code.instructions) {
        if (instruction.op == rolang::Op::Constant &&
            (instruction.value.type() == rolang::ValueType::Int64 ||
             instruction.value.type() == rolang::ValueType::Float64))
            types.push_back(instruction.value.type());
    }
    require(types.size() == 3, "expected three numeric bytecode constants");
    require(types[0] == rolang::ValueType::Int64, "integer spelling must compile as Int64");
    require(types[1] == rolang::ValueType::Float64, "decimal spelling must compile as Float64");
    require(types[2] == rolang::ValueType::Float64, "exponent spelling must compile as Float64");
}

void test_numeric_runtime_operations() {
    const auto output = run(
        "print(9223372036854775807)\n"
        "print(-9223372036854775808)\n"
        "print(1 + 2)\n"
        "print(1.5 + 2.25)\n"
        "print(1 + 0.5)\n"
        "print(-7 // 3)\n"
        "print(-7 % 3)\n"
        "print(2 ** 10)\n"
        "print(2 ** -2)\n"
        "print(1 / 2)\n");
    require(output == std::vector<std::string>{
        "9223372036854775807", "-9223372036854775808", "3", "3.75", "1.5",
        "-3", "2", "1024", "0.25", "0.5"},
        "numeric operations returned unexpected values");
}

void test_comparisons_booleans_strings_null_and_calls() {
    const auto output = run(
        "def add(a, b):\n"
        "    return a + b\n"
        "print(1 < 2, 1 == 1.0, True == 1)\n"
        "print(True and not False, False or True)\n"
        "print(True + 1)\n"
        "print(9007199254740993 == 9007199254740992.0)\n"
        "print(\"rol\" + \"ang\")\n"
        "print(None, not None, None == None)\n"
        "print(add(19, 23))\n"
        "def same():\n"
        "    return None\n"
        "def other():\n"
        "    return None\n"
        "print(same == same, same == other)\n"
        "def empty():\n"
        "    return\n"
        "print(empty())\n");
    require(output == std::vector<std::string>{
        "True True True", "True True", "2", "True", "rolang", "None True True",
        "42", "True False", "None"},
        "runtime value behavior or function return value was incorrect");
}

void test_checked_errors() {
    require_error("print(9223372036854775807 + 1)\n", "integer overflow");
    require_error("print(-9223372036854775808 - 1)\n", "integer overflow");
    require_error("print(-(-9223372036854775808))\n", "integer overflow");
    require_error("print(3037000500 * 3037000500)\n", "integer overflow");
    require_error("print(2 ** 63)\n", "integer overflow");
    require_error("print(1 // 0)\n", "division by zero");
    require_error("print(1 % 0)\n", "modulo by zero");
    require_error("print(1.0 / 0.0)\n", "division by zero");
    require_error("print(-9223372036854775808 // -1)\n", "integer overflow");
    require_error("print(9223372036854775808)\n", "integer literal out of range");
    require_error("print(1e308 * 1e308)\n", "arithmetic result is not finite");
}

} // namespace

int main() {
    try {
        test_compiled_literal_tags();
        test_numeric_runtime_operations();
        test_comparisons_booleans_strings_null_and_calls();
        test_checked_errors();
    } catch (const std::exception& error) {
        std::cerr << "runtime test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
