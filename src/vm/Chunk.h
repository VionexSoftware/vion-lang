#pragma once

#include <vector>
#include <cstdint>
#include "vm/OpCode.h"
#include "runtime/Value.h"

class Chunk {
public:
    Chunk() = default;

    void write(uint8_t byte, int line);
    void write(OpCode opcode, int line);
    
    // Add constant and return its index
    int addConstant(Value value);

    std::vector<uint8_t> code;
    std::vector<Value> constants;
    std::vector<int> lines; // Line numbers corresponding to each byte in 'code'
};
