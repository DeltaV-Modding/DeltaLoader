#include "sw/game_state.hpp"

#include "sw/hook.hpp"
#include "sw/runtime.hpp"

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>

namespace sw::state {

namespace {

std::mutex g_mutex;
std::vector<std::pair<ChangeCallback, void*>> g_change;
std::vector<std::pair<WorldCallback, void*>> g_world;
bool g_watching = false;
bool g_world_hooked = false;
std::atomic<std::uintptr_t> g_manager{0};
std::atomic<std::uint8_t> g_last{0};

#if defined(_WIN64)
template <typename T>
bool read_at(std::uintptr_t address, T& value) {
    if (!address || !sw::Runtime::get().readable(reinterpret_cast<const void*>(address), sizeof(T))) return false;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return true;
}

void dispatch(const Change& change) {
    std::vector<std::pair<ChangeCallback, void*>> copy;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        copy = g_change;
    }
    for (const auto& e : copy) e.first(change, e.second);
}

// Per frame, first argument = the state manager.
void on_update(void* self, void*) {
    const auto manager = reinterpret_cast<std::uintptr_t>(self);
    g_manager.store(manager, std::memory_order_relaxed);
    std::uintptr_t object = 0;
    if (!read_at(manager + kOffCurrentState, object)) return;
    const Id now = identify(reinterpret_cast<const void*>(object));
    const auto last = static_cast<Id>(g_last.exchange(static_cast<std::uint8_t>(now), std::memory_order_relaxed));
    if (now != last && now != Id::none) dispatch({ChangeKind::became_current, last, now, self});
}

// The switch function: (manager, engine, id). The id is the third argument (r8d), read only.
void on_factory(void* self, void*) {
    g_manager.store(reinterpret_cast<std::uintptr_t>(self), std::memory_order_relaxed);
    const auto id = static_cast<std::uint32_t>(sw::Hook::arg(2));
    Id to = Id::none;
    if (id >= 1 && id <= 6) to = static_cast<Id>(id);
    const Id from = current();
    dispatch({ChangeKind::requested, from, to, self});
}

void world_callbacks(WorldEvent event, void* object) {
    std::vector<std::pair<WorldCallback, void*>> copy;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        copy = g_world;
    }
    for (const auto& e : copy) e.first(event, object, e.second);
}
void on_load_world(void* self, void*) { world_callbacks(WorldEvent::load, self); }
void on_game_unload(void* self, void*) { world_callbacks(WorldEvent::unload, self); }
#endif

}  // namespace

const StateInfo* find(Id id) {
    for (std::size_t i = 0; i < kStateCount; ++i)
        if (kStates[i].id == static_cast<std::uint8_t>(id)) return &kStates[i];
    return nullptr;
}

const StateInfo* find(const char* name) {
    if (!name) return nullptr;
    for (std::size_t i = 0; i < kStateCount; ++i)
        if (std::strcmp(kStates[i].name, name) == 0) return &kStates[i];
    return nullptr;
}

const StateInfo* find_class(const char* class_name) {
    if (!class_name) return nullptr;
    for (std::size_t i = 0; i < kStateCount; ++i)
        if (std::strcmp(kStates[i].class_name, class_name) == 0) return &kStates[i];
    return nullptr;
}

const char* to_string(Id id) {
    const StateInfo* info = find(id);
    return info ? info->name : "none";
}

const char* to_string(Phase phase) {
    switch (phase) {
        case Phase::unloading: return "unloading";
        case Phase::waiting: return "waiting";
        case Phase::loading: return "loading";
        case Phase::finishing: return "finishing";
        case Phase::running: return "running";
        case Phase::unknown: break;
    }
    return "unknown";
}

std::vector<const char*> names() {
    std::vector<const char*> out;
    for (std::size_t i = 0; i < kStateCount; ++i) out.push_back(kStates[i].name);
    return out;
}

std::size_t count() { return kStateCount; }

#if defined(_WIN64)

Id identify(const void* state_object) {
    auto& rt = sw::Runtime::get();
    std::uintptr_t vtable = 0;
    if (!state_object || !rt.attached() || !read_at(reinterpret_cast<std::uintptr_t>(state_object), vtable)) return Id::none;
    const auto base = rt.base();
    if (vtable < base) return Id::none;
    const std::uint64_t rva = vtable - base;
    for (std::size_t i = 0; i < kStateCount; ++i)
        if (kStates[i].vtable == rva) return static_cast<Id>(kStates[i].id);
    return Id::none;
}

bool watch() {
    auto& rt = sw::Runtime::get();
    if (!rt.attached()) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_watching) return true;
    std::string error;
    if (rt.hook_rva(kUpdateRva, &on_update, nullptr, error) != sw::HookStatus::ok) return false;
    if (rt.hook_rva(kFactoryRva, &on_factory, nullptr, error) != sw::HookStatus::ok) {
        rt.unhook(reinterpret_cast<std::uintptr_t>(rt.at_rva(kUpdateRva)), &on_update, nullptr, error);
        return false;
    }
    g_watching = true;
    return true;
}

void* manager() { return reinterpret_cast<void*>(g_manager.load(std::memory_order_relaxed)); }

void* current_object() {
    std::uintptr_t object = 0;
    const auto m = g_manager.load(std::memory_order_relaxed);
    if (!m || !read_at(m + kOffCurrentState, object)) return nullptr;
    return reinterpret_cast<void*>(object);
}

Id current() { return identify(current_object()); }

Id requested() {
    std::uint32_t id = 0;
    const auto m = g_manager.load(std::memory_order_relaxed);
    if (!m || !read_at(m + kOffActiveId, id) || id < 1 || id > 6) return Id::none;
    return static_cast<Id>(id);
}

Phase phase() {
    std::uint32_t p = 0;
    const auto m = g_manager.load(std::memory_order_relaxed);
    if (!m || !read_at(m + kOffPhase, p) || p > 4) return Phase::unknown;
    return static_cast<Phase>(p);
}

bool on_change(ChangeCallback callback, void* user) {
    if (!callback) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_watching) return false;
    for (const auto& e : g_change)
        if (e.first == callback && e.second == user) return false;
    g_change.emplace_back(callback, user);
    return true;
}

bool on_world(WorldCallback callback, void* user) {
    auto& rt = sw::Runtime::get();
    if (!callback || !rt.attached()) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_world_hooked) {
        std::string error;
        if (rt.hook_rva(kLoadWorldRva, &on_load_world, nullptr, error) != sw::HookStatus::ok) return false;
        if (rt.hook_rva(kGameUnloadRva, &on_game_unload, nullptr, error) != sw::HookStatus::ok) {
            rt.unhook(reinterpret_cast<std::uintptr_t>(rt.at_rva(kLoadWorldRva)), &on_load_world, nullptr, error);
            return false;
        }
        g_world_hooked = true;
    }
    for (const auto& e : g_world)
        if (e.first == callback && e.second == user) return false;
    g_world.emplace_back(callback, user);
    return true;
}

#else

Id identify(const void*) { return Id::none; }
bool watch() { return false; }
void* manager() { return nullptr; }
void* current_object() { return nullptr; }
Id current() { return Id::none; }
Id requested() { return Id::none; }
Phase phase() { return Phase::unknown; }
bool on_change(ChangeCallback, void*) { return false; }
bool on_world(WorldCallback, void*) { return false; }

#endif

bool running() { return phase() == Phase::running; }

void unwatch() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_change.clear();
    g_world.clear();
}

}  // namespace sw::state
