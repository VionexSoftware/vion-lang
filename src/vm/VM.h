#pragma once

#include <vector>
#include "vm/Chunk.h"
#include "runtime/Value.h"

#include <unordered_map>
#include <string>

enum class InterpretResult {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
};

#include <memory>

struct BytecodeFunction : public GCObject {
    int arity = 0;
    std::shared_ptr<Chunk> chunk;
    std::string name;
    
    BytecodeFunction() {
        chunk = std::make_shared<Chunk>();
    }

    void trace(std::vector<std::shared_ptr<GCObject>>& children) const override {}
    void breakCycles() override { chunk.reset(); }
};

struct CallFrame {
    std::shared_ptr<BytecodeFunction> function;
    uint8_t* ip;
    int slots_base;
};

struct TryHandler {
    int frameIndex;
    int stackSize;
    uint8_t* catchIp;
};

class VM {
public:
    VM();
    ~VM();

    InterpretResult interpret(std::shared_ptr<BytecodeFunction> function, const std::string& scriptPath = "");
    void push(Value value);
    Value pop();

    void defineNative(const std::string& name, NativeFn function);

private:
    bool handleError(const std::string& message);
    
    std::vector<Value> stack;
    std::vector<CallFrame> frames;
    std::vector<TryHandler> tryHandlers;
    std::string currentScriptPath;
    
    std::unordered_map<std::string, Value> globals;

    uint8_t readByte();
    uint16_t readShort();
    Value readConstant();
    InterpretResult run();
};
