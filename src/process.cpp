#include "sw/process.hpp"

#include "sw/pe_file.hpp"
#include "sw/target.hpp"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <iterator>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

namespace sw {

namespace {

std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return s;
}

std::wstring win32_error_text(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD n = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring out = (n && buffer) ? std::wstring(buffer, n) : L"unknown error";
    if (buffer) LocalFree(buffer);
    while (!out.empty() && (out.back() == L'\r' || out.back() == L'\n' || out.back() == L' ')) {
        out.pop_back();
    }
    return out;
}

struct Handle {
    HANDLE h = nullptr;
    ~Handle() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle() = default;
};

}  // namespace

std::wstring format_win_error(unsigned long code) {
    return win32_error_text(static_cast<DWORD>(code));
}

std::optional<ProcessInfo> find_process(const std::wstring& exe_name, std::wstring& err) {
    err.clear();

    Handle snap;
    snap.h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap.h == INVALID_HANDLE_VALUE) {
        err = L"process list not readable: " + win32_error_text(GetLastError());
        return std::nullopt;
    }

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (!Process32FirstW(snap.h, &pe)) {
        err = L"process list not readable: " + win32_error_text(GetLastError());
        return std::nullopt;
    }

    const std::wstring want = lower(exe_name);
    do {
        if (lower(pe.szExeFile) != want) continue;

        ProcessInfo info;
        info.pid = static_cast<std::uint32_t>(pe.th32ProcessID);

        Handle proc;
        proc.h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (proc.h) {
            wchar_t path[MAX_PATH * 4]{};
            DWORD n = static_cast<DWORD>(std::size(path));
            if (QueryFullProcessImageNameW(proc.h, 0, path, &n)) {
                info.image_path.assign(path, n);
            }
        }
        return info;
    } while (Process32NextW(snap.h, &pe));

    err = L"process not found: " + exe_name;
    return std::nullopt;
}

std::optional<ProcessInfo> wait_for_process(const std::wstring& exe_name, int seconds,
                                            std::wstring& err) {
    const int tries = seconds > 0 ? seconds * 4 : 1;
    for (int i = 0; i < tries; ++i) {
        std::wstring local;
        auto info = find_process(exe_name, local);
        if (info) return info;
        if (i + 1 < tries) Sleep(250);
    }
    err = L"process not found: " + exe_name;
    return std::nullopt;
}

bool same_bitness(std::uint32_t pid, bool& same, std::wstring& err) {
    err.clear();
    same = false;

    Handle proc;
    proc.h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc.h) {
        err = L"process not queryable: " + win32_error_text(GetLastError());
        return false;
    }

    BOOL target_wow = FALSE;
    if (!IsWow64Process(proc.h, &target_wow)) {
        err = L"architecture not queryable: " + win32_error_text(GetLastError());
        return false;
    }
    BOOL self_wow = FALSE;
    if (!IsWow64Process(GetCurrentProcess(), &self_wow)) {
        err = L"own architecture not queryable: " + win32_error_text(GetLastError());
        return false;
    }
    same = (target_wow != FALSE) == (self_wow != FALSE);
    return true;
}

AntiCheatCheck find_anticheat_module(std::uint32_t pid) {
    // Only known names. A failed query is reported as "not checked", not
    // as "clean".
    static const wchar_t* kNames[] = {
        L"easyanticheat", L"eac_", L"battleye", L"beservice", L"beclient",
        L"xigncode", L"anticheat", L"vanguard", L"vgc", L"faceit",
    };

    AntiCheatCheck result;

    Handle snap;
    snap.h = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap.h == INVALID_HANDLE_VALUE) return result;

    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    if (!Module32FirstW(snap.h, &me)) return result;

    result.checked = true;
    do {
        const std::wstring mod = lower(me.szModule);
        for (const wchar_t* name : kNames) {
            if (mod.find(name) != std::wstring::npos) {
                result.module = me.szModule;
                return result;
            }
        }
    } while (Module32NextW(snap.h, &me));
    return result;
}

std::optional<InjectResult> inject_dll(std::uint32_t pid, const std::wstring& dll_path,
                                       std::wstring& err) {
    err.clear();

    wchar_t full[MAX_PATH * 4]{};
    const DWORD full_len = GetFullPathNameW(dll_path.c_str(), static_cast<DWORD>(std::size(full)),
                                            full, nullptr);
    if (full_len == 0 || full_len >= std::size(full)) {
        err = L"DLL path invalid: " + win32_error_text(GetLastError());
        return std::nullopt;
    }
    const std::wstring path(full, full_len);

    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        err = L"DLL not found: " + path;
        return std::nullopt;
    }

    std::string pe_err;
    auto pe = PeFile::open(path, pe_err);
    if (!pe) {
        err = L"DLL is not a readable PE file: " + std::wstring(pe_err.begin(), pe_err.end());
        return std::nullopt;
    }
    if (pe->is_x64() != kBuildIsX64) {
        err = kBuildIsX64 ? L"DLL is not a 64-bit DLL."
                          : L"DLL is not a 32-bit DLL.";
        return std::nullopt;
    }

    bool same = false;
    std::wstring arch_err;
    if (!same_bitness(pid, same, arch_err)) {
        err = arch_err;
        return std::nullopt;
    }
    if (!same) {
        err = L"Target process has a different bitness than this loader.";
        return std::nullopt;
    }

    Handle proc;
    proc.h = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                             PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ |
                             SYNCHRONIZE,
                         FALSE, pid);
    if (!proc.h) {
        err = L"process not openable: " + win32_error_text(GetLastError());
        return std::nullopt;
    }

    const SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(proc.h, nullptr, bytes, MEM_COMMIT | MEM_RESERVE,
                                  PAGE_READWRITE);
    if (!remote) {
        err = L"memory in the target process not reservable: " + win32_error_text(GetLastError());
        return std::nullopt;
    }

    auto free_remote = [&]() { VirtualFreeEx(proc.h, remote, 0, MEM_RELEASE); };

    SIZE_T written = 0;
    if (!WriteProcessMemory(proc.h, remote, path.c_str(), bytes, &written) ||
        written != bytes) {
        err = L"DLL path not writable: " + win32_error_text(GetLastError());
        free_remote();
        return std::nullopt;
    }

    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    if (!kernel) {
        err = L"kernel32.dll not found";
        free_remote();
        return std::nullopt;
    }
    auto load_library = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(kernel, "LoadLibraryW"));
    if (!load_library) {
        err = L"LoadLibraryW not found";
        free_remote();
        return std::nullopt;
    }

    Handle thread;
    thread.h = CreateRemoteThread(proc.h, nullptr, 0, load_library, remote, 0, nullptr);
    if (!thread.h) {
        err = L"remote thread not creatable: " + win32_error_text(GetLastError());
        free_remote();
        return std::nullopt;
    }

    const DWORD wait = WaitForSingleObject(thread.h, 30000);
    if (wait != WAIT_OBJECT_0) {
        err = (wait == WAIT_TIMEOUT)
                  ? L"timeout while loading the DLL (30 s)"
                  : L"waiting for the load operation failed: " + win32_error_text(GetLastError());
        return std::nullopt;
    }

    DWORD exit_code = 0;
    if (!GetExitCodeThread(thread.h, &exit_code)) {
        err = L"load result not readable: " + win32_error_text(GetLastError());
        free_remote();
        return std::nullopt;
    }
    free_remote();

    if (exit_code == 0) {
        err = L"LoadLibraryW failed in the target process (exit code 0). "
              L"Possible cause: missing dependency or wrong architecture.";
        return std::nullopt;
    }

    InjectResult result;
    result.pid = pid;
    result.dll_path = path;
    return result;
}

std::vector<RemoteModule> list_remote_modules(std::uint32_t pid, std::wstring& err) {
    err.clear();
    std::vector<RemoteModule> out;
    Handle snap;
    snap.h = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap.h == INVALID_HANDLE_VALUE) {
        err = L"module list not readable: " + win32_error_text(GetLastError());
        return out;
    }
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    for (BOOL ok = Module32FirstW(snap.h, &me); ok; ok = Module32NextW(snap.h, &me)) {
        RemoteModule m;
        m.base = reinterpret_cast<std::uintptr_t>(me.modBaseAddr);
        m.name = me.szModule;
        m.path = me.szExePath;
        out.push_back(m);
    }
    return out;
}

namespace {

bool run_remote(HANDLE proc, std::uintptr_t fn, std::uintptr_t arg, DWORD timeout_ms,
                DWORD& exit_code, std::wstring& err) {
    Handle thread;
    thread.h = CreateRemoteThread(proc, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(fn),
                                  reinterpret_cast<void*>(arg), 0, nullptr);
    if (!thread.h) {
        err = L"remote thread not creatable: " + win32_error_text(GetLastError());
        return false;
    }
    const DWORD wait = WaitForSingleObject(thread.h, timeout_ms);
    if (wait != WAIT_OBJECT_0) {
        err = L"timeout in the game";
        return false;
    }
    if (!GetExitCodeThread(thread.h, &exit_code)) {
        err = L"result not readable: " + win32_error_text(GetLastError());
        return false;
    }
    return true;
}

// RVA of an export in a DLL file, without executing its code.
std::uintptr_t export_rva(const std::wstring& dll, const char* name) {
    HMODULE local = LoadLibraryExW(dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!local) return 0;
    const auto fn = reinterpret_cast<std::uintptr_t>(GetProcAddress(local, name));
    const std::uintptr_t rva = fn ? fn - reinterpret_cast<std::uintptr_t>(local) : 0;
    FreeLibrary(local);
    return rva;
}

}  // namespace

std::optional<UnloadResult> unload_mod(std::uint32_t pid, const RemoteModule& module,
                                       const std::wstring& local_dll, std::wstring& err) {
    err.clear();
    const std::uintptr_t rva = export_rva(local_dll, "swmod_shutdown");
    if (!rva) {
        err = L"Module has no swmod_shutdown - not a mod of this skeleton, not unloaded.";
        return std::nullopt;
    }
    Handle proc;
    proc.h = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                             PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ |
                             SYNCHRONIZE,
                         FALSE, pid);
    if (!proc.h) {
        err = L"process not openable: " + win32_error_text(GetLastError());
        return std::nullopt;
    }

    UnloadResult result;
    DWORD code = 0;
    if (!run_remote(proc.h, module.base + rva, 0, 15000, code, err)) return std::nullopt;
    result.shutdown_called = true;
    result.shutdown_code = code;
    if (code == 3) {
        // A callback is still running: the DLL stays loaded (leak instead of crash).
        err = L"Mod reports: a callback is still running - DLL stays loaded.";
        return result;
    }

    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    const auto free_library =
        reinterpret_cast<std::uintptr_t>(kernel ? GetProcAddress(kernel, "FreeLibrary") : nullptr);
    if (!free_library) {
        err = L"FreeLibrary not found";
        return result;
    }
    if (!run_remote(proc.h, free_library, module.base, 15000, code, err)) return result;
    result.freed = code != 0;
    if (!result.freed) err = L"FreeLibrary failed in the game";
    return result;
}

namespace {

bool file_exists(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring join(std::wstring dir, const std::wstring& rest) {
    while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
    return dir + L"\\" + rest;
}

std::wstring reg_string(HKEY root, const wchar_t* key, const wchar_t* value) {
    wchar_t buf[MAX_PATH * 2]{};
    DWORD size = sizeof(buf);
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) {
        return {};
    }
    std::wstring out(buf);
    for (wchar_t& c : out) if (c == L'/') c = L'\\';
    return out;
}

// Reads the "path" entries from Steam's libraryfolders.vdf.
std::vector<std::wstring> steam_libraries(const std::wstring& steam) {
    std::vector<std::wstring> libs;
    if (!steam.empty()) libs.push_back(steam);
    const std::wstring vdf = join(steam, L"steamapps\\libraryfolders.vdf");
    HANDLE f = CreateFileW(vdf.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0,
                           nullptr);
    if (f == INVALID_HANDLE_VALUE) return libs;
    std::string text;
    char chunk[4096];
    DWORD got = 0;
    while (ReadFile(f, chunk, sizeof(chunk), &got, nullptr) && got > 0) text.append(chunk, got);
    CloseHandle(f);
    std::size_t pos = 0;
    while ((pos = text.find("\"path\"", pos)) != std::string::npos) {
        pos += 6;
        const std::size_t a = text.find('"', pos);
        if (a == std::string::npos) break;
        std::string val;
        std::size_t i = a + 1;
        for (; i < text.size() && text[i] != '"'; ++i) {
            if (text[i] == '\\' && i + 1 < text.size()) ++i;   // double backslash -> one
            val.push_back(text[i]);
        }
        pos = i;
        const int n = MultiByteToWideChar(CP_UTF8, 0, val.data(), static_cast<int>(val.size()),
                                          nullptr, 0);
        std::wstring w(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
        if (n > 0) MultiByteToWideChar(CP_UTF8, 0, val.data(), static_cast<int>(val.size()), w.data(), n);
        if (!w.empty() && std::find(libs.begin(), libs.end(), w) == libs.end()) libs.push_back(w);
    }
    return libs;
}

}  // namespace

GameLocation find_game() {
    GameLocation loc;
    const std::wstring exe_name = kTargetExe;

    wchar_t env[MAX_PATH * 2]{};
    const DWORD n = GetEnvironmentVariableW(L"STORMWORKS_DIR", env, static_cast<DWORD>(std::size(env)));
    if (n > 0 && n < std::size(env)) {
        const std::wstring p = join(env, exe_name);
        loc.searched.push_back(p);
        if (file_exists(p)) {
            loc.exe = p;
            loc.source = L"environment variable STORMWORKS_DIR";
            return loc;
        }
        loc.env_ignored = std::wstring(env, n);
    }

    std::wstring steam = reg_string(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (steam.empty()) {
        steam = reg_string(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
    }
    for (const auto& lib : steam_libraries(steam)) {
        const std::wstring p = join(lib, L"steamapps\\common\\Stormworks\\" + exe_name);
        loc.searched.push_back(p);
        if (file_exists(p)) {
            loc.exe = p;
            loc.source = L"Steam library " + lib;
            return loc;
        }
    }

    const std::wstring def = kDefaultExePath;
    loc.searched.push_back(def);
    if (file_exists(def)) {
        loc.exe = def;
        loc.source = L"default path";
    }
    return loc;
}

}  // namespace sw
