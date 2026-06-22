#include "Lexer.h"
#include <cctype>
#include <stdexcept>

// Initialize keyword map
std::unordered_map<std::string, TokenType> Lexer::s_keywords = {
    {"int",     TokenType::TOK_INT},
    {"void",    TokenType::TOK_VOID},
    {"const",   TokenType::TOK_CONST},
    {"if",      TokenType::TOK_IF},
    {"else",    TokenType::TOK_ELSE},
    {"while",   TokenType::TOK_WHILE},
    {"break",   TokenType::TOK_BREAK},
    {"continue",TokenType::TOK_CONTINUE},
    {"return",  TokenType::TOK_RETURN},
};

Lexer::Lexer(std::istream& input)
    : m_input(input), m_line(1), m_col(1), m_prevType(TokenType::TOK_EOF) {}

int Lexer::getChar() {
    int ch = m_input.get();
    if (ch == '\n') {
        m_line++;
        m_col = 1;
    } else if (ch != std::char_traits<char>::eof()) {
        m_col++;
    }
    return ch;
}

void Lexer::ungetChar(int ch) {
    m_input.putback(static_cast<char>(ch));
    if (ch != std::char_traits<char>::eof()) {
        m_col--;
    }
}

bool Lexer::canStartNegativeNumber() const {
    switch (m_prevType) {
        case TokenType::TOK_EOF:
        case TokenType::TOK_ASSIGN:
        case TokenType::TOK_ADD:
        case TokenType::TOK_SUB:
        case TokenType::TOK_MUL:
        case TokenType::TOK_DIV:
        case TokenType::TOK_MOD:
        case TokenType::TOK_LT:
        case TokenType::TOK_GT:
        case TokenType::TOK_LE:
        case TokenType::TOK_GE:
        case TokenType::TOK_EQ:
        case TokenType::TOK_NE:
        case TokenType::TOK_AND:
        case TokenType::TOK_OR:
        case TokenType::TOK_NOT:
        case TokenType::TOK_LPAREN:
        case TokenType::TOK_LBRACE:
        case TokenType::TOK_SEMICOLON:
        case TokenType::TOK_COMMA:
        case TokenType::TOK_RETURN:
            return true;
        default:
            return false;
    }
}

void Lexer::skipWhitespaceAndComments() {
    while (true) {
        int ch = getChar();
        if (ch == std::char_traits<char>::eof()) return;

        if (std::isspace(ch)) continue;

        if (ch == '/') {
            int next = getChar();
            if (next == '/') {
                while (true) {
                    int c = getChar();
                    if (c == '\n' || c == std::char_traits<char>::eof()) break;
                }
                continue;
            } else if (next == '*') {
                while (true) {
                    int c = getChar();
                    if (c == std::char_traits<char>::eof()) return;
                    if (c == '*') {
                        int c2 = getChar();
                        if (c2 == '/') break;
                        if (c2 != std::char_traits<char>::eof()) ungetChar(c2);
                    }
                }
                continue;
            } else {
                ungetChar(next);
                ungetChar(ch);
                return;
            }
        }

        ungetChar(ch);
        return;
    }
}

// Internal: read a raw token, given previous token type for negative number context
static Token readRawToken(std::istream& input, int& line, int& col,
                          TokenType prevType) {
    // Skip whitespace and comments
    while (true) {
        int ch = input.get();
        if (ch == std::char_traits<char>::eof()) {
            return Token(TokenType::TOK_EOF, "", line, col);
        }
        if (ch == '\n') { line++; col = 1; continue; }
        if (ch == '\r') { col = 1; continue; }
        if (std::isspace(ch)) { col++; continue; }

        if (ch == '/') {
            int next = input.get();
            if (next == '/') {
                while (true) {
                    int c = input.get();
                    if (c == '\n' || c == std::char_traits<char>::eof()) { line++; col = 1; break; }
                }
                continue;
            } else if (next == '*') {
                while (true) {
                    int c = input.get();
                    if (c == std::char_traits<char>::eof()) {
                        return Token(TokenType::TOK_EOF, "", line, col);
                    }
                    if (c == '\n') { line++; col = 1; }
                    else col++;
                    if (c == '*') {
                        int c2 = input.get();
                        if (c2 == '/') { col++; break; }
                        if (c2 != std::char_traits<char>::eof()) input.putback(static_cast<char>(c2));
                    }
                }
                continue;
            } else {
                if (next != std::char_traits<char>::eof()) input.putback(static_cast<char>(next));
                input.putback(static_cast<char>(ch));
                // Don't return here - fall through to process ch
                ch = input.get();
                col++;
                // fall through to switch below
            }
        } else {
            col++;
        }

        int startLine = line;
        int startCol = col;

        // Identifier or keyword: [a-zA-Z_][a-zA-Z0-9_]*
        if (std::isalpha(ch) || ch == '_') {
            std::string lexeme;
            lexeme += static_cast<char>(ch);
            while (true) {
                int c = input.get();
                if (c == std::char_traits<char>::eof()) break;
                if (std::isalnum(c) || c == '_') {
                    lexeme += static_cast<char>(c);
                    col++;
                } else {
                    input.putback(static_cast<char>(c));
                    break;
                }
            }
            auto it = Lexer::s_keywords.find(lexeme);
            if (it != Lexer::s_keywords.end()) {
                return Token(it->second, lexeme, startLine, startCol);
            }
            return Token(TokenType::TOK_ID, lexeme, startLine, startCol);
        }

        // Number: [0-9] (possibly preceded by minus handled in the '-' case)
        if (std::isdigit(ch)) {
            std::string lexeme;
            lexeme += static_cast<char>(ch);
            while (true) {
                int c = input.get();
                if (c == std::char_traits<char>::eof()) break;
                if (std::isdigit(c)) {
                    lexeme += static_cast<char>(c);
                    col++;
                } else {
                    input.putback(static_cast<char>(c));
                    break;
                }
            }
            int numValue = 0;
            try { numValue = std::stoi(lexeme); } catch (...) {}
            return Token(TokenType::TOK_NUMBER, lexeme, startLine, startCol, numValue);
        }

        // Minus sign: check for negative number
        if (ch == '-') {
            // Check if previous token allows a negative number literal
            bool allowNeg = false;
            switch (prevType) {
                case TokenType::TOK_EOF:
                case TokenType::TOK_ASSIGN:
                case TokenType::TOK_ADD:
                case TokenType::TOK_SUB:
                case TokenType::TOK_MUL:
                case TokenType::TOK_DIV:
                case TokenType::TOK_MOD:
                case TokenType::TOK_LT:
                case TokenType::TOK_GT:
                case TokenType::TOK_LE:
                case TokenType::TOK_GE:
                case TokenType::TOK_EQ:
                case TokenType::TOK_NE:
                case TokenType::TOK_AND:
                case TokenType::TOK_OR:
                case TokenType::TOK_NOT:
                case TokenType::TOK_LPAREN:
                case TokenType::TOK_LBRACE:
                case TokenType::TOK_SEMICOLON:
                case TokenType::TOK_COMMA:
                case TokenType::TOK_RETURN:
                    allowNeg = true;
                    break;
                default: break;
            }

            int next = input.get();
            if (next != std::char_traits<char>::eof() && std::isdigit(next) && allowNeg) {
                // Negative number
                std::string lexeme = "-";
                lexeme += static_cast<char>(next);
                col++;
                while (true) {
                    int c = input.get();
                    if (c == std::char_traits<char>::eof()) break;
                    if (std::isdigit(c)) {
                        lexeme += static_cast<char>(c);
                        col++;
                    } else {
                        input.putback(static_cast<char>(c));
                        break;
                    }
                }
                int numValue = 0;
                try { numValue = std::stoi(lexeme); } catch (...) {}
                return Token(TokenType::TOK_NUMBER, lexeme, startLine, startCol, numValue);
            } else {
                // Subtraction operator
                if (next != std::char_traits<char>::eof()) input.putback(static_cast<char>(next));
                return Token(TokenType::TOK_SUB, "-", startLine, startCol);
            }
        }

        // All other single/double char tokens
        switch (ch) {
            case '+': return Token(TokenType::TOK_ADD, "+", startLine, startCol);
            case '*': return Token(TokenType::TOK_MUL, "*", startLine, startCol);
            case '/': return Token(TokenType::TOK_DIV, "/", startLine, startCol);
            case '%': return Token(TokenType::TOK_MOD, "%", startLine, startCol);
            case '(': return Token(TokenType::TOK_LPAREN, "(", startLine, startCol);
            case ')': return Token(TokenType::TOK_RPAREN, ")", startLine, startCol);
            case '{': return Token(TokenType::TOK_LBRACE, "{", startLine, startCol);
            case '}': return Token(TokenType::TOK_RBRACE, "}", startLine, startCol);
            case ';': return Token(TokenType::TOK_SEMICOLON, ";", startLine, startCol);
            case ',': return Token(TokenType::TOK_COMMA, ",", startLine, startCol);

            case '!': {
                int n = input.get();
                if (n == '=') { col++; return Token(TokenType::TOK_NE, "!=", startLine, startCol); }
                if (n != std::char_traits<char>::eof()) input.putback(static_cast<char>(n));
                return Token(TokenType::TOK_NOT, "!", startLine, startCol);
            }
            case '=': {
                int n = input.get();
                if (n == '=') { col++; return Token(TokenType::TOK_EQ, "==", startLine, startCol); }
                if (n != std::char_traits<char>::eof()) input.putback(static_cast<char>(n));
                return Token(TokenType::TOK_ASSIGN, "=", startLine, startCol);
            }
            case '<': {
                int n = input.get();
                if (n == '=') { col++; return Token(TokenType::TOK_LE, "<=", startLine, startCol); }
                if (n != std::char_traits<char>::eof()) input.putback(static_cast<char>(n));
                return Token(TokenType::TOK_LT, "<", startLine, startCol);
            }
            case '>': {
                int n = input.get();
                if (n == '=') { col++; return Token(TokenType::TOK_GE, ">=", startLine, startCol); }
                if (n != std::char_traits<char>::eof()) input.putback(static_cast<char>(n));
                return Token(TokenType::TOK_GT, ">", startLine, startCol);
            }
            case '&': {
                int n = input.get();
                if (n == '&') { col++; return Token(TokenType::TOK_AND, "&&", startLine, startCol); }
                if (n != std::char_traits<char>::eof()) input.putback(static_cast<char>(n));
                return Token(TokenType::TOK_ERROR, "&", startLine, startCol);
            }
            case '|': {
                int n = input.get();
                if (n == '|') { col++; return Token(TokenType::TOK_OR, "||", startLine, startCol); }
                if (n != std::char_traits<char>::eof()) input.putback(static_cast<char>(n));
                return Token(TokenType::TOK_ERROR, "|", startLine, startCol);
            }
            default:
                return Token(TokenType::TOK_ERROR,
                             std::string(1, static_cast<char>(ch)), startLine, startCol);
        }
    }
}

Token Lexer::nextToken() {
    if (!m_buffer.empty()) {
        Token t = std::move(m_buffer.front());
        m_buffer.pop_front();
        m_prevType = t.type;
        return t;
    }

    // Determine effective previous type
    TokenType effectivePrev = m_prevType;
    if (!m_buffer.empty()) {
        effectivePrev = m_buffer.back().type;
    }

    Token t = readRawToken(m_input, m_line, m_col, effectivePrev);
    m_prevType = t.type;
    return t;
}

Token Lexer::peekToken(int lookahead) {
    // Fill buffer as needed
    while (static_cast<int>(m_buffer.size()) < lookahead) {
        TokenType prevForNext = m_buffer.empty()
            ? m_prevType
            : m_buffer.back().type;
        m_buffer.push_back(readRawToken(m_input, m_line, m_col, prevForNext));
    }
    return m_buffer[lookahead - 1];
}

bool Lexer::isAtEnd() const {
    // Check if the front of the buffer is EOF
    if (!m_buffer.empty() && m_buffer.front().type == TokenType::TOK_EOF) {
        return true;
    }
    return false;
}
