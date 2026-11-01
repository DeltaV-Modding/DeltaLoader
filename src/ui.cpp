#include "sw/ui.hpp"

#include "sw/hook.hpp"
#include "sw/mod.hpp"
#include "sw/protos.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>

namespace sw::ui::main_menu {

#if defined(_WIN64)

namespace {

constexpr std::size_t kOffParent = 0x20;
constexpr std::size_t kOffChildList = 0x30;
constexpr std::size_t kOffAction = 0x90;
constexpr std::size_t kOffCallback = 0xd8;
constexpr std::size_t kOffCbUser = 0xe0;
constexpr std::size_t kViewButtonFields[] = {0x80, 0x70, 0x78, 0x98, 0x88, 0x90};
constexpr std::size_t kSlotInit = 1;
constexpr std::size_t kSlotAttach = 29;
constexpr std::uint32_t kVtablePointButton = 0xaf2af0;
constexpr std::uint32_t kVtableText = 0xbac908;   // c_ui_element_text
constexpr float kArg9 = 0.42f;
constexpr float kArg10 = 0.14f;

using sw::protos::MmString;
using ElementFn = void (*)(void* self, void* arg);

struct Item {
    std::string text;
    std::string icon;
    Anchor anchor;
    float x, y;
    ClickCallback callback;
    void* user;
    std::uintptr_t element = 0;   // built in the current view, 0 = not yet
    bool is_text = false;         // a text element, not a button
    float width = 0.8f;
    float height = 0.14f;
    std::uint32_t color = 0xFFE6E6E6u;
};

std::mutex g_mutex;
std::deque<Item> g_items;   // deque: the address of an item stays stable
std::atomic<std::uintptr_t> g_ctx{0};
std::uintptr_t g_view = 0;
std::uintptr_t g_buttonVtable = 0;
bool g_ready = false;
bool g_gave_up = false;

template <typename T>
bool read_value(std::uintptr_t address, T& value) {
    if (!address || !sw::mod::rt().readable(reinterpret_cast<const void*>(address), sizeof(T)))
        return false;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return true;
}

template <typename T>
void write_value(std::uintptr_t address, T value) {
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(T));
}

bool has_vtable(std::uintptr_t object, std::uintptr_t vtable) {
    std::uintptr_t v = 0;
    return read_value(object, v) && v == vtable;
}

ElementFn slot(std::uintptr_t object, std::size_t index) {
    std::uintptr_t vtable = 0, fn = 0;
    if (!read_value(object, vtable) || !read_value(vtable + index * sizeof(void*), fn)) return nullptr;
    return reinterpret_cast<ElementFn>(fn);
}

bool build_string(MmString& target, const char* text) {
    const auto length = static_cast<std::uint32_t>(std::strlen(text));
    target = MmString{nullptr, 0, 0};
    sw::protos::StringReserve(&target, length + 1);
    if (!target.data) return false;
    std::memcpy(target.data, text, length);
    target.data[length] = '\0';
    target.size = length;
    return true;
}

// The game calls this with the input and the user pointer of the button.
void trampoline(void* /*input*/, void* user) {
    const auto* item = static_cast<const Item*>(user);
    if (item && item->callback) item->callback(item->user);
}

void give_up(const char* reason) {
    sw::mod::log("ui: %s - no more menu elements.", reason);
    g_gave_up = true;
}

bool build_text(Item& item, std::uintptr_t ctx, std::uintptr_t parent) {
    MmString text{};
    if (!build_string(text, item.text.c_str())) {
        give_up("the allocator of the game returned no buffer");
        return false;
    }
    void* memory = sw::protos::OperatorNew(sw::protos::kTextSize);
    if (!memory) {
        give_up("operator new returned nothing");
        return false;
    }
    const auto t = reinterpret_cast<std::uintptr_t>(
        sw::protos::Text(memory, item.x, item.y, item.width, item.height,
                         static_cast<std::uint32_t>(item.anchor), &text, 4, item.color, 0.0005f,
                         0.8f, 0));
    if (!has_vtable(t, reinterpret_cast<std::uintptr_t>(sw::mod::rt().at_rva(kVtableText)))) {
        give_up("the text element has an unexpected vtable");
        return false;
    }
    const ElementFn init_fn = slot(t, kSlotInit);
    const ElementFn attach_fn = slot(t, kSlotAttach);
    if (!init_fn || !attach_fn) {
        give_up("vtable slots not readable");
        return false;
    }
    sw::protos::AddChild(reinterpret_cast<void*>(parent + kOffChildList), reinterpret_cast<void*>(t));
    init_fn(reinterpret_cast<void*>(t), reinterpret_cast<void*>(ctx));
    write_value(t + kOffParent, parent);
    attach_fn(reinterpret_cast<void*>(t), reinterpret_cast<void*>(parent));
    item.element = t;
    return true;
}

bool build(Item& item, std::uintptr_t ctx, std::uintptr_t parent) {
    if (item.is_text) return build_text(item, ctx, parent);
    MmString icon{}, text{};
    if (!build_string(icon, item.icon.c_str()) || !build_string(text, item.text.c_str())) {
        give_up("the allocator of the game returned no buffer");
        return false;
    }
    const auto k = reinterpret_cast<std::uintptr_t>(sw::protos::ButtonFactory(
        reinterpret_cast<void*>(ctx), item.x, item.y, static_cast<std::uint32_t>(item.anchor),
        &icon, &text, 1, 0, kArg9, kArg10));
    std::uint32_t action = 1;
    if (!has_vtable(k, g_buttonVtable) || !read_value(k + kOffAction, action) || action != 0) {
        give_up("the factory did not return a plain button");
        return false;
    }
    const ElementFn init_fn = slot(k, kSlotInit);
    const ElementFn attach_fn = slot(k, kSlotAttach);
    if (!init_fn || !attach_fn) {
        give_up("vtable slots not readable");
        return false;
    }
    sw::protos::AddChild(reinterpret_cast<void*>(parent + kOffChildList), reinterpret_cast<void*>(k));
    init_fn(reinterpret_cast<void*>(k), reinterpret_cast<void*>(ctx));
    write_value(k + kOffParent, parent);
    attach_fn(reinterpret_cast<void*>(k), reinterpret_cast<void*>(parent));
    if (item.callback) {
        write_value(k + kOffCbUser, reinterpret_cast<std::uintptr_t>(&item));
        write_value(k + kOffCallback, reinterpret_cast<std::uintptr_t>(&trampoline));
    }
    item.element = k;
    return true;
}

void on_build(void*, void*) {
    const auto ctx = reinterpret_cast<std::uintptr_t>(sw::Hook::arg2());
    if (ctx) g_ctx.store(ctx, std::memory_order_relaxed);
}

void on_view_tick(void* self, void*) {
    const auto view = reinterpret_cast<std::uintptr_t>(self);
    std::unique_lock<std::mutex> lock(g_mutex, std::try_to_lock);
    if (!lock.owns_lock()) return;   // never block the engine
    if (g_view != view) {
        g_view = view;               // a new menu: the old elements are gone
        for (auto& item : g_items) item.element = 0;
    }
    const std::uintptr_t ctx = g_ctx.load(std::memory_order_relaxed);
    if (g_gave_up || !ctx) return;

    std::uintptr_t model = 0;
    for (std::size_t off : kViewButtonFields) {
        std::uintptr_t candidate = 0;
        if (read_value(view + off, candidate) && has_vtable(candidate, g_buttonVtable)) {
            model = candidate;
            break;
        }
    }
    if (!model) return;   // the menu is not finished yet
    std::uintptr_t column = 0;
    if (!read_value(model + kOffParent, column) || !column) return;

    for (auto& item : g_items) {
        if (item.element) continue;
        if (!build(item, ctx, column)) return;
    }
}

// Called with g_mutex held.
bool ensure_ready() {
    if (g_ready) return true;
    if (g_gave_up) return false;
    auto& rt = sw::mod::rt();
    if (!sw::protos::bind(rt)) {
        give_up("the game functions do not match the mapping");
        return false;
    }
    g_buttonVtable = reinterpret_cast<std::uintptr_t>(rt.at_rva(kVtablePointButton));
    std::string error;
    const std::uint32_t build_rvas[] = {sw::protos::C_ui_main_menu_default_view_build::kRva,
                                        sw::protos::C_ui_main_menu_interface_build::kRva};
    for (std::uint32_t rva : build_rvas) {
        if (rt.hook_rva(rva, &on_build, nullptr, error) != sw::HookStatus::ok)
            sw::mod::log("ui: build hook 0x%x not set: %s", rva, error.c_str());
    }
    if (rt.hook_rva(sw::protos::C_ui_main_menu_default_view_tick::kRva, &on_view_tick, nullptr,
                    error) != sw::HookStatus::ok) {
        give_up("the menu tick could not be hooked");
        return false;
    }
    g_ready = true;
    return true;
}

bool add(const char* text, const char* icon, Anchor anchor, float x, float y,
         ClickCallback callback, void* user) {
    if (!text || !icon) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ensure_ready()) return false;
    g_items.push_back(Item{text, icon, anchor, x, y, callback, user, 0});
    return true;
}

}  // namespace

bool add_text(const char* text, Anchor anchor, float x, float y, float width, float height,
              unsigned color) {
    if (!add(text, "", anchor, x, y, nullptr, nullptr)) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    Item& item = g_items.back();
    item.is_text = true;
    item.width = width;
    item.height = height;
    item.color = color;
    return true;
}

bool add_button(const char* text, const char* icon, Anchor anchor, ClickCallback callback,
                void* user, float x, float y) {
    return add(text, icon, anchor, x, y, callback, user);
}

void clear() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_ready) {
        for (auto& item : g_items) {
            if (item.element && has_vtable(item.element, g_buttonVtable))
                write_value(item.element + kOffCallback, std::uintptr_t{0});
        }
    }
    g_items.clear();
    g_view = 0;
    g_ready = false;   // the hooks are removed by the skeleton; bind again next time
}

std::size_t count() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_items.size();
}

#else  // !_WIN64

bool add_text(const char*, Anchor, float, float, float, float, unsigned) { return false; }
bool add_button(const char*, const char*, Anchor, ClickCallback, void*, float, float) {
    return false;
}
void clear() {}
std::size_t count() { return 0; }

#endif

}  // namespace sw::ui::main_menu
