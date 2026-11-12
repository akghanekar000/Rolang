#include "scope_manager.hpp"

#include <limits>
#include <stdexcept>

namespace rolang::semantics {

ScopeManager::ScopeManager(SymbolTable& symbols) : symbols_(symbols) {
    module_scope_ = create_scope(ScopeKind::Module);
    stack_.push_back(module_scope_);
    builtin_scope_ = create_scope(ScopeKind::Builtin);
}

ScopeId ScopeManager::module_scope() const { return module_scope_; }
ScopeId ScopeManager::builtin_scope() const { return builtin_scope_; }

ScopeId ScopeManager::create_scope(ScopeKind kind, std::optional<SymbolId> owning_function) {
    if (next_scope_id_ == 0 || next_scope_id_ == std::numeric_limits<ScopeId>::max())
        throw std::logic_error("scope ID space exhausted");
    const ScopeId id = next_scope_id_++;
    const std::optional<ScopeId> parent = stack_.empty() ? std::nullopt : std::optional<ScopeId>(stack_.back());
    scopes_.push_back({id, parent, kind, owning_function});
    symbols_.create_scope_index(id);
    return id;
}

void ScopeManager::enter(ScopeId scope) {
    (void)get_scope(scope);
    stack_.push_back(scope);
}

void ScopeManager::leave() {
    if (stack_.size() <= 1) throw std::logic_error("cannot leave the root scope");
    stack_.pop_back();
}

ScopeId ScopeManager::current_scope() const {
    if (stack_.empty()) throw std::logic_error("no active scope");
    return stack_.back();
}

std::optional<SymbolId> ScopeManager::lookup(const std::string& name) const {
    ScopeId scope_id = current_scope();
    while (true) {
        if (auto symbol = symbols_.find_local(scope_id, name)) return symbol;
        const Scope& scope = get_scope(scope_id);
        if (scope.kind == ScopeKind::Module) break;
        if (!scope.parent) break;
        scope_id = *scope.parent;
        if (scope.kind == ScopeKind::Function) {
            while (get_scope(scope_id).kind == ScopeKind::Function) {
                const Scope& enclosing = get_scope(scope_id);
                if (!enclosing.parent) return {};
                scope_id = *enclosing.parent;
            }
        }
    }
    return {};
}

std::optional<SymbolId> ScopeManager::lookup_local(const std::string& name) const {
    return symbols_.find_local(current_scope(), name);
}

std::optional<SymbolId> ScopeManager::lookup_builtin(const std::string& name) const {
    return symbols_.find_local(builtin_scope_, name);
}

const Scope& ScopeManager::get_scope(ScopeId id) const {
    if (id == 0 || static_cast<std::size_t>(id) >= scopes_.size())
        throw std::logic_error("invalid scope ID");
    return scopes_[id];
}

} // namespace rolang::semantics
