// shader_config - your own #define macros and a newer GLSL version for the game's shaders, applied while the game runs
// (sw/shader_build.hpp, REPORT 3.213).
//
// For players and shader authors (e.g. the settings block of a shader pack): edit <game>\mods\shader_config\shader_config.ini,
// save it, and the shaders are rebuilt within a second - no restart, no reloading the save. Ctrl+F8 rebuilds by hand.
// An edited .glsl file in rom\graphics\shaders (or a shader mod laid over it by the launcher) is picked up the same way.
//
// The file is made with explanations the first time the mod runs. Not tried in a running game.

#include "sw/mod.hpp"
#include "sw/shader_build.hpp"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

SW_MOD("shader_config")

namespace {

const char kDefaultIni[] =
    "; shader_config - your own #define macros and the GLSL version of the game's shaders.\r\n"
    "; Save this file and the shaders are rebuilt within a second (no restart, no reloading the save).\r\n"
    "; Ctrl+F8 in the game rebuilds them by hand. What happened is in %TEMP%\\stormworks_mods.log (lines \"shader_config:\").\r\n"
    "\r\n"
    "[general]\r\n"
    "; 0 = the game's (330). 460 = OpenGL 4.6 features, if your graphics driver has them (current NVIDIA / AMD / Intel do).\r\n"
    "glsl_version = 0\r\n"
    "; core (as the game) or compatibility\r\n"
    "profile = core\r\n"
    "; 1 = rebuild when this file or a shader file in rom\\graphics\\shaders changes\r\n"
    "watch = 1\r\n"
    "\r\n"
    "[defines]\r\n"
    "; NAME = value   -> \"#define NAME value\" in every shader. If the game or the shader file already has \"#define NAME\",\r\n"
    ";                   that line is replaced, so a shader pack can be configured here without editing its files.\r\n"
    "; NAME =         -> \"#define NAME\" (no value)\r\n"
    "; NAME = !       -> removes \"#define NAME\" of the game or of the file (the shader takes its #else / #ifndef branch)\r\n"
    ";EXAMPLE_SETTING = 1\r\n"
    "\r\n"
    "; Macros for one file only: a section with the file name, e.g.\r\n"
    ";[light.glslf]\r\n"
    ";EXAMPLE_FOG_STEPS = 32\r\n"
    "; or for one kind of file: [.glslf] (fragment), [.glslv] (vertex), [.glslg] (geometry).\r\n";

std::wstring game_folder() {
    wchar_t exe[MAX_PATH * 2]{};
    const DWORD n = GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
    std::wstring path(exe, n);
    const std::size_t at = path.find_last_of(L"\\/");
    return at == std::wstring::npos ? std::wstring() : path.substr(0, at);
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

// a cheap fingerprint of a file or of every file of a folder: size and time
unsigned long long fingerprint(const std::wstring& path, bool folder) {
    unsigned long long sum = 0;
    WIN32_FIND_DATAW data{};
    HANDLE h = FindFirstFileW(folder ? (path + L"\\*.glsl*").c_str() : path.c_str(), &data);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        sum = sum * 1099511628211ull + data.ftLastWriteTime.dwLowDateTime + (static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime) << 32) +
              data.nFileSizeLow;
    } while (folder && FindNextFileW(h, &data));
    FindClose(h);
    return sum;
}

struct Config {
    int version = 0;
    std::string profile;
    bool watch = true;
    int macros = 0;
    std::vector<std::string> problems;
};

// reads the ini and hands every macro to sw::shader_build (after clear())
Config apply(const std::wstring& file) {
    Config c;
    std::ifstream in(file, std::ios::binary);
    sw::shader_build::clear();
    if (!in) return c;
    std::string line, section;
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        const std::size_t semi = line.find(';');
        if (semi != std::string::npos) line = line.substr(0, semi);
        line = trim(line);
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }
        const std::size_t eq = line.find('=');
        const std::string key = trim(eq == std::string::npos ? line : line.substr(0, eq));
        const std::string value = eq == std::string::npos ? std::string() : trim(line.substr(eq + 1));
        char where[64];
        std::snprintf(where, sizeof(where), "line %d", number);
        if (section == "general") {
            if (key == "glsl_version") c.version = std::atoi(value.c_str());
            else if (key == "profile") c.profile = value == "core" ? "" : value;
            else if (key == "watch") c.watch = value != "0";
            else c.problems.push_back(std::string(where) + ": unknown setting " + key);
            continue;
        }
        const std::string target = section == "defines" ? std::string() : section;
        if (section.empty()) {
            c.problems.push_back(std::string(where) + ": a macro before [defines]");
            continue;
        }
        const bool ok = value == "!" ? sw::shader_build::undefine(key, target) : sw::shader_build::define(key, value, target);
        if (ok) ++c.macros;
        else c.problems.push_back(std::string(where) + ": \"" + key + "\" is not a macro name (letters, digits, _)");
    }
    if (!sw::shader_build::glsl_version(c.version, c.profile)) {
        c.problems.push_back("glsl_version " + std::to_string(c.version) + " / profile \"" + c.profile + "\" is not valid - the game's is used");
        sw::shader_build::glsl_version(0);
        c.version = 0;
    }
    return c;
}

bool key_pressed() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid != GetCurrentProcessId()) return false;
    return (GetAsyncKeyState(VK_F8) & 1) && (GetAsyncKeyState(VK_CONTROL) & 0x8000);
}

}  // namespace

void sw_mod_start() {
    std::string error;
    if (!sw::shader_build::install(error)) {
        sw::mod::log("shader_config: not active (%s).", error.c_str());
        return;
    }
    const std::wstring game = game_folder();
    const std::wstring folder = game + L"\\mods\\shader_config";
    const std::wstring ini = folder + L"\\shader_config.ini";
    const std::wstring shaders = game + L"\\rom\\graphics\\shaders";
    CreateDirectoryW(folder.c_str(), nullptr);
    if (GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::ofstream out(ini, std::ios::binary);
        out << kDefaultIni;
        sw::mod::log("shader_config: made %ls - edit and save it, the shaders follow.", ini.c_str());
    }

    unsigned long long ini_print = 0, shader_print = 0;
    bool watch = true, first = true, waiting = false;
    unsigned long long rebuilds_before = 0;
    int ticks_waiting = 0;
    while (sw::mod::sleep(250)) {
        const unsigned long long ip = fingerprint(ini, false);
        const unsigned long long sp = watch ? fingerprint(shaders, true) : shader_print;
        const bool by_key = key_pressed();
        if (first || ip != ini_print || sp != shader_print || by_key) {
            const bool file_changed = !first && ip != ini_print;
            ini_print = ip;
            shader_print = sp;
            const Config c = apply(ini);
            watch = c.watch;
            for (const auto& p : c.problems) sw::mod::log("shader_config: %ls %s", ini.c_str(), p.c_str());
            // at the start only rebuild when there is something to change (the game built its shaders already)
            if (!first || c.macros > 0 || c.version != 0) {
                rebuilds_before = sw::shader_build::status().rebuilds;
                sw::shader_build::rebuild();
                waiting = true;
                ticks_waiting = 0;
                sw::mod::log("shader_config: %s - %d macro(s), GLSL %s, rebuilding the shaders.",
                             by_key ? "Ctrl+F8" : first ? "start" : file_changed ? "the ini changed" : "a shader file changed",
                             c.macros, c.version ? std::to_string(c.version).c_str() : "of the game (330)");
            }
            first = false;
        }
        if (waiting) {
            const auto s = sw::shader_build::status();
            if (s.rebuilds != rebuilds_before && ++ticks_waiting >= 8) {   // 2 s for the programs to be built again
                waiting = false;
                if (s.failures.empty()) {
                    sw::mod::log("shader_config: %d shader program(s) rebuilt, no errors.", s.last_programs);
                } else {
                    sw::mod::log("shader_config: %d program(s) rebuilt, %zu ERROR(S):", s.last_programs, s.failures.size());
                    for (const auto& f : s.failures) sw::mod::log("shader_config:   %s: %s", f.file.c_str(), f.message.c_str());
                    if (s.version != 0) sw::mod::log("shader_config:   (glsl_version = 0 in the ini goes back to the game's version)");
                }
            } else if (s.rebuilds == rebuilds_before && ++ticks_waiting > 40) {
                waiting = false;
                sw::mod::log("shader_config: the shaders were not rebuilt yet - they will be when a world or the menu is drawn.");
            }
        }
    }
}

void sw_mod_stop() {
    // the shaders keep the macros until they are built again; the game's GL functions go back into their slots
    sw::shader_build::uninstall();
}
