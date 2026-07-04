#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "parser/AST.h"
#include "runtime/Environment.h"
#include "runtime/Value.h"

class Interpreter;

class VionCallable : public GCObject {
public:
    virtual ~VionCallable() = default;
    virtual int arity() const = 0;
    virtual Value call(Interpreter& interpreter, const std::vector<Value>& arguments) = 0;
    virtual std::string toString() const = 0;
    
    void trace(std::vector<std::shared_ptr<GCObject>>& children) const override {}
};

class Interpreter {
public:
    Interpreter();

    void interpret(const Program& program);
    void executeBlock(const BlockStmt& block, std::shared_ptr<Environment> blockEnvironment);

    // Public for import support
    void setCurrentDir(const std::string& dir) { currentDir_ = dir; }

    void execute(const Stmt& statement);
    Value evaluate(const Expr& expression);

private:
    std::shared_ptr<Environment> globals;
    std::shared_ptr<Environment> environment;
    int callDepth = 0;
    static constexpr int kMaxCallDepth = 500;
    int evalDepth = 0;
    static constexpr int kMaxEvalDepth = 500;
    std::string currentDir_;
    std::unordered_set<std::string> importedFiles_;

    Value evaluateUnary(const UnaryExpr& expression);
    Value evaluateLogical(const LogicalExpr& expression);
    Value evaluateBinary(const BinaryExpr& expression);
    Value evaluateCall(const CallExpr& expression);
    Value evaluateIndex(const IndexExpr& expression);
    void executeImport(const ImportStmt& stmt);

    bool valuesEqual(const Value& left, const Value& right) const;

    std::string locationOf(int line) const;

    void registerBuiltins();
};
