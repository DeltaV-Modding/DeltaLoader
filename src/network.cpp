#include "sw/network.hpp"

#include "sw/hook.hpp"
#include "sw/runtime.hpp"

#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

namespace sw::net {

namespace {

struct Entry {
    const MessageInfo* info;
    MessageCallback message;
    HandledCallback handled;
    void* user;
};

std::mutex g_mutex;
std::vector<Entry> g_entries;
std::map<std::uint32_t, bool> g_hooked_serializers;   // rva -> installed
std::map<std::uint32_t, bool> g_hooked_handlers;
std::atomic<std::uint64_t> g_seen[5][1024];
bool g_watching = false;

bool same(const MessageInfo* a, const MessageInfo* b) { return a == b; }

#if defined(_WIN64)
template <typename T>
bool read_at(std::uintptr_t address, T& value) {
    if (!address || !sw::Runtime::get().readable(reinterpret_cast<const void*>(address), sizeof(T))) return false;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return true;
}

// Inside a serialiser: (message, stream).
void on_serialize(void* self, void*) {
    const MessageInfo* info = identify(self);
    if (!info) return;
    void* stream = reinterpret_cast<void*>(sw::Hook::arg(1));
    std::uint8_t flag = 0;
    const bool writing = read_at(reinterpret_cast<std::uintptr_t>(stream) + 0x14, flag) && flag == 1;
    g_seen[static_cast<int>(info->family)][info->id & 1023].fetch_add(1, std::memory_order_relaxed);
    std::vector<Entry> copy;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        copy = g_entries;
    }
    for (const Entry& e : copy)
        if (e.message && same(e.info, info)) e.message(*info, self, stream, writing, e.user);
}

// Inside a handler: the message is one of the first three arguments.
void on_handler(void*, void*) {
    for (unsigned i = 0; i < 3; ++i) {
        void* arg = reinterpret_cast<void*>(sw::Hook::arg(i));
        const MessageInfo* info = identify(arg);
        if (!info) continue;
        std::vector<Entry> copy;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            copy = g_entries;
        }
        for (const Entry& e : copy)
            if (e.handled && same(e.info, info)) e.handled(*info, arg, e.user);
        return;
    }
}
#endif

}  // namespace

const MessageInfo* find(Family family, std::uint16_t id) {
    if (family == Family::net_message) {
        for (const auto& n : kNetMessageIds)
            if (n.id == id)
                for (std::size_t i = 0; i < kMessageCount; ++i)
                    if (kMessages[i].family == family && kMessages[i].vtable == n.vtable) return &kMessages[i];
        return nullptr;
    }
    for (std::size_t i = 0; i < kMessageCount; ++i)
        if (kMessages[i].family == family && kMessages[i].id == id && (id != 0 || family == Family::net_message)) return &kMessages[i];
    return nullptr;
}

std::uint8_t net_id(const MessageInfo& info) {
    if (info.family != Family::net_message) return 0;
    for (const auto& n : kNetMessageIds) if (n.vtable == info.vtable) return n.id;
    return 0;
}

const MessageInfo* find(Family family, const char* name) {
    if (!name) return nullptr;
    for (std::size_t i = 0; i < kMessageCount; ++i)
        if (kMessages[i].family == family && std::strcmp(kMessages[i].name, name) == 0) return &kMessages[i];
    return nullptr;
}

const MessageInfo* find_class(const char* class_name) {
    if (!class_name) return nullptr;
    for (std::size_t i = 0; i < kMessageCount; ++i)
        if (std::strcmp(kMessages[i].class_name, class_name) == 0) return &kMessages[i];
    return nullptr;
}

std::vector<const MessageInfo*> messages(Family family) {
    std::vector<const MessageInfo*> out;
    for (std::size_t i = 0; i < kMessageCount; ++i)
        if (kMessages[i].family == family) out.push_back(&kMessages[i]);
    return out;
}

std::size_t count(Family family) { return messages(family).size(); }

const char* to_string(Family f) {
    switch (f) {
        case Family::s2c_event: return "s2c_event";
        case Family::c2s_request: return "c2s_request";
        case Family::game_to_logic: return "game_to_logic";
        case Family::logic_to_game: return "logic_to_game";
        case Family::net_message: return "net_message";
    }
    return "?";
}

#if defined(_WIN64)

const MessageInfo* identify(const void* object) {
    auto& rt = sw::Runtime::get();
    std::uintptr_t vtable = 0;
    if (!object || !rt.attached() || !read_at(reinterpret_cast<std::uintptr_t>(object), vtable)) return nullptr;
    const auto base = rt.base();
    if (vtable < base) return nullptr;
    const std::uint64_t rva = vtable - base;
    for (std::size_t i = 0; i < kMessageCount; ++i)
        if (kMessages[i].vtable == rva) return &kMessages[i];
    return nullptr;
}

std::uint32_t read_id(const void* object) {
    std::uint32_t id = 0;
    return read_at(reinterpret_cast<std::uintptr_t>(object) + 8, id) ? id : 0;
}

bool watch() {
    auto& rt = sw::Runtime::get();
    if (!rt.attached()) return false;
    g_watching = true;
    return true;
}

bool on_message(Family family, std::uint16_t id, MessageCallback callback, void* user) {
    const MessageInfo* info = find(family, id);
    if (!g_watching || !info || !callback || (family != Family::s2c_event && family != Family::c2s_request && family != Family::net_message)) return false;
    std::string error;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_hooked_serializers.count(info->slot0)) {
            if (sw::Runtime::get().hook_rva(info->slot0, &on_serialize, nullptr, error) != sw::HookStatus::ok) return false;
            g_hooked_serializers[info->slot0] = true;
        }
        for (const Entry& e : g_entries)
            if (e.info == info && e.message == callback && e.user == user) return false;
        g_entries.push_back({info, callback, nullptr, user});
    }
    return true;
}

bool on_handled(Family family, std::uint16_t id, HandledCallback callback, void* user) {
    const MessageInfo* info = find(family, id);
    if (!g_watching || !info || !callback || !info->handler_hint) return false;
    std::string error;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_hooked_handlers.count(info->handler_hint)) {
            if (sw::Runtime::get().hook_rva(info->handler_hint, &on_handler, nullptr, error) != sw::HookStatus::ok) return false;
            g_hooked_handlers[info->handler_hint] = true;
        }
        for (const Entry& e : g_entries)
            if (e.info == info && e.handled == callback && e.user == user) return false;
        g_entries.push_back({info, nullptr, callback, user});
    }
    return true;
}

#else

const MessageInfo* identify(const void*) { return nullptr; }
std::uint32_t read_id(const void*) { return 0; }
bool watch() { return false; }
bool on_message(Family, std::uint16_t, MessageCallback, void*) { return false; }
bool on_handled(Family, std::uint16_t, HandledCallback, void*) { return false; }

#endif

std::vector<const FieldInfo*> fields(const MessageInfo& info) {
    std::vector<const FieldInfo*> out;
    for (std::size_t i = 0; i < kFieldCount; ++i)
        if (kFields[i].family == info.family && kFields[i].id == info.id) out.push_back(&kFields[i]);
    return out;
}

#if defined(_WIN64)
namespace {
template <typename T>
bool read_field(const void* message, std::uint16_t offset, T& out) {
    return message && read_at(reinterpret_cast<std::uintptr_t>(message) + offset, out);
}
}  // namespace

bool read_u8(const void* m, std::uint16_t o, std::uint8_t& v) { return read_field(m, o, v); }
bool read_u16(const void* m, std::uint16_t o, std::uint16_t& v) { return read_field(m, o, v); }
bool read_u32(const void* m, std::uint16_t o, std::uint32_t& v) { return read_field(m, o, v); }
bool read_u64(const void* m, std::uint16_t o, std::uint64_t& v) { return read_field(m, o, v); }
bool read_f32(const void* m, std::uint16_t o, float& v) { return read_field(m, o, v); }
bool read_f64(const void* m, std::uint16_t o, double& v) { return read_field(m, o, v); }

bool read_string(const void* m, std::uint16_t o, std::string& out) {
    // mm_string: { char* data; std::uint32_t size; std::uint32_t capacity }
    struct S { const char* data; std::uint32_t size; std::uint32_t capacity; } s{};
    if (!read_field(m, o, s) || !s.data || s.size > 0x10000 || !sw::Runtime::get().readable(s.data, s.size)) return false;
    out.assign(s.data, s.size);
    return true;
}

bool read_vec3d(const void* m, std::uint16_t o, double out[3]) {
    double v[3] = {};
    if (!m || !sw::Runtime::get().readable(reinterpret_cast<const char*>(m) + o, sizeof(v))) return false;
    std::memcpy(v, reinterpret_cast<const char*>(m) + o, sizeof(v));
    out[0] = v[0];
    out[1] = v[1];
    out[2] = v[2];
    return true;
}
#else
bool read_u8(const void*, std::uint16_t, std::uint8_t&) { return false; }
bool read_u16(const void*, std::uint16_t, std::uint16_t&) { return false; }
bool read_u32(const void*, std::uint16_t, std::uint32_t&) { return false; }
bool read_u64(const void*, std::uint16_t, std::uint64_t&) { return false; }
bool read_f32(const void*, std::uint16_t, float&) { return false; }
bool read_f64(const void*, std::uint16_t, double&) { return false; }
bool read_string(const void*, std::uint16_t, std::string&) { return false; }
bool read_vec3d(const void*, std::uint16_t, double[3]) { return false; }
#endif

std::uint64_t seen(Family family, std::uint16_t id) { return g_seen[static_cast<int>(family)][id & 1023].load(); }

void unwatch() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_entries.clear();
}

}  // namespace sw::net
