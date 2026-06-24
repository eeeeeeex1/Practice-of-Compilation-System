#pragma once

#include <string>
#include <vector>
#include <memory>
#include "ast/AST.h"
#include "SymbolTable.h"

namespace semantic {

// =============================================================================
// Semantic analysis result
// =============================================================================
struct SemanticResult {
    bool success = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    void addError(const std::string& msg);
    void addWarning(const std::string& msg);
};

// =============================================================================
// SemanticAnalyzer: performs semantic analysis on the AST
// =============================================================================
class SemanticAnalyzer {
public:
    SemanticAnalyzer();

    SemanticResult analyze(const CompUnit& compUnit);

    // Access the symbol table after analysis (for downstream use)
    SymbolTable& getSymbolTable() { return symTable_; }
    const SymbolTable& getSymbolTable() const { return symTable_; }

private:
    SemanticResult result_;
    SymbolTable symTable_;

    // Analysis state
    TypeKind currentFunctionReturnType_ = TypeKind::Void;
    bool hasReturnStatement_ = false;
    bool hasMainFunction_ = false;

    // Top-level analysis
    void analyzeTopLevel(const TopLevel& item);
    void analyzeFuncDef(const FuncDef& funcDef);
    void analyzeConstDecl(const ConstDecl& decl);
    void analyzeVarDecl(const VarDecl& decl);

    // Statement analysis
    void analyzeStmt(const Stmt& stmt);
    void analyzeBlock(const Block& block);
    void analyzeIfStmt(const IfStmt& stmt);
    void analyzeWhileStmt(const WhileStmt& stmt);
    void analyzeBreakStmt(const BreakStmt& stmt);
    void analyzeContinueStmt(const ContinueStmt& stmt);
    void analyzeReturnStmt(const ReturnStmt& stmt);
    void analyzeExprStmt(const ExprStmt& stmt);
    void analyzeAssignStmt(const AssignStmt& stmt);
    void analyzeEmptyStmt(const EmptyStmt& stmt);

    // Expression analysis
    TypeKind analyzeExpr(const Expr& expr);
    TypeKind analyzeNumberExpr(const NumberExpr& expr);
    TypeKind analyzeIdExpr(const IdExpr& expr);
    TypeKind analyzeBinaryExpr(const BinaryExpr& expr);
    TypeKind analyzeUnaryExpr(const UnaryExpr& expr);
    TypeKind analyzeCallExpr(const CallExpr& expr);

    // Helper methods
    void checkMainFunction();
    void checkReturnPath(const FuncDef& funcDef);
    bool isBooleanOp(BinaryOp op) const;
    bool isComparisonOp(BinaryOp op) const;
    bool isArithmeticOp(BinaryOp op) const;
};

} // namespace semantic