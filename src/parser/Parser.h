#pragma once

#include <memory>
#include <string>
#include <vector>

#include "lexer/Token.h"
#include "parser/AST.h"

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    Program parse();

private:
    std::vector<Token> tokens;
    int current = 0;

    std::unique_ptr<Stmt> declaration();
    std::unique_ptr<Stmt> functionDeclaration();
    std::unique_ptr<Stmt> statement();
    std::unique_ptr<Stmt> letStatement();
    std::unique_ptr<Stmt> printStatement();
    std::unique_ptr<Stmt> ifStatement();
    std::unique_ptr<Stmt> whileStatement();
    std::unique_ptr<Stmt> forStatement();
    std::unique_ptr<Stmt> returnStatement();
    std::unique_ptr<Stmt> breakStatement();
    std::unique_ptr<Stmt> continueStatement();
    std::unique_ptr<Stmt> expressionStatement();
    std::unique_ptr<BlockStmt> blockStatement();

    std::unique_ptr<Expr> expression();
    std::unique_ptr<Expr> assignment();
    std::unique_ptr<Expr> logicalOr();
    std::unique_ptr<Expr> logicalAnd();
    std::unique_ptr<Expr> equality();
    std::unique_ptr<Expr> comparison();
    std::unique_ptr<Expr> term();
    std::unique_ptr<Expr> factor();
    std::unique_ptr<Expr> unary();
    std::unique_ptr<Expr> call();
    std::unique_ptr<Expr> finishCall(std::unique_ptr<Expr> callee);
    std::unique_ptr<Expr> primary();

    bool match(TokenType type);
    bool check(TokenType type) const;
    bool isAtEnd() const;

    const Token& advance();
    const Token& peek() const;
    const Token& peekNext() const;
    const Token& previous() const;
    const Token& consume(TokenType type, const std::string& message);

    [[noreturn]] void errorAt(const Token& token, const std::string& message) const;
    [[noreturn]] void errorAtCurrent(const std::string& message) const;
};
