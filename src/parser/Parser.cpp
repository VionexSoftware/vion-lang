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

    std::vector<std::pair<std::string, std::shared_ptr<Expr>>> parameters;
    if (!check(TokenType::RIGHT_PAREN)) {
        do {
            std::string paramName = consume(TokenType::IDENTIFIER, "expected parameter name.").lexeme;
            std::shared_ptr<Expr> defaultVal = nullptr;
            if (match(TokenType::EQUAL)) {
                defaultVal = expression();
            }
            parameters.push_back({paramName, std::move(defaultVal)});
        } while (match(TokenType::COMMA));
    }

    consume(TokenType::RIGHT_PAREN, "expected ')' after function parameters.");
    consume(TokenType::LEFT_BRACE, "expected '{' before function body.");

    return std::make_unique<FunctionStmt>(name.lexeme, std::move(parameters), blockStatement(), fnLine);
}

std::unique_ptr<Stmt> Parser::statement() {
    if (match(TokenType::LET))      return letStatement();
    if (match(TokenType::CONST))    return constStatement();
    if (match(TokenType::TRY))      return tryCatchStatement();
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
        if (match(TokenType::IF)) {
            // else if — parse nested IfStmt, wrap in a single-statement block
            auto nested = ifStatement();
            int nestedLine = nested->line;
            std::vector<std::unique_ptr<Stmt>> stmts;
            stmts.push_back(std::move(nested));
            elseBranch = std::make_unique<BlockStmt>(std::move(stmts), nestedLine);
        } else {
            consume(TokenType::LEFT_BRACE, "expected '{' or 'if' after 'else'.");
            elseBranch = blockStatement();
        }
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


std::unique_ptr<Stmt> Parser::constStatement() {
    const Token& name = consume(TokenType::IDENTIFIER, "expected variable name after 'const'.");
    int stmtLine = name.line;
    consume(TokenType::EQUAL, "expected '=' after const name.");
    auto value = expression();
    return std::make_unique<ConstStmt>(name.lexeme, std::move(value), stmtLine);
}

std::unique_ptr<Stmt> Parser::tryCatchStatement() {
    int stmtLine = previous().line;
    consume(TokenType::LEFT_BRACE, "expected '{' after 'try'.");
    auto tryBody = blockStatement();
    consume(TokenType::CATCH, "expected 'catch' after try block.");
    std::string catchVar = "_err";
    if (check(TokenType::IDENTIFIER)) {
        catchVar = peek().lexeme;
        advance();
    }
    consume(TokenType::LEFT_BRACE, "expected '{' after catch.");
    auto catchBody = blockStatement();
    return std::make_unique<TryCatchStmt>(std::move(tryBody), catchVar, std::move(catchBody), stmtLine);
}

std::unique_ptr<Stmt> Parser::importStatement() {
    int stmtLine = previous().line;
    const Token& path = consume(TokenType::STRING, "expected file path after 'import'.");
    return std::make_unique<ImportStmt>(path.lexeme, stmtLine);
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
    if (depth >= kMaxDepth) {
        errorAtCurrent("maximum expression depth exceeded.");
    }
    ++depth;
    struct Guard { Parser& p; ~Guard() { --p.depth; } } guard{*this};
    return assignment();
}

std::unique_ptr<Expr> Parser::assignment() {
    auto expr = logicalOr();

    // Compound assignment: +=, -=, *=, /=, %=
    auto tryCompound = [&](TokenType tok, const std::string& op) -> bool {
        if (!match(tok)) return false;
        int assignLine = previous().line;
        auto rhs = assignment();
        std::string varName;
        if (const auto* ident = dynamic_cast<const IdentifierExpr*>(expr.get()))
            varName = ident->name;
        else
            errorAt(previous(), "compound assignment requires a variable.");
        // Desugar: x op= rhs  →  x = x op rhs
        auto left  = std::make_unique<IdentifierExpr>(varName, assignLine);
        auto binary = std::make_unique<BinaryExpr>(std::move(left), op, std::move(rhs), assignLine);
        expr = std::make_unique<AssignmentExpr>(varName, std::move(binary), assignLine);
        return true;
    };

    if (tryCompound(TokenType::PLUS_EQUAL,    "+")) return expr;
    if (tryCompound(TokenType::MINUS_EQUAL,   "-")) return expr;
    if (tryCompound(TokenType::STAR_EQUAL,    "*")) return expr;
    if (tryCompound(TokenType::SLASH_EQUAL,   "/")) return expr;
    if (tryCompound(TokenType::PERCENT_EQUAL, "%")) return expr;

    if (match(TokenType::EQUAL)) {
        const Token& equalsToken = previous();
        int assignLine = equalsToken.line;
        auto value = assignment();

        if (const auto* identifier = dynamic_cast<const IdentifierExpr*>(expr.get())) {
            return std::make_unique<AssignmentExpr>(identifier->name, std::move(value), assignLine);
        }

        // arr[i] = value
        if (const auto* indexExpr = dynamic_cast<const IndexExpr*>(expr.get())) {
            auto owned = std::unique_ptr<IndexExpr>(static_cast<IndexExpr*>(expr.release()));
            return std::make_unique<IndexAssignExpr>(
                std::move(owned->object),
                std::move(owned->index),
                std::move(value),
                assignLine
            );
        }

        // obj.prop = value
        if (const auto* getExpr = dynamic_cast<const GetExpr*>(expr.get())) {
            auto owned = std::unique_ptr<GetExpr>(static_cast<GetExpr*>(expr.release()));
            return std::make_unique<SetExpr>(
                std::move(owned->object),
                owned->name,
                std::move(value),
                assignLine
            );
        }

        errorAt(equalsToken, "invalid assignment target.");
    }

    // Ternary: expr ? thenExpr : elseExpr
    if (match(TokenType::QUESTION)) {
        int ternLine = previous().line;
        auto thenExpr = expression();
        consume(TokenType::COLON, "expected ':' in ternary expression.");
        auto elseExpr = expression();
        return std::make_unique<TernaryExpr>(std::move(expr), std::move(thenExpr), std::move(elseExpr), ternLine);
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

    auto expr = call();

    // Postfix ++ / -- : desugar to x = x + 1
    if (match(TokenType::PLUS_PLUS) || match(TokenType::MINUS_MINUS)) {
        std::string op = (previous().type == TokenType::PLUS_PLUS) ? "+" : "-";
        int opLine = previous().line;
        if (const auto* ident = dynamic_cast<const IdentifierExpr*>(expr.get())) {
            std::string varName = ident->name;
            auto left   = std::make_unique<IdentifierExpr>(varName, opLine);
            auto one    = std::make_unique<NumberExpr>(1.0, opLine);
            auto binary = std::make_unique<BinaryExpr>(std::move(left), op, std::move(one), opLine);
            return std::make_unique<AssignmentExpr>(varName, std::move(binary), opLine);
        }
        errorAt(previous(), "'++' / '--' requires a variable.");
    }

    return expr;
}

std::unique_ptr<Expr> Parser::call() {
    auto expr = primary();

    while (true) {
        if (match(TokenType::LEFT_PAREN)) {
            expr = finishCall(std::move(expr));
        } else if (match(TokenType::LEFT_BRACKET)) {
            int bracketLine = previous().line;
            auto index = expression();
            consume(TokenType::RIGHT_BRACKET, "expected ']' after index.");
            expr = std::make_unique<IndexExpr>(std::move(expr), std::move(index), bracketLine);
        } else if (match(TokenType::DOT)) {
            int dotLine = previous().line;
            const Token& prop = consume(TokenType::IDENTIFIER, "expected property name after '.'.");
            expr = std::make_unique<GetExpr>(std::move(expr), prop.lexeme, dotLine);
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

    // String interpolation
    if (match(TokenType::INTERP_START)) {
        int interpLine = previous().line;
        std::vector<std::unique_ptr<Expr>> parts;
        // First literal part (may be empty)
        parts.push_back(std::make_unique<StringExpr>(previous().lexeme, interpLine));
        // Expression + mid parts
        parts.push_back(expression());
        while (match(TokenType::INTERP_MID)) {
            parts.push_back(std::make_unique<StringExpr>(previous().lexeme, interpLine));
            parts.push_back(expression());
        }
        consume(TokenType::INTERP_END, "expected end of interpolated string.");
        parts.push_back(std::make_unique<StringExpr>(previous().lexeme, interpLine));
        return std::make_unique<InterpolatedStringExpr>(std::move(parts), interpLine);
    }

    // Match expression
    if (match(TokenType::MATCH)) {
        int matchLine = previous().line;
        auto subject = expression();
        consume(TokenType::LEFT_BRACE, "expected '{' after match subject.");
        std::vector<MatchCase> cases;
        while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
            std::unique_ptr<Expr> pattern = nullptr;
            if (check(TokenType::IDENTIFIER) && peek().lexeme == "_") {
                advance(); // consume _
            } else {
                pattern = expression();
            }
            consume(TokenType::ARROW, "expected '->' in match case.");
            auto body = expression();
            cases.push_back(MatchCase{std::move(pattern), std::move(body)});
            match(TokenType::COMMA); // optional comma
        }
        consume(TokenType::RIGHT_BRACE, "expected '}' after match cases.");
        return std::make_unique<MatchExpr>(std::move(subject), std::move(cases), matchLine);
    }

    if (match(TokenType::IMPORT)) {
        int importLine = previous().line;
        auto path = expression();
        return std::make_unique<ImportExpr>(std::move(path), importLine);
    }

    if (match(TokenType::LEFT_PAREN)) {
        auto expr = expression();
        consume(TokenType::RIGHT_PAREN, "expected ')' after expression.");
        return expr;
    }

    // fn(...) { body } as an expression value (lambda)
    if (match(TokenType::FN)) {
        int fnLine = previous().line;
        // Anonymous fn may have a name or not
        std::string fnName = "__lambda__";
        if (check(TokenType::IDENTIFIER)) {
            fnName = peek().lexeme;
            advance();
        }
        consume(TokenType::LEFT_PAREN, "expected '(' after 'fn'.");
        std::vector<std::pair<std::string, std::shared_ptr<Expr>>> params;
        if (!check(TokenType::RIGHT_PAREN)) {
            do {
                std::string paramName = consume(TokenType::IDENTIFIER, "expected parameter name.").lexeme;
                std::shared_ptr<Expr> defaultVal = nullptr;
                if (match(TokenType::EQUAL)) {
                    defaultVal = expression();
                }
                params.push_back({paramName, std::move(defaultVal)});
            } while (match(TokenType::COMMA));
        }
        consume(TokenType::RIGHT_PAREN, "expected ')' after parameters.");
        consume(TokenType::LEFT_BRACE, "expected '{' before fn body.");
        auto body = blockStatement();
        // Wrap in a FunctionStmt and return as LambdaExpr
        auto fnStmt = std::make_unique<FunctionStmt>(fnName, std::move(params), std::move(body), fnLine);
        return std::make_unique<LambdaExpr>(std::move(fnStmt), fnLine);
    }

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
