#include "sw/world.hpp"

#include "sw/hook.hpp"
#include "sw/protos.hpp"
#include "sw/runtime.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

namespace sw::world {

#if defined(_WIN64)

namespace {

std::atomic<std::uintptr_t> g_system{0};   // the tile system of the last placement
bool g_watching = false;

template <typename T>
bool read_at(std::uintptr_t address, T& value) {
    if (!address || !sw::Runtime::get().readable(reinterpret_cast<const void*>(address), sizeof(T)))
        return false;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return true;
}

// Entry into the placement: argument 1 is the tile system.
void on_placement(void*, void*) {
    g_system.store(static_cast<std::uintptr_t>(sw::Hook::arg(0)), std::memory_order_relaxed);
}

// The first level of the tile system (ring buffer of levels).
std::uintptr_t first_level() {
    const std::uintptr_t system = g_system.load(std::memory_order_relaxed);
    std::uintptr_t levels = 0;
    std::uint32_t cap = 0, start = 0;
    if (!system || !read_at(system + sw::protos::kOffLevels, levels) ||
        !read_at(system + sw::protos::kOffLevelsCap, cap) ||
        !read_at(system + sw::protos::kOffLevelsStart, start) || !levels || cap == 0)
        return 0;
    return levels + sw::protos::kLevelSize * (start % cap);
}

// The island list of a level: the ring buffer at level + 0x88.
struct IslandList {
    std::uintptr_t data = 0;
    std::uint32_t cap = 0;
    std::uint32_t start = 0;
    std::uint32_t count = 0;
};

IslandList island_list(std::uintptr_t level) {
    IslandList list;
    if (!level) return list;
    const std::uintptr_t base = level + sw::protos::kOffIslandList;
    if (!read_at(base, list.data) || !read_at(base + sw::protos::kRingCap, list.cap) ||
        !read_at(base + sw::protos::kRingStart, list.start) ||
        !read_at(base + sw::protos::kRingCount, list.count))
        return IslandList{};
    if (!list.data || list.cap == 0 || list.count > list.cap) return IslandList{};
    return list;
}

std::string cell_name(std::uintptr_t cell) {
    sw::protos::MmString str{};
    if (!read_at(cell + sw::protos::kOffCellName, str)) return {};
    if (!str.data || str.size == 0 || str.size > 4096) return {};
    if (!sw::Runtime::get().readable(str.data, str.size)) return {};
    return std::string(str.data, str.size);
}

}  // namespace

bool watch() {
    if (g_watching) return true;
    auto& rt = sw::Runtime::get();
    if (!sw::protos::bind(rt)) return false;
    std::string error;
    if (rt.hook_rva(sw::protos::Tiles_place_fixed::kRva, &on_placement, nullptr, error) !=
        sw::HookStatus::ok) {
        return false;
    }
    g_watching = true;
    return true;
}

bool ready() { return first_level() != 0; }

Level level() {
    Level out;
    const std::uintptr_t lv = first_level();
    if (!lv) return out;
    std::int32_t x = 0, y = 0;
    if (!read_at(lv + sw::protos::kOffOriginX, x) || !read_at(lv + sw::protos::kOffOriginY, y))
        return out;
    out.origin_x = x;
    out.origin_y = y;
    const IslandList list = island_list(lv);
    out.island_count = static_cast<int>(list.count);
    out.island_capacity = static_cast<int>(list.cap);
    return out;
}

std::vector<Island> islands() {
    std::vector<Island> out;
    const IslandList list = island_list(first_level());
    if (!list.data) return out;
    for (std::uint32_t i = 0; i < list.count; ++i) {
        const std::uintptr_t e = list.data + sw::protos::kIslandEntry * ((list.start + i) % list.cap);
        std::int32_t gx = 0, gy = 0;
        if (read_at(e, gx) && read_at(e + 4, gy)) out.push_back(Island{gx, gy});
    }
    return out;
}

std::size_t tile_count() {
    const std::uintptr_t lv = first_level();
    if (!lv || !sw::protos::Area) return 0;
    // The query of the game covers the whole grid of the level.
    const int min[2] = {-sw::protos::kAreaAxis, -sw::protos::kAreaAxis};
    const int max[2] = {sw::protos::kAreaAxis - 1, sw::protos::kAreaAxis - 1};
    sw::protos::AreaResult result{};
    sw::protos::Area(lv, &result, min, max);
    const std::size_t count = result.count;
    // The buffer belongs to the engine and has to be given back.
    if (result.data && sw::protos::Free) sw::protos::Free(result.data);
    return count;
}

bool is_island(int gx, int gy) {
    const IslandList list = island_list(first_level());
    if (!list.data) return false;
    for (std::uint32_t i = 0; i < list.count; ++i) {
        const std::uintptr_t e = list.data + sw::protos::kIslandEntry * ((list.start + i) % list.cap);
        std::int32_t ex = 0, ey = 0;
        if (read_at(e, ex) && read_at(e + 4, ey) && ex == gx && ey == gy) return true;
    }
    return false;
}

bool relative(int gx, int gy, int& rx, int& ry) {
    rx = 0;
    ry = 0;
    if (!ready()) return false;
    const Level lv = level();
    rx = gx - lv.origin_x;
    ry = gy - lv.origin_y;
    if (rx < -sw::protos::kAreaAxis || rx >= sw::protos::kAreaAxis) return false;
    if (ry < -sw::protos::kAreaAxis || ry >= sw::protos::kAreaAxis) return false;
    return true;
}

bool cell_of(double x, double z, int& gx, int& gy) {
    gx = 0;
    gy = 0;
    if (!ready()) return false;
    gx = static_cast<int>(x / 1000.0);
    gy = static_cast<int>(z / 1000.0);
    return true;
}

double world_x(int gx) { return gx * 1000.0; }
double world_y(int gy) { return gy * 1000.0; }

float sea_floor(int gx, int gy) { return ocean_depth(world_x(gx), world_y(gy)); }

float ocean_depth(double x, double z) {
    const std::uintptr_t lv = first_level();
    if (!lv || !sw::protos::OceanDepth) return 0.0f;
    return sw::protos::OceanDepth(x, z, lv + sw::protos::kOffIslandList);
}

#else  // !_WIN64

bool watch() { return false; }
bool ready() { return false; }
Level level() { return Level{}; }
std::vector<Island> islands() { return {}; }
std::size_t tile_count() { return 0; }
bool is_island(int, int) { return false; }
bool relative(int, int, int& rx, int& ry) { rx = 0; ry = 0; return false; }
bool cell_of(double, double, int& gx, int& gy) { gx = 0; gy = 0; return false; }
// The conversion is pure mathematics - it does not need the game.
double world_x(int gx) { return gx * 1000.0; }
double world_y(int gy) { return gy * 1000.0; }
float sea_floor(int, int) { return 0.0f; }
float ocean_depth(double, double) { return 0.0f; }

#endif

}  // namespace sw::world
