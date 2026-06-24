#pragma once

#include <memory>
#include <string>
#include <stack>
#include "Symbol.h"

namespace semantic {

// =============================================================================
// SymbolTable: manages hierarchical scopes
// =============================================================================
class SymbolTable {
public:
    SymbolTable();

    // Scope management
    Scope* enterScope(Scope::ScopeKind kind);
    void exitScope();
    Scope* currentScope();
    const Scope* currentScope() const;

    // Symbol operations
    bool insert(std::unique_ptr<Symbol> symbol);
    Symbol* lookup(const std::string& name) const;
    Symbol* lookupCurrentScope(const std::string& name) const;

    // Query helpers
    bool isInLoop() const;
    Scope* getFunctionScope() const;
    Scope* getGlobalScope() const;
    bool isInFunction() const;

private:
    std::vector<std::unique_ptr<Scope>> scopes_;
    Scope* currentScope_;
};

} // namespace semantic