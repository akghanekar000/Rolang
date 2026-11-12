#ifndef ROLANGE_BACKEND_HPP
#define ROLANGE_BACKEND_HPP

#include "frontend.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace rolang {
struct Code;
struct Function { std::shared_ptr<Code> code; };
struct BuiltinPrint {};
enum class ValueType { Null, Int64, Float64, Boolean, String, Function, BuiltinFunction };

class Value {
public:
    Value() = default;
    static Value null();
    static Value int64(std::int64_t value);
    static Value float64(double value);
    static Value boolean(bool value);
    static Value string(std::string value);
    static Value function(std::shared_ptr<Function> value);
    static Value builtin_print();

    ValueType type() const noexcept;
    const std::int64_t* get_int64() const noexcept;
    const double* get_float64() const noexcept;
    const bool* get_boolean() const noexcept;
    const std::string* get_string() const noexcept;
    const std::shared_ptr<Function>* get_function() const noexcept;

private:
    using Storage = std::variant<std::monostate, std::int64_t, double, bool,
                                 std::string, std::shared_ptr<Function>, BuiltinPrint>;
    explicit Value(Storage storage) : storage_(std::move(storage)) {}
    Storage storage_{};
    friend class ValueOperations;
};

class ValueOperations {
public:
    static bool truthy(const Value& value);
    static bool equal(const Value& left, const Value& right);
    static std::string display(const Value& value);
    static Value unary(const std::string& op, const Value& operand);
    static Value binary(const std::string& op, const Value& left, const Value& right);
};

enum class Op { Constant, Load, Store, Pop, Unary, Binary, ToBool, JumpFalse, Jump, MakeFunction, Call, Return, Nop };
struct Instruction {
    Op op;
    Value value{};
    std::string text;
    int target = 0;
    int line = 1;
    std::shared_ptr<Code> function_code;
    int column = 1;
};
struct Code { std::string name; std::vector<std::string> params; std::vector<Instruction> instructions; };
Code compile(const Node& root);
std::string disassemble(const Code& code);
class VM {
public:
    explicit VM(bool show_output = true) : show_output_(show_output) {}
    void run(const Code& code);
    const std::vector<std::string>& output() const noexcept { return output_; }
private:
    bool show_output_;
    std::size_t steps_ = 0;
    std::vector<std::string> output_;
    Value execute(const Code& code, std::unordered_map<std::string, Value>& globals, std::unordered_map<std::string, Value> locals, bool module, std::size_t call_depth);
};
} // namespace rolang
#endif
