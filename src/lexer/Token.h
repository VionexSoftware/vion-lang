#pragma once

#include <string>

enum class TokenType {
    LEFT_PAREN,
    RIGHT_PAREN,
    LEFT_BRACE,
    RIGHT_BRACE,
    LEFT_BRACKET,
    RIGHT_BRACKET,
    COMMA,
    DOT,
    COLON,

    PLUS,
    MINUS,
    STAR,
    SLASH,
    PERCENT,

    PLUS_EQUAL,    // +=
    MINUS_EQUAL,   // -=
    STAR_EQUAL,    // *=
    SLASH_EQUAL,   // /=
    PERCENT_EQUAL, // %=
    PLUS_PLUS,     // ++
    MINUS_MINUS,   // --

    EQUAL,
    EQUAL_EQUAL,
    BANG,
    BANG_EQUAL,

    GREATER,
    GREATER_EQUAL,
    LESS,
    LESS_EQUAL,

    IDENTIFIER,
    NUMBER,
    STRING,

    LET,
    PRINT,
    IF,
    ELSE,
    WHILE,
    FOR,
    IN,
    FN,
    RETURN,
    BREAK,
    CONTINUE,
    AND,
    OR,
    TRUE,
    FALSE,
    NIL,

    CONST,
    TRY,
    CATCH,
    MATCH,
    IMPORT,
    ARROW,         // ->
    QUESTION,      // ?
    INTERP_START,  // start of interpolated string
    INTERP_MID,    // middle segment of interpolated string
    INTERP_END,    // end of interpolated string

    END_OF_FILE
};

struct Token {
    TokenType type;
    std::string lexeme;
    int line;
    int column;
};

inline std::string tokenTypeToString(TokenType type) {
    switch (type) {
        case TokenType::LEFT_PAREN:    return "LEFT_PAREN";
        case TokenType::RIGHT_PAREN:   return "RIGHT_PAREN";
        case TokenType::LEFT_BRACE:    return "LEFT_BRACE";
        case TokenType::RIGHT_BRACE:   return "RIGHT_BRACE";
        case TokenType::LEFT_BRACKET:  return "LEFT_BRACKET";
        case TokenType::RIGHT_BRACKET: return "RIGHT_BRACKET";
        case TokenType::COMMA:         return "COMMA";
        case TokenType::COLON:         return "COLON";
        case TokenType::PLUS:          return "PLUS";
        case TokenType::MINUS:         return "MINUS";
        case TokenType::STAR:          return "STAR";
        case TokenType::SLASH:         return "SLASH";
        case TokenType::PERCENT:       return "PERCENT";
        case TokenType::PLUS_EQUAL:    return "PLUS_EQUAL";
        case TokenType::MINUS_EQUAL:   return "MINUS_EQUAL";
        case TokenType::STAR_EQUAL:    return "STAR_EQUAL";
        case TokenType::SLASH_EQUAL:   return "SLASH_EQUAL";
        case TokenType::PERCENT_EQUAL: return "PERCENT_EQUAL";
        case TokenType::PLUS_PLUS:     return "PLUS_PLUS";
        case TokenType::MINUS_MINUS:   return "MINUS_MINUS";
        case TokenType::EQUAL:         return "EQUAL";
        case TokenType::EQUAL_EQUAL:   return "EQUAL_EQUAL";
        case TokenType::BANG:          return "BANG";
        case TokenType::BANG_EQUAL:    return "BANG_EQUAL";
        case TokenType::GREATER:       return "GREATER";
        case TokenType::GREATER_EQUAL: return "GREATER_EQUAL";
        case TokenType::LESS:          return "LESS";
        case TokenType::LESS_EQUAL:    return "LESS_EQUAL";
        case TokenType::DOT:           return "DOT";
        case TokenType::IDENTIFIER:    return "IDENTIFIER";
        case TokenType::NUMBER:        return "NUMBER";
        case TokenType::STRING:        return "STRING";
        case TokenType::LET:           return "LET";
        case TokenType::PRINT:         return "PRINT";
        case TokenType::IF:            return "IF";
        case TokenType::ELSE:          return "ELSE";
        case TokenType::WHILE:         return "WHILE";
        case TokenType::FOR:           return "FOR";
        case TokenType::IN:            return "IN";
        case TokenType::FN:            return "FN";
        case TokenType::RETURN:        return "RETURN";
        case TokenType::BREAK:         return "BREAK";
        case TokenType::CONTINUE:      return "CONTINUE";
        case TokenType::AND:           return "AND";
        case TokenType::OR:            return "OR";
        case TokenType::TRUE:          return "TRUE";
        case TokenType::FALSE:         return "FALSE";
        case TokenType::NIL:           return "NIL";
        case TokenType::CONST:         return "CONST";
        case TokenType::TRY:           return "TRY";
        case TokenType::CATCH:         return "CATCH";
        case TokenType::MATCH:         return "MATCH";
        case TokenType::IMPORT:        return "IMPORT";
        case TokenType::ARROW:         return "ARROW";
        case TokenType::QUESTION:      return "QUESTION";
        case TokenType::INTERP_START:  return "INTERP_START";
        case TokenType::INTERP_MID:    return "INTERP_MID";
        case TokenType::INTERP_END:    return "INTERP_END";
        case TokenType::END_OF_FILE:   return "EOF";
        default:                       return "UNKNOWN";
    }
}
