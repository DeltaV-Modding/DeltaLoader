#include "sw/start_tiles.hpp"

#include "sw/hook.hpp"
#include "sw/protos.hpp"
#include "sw/runtime.hpp"
#include "sw/tiles.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>

namespace sw::start_tiles {

#if defined(_WIN64)

namespace {

struct Wanted {
    bool has_description = false;
    std::string description;
    bool has_tile = false;
    std::string tile;
    bool has_arid = false;
    std::string arid;
};

struct Saved {
    bool tile = false;
    std::string tile_text;
    bool arid = false;
    std::string arid_text;
    bool description = false;
    std::string description_text;
};

std::mutex g_mutex;
Wanted g_wanted[kOptionCount];
Saved g_saved[kOptionCount];
std::atomic<std::uintptr_t> g_view{0};
std::atomic<long long> g_last_tick{0};
std::uintptr_t g_saved_view = 0;   // the view the saved descriptions belong to
bool g_watching = false;

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

template <typename T>
bool read_at(std::uintptr_t address, T& value) {
    if (!address || !sw::Runtime::get().readable(reinterpret_cast<const void*>(address), sizeof(T)))
        return false;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return true;
}

// The text of an mm_string of the game.
bool read_string(std::uintptr_t address, std::string& out) {
    sw::protos::MmString s{};
    if (!read_at(address, s) || !s.data || s.size > 4096) return false;
    if (s.size && !sw::Runtime::get().readable(s.data, s.size)) return false;
    out.assign(s.data, s.size);
    return true;
}

bool write_string(std::uintptr_t address, const std::string& text) {
    if (!sw::protos::StringAssign || !address) return false;
    sw::protos::MmString s{};
    if (!read_at(address, s)) return false;   // the string has to exist
    sw::protos::StringAssign(reinterpret_cast<sw::protos::MmString*>(address), text.c_str());
    return true;
}

std::uintptr_t rva_address(std::uint32_t rva) {
    return reinterpret_cast<std::uintptr_t>(sw::Runtime::get().at_rva(rva));
}

// The structure of the third start depends on a flag of the menu.
std::uintptr_t start_struct(int option, std::uintptr_t view) {
    std::uint32_t rva = kRvaStartStruct[option];
    std::uint8_t multiplayer = 0;
    if (option == 2 && view && read_at(view + kOffMenuMultiplayerFlag, multiplayer) && multiplayer)
        rva = kRvaStartStructMultiplayer;
    return rva_address(rva);
}

bool is_menu(std::uintptr_t view) {
    std::uintptr_t vtable = 0;
    return view && read_at(view, vtable) && vtable == rva_address(kRvaMenuVtable);
}

// Writes what the mod wants into the game; saves what was there.
void apply(std::uintptr_t view) {
    if (view != g_saved_view) {
        for (Saved& s : g_saved) s.description = false;   // a new menu: its strings are new
        g_saved_view = view;
    }
    for (int i = 0; i < kOptionCount; ++i) {
        const Wanted& w = g_wanted[i];
        Saved& saved = g_saved[i];
        std::string current;
        if (w.has_description) {
            const std::uintptr_t address = view + kOffMenuDescription[i];
            if (read_string(address, current) && current != w.description) {
                if (!saved.description) {
                    saved.description_text = current;
                    saved.description = true;
                }
                write_string(address, w.description);
            }
        }
        const std::uintptr_t base = start_struct(i, view);
        if (w.has_tile && read_string(base + kOffTile, current) && current != w.tile) {
            if (!saved.tile) {
                saved.tile_text = current;
                saved.tile = true;
            }
            write_string(base + kOffTile, w.tile);
        }
        if (w.has_arid && read_string(base + kOffAridTile, current) && current != w.arid) {
            if (!saved.arid) {
                saved.arid_text = current;
                saved.arid = true;
            }
            write_string(base + kOffAridTile, w.arid);
        }
    }
}

void inject_bases(std::uintptr_t view);

void on_menu_tick(void* self, void*) {
    const auto view = reinterpret_cast<std::uintptr_t>(self);
    if (!is_menu(view)) return;
    g_view.store(view, std::memory_order_relaxed);
    g_last_tick.store(now_ms(), std::memory_order_relaxed);
    std::unique_lock<std::mutex> lock(g_mutex, std::try_to_lock);
    if (!lock.owns_lock()) return;   // never block the engine
    apply(view);
    inject_bases(view);
}

bool option_ok(int option) { return option >= 0 && option < kOptionCount; }

// ------------------------------------------------------------ the bases

using AddBaseFn = void (*)(std::uintptr_t state, void* ctx, sw::protos::MmString* title,
                           sw::protos::MmString* key, sw::protos::MmString* description);

std::vector<Base> g_bases;                  // the cards the mod wants
std::string g_last_start;                   // what the mod did at the last start of a game
std::vector<std::wstring> g_made_files;     // picture files this mod made (removed on unload)
std::atomic<std::uintptr_t> g_state{0};     // the main menu state of the last build
std::atomic<void*> g_ctx{nullptr};          // the UI context the game passes to AddBase
std::uintptr_t g_injected_view = 0;         // the menu the cards were made for
thread_local bool g_injecting = false;

bool build_mm_string(sw::protos::MmString& s, const std::string& text) {
    s = sw::protos::MmString{nullptr, 0, 0};
    const auto n = static_cast<std::uint32_t>(text.size());
    sw::protos::StringReserve(&s, n + 1);
    if (!s.data) return false;
    std::memcpy(s.data, text.c_str(), n + 1);
    s.size = n;
    return true;
}

void release_mm_string(sw::protos::MmString& s) {
    if (s.data && sw::protos::Free) sw::protos::Free(s.data);
    s = sw::protos::MmString{nullptr, 0, 0};
}

std::wstring game_tiles_folder() {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring p(exe);
    p = p.substr(0, p.find_last_of(L"\\/"));
    return p + L"\\rom\\data\\tiles\\";
}

bool file_exists(const std::wstring& path) {
    using GetFileAttributesExWFn = BOOL(WINAPI*)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
    const auto game = reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(game);
    if (dos && dos->e_magic == IMAGE_DOS_SIGNATURE) {
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(game + dos->e_lfanew);
        const auto& imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(game + imports.VirtualAddress);
        const auto kernel = GetModuleHandleW(L"kernel32.dll");
        const auto real = kernel ? GetProcAddress(kernel, "GetFileAttributesExW") : nullptr;
        for (; imports.VirtualAddress && desc->Name; ++desc) {
            if (_stricmp(reinterpret_cast<const char*>(game + desc->Name), "KERNEL32.dll") != 0) continue;
            auto* names = reinterpret_cast<const IMAGE_THUNK_DATA*>(game + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
            auto* slots = reinterpret_cast<const IMAGE_THUNK_DATA*>(game + desc->FirstThunk);
            for (; names->u1.AddressOfData; ++names, ++slots) {
                if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
                const auto* by_name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(game + names->u1.AddressOfData);
                if (std::strcmp(reinterpret_cast<const char*>(by_name->Name), "GetFileAttributesExW") != 0) continue;
                auto fn = reinterpret_cast<GetFileAttributesExWFn>(slots->u1.Function);
                if (fn && reinterpret_cast<FARPROC>(fn) != real) {
                    WIN32_FILE_ATTRIBUTE_DATA data{};
                    return fn(path.c_str(), GetFileExInfoStandard, &data) &&
                           !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
                }
                break;
            }
            break;
        }
    }
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool virtual_filesystem_active() {
    const auto game = reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(game);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(game + dos->e_lfanew);
    const auto& imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imports.VirtualAddress) return false;
    const auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(game + imports.VirtualAddress);
    const auto kernel = GetModuleHandleW(L"kernel32.dll");
    const auto real = kernel ? GetProcAddress(kernel, "GetFileAttributesExW") : nullptr;
    for (; desc->Name; ++desc) {
        if (_stricmp(reinterpret_cast<const char*>(game + desc->Name), "KERNEL32.dll") != 0) continue;
        const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA*>(game + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
        const auto* slots = reinterpret_cast<const IMAGE_THUNK_DATA*>(game + desc->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            const auto* by_name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(game + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(by_name->Name), "GetFileAttributesExW") == 0)
                return reinterpret_cast<FARPROC>(slots->u1.Function) != real;
        }
        return false;
    }
    return false;
}

bool key_ok(const std::string& key) {
    if (key.empty() || key.size() > 64) return false;
    for (char c : key) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

std::string chosen_start_tile() {
    if (!sw::protos::GameSettings) return {};
    const std::uintptr_t settings = sw::protos::GameSettings();
    std::string text;
    if (!settings || !read_string(settings + 0x10, text)) return {};
    return text;
}

bool same_name(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

void on_tiles_placed(const sw::tiles::EventInfo&, void*) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_last_start.clear();
    const std::string chosen = chosen_start_tile();
    if (chosen.empty()) return;
    for (const Base& b : g_bases) {
        const std::string full = "data/tiles/" + b.key + ".xml";
        if (!same_name(chosen, full)) continue;
        // A tile that a map placed already is left alone.
        for (const sw::tiles::Cell& c : sw::tiles::cells()) {
            if (same_name(c.name, full)) {
                g_last_start = full + ": already in the world at " + std::to_string(c.gx) + ", " +
                               std::to_string(c.gy);
                return;
            }
        }
        std::string error;
        if (!b.fixed_cell) {
            if (sw::tiles::place_random(b.key + ".xml", 0, 0, 25, 100, error))
                g_last_start = full + ": not in the world, queued at a random cell 25 to 100 cells from the origin";
            else
                g_last_start = full + ": " + error;
            return;
        }
        const int gx = b.grid_x, gy = b.grid_y;
        if (sw::tiles::place(b.key + ".xml", gx, gy, error))
            g_last_start = full + ": put into the world at " + std::to_string(gx) + ", " + std::to_string(gy);
        else
            g_last_start = full + ": " + error;
        return;
    }
}

void on_add_base(void*, void*) {
    if (g_injecting) return;
    g_state.store(static_cast<std::uintptr_t>(sw::Hook::arg(0)), std::memory_order_relaxed);
    g_ctx.store(reinterpret_cast<void*>(sw::Hook::arg(1)), std::memory_order_relaxed);
}

std::uintptr_t menu_of(std::uintptr_t state) {
    std::uintptr_t interface = 0, menu = 0;
    if (!state || !read_at(state + kOffStateInterface, interface) || !interface) return 0;
    if (!read_at(interface + kOffInterfaceNewGame, menu)) return 0;
    return menu;
}

// Makes the waiting cards with the game's own function.
void inject_bases(std::uintptr_t view) {
    if (g_bases.empty() || view == g_injected_view) return;
    const std::uintptr_t state = g_state.load(std::memory_order_relaxed);
    void* ctx = g_ctx.load(std::memory_order_relaxed);
    if (!state || !ctx || menu_of(state) != view) return;   // not the menu of this state
    const auto* fn = sw::Runtime::get().at_rva(kRvaAddBase);
    if (!fn || !sw::Runtime::get().is_function_start(reinterpret_cast<std::uintptr_t>(fn))) return;
    g_injected_view = view;
    g_injecting = true;
    for (const Base& b : g_bases) {
        sw::protos::MmString title{}, key{}, description{};
        if (build_mm_string(title, b.title) && build_mm_string(key, b.key) &&
            build_mm_string(description, b.description))
            reinterpret_cast<AddBaseFn>(const_cast<void*>(fn))(state, ctx, &title, &key, &description);
        release_mm_string(title);   // the game copied the texts
        release_mm_string(key);
        release_mm_string(description);
    }
    g_injecting = false;
}

}  // namespace

bool watch() {
    if (g_watching) return true;
    auto& rt = sw::Runtime::get();
    if (!sw::protos::bind(rt)) return false;
    std::string error;
    if (rt.hook_rva(kRvaMenuTick, &on_menu_tick, nullptr, error) != sw::HookStatus::ok) return false;
    if (rt.hook_rva(kRvaAddBase, &on_add_base, nullptr, error) != sw::HookStatus::ok) return false;
    // The tile of the chosen card has to be in the world at the start.
    if (!sw::tiles::watch() ||
        !sw::tiles::on(sw::tiles::Event::tiles_placed_late, &on_tiles_placed, nullptr, error))
        return false;
    g_watching = true;
    return true;
}

std::string last_start() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_last_start;
}

bool add_base(const Base& base, std::string& error) {
    if (!g_watching) {
        error = "call sw::start_tiles::watch() first";
        return false;
    }
    if (!key_ok(base.key)) {
        error = "the key has to be the file name of the tile without folder and extension (a-z, 0-9, _ -)";
        return false;
    }
    if (base.title.empty() || base.title.size() > 128 || base.description.size() > 512) {
        error = "the title is empty or too long (128), or the description is too long (512)";
        return false;
    }
    const std::wstring folder = game_tiles_folder();
    const std::wstring wkey(base.key.begin(), base.key.end());
    if (!file_exists(folder + wkey + L".xml")) {
        error = "rom\\data\\tiles\\" + base.key + ".xml does not exist";
        return false;
    }
    {
        std::ifstream in(std::filesystem::path(folder + wkey + L".xml"), std::ios::binary);
        const std::string xml((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const std::wstring rom = folder + L"..\\..\\";
        for (std::size_t at = xml.find("file_name=\""); at != std::string::npos; at = xml.find("file_name=\"", at + 1)) {
            const std::size_t b = at + 11, e = xml.find('"', b);
            if (e == std::string::npos) break;
            std::string rel = xml.substr(b, e - b);
            for (char& c : rel) if (c == '/') c = '\\';
            if (!file_exists(rom + std::wstring(rel.begin(), rel.end()))) {
                error = base.key + ".xml refers to rom\\" + rel + ", which does not exist; the game would crash near this tile";
                return false;
            }
        }
        if (xml.find("is_volcanic=\"true\"") != std::string::npos && xml.find("<definition") != std::string::npos) {
            const std::size_t d = xml.find("<definition"), de = xml.find('>', d);
            const bool part_volcanic = xml.find("is_volcanic=\"true\"", de) != std::string::npos;
            const bool def_volcanic = xml.substr(d, de - d).find("is_volcanic=\"true\"") != std::string::npos;
            if (part_volcanic && !def_volcanic) {
                error = base.key + ".xml has volcanic parts but the definition lacks is_volcanic=\"true\"";
                return false;
            }
        }
    }
    const std::wstring bin = folder + wkey + L"_map_geometry.bin";
    if (!file_exists(bin)) {
        if (virtual_filesystem_active()) {
            error = "rom\\data\\tiles\\" + base.key + "_map_geometry.bin is missing; the virtual file system cannot create files in rom, so supply this file in the mod folder";
            return false;
        }
        const std::string template_key =
            base.geometry_template.empty() ? "moon_surface_0_0" : base.geometry_template;
        if (!key_ok(template_key)) {
            error = "geometry_template has to be the file name of a tile without folder and extension";
            return false;
        }
        const std::wstring wtemplate(template_key.begin(), template_key.end());
        const std::wstring source = folder + wtemplate + L"_map_geometry.bin";
        if (!file_exists(source)) {
            error = "rom\\data\\tiles\\" + base.key + "_map_geometry.bin is missing and so is the template " +
                    template_key + "_map_geometry.bin";
            return false;
        }
        if (!CopyFileW(source.c_str(), bin.c_str(), TRUE)) {
            error = "the picture file could not be made (copy of " + template_key + "_map_geometry.bin)";
            return false;
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        g_made_files.push_back(bin);
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_bases.push_back(base);
    g_injected_view = 0;   // the next frame of the menu makes the card
    return true;
}

void remove_bases() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_bases.clear();
    for (const std::wstring& f : g_made_files) DeleteFileW(f.c_str());
    g_made_files.clear();
}

std::vector<BaseInfo> bases() {
    std::vector<BaseInfo> out;
    const std::uintptr_t menu = menu_of(g_state.load());
    std::uintptr_t data = 0;
    std::uint32_t cap = 0, start = 0, count = 0;
    if (!menu || !read_at(menu + kOffMenuLocations, data) ||
        !read_at(menu + kOffMenuLocations + 8, cap) || !read_at(menu + kOffMenuLocations + 0xc, start) ||
        !read_at(menu + kOffMenuLocations + 0x10, count) || !data || !cap || count > cap || count > 512)
        return out;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uintptr_t desc = 0;
        if (!read_at(data + ((start + i) % cap) * 8, desc) || !desc) continue;
        BaseInfo info;
        read_string(desc + kOffDescTile, info.tile);
        read_string(desc + kOffDescGeometry, info.geometry);
        read_string(desc + kOffDescTitle, info.title);
        read_string(desc + kOffDescDescription, info.description);
        out.push_back(std::move(info));
    }
    return out;
}

bool menu_open() {
    return g_view.load() != 0 && now_ms() - g_last_tick.load() < 500;
}

std::vector<Option> options() {
    std::vector<Option> out;
    const std::uintptr_t view = menu_open() ? g_view.load() : 0;
    for (int i = 0; i < kOptionCount; ++i) {
        Option o;
        o.index = i;
        const std::uintptr_t base = start_struct(i, view);
        read_string(base + kOffTile, o.tile);
        read_string(base + kOffAridTile, o.arid_tile);
        if (view) read_string(view + kOffMenuDescription[i], o.description);
        out.push_back(std::move(o));
    }
    return out;
}

bool set_description(int option, std::string_view text) {
    if (!g_watching || !option_ok(option)) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_wanted[option].has_description = true;
    g_wanted[option].description = std::string(text);
    return true;
}

bool set_tile(int option, std::string_view file) {
    if (!g_watching || !option_ok(option) || file.empty()) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_wanted[option].has_tile = true;
    g_wanted[option].tile = std::string(file);
    return true;
}

bool set_arid_tile(int option, std::string_view file) {
    if (!g_watching || !option_ok(option) || file.empty()) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_wanted[option].has_arid = true;
    g_wanted[option].arid = std::string(file);
    return true;
}

bool define(int option, const Custom& custom) {
    if (!g_watching || !option_ok(option)) return false;
    std::string text = custom.title;
    if (!custom.title.empty() && !custom.description.empty()) text += " - ";
    text += custom.description;
    if (!text.empty() && !set_description(option, text)) return false;
    if (!custom.tile.empty() && !set_tile(option, custom.tile)) return false;
    if (!custom.arid_tile.empty() && !set_arid_tile(option, custom.arid_tile)) return false;
    return true;
}

void clear(int option) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (option < 0) {
        g_bases.clear();
        for (const std::wstring& f : g_made_files) DeleteFileW(f.c_str());
        g_made_files.clear();
    }
    const std::uintptr_t view = g_view.load();
    for (int i = 0; i < kOptionCount; ++i) {
        if (option >= 0 && option != i) continue;
        Saved& saved = g_saved[i];
        const std::uintptr_t base = start_struct(i, view);
        if (saved.tile) write_string(base + kOffTile, saved.tile_text);
        if (saved.arid) write_string(base + kOffAridTile, saved.arid_text);
        if (saved.description && view && view == g_saved_view && is_menu(view))
            write_string(view + kOffMenuDescription[i], saved.description_text);
        saved = Saved{};
        g_wanted[i] = Wanted{};
    }
}

#else  // !_WIN64

bool watch() { return false; }
std::string last_start() { return {}; }
bool add_base(const Base&, std::string& error) {
    error = "x64 only";
    return false;
}
void remove_bases() {}
std::vector<BaseInfo> bases() { return {}; }
bool menu_open() { return false; }
std::vector<Option> options() { return {}; }
bool set_description(int, std::string_view) { return false; }
bool set_tile(int, std::string_view) { return false; }
bool set_arid_tile(int, std::string_view) { return false; }
bool define(int, const Custom&) { return false; }
void clear(int) {}

#endif

}  // namespace sw::start_tiles
