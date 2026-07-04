#pragma once

#include "parser/AST.h"
#include "vm/Chunk.h"
#include "vm/OpCode.h"
#include "vm/VM.h"

struct Local {
    std::string name;
    int depth;
};

enum class FunctionType {
    TYPE_SCRIPT,
    TYPE_FUNCTION
};

struct LoopInfo {
    int startOffset;
    int scopeDepth;
    std::vector<int> breakJumps;
};

class Compiler {
public:
    Compiler(Compiler* enclosing, FunctionType type);
    
    // Compiles an AST program into a BytecodeFunction
    std::shared_ptr<BytecodeFunction> compile(const Program& program);
    std::shared_ptr<BytecodeFunction> endCompiler();

private:
    Compiler* enclosing;
    std::shared_ptr<BytecodeFunction> function;
    FunctionType type;

    std::vector<Local> locals;
    int scopeDepth;
    std::vector<LoopInfo> loops;

    Chunk* currentChunk();

    void emitByte(uint8_t byte);
    void emitBytes(uint8_t byte1, uint8_t byte2);
    void emitConstant(Value value);
    
    int emitJump(uint8_t instruction);
    void patchJump(int offset);
    void emitLoop(int loopStart);

    void beginScope();
    void endScope();
    void addLocal(const std::string& name);
    int resolveLocal(const std::string& name);

    void compileStatement(const Stmt& stmt);
    void compileExpression(const Expr& expr);
};
