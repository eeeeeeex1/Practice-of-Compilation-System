#include "Parser.h"
#include <stdexcept>
#include <sstream>

Parser::Parser(Lexer& lexer) : m_lexer(lexer), m_havePeeked(false) {}

Token Parser::peek() {
    if (!m_havePeeked) {
        m_current = m_lexer.nextToken();
        m_havePeeked = true;
    }
    return m_current;
}

Token Parser::peek2() {
    // Save state, peek the token after m_current
    Token cur = peek();
    Token next = m_lexer.peekToken();
    // m_lexer peekToken doesn't consume, so we're fine
    // But we need to get the second token ahead.
    // Since we already have m_current buffered, the lexer's next token
    // is the one after m_current. So peekToken() should give us that.
    // However, our Lexer::peekToken implementation has issues.
    // Let me use a different approach: just get the next token and buffer it.
    return next;
}

Token Parser::consume() {
    Token t = peek();
    m_havePeeked = false;
    return t;
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        consume();
        return true;
    }
    return false;
}

bool Parser::check(TokenType type) {
    return peek().type == type;
}

Token Parser::expect(TokenType type) {
    Token t = peek();
    if (t.type != type) {
        std::ostringstream oss;
        oss << "Expected '" << tokenTypeName(type) << "', got '"
            << tokenTypeName(t.type) << "'";
        error(oss.str(), t);
    }
    return consume();
}

bool Parser::isAtEnd() {
    return check(TokenType::TOK_EOF);
}

[[noreturn]] void Parser::error(const std::string& msg, const Token& tok) {
    std::ostringstream oss;
    oss << "Parse error at line " << tok.line << ", col " << tok.col
        << ": " << msg << " (got '" << tok.lexeme << "')";
    throw std::runtime_error(oss.str());
}

// === Compilation Unit ===

std::unique_ptr<CompUnit> Parser::parseCompUnit() {
    auto comp = std::make_unique<CompUnit>();

    while (!isAtEnd()) {
        Token t = peek();

        if (t.type == TokenType::TOK_CONST) {
            // const declaration (must be global)
            comp->globals.push_back(parseConstDecl());

        } else if (t.type == TokenType::TOK_INT || t.type == TokenType::TOK_VOID) {
            // Could be variable declaration or function definition
            FuncReturnType retType = (t.type == TokenType::TOK_INT)
                ? FuncReturnType::INT : FuncReturnType::VOID;
            Token typeTok = consume();  // consume int/void

            Token idTok = expect(TokenType::TOK_ID);
            Token next = peek();

            if (next.type == TokenType::TOK_LPAREN) {
                // Function definition
                consume();  // (
                std::vector<std::string> params;

                if (!check(TokenType::TOK_RPAREN)) {
                    // Parse first param
                    expect(TokenType::TOK_INT);
                    Token paramName = expect(TokenType::TOK_ID);
                    params.push_back(paramName.lexeme);

                    while (match(TokenType::TOK_COMMA)) {
                        expect(TokenType::TOK_INT);
                        Token pn = expect(TokenType::TOK_ID);
                        params.push_back(pn.lexeme);
                    }
                }

                expect(TokenType::TOK_RPAREN);
                BlockPtr body = parseBlock();

                auto funcDef = std::make_unique<FuncDef>(
                    retType, idTok.lexeme, std::move(params), std::move(body), idTok);
                comp->functions.push_back(std::move(funcDef));

            } else if (next.type == TokenType::TOK_ASSIGN) {
                // Global variable declaration
                if (retType == FuncReturnType::VOID) {
                    error("Global variable cannot be void", next);
                }
                consume();  // =
                ExprPtr init = parseExpr();
                Token semi = expect(TokenType::TOK_SEMICOLON);

                auto decl = std::make_unique<Decl>(
                    DeclKind::VARIABLE, idTok, idTok.lexeme, std::move(init), true);
                comp->globals.push_back(std::move(decl));

            } else {
                error("Expected '(' for function or '=' for variable after ID", next);
            }

        } else {
            error("Expected declaration or function definition", t);
        }
    }

    return comp;
}

// === Declarations ===

DeclPtr Parser::parseDecl() {
    Token t = peek();
    if (t.type == TokenType::TOK_CONST) {
        return parseConstDecl();
    }
    return parseVarDecl();
}

DeclPtr Parser::parseConstDecl() {
    Token constTok = expect(TokenType::TOK_CONST);
    expect(TokenType::TOK_INT);
    Token idTok = expect(TokenType::TOK_ID);
    expect(TokenType::TOK_ASSIGN);
    ExprPtr init = parseExpr();
    expect(TokenType::TOK_SEMICOLON);

    return std::make_unique<Decl>(
        DeclKind::CONSTANT, idTok, idTok.lexeme, std::move(init), false);
}

DeclPtr Parser::parseVarDecl() {
    Token intTok = expect(TokenType::TOK_INT);
    Token idTok = expect(TokenType::TOK_ID);
    expect(TokenType::TOK_ASSIGN);
    ExprPtr init = parseExpr();
    expect(TokenType::TOK_SEMICOLON);

    return std::make_unique<Decl>(
        DeclKind::VARIABLE, idTok, idTok.lexeme, std::move(init), false);
}

// === Statements ===

StmtPtr Parser::parseStmt() {
    Token t = peek();

    switch (t.type) {
        case TokenType::TOK_LBRACE:
            return std::make_unique<BlockStmt>(t, parseBlock());

        case TokenType::TOK_SEMICOLON: {
            consume();
            return std::make_unique<EmptyStmt>(t);
        }

        case TokenType::TOK_CONST: {
            auto decl = parseConstDecl();
            return std::make_unique<DeclStmt>(decl->tok, std::move(decl));
        }

        case TokenType::TOK_INT: {
            // Could be a variable declaration
            // Peek ahead to check: int ID = ... is var decl
            auto decl = parseVarDecl();
            return std::make_unique<DeclStmt>(decl->tok, std::move(decl));
        }

        case TokenType::TOK_IF: {
            consume();  // if
            expect(TokenType::TOK_LPAREN);
            ExprPtr condition = parseExpr();
            expect(TokenType::TOK_RPAREN);
            StmtPtr thenStmt = parseStmt();
            StmtPtr elseStmt = nullptr;
            if (match(TokenType::TOK_ELSE)) {
                elseStmt = parseStmt();
            }
            return std::make_unique<IfStmt>(t, std::move(condition),
                                            std::move(thenStmt), std::move(elseStmt));
        }

        case TokenType::TOK_WHILE: {
            consume();  // while
            expect(TokenType::TOK_LPAREN);
            ExprPtr condition = parseExpr();
            expect(TokenType::TOK_RPAREN);
            StmtPtr body = parseStmt();
            return std::make_unique<WhileStmt>(t, std::move(condition), std::move(body));
        }

        case TokenType::TOK_BREAK: {
            consume();
            expect(TokenType::TOK_SEMICOLON);
            return std::make_unique<BreakStmt>(t);
        }

        case TokenType::TOK_CONTINUE: {
            consume();
            expect(TokenType::TOK_SEMICOLON);
            return std::make_unique<ContinueStmt>(t);
        }

        case TokenType::TOK_RETURN: {
            consume();  // return
            ExprPtr expr = nullptr;
            bool hasExpr = false;
            if (!check(TokenType::TOK_SEMICOLON)) {
                expr = parseExpr();
                hasExpr = true;
            }
            expect(TokenType::TOK_SEMICOLON);
            return std::make_unique<ReturnStmt>(t, std::move(expr), hasExpr);
        }

        case TokenType::TOK_ID: {
            // Could be assignment (ID = Expr ;) or expression statement (Expr ;)
            Token idTok = peek();
            Token next = m_lexer.peekToken();
            if (next.type == TokenType::TOK_ASSIGN) {
                consume();  // ID
                consume();  // =
                ExprPtr value = parseExpr();
                expect(TokenType::TOK_SEMICOLON);
                return std::make_unique<AssignStmt>(idTok, idTok.lexeme, std::move(value));
            } else {
                // Expression statement
                ExprPtr expr = parseExpr();
                expect(TokenType::TOK_SEMICOLON);
                return std::make_unique<ExprStmt>(t, std::move(expr));
            }
        }

        default:
            // Try expression statement
            {
                ExprPtr expr = parseExpr();
                expect(TokenType::TOK_SEMICOLON);
                return std::make_unique<ExprStmt>(t, std::move(expr));
            }
    }
}

BlockPtr Parser::parseBlock() {
    Token braceTok = expect(TokenType::TOK_LBRACE);
    auto block = std::make_unique<Block>();

    while (!check(TokenType::TOK_RBRACE) && !isAtEnd()) {
        block->stmts.push_back(parseStmt());
    }

    expect(TokenType::TOK_RBRACE);
    return block;
}

// === Expressions ===

ExprPtr Parser::parseExpr() {
    return parseLOrExpr();
}

// LOrExpr → LAndExpr ( "||" LAndExpr )*
ExprPtr Parser::parseLOrExpr() {
    ExprPtr left = parseLAndExpr();
    Token tok = peek();

    while (tok.type == TokenType::TOK_OR) {
        consume();  // ||
        ExprPtr right = parseLAndExpr();
        left = std::make_unique<BinaryExpr>(tok, TokenType::TOK_OR,
                                            std::move(left), std::move(right));
        tok = peek();
    }

    return left;
}

// LAndExpr → RelExpr ( "&&" RelExpr )*
ExprPtr Parser::parseLAndExpr() {
    ExprPtr left = parseRelExpr();
    Token tok = peek();

    while (tok.type == TokenType::TOK_AND) {
        consume();  // &&
        ExprPtr right = parseRelExpr();
        left = std::make_unique<BinaryExpr>(tok, TokenType::TOK_AND,
                                            std::move(left), std::move(right));
        tok = peek();
    }

    return left;
}

// RelExpr -> AddExpr ( ( "<" | ">" | "<=" | ">=" | "==" | "!=" ) AddExpr )*
ExprPtr Parser::parseRelExpr() {
    ExprPtr left = parseAddExpr();
    Token tok = peek();

    while (tok.type == TokenType::TOK_LT || tok.type == TokenType::TOK_GT ||
           tok.type == TokenType::TOK_LE || tok.type == TokenType::TOK_GE ||
           tok.type == TokenType::TOK_EQ || tok.type == TokenType::TOK_NE) {
        consume();
        ExprPtr right = parseAddExpr();
        left = std::make_unique<BinaryExpr>(tok, tok.type,
                                            std::move(left), std::move(right));
        tok = peek();
    }

    return left;
}

// AddExpr → MulExpr ( ( "+" | "-" ) MulExpr )*
ExprPtr Parser::parseAddExpr() {
    ExprPtr left = parseMulExpr();
    Token tok = peek();

    while (tok.type == TokenType::TOK_ADD || tok.type == TokenType::TOK_SUB) {
        consume();
        ExprPtr right = parseMulExpr();
        left = std::make_unique<BinaryExpr>(tok, tok.type,
                                            std::move(left), std::move(right));
        tok = peek();
    }

    return left;
}

// MulExpr → UnaryExpr ( ( "*" | "/" | "%" ) UnaryExpr )*
ExprPtr Parser::parseMulExpr() {
    ExprPtr left = parseUnaryExpr();
    Token tok = peek();

    while (tok.type == TokenType::TOK_MUL || tok.type == TokenType::TOK_DIV ||
           tok.type == TokenType::TOK_MOD) {
        consume();
        ExprPtr right = parseUnaryExpr();
        left = std::make_unique<BinaryExpr>(tok, tok.type,
                                            std::move(left), std::move(right));
        tok = peek();
    }

    return left;
}

// UnaryExpr → PrimaryExpr | ( "+" | "-" | "!" ) UnaryExpr
ExprPtr Parser::parseUnaryExpr() {
    Token tok = peek();

    if (tok.type == TokenType::TOK_SUB || tok.type == TokenType::TOK_ADD ||
        tok.type == TokenType::TOK_NOT) {
        consume();
        ExprPtr operand = parseUnaryExpr();
        return std::make_unique<UnaryExpr>(tok, tok.type, std::move(operand));
    }

    return parsePrimaryExpr();
}

// PrimaryExpr → ID | NUMBER | "(" Expr ")"
//             | ID "(" ( Expr ( "," Expr )* )? ")"
ExprPtr Parser::parsePrimaryExpr() {
    Token tok = peek();

    // Number literal
    if (tok.type == TokenType::TOK_NUMBER) {
        consume();
        return std::make_unique<NumberExpr>(tok, tok.numValue);
    }

    // Parenthesized expression
    if (tok.type == TokenType::TOK_LPAREN) {
        consume();  // (
        ExprPtr expr = parseExpr();
        expect(TokenType::TOK_RPAREN);
        return expr;
    }

    // Identifier (variable reference or function call)
    if (tok.type == TokenType::TOK_ID) {
        Token idTok = consume();
        Token next = peek();

        if (next.type == TokenType::TOK_LPAREN) {
            // Function call
            consume();  // (
            std::vector<ExprPtr> args;

            if (!check(TokenType::TOK_RPAREN)) {
                args.push_back(parseExpr());
                while (match(TokenType::TOK_COMMA)) {
                    args.push_back(parseExpr());
                }
            }

            expect(TokenType::TOK_RPAREN);
            return std::make_unique<CallExpr>(idTok, idTok.lexeme, std::move(args));
        }

        // Variable/constant reference
        return std::make_unique<IdExpr>(idTok, idTok.lexeme);
    }

    error("Expected expression", tok);
}
