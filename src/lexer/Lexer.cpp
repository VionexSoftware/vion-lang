#include "lexer/Lexer.h"

#include <cctype>
#include <stdexcept>
#include <unordered_map>

Lexer::Lexer(std::string source) : source(std::move(source)) {
    // Strip UTF-8 BOM if present
    if (
        this->source.size() >= 3 &&
        static_cast<unsigned char>(this->source[0]) == 0xEF &&
        static_cast<unsigned char>(this->source[1]) == 0xBB &&
        static_cast<unsigned char>(this->source[2]) == 0xBF
    ) {
        this->source.erase(0, 3);
    }
}

std::vector<Token> Lexer::scanTokens() {
    while (!isAtEnd()) {
        start = current;
        tokenLine = line;
        tokenColumn = column;
        scanToken();
    }

    tokens.push_back(Token{TokenType::END_OF_FILE, "", line, column});
    return tokens;
}

bool Lexer::isAtEnd() const {
    return current >= static_cast<int>(source.length());
}

char Lexer::advance() {
    char c = source[current++];
    column++;
    return c;
}

char Lexer::peek() const {
    if (isAtEnd()) return '\0';
    return source[current];
}

char Lexer::peekNext() const {
    if (current + 1 >= static_cast<int>(source.length())) return '\0';
    return source[current + 1];
}

bool Lexer::match(char expected) {
    if (isAtEnd()) return false;
    if (source[current] != expected) return false;
    current++;
    column++;
    return true;
}

void Lexer::scanToken() {
    char c = advance();

    switch (c) {
        case '(':
            addToken(TokenType::LEFT_PAREN);
            break;
        case ')':
            addToken(TokenType::RIGHT_PAREN);
            break;
        case '{':
            addToken(TokenType::LEFT_BRACE);
            break;
        case '}':
            if (interpDepth > 0) { --interpDepth; interpolatedString(); break; }
            addToken(TokenType::RIGHT_BRACE);
            break;
        case '[':
            addToken(TokenType::LEFT_BRACKET);
            break;
        case ']':
            addToken(TokenType::RIGHT_BRACKET);
            break;
        case ',':
            addToken(TokenType::COMMA);
            break;
        case '.':
            addToken(TokenType::DOT);
            break;
        case ':':
            addToken(TokenType::COLON);
            break;
        case '?':
            addToken(TokenType::QUESTION);
            break;
        case '+':
            addToken(match('=') ? TokenType::PLUS_EQUAL
                   : match('+') ? TokenType::PLUS_PLUS
                   : TokenType::PLUS);
            break;
        case '-':
            addToken(match('=') ? TokenType::MINUS_EQUAL
                   : match('-') ? TokenType::MINUS_MINUS
                   : match('>') ? TokenType::ARROW
                   : TokenType::MINUS);
            break;
        case '*':
            addToken(match('=') ? TokenType::STAR_EQUAL : TokenType::STAR);
            break;
        case '%':
            addToken(match('=') ? TokenType::PERCENT_EQUAL : TokenType::PERCENT);
            break;
        case '/':
            if (match('/')) {
                while (peek() != '\n' && !isAtEnd()) advance();
            } else if (match('=')) {
                addToken(TokenType::SLASH_EQUAL);
            } else {
                addToken(TokenType::SLASH);
            }
            break;
        case '=':
            addToken(match('=') ? TokenType::EQUAL_EQUAL : TokenType::EQUAL);
            break;
        case '!':
            addToken(match('=') ? TokenType::BANG_EQUAL : TokenType::BANG);
            break;
        case '>':
            addToken(match('=') ? TokenType::GREATER_EQUAL : TokenType::GREATER);
            break;
        case '<':
            addToken(match('=') ? TokenType::LESS_EQUAL : TokenType::LESS);
            break;
        case ' ':
        case '\r':
        case '\t':
            break;
        case '\n':
            line++;
            column = 1;
            break;
        case '"':
            if (peek() == '"' && peekNext() == '"') { advance(); advance(); multilineString(); }
            else { string(); }
            break;
        default:
            if (isDigit(c)) {
                number();
            } else if (isAlpha(c)) {
                identifier();
            } else {
                throw std::runtime_error(
                    "Lexer Error at line " + std::to_string(tokenLine) +
                    ", column " + std::to_string(tokenColumn) +
                    ": unexpected character '" + std::string(1, c) + "'."
                );
            }
            break;
    }
}

void Lexer::addToken(TokenType type) {
    addToken(type, source.substr(start, current - start));
}

void Lexer::addToken(TokenType type, const std::string& lexeme) {
    tokens.push_back(Token{type, lexeme, tokenLine, tokenColumn});
}

void Lexer::identifier() {
    while (isAlphaNumeric(peek())) advance();

    std::string text = source.substr(start, current - start);

    static const std::unordered_map<std::string, TokenType> keywords = {
        {"let",      TokenType::LET},
        {"print",    TokenType::PRINT},
        {"if",       TokenType::IF},
        {"else",     TokenType::ELSE},
        {"while",    TokenType::WHILE},
        {"for",      TokenType::FOR},
        {"in",       TokenType::IN},
        {"fn",       TokenType::FN},
        {"return",   TokenType::RETURN},
        {"break",    TokenType::BREAK},
        {"continue", TokenType::CONTINUE},
        {"and",      TokenType::AND},
        {"or",       TokenType::OR},
        {"true",     TokenType::TRUE},
        {"false",    TokenType::FALSE},
        {"nil",      TokenType::NIL},
        {"const",    TokenType::CONST},
        {"try",      TokenType::TRY},
        {"catch",    TokenType::CATCH},
        {"match",    TokenType::MATCH},
        {"import",   TokenType::IMPORT}
    };

    auto found = keywords.find(text);
    if (found != keywords.end()) {
        addToken(found->second, text);
    } else {
        addToken(TokenType::IDENTIFIER, text);
    }
}

void Lexer::number() {
    while (isDigit(peek())) advance();

    if (peek() == '.' && isDigit(peekNext())) {
        advance();
        while (isDigit(peek())) advance();
    }

    addToken(TokenType::NUMBER);
}

void Lexer::string() {
    std::string value;
    bool hasInterp = false;

    while (peek() != '"' && !isAtEnd()) {
        char ch = peek();

        if (ch == '{') {
            // String interpolation: emit what we have, switch to expression mode
            addToken(hasInterp ? TokenType::INTERP_MID : TokenType::INTERP_START, value);
            hasInterp = true;
            current++; column++;
            ++interpDepth;
            return; // main loop will lex the expression; '}' handler calls interpolatedString()
        }

        if (ch == '\n') {
            line++;
            column = 1;
            value += '\n';
            current++;
        } else if (ch == '\\') {
            current++; column++;
            if (isAtEnd()) break;
            char escaped = source[current++]; column++;
            switch (escaped) {
                case 'n':  value += '\n'; break;
                case 't':  value += '\t'; break;
                case 'r':  value += '\r'; break;
                case '\\': value += '\\'; break;
                case '"':  value += '"';  break;
                case '0':  value += '\0'; break;
                case '{':  value += '{';  break;
                default:   value += '\\'; value += escaped; break;
            }
        } else {
            value += ch;
            current++; column++;
        }
    }

    if (isAtEnd()) {
        throw std::runtime_error(
            "Lexer Error at line " + std::to_string(tokenLine) +
            ", column " + std::to_string(tokenColumn) +
            ": unterminated string."
        );
    }

    advance(); // closing "

    if (hasInterp) {
        addToken(TokenType::INTERP_END, value);
    } else {
        addToken(TokenType::STRING, value);
    }
}

void Lexer::interpolatedString() {
    // Called after '}' inside an interpolated string — resume scanning the string
    std::string value;

    while (peek() != '"' && !isAtEnd()) {
        char ch = peek();

        if (ch == '{') {
            addToken(TokenType::INTERP_MID, value);
            current++; column++;
            ++interpDepth;
            return;
        }

        if (ch == '\n') {
            line++; column = 1; value += '\n'; current++;
        } else if (ch == '\\') {
            current++; column++;
            if (isAtEnd()) break;
            char escaped = source[current++]; column++;
            switch (escaped) {
                case 'n':  value += '\n'; break;
                case 't':  value += '\t'; break;
                case 'r':  value += '\r'; break;
                case '\\': value += '\\'; break;
                case '"':  value += '"';  break;
                case '{':  value += '{';  break;
                default:   value += '\\'; value += escaped; break;
            }
        } else {
            value += ch; current++; column++;
        }
    }

    if (isAtEnd()) {
        throw std::runtime_error("Lexer Error at line " + std::to_string(tokenLine) +
            ", column " + std::to_string(tokenColumn) + ": unterminated interpolated string.");
    }

    advance(); // closing "
    addToken(TokenType::INTERP_END, value);
}

void Lexer::multilineString() {
    std::string value;

    while (!isAtEnd()) {
        if (peek() == '"' && peekNext() == '"') {
            // Check for third "
            if (current + 2 < static_cast<int>(source.length()) && source[current + 2] == '"') {
                advance(); advance(); advance(); // consume """
                addToken(TokenType::STRING, value);
                return;
            }
        }

        char ch = peek();
        if (ch == '\n') { line++; column = 1; }
        value += ch;
        current++; column++;
    }

    throw std::runtime_error("Lexer Error at line " + std::to_string(tokenLine) +
        ", column " + std::to_string(tokenColumn) + ": unterminated multi-line string.");
}

bool Lexer::isAlpha(char c) const {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool Lexer::isDigit(char c) const {
    return std::isdigit(static_cast<unsigned char>(c));
}

bool Lexer::isAlphaNumeric(char c) const {
    return isAlpha(c) || isDigit(c);
}
