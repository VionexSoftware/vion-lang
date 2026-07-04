#pragma once

#include <vector>
#include <memory>
#include <mutex>

class GCObject {
public:
    virtual ~GCObject() = default;
    
    // Return all children that are also GCObjects
    virtual void trace(std::vector<std::shared_ptr<GCObject>>& children) const = 0;
    
    // Clear internal references to break cycles
    virtual void breakCycles() {}
    
    // Internal GC state
    mutable int gc_refs = 0;
    mutable bool gc_visited = false;
};

class GC {
public:
    static GC& instance() {
        static GC gc;
        return gc;
    }

    void registerObject(std::weak_ptr<GCObject> obj);
    void collectCycles();
    
    // Expose memory tracking
    std::size_t getTrackedCount() const { return objects_.size(); }

private:
    GC() = default;
    std::vector<std::weak_ptr<GCObject>> objects_;
    std::mutex mutex_;
};

template<typename T, typename... Args>
std::shared_ptr<T> gc_make_shared(Args&&... args) {
    auto ptr = std::make_shared<T>(std::forward<Args>(args)...);
    GC::instance().registerObject(ptr);
    return ptr;
}
