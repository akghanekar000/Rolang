#ifndef ROLANGE_BACKEND_HPP
#define ROLANGE_BACKEND_HPP

#include "frontend.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace rolang {
struct Code;
struct Function { std::shared_ptr<Code> code; };
using Value = std::variant<std::monostate, double, bool, std::string, std::shared_ptr<Function>>;
enum class Op { Constant, Load, Store, Pop, Unary, Binary, ToBool, JumpFalse, Jump, MakeFunction, Call, Return, Nop };
struct Instruction {
    Op op;
    Value value{};
    std::string text;
    int target = 0;
    int line = 1;
    std::shared_ptr<Code> function_code;
};
struct Code { std::string name; std::vector<std::string> params; std::vector<Instruction> instructions; };
Code compile(const Node& root);
std::string disassemble(const Code& code);
class VM {
public:
    explicit VM(bool show_output = true) : show_output_(show_output) {}
    void run(const Code& code);
private:
    bool show_output_;
    std::size_t steps_ = 0;
    std::vector<std::string> output_;
    Value execute(const Code& code, std::unordered_map<std::string, Value>& globals, std::unordered_map<std::string, Value> locals, bool module);
};
} // namespace rolang
#endif
