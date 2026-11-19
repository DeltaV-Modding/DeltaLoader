// shader_config - your own #define macros and a newer GLSL version for the game's shaders, applied while the game runs
// (sw/shader_build.hpp, REPORT 3.213), and post-processing effects (sw/postfx.hpp, REPORT 3.225, 3.226): before the tonemap on the HDR
// scene with depth and normals ("#pragma stage hdr", no HUD), or over the finished picture.
//
// For players and shader authors (e.g. the settings block of a shader pack): edit <game>\mods\shader_config\shader_config.ini,
// save it, and the shaders are rebuilt within a second - no restart, no reloading the save. Ctrl+F8 rebuilds by hand.
// An edited .glsl file in rom\graphics\shaders (or a shader mod laid over it by the launcher) is picked up the same way.
//
// Post-processing: the built-in presets switched on in the [postfx] section, and every *.glsl file of
// <game>\mods\shader_config\postfx (a file with `vec4 effect(vec4 color, vec2 uv)`, see postfx\README.txt). Saved files are
// picked up within a second; Ctrl+F7 switches all effects off and on. Compile errors of the driver go to the log. A change in
// [postfx] does not rebuild the game's shaders.
//
// The file is made with explanations the first time the mod runs; an older file gets the [postfx] section appended once.
// Not tried in a running game.

#include "sw/mod.hpp"
#include "sw/postfx.hpp"
#include "sw/shader_build.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
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
    "; or for one kind of file: [.glslf] (fragment), [.glslv] (vertex), [.glslg] (geometry).\r\n"
    "\r\n"
    "[postfx]\r\n"
    "; Effects over the finished picture (after the game's tonemap and anti-aliasing, with the HUD on it).\r\n"
    "; Changes here apply at once and do not rebuild the shaders. Ctrl+F7 in the game switches all effects off and on.\r\n"
    "; 1 = on, 0 = no effects at all\r\n"
    "enabled = 1\r\n"
    "; built-in effects, run in the order you list them. Before the tonemap (HDR, no HUD): exposure, bloom, sun_rays.\r\n"
    "; Over the final picture: sharpen, cas, fxaa, saturation, contrast, color_grade, tint, vignette, grain, chromatic_aberration\r\n"
    "; e.g. presets = sharpen, saturation, vignette\r\n"
    "presets =\r\n"
    "; Your own effects: every *.glsl file in the folder postfx next to this file (see postfx\\README.txt there).\r\n"
    "\r\n"
    "; The settings of an effect: a section [postfx:<name>] with its uniforms (1 to 4 numbers), e.g.\r\n"
    ";[postfx:vignette]\r\n"
    ";u_amount = 0.35\r\n"
    ";u_softness = 0.45\r\n"
    ";[postfx:sharpen]\r\n"
    ";u_amount = 0.4\r\n"
    ";[postfx:saturation]\r\n"
    ";u_amount = 1.2\r\n"
    ";[postfx:contrast]\r\n"
    ";u_contrast = 1.1\r\n"
    ";u_brightness = 0.0\r\n"
    ";[postfx:tint]\r\n"
    ";u_color = 1.0 0.97 0.92\r\n"
    ";[postfx:grain]\r\n"
    ";u_amount = 0.04\r\n"
    ";[postfx:chromatic_aberration]\r\n"
    ";u_amount = 1.5\r\n"
    ";[postfx:cas]\r\n"
    ";u_amount = 0.5\r\n"
    ";[postfx:fxaa]\r\n"
    ";u_amount = 0.75\r\n"
    ";[postfx:color_grade]\r\n"
    ";u_lift = 0 0 0\r\n"
    ";u_gamma = 1 1 1\r\n"
    ";u_gain = 1 1 1\r\n"
    ";u_temperature = 0\r\n"
    ";[postfx:exposure]\r\n"
    ";u_exposure = 1.0\r\n"
    ";[postfx:bloom]\r\n"
    ";u_threshold = 1.0\r\n"
    ";u_amount = 0.15\r\n"
    ";u_radius = 6\r\n"
    ";[postfx:sun_rays]\r\n"
    ";u_amount = 0.35\r\n"
    ";u_decay = 0.96\r\n";

const char kPostfxReadme[] =
    "Your own post-processing effects for shader_config (sw/postfx.hpp).\r\n"
    "\r\n"
    "Every *.glsl file in this folder is an effect, drawn over the finished picture at the end of every frame.\r\n"
    "A number at the start of the name is the order: 10_grade.glsl runs before 20_vignette.glsl.\r\n"
    "Save a file and it is used within a second; delete it and it is gone. Errors are in %TEMP%\\stormworks_mods.log.\r\n"
    "\r\n"
    "The shortest effect (copy it into a file named 10_example.glsl):\r\n"
    "\r\n"
    "    uniform float u_amount = 0.5;\r\n"
    "    vec4 effect(vec4 color, vec2 uv) {\r\n"
    "        float grey = dot(color.rgb, vec3(0.299, 0.587, 0.114));\r\n"
    "        return vec4(mix(color.rgb, vec3(grey), u_amount), 1.0);\r\n"
    "    }\r\n"
    "\r\n"
    "TWO STAGES. Without a line it runs over the finished picture (after tonemap and AA, with the HUD).\r\n"
    "With the line  #pragma stage hdr  it runs BEFORE the tonemap, after all geometry is drawn (ocean, glass and lights\r\n"
    "included), on the HDR scene colour, without the HUD - with depth and normals (see below).\r\n"
    "Examples to copy are in the folder examples (they are not run from there).\r\n"
    "\r\n"
    "Ready to use in every effect (do not declare them again):\r\n"
    "    u_scene        sampler2D  the picture so far\r\n"
    "    u_resolution   vec2       its size in pixels\r\n"
    "    u_time         float      seconds since the start\r\n"
    "    u_frame        int        frame counter\r\n"
    "    v_uv           vec2       0..1, (0,0) at the lower left\r\n"
    "    scene(uv)      vec4       the picture at uv\r\n"
    "    texel()        vec2       one pixel in uv units\r\n"
    "Only with #pragma stage hdr:\r\n"
    "    u_depth        sampler2D  depth of the scene incl. ocean and glass (the game's logarithmic depth)\r\n"
    "    u_gdepth       sampler2D  depth of the G-buffer (opaque geometry only), also as gdepth\r\n"
    "    u_normal       sampler2D  the G-buffer normals (gnormal), u_albedo (gcolor), u_velocity (gvelocity)\r\n"
    "    #include \"depth_utils.glslh\"  gives decode_depth_linear() of the game (any file of rom\\graphics\\shaders works)\r\n"
    "    Any uniform of the game's light shader is filled in by name, e.g.:\r\n"
    "        uniform vec3 sun_light_direction;   (points AWAY from the sun)\r\n"
    "        uniform mat4 mat_view_proj_inverse;  uniform vec3 sun_light_color;  uniform float camera_far;\r\n"
    "\r\n"
    "A first line \"#version 460\" uses a newer GLSL. A file with its own \"void main()\" is used as it is.\r\n"
    "Uniforms of a file are set in shader_config.ini, section [postfx:<file name without .glsl>].\r\n"
    "If the hdr stage is not available on this game version, the log says so; hdr effects then do nothing.\r\n";

// Examples written to postfx\examples once (not run from there: copy one up into postfx to use it).
const char kExampleSunRays[] =
    "// Light shafts from the sun, drawn BEFORE the tonemap, after the ocean, glass and lights are drawn.\r\n"
    "// Copy this file into the postfx folder. Settings: [postfx:sun_rays] in shader_config.ini.\r\n"
    "#pragma stage hdr\r\n"
    "#include \"depth_utils.glslh\"\r\n"
    "uniform float u_amount = 0.35;\r\n"
    "uniform float u_decay = 0.96;\r\n"
    "uniform int u_samples = 48;\r\n"
    "uniform vec3 sun_light_direction;    // from the game's light shader; points away from the sun\r\n"
    "uniform vec3 sun_light_color;\r\n"
    "uniform mat4 mat_view_proj_inverse;\r\n"
    "float sky(vec2 uv) { return decode_depth_linear(texture(u_depth, uv).r) > 20000.0 ? 1.0 : 0.0; }\r\n"
    "vec4 effect(vec4 color, vec2 uv) {\r\n"
    "    mat4 vp = inverse(mat_view_proj_inverse);\r\n"
    "    vec4 cam = mat_view_proj_inverse * vec4(0.0, 0.0, -1.0, 1.0);\r\n"
    "    vec4 sp = vp * vec4(cam.xyz / cam.w + normalize(-sun_light_direction) * 10000.0, 1.0);\r\n"
    "    if (sp.w <= 0.0) return color;\r\n"
    "    vec2 sun = sp.xy / sp.w * 0.5 + 0.5;\r\n"
    "    vec2 dstep = (sun - uv) / float(u_samples);\r\n"
    "    vec2 p = uv;\r\n"
    "    float w = 1.0, sum = 0.0;\r\n"
    "    for (int i = 0; i < 128; ++i) {\r\n"
    "        if (i >= u_samples) break;\r\n"
    "        p += dstep;\r\n"
    "        sum += sky(clamp(p, vec2(0.0), vec2(1.0))) * w;\r\n"
    "        w *= u_decay;\r\n"
    "    }\r\n"
    "    float fade = clamp(1.0 - length((sun - 0.5) * vec2(u_resolution.x / u_resolution.y, 1.0)), 0.0, 1.0);\r\n"
    "    return vec4(color.rgb + sun_light_color * sum / float(u_samples) * u_amount * fade, color.a);\r\n"
    "}\r\n";

const char kExampleDepthView[] =
    "// Debug view: the scene depth in metres as grey (0 m black, 2000 m white), the sky red. Shows what an hdr effect sees.\r\n"
    "// Copy it into postfx, look, delete it again.\r\n"
    "#pragma stage hdr\r\n"
    "#include \"depth_utils.glslh\"\r\n"
    "vec4 effect(vec4 color, vec2 uv) {\r\n"
    "    float d = decode_depth_linear(texture(u_depth, uv).r);\r\n"
    "    if (d > 20000.0) return vec4(1.0, 0.0, 0.0, color.a);\r\n"
    "    return vec4(vec3(clamp(d / 2000.0, 0.0, 1.0)), color.a);\r\n"
    "}\r\n";

const char kExampleNormalView[] =
    "// Debug view: the G-buffer normals as colours. Copy it into postfx, look, delete it again.\r\n"
    "#pragma stage hdr\r\n"
    "vec4 effect(vec4 color, vec2 uv) {\r\n"
    "    return vec4(texture(u_normal, uv).xyz * 0.5 + 0.5, color.a);\r\n"
    "}\r\n";

const char kExampleGrade[] =
    "// A warm, slightly stronger look over the final picture. Copy it into postfx.\r\n"
    "uniform float u_amount = 1.0;\r\n"
    "vec4 effect(vec4 color, vec2 uv) {\r\n"
    "    vec3 c = color.rgb;\r\n"
    "    float grey = dot(c, vec3(0.2126, 0.7152, 0.0722));\r\n"
    "    vec3 graded = mix(vec3(grey), c, 1.15) * vec3(1.04, 1.0, 0.95);\r\n"
    "    graded = (graded - 0.5) * 1.06 + 0.5;\r\n"
    "    return vec4(clamp(mix(c, graded, u_amount), 0.0, 1.0), 1.0);\r\n"
    "}\r\n";

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

std::vector<std::string> split_list(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s + ",") {
        if (c == ',' || c == ' ' || c == '\t') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    return out;
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
    std::string macro_key;   // what the shaders are built with; a change means rebuild
    bool postfx_on = true;
    std::vector<std::string> presets;
    std::map<std::string, std::vector<std::pair<std::string, std::vector<float>>>> uniforms;   // effect -> uniform -> values
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
        if (section == "postfx") {
            if (key == "enabled") c.postfx_on = value != "0";
            else if (key == "presets") c.presets = split_list(value);
            else c.problems.push_back(std::string(where) + ": unknown setting " + key + " in [postfx]");
            continue;
        }
        if (section.rfind("postfx:", 0) == 0) {
            std::vector<float> v;
            for (const std::string& part : split_list(value)) v.push_back(static_cast<float>(std::atof(part.c_str())));
            if (v.empty() || v.size() > 4) c.problems.push_back(std::string(where) + ": " + key + " needs 1 to 4 numbers");
            else c.uniforms[trim(section.substr(7))].push_back({key, v});
            continue;
        }
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
        c.macro_key += target + "|" + key + "=" + value + "\n";
        if (ok) ++c.macros;
        else c.problems.push_back(std::string(where) + ": \"" + key + "\" is not a macro name (letters, digits, _)");
    }
    if (!sw::shader_build::glsl_version(c.version, c.profile)) {
        c.problems.push_back("glsl_version " + std::to_string(c.version) + " / profile \"" + c.profile + "\" is not valid - the game's is used");
        sw::shader_build::glsl_version(0);
        c.version = 0;
    }
    c.macro_key += "v" + std::to_string(c.version) + c.profile;
    return c;
}

// the [postfx] part of the ini: presets switched on, every effect on or off, the uniforms
std::vector<std::string> g_preset_names;   // what this mod added as presets
void apply_postfx(const Config& c, bool keys_off) {
    for (const std::string& n : g_preset_names) sw::postfx::remove(n);
    g_preset_names.clear();
    int order = 0;
    for (const std::string& p : c.presets) {
        if (sw::postfx::add_preset(p, order)) g_preset_names.push_back(p);
        else sw::mod::log("shader_config: [postfx] presets: there is no preset \"%s\"", p.c_str());
        order += 10;   // the folder's files without a number come after them (1000)
    }
    const bool on = c.postfx_on && !keys_off;
    for (const auto& e : sw::postfx::effects()) sw::postfx::enable(e.name, on);
    for (const auto& [effect, list] : c.uniforms)
        for (const auto& [name, v] : list) sw::postfx::set(effect, name, v.data(), static_cast<int>(v.size()));
}

bool key_pressed(int vk) {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid != GetCurrentProcessId()) return false;
    return (GetAsyncKeyState(vk) & 1) && (GetAsyncKeyState(VK_CONTROL) & 0x8000);
}

}  // namespace

void sw_mod_start() {
    std::string error;
    const bool build_on = sw::shader_build::install(error);
    if (!build_on) sw::mod::log("shader_config: macros and GLSL version not active (%s).", error.c_str());
    std::string fx_error;
    const bool fx_on = sw::postfx::install(&fx_error);
    if (!fx_on) sw::mod::log("shader_config: post-processing not active (%s).", fx_error.c_str());
    if (!build_on && !fx_on) return;

    const std::wstring game = game_folder();
    const std::wstring folder = game + L"\\mods\\shader_config";
    const std::wstring ini = folder + L"\\shader_config.ini";
    const std::wstring shaders = game + L"\\rom\\graphics\\shaders";
    const std::wstring fx_folder = folder + L"\\postfx";
    CreateDirectoryW(folder.c_str(), nullptr);
    CreateDirectoryW(fx_folder.c_str(), nullptr);
    if (GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::ofstream out(ini, std::ios::binary);
        out << kDefaultIni;
        sw::mod::log("shader_config: made %ls - edit and save it, the shaders follow.", ini.c_str());
    } else if (fx_on) {
        // an ini of an older version: add the [postfx] part once, the rest stays as the player wrote it
        std::string text;
        {
            std::ifstream in(ini, std::ios::binary);
            std::stringstream s;
            s << in.rdbuf();
            text = s.str();
        }
        if (text.find("[postfx]") == std::string::npos) {
            std::ofstream out(ini, std::ios::binary | std::ios::app);
            const std::string all = kDefaultIni;
            out << "\r\n" << all.substr(all.find("[postfx]"));
            sw::mod::log("shader_config: added the [postfx] section to %ls.", ini.c_str());
        }
    }
    {   // the README follows the version of the mod (it is ours, not the player's)
        std::ofstream out(fx_folder + L"\\README.txt", std::ios::binary);
        out << kPostfxReadme;
    }
    const std::wstring examples = fx_folder + L"\\examples";
    if (GetFileAttributesW(examples.c_str()) == INVALID_FILE_ATTRIBUTES) {
        CreateDirectoryW(examples.c_str(), nullptr);
        const std::pair<const wchar_t*, const char*> files[] = {{L"sun_rays.glsl", kExampleSunRays},
                                                                 {L"debug_depth.glsl", kExampleDepthView},
                                                                 {L"debug_normals.glsl", kExampleNormalView},
                                                                 {L"warm_grade.glsl", kExampleGrade}};
        for (const auto& [name, text] : files) {
            std::ofstream out(examples + L"\\" + name, std::ios::binary);
            out << text;
        }
    }
    if (fx_on) {
        const auto st = sw::postfx::status();
        if (st.hdr_ready) sw::mod::log("shader_config: the hdr stage (before the tonemap, with depth and normals) is ready.");
        else sw::mod::log("shader_config: the hdr stage is NOT available (%s) - effects with #pragma stage hdr do nothing.", st.hdr_error.c_str());
    }
    if (fx_on)
        sw::mod::log("shader_config: post-processing ready - %d effect file(s) in %ls; Ctrl+F7 switches the effects off and on.",
                     sw::postfx::add_folder(fx_folder), fx_folder.c_str());

    unsigned long long ini_print = 0, shader_print = 0;
    bool watch = true, first = true, waiting = false, fx_keys_off = false;
    std::string built_key;
    Config current;
    unsigned long long rebuilds_before = 0;
    int ticks_waiting = 0;
    std::map<std::string, std::string> reported;   // effect -> the error that was logged ("" = it runs)
    while (sw::mod::sleep(250)) {
        const unsigned long long ip = fingerprint(ini, false);
        const unsigned long long sp = watch ? fingerprint(shaders, true) : shader_print;
        const bool by_key = build_on && key_pressed(VK_F8);
        if (fx_on && key_pressed(VK_F7)) {
            fx_keys_off = !fx_keys_off;
            apply_postfx(current, fx_keys_off);
            sw::mod::log("shader_config: Ctrl+F7 - post-processing %s.", fx_keys_off ? "off" : "on");
        }
        if (first || ip != ini_print || sp != shader_print || by_key) {
            const bool file_changed = !first && ip != ini_print;
            const bool shaders_changed = !first && sp != shader_print;
            ini_print = ip;
            shader_print = sp;
            current = apply(ini);
            watch = current.watch;
            for (const auto& p : current.problems) sw::mod::log("shader_config: %ls %s", ini.c_str(), p.c_str());
            if (fx_on) apply_postfx(current, fx_keys_off);
            // rebuild the game's shaders only when what they are built with changed (an edit of [postfx] does not need it);
            // at the start only when there is something to change (the game built its shaders already)
            const bool macros_changed = current.macro_key != built_key;
            if (build_on && (by_key || shaders_changed || (first ? (current.macros > 0 || current.version != 0) : macros_changed))) {
                rebuilds_before = sw::shader_build::status().rebuilds;
                sw::shader_build::rebuild();
                waiting = true;
                ticks_waiting = 0;
                sw::mod::log("shader_config: %s - %d macro(s), GLSL %s, rebuilding the shaders.",
                             by_key ? "Ctrl+F8" : first ? "start" : file_changed ? "the ini changed" : "a shader file changed",
                             current.macros, current.version ? std::to_string(current.version).c_str() : "of the game (330)");
            }
            built_key = current.macro_key;
            first = false;
        }
        if (fx_on) {
            if (sw::postfx::sync() > 0) apply_postfx(current, fx_keys_off);   // new files get the switch and their uniforms
            for (const auto& e : sw::postfx::effects()) {
                if (!e.compiled && e.error.empty()) continue;   // not built yet (or switched off)
                auto it = reported.find(e.name);
                if (it != reported.end() && it->second == e.error) continue;
                reported[e.name] = e.error;
                if (e.error.empty())
                    sw::mod::log("shader_config: effect %s is running (%s).", e.name.c_str(),
                                 e.stage == sw::postfx::Stage::hdr ? "before the tonemap" : "over the final picture");
                else sw::mod::log("shader_config: effect %s has an ERROR (line numbers are those of its file):\n%s", e.name.c_str(), e.error.c_str());
            }
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
    sw::postfx::uninstall();   // the effects stop at the next frame end
}
