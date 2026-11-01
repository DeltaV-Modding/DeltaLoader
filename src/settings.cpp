#include "sw/settings.hpp"

#include "sw/generated_settings_map.hpp"

#if defined(_WIN64)

#include "sw/protos.hpp"
#include "sw/runtime.hpp"

#include <cstring>

namespace sw::settings {

namespace {

// The state of the server. 0 if the game is not attached.
std::uintptr_t state() {
    if (!sw::protos::GameSettings) return 0;
    return sw::protos::GameSettings();
}

bool read_at(std::uintptr_t address, void* out, std::size_t size) {
    if (!address) return false;
    if (!sw::Runtime::get().readable(reinterpret_cast<const void*>(address), size)) return false;
    std::memcpy(out, reinterpret_cast<const void*>(address), size);
    return true;
}

bool write_at(std::uintptr_t address, const void* in, std::size_t size) {
    if (!address) return false;
    return sw::Runtime::get().write_bytes(reinterpret_cast<void*>(address), in, size);
}

}  // namespace

std::vector<std::string> names() {
    std::vector<std::string> out;
    out.reserve(sw::map::kSettingVariables.size());
    for (const auto& v : sw::map::kSettingVariables) out.emplace_back(v.name);
    return out;
}

const Variable* find(std::string_view name) {
    for (const auto& v : sw::map::kSettingVariables) {
        if (v.name == name) {
            static thread_local Variable cache;
            cache = Variable{v.name, v.offset, v.is_bool};
            return &cache;
        }
    }
    return nullptr;
}

bool ready() { return state() != 0; }

bool get_bool(std::string_view name, bool& out) {
    const Variable* v = find(name);
    if (!v || !v->is_bool) return false;
    const std::uintptr_t s = state();
    if (!s) return false;
    std::uint8_t value = 0;
    if (!read_at(s + v->offset, &value, sizeof(value))) return false;
    out = value != 0;
    return true;
}

bool get_number(std::string_view name, double& out) {
    const Variable* v = find(name);
    if (!v || v->is_bool) return false;
    const std::uintptr_t s = state();
    if (!s) return false;
    std::uint32_t value = 0;
    if (!read_at(s + v->offset, &value, sizeof(value))) return false;
    out = static_cast<double>(value);
    return true;
}

bool set_bool(std::string_view name, bool value) {
    const Variable* v = find(name);
    if (!v || !v->is_bool) return false;
    const std::uintptr_t s = state();
    if (!s) return false;
    const std::uint8_t byte = value ? 1 : 0;
    return write_at(s + v->offset, &byte, sizeof(byte));
}

bool set_number(std::string_view name, double value) {
    const Variable* v = find(name);
    if (!v || v->is_bool) return false;
    const std::uintptr_t s = state();
    if (!s) return false;
    const auto integer = static_cast<std::uint32_t>(value);
    return write_at(s + v->offset, &integer, sizeof(integer));
}

}  // namespace sw::settings

#else  // !_WIN64

namespace sw::settings {

std::vector<std::string> names() {
    std::vector<std::string> out;
    out.reserve(sw::map::kSettingVariables.size());
    for (const auto& v : sw::map::kSettingVariables) out.emplace_back(v.name);
    return out;
}

const Variable* find(std::string_view name) {
    for (const auto& v : sw::map::kSettingVariables) {
        if (v.name == name) {
            static thread_local Variable cache;
            cache = Variable{v.name, v.offset, v.is_bool};
            return &cache;
        }
    }
    return nullptr;
}

bool ready() { return false; }
bool get_bool(std::string_view, bool&) { return false; }
bool get_number(std::string_view, double&) { return false; }
bool set_bool(std::string_view, bool) { return false; }
bool set_number(std::string_view, double) { return false; }

}  // namespace sw::settings

#endif
