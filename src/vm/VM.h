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

class VM {
public:
    VM();
    ~VM();

    InterpretResult interpret(Chunk* chunk);
    void push(Value value);
    Value pop();

private:
    Chunk* chunk;
    uint8_t* ip;
    std::vector<Value> stack;
    std::unordered_map<std::string, Value> globals;

    uint8_t readByte();
    uint16_t readShort();
    Value readConstant();
    InterpretResult run();
};
