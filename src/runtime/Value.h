#pragma once

#include <cmath>
#include <unordered_map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <unordered_set>
#include <functional>

#include "runtime/GC.h"
#include "sqlite/sqlite3.h"

class VionCallable;
struct BytecodeFunction;

enum class ValueType {
    NUMBER,
    STRING,
    BOOLEAN,
    ARRAY,
    MAP,
    NIL,
    BYTECODE_FUNCTION,
    NATIVE_FUNCTION,
    DB_CONNECTION,
    CLASS,
    INSTANCE
};

using NativeFn = std::function<struct Value(int argCount, struct Value* args)>;

// Forward declaration for VMNativeFunction
struct VMNativeFunction;

// Forward declaration for shared array storage
struct VionArray : public GCObject {
    std::vector<struct Value> elements;
    void trace(std::vector<std::shared_ptr<GCObject>>& children) const override;
    void breakCycles() override { elements.clear(); }
};

// Map storage — unordered for O(1) average lookup
struct VionMap : public GCObject {
    std::unordered_map<std::string, struct Value> entries;
    void trace(std::vector<std::shared_ptr<GCObject>>& children) const override;
    void breakCycles() override { entries.clear(); }
};

// ── Class system ──────────────────────────────────────────────────────────────

// Forward declare for VionClass
struct VionClass;

struct VionClass : public GCObject {
    std::string name;
    std::unordered_map<std::string, struct Value> methods;  // name → BytecodeFunction
    std::unordered_map<std::string, struct Value> statics;  // static methods/fields
    std::shared_ptr<VionClass> superclass;  // nullptr = no parent

    void trace(std::vector<std::shared_ptr<GCObject>>& children) const override {}
    void breakCycles() override { methods.clear(); statics.clear(); superclass.reset(); }
};

struct VionInstance : public GCObject {
    std::shared_ptr<VionClass> klass;
    std::unordered_map<std::string, struct Value> fields;  // instance fields

    void trace(std::vector<std::shared_ptr<GCObject>>& children) const override {}
    void breakCycles() override { fields.clear(); klass.reset(); }
};

// SQLite database connection wrapper — supports multiple drivers
enum class DBDriver { SQLITE, POSTGRES, MYSQL };

struct VionDB : public GCObject {
    DBDriver driver = DBDriver::SQLITE;
    // SQLite
    sqlite3* db = nullptr;
    // PostgreSQL — store full DSN for psql CLI
    std::string connstr;
    // MySQL — individual connection parameters
    std::string host = "127.0.0.1";
    std::string port = "3306";
    std::string user;
    std::string password;
    std::string database;
    // State
    bool closed = false;

    ~VionDB() {
        if (driver == DBDriver::SQLITE && db && !closed) {
            sqlite3_close(db);
            db = nullptr;
        }
    }
    void trace(std::vector<std::shared_ptr<GCObject>>& children) const override {}
    void breakCycles() override {
        if (driver == DBDriver::SQLITE && db && !closed) {
            sqlite3_close(db);
            db = nullptr;
        }
        closed = true;
    }
};


struct Value {
    ValueType type = ValueType::NIL;
    std::variant<
        double,
        std::string,
        bool,
        std::shared_ptr<VionArray>,
        std::shared_ptr<VionMap>,
        std::shared_ptr<BytecodeFunction>,
        std::shared_ptr<VMNativeFunction>,
        std::shared_ptr<VionDB>,
        std::shared_ptr<VionClass>,
        std::shared_ptr<VionInstance>
    > data;

    // ── Factories ──────────────────────────────────────────────────────────

    static Value number(double v) {
        Value r; r.type = ValueType::NUMBER; r.data = v; return r;
    }
    static Value string(std::string v) {
        Value r; r.type = ValueType::STRING; r.data = std::move(v); return r;
    }
    static Value bytecodeFunction(std::shared_ptr<BytecodeFunction> v) {
        Value r; r.type = ValueType::BYTECODE_FUNCTION; r.data = std::move(v); return r;
    }
    static Value nativeFunction(std::shared_ptr<VMNativeFunction> v) {
        Value r; r.type = ValueType::NATIVE_FUNCTION; r.data = std::move(v); return r;
    }
    static Value boolean(bool v) {
        Value r; r.type = ValueType::BOOLEAN; r.data = v; return r;
    }
    static Value array(std::shared_ptr<VionArray> v) {
        Value r; r.type = ValueType::ARRAY; r.data = std::move(v); return r;
    }
    static Value array() {
        return array(std::make_shared<VionArray>());
    }
    static Value map(std::shared_ptr<VionMap> v) {
        Value r; r.type = ValueType::MAP; r.data = std::move(v); return r;
    }
    static Value map() {
        return map(std::make_shared<VionMap>());
    }
    static Value db(std::shared_ptr<VionDB> v) {
        Value r; r.type = ValueType::DB_CONNECTION; r.data = std::move(v); return r;
    }
    static Value nil() { return Value{}; }
    static Value vionClass(std::shared_ptr<VionClass> v) {
        Value r; r.type = ValueType::CLASS; r.data = std::move(v); return r;
    }
    static Value instance(std::shared_ptr<VionInstance> v) {
        Value r; r.type = ValueType::INSTANCE; r.data = std::move(v); return r;
    }

    // ── Accessors ──────────────────────────────────────────────────────────

    double asNumber() const {
        if (type != ValueType::NUMBER)
            throw std::runtime_error("Runtime Error: expected number.");
        return std::get<double>(data);
    }

    const std::string& asString() const {
        if (type != ValueType::STRING)
            throw std::runtime_error("Runtime Error: expected string.");
        return std::get<std::string>(data);
    }

    bool asBoolean() const {
        if (type != ValueType::BOOLEAN)
            throw std::runtime_error("Runtime Error: expected boolean.");
        return std::get<bool>(data);
    }

    std::shared_ptr<VionArray> asArray() const {
        if (type != ValueType::ARRAY)
            throw std::runtime_error("Runtime Error: expected array.");
        return std::get<std::shared_ptr<VionArray>>(data);
    }

    std::shared_ptr<VionMap> asMap() const {
        if (type != ValueType::MAP)
            throw std::runtime_error("Runtime Error: expected map.");
        return std::get<std::shared_ptr<VionMap>>(data);
    }

    // ── Truthiness ────────────────────────────────────────────────────────

    bool isTruthy() const {
        switch (type) {
            case ValueType::NIL:      return false;
            case ValueType::BOOLEAN:  return std::get<bool>(data);
            case ValueType::NUMBER:   return std::get<double>(data) != 0.0;
            case ValueType::STRING:   return !std::get<std::string>(data).empty();
            case ValueType::BYTECODE_FUNCTION: return true;
            case ValueType::NATIVE_FUNCTION: return true;
            case ValueType::ARRAY:    return true;
            case ValueType::MAP:      return true;
            case ValueType::DB_CONNECTION: return true;
            case ValueType::CLASS:    return true;
            case ValueType::INSTANCE: return true;
        }
        return false;
    }

    bool operator==(const Value& other) const {
        if (type != other.type) return false;
        switch (type) {
            case ValueType::NIL: return true;
            case ValueType::BOOLEAN: return std::get<bool>(data) == std::get<bool>(other.data);
            case ValueType::NUMBER: return std::get<double>(data) == std::get<double>(other.data);
            case ValueType::STRING: return std::get<std::string>(data) == std::get<std::string>(other.data);
            case ValueType::BYTECODE_FUNCTION: return std::get<std::shared_ptr<BytecodeFunction>>(data) == std::get<std::shared_ptr<BytecodeFunction>>(other.data);
            case ValueType::NATIVE_FUNCTION: return std::get<std::shared_ptr<VMNativeFunction>>(data) == std::get<std::shared_ptr<VMNativeFunction>>(other.data);
            case ValueType::ARRAY: return std::get<std::shared_ptr<VionArray>>(data) == std::get<std::shared_ptr<VionArray>>(other.data);
            case ValueType::MAP: return std::get<std::shared_ptr<VionMap>>(data) == std::get<std::shared_ptr<VionMap>>(other.data);
            case ValueType::DB_CONNECTION: return std::get<std::shared_ptr<VionDB>>(data) == std::get<std::shared_ptr<VionDB>>(other.data);
            case ValueType::CLASS: return std::get<std::shared_ptr<VionClass>>(data) == std::get<std::shared_ptr<VionClass>>(other.data);
            case ValueType::INSTANCE: return std::get<std::shared_ptr<VionInstance>>(data) == std::get<std::shared_ptr<VionInstance>>(other.data);
        }
        return false;
    }

    // ── Display ───────────────────────────────────────────────────────────

    std::string toString() const {
        std::unordered_set<const void*> visited;
        return toStringImpl(visited, false);
    }
    
    std::string toJsonString() const {
        std::unordered_set<const void*> visited;
        return toStringImpl(visited, true);
    }

    std::string toStringImpl(std::unordered_set<const void*>& visited, bool quoteStrings) const {
        switch (type) {
            case ValueType::NUMBER: {
                double v = std::get<double>(data);
                // Print integers without decimal point: 10.0 → "10"
                if (v == std::floor(v) && std::isfinite(v) &&
                    v >= -1e15 && v <= 1e15) {
                    std::ostringstream out;
                    out << static_cast<long long>(v);
                    return out.str();
                }
                std::ostringstream out;
                out << v;
                return out.str();
            }
            case ValueType::STRING: {
                std::string s = std::get<std::string>(data);
                if (quoteStrings) {
                    // Extremely basic escaping
                    std::string escaped = "\"";
                    for (char c : s) {
                        if (c == '"') escaped += "\\\"";
                        else if (c == '\\') escaped += "\\\\";
                        else escaped += c;
                    }
                    escaped += "\"";
                    return escaped;
                }
                return s;
            }
            case ValueType::BOOLEAN:
                return std::get<bool>(data) ? "true" : "false";
            case ValueType::BYTECODE_FUNCTION:
                return "<fn>";
            case ValueType::NATIVE_FUNCTION:
                return "<native fn>";
            case ValueType::ARRAY: {
                const auto& arr = std::get<std::shared_ptr<VionArray>>(data);
                if (visited.count(arr.get())) return "[Circular]";
                visited.insert(arr.get());
                std::ostringstream out;
                out << "[";
                for (std::size_t i = 0; i < arr->elements.size(); ++i) {
                    if (i > 0) out << ", ";
                    out << arr->elements[i].toStringImpl(visited, quoteStrings);
                }
                out << "]";
                visited.erase(arr.get());
                return out.str();
            }
            case ValueType::MAP: {
                const auto& m = std::get<std::shared_ptr<VionMap>>(data);
                if (visited.count(m.get())) return "[Circular]";
                visited.insert(m.get());
                std::ostringstream out;
                out << "{";
                bool first = true;
                for (const auto& [k, v] : m->entries) {
                    if (!first) out << ", ";
                    first = false;
                    out << '"' << k << "\": " << v.toStringImpl(visited, quoteStrings);
                }
                out << "}";
                visited.erase(m.get());
                return out.str();
            }
            case ValueType::DB_CONNECTION: {
                auto dbConn = std::get<std::shared_ptr<VionDB>>(data);
                if (dbConn->closed || !dbConn->db) return "<db:closed>";
                return "<db:" + std::string(sqlite3_db_filename(dbConn->db, "main")) + ">";
            }
            case ValueType::CLASS: {
                auto klass = std::get<std::shared_ptr<VionClass>>(data);
                return "<class " + klass->name + ">";
            }
            case ValueType::INSTANCE: {
                auto inst = std::get<std::shared_ptr<VionInstance>>(data);
                // Check for __str__ method
                auto it = inst->klass->methods.find("__str__");
                if (it == inst->klass->methods.end())
                    it = inst->klass->methods.find("to_string");
                // Print fields as map-like for now
                if (visited.count(inst.get())) return "[Circular]";
                visited.insert(inst.get());
                std::ostringstream out;
                out << inst->klass->name << "{";
                bool first = true;
                for (const auto& [k, v] : inst->fields) {
                    if (!first) out << ", ";
                    first = false;
                    out << k << ": " << v.toStringImpl(visited, quoteStrings);
                }
                out << "}";
                visited.erase(inst.get());
                return out.str();
            }
            case ValueType::NIL:
                return "null"; // JSON uses null instead of nil
        }
        return "null";
    }

    // ── Type name ─────────────────────────────────────────────────────────

    std::string typeName() const {
        switch (type) {
            case ValueType::NUMBER:   return "number";
            case ValueType::STRING:   return "string";
            case ValueType::BOOLEAN:  return "boolean";
            case ValueType::ARRAY:    return "array";
            case ValueType::MAP:      return "map";
            case ValueType::NIL:      return "nil";
            case ValueType::DB_CONNECTION: return "db";
            case ValueType::CLASS: return "class";
            case ValueType::INSTANCE: {
                auto inst = std::get<std::shared_ptr<VionInstance>>(data);
                return inst->klass->name;
            }
            default: return "unknown";
        }
        return "unknown";
    }
};

struct VMNativeFunction : public GCObject {
    NativeFn function;
    std::string name;
    void trace(std::vector<std::shared_ptr<GCObject>>& children) const override {}
    void breakCycles() override {}
};
