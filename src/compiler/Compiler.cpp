#include "compiler/Compiler.h"

Compiler::Compiler(Compiler* enclosing, FunctionType type) 
    : enclosing(enclosing), type(type), scopeDepth(0) {
    function = std::make_shared<BytecodeFunction>();
    if (type != FunctionType::TYPE_SCRIPT) {
        function->name = "<fn>"; // Real name will be set later
    }
    
    // Reserve slot 0 for the function itself
    Local local;
    local.depth = 0;
    local.name = (type == FunctionType::TYPE_FUNCTION) ? function->name : "";
    locals.push_back(local);
}

Chunk* Compiler::currentChunk() {
    return function->chunk.get();
}

std::shared_ptr<BytecodeFunction> Compiler::compile(const Program& program) {
    for (const auto& stmt : program.statements) {
        compileStatement(*stmt);
    }
    return endCompiler();
}

std::shared_ptr<BytecodeFunction> Compiler::endCompiler() {
    emitByte(static_cast<uint8_t>(OpCode::OP_NIL));
    emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));
    return function;
}

void Compiler::emitByte(uint8_t byte) {
    currentChunk()->write(byte, 0); // TODO: Line numbers
}

void Compiler::emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

void Compiler::emitConstant(Value value) {
    int index = currentChunk()->addConstant(value);
    emitBytes(static_cast<uint8_t>(OpCode::OP_CONSTANT), static_cast<uint8_t>(index));
}

int Compiler::emitJump(uint8_t instruction) {
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return static_cast<int>(currentChunk()->code.size()) - 2;
}

void Compiler::patchJump(int offset) {
    int jump = static_cast<int>(currentChunk()->code.size()) - offset - 2;
    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

void Compiler::emitLoop(int loopStart) {
    emitByte(static_cast<uint8_t>(OpCode::OP_LOOP));
    int offset = static_cast<int>(currentChunk()->code.size()) - loopStart + 2;
    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

void Compiler::beginScope() {
    scopeDepth++;
}

void Compiler::endScope() {
    scopeDepth--;
    // Pop local variables from stack
    while (!locals.empty() && locals.back().depth > scopeDepth) {
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        locals.pop_back();
    }
}

void Compiler::addLocal(const std::string& name) {
    locals.push_back({name, scopeDepth});
}

int Compiler::resolveLocal(const std::string& name) {
    for (int i = locals.size() - 1; i >= 0; i--) {
        if (locals[i].name == name) {
            return i;
        }
    }
    return -1;
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
        
        if (scopeDepth > 0) {
            addLocal(letStmt->name);
        } else {
            int index = currentChunk()->addConstant(Value::string(letStmt->name));
            emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL), static_cast<uint8_t>(index));
        }
    } else if (const auto* blockStmt = dynamic_cast<const BlockStmt*>(&stmt)) {
        beginScope();
        for (const auto& s : blockStmt->statements) {
            compileStatement(*s);
        }
        endScope();
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
        int loopStart = static_cast<int>(currentChunk()->code.size());
        compileExpression(*whileStmt->condition);
        
        int jumpIfFalse = emitJump(static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE));
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        
        compileStatement(*whileStmt->body);
        emitLoop(loopStart);
        
        patchJump(jumpIfFalse);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
    } else if (const auto* funcStmt = dynamic_cast<const FunctionStmt*>(&stmt)) {
        int globalIndex = currentChunk()->addConstant(Value::string(funcStmt->name));
        
        Compiler childCompiler(this, FunctionType::TYPE_FUNCTION);
        childCompiler.function->name = funcStmt->name;
        childCompiler.function->arity = funcStmt->parameters.size();
        
        childCompiler.beginScope();
        for (const auto& param : funcStmt->parameters) {
            childCompiler.addLocal(param.first);
        }
        
        for (const auto& s : funcStmt->body->statements) {
            childCompiler.compileStatement(*s);
        }
        
        std::shared_ptr<BytecodeFunction> compiledFunc = childCompiler.endCompiler();
        int funcIndex = currentChunk()->addConstant(Value::bytecodeFunction(compiledFunc));
        emitBytes(static_cast<uint8_t>(OpCode::OP_CLOSURE), static_cast<uint8_t>(funcIndex));
        
        if (scopeDepth > 0) {
            addLocal(funcStmt->name);
        } else {
            emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL), static_cast<uint8_t>(globalIndex));
        }
    } else if (const auto* retStmt = dynamic_cast<const ReturnStmt*>(&stmt)) {
        if (type == FunctionType::TYPE_SCRIPT) {
            // Can't return from top-level code (for now)
        }
        if (retStmt->value) {
            compileExpression(*retStmt->value);
        } else {
            emitByte(static_cast<uint8_t>(OpCode::OP_NIL));
        }
        emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));
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
        int localArg = resolveLocal(idExpr->name);
        if (localArg != -1) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), static_cast<uint8_t>(localArg));
        } else {
            int index = currentChunk()->addConstant(Value::string(idExpr->name));
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL), static_cast<uint8_t>(index));
        }
    } else if (const auto* assignExpr = dynamic_cast<const AssignmentExpr*>(&expr)) {
        compileExpression(*assignExpr->value);
        int localArg = resolveLocal(assignExpr->name);
        if (localArg != -1) {
            emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL), static_cast<uint8_t>(localArg));
        } else {
            int index = currentChunk()->addConstant(Value::string(assignExpr->name));
            emitBytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL), static_cast<uint8_t>(index));
        }
    } else if (const auto* callExpr = dynamic_cast<const CallExpr*>(&expr)) {
        compileExpression(*callExpr->callee);
        for (const auto& arg : callExpr->arguments) {
            compileExpression(*arg);
        }
        emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), static_cast<uint8_t>(callExpr->arguments.size()));
    }
}
