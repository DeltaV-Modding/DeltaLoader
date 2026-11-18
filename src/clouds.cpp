// clouds.cpp - where the game's clouds appear (sw/clouds.hpp, REPORT 3.214).
#include "sw/clouds.hpp"

#include "sw/hook.hpp"
#include "sw/runtime.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <unordered_map>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace sw::clouds {

// ---- the rules (pure) ----------------------------------------------------------------------------------------------------

namespace {
bool inside(const Area& a, double x, double z) {
    const double dx = x - a.x, dz = z - a.z;
    return a.radius > 0 && dx * dx + dz * dz <= a.radius * a.radius;
}
}  // namespace

bool allowed(const Rules& rules, double x, double z) {
    for (const auto& a : rules.never_in)
        if (inside(a, x, z)) return false;
    if (rules.only_in.empty()) return true;
    for (const auto& a : rules.only_in)
        if (inside(a, x, z)) return true;
    return false;
}

bool choose_place(const Rules& rules, double cx, double cz, double radius, double& x, double& z, double (*random)(void*), void* state) {
    if (allowed(rules, x, z)) return true;
    if (radius <= 0 || !random) return false;
    // the areas of only_in that reach into the field
    std::vector<const Area*> reach;
    for (const auto& a : rules.only_in) {
        const double dx = a.x - cx, dz = a.z - cz;
        if (a.radius > 0 && std::sqrt(dx * dx + dz * dz) < a.radius + radius) reach.push_back(&a);
    }
    if (!rules.only_in.empty() && reach.empty()) return false;
    const double pi = 3.14159265358979323846;
    for (int attempt = 0; attempt < 32; ++attempt) {
        double px, pz;
        if (!reach.empty()) {
            const Area& a = *reach[static_cast<std::size_t>(random(state) * static_cast<double>(reach.size())) % reach.size()];
            const double r = a.radius * std::sqrt(random(state)), t = 2 * pi * random(state);
            px = a.x + r * std::cos(t);
            pz = a.z + r * std::sin(t);
        } else {
            px = cx + (random(state) * 2 - 1) * radius;   // as the game: a square around the centre
            pz = cz + (random(state) * 2 - 1) * radius;
        }
        const double dx = px - cx, dz = pz - cz;
        if (dx * dx + dz * dz > radius * radius * 0.9) continue;   // the game ends a cloud outside the radius
        if (!allowed(rules, px, pz)) continue;
        x = px;
        z = pz;
        return true;
    }
    return false;
}

// ---- the game side ------------------------------------------------------------------------------------------------------
// One entry hook on the update 0x2c9700 does everything. A cloud the update spawned (0x2c90d0) is seen at the entry of the next
// update and moved there: a new cloud starts at size 0 (the spawn clears +0x1a8..+0x1b0) and the update scales it by age / lifetime
// over the first tenth of its life, so the tick it spent at the game's place is not visible. No detour is needed (and the public SDK has none).

namespace {

// The game updates two cloud fields with the same function (REPORT 3.214a): the logic world's and the one the renderer draws.
// Each field keeps its own state; the rules apply to both, a cloud of spawn_at() goes into the drawn one.
struct Field {
    int game_max = -1;          // the game's own values, read before the first write
    float game_radius = -1;
    bool drawn = false;
    unsigned long long respawn_done = 0;   // the respawn_all() request this field carried out
    unsigned long long last_seen = 0;      // GetTickCount64 of its last update
    std::unordered_map<std::uintptr_t, std::uint32_t> seen;   // cloud address -> the bits of its lifetime: a cloud already handled
};

std::mutex g_mutex;
Rules g_rules;
std::deque<std::array<double, 3>> g_queue;
std::unordered_map<std::uintptr_t, Field> g_fields;
unsigned long long g_respawn = 0;   // number of respawn_all() requests
std::atomic<bool> g_installed{false};
std::atomic<std::uintptr_t> g_field_drawn{0}, g_field_any{0};
std::atomic<unsigned long long> g_drawn_seen{0};
std::atomic<unsigned long long> g_spawned{0}, g_moved{0}, g_removed{0}, g_placed{0};

std::uint64_t g_rng = 0x9e3779b97f4a7c15ull;
double next_random(void*) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return static_cast<double>(g_rng >> 11) * (1.0 / 9007199254740992.0);
}

template <class T>
bool get(std::uintptr_t at, T& out) {
    auto& rt = sw::Runtime::get();
    if (!rt.readable(reinterpret_cast<const void*>(at), sizeof(T))) return false;
    std::memcpy(&out, reinterpret_cast<const void*>(at), sizeof(T));
    return true;
}
template <class T>
void put(std::uintptr_t at, const T& value) {
    std::memcpy(reinterpret_cast<void*>(at), &value, sizeof(T));
}

std::uintptr_t address_of(std::uint32_t rva) { return reinterpret_cast<std::uintptr_t>(sw::Runtime::get().at_rva(rva)); }

// cloud i of the ring
std::uintptr_t cloud_at(std::uintptr_t field, std::uint32_t i) {
    std::uintptr_t data = 0;
    std::uint32_t cap = 0, head = 0;
    if (!get(field + kRing, data) || !get(field + kRing + 8, cap) || !get(field + kRing + 12, head) || !data || !cap) return 0;
    return data + static_cast<std::uintptr_t>((head + i) % cap) * kCloudSize;
}

void end_cloud(std::uintptr_t cloud) {
    float life = 0;
    if (get(cloud + kCloudLife, life)) put(cloud + kCloudAge, life + 1.0f);
}

// A cloud shrinks away the way the game ends one: from 0.9 of its life it shrinks to 0 (the update, 0x2c98e1).
void fade_cloud(std::uintptr_t cloud) {
    float life = 0, age = 0;
    if (!get(cloud + kCloudLife, life) || !get(cloud + kCloudAge, age) || life <= 0) return;
    if (age < 0.1f * life) {   // still growing: shrink from the size it has now
        put(cloud + kCloudAge, life - age);
    } else if (age < 0.9f * life) {
        put(cloud + kCloudAge, 0.9f * life);
    }
}

void set_position(std::uintptr_t cloud, double x, double y, double z) {
    put(cloud + kCloudPos, x);
    put(cloud + kCloudPos + 8, y);
    put(cloud + kCloudPos + 16, z);
    for (std::uint32_t m : {kCloudMatrix, kCloudMatrixLast}) {   // translation of the matrix and of the one of the tick before
        put(cloud + m + 0x60, x);
        put(cloud + m + 0x68, y);
        put(cloud + m + 0x70, z);
    }
}

// The rules for one cloud: move it, or take it away (g_mutex held). A cloud the game just made (size 0) is ended at once; a cloud
// already in the sky (existing = true, after respawn_all) keeps its size and shrinks away like a cloud at the end of its life.
void adopt(std::uintptr_t field, std::uintptr_t cloud, bool takes_queue, bool existing = false) {
    if (!existing) g_spawned.fetch_add(1);
    double pos[3] = {};
    double centre[3] = {};
    float radius = 0;
    if (!get(cloud + kCloudPos, pos) || !get(field + kCentre, centre) || !get(field + kRadius, radius)) return;
    if (!existing && takes_queue && !g_queue.empty()) {
        const auto p = g_queue.front();
        g_queue.pop_front();
        set_position(cloud, p[0], p[1], p[2]);
        float life = 0;   // half its size at once (a new cloud grows from 0 over a tenth of its life)
        if (get(cloud + kCloudLife, life)) put(cloud + kCloudAge, 0.05f * life);
        g_placed.fetch_add(1);
        return;
    }
    const Rules& r = g_rules;
    bool changed = false;
    if (r.max_height > r.min_height && r.min_height >= 0) {
        pos[1] = r.min_height + next_random(nullptr) * (r.max_height - r.min_height);
        changed = true;
    }
    if (!r.only_in.empty() || !r.never_in.empty()) {
        double x = pos[0], z = pos[2];
        if (!choose_place(r, centre[0], centre[2], radius, x, z, &next_random, nullptr)) {
            if (existing) {
                fade_cloud(cloud);
            } else {
                set_position(cloud, pos[0], -10000.0, pos[2]);   // under the world for the rest of this tick
                end_cloud(cloud);
            }
            g_removed.fetch_add(1);
            return;
        }
        if (x != pos[0] || z != pos[2]) {
            pos[0] = x;
            pos[2] = z;
            changed = true;
        }
    }
    if (r.mesh >= 0) {
        std::uint16_t meshes = 0;
        if (get(field + kMeshCount, meshes) && r.mesh < meshes) put(cloud + kCloudMesh, static_cast<std::uint16_t>(r.mesh));
    }
    if (changed) {
        set_position(cloud, pos[0], pos[1], pos[2]);
        g_moved.fetch_add(1);
    }
}

// the entry of the update 0x2c9700(field, &ticks, environment, centre): new clouds, maximum, radius, ending clouds
void on_update(void* self, void*) {
    const std::uintptr_t field = reinterpret_cast<std::uintptr_t>(self);
    const std::uintptr_t environment = static_cast<std::uintptr_t>(sw::Hook::arg(2));
    const bool drawn = environment == field + kDrawnEnvironment;
    const unsigned long long now_ms = GetTickCount64();
    g_field_any.store(field);
    if (drawn) {
        g_field_drawn.store(field);
        g_drawn_seen.store(now_ms);
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    std::uint32_t max = 0, count = 0;
    float radius = 0;
    if (!get(field + kMaxClouds, max) || !get(field + kRadius, radius) || !get(field + kRing + 0x10, count)) return;
    auto found = g_fields.find(field);
    if (found == g_fields.end() || now_ms - found->second.last_seen > 5000) {   // a new field (another world): its own values are the game's
        if (g_fields.size() > 16) {
            for (auto it = g_fields.begin(); it != g_fields.end();)
                it = now_ms - it->second.last_seen > 5000 ? g_fields.erase(it) : std::next(it);
        }
        Field f;
        f.game_max = static_cast<int>(max);
        f.game_radius = radius;
        f.respawn_done = g_respawn;
        found = g_fields.insert_or_assign(field, std::move(f)).first;
    }
    Field& f = found->second;
    f.last_seen = now_ms;
    f.drawn = drawn;
    // the drawn field places the clouds of spawn_at(); the logic field only when no drawn one runs (a server)
    const bool takes_queue = drawn || now_ms - g_drawn_seen.load() > 2000;

    const std::uint32_t want_max =
        g_rules.max_clouds >= 0 ? static_cast<std::uint32_t>(std::min(g_rules.max_clouds, 2000)) : static_cast<std::uint32_t>(f.game_max);
    const float want_radius = g_rules.field_radius > 0 ? g_rules.field_radius : f.game_radius;
    if (want_max != max) put(field + kMaxClouds, want_max);
    if (want_radius != radius) put(field + kRadius, want_radius);

    // the clouds made since the last tick of this field
    std::unordered_map<std::uintptr_t, std::uint32_t> now;
    now.reserve(count);
    for (std::uint32_t i = 0; i < count && i < 4096; ++i) {
        const std::uintptr_t c = cloud_at(field, i);
        std::uint32_t life = 0;
        if (!c || !get(c + kCloudLife, life)) continue;
        now[c] = life;
        auto it = f.seen.find(c);
        if (it == f.seen.end() || it->second != life) adopt(field, c, takes_queue);
    }
    f.seen.swap(now);

    if (f.respawn_done != g_respawn) {
        // The clouds in the sky follow the new rules where they are: moved, or shrinking away. Ending them all would leave the sky
        // empty for a long time - the game's new clouds start at size 0 and grow over a tenth of their life (REPORT 3.216).
        f.respawn_done = g_respawn;
        for (std::uint32_t i = 0; i < count && i < 4096; ++i)
            if (const std::uintptr_t c = cloud_at(field, i)) adopt(field, c, false, true);
    } else if (takes_queue && !g_queue.empty() && count >= want_max && count > 0) {
        // make room for a cloud of spawn_at(): the one nearest to the end of its life goes
        std::uintptr_t oldest = 0;
        float best = -1;
        for (std::uint32_t i = 0; i < count && i < 4096; ++i) {
            const std::uintptr_t c = cloud_at(field, i);
            float life = 0, age = 0;
            if (!c || !get(c + kCloudLife, life) || !get(c + kCloudAge, age) || life <= 0) continue;
            if (age / life > best) {
                best = age / life;
                oldest = c;
            }
        }
        if (oldest) end_cloud(oldest);
    }
}

bool bytes_at(std::uint32_t rva, const std::vector<std::uint8_t>& expect) {
    auto& rt = sw::Runtime::get();
    const void* p = rt.at_rva(rva);
    return p && rt.readable(p, expect.size()) && std::memcmp(p, expect.data(), expect.size()) == 0;
}

}  // namespace

bool install(std::string& error) {
    if (g_installed.load()) return true;
    auto& rt = sw::Runtime::get();
    if (!rt.attached()) {
        error = "the game is not the build of the mapping";
        return false;
    }
    // the stores and reads of REPORT 3.214, checked before anything is changed
    if (!bytes_at(0x2c91d3, {0xf2, 0x0f, 0x11, 0x8f, 0x90, 0x01, 0x00, 0x00}) ||   // movsd [rdi+0x190], xmm1   (x)
        !bytes_at(0x2c9206, {0xf2, 0x0f, 0x11, 0x87, 0x98, 0x01, 0x00, 0x00}) ||   // movsd [rdi+0x198], xmm0   (y)
        !bytes_at(0x2c91e8, {0xf2, 0x0f, 0x11, 0x97, 0xa0, 0x01, 0x00, 0x00}) ||   // movsd [rdi+0x1a0], xmm2   (z)
        !bytes_at(0x2c913f, {0xf3, 0x0f, 0x10, 0x4e, 0x38}) ||                     // movss xmm1, [rsi+0x38]   (radius)
        !bytes_at(0x2c9100, {0x69, 0x41, 0x5c, 0xfd, 0x43, 0x03, 0x00})) {         // imul eax, [rcx+0x5c], 0x343fd (seed)
        error = "the cloud spawn 0x2c90d0 is not the one this was made for (or this is not the game client)";
        return false;
    }
    if (!bytes_at(0x2c9750, {0x3b, 0x41, 0x58}) || !bytes_at(0x2c9764, {0xe8})) {   // cmp eax, [rcx+0x58]; call 0x2c90d0: the fill loop
        error = "the cloud update 0x2c9700 is not the one this was made for";
        return false;
    }
    // the drawn field: field = X + 0xb49a8, environment = X + 0xb5050 (= field + 0x6a8) at its update call; the renderer reads it
    if (!bytes_at(0x5f69e5, {0x4c, 0x8d, 0xbe, 0xa8, 0x49, 0x0b, 0x00, 0x4c, 0x8d, 0x86, 0x50, 0x50, 0x0b, 0x00}) ||
        !bytes_at(0x6cec8e, {0x41, 0x39, 0x9f, 0xc0, 0x49, 0x0b, 0x00})) {
        error = "the drawn cloud field is not where it was";
        return false;
    }
    if (rt.hook_rva(kRvaUpdate, &on_update, nullptr, error) != sw::HookStatus::ok) return false;
    g_rng ^= static_cast<std::uint64_t>(GetTickCount64());
    g_installed.store(true);
    return true;
}

void uninstall() {
    auto& rt = sw::Runtime::get();
    std::string ignore;
    if (g_installed.load() && rt.attached()) rt.unhook(address_of(kRvaUpdate), &on_update, nullptr, ignore);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const unsigned long long now_ms = GetTickCount64();
        for (const auto& [field, f] : g_fields) {   // only fields that still run (a field of an unloaded world is gone)
            if (now_ms - f.last_seen > 2000 || !rt.readable(reinterpret_cast<const void*>(field), kSeed + 4)) continue;
            if (f.game_max >= 0) put(field + kMaxClouds, static_cast<std::uint32_t>(f.game_max));
            if (f.game_radius > 0) put(field + kRadius, f.game_radius);
        }
        g_fields.clear();
        g_queue.clear();
    }
    g_installed.store(false);
}

bool installed() { return g_installed.load(); }

void set_rules(const Rules& rules) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_rules = rules;
}

Rules rules() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_rules;
}

bool spawn_at(double x, double y, double z) {
    if (!g_installed.load() || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_queue.size() >= 64) return false;
    g_queue.push_back({x, y, z});
    return true;
}

void respawn_all() {
    std::lock_guard<std::mutex> lock(g_mutex);
    ++g_respawn;
}

Info info() {
    Info out;
    out.spawned = g_spawned.load();
    out.moved = g_moved.load();
    out.removed = g_removed.load();
    out.placed = g_placed.load();
    const bool drawn_runs = GetTickCount64() - g_drawn_seen.load() <= 2000;
    const std::uintptr_t field = drawn_runs ? g_field_drawn.load() : g_field_any.load();
    auto& rt = sw::Runtime::get();
    if (!field || !rt.readable(reinterpret_cast<const void*>(field), kSeed + 4)) return out;
    out.active = true;
    out.drawn = drawn_runs;
    std::uint32_t count = 0, max = 0;
    std::uint16_t meshes = 0;
    get(field + kRing + 0x10, count);
    get(field + kMaxClouds, max);
    get(field + kRadius, out.field_radius);
    get(field + kCentre, out.centre);
    get(field + kMeshCount, meshes);
    out.clouds = static_cast<int>(count);
    out.max_clouds = static_cast<int>(max);
    out.meshes = meshes;
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_fields.find(field);
    out.game_max_clouds = it != g_fields.end() ? it->second.game_max : -1;
    out.fields = static_cast<int>(g_fields.size());
    return out;
}

}  // namespace sw::clouds
