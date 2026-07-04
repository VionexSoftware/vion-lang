#include "runtime/GC.h"
#include "runtime/Value.h"
#include "vm/VM.h"

void VionArray::trace(std::vector<std::shared_ptr<GCObject>>& children) const {
    for (const auto& el : elements) {
        if (el.type == ValueType::ARRAY) children.push_back(std::static_pointer_cast<GCObject>(std::get<std::shared_ptr<VionArray>>(el.data)));
        else if (el.type == ValueType::MAP) children.push_back(std::static_pointer_cast<GCObject>(std::get<std::shared_ptr<VionMap>>(el.data)));
        else if (el.type == ValueType::BYTECODE_FUNCTION) children.push_back(std::static_pointer_cast<GCObject>(std::get<std::shared_ptr<BytecodeFunction>>(el.data)));
        else if (el.type == ValueType::NATIVE_FUNCTION) children.push_back(std::static_pointer_cast<GCObject>(std::get<std::shared_ptr<VMNativeFunction>>(el.data)));
    }
}

void VionMap::trace(std::vector<std::shared_ptr<GCObject>>& children) const {
    for (const auto& pair : entries) {
        const auto& el = pair.second;
        if (el.type == ValueType::ARRAY) children.push_back(std::static_pointer_cast<GCObject>(std::get<std::shared_ptr<VionArray>>(el.data)));
        else if (el.type == ValueType::MAP) children.push_back(std::static_pointer_cast<GCObject>(std::get<std::shared_ptr<VionMap>>(el.data)));
        else if (el.type == ValueType::BYTECODE_FUNCTION) children.push_back(std::static_pointer_cast<GCObject>(std::get<std::shared_ptr<BytecodeFunction>>(el.data)));
        else if (el.type == ValueType::NATIVE_FUNCTION) children.push_back(std::static_pointer_cast<GCObject>(std::get<std::shared_ptr<VMNativeFunction>>(el.data)));
    }
}

void GC::registerObject(std::weak_ptr<GCObject> obj) {
    std::lock_guard<std::mutex> lock(mutex_);
    objects_.push_back(std::move(obj));
}

void GC::collectCycles() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::shared_ptr<GCObject>> live;
    live.reserve(objects_.size());
    
    // 1. Gather live objects and clean up registry
    for (auto it = objects_.begin(); it != objects_.end(); ) {
        if (auto p = it->lock()) {
            live.push_back(p);
            ++it;
        } else {
            it = objects_.erase(it);
        }
    }

    // 2. Initialize gc_refs (use_count - 1 because 'live' holds 1 ref)
    for (auto& obj : live) {
        obj->gc_refs = static_cast<int>(obj.use_count()) - 1;
        obj->gc_visited = false;
    }

    // 3. Decrement gc_refs for all children (simulating removing internal references)
    std::vector<std::shared_ptr<GCObject>> children;
    for (auto& obj : live) {
        children.clear();
        obj->trace(children);
        for (auto& child : children) {
            if (child) child->gc_refs--;
        }
    }

    // 4. Roots are objects with gc_refs > 0 (externally referenced)
    std::vector<std::shared_ptr<GCObject>> roots;
    for (auto& obj : live) {
        if (obj->gc_refs > 0) {
            roots.push_back(obj);
        }
    }

    // 5. Mark all reachable objects from roots
    std::vector<std::shared_ptr<GCObject>> stack = roots;
    while (!stack.empty()) {
        auto obj = stack.back();
        stack.pop_back();

        if (obj->gc_visited) continue;
        obj->gc_visited = true;

        children.clear();
        obj->trace(children);
        for (auto& child : children) {
            if (child && !child->gc_visited) {
                stack.push_back(child);
            }
        }
    }

    // 6. Break cycles for unreachable objects
    for (auto& obj : live) {
        if (!obj->gc_visited) {
            obj->breakCycles();
        }
    }
}
