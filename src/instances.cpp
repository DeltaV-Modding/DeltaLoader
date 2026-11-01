#include "sw/instances.hpp"

#include "sw/runtime.hpp"

#include <algorithm>
#include <memory>
#include <mutex>
#include <unordered_set>

namespace sw::Instances {

namespace {

constexpr std::size_t kMaxPerClass = 100000;

struct Tracked {
    std::string cls;
    std::mutex mutex;
    std::vector<std::uintptr_t> objects;
};

std::mutex g_mutex;
std::vector<std::unique_ptr<Tracked>> g_tracked;

Tracked* find(std::string_view cls) {
    for (auto& t : g_tracked) {
        if (t->cls == cls) return t.get();
    }
    return nullptr;
}

void on_constructed(void* self, void* user) {
    auto* t = static_cast<Tracked*>(user);
    const auto obj = reinterpret_cast<std::uintptr_t>(self);
    std::lock_guard<std::mutex> lock(t->mutex);
    if (t->objects.size() >= kMaxPerClass) {
        t->objects.erase(t->objects.begin(), t->objects.begin() + kMaxPerClass / 2);
    }
    t->objects.push_back(obj);
}

// Set of all vtable addresses of the game (for the alive check).
const std::unordered_set<std::uintptr_t>& known_vtables() {
    static std::unordered_set<std::uintptr_t> set;
    static std::once_flag once;
    std::call_once(once, [] {
        auto& rt = Runtime::get();
        for (const auto& c : map::kClasses) {
            set.insert(reinterpret_cast<std::uintptr_t>(rt.at_rva(c.vtable_rva)));
        }
    });
    return set;
}

bool looks_alive(std::uintptr_t obj) {
    const std::uintptr_t vt = Runtime::get().vtable_of(obj);
    return vt != 0 && known_vtables().count(vt) != 0;
}

}  // namespace

HookStatus track(std::string_view cls, std::string& error) {
    error.clear();
    std::lock_guard<std::mutex> lock(g_mutex);
    if (find(cls)) return HookStatus::ok;
    auto t = std::make_unique<Tracked>();
    t->cls.assign(cls.begin(), cls.end());
    const HookStatus st = Runtime::get().hook_ctor(cls, &on_constructed, t.get(), error);
    if (st != HookStatus::ok) return st;
    g_tracked.push_back(std::move(t));
    return HookStatus::ok;
}

void untrack(std::string_view cls) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto it = g_tracked.begin(); it != g_tracked.end(); ++it) {
        if ((*it)->cls != cls) continue;
        std::string ignored;
        Runtime::get().remove_ctor(cls, &on_constructed, it->get(), ignored);
        // Do not delete: a thread of the engine may still be in the callback.
        // The list is emptied, the entry stays until clear().
        std::lock_guard<std::mutex> inner((*it)->mutex);
        (*it)->objects.clear();
        (*it)->cls.clear();
        return;
    }
}

std::vector<std::uintptr_t> list(std::string_view cls, bool only_alive) {
    Tracked* t = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        t = find(cls);
    }
    if (!t) return {};
    std::vector<std::uintptr_t> copy;
    {
        std::lock_guard<std::mutex> lock(t->mutex);
        copy = t->objects;
    }
    std::sort(copy.begin(), copy.end());
    copy.erase(std::unique(copy.begin(), copy.end()), copy.end());
    if (only_alive) {
        copy.erase(std::remove_if(copy.begin(), copy.end(),
                                  [](std::uintptr_t o) { return !looks_alive(o); }),
                   copy.end());
        // Also remove dead entries from the list.
        std::lock_guard<std::mutex> lock(t->mutex);
        t->objects = copy;
    }
    return copy;
}

std::size_t count(std::string_view cls) { return list(cls, true).size(); }

std::vector<std::string> tracked() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<std::string> out;
    for (const auto& t : g_tracked) {
        if (!t->cls.empty()) out.push_back(t->cls);
    }
    return out;
}

void clear() {
    std::lock_guard<std::mutex> lock(g_mutex);
    // Call only after Hook::remove_all(): then no thread calls
    // on_constructed() with one of these pointers any more.
    g_tracked.clear();
}

}  // namespace sw::Instances
