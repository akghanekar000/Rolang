#pragma once

#include "semantic_types.hpp"

#include <deque>
#include <optional>
#include <string>
#include <unordered_map>

namespace rolang::semantics {

struct InsertResult {
    std::optional<SymbolId> inserted;
    std::optional<SymbolId> existing;
};

class SymbolTable {
public:
    void create_scope_index(ScopeId scope);
    InsertResult insert(Symbol symbol);
    std::optional<SymbolId> find_local(ScopeId scope, const std::string& name) const;
    const Symbol* get(SymbolId id) const;

private:
    SymbolId next_id_ = 1;
    std::deque<Symbol> symbols_{1};
    std::unordered_map<ScopeId, std::unordered_map<std::string, SymbolId>> indexes_;
};

} // namespace rolang::semantics
