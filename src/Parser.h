#ifndef PARSER_H
#define PARSER_H

#include "AST.h"
#include "Lexer.h"
#include <memory>

class Parser {
public:
    explicit Parser(Lexer& lexer);
    std::unique_ptr<CompUnit> parseCompUnit();

private:
    // Declarations
    DeclPtr parseDecl();
    DeclPtr parseConstDecl();
    DeclPtr parseVarDecl();

    // Functions
    FuncDefPtr parseFuncDef(FuncReturnType retType, Token typeTok);

    // Statements
    StmtPtr parseStmt();
    BlockPtr parseBlock();
    StmtPtr parseExprOrAssign();

    // Expressions (in order of precedence)
    ExprPtr parseExpr();
    ExprPtr parseLOrExpr();
    ExprPtr parseLAndExpr();
    ExprPtr parseRelExpr();
    ExprPtr parseAddExpr();
    ExprPtr parseMulExpr();
    ExprPtr parseUnaryExpr();
    ExprPtr parsePrimaryExpr();

    // Helper methods
    Token consume();
    Token peek();
    Token peek2();
    bool match(TokenType type);
    bool check(TokenType type);
    Token expect(TokenType type);
    bool isAtEnd();

    [[noreturn]] void error(const std::string& msg, const Token& tok);

    Lexer& m_lexer;
    Token m_current;
    bool m_havePeeked;
};

#endif // PARSER_H
