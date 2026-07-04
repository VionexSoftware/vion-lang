#include "compiler/Compiler.h"
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <iostream>

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
        loops.push_back({loopStart, scopeDepth, {}});
        
        compileExpression(*whileStmt->condition);
        int jumpIfFalse = emitJump(static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE));
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        
        compileStatement(*whileStmt->body);
        emitLoop(loopStart);
        
        patchJump(jumpIfFalse);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));
        
        for (int breakJump : loops.back().breakJumps) {
            patchJump(breakJump);
        }
    } else if (const auto* forStmt = dynamic_cast<const ForStmt*>(&stmt)) {
        beginScope();

        compileExpression(*forStmt->iterable);
        addLocal("!iter");

        emitConstant(Value::number(0));
        addLocal("!index");

        int loopStart = static_cast<int>(currentChunk()->code.size());
        loops.push_back({loopStart, scopeDepth, {}});

        int indexArg = resolveLocal("!index");
        int iterArg = resolveLocal("!iter");

        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), static_cast<uint8_t>(indexArg));
        
        int lenGlobal = currentChunk()->addConstant(Value::string("len"));
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_GLOBAL), static_cast<uint8_t>(lenGlobal));
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), static_cast<uint8_t>(iterArg));
        emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), 1);
        
        emitByte(static_cast<uint8_t>(OpCode::OP_LESS));

        int jumpIfFalse = emitJump(static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE));
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));

        beginScope();
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), static_cast<uint8_t>(iterArg));
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), static_cast<uint8_t>(indexArg));
        emitByte(static_cast<uint8_t>(OpCode::OP_INDEX_GET));
        addLocal(forStmt->variable);

        compileStatement(*forStmt->body);

        endScope();

        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), static_cast<uint8_t>(indexArg));
        emitConstant(Value::number(1));
        emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
        emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL), static_cast<uint8_t>(indexArg));
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));

        emitLoop(loopStart);

        patchJump(jumpIfFalse);
        emitByte(static_cast<uint8_t>(OpCode::OP_POP));

        for (int breakJump : loops.back().breakJumps) {
            patchJump(breakJump);
        }
        loops.pop_back();

        endScope();
    } else if (dynamic_cast<const BreakStmt*>(&stmt)) {
        if (loops.empty()) {
            std::cerr << "Compile Error: 'break' outside of loop.\n";
            return;
        }
        // Discard local variables created inside the loop
        for (int i = static_cast<int>(locals.size()) - 1; i >= 0; i--) {
            if (locals[i].depth > loops.back().scopeDepth) {
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));
            } else {
                break;
            }
        }
        loops.back().breakJumps.push_back(emitJump(static_cast<uint8_t>(OpCode::OP_JUMP)));
    } else if (dynamic_cast<const ContinueStmt*>(&stmt)) {
        if (loops.empty()) {
            std::cerr << "Compile Error: 'continue' outside of loop.\n";
            return;
        }
        // Discard local variables created inside the loop
        for (int i = static_cast<int>(locals.size()) - 1; i >= 0; i--) {
            if (locals[i].depth > loops.back().scopeDepth) {
                emitByte(static_cast<uint8_t>(OpCode::OP_POP));
            } else {
                break;
            }
        }
        emitLoop(loops.back().startOffset);
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
            std::cerr << "Error: Runtime Error: return outside function.\n";
            exit(1);
        }
        if (retStmt->value) {
            compileExpression(*retStmt->value);
        } else {
            emitByte(static_cast<uint8_t>(OpCode::OP_NIL));
        }
        emitByte(static_cast<uint8_t>(OpCode::OP_RETURN));
    } else if (const auto* tryStmt = dynamic_cast<const TryCatchStmt*>(&stmt)) {
        int tryJump = emitJump(static_cast<uint8_t>(OpCode::OP_TRY_BEGIN));
        
        compileStatement(*tryStmt->tryBody);
        
        emitByte(static_cast<uint8_t>(OpCode::OP_TRY_END));
        int endJump = emitJump(static_cast<uint8_t>(OpCode::OP_JUMP));
        
        patchJump(tryJump); // This is the catch target!
        
        beginScope();
        addLocal(tryStmt->catchVar);
        // The VM will push the error message string onto the stack,
        // which matches the local variable 'catchVar' we just declared!
        compileStatement(*tryStmt->catchBody);
        endScope();
        
        patchJump(endJump);
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
        else if (op == "%") emitByte(static_cast<uint8_t>(OpCode::OP_MODULO));
        else if (op == "==") emitByte(static_cast<uint8_t>(OpCode::OP_EQUAL));
        else if (op == "!=") {
            emitByte(static_cast<uint8_t>(OpCode::OP_EQUAL));
            emitByte(static_cast<uint8_t>(OpCode::OP_NOT));
        }
        else if (op == ">") emitByte(static_cast<uint8_t>(OpCode::OP_GREATER));
        else if (op == "<") emitByte(static_cast<uint8_t>(OpCode::OP_LESS));
        else if (op == ">=") {
            emitByte(static_cast<uint8_t>(OpCode::OP_LESS));
            emitByte(static_cast<uint8_t>(OpCode::OP_NOT));
        }
        else if (op == "<=") {
            emitByte(static_cast<uint8_t>(OpCode::OP_GREATER));
            emitByte(static_cast<uint8_t>(OpCode::OP_NOT));
        }
    } else if (const auto* logicalExpr = dynamic_cast<const LogicalExpr*>(&expr)) {
        compileExpression(*logicalExpr->left);
        
        if (logicalExpr->op == "and") {
            int endJump = emitJump(static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE));
            emitByte(static_cast<uint8_t>(OpCode::OP_POP));
            compileExpression(*logicalExpr->right);
            patchJump(endJump);
        } else if (logicalExpr->op == "or") {
            int elseJump = emitJump(static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE));
            int endJump = emitJump(static_cast<uint8_t>(OpCode::OP_JUMP));
            
            patchJump(elseJump);
            emitByte(static_cast<uint8_t>(OpCode::OP_POP));
            compileExpression(*logicalExpr->right);
            
            patchJump(endJump);
        }
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
        if (const auto* getExpr = dynamic_cast<const GetExpr*>(callExpr->callee.get())) {
            compileExpression(*getExpr->object);
            for (const auto& arg : callExpr->arguments) {
                compileExpression(*arg);
            }
            int nameIndex = currentChunk()->addConstant(Value::string(getExpr->name));
            emitBytes(static_cast<uint8_t>(OpCode::OP_INVOKE), static_cast<uint8_t>(nameIndex));
            emitByte(static_cast<uint8_t>(callExpr->arguments.size()));
        } else {
            compileExpression(*callExpr->callee);
            for (const auto& arg : callExpr->arguments) {
                compileExpression(*arg);
            }
            emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), static_cast<uint8_t>(callExpr->arguments.size()));
        }
    } else if (const auto* arrayExpr = dynamic_cast<const ArrayExpr*>(&expr)) {
        for (const auto& elem : arrayExpr->elements) {
            compileExpression(*elem);
        }
        emitBytes(static_cast<uint8_t>(OpCode::OP_BUILD_ARRAY), static_cast<uint8_t>(arrayExpr->elements.size()));
    } else if (const auto* mapExpr = dynamic_cast<const MapExpr*>(&expr)) {
        for (const auto& pair : mapExpr->pairs) {
            int keyIndex = currentChunk()->addConstant(Value::string(pair.first));
            emitBytes(static_cast<uint8_t>(OpCode::OP_CONSTANT), static_cast<uint8_t>(keyIndex));
            compileExpression(*pair.second);
        }
        emitBytes(static_cast<uint8_t>(OpCode::OP_BUILD_MAP), static_cast<uint8_t>(mapExpr->pairs.size()));
    } else if (const auto* indexExpr = dynamic_cast<const IndexExpr*>(&expr)) {
        compileExpression(*indexExpr->object);
        compileExpression(*indexExpr->index);
        emitByte(static_cast<uint8_t>(OpCode::OP_INDEX_GET));
    } else if (const auto* indexAssignExpr = dynamic_cast<const IndexAssignExpr*>(&expr)) {
        compileExpression(*indexAssignExpr->object);
        compileExpression(*indexAssignExpr->index);
        compileExpression(*indexAssignExpr->value);
        emitByte(static_cast<uint8_t>(OpCode::OP_INDEX_SET));
    } else if (const auto* getExpr = dynamic_cast<const GetExpr*>(&expr)) {
        compileExpression(*getExpr->object);
        int nameIndex = currentChunk()->addConstant(Value::string(getExpr->name));
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_PROPERTY), static_cast<uint8_t>(nameIndex));
    } else if (const auto* setExpr = dynamic_cast<const SetExpr*>(&expr)) {
        compileExpression(*setExpr->object);
        compileExpression(*setExpr->value);
        int nameIndex = currentChunk()->addConstant(Value::string(setExpr->name));
        emitBytes(static_cast<uint8_t>(OpCode::OP_SET_PROPERTY), static_cast<uint8_t>(nameIndex));
    } else if (const auto* importExpr = dynamic_cast<const ImportExpr*>(&expr)) {
        compileExpression(*importExpr->modulePath);
        emitByte(static_cast<uint8_t>(OpCode::OP_IMPORT));
    } else if (const auto* interpExpr = dynamic_cast<const InterpolatedStringExpr*>(&expr)) {
        if (!interpExpr->parts.empty()) {
            compileExpression(*interpExpr->parts[0]);
            for (size_t i = 1; i < interpExpr->parts.size(); ++i) {
                compileExpression(*interpExpr->parts[i]);
                emitByte(static_cast<uint8_t>(OpCode::OP_ADD));
            }
        } else {
            emitConstant(Value::string(""));
        }
    }
}
