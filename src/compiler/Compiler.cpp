#include "compiler/Compiler.h"

Compiler::Compiler() {
    compilingChunk = nullptr;
}

bool Compiler::compile(const Program& program, Chunk* chunk) {
    compilingChunk = chunk;
    
    for (const auto& stmt : program.statements) {
        compileStatement(*stmt);
    }
    
    emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));
    return true;
}

void Compiler::emitByte(uint8_t byte) {
    compilingChunk->write(byte, 0); // TODO: Line numbers
}

void Compiler::emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

void Compiler::emitConstant(Value value) {
    int index = compilingChunk->addConstant(value);
    emitBytes(static_cast<uint8_t>(OpCode::OP_CONSTANT), static_cast<uint8_t>(index));
}

int Compiler::emitJump(uint8_t instruction) {
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return static_cast<int>(compilingChunk->code.size()) - 2;
}

void Compiler::patchJump(int offset) {
    int jump = static_cast<int>(compilingChunk->code.size()) - offset - 2;
    compilingChunk->code[offset] = (jump >> 8) & 0xff;
    compilingChunk->code[offset + 1] = jump & 0xff;
}

void Compiler::emitLoop(int loopStart) {
    emitByte(static_cast<uint8_t>(OpCode::OP_LOOP));
    int offset = static_cast<int>(compilingChunk->code.size()) - loopStart + 2;
    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

void Compiler::compileStatement(const Stmt& stmt) {
    if (const auto* exprStmt = dynamic_cast<const ExpressionStmt*>(&stmt)) {
        compileExpression(*exprStmt->value);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
    } else if (const auto* printStmt = dynamic_cast<const PrintStmt*>(&stmt)) {
        for (const auto& expr : printStmt->values) {
            compileExpression(*expr);
            emitByte(static_cast<uint8_t>(OpCode::OP_PRINT));
        }
    } else if (const auto* letStmt = dynamic_cast<const LetStmt*>(&stmt)) {
        if (letStmt->value) {
            compileExpression(*letStmt->value);
        } else {
            emitByte(static_cast<uint8_t>(OpCode::OP_NIL));
        }
        int index = compilingChunk->addConstant(Value::string(letStmt->name));
        emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL), static_cast<uint8_t>(index));
    } else if (const auto* blockStmt = dynamic_cast<const BlockStmt*>(&stmt)) {
        for (const auto& s : blockStmt->statements) {
            compileStatement(*s);
        }
    } else if (const auto* ifStmt = dynamic_cast<const IfStmt*>(&stmt)) {
        compileExpression(*ifStmt->condition);
        int jumpIfFalse = emitJump(static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE));
        emitByte(static_cast<uint8_t>(OpCode::OP_POP)); // Pop condition
        
        compileStatement(*ifStmt->thenBranch);
        int jump = emitJump(static_cast<uint8_t>(OpCode::OP_JUMP));
        
        patchJump(jumpIfFalse);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP)); // Pop condition
        
        if (ifStmt->elseBranch) {
            compileStatement(*ifStmt->elseBranch);
        }
        patchJump(jump);
    } else if (const auto* whileStmt = dynamic_cast<const WhileStmt*>(&stmt)) {
        int loopStart = static_cast<int>(compilingChunk->code.size());
        compileExpression(*whileStmt->condition);
        
        int jumpIfFalse = emitJump(static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE));
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        
        compileStatement(*whileStmt->body);
        emitLoop(loopStart);
        
        patchJump(jumpIfFalse);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
    }
}

void Compiler::compileExpression(const Expr& expr) {
    if (const auto* numExpr = dynamic_cast<const NumberExpr*>(&expr)) {
        emitConstant(Value::number(numExpr->value));
    } else if (const auto* strExpr = dynamic_cast<const StringExpr*>(&expr)) {
        emitConstant(Value::string(strExpr->value));
    } else if (const auto* boolExpr = dynamic_cast<const BooleanExpr*>(&expr)) {
        emitByte(static_cast<uint8_t>(boolExpr->value ? OpCode::OP_TRUE : OpCode::OP_FALSE));
    } else if (dynamic_cast<const NilExpr*>(&expr)) {
        emitByte(static_cast<uint8_t>(OpCode::OP_NIL));
    } else if (const auto* binaryExpr = dynamic_cast<const BinaryExpr*>(&expr)) {
        compileExpression(*binaryExpr->left);
        compileExpression(*binaryExpr->right);
        
        std::string op = binaryExpr->op;
        if (op == "+") emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
        else if (op == "-") emitByte(static_cast<uint8_t>(OpCode::OP_SUBTRACT));
        else if (op == "*") emitByte(static_cast<uint8_t>(OpCode::OP_MULTIPLY));
        else if (op == "/") emitByte(static_cast<uint8_t>(OpCode::OP_DIVIDE));
        else if (op == "==") emitByte(static_cast<uint8_t>(OpCode::OP_EQUAL));
        else if (op == ">") emitByte(static_cast<uint8_t>(OpCode::OP_GREATER));
        else if (op == "<") emitByte(static_cast<uint8_t>(OpCode::OP_LESS));
    } else if (const auto* unaryExpr = dynamic_cast<const UnaryExpr*>(&expr)) {
        compileExpression(*unaryExpr->right);
        
        std::string op = unaryExpr->op;
        if (op == "-") emitByte(static_cast<uint8_t>(OpCode::OP_NEGATE));
        else if (op == "!") emitByte(static_cast<uint8_t>(OpCode::OP_NOT));
    } else if (const auto* idExpr = dynamic_cast<const IdentifierExpr*>(&expr)) {
        int index = compilingChunk->addConstant(Value::string(idExpr->name));
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL), static_cast<uint8_t>(index));
    } else if (const auto* assignExpr = dynamic_cast<const AssignmentExpr*>(&expr)) {
        compileExpression(*assignExpr->value);
        int index = compilingChunk->addConstant(Value::string(assignExpr->name));
        emitBytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL), static_cast<uint8_t>(index));
    }
}
