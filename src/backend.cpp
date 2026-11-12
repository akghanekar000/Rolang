#include "backend.hpp"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace rolang {
namespace {
class Generator {
public:
    std::shared_ptr<Code> generate(const Node& root, const std::string& name = "<module>", std::vector<std::string> params = {}) {
        code_ = std::make_shared<Code>(); code_->name = name; code_->params = std::move(params);
        statements(root); emit(Op::Constant, {}, "", 0, root.line); emit(Op::Return, {}, "", 0, root.line); return code_;
    }
private:
    std::shared_ptr<Code> code_;
    int emit(Op op, Value value = {}, std::string text = {}, int target = 0, int line = 1, std::shared_ptr<Code> nested = {}) {
        code_->instructions.push_back({op, std::move(value), std::move(text), target, line, std::move(nested)}); return static_cast<int>(code_->instructions.size()-1);
    }
    void patch(int i, int target) { code_->instructions.at(static_cast<std::size_t>(i)).target = target; }
    void statements(const Node& node) { for (const auto& child : node.children) statement(child); }
    void statement(const Node& n) {
        switch(n.kind) {
            case NodeKind::Program: case NodeKind::Block: statements(n); break;
            case NodeKind::Function: {
                Generator nested; auto fn = nested.generate(n.children.at(0), n.text, n.params);
                emit(Op::MakeFunction, {}, n.text, 0, n.line, fn); emit(Op::Store, {}, n.text, 0, n.line); break;
            }
            case NodeKind::Assign: expression(n.children.at(0)); emit(Op::Store, {}, n.text, 0, n.line); break;
            case NodeKind::Expression: expression(n.children.at(0)); emit(Op::Pop, {}, "", 0, n.line); break;
            case NodeKind::Return:
                if (n.children.empty()) emit(Op::Constant, Value{std::monostate{}}, "", 0, n.line); else expression(n.children.at(0));
                emit(Op::Return, {}, "", 0, n.line); break;
            case NodeKind::Pass: emit(Op::Nop, {}, "", 0, n.line); break;
            case NodeKind::If: {
                expression(n.children.at(0)); int no = emit(Op::JumpFalse, {}, "", 0, n.line); statement(n.children.at(1));
                if (n.children.size() > 2) { int end = emit(Op::Jump, {}, "", 0, n.line); patch(no, static_cast<int>(code_->instructions.size())); statement(n.children.at(2)); patch(end, static_cast<int>(code_->instructions.size())); }
                else patch(no, static_cast<int>(code_->instructions.size())); break;
            }
            case NodeKind::While: {
                int start = static_cast<int>(code_->instructions.size()); expression(n.children.at(0)); int end = emit(Op::JumpFalse, {}, "", 0, n.line);
                statement(n.children.at(1)); emit(Op::Jump, {}, "", start, n.line); patch(end, static_cast<int>(code_->instructions.size())); break;
            }
            default: throw std::runtime_error("invalid statement node");
        }
    }
    void expression(const Node& n) {
        switch(n.kind) {
            case NodeKind::Literal:
                if (n.text == "True") emit(Op::Constant, Value{true}, "", 0, n.line);
                else if (n.text == "False") emit(Op::Constant, Value{false}, "", 0, n.line);
                else if (n.text == "None") emit(Op::Constant, Value{std::monostate{}}, "", 0, n.line);
                else if (!n.text.empty()) emit(Op::Constant, Value{n.text}, "", 0, n.line);
                else emit(Op::Constant, Value{n.number}, "", 0, n.line);
                break;
            case NodeKind::Name: emit(Op::Load, {}, n.text, 0, n.line); break;
            case NodeKind::Unary: expression(n.children.at(0)); emit(Op::Unary, {}, n.text, 0, n.line); break;
            case NodeKind::Binary: expression(n.children.at(0)); expression(n.children.at(1)); emit(Op::Binary, {}, n.text, 0, n.line); break;
            case NodeKind::Call:
                for (const auto& arg : n.children) expression(arg);
                emit(Op::Call, {}, "", static_cast<int>(n.children.size()-1), n.line);
                // The callee is evaluated before arguments; rotate call operands via a temporary instruction order fix.
                // Re-emit this expression correctly by rearranging its emitted chunk.
                break;
            default: throw std::runtime_error("invalid expression node");
        }
        if (n.kind == NodeKind::Call) {
            const int argc = static_cast<int>(n.children.size()-1);
            const int begin = static_cast<int>(code_->instructions.size()) - argc - 1;
            // Make call layout: callee first, followed by evaluated arguments.
            auto end = code_->instructions.end();
            Instruction call = std::move(*(end-1));
            code_->instructions.erase(end-1);
            code_->instructions.insert(code_->instructions.begin()+begin, std::move(call));
            // This relocates instructions, so control-flow jump targets emitted earlier must be adjusted.
            for (auto& ins : code_->instructions) if ((ins.op == Op::Jump || ins.op == Op::JumpFalse) && ins.target >= begin) ins.target += 1;
        }
    }
};

bool truthy(const Value& v) {
    if (std::holds_alternative<std::monostate>(v)) return false;
    if (auto p = std::get_if<bool>(&v)) return *p;
    if (auto p = std::get_if<double>(&v)) return *p != 0.0;
    if (auto p = std::get_if<std::string>(&v)) return !p->empty();
    return true;
}
double number(const Value& v) {
    if (auto p = std::get_if<double>(&v)) return *p;
    if (auto p = std::get_if<bool>(&v)) return *p ? 1.0 : 0.0;
    throw std::runtime_error("numeric operation requires numbers");
}
std::string display(const Value& v) {
    if (std::holds_alternative<std::monostate>(v)) return "None";
    if (auto p = std::get_if<bool>(&v)) return *p ? "True" : "False";
    if (auto p = std::get_if<double>(&v)) { std::ostringstream s; s << std::setprecision(15) << *p; return s.str(); }
    if (auto p = std::get_if<std::string>(&v)) return *p;
    return "<function>";
}
std::string op_name(Op op) {
    switch(op) { case Op::Constant:return "CONST"; case Op::Load:return "LOAD"; case Op::Store:return "STORE"; case Op::Pop:return "POP"; case Op::Unary:return "UNARY"; case Op::Binary:return "BINARY"; case Op::JumpFalse:return "JUMP_IF_FALSE"; case Op::Jump:return "JUMP"; case Op::MakeFunction:return "MAKE_FUNCTION"; case Op::Call:return "CALL"; case Op::Return:return "RETURN"; case Op::Nop:return "NOP"; } return "?";
}
} // namespace

Code compile(const Node& root) { return *Generator().generate(root); }
std::string disassemble(const Code& code) {
    std::ostringstream out; out << "ROLANGE-BYTECODE 1\n" << "code " << code.name << "\n";
    for (std::size_t i=0; i<code.instructions.size(); ++i) {
        const auto& ins=code.instructions[i]; out << std::setw(4) << i << "  " << std::left << std::setw(14) << op_name(ins.op) << std::right;
        if (ins.op==Op::Constant) out << display(ins.value); else if (ins.op==Op::Load || ins.op==Op::Store || ins.op==Op::Unary || ins.op==Op::Binary || ins.op==Op::MakeFunction) out << ins.text; else if (ins.op==Op::Jump || ins.op==Op::JumpFalse || ins.op==Op::Call) out << ins.target;
        out << "  ; line " << ins.line << '\n';
        if (ins.function_code) out << disassemble(*ins.function_code);
    }
    return out.str();
}

Value VM::execute(const Code& code, std::unordered_map<std::string, Value>& globals, std::unordered_map<std::string, Value> locals, bool module) {
    std::vector<Value> stack;
    auto pop = [&]() { if(stack.empty()) throw std::runtime_error("bytecode stack underflow"); Value v=std::move(stack.back()); stack.pop_back(); return v; };
    for (int ip=0; ip<static_cast<int>(code.instructions.size());) {
        if (++steps_ > 1'000'000) throw std::runtime_error("execution limit exceeded");
        const auto& ins=code.instructions[static_cast<std::size_t>(ip++)];
        try {
            switch(ins.op) {
                case Op::Constant: stack.push_back(ins.value); break;
                case Op::Load:
                    if (ins.text=="print") stack.push_back(std::string("__rolang_builtin_print__"));
                    else if (auto it=locals.find(ins.text); it!=locals.end()) stack.push_back(it->second);
                    else if (auto it=globals.find(ins.text); it!=globals.end()) stack.push_back(it->second);
                    else throw std::runtime_error("undefined name '"+ins.text+"'"); break;
                case Op::Store: { Value v=pop(); if(module) globals[ins.text]=std::move(v); else locals[ins.text]=std::move(v); break; }
                case Op::Pop: (void)pop(); break;
                case Op::Nop: break;
                case Op::Unary: { Value v=pop(); if(ins.text=="not") stack.push_back(!truthy(v)); else if(ins.text=="-") stack.push_back(-number(v)); else stack.push_back(number(v)); break; }
                case Op::Binary: {
                    Value right=pop(), left=pop(); const auto& op=ins.text;
                    if(op=="and") stack.push_back(truthy(left) && truthy(right)); else if(op=="or") stack.push_back(truthy(left) || truthy(right));
                    else if(op=="+") { if(auto a=std::get_if<std::string>(&left)) { auto b=std::get_if<std::string>(&right); if(!b) throw std::runtime_error("cannot concatenate string and non-string"); stack.push_back(*a+*b); } else stack.push_back(number(left)+number(right)); }
                    else if(op=="-") stack.push_back(number(left)-number(right)); else if(op=="*") stack.push_back(number(left)*number(right));
                    else if(op=="/") { double b=number(right); if(b==0) throw std::runtime_error("division by zero"); stack.push_back(number(left)/b); }
                    else if(op=="//") { double b=number(right); if(b==0) throw std::runtime_error("division by zero"); stack.push_back(std::floor(number(left)/b)); }
                    else if(op=="%") stack.push_back(std::fmod(number(left),number(right)));
                    else if(op=="**") stack.push_back(std::pow(number(left),number(right)));
                    else if(op=="==") stack.push_back(display(left)==display(right)); else if(op=="!=") stack.push_back(display(left)!=display(right));
                    else if(op=="<") stack.push_back(number(left)<number(right)); else if(op=="<=") stack.push_back(number(left)<=number(right)); else if(op==">") stack.push_back(number(left)>number(right)); else if(op==">=") stack.push_back(number(left)>=number(right));
                    else throw std::runtime_error("unknown operator "+op); break;
                }
                case Op::JumpFalse: if(!truthy(pop())) ip=ins.target; break;
                case Op::Jump: ip=ins.target; break;
                case Op::MakeFunction: { auto fn=std::make_shared<Function>(); fn->code=ins.function_code; stack.push_back(fn); break; }
                case Op::Call: {
                    std::vector<Value> args(static_cast<std::size_t>(ins.target));
                    for(int i=ins.target-1;i>=0;--i) args[static_cast<std::size_t>(i)]=pop();
                    Value callee=pop();
                    if(auto builtin=std::get_if<std::string>(&callee); builtin && *builtin=="__rolang_builtin_print__") {
                        std::string line; for(std::size_t i=0;i<args.size();++i) { if(i) line+=' '; line+=display(args[i]); }
                        if(show_output_) std::cout << line << '\n'; output_.push_back(line); stack.push_back(std::monostate{});
                    } else if(auto fn=std::get_if<std::shared_ptr<Function>>(&callee); fn && *fn) {
                        if(args.size()!=(*fn)->code->params.size()) throw std::runtime_error((*fn)->code->name+"() argument count mismatch");
                        std::unordered_map<std::string,Value> local;
                        for(std::size_t i=0;i<args.size();++i) local[(*fn)->code->params[i]]=args[i];
                        stack.push_back(execute(*(*fn)->code,globals,std::move(local),false));
                    } else throw std::runtime_error("object is not callable");
                    break;
                }
                case Op::Return: return pop();
            }
        } catch(const std::exception& e) { throw std::runtime_error("line "+std::to_string(ins.line)+": "+e.what()); }
    }
    return std::monostate{};
}
void VM::run(const Code& code) { std::unordered_map<std::string,Value> globals; steps_=0; (void)execute(code,globals,{},true); }
} // namespace rolang
