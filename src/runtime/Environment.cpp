#include "runtime/Environment.h"
#include "interpreter/Interpreter.h"

#include <stdexcept>

void VionArray::trace(std::vector<std::shared_ptr<GCObject>>& children) const {
    for (const auto& el : elements) {
        if (el.type == ValueType::ARRAY) children.push_back(std::get<std::shared_ptr<VionArray>>(el.data));
        else if (el.type == ValueType::MAP) children.push_back(std::get<std::shared_ptr<VionMap>>(el.data));
        else if (el.type == ValueType::FUNCTION) children.push_back(std::get<std::shared_ptr<VionCallable>>(el.data));
    }
}

void VionMap::trace(std::vector<std::shared_ptr<GCObject>>& children) const {
    for (const auto& [k, el] : entries) {
        if (el.type == ValueType::ARRAY) children.push_back(std::get<std::shared_ptr<VionArray>>(el.data));
        else if (el.type == ValueType::MAP) children.push_back(std::get<std::shared_ptr<VionMap>>(el.data));
        else if (el.type == ValueType::FUNCTION) children.push_back(std::get<std::shared_ptr<VionCallable>>(el.data));
    }
}

Environment::Environment(std::shared_ptr<Environment> enclosing) : enclosing(std::move(enclosing)) {}

void Environment::define(const std::string& name, const Value& value, bool isConst) {
    values[name] = value;
    if (isConst) constants_.insert(name);
}

void Environment::assign(const std::string& name, const Value& value) {
    auto found = values.find(name);
    if (found != values.end()) {
        if (constants_.count(name))
            throw std::runtime_error("Runtime Error: cannot reassign const '" + name + "'.");
        found->second = value;
        return;
    }

    if (enclosing) {
        enclosing->assign(name, value);
        return;
    }

    throw std::runtime_error("Runtime Error: undefined variable '" + name + "'.");
}

Value Environment::get(const std::string& name) const {
    auto found = values.find(name);
    if (found != values.end()) {
        return found->second;
    }

    if (enclosing) {
        return enclosing->get(name);
    }

    throw std::runtime_error("Runtime Error: undefined variable '" + name + "'.");
}

void Environment::trace(std::vector<std::shared_ptr<GCObject>>& children) const {
    if (enclosing) children.push_back(enclosing);
    for (const auto& [name, val] : values) {
        if (val.type == ValueType::ARRAY) children.push_back(std::get<std::shared_ptr<VionArray>>(val.data));
        else if (val.type == ValueType::MAP) children.push_back(std::get<std::shared_ptr<VionMap>>(val.data));
        else if (val.type == ValueType::FUNCTION) children.push_back(std::get<std::shared_ptr<VionCallable>>(val.data));
    }
}

void Environment::breakCycles() {
    enclosing.reset();
    values.clear();
}
