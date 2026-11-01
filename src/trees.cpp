#include "sw/trees.hpp"

#include "sw/runtime.hpp"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

namespace sw::trees {

namespace {

struct Rule {
    bool is_tile;
    int a, b;
    Kind kind;
};

struct Table {
    std::vector<Rule> rules;
};

constexpr std::size_t kMaxRules = 4096;

std::mutex g_edit;
std::atomic<const Table*> g_table{nullptr};

bool valid(Kind kind) {
    return kind == Kind::fir || kind == Kind::burnt || kind == Kind::snow;
}

bool put(bool is_tile, int a, int b, Kind kind) {
    if (!valid(kind)) return false;
    std::lock_guard<std::mutex> lock(g_edit);
    const Table* old = g_table.load();
    auto* next = new Table();
    if (old) next->rules = old->rules;
    for (auto& r : next->rules) {
        if (r.is_tile == is_tile && r.a == a && r.b == b) {
            r.kind = kind;
            g_table.store(next);
            return true;
        }
    }
    if (next->rules.size() >= kMaxRules) {
        delete next;
        return false;
    }
    next->rules.push_back(Rule{is_tile, a, b, kind});
    g_table.store(next);
    return true;
}

bool drop(bool is_tile, int a, int b) {
    std::lock_guard<std::mutex> lock(g_edit);
    const Table* old = g_table.load();
    if (!old) return false;
    auto* next = new Table();
    bool found = false;
    for (const auto& r : old->rules) {
        if (r.is_tile == is_tile && r.a == a && r.b == b) found = true;
        else next->rules.push_back(r);
    }
    if (!found) {
        delete next;
        return false;
    }
    g_table.store(next);
    return true;
}

}  // namespace

bool set_tile(int gx, int gz, Kind kind) { return put(true, gx, gz, kind); }
bool set_chunk(int cx, int cz, Kind kind) { return put(false, cx, cz, kind); }
bool clear_tile(int gx, int gz) { return drop(true, gx, gz); }
bool clear_chunk(int cx, int cz) { return drop(false, cx, cz); }

void clear_rules() {
    std::lock_guard<std::mutex> lock(g_edit);
    g_table.store(nullptr);
}

std::size_t rule_count() {
    const Table* t = g_table.load();
    return t ? t->rules.size() : 0;
}

Kind resolve(Kind original, int cx, int cz) {
    if (!valid(original)) return original;
    const Table* t = g_table.load();
    if (!t) return original;
    const int gx = tile_of_chunk(cx), gz = tile_of_chunk(cz);
    bool any = false;
    Kind want = original;
    for (const Rule& r : t->rules) {   // the last matching rule wins
        if (r.is_tile ? (r.a == gx && r.b == gz) : (r.a == cx && r.b == cz)) {
            want = r.kind;
            any = true;
        }
    }
    if (!any) return original;
    if (want == Kind::burnt) return Kind::burnt;      // forces the dead mesh
    if (original == Kind::burnt) return original;     // a forest fire is never undone
    return want;
}

#if defined(_WIN64)

namespace {

// The 35 bytes of both places, and the instruction after them.
const std::uint8_t kOriginal[kSiteBytes] = {
    0x49, 0x63, 0x87, 0xf8, 0x00, 0x00, 0x00,   // movsxd rax, dword [r15 + 0xf8]
    0x48, 0x8d, 0x0c, 0x40,                     // lea rcx, [rax + rax*2]
    0x49, 0x63, 0x87, 0xfc, 0x00, 0x00, 0x00,   // movsxd rax, dword [r15 + 0xfc]
    0x48, 0x03, 0xc8,                           // add rcx, rax
    0x48, 0x8b, 0x45, 0xf0,                     // mov rax, [rbp - 0x10]
    0x48, 0x8d, 0x04, 0xc8,                     // lea rax, [rax + rcx*8]
    0x48, 0x05, 0xc0, 0x01, 0x00, 0x00};        // add rax, 0x1c0
const std::uint8_t kAfter[3] = {0x48, 0x8b, 0xc8};   // mov rcx, rax

constexpr std::uint32_t kSites[2] = {kRvaSiteFoliage, kRvaSiteSecond};

std::uint8_t* g_stub[2] = {nullptr, nullptr};
std::uint8_t g_saved[2][kSiteBytes];
bool g_patched[2] = {false, false};

constexpr std::size_t kSeenMax = 64;
std::mutex g_seen_mutex;
ChunkSeen g_seen[kSeenMax];
std::atomic<std::size_t> g_seen_count{0};

constexpr std::size_t kSampleMax = 24;
std::mutex g_sample_mutex;
ElementSample g_samples[kSampleMax];
std::atomic<std::size_t> g_sample_count{0};

void note_element(const std::uint8_t* el, int kind, int lod) {
    if (g_sample_count.load(std::memory_order_relaxed) >= kSampleMax) return;
    std::lock_guard<std::mutex> lock(g_sample_mutex);
    const std::size_t n = g_sample_count.load();
    if (n >= kSampleMax) return;
    const auto address = reinterpret_cast<std::uintptr_t>(el);
    for (std::size_t i = 0; i < n; ++i)
        if (g_samples[i].address == address) return;
    ElementSample& e = g_samples[n];
    e.address = address;
    e.kind = kind;
    e.lod = lod;
    std::memcpy(e.q, el, sizeof(e.q));
    g_sample_count.store(n + 1);
}

void note_chunk(int cx, int cz, int kind, int lod, int drawn) {
    if (g_seen_count.load(std::memory_order_relaxed) >= kSeenMax) return;
    std::lock_guard<std::mutex> lock(g_seen_mutex);
    const std::size_t n = g_seen_count.load();
    for (std::size_t i = 0; i < n; ++i)
        if (g_seen[i].cx == cx && g_seen[i].cz == cz && g_seen[i].kind == kind) return;
    if (n >= kSeenMax) return;
    g_seen[n] = ChunkSeen{cx, cz, kind, lod, drawn};
    g_seen_count.store(n + 1);
}

extern "C" std::uintptr_t sw_trees_choose_slot(const std::uint8_t* el, std::uintptr_t library) {
    std::int32_t kind = 0, lod = 0, cx = 0, cz = 0;
    std::memcpy(&kind, el + kOffElementKind, 4);
    std::memcpy(&lod, el + kOffElementLod, 4);
    std::memcpy(&cx, el + kOffElementChunkX, 4);
    std::memcpy(&cz, el + kOffElementChunkZ, 4);
    std::int32_t drawn = kind;
    if (kind >= 0 && kind <= 2 && lod >= 0 && lod <= 2) {
        drawn = static_cast<std::int32_t>(resolve(static_cast<Kind>(kind), cx, cz));
        note_chunk(cx, cz, kind, lod, drawn);
        note_element(el, kind, lod);
    }
    return library + kLibrarySlotsOffset + (static_cast<std::uintptr_t>(drawn) * 3 + static_cast<std::uintptr_t>(lod)) * 8;
}

std::uint8_t* alloc_near(std::uintptr_t base) {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const std::uintptr_t gran = si.dwAllocationGranularity;
    for (std::uintptr_t at = base + 0x10000000; at < base + 0x60000000; at += gran * 16) {
        void* p = VirtualAlloc(reinterpret_cast<void*>(at), 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (p) return static_cast<std::uint8_t*>(p);
    }
    return nullptr;
}

bool same_bytes(const void* at, const std::uint8_t* want, std::size_t n) {
    return sw::Runtime::get().readable(at, n) && std::memcmp(at, want, n) == 0;
}

bool fits_rel32(std::intptr_t rel) {
    return rel >= INT32_MIN && rel <= INT32_MAX;
}

bool patch_site(int i) {
    auto& rt = sw::Runtime::get();
    auto* site = const_cast<std::uint8_t*>(static_cast<const std::uint8_t*>(rt.at_rva(kSites[i])));
    if (!site || !same_bytes(site, kOriginal, kSiteBytes) || !same_bytes(site + kSiteBytes, kAfter, sizeof(kAfter)))
        return false;
    std::uint8_t* stub = alloc_near(rt.base());
    if (!stub) return false;

    std::uint8_t* p = stub;
    *p++ = 0x49; *p++ = 0x8b; *p++ = 0xcf;                  // mov rcx, r15
    *p++ = 0x48; *p++ = 0x8b; *p++ = 0x55; *p++ = 0xf0;     // mov rdx, [rbp - 0x10]
    *p++ = 0x48; *p++ = 0xb8;                               // mov rax, imm64
    const auto fn = reinterpret_cast<std::uintptr_t>(&sw_trees_choose_slot);
    std::memcpy(p, &fn, 8);
    p += 8;
    *p++ = 0xff; *p++ = 0xd0;                               // call rax
    *p++ = 0xe9;                                            // jmp to the instruction after the 35 bytes
    const auto resume = reinterpret_cast<std::intptr_t>(site) + static_cast<std::intptr_t>(kSiteBytes);
    const auto back = resume - (reinterpret_cast<std::intptr_t>(p) + 4);
    const auto jump = reinterpret_cast<std::intptr_t>(stub) - (reinterpret_cast<std::intptr_t>(site) + 5);
    if (!fits_rel32(back) || !fits_rel32(jump)) {
        VirtualFree(stub, 0, MEM_RELEASE);
        return false;
    }
    const auto back32 = static_cast<std::int32_t>(back);
    std::memcpy(p, &back32, 4);
    FlushInstructionCache(GetCurrentProcess(), stub, 64);

    std::memcpy(g_saved[i], site, kSiteBytes);
    std::uint8_t patch[kSiteBytes];
    std::memset(patch, 0x90, sizeof(patch));
    patch[0] = 0xe9;
    const auto jump32 = static_cast<std::int32_t>(jump);
    std::memcpy(patch + 1, &jump32, 4);
    if (!rt.write_bytes(site, patch, sizeof(patch))) {
        VirtualFree(stub, 0, MEM_RELEASE);
        return false;
    }
    g_stub[i] = stub;
    g_patched[i] = true;
    return true;
}

}  // namespace

bool watch() {
    if (!sw::Runtime::get().attached()) return false;
    if (g_patched[0] && g_patched[1]) return true;
    for (int i = 0; i < 2; ++i) {
        if (g_patched[i]) continue;
        if (!patch_site(i)) {
            unwatch();   // both places or none: the second pass would draw another kind than the first
            return false;
        }
    }
    return true;
}

void unwatch() {
    auto& rt = sw::Runtime::get();
    for (int i = 0; i < 2; ++i) {
        if (!g_patched[i]) continue;
        auto* site = const_cast<std::uint8_t*>(static_cast<const std::uint8_t*>(rt.at_rva(kSites[i])));
        if (site) rt.write_bytes(site, g_saved[i], kSiteBytes);
        g_patched[i] = false;   // the stub stays allocated
    }
}

bool active() { return g_patched[0] && g_patched[1]; }

std::size_t element_samples(ElementSample* out, std::size_t max) {
    std::lock_guard<std::mutex> lock(g_sample_mutex);
    const std::size_t n = g_sample_count.load() < max ? g_sample_count.load() : max;
    for (std::size_t i = 0; i < n && out; ++i) out[i] = g_samples[i];
    return n;
}

std::size_t chunk_seen(ChunkSeen* out, std::size_t max) {
    std::lock_guard<std::mutex> lock(g_seen_mutex);
    const std::size_t n = g_seen_count.load() < max ? g_seen_count.load() : max;
    for (std::size_t i = 0; i < n && out; ++i) out[i] = g_seen[i];
    return n;
}

#else  // 32 bit build: only the rule table

bool watch() { return false; }
void unwatch() {}
bool active() { return false; }
std::size_t chunk_seen(ChunkSeen*, std::size_t) { return 0; }
std::size_t element_samples(ElementSample*, std::size_t) { return 0; }

#endif

}  // namespace sw::trees
