#include "sw/input.hpp"

#include "sw/hook.hpp"
#include "sw/runtime.hpp"

#include <cstring>
#include <mutex>
#include <string>

namespace sw::input {

// ---- tables ----------------------------------------------------------------------------------------------------
std::vector<const ActionInfo*> actions() {
    std::vector<const ActionInfo*> out;
    for (std::size_t i = 0; i < kActionCount; ++i) out.push_back(&kActions[i]);
    return out;
}

const ActionInfo* find_action(std::uint16_t id) {
    if (id == 0) return &kAction0;
    return (id <= kActionCount) ? &kActions[id - 1] : nullptr;
}

const ActionInfo* find_action(const char* name) {
    if (!name) return nullptr;
    if (std::strcmp(kAction0.name, name) == 0) return &kAction0;
    for (std::size_t i = 0; i < kActionCount; ++i)
        if (std::strcmp(kActions[i].name, name) == 0) return &kActions[i];
    return nullptr;
}

const char* key_name(int game_key) {
    return (game_key >= 0 && game_key < 105) ? kKeyNames[game_key] : "invalid_key";
}

int game_key_from_glfw(int glfw_key) {
    for (std::size_t i = 0; i < kGlfwKeyMapCount; ++i)
        if (kGlfwKeyMap[i].glfw_key == glfw_key) return kGlfwKeyMap[i].game_key;
    return -1;
}

int glfw_key_from_game(int game_key) {
    for (std::size_t i = 0; i < kGlfwKeyMapCount; ++i)
        if (kGlfwKeyMap[i].game_key == game_key) return kGlfwKeyMap[i].glfw_key;
    return -1;
}

const char* default_key_name(const ActionInfo& action) {
    if (action.default_key < 0 || action.default_key >= 105) return "";
    return kKeyNames[action.default_key];
}

#if defined(_WIN64)

namespace {

std::mutex g_mutex;
KeyCallback g_key = nullptr;
void* g_key_user = nullptr;
CharCallback g_char = nullptr;
void* g_char_user = nullptr;
MouseButtonCallback g_mouse = nullptr;
void* g_mouse_user = nullptr;
bool g_watching = false;
bool g_hooked_key = false, g_hooked_char = false, g_hooked_mouse = false;

template <typename T>
bool read_at(std::uintptr_t address, T& value) {
    if (!address || !sw::Runtime::get().readable(reinterpret_cast<const void*>(address), sizeof(T))) return false;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return true;
}

void on_key_hook(void* window, void*) {
    KeyCallback cb;
    void* user;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        cb = g_key;
        user = g_key_user;
    }
    if (!cb) return;
    const int key = static_cast<int>(sw::Hook::arg(1));
    const int action = static_cast<int>(sw::Hook::arg(3));
    cb(key, game_key_from_glfw(key), action, window, user);
}

void on_char_hook(void* window, void*) {
    CharCallback cb;
    void* user;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        cb = g_char;
        user = g_char_user;
    }
    if (cb) cb(static_cast<std::uint32_t>(sw::Hook::arg(1)), window, user);
}

void on_mouse_hook(void* window, void*) {
    MouseButtonCallback cb;
    void* user;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        cb = g_mouse;
        user = g_mouse_user;
    }
    if (cb) cb(static_cast<int>(sw::Hook::arg(1)), static_cast<int>(sw::Hook::arg(2)), window, user);
}

constexpr std::uintptr_t kWindowUserPlatform = 0x20;
constexpr std::uintptr_t kWindowKeyState = 0x80;
constexpr std::uintptr_t kPlatformWindow = 0xd38;
constexpr std::uintptr_t kPlatformCursorX = 0xbc8, kPlatformCursorY = 0xbcc, kPlatformMouseDown = 0xbd0;

}  // namespace

bool watch() {
    auto& rt = sw::Runtime::get();
    if (!rt.attached()) return false;
    g_watching = true;
    return true;
}

bool on_key(KeyCallback callback, void* user) {
    if (!g_watching || !callback) return false;
    std::string error;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_hooked_key) {
        if (sw::Runtime::get().hook_rva(kKeyCallbackRva, &on_key_hook, nullptr, error) != sw::HookStatus::ok) return false;
        g_hooked_key = true;
    }
    g_key = callback;
    g_key_user = user;
    return true;
}

bool on_char(CharCallback callback, void* user) {
    if (!g_watching || !callback) return false;
    std::string error;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_hooked_char) {
        if (sw::Runtime::get().hook_rva(kCharCallbackRva, &on_char_hook, nullptr, error) != sw::HookStatus::ok) return false;
        g_hooked_char = true;
    }
    g_char = callback;
    g_char_user = user;
    return true;
}

bool on_mouse_button(MouseButtonCallback callback, void* user) {
    if (!g_watching || !callback) return false;
    std::string error;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_hooked_mouse) {
        if (sw::Runtime::get().hook_rva(kMouseButtonCallbackRva, &on_mouse_hook, nullptr, error) != sw::HookStatus::ok) return false;
        g_hooked_mouse = true;
    }
    g_mouse = callback;
    g_mouse_user = user;
    return true;
}

void unwatch() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_key = nullptr;
    g_char = nullptr;
    g_mouse = nullptr;
    g_watching = false;
    // the hooks stay in place until sw::Hook::remove_all(); with no callback set they do nothing
}

void* glfw_window() {
    auto& rt = sw::Runtime::get();
    if (!rt.attached()) return nullptr;
    std::uintptr_t window = 0, plat = 0, back = 0;
    if (!read_at(rt.base() + kGlfwWindowListHeadRva, window) || !window) return nullptr;
    if (!read_at(window + kWindowUserPlatform, plat) || !plat) return nullptr;
    if (!read_at(plat + kPlatformWindow, back) || back != window) return nullptr;
    return reinterpret_cast<void*>(window);
}

void* platform() {
    const auto window = reinterpret_cast<std::uintptr_t>(glfw_window());
    std::uintptr_t p = 0;
    return (window && read_at(window + kWindowUserPlatform, p)) ? reinterpret_cast<void*>(p) : nullptr;
}

bool mouse_down(int button, bool& down) {
    const auto p = reinterpret_cast<std::uintptr_t>(platform());
    std::uint8_t v = 0;
    if (!p || button < 0 || button > 7 || !read_at(p + kPlatformMouseDown + static_cast<unsigned>(button), v)) return false;
    down = v != 0;
    return true;
}

bool cursor_ndc(float& x, float& y) {
    const auto p = reinterpret_cast<std::uintptr_t>(platform());
    return p && read_at(p + kPlatformCursorX, x) && read_at(p + kPlatformCursorY, y);
}

bool glfw_key_state(int glfw_key, std::uint8_t& raw) {
    const auto w = reinterpret_cast<std::uintptr_t>(glfw_window());
    if (!w || glfw_key < 0 || glfw_key > 0x15c) return false;
    return read_at(w + kWindowKeyState + static_cast<unsigned>(glfw_key), raw);
}

namespace {
bool table_base(std::uintptr_t& table) {
    auto& rt = sw::Runtime::get();
    if (!rt.attached()) return false;
    table = rt.base() + kActionTableRva;
    for (std::uint32_t i = 0; i < kActionEntryCount; ++i) {
        std::uint32_t id = 0;
        if (!read_at(table + i * kActionEntrySize + 0x10, id) || id != i) return false;
    }
    return true;
}
}  // namespace

bool binding(const ActionInfo& action, Binding& out) {
    std::uintptr_t table = 0;
    if (!table_base(table)) return false;
    const std::uintptr_t entry = table + static_cast<std::uintptr_t>(action.id) * kActionEntrySize;
    std::uintptr_t data = 0;
    std::uint32_t length = 0;
    if (!read_at(entry, data) || !read_at(entry + 8, length) || !data || length != std::strlen(action.name)) return false;
    char text[64] = {};
    if (length >= sizeof(text) || !sw::Runtime::get().readable(reinterpret_cast<const void*>(data), length + 1)) return false;
    std::memcpy(text, reinterpret_cast<const void*>(data), length);
    if (std::strcmp(text, action.name) != 0) return false;
    std::int32_t v[5] = {};
    if (!read_at(entry + kEntryKey, v[0]) || !read_at(entry + kEntryMouse, v[1]) || !read_at(entry + kEntryPadButton, v[2]) ||
        !read_at(entry + kEntryPadController, v[3]) || !read_at(entry + kEntryPadAxis, v[4]))
        return false;
    out = {v[0], v[1], v[2], v[3], v[4]};
    return true;
}

bool binding(const char* action_name, Binding& out) {
    const ActionInfo* a = find_action(action_name);
    return a && binding(*a, out);
}

const char* binding_key_name(const ActionInfo& action) {
    Binding b{};
    if (!binding(action, b) || b.key < 0 || b.key >= 105) return "";
    return kKeyNames[b.key];
}

bool is_down(const ActionInfo& action, bool& down) {
    Binding b{};
    if (!binding(action, b)) return false;
    bool any = false, readable = false;
    if (b.key >= 0 && b.key < 105) {
        std::uint8_t raw = 0;
        const int g = glfw_key_from_game(b.key);
        if (g >= 0 && glfw_key_state(g, raw)) {
            readable = true;
            any = any || raw != 0;
        }
    }
    if (b.mouse >= 0 && b.mouse <= 7) {
        bool m = false;
        if (mouse_down(b.mouse, m)) {
            readable = true;
            any = any || m;
        }
    }
    if (!readable) return false;
    down = any;
    return true;
}

bool is_down(const char* action_name, bool& down) {
    const ActionInfo* a = find_action(action_name);
    return a && is_down(*a, down);
}

std::vector<const ActionInfo*> actions_for_key(int game_key) {
    std::vector<const ActionInfo*> out;
    if (game_key < 0 || game_key >= 105) return out;
    Binding b{};
    if (binding(kAction0, b) && b.key == game_key) out.push_back(&kAction0);
    for (std::size_t i = 0; i < kActionCount; ++i)
        if (binding(kActions[i], b) && b.key == game_key) out.push_back(&kActions[i]);
    return out;
}

bool queued(Ring ring, std::uint32_t& count) {
    const auto p = reinterpret_cast<std::uintptr_t>(platform());
    if (!p) return false;
    // ring buffer: data@+0 cap@+8 start@+0xc count@+0x10 (u32 each)
    const std::uintptr_t base[4] = {0xc68, 0xc80, 0xc98, 0xd10};
    return read_at(p + base[static_cast<int>(ring)] + 0x10, count);
}

#else  // 32 bit build: only the tables

bool watch() { return false; }
void unwatch() {}
bool on_key(KeyCallback, void*) { return false; }
bool on_char(CharCallback, void*) { return false; }
bool on_mouse_button(MouseButtonCallback, void*) { return false; }
void* glfw_window() { return nullptr; }
void* platform() { return nullptr; }
bool mouse_down(int, bool&) { return false; }
bool cursor_ndc(float&, float&) { return false; }
bool glfw_key_state(int, std::uint8_t&) { return false; }
bool queued(Ring, std::uint32_t&) { return false; }
bool binding(const ActionInfo&, Binding&) { return false; }
bool binding(const char*, Binding&) { return false; }
const char* binding_key_name(const ActionInfo&) { return ""; }
bool is_down(const ActionInfo&, bool&) { return false; }
bool is_down(const char*, bool&) { return false; }
std::vector<const ActionInfo*> actions_for_key(int) { return {}; }

#endif

}  // namespace sw::input
