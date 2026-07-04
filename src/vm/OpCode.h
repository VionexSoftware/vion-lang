#pragma once

#include <cstdint>

enum class OpCode : uint8_t {
    // Constants
    OP_CONSTANT,     // Load constant from pool
    OP_NIL,
    OP_TRUE,
    OP_FALSE,

    // Stack Operations
    OP_POP,

    // Variables
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_GET_GLOBAL,
    OP_DEFINE_GLOBAL,
    OP_SET_GLOBAL,

    // Math/Logic
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MODULO,
    OP_NOT,
    OP_NEGATE,

    OP_GET_PROPERTY,
    OP_SET_PROPERTY,
    OP_INVOKE,
    OP_IMPORT,

    // Control Flow
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,         // Jump backwards

    OP_TRY_BEGIN,
    OP_TRY_END,

    // Functions/Closures
    OP_CALL,
    OP_CLOSURE,
    OP_RETURN,

    // Collections
    OP_BUILD_ARRAY,
    OP_BUILD_MAP,
    OP_INDEX_GET,
    OP_INDEX_SET,

    // Miscellaneous
    OP_PRINT
};
