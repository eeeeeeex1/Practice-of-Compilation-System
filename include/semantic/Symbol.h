#pragma once

#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <unordered_map>
#include "ast/AST.h"

namespace semantic {

// =============================================================================
// Symbol kinds
// =============================================================================
enum class SymbolKind {
    Variable,
    Const,
    Param,
    Function
};

// =============================================================================
// Symbol base class
// =============================================================================
struct Symbol {
    std::string name;
    SymbolKind kind;

    Symbol(std::string name, SymbolKind kind);
    virtual ~Symbol() = default;
};

// =============================================================================
// Variable symbol (includes const and param)
// =============================================================================
struct VariableSymbol : Symbol {
    TypeKind type;          // Int only in ToyC
    bool isConst;
    bool isParam;
    int constValue;         // Valid only when isConst and compile-time evaluated

    VariableSymbol(std::string name, TypeKind type, bool isConst, bool isParam);
};

// =============================================================================
// Function symbol
// =============================================================================
struct FunctionSymbol : Symbol {
    TypeKind returnType;
    std::vector<Param> params;
    bool isDefined;

    FunctionSymbol(std::string name, TypeKind returnType,
                   std::vector<Param> params, bool isDefined = false);
};

// =============================================================================
// Scope
// =============================================================================
class Scope {
public:
    enum class ScopeKind {
        Global,
        Function,
        Block,
        Loop       // while body
    };

    explicit Scope(ScopeKind kind, Scope* parent = nullptr);

    bool insert(std::unique_ptr<Symbol> symbol);
    Symbol* lookup(const std::string& name) const;
    Symbol* lookupLocal(const std::string& name) const;
    bool contains(const std::string& name) const;

    ScopeKind getKind() const { return kind_; }
    Scope* getParent() const { return parent_; }

    const std::unordered_map<std::string, std::unique_ptr<Symbol>>& symbols() const {
        return symbols_;
    }

private:
    ScopeKind kind_;
    Scope* parent_;
    std::unordered_map<std::string, std::unique_ptr<Symbol>> symbols_;
};

} // namespace semantic