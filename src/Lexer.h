#ifndef LEXER_H
#define LEXER_H

#include "Token.h"
#include <istream>
#include <deque>
#include <unordered_map>

class Lexer {
public:
    explicit Lexer(std::istream& input);

    // Get the next token (consumes it from the stream)
    Token nextToken();

    // Peek at the next token without consuming it
    Token peekToken(int lookahead = 1);

    // Check if we've reached EOF
    bool isAtEnd() const;

    // Get current line number
    int currentLine() const { return m_line; }

private:
    // Read a single character, updating line/col
    int getChar();
    // Put back a character
    void ungetChar(int ch);
    // Skip whitespace and comments
    void skipWhitespaceAndComments();
    // Read an identifier or keyword
    Token readIdentifier(int startLine, int startCol);
    // Read a number (possibly negative)
    Token readNumber(int startLine, int startCol, bool negative = false);
    // Check if previous token type allows negative number
    bool canStartNegativeNumber() const;

    std::istream& m_input;
    int m_line;
    int m_col;
    TokenType m_prevType;  // previous token type for negative number detection
    std::deque<Token> m_buffer;  // lookahead buffer

public:
    static std::unordered_map<std::string, TokenType> s_keywords;
private:
};

#endif // LEXER_H
