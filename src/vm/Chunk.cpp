#include "vm/Chunk.h"

void Chunk::write(uint8_t byte, int line) {
    code.push_back(byte);
    lines.push_back(line);
}

void Chunk::write(OpCode opcode, int line) {
    write(static_cast<uint8_t>(opcode), line);
}

int Chunk::addConstant(Value value) {
    constants.push_back(value);
    return static_cast<int>(constants.size() - 1);
}
