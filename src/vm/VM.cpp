#include "vm/VM.h"
#include <iostream>
#include <cmath>

VM::VM() {
    chunk = nullptr;
    ip = nullptr;
}

VM::~VM() {}

void VM::push(Value value) {
    stack.push_back(std::move(value));
}

Value VM::pop() {
    Value value = std::move(stack.back());
    stack.pop_back();
    return value;
}

uint8_t VM::readByte() {
    return *ip++;
}

uint16_t VM::readShort() {
    ip += 2;
    return static_cast<uint16_t>((ip[-2] << 8) | ip[-1]);
}

Value VM::readConstant() {
    return chunk->constants[readByte()];
}

InterpretResult VM::interpret(Chunk* chunk) {
    this->chunk = chunk;
    this->ip = chunk->code.data();
    return run();
}

InterpretResult VM::run() {
    for (;;) {
        uint8_t instruction;
        switch (instruction = readByte()) {
            case static_cast<uint8_t>(OpCode::OP_CONSTANT): {
                Value constant = readConstant();
                push(constant);
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_NIL): push(Value::nil()); break;
            case static_cast<uint8_t>(OpCode::OP_TRUE): push(Value::boolean(true)); break;
            case static_cast<uint8_t>(OpCode::OP_FALSE): push(Value::boolean(false)); break;
            case static_cast<uint8_t>(OpCode::OP_EQUAL): {
                Value b = pop();
                Value a = pop();
                push(Value::boolean(a == b));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_GREATER): {
                Value b = pop();
                Value a = pop();
                if (a.type != ValueType::NUMBER || b.type != ValueType::NUMBER) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                push(Value::boolean(std::get<double>(a.data) > std::get<double>(b.data)));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_LESS): {
                Value b = pop();
                Value a = pop();
                if (a.type != ValueType::NUMBER || b.type != ValueType::NUMBER) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                push(Value::boolean(std::get<double>(a.data) < std::get<double>(b.data)));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_ADD): {
                Value b = pop();
                Value a = pop();
                if (a.type == ValueType::NUMBER && b.type == ValueType::NUMBER) {
                    push(Value::number(std::get<double>(a.data) + std::get<double>(b.data)));
                } else if (a.type == ValueType::STRING || b.type == ValueType::STRING) {
                    push(Value::string(a.toString() + b.toString()));
                } else {
                    std::cerr << "Runtime Error: operands must be numbers or strings.\n";
                    return InterpretResult::INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_SUBTRACT): {
                Value b = pop();
                Value a = pop();
                if (a.type != ValueType::NUMBER || b.type != ValueType::NUMBER) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                push(Value::number(std::get<double>(a.data) - std::get<double>(b.data)));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_MULTIPLY): {
                Value b = pop();
                Value a = pop();
                if (a.type != ValueType::NUMBER || b.type != ValueType::NUMBER) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                push(Value::number(std::get<double>(a.data) * std::get<double>(b.data)));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_DIVIDE): {
                Value b = pop();
                Value a = pop();
                if (a.type != ValueType::NUMBER || b.type != ValueType::NUMBER) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                push(Value::number(std::get<double>(a.data) / std::get<double>(b.data)));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_NEGATE): {
                Value a = pop();
                if (a.type != ValueType::NUMBER) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                push(Value::number(-std::get<double>(a.data)));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_PRINT): {
                std::cout << pop().toString() << "\n";
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_POP): {
                pop();
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL): {
                Value name = readConstant();
                globals[name.toString()] = pop();
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_GET_GLOBAL): {
                Value name = readConstant();
                std::string key = name.toString();
                auto it = globals.find(key);
                if (it == globals.end()) {
                    std::cerr << "Runtime Error: Undefined variable '" << key << "'.\n";
                    return InterpretResult::INTERPRET_RUNTIME_ERROR;
                }
                push(it->second);
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_SET_GLOBAL): {
                Value name = readConstant();
                std::string key = name.toString();
                if (globals.find(key) == globals.end()) {
                    std::cerr << "Runtime Error: Undefined variable '" << key << "'.\n";
                    return InterpretResult::INTERPRET_RUNTIME_ERROR;
                }
                globals[key] = stack.back(); // peek, don't pop for assignment
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE): {
                uint16_t offset = readShort();
                Value condition = stack.back(); // peek
                bool isFalse = (condition.type == ValueType::NIL) || 
                               (condition.type == ValueType::BOOLEAN && !std::get<bool>(condition.data));
                if (isFalse) ip += offset;
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_JUMP): {
                uint16_t offset = readShort();
                ip += offset;
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_LOOP): {
                uint16_t offset = readShort();
                ip -= offset;
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_RETURN): {
                if (!stack.empty()) {
                    std::cout << pop().toString() << "\n";
                }
                return InterpretResult::INTERPRET_OK;
            }
            default:
                std::cerr << "Runtime Error: Unknown OpCode.\n";
                return InterpretResult::INTERPRET_RUNTIME_ERROR;
        }
    }
}
