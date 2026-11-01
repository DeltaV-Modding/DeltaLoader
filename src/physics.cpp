#include "sw/physics.hpp"

#include "sw/hook.hpp"
#include "sw/runtime.hpp"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <string>

namespace sw::physics {

#if defined(_WIN64)

namespace {

std::atomic<std::uintptr_t> g_world{0};
std::atomic<std::uint64_t> g_steps{0};
std::atomic<long long> g_last_step{0};
std::atomic<bool> g_want_gravity{false};
std::atomic<double> g_gx{0}, g_gy{0}, g_gz{0};
std::atomic<int> g_want_iterations{0};   // 0 = none
bool g_watching = false;

template <typename T>
bool read_at(std::uintptr_t address, T& value) {
    if (!address || !sw::Runtime::get().readable(reinterpret_cast<const void*>(address), sizeof(T))) return false;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return true;
}

long long now_ms() { return static_cast<long long>(GetTickCount64()); }

bool finite_small(double v) { return std::isfinite(v) && std::fabs(v) <= 1000.0; }

// On the physics thread, before the step runs.
void on_step(void* self, void*) {
    const auto world = reinterpret_cast<std::uintptr_t>(self);
    if (!is_world(world)) return;
    g_world.store(world, std::memory_order_relaxed);
    g_steps.fetch_add(1, std::memory_order_relaxed);
    g_last_step.store(now_ms(), std::memory_order_relaxed);

    if (g_want_gravity.load()) {
        Vec3 want{g_gx.load(), g_gy.load(), g_gz.load()};
        Vec3 have;
        if (read_at(world + kOffGravity, have) && (have.x != want.x || have.y != want.y || have.z != want.z)) {
            std::uintptr_t vtable = 0, fn = 0;
            if (read_at(world, vtable) && read_at(vtable + kSlotSetGravity * 8, fn) &&
                fn == reinterpret_cast<std::uintptr_t>(sw::Runtime::get().at_rva(kRvaSetGravity))) {
                alignas(16) double v[4] = {want.x, want.y, want.z, 0.0};
                reinterpret_cast<void (*)(void*, const double*)>(fn)(self, v);
            }
        }
    }
    const int it = g_want_iterations.load();
    if (it > 0) {
        int have = 0;
        if (read_at(world + kOffSolverIterations, have) && have != it)
            std::memcpy(reinterpret_cast<void*>(world + kOffSolverIterations), &it, sizeof(it));
    }
}

bool step_site_ok() {
    auto& rt = sw::Runtime::get();
    const auto* fn = static_cast<const std::uint8_t*>(rt.at_rva(kRvaPhysicsSceneStep));
    if (!fn || !rt.readable(fn, 0x40)) return false;
    auto i32 = [&](std::size_t at) {
        std::int32_t v;
        std::memcpy(&v, fn + at, 4);
        return v;
    };
    // movsd xmm3, [rip+d] at +0xf ; movsd xmm1, [rip+d] at +0x1a ; mov r8d, imm32 at +0x22
    const bool ops = fn[0xf] == 0xf2 && fn[0x10] == 0x0f && fn[0x11] == 0x10 && fn[0x12] == 0x1d &&
                     fn[0x1a] == 0xf2 && fn[0x1b] == 0x0f && fn[0x1c] == 0x10 && fn[0x1d] == 0x0d &&
                     fn[0x22] == 0x41 && fn[0x23] == 0xb8 && fn[0x35] == 0xff && fn[0x36] == 0x50 && fn[0x37] == 0x60;
    return ops && kRvaPhysicsSceneStep + 0x17 + i32(0x13) == kRvaConstFixedStep &&
           kRvaPhysicsSceneStep + 0x22 + i32(0x1e) == kRvaConstStepTime;
}

bool patch_double(std::uint32_t rva, double v) {
    return step_site_ok() && sw::Runtime::get().write_rva(rva, v);
}

bool patch_int(std::uint32_t rva, std::int32_t v) {
    return step_site_ok() && sw::Runtime::get().write_rva(rva, v);
}

}  // namespace

bool set_fixed_time_step(double s) {
    return std::isfinite(s) && s >= 1.0 / 2000 && s <= 1.0 / 30 && patch_double(kRvaConstFixedStep, s);
}

bool set_step_time(double s) {
    return std::isfinite(s) && s >= 1.0 / 480 && s <= 1.0 / 10 && patch_double(kRvaConstStepTime, s);
}

bool set_max_sub_steps(int n) { return n >= 1 && n <= 100000 && patch_int(kRvaImmMaxSubSteps, n); }

bool step_time(double& out) { return step_site_ok() && sw::Runtime::get().read_rva(kRvaConstStepTime, out); }

bool max_sub_steps(int& out) {
    std::int32_t v = 0;
    if (!step_site_ok() || !sw::Runtime::get().read_rva(kRvaImmMaxSubSteps, v)) return false;
    out = v;
    return true;
}

bool restore_time_steps() {
    return patch_double(kRvaConstFixedStep, kGameFixedStep) && patch_double(kRvaConstStepTime, kGameStepTime) &&
           patch_int(kRvaImmMaxSubSteps, kGameMaxSubSteps);
}

bool is_world(std::uintptr_t address) {
    std::uintptr_t vtable = 0;
    if (!address || !read_at(address, vtable)) return false;
    auto& rt = sw::Runtime::get();
    return vtable == reinterpret_cast<std::uintptr_t>(rt.at_rva(kVtableDiscreteWorld)) ||
           vtable == reinterpret_cast<std::uintptr_t>(rt.at_rva(kVtableSoftRigidWorld));
}

bool watch() {
    if (g_watching) return true;
    std::string error;
    if (sw::Runtime::get().hook_rva(kRvaStepSimulation, &on_step, nullptr, error) != sw::HookStatus::ok) return false;
    g_watching = true;
    return true;
}

bool ready() { return g_world.load() != 0 && now_ms() - g_last_step.load() < 1000; }

bool gravity(Vec3& out) {
    if (!ready()) return false;
    return read_at(g_world.load() + kOffGravity, out);
}

bool set_gravity(const Vec3& v) {
    if (!g_watching || !finite_small(v.x) || !finite_small(v.y) || !finite_small(v.z)) return false;
    g_gx.store(v.x);
    g_gy.store(v.y);
    g_gz.store(v.z);
    g_want_gravity.store(true);
    return true;
}

bool solver_iterations(int& out) {
    if (!ready()) return false;
    return read_at(g_world.load() + kOffSolverIterations, out);
}

bool set_solver_iterations(int iterations) {
    if (!g_watching || iterations < 1 || iterations > 256) return false;
    g_want_iterations.store(iterations);
    return true;
}

bool fixed_time_step(double& out) {
    if (!ready()) return false;
    return read_at(g_world.load() + kOffFixedTimeStep, out);
}

bool local_time(double& out) {
    if (!ready()) return false;
    return read_at(g_world.load() + kOffLocalTime, out);
}

std::uint64_t steps() { return g_steps.load(); }

void clear_overrides() {
    g_want_gravity.store(false);
    g_want_iterations.store(0);
}

#else

bool is_world(std::uintptr_t) { return false; }
bool watch() { return false; }
bool ready() { return false; }
bool gravity(Vec3&) { return false; }
bool set_gravity(const Vec3&) { return false; }
bool solver_iterations(int&) { return false; }
bool set_solver_iterations(int) { return false; }
bool fixed_time_step(double&) { return false; }
bool local_time(double&) { return false; }
bool set_fixed_time_step(double) { return false; }
bool set_step_time(double) { return false; }
bool set_max_sub_steps(int) { return false; }
bool step_time(double&) { return false; }
bool max_sub_steps(int&) { return false; }
bool restore_time_steps() { return false; }
std::uint64_t steps() { return 0; }
void clear_overrides() {}

#endif

}  // namespace sw::physics
