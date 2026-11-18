// cloud_control - where the clouds appear: areas with clouds only, areas without clouds, height, number, distance, shape, and
// single clouds where you want them (sw/clouds.hpp, REPORT 3.214).
//
// Edit <game>\mods\cloud_control\cloud_control.ini and save it: the sky follows within a second (the clouds in the sky are made
// again). Ctrl+F9 puts one cloud above the camera. The file is made with explanations the first time the mod runs.
// Clouds are drawn by your own game: other players see their own sky. Not tried in a running game.

#include "sw/clouds.hpp"
#include "sw/mod.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

SW_MOD("cloud_control")

namespace {

const char kDefaultIni[] =
    "; cloud_control - where the clouds appear. Save this file and the sky follows within a second.\r\n"
    "; Positions are world metres as in the game's map and getPlayerPos (x = east, z = north). Clouds only live within\r\n"
    "; field_radius of the camera: an area far away gets its clouds when you come near.\r\n"
    "; Ctrl+F9 in the game puts one cloud above you. What happened is in %TEMP%\\stormworks_mods.log (\"cloud_control:\").\r\n"
    "\r\n"
    "[clouds]\r\n"
    "; how many clouds at the same time, -1 = the game's\r\n"
    "max_clouds = -1\r\n"
    "; metres around the camera in which clouds live, 0 = the game's (3000)\r\n"
    "field_radius = 0\r\n"
    "; height in metres, 0 and 0 = the game's (300 to 1000)\r\n"
    "min_height = 0\r\n"
    "max_height = 0\r\n"
    "; 0 = random shapes (the game's), 2 to 8 = always meshes/cloud_02.mesh .. cloud_08.mesh\r\n"
    "shape = 0\r\n"
    "\r\n"
    "[only_in]\r\n"
    "; area = x, z, radius   -> clouds ONLY in these circles (as many lines as you like). No line = everywhere.\r\n"
    ";area = 0, 0, 20000\r\n"
    "\r\n"
    "[never_in]\r\n"
    "; area = x, z, radius   -> NO clouds in these circles\r\n"
    ";area = 0, 0, 3000\r\n"
    "\r\n"
    "[spawn]\r\n"
    "; cloud = x, height, z  -> one cloud there every time this file is saved (near the camera, see above)\r\n"
    ";cloud = 500, 700, 500\r\n";

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

// "a, b, c" -> three numbers
bool numbers(const std::string& text, double* out, int count) {
    std::string rest = text;
    for (int i = 0; i < count; ++i) {
        const std::size_t comma = rest.find(',');
        const std::string part = trim(comma == std::string::npos ? rest : rest.substr(0, comma));
        char* end = nullptr;
        out[i] = std::strtod(part.c_str(), &end);
        if (part.empty() || !end || *end) return false;
        if (i + 1 < count) {
            if (comma == std::string::npos) return false;
            rest = rest.substr(comma + 1);
        } else if (comma != std::string::npos) {
            return false;
        }
    }
    return true;
}

unsigned long long fingerprint(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) return 0;
    return (static_cast<unsigned long long>(a.ftLastWriteTime.dwHighDateTime) << 32 | a.ftLastWriteTime.dwLowDateTime) ^ a.nFileSizeLow;
}

struct Parsed {
    sw::clouds::Rules rules;
    std::vector<std::vector<double>> spawns;
    std::vector<std::string> problems;
};

Parsed read(const std::wstring& file) {
    Parsed p;
    std::ifstream in(file, std::ios::binary);
    std::string line, section;
    int number = 0;
    while (in && std::getline(in, line)) {
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
        const std::string where = "line " + std::to_string(number) + ": ";
        double v[3] = {};
        if (section == "clouds") {
            if (!numbers(value, v, 1)) {
                p.problems.push_back(where + key + " needs a number");
                continue;
            }
            if (key == "max_clouds") p.rules.max_clouds = static_cast<int>(v[0]);
            else if (key == "field_radius") p.rules.field_radius = static_cast<float>(v[0]);
            else if (key == "min_height") p.rules.min_height = static_cast<float>(v[0]);
            else if (key == "max_height") p.rules.max_height = static_cast<float>(v[0]);
            else if (key == "shape") p.rules.mesh = v[0] >= 2 && v[0] <= 8 ? static_cast<int>(v[0]) - 2 : -1;
            else p.problems.push_back(where + "unknown setting " + key);
        } else if (section == "only_in" || section == "never_in") {
            if (key != "area" || !numbers(value, v, 3) || v[2] <= 0) {
                p.problems.push_back(where + "write area = x, z, radius");
                continue;
            }
            (section == "only_in" ? p.rules.only_in : p.rules.never_in).push_back({v[0], v[1], v[2]});
        } else if (section == "spawn") {
            if (key != "cloud" || !numbers(value, v, 3)) {
                p.problems.push_back(where + "write cloud = x, height, z");
                continue;
            }
            p.spawns.push_back({v[0], v[1], v[2]});
        } else {
            p.problems.push_back(where + "outside a known section");
        }
    }
    if (p.rules.max_height > 0 && p.rules.max_height <= p.rules.min_height) p.problems.push_back("max_height must be above min_height - the game's heights are used");
    return p;
}

bool key_pressed() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid != GetCurrentProcessId()) return false;
    return (GetAsyncKeyState(VK_F9) & 1) && (GetAsyncKeyState(VK_CONTROL) & 0x8000);
}

}  // namespace

void sw_mod_start() {
    std::string error;
    if (!sw::clouds::install(error)) {
        sw::mod::log("cloud_control: not active (%s).", error.c_str());
        return;
    }
    const std::wstring folder = game_folder() + L"\\mods\\cloud_control";
    const std::wstring ini = folder + L"\\cloud_control.ini";
    CreateDirectoryW(folder.c_str(), nullptr);
    if (GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::ofstream out(ini, std::ios::binary);
        out << kDefaultIni;
        sw::mod::log("cloud_control: made %ls - edit and save it, the sky follows.", ini.c_str());
    }

    unsigned long long print = 0;
    bool first = true;
    int ticks = 0;
    while (sw::mod::sleep(250)) {
        const unsigned long long now = fingerprint(ini);
        if (first || now != print) {
            print = now;
            Parsed p = read(ini);
            for (const auto& problem : p.problems) sw::mod::log("cloud_control: %ls %s", ini.c_str(), problem.c_str());
            sw::clouds::set_rules(p.rules);
            if (!first) sw::clouds::respawn_all();   // the clouds in the sky now follow the new rules too
            int placed = 0;
            for (const auto& s : p.spawns) placed += sw::clouds::spawn_at(s[0], s[1], s[2]) ? 1 : 0;
            sw::mod::log("cloud_control: %s - %zu area(s) with clouds only, %zu without clouds, max %d, radius %.0f m, height %s, %d cloud(s) placed.",
                         first ? "start" : "the ini changed", p.rules.only_in.size(), p.rules.never_in.size(), p.rules.max_clouds,
                         p.rules.field_radius, p.rules.max_height > p.rules.min_height ? (std::to_string(static_cast<int>(p.rules.min_height)) + ".." +
                                                                                          std::to_string(static_cast<int>(p.rules.max_height)) + " m").c_str()
                                                                                       : "of the game",
                         placed);
            first = false;
        }
        if (key_pressed()) {
            const auto info = sw::clouds::info();
            if (!info.active) {
                sw::mod::log("cloud_control: Ctrl+F9 - no cloud field yet (load a world first).");
            } else if (sw::clouds::spawn_at(info.centre[0], 650.0, info.centre[2])) {
                sw::mod::log("cloud_control: Ctrl+F9 - a cloud at x %.0f, z %.0f, 650 m high.", info.centre[0], info.centre[2]);
            }
        }
        if (++ticks % 240 == 0) {   // once a minute
            const auto info = sw::clouds::info();
            if (info.active)
                sw::mod::log("cloud_control: %d of %d clouds (game: %d), radius %.0f m; since start %llu made, %llu moved, %llu taken away, %llu placed.",
                             info.clouds, info.max_clouds, info.game_max_clouds, info.field_radius, info.spawned, info.moved, info.removed, info.placed);
        }
    }
}

void sw_mod_stop() { sw::clouds::uninstall(); }
