#include "interpreter/Interpreter.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

// ── Control-flow signals ──────────────────────────────────────────────────────

namespace {

class ReturnSignal {
public:
    explicit ReturnSignal(Value value) : value(std::move(value)) {}
    Value value;
};

class BreakSignal {};
class ContinueSignal {};

// ── User-defined function ─────────────────────────────────────────────────────

class VionFunction final : public VionCallable {
public:
    VionFunction(const FunctionStmt& declaration, std::shared_ptr<Environment> closure)
        : name_(declaration.name),
          parameters_(declaration.parameters),
          body_(declaration.body),         // shared_ptr — body outlives the AST safely
          closure_(std::move(closure)) {}

    int arity() const override {
        return static_cast<int>(parameters_.size());
    }

    Value call(Interpreter& interpreter, const std::vector<Value>& arguments) override {
        auto callEnv = std::make_shared<Environment>(closure_);

        for (std::size_t i = 0; i < parameters_.size(); ++i) {
            callEnv->define(parameters_[i], arguments[i]);
        }

        try {
            interpreter.executeBlock(*body_, callEnv);
        } catch (const ReturnSignal& sig) {
            return sig.value;
        }

        return Value::nil();
    }

    std::string toString() const override {
        return "<fn " + name_ + ">";
    }

private:
    std::string name_;
    std::vector<std::string> parameters_;
    std::shared_ptr<BlockStmt> body_;      // shared ownership — safe across REPL sessions
    std::shared_ptr<Environment> closure_;
};

// ── Native function helper ────────────────────────────────────────────────────

class NativeFunction final : public VionCallable {
public:
    using Fn = std::function<Value(Interpreter&, const std::vector<Value>&)>;

    NativeFunction(std::string name, int arity, Fn fn)
        : name_(std::move(name)), arity_(arity), fn_(std::move(fn)) {}

    int arity() const override { return arity_; }

    Value call(Interpreter& interpreter, const std::vector<Value>& args) override {
        return fn_(interpreter, args);
    }

    std::string toString() const override { return "<native fn " + name_ + ">"; }

private:
    std::string name_;
    int arity_;
    Fn fn_;
};

// ── Error formatting ──────────────────────────────────────────────────────────

std::string argCountError(int expected, std::size_t received) {
    std::ostringstream out;
    out << "Runtime Error: expected " << expected
        << " argument(s) but got " << received << ".";
    return out.str();
}

} // namespace

// ── Interpreter ───────────────────────────────────────────────────────────────

Interpreter::Interpreter()
    : globals(std::make_shared<Environment>()), environment(globals) {
    registerBuiltins();
}

std::string Interpreter::locationOf(int line) const {
    if (line > 0) return " [line " + std::to_string(line) + "]";
    return "";
}

void Interpreter::registerBuiltins() {
    // len(val) — string length or array length
    globals->define("len", Value::function(std::make_shared<NativeFunction>(
        "len", 1,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            const Value& v = args[0];
            if (v.type == ValueType::STRING)
                return Value::number(static_cast<double>(v.asString().size()));
            if (v.type == ValueType::ARRAY)
                return Value::number(static_cast<double>(v.asArray()->elements.size()));
            throw std::runtime_error("Runtime Error: len() expects a string or array.");
        }
    )));

    // push(arr, val) — append element to array, returns the array
    globals->define("push", Value::function(std::make_shared<NativeFunction>(
        "push", 2,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            if (args[0].type != ValueType::ARRAY)
                throw std::runtime_error("Runtime Error: push() expects an array as first argument.");
            args[0].asArray()->elements.push_back(args[1]);
            return args[0];
        }
    )));

    // pop(arr) — remove and return the last element
    globals->define("pop", Value::function(std::make_shared<NativeFunction>(
        "pop", 1,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            if (args[0].type != ValueType::ARRAY)
                throw std::runtime_error("Runtime Error: pop() expects an array.");
            auto arr = args[0].asArray();
            if (arr->elements.empty())
                throw std::runtime_error("Runtime Error: pop() called on an empty array.");
            Value last = arr->elements.back();
            arr->elements.pop_back();
            return last;
        }
    )));

    // str(val) — convert any value to its string representation
    globals->define("str", Value::function(std::make_shared<NativeFunction>(
        "str", 1,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            return Value::string(args[0].toString());
        }
    )));

    // num(val) — convert string to number
    globals->define("num", Value::function(std::make_shared<NativeFunction>(
        "num", 1,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            if (args[0].type == ValueType::NUMBER) return args[0];
            if (args[0].type == ValueType::STRING) {
                try {
                    return Value::number(std::stod(args[0].asString()));
                } catch (...) {
                    throw std::runtime_error(
                        "Runtime Error: num() cannot convert \"" + args[0].asString() + "\" to a number.");
                }
            }
            throw std::runtime_error("Runtime Error: num() expects a string or number.");
        }
    )));

    // type(val) — return the type name as a string
    globals->define("type", Value::function(std::make_shared<NativeFunction>(
        "type", 1,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            return Value::string(args[0].typeName());
        }
    )));

    // input(prompt?) — read a line from stdin; prompt is optional
    globals->define("input", Value::function(std::make_shared<NativeFunction>(
        "input", -1,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            if (!args.empty()) std::cout << args[0].toString();
            std::string line;
            if (!std::getline(std::cin, line)) return Value::nil();
            return Value::string(line);
        }
    )));

    // clock() — seconds since epoch (useful for timing)
    globals->define("clock", Value::function(std::make_shared<NativeFunction>(
        "clock", 0,
        [](Interpreter&, const std::vector<Value>&) -> Value {
            auto now = std::chrono::system_clock::now().time_since_epoch();
            double secs = std::chrono::duration<double>(now).count();
            return Value::number(secs);
        }
    )));

    // floor(n)
    globals->define("floor", Value::function(std::make_shared<NativeFunction>(
        "floor", 1,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            return Value::number(std::floor(args[0].asNumber()));
        }
    )));

    // ceil(n)
    globals->define("ceil", Value::function(std::make_shared<NativeFunction>(
        "ceil", 1,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            return Value::number(std::ceil(args[0].asNumber()));
        }
    )));

    // sqrt(n)
    globals->define("sqrt", Value::function(std::make_shared<NativeFunction>(
        "sqrt", 1,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            double v = args[0].asNumber();
            if (v < 0) throw std::runtime_error("Runtime Error: sqrt() of negative number.");
            return Value::number(std::sqrt(v));
        }
    )));

    // abs(n)
    globals->define("abs", Value::function(std::make_shared<NativeFunction>(
        "abs", 1,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            return Value::number(std::abs(args[0].asNumber()));
        }
    )));

    // max(a, b)
    globals->define("max", Value::function(std::make_shared<NativeFunction>(
        "max", 2,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            return Value::number(std::max(args[0].asNumber(), args[1].asNumber()));
        }
    )));

    // min(a, b)
    globals->define("min", Value::function(std::make_shared<NativeFunction>(
        "min", 2,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            return Value::number(std::min(args[0].asNumber(), args[1].asNumber()));
        }
    )));

    // array(size, fill) — create array with `size` elements initialized to `fill`
    globals->define("array", Value::function(std::make_shared<NativeFunction>(
        "array", 2,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            int size = static_cast<int>(args[0].asNumber());
            if (size < 0) throw std::runtime_error("Runtime Error: array() size cannot be negative.");
            auto arr = std::make_shared<VionArray>();
            arr->elements.assign(size, args[1]);
            return Value::array(arr);
        }
    )));
    // range(n) / range(start,end) / range(start,end,step)
    globals->define("range", Value::function(std::make_shared<NativeFunction>(
        "range", -1,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            if (args.empty() || args.size() > 3)
                throw std::runtime_error("Runtime Error: range() expects 1-3 arguments.");
            double start = 0, end = 0, step = 1;
            if (args.size() == 1) { end = args[0].asNumber(); }
            else if (args.size() == 2) { start = args[0].asNumber(); end = args[1].asNumber(); }
            else { start = args[0].asNumber(); end = args[1].asNumber(); step = args[2].asNumber(); }
            if (std::abs(step) < 1e-12) throw std::runtime_error("Runtime Error: range() step cannot be zero.");
            auto arr = std::make_shared<VionArray>();
            for (double i = start; (step > 0 ? i < end : i > end); i += step)
                arr->elements.push_back(Value::number(i));
            return Value::array(arr);
        }
    )));
    globals->define("upper", Value::function(std::make_shared<NativeFunction>("upper", 1,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            std::string s = args[0].asString();
            std::transform(s.begin(), s.end(), s.begin(), ::toupper);
            return Value::string(s);
        }
    )));
    globals->define("lower", Value::function(std::make_shared<NativeFunction>("lower", 1,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            std::string s = args[0].asString();
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            return Value::string(s);
        }
    )));
    globals->define("trim", Value::function(std::make_shared<NativeFunction>("trim", 1,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            std::string s = args[0].asString();
            auto st = s.find_first_not_of(" \t\r\n");
            auto en = s.find_last_not_of(" \t\r\n");
            return Value::string(st == std::string::npos ? "" : s.substr(st, en - st + 1));
        }
    )));
    globals->define("split", Value::function(std::make_shared<NativeFunction>("split", 2,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            const std::string& s = args[0].asString(), &sep = args[1].asString();
            auto arr = std::make_shared<VionArray>();
            if (sep.empty()) {
                for (char c : s) arr->elements.push_back(Value::string(std::string(1, c)));
                return Value::array(arr);
            }
            std::size_t pos = 0, found;
            while ((found = s.find(sep, pos)) != std::string::npos) {
                arr->elements.push_back(Value::string(s.substr(pos, found - pos)));
                pos = found + sep.size();
            }
            arr->elements.push_back(Value::string(s.substr(pos)));
            return Value::array(arr);
        }
    )));
    globals->define("join", Value::function(std::make_shared<NativeFunction>("join", 2,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            auto arr = args[0].asArray();
            const std::string& sep = args[1].asString();
            std::ostringstream out;
            for (std::size_t i = 0; i < arr->elements.size(); ++i) {
                if (i > 0) out << sep;
                out << arr->elements[i].toString();
            }
            return Value::string(out.str());
        }
    )));
    globals->define("contains", Value::function(std::make_shared<NativeFunction>("contains", 2,
        [this](Interpreter& interp, const std::vector<Value>& args) -> Value {
            if (args[0].type == ValueType::STRING)
                return Value::boolean(args[0].asString().find(args[1].asString()) != std::string::npos);
            if (args[0].type == ValueType::ARRAY) {
                for (const auto& e : args[0].asArray()->elements)
                    if (interp.valuesEqual(e, args[1])) return Value::boolean(true);
                return Value::boolean(false);
            }
            throw std::runtime_error("Runtime Error: contains() expects string or array.");
        }
    )));
    globals->define("replace", Value::function(std::make_shared<NativeFunction>("replace", 3,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            std::string s = args[0].asString();
            const std::string& from = args[1].asString(), &to = args[2].asString();
            if (!from.empty()) {
                std::size_t pos = 0;
                while ((pos = s.find(from, pos)) != std::string::npos) { s.replace(pos, from.size(), to); pos += to.size(); }
            }
            return Value::string(s);
        }
    )));
    globals->define("startsWith", Value::function(std::make_shared<NativeFunction>("startsWith", 2,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            const std::string& s = args[0].asString(), &p = args[1].asString();
            return Value::boolean(s.size() >= p.size() && s.substr(0, p.size()) == p);
        }
    )));
    globals->define("endsWith", Value::function(std::make_shared<NativeFunction>("endsWith", 2,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            const std::string& s = args[0].asString(), &p = args[1].asString();
            return Value::boolean(s.size() >= p.size() && s.substr(s.size() - p.size()) == p);
        }
    )));
    globals->define("indexOf", Value::function(std::make_shared<NativeFunction>("indexOf", 2,
        [this](Interpreter& interp, const std::vector<Value>& args) -> Value {
            if (args[0].type == ValueType::STRING) {
                auto p = args[0].asString().find(args[1].asString());
                return p == std::string::npos ? Value::number(-1) : Value::number((double)p);
            }
            if (args[0].type == ValueType::ARRAY) {
                const auto& el = args[0].asArray()->elements;
                for (std::size_t i = 0; i < el.size(); ++i)
                    if (interp.valuesEqual(el[i], args[1])) return Value::number((double)i);
                return Value::number(-1);
            }
            throw std::runtime_error("Runtime Error: indexOf() expects string or array.");
        }
    )));
    globals->define("keys", Value::function(std::make_shared<NativeFunction>("keys", 1,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            auto arr = std::make_shared<VionArray>();
            for (const auto& [k, v] : args[0].asMap()->entries) arr->elements.push_back(Value::string(k));
            return Value::array(arr);
        }
    )));
    globals->define("values", Value::function(std::make_shared<NativeFunction>("values", 1,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            auto arr = std::make_shared<VionArray>();
            for (const auto& [k, v] : args[0].asMap()->entries) arr->elements.push_back(v);
            return Value::array(arr);
        }
    )));
    globals->define("hasKey", Value::function(std::make_shared<NativeFunction>("hasKey", 2,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            return Value::boolean(args[0].asMap()->entries.count(args[1].asString()) > 0);
        }
    )));
    globals->define("del", Value::function(std::make_shared<NativeFunction>("del", 2,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            args[0].asMap()->entries.erase(args[1].asString());
            return args[0];
        }
    )));
    globals->define("readFile", Value::function(std::make_shared<NativeFunction>("readFile", 1,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            std::ifstream f(args[0].asString());
            if (!f) throw std::runtime_error("Runtime Error: cannot open '" + args[0].asString() + "'.");
            std::ostringstream buf; buf << f.rdbuf();
            return Value::string(buf.str());
        }
    )));
    globals->define("writeFile", Value::function(std::make_shared<NativeFunction>("writeFile", 2,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            std::ofstream f(args[0].asString());
            if (!f) throw std::runtime_error("Runtime Error: cannot write '" + args[0].asString() + "'.");
            f << args[1].asString(); return Value::boolean(true);
        }
    )));
    globals->define("appendFile", Value::function(std::make_shared<NativeFunction>("appendFile", 2,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            std::ofstream f(args[0].asString(), std::ios::app);
            if (!f) throw std::runtime_error("Runtime Error: cannot append '" + args[0].asString() + "'.");
            f << args[1].asString(); return Value::boolean(true);
        }
    )));
    globals->define("fileExists", Value::function(std::make_shared<NativeFunction>("fileExists", 1,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            return Value::boolean(std::ifstream(args[0].asString()).good());
        }
    )));
    globals->define("random", Value::function(std::make_shared<NativeFunction>("random", 0,
        [](Interpreter&, const std::vector<Value>&) -> Value {
            static std::mt19937 rng(std::random_device{}());
            static std::uniform_real_distribution<double> dist(0.0, 1.0);
            return Value::number(dist(rng));
        }
    )));
    globals->define("sleep", Value::function(std::make_shared<NativeFunction>("sleep", 1,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            std::this_thread::sleep_for(std::chrono::milliseconds((int)args[0].asNumber()));
            return Value::nil();
        }
    )));
    globals->define("exit", Value::function(std::make_shared<NativeFunction>("exit", 1,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            std::exit((int)args[0].asNumber());
        }
    )));

    // substr(str, start, len) — substring
    globals->define("substr", Value::function(std::make_shared<NativeFunction>("substr", 3,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            const std::string& s = args[0].asString();
            int start = static_cast<int>(args[1].asNumber());
            int len   = static_cast<int>(args[2].asNumber());
            if (start < 0) start = std::max(0, (int)s.size() + start);
            if (start >= (int)s.size()) return Value::string("");
            return Value::string(s.substr(static_cast<std::size_t>(start),
                                          static_cast<std::size_t>(std::max(0, len))));
        }
    )));

    // randInt(min, max) — random integer in [min, max]
    globals->define("randInt", Value::function(std::make_shared<NativeFunction>("randInt", 2,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            static std::mt19937 rng(std::random_device{}());
            int lo = static_cast<int>(args[0].asNumber());
            int hi = static_cast<int>(args[1].asNumber());
            if (lo > hi) throw std::runtime_error("Runtime Error: randInt() min > max.");
            std::uniform_int_distribution<int> dist(lo, hi);
            return Value::number(static_cast<double>(dist(rng)));
        }
    )));

    // -- Phase 2: Output functions
    globals->define("println", Value::function(std::make_shared<NativeFunction>("println", -1,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            for (std::size_t i = 0; i < args.size(); ++i) { if (i > 0) std::cout << " "; std::cout << args[i].toString(); }
            std::cout << "\n"; return Value::nil();
        })));
    globals->define("write", Value::function(std::make_shared<NativeFunction>("write", -1,
        [](Interpreter&, const std::vector<Value>& args) -> Value {
            for (std::size_t i = 0; i < args.size(); ++i) { if (i > 0) std::cout << " "; std::cout << args[i].toString(); }
            return Value::nil();
        })));

    // -- Phase 2: Higher-order functions
    globals->define("map", Value::function(std::make_shared<NativeFunction>("map", 2,
        [](Interpreter& interp, const std::vector<Value>& args) -> Value {
            auto src = args[0].asArray(); auto fn = args[1].asFunction();
            auto out = std::make_shared<VionArray>();
            for (const auto& e : src->elements) out->elements.push_back(fn->call(interp, {e}));
            return Value::array(out);
        })));
    globals->define("filter", Value::function(std::make_shared<NativeFunction>("filter", 2,
        [](Interpreter& interp, const std::vector<Value>& args) -> Value {
            auto src = args[0].asArray(); auto fn = args[1].asFunction();
            auto out = std::make_shared<VionArray>();
            for (const auto& e : src->elements) if (fn->call(interp, {e}).isTruthy()) out->elements.push_back(e);
            return Value::array(out);
        })));
    globals->define("reduce", Value::function(std::make_shared<NativeFunction>("reduce", 3,
        [](Interpreter& interp, const std::vector<Value>& args) -> Value {
            auto src = args[0].asArray(); auto fn = args[2].asFunction(); Value acc = args[1];
            for (const auto& e : src->elements) acc = fn->call(interp, {acc, e});
            return acc;
        })));

    // -- Phase 4.1: Math
    globals->define("pow", Value::function(std::make_shared<NativeFunction>("pow", 2,
        [](Interpreter&, const std::vector<Value>& a) -> Value { return Value::number(std::pow(a[0].asNumber(), a[1].asNumber())); })));
    globals->define("log", Value::function(std::make_shared<NativeFunction>("log", -1,
        [](Interpreter&, const std::vector<Value>& a) -> Value {
            if (a.empty() || a.size() > 2) throw std::runtime_error("Runtime Error: log() expects 1-2 args.");
            return a.size()==1 ? Value::number(std::log(a[0].asNumber())) : Value::number(std::log(a[0].asNumber())/std::log(a[1].asNumber()));
        })));
    globals->define("round", Value::function(std::make_shared<NativeFunction>("round", -1,
        [](Interpreter&, const std::vector<Value>& a) -> Value {
            if (a.empty() || a.size() > 2) throw std::runtime_error("Runtime Error: round() expects 1-2 args.");
            if (a.size()==1) return Value::number(std::round(a[0].asNumber()));
            double f=std::pow(10.0,a[1].asNumber()); return Value::number(std::round(a[0].asNumber()*f)/f);
        })));
    globals->define("pi",  Value::function(std::make_shared<NativeFunction>("pi",  0,
        [](Interpreter&, const std::vector<Value>&) -> Value { return Value::number(3.14159265358979323846); })));
    globals->define("sin", Value::function(std::make_shared<NativeFunction>("sin", 1,
        [](Interpreter&, const std::vector<Value>& a) -> Value { return Value::number(std::sin(a[0].asNumber())); })));
    globals->define("cos", Value::function(std::make_shared<NativeFunction>("cos", 1,
        [](Interpreter&, const std::vector<Value>& a) -> Value { return Value::number(std::cos(a[0].asNumber())); })));
    globals->define("tan", Value::function(std::make_shared<NativeFunction>("tan", 1,
        [](Interpreter&, const std::vector<Value>& a) -> Value { return Value::number(std::tan(a[0].asNumber())); })));

    // -- Phase 4.2: String extras
    globals->define("repeat", Value::function(std::make_shared<NativeFunction>("repeat", 2,
        [](Interpreter&, const std::vector<Value>& a) -> Value {
            const std::string& s=a[0].asString(); int n=(int)a[1].asNumber();
            std::string out; for(int i=0;i<n;++i) out+=s; return Value::string(out);
        })));
    globals->define("padLeft", Value::function(std::make_shared<NativeFunction>("padLeft", -1,
        [](Interpreter&, const std::vector<Value>& a) -> Value {
            if(a.size()<2) throw std::runtime_error("Runtime Error: padLeft() needs 2-3 args.");
            std::string s=a[0].asString(); int w=(int)a[1].asNumber();
            std::string pad=a.size()>=3?a[2].asString():" "; if(pad.empty()) pad=" ";
            while((int)s.size()<w) s=pad+s;
            return Value::string(s.size()>(std::size_t)w?s.substr(s.size()-w):s);
        })));
    globals->define("padRight", Value::function(std::make_shared<NativeFunction>("padRight", -1,
        [](Interpreter&, const std::vector<Value>& a) -> Value {
            if(a.size()<2) throw std::runtime_error("Runtime Error: padRight() needs 2-3 args.");
            std::string s=a[0].asString(); int w=(int)a[1].asNumber();
            std::string pad=a.size()>=3?a[2].asString():" "; if(pad.empty()) pad=" ";
            while((int)s.size()<w) s+=pad;
            return Value::string(s.substr(0,(std::size_t)w));
        })));

    // -- Phase 4.3: Array extras
    globals->define("sort", Value::function(std::make_shared<NativeFunction>("sort", -1,
        [](Interpreter& interp, const std::vector<Value>& a) -> Value {
            if(a.empty()) throw std::runtime_error("Runtime Error: sort() needs 1-2 args.");
            auto out=std::make_shared<VionArray>(); out->elements=a[0].asArray()->elements;
            if(a.size()>=2){auto fn=a[1].asFunction(); std::sort(out->elements.begin(),out->elements.end(),[&](const Value& x,const Value& y){return fn->call(interp,{x,y}).isTruthy();});}
            else{std::sort(out->elements.begin(),out->elements.end(),[](const Value& x,const Value& y){if(x.type==ValueType::NUMBER&&y.type==ValueType::NUMBER)return x.asNumber()<y.asNumber();return x.toString()<y.toString();});}
            return Value::array(out);
        })));
    globals->define("reverse", Value::function(std::make_shared<NativeFunction>("reverse", 1,
        [](Interpreter&, const std::vector<Value>& a) -> Value {
            auto out=std::make_shared<VionArray>(); out->elements=a[0].asArray()->elements;
            std::reverse(out->elements.begin(),out->elements.end()); return Value::array(out);
        })));
    globals->define("slice", Value::function(std::make_shared<NativeFunction>("slice", -1,
        [](Interpreter&, const std::vector<Value>& a) -> Value {
            if(a.empty()) throw std::runtime_error("Runtime Error: slice() needs 1-3 args.");
            auto src=a[0].asArray(); int sz=(int)src->elements.size();
            int st=a.size()>=2?(int)a[1].asNumber():0, en=a.size()>=3?(int)a[2].asNumber():sz;
            if(st<0)st=std::max(0,sz+st); if(en<0)en=std::max(0,sz+en);
            st=std::min(st,sz); en=std::min(en,sz);
            auto out=std::make_shared<VionArray>();
            for(int i=st;i<en;++i) out->elements.push_back(src->elements[i]);
            return Value::array(out);
        })));
    globals->define("flat", Value::function(std::make_shared<NativeFunction>("flat", 1,
        [](Interpreter&, const std::vector<Value>& a) -> Value {
            auto out=std::make_shared<VionArray>();
            for(const auto& e:a[0].asArray()->elements){
                if(e.type==ValueType::ARRAY) for(const auto& inner:e.asArray()->elements) out->elements.push_back(inner);
                else out->elements.push_back(e);
            }
            return Value::array(out);
        })));

    // -- Phase 4.4: System
    globals->define("env", Value::function(std::make_shared<NativeFunction>("env", 1,
        [](Interpreter&, const std::vector<Value>& a) -> Value {
            const char* v=std::getenv(a[0].asString().c_str()); return v?Value::string(v):Value::nil();
        })));
}

// ── Core interpreter ──────────────────────────────────────────────────────────

void Interpreter::interpret(const Program& program) {
    try {
        for (const auto& stmt : program.statements) {
            execute(*stmt);
        }
    } catch (const ReturnSignal&) {
        throw std::runtime_error("Runtime Error: return outside function.");
    } catch (const BreakSignal&) {
        throw std::runtime_error("Runtime Error: break outside loop.");
    } catch (const ContinueSignal&) {
        throw std::runtime_error("Runtime Error: continue outside loop.");
    }
}

void Interpreter::executeBlock(const BlockStmt& block, std::shared_ptr<Environment> blockEnv) {
    auto previous = environment;
    environment = std::move(blockEnv);

    try {
        for (const auto& stmt : block.statements) {
            execute(*stmt);
        }
    } catch (...) {
        environment = previous;
        throw;
    }

    environment = previous;
}

void Interpreter::execute(const Stmt& statement) {
    if (const auto* letStmt = dynamic_cast<const LetStmt*>(&statement)) {
        Value value = evaluate(*letStmt->value);
        environment->define(letStmt->name, value);
        return;
    }

    if (const auto* printStmt = dynamic_cast<const PrintStmt*>(&statement)) {
        std::string out;
        for (std::size_t i = 0; i < printStmt->values.size(); ++i) {
            if (i > 0) out += " ";
            out += evaluate(*printStmt->values[i]).toString();
        }
        std::cout << out << "\n";
        return;
    }

    if (const auto* exprStmt = dynamic_cast<const ExpressionStmt*>(&statement)) {
        evaluate(*exprStmt->value);
        return;
    }

    if (const auto* blockStmt = dynamic_cast<const BlockStmt*>(&statement)) {
        executeBlock(*blockStmt, std::make_shared<Environment>(environment));
        return;
    }

    if (const auto* ifStmt = dynamic_cast<const IfStmt*>(&statement)) {
        Value condition = evaluate(*ifStmt->condition);
        if (condition.isTruthy()) {
            executeBlock(*ifStmt->thenBranch, std::make_shared<Environment>(environment));
        } else if (ifStmt->elseBranch) {
            executeBlock(*ifStmt->elseBranch, std::make_shared<Environment>(environment));
        }
        return;
    }

    if (const auto* whileStmt = dynamic_cast<const WhileStmt*>(&statement)) {
        while (evaluate(*whileStmt->condition).isTruthy()) {
            try {
                executeBlock(*whileStmt->body, std::make_shared<Environment>(environment));
            } catch (const BreakSignal&) {
                break;
            } catch (const ContinueSignal&) {
                continue;
            }
        }
        return;
    }

    if (const auto* forStmt = dynamic_cast<const ForStmt*>(&statement)) {
        Value iterableVal = evaluate(*forStmt->iterable);

        if (iterableVal.type == ValueType::ARRAY) {
            // Snapshot the array — iterate a copy of the element list
            auto elements = iterableVal.asArray()->elements;
            for (const Value& element : elements) {
                auto loopEnv = std::make_shared<Environment>(environment);
                loopEnv->define(forStmt->variable, element);
                try {
                    executeBlock(*forStmt->body, loopEnv);
                } catch (const BreakSignal&) {
                    return;
                } catch (const ContinueSignal&) {
                    continue;
                }
            }
        } else if (iterableVal.type == ValueType::STRING) {
            // Iterate over characters
            const std::string& s = iterableVal.asString();
            for (char ch : s) {
                auto loopEnv = std::make_shared<Environment>(environment);
                loopEnv->define(forStmt->variable, Value::string(std::string(1, ch)));
                try {
                    executeBlock(*forStmt->body, loopEnv);
                } catch (const BreakSignal&) {
                    return;
                } catch (const ContinueSignal&) {
                    continue;
                }
            }
        } else if (iterableVal.type == ValueType::MAP) {
            // Iterate over map keys
            auto entries = iterableVal.asMap()->entries;
            for (const auto& [key, val] : entries) {
                auto loopEnv = std::make_shared<Environment>(environment);
                loopEnv->define(forStmt->variable, Value::string(key));
                try {
                    executeBlock(*forStmt->body, loopEnv);
                } catch (const BreakSignal&) {
                    return;
                } catch (const ContinueSignal&) {
                    continue;
                }
            }
        } else {
            throw std::runtime_error(
                "Runtime Error" + locationOf(forStmt->line) +
                ": 'for in' expects an array, string, or map, got " + iterableVal.typeName() + "."
            );
        }
        return;
    }

    if (const auto* functionStmt = dynamic_cast<const FunctionStmt*>(&statement)) {
        environment->define(
            functionStmt->name,
            Value::function(std::make_shared<VionFunction>(*functionStmt, environment))
        );
        return;
    }

    if (const auto* returnStmt = dynamic_cast<const ReturnStmt*>(&statement)) {
        Value value = returnStmt->value ? evaluate(*returnStmt->value) : Value::nil();
        throw ReturnSignal(value);
    }

    if (dynamic_cast<const BreakStmt*>(&statement)) {
        throw BreakSignal{};
    }

    if (dynamic_cast<const ContinueStmt*>(&statement)) {
        throw ContinueSignal{};
    }

    throw std::runtime_error("Runtime Error: unknown statement type.");
}

// ── Expressions ───────────────────────────────────────────────────────────────

Value Interpreter::evaluate(const Expr& expression) {
    if (const auto* numberExpr = dynamic_cast<const NumberExpr*>(&expression)) {
        return Value::number(numberExpr->value);
    }
    if (const auto* stringExpr = dynamic_cast<const StringExpr*>(&expression)) {
        return Value::string(stringExpr->value);
    }
    if (const auto* boolExpr = dynamic_cast<const BooleanExpr*>(&expression)) {
        return Value::boolean(boolExpr->value);
    }
    if (dynamic_cast<const NilExpr*>(&expression)) {
        return Value::nil();
    }
    if (const auto* identExpr = dynamic_cast<const IdentifierExpr*>(&expression)) {
        try {
            return environment->get(identExpr->name);
        } catch (const std::runtime_error&) {
            throw std::runtime_error(
                "Runtime Error" + locationOf(identExpr->line) +
                ": undefined variable '" + identExpr->name + "'."
            );
        }
    }
    if (const auto* assignExpr = dynamic_cast<const AssignmentExpr*>(&expression)) {
        Value value = evaluate(*assignExpr->value);
        try {
            environment->assign(assignExpr->name, value);
        } catch (const std::runtime_error&) {
            throw std::runtime_error(
                "Runtime Error" + locationOf(assignExpr->line) +
                ": undefined variable '" + assignExpr->name + "'."
            );
        }
        return value;
    }
    if (const auto* idxAssign = dynamic_cast<const IndexAssignExpr*>(&expression)) {
        Value obj = evaluate(*idxAssign->object);
        Value idx = evaluate(*idxAssign->index);
        Value val = evaluate(*idxAssign->value);

        if (obj.type == ValueType::MAP) {
            obj.asMap()->entries[idx.toString()] = val;
            return val;
        }
        if (obj.type != ValueType::ARRAY)
            throw std::runtime_error(
                "Runtime Error" + locationOf(idxAssign->line) + ": index assignment requires an array or map.");

        int i = static_cast<int>(idx.asNumber());
        auto arr = obj.asArray();
        if (i < 0) i = static_cast<int>(arr->elements.size()) + i;
        if (i < 0 || i >= static_cast<int>(arr->elements.size()))
            throw std::runtime_error(
                "Runtime Error" + locationOf(idxAssign->line) +
                ": array index " + std::to_string(i) + " out of bounds (size " +
                std::to_string(arr->elements.size()) + ").");

        arr->elements[i] = val;
        return val;
    }
    if (const auto* unaryExpr = dynamic_cast<const UnaryExpr*>(&expression)) {
        return evaluateUnary(*unaryExpr);
    }
    if (const auto* logicalExpr = dynamic_cast<const LogicalExpr*>(&expression)) {
        return evaluateLogical(*logicalExpr);
    }
    if (const auto* binaryExpr = dynamic_cast<const BinaryExpr*>(&expression)) {
        return evaluateBinary(*binaryExpr);
    }
    if (const auto* callExpr = dynamic_cast<const CallExpr*>(&expression)) {
        return evaluateCall(*callExpr);
    }
    if (const auto* indexExpr = dynamic_cast<const IndexExpr*>(&expression)) {
        return evaluateIndex(*indexExpr);
    }
    if (const auto* arrayExpr = dynamic_cast<const ArrayExpr*>(&expression)) {
        auto arr = std::make_shared<VionArray>();
        for (const auto& elem : arrayExpr->elements) {
            arr->elements.push_back(evaluate(*elem));
        }
        return Value::array(arr);
    }
    if (const auto* mapExpr = dynamic_cast<const MapExpr*>(&expression)) {
        auto m = std::make_shared<VionMap>();
        for (const auto& [key, valExpr] : mapExpr->pairs) {
            m->entries[key] = evaluate(*valExpr);
        }
        return Value::map(m);
    }
    if (const auto* lambdaExpr = dynamic_cast<const LambdaExpr*>(&expression)) {
        return Value::function(std::make_shared<VionFunction>(*lambdaExpr->decl, environment));
    }

    throw std::runtime_error("Runtime Error: unknown expression type.");
}

Value Interpreter::evaluateUnary(const UnaryExpr& expression) {
    Value right = evaluate(*expression.right);

    if (expression.op == "-") {
        if (right.type != ValueType::NUMBER)
            throw std::runtime_error(
                "Runtime Error" + locationOf(expression.line) + ": unary '-' expects a number.");
        return Value::number(-right.asNumber());
    }

    if (expression.op == "!") {
        return Value::boolean(!right.isTruthy());
    }

    throw std::runtime_error("Runtime Error: unknown unary operator '" + expression.op + "'.");
}

Value Interpreter::evaluateLogical(const LogicalExpr& expression) {
    Value left = evaluate(*expression.left);

    if (expression.op == "or") {
        if (left.isTruthy()) return left;
        return evaluate(*expression.right);
    }

    if (expression.op == "and") {
        if (!left.isTruthy()) return left;
        return evaluate(*expression.right);
    }

    throw std::runtime_error("Runtime Error: unknown logical operator '" + expression.op + "'.");
}

Value Interpreter::evaluateBinary(const BinaryExpr& expression) {
    Value left  = evaluate(*expression.left);
    Value right = evaluate(*expression.right);
    const std::string& op = expression.op;
    int line = expression.line;

    if (op == "+") {
        if (left.type == ValueType::NUMBER && right.type == ValueType::NUMBER)
            return Value::number(left.asNumber() + right.asNumber());
        if (left.type == ValueType::STRING || right.type == ValueType::STRING)
            return Value::string(left.toString() + right.toString());
        if (left.type == ValueType::ARRAY && right.type == ValueType::ARRAY) {
            auto result = std::make_shared<VionArray>();
            result->elements = left.asArray()->elements;
            for (const auto& e : right.asArray()->elements) result->elements.push_back(e);
            return Value::array(result);
        }
        throw std::runtime_error(
            "Runtime Error" + locationOf(line) + ": '+' requires numbers, strings, or arrays.");
    }

    if (op == "-") return Value::number(left.asNumber() - right.asNumber());
    if (op == "*") return Value::number(left.asNumber() * right.asNumber());

    if (op == "/") {
        double divisor = right.asNumber();
        if (std::abs(divisor) < 1e-12)
            throw std::runtime_error(
                "Runtime Error" + locationOf(line) + ": division by zero.");
        return Value::number(left.asNumber() / divisor);
    }

    if (op == "%") {
        double divisor = right.asNumber();
        if (std::abs(divisor) < 1e-12)
            throw std::runtime_error(
                "Runtime Error" + locationOf(line) + ": modulo by zero.");
        return Value::number(std::fmod(left.asNumber(), divisor));
    }

    if (op == ">")  return Value::boolean(left.asNumber() > right.asNumber());
    if (op == ">=") return Value::boolean(left.asNumber() >= right.asNumber());
    if (op == "<")  return Value::boolean(left.asNumber() < right.asNumber());
    if (op == "<=") return Value::boolean(left.asNumber() <= right.asNumber());
    if (op == "==") return Value::boolean(valuesEqual(left, right));
    if (op == "!=") return Value::boolean(!valuesEqual(left, right));

    throw std::runtime_error("Runtime Error: unknown binary operator '" + op + "'.");
}

Value Interpreter::evaluateCall(const CallExpr& expression) {
    Value callee = evaluate(*expression.callee);

    if (callee.type != ValueType::FUNCTION) {
        throw std::runtime_error(
            "Runtime Error" + locationOf(expression.line) + ": can only call functions.");
    }

    std::vector<Value> arguments;
    for (const auto& arg : expression.arguments) {
        arguments.push_back(evaluate(*arg));
    }

    auto fn = callee.asFunction();
    if (fn->arity() >= 0 && fn->arity() != static_cast<int>(arguments.size())) {
        throw std::runtime_error(argCountError(fn->arity(), arguments.size()));
    }

    if (callDepth >= kMaxCallDepth) {
        throw std::runtime_error(
            "Runtime Error" + locationOf(expression.line) +
            ": maximum call stack depth (" + std::to_string(kMaxCallDepth) + ") exceeded.");
    }
    ++callDepth;
    Value result = fn->call(*this, arguments);
    --callDepth;
    return result;
}

Value Interpreter::evaluateIndex(const IndexExpr& expression) {
    Value obj = evaluate(*expression.object);
    Value idx = evaluate(*expression.index);
    int line = expression.line;

    if (obj.type == ValueType::ARRAY) {
        int i = static_cast<int>(idx.asNumber());
        auto arr = obj.asArray();
        if (i < 0) i = static_cast<int>(arr->elements.size()) + i;
        if (i < 0 || i >= static_cast<int>(arr->elements.size()))
            throw std::runtime_error(
                "Runtime Error" + locationOf(line) +
                ": array index " + std::to_string(i) + " out of bounds (size " +
                std::to_string(arr->elements.size()) + ").");
        return arr->elements[i];
    }

    if (obj.type == ValueType::STRING) {
        int i = static_cast<int>(idx.asNumber());
        const std::string& s = obj.asString();
        if (i < 0) i = static_cast<int>(s.size()) + i;
        if (i < 0 || i >= static_cast<int>(s.size()))
            throw std::runtime_error(
                "Runtime Error" + locationOf(line) +
                ": string index " + std::to_string(i) + " out of bounds.");
        return Value::string(std::string(1, s[i]));
    }

    if (obj.type == ValueType::MAP) {
        const std::string& key = idx.toString();
        auto m = obj.asMap();
        auto it = m->entries.find(key);
        if (it == m->entries.end())
            throw std::runtime_error(
                "Runtime Error" + locationOf(line) +
                ": map key '" + key + "' not found.");
        return it->second;
    }

    throw std::runtime_error(
        "Runtime Error" + locationOf(line) +
        ": index operator requires array, string, or map, got " + obj.typeName() + ".");
}

bool Interpreter::valuesEqual(const Value& left, const Value& right) const {
    if (left.type != right.type) return false;

    switch (left.type) {
        case ValueType::NUMBER:
            return std::abs(left.asNumber() - right.asNumber()) < 1e-12;
        case ValueType::STRING:
            return left.asString() == right.asString();
        case ValueType::BOOLEAN:
            return left.asBoolean() == right.asBoolean();
        case ValueType::FUNCTION:
            return left.asFunction() == right.asFunction();
        case ValueType::ARRAY:
            return left.asArray() == right.asArray(); // reference equality
        case ValueType::NIL:
            return true;
    }

    return false;
}
