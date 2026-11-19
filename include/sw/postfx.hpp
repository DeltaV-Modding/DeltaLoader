// postfx.hpp - post-processing effects for the game, written as small GLSL shaders, in two places of the frame.
//
// THE TWO STAGES (REPORT 3.225, 3.226):
//   Stage::hdr     - BEFORE the tonemap, after ALL geometry of the world is drawn (opaque parts, ocean, glass, particles, light
//                    sources) and after the anti-aliasing. The picture is the HDR scene colour (RGBA16F). No HUD, no menus.
//                    Inputs: colour, depth, normals, albedo and velocity of the G-buffer, and every uniform of the game's `light`
//                    shader your effect declares by the same name (sun_light_direction, mat_view_proj_inverse, camera_near ...).
//                    The place: the entry of shader_tonemap_draw 0x68e8b0 called from the frame 0x6dd94f (main view only):
//                    its 3rd argument is the target the tonemap reads, its 4th is the pipeline + 0x48.
//   Stage::screen  - at the end of the frame (the swap 0x90740): the FINAL picture after tonemap and AA, 8 bits per channel,
//                    with the HUD and the menus on it.
//   A source picks its stage with a line `#pragma stage hdr` (or `#pragma stage screen`, the default); add() takes it too.
//
// THE SHORTEST EFFECT - a file or a string with one function:
//
//     uniform float u_amount = 1.0;   // a default; sw::postfx::set("grey", "u_amount", 0.5f) or the ini changes it
//     vec4 effect(vec4 color, vec2 uv) {
//         float grey = dot(color.rgb, vec3(0.299, 0.587, 0.114));
//         return vec4(mix(color.rgb, vec3(grey), u_amount), 1.0);
//     }
//
//   Around it the library declares (use them, do not declare them again):
//     both stages   uniform sampler2D u_scene;      the picture so far (hdr: linear HDR colour, screen: display colour)
//                   uniform vec2      u_resolution; its size in pixels
//                   uniform float     u_time;       seconds since install()
//                   uniform int       u_frame;      frames since install()
//                   in vec2           v_uv;         0..1, (0,0) at the lower left
//                   vec4 scene(vec2 uv), vec2 texel()
//     hdr only      uniform sampler2D u_depth;      the depth of the scene target: the G-buffer depth copied in, plus what
//                                                   the forward passes (ocean, glass) wrote - falls back to u_gdepth
//                   uniform sampler2D u_gdepth;     the G-buffer depth (opaque geometry only); both are the game's
//                                                   logarithmic depth, see decode_depth_linear() below
//                   uniform sampler2D u_normal;     attachment 0 (RGBA16F): the game's `gnormal` (normal + light factor)
//                   uniform sampler2D u_albedo;     attachment 1 (RGBA8): the game's `gcolor`
//                   uniform sampler2D u_velocity;   attachment 2 (RG16F): screen-space motion
//                   The names of the game's light shader work as well: `uniform sampler2D gdepth;` (= u_gdepth), gnormal,
//                   gcolor, gvelocity get the same textures, so code from light.glslf can be copied as it is.
//   `#include "depth_utils.glslh"` (any file name) is resolved in the effect's own folder first, then in the game's
//   rom\graphics\shaders - the game's own decode_depth_linear() is then there. A `#version` line replaces 330. A source with
//   its own `void main()` is used as it is (declare what you use).
//   The game's uniforms: any `uniform` of your effect whose name is not u_* and that you did not set() is copied every frame from
//   the game's `light` program if that program has a uniform of the same name (float, vecN, int, bool, mat3, mat4, arrays).
//
// IN A MOD:
//
//     #include "sw/postfx.hpp"
//     void sw_mod_start() {
//         std::string error;
//         if (!sw::postfx::install(&error)) { sw::mod::log("postfx: %s", error.c_str()); return; }
//         sw::postfx::add_preset("vignette");                       // built in, see presets()
//         sw::postfx::add_file("rays", L"C:\\...\\rays.glsl");      // "#pragma stage hdr" inside; reloaded by sync()
//         sw::postfx::set("vignette", "u_amount", 0.6f);
//     }
//     void sw_mod_stop() { sw::postfx::uninstall(); }
//
// Compile errors of the driver are kept per effect (effects()[i].error, line numbers of your file); a broken effect is skipped.
// Every OpenGL state the library changes is put back. Tested with a real OpenGL context (swpostfx_test); NOT tried in a running
// game. The hdr stage relies on 1.15.27 facts that install() checks in the bytes (status().hdr_error says why it is off).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sw::postfx {

enum class Stage { screen = 0, hdr = 1 };

// Hooks the frame end (sw::render::watch()) and the tonemap pass. Safe to call twice. False when not even the frame end could
// be hooked (reason in `error`); when only the hdr stage is missing, true and status().hdr_error says why.
bool install(std::string* error = nullptr);

// Stops drawing. The GL objects are deleted at the next frame end; call it from sw_mod_stop.
void uninstall();

// Adds or replaces an effect. `order`: lower runs first (ties: by name). A `#pragma stage hdr|screen` line in the source wins
// over `stage`.
bool add(const std::string& name, const std::string& glsl, int order = 0, Stage stage = Stage::screen);

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

// Where `#include "..."` is looked up after the effect's own folder. Default: <folder of the game's exe>\rom\graphics\shaders.
void set_include_folder(const std::wstring& folder);

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

// Built-in effects. add_preset(name) adds one under its own name; `about` lists its stage, uniforms and defaults.
struct Preset {
    const char* name;
    const char* about;     // one line, with the uniforms
    const char* source;    // the GLSL (with its #pragma stage)
};
const std::vector<Preset>& presets();
bool add_preset(const std::string& preset, int order = 0, const std::string& as_name = "");

struct Info {
    std::string name;
    int order = 0;
    Stage stage = Stage::screen;
    bool enabled = true;
    bool compiled = false;          // compiled and linked at the last frame of its stage
    std::string error;              // the driver's message when it failed (or a missing #include)
    std::wstring file;              // empty for add() / add_preset()
};
std::vector<Info> effects();

struct Status {
    bool installed = false;
    bool hdr_ready = false;          // the tonemap pass is hooked
    std::string hdr_error;           // why the hdr stage is off
    std::uint64_t frames_drawn = 0;  // frames in which at least one screen effect ran
    std::uint64_t hdr_frames = 0;    // frames in which at least one hdr effect ran
    int width = 0, height = 0;       // size of the last picture
    unsigned light_program = 0;      // the game's light program the uniforms are copied from (0 = not found yet)
    std::string gl_error;            // why nothing can be drawn (no GL functions, no context)
};
Status status();

// The fragment shader the driver gets for an effect source (prelude, #includes resolved, main() around effect()). `folder` is
// the effect's own folder for #include (may be empty).
std::string fragment_source(const std::string& effect_glsl, Stage stage = Stage::screen, const std::wstring& folder = L"");

// The stage a source asks for with `#pragma stage ...` (screen without one).
Stage stage_of(const std::string& effect_glsl, Stage fallback = Stage::screen);

namespace testing {
// Without a game: switches the library on as install() would (there is no frame end to hook).
void start();
// Without a game: runs the screen stage into `framebuffer` (0 = the window) of the given size, with the GL context that is
// current on this thread; resolver finds the GL functions. Returns false when nothing was drawn.
bool run_frame(void* (*resolver)(const char* name), unsigned framebuffer, int width, int height);
// Without a game: runs the hdr stage on a scene texture with the given G-buffer textures (0 = none), as the tonemap hook does.
// `light_program` stands in for the game's light program (0 = no uniforms to copy).
struct HdrInputs {
    unsigned scene_texture = 0, scene_fbo = 0;
    int width = 0, height = 0;
    unsigned depth = 0, scene_depth = 0, normal = 0, albedo = 0, velocity = 0;
    unsigned light_program = 0;
};
bool run_hdr(void* (*resolver)(const char* name), const HdrInputs& in);
}  // namespace testing

}  // namespace sw::postfx
