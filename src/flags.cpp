#include "sw/flags.hpp"

#include "sw/hook.hpp"
#include "sw/protos.hpp"
#include "sw/runtime.hpp"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <mutex>

namespace sw::flags {

namespace {

struct Stored {
    CustomFlag info;
    gfx::Image picture;
};

std::mutex g_mutex;
std::vector<Stored> g_custom;
std::vector<std::uintptr_t> g_components;   // addresses seen by the constructor hook
bool g_watching = false;
bool g_redirecting = false;
std::string g_redirect_path = kCustomAtlasPath;
std::wstring g_custom_file;                 // the file this header made

#if defined(_WIN64)

template <typename T>
bool read_at(std::uintptr_t address, T& value) {
    if (!address || !sw::Runtime::get().readable(reinterpret_cast<const void*>(address), sizeof(T))) return false;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return true;
}

std::wstring game_dir() {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring p(exe);
    return p.substr(0, p.find_last_of(L"\\/"));
}

bool is_component(std::uintptr_t address) {
    std::uintptr_t vtable = 0;
    return read_at(address, vtable) &&
           vtable == reinterpret_cast<std::uintptr_t>(sw::Runtime::get().at_rva(sw::gfx::kVtableFlagComponent));
}

void on_component_ctor(void* self, void*) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_components.push_back(reinterpret_cast<std::uintptr_t>(self));
}

// The texture loader: (manager, mm_string* path, ...). Replaces the path of the
// flag atlas by the path of the custom atlas.
void on_texture_load(void*, void*) {
    auto* path = reinterpret_cast<sw::protos::MmString*>(sw::Hook::arg(1));
    sw::protos::MmString s{};
    if (!read_at(reinterpret_cast<std::uintptr_t>(path), s) || !s.data || s.size != std::strlen(sw::gfx::kFlagAtlasPath)) return;
    if (!sw::Runtime::get().readable(s.data, s.size) || std::memcmp(s.data, sw::gfx::kFlagAtlasPath, s.size) != 0) return;
    std::string redirect_path;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        redirect_path = g_redirect_path;
    }
    if (sw::protos::StringAssign) sw::protos::StringAssign(path, redirect_path.c_str());
}

// The menu table: a copy of the 162 entries and the custom ones, in memory near the image
// (the code reaches it by rip-relative addressing).
std::uint8_t* g_menu_table = nullptr;
int g_menu_count = kDesignCount;
std::uintptr_t g_site_build = 0, g_site_category = 0;   // the lea instructions that were found
bool g_loc_hooked = false;

std::uint8_t* alloc_near(std::size_t size) {
    auto& rt = sw::Runtime::get();
    const auto base = rt.base();
    for (std::uintptr_t at = base + 0x10000000; at < base + 0x60000000; at += 0x10000) {
        void* p = VirtualAlloc(reinterpret_cast<void*>(at), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (p) return static_cast<std::uint8_t*>(p);
    }
    return nullptr;
}

// Finds "lea reg, [rip+d]" (48 8d 1d / 48 8d 3d) in [start, start+size) whose target is
// `target`, followed by `mov reg, 162` (be a2 00 00 00 or 41 be a2 00 00 00).
std::uintptr_t find_lea(std::uint32_t start, std::uint32_t size, std::uintptr_t target) {
    auto& rt = sw::Runtime::get();
    const auto* code = static_cast<const std::uint8_t*>(rt.at_rva(start));
    if (!code || !rt.readable(code, size)) return 0;
    for (std::uint32_t i = 0; i + 14 < size; ++i) {
        if (code[i] != 0x48 || code[i + 1] != 0x8d || (code[i + 2] != 0x1d && code[i + 2] != 0x3d)) continue;
        std::int32_t d;
        std::memcpy(&d, code + i + 3, 4);
        if (reinterpret_cast<std::uintptr_t>(code + i + 7) + d != target) continue;
        if (code[i + 7] == 0xbe && code[i + 8] == 0xa2 && !code[i + 9] && !code[i + 10] && !code[i + 11])
            return reinterpret_cast<std::uintptr_t>(code + i);
        if (code[i + 7] == 0x41 && code[i + 8] == 0xbe && code[i + 9] == 0xa2 && !code[i + 10] && !code[i + 11] && !code[i + 12])
            return reinterpret_cast<std::uintptr_t>(code + i);
    }
    return 0;
}

// The localisation function: answers the ids of the custom flags from their names.
bool on_loc_text(void* self, void*, std::uint64_t* result) {
    const auto id = static_cast<std::uint32_t>(sw::Hook::arg(1));
    if (id < kCustomLocBase || id >= kCustomLocBase + kMenuCapacity || !sw::protos::StringAssign) return false;
    std::string name;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const std::size_t i = id - kCustomLocBase;
        if (i >= g_custom.size()) return false;
        name = g_custom[i].info.name;
    }
    std::memset(self, 0, 16);   // an empty mm_string (the original does the same before it reserves)
    sw::protos::StringAssign(static_cast<sw::protos::MmString*>(self), name.c_str());
    *result = reinterpret_cast<std::uint64_t>(self);
    return true;
}

// The immediate after a lea (7 bytes): "be imm32" or "41 be imm32".
void* imm_of(std::uintptr_t site) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(site) + 7;
    return const_cast<std::uint8_t*>(p[0] == 0x41 ? p + 2 : p + 1);
}

#endif

}  // namespace

#if defined(_WIN64)

bool show_in_menu(int category, std::string& error) {
    if (category < 0 || category > 4) {
        error = "the category is 0 (colors) to 4 (shipping)";
        return false;
    }
    auto& rt = sw::Runtime::get();
    if (!rt.attached() || !sw::protos::bind(rt)) {
        error = "the game functions do not match the mapping";
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_custom.empty()) {
        error = "add_flag first";
        return false;
    }
    if (kDesignCount + g_custom.size() > kMenuCapacity) {
        error = "too many custom flags for the menu table";
        return false;
    }
    const auto* live = static_cast<const std::uint8_t*>(rt.at_rva(kRvaDesignTable));
    std::int32_t first[5] = {};
    if (!read_at(reinterpret_cast<std::uintptr_t>(live), first) || first[3] == 0) {
        error = "the table of the game is not filled yet (the cells are written at the start)";
        return false;
    }
    if (!g_menu_table) {
        g_menu_table = alloc_near(kMenuCapacity * kDesignStride);
        if (!g_menu_table) {
            error = "no memory near the image";
            return false;
        }
        std::memcpy(g_menu_table, live, kDesignCount * kDesignStride);
    }
    for (std::size_t i = 0; i < g_custom.size(); ++i) {
        std::int32_t e[5] = {category, static_cast<std::int32_t>(kCustomLocBase + i), g_custom[i].info.cell_x,
                             g_custom[i].info.cell_y, 0};
        std::memcpy(g_menu_table + (kDesignCount + i) * kDesignStride, e, sizeof(e));
    }
    const int count = kDesignCount + static_cast<int>(g_custom.size());

    // The two loops: find the instructions the first time, afterwards only the counts change.
    const auto table = reinterpret_cast<std::uintptr_t>(g_menu_table);
    const auto game_table = reinterpret_cast<std::uintptr_t>(live);
    if (!g_site_build) {
        const std::uintptr_t a = find_lea(kRvaMenuBuild, kMenuBuildSize, game_table + 4);
        const std::uintptr_t b = find_lea(kRvaMenuCategory, kMenuCategorySize, game_table + 8);
        if (!a || !b) {
            error = "the loops of the menu were not found (the game does not match)";
            return false;
        }
        const std::int32_t d1 = static_cast<std::int32_t>(table + 4 - (a + 7));
        const std::int32_t d2 = static_cast<std::int32_t>(table + 8 - (b + 7));
        if (!rt.write_bytes(reinterpret_cast<void*>(a + 3), &d1, 4) || !rt.write_bytes(reinterpret_cast<void*>(b + 3), &d2, 4)) {
            error = "the code could not be written";
            return false;
        }
        g_site_build = a;
        g_site_category = b;
    }
    const std::int32_t n = count;
    if (!rt.write_bytes(imm_of(g_site_build), &n, 4) || !rt.write_bytes(imm_of(g_site_category), &n, 4)) {
        error = "the count could not be written";
        return false;
    }
    g_menu_count = count;
    if (!g_loc_hooked) {
        if (sw::Hook::install_replace(const_cast<void*>(rt.at_rva(kRvaLocTextGet)), &on_loc_text, nullptr, error) !=
            sw::HookStatus::ok)
            return false;
        g_loc_hooked = true;
    }
    error.clear();
    return true;
}

int menu_entry_count() { return g_menu_count; }

std::vector<Design> designs() {
    std::vector<Design> out;
    auto& rt = sw::Runtime::get();
    if (!rt.attached()) return out;
    const auto base = reinterpret_cast<std::uintptr_t>(rt.at_rva(kRvaDesignTable));
    for (int i = 0; i < kDesignCount; ++i) {
        std::int32_t v[5] = {};
        if (!read_at(base + static_cast<std::size_t>(i) * kDesignStride, v)) break;
        Design d;
        d.index = i;
        d.category = v[0];
        d.loc_id = static_cast<std::uint32_t>(v[1]);
        d.cell_x = v[2];
        d.cell_y = v[3];
        d.cell_z = v[4];
        out.push_back(d);
    }
    return out;
}

bool watch() {
    if (g_watching) return true;
    std::string error;
    if (sw::Runtime::get().hook_rva(kRvaComponentCtor, &on_component_ctor, nullptr, error) != sw::HookStatus::ok) return false;
    g_watching = true;
    return true;
}

std::vector<Obj> components() {
    std::vector<Obj> out;
    std::lock_guard<std::mutex> lock(g_mutex);
    for (std::uintptr_t a : g_components)
        if (is_component(a)) out.push_back(Objects::instance().observe(a, "c_scene_vehicle_component_flag"));
    return out;
}

bool get_cell(const Obj& c, int& x, int& y) {
    std::int32_t vx = 0, vy = 0;
    if (!c.alive() || !c.is("c_scene_vehicle_component_flag") || !c.get(kOffComponentCellX, vx) || !c.get(kOffComponentCellY, vy)) return false;
    x = vx;
    y = vy;
    return true;
}

bool set_cell(const Obj& c, int x, int y) {
    if (x < 0 || y < 0 || x >= kAtlasColumns || y >= 32) return false;
    if (!c.alive() || !c.is("c_scene_vehicle_component_flag") || !is_component(c.address())) return false;
    const std::int32_t vx = x, vy = y;
    return c.set(kOffComponentCellX, vx) && c.set(kOffComponentCellY, vy);
}

bool redirect_atlas(std::string& error) {
    return redirect_atlas(kCustomAtlasPath, error);
}

bool redirect_atlas(const std::string& rom_relative_path, std::string& error) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_custom.empty()) {
            error = "register a custom flag first";
            return false;
        }
    }
    if (rom_relative_path.empty() || rom_relative_path.front() == '/' ||
        rom_relative_path.find('\\') != std::string::npos ||
        rom_relative_path.find("..") != std::string::npos ||
        rom_relative_path.find(':') != std::string::npos) {
        error = "the atlas path must be a safe path relative to rom/ using forward slashes";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_redirecting) {
            if (g_redirect_path == rom_relative_path) { error.clear(); return true; }
            error = "another mod already selected atlas " + g_redirect_path;
            return false;
        }
        g_redirect_path = rom_relative_path;
    }
    auto& rt = sw::Runtime::get();
    if (!sw::protos::bind(rt)) {
        error = "the game functions do not match the mapping";
        return false;
    }
    if (rt.hook_rva(sw::gfx::kRvaTextureLoad, &on_texture_load, nullptr, error) != sw::HookStatus::ok) return false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_redirecting = true;
    }
    error.clear();
    return true;
}

#else

std::vector<Design> designs() { return {}; }
bool watch() { return false; }
std::vector<Obj> components() { return {}; }
bool get_cell(const Obj&, int&, int&) { return false; }
bool set_cell(const Obj&, int, int) { return false; }
bool redirect_atlas(std::string& error) {
    error = "x64 only";
    return false;
}
bool redirect_atlas(const std::string&, std::string& error) {
    error = "x64 only";
    return false;
}
bool show_in_menu(int, std::string& error) {
    error = "x64 only";
    return false;
}
int menu_entry_count() { return kDesignCount; }

#endif

bool add_flag(const std::string& name, const gfx::Image& picture, std::string& error) {
    if (picture.width != kCellWidth || picture.height != kCellHeight) {
        error = "the picture has to be exactly 64 x 32 pixels";
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    // The atlas of the game, as it is.
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    const std::wstring original = dir + L"\\rom\\graphics\\flags.txtr";
    const std::wstring custom = dir + L"\\rom\\graphics\\flags_custom.txtr";
    auto narrow = [](const std::wstring& w) {
        if (w.empty()) return std::string{};
        const int size = WideCharToMultiByte(CP_ACP, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0) return std::string{};
        std::string result(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_ACP, 0, w.data(), static_cast<int>(w.size()), result.data(), size, nullptr, nullptr);
        return result;
    };
    gfx::Texture atlas;
    if (!gfx::read(narrow(original), atlas, error)) return false;

    // Every custom flag so far, then the new one, each into the next free cell.
    std::vector<Stored> all = g_custom;
    Stored added;
    added.info.name = name;
    added.picture = picture;
    all.push_back(added);
    for (Stored& s : all) {
        bool placed = false;
        for (int y = 0; y < kFirstUsedRow && !placed; ++y) {
            for (int x = 0; x < kAtlasColumns && !placed; ++x) {
                if (gfx::put(atlas, s.picture, x * kCellWidth, y * kCellHeight, error)) {
                    s.info.cell_x = x;
                    s.info.cell_y = y;
                    placed = true;
                }
            }
        }
        if (!placed) {
            error = "no free cell is left in the top half of the atlas";
            return false;
        }
    }
    error.clear();
    atlas.source.clear();   // the output is a new file
    if (!gfx::write(narrow(custom), atlas, error)) return false;
    g_custom = all;
    g_custom_file = custom;
    return true;
}

bool use_flag(const std::string& name, int cell_x, int cell_y, std::string& error) {
    if (name.empty() || name.size() > 128) {
        error = "the name is empty or too long (128)";
        return false;
    }
    if (cell_x < 0 || cell_x >= kAtlasColumns || cell_y < 0 || cell_y >= kFirstUsedRow) {
        error = "the cell has to be in the top half of the atlas";
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const Stored& s : g_custom) {
        if (s.info.cell_x == cell_x && s.info.cell_y == cell_y) {
            error = "the cell is already registered";
            return false;
        }
    }
    Stored added;
    added.info.name = name;
    added.info.cell_x = cell_x;
    added.info.cell_y = cell_y;
    g_custom.push_back(std::move(added));
    error.clear();
    return true;
}

std::vector<CustomFlag> custom_flags() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<CustomFlag> out;
    for (const Stored& s : g_custom) out.push_back(s.info);
    return out;
}

void clear() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_custom.clear();
    g_components.clear();
    if (!g_custom_file.empty()) DeleteFileW(g_custom_file.c_str());
    g_custom_file.clear();
}

}  // namespace sw::flags
