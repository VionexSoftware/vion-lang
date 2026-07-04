#pragma once

#include "parser/AST.h"
#include "vm/Chunk.h"
#include "vm/OpCode.h"

class Compiler {
public:
    Compiler();
    
    // Compiles an AST program into a Bytecode Chunk
    bool compile(const Program& program, Chunk* chunk);

private:
    Chunk* compilingChunk;

    void emitByte(uint8_t byte);
    void emitBytes(uint8_t byte1, uint8_t byte2);
    void emitConstant(Value value);
    
    int emitJump(uint8_t instruction);
    void patchJump(int offset);
    void emitLoop(int loopStart);

    void compileStatement(const Stmt& stmt);
    void compileExpression(const Expr& expr);
};
