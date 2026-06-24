#include "semantic/SymbolTable.h"

namespace semantic {

SymbolTable::SymbolTable() {
    // Create global scope
    auto global = std::make_unique<Scope>(Scope::ScopeKind::Global);
    currentScope_ = global.get();
    scopes_.push_back(std::move(global));
}

Scope* SymbolTable::enterScope(Scope::ScopeKind kind) {
    auto scope = std::make_unique<Scope>(kind, currentScope_);
    Scope* ptr = scope.get();
    scopes_.push_back(std::move(scope));
    currentScope_ = ptr;
    return ptr;
}

void SymbolTable::exitScope() {
    if (scopes_.size() > 1) {
        scopes_.pop_back();
        currentScope_ = scopes_.back().get();
    }
}

Scope* SymbolTable::currentScope() {
    return currentScope_;
}

const Scope* SymbolTable::currentScope() const {
    return currentScope_;
}

bool SymbolTable::insert(std::unique_ptr<Symbol> symbol) {
    return currentScope_->insert(std::move(symbol));
}

Symbol* SymbolTable::lookup(const std::string& name) const {
    return currentScope_->lookup(name);
}

Symbol* SymbolTable::lookupCurrentScope(const std::string& name) const {
    return currentScope_->lookupLocal(name);
}

bool SymbolTable::isInLoop() const {
    const Scope* scope = currentScope_;
    while (scope) {
        if (scope->getKind() == Scope::ScopeKind::Loop) {
            return true;
        }
        if (scope->getKind() == Scope::ScopeKind::Function) {
            return false;
        }
        scope = scope->getParent();
    }
    return false;
}

Scope* SymbolTable::getFunctionScope() const {
    const Scope* scope = currentScope_;
    while (scope) {
        if (scope->getKind() == Scope::ScopeKind::Function) {
            return const_cast<Scope*>(scope);
        }
        scope = scope->getParent();
    }
    return nullptr;
}

Scope* SymbolTable::getGlobalScope() const {
    return scopes_.front().get();
}

bool SymbolTable::isInFunction() const {
    return getFunctionScope() != nullptr;
}

} // namespace semantic