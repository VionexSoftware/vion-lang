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

// condition ? thenExpr : elseExpr
struct TernaryExpr : Expr {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Expr> thenExpr;
    std::unique_ptr<Expr> elseExpr;
    TernaryExpr(std::unique_ptr<Expr> condition, std::unique_ptr<Expr> thenExpr,
                std::unique_ptr<Expr> elseExpr, int line = 0)
        : condition(std::move(condition)), thenExpr(std::move(thenExpr)),
          elseExpr(std::move(elseExpr)) { this->line = line; }
    std::string toString() const override { return "(ternary)"; }
};

// "hello {expr} world"
struct InterpolatedStringExpr : Expr {
    std::vector<std::unique_ptr<Expr>> parts;  // StringExpr for literals, any Expr for interpolated
    explicit InterpolatedStringExpr(std::vector<std::unique_ptr<Expr>> parts, int line = 0)
        : parts(std::move(parts)) { this->line = line; }
    std::string toString() const override { return "<interpolated-string>"; }
};

// match subject { pattern -> expr, ... _ -> expr }
struct MatchCase {
    std::unique_ptr<Expr> pattern;  // nullptr = wildcard _
    std::unique_ptr<Expr> body;
};

struct MatchExpr : Expr {
    std::unique_ptr<Expr> subject;
    std::vector<MatchCase> cases;
    explicit MatchExpr(std::unique_ptr<Expr> subject, std::vector<MatchCase> cases, int line = 0)
        : subject(std::move(subject)), cases(std::move(cases)) { this->line = line; }
    std::string toString() const override { return "<match>"; }
};

struct ImportExpr : Expr {
    std::unique_ptr<Expr> modulePath;
    ImportExpr(std::unique_ptr<Expr> modulePath, int line = 0)
        : modulePath(std::move(modulePath)) { this->line = line; }
    std::string toString() const override { return "import " + modulePath->toString(); }
};

struct GetExpr : Expr {
    std::unique_ptr<Expr> object;
    std::string name;
    GetExpr(std::unique_ptr<Expr> object, std::string name, int line = 0)
        : object(std::move(object)), name(std::move(name)) { this->line = line; }
    std::string toString() const override { return "(." + name + ")"; }
};

struct SetExpr : Expr {
    std::unique_ptr<Expr> object;
    std::string name;
    std::unique_ptr<Expr> value;
    SetExpr(std::unique_ptr<Expr> object, std::string name, std::unique_ptr<Expr> value, int line = 0)
        : object(std::move(object)), name(std::move(name)), value(std::move(value)) { this->line = line; }
    std::string toString() const override { return "(." + name + " = ...)"; }
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
    std::vector<std::pair<std::string, std::shared_ptr<Expr>>> parameters;
    std::shared_ptr<BlockStmt> body;   // shared ownership — safe for closure capture
    FunctionStmt(std::string name, std::vector<std::pair<std::string, std::shared_ptr<Expr>>> parameters,
                 std::unique_ptr<BlockStmt> body, int line = 0)
        : name(std::move(name)), parameters(std::move(parameters)),
          body(std::move(body)) { this->line = line; }
    std::string toString(int indent = 0) const override {
        std::ostringstream out;
        out << indentText(indent) << "Fn " << name << "(";
        for (std::size_t i = 0; i < parameters.size(); ++i) {
            if (i > 0) out << ", ";
            out << parameters[i].first;
            if (parameters[i].second) out << " = " << parameters[i].second->toString();
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


// const name = value (immutable binding)
struct ConstStmt : Stmt {
    std::string name;
    std::unique_ptr<Expr> value;
    ConstStmt(std::string name, std::unique_ptr<Expr> value, int line = 0)
        : name(std::move(name)), value(std::move(value)) { this->line = line; }
    std::string toString(int indent = 0) const override {
        return indentText(indent) + "Const " + name + " = " + value->toString();
    }
};

// try { ... } catch errVar { ... }
struct TryCatchStmt : Stmt {
    std::unique_ptr<BlockStmt> tryBody;
    std::string catchVar;
    std::unique_ptr<BlockStmt> catchBody;
    TryCatchStmt(std::unique_ptr<BlockStmt> tryBody, std::string catchVar,
                 std::unique_ptr<BlockStmt> catchBody, int line = 0)
        : tryBody(std::move(tryBody)), catchVar(std::move(catchVar)),
          catchBody(std::move(catchBody)) { this->line = line; }
    std::string toString(int indent = 0) const override {
        return indentText(indent) + "TryCatch";
    }
};

// import "path.vion"
struct ImportStmt : Stmt {
    std::string path;
    explicit ImportStmt(std::string path, int line = 0)
        : path(std::move(path)) { this->line = line; }
    std::string toString(int indent = 0) const override {
        return indentText(indent) + "Import \"" + path + "\"";
    }
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
