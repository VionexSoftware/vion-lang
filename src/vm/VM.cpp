#include "vm/VM.h"
#include <iostream>
#include <iomanip>
#include <cmath>

#include <chrono>
#include <thread>
#include <fstream>
#include <random>

VM::VM() {
    defineNative("print", [](int argCount, Value* args) {
        for (int i = 0; i < argCount; ++i) {
            std::cout << args[i].toString();
        }
        std::cout << "\n";
        return Value::nil();
    });

    defineNative("len", [](int argCount, Value* args) {
        if (argCount != 1) return Value::number(0);
        if (args[0].type == ValueType::STRING) {
            return Value::number(std::get<std::string>(args[0].data).length());
        }
        if (args[0].type == ValueType::ARRAY) {
            return Value::number(std::get<std::shared_ptr<VionArray>>(args[0].data)->elements.size());
        }
        if (args[0].type == ValueType::MAP) {
            return Value::number(std::get<std::shared_ptr<VionMap>>(args[0].data)->entries.size());
        }
        return Value::number(0);
    });

    defineNative("time", [](int argCount, Value* args) {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        return Value::number(std::chrono::duration_cast<std::chrono::milliseconds>(now).count() / 1000.0);
    });

    defineNative("sleep", [](int argCount, Value* args) {
        if (argCount == 1 && args[0].type == ValueType::NUMBER) {
            int ms = static_cast<int>(std::get<double>(args[0].data));
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }
        return Value::nil();
    });

    defineNative("random", [](int argCount, Value* args) {
        static std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        return Value::number(dist(rng));
    });

    defineNative("push", [](int argCount, Value* args) {
        if (argCount == 2 && args[0].type == ValueType::ARRAY) {
            std::get<std::shared_ptr<VionArray>>(args[0].data)->elements.push_back(args[1]);
        }
        return Value::nil();
    });

    defineNative("pop", [](int argCount, Value* args) {
        if (argCount == 1 && args[0].type == ValueType::ARRAY) {
            auto arr = std::get<std::shared_ptr<VionArray>>(args[0].data);
            if (!arr->elements.empty()) {
                Value last = arr->elements.back();
                arr->elements.pop_back();
                return last;
            }
        }
        return Value::nil();
    });

    defineNative("type", [](int argCount, Value* args) {
        if (argCount == 1) {
            return Value::string(args[0].typeName());
        }
        return Value::nil();
    });

    defineNative("array", [](int argCount, Value* args) {
        if (argCount == 2 && args[0].type == ValueType::NUMBER) {
            int size = static_cast<int>(std::get<double>(args[0].data));
            auto arr = std::make_shared<VionArray>();
            for (int i = 0; i < size; ++i) {
                arr->elements.push_back(args[1]);
            }
            return Value::array(arr);
        }
        return Value::nil();
    });
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
    return *frames.back().ip++;
}

uint16_t VM::readShort() {
    frames.back().ip += 2;
    return static_cast<uint16_t>((frames.back().ip[-2] << 8) | frames.back().ip[-1]);
}

Value VM::readConstant() {
    return frames.back().function->chunk->constants[readByte()];
}

InterpretResult VM::interpret(std::shared_ptr<BytecodeFunction> function) {
    frames.clear();
    stack.clear();
    
    push(Value::bytecodeFunction(function));
    CallFrame frame;
    frame.function = function;
    frame.ip = function->chunk->code.data();
    frame.slots_base = 0;
    frames.push_back(frame);
    return run();
}

void VM::defineNative(const std::string& name, NativeFn function) {
    auto native = std::make_shared<VMNativeFunction>();
    native->name = name;
    native->function = function;
    globals[name] = Value::nativeFunction(native);
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
                } else if (a.type == ValueType::STRING && b.type == ValueType::STRING) {
                    push(Value::string(std::get<std::string>(a.data) + std::get<std::string>(b.data)));
                } else if (a.type == ValueType::ARRAY && b.type == ValueType::ARRAY) {
                    auto arrA = std::get<std::shared_ptr<VionArray>>(a.data);
                    auto arrB = std::get<std::shared_ptr<VionArray>>(b.data);
                    auto newArr = std::make_shared<VionArray>();
                    newArr->elements = arrA->elements;
                    newArr->elements.insert(newArr->elements.end(), arrB->elements.begin(), arrB->elements.end());
                    push(Value::array(newArr));
                } else {
                    std::cerr << "Runtime Error: operands must be numbers or strings or arrays.\n";
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
                double denominator = std::get<double>(b.data);
                if (denominator == 0) {
                    std::cerr << "Runtime Error: division by zero.\n";
                    return InterpretResult::INTERPRET_RUNTIME_ERROR;
                }
                push(Value::number(std::get<double>(a.data) / denominator));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_MODULO): {
                Value b = pop();
                Value a = pop();
                if (a.type != ValueType::NUMBER || b.type != ValueType::NUMBER) return InterpretResult::INTERPRET_RUNTIME_ERROR;
                double denominator = std::get<double>(b.data);
                if (denominator == 0) {
                    std::cerr << "Runtime Error: modulo by zero.\n";
                    return InterpretResult::INTERPRET_RUNTIME_ERROR;
                }
                push(Value::number(std::fmod(std::get<double>(a.data), denominator)));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_NOT): {
                Value a = pop();
                bool isFalse = (a.type == ValueType::NIL) || 
                               (a.type == ValueType::BOOLEAN && !std::get<bool>(a.data));
                push(Value::boolean(isFalse));
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
            case static_cast<uint8_t>(OpCode::OP_GET_LOCAL): {
                uint8_t slot = readByte();
                push(stack[frames.back().slots_base + slot]);
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_SET_LOCAL): {
                uint8_t slot = readByte();
                stack[frames.back().slots_base + slot] = stack.back(); // peek
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_JUMP_IF_FALSE): {
                uint16_t offset = readShort();
                Value condition = stack.back(); // peek
                bool isFalse = (condition.type == ValueType::NIL) || 
                               (condition.type == ValueType::BOOLEAN && !std::get<bool>(condition.data));
                if (isFalse) frames.back().ip += offset;
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_JUMP): {
                uint16_t offset = readShort();
                frames.back().ip += offset;
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_LOOP): {
                uint16_t offset = readShort();
                frames.back().ip -= offset;
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_CLOSURE): {
                Value constant = readConstant();
                auto function = std::get<std::shared_ptr<BytecodeFunction>>(constant.data);
                push(Value::bytecodeFunction(function));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_CALL): {
                int argCount = readByte();
                Value callee = stack[stack.size() - 1 - argCount];
                if (callee.type == ValueType::BYTECODE_FUNCTION) {
                    auto function = std::get<std::shared_ptr<BytecodeFunction>>(callee.data);
                    if (argCount != function->arity) {
                        std::cerr << "Runtime Error: Expected " << function->arity << " arguments but got " << argCount << ".\n";
                        return InterpretResult::INTERPRET_RUNTIME_ERROR;
                    }
                    CallFrame frame;
                    frame.function = function;
                    frame.ip = function->chunk->code.data();
                    frame.slots_base = stack.size() - argCount - 1;
                    frames.push_back(frame);
                } else if (callee.type == ValueType::NATIVE_FUNCTION) {
                    auto native = std::get<std::shared_ptr<VMNativeFunction>>(callee.data);
                    Value result = native->function(argCount, stack.data() + stack.size() - argCount);
                    stack.erase(stack.begin() + stack.size() - argCount - 1, stack.end());
                    push(result);
                } else {
                    std::cerr << "Runtime Error: Can only call functions.\n";
                    return InterpretResult::INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_RETURN): {
                Value result = pop();
                CallFrame frame = frames.back();
                frames.pop_back();
                if (frames.empty()) {
                    pop(); // pop the script function
                    return InterpretResult::INTERPRET_OK;
                }
                stack.erase(stack.begin() + frame.slots_base, stack.end());
                push(result);
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_BUILD_ARRAY): {
                int count = readByte();
                auto arr = std::make_shared<VionArray>();
                for (int i = 0; i < count; ++i) {
                    arr->elements.insert(arr->elements.begin(), pop());
                }
                push(Value::array(arr));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_BUILD_MAP): {
                int count = readByte();
                auto map = std::make_shared<VionMap>();
                for (int i = 0; i < count; ++i) {
                    Value value = pop();
                    Value key = pop();
                    map->entries[key.toString()] = value;
                }
                push(Value::map(map));
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_INDEX_GET): {
                Value index = pop();
                Value collection = pop();
                if (collection.type == ValueType::ARRAY && index.type == ValueType::NUMBER) {
                    auto arr = std::get<std::shared_ptr<VionArray>>(collection.data);
                    int idx = static_cast<int>(std::get<double>(index.data));
                    if (idx < 0) idx += arr->elements.size();
                    if (idx >= 0 && idx < arr->elements.size()) {
                        push(arr->elements[idx]);
                    } else {
                        push(Value::nil());
                    }
                } else if (collection.type == ValueType::MAP && index.type == ValueType::STRING) {
                    auto map = std::get<std::shared_ptr<VionMap>>(collection.data);
                    auto it = map->entries.find(index.toString());
                    if (it != map->entries.end()) {
                        push(it->second);
                    } else {
                        push(Value::nil());
                    }
                } else {
                    std::cerr << "Runtime Error: Invalid index get operation.\n";
                    return InterpretResult::INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case static_cast<uint8_t>(OpCode::OP_INDEX_SET): {
                Value value = pop();
                Value index = pop();
                Value obj = pop();
                if (obj.type == ValueType::ARRAY && index.type == ValueType::NUMBER) {
                    auto arr = std::get<std::shared_ptr<VionArray>>(obj.data);
                    int idx = static_cast<int>(std::get<double>(index.data));
                    if (idx >= 0 && idx < arr->elements.size()) {
                        arr->elements[idx] = value;
                        push(value);
                    } else {
                        std::cerr << "Runtime Error: Array index out of bounds.\n";
                        return InterpretResult::INTERPRET_RUNTIME_ERROR;
                    }
                } else if (obj.type == ValueType::MAP && index.type == ValueType::STRING) {
                    auto map = std::get<std::shared_ptr<VionMap>>(obj.data);
                    map->entries[index.toString()] = value;
                    push(value);
                } else {
                    std::cerr << "Runtime Error: Invalid index set operation.\n";
                    return InterpretResult::INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            default:
                std::cerr << "Runtime Error: Unknown OpCode: " << static_cast<int>(instruction) << "\n";
                return InterpretResult::INTERPRET_RUNTIME_ERROR;
        }
    }
}
