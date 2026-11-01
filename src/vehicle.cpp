#include "sw/vehicle.hpp"

#include "sw/generated_vehicle.hpp"

#include <cstring>

#if defined(_WIN64)

#include "sw/hook.hpp"
#include "sw/runtime.hpp"

#include <atomic>

namespace sw::vehicle {

namespace {

namespace m = sw::vehicle::map;

constexpr std::uint32_t kMaxRing = 1u << 16;     // sanity bound of capacity
constexpr std::uint32_t kMaxName = 512;

std::atomic<std::uintptr_t> g_world{0};
bool g_watching = false;

bool readable(std::uintptr_t a, std::size_t n) {
    return a && sw::Runtime::get().readable(reinterpret_cast<const void*>(a), n);
}

template <typename T>
bool rd(std::uintptr_t base, std::uintptr_t offset, T& out) {
    if (!readable(base + offset, sizeof(T))) return false;
    std::memcpy(&out, reinterpret_cast<const void*>(base + offset), sizeof(T));
    return true;
}

// A ring {ptr data, u32 capacity, u32 head, u32 count} at address a.
struct Ring {
    std::uintptr_t data = 0;
    std::uint32_t cap = 0, head = 0, count = 0;
};

bool read_ring(std::uintptr_t a, Ring& r) {
    std::uint64_t d = 0;
    if (!rd(a, 0, d) || !rd(a, 8, r.cap) || !rd(a, 0xc, r.head) || !rd(a, 0x10, r.count)) return false;
    r.data = static_cast<std::uintptr_t>(d);
    if (r.count == 0) return r.cap <= kMaxRing;      // an empty ring may have no storage
    if (r.cap == 0 || r.cap > kMaxRing || r.count > r.cap) return false;
    return true;
}

bool entry(const Ring& r, std::uint32_t i, std::size_t stride, std::uintptr_t& out) {
    if (i >= r.count) return false;
    const std::uint64_t idx = (static_cast<std::uint64_t>(r.head) + i) % r.cap;
    out = r.data + static_cast<std::uintptr_t>(idx) * stride;
    return readable(out, stride);
}

bool world_ok(std::uintptr_t w) {
    if (!w || (w & 7)) return false;
    if (!readable(w, m::kWorldMinBytes)) return false;
    Ring r;
    if (!read_ring(w + m::kOffRingRecords, r) || !read_ring(w + m::kOffRingStates, r) || !read_ring(w + m::kOffRingGroups, r)) return false;
    return true;
}

void accept(std::uintptr_t w) {
    if (w == g_world.load(std::memory_order_relaxed)) return;
    if (world_ok(w)) g_world.store(w, std::memory_order_relaxed);
}

void on_weather(void*, void*) { std::uintptr_t e = static_cast<std::uintptr_t>(sw::Hook::arg(0)); if (e > m::kOffWeatherInWorld) accept(e - m::kOffWeatherInWorld); }
void on_table(void*, void*) { std::uintptr_t t = static_cast<std::uintptr_t>(sw::Hook::arg(0)); if (t > m::kOffTableInWorld) accept(t - m::kOffTableInWorld); }

std::uintptr_t world() {
    const std::uintptr_t w = g_world.load(std::memory_order_relaxed);
    return world_ok(w) ? w : 0;
}

bool read_string(std::uintptr_t ptr, std::uint32_t len, std::string& out) {
    out.clear();
    if (len > kMaxName) return false;
    if (len == 0) return true;
    if (!readable(ptr, len)) return false;
    out.assign(reinterpret_cast<const char*>(ptr), len);
    return true;
}

// The record of a vehicle id, 0 if none.
std::uintptr_t record_of(std::uintptr_t w, int id, std::uint32_t* index = nullptr) {
    Ring r;
    if (!read_ring(w + m::kOffRingRecords, r)) return 0;
    for (std::uint32_t i = 0; i < r.count && i < kMaxRing; ++i) {
        std::uintptr_t e = 0;
        std::uint64_t p = 0;
        if (!entry(r, i, 8, e) || !rd(e, 0, p) || !p) continue;
        std::int32_t rid = 0;
        if (rd(static_cast<std::uintptr_t>(p), m::kRecId, rid) && rid == id) {
            if (index) *index = i;
            return static_cast<std::uintptr_t>(p);
        }
    }
    return 0;
}

bool fill_info(std::uintptr_t rec, Info& v) {
    std::uint32_t name_len = 0;
    std::uint64_t name_ptr = 0, st = 0;
    std::int32_t id = 0, gid = 0;
    std::uint8_t ed = 0, inv = 0;
    if (!rd(rec, m::kRecId, id) || !rd(rec, m::kRecGroupId, gid) || !rd(rec, m::kRecNamePtr, name_ptr) ||
        !rd(rec, m::kRecNameLen, name_len) || !rd(rec, m::kRecEditable, ed) || !rd(rec, m::kRecInvulnerable, inv) ||
        !rd(rec, m::kRecState, st))
        return false;
    v = Info{};
    v.id = id;
    v.group_id = gid;
    if (!read_string(static_cast<std::uintptr_t>(name_ptr), name_len, v.name)) v.name.clear();
    if (!readable(rec + m::kRecMatrix, sizeof(v.matrix))) return false;
    std::memcpy(v.matrix, reinterpret_cast<const void*>(rec + m::kRecMatrix), sizeof(v.matrix));
    v.x = v.matrix[12];
    v.y = v.matrix[13];
    v.z = v.matrix[14];
    v.editable = ed != 0;
    v.invulnerable = inv != 0;
    if (st) {
        const auto x = static_cast<std::uintptr_t>(st);
        std::uint8_t flag = 1;
        std::uint64_t scene = 0;
        std::uint32_t state = 0;
        if (rd(x, m::kStFlag0, flag) && rd(x, m::kStScene, scene) && rd(x, m::kStState, state)) {
            v.has_state = true;
            v.state = state;
            if (scene) {
                Ring bodies;
                if (read_ring(static_cast<std::uintptr_t>(scene) + m::kSceneRingBodies, bodies)) {
                    v.bodies = static_cast<int>(bodies.count);
                    v.simulating = state == m::kStReady && bodies.count != 0 && flag == 0;
                }
            }
        }
    }
    return true;
}

// The scene vehicle SV of a record, 0 if none.
std::uintptr_t scene_of(std::uintptr_t rec) {
    std::uint64_t st = 0, scene = 0;
    if (!rd(rec, m::kRecState, st) || !st || !rd(static_cast<std::uintptr_t>(st), m::kStScene, scene) || !scene) return 0;
    const auto s = static_cast<std::uintptr_t>(scene);
    return readable(s, m::kSceneMinBytes) ? s : 0;
}

const m::KindRow* row(Kind k) {
    const auto i = static_cast<std::size_t>(k);
    return i < m::kKindCount ? &m::kKinds[i] : nullptr;
}

const char* class_of(std::uintptr_t comp) {
    std::uint64_t vt = 0;
    if (!rd(comp, m::kCompVtable, vt)) return nullptr;
    const std::uintptr_t base = sw::Runtime::get().base();
    if (static_cast<std::uintptr_t>(vt) < base) return nullptr;
    const auto rva = static_cast<std::uint64_t>(vt) - base;
    for (std::size_t i = 0; i < m::kComponentClassCount; ++i)
        if (m::kComponentClasses[i].vtable_rva == rva) return m::kComponentClasses[i].name;
    return nullptr;
}

bool fill_component(std::uintptr_t comp, Kind kind, std::uint32_t key, Component& c) {
    const auto* kr = row(kind);
    if (!kr || !readable(comp, m::kCompMinBytes)) return false;
    std::uint64_t vt = 0;
    if (!rd(comp, m::kCompVtable, vt)) return false;
    if (static_cast<std::uint64_t>(vt) != static_cast<std::uint64_t>(sw::Runtime::get().base()) + kr->vtable_rva) return false;   // wrong class: skip
    std::uint64_t np = 0;
    std::uint32_t nl = 0;
    std::int32_t x = 0, y = 0, z = 0;
    if (!rd(comp, m::kCompNamePtr, np) || !rd(comp, m::kCompNameLen, nl) || !rd(comp, m::kCompPosX, x) ||
        !rd(comp, m::kCompPosY, y) || !rd(comp, m::kCompPosZ, z))
        return false;
    c = Component{};
    c.kind = kind;
    c.key = key;
    if (!read_string(static_cast<std::uintptr_t>(np), nl, c.name)) c.name.clear();
    const char* cn = class_of(comp);
    c.class_name = cn ? cn : "";
    c.x = x;
    c.y = y;
    c.z = z;
    c.address = comp;
    return true;
}

}  // namespace

const char* kind_name(Kind kind) { const auto* r = row(kind); return r ? r->name : ""; }
const char* kind_lua_api(Kind kind) { const auto* r = row(kind); return r ? r->lua_api : ""; }
bool kind_from_name(const char* name, Kind& out) {
    if (!name) return false;
    for (std::size_t i = 0; i < m::kKindCount; ++i)
        if (std::strcmp(m::kKinds[i].name, name) == 0) { out = static_cast<Kind>(i); return true; }
    return false;
}

bool watch() {
    if (g_watching) return true;
    auto& rt = sw::Runtime::get();
    if (!rt.attached()) return false;
    std::string error;
    bool any = false;
    any |= rt.hook_rva(m::kRvaSamplerWeather, &on_weather, nullptr, error) == sw::HookStatus::ok;
    any |= rt.hook_rva(m::kRvaSamplerDelta, &on_table, nullptr, error) == sw::HookStatus::ok;
    any |= rt.hook_rva(m::kRvaSamplerAdd, &on_table, nullptr, error) == sw::HookStatus::ok;
    g_watching = any;
    return any;
}

bool ready() { return world() != 0; }
std::uintptr_t world_address() { return world(); }

std::size_t count() {
    const auto w = world();
    Ring r;
    if (!w || !read_ring(w + m::kOffRingRecords, r)) return 0;
    return r.count;
}

std::vector<Info> list() {
    std::vector<Info> out;
    const auto w = world();
    Ring r;
    if (!w || !read_ring(w + m::kOffRingRecords, r)) return out;
    for (std::uint32_t i = 0; i < r.count && i < kMaxRing; ++i) {
        std::uintptr_t e = 0;
        std::uint64_t p = 0;
        Info v;
        if (entry(r, i, 8, e) && rd(e, 0, p) && p && fill_info(static_cast<std::uintptr_t>(p), v)) out.push_back(std::move(v));
    }
    return out;
}

std::vector<int> ids() {
    std::vector<int> out;
    for (const auto& v : list()) out.push_back(v.id);
    return out;
}

bool find(int id, Info& out) {
    const auto w = world();
    if (!w) return false;
    const auto rec = record_of(w, id);
    return rec && fill_info(rec, out);
}

bool position(int id, double& x, double& y, double& z) {
    Info v;
    if (!find(id, v)) return false;
    x = v.x; y = v.y; z = v.z;
    return true;
}

int find_by_name(const std::string& name) {
    for (const auto& v : list())
        if (v.name == name) return v.id;
    return -1;
}

std::vector<int> group_members(int group_id) {
    std::vector<int> out;
    const auto w = world();
    Ring groups;
    if (!w || !read_ring(w + m::kOffRingGroups, groups)) return out;
    for (std::uint32_t i = 0; i < groups.count && i < kMaxRing; ++i) {
        std::uintptr_t e = 0;
        std::uint64_t g = 0;
        if (!entry(groups, i, 8, e) || !rd(e, 0, g) || !g) continue;
        const auto ga = static_cast<std::uintptr_t>(g);
        std::int32_t gid = 0;
        if (!rd(ga, m::kGroupId, gid) || gid != group_id) continue;
        Ring ids;
        if (!readable(ga, m::kGroupMinBytes) || !read_ring(ga + m::kGroupIds, ids)) return out;
        for (std::uint32_t k = 0; k < ids.count && k < kMaxRing; ++k) {
            std::uintptr_t ea = 0;
            std::uint32_t vid = 0;
            if (entry(ids, k, 4, ea) && rd(ea, 0, vid)) out.push_back(static_cast<int>(vid));
        }
        return out;
    }
    return out;
}

std::vector<Component> components(int vehicle_id, Kind kind) {
    std::vector<Component> out;
    const auto* kr = row(kind);
    const auto w = world();
    if (!kr || !w) return out;
    const auto rec = record_of(w, vehicle_id);
    const auto sv = rec ? scene_of(rec) : 0;
    Ring r;
    if (!sv || !read_ring(sv + kr->ring_offset, r)) return out;
    for (std::uint32_t i = 0; i < r.count && i < kMaxRing; ++i) {
        std::uintptr_t e = 0;
        std::uint32_t key = 0;
        std::uint64_t p = 0;
        Component c;
        if (entry(r, i, 16, e) && rd(e, 0, key) && rd(e, 8, p) && p && fill_component(static_cast<std::uintptr_t>(p), kind, key, c))
            out.push_back(std::move(c));
    }
    return out;
}

bool find_component(int vehicle_id, Kind kind, const std::string& name, Component& out) {
    for (auto& c : components(vehicle_id, kind))
        if (c.name == name) { out = std::move(c); return true; }
    return false;
}

bool button_values(int vehicle_id, const std::string& name, ButtonValues& out) {
    Component c;
    return find_component(vehicle_id, Kind::button, name, c) && rd(c.address, m::kCompButtonValue, out.value) &&
           rd(c.address, m::kCompButtonValue2, out.value2);
}

bool battery_charge_raw(int vehicle_id, const std::string& name, float& out) {
    Component c;
    return find_component(vehicle_id, Kind::battery, name, c) && rd(c.address, m::kCompBatteryChargeRaw, out);
}

bool tank_capacity_raw(int vehicle_id, const std::string& name, float& out) {
    Component c;
    return find_component(vehicle_id, Kind::tank, name, c) && rd(c.address, m::kCompTankCapacityRaw, out);
}

bool raw_u32(const Component& c, std::uint32_t offset, std::uint32_t& out) {
    return offset < m::kCompMaxRaw && (offset & 3) == 0 && rd(c.address, offset, out);
}
bool raw_f32(const Component& c, std::uint32_t offset, float& out) {
    return offset < m::kCompMaxRaw && (offset & 3) == 0 && rd(c.address, offset, out);
}

}  // namespace sw::vehicle

#else  // x86: nothing is offered

namespace sw::vehicle {
namespace m = sw::vehicle::map;
const char* kind_name(Kind kind) { const auto i = static_cast<std::size_t>(kind); return i < m::kKindCount ? m::kKinds[i].name : ""; }
const char* kind_lua_api(Kind kind) { const auto i = static_cast<std::size_t>(kind); return i < m::kKindCount ? m::kKinds[i].lua_api : ""; }
bool kind_from_name(const char* name, Kind& out) {
    if (!name) return false;
    for (std::size_t i = 0; i < m::kKindCount; ++i)
        if (std::strcmp(m::kKinds[i].name, name) == 0) { out = static_cast<Kind>(i); return true; }
    return false;
}
bool watch() { return false; }
bool ready() { return false; }
std::uintptr_t world_address() { return 0; }
std::size_t count() { return 0; }
std::vector<int> ids() { return {}; }
bool find(int, Info&) { return false; }
std::vector<Info> list() { return {}; }
bool position(int, double&, double&, double&) { return false; }
std::vector<int> group_members(int) { return {}; }
int find_by_name(const std::string&) { return -1; }
std::vector<Component> components(int, Kind) { return {}; }
bool find_component(int, Kind, const std::string&, Component&) { return false; }
bool button_values(int, const std::string&, ButtonValues&) { return false; }
bool battery_charge_raw(int, const std::string&, float&) { return false; }
bool tank_capacity_raw(int, const std::string&, float&) { return false; }
bool raw_u32(const Component&, std::uint32_t, std::uint32_t&) { return false; }
bool raw_f32(const Component&, std::uint32_t, float&) { return false; }
}  // namespace sw::vehicle

#endif
