// shader.hpp - feed your own shader: textures for `uniform sampler2D`, and numbers for float / vec2..4 / int uniforms.
// Everything here is callable from ANY thread (the mod thread is fine); the work is done on the render thread at the next
// frame end. Built on sw/render.hpp (REPORT 3.193).
//
// A shader mod (AGENTS.md section 25) is a mod folder with edited .glsl files in Graphics\shaders. The game sets only the
// uniforms its own C++ classes know; a uniform you add keeps whatever value you give it. This header gives it one:
//
//   // in your .glsl:   uniform sampler2D u_noise;   uniform float u_strength;   uniform vec3 u_tint;
//
//   #include "sw/mod.hpp"
//   #include "sw/shader.hpp"
//
//   void sw_mod_start() {
//       if (!sw::shader::start()) return;                         // the game does not match: nothing happens
//       std::string error;
//       if (!sw::shader::texture("u_noise", L"textures/noise.png", {}, &error))   // <game>\mods\<your mod>\graphics\textures\noise.png
//           sw::mod::log("noise: %s", error.c_str());
//       sw::shader::uniform("u_strength", 0.5f);
//       sw::shader::uniform("u_tint", 1.0f, 0.8f, 0.6f);
//       while (sw::mod::sleep(100)) {
//           sw::shader::uniform("u_strength", ...);                // change it whenever you like
//       }
//   }
//
// PROVEN (tests): with a real OpenGL 4.6 context and no game, a texture given here is sampled by a shader that declares
// the sampler, on a unit of its own; the GL state around it is put back; a new picture replaces the old one on the same
// unit; a unit someone else cleared is bound again at the next frame end (native/tests/texture_uniform_test.cpp). Every GL
// function used is loaded by the game (gl_functions.tsv; swapi_test checks the table).
// NOT TRIED in a running game. Pictures: PNG, JPEG, BMP, GIF, TIFF, or a .txtr of the game. At most 16 textures.
//
// WHERE YOUR PICTURES GO: like the game's own in rom\graphics, a mod keeps them in its folder under graphics, with any
// subfolders you like: <game>\mods\<your mod>\graphics\textures\noise.png is texture("u_noise", L"textures/noise.png").
// A relative path is looked up by graphics_file() (see the end of this file); a full path is used as it is.
#pragma once

#include "sw/render.hpp"

#include <cstdint>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace sw::shader {

using TextureOptions = sw::render::TextureOptions;

// Starts watching the frame end of the game. Call once in sw_mod_start; false when the game does not match.
inline bool start() { return sw::render::watch(); }

// ---- textures -----------------------------------------------------------------------------------------------------------
// Shows the picture in the sampler `name` of every shader that declares it. Calling it again with the same name swaps the
// picture. `options`: repeat (else clamp), linear (else nearest), mipmaps, srgb (a colour picture; leave it off for data
// such as noise or normals).
// `path`: relative to your mod's graphics folder (L"textures/noise.png", subfolders allowed, / or \), or a full path.
inline std::wstring graphics_file(const std::wstring& relative);
inline std::wstring graphics_tried(const std::wstring& relative);
inline bool texture(const char* name, const std::wstring& path, const TextureOptions& options = {}, std::string* error = nullptr) {
    const bool full = (path.size() > 1 && path[1] == L':') || (!path.empty() && (path[0] == L'\\' || path[0] == L'/') &&
                                                                path.size() > 1 && (path[1] == L'\\' || path[1] == L'/'));
    if (full) return sw::render::texture_uniform(name, path, options, error);
    const std::wstring found = graphics_file(path);
    if (found.empty()) {
        if (error) {
            const std::wstring tried = graphics_tried(path);
            const int n = WideCharToMultiByte(CP_UTF8, 0, tried.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string text(n > 1 ? static_cast<size_t>(n - 1) : 0, '\0');
            if (n > 1) WideCharToMultiByte(CP_UTF8, 0, tried.c_str(), -1, text.data(), n, nullptr, nullptr);
            *error = "not found, looked in: " + text;
        }
        return false;
    }
    return sw::render::texture_uniform(name, found, options, error);
}
// Your own pixels: width * height * 4 bytes, RGBA, row by row from the top.
inline bool texture(const char* name, const std::uint8_t* rgba, int width, int height, const TextureOptions& options = {}) {
    return sw::render::texture_uniform_rgba(name, rgba, width, height, options);
}
inline bool texture(const char* name, const std::vector<std::uint8_t>& rgba, int width, int height, const TextureOptions& options = {}) {
    return rgba.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u &&
           sw::render::texture_uniform_rgba(name, rgba.data(), width, height, options);
}
// Takes the texture away again.
inline void remove_texture(const char* name) { sw::render::remove_texture_uniform(name); }
// The texture unit of `name` once it is on the graphics card (after the next frame end), else -1.
inline int texture_unit(const char* name) { return sw::render::texture_unit(name); }

// ---- numbers ------------------------------------------------------------------------------------------------------------
inline bool uniform(const char* name, float x) { return sw::render::keep_uniform(name, &x, 1); }
inline bool uniform(const char* name, float x, float y) {
    const float v[2] = {x, y};
    return sw::render::keep_uniform(name, v, 2);
}
inline bool uniform(const char* name, float x, float y, float z) {
    const float v[3] = {x, y, z};
    return sw::render::keep_uniform(name, v, 3);
}
inline bool uniform(const char* name, float x, float y, float z, float w) {
    const float v[4] = {x, y, z, w};
    return sw::render::keep_uniform(name, v, 4);
}
inline bool uniform(const char* name, int value) { return sw::render::keep_uniform_int(name, value); }
inline bool uniform(const char* name, bool value) { return sw::render::keep_uniform_int(name, value ? 1 : 0); }
// Stops setting a number uniform (the shader keeps the last value).
inline void remove_uniform(const char* name) { sw::render::keep_uniform(name, nullptr, 0); }

// ---- your mod's files ---------------------------------------------------------------------------------------------------
// swinject does not load your DLL where it lies: it loads a copy %TEMP%\swmods\<name>.<n>.dll (so the next build can
// overwrite the original). The folder of the DLL is therefore no place for pictures. Your mod folder is
// <game>\mods\<mod>\, where <mod> is the name of SW_MOD("...") or the file name of the DLL without ".dll" / ".<n>.dll".
namespace detail {

inline bool is_file(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
inline bool is_folder(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
inline std::wstring parent(const std::wstring& p) {
    const auto slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : p.substr(0, slash);
}
inline std::wstring leaf(const std::wstring& p) {
    const auto slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? p : p.substr(slash + 1);
}
inline std::wstring lower(std::wstring s) {
    for (auto& ch : s) ch = static_cast<wchar_t>(towlower(ch));
    return s;
}
inline std::wstring widen(const char* s) {
    if (!s || !*s) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring w(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    return w;
}
// "textures/noise.png" -> "textures\noise.png"; a leading separator and "graphics\" are dropped.
inline std::wstring clean(std::wstring rel) {
    for (auto& ch : rel) if (ch == L'/') ch = L'\\';
    while (!rel.empty() && rel[0] == L'\\') rel.erase(0, 1);
    if (lower(rel.substr(0, 9)) == L"graphics\\") rel.erase(0, 9);
    return rel;
}
inline HMODULE self() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&self), &module);
    return module;
}
inline std::wstring module_path(HMODULE module) {
    wchar_t path[MAX_PATH * 2] = {};
    const DWORD n = GetModuleFileNameW(module, path, static_cast<DWORD>(MAX_PATH * 2));
    return (n == 0 || n >= MAX_PATH * 2) ? std::wstring() : std::wstring(path, n);
}
// The names your mod folder may have: SW_MOD("name") (exported as swmod_name), and the DLL file name before the first dot.
inline std::vector<std::wstring> mod_names(HMODULE module) {
    std::vector<std::wstring> names;
    using name_fn = const char* (*)();
    if (module) {
        if (auto fn = reinterpret_cast<name_fn>(reinterpret_cast<void*>(GetProcAddress(module, "swmod_name"))))
            if (auto w = widen(fn()); !w.empty()) names.push_back(w);
        std::wstring stem = leaf(module_path(module));
        stem = stem.substr(0, stem.find(L'.'));
        if (!stem.empty() && (names.empty() || lower(names[0]) != lower(stem))) names.push_back(stem);
    }
    return names;
}
// The mod folders the launcher lists for the virtual file system (%APPDATA%\DeltaVModdingLauncher\vfs_folders.txt).
inline std::vector<std::wstring> launcher_folders() {
    std::vector<std::wstring> out;
    wchar_t appdata[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH) == 0) return out;
    const std::wstring list = std::wstring(appdata) + L"\\DeltaVModdingLauncher\\vfs_folders.txt";
    HANDLE f = CreateFileW(list.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return out;
    std::string text;
    char buffer[4096];
    DWORD got = 0;
    while (ReadFile(f, buffer, sizeof(buffer), &got, nullptr) && got > 0 && text.size() < (1u << 20)) text.append(buffer, got);
    CloseHandle(f);
    size_t at = 0;
    while (at < text.size()) {
        size_t nl = text.find('\n', at);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(at, nl - at);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) out.push_back(widen(line.c_str()));
        at = nl + 1;
    }
    return out;
}

}  // namespace detail

// The folders graphics_file() looks in, in this order (the first that has the file wins):
//   1. <game>\mods\<mod>\graphics                 your mod folder (<mod> = SW_MOD name, then the DLL name)
//   2. <listed mod folder>\graphics               a folder of the launcher's list whose name is <mod>
//   3. <folder of the DLL>\graphics, and the folder of the DLL itself (a DLL that is loaded where it lies)
//   4. <game>\rom\graphics                         the game's own pictures
// `game` is the game folder (empty = the folder of the running exe).
inline std::vector<std::wstring> graphics_folders(const std::wstring& game = {}) {
    const HMODULE module = detail::self();
    const std::wstring root = game.empty() ? detail::parent(detail::module_path(nullptr)) : game;
    const auto names = detail::mod_names(module);
    std::vector<std::wstring> out;
    auto add = [&](const std::wstring& dir) {
        if (dir.empty()) return;
        for (const auto& have : out) if (detail::lower(have) == detail::lower(dir)) return;
        out.push_back(dir);
    };
    for (const auto& name : names) add(root + L"\\mods\\" + name + L"\\graphics");
    const auto listed = detail::launcher_folders();
    for (auto it = listed.rbegin(); it != listed.rend(); ++it)   // the last listed wins, as in the virtual file system
        for (const auto& name : names)
            if (detail::lower(detail::leaf(*it)) == detail::lower(name)) add(*it + L"\\graphics");
    const std::wstring dll_dir = detail::parent(detail::module_path(module));
    if (!dll_dir.empty()) {
        add(dll_dir + L"\\graphics");
        add(dll_dir);
    }
    add(root + L"\\rom\\graphics");
    return out;
}

// The full path of a picture of your mod: graphics_file(L"textures/noise.png") is
// <game>\mods\<mod>\graphics\textures\noise.png (subfolders allowed, / or \). Empty if no folder of
// graphics_folders() has it.
inline std::wstring graphics_file(const std::wstring& relative, const std::wstring& game) {
    const std::wstring rel = detail::clean(relative);
    if (rel.empty()) return {};
    for (const auto& dir : graphics_folders(game))
        if (detail::is_file(dir + L"\\" + rel)) return dir + L"\\" + rel;
    return {};
}
inline std::wstring graphics_file(const std::wstring& relative) { return graphics_file(relative, std::wstring()); }

// The places graphics_file() looked in for `relative`, separated by "; " (for an error message).
inline std::wstring graphics_tried(const std::wstring& relative) {
    std::wstring text;
    for (const auto& dir : graphics_folders()) text += (text.empty() ? L"" : L"; ") + dir + L"\\" + detail::clean(relative);
    return text;
}

// Your mod folder <game>\mods\<mod> (or the listed folder of that name); empty if there is none.
inline std::wstring mod_folder() {
    const auto names = detail::mod_names(detail::self());
    const std::wstring root = detail::parent(detail::module_path(nullptr));
    for (const auto& name : names)
        if (detail::is_folder(root + L"\\mods\\" + name)) return root + L"\\mods\\" + name;
    const auto listed = detail::launcher_folders();
    for (auto it = listed.rbegin(); it != listed.rend(); ++it)
        for (const auto& name : names)
            if (detail::lower(detail::leaf(*it)) == detail::lower(name) && detail::is_folder(*it)) return *it;
    return {};
}

// The folder the DLL was loaded from + `name`. For a mod started by swinject that is the copy in %TEMP%\swmods:
// use graphics_file() / texture(name, L"relative/path") for pictures instead.
inline std::wstring mod_file(const wchar_t* name) {
    const std::wstring dir = detail::parent(detail::module_path(detail::self()));
    return dir.empty() ? std::wstring() : dir + L"\\" + (name ? name : L"");
}

}  // namespace sw::shader
