#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>
#include <optional>

// =============================================================================
// Forward declarations
// =============================================================================
struct Stmt;
struct Expr;

// =============================================================================
// Type system
// =============================================================================
enum class TypeKind { Int, Void };

// =============================================================================
// Operators
// =============================================================================
enum class BinaryOp {
    Add, Sub, Mul, Div, Mod,
    Lt, Gt, Le, Ge, Eq, Ne,
    And, Or
};

enum class UnaryOp {
    Pos, Neg, Not
};

// =============================================================================
// Expression nodes
// =============================================================================
struct NumberExpr {
    int value;
};

struct IdExpr {
    std::string name;
};

struct BinaryExpr {
    BinaryOp op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    BinaryExpr() = default;
    ~BinaryExpr();
    BinaryExpr(BinaryExpr&&) = default;
    BinaryExpr& operator=(BinaryExpr&&) = default;
};

struct UnaryExpr {
    UnaryOp op;
    std::unique_ptr<Expr> operand;
    UnaryExpr() = default;
    ~UnaryExpr();
    UnaryExpr(UnaryExpr&&) = default;
    UnaryExpr& operator=(UnaryExpr&&) = default;
};

struct CallExpr {
    std::string name;
    std::vector<std::unique_ptr<Expr>> args;
    CallExpr() = default;
    CallExpr(std::string n, std::vector<std::unique_ptr<Expr>> a)
        : name(std::move(n)), args(std::move(a)) {}
    ~CallExpr();
    CallExpr(CallExpr&&) = default;
    CallExpr& operator=(CallExpr&&) = default;
};

struct Expr : std::variant<NumberExpr, IdExpr, BinaryExpr, UnaryExpr, CallExpr> {
    using std::variant<NumberExpr, IdExpr, BinaryExpr, UnaryExpr, CallExpr>::variant;
};

// Destructors for expression types containing unique_ptr<Expr>
inline BinaryExpr::~BinaryExpr() = default;
inline UnaryExpr::~UnaryExpr() = default;
inline CallExpr::~CallExpr() = default;

// =============================================================================
// Statement nodes
// =============================================================================
struct Block {
    std::vector<std::unique_ptr<Stmt>> stmts;
    Block() = default;
    ~Block();
    Block(Block&&) = default;
    Block& operator=(Block&&) = default;
};

struct IfStmt {
    std::unique_ptr<Expr> cond;
    std::unique_ptr<Stmt> thenStmt;
    std::unique_ptr<Stmt> elseStmt;
    IfStmt() = default;
    ~IfStmt();
    IfStmt(IfStmt&&) = default;
    IfStmt& operator=(IfStmt&&) = default;
};

struct WhileStmt {
    std::unique_ptr<Expr> cond;
    std::unique_ptr<Stmt> body;
    WhileStmt() = default;
    ~WhileStmt();
    WhileStmt(WhileStmt&&) = default;
    WhileStmt& operator=(WhileStmt&&) = default;
};

struct BreakStmt {};

struct ContinueStmt {};

struct ReturnStmt {
    std::unique_ptr<Expr> value;
};

struct ExprStmt {
    std::unique_ptr<Expr> expr;
};

struct AssignStmt {
    std::string name;
    std::unique_ptr<Expr> value;
};

struct EmptyStmt {};

struct ConstDecl {
    std::string name;
    std::unique_ptr<Expr> init;
};

struct VarDecl {
    std::string name;
    std::unique_ptr<Expr> init;
};

struct Stmt : std::variant<
    Block, IfStmt, WhileStmt, BreakStmt, ContinueStmt,
    ReturnStmt, ExprStmt, AssignStmt, EmptyStmt,
    ConstDecl, VarDecl
> {
    using std::variant<
        Block, IfStmt, WhileStmt, BreakStmt, ContinueStmt,
        ReturnStmt, ExprStmt, AssignStmt, EmptyStmt,
        ConstDecl, VarDecl
    >::variant;
};

// Destructors for statement types containing unique_ptr<Stmt>
inline Block::~Block() = default;
inline IfStmt::~IfStmt() = default;
inline WhileStmt::~WhileStmt() = default;

// =============================================================================
// Function definition and top-level nodes
// =============================================================================
struct Param {
    std::string name;
};

struct FuncDef {
    TypeKind returnType = TypeKind::Void;
    std::string name;
    std::vector<Param> params;
    Block body;
};

using TopLevel = std::variant<ConstDecl, VarDecl, FuncDef>;

struct CompUnit {
    std::vector<TopLevel> items;
};

// =============================================================================
// Visitor helper
// =============================================================================
template<class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;