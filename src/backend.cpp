#include "backend.hpp"

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace rolang {

Value Value::null() { return Value{}; }
Value Value::int64(std::int64_t value) { return Value{Storage{value}}; }
Value Value::float64(double value) {
    if (!std::isfinite(value)) throw std::runtime_error("arithmetic result is not finite");
    return Value{Storage{value}};
}
Value Value::boolean(bool value) { return Value{Storage{value}}; }
Value Value::string(std::string value) { return Value{Storage{std::move(value)}}; }
Value Value::function(std::shared_ptr<Function> value) {
    if (!value) throw std::runtime_error("function value cannot be null");
    return Value{Storage{std::move(value)}};
}
Value Value::builtin_print() { return Value{Storage{BuiltinPrint{}}}; }

ValueType Value::type() const noexcept {
    if (std::holds_alternative<std::monostate>(storage_)) return ValueType::Null;
    if (std::holds_alternative<std::int64_t>(storage_)) return ValueType::Int64;
    if (std::holds_alternative<double>(storage_)) return ValueType::Float64;
    if (std::holds_alternative<bool>(storage_)) return ValueType::Boolean;
    if (std::holds_alternative<std::string>(storage_)) return ValueType::String;
    if (std::holds_alternative<std::shared_ptr<Function>>(storage_)) return ValueType::Function;
    return ValueType::BuiltinFunction;
}
const std::int64_t* Value::get_int64() const noexcept { return std::get_if<std::int64_t>(&storage_); }
const double* Value::get_float64() const noexcept { return std::get_if<double>(&storage_); }
const bool* Value::get_boolean() const noexcept { return std::get_if<bool>(&storage_); }
const std::string* Value::get_string() const noexcept { return std::get_if<std::string>(&storage_); }
const std::shared_ptr<Function>* Value::get_function() const noexcept { return std::get_if<std::shared_ptr<Function>>(&storage_); }

namespace {
constexpr std::size_t max_call_depth = 128;
constexpr std::size_t max_instructions = 1'000'000;

class Generator {
public:
    std::shared_ptr<Code> generate(const Node& root, const std::string& name = "<module>", std::vector<std::string> params = {}) {
        code_ = std::make_shared<Code>(); code_->name = name; code_->params = std::move(params);
        statements(root); emit(Op::Constant, {}, "", 0, root.line, {}, root.column); emit(Op::Return, {}, "", 0, root.line, {}, root.column); return code_;
    }
private:
    std::shared_ptr<Code> code_;
    int emit(Op op, Value value = {}, std::string text = {}, int target = 0, int line = 1, std::shared_ptr<Code> nested = {}, int column = 1) {
        code_->instructions.push_back({op, std::move(value), std::move(text), target, line, std::move(nested), column}); return static_cast<int>(code_->instructions.size()-1);
    }
    void patch(int i, int target) { code_->instructions.at(static_cast<std::size_t>(i)).target = target; }
    void statements(const Node& node) { for (const auto& child : node.children) statement(child); }
    void statement(const Node& n) {
        switch(n.kind) {
            case NodeKind::Program: case NodeKind::Block: statements(n); break;
            case NodeKind::Function: {
                Generator nested; auto fn = nested.generate(n.children.at(0), n.text, n.params);
                emit(Op::MakeFunction, {}, n.text, 0, n.line, fn, n.column); emit(Op::Store, {}, n.text, 0, n.line, {}, n.column); break;
            }
            case NodeKind::Assign: expression(n.children.at(0)); emit(Op::Store, {}, n.text, 0, n.line, {}, n.column); break;
            case NodeKind::Expression: expression(n.children.at(0)); emit(Op::Pop, {}, "", 0, n.line, {}, n.column); break;
            case NodeKind::Return:
                if (n.children.empty()) emit(Op::Constant, Value::null(), "", 0, n.line, {}, n.column); else expression(n.children.at(0));
                emit(Op::Return, {}, "", 0, n.line, {}, n.column); break;
            case NodeKind::Pass: emit(Op::Nop, {}, "", 0, n.line, {}, n.column); break;
            case NodeKind::If: {
                expression(n.children.at(0)); int no = emit(Op::JumpFalse, {}, "", 0, n.line, {}, n.column); statement(n.children.at(1));
                if (n.children.size() > 2) { int end = emit(Op::Jump, {}, "", 0, n.line, {}, n.column); patch(no, static_cast<int>(code_->instructions.size())); statement(n.children.at(2)); patch(end, static_cast<int>(code_->instructions.size())); }
                else patch(no, static_cast<int>(code_->instructions.size()));
                break;
            }
            case NodeKind::While: {
                int start = static_cast<int>(code_->instructions.size()); expression(n.children.at(0)); int end = emit(Op::JumpFalse, {}, "", 0, n.line, {}, n.column);
                statement(n.children.at(1)); emit(Op::Jump, {}, "", start, n.line, {}, n.column); patch(end, static_cast<int>(code_->instructions.size())); break;
            }
            default: throw std::runtime_error("invalid statement node");
        }
    }
    void expression(const Node& n) {
        switch(n.kind) {
            case NodeKind::Literal:
                if (n.string_literal) emit(Op::Constant, Value::string(n.text), "", 0, n.line);
                else if (n.text == "True") emit(Op::Constant, Value::boolean(true), "", 0, n.line);
                else if (n.text == "False") emit(Op::Constant, Value::boolean(false), "", 0, n.line);
                else if (n.text == "None") emit(Op::Constant, Value::null(), "", 0, n.line);
                else if (n.integer_literal) emit(Op::Constant, Value::int64(n.integer), "", 0, n.line);
                else emit(Op::Constant, Value::float64(n.number), "", 0, n.line);
                break;
            case NodeKind::Name: emit(Op::Load, {}, n.text, 0, n.line, {}, n.column); break;
            case NodeKind::Unary: expression(n.children.at(0)); emit(Op::Unary, {}, n.text, 0, n.line, {}, n.column); break;
            case NodeKind::Binary:
                if (n.text == "and") {
                    expression(n.children.at(0));
                    const int false_branch = emit(Op::JumpFalse, {}, "", 0, n.line, {}, n.column);
                    expression(n.children.at(1));
                    emit(Op::ToBool, {}, "", 0, n.line, {}, n.column);
                    const int end_branch = emit(Op::Jump, {}, "", 0, n.line, {}, n.column);
                    patch(false_branch, static_cast<int>(code_->instructions.size()));
                    emit(Op::Constant, Value::boolean(false), "", 0, n.line, {}, n.column);
                    patch(end_branch, static_cast<int>(code_->instructions.size()));
                } else if (n.text == "or") {
                    expression(n.children.at(0));
                    const int right_branch = emit(Op::JumpFalse, {}, "", 0, n.line, {}, n.column);
                    emit(Op::Constant, Value::boolean(true), "", 0, n.line, {}, n.column);
                    const int end_branch = emit(Op::Jump, {}, "", 0, n.line, {}, n.column);
                    patch(right_branch, static_cast<int>(code_->instructions.size()));
                    expression(n.children.at(1));
                    emit(Op::ToBool, {}, "", 0, n.line, {}, n.column);
                    patch(end_branch, static_cast<int>(code_->instructions.size()));
                } else {
                    expression(n.children.at(0));
                    expression(n.children.at(1));
                    emit(Op::Binary, {}, n.text, 0, n.line, {}, n.column);
                }
                break;
            case NodeKind::Call:
                if (n.children.empty()) throw std::runtime_error("call node has no callee");
                expression(n.children.at(0));
                for (std::size_t i = 1; i < n.children.size(); ++i) expression(n.children[i]);
                emit(Op::Call, {}, "", static_cast<int>(n.children.size()-1), n.line, {}, n.column);
                break;
            default: throw std::runtime_error("invalid expression node");
        }
    }
};

bool is_integer_like(const Value& value) {
    return value.type() == ValueType::Int64 || value.type() == ValueType::Boolean;
}
bool is_numeric(const Value& value) {
    return is_integer_like(value) || value.type() == ValueType::Float64;
}
std::int64_t integer_value(const Value& value) {
    if (const auto* integer = value.get_int64()) return *integer;
    if (const auto* boolean = value.get_boolean()) return *boolean ? 1 : 0;
    throw std::runtime_error("numeric operation requires numbers");
}
double numeric_value(const Value& value) {
    if (const auto* floating = value.get_float64()) return *floating;
    if (is_integer_like(value)) return static_cast<double>(integer_value(value));
    throw std::runtime_error("numeric operation requires numbers");
}
double finite_result(double value) {
    if (!std::isfinite(value)) throw std::runtime_error("arithmetic result is not finite");
    return value;
}
std::int64_t checked_add(std::int64_t left, std::int64_t right) {
    constexpr auto low = std::numeric_limits<std::int64_t>::min();
    constexpr auto high = std::numeric_limits<std::int64_t>::max();
    if ((right > 0 && left > high - right) || (right < 0 && left < low - right))
        throw std::runtime_error("integer overflow");
    return left + right;
}
std::int64_t checked_subtract(std::int64_t left, std::int64_t right) {
    constexpr auto low = std::numeric_limits<std::int64_t>::min();
    constexpr auto high = std::numeric_limits<std::int64_t>::max();
    if ((right < 0 && left > high + right) || (right > 0 && left < low + right))
        throw std::runtime_error("integer overflow");
    return left - right;
}
std::int64_t checked_multiply(std::int64_t left, std::int64_t right) {
    constexpr auto low = std::numeric_limits<std::int64_t>::min();
    constexpr auto high = std::numeric_limits<std::int64_t>::max();
    if (left > 0) {
        if ((right > 0 && left > high / right) || (right < 0 && right < low / left))
            throw std::runtime_error("integer overflow");
    } else if (left < 0) {
        if ((right > 0 && left < low / right) || (right < 0 && right < high / left))
            throw std::runtime_error("integer overflow");
    }
    return left * right;
}
std::int64_t floor_quotient(std::int64_t left, std::int64_t right) {
    if (right == 0) throw std::runtime_error("division by zero");
    if (left == std::numeric_limits<std::int64_t>::min() && right == -1)
        throw std::runtime_error("integer overflow");
    std::int64_t quotient = left / right;
    const std::int64_t remainder = left % right;
    if (remainder != 0 && ((remainder < 0) != (right < 0))) --quotient;
    return quotient;
}
std::int64_t floor_remainder(std::int64_t left, std::int64_t right) {
    if (right == 0) throw std::runtime_error("modulo by zero");
    if (left == std::numeric_limits<std::int64_t>::min() && right == -1) return 0;
    std::int64_t remainder = left % right;
    if (remainder != 0 && ((remainder < 0) != (right < 0))) remainder += right;
    return remainder;
}
std::int64_t integer_power(std::int64_t base, std::uint64_t exponent) {
    std::int64_t result = 1;
    while (exponent != 0) {
        if ((exponent & 1U) != 0) result = checked_multiply(result, base);
        exponent >>= 1U;
        if (exponent != 0) base = checked_multiply(base, base);
    }
    return result;
}
std::string op_name(Op op) {
    switch(op) { case Op::Constant:return "CONST"; case Op::Load:return "LOAD"; case Op::Store:return "STORE"; case Op::Pop:return "POP"; case Op::Unary:return "UNARY"; case Op::Binary:return "BINARY"; case Op::ToBool:return "TO_BOOL"; case Op::JumpFalse:return "JUMP_IF_FALSE"; case Op::Jump:return "JUMP"; case Op::MakeFunction:return "MAKE_FUNCTION"; case Op::Call:return "CALL"; case Op::Return:return "RETURN"; case Op::Nop:return "NOP"; } return "?";
}
void validate_code(const Code& code) {
    for (const auto& ins : code.instructions) {
        auto invalid = [&](const std::string& reason) {
            throw std::runtime_error("line " + std::to_string(ins.line) + ":" + std::to_string(ins.column) + ": " + reason);
        };
        switch (ins.op) {
            case Op::Jump:
            case Op::JumpFalse:
                if (ins.target < 0 || static_cast<std::size_t>(ins.target) > code.instructions.size())
                    invalid("invalid jump target");
                break;
            case Op::Call:
                if (ins.target < 0) invalid("invalid argument count in bytecode");
                break;
            case Op::MakeFunction:
                if (!ins.function_code) invalid("function has no bytecode");
                break;
            case Op::Constant: case Op::Load: case Op::Store: case Op::Pop:
            case Op::Unary: case Op::Binary: case Op::ToBool: case Op::Return:
            case Op::Nop:
                break;
            default:
                invalid("invalid opcode in bytecode");
        }
    }
}
} // namespace

bool ValueOperations::truthy(const Value& value) {
    switch (value.type()) {
        case ValueType::Null: return false;
        case ValueType::Boolean: return *value.get_boolean();
        case ValueType::Int64: return *value.get_int64() != 0;
        case ValueType::Float64: return *value.get_float64() != 0.0;
        case ValueType::String: return !value.get_string()->empty();
        case ValueType::Function:
        case ValueType::BuiltinFunction: return true;
    }
    return false;
}

bool ValueOperations::equal(const Value& left, const Value& right) {
    if (is_numeric(left) && is_numeric(right)) {
        if (is_integer_like(left) && is_integer_like(right))
            return integer_value(left) == integer_value(right);
        return numeric_value(left) == numeric_value(right);
    }
    if (left.type() != right.type()) return false;
    switch (left.type()) {
        case ValueType::Null: return true;
        case ValueType::Int64: return *left.get_int64() == *right.get_int64();
        case ValueType::Float64: return *left.get_float64() == *right.get_float64();
        case ValueType::Boolean: return *left.get_boolean() == *right.get_boolean();
        case ValueType::String: return *left.get_string() == *right.get_string();
        case ValueType::Function: return *left.get_function() == *right.get_function();
        case ValueType::BuiltinFunction: return true;
    }
    return false;
}

std::string ValueOperations::display(const Value& value) {
    switch (value.type()) {
        case ValueType::Null: return "None";
        case ValueType::Boolean: return *value.get_boolean() ? "True" : "False";
        case ValueType::Int64: return std::to_string(*value.get_int64());
        case ValueType::Float64: {
            std::ostringstream stream;
            stream << std::setprecision(15) << *value.get_float64();
            return stream.str();
        }
        case ValueType::String: return *value.get_string();
        case ValueType::BuiltinFunction: return "<built-in print>";
        case ValueType::Function: return "<function>";
    }
    return {};
}

Value ValueOperations::unary(const std::string& op, const Value& operand) {
    if (op == "not") return Value::boolean(!truthy(operand));
    if (op != "+" && op != "-") throw std::runtime_error("unknown unary operator " + op);
    if (!is_numeric(operand)) throw std::runtime_error("numeric operation requires numbers");
    if (operand.type() == ValueType::Float64) {
        const double value = *operand.get_float64();
        return Value::float64(finite_result(op == "-" ? -value : value));
    }
    const std::int64_t value = integer_value(operand);
    if (op == "+") return Value::int64(value);
    if (value == std::numeric_limits<std::int64_t>::min())
        throw std::runtime_error("integer overflow");
    return Value::int64(-value);
}

Value ValueOperations::binary(const std::string& op, const Value& left, const Value& right) {
    if (op == "==") return Value::boolean(equal(left, right));
    if (op == "!=") return Value::boolean(!equal(left, right));
    if (op == "+" && left.type() == ValueType::String) {
        if (right.type() != ValueType::String)
            throw std::runtime_error("cannot concatenate string and non-string");
        return Value::string(*left.get_string() + *right.get_string());
    }
    if (op == "<" || op == "<=" || op == ">" || op == ">=") {
        if (!is_numeric(left) || !is_numeric(right))
            throw std::runtime_error("numeric operation requires numbers");
        int comparison;
        if (is_integer_like(left) && is_integer_like(right)) {
            const auto a = integer_value(left);
            const auto b = integer_value(right);
            comparison = a < b ? -1 : a > b ? 1 : 0;
        } else {
            const double a = numeric_value(left);
            const double b = numeric_value(right);
            comparison = a < b ? -1 : a > b ? 1 : 0;
        }
        if (op == "<") return Value::boolean(comparison < 0);
        if (op == "<=") return Value::boolean(comparison <= 0);
        if (op == ">") return Value::boolean(comparison > 0);
        return Value::boolean(comparison >= 0);
    }
    if (!is_numeric(left) || !is_numeric(right))
        throw std::runtime_error("numeric operation requires numbers");

    const bool integer_operation = is_integer_like(left) && is_integer_like(right);
    if (op == "/" || !integer_operation) {
        const double a = numeric_value(left);
        const double b = numeric_value(right);
        if (op == "+") return Value::float64(finite_result(a + b));
        if (op == "-") return Value::float64(finite_result(a - b));
        if (op == "*") return Value::float64(finite_result(a * b));
        if (op == "/") {
            if (b == 0.0) throw std::runtime_error("division by zero");
            return Value::float64(finite_result(a / b));
        }
        if (op == "//") {
            if (b == 0.0) throw std::runtime_error("division by zero");
            return Value::float64(finite_result(std::floor(a / b)));
        }
        if (op == "%") {
            if (b == 0.0) throw std::runtime_error("modulo by zero");
            return Value::float64(finite_result(std::fmod(a, b)));
        }
        if (op == "**") return Value::float64(finite_result(std::pow(a, b)));
        throw std::runtime_error("unknown operator " + op);
    }

    const std::int64_t a = integer_value(left);
    const std::int64_t b = integer_value(right);
    if (op == "+") return Value::int64(checked_add(a, b));
    if (op == "-") return Value::int64(checked_subtract(a, b));
    if (op == "*") return Value::int64(checked_multiply(a, b));
    if (op == "//") return Value::int64(floor_quotient(a, b));
    if (op == "%") return Value::int64(floor_remainder(a, b));
    if (op == "**") {
        if (b < 0) {
            if (a == 0) throw std::runtime_error("arithmetic result is not finite");
            return Value::float64(finite_result(std::pow(static_cast<double>(a), static_cast<double>(b))));
        }
        return Value::int64(integer_power(a, static_cast<std::uint64_t>(b)));
    }
    throw std::runtime_error("unknown operator " + op);
}

Code compile(const Node& root) { return *Generator().generate(root); }
std::string disassemble(const Code& code) {
    std::ostringstream out; out << "ROLANGE-BYTECODE 1\n" << "code " << code.name << "\n";
    for (std::size_t i=0; i<code.instructions.size(); ++i) {
        const auto& ins=code.instructions[i]; out << std::setw(4) << i << "  " << std::left << std::setw(14) << op_name(ins.op) << std::right;
        if (ins.op==Op::Constant) out << ValueOperations::display(ins.value); else if (ins.op==Op::Load || ins.op==Op::Store || ins.op==Op::Unary || ins.op==Op::Binary || ins.op==Op::MakeFunction) out << ins.text; else if (ins.op==Op::Jump || ins.op==Op::JumpFalse || ins.op==Op::Call) out << ins.target;
        out << "  ; line " << ins.line << ":" << ins.column << '\n';
        if (ins.function_code) out << disassemble(*ins.function_code);
    }
    return out.str();
}

Value VM::execute(const Code& code, std::unordered_map<std::string, Value>& globals, std::unordered_map<std::string, Value> locals, bool module, std::size_t call_depth) {
    validate_code(code);
    std::vector<Value> stack;
    auto pop = [&]() { if(stack.empty()) throw std::runtime_error("bytecode stack underflow"); Value v=std::move(stack.back()); stack.pop_back(); return v; };
    for (std::size_t ip=0; ip<code.instructions.size();) {
        const auto& ins=code.instructions[ip++];
        try {
            if (++steps_ > max_instructions) throw std::runtime_error("execution limit exceeded");
            switch(ins.op) {
                case Op::Constant: stack.push_back(ins.value); break;
                case Op::Load:
                    if (auto it=locals.find(ins.text); it!=locals.end()) stack.push_back(it->second);
                    else if (auto it=globals.find(ins.text); it!=globals.end()) stack.push_back(it->second);
                    else if (ins.text=="print") stack.push_back(Value::builtin_print());
                    else throw std::runtime_error("undefined name '"+ins.text+"'");
                    break;
                case Op::Store: { Value v=pop(); if(module) globals[ins.text]=std::move(v); else locals[ins.text]=std::move(v); break; }
                case Op::Pop: (void)pop(); break;
                case Op::Nop: break;
                case Op::Unary: {
                    stack.push_back(ValueOperations::unary(ins.text, pop()));
                    break;
                }
                case Op::ToBool: { Value v=pop(); stack.push_back(Value::boolean(ValueOperations::truthy(v))); break; }
                case Op::Binary: {
                    Value right=pop(), left=pop();
                    stack.push_back(ValueOperations::binary(ins.text, left, right));
                    break;
                }
                case Op::JumpFalse:
                    if(!ValueOperations::truthy(pop())) ip=static_cast<std::size_t>(ins.target);
                    break;
                case Op::Jump:
                    ip=static_cast<std::size_t>(ins.target);
                    break;
                case Op::MakeFunction: {
                    auto fn=std::make_shared<Function>(); fn->code=ins.function_code; stack.push_back(Value::function(std::move(fn))); break;
                }
                case Op::Call: {
                    if(stack.size()<static_cast<std::size_t>(ins.target)+1) throw std::runtime_error("bytecode stack underflow");
                    std::vector<Value> args(static_cast<std::size_t>(ins.target));
                    for(int i=ins.target-1;i>=0;--i) args[static_cast<std::size_t>(i)]=pop();
                    Value callee=pop();
                    if(callee.type() == ValueType::BuiltinFunction) {
                        std::string line; for(std::size_t i=0;i<args.size();++i) { if(i) line+=' '; line+=ValueOperations::display(args[i]); }
                        if(show_output_) std::cout << line << '\n';
                        output_.push_back(line);
                        stack.push_back(Value::null());
                    } else if(callee.type() == ValueType::Function) {
                        const auto& fn = *callee.get_function();
                        if(!fn || !fn->code) throw std::runtime_error("function has no bytecode");
                        if(args.size()!=fn->code->params.size()) throw std::runtime_error(fn->code->name+"() takes "+std::to_string(fn->code->params.size())+" argument(s), got "+std::to_string(args.size()));
                        std::unordered_map<std::string,Value> local;
                        for(std::size_t i=0;i<args.size();++i) local[fn->code->params[i]]=args[i];
                        if(call_depth >= max_call_depth) throw std::runtime_error("maximum call depth exceeded");
                        stack.push_back(execute(*fn->code,globals,std::move(local),false,call_depth+1));
                    } else throw std::runtime_error("object is not callable");
                    break;
                }
                case Op::Return:
                    if(module && ip != code.instructions.size()) throw std::runtime_error("return outside a function");
                    return pop();
            }
        } catch(const std::exception& e) {
            const std::string message = e.what();
            if(message.rfind("line ", 0) == 0) throw;
            throw std::runtime_error("line "+std::to_string(ins.line)+":"+std::to_string(ins.column)+": "+message);
        }
    }
    return Value::null();
}
void VM::run(const Code& code) { std::unordered_map<std::string,Value> globals; steps_=0; (void)execute(code,globals,{},true,0); }
} // namespace rolang
