// postfx.hpp - post-processing effects over the finished picture of the game, written as small GLSL shaders.
//
// What it does (REPORT 3.225):
//   At the end of every frame (the hook on the swap 0x90740, sw/render.hpp) the library copies the picture the game drew
//   into framebuffer 0 into a texture and draws your effects over it, one after the other, each reading the result of the
//   one before. Every OpenGL state it touches is put back. It runs on the game's render thread with the game's context.
//
// What it is NOT (be honest with your players):
//   * The picture is the FINAL one: after the game's tonemap and anti-aliasing, 8 bits per channel, with the HUD and the
//     menus on it. There is no HDR, no depth and no "picture without the HUD" here; a pass before the tonemap needs a hook
//     that does not exist yet (sw/render.hpp, upscaler_inputs(), says where it would go).
//   * Not tried in a running game. The drawing is tested with a real OpenGL context (swpostfx_test).
//
// The shortest effect - a file or a string with one function:
//
//     uniform float u_amount = 1.0;   // a default; sw::postfx::set("grey", "u_amount", 0.5f) changes it
//     vec4 effect(vec4 color, vec2 uv) {
//         float grey = dot(color.rgb, vec3(0.299, 0.587, 0.114));
//         return vec4(mix(color.rgb, vec3(grey), u_amount), 1.0);
//     }
//
//   Around it the library declares (you can use them, do not declare them again):
//     uniform sampler2D u_scene;      the picture so far
//     uniform vec2      u_resolution; its size in pixels
//     uniform float     u_time;       seconds since install()
//     uniform int       u_frame;      frames since install()
//     in vec2           v_uv;         0..1, (0,0) at the lower left
//     vec4 scene(vec2 uv)             texture(u_scene, uv)
//     vec2 texel()                    one pixel in uv units
//   A source with its own `void main()` is used as it is (#version 330 is added when it has none; it may use the names
//   above if it declares them). A `#version` line of yours replaces 330, e.g. `#version 460`.
//
// In a mod:
//
//     #include "sw/postfx.hpp"
//     void sw_mod_start() {
//         std::string error;
//         if (!sw::postfx::install(&error)) { sw::mod::log("postfx: %s", error.c_str()); return; }
//         sw::postfx::add_preset("vignette");                       // built in, see presets()
//         sw::postfx::add_file("my_look", L"C:\\...\\my_look.glsl"); // reloaded when the file changes (sync())
//         sw::postfx::set("vignette", "u_amount", 0.6f);
//     }
//     void sw_mod_stop() { sw::postfx::uninstall(); }
//
// Compile errors of the driver are kept per effect (effects()[i].error); a broken effect is skipped, the others run.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sw::postfx {

// Hooks the frame end (sw::render::watch()). Safe to call twice. False when the game does not match (reason in `error`).
bool install(std::string* error = nullptr);

// Stops drawing. The GL objects are deleted at the next frame end; call it from sw_mod_stop.
void uninstall();

// Adds or replaces an effect. `order`: lower runs first (ties: by name).
bool add(const std::string& name, const std::string& glsl, int order = 0);

// The same from a file (UTF-8 text). The file is read again by sync() when its time or size changes.
bool add_file(const std::string& name, const std::wstring& path, int order = 0, std::string* error = nullptr);

// Every *.glsl file of a folder as an effect named after the file (without .glsl). A number at the start of the file name
// is the order ("10_grade.glsl" runs before "20_vignette.glsl"); without one the order is 1000. Files that are gone are
// removed again by sync(). Returns the number of effects from that folder.
int add_folder(const std::wstring& folder);

// Reads changed files (add_file / add_folder) again and picks up new or deleted files of the folders. Call it now and then
// (shader_config does every 250 ms). Returns the number of effects that changed.
int sync();

bool remove(const std::string& name);
void clear();
bool enable(const std::string& name, bool on);
bool set_order(const std::string& name, int order);

// Uniforms of an effect: float, vec2, vec3, vec4 (count 1..4) and int. They are kept and written every frame.
bool set(const std::string& effect, const std::string& uniform, const float* values, int count);
inline bool set(const std::string& effect, const std::string& uniform, float x) { return set(effect, uniform, &x, 1); }
inline bool set(const std::string& effect, const std::string& uniform, float x, float y) {
    const float v[2] = {x, y};
    return set(effect, uniform, v, 2);
}
inline bool set(const std::string& effect, const std::string& uniform, float x, float y, float z) {
    const float v[3] = {x, y, z};
    return set(effect, uniform, v, 3);
}
inline bool set(const std::string& effect, const std::string& uniform, float x, float y, float z, float w) {
    const float v[4] = {x, y, z, w};
    return set(effect, uniform, v, 4);
}
bool set_int(const std::string& effect, const std::string& uniform, int value);

// Built-in effects. add_preset(name) adds one under its own name; the comment of each lists its uniforms and defaults.
struct Preset {
    const char* name;
    const char* about;     // one line, with the uniforms
    const char* source;    // the GLSL
};
const std::vector<Preset>& presets();
bool add_preset(const std::string& preset, int order = 0, const std::string& as_name = "");

struct Info {
    std::string name;
    int order = 0;
    bool enabled = true;
    bool compiled = false;          // compiled and linked at the last frame end
    std::string error;              // the driver's message when it failed
    std::wstring file;              // empty for add() / add_preset()
};
std::vector<Info> effects();

struct Status {
    bool installed = false;
    std::uint64_t frames_drawn = 0;  // frames in which at least one effect ran
    int width = 0, height = 0;       // size of the last picture
    std::string gl_error;            // why nothing can be drawn (no GL functions, no context)
};
Status status();

// The fragment shader the driver gets for an effect source: the prelude and main() around effect(), or the source itself
// when it has its own main(). Useful to see the line numbers of a driver message.
std::string fragment_source(const std::string& effect_glsl);

namespace testing {
// Without a game: switches the library on as install() would (there is no frame end to hook).
void start();
// Without a game: runs the frame-end work into `framebuffer` (0 = the window) of the given size, with the GL context that is
// current on this thread; resolver finds the GL functions. Returns false when nothing was drawn.
bool run_frame(void* (*resolver)(const char* name), unsigned framebuffer, int width, int height);
}  // namespace testing

}  // namespace sw::postfx
