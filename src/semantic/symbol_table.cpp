#include "symbol_table.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace rolang::semantics {

void SymbolTable::create_scope_index(ScopeId scope) {
    if (scope == 0 || !indexes_.emplace(scope, std::unordered_map<std::string, SymbolId>{}).second)
        throw std::logic_error("invalid or duplicate symbol-table scope");
}

InsertResult SymbolTable::insert(Symbol symbol) {
    auto scope = indexes_.find(symbol.owner);
    if (scope == indexes_.end()) throw std::logic_error("symbol inserted into unknown scope");
    auto existing = scope->second.find(symbol.name);
    if (existing != scope->second.end()) return {{}, existing->second};
    if (next_id_ == 0 || next_id_ == std::numeric_limits<SymbolId>::max())
        throw std::logic_error("symbol ID space exhausted");

    symbol.id = next_id_++;
    const SymbolId id = symbol.id;
    symbols_.push_back(std::move(symbol));
    scope->second.emplace(symbols_.back().name, id);
    return {id, {}};
}

std::optional<SymbolId> SymbolTable::find_local(ScopeId scope, const std::string& name) const {
    auto scope_index = indexes_.find(scope);
    if (scope_index == indexes_.end()) throw std::logic_error("lookup in unknown symbol-table scope");
    auto symbol = scope_index->second.find(name);
    if (symbol == scope_index->second.end()) return {};
    return symbol->second;
}

const Symbol* SymbolTable::get(SymbolId id) const {
    if (id == 0 || static_cast<std::size_t>(id) >= symbols_.size()) return nullptr;
    return &symbols_[id];
}

} // namespace rolang::semantics
