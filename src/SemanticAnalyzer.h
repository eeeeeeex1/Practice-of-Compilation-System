#ifndef SEMANTIC_ANALYZER_H
#define SEMANTIC_ANALYZER_H

#include "AST.h"
#include <stdexcept>
#include <sstream>

class SemanticError : public std::runtime_error {
public:
    SemanticError(const std::string& msg, const Token& tok)
        : std::runtime_error(formatError(msg, tok)), m_line(tok.line), m_col(tok.col) {}

    int line() const { return m_line; }
    int col() const { return m_col; }

private:
    int m_line, m_col;
    static std::string formatError(const std::string& msg, const Token& tok) {
        std::ostringstream oss;
        oss << "Semantic error at line " << tok.line << ", col " << tok.col
            << ": " << msg;
        return oss.str();
    }
};

class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(bool optimize = false);

    void analyze(CompUnit& comp);

private:
    // Expression analysis: returns true if expression is a compile-time constant
    bool analyzeExpr(Expr* expr);
    bool analyzeConstExpr(Expr* expr);  // requires constant, throws on failure

    // Statement analysis
    void analyzeStmt(Stmt* stmt, TypeKind funcReturnType, int& loopDepth);

    // Declaration analysis
    void analyzeDecl(Decl* decl, bool isGlobal);

    // Function analysis
    void analyzeFuncDef(FuncDef* func);

    // Symbol table
    SymbolTable m_symbols;

    // Registered function list (for forward declaration checking)
    std::vector<Symbol> m_functions;

    // Current function being analyzed
    FuncDef* m_currentFunc = nullptr;

    // Optimization flag
    bool m_optimize;

    // Check if expression can be evaluated at compile time
    bool canEvaluateConst(Expr* expr);
    int evaluateConstExpr(Expr* expr);

    // Check if a node is a valid constant initializer
    bool isValidConstInit(Expr* expr);

    // Helper
    [[noreturn]] void error(const std::string& msg, const Token& tok);
};

#endif // SEMANTIC_ANALYZER_H
