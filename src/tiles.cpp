#include "sw/tiles.hpp"

#include "sw/hook.hpp"
#include "sw/protos.hpp"
#include "sw/runtime.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace sw::tiles {

const std::vector<Section>& sections() {
    constexpr std::string_view kBase = "sets the position";
    constexpr std::string_view kBody = "sets the position; if the matrix of the body differs from the matrix of the object, queues a move of the body for the physics";
    static const std::vector<Section> table = {
        {"meshes", "mesh", 0x220, 0x10, "c_scene_object_static_mesh", kBase},
        {"physics_objects", "physics_object", 0, 0x28, "c_scene_object_physics_static", kBody},
        {"physics_meshes", "physics_mesh", 0, 0x40, "c_scene_object_physics_mesh_static", "sets the position; queues a move for every body of the mesh whose matrix differs"},
        {"physics_dynamics", "physics_object", 0x218, 0x58, "c_scene_object_physics_dynamic", kBody},
        {"lights_omni", "light", 0x230, 0x70, "c_scene_object_light_omni", kBase},
        {"lights_spot", "light", 0x510, 0x88, "c_scene_object_light_spot", kBase},
        {"lights_tube", "light", 0x218, 0xa0, "c_scene_object_light_tube", kBase},
        {"screens", "screen", 0x248, 0xb8, "c_scene_object_screen", kBase},
        {"edit_areas", "edit_area", 0x278, 0xd0, "c_scene_object_edit_grid", "sets the position and the size of the box from the grid type (1..7)"},
        {"interactables", "interactable", 0x280, 0xe8, "c_scene_object_interactable", "sets the position and computes the corners of its box"},
        {"door_logic_nodes", "logic", 0x2c0, 0x108, "c_scene_object_logic_two_pos", kBase},
        {"light_logic_nodes", "logic", 0x230, 0x120, "c_scene_object_logic_light", kBase},
        {"rotation_logic_nodes", "logic", 0x260, 0x138, "c_scene_object_logic_rotation", kBase},
        {"hangar_door_logic_nodes", "logic", 0x2d8, 0x150, "c_scene_object_logic_multi_switch", kBase},
        {"audio_logic_nodes", "logic", 0x388, 0x168, "c_scene_object_logic_audio", kBase},
        {"tile_markers", "markers", 0x230, 0x180, "c_scene_object_tile_marker", kBase},
        {"mission_zones", "zone", 0x318, 0x198, "c_scene_object_tile_zone", "sets the position, computes the box and the matrix of the zone"},
        {"dynamic_markers", "dynamic_marker", 0x208, 0x1e0, "c_scene_object_dynamic_marker", kBase},
        {"ladders", "ladder", 0x218, 0x1b0, "c_scene_object_ladder", "sets the position and queues a move of its body to the middle of the ladder"},
        {"perches", "perch", 0x208, 0x1c8, "c_scene_object_perch", kBase},
        {"fires", "fire", 0x210, 0x1f8, "c_scene_object_fire", kBase},
        {"train_tracks", "track", 0x220, 0x210, "c_scene_object_train_track", kBase},
        {"volcano_markers", "volcano", 0x240, 0x228, "c_scene_object_volcano_marker", kBase},
        {"mineral_rocks", "rock", 0x208, 0x240, "c_scene_object_mineral_rock", kBase},
    };
    return table;
}

const std::vector<Attribute>& attributes() {
    static const std::vector<Attribute> table = {
        {"is_island", "bool", 0x278, 0xbc},
        {"is_volcanic", "bool", 0x279, 0xbd},
        {"sea_floor_height", "float", 0x27c, 0xc4},
        {"tile_type", "int", 0x280, 0xc8},
        {"is_purchasable", "bool", 0x284, 0xcc},
        {"purchase_cost", "int", 0x288, 0xd0},
    };
    return table;
}

const char* state_name(int state) {
    switch (state) {
        case 0: return "idle";
        case 1: return "content loaded";
        case 2: return "loading (job running)";
        case 3: return "unloading";
        case 4: return "loading (job running, second path)";
        case 5: return "content ready, waiting";
        case 6: return "unload requested";
        default: return "unknown";
    }
}

#if defined(_WIN64)

namespace {

constexpr std::uint32_t kRvaCellFind = 0x578f80;
using CellFindFn = std::uintptr_t (*)(std::uintptr_t level, std::uint64_t packed);

constexpr std::size_t kOffCellGx = 0x0c;
constexpr std::size_t kOffCellGy = 0x10;
constexpr std::size_t kOffCellVolcanic = 0xbd;
constexpr std::size_t kOffCellFloor = 0xc4;
constexpr std::size_t kOffCellPurchasable = 0xcc;
constexpr std::size_t kOffCellCost = 0xd0;
constexpr std::size_t kOffCellType = 0xc8;
constexpr std::size_t kOffDefType = 0x280;
constexpr std::size_t kOffDefVolcanic = 0x279;
constexpr std::size_t kOffDefFloor = 0x27c;
constexpr std::size_t kOffDefPurchasable = 0x284;
constexpr std::size_t kOffDefCost = 0x288;

struct Pending {
    std::string file;
    int gx;
    int gy;
    bool random = false;   // the cell is drawn from the seed of the world
    int min_distance = 0;
    int max_distance = 0;
};

constexpr std::size_t kOffStartSeed = 0x228;

std::uint32_t name_hash(const std::string& s) {   // FNV-1a
    std::uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

std::mutex g_mutex;
std::vector<Pending> g_pending;
std::vector<std::string> g_report;
std::uint64_t g_a[14] = {};          // A1..A13 of the last placement
std::atomic<bool> g_captured{false};
std::atomic<bool> g_captured_once{false};   // a placement was seen: g_a is valid
std::atomic<std::uintptr_t> g_system{0};
bool g_watching = false;

template <typename T>
bool read_at(std::uintptr_t address, T& value) {
    if (!address || !sw::Runtime::get().readable(reinterpret_cast<const void*>(address), sizeof(T)))
        return false;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return true;
}

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

bool build_name(sw::protos::MmString& s, const std::string& text) {
    s = sw::protos::MmString{nullptr, 0, 0};
    const auto n = static_cast<std::uint32_t>(text.size());
    sw::protos::StringReserve(&s, n + 1);
    if (!s.data) return false;
    std::memcpy(s.data, text.c_str(), n + 1);
    s.size = n;
    return true;
}

void release_name(sw::protos::MmString& s) {
    if (s.data) sw::protos::Free(s.data);
    s = sw::protos::MmString{nullptr, 0, 0};
}

std::string full_name(std::string_view file) {
    constexpr std::string_view kPrefix = "data/tiles/";
    std::string name(file);
    if (name.compare(0, kPrefix.size(), kPrefix) != 0) name = std::string(kPrefix) + name;
    return name;
}

std::uintptr_t find_definition(std::string_view file) {
    const std::uintptr_t system = g_system.load(std::memory_order_relaxed);
    if (!system || !sw::protos::DefFind) return 0;
    sw::protos::MmString name{};
    if (!build_name(name, full_name(file))) return 0;
    const std::uintptr_t def = sw::protos::DefFind(system + sw::protos::kOffDefList, &name);
    release_name(name);
    return def;
}

// The cell and its eight neighbours are free.
bool is_free(std::uintptr_t level, int rx, int ry) {
    const int min[2] = {rx - 1, ry - 1};
    const int max[2] = {rx + 1, ry + 1};
    sw::protos::AreaResult result{};
    sw::protos::Area(level, &result, min, max);
    const bool empty = result.count == 0;
    if (result.capacity != 0 && result.data) sw::protos::Free(result.data);
    return empty;
}

void dispatch(const EventInfo& info);

void on_placement(void*, void*) {
    for (unsigned i = 0; i < 13; ++i) g_a[i + 1] = sw::Hook::arg(i);
    g_system.store(static_cast<std::uintptr_t>(g_a[1]), std::memory_order_relaxed);
    g_captured.store(true, std::memory_order_relaxed);
    g_captured_once.store(true, std::memory_order_relaxed);
}

// Places what mods queued (place, place_random).
void place_pending(bool first) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (first) g_report.clear();
    const std::uintptr_t level = first_level();
    const std::uintptr_t system = g_system.load();
    if (!level || g_pending.empty()) return;

    std::int32_t ux = 0, uy = 0;
    if (!read_at(level + sw::protos::kOffOriginX, ux) || !read_at(level + sw::protos::kOffOriginY, uy)) {
        g_report.push_back("the level is not readable - nothing placed");
        return;
    }
    unsigned placed = 0;
    for (const Pending& p : g_pending) {
        int gx = p.gx;
        int gy = p.gy;
        if (p.random) {
            std::int32_t seed = 0;
            read_at(static_cast<std::uintptr_t>(g_a[4]) + kOffStartSeed, seed);
            std::uint32_t s = static_cast<std::uint32_t>(seed) ^ name_hash(p.file);
            const int lo = p.min_distance < 0 ? 0 : p.min_distance;
            const int hi = p.max_distance < lo ? lo : p.max_distance;
            auto draw = [&s]() {
                s = s * 0x343fdu + 0x269ec3u;
                return (s >> 16) & 0x7fffu;
            };
            bool found = false;
            for (int attempt = 0; attempt < 1000 && !found; ++attempt) {
                const int ax = static_cast<int>(draw() % static_cast<unsigned>(hi + 1));
                const int from = ax < lo ? lo : 0;
                const int ay = from + static_cast<int>(draw() % static_cast<unsigned>(hi - from + 1));
                const int cx = p.gx + ((draw() & 1) ? ax : -ax);
                const int cy = p.gy + ((draw() & 1) ? ay : -ay);
                const int crx = cx - ux;
                const int cry = cy - uy;
                const int lim = sw::protos::kAreaAxis;
                if (crx - 1 < -lim || cry - 1 < -lim || crx + 1 >= lim || cry + 1 >= lim) continue;
                if (!is_free(level, crx, cry)) continue;
                gx = cx;
                gy = cy;
                found = true;
            }
            if (!found) {
                g_report.push_back(p.file + ": no free cell found for the seed " + std::to_string(seed));
                continue;
            }
        }
        const int rx = gx - ux;
        const int ry = gy - uy;
        const int limit = sw::protos::kAreaAxis;
        if (rx - 1 < -limit || ry - 1 < -limit || rx + 1 >= limit || ry + 1 >= limit) {
            g_report.push_back(p.file + ": outside the level");
            continue;
        }
        const std::uintptr_t def = find_definition(p.file);
        if (!def) {
            g_report.push_back(p.file + ": the game has no definition of this file");
            continue;
        }
        if (!is_free(level, rx, ry)) {
            g_report.push_back(p.file + ": the cell or a neighbour is taken");
            continue;
        }
        sw::protos::MmString name{};
        if (!build_name(name, full_name(p.file))) {
            g_report.push_back(p.file + ": no memory for the name");
            continue;
        }
        sw::protos::CellSet(level, g_a[2], g_a[3], g_a[5], g_a[6], g_a[7], 0, g_a[8], g_a[9],
                            g_a[10], g_a[11], gx, gy, &name, def, g_a[13] & 0xff);
        release_name(name);
        ++placed;
        g_report.push_back(p.file + ": placed at " + std::to_string(gx) + ", " + std::to_string(gy));
    }
    if (placed && sw::protos::WaveRaster) sw::protos::WaveRaster(system, g_a[3]);
    g_pending.clear();
}

void on_afterwards(void* self, void*) {
    if (!g_captured.exchange(false)) return;
    if (reinterpret_cast<std::uint64_t>(self) != g_a[9]) return;   // the other call sites
    dispatch(EventInfo{Event::tiles_placed});
    place_pending(true);
    dispatch(EventInfo{Event::tiles_placed_late});
    place_pending(false);
}

bool read_cell(std::uintptr_t cell, Cell& out) {
    std::int32_t st = 0;
    if (read_at(cell, st)) out.state = st;
    if (read_at(cell + 4, st)) out.requested = st;
    std::int32_t gx = 0, gy = 0;
    if (!read_at(cell + kOffCellGx, gx) || !read_at(cell + kOffCellGy, gy)) return false;
    out.gx = gx;
    out.gy = gy;
    std::uint8_t b = 0;
    if (read_at(cell + sw::protos::kOffCellIsIsland, b)) out.is_island = b != 0;
    if (read_at(cell + kOffCellVolcanic, b)) out.is_volcanic = b != 0;
    if (read_at(cell + kOffCellPurchasable, b)) out.purchasable = b != 0;
    read_at(cell + kOffCellFloor, out.sea_floor_height);
    std::int32_t type = 0;
    if (read_at(cell + kOffCellType, type)) out.tile_type = type;
    std::int32_t cost = 0;
    if (read_at(cell + kOffCellCost, cost)) out.purchase_cost = cost;
    sw::protos::MmString str{};
    if (read_at(cell + sw::protos::kOffCellName, str) && str.data && str.size > 0 && str.size < 4096 &&
        sw::Runtime::get().readable(str.data, str.size))
        out.name.assign(str.data, str.size);
    return true;
}

}  // namespace

namespace {

struct Listener {
    Event event;
    EventCallback callback;
    void* user;
};
std::mutex g_listener_mutex;
std::vector<Listener> g_listeners;
bool g_hooked[6] = {};

void dispatch(const EventInfo& info) {
    std::vector<Listener> copy;
    {
        std::lock_guard<std::mutex> lock(g_listener_mutex);
        copy = g_listeners;
    }
    for (const Listener& l : copy)
        if (l.event == info.event) l.callback(info, l.user);
}

void hook_definitions(void*, void*) { dispatch(EventInfo{Event::definitions_loaded}); }
void hook_world(void*, void*) { dispatch(EventInfo{Event::world_created}); }
void hook_stream(void*, void*) { dispatch(EventInfo{Event::stream_update}); }
void hook_cell(void*, void*) {
    EventInfo info{Event::cell_created};
    info.gx = static_cast<int>(sw::Hook::arg(1));
    info.gy = static_cast<int>(sw::Hook::arg(2));
    dispatch(info);
}

}  // namespace

bool on(Event event, EventCallback callback, void* user, std::string& error) {
    if (!callback) {
        error = "no callback";
        return false;
    }
    auto& rt = sw::Runtime::get();
    const int index = static_cast<int>(event);
    if (!g_hooked[index] && event != Event::tiles_placed && event != Event::tiles_placed_late) {
        if (!sw::protos::bind(rt)) {
            error = "the game functions do not match the mapping";
            return false;
        }
        std::uint32_t rva = 0;
        sw::HookCallback cb = nullptr;
        switch (event) {
            case Event::definitions_loaded: rva = 0x575c50; cb = &hook_definitions; break;
            case Event::world_created: rva = 0x741cf0; cb = &hook_world; break;
            case Event::stream_update: rva = 0x5797b0; cb = &hook_stream; break;
            case Event::cell_created: rva = 0x579670; cb = &hook_cell; break;
            default: break;
        }
        if (sw::Runtime::get().hook_rva(rva, cb, nullptr, error) != sw::HookStatus::ok) return false;
        g_hooked[index] = true;
    }
    std::lock_guard<std::mutex> lock(g_listener_mutex);
    g_listeners.push_back(Listener{event, callback, user});
    return true;
}

bool off(Event event, EventCallback callback, void* user) {
    std::lock_guard<std::mutex> lock(g_listener_mutex);
    for (auto it = g_listeners.begin(); it != g_listeners.end(); ++it) {
        if (it->event == event && it->callback == callback && it->user == user) {
            g_listeners.erase(it);
            return true;
        }
    }
    return false;
}

namespace {

bool lookup_cell(std::uintptr_t level, int gx, int gy, std::uintptr_t& cell) {
    auto& rt = sw::Runtime::get();
    const auto* fn = rt.at_rva(kRvaCellFind);
    if (!fn || !rt.is_function_start(reinterpret_cast<std::uintptr_t>(fn))) return false;
    const std::uint64_t packed =
        static_cast<std::uint32_t>(gx) | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(gy)) << 32);
    cell = reinterpret_cast<CellFindFn>(const_cast<void*>(fn))(level, packed);
    return cell != 0;
}

}  // namespace

std::vector<Cell> cells_in(int min_gx, int min_gy, int max_gx, int max_gy) {
    std::vector<Cell> out;
    const std::uintptr_t level = first_level();
    if (!level || max_gx < min_gx || max_gy < min_gy) return out;
    // The level is 256 x 256 cells; a larger rectangle cannot hold more.
    if (max_gx - min_gx > 255) max_gx = min_gx + 255;
    if (max_gy - min_gy > 255) max_gy = min_gy + 255;
    for (int gy = min_gy; gy <= max_gy; ++gy) {
        for (int gx = min_gx; gx <= max_gx; ++gx) {
            std::uintptr_t cell = 0;
            Cell c;
            if (lookup_cell(level, gx, gy, cell) && read_cell(cell, c)) out.push_back(std::move(c));
        }
    }
    return out;
}

std::vector<Cell> cells() {
    std::int32_t ux = 0, uy = 0;
    const std::uintptr_t level = first_level();
    if (!level || !read_at(level + sw::protos::kOffOriginX, ux) ||
        !read_at(level + sw::protos::kOffOriginY, uy))
        return {};
    const int half = sw::protos::kAreaAxis;
    return cells_in(ux - half, uy - half, ux + half - 1, uy + half - 1);
}

bool is_free(int gx, int gy) {
    const std::uintptr_t level = first_level();
    std::int32_t ux = 0, uy = 0;
    if (!level || !sw::protos::Area || !read_at(level + sw::protos::kOffOriginX, ux) ||
        !read_at(level + sw::protos::kOffOriginY, uy))
        return false;
    const int rx = gx - ux;
    const int ry = gy - uy;
    const int limit = sw::protos::kAreaAxis;
    if (rx - 1 < -limit || ry - 1 < -limit || rx + 1 >= limit || ry + 1 >= limit) return false;
    return is_free(level, rx, ry);
}

bool watch() {
    if (g_watching) return true;
    auto& rt = sw::Runtime::get();
    if (!sw::protos::bind(rt)) return false;
    std::string error;
    if (rt.hook_rva(sw::protos::Tiles_place_fixed::kRva, &on_placement, nullptr, error) !=
        sw::HookStatus::ok)
        return false;
    if (rt.hook_rva(sw::protos::kRvaAfterwards, &on_afterwards, nullptr, error) !=
        sw::HookStatus::ok)
        return false;
    g_watching = true;
    return true;
}

bool ready() { return first_level() != 0; }

Definition definition(std::string_view file) {
    Definition out;
    const std::uintptr_t def = find_definition(file);
    if (!def) return out;
    out.found = true;
    std::uint8_t b = 0;
    if (read_at(def + sw::protos::kOffDefIsIsland, b)) out.is_island = b != 0;
    if (read_at(def + kOffDefVolcanic, b)) out.is_volcanic = b != 0;
    if (read_at(def + kOffDefPurchasable, b)) out.purchasable = b != 0;
    read_at(def + kOffDefFloor, out.sea_floor_height);
    std::int32_t type = 0;
    if (read_at(def + kOffDefType, type)) out.tile_type = type;
    std::int32_t cost = 0;
    if (read_at(def + kOffDefCost, cost)) out.purchase_cost = cost;
    return out;
}

bool cell_at(int gx, int gy, Cell& out) {
    const std::uintptr_t level = first_level();
    if (!level) return false;
    auto& rt = sw::Runtime::get();
    const auto* fn = rt.at_rva(kRvaCellFind);
    if (!fn || !rt.is_function_start(reinterpret_cast<std::uintptr_t>(fn))) return false;
    const std::uint64_t packed =
        static_cast<std::uint32_t>(gx) | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(gy)) << 32);
    const std::uintptr_t cell = reinterpret_cast<CellFindFn>(const_cast<void*>(fn))(level, packed);
    if (!cell) return false;
    out = Cell{};
    return read_cell(cell, out);
}

bool place(std::string_view file, int gx, int gy, std::string& error) {
    if (!g_watching) {
        error = "call sw::tiles::watch() first";
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_pending.push_back(Pending{std::string(file), gx, gy});
    return true;
}

bool place_random(std::string_view file, int center_gx, int center_gy, int min_distance,
                  int max_distance, std::string& error) {
    if (!g_watching) {
        error = "call sw::tiles::watch() first";
        return false;
    }
    if (max_distance < 0 || max_distance > 120 || min_distance > max_distance) {
        error = "the distances have to be 0 <= min <= max <= 120";
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    Pending p{std::string(file), center_gx, center_gy};
    p.random = true;
    p.min_distance = min_distance;
    p.max_distance = max_distance;
    g_pending.push_back(std::move(p));
    return true;
}

int world_seed() {
    if (!g_captured_once.load()) return 0;
    std::int32_t seed = 0;
    read_at(static_cast<std::uintptr_t>(g_a[4]) + kOffStartSeed, seed);
    return seed;
}

std::vector<std::string> last_report() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_report;
}

std::size_t pending() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_pending.size();
}

void clear() {
    {
        std::lock_guard<std::mutex> lock(g_listener_mutex);
        g_listeners.clear();
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_pending.clear();
    g_report.clear();
}

#else  // !_WIN64

bool watch() { return false; }
bool ready() { return false; }
Definition definition(std::string_view) { return Definition{}; }
bool cell_at(int, int, Cell&) { return false; }
bool place(std::string_view, int, int, std::string& error) {
    error = "x64 only";
    return false;
}
bool place_random(std::string_view, int, int, int, int, std::string& error) {
    error = "x64 only";
    return false;
}
int world_seed() { return 0; }
std::vector<std::string> last_report() { return {}; }
std::size_t pending() { return 0; }
void clear() {}
bool on(Event, EventCallback, void*, std::string& error) {
    error = "x64 only";
    return false;
}
bool off(Event, EventCallback, void*) { return false; }
std::vector<Cell> cells() { return {}; }
std::vector<Cell> cells_in(int, int, int, int) { return {}; }
bool is_free(int, int) { return false; }

#endif

}  // namespace sw::tiles
