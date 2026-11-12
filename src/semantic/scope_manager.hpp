#pragma once

#include "symbol_table.hpp"

#include <optional>
#include <vector>

namespace rolang::semantics {

struct Scope {
    ScopeId id = 0;
    std::optional<ScopeId> parent;
    ScopeKind kind = ScopeKind::Module;
    std::optional<SymbolId> owning_function;
};

class ScopeManager {
public:
    explicit ScopeManager(SymbolTable& symbols);

    ScopeId module_scope() const;
    ScopeId builtin_scope() const;
    ScopeId create_scope(ScopeKind kind, std::optional<SymbolId> owning_function = std::nullopt);
    void enter(ScopeId scope);
    void leave();
    ScopeId current_scope() const;
    std::optional<SymbolId> lookup(const std::string& name) const;
    std::optional<SymbolId> lookup_local(const std::string& name) const;
    std::optional<SymbolId> lookup_builtin(const std::string& name) const;
    const Scope& get_scope(ScopeId id) const;

private:
    SymbolTable& symbols_;
    std::vector<Scope> scopes_{1};
    std::vector<ScopeId> stack_;
    ScopeId module_scope_ = 0;
    ScopeId builtin_scope_ = 0;
    ScopeId next_scope_id_ = 1;
};

} // namespace rolang::semantics
