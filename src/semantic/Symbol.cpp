#include "semantic/Symbol.h"

namespace semantic {

// =============================================================================
// Symbol
// =============================================================================
Symbol::Symbol(std::string name, SymbolKind kind)
    : name(std::move(name)), kind(kind) {}

// =============================================================================
// VariableSymbol
// =============================================================================
VariableSymbol::VariableSymbol(std::string name, TypeKind type,
                               bool isConst, bool isParam)
    : Symbol(std::move(name), isConst ? SymbolKind::Const
             : isParam            ? SymbolKind::Param
                                  : SymbolKind::Variable)
    , type(type)
    , isConst(isConst)
    , isParam(isParam)
    , constValue(0) {}

// =============================================================================
// FunctionSymbol
// =============================================================================
FunctionSymbol::FunctionSymbol(std::string name, TypeKind returnType,
                               std::vector<Param> params, bool isDefined)
    : Symbol(std::move(name), SymbolKind::Function)
    , returnType(returnType)
    , params(std::move(params))
    , isDefined(isDefined) {}

// =============================================================================
// Scope
// =============================================================================
Scope::Scope(ScopeKind kind, Scope* parent)
    : kind_(kind), parent_(parent) {}

bool Scope::insert(std::unique_ptr<Symbol> symbol) {
    if (symbols_.contains(symbol->name)) {
        return false;
    }
    symbols_[symbol->name] = std::move(symbol);
    return true;
}

Symbol* Scope::lookup(const std::string& name) const {
    auto it = symbols_.find(name);
    if (it != symbols_.end()) {
        return it->second.get();
    }
    if (parent_) {
        return parent_->lookup(name);
    }
    return nullptr;
}

Symbol* Scope::lookupLocal(const std::string& name) const {
    auto it = symbols_.find(name);
    if (it != symbols_.end()) {
        return it->second.get();
    }
    return nullptr;
}

bool Scope::contains(const std::string& name) const {
    return symbols_.contains(name);
}

} // namespace semantic