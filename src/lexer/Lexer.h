#pragma once

#include <string>
#include <vector>
#include "lexer/Token.h"

class Lexer {
public:
    explicit Lexer(std::string source);
    std::vector<Token> scanTokens();

private:
    std::string source;
    std::vector<Token> tokens;

    int start = 0;
    int current = 0;
    int line = 1;
    int column = 1;
    int tokenLine = 1;
    int tokenColumn = 1;
    int interpDepth = 0;  // tracks {} nesting inside interpolated strings

    bool isAtEnd() const;
    char advance();
    char peek() const;
    char peekNext() const;
    bool match(char expected);

    void scanToken();
    void addToken(TokenType type);
    void addToken(TokenType type, const std::string& lexeme);

    void identifier();
    void number();
    void string();
    void multilineString();
    void interpolatedString();

    bool isAlpha(char c) const;
    bool isDigit(char c) const;
    bool isAlphaNumeric(char c) const;
};
