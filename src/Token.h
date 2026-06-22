#ifndef TOKEN_H
#define TOKEN_H

#include <string>
#include <unordered_map>
#include <cstdint>

enum class TokenType {
    // Keywords
    TOK_INT,
    TOK_VOID,
    TOK_CONST,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_RETURN,

    // Operators
    TOK_ADD,        // +
    TOK_SUB,        // -
    TOK_MUL,        // *
    TOK_DIV,        // /
    TOK_MOD,        // %
    TOK_LT,         // <
    TOK_GT,         // >
    TOK_LE,         // <=
    TOK_GE,         // >=
    TOK_EQ,         // ==
    TOK_NE,         // !=
    TOK_AND,        // &&
    TOK_OR,         // ||
    TOK_NOT,        // !
    TOK_ASSIGN,     // =

    // Delimiters
    TOK_LPAREN,     // (
    TOK_RPAREN,     // )
    TOK_LBRACE,     // {
    TOK_RBRACE,     // }
    TOK_SEMICOLON,  // ;
    TOK_COMMA,      // ,

    // Literals and identifiers
    TOK_ID,
    TOK_NUMBER,

    // Special
    TOK_EOF,
    TOK_ERROR
};

struct Token {
    TokenType type;
    std::string lexeme;
    int line;
    int col;
    int numValue;  // valid only for TOK_NUMBER

    Token() : type(TokenType::TOK_EOF), line(0), col(0), numValue(0) {}
    Token(TokenType t, std::string lex, int l, int c, int nv = 0)
        : type(t), lexeme(std::move(lex)), line(l), col(c), numValue(nv) {}
};

// Get a human-readable name for a token type
inline const char* tokenTypeName(TokenType t) {
    switch (t) {
        case TokenType::TOK_INT:       return "int";
        case TokenType::TOK_VOID:      return "void";
        case TokenType::TOK_CONST:     return "const";
        case TokenType::TOK_IF:        return "if";
        case TokenType::TOK_ELSE:      return "else";
        case TokenType::TOK_WHILE:     return "while";
        case TokenType::TOK_BREAK:     return "break";
        case TokenType::TOK_CONTINUE:  return "continue";
        case TokenType::TOK_RETURN:    return "return";
        case TokenType::TOK_ADD:       return "+";
        case TokenType::TOK_SUB:       return "-";
        case TokenType::TOK_MUL:       return "*";
        case TokenType::TOK_DIV:       return "/";
        case TokenType::TOK_MOD:       return "%";
        case TokenType::TOK_LT:        return "<";
        case TokenType::TOK_GT:        return ">";
        case TokenType::TOK_LE:        return "<=";
        case TokenType::TOK_GE:        return ">=";
        case TokenType::TOK_EQ:        return "==";
        case TokenType::TOK_NE:        return "!=";
        case TokenType::TOK_AND:       return "&&";
        case TokenType::TOK_OR:        return "||";
        case TokenType::TOK_NOT:       return "!";
        case TokenType::TOK_ASSIGN:    return "=";
        case TokenType::TOK_LPAREN:    return "(";
        case TokenType::TOK_RPAREN:    return ")";
        case TokenType::TOK_LBRACE:    return "{";
        case TokenType::TOK_RBRACE:    return "}";
        case TokenType::TOK_SEMICOLON: return ";";
        case TokenType::TOK_COMMA:     return ",";
        case TokenType::TOK_ID:        return "identifier";
        case TokenType::TOK_NUMBER:    return "number";
        case TokenType::TOK_EOF:       return "EOF";
        case TokenType::TOK_ERROR:     return "ERROR";
    }
    return "UNKNOWN";
}

#endif // TOKEN_H
