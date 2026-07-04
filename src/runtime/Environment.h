#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "runtime/Value.h"

#include "runtime/GC.h"

class Environment : public GCObject {
public:
    Environment() = default;
    explicit Environment(std::shared_ptr<Environment> enclosing);

    void define(const std::string& name, const Value& value, bool isConst = false);
    void assign(const std::string& name, const Value& value);
    Value get(const std::string& name) const;
    
    void trace(std::vector<std::shared_ptr<GCObject>>& children) const override;
    void breakCycles() override;

private:
    std::unordered_map<std::string, Value> values;
    std::unordered_set<std::string> constants_;
    std::shared_ptr<Environment> enclosing = nullptr;
};
