#include "sw/render.hpp"

#include "sw/hook.hpp"
#include "sw/runtime.hpp"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace sw::render {

namespace {

struct Cb {
    FrameCallback fn;
    void* user;
};

std::mutex g_mutex;
std::vector<Cb> g_callbacks;
std::atomic<std::uint64_t> g_frames{0};
std::atomic<std::uintptr_t> g_window{0};
std::atomic<long long> g_last_qpc{0};
std::atomic<long long> g_last_tick{0};
std::atomic<double> g_frame_ms{0};
bool g_watching = false;

const GlEntry* find_entry(const char* name) {
    if (!name) return nullptr;
    for (std::size_t i = 0; i < kGlEntryCount; ++i)
        if (std::strcmp(kGlEntries[i].name, name) == 0) return &kGlEntries[i];
    return nullptr;
}

std::mutex g_target_mutex;
std::vector<std::uintptr_t> g_targets;
std::atomic<std::uintptr_t> g_graphics{0};
std::atomic<std::uintptr_t> g_renderer{0};
bool g_watching_renderer = false;
bool g_watching_targets = false;
bool g_watching_view = false;

#if defined(_WIN64)
// The hook on the swap of the window: count the frame, call the callbacks, then the game swaps.
void on_swap(void* self, void*) {
    g_window.store(reinterpret_cast<std::uintptr_t>(self), std::memory_order_relaxed);
    g_frames.fetch_add(1, std::memory_order_relaxed);
    g_last_tick.store(static_cast<long long>(GetTickCount64()), std::memory_order_relaxed);
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    const long long prev = g_last_qpc.exchange(now.QuadPart);
    if (prev) g_frame_ms.store(static_cast<double>(now.QuadPart - prev) * 1000.0 / static_cast<double>(freq.QuadPart));
    std::vector<Cb> copy;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        copy = g_callbacks;
    }
    for (const Cb& c : copy) c.fn(self, c.user);
}

template <typename T>
bool read_at(std::uintptr_t address, T& value) {
    if (!address || !sw::Runtime::get().readable(reinterpret_cast<const void*>(address), sizeof(T))) return false;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return true;
}

int kind_of_vtable(std::uintptr_t vtable) {
    auto& rt = sw::Runtime::get();
    const std::uint32_t list[4] = {kVtableTarget, kVtableTargetMultiple, kVtableTargetMultisample, kVtableTargetMipmapped};
    for (int i = 0; i < 4; ++i)
        if (vtable == reinterpret_cast<std::uintptr_t>(rt.at_rva(list[i]))) return i;
    return -1;
}

void on_target_create(void* self, void*) {
    const auto a = reinterpret_cast<std::uintptr_t>(self);
    std::uintptr_t vtable = 0;
    if (!read_at(a, vtable) || kind_of_vtable(vtable) < 0) return;
    std::lock_guard<std::mutex> lock(g_target_mutex);
    for (std::uintptr_t x : g_targets)
        if (x == a) return;
    g_targets.push_back(a);
}

void on_target_delete(void* self, void*) {
    const auto a = reinterpret_cast<std::uintptr_t>(self);
    std::lock_guard<std::mutex> lock(g_target_mutex);
    for (std::size_t i = 0; i < g_targets.size(); ++i)
        if (g_targets[i] == a) {
            g_targets.erase(g_targets.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
}

void on_view_update(void* self, void*) { g_graphics.store(reinterpret_cast<std::uintptr_t>(self)); }
void on_frame_function(void* self, void*) { g_renderer.store(reinterpret_cast<std::uintptr_t>(self), std::memory_order_relaxed); }
#endif

}  // namespace

#if defined(_WIN64)

bool read_target(std::uintptr_t address, TargetInfo& out) {
    std::uintptr_t vtable = 0;
    if (!read_at(address, vtable)) return false;
    const int k = kind_of_vtable(vtable);
    if (k < 0) return false;
    TargetInfo t;
    t.address = address;
    t.kind = static_cast<TargetKind>(k);
    std::int32_t v = 0;
    if (!read_at(address + kOffTargetFbo, t.fbo) || !read_at(address + kOffTargetWidth, t.width) ||
        !read_at(address + kOffTargetHeight, t.height))
        return false;
    if (t.kind == TargetKind::multiple) {
        std::uintptr_t ring = 0;
        std::uint32_t cap = 0, head = 0, count = 0;
        read_at(address + kOffTargetMultipleColors, ring);
        read_at(address + kOffTargetMultipleCapacity, cap);
        read_at(address + kOffTargetMultipleHead, head);
        read_at(address + kOffTargetMultipleCount, count);
        t.attachments = static_cast<int>(count);
        if (ring && cap && count && count <= 16) {
            for (std::uint32_t i = 0; i < count; ++i) {
                std::uint32_t e[3] = {};
                if (!read_at(ring + static_cast<std::uintptr_t>((head + i) % cap) * 12, e)) break;
                t.colors.push_back({e[0], static_cast<int>(e[1]), static_cast<TargetFormat>(e[2])});
            }
            if (!t.colors.empty()) {
                t.color_texture = t.colors[0].texture;
                t.color_format = t.colors[0].format;
            }
        }
        read_at(address + kOffTargetMultipleDepthTexture, t.depth_texture);
        if (read_at(address + kOffTargetMultipleDepthFormat, v)) t.depth_format = static_cast<TargetFormat>(v);
    } else {
        read_at(address + kOffTargetColorTexture, t.color_texture);
        if (read_at(address + kOffTargetColorFormat, v)) t.color_format = static_cast<TargetFormat>(v);
        read_at(address + kOffTargetDepthTexture, t.depth_texture);
        if (read_at(address + kOffTargetDepthFormat, v)) t.depth_format = static_cast<TargetFormat>(v);
        t.attachments = t.color_format == TargetFormat::none ? 0 : 1;
        if (t.attachments) t.colors.push_back({t.color_texture, 0, t.color_format});
    }
    out = t;
    return true;
}

bool watch_targets() {
    if (g_watching_targets) return true;
    std::string error;
    auto& rt = sw::Runtime::get();
    for (int i = 0; i < 4; ++i) {
        if (rt.hook_rva(kRvaTargetCreateGl[i], &on_target_create, nullptr, error) != sw::HookStatus::ok) return false;
        if (rt.hook_rva(kRvaTargetDeleteGl[i], &on_target_delete, nullptr, error) != sw::HookStatus::ok) return false;
    }
    g_watching_targets = true;
    return true;
}

std::vector<TargetInfo> targets() {
    std::vector<std::uintptr_t> copy;
    {
        std::lock_guard<std::mutex> lock(g_target_mutex);
        copy = g_targets;
    }
    std::vector<TargetInfo> out;
    for (std::uintptr_t a : copy) {
        TargetInfo t;
        if (read_target(a, t)) out.push_back(t);
    }
    return out;
}

bool watch_renderer() {
    if (g_watching_renderer) return true;
    std::string error;
    if (sw::Runtime::get().hook_rva(kRvaFrameRender, &on_frame_function, nullptr, error) != sw::HookStatus::ok) return false;
    g_watching_renderer = true;
    return true;
}

void* renderer() { return reinterpret_cast<void*>(g_renderer.load()); }

const TargetInfo& Pipeline::slot(PipelineSlot s) const {
    static const TargetInfo none;
    const auto i = static_cast<std::size_t>(s);
    return i < slots.size() ? slots[i] : none;
}

bool pipeline(int view, Pipeline& out) {
    const std::uintptr_t r = g_renderer.load();
    if (!r || (view != 0 && view != 1)) return false;
    const std::uintptr_t p = r + (view == 0 ? kOffRendererPipelineMain : kOffRendererPipelineSecond);
    Pipeline pl;
    pl.address = p;
    std::int32_t aa = 0;
    std::uint8_t mb = 0;
    if (!read_at(p + kOffPipelineAaMode, aa) || !read_at(p + kOffPipelineMotionBlur, mb)) return false;
    pl.aa_mode = static_cast<AaMode>(aa);
    pl.motion_blur = mb != 0;
    pl.slots.assign(0x18, TargetInfo{});
    int found = 0;
    for (std::size_t i = 0; i < pl.slots.size(); ++i) {
        std::uintptr_t t = 0;
        if (read_at(p + i * 8, t) && t && read_target(t, pl.slots[i])) ++found;
    }
    if (found < 4) return false;
    out = std::move(pl);
    return true;
}

bool upscaler_inputs(UpscalerInputs& out) {
    Pipeline p;
    if (!pipeline(0, p)) return false;
    const TargetInfo& g = p.slot(PipelineSlot::gbuffer);
    const TargetInfo& scene = p.slot(PipelineSlot::scene);
    const TargetInfo& hist = p.slot(PipelineSlot::history);
    const TargetInfo& aa = p.slot(PipelineSlot::aa_output);
    if (!g.address || g.kind != TargetKind::multiple || g.colors.size() < 3 || !scene.address || !aa.address) return false;
    UpscalerInputs u;
    u.color_texture = scene.color_texture;
    u.velocity_texture = g.colors[2].texture;
    u.velocity_is_rg16f = g.colors[2].format == TargetFormat::rg16f;
    u.depth_texture = g.depth_texture;
    u.history_texture = hist.color_texture;
    u.output_texture = aa.color_texture;
    u.output_fbo = aa.fbo;
    u.input_width = scene.width;
    u.input_height = scene.height;
    u.output_width = aa.width;
    u.output_height = aa.height;
    u.aa_mode = p.aa_mode;
    out = u;
    return true;
}

bool watch_view() {
    if (g_watching_view) return true;
    std::string error;
    if (sw::Runtime::get().hook_rva(kRvaUpdateViewportSize, &on_view_update, nullptr, error) != sw::HookStatus::ok) return false;
    g_watching_view = true;
    return true;
}

bool framebuffer_size(int& width, int& height) {
    const std::uintptr_t g = g_graphics.load();
    std::int32_t w = 0, h = 0;
    if (!g || !read_at(g + kOffGraphicsFramebufferWidth, w) || !read_at(g + kOffGraphicsFramebufferHeight, h) || w <= 0 || h <= 0) return false;
    width = w;
    height = h;
    return true;
}

bool window_size(int& width, int& height) {
    const std::uintptr_t g = g_graphics.load();
    std::int32_t w = 0, h = 0;
    if (!g || !read_at(g + kOffGraphicsWindowWidth, w) || !read_at(g + kOffGraphicsWindowHeight, h) || w <= 0 || h <= 0) return false;
    width = w;
    height = h;
    return true;
}

float aspect() {
    const std::uintptr_t g = g_graphics.load();
    float a = 0;
    return g && read_at(g + kOffGraphicsAspect, a) ? a : 0.0f;
}

bool watch() {
    if (g_watching) return true;
    std::string error;
    if (sw::Runtime::get().hook_rva(kRvaGlfwSwapBuffers, &on_swap, nullptr, error) != sw::HookStatus::ok) return false;
    g_watching = true;
    return true;
}

bool ready() { return g_frames.load() != 0 && static_cast<long long>(GetTickCount64()) - g_last_tick.load() < 1000; }

void* window_dc() {
    const std::uintptr_t w = g_window.load();
    void* dc = nullptr;
    if (!w || !sw::Runtime::get().readable(reinterpret_cast<const void*>(w + kOffWindowDc), sizeof(dc))) return nullptr;
    std::memcpy(&dc, reinterpret_cast<const void*>(w + kOffWindowDc), sizeof(dc));
    return dc;
}

void* gl_function(const char* name) {
    const GlEntry* e = find_entry(name);
    auto& rt = sw::Runtime::get();
    if (!e || !rt.attached()) return nullptr;
    void* fn = nullptr;
    if (!rt.read_rva(e->slot_rva, fn)) return nullptr;
    return fn;
}

bool viewport(int& x, int& y, int& width, int& height) {
    using GetIntegerv = void (*)(unsigned, int*);
    auto fn = reinterpret_cast<GetIntegerv>(gl_function("glGetIntegerv"));
    if (!fn || !g_watching) return false;
    int v[4] = {};
    fn(0x0BA2 /* GL_VIEWPORT */, v);
    x = v[0];
    y = v[1];
    width = v[2];
    height = v[3];
    return width > 0 && height > 0;
}

#else

bool watch() { return false; }
bool read_target(std::uintptr_t, TargetInfo&) { return false; }
bool watch_targets() { return false; }
std::vector<TargetInfo> targets() { return {}; }
bool watch_view() { return false; }
bool watch_renderer() { return false; }
void* renderer() { return nullptr; }
const TargetInfo& Pipeline::slot(PipelineSlot) const {
    static const TargetInfo none;
    return none;
}
bool pipeline(int, Pipeline&) { return false; }
bool upscaler_inputs(UpscalerInputs&) { return false; }
bool framebuffer_size(int&, int&) { return false; }
bool window_size(int&, int&) { return false; }
float aspect() { return 0.0f; }
bool ready() { return false; }
void* window_dc() { return nullptr; }
void* gl_function(const char*) { return nullptr; }
bool viewport(int&, int&, int&, int&) { return false; }

#endif

bool on_frame_end(FrameCallback callback, void* user) {
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

std::uint64_t frames() { return g_frames.load(); }
double frame_ms() { return g_frame_ms.load(); }
void* window() { return reinterpret_cast<void*>(g_window.load()); }

const GlEntry* gl_entry(const char* name) { return find_entry(name); }

int gl_uses(const char* name) {
    const GlEntry* e = find_entry(name);
    return e ? e->uses : 0;
}

unsigned gl_internal_format(TargetFormat f) {
    switch (f) {
        case TargetFormat::depth24:
        case TargetFormat::depth24_shadow: return 0x81A6;
        case TargetFormat::depth24_stencil8: return 0x88F0;
        case TargetFormat::rgba8: return 0x8058;
        case TargetFormat::rgba16f: return 0x881A;
        case TargetFormat::rgba32f: return 0x8814;
        case TargetFormat::r8: return 0x8229;
        case TargetFormat::r16f: return 0x822D;
        case TargetFormat::rg8: return 0x822B;
        case TargetFormat::rg16f: return 0x822F;
        default: return 0;
    }
}

int bytes_per_pixel(TargetFormat f) {
    switch (f) {
        case TargetFormat::rgba8:
        case TargetFormat::rg16f: return 4;
        case TargetFormat::rgba16f: return 8;
        case TargetFormat::rgba32f: return 16;
        case TargetFormat::r8: return 1;
        case TargetFormat::r16f:
        case TargetFormat::rg8: return 2;
        default: return 0;
    }
}

std::uint32_t renderer_shader_offset(const char* shader) {
    if (!shader) return 0;
    const std::size_t n = std::strlen(shader);
    for (std::size_t i = 0; i < kRendererShaderCount; ++i) {
        const char* list = kRendererShaders[i].shaders;
        for (const char* p = list; *p;) {
            const char* e = std::strchr(p, ',');
            const std::size_t len = e ? static_cast<std::size_t>(e - p) : std::strlen(p);
            if (len == n && std::strncmp(p, shader, n) == 0) return kRendererShaders[i].offset;
            if (!e) break;
            p = e + 1;
        }
    }
    return 0;
}

}  // namespace sw::render
