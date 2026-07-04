#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "parser/AST.h"
#include "runtime/Environment.h"
#include "runtime/Value.h"

class Interpreter;

class VionCallable {
public:
    virtual ~VionCallable() = default;
    virtual int arity() const = 0;
    virtual Value call(Interpreter& interpreter, const std::vector<Value>& arguments) = 0;
    virtual std::string toString() const = 0;
};

class Interpreter {
public:
    Interpreter();

    void interpret(const Program& program);
    void executeBlock(const BlockStmt& block, std::shared_ptr<Environment> blockEnvironment);

private:
    std::shared_ptr<Environment> globals;
    std::shared_ptr<Environment> environment;
    int callDepth = 0;
    static constexpr int kMaxCallDepth = 500;

    void execute(const Stmt& statement);
    Value evaluate(const Expr& expression);

    Value evaluateUnary(const UnaryExpr& expression);
    Value evaluateLogical(const LogicalExpr& expression);
    Value evaluateBinary(const BinaryExpr& expression);
    Value evaluateCall(const CallExpr& expression);
    Value evaluateIndex(const IndexExpr& expression);

    bool valuesEqual(const Value& left, const Value& right) const;

    std::string locationOf(int line) const;

    void registerBuiltins();
};
