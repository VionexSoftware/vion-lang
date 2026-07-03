#include "parser/Parser.h"

#include <stdexcept>

Parser::Parser(std::vector<Token> tokens) : tokens(std::move(tokens)) {}

Program Parser::parse() {
    Program program;

    while (!isAtEnd()) {
        program.statements.push_back(declaration());
    }

    return program;
}

std::unique_ptr<Stmt> Parser::declaration() {
    if (match(TokenType::FN)) return functionDeclaration();
    return statement();
}

std::unique_ptr<Stmt> Parser::functionDeclaration() {
    const Token& name = consume(TokenType::IDENTIFIER, "expected function name after 'fn'.");
    int fnLine = name.line;

    consume(TokenType::LEFT_PAREN, "expected '(' after function name.");

    std::vector<std::string> parameters;
    if (!check(TokenType::RIGHT_PAREN)) {
        do {
            parameters.push_back(consume(TokenType::IDENTIFIER, "expected parameter name.").lexeme);
        } while (match(TokenType::COMMA));
    }

    consume(TokenType::RIGHT_PAREN, "expected ')' after function parameters.");
    consume(TokenType::LEFT_BRACE, "expected '{' before function body.");

    return std::make_unique<FunctionStmt>(name.lexeme, std::move(parameters), blockStatement(), fnLine);
}

std::unique_ptr<Stmt> Parser::statement() {
    if (match(TokenType::LET))      return letStatement();
    if (match(TokenType::PRINT))    return printStatement();
    if (match(TokenType::IF))       return ifStatement();
    if (match(TokenType::WHILE))    return whileStatement();
    if (match(TokenType::FOR))      return forStatement();
    if (match(TokenType::RETURN))   return returnStatement();
    if (match(TokenType::BREAK))    return breakStatement();
    if (match(TokenType::CONTINUE)) return continueStatement();
    if (match(TokenType::LEFT_BRACE)) return blockStatement();

    return expressionStatement();
}

std::unique_ptr<Stmt> Parser::letStatement() {
    const Token& name = consume(TokenType::IDENTIFIER, "expected variable name after 'let'.");
    int stmtLine = name.line;
    consume(TokenType::EQUAL, "expected '=' after variable name.");
    auto value = expression();
    return std::make_unique<LetStmt>(name.lexeme, std::move(value), stmtLine);
}

std::unique_ptr<Stmt> Parser::printStatement() {
    int stmtLine = previous().line;
    std::vector<std::unique_ptr<Expr>> values;
    values.push_back(expression());
    while (match(TokenType::COMMA)) {
        values.push_back(expression());
    }
    return std::make_unique<PrintStmt>(std::move(values), stmtLine);
}

std::unique_ptr<Stmt> Parser::ifStatement() {
    int stmtLine = previous().line;
    auto condition = expression();

    consume(TokenType::LEFT_BRACE, "expected '{' after if condition.");
    auto thenBranch = blockStatement();

    std::unique_ptr<BlockStmt> elseBranch = nullptr;
    if (match(TokenType::ELSE)) {
        consume(TokenType::LEFT_BRACE, "expected '{' after else.");
        elseBranch = blockStatement();
    }

    return std::make_unique<IfStmt>(std::move(condition), std::move(thenBranch), std::move(elseBranch), stmtLine);
}

std::unique_ptr<Stmt> Parser::whileStatement() {
    int stmtLine = previous().line;
    auto condition = expression();

    consume(TokenType::LEFT_BRACE, "expected '{' after while condition.");
    auto body = blockStatement();

    return std::make_unique<WhileStmt>(std::move(condition), std::move(body), stmtLine);
}

std::unique_ptr<Stmt> Parser::forStatement() {
    int stmtLine = previous().line;
    const Token& varName = consume(TokenType::IDENTIFIER, "expected variable name after 'for'.");
    consume(TokenType::IN, "expected 'in' after for variable.");
    auto iterable = expression();

    consume(TokenType::LEFT_BRACE, "expected '{' after for iterable.");
    auto body = blockStatement();

    return std::make_unique<ForStmt>(varName.lexeme, std::move(iterable), std::move(body), stmtLine);
}

std::unique_ptr<Stmt> Parser::returnStatement() {
    int stmtLine = previous().line;
    if (check(TokenType::RIGHT_BRACE) || check(TokenType::END_OF_FILE)) {
        return std::make_unique<ReturnStmt>(nullptr, stmtLine);
    }
    return std::make_unique<ReturnStmt>(expression(), stmtLine);
}

std::unique_ptr<Stmt> Parser::breakStatement() {
    return std::make_unique<BreakStmt>(previous().line);
}

std::unique_ptr<Stmt> Parser::continueStatement() {
    return std::make_unique<ContinueStmt>(previous().line);
}

std::unique_ptr<Stmt> Parser::expressionStatement() {
    int stmtLine = peek().line;
    auto expr = expression();
    return std::make_unique<ExpressionStmt>(std::move(expr), stmtLine);
}

std::unique_ptr<BlockStmt> Parser::blockStatement() {
    std::vector<std::unique_ptr<Stmt>> statements;
    int stmtLine = previous().line;

    while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
        statements.push_back(declaration());
    }

    consume(TokenType::RIGHT_BRACE, "expected '}' after block.");
    return std::make_unique<BlockStmt>(std::move(statements), stmtLine);
}

std::unique_ptr<Expr> Parser::expression() {
    return assignment();
}

std::unique_ptr<Expr> Parser::assignment() {
    auto expr = logicalOr();

    if (match(TokenType::EQUAL)) {
        const Token& equalsToken = previous();
        int assignLine = equalsToken.line;
        auto value = assignment();

        if (const auto* identifier = dynamic_cast<const IdentifierExpr*>(expr.get())) {
            return std::make_unique<AssignmentExpr>(identifier->name, std::move(value), assignLine);
        }

        // arr[i] = value
        if (const auto* indexExpr = dynamic_cast<const IndexExpr*>(expr.get())) {
            // We need to rebuild IndexAssignExpr — but since the ptrs are moved, re-parse is not easy.
            // Use a clone trick via cast + move.
            // Actually, we can't "move" a const. We need a non-const ptr.
            // Let's cast via mutable ref. The unique_ptr owns it so we can release safely.
            auto* mutable_index = const_cast<IndexExpr*>(indexExpr);
            (void)mutable_index;

            // Rebuild from the original expr (which is IndexExpr)
            auto owned = std::unique_ptr<IndexExpr>(static_cast<IndexExpr*>(expr.release()));
            return std::make_unique<IndexAssignExpr>(
                std::move(owned->object),
                std::move(owned->index),
                std::move(value),
                assignLine
            );
        }

        errorAt(equalsToken, "invalid assignment target.");
    }

    return expr;
}

std::unique_ptr<Expr> Parser::logicalOr() {
    auto expr = logicalAnd();

    while (match(TokenType::OR)) {
        int opLine = previous().line;
        std::string op = previous().lexeme;
        auto right = logicalAnd();
        expr = std::make_unique<LogicalExpr>(std::move(expr), op, std::move(right), opLine);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::logicalAnd() {
    auto expr = equality();

    while (match(TokenType::AND)) {
        int opLine = previous().line;
        std::string op = previous().lexeme;
        auto right = equality();
        expr = std::make_unique<LogicalExpr>(std::move(expr), op, std::move(right), opLine);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::equality() {
    auto expr = comparison();

    while (match(TokenType::EQUAL_EQUAL) || match(TokenType::BANG_EQUAL)) {
        int opLine = previous().line;
        std::string op = previous().lexeme;
        auto right = comparison();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right), opLine);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::comparison() {
    auto expr = term();

    while (
        match(TokenType::GREATER) ||
        match(TokenType::GREATER_EQUAL) ||
        match(TokenType::LESS) ||
        match(TokenType::LESS_EQUAL)
    ) {
        int opLine = previous().line;
        std::string op = previous().lexeme;
        auto right = term();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right), opLine);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::term() {
    auto expr = factor();

    while (match(TokenType::PLUS) || match(TokenType::MINUS)) {
        int opLine = previous().line;
        std::string op = previous().lexeme;
        auto right = factor();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right), opLine);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::factor() {
    auto expr = unary();

    while (match(TokenType::STAR) || match(TokenType::SLASH) || match(TokenType::PERCENT)) {
        int opLine = previous().line;
        std::string op = previous().lexeme;
        auto right = unary();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right), opLine);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::unary() {
    if (match(TokenType::BANG) || match(TokenType::MINUS)) {
        int opLine = previous().line;
        std::string op = previous().lexeme;
        auto right = unary();
        return std::make_unique<UnaryExpr>(op, std::move(right), opLine);
    }

    return call();
}

std::unique_ptr<Expr> Parser::call() {
    auto expr = primary();

    while (true) {
        if (match(TokenType::LEFT_PAREN)) {
            expr = finishCall(std::move(expr));
        } else if (match(TokenType::LEFT_BRACKET)) {
            // Index expression: expr[index]
            int bracketLine = previous().line;
            auto index = expression();
            consume(TokenType::RIGHT_BRACKET, "expected ']' after index.");
            expr = std::make_unique<IndexExpr>(std::move(expr), std::move(index), bracketLine);
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<Expr> Parser::finishCall(std::unique_ptr<Expr> callee) {
    int callLine = previous().line;
    std::vector<std::unique_ptr<Expr>> arguments;

    if (!check(TokenType::RIGHT_PAREN)) {
        do {
            arguments.push_back(expression());
        } while (match(TokenType::COMMA));
    }

    consume(TokenType::RIGHT_PAREN, "expected ')' after arguments.");
    return std::make_unique<CallExpr>(std::move(callee), std::move(arguments), callLine);
}

std::unique_ptr<Expr> Parser::primary() {
    if (match(TokenType::NUMBER)) {
        return std::make_unique<NumberExpr>(std::stod(previous().lexeme), previous().line);
    }

    if (match(TokenType::STRING)) {
        return std::make_unique<StringExpr>(previous().lexeme, previous().line);
    }

    if (match(TokenType::TRUE)) {
        return std::make_unique<BooleanExpr>(true, previous().line);
    }

    if (match(TokenType::FALSE)) {
        return std::make_unique<BooleanExpr>(false, previous().line);
    }

    if (match(TokenType::NIL)) {
        return std::make_unique<NilExpr>(previous().line);
    }

    if (match(TokenType::IDENTIFIER)) {
        return std::make_unique<IdentifierExpr>(previous().lexeme, previous().line);
    }

    if (match(TokenType::LEFT_PAREN)) {
        auto expr = expression();
        consume(TokenType::RIGHT_PAREN, "expected ')' after expression.");
        return expr;
    }

    // Map literal: {"key": val, ...} or {}
    if (match(TokenType::LEFT_BRACE)) {
        int mapLine = previous().line;
        std::vector<std::pair<std::string, std::unique_ptr<Expr>>> pairs;

        if (!check(TokenType::RIGHT_BRACE)) {
            // Must be key: value pairs
            do {
                std::string key;
                if (check(TokenType::STRING)) {
                    key = peek().lexeme;
                    advance();
                } else if (check(TokenType::IDENTIFIER)) {
                    key = peek().lexeme;
                    advance();
                } else {
                    errorAtCurrent("expected string or identifier as map key.");
                }
                consume(TokenType::COLON, "expected ':' after map key.");
                auto val = expression();
                pairs.emplace_back(key, std::move(val));
            } while (match(TokenType::COMMA) && !check(TokenType::RIGHT_BRACE));
        }

        consume(TokenType::RIGHT_BRACE, "expected '}' after map entries.");
        return std::make_unique<MapExpr>(std::move(pairs), mapLine);
    }

    // Array literal: [expr, expr, ...]
    if (match(TokenType::LEFT_BRACKET)) {
        int arrLine = previous().line;
        std::vector<std::unique_ptr<Expr>> elements;

        if (!check(TokenType::RIGHT_BRACKET)) {
            do {
                elements.push_back(expression());
            } while (match(TokenType::COMMA));
        }

        consume(TokenType::RIGHT_BRACKET, "expected ']' after array elements.");
        return std::make_unique<ArrayExpr>(std::move(elements), arrLine);
    }

    errorAtCurrent("expected expression.");
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::check(TokenType type) const {
    if (isAtEnd()) return false;
    return peek().type == type;
}

bool Parser::isAtEnd() const {
    return peek().type == TokenType::END_OF_FILE;
}

const Token& Parser::advance() {
    if (!isAtEnd()) current++;
    return previous();
}

const Token& Parser::peek() const {
    return tokens[current];
}

const Token& Parser::peekNext() const {
    if (current + 1 >= static_cast<int>(tokens.size())) return tokens.back();
    return tokens[current + 1];
}

const Token& Parser::previous() const {
    return tokens[current - 1];
}

const Token& Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    errorAtCurrent(message);
}

void Parser::errorAt(const Token& token, const std::string& message) const {
    throw std::runtime_error(
        "Parser Error at line " + std::to_string(token.line) +
        ", column " + std::to_string(token.column) +
        ": " + message
    );
}

void Parser::errorAtCurrent(const std::string& message) const {
    errorAt(peek(), message);
}
