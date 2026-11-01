#include "sw/mapicons.hpp"

#include "sw/hook.hpp"
#include "sw/protos.hpp"
#include "sw/runtime.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>

namespace sw::mapicons {

namespace {

std::mutex g_mutex;
std::vector<CustomIcon> g_icons;
std::vector<gfx::Image> g_pictures;
std::wstring g_files[2];
bool g_redirecting = false;

std::wstring game_dir() {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring p(exe);
    return p.substr(0, p.find_last_of(L"\\/"));
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int size = WideCharToMultiByte(CP_ACP, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_ACP, 0, w.data(), static_cast<int>(w.size()), result.data(), size, nullptr, nullptr);
    return result;
}

// The game directory can be named by SW_GAME_DIR (for tools and tests without a game process).
std::wstring rom_graphics() {
    char* directory = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&directory, &length, "SW_GAME_DIR") == 0 && directory) {
        const int size = MultiByteToWideChar(CP_ACP, 0, directory, -1, nullptr, 0);
        std::wstring result;
        if (size > 0) {
            result.resize(static_cast<std::size_t>(size));
            MultiByteToWideChar(CP_ACP, 0, directory, -1, result.data(), size);
            result.pop_back();
        }
        std::free(directory);
        if (!result.empty()) return result + L"\\rom\\graphics";
    }
    return game_dir() + L"\\rom\\graphics";
}

bool read_text(const std::wstring& path, std::string& out) {
    std::ifstream f(narrow(path), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

std::string attr(const std::string& tag, const char* key) {
    const std::string k = std::string(key) + "=\"";
    const auto p = tag.find(k);
    if (p == std::string::npos) return {};
    const auto e = tag.find('"', p + k.size());
    return e == std::string::npos ? std::string() : tag.substr(p + k.size(), e - p - k.size());
}

#if defined(_WIN64)
template <typename T>
bool read_at(std::uintptr_t address, T& value) {
    if (!address || !sw::Runtime::get().readable(reinterpret_cast<const void*>(address), sizeof(T))) return false;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return true;
}

// graphic_atlas_add_sprites(target, ., mm_string* xml path): swap the path of the map atlas.
void on_atlas(void*, void*) {
    auto* path = reinterpret_cast<sw::protos::MmString*>(sw::Hook::arg(2));
    sw::protos::MmString s{};
    if (!read_at(reinterpret_cast<std::uintptr_t>(path), s) || !s.data || s.size != std::strlen(kAtlasXml)) return;
    if (!sw::Runtime::get().readable(s.data, s.size) || std::memcmp(s.data, kAtlasXml, s.size) != 0) return;
    if (sw::protos::StringAssign) sw::protos::StringAssign(path, kCustomXml);
}

// ---- marker types ---------------------------------------------------------------------
struct Marker {
    std::string region;
    std::uint32_t argb = 0;
    float scale = 0;
};
std::vector<Marker> g_markers;
std::uint8_t* g_block = nullptr;
void* g_ctx = nullptr;
std::uintptr_t g_built_map = 0;
int g_built = 0;
bool g_marker_hooks = false;
constexpr std::size_t kStubBytes = 32;
constexpr std::size_t kStubsAt = 256;
constexpr std::size_t kSlotsAt = kStubsAt + kMaxCustomMarkerTypes * kStubBytes;

using SlotInitFn = void (*)(void* map, void* slot, sw::protos::MmString* name, void* ctx, std::uint32_t* color, std::uint32_t scale_bits);

// On entry of the init of the slots: remember the context.
void on_slots_init(void*, void*) {
    g_ctx = reinterpret_cast<void*>(sw::Hook::arg(1));
    g_built_map = 0;
}

// On entry of the drawing of a marker: build the slots of our types once per map object.
void on_marker_draw(void* self, void*) {
    const auto map = reinterpret_cast<std::uintptr_t>(self);
    if (!g_block || !g_ctx || map == g_built_map || !sw::protos::StringAssign) return;
    auto* init = reinterpret_cast<SlotInitFn>(const_cast<void*>(sw::Runtime::get().at_rva(kRvaMapIconSlot)));
    int built = 0;
    for (std::size_t i = 0; i < g_markers.size(); ++i) {
        sw::protos::MmString name{};
        sw::protos::StringAssign(&name, g_markers[i].region.c_str());
        std::uint32_t scale_bits;
        std::memcpy(&scale_bits, &g_markers[i].scale, 4);
        std::uint32_t color = g_markers[i].argb;
        auto* slot = g_block + kSlotsAt + i * 16;
        std::memset(slot, 0, 16);
        init(self, slot, &name, g_ctx, &color, scale_bits);
        std::uint64_t first = 0;
        std::memcpy(&first, slot, 8);
        if (first) ++built;
    }
    g_built = built;
    g_built_map = map;
}

bool patch_switch(std::string& error) {
    auto& rt = sw::Runtime::get();
    const auto* c = static_cast<const std::uint8_t*>(rt.at_rva(kRvaMarkerSwitchCmp));
    const auto* l = static_cast<const std::uint8_t*>(rt.at_rva(kRvaMarkerTableLoad));
    const auto* t = static_cast<const std::uint8_t*>(rt.at_rva(kRvaMarkerTable));
    const auto* common = static_cast<const std::uint8_t*>(rt.at_rva(kRvaMarkerCommon));
    if (!c || !rt.readable(c, 3) || !rt.readable(l, 7) || !rt.readable(t, 80) || !rt.readable(common, 7)) {
        error = "the code cannot be read";
        return false;
    }
    std::int32_t disp;
    std::memcpy(&disp, l + 3, 4);
    // cmp eax,0x13 | mov edx,[rcx+rax*4+disp32] | mov rdx,[rbx]; lea r9,[rbp-0x59]
    const bool ok = c[0] == 0x83 && c[1] == 0xf8 && c[2] == 0x13 && l[0] == 0x8b && l[1] == 0x94 && l[2] == 0x81 &&
                    disp == static_cast<std::int32_t>(kRvaMarkerTable) && common[0] == 0x48 && common[1] == 0x8b &&
                    common[2] == 0x13 && common[3] == 0x4c && common[4] == 0x8d && common[5] == 0x4d && common[6] == 0xa7;
    if (!ok) {
        error = "the switch of the map does not look like the one of the mapping";
        return false;
    }
    const auto base = rt.base();
    for (std::uintptr_t at = base + 0x10000000; at < base + 0x60000000 && !g_block; at += 0x10000)
        g_block = static_cast<std::uint8_t*>(
            VirtualAlloc(reinterpret_cast<void*>(at), 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!g_block) {
        error = "no memory near the image";
        return false;
    }
    std::memset(g_block, 0, 4096);
    // the table: the 20 entries of the game, then the stubs
    std::memcpy(g_block, t, 20 * 4);
    const auto target = [&](std::uint32_t rva) { return reinterpret_cast<std::uintptr_t>(rt.at_rva(rva)); };
    for (int i = 0; i < kMaxCustomMarkerTypes; ++i) {
        std::uint8_t* s = g_block + kStubsAt + static_cast<std::size_t>(i) * kStubBytes;
        const auto slot = reinterpret_cast<std::uint64_t>(g_block + kSlotsAt + static_cast<std::size_t>(i) * 16);
        std::size_t n = 0;
        s[n++] = 0x49; s[n++] = 0xb8;                                        // mov r8, imm64
        std::memcpy(s + n, &slot, 8); n += 8;
        s[n++] = 0x4d; s[n++] = 0x8b; s[n++] = 0x00;                         // mov r8, [r8]
        s[n++] = 0x4d; s[n++] = 0x85; s[n++] = 0xc0;                         // test r8, r8
        s[n++] = 0x0f; s[n++] = 0x84;                                        // jz default
        std::int32_t rel = static_cast<std::int32_t>(target(kRvaMarkerDefault) - (reinterpret_cast<std::uintptr_t>(s + n) + 4));
        std::memcpy(s + n, &rel, 4); n += 4;
        s[n++] = 0xe9;                                                       // jmp common
        rel = static_cast<std::int32_t>(target(kRvaMarkerCommon) - (reinterpret_cast<std::uintptr_t>(s + n) + 4));
        std::memcpy(s + n, &rel, 4); n += 4;
        const std::int32_t entry = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(s) - base);
        std::memcpy(g_block + (20 + static_cast<std::size_t>(i)) * 4, &entry, 4);
    }
    FlushInstructionCache(GetCurrentProcess(), g_block, 4096);
    const std::uint8_t bound = static_cast<std::uint8_t>(0x13 + kMaxCustomMarkerTypes);
    const std::int32_t new_disp = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(g_block) - base);
    if (!rt.write_bytes(const_cast<std::uint8_t*>(c + 2), &bound, 1) ||
        !rt.write_bytes(const_cast<std::uint8_t*>(l + 3), &new_disp, 4)) {
        error = "the code could not be written";
        return false;
    }
    return true;
}
#endif

}  // namespace

#if defined(_WIN64)
int add_marker_type(const std::string& region, std::uint32_t argb, float scale, std::string& error) {
    if (region.empty() || scale <= 0.0f || scale > 4.0f) {
        error = "the region must be named and the scale between 0 and 4";
        return -1;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_markers.size() >= static_cast<std::size_t>(kMaxCustomMarkerTypes)) {
        error = "at most 16 new marker types";
        return -1;
    }
    auto& rt = sw::Runtime::get();
    if (!rt.attached() || !sw::protos::bind(rt)) {
        error = "the game functions do not match the mapping";
        return -1;
    }
    if (!g_block && !patch_switch(error)) return -1;
    if (!g_marker_hooks) {
        if (rt.hook_rva(kRvaMapIconsInit, &on_slots_init, nullptr, error) != sw::HookStatus::ok ||
            rt.hook_rva(kRvaMarkerDraw, &on_marker_draw, nullptr, error) != sw::HookStatus::ok)
            return -1;
        g_marker_hooks = true;
    }
    g_markers.push_back({region, argb, scale});
    g_built_map = 0;   // build again at the next draw
    error.clear();
    return kFirstCustomMarkerType + static_cast<int>(g_markers.size()) - 1;
}

int marker_type_count() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return static_cast<int>(g_markers.size());
}
int marker_slots_built() { return g_built; }
#else
int add_marker_type(const std::string&, std::uint32_t, float, std::string& error) {
    error = "x64 only";
    return -1;
}
int marker_type_count() { return 0; }
int marker_slots_built() { return 0; }
#endif

bool read_regions(std::vector<Region>& out, std::string& error) {
    std::string xml;
    if (!read_text(rom_graphics() + L"\\ui\\map\\atlas_mapicons.xml", xml)) {
        error = "cannot read rom/graphics/ui/map/atlas_mapicons.xml";
        return false;
    }
    out.clear();
    std::size_t p = 0;
    while ((p = xml.find("<region ", p)) != std::string::npos) {
        const auto e = xml.find("/>", p);
        if (e == std::string::npos) break;
        const std::string tag = xml.substr(p, e - p);
        Region r;
        r.name = attr(tag, "name");
        r.l = static_cast<float>(std::atof(attr(tag, "l").c_str()));
        r.r = static_cast<float>(std::atof(attr(tag, "r").c_str()));
        r.b = static_cast<float>(std::atof(attr(tag, "b").c_str()));
        r.t = static_cast<float>(std::atof(attr(tag, "t").c_str()));
        if (!r.name.empty()) out.push_back(r);
        p = e;
    }
    if (out.empty()) {
        error = "no regions in the XML";
        return false;
    }
    return true;
}

bool add_icon(const std::string& name, const gfx::Image& picture, std::string& error) {
    if (name.empty() || name.find('"') != std::string::npos || name.find('<') != std::string::npos) {
        error = "the name must not be empty or contain quotes";
        return false;
    }
    if (picture.width < 4 || picture.height < 4 || picture.width > 256 || picture.height > 256 || picture.width % 4 ||
        picture.height % 4 ||
        picture.rgba.size() != static_cast<std::size_t>(picture.width) * static_cast<std::size_t>(picture.height) * 4) {
        error = "the picture has to be 4 to 256 pixels in each direction, a multiple of 4";
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<Region> regions;
    if (!read_regions(regions, error)) return false;
    for (const Region& r : regions)
        if (r.name == name) {
            error = "the game has an icon with this name";
            return false;
        }
    for (const CustomIcon& c : g_icons)
        if (c.name == name) {
            error = "this name was added already";
            return false;
        }

    gfx::TextureFile file;
    if (!gfx::read_file(narrow(rom_graphics() + L"\\ui\\map\\atlas_mapicons.txtr"), file, error)) return false;
    if (file.format != gfx::kTxtrFormatBc3 || file.width != kAtlasSize || file.height != kAtlasSize) {
        error = "the atlas is not 1024 x 1024 BC3 (the game does not match)";
        return false;
    }
    gfx::Image level0;
    if (!gfx::decode(file, 0, level0, error)) return false;
    gfx::Texture atlas;
    atlas.width = level0.width;
    atlas.height = level0.height;
    atlas.rgba = level0.rgba;

    std::vector<CustomIcon> icons = g_icons;
    std::vector<gfx::Image> pictures = g_pictures;
    CustomIcon added;
    added.name = name;
    added.width = picture.width;
    added.height = picture.height;
    icons.push_back(added);
    pictures.push_back(picture);
    for (std::size_t i = 0; i < icons.size(); ++i) {
        int x = 0, y = 0;
        if (!gfx::find_free(atlas, icons[i].width, icons[i].height, x, y) ||
            !gfx::put(atlas, pictures[i], x, y, error)) {
            if (error.empty()) error = "no free place is left in the atlas";
            return false;
        }
        icons[i].x = x;
        icons[i].y = y;
    }

    gfx::Image whole;
    whole.width = atlas.width;
    whole.height = atlas.height;
    whole.rgba = atlas.rgba;
    gfx::TextureFile made;
    if (!gfx::make_texture(whole, gfx::kTxtrFormatBc3, made, error)) return false;
    made.flag = file.flag;
    made.trailer = file.trailer;
    const std::wstring dir = rom_graphics() + L"\\ui\\map\\";
    if (!gfx::write_file(narrow(dir + L"atlas_mapicons_custom.txtr"), made, error)) return false;

    char buf[256];
    std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<atlas name=\"" + std::string(kCustomTexture) +
                      "\" width=\"1024\" height=\"1024\">\n\t<regions>\n";
    for (const Region& r : regions) {
        std::snprintf(buf, sizeof(buf), "\t\t<region name=\"%s\" l=\"%.6f\" r=\"%.6f\" b=\"%.6f\" t=\"%.6f\"/>\n", r.name.c_str(),
                      r.l, r.r, r.b, r.t);
        xml += buf;
    }
    for (const CustomIcon& c : icons) {
        std::snprintf(buf, sizeof(buf), "\t\t<region name=\"%s\" l=\"%.6f\" r=\"%.6f\" b=\"%.6f\" t=\"%.6f\"/>\n", c.name.c_str(),
                      static_cast<double>(c.x) / kAtlasSize, static_cast<double>(c.x + c.width) / kAtlasSize,
                      static_cast<double>(c.y) / kAtlasSize, static_cast<double>(c.y + c.height) / kAtlasSize);
        xml += buf;
    }
    xml += "\t</regions>\n</atlas>\n";
    std::ofstream f(narrow(dir + L"atlas_mapicons_custom.xml"), std::ios::binary | std::ios::trunc);
    if (!f) {
        error = "cannot write the custom XML";
        return false;
    }
    f.write(xml.data(), static_cast<std::streamsize>(xml.size()));
    f.close();

    g_icons = icons;
    g_pictures = pictures;
    g_files[0] = dir + L"atlas_mapicons_custom.txtr";
    g_files[1] = dir + L"atlas_mapicons_custom.xml";
    error.clear();
    return true;
}

std::vector<CustomIcon> custom_icons() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_icons;
}

#if defined(_WIN64)
bool redirect_atlas(std::string& error) {
    if (g_icons.empty()) {
        error = "add_icon first: there is no custom atlas yet";
        return false;
    }
    if (g_redirecting) return true;
    auto& rt = sw::Runtime::get();
    if (!sw::protos::bind(rt)) {
        error = "the game functions do not match the mapping";
        return false;
    }
    if (rt.hook_rva(kRvaAtlasAddSprites, &on_atlas, nullptr, error) != sw::HookStatus::ok) return false;
    g_redirecting = true;
    return true;
}
#else
bool redirect_atlas(std::string& error) {
    error = "x64 only";
    return false;
}
#endif

void clear() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_icons.clear();
    g_pictures.clear();
    for (auto& f : g_files)
        if (!f.empty()) DeleteFileW(f.c_str());
    g_files[0].clear();
    g_files[1].clear();
}

}  // namespace sw::mapicons
