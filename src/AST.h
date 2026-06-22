#ifndef AST_H
#define AST_H

#include "Token.h"
#include <memory>
#include <vector>
#include <deque>
#include <string>

// Forward declarations
struct Expr;
struct Stmt;
struct Block;
struct Decl;
struct FuncDef;
struct CompUnit;
class SymbolTable;

using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
using BlockPtr = std::unique_ptr<Block>;
using DeclPtr = std::unique_ptr<Decl>;
using FuncDefPtr = std::unique_ptr<FuncDef>;

// === Type system ===
enum class TypeKind {
    INT,
    VOID
};

// === Symbol kinds ===
enum class SymbolKind {
    VARIABLE,
    CONSTANT,
    PARAMETER,
    FUNCTION
};

// === Symbol table entry ===
struct Symbol {
    std::string name;
    SymbolKind kind;
    TypeKind type;
    int intValue = 0;
    bool hasValue = false;
    int stackOffset = 0;   // offset from fp for locals/params (negative for locals)
    bool isGlobal = false;
    std::vector<TypeKind> paramTypes;  // for functions

    Symbol() : kind(SymbolKind::VARIABLE), type(TypeKind::INT) {}
};

// === Scope ===
class Scope {
public:
    bool add(const std::string& name, Symbol sym);
    Symbol* lookup(const std::string& name);
    bool contains(const std::string& name) const;
    const auto& symbols() const { return m_symbols; }

private:
    std::deque<std::pair<std::string, Symbol>> m_symbols;  // deque — stable references
};

// === Symbol table ===
class SymbolTable {
public:
    SymbolTable();

    void enterScope();
    void leaveScope();
    bool add(const std::string& name, Symbol sym);
    Symbol* lookup(const std::string& name);
    Symbol* lookupLocal(const std::string& name);
    bool isGlobal() const;
    int scopeDepth() const { return static_cast<int>(m_scopes.size()); }

private:
    std::deque<Scope> m_scopes;      // deque — stable element addresses
    std::deque<Symbol> m_globals;    // deque — stable element addresses
};

// === Expression ast nodes ===

enum class ExprKind {
    NUMBER,
    ID,
    BINARY,
    UNARY,
    CALL
};

struct Expr {
    ExprKind kind;
    Token tok;
    TypeKind type = TypeKind::INT;  // set by semantic analysis
    bool isConst = false;
    int constValue = 0;

    explicit Expr(ExprKind k, Token t) : kind(k), tok(std::move(t)) {}
    virtual ~Expr() = default;
};

struct NumberExpr : public Expr {
    int value;
    NumberExpr(Token t, int v) : Expr(ExprKind::NUMBER, std::move(t)), value(v) {}
};

struct IdExpr : public Expr {
    std::string name;
    int varOffset = 0;     // stack offset from fp (for locals)
    bool varIsGlobal = false;  // true if global variable
    bool varIsConst = false;
    int varConstValue = 0;
    IdExpr(Token t, std::string n) : Expr(ExprKind::ID, std::move(t)), name(std::move(n)) {}
};

struct BinaryExpr : public Expr {
    TokenType op;
    ExprPtr left;
    ExprPtr right;
    BinaryExpr(Token t, TokenType o, ExprPtr l, ExprPtr r)
        : Expr(ExprKind::BINARY, std::move(t)), op(o), left(std::move(l)), right(std::move(r)) {}
};

struct UnaryExpr : public Expr {
    TokenType op;  // TOK_SUB, TOK_ADD, TOK_NOT
    ExprPtr operand;
    UnaryExpr(Token t, TokenType o, ExprPtr e)
        : Expr(ExprKind::UNARY, std::move(t)), op(o), operand(std::move(e)) {}
};

struct CallExpr : public Expr {
    std::string funcName;
    std::vector<ExprPtr> args;
    int funcParamCount = 0;
    CallExpr(Token t, std::string name, std::vector<ExprPtr> a)
        : Expr(ExprKind::CALL, std::move(t)), funcName(std::move(name)), args(std::move(a)) {}
};

// === Statement ast nodes ===

enum class StmtKind {
    BLOCK,
    EMPTY,
    EXPR,
    ASSIGN,
    DECL,
    IF,
    WHILE,
    BREAK,
    CONTINUE,
    RETURN
};

struct Stmt {
    StmtKind kind;
    Token tok;
    explicit Stmt(StmtKind k, Token t) : kind(k), tok(std::move(t)) {}
    virtual ~Stmt() = default;
};

struct BlockStmt : public Stmt {
    BlockPtr block;
    BlockStmt(Token t, BlockPtr b) : Stmt(StmtKind::BLOCK, std::move(t)), block(std::move(b)) {}
};

struct EmptyStmt : public Stmt {
    EmptyStmt(Token t) : Stmt(StmtKind::EMPTY, std::move(t)) {}
};

struct ExprStmt : public Stmt {
    ExprPtr expr;
    ExprStmt(Token t, ExprPtr e) : Stmt(StmtKind::EXPR, std::move(t)), expr(std::move(e)) {}
};

struct AssignStmt : public Stmt {
    std::string name;
    ExprPtr value;
    int varOffset = 0;
    bool varIsGlobal = false;
    AssignStmt(Token t, std::string n, ExprPtr v)
        : Stmt(StmtKind::ASSIGN, std::move(t)), name(std::move(n)), value(std::move(v)) {}
};

struct DeclStmt : public Stmt {
    DeclPtr decl;
    DeclStmt(Token t, DeclPtr d) : Stmt(StmtKind::DECL, std::move(t)), decl(std::move(d)) {}
};

struct IfStmt : public Stmt {
    ExprPtr condition;
    StmtPtr thenStmt;
    StmtPtr elseStmt;  // may be null
    IfStmt(Token t, ExprPtr cond, StmtPtr thenS, StmtPtr elseS)
        : Stmt(StmtKind::IF, std::move(t)), condition(std::move(cond)),
          thenStmt(std::move(thenS)), elseStmt(std::move(elseS)) {}
};

struct WhileStmt : public Stmt {
    ExprPtr condition;
    StmtPtr body;
    WhileStmt(Token t, ExprPtr cond, StmtPtr b)
        : Stmt(StmtKind::WHILE, std::move(t)), condition(std::move(cond)), body(std::move(b)) {}
};

struct BreakStmt : public Stmt {
    BreakStmt(Token t) : Stmt(StmtKind::BREAK, std::move(t)) {}
};

struct ContinueStmt : public Stmt {
    ContinueStmt(Token t) : Stmt(StmtKind::CONTINUE, std::move(t)) {}
};

struct ReturnStmt : public Stmt {
    ExprPtr expr;
    bool hasExpr;
    ReturnStmt(Token t, ExprPtr e, bool he)
        : Stmt(StmtKind::RETURN, std::move(t)), expr(std::move(e)), hasExpr(he) {}
};

// === Block ===
struct Block {
    std::vector<StmtPtr> stmts;
};

// === Declaration ===
enum class DeclKind {
    VARIABLE,
    CONSTANT
};

struct Decl {
    DeclKind kind;
    Token tok;
    std::string name;
    ExprPtr init;
    bool isGlobal;
    int varOffset = 0;    // stack offset from fp (for locals only)

    Decl(DeclKind k, Token t, std::string n, ExprPtr i, bool g)
        : kind(k), tok(std::move(t)), name(std::move(n)), init(std::move(i)), isGlobal(g) {}
};

// === Function definition ===
enum class FuncReturnType {
    INT,
    VOID
};

struct FuncDef {
    FuncReturnType returnType;
    std::string name;
    std::vector<std::string> params;
    BlockPtr body;
    Token tok;
    Symbol* symbol = nullptr;
    int stackSize = 0;
    std::vector<Symbol*> paramSymbols;

    std::vector<int> paramOffsets;  // stack offsets for each param

    FuncDef(FuncReturnType rt, std::string n, std::vector<std::string> p,
            BlockPtr b, Token t)
        : returnType(rt), name(std::move(n)), params(std::move(p)),
          body(std::move(b)), tok(std::move(t)) {}
};

// === Compilation unit ===
struct CompUnit {
    std::vector<DeclPtr> globals;
    std::vector<FuncDefPtr> functions;
};

// === Helper functions for dynamic_cast checking ===
inline bool exprIsInt(const Expr* e) {
    return e && e->type == TypeKind::INT;
}

inline bool exprIsConst(const Expr* e) {
    return e && e->isConst;
}

#endif // AST_H
