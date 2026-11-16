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
//       if (!sw::shader::texture("u_noise", sw::shader::mod_file(L"noise.png"), {}, &error))
//           sw::mod::log("noise.png: %s", error.c_str());
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
inline bool texture(const char* name, const std::wstring& path, const TextureOptions& options = {}, std::string* error = nullptr) {
    return sw::render::texture_uniform(name, path, options, error);
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

// ---- files next to your mod ---------------------------------------------------------------------------------------------
// The full path of a file in the folder of this mod's DLL, e.g. mod_file(L"noise.png"). Empty if the DLL is not found.
inline std::wstring mod_file(const wchar_t* name) {
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&mod_file), &self))
        return {};
    wchar_t path[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    std::wstring dir(path);
    const auto slash = dir.find_last_of(L"\\/");
    return (slash == std::wstring::npos ? std::wstring() : dir.substr(0, slash + 1)) + (name ? name : L"");
}

}  // namespace sw::shader
