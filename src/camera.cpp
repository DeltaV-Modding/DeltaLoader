#include "sw/camera.hpp"

#include "sw/hook.hpp"
#include "sw/runtime.hpp"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <string>

namespace sw::camera {

#if defined(_WIN64)

namespace {

std::atomic<std::uintptr_t> g_shelf{0};
std::atomic<long long> g_last_tick{0};
std::atomic<int> g_pending{-1};   // -1 none, else a Mode
bool g_watching = false;

template <typename T>
bool read_at(std::uintptr_t address, T& value) {
    if (!address || !sw::Runtime::get().readable(reinterpret_cast<const void*>(address), sizeof(T))) return false;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return true;
}

long long now_ms() { return static_cast<long long>(GetTickCount64()); }

bool is_shelf(std::uintptr_t shelf) {
    std::uintptr_t vtable = 0;
    return shelf && read_at(shelf, vtable) &&
           vtable == reinterpret_cast<std::uintptr_t>(sw::Runtime::get().at_rva(kVtableCameraToolshelf));
}

// The member object of a toggle (button + 0x100) of the shelf.
std::uintptr_t member(std::uintptr_t shelf, std::size_t off) {
    std::uintptr_t button = 0;
    if (!read_at(shelf + off, button) || !button) return 0;
    return button + kOffToggleMember;
}

bool is_on(std::uintptr_t member_object) {
    std::uintptr_t vtable = 0, fn = 0;
    if (!read_at(member_object, vtable) || !read_at(vtable + kSlotToggleIsOn * 8, fn) || !fn) return false;
    return reinterpret_cast<bool (*)(void*)>(fn)(reinterpret_cast<void*>(member_object));
}

// On the thread of the game, at the tick of the shelf.
void on_tick(void* self, void*) {
    const auto shelf = reinterpret_cast<std::uintptr_t>(self);
    if (!is_shelf(shelf)) return;
    g_shelf.store(shelf, std::memory_order_relaxed);
    g_last_tick.store(now_ms(), std::memory_order_relaxed);
    const int pending = g_pending.exchange(-1);
    if (pending < 0) return;
    const std::uintptr_t target = member(shelf, pending == static_cast<int>(Mode::orbit) ? kOffToolshelfOrbitButton
                                                                                          : kOffToolshelfFreeButton);
    if (!target) return;
    const auto* fn = sw::Runtime::get().at_rva(kRvaToggleGroupSelect);
    if (!fn || !sw::Runtime::get().is_function_start(reinterpret_cast<std::uintptr_t>(fn))) return;
    reinterpret_cast<void (*)(void*, void*, void*)>(const_cast<void*>(fn))(
        reinterpret_cast<void*>(shelf + kOffToolshelfGroup), nullptr, reinterpret_cast<void*>(target));
}

}  // namespace

bool watch() {
    if (g_watching) return true;
    std::string error;
    if (sw::Runtime::get().hook_rva(kRvaCameraToolshelfTick, &on_tick, nullptr, error) != sw::HookStatus::ok) return false;
    g_watching = true;
    return true;
}

bool shelf_ready() { return g_shelf.load() != 0 && now_ms() - g_last_tick.load() < 500; }

bool set_mode(Mode m) {
    if (!g_watching) return false;
    g_pending.store(static_cast<int>(m));
    return true;
}

bool mode(Mode& out) {
    if (!shelf_ready()) return false;
    const std::uintptr_t shelf = g_shelf.load();
    if (!is_shelf(shelf)) return false;
    const std::uintptr_t orbit = member(shelf, kOffToolshelfOrbitButton);
    if (!orbit) return false;
    out = is_on(orbit) ? Mode::orbit : Mode::free;
    return true;
}

#else

bool watch() { return false; }
bool shelf_ready() { return false; }
bool set_mode(Mode) { return false; }
bool mode(Mode&) { return false; }

#endif

}  // namespace sw::camera
