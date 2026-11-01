#include "sw/audio.hpp"

#include "sw/hook.hpp"
#include "sw/runtime.hpp"

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN64)
#include <windows.h>
#endif

namespace sw::audio {

namespace {

struct Cb {
    PlayCallback fn;
    void* user;
};

std::mutex g_mutex;
std::vector<Cb> g_callbacks;
std::atomic<std::uintptr_t> g_engine{0};
std::atomic<std::uint64_t> g_plays{0};
std::atomic<std::uint64_t> g_updates{0};
std::atomic<long long> g_last_tick{0};
bool g_watching = false;

const AlEntry* find_entry(const char* name) {
    if (!name) return nullptr;
    for (std::size_t i = 0; i < kAlEntryCount; ++i)
        if (std::strcmp(kAlEntries[i].name, name) == 0) return &kAlEntries[i];
    return nullptr;
}

}  // namespace

const AlEntry* al_entry(const char* name) { return find_entry(name); }

int al_uses(const char* name) {
    const AlEntry* e = find_entry(name);
    return e ? e->uses : 0;
}

std::size_t sound_count() { return kSoundPathCount; }
const SoundPath* sound_at(std::size_t index) { return index < kSoundPathCount ? &kSoundPaths[index] : nullptr; }
const SoundPath* find_sound(const char* part) {
    if (!part || !*part) return nullptr;
    for (std::size_t i = 0; i < kSoundPathCount; ++i)
        if (std::strstr(kSoundPaths[i].path, part)) return &kSoundPaths[i];
    return nullptr;
}

#if defined(_WIN64)

namespace {

template <typename T>
bool read_at(std::uintptr_t address, T& value) {
    if (!address || !sw::Runtime::get().readable(reinterpret_cast<const void*>(address), sizeof(T))) return false;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return true;
}

// The opcodes at the start of the two hooked functions (checked before anything is patched).
const std::uint8_t kUpdatePrologue[8] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x10, 0x55};
const std::uint8_t kPlayPrologue[11] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x68, 0x18};

bool prologue_ok(std::uint32_t rva, const std::uint8_t* want, std::size_t n) {
    const void* p = sw::Runtime::get().at_rva(rva);
    return p && sw::Runtime::get().readable(p, n) && std::memcmp(p, want, n) == 0;
}

// The buffer id behind a buffer reference: ref -> {buffer*, count}; buffer* -> ALuint at +0.
std::uint32_t buffer_id_of_ref(std::uintptr_t ref) {
    std::uintptr_t block = 0, buffer = 0;
    std::uint32_t id = 0;
    if (!read_at(ref, block) || !read_at(block, buffer) || !read_at(buffer + kOffBufferId, id)) return 0;
    return id;
}

void on_update(void* self, void*) {
    g_engine.store(reinterpret_cast<std::uintptr_t>(self), std::memory_order_relaxed);
    g_updates.fetch_add(1, std::memory_order_relaxed);
    g_last_tick.store(static_cast<long long>(GetTickCount64()), std::memory_order_relaxed);
}

void on_play_sound(void* self, void*) {
    g_plays.fetch_add(1, std::memory_order_relaxed);
    std::vector<Cb> copy;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_callbacks.empty()) return;
        copy = g_callbacks;
    }
    PlayEvent ev;
    ev.engine = self;
    ev.buffer = buffer_id_of_ref(static_cast<std::uintptr_t>(sw::Hook::arg(1)));
    ev.loop = (sw::Hook::arg(3) & 0xff) != 0;
    for (const Cb& c : copy) c.fn(ev, c.user);
}

}  // namespace

void* al_function(const char* name) {
    const AlEntry* e = find_entry(name);
    auto& rt = sw::Runtime::get();
    if (!e || !rt.attached()) return nullptr;
    void* fn = nullptr;
    if (!rt.read_rva(e->iat_rva, fn)) return nullptr;
    return fn;
}

bool watch() {
    if (g_watching) return true;
    auto& rt = sw::Runtime::get();
    if (!rt.attached()) return false;
    if (!prologue_ok(kRvaEngineUpdate, kUpdatePrologue, sizeof(kUpdatePrologue)) ||
        !prologue_ok(kRvaEnginePlaySound, kPlayPrologue, sizeof(kPlayPrologue)))
        return false;
    std::string error;
    if (rt.hook_rva(kRvaEngineUpdate, &on_update, nullptr, error) != sw::HookStatus::ok) return false;
    if (rt.hook_rva(kRvaEnginePlaySound, &on_play_sound, nullptr, error) != sw::HookStatus::ok) {
        rt.unhook(reinterpret_cast<std::uintptr_t>(rt.at_rva(kRvaEngineUpdate)), &on_update, nullptr, error);
        return false;
    }
    g_watching = true;
    return true;
}

void unwatch() {
    if (!g_watching) return;
    auto& rt = sw::Runtime::get();
    std::string error;
    rt.unhook(reinterpret_cast<std::uintptr_t>(rt.at_rva(kRvaEngineUpdate)), &on_update, nullptr, error);
    rt.unhook(reinterpret_cast<std::uintptr_t>(rt.at_rva(kRvaEnginePlaySound)), &on_play_sound, nullptr, error);
    g_watching = false;
    remove_callbacks();
}

bool ready() { return g_updates.load() != 0 && static_cast<long long>(GetTickCount64()) - g_last_tick.load() < 1000; }

std::vector<SourceInfo> sources() {
    std::vector<SourceInfo> out;
    const std::uintptr_t e = g_engine.load();
    if (!e) return out;
    for (int i = 0; i < kPoolSources; ++i) {
        const std::uintptr_t slot = e + kOffEnginePool + static_cast<std::uintptr_t>(i) * kPoolSlotSize;
        SourceInfo s{};
        std::uint8_t used = 0;
        std::uintptr_t ref = 0;
        if (!read_at(slot + kOffSlotSource, s.source) || !read_at(slot + kOffSlotGain, s.gain) ||
            !read_at(slot + kOffSlotInUse, used) || !read_at(slot + kOffSlotBufferRef, ref))
            return {};
        s.index = i;
        s.in_use = used != 0;
        s.buffer = ref ? buffer_id_of_ref(slot + kOffSlotBufferRef) : 0;
        out.push_back(s);
    }
    return out;
}

bool listener(ListenerPosition& out) {
    const std::uintptr_t e = g_engine.load();
    float v[3] = {};
    if (!e || !read_at(e + kOffEngineListenerPosition, v)) return false;
    out = {v[0], v[1], v[2]};
    return true;
}

bool group_gains(GroupGains& out) {
    const std::uintptr_t e = g_engine.load();
    float v[4] = {};
    if (!e || !read_at(e + kOffEngineMasterGain, v[0]) || !read_at(e + kOffEngineEmitterGain, v[1]) ||
        !read_at(e + kOffEngineStreamGain, v[2]) || !read_at(e + kOffEngineGroup4Gain, v[3]))
        return false;
    out = {v[0], v[1], v[2], v[3]};
    return true;
}

bool music_mood(Mood& mood, int* playing_kind) {
    const void* base = sw::Runtime::get().at_rva(kRvaMusicState);
    if (!base) return false;
    const std::uintptr_t a = reinterpret_cast<std::uintptr_t>(base);
    std::int32_t first = 0, value = 0, kind = 0;
    if (!read_at(a, first) || first != 60 || !read_at(a + kOffMoodValue, value) || !read_at(a + kOffMusicPlayingKind, kind)) return false;
    if (value < 0 || value > 4) return false;
    mood = static_cast<Mood>(value);
    if (playing_kind) *playing_kind = kind;
    return true;
}

std::vector<CachedSound> library_cache(const void* library) {
    std::vector<CachedSound> out;
    const std::uintptr_t lib = reinterpret_cast<std::uintptr_t>(library);
    std::uintptr_t data = 0;
    std::uint32_t cap = 0, start = 0, count = 0;
    if (!lib || !read_at(lib + kOffLibraryCache, data) || !read_at(lib + kOffLibraryCache + 8, cap) ||
        !read_at(lib + kOffLibraryCache + 12, start) || !read_at(lib + kOffLibraryCache + 16, count))
        return out;
    if (!data || cap == 0 || cap > 100000 || count > cap || start >= cap) return out;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uintptr_t entry = data + static_cast<std::uintptr_t>((start + i) % cap) * 0x18;
        std::uintptr_t name = 0;
        std::int32_t len = 0;
        if (!read_at(entry, name) || !read_at(entry + 8, len) || !name || len <= 0 || len > 260) continue;
        if (!sw::Runtime::get().readable(reinterpret_cast<const void*>(name), static_cast<std::size_t>(len))) continue;
        CachedSound s;
        s.name.assign(reinterpret_cast<const char*>(name), static_cast<std::size_t>(len));
        s.buffer = buffer_id_of_ref(entry + 0x10);
        out.push_back(std::move(s));
    }
    return out;
}

#else

void* al_function(const char*) { return nullptr; }
bool watch() { return false; }
void unwatch() {}
bool ready() { return false; }
std::vector<SourceInfo> sources() { return {}; }
bool listener(ListenerPosition&) { return false; }
bool group_gains(GroupGains&) { return false; }
bool music_mood(Mood&, int*) { return false; }
std::vector<CachedSound> library_cache(const void*) { return {}; }

#endif

std::uint32_t library_buffer(const void* library, const char* path) {
    if (!path) return 0;
    for (const CachedSound& s : library_cache(library))
        if (s.name == path) return s.buffer;
    return 0;
}

const char* mood_name(int value) {
    switch (value) {
        case 0: return "none";
        case 1: return "main_menu";
        case 2: return "mood_normal";
        case 3: return "mood_mission_mid";
        case 4: return "mood_mission_high";
        default: return nullptr;
    }
}

void* engine() { return reinterpret_cast<void*>(g_engine.load()); }

int sources_in_use() {
    int n = 0;
    for (const SourceInfo& s : sources())
        if (s.in_use) ++n;
    return n;
}

bool on_play(PlayCallback callback, void* user) {
    if (!g_watching || !callback) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const Cb& c : g_callbacks)
        if (c.fn == callback && c.user == user) return false;
    g_callbacks.push_back({callback, user});
    return true;
}

void remove_callbacks() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_callbacks.clear();
}

std::uint64_t plays() { return g_plays.load(); }
std::uint64_t updates() { return g_updates.load(); }

}  // namespace sw::audio
