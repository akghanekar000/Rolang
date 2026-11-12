#include "semantic_analyzer.hpp"

#include "diagnostic_engine.hpp"
#include "scope_manager.hpp"
#include "type_checker.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace rolang::semantics {
namespace {

void malformed(const std::string& message) {
    throw std::logic_error("malformed AST: " + message);
}

void require_kind(const Node& node, NodeKind expected, const char* role) {
    if (node.kind != expected) malformed(std::string(role) + " has the wrong node kind");
}

void validate_expression(const Node& node);
void validate_statement(const Node& node);

void validate_block(const Node& block) {
    require_kind(block, NodeKind::Block, "block");
    for (const Node& child : block.children) validate_statement(child);
}

void validate_statement(const Node& node) {
    switch (node.kind) {
        case NodeKind::Function:
            if (node.text.empty() || node.children.size() != 1)
                malformed("function must have a name and exactly one body");
            validate_block(node.children[0]);
            return;
        case NodeKind::If:
            if (node.children.size() != 2 && node.children.size() != 3)
                malformed("if must have a condition, body, and optional else body");
            validate_expression(node.children[0]);
            validate_block(node.children[1]);
            if (node.children.size() == 3) validate_block(node.children[2]);
            return;
        case NodeKind::While:
            if (node.children.size() != 2) malformed("while must have a condition and body");
            validate_expression(node.children[0]);
            validate_block(node.children[1]);
            return;
        case NodeKind::Return:
            if (node.children.size() > 1) malformed("return has too many children");
            if (!node.children.empty()) validate_expression(node.children[0]);
            return;
        case NodeKind::Pass:
            if (!node.children.empty()) malformed("pass cannot have children");
            return;
        case NodeKind::Assign:
            if (node.text.empty() || node.children.size() != 1)
                malformed("assignment must have a name and exactly one value");
            validate_expression(node.children[0]);
            return;
        case NodeKind::Expression:
            if (node.children.size() != 1) malformed("expression statement must have one child");
            validate_expression(node.children[0]);
            return;
        default:
            malformed("non-statement node in statement position");
    }
}

void validate_expression(const Node& node) {
    switch (node.kind) {
        case NodeKind::Literal:
            if (!node.children.empty()) malformed("literal cannot have children");
            if (!node.string_literal && !node.text.empty() &&
                node.text != "True" && node.text != "False" && node.text != "None")
                malformed("literal has an unknown value kind");
            return;
        case NodeKind::Name:
            if (node.text.empty() || !node.children.empty()) malformed("name node is malformed");
            return;
        case NodeKind::Unary:
            if (node.children.size() != 1 ||
                (node.text != "not" && node.text != "+" && node.text != "-"))
                malformed("unary operation is malformed");
            validate_expression(node.children[0]);
            return;
        case NodeKind::Binary: {
            static const std::unordered_set<std::string> operators = {
                "+", "-", "*", "/", "//", "%", "**", "==", "!=", "<", "<=", ">", ">=", "and", "or"};
            if (node.children.size() != 2 || operators.count(node.text) == 0)
                malformed("binary operation is malformed");
            validate_expression(node.children[0]);
            validate_expression(node.children[1]);
            return;
        }
        case NodeKind::Call:
            if (node.children.empty()) malformed("call must have a callee");
            for (const Node& child : node.children) validate_expression(child);
            return;
        default:
            malformed("non-expression node in expression position");
    }
}

SourceLocation source_location(const std::string& source_name, const Node& node) {
    return {source_name, node.line, node.column};
}

struct BindingCandidate {
    std::string name;
    SourceLocation declaration;
    bool has_assignment = false;
    bool has_parameter = false;
    std::vector<const Node*> functions;
    SymbolId symbol = 0;
};

struct ScopeBindings {
    std::vector<BindingCandidate> ordered;
    std::unordered_map<std::string, std::size_t> by_name;
};

class AnalysisContext {
public:
    explicit AnalysisContext(const AnalysisOptions& options)
        : source_name_(options.source_name), scopes_(symbols_), checker_(diagnostics_, source_name_) {
        Symbol builtin;
        builtin.name = "print";
        builtin.kind = SymbolKind::Builtin;
        builtin.owner = scopes_.builtin_scope();
        builtin.declaration = {source_name_, 1, 1};
        builtin.type.kind = TypeKind::Function;
        builtin.signature = FunctionSignature{{}, true};
        symbols_.insert(std::move(builtin));
    }

    AnalysisResult analyze(const Node& root) {
        if (root.kind != NodeKind::Program) malformed("analysis root must be a program");
        for (const Node& statement : root.children) validate_statement(statement);

        collect_scope(scopes_.module_scope(), root.children, nullptr);
        analyze_statements(root.children);

        AnalysisResult result;
        result.diagnostics = diagnostics_.take_diagnostics();
        result.resolved_names = std::move(resolved_names_);
        result.known_call_targets = std::move(known_call_targets_);
        result.inferred_types = std::move(inferred_types_);
        return result;
    }

private:
    std::string source_name_;
    SymbolTable symbols_;
    ScopeManager scopes_;
    DiagnosticEngine diagnostics_;
    TypeChecker checker_;
    std::unordered_map<ScopeId, ScopeBindings> bindings_;
    std::unordered_map<const Node*, ScopeId> function_scopes_;
    std::unordered_map<ScopeId, std::unordered_map<std::string, SymbolId>> known_functions_;
    std::unordered_map<const Node*, SymbolId> resolved_names_;
    std::unordered_map<const Node*, SymbolId> known_call_targets_;
    std::unordered_map<const Node*, Type> inferred_types_;
    std::size_t function_depth_ = 0;

    BindingCandidate& candidate(ScopeId scope, const std::string& name, const Node& declaration) {
        ScopeBindings& scope_bindings = bindings_[scope];
        auto found = scope_bindings.by_name.find(name);
        if (found != scope_bindings.by_name.end()) return scope_bindings.ordered[found->second];

        const std::size_t index = scope_bindings.ordered.size();
        scope_bindings.by_name.emplace(name, index);
        scope_bindings.ordered.push_back(
            {name, source_location(source_name_, declaration), false, false, {}, 0});
        return scope_bindings.ordered.back();
    }

    void collect_scope(ScopeId scope, const std::vector<Node>& statements, const Node* function) {
        const bool entered = scopes_.current_scope() != scope;
        if (entered) scopes_.enter(scope);

        if (function) {
            for (const std::string& parameter : function->params) {
                BindingCandidate& binding = candidate(scope, parameter, *function);
                binding.has_parameter = true;
            }
        }

        std::vector<const Node*> functions;
        collect_statements(scope, statements, functions);
        ScopeBindings& scope_bindings = bindings_[scope];
        for (BindingCandidate& binding : scope_bindings.ordered) {
            const bool unique_function = !binding.has_assignment && !binding.has_parameter &&
                                         binding.functions.size() == 1;
            Symbol symbol;
            symbol.name = binding.name;
            symbol.owner = scope;
            symbol.declaration = binding.declaration;
            if (unique_function) {
                symbol.kind = SymbolKind::Function;
                symbol.type.kind = TypeKind::Function;
                symbol.signature = FunctionSignature{binding.functions.front()->params, false};
            } else if (binding.has_parameter && binding.functions.empty() && !binding.has_assignment) {
                symbol.kind = SymbolKind::Parameter;
            } else {
                symbol.kind = SymbolKind::Variable;
            }
            InsertResult inserted = symbols_.insert(std::move(symbol));
            if (!inserted.inserted) throw std::logic_error("duplicate candidate in semantic symbol table");
            binding.symbol = *inserted.inserted;
        }

        std::vector<std::pair<const Node*, SymbolId>> nested_functions;
        nested_functions.reserve(functions.size());
        for (const Node* function_node : functions) {
            const auto function_binding = scope_bindings.by_name.find(function_node->text);
            if (function_binding == scope_bindings.by_name.end())
                throw std::logic_error("function candidate was not registered");
            const SymbolId owner = scope_bindings.ordered[function_binding->second].symbol;
            nested_functions.emplace_back(function_node, owner);
        }
        for (const auto& nested : nested_functions) {
            const Node* function_node = nested.first;
            const ScopeId parent_scope = scopes_.current_scope();
            const ScopeId function_scope = scopes_.create_scope(ScopeKind::Function, nested.second);
            function_scopes_.emplace(function_node, function_scope);
            collect_scope(function_scope, function_node->children[0].children, function_node);
            if (scopes_.current_scope() != parent_scope)
                throw std::logic_error("semantic scope stack was not restored");
        }
        if (entered) scopes_.leave();
    }

    void collect_statements(ScopeId scope, const std::vector<Node>& statements,
                            std::vector<const Node*>& functions) {
        for (const Node& statement : statements) {
            switch (statement.kind) {
                case NodeKind::Assign:
                    candidate(scope, statement.text, statement).has_assignment = true;
                    break;
                case NodeKind::Function:
                    candidate(scope, statement.text, statement).functions.push_back(&statement);
                    functions.push_back(&statement);
                    break;
                case NodeKind::If:
                    collect_statements(scope, statement.children[1].children, functions);
                    if (statement.children.size() == 3)
                        collect_statements(scope, statement.children[2].children, functions);
                    break;
                case NodeKind::While:
                    collect_statements(scope, statement.children[1].children, functions);
                    break;
                default:
                    break;
            }
        }
    }

    void analyze_statements(const std::vector<Node>& statements) {
        for (const Node& statement : statements) analyze_statement(statement);
    }

    void analyze_statement(const Node& statement) {
        switch (statement.kind) {
            case NodeKind::Function:
                analyze_function(statement);
                remember_function(statement);
                break;
            case NodeKind::If: {
                (void)analyze_expression(statement.children[0]);
                const ScopeId scope = scopes_.current_scope();
                const auto before = known_functions_[scope];
                analyze_statements(statement.children[1].children);
                known_functions_[scope] = before;
                if (statement.children.size() == 3)
                    analyze_statements(statement.children[2].children);
                known_functions_[scope] = before;
                forget_modified_bindings(scope, statement);
                break;
            }
            case NodeKind::While: {
                (void)analyze_expression(statement.children[0]);
                const ScopeId scope = scopes_.current_scope();
                const auto before = known_functions_[scope];
                analyze_statements(statement.children[1].children);
                known_functions_[scope] = before;
                forget_modified_bindings(scope, statement);
                break;
            }
            case NodeKind::Return:
                if (function_depth_ == 0)
                    diagnostics_.error(DiagnosticCode::InvalidReturn,
                                       source_location(source_name_, statement),
                                       "return outside a function");
                if (!statement.children.empty()) (void)analyze_expression(statement.children[0]);
                break;
            case NodeKind::Pass:
                break;
            case NodeKind::Assign: {
                (void)analyze_expression(statement.children[0]);
                known_functions_[scopes_.current_scope()].erase(statement.text);
                break;
            }
            case NodeKind::Expression:
                (void)analyze_expression(statement.children[0]);
                break;
            default:
                malformed("non-statement node during analysis");
        }
    }

    void analyze_function(const Node& function) {
        auto scope = function_scopes_.find(&function);
        if (scope == function_scopes_.end()) throw std::logic_error("function scope was not registered");
        scopes_.enter(scope->second);
        const Scope& function_scope = scopes_.get_scope(scope->second);
        if (function_scope.owning_function) {
            const Symbol* owner = symbols_.get(*function_scope.owning_function);
            if (owner && owner->kind == SymbolKind::Function &&
                owner->owner == scopes_.module_scope() && owner->name == function.text)
                known_functions_[scope->second][function.text] = owner->id;
        }
        ++function_depth_;
        analyze_statements(function.children[0].children);
        --function_depth_;
        scopes_.leave();
    }

    void remember_function(const Node& function) {
        const ScopeId scope = scopes_.current_scope();
        auto scope_bindings = bindings_.find(scope);
        if (scope_bindings == bindings_.end()) return;
        auto found = scope_bindings->second.by_name.find(function.text);
        if (found == scope_bindings->second.by_name.end()) return;
        const BindingCandidate& binding = scope_bindings->second.ordered[found->second];
        if (binding.has_assignment || binding.has_parameter || binding.functions.size() != 1 ||
            binding.functions.front() != &function)
            return;
        known_functions_[scope][function.text] = binding.symbol;
    }

    void forget_modified_bindings(ScopeId scope, const Node& control) {
        std::unordered_set<std::string> names;
        collect_modified_names(control, names);
        auto& known = known_functions_[scope];
        for (const std::string& name : names) known.erase(name);
    }

    void collect_modified_names(const Node& node, std::unordered_set<std::string>& names) const {
        if (node.kind == NodeKind::Assign) names.insert(node.text);
        else if (node.kind == NodeKind::Function) names.insert(node.text);
        else if (node.kind == NodeKind::If || node.kind == NodeKind::While) {
            for (std::size_t i = 1; i < node.children.size(); ++i)
                for (const Node& child : node.children[i].children) collect_modified_names(child, names);
        }
    }

    Type analyze_expression(const Node& expression) {
        Type result;
        switch (expression.kind) {
            case NodeKind::Literal:
                result = checker_.type_of_literal(expression);
                break;
            case NodeKind::Name:
                result = analyze_name(expression);
                break;
            case NodeKind::Unary:
                result = checker_.check_unary(expression, analyze_expression(expression.children[0]));
                break;
            case NodeKind::Binary: {
                Type left = analyze_expression(expression.children[0]);
                Type right = analyze_expression(expression.children[1]);
                result = checker_.check_binary(expression, left, right);
                break;
            }
            case NodeKind::Call:
                result = analyze_call(expression);
                break;
            default:
                malformed("non-expression node during analysis");
        }
        inferred_types_[&expression] = result;
        return result;
    }

    Type analyze_name(const Node& name) {
        auto symbol_id = scopes_.lookup(name.text);
        if (!symbol_id) symbol_id = scopes_.lookup_builtin(name.text);
        if (!symbol_id) {
            diagnostics_.error(DiagnosticCode::UndefinedName, source_location(source_name_, name),
                               "undefined name '" + name.text + "'");
            return {TypeKind::Error, {}};
        }

        resolved_names_[&name] = *symbol_id;
        const Symbol* symbol = symbols_.get(*symbol_id);
        if (!symbol) throw std::logic_error("resolved symbol ID is invalid");
        if (symbol->kind == SymbolKind::Builtin)
            return {TypeKind::Function, *symbol_id};

        const ScopeId current = scopes_.current_scope();
        auto known_scope = known_functions_.find(current);
        if (known_scope != known_functions_.end()) {
            auto known = known_scope->second.find(name.text);
            if (known != known_scope->second.end() && known->second == *symbol_id)
                return {TypeKind::Function, *symbol_id};
        }
        return {TypeKind::Dynamic, {}};
    }

    Type analyze_call(const Node& call) {
        Type callee = analyze_expression(call.children[0]);
        for (std::size_t i = 1; i < call.children.size(); ++i)
            (void)analyze_expression(call.children[i]);

        if (call.children[0].kind != NodeKind::Name || callee.kind != TypeKind::Function ||
            !callee.function_symbol)
            return checker_.check_call_result(call, callee);

        const Symbol* symbol = symbols_.get(*callee.function_symbol);
        if (!symbol) throw std::logic_error("call target symbol ID is invalid");
        if (symbol->kind == SymbolKind::Builtin) return {TypeKind::None, {}};

        const ScopeId current = scopes_.current_scope();
        auto known_scope = known_functions_.find(current);
        const std::string& name = call.children[0].text;
        if (known_scope == known_functions_.end()) return checker_.check_call_result(call, callee);
        auto known = known_scope->second.find(name);
        if (known == known_scope->second.end() || known->second != *callee.function_symbol)
            return checker_.check_call_result(call, callee);

        if (!symbol->signature || symbol->signature->variadic)
            throw std::logic_error("known function has no fixed signature");
        known_call_targets_[&call] = *callee.function_symbol;
        const std::size_t actual = call.children.size() - 1;
        const std::size_t expected = symbol->signature->parameter_names.size();
        if (actual != expected) {
            diagnostics_.error_with_related(
                DiagnosticCode::ArgumentCount,
                source_location(source_name_, call.children[0]),
                "function '" + name + "' expects " + std::to_string(expected) +
                    " arguments, got " + std::to_string(actual),
                {symbol->declaration, "function declared here"});
        }
        return checker_.check_call_result(call, callee);
    }
};

} // namespace

AnalysisResult SemanticAnalyzer::analyze(const Node& root, const AnalysisOptions& options) const {
    return AnalysisContext(options).analyze(root);
}

} // namespace rolang::semantics
