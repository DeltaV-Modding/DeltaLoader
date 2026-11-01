#include "sw/vfs.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cwctype>
#include <map>
#include <memory>
#include <mutex>
#include <set>

namespace sw::vfs {

namespace {

std::wstring lowerw(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

struct State {
    Config cfg;
    std::wstring rom_lower;                                    // absolute, lower case, trailing backslash
    std::wstring rom_full;
    std::vector<std::map<std::wstring, std::wstring>> tops;    // per mod folder: lower name of a top level folder -> real name
};

std::mutex g_mutex;
std::shared_ptr<const State> g_state;
std::atomic<int> g_inflight{0};
std::atomic<std::size_t> g_opens{0}, g_redirected{0}, g_attributes{0}, g_listings{0};

struct Guard {
    Guard() { ++g_inflight; }
    ~Guard() { --g_inflight; }
};

std::shared_ptr<const State> state() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_state;
}

// the entries that were in the import table (the functions the game used before)
using CreateFileW_t = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CreateFileA_t = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using GetFileAttributesExW_t = BOOL(WINAPI*)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
using FindFirstFileW_t = HANDLE(WINAPI*)(LPCWSTR, LPWIN32_FIND_DATAW);
using FindFirstFileExW_t = HANDLE(WINAPI*)(LPCWSTR, FINDEX_INFO_LEVELS, LPVOID, FINDEX_SEARCH_OPS, LPVOID, DWORD);
using FindNextFileW_t = BOOL(WINAPI*)(HANDLE, LPWIN32_FIND_DATAW);
using FindClose_t = BOOL(WINAPI*)(HANDLE);

struct Real {
    CreateFileW_t createW = nullptr;
    CreateFileA_t createA = nullptr;
    GetFileAttributesExW_t attrEx = nullptr;
    FindFirstFileW_t ff = nullptr;
    FindFirstFileExW_t ffex = nullptr;
    FindNextFileW_t fn = nullptr;
    FindClose_t fc = nullptr;
} g_real;

struct Slot {
    void** address;
    void* original;
};
std::vector<Slot> g_slots;
void* g_module = nullptr;

bool to_rel(const State& st, const wchar_t* in, std::wstring& rel) {
    if (!in || !*in) return false;
    std::wstring p(in);
    if (p.compare(0, 4, L"\\\\?\\") == 0 && p.compare(0, 8, L"\\\\?\\UNC\\") != 0) p = p.substr(4);
    wchar_t buf[2048];
    const DWORD n = GetFullPathNameW(p.c_str(), static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])), buf, nullptr);
    if (n == 0 || n >= sizeof(buf) / sizeof(buf[0])) return false;
    const std::wstring full(buf, n);
    const std::wstring lo = lowerw(full);
    if (lo.size() + 1 == st.rom_lower.size() && lo + L"\\" == st.rom_lower) { rel.clear(); return true; }
    if (lo.size() <= st.rom_lower.size() || lo.compare(0, st.rom_lower.size(), st.rom_lower) != 0) return false;
    rel = full.substr(st.rom_lower.size());
    while (!rel.empty() && rel.back() == L'\\') rel.pop_back();
    return true;
}

bool hidden_in_mod(const std::wstring& name) {
    const std::wstring l = lowerw(name);
    return (l.size() > 12 && l.compare(l.size() - 12, 12, L".placeholder") == 0) || l == L"desktop.ini" || l == L"thumbs.db";
}

std::wstring map_to_mod(const State& st, const std::wstring& rel, bool files_only) {
    if (rel.empty()) return std::wstring();
    const std::size_t cut = rel.find(L'\\');
    const std::wstring top = lowerw(rel.substr(0, cut));
    const std::wstring rest = cut == std::wstring::npos ? std::wstring() : rel.substr(cut + 1);
    for (std::size_t i = st.cfg.roots.size(); i-- > 0;) {
        const auto it = st.tops[i].find(top);
        if (it == st.tops[i].end()) continue;
        std::wstring path = st.cfg.roots[i].wstring() + L"\\" + it->second;
        if (!rest.empty()) path += L"\\" + rest;
        const DWORD a = GetFileAttributesW(path.c_str());
        if (a == INVALID_FILE_ATTRIBUTES) continue;
        if (files_only && (a & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (!rest.empty() && hidden_in_mod(rest.substr(rest.find_last_of(L'\\') == std::wstring::npos ? 0 : rest.find_last_of(L'\\') + 1))) continue;
        return path;
    }
    return std::wstring();
}

bool reads_only(DWORD access, DWORD disposition) {
    const DWORD writes = GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA | DELETE | FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA;
    return (access & writes) == 0 && disposition == OPEN_EXISTING;
}

HANDLE WINAPI my_CreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    Guard guard;
    const auto st = state();
    if (st && name && reads_only(access, disp)) {
        ++g_opens;
        std::wstring rel;
        if (to_rel(*st, name, rel) && !rel.empty()) {
            const std::wstring mod = map_to_mod(*st, rel, true);
            if (!mod.empty()) {
                ++g_redirected;
                return g_real.createW(mod.c_str(), access, share, sa, disp, flags, tmpl);
            }
        }
    }
    return g_real.createW(name, access, share, sa, disp, flags, tmpl);
}

HANDLE WINAPI my_CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    Guard guard;
    const auto st = state();
    if (st && name && reads_only(access, disp)) {
        ++g_opens;
        wchar_t wide[2048];
        const int n = MultiByteToWideChar(CP_ACP, 0, name, -1, wide, static_cast<int>(sizeof(wide) / sizeof(wide[0])));
        std::wstring rel;
        if (n > 0 && to_rel(*st, wide, rel) && !rel.empty()) {
            const std::wstring mod = map_to_mod(*st, rel, true);
            if (!mod.empty()) {
                ++g_redirected;
                return g_real.createW(mod.c_str(), access, share, sa, disp, flags, tmpl);
            }
        }
    }
    return g_real.createA(name, access, share, sa, disp, flags, tmpl);
}

BOOL WINAPI my_GetFileAttributesExW(LPCWSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info) {
    Guard guard;
    const auto st = state();
    if (st && name) {
        std::wstring rel;
        if (to_rel(*st, name, rel) && !rel.empty()) {
            const std::wstring mod = map_to_mod(*st, rel, false);
            if (!mod.empty()) {
                ++g_attributes;
                return g_real.attrEx(mod.c_str(), level, info);
            }
        }
    }
    return g_real.attrEx(name, level, info);
}

// a merged listing
struct FindState {
    std::vector<WIN32_FIND_DATAW> items;
    std::size_t next = 0;
};
std::set<FindState*> g_finds;

HANDLE merged_find(LPCWSTR pattern, FINDEX_SEARCH_OPS op, LPVOID filter, LPWIN32_FIND_DATAW out, bool* handled) {
    *handled = false;
    const auto st = state();
    if (!st || !pattern || op != FindExSearchNameMatch || filter) return INVALID_HANDLE_VALUE;
    std::wstring p(pattern);
    if (p.compare(0, 4, L"\\\\?\\") == 0 && p.compare(0, 8, L"\\\\?\\UNC\\") != 0) p = p.substr(4);
    wchar_t buf[2048];
    const DWORD n = GetFullPathNameW(p.c_str(), static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])), buf, nullptr);
    if (n == 0 || n >= sizeof(buf) / sizeof(buf[0])) return INVALID_HANDLE_VALUE;
    const std::wstring full(buf, n);
    const std::size_t slash = full.find_last_of(L'\\');
    if (slash == std::wstring::npos) return INVALID_HANDLE_VALUE;
    const std::wstring dir = full.substr(0, slash), mask = full.substr(slash + 1);
    std::wstring rel;
    if (!to_rel(*st, dir.c_str(), rel) || rel.empty()) return INVALID_HANDLE_VALUE;
    std::vector<std::wstring> mods;
    for (std::size_t i = 0; i < st->cfg.roots.size(); ++i) {
        // the folder of this mod that stands for `rel`
        const std::size_t cut = rel.find(L'\\');
        const auto it = st->tops[i].find(lowerw(rel.substr(0, cut)));
        if (it == st->tops[i].end()) continue;
        std::wstring path = st->cfg.roots[i].wstring() + L"\\" + it->second;
        if (cut != std::wstring::npos) path += L"\\" + rel.substr(cut + 1);
        const DWORD a = GetFileAttributesW(path.c_str());
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) mods.push_back(path);
    }
    if (mods.empty()) return INVALID_HANDLE_VALUE;   // nothing from the mods: the game's own listing runs
    *handled = true;
    ++g_listings;
    std::vector<WIN32_FIND_DATAW> items;
    std::map<std::wstring, std::size_t> index;
    auto collect = [&](const std::wstring& d, bool from_mod) {
        WIN32_FIND_DATAW fd{};
        HANDLE h = g_real.ffex((d + L"\\" + mask).c_str(), FindExInfoStandard, &fd, FindExSearchNameMatch, nullptr, 0);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            const std::wstring name = fd.cFileName;
            if (from_mod && (hidden_in_mod(name) || name == L"." || name == L"..")) continue;
            const std::wstring key = lowerw(name);
            const auto f = index.find(key);
            if (f == index.end()) { index[key] = items.size(); items.push_back(fd); }
            else items[f->second] = fd;
        } while (g_real.fn(h, &fd));
        g_real.fc(h);
    };
    collect(dir, false);
    for (const auto& m : mods) collect(m, true);
    if (items.empty()) { SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_HANDLE_VALUE; }
    auto* fs_state = new FindState();
    fs_state->items = std::move(items);
    *out = fs_state->items[0];
    fs_state->next = 1;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_finds.insert(fs_state);
    }
    return reinterpret_cast<HANDLE>(fs_state);
}

HANDLE WINAPI my_FindFirstFileExW(LPCWSTR pattern, FINDEX_INFO_LEVELS level, LPVOID data, FINDEX_SEARCH_OPS op, LPVOID filter, DWORD flags) {
    Guard guard;
    bool handled = false;
    HANDLE h = merged_find(pattern, op, filter, static_cast<LPWIN32_FIND_DATAW>(data), &handled);
    if (handled) return h;
    return g_real.ffex(pattern, level, data, op, filter, flags);
}

HANDLE WINAPI my_FindFirstFileW(LPCWSTR pattern, LPWIN32_FIND_DATAW data) {
    Guard guard;
    bool handled = false;
    HANDLE h = merged_find(pattern, FindExSearchNameMatch, nullptr, data, &handled);
    if (handled) return h;
    return g_real.ff(pattern, data);
}

BOOL WINAPI my_FindNextFileW(HANDLE handle, LPWIN32_FIND_DATAW data) {
    Guard guard;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto* fs_state = reinterpret_cast<FindState*>(handle);
        if (g_finds.count(fs_state)) {
            if (fs_state->next < fs_state->items.size()) {
                *data = fs_state->items[fs_state->next++];
                return TRUE;
            }
            SetLastError(ERROR_NO_MORE_FILES);
            return FALSE;
        }
    }
    return g_real.fn(handle, data);
}

BOOL WINAPI my_FindClose(HANDLE handle) {
    Guard guard;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto* fs_state = reinterpret_cast<FindState*>(handle);
        if (g_finds.count(fs_state)) {
            g_finds.erase(fs_state);
            delete fs_state;
            return TRUE;
        }
    }
    return g_real.fc(handle);
}


bool patch(HMODULE module, const char* dll, const char* function, void* replacement, void** original) {
    auto* base = reinterpret_cast<BYTE*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;
    for (auto* d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; ++d) {
        if (_stricmp(reinterpret_cast<const char*>(base + d->Name), dll) != 0) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        auto* addrs = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addrs) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto* by_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(by_name->Name), function) != 0) continue;
            void** slot = reinterpret_cast<void**>(&addrs->u1.Function);
            DWORD old = 0;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
            *original = *slot;
            *slot = replacement;
            DWORD ignore = 0;
            VirtualProtect(slot, sizeof(void*), old, &ignore);
            g_slots.push_back({slot, *original});
            return true;
        }
    }
    return false;
}

}  // namespace

bool install(const Config& config, std::string& error, void* module) {
    auto st = std::make_shared<State>();
    st->cfg = config;
    wchar_t buf[2048];
    DWORD n = GetFullPathNameW(config.rom.wstring().c_str(), static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])), buf, nullptr);
    if (n == 0 || n >= sizeof(buf) / sizeof(buf[0])) { error = "rom path not usable"; return false; }
    st->rom_full = std::wstring(buf, n);
    while (!st->rom_full.empty() && st->rom_full.back() == L'\\') st->rom_full.pop_back();
    st->rom_lower = lowerw(st->rom_full) + L"\\";
    for (auto& root : st->cfg.roots) {
        n = GetFullPathNameW(root.wstring().c_str(), static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])), buf, nullptr);
        if (n > 0 && n < sizeof(buf) / sizeof(buf[0])) root = fs::path(std::wstring(buf, n));
        std::map<std::wstring, std::wstring> tops;
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(root, ec))
            if (e.is_directory(ec)) tops[lowerw(e.path().filename().wstring())] = e.path().filename().wstring();
        st->tops.push_back(std::move(tops));
    }
    HMODULE mod = module ? static_cast<HMODULE>(module) : GetModuleHandleW(nullptr);
    if (g_slots.empty() || g_module != mod) {
        if (!g_slots.empty()) uninstall();
        struct Entry { const char* name; void* replacement; void** original; };
        void* oc_w = nullptr, *oc_a = nullptr, *oattr = nullptr, *off = nullptr, *offex = nullptr, *ofn = nullptr, *ofc = nullptr;
        const Entry table[] = {
            {"CreateFileW", reinterpret_cast<void*>(my_CreateFileW), &oc_w},
            {"CreateFileA", reinterpret_cast<void*>(my_CreateFileA), &oc_a},
            {"GetFileAttributesExW", reinterpret_cast<void*>(my_GetFileAttributesExW), &oattr},
            {"FindFirstFileW", reinterpret_cast<void*>(my_FindFirstFileW), &off},
            {"FindFirstFileExW", reinterpret_cast<void*>(my_FindFirstFileExW), &offex},
            {"FindNextFileW", reinterpret_cast<void*>(my_FindNextFileW), &ofn},
            {"FindClose", reinterpret_cast<void*>(my_FindClose), &ofc},
        };
        int patched = 0;
        for (const auto& e : table) if (patch(mod, "KERNEL32.dll", e.name, e.replacement, e.original)) ++patched;
        // a function the module does not import is called through KERNEL32 itself
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        auto orig = [&](void* got, const char* name) { return got ? got : reinterpret_cast<void*>(GetProcAddress(k32, name)); };
        g_real.createW = reinterpret_cast<CreateFileW_t>(orig(oc_w, "CreateFileW"));
        g_real.createA = reinterpret_cast<CreateFileA_t>(orig(oc_a, "CreateFileA"));
        g_real.attrEx = reinterpret_cast<GetFileAttributesExW_t>(orig(oattr, "GetFileAttributesExW"));
        g_real.ff = reinterpret_cast<FindFirstFileW_t>(orig(off, "FindFirstFileW"));
        g_real.ffex = reinterpret_cast<FindFirstFileExW_t>(orig(offex, "FindFirstFileExW"));
        g_real.fn = reinterpret_cast<FindNextFileW_t>(orig(ofn, "FindNextFileW"));
        g_real.fc = reinterpret_cast<FindClose_t>(orig(ofc, "FindClose"));
        g_module = mod;
        if (patched == 0) { error = "the module imports none of the file functions from KERNEL32"; return false; }
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state = st;
    return true;
}

void uninstall() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_state.reset();
    }
    for (const auto& s : g_slots) {
        DWORD old = 0;
        if (VirtualProtect(s.address, sizeof(void*), PAGE_READWRITE, &old)) {
            *s.address = s.original;
            DWORD ignore = 0;
            VirtualProtect(s.address, sizeof(void*), old, &ignore);
        }
    }
    g_slots.clear();
    g_module = nullptr;
    for (int i = 0; i < 400 && g_inflight.load() > 0; ++i) Sleep(5);   // a thread that is inside one of the replacements leaves it first
}

bool installed() { return !g_slots.empty() && state() != nullptr; }

fs::path resolve(const fs::path& path, bool files_only) {
    const auto st = state();
    if (!st) return fs::path();
    std::wstring rel;
    if (!to_rel(*st, path.wstring().c_str(), rel) || rel.empty()) return fs::path();
    return fs::path(map_to_mod(*st, rel, files_only));
}

Stats stats() {
    Stats s;
    s.opens = g_opens; s.redirected = g_redirected; s.attributes = g_attributes; s.listings = g_listings;
    return s;
}

}  // namespace sw::vfs
