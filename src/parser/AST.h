#pragma once

#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// ── Base types ────────────────────────────────────────────────────────────────

struct Expr {
    int line = 0;
    virtual ~Expr() = default;
    virtual std::string toString() const = 0;
};

struct Stmt {
    int line = 0;
    virtual ~Stmt() = default;
    virtual std::string toString(int indent = 0) const = 0;
};

inline std::string indentText(int indent) {
    return std::string(indent, ' ');
}

// ── Expressions ───────────────────────────────────────────────────────────────

struct NumberExpr : Expr {
    double value;
    explicit NumberExpr(double value, int line = 0) : value(value) { this->line = line; }
    std::string toString() const override {
        std::ostringstream out;
        out << "Number(" << value << ")";
        return out.str();
    }
};

struct StringExpr : Expr {
    std::string value;
    explicit StringExpr(std::string value, int line = 0) : value(std::move(value)) { this->line = line; }
    std::string toString() const override { return "String(\"" + value + "\")"; }
};

struct BooleanExpr : Expr {
    bool value;
    explicit BooleanExpr(bool value, int line = 0) : value(value) { this->line = line; }
    std::string toString() const override {
        return std::string("Boolean(") + (value ? "true" : "false") + ")";
    }
};

struct NilExpr : Expr {
    NilExpr(int line = 0) { this->line = line; }
    std::string toString() const override { return "Nil"; }
};

struct IdentifierExpr : Expr {
    std::string name;
    explicit IdentifierExpr(std::string name, int line = 0) : name(std::move(name)) { this->line = line; }
    std::string toString() const override { return "Identifier(" + name + ")"; }
};

struct AssignmentExpr : Expr {
    std::string name;
    std::unique_ptr<Expr> value;
    AssignmentExpr(std::string name, std::unique_ptr<Expr> value, int line = 0)
        : name(std::move(name)), value(std::move(value)) { this->line = line; }
    std::string toString() const override {
        return "(assign " + name + " " + value->toString() + ")";
    }
};

// arr[index] = value
struct IndexAssignExpr : Expr {
    std::unique_ptr<Expr> object;
    std::unique_ptr<Expr> index;
    std::unique_ptr<Expr> value;
    IndexAssignExpr(std::unique_ptr<Expr> object, std::unique_ptr<Expr> index,
                    std::unique_ptr<Expr> value, int line = 0)
        : object(std::move(object)), index(std::move(index)), value(std::move(value)) { this->line = line; }
    std::string toString() const override {
        return "(index-assign " + object->toString() + "[" + index->toString() + "] " + value->toString() + ")";
    }
};

struct UnaryExpr : Expr {
    std::string op;
    std::unique_ptr<Expr> right;
    UnaryExpr(std::string op, std::unique_ptr<Expr> right, int line = 0)
        : op(std::move(op)), right(std::move(right)) { this->line = line; }
    std::string toString() const override { return "(" + op + " " + right->toString() + ")"; }
};

struct LogicalExpr : Expr {
    std::unique_ptr<Expr> left;
    std::string op;
    std::unique_ptr<Expr> right;
    LogicalExpr(std::unique_ptr<Expr> left, std::string op, std::unique_ptr<Expr> right, int line = 0)
        : left(std::move(left)), op(std::move(op)), right(std::move(right)) { this->line = line; }
    std::string toString() const override {
        return "(" + left->toString() + " " + op + " " + right->toString() + ")";
    }
};

struct BinaryExpr : Expr {
    std::unique_ptr<Expr> left;
    std::string op;
    std::unique_ptr<Expr> right;
    BinaryExpr(std::unique_ptr<Expr> left, std::string op, std::unique_ptr<Expr> right, int line = 0)
        : left(std::move(left)), op(std::move(op)), right(std::move(right)) { this->line = line; }
    std::string toString() const override {
        return "(" + left->toString() + " " + op + " " + right->toString() + ")";
    }
};

struct CallExpr : Expr {
    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> arguments;
    CallExpr(std::unique_ptr<Expr> callee, std::vector<std::unique_ptr<Expr>> arguments, int line = 0)
        : callee(std::move(callee)), arguments(std::move(arguments)) { this->line = line; }
    std::string toString() const override {
        std::ostringstream out;
        out << "Call " << callee->toString() << "(";
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            if (i > 0) out << ", ";
            out << arguments[i]->toString();
        }
        out << ")";
        return out.str();
    }
};

// arr[index]
struct IndexExpr : Expr {
    std::unique_ptr<Expr> object;
    std::unique_ptr<Expr> index;
    IndexExpr(std::unique_ptr<Expr> object, std::unique_ptr<Expr> index, int line = 0)
        : object(std::move(object)), index(std::move(index)) { this->line = line; }
    std::string toString() const override {
        return "Index(" + object->toString() + "[" + index->toString() + "])";
    }
};

// [1, 2, 3]
struct ArrayExpr : Expr {
    std::vector<std::unique_ptr<Expr>> elements;
    explicit ArrayExpr(std::vector<std::unique_ptr<Expr>> elements, int line = 0)
        : elements(std::move(elements)) { this->line = line; }
    std::string toString() const override {
        std::ostringstream out;
        out << "Array[";
        for (std::size_t i = 0; i < elements.size(); ++i) {
            if (i > 0) out << ", ";
            out << elements[i]->toString();
        }
        out << "]";
        return out.str();
    }
};

// {"key": value, ...}
struct MapExpr : Expr {
    std::vector<std::pair<std::string, std::unique_ptr<Expr>>> pairs;
    explicit MapExpr(std::vector<std::pair<std::string, std::unique_ptr<Expr>>> pairs, int line = 0)
        : pairs(std::move(pairs)) { this->line = line; }
    std::string toString() const override {
        std::ostringstream out;
        out << "{";
        for (std::size_t i = 0; i < pairs.size(); ++i) {
            if (i > 0) out << ", ";
            out << '"' << pairs[i].first << "\": " << pairs[i].second->toString();
        }
        out << "}";
        return out.str();
    }
};

// Forward declare FunctionStmt for LambdaExpr
struct FunctionStmt;

// fn(params) { body } as an expression — captures environment at definition site
struct LambdaExpr : Expr {
    std::unique_ptr<FunctionStmt> decl;
    explicit LambdaExpr(std::unique_ptr<FunctionStmt> decl, int line = 0)
        : decl(std::move(decl)) { this->line = line; }
    std::string toString() const override { return "<lambda>"; }
};

// ── Statements ────────────────────────────────────────────────────────────────

struct LetStmt : Stmt {
    std::string name;
    std::unique_ptr<Expr> value;
    LetStmt(std::string name, std::unique_ptr<Expr> value, int line = 0)
        : name(std::move(name)), value(std::move(value)) { this->line = line; }
    std::string toString(int indent = 0) const override {
        return indentText(indent) + "Let " + name + " = " + value->toString();
    }
};

struct PrintStmt : Stmt {
    std::vector<std::unique_ptr<Expr>> values;
    explicit PrintStmt(std::vector<std::unique_ptr<Expr>> values, int line = 0)
        : values(std::move(values)) { this->line = line; }
    std::string toString(int indent = 0) const override {
        std::ostringstream out;
        out << indentText(indent) << "Print";
        for (const auto& v : values) out << " " << v->toString();
        return out.str();
    }
};

struct ExpressionStmt : Stmt {
    std::unique_ptr<Expr> value;
    explicit ExpressionStmt(std::unique_ptr<Expr> value, int line = 0)
        : value(std::move(value)) { this->line = line; }
    std::string toString(int indent = 0) const override {
        return indentText(indent) + "Expr " + value->toString();
    }
};

struct BlockStmt : Stmt {
    std::vector<std::unique_ptr<Stmt>> statements;
    explicit BlockStmt(std::vector<std::unique_ptr<Stmt>> statements, int line = 0)
        : statements(std::move(statements)) { this->line = line; }
    std::string toString(int indent = 0) const override {
        std::ostringstream out;
        out << indentText(indent) << "Block\n";
        for (const auto& stmt : statements) {
            out << stmt->toString(indent + 2) << "\n";
        }
        return out.str();
    }
};

struct WhileStmt : Stmt {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<BlockStmt> body;
    WhileStmt(std::unique_ptr<Expr> condition, std::unique_ptr<BlockStmt> body, int line = 0)
        : condition(std::move(condition)), body(std::move(body)) { this->line = line; }
    std::string toString(int indent = 0) const override {
        std::ostringstream out;
        out << indentText(indent) << "While " << condition->toString() << "\n";
        out << body->toString(indent + 2);
        return out.str();
    }
};

// for variable in iterable { body }
struct ForStmt : Stmt {
    std::string variable;
    std::unique_ptr<Expr> iterable;
    std::unique_ptr<BlockStmt> body;
    ForStmt(std::string variable, std::unique_ptr<Expr> iterable,
            std::unique_ptr<BlockStmt> body, int line = 0)
        : variable(std::move(variable)), iterable(std::move(iterable)),
          body(std::move(body)) { this->line = line; }
    std::string toString(int indent = 0) const override {
        std::ostringstream out;
        out << indentText(indent) << "For " << variable << " in "
            << iterable->toString() << "\n";
        out << body->toString(indent + 2);
        return out.str();
    }
};

struct IfStmt : Stmt {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<BlockStmt> thenBranch;
    std::unique_ptr<BlockStmt> elseBranch;
    IfStmt(std::unique_ptr<Expr> condition,
           std::unique_ptr<BlockStmt> thenBranch,
           std::unique_ptr<BlockStmt> elseBranch,
           int line = 0)
        : condition(std::move(condition)),
          thenBranch(std::move(thenBranch)),
          elseBranch(std::move(elseBranch)) { this->line = line; }
    std::string toString(int indent = 0) const override {
        std::ostringstream out;
        out << indentText(indent) << "If " << condition->toString() << "\n";
        out << thenBranch->toString(indent + 2);
        if (elseBranch) {
            out << indentText(indent) << "Else\n";
            out << elseBranch->toString(indent + 2);
        }
        return out.str();
    }
};

// FunctionStmt uses shared_ptr<BlockStmt> so VionFunction can safely
// co-own the function body without dangling references across REPL sessions.
struct FunctionStmt : Stmt {
    std::string name;
    std::vector<std::string> parameters;
    std::shared_ptr<BlockStmt> body;   // shared ownership — safe for closure capture
    FunctionStmt(std::string name, std::vector<std::string> parameters,
                 std::unique_ptr<BlockStmt> body, int line = 0)
        : name(std::move(name)), parameters(std::move(parameters)),
          body(std::move(body)) { this->line = line; }
    std::string toString(int indent = 0) const override {
        std::ostringstream out;
        out << indentText(indent) << "Fn " << name << "(";
        for (std::size_t i = 0; i < parameters.size(); ++i) {
            if (i > 0) out << ", ";
            out << parameters[i];
        }
        out << ")\n";
        out << body->toString(indent + 2);
        return out.str();
    }
};

struct ReturnStmt : Stmt {
    std::unique_ptr<Expr> value;
    explicit ReturnStmt(std::unique_ptr<Expr> value, int line = 0)
        : value(std::move(value)) { this->line = line; }
    std::string toString(int indent = 0) const override {
        if (!value) return indentText(indent) + "Return";
        return indentText(indent) + "Return " + value->toString();
    }
};

struct BreakStmt : Stmt {
    BreakStmt(int line = 0) { this->line = line; }
    std::string toString(int indent = 0) const override { return indentText(indent) + "Break"; }
};

struct ContinueStmt : Stmt {
    ContinueStmt(int line = 0) { this->line = line; }
    std::string toString(int indent = 0) const override { return indentText(indent) + "Continue"; }
};

// ── Program ───────────────────────────────────────────────────────────────────

struct Program {
    std::vector<std::unique_ptr<Stmt>> statements;
    std::string toString() const {
        std::ostringstream out;
        out << "Program\n";
        for (const auto& stmt : statements) {
            out << stmt->toString(2) << "\n";
        }
        return out.str();
    }
};
