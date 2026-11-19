// postfx.cpp - post-processing effects in two places of the frame (sw/postfx.hpp, REPORT 3.225, 3.226).
//
// Two sides: the effect list (any thread, under g_mutex) and the GL side (render thread only). The screen stage runs at the frame
// end: framebuffer 0 is copied into an RGBA8 texture and each effect draws a full-screen triangle back into framebuffer 0. The hdr
// stage runs at the entry of the tonemap pass of the main view: each effect reads the HDR target the tonemap is about to read
// (and the G-buffer) and draws into a temporary RGBA16F texture, which is then blitted back into that target. Every GL state that
// is changed is saved first and put back.

#include "sw/postfx.hpp"

#include "sw/hook.hpp"
#include "sw/render.hpp"
#include "sw/runtime.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>

#pragma comment(lib, "opengl32.lib")

namespace sw::postfx {

namespace {

// ---------------------------------------------------------------------------------------------------------------- the list

struct Uniform {
    std::string name;
    float values[4] = {};
    int count = 0;
    bool is_int = false;
    int int_value = 0;
};

struct Effect {
    std::string name;
    std::string source;
    int order = 0;
    Stage stage = Stage::screen;
    bool enabled = true;
    std::wstring file, folder;
    unsigned long long stamp = 0;     // time and size of the file when it was read
    std::uint64_t version = 0;        // changes with the source
    std::vector<Uniform> uniforms;
    bool compiled = false;
    std::string error;
};

std::mutex g_mutex;
std::vector<Effect> g_effects;
std::vector<std::wstring> g_folders;
std::wstring g_include_folder;        // under g_mutex; empty = the game's rom\graphics\shaders
std::uint64_t g_next_version = 1;
std::atomic<bool> g_on{false};
bool g_hooked = false, g_hdr_hooked = false;
std::string g_hdr_error = "install() was not called";
std::atomic<std::uint64_t> g_frames_drawn{0}, g_hdr_frames{0};
std::atomic<int> g_last_w{0}, g_last_h{0};
std::atomic<unsigned> g_light_seen{0};
std::string g_gl_error;               // under g_mutex

Effect* find(const std::string& name) {
    for (Effect& e : g_effects)
        if (e.name == name) return &e;
    return nullptr;
}

unsigned long long file_stamp(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d)) return 0;
    return ((static_cast<unsigned long long>(d.ftLastWriteTime.dwHighDateTime) << 32) | d.ftLastWriteTime.dwLowDateTime) ^
           (static_cast<unsigned long long>(d.nFileSizeLow) * 0x9E3779B97F4A7C15ull);
}

bool read_file(const std::wstring& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream s;
    s << in.rdbuf();
    out = s.str();
    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF && static_cast<unsigned char>(out[1]) == 0xBB &&
        static_cast<unsigned char>(out[2]) == 0xBF)
        out.erase(0, 3);
    return true;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::wstring parent_of(const std::wstring& path) {
    const std::size_t at = path.find_last_of(L"\\/");
    return at == std::wstring::npos ? std::wstring() : path.substr(0, at);
}

std::wstring include_folder() {   // under g_mutex or with a copy
    if (!g_include_folder.empty()) return g_include_folder;
    wchar_t exe[MAX_PATH * 2]{};
    const DWORD n = GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
    return parent_of(std::wstring(exe, n)) + L"\\rom\\graphics\\shaders";
}

// under g_mutex
void put(const std::string& name, const std::string& source, int order, Stage stage, const std::wstring& file, const std::wstring& folder,
         unsigned long long stamp) {
    Effect* e = find(name);
    if (!e) {
        g_effects.push_back(Effect{});
        e = &g_effects.back();
        e->name = name;
    }
    const Stage st = stage_of(source, stage);
    if (e->source != source || e->version == 0 || e->stage != st) {
        e->source = source;
        e->version = g_next_version++;
        e->compiled = false;
        e->error.clear();
    }
    e->stage = st;
    e->order = order;
    e->file = file;
    e->folder = folder;
    e->stamp = stamp;
}

bool valid_name(const std::string& s) {
    if (s.empty() || s.size() > 128) return false;
    for (char c : s)
        if (static_cast<unsigned char>(c) < 0x20) return false;
    return true;
}

// The number at the start of a file name, 1000 without one.
int order_of(const std::wstring& file_name) {
    int n = 0;
    std::size_t i = 0;
    while (i < file_name.size() && i < 6 && file_name[i] >= L'0' && file_name[i] <= L'9') n = n * 10 + (file_name[i++] - L'0');
    return i == 0 ? 1000 : n;
}

// ---------------------------------------------------------------------------------------------------------------- GLSL

const char kVertex[] = R"(#version 330
out vec2 v_uv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    v_uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char kPrelude[] = R"(
in vec2 v_uv;
out vec4 o_color;
uniform sampler2D u_scene;
uniform vec2 u_resolution;
uniform float u_time;
uniform int u_frame;
vec4 scene(vec2 uv) { return texture(u_scene, uv); }
vec2 texel() { return 1.0 / u_resolution; }
)";

const char kPreludeHdr[] = R"(uniform sampler2D u_depth;
uniform sampler2D u_gdepth;
uniform sampler2D u_normal;
uniform sampler2D u_albedo;
uniform sampler2D u_velocity;
)";

const char kMainScreen[] = R"(
void main() {
    vec4 c = effect(texture(u_scene, v_uv), v_uv);
    o_color = vec4(c.rgb, 1.0);
}
)";

const char kMainHdr[] = R"(
void main() {
    vec4 src = texture(u_scene, v_uv);
    vec4 c = effect(src, v_uv);
    o_color = vec4(c.rgb, src.a);
}
)";

bool has_main(const std::string& s) {
    for (std::size_t at = s.find("main"); at != std::string::npos; at = s.find("main", at + 4)) {
        std::size_t b = at;
        while (b > 0 && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
        if (b < 4 || s.compare(b - 4, 4, "void") != 0) continue;
        std::size_t a = at + 4;
        while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
        if (a < s.size() && s[a] == '(') return true;
    }
    return false;
}

std::string trim_line(std::string line) {
    while (!line.empty() && (line.back() == '\r' || std::isspace(static_cast<unsigned char>(line.back())))) line.pop_back();
    std::size_t i = 0;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    return line.substr(i);
}

// Resolves `#include "file"` (effect folder first, then the include folder); depth-limited. Missing files become an #error.
std::string resolve_includes(const std::string& text, const std::wstring& own, const std::wstring& shared, int depth) {
    std::istringstream in(text);
    std::string line, out;
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        const std::string t = trim_line(line);
        if (t.rfind("#include", 0) == 0) {
            const std::size_t a = t.find('"'), b = t.rfind('"');
            if (a != std::string::npos && b > a) {
                const std::string name = t.substr(a + 1, b - a - 1);
                std::string body;
                bool found = false;
                for (const std::wstring& dir : {own, shared})
                    if (!dir.empty() && read_file(dir + L"\\" + widen(name), body)) {
                        found = true;
                        break;
                    }
                if (!found || depth > 8) {
                    out += "#error \"postfx: cannot #include " + name + "\"\n";
                } else {
                    out += resolve_includes(body, own, shared, depth + 1);
                    out += "\n#line " + std::to_string(number + 1) + "\n";
                }
                continue;
            }
        }
        out += line;
        out += '\n';
    }
    return out;
}

}  // namespace

Stage stage_of(const std::string& src, Stage fallback) {
    std::istringstream in(src);
    std::string line;
    while (std::getline(in, line)) {
        const std::string t = trim_line(line);
        if (t.rfind("#pragma", 0) != 0) continue;
        std::istringstream words(t.substr(7));
        std::string w1, w2;
        words >> w1 >> w2;
        if (w1 != "stage") continue;
        if (w2 == "hdr") return Stage::hdr;
        if (w2 == "screen") return Stage::screen;
    }
    return fallback;
}

std::string fragment_source(const std::string& user, Stage stage, const std::wstring& folder) {
    std::string version = "#version 330";
    std::string body = user;
    // a #version line of the user's replaces 330 (it must be the first line of the shader); the line itself becomes empty
    std::istringstream lines(user);
    std::string line;
    std::size_t pos = 0;
    while (std::getline(lines, line)) {
        const std::string t = trim_line(line);
        if (t.rfind("#version", 0) == 0) {
            version = t;
            body = user.substr(0, pos) + "\n" + user.substr(std::min(user.size(), pos + line.size() + 1));
            break;
        }
        pos += line.size() + 1;
    }
    std::wstring shared;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        shared = include_folder();
    }
    if (body.find("#include") != std::string::npos) body = resolve_includes(body, folder, shared, 0);
    if (has_main(body)) return version + "\n#line 1\n" + body + "\n";
    return version + "\n" + kPrelude + (stage == Stage::hdr ? kPreludeHdr : "") + "#line 1\n" + body + "\n" +
           (stage == Stage::hdr ? kMainHdr : kMainScreen);
}

namespace {

// ---------------------------------------------------------------------------------------------------------------- GL side

using GLchar = char;
constexpr GLenum kFragmentShader = 0x8B30, kVertexShader = 0x8B31, kCompileStatus = 0x8B81, kLinkStatus = 0x8B82;
constexpr GLenum kInfoLogLength = 0x8B84, kTexture0 = 0x84C0, kFramebuffer = 0x8D40, kReadFramebuffer = 0x8CA8,
                 kDrawFramebuffer = 0x8CA9, kColorAttachment0 = 0x8CE0, kFramebufferComplete = 0x8CD5;
constexpr GLenum kCurrentProgram = 0x8B8D, kVertexArrayBinding = 0x85B5, kActiveTexture = 0x84E0;
constexpr GLenum kDrawFramebufferBinding = 0x8CA6, kReadFramebufferBinding = 0x8CAA, kSamplerBinding = 0x8919;
constexpr GLenum kFramebufferSrgb = 0x8DB9, kClampToEdge = 0x812F, kRgba8 = 0x8058, kRgba16f = 0x881A, kHalfFloat = 0x140B;
constexpr GLenum kPixelUnpackBuffer = 0x88EC, kPixelUnpackBufferBinding = 0x88EF;
constexpr GLenum kActiveUniforms = 0x8B86;
constexpr GLenum kFloat = 0x1406, kVec2 = 0x8B50, kVec3 = 0x8B51, kVec4 = 0x8B52, kInt = 0x1404, kBool = 0x8B56, kMat3 = 0x8B5B,
                 kMat4 = 0x8B5C;
constexpr int kUnits = 6;   // 0 scene, 1 depth, 2 gdepth, 3 normal, 4 albedo, 5 velocity

struct Gl {
    GLuint(APIENTRY* CreateShader)(GLenum);
    void(APIENTRY* ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
    void(APIENTRY* CompileShader)(GLuint);
    void(APIENTRY* GetShaderiv)(GLuint, GLenum, GLint*);
    void(APIENTRY* GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    GLuint(APIENTRY* CreateProgram)();
    void(APIENTRY* AttachShader)(GLuint, GLuint);
    void(APIENTRY* LinkProgram)(GLuint);
    void(APIENTRY* GetProgramiv)(GLuint, GLenum, GLint*);
    void(APIENTRY* GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    void(APIENTRY* DeleteShader)(GLuint);
    void(APIENTRY* DeleteProgram)(GLuint);
    GLboolean(APIENTRY* IsProgram)(GLuint);
    void(APIENTRY* UseProgram)(GLuint);
    GLint(APIENTRY* GetUniformLocation)(GLuint, const GLchar*);
    void(APIENTRY* GetActiveUniform)(GLuint, GLuint, GLsizei, GLsizei*, GLint*, GLenum*, GLchar*);
    void(APIENTRY* GetUniformfv)(GLuint, GLint, GLfloat*);
    void(APIENTRY* GetUniformiv)(GLuint, GLint, GLint*);
    void(APIENTRY* Uniform1i)(GLint, GLint);
    void(APIENTRY* Uniform1f)(GLint, GLfloat);
    void(APIENTRY* Uniform2f)(GLint, GLfloat, GLfloat);
    void(APIENTRY* Uniform3f)(GLint, GLfloat, GLfloat, GLfloat);
    void(APIENTRY* Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    void(APIENTRY* Uniform1fv)(GLint, GLsizei, const GLfloat*);
    void(APIENTRY* Uniform2fv)(GLint, GLsizei, const GLfloat*);
    void(APIENTRY* Uniform3fv)(GLint, GLsizei, const GLfloat*);
    void(APIENTRY* Uniform4fv)(GLint, GLsizei, const GLfloat*);
    void(APIENTRY* UniformMatrix3fv)(GLint, GLsizei, GLboolean, const GLfloat*);
    void(APIENTRY* UniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
    void(APIENTRY* GenVertexArrays)(GLsizei, GLuint*);
    void(APIENTRY* BindVertexArray)(GLuint);
    void(APIENTRY* DeleteVertexArrays)(GLsizei, const GLuint*);
    void(APIENTRY* ActiveTexture)(GLenum);
    void(APIENTRY* BindFramebuffer)(GLenum, GLuint);
    void(APIENTRY* GenFramebuffers)(GLsizei, GLuint*);
    void(APIENTRY* DeleteFramebuffers)(GLsizei, const GLuint*);
    void(APIENTRY* FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
    GLenum(APIENTRY* CheckFramebufferStatus)(GLenum);
    void(APIENTRY* BlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
    void(APIENTRY* BindSampler)(GLuint, GLuint);
    void(APIENTRY* BindBuffer)(GLenum, GLuint);
};

using Resolver = void* (*)(const char*);

void* default_resolver(const char* name) {
    void* p = reinterpret_cast<void*>(wglGetProcAddress(name));
    const auto v = reinterpret_cast<std::intptr_t>(p);
    if (v == 0 || v == 1 || v == 2 || v == 3 || v == -1) {
        static HMODULE gl = GetModuleHandleW(L"opengl32.dll");
        p = gl ? reinterpret_cast<void*>(GetProcAddress(gl, name)) : nullptr;
    }
    return p;
}

template <typename T>
bool load(Resolver r, T& fn, const char* name) {
    fn = reinterpret_cast<T>(r(name));
    return fn != nullptr;
}

bool load_gl(Resolver r, Gl& g) {
    return load(r, g.CreateShader, "glCreateShader") && load(r, g.ShaderSource, "glShaderSource") &&
           load(r, g.CompileShader, "glCompileShader") && load(r, g.GetShaderiv, "glGetShaderiv") &&
           load(r, g.GetShaderInfoLog, "glGetShaderInfoLog") && load(r, g.CreateProgram, "glCreateProgram") &&
           load(r, g.AttachShader, "glAttachShader") && load(r, g.LinkProgram, "glLinkProgram") &&
           load(r, g.GetProgramiv, "glGetProgramiv") && load(r, g.GetProgramInfoLog, "glGetProgramInfoLog") &&
           load(r, g.DeleteShader, "glDeleteShader") && load(r, g.DeleteProgram, "glDeleteProgram") &&
           load(r, g.IsProgram, "glIsProgram") && load(r, g.UseProgram, "glUseProgram") &&
           load(r, g.GetUniformLocation, "glGetUniformLocation") && load(r, g.GetActiveUniform, "glGetActiveUniform") &&
           load(r, g.GetUniformfv, "glGetUniformfv") && load(r, g.GetUniformiv, "glGetUniformiv") &&
           load(r, g.Uniform1i, "glUniform1i") && load(r, g.Uniform1f, "glUniform1f") && load(r, g.Uniform2f, "glUniform2f") &&
           load(r, g.Uniform3f, "glUniform3f") && load(r, g.Uniform4f, "glUniform4f") && load(r, g.Uniform1fv, "glUniform1fv") &&
           load(r, g.Uniform2fv, "glUniform2fv") && load(r, g.Uniform3fv, "glUniform3fv") && load(r, g.Uniform4fv, "glUniform4fv") &&
           load(r, g.UniformMatrix3fv, "glUniformMatrix3fv") && load(r, g.UniformMatrix4fv, "glUniformMatrix4fv") &&
           load(r, g.GenVertexArrays, "glGenVertexArrays") && load(r, g.BindVertexArray, "glBindVertexArray") &&
           load(r, g.DeleteVertexArrays, "glDeleteVertexArrays") && load(r, g.ActiveTexture, "glActiveTexture") &&
           load(r, g.BindFramebuffer, "glBindFramebuffer") && load(r, g.GenFramebuffers, "glGenFramebuffers") &&
           load(r, g.DeleteFramebuffers, "glDeleteFramebuffers") && load(r, g.FramebufferTexture2D, "glFramebufferTexture2D") &&
           load(r, g.CheckFramebufferStatus, "glCheckFramebufferStatus") && load(r, g.BlitFramebuffer, "glBlitFramebuffer") &&
           load(r, g.BindSampler, "glBindSampler") && load(r, g.BindBuffer, "glBindBuffer");
}

// A uniform of an effect that is copied from the game's light program every frame.
struct Forward {
    GLint dst = -1, src = -1;
    GLenum type = 0;
    std::string name;
};

struct Program {
    std::string name;
    std::uint64_t version = 0;
    Stage stage = Stage::screen;
    GLuint program = 0;
    GLint u_scene = -1, u_resolution = -1, u_time = -1, u_frame = -1;
    GLint samplers[kUnits][2] = {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}};   // u_* name, game name
    std::vector<std::string> active;           // names of the active non-sampler uniforms (for forwarding)
    std::vector<Forward> forwards;
    GLuint forwards_for = 0;                   // the light program the forwards were built for
    bool ok = false;
};

struct GlSide {
    bool loaded = false, failed = false;
    Gl gl{};
    GLuint vertex = 0, vao = 0;
    GLuint screen_tex = 0;
    int screen_w = 0, screen_h = 0;
    GLuint hdr_tex = 0, hdr_fbo = 0;
    int hdr_w = 0, hdr_h = 0;
    std::vector<Program> programs;
    HGLRC context = nullptr;          // the context the objects belong to
};
GlSide g_side;
LARGE_INTEGER g_start{};
std::uint64_t g_frame_index = 0;
GLuint g_light_program = 0;           // render thread

std::string info_log(Gl& g, GLuint object, bool program) {
    GLint n = 0;
    if (program) g.GetProgramiv(object, kInfoLogLength, &n);
    else g.GetShaderiv(object, kInfoLogLength, &n);
    if (n <= 1) return "(no message)";
    std::string s(static_cast<std::size_t>(n), '\0');
    if (program) g.GetProgramInfoLog(object, n, nullptr, s.data());
    else g.GetShaderInfoLog(object, n, nullptr, s.data());
    while (!s.empty() && (s.back() == '\0' || s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

GLuint compile(Gl& g, GLenum kind, const std::string& source, std::string& error) {
    const GLuint s = g.CreateShader(kind);
    const char* p = source.c_str();
    g.ShaderSource(s, 1, &p, nullptr);
    g.CompileShader(s);
    GLint ok = 0;
    g.GetShaderiv(s, kCompileStatus, &ok);
    if (!ok) {
        error = info_log(g, s, false);
        g.DeleteShader(s);
        return 0;
    }
    return s;
}

void release_side() {
    GlSide& s = g_side;
    if (s.loaded && s.context && wglGetCurrentContext() == s.context) {
        for (Program& p : s.programs)
            if (p.program) s.gl.DeleteProgram(p.program);
        if (s.vertex) s.gl.DeleteShader(s.vertex);
        if (s.vao) s.gl.DeleteVertexArrays(1, &s.vao);
        if (s.screen_tex) glDeleteTextures(1, &s.screen_tex);
        if (s.hdr_tex) glDeleteTextures(1, &s.hdr_tex);
        if (s.hdr_fbo) s.gl.DeleteFramebuffers(1, &s.hdr_fbo);
    }
    s.programs.clear();
    s.vertex = s.vao = s.screen_tex = s.hdr_tex = s.hdr_fbo = 0;
    s.screen_w = s.screen_h = s.hdr_w = s.hdr_h = 0;
    s.context = nullptr;
    g_light_program = 0;
}

struct Saved {
    GLint program, vao, active_texture, texture[kUnits], sampler[kUnits], read_fb, draw_fb, read_buffer, viewport[4], unpack_buffer;
    GLboolean blend, depth, cull, scissor, stencil, srgb, color_mask[4];
};

void save(Gl& g, Saved& s, int units) {
    glGetIntegerv(kCurrentProgram, &s.program);
    glGetIntegerv(kVertexArrayBinding, &s.vao);
    glGetIntegerv(kActiveTexture, &s.active_texture);
    for (int u = 0; u < units; ++u) {
        g.ActiveTexture(kTexture0 + u);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &s.texture[u]);
        glGetIntegerv(kSamplerBinding, &s.sampler[u]);
    }
    glGetIntegerv(kReadFramebufferBinding, &s.read_fb);
    glGetIntegerv(kDrawFramebufferBinding, &s.draw_fb);
    glGetIntegerv(GL_READ_BUFFER, &s.read_buffer);
    glGetIntegerv(GL_VIEWPORT, s.viewport);
    glGetIntegerv(kPixelUnpackBufferBinding, &s.unpack_buffer);
    glGetBooleanv(GL_COLOR_WRITEMASK, s.color_mask);
    s.blend = glIsEnabled(GL_BLEND);
    s.depth = glIsEnabled(GL_DEPTH_TEST);
    s.cull = glIsEnabled(GL_CULL_FACE);
    s.scissor = glIsEnabled(GL_SCISSOR_TEST);
    s.stencil = glIsEnabled(GL_STENCIL_TEST);
    s.srgb = glIsEnabled(kFramebufferSrgb);
}

void set_enabled(GLenum cap, GLboolean on) {
    if (on) glEnable(cap);
    else glDisable(cap);
}

void restore(Gl& g, const Saved& s, int units) {
    g.UseProgram(static_cast<GLuint>(s.program));
    g.BindVertexArray(static_cast<GLuint>(s.vao));
    for (int u = 0; u < units; ++u) {
        g.ActiveTexture(kTexture0 + u);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(s.texture[u]));
        g.BindSampler(static_cast<GLuint>(u), static_cast<GLuint>(s.sampler[u]));
    }
    g.ActiveTexture(static_cast<GLenum>(s.active_texture));
    g.BindFramebuffer(kReadFramebuffer, static_cast<GLuint>(s.read_fb));
    g.BindFramebuffer(kDrawFramebuffer, static_cast<GLuint>(s.draw_fb));
    glReadBuffer(static_cast<GLenum>(s.read_buffer));
    g.BindBuffer(kPixelUnpackBuffer, static_cast<GLuint>(s.unpack_buffer));
    glViewport(s.viewport[0], s.viewport[1], s.viewport[2], s.viewport[3]);
    glColorMask(s.color_mask[0], s.color_mask[1], s.color_mask[2], s.color_mask[3]);
    set_enabled(GL_BLEND, s.blend);
    set_enabled(GL_DEPTH_TEST, s.depth);
    set_enabled(GL_CULL_FACE, s.cull);
    set_enabled(GL_SCISSOR_TEST, s.scissor);
    set_enabled(GL_STENCIL_TEST, s.stencil);
    set_enabled(kFramebufferSrgb, s.srgb);
}

void plain_state() {
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(kFramebufferSrgb);   // copy and draw without colour conversion
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

struct Snapshot {
    std::string name, source;
    std::uint64_t version;
    Stage stage;
    std::wstring folder;              // for #include
    std::vector<Uniform> uniforms;
};

std::vector<Snapshot> snapshot(Stage stage) {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<const Effect*> on;
    for (const Effect& e : g_effects)
        if (e.enabled && !e.source.empty() && e.stage == stage) on.push_back(&e);
    std::stable_sort(on.begin(), on.end(), [](const Effect* a, const Effect* b) {
        return a->order != b->order ? a->order < b->order : a->name < b->name;
    });
    std::vector<Snapshot> list;
    for (const Effect* e : on) list.push_back({e->name, e->source, e->version, e->stage, e->file.empty() ? std::wstring() : parent_of(e->file), e->uniforms});
    return list;
}

// Loads the GL functions and makes the shared objects for the context of this thread. False when nothing can be drawn.
bool ready_side(Resolver resolver) {
    HGLRC context = wglGetCurrentContext();
    if (!context) return false;
    GlSide& side = g_side;
    if (side.context && side.context != context) release_side();   // a new context (window made again): start over
    if (!side.loaded) {
        if (side.failed) return false;
        if (!load_gl(resolver, side.gl)) {
            side.failed = true;
            std::lock_guard<std::mutex> lock(g_mutex);
            g_gl_error = "the OpenGL 3 functions could not be loaded";
            return false;
        }
        side.loaded = true;
    }
    if (!side.context) {
        side.context = context;
        std::string error;
        side.vertex = compile(side.gl, kVertexShader, kVertex, error);
        side.gl.GenVertexArrays(1, &side.vao);
        if (!side.vertex) {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_gl_error = "the vertex shader did not compile: " + error;
        }
    }
    return side.vertex != 0;
}

// (Re)builds the programs of the list whose source changed.
void build_programs(const std::vector<Snapshot>& list) {
    GlSide& side = g_side;
    Gl& g = side.gl;
    for (const Snapshot& s : list) {
        Program* p = nullptr;
        for (Program& x : side.programs)
            if (x.name == s.name) p = &x;
        if (!p) {
            side.programs.push_back(Program{});
            p = &side.programs.back();
            p->name = s.name;
        }
        if (p->version == s.version) continue;
        if (p->program) g.DeleteProgram(p->program);
        *p = Program{};
        p->name = s.name;
        p->version = s.version;
        p->stage = s.stage;
        std::string error;
        const std::string fs_text = fragment_source(s.source, s.stage, s.folder);
        const GLuint fs = compile(g, kFragmentShader, fs_text, error);
        if (fs) {
            const GLuint program = g.CreateProgram();
            g.AttachShader(program, side.vertex);
            g.AttachShader(program, fs);
            g.LinkProgram(program);
            g.DeleteShader(fs);
            GLint ok = 0;
            g.GetProgramiv(program, kLinkStatus, &ok);
            if (ok) {
                p->program = program;
                p->ok = true;
                p->u_scene = g.GetUniformLocation(program, "u_scene");
                p->u_resolution = g.GetUniformLocation(program, "u_resolution");
                p->u_time = g.GetUniformLocation(program, "u_time");
                p->u_frame = g.GetUniformLocation(program, "u_frame");
                static const char* const kNames[kUnits][2] = {{"u_scene", "u_scene"}, {"u_depth", "u_depth"}, {"u_gdepth", "gdepth"},
                                                              {"u_normal", "gnormal"}, {"u_albedo", "gcolor"}, {"u_velocity", "gvelocity"}};
                for (int u = 0; u < kUnits; ++u)
                    for (int k = 0; k < 2; ++k) p->samplers[u][k] = g.GetUniformLocation(program, kNames[u][k]);
                GLint count = 0;
                g.GetProgramiv(program, kActiveUniforms, &count);
                for (GLint i = 0; i < count; ++i) {
                    char name[256] = {};
                    GLint size = 0;
                    GLenum type = 0;
                    g.GetActiveUniform(program, static_cast<GLuint>(i), sizeof(name) - 1, nullptr, &size, &type, name);
                    std::string n = name;
                    if (n.rfind("u_", 0) == 0 || n.rfind("gl_", 0) == 0) continue;
                    if (!(type == kFloat || type == kVec2 || type == kVec3 || type == kVec4 || type == kInt || type == kBool || type == kMat3 ||
                          type == kMat4))
                        continue;   // samplers and the rest
                    const std::size_t br = n.find('[');
                    if (br != std::string::npos) {
                        const std::string base = n.substr(0, br);
                        for (GLint k = 0; k < size; ++k) p->active.push_back(base + "[" + std::to_string(k) + "]");
                    } else {
                        p->active.push_back(n);
                    }
                }
            } else {
                error = info_log(g, program, true);
                g.DeleteProgram(program);
            }
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        if (Effect* e = find(s.name); e && e->version == s.version) {
            e->compiled = p->ok;
            e->error = p->ok ? std::string() : error;
        }
    }
}

const Program* program_of(const std::string& name) {
    for (const Program& x : g_side.programs)
        if (x.name == name) return &x;
    return nullptr;
}

// The game's light program: the one that declares gnormal and sun_light_direction (the deferred composition, REPORT 3.113).
GLuint find_light_program(Gl& g) {
    if (g_light_program && g.IsProgram(g_light_program) && g.GetUniformLocation(g_light_program, "gnormal") >= 0) return g_light_program;
    g_light_program = 0;
    static unsigned calls = 0;
    if (calls++ % 120 != 0) return 0;   // looked recently: not every frame while there is none (the menu)
    for (GLuint id = 1; id <= 4096; ++id) {
        if (!g.IsProgram(id)) continue;
        if (g.GetUniformLocation(id, "gnormal") >= 0 && g.GetUniformLocation(id, "sun_light_direction") >= 0) {
            g_light_program = id;
            break;
        }
    }
    g_light_seen.store(g_light_program);
    return g_light_program;
}

// Copies the uniforms of the light program that the effect declares (and that the mod did not set).
void forward_uniforms(Gl& g, Program& p, GLuint light, const std::vector<Uniform>& set_by_mod) {
    if (!light || p.active.empty()) return;
    if (p.forwards_for != light) {
        p.forwards.clear();
        p.forwards_for = light;
        for (const std::string& n : p.active) {
            bool by_mod = false;
            for (const Uniform& u : set_by_mod) by_mod |= u.name == n;
            if (by_mod) continue;
            const GLint src = g.GetUniformLocation(light, n.c_str());
            const GLint dst = g.GetUniformLocation(p.program, n.c_str());
            if (src < 0 || dst < 0) continue;
            // the type of the destination decides how it is written
            GLenum type = 0;
            GLint count = 0;
            g.GetProgramiv(p.program, kActiveUniforms, &count);
            const std::string base = n.substr(0, n.find('['));
            for (GLint i = 0; i < count && !type; ++i) {
                char name[256] = {};
                GLint size = 0;
                GLenum t = 0;
                g.GetActiveUniform(p.program, static_cast<GLuint>(i), sizeof(name) - 1, nullptr, &size, &t, name);
                if (std::string(name).substr(0, std::string(name).find('[')) == base) type = t;
            }
            if (type) p.forwards.push_back({dst, src, type, n});
        }
    }
    for (const Forward& f : p.forwards) {
        bool by_mod = false;
        for (const Uniform& u : set_by_mod) by_mod |= u.name == f.name;
        if (by_mod) continue;
        float v[16] = {};
        if (f.type == kInt || f.type == kBool) {
            GLint i = 0;
            g.GetUniformiv(light, f.src, &i);
            g.Uniform1i(f.dst, i);
            continue;
        }
        g.GetUniformfv(light, f.src, v);
        switch (f.type) {
            case kFloat: g.Uniform1fv(f.dst, 1, v); break;
            case kVec2: g.Uniform2fv(f.dst, 1, v); break;
            case kVec3: g.Uniform3fv(f.dst, 1, v); break;
            case kVec4: g.Uniform4fv(f.dst, 1, v); break;
            case kMat3: g.UniformMatrix3fv(f.dst, 1, GL_FALSE, v); break;
            case kMat4: g.UniformMatrix4fv(f.dst, 1, GL_FALSE, v); break;
            default: break;
        }
    }
}

float seconds_now() {
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    return static_cast<float>(static_cast<double>(now.QuadPart - g_start.QuadPart) / static_cast<double>(freq.QuadPart));
}

void common_uniforms(Gl& g, const Program& p, int width, int height, float seconds, const std::vector<Uniform>& uniforms) {
    if (p.u_resolution >= 0) g.Uniform2f(p.u_resolution, static_cast<float>(width), static_cast<float>(height));
    if (p.u_time >= 0) g.Uniform1f(p.u_time, seconds);
    if (p.u_frame >= 0) g.Uniform1i(p.u_frame, static_cast<GLint>(g_frame_index & 0x7fffffff));
    for (const Uniform& u : uniforms) {
        const GLint at = g.GetUniformLocation(p.program, u.name.c_str());
        if (at < 0) continue;
        if (u.is_int) g.Uniform1i(at, u.int_value);
        else if (u.count == 1) g.Uniform1f(at, u.values[0]);
        else if (u.count == 2) g.Uniform2f(at, u.values[0], u.values[1]);
        else if (u.count == 3) g.Uniform3f(at, u.values[0], u.values[1], u.values[2]);
        else if (u.count == 4) g.Uniform4f(at, u.values[0], u.values[1], u.values[2], u.values[3]);
    }
}

// Programs of effects that are gone (switched-off ones are kept).
void drop_removed() {
    Gl& g = g_side.gl;
    g_side.programs.erase(std::remove_if(g_side.programs.begin(), g_side.programs.end(),
                                         [&](Program& p) {
                                             std::lock_guard<std::mutex> lock(g_mutex);
                                             if (find(p.name)) return false;
                                             if (p.program) g.DeleteProgram(p.program);
                                             return true;
                                         }),
                          g_side.programs.end());
}

// ---- the screen stage: the work of one frame end. False when nothing was drawn.
bool draw_screen(Resolver resolver, GLuint framebuffer, int width, int height) {
    if (!g_on.load()) {
        if (g_side.context) release_side();
        return false;
    }
    if (width <= 0 || height <= 0) return false;
    const std::vector<Snapshot> list = snapshot(Stage::screen);
    if (list.empty()) {
        drop_removed();
        ++g_frame_index;
        return false;
    }
    if (!ready_side(resolver)) return false;
    GlSide& side = g_side;
    Gl& g = side.gl;
    build_programs(list);
    const GLuint light = find_light_program(g);

    Saved saved{};
    save(g, saved, 1);
    g.BindBuffer(kPixelUnpackBuffer, 0);
    g.ActiveTexture(kTexture0);
    g.BindSampler(0, 0);
    if (!side.screen_tex) glGenTextures(1, &side.screen_tex);
    glBindTexture(GL_TEXTURE_2D, side.screen_tex);
    if (side.screen_w != width || side.screen_h != height) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, kClampToEdge);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, kClampToEdge);
        glTexImage2D(GL_TEXTURE_2D, 0, kRgba8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        side.screen_w = width;
        side.screen_h = height;
    }
    g.BindFramebuffer(kReadFramebuffer, framebuffer);
    g.BindFramebuffer(kDrawFramebuffer, framebuffer);
    if (framebuffer == 0) glReadBuffer(GL_BACK);
    glViewport(0, 0, width, height);
    plain_state();
    g.BindVertexArray(side.vao);

    const float seconds = seconds_now();
    int drawn = 0;
    for (const Snapshot& s : list) {
        Program* p = const_cast<Program*>(program_of(s.name));
        if (!p || !p->ok) continue;
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);   // the picture so far
        g.UseProgram(p->program);
        if (p->u_scene >= 0) g.Uniform1i(p->u_scene, 0);
        common_uniforms(g, *p, width, height, seconds, s.uniforms);
        forward_uniforms(g, *p, light, s.uniforms);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        ++drawn;
    }
    restore(g, saved, 1);
    drop_removed();
    ++g_frame_index;
    g_last_w.store(width);
    g_last_h.store(height);
    if (drawn > 0) g_frames_drawn.fetch_add(1);
    return drawn > 0;
}

// ---- the hdr stage: before the tonemap. False when nothing was drawn.
struct HdrTarget {
    GLuint scene = 0, scene_fbo = 0;
    int width = 0, height = 0;
    GLuint units[kUnits] = {};        // the textures: scene, depth, gdepth, normal, albedo, velocity
};

bool draw_hdr(Resolver resolver, const HdrTarget& t, GLuint light_override) {
    if (!g_on.load() || !t.scene || !t.scene_fbo || t.width <= 0 || t.height <= 0) return false;
    const std::vector<Snapshot> list = snapshot(Stage::hdr);
    if (list.empty()) return false;
    if (!ready_side(resolver)) return false;
    GlSide& side = g_side;
    Gl& g = side.gl;
    build_programs(list);
    const GLuint light = light_override ? light_override : find_light_program(g);

    Saved saved{};
    save(g, saved, kUnits);
    g.BindBuffer(kPixelUnpackBuffer, 0);
    // the temporary target: RGBA16F at the size of the scene
    if (!side.hdr_tex) glGenTextures(1, &side.hdr_tex);
    if (!side.hdr_fbo) g.GenFramebuffers(1, &side.hdr_fbo);
    g.ActiveTexture(kTexture0);
    g.BindSampler(0, 0);
    if (side.hdr_w != t.width || side.hdr_h != t.height) {
        glBindTexture(GL_TEXTURE_2D, side.hdr_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, kClampToEdge);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, kClampToEdge);
        glTexImage2D(GL_TEXTURE_2D, 0, kRgba16f, t.width, t.height, 0, GL_RGBA, kHalfFloat, nullptr);
        g.BindFramebuffer(kFramebuffer, side.hdr_fbo);
        g.FramebufferTexture2D(kFramebuffer, kColorAttachment0, GL_TEXTURE_2D, side.hdr_tex, 0);
        side.hdr_w = t.width;
        side.hdr_h = t.height;
    }
    g.BindFramebuffer(kFramebuffer, side.hdr_fbo);
    if (g.CheckFramebufferStatus(kFramebuffer) != kFramebufferComplete) {
        restore(g, saved, kUnits);
        return false;
    }
    glViewport(0, 0, t.width, t.height);
    plain_state();
    g.BindVertexArray(side.vao);
    for (int u = 0; u < kUnits; ++u) {
        g.ActiveTexture(kTexture0 + u);
        g.BindSampler(static_cast<GLuint>(u), 0);
        glBindTexture(GL_TEXTURE_2D, t.units[u]);
    }

    const float seconds = seconds_now();
    int drawn = 0;
    for (const Snapshot& s : list) {
        Program* p = const_cast<Program*>(program_of(s.name));
        if (!p || !p->ok) continue;
        // effect: scene -> temporary
        g.BindFramebuffer(kFramebuffer, side.hdr_fbo);
        g.UseProgram(p->program);
        for (int u = 0; u < kUnits; ++u)
            for (int k = 0; k < 2; ++k)
                if (p->samplers[u][k] >= 0) g.Uniform1i(p->samplers[u][k], u);
        common_uniforms(g, *p, t.width, t.height, seconds, s.uniforms);
        forward_uniforms(g, *p, light, s.uniforms);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        // temporary -> scene (the next effect and the tonemap read it there)
        g.BindFramebuffer(kReadFramebuffer, side.hdr_fbo);
        glReadBuffer(kColorAttachment0);
        g.BindFramebuffer(kDrawFramebuffer, t.scene_fbo);
        g.BlitFramebuffer(0, 0, t.width, t.height, 0, 0, t.width, t.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        ++drawn;
    }
    restore(g, saved, kUnits);
    if (drawn > 0) g_hdr_frames.fetch_add(1);
    return drawn > 0;
}

// ---- the hook on the tonemap pass of the main view

template <typename T>
bool read_at(std::uintptr_t address, T& value) {
    if (!address || !sw::Runtime::get().readable(reinterpret_cast<const void*>(address), sizeof(T))) return false;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return true;
}

void on_tonemap(void*, void*) {
    if (!g_on.load(std::memory_order_relaxed)) return;
    // Only the call of the main view's frame (0x6dd94f): there r9 = pipeline + 0x48 and r8 is the pipeline's [1] (scene colour) or
    // [3] (AA output). The other caller 0x6e3cb6 is a different renderer with r9 on its stack - its "pipeline" fails this test.
    const std::uintptr_t input = static_cast<std::uintptr_t>(sw::Hook::arg(2));
    const std::uintptr_t pipeline = static_cast<std::uintptr_t>(sw::Hook::arg(3)) - 0x48;
    std::uintptr_t gbuf_ptr = 0, scene_ptr = 0, aa_ptr = 0;
    if (!read_at(pipeline + 0x0, gbuf_ptr) || !read_at(pipeline + 0x8, scene_ptr) || !read_at(pipeline + 0x18, aa_ptr)) return;
    if (!input || (input != scene_ptr && input != aa_ptr)) return;
    sw::render::TargetInfo in, gbuf, scene;
    if (!sw::render::read_target(input, in) || !in.fbo || !in.color_texture || in.width <= 0 || in.height <= 0) return;
    if (!sw::render::read_target(gbuf_ptr, gbuf) || gbuf.kind != sw::render::TargetKind::multiple) return;
    HdrTarget t;
    t.scene = in.color_texture;
    t.scene_fbo = in.fbo;
    t.width = in.width;
    t.height = in.height;
    t.units[0] = in.color_texture;
    t.units[2] = gbuf.depth_texture;
    if (gbuf.colors.size() > 0) t.units[3] = gbuf.colors[0].texture;
    if (gbuf.colors.size() > 1) t.units[4] = gbuf.colors[1].texture;
    if (gbuf.colors.size() > 2) t.units[5] = gbuf.colors[2].texture;
    t.units[1] = sw::render::read_target(scene_ptr, scene) && scene.depth_texture ? scene.depth_texture : t.units[2];
    draw_hdr(&default_resolver, t, 0);
}

// The bytes the hdr stage relies on (1.15.27): the call of the tonemap pass in the frame, with r9 = pipeline + 0x48 before it.
bool check_tonemap_site(std::string& why) {
    auto& rt = sw::Runtime::get();
    const auto* lea = static_cast<const std::uint8_t*>(rt.at_rva(0x6dd94a));
    static const std::uint8_t kLea[5] = {0x4d, 0x8d, 0x4c, 0x24, 0x48};   // lea r9,[r12+48h]
    if (!lea || !rt.readable(lea, 10) || std::memcmp(lea, kLea, 5) != 0 || lea[5] != 0xe8) {
        why = "the call of the tonemap pass at 0x6dd94f is not the expected code";
        return false;
    }
    std::int32_t rel = 0;
    std::memcpy(&rel, lea + 6, 4);
    const std::uintptr_t target = reinterpret_cast<std::uintptr_t>(lea) + 10 + static_cast<std::intptr_t>(rel);
    if (target != reinterpret_cast<std::uintptr_t>(rt.at_rva(sw::render::kRvaTonemapDraw))) {
        why = "0x6dd94f does not call the tonemap pass 0x68e8b0";
        return false;
    }
    return true;
}

// The size of framebuffer 0: the client area of the window the game draws into.
bool window_client(int& w, int& h) {
    if (HDC dc = reinterpret_cast<HDC>(sw::render::window_dc())) {
        if (HWND wnd = WindowFromDC(dc)) {
            RECT r{};
            if (GetClientRect(wnd, &r) && r.right > 0 && r.bottom > 0) {
                w = r.right;
                h = r.bottom;
                return true;
            }
        }
    }
    return sw::render::window_size(w, h);
}

void on_frame(void*, void*) {
    int w = 0, h = 0;
    if (!g_on.load()) {
        if (g_side.context) release_side();
        return;
    }
    if (!window_client(w, h)) return;
    draw_screen(&default_resolver, 0, w, h);
}

// ---------------------------------------------------------------------------------------------------------------- presets

const std::vector<Preset> kPresets = {
    {"vignette", "screen: darker corners - u_amount 0.35, u_softness 0.45",
     R"(uniform float u_amount = 0.35;
uniform float u_softness = 0.45;
vec4 effect(vec4 color, vec2 uv) {
    vec2 d = uv - 0.5;
    d.x *= u_resolution.x / u_resolution.y;
    float v = smoothstep(0.75, 0.75 - u_softness, length(d));
    return vec4(color.rgb * mix(1.0 - u_amount, 1.0, v), 1.0);
}
)"},
    {"sharpen", "screen: sharper edges - u_amount 0.4 (0 = off, 1 = strong)",
     R"(uniform float u_amount = 0.4;
vec4 effect(vec4 color, vec2 uv) {
    vec2 t = texel();
    vec3 blur = (scene(uv + vec2(t.x, 0.0)).rgb + scene(uv - vec2(t.x, 0.0)).rgb +
                 scene(uv + vec2(0.0, t.y)).rgb + scene(uv - vec2(0.0, t.y)).rgb) * 0.25;
    return vec4(clamp(color.rgb + (color.rgb - blur) * u_amount * 2.0, 0.0, 1.0), 1.0);
}
)"},
    {"cas", "screen: contrast adaptive sharpening (sharpens soft areas more than hard edges) - u_amount 0.5",
     R"(uniform float u_amount = 0.5;
vec4 effect(vec4 color, vec2 uv) {
    vec2 t = texel();
    vec3 a = scene(uv + vec2(0.0, t.y)).rgb, b = scene(uv - vec2(t.x, 0.0)).rgb;
    vec3 d = scene(uv + vec2(t.x, 0.0)).rgb, e = scene(uv - vec2(0.0, t.y)).rgb;
    vec3 mn = min(min(min(a, b), min(d, e)), color.rgb);
    vec3 mx = max(max(max(a, b), max(d, e)), color.rgb);
    vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, 1e-4), 0.0, 1.0));
    vec3 w = -amp / mix(8.0, 5.0, clamp(u_amount, 0.0, 1.0));
    vec3 c = ((a + b + d + e) * w + color.rgb) / (1.0 + 4.0 * w);
    return vec4(clamp(c, 0.0, 1.0), 1.0);
}
)"},
    {"fxaa", "screen: fast anti-aliasing of edges (FXAA-style, on the final picture) - u_amount 0.75",
     R"(uniform float u_amount = 0.75;
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
vec4 effect(vec4 color, vec2 uv) {
    vec2 t = texel();
    float nw = luma(scene(uv + vec2(-t.x, t.y)).rgb), ne = luma(scene(uv + vec2(t.x, t.y)).rgb);
    float sw = luma(scene(uv + vec2(-t.x, -t.y)).rgb), se = luma(scene(uv + vec2(t.x, -t.y)).rgb);
    float m = luma(color.rgb);
    float lo = min(m, min(min(nw, ne), min(sw, se))), hi = max(m, max(max(nw, ne), max(sw, se)));
    if (hi - lo < max(0.0312, hi * 0.125)) return color;
    vec2 dir = vec2(-((nw + ne) - (sw + se)), (nw + sw) - (ne + se));
    float reduce = max((nw + ne + sw + se) * 0.03125, 1.0 / 128.0);
    dir = clamp(dir / (min(abs(dir.x), abs(dir.y)) + reduce), -8.0, 8.0) * t;
    vec3 a = 0.5 * (scene(uv - dir / 6.0).rgb + scene(uv + dir / 6.0).rgb);
    vec3 b = a * 0.5 + 0.25 * (scene(uv - dir * 0.5).rgb + scene(uv + dir * 0.5).rgb);
    float lb = luma(b);
    vec3 r = (lb < lo || lb > hi) ? a : b;
    return vec4(mix(color.rgb, r, u_amount), 1.0);
}
)"},
    {"saturation", "screen: colour strength - u_amount 1.2 (0 = grey, 1 = unchanged)",
     R"(uniform float u_amount = 1.2;
vec4 effect(vec4 color, vec2 uv) {
    float grey = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
    return vec4(clamp(mix(vec3(grey), color.rgb, u_amount), 0.0, 1.0), 1.0);
}
)"},
    {"contrast", "screen: contrast and brightness - u_contrast 1.1, u_brightness 0.0",
     R"(uniform float u_contrast = 1.1;
uniform float u_brightness = 0.0;
vec4 effect(vec4 color, vec2 uv) {
    return vec4(clamp((color.rgb - 0.5) * u_contrast + 0.5 + u_brightness, 0.0, 1.0), 1.0);
}
)"},
    {"color_grade", "screen: lift / gamma / gain per colour and white balance - u_lift 0 0 0, u_gamma 1 1 1, u_gain 1 1 1, u_temperature 0 (-1 cool .. 1 warm)",
     R"(uniform vec3 u_lift = vec3(0.0);
uniform vec3 u_gamma = vec3(1.0);
uniform vec3 u_gain = vec3(1.0);
uniform float u_temperature = 0.0;
vec4 effect(vec4 color, vec2 uv) {
    vec3 c = color.rgb * u_gain + u_lift * (1.0 - color.rgb);
    c = pow(max(c, 0.0), 1.0 / max(u_gamma, vec3(0.01)));
    c *= vec3(1.0 + 0.1 * u_temperature, 1.0, 1.0 - 0.1 * u_temperature);
    return vec4(clamp(c, 0.0, 1.0), 1.0);
}
)"},
    {"tint", "screen: colour tint - u_color 1.0 0.97 0.92 multiplies the picture, u_amount 1.0",
     R"(uniform vec3 u_color = vec3(1.0, 0.97, 0.92);
uniform float u_amount = 1.0;
vec4 effect(vec4 color, vec2 uv) {
    return vec4(mix(color.rgb, color.rgb * u_color, u_amount), 1.0);
}
)"},
    {"grain", "screen: film grain - u_amount 0.04",
     R"(uniform float u_amount = 0.04;
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
vec4 effect(vec4 color, vec2 uv) {
    float n = hash(uv * u_resolution + vec2(float(u_frame % 997), u_time)) - 0.5;
    return vec4(clamp(color.rgb + n * u_amount, 0.0, 1.0), 1.0);
}
)"},
    {"chromatic_aberration", "screen: colour fringes towards the edges - u_amount 1.5 (pixels at the corner)",
     R"(uniform float u_amount = 1.5;
vec4 effect(vec4 color, vec2 uv) {
    vec2 d = (uv - 0.5) * 2.0 * texel() * u_amount;
    return vec4(scene(uv + d).r, color.g, scene(uv - d).b, 1.0);
}
)"},
    {"exposure", "hdr: brightness of the scene before the tonemap, HUD untouched - u_exposure 1.0 (2.0 = one stop brighter)",
     R"(#pragma stage hdr
uniform float u_exposure = 1.0;
vec4 effect(vec4 color, vec2 uv) {
    return vec4(color.rgb * u_exposure, color.a);
}
)"},
    {"bloom", "hdr: glow around bright parts of the scene (sun, lights), before the tonemap - u_threshold 1.0, u_amount 0.15, u_radius 6 (pixels)",
     R"(#pragma stage hdr
uniform float u_threshold = 1.0;
uniform float u_amount = 0.15;
uniform float u_radius = 6.0;
vec3 bright(vec2 uv) { vec3 c = scene(uv).rgb; return max(c - vec3(u_threshold), 0.0); }
vec4 effect(vec4 color, vec2 uv) {
    vec2 t = texel() * u_radius;
    vec3 sum = vec3(0.0);
    float w = 0.0;
    for (int i = -3; i <= 3; ++i)
        for (int j = -3; j <= 3; ++j) {
            float k = exp(-float(i * i + j * j) / 8.0);
            sum += bright(uv + vec2(float(i), float(j)) * t) * k;
            w += k;
        }
    return vec4(color.rgb + sum / w * u_amount * 8.0, color.a);
}
)"},
    {"sun_rays", "hdr: light shafts from the sun over everything (ocean and glass included) - u_amount 0.35, u_decay 0.96, u_samples 48; uses the game's sun_light_direction and matrices",
     R"(#pragma stage hdr
#include "depth_utils.glslh"
uniform float u_amount = 0.35;
uniform float u_decay = 0.96;
uniform int u_samples = 48;
uniform vec3 sun_light_direction;       // copied from the game's light shader; points away from the sun
uniform vec3 sun_light_color;
uniform mat4 mat_view_proj_inverse;
float sky(vec2 uv) { return decode_depth_linear(texture(u_depth, uv).r) > 20000.0 ? 1.0 : 0.0; }
vec4 effect(vec4 color, vec2 uv) {
    mat4 vp = inverse(mat_view_proj_inverse);
    vec4 cam = mat_view_proj_inverse * vec4(0.0, 0.0, -1.0, 1.0);
    vec4 sp = vp * vec4(cam.xyz / cam.w + normalize(-sun_light_direction) * 10000.0, 1.0);
    if (sp.w <= 0.0) return color;              // the sun is behind the camera
    vec2 sun = sp.xy / sp.w * 0.5 + 0.5;
    vec2 dstep = (sun - uv) / float(u_samples);
    vec2 p = uv;
    float w = 1.0, sum = 0.0;
    for (int i = 0; i < 128; ++i) {
        if (i >= u_samples) break;
        p += dstep;
        sum += sky(clamp(p, vec2(0.0), vec2(1.0))) * w;
        w *= u_decay;
    }
    float fade = clamp(1.0 - length((sun - 0.5) * vec2(u_resolution.x / u_resolution.y, 1.0)), 0.0, 1.0);
    return vec4(color.rgb + sun_light_color * sum / float(u_samples) * u_amount * fade, color.a);
}
)"},
};

}  // namespace

// ------------------------------------------------------------------------------------------------------------- public

bool install(std::string* error) {
    if (!g_hooked) {
        if (!sw::render::watch()) {
            if (error) *error = "the frame end of this game build is not known (sw::render::watch failed)";
            return false;
        }
        if (!sw::render::on_frame_end(&on_frame, nullptr)) {
            if (error) *error = "the frame end callback could not be added";
            return false;
        }
        g_hooked = true;
    }
    if (!g_hdr_hooked) {
        std::string why;
        if (!check_tonemap_site(why)) {
            g_hdr_error = why;
        } else {
            std::string herr;
            const auto st = sw::Runtime::get().hook_rva(sw::render::kRvaTonemapDraw, &on_tonemap, nullptr, herr);
            if (st == sw::HookStatus::ok) {
                g_hdr_hooked = true;
                g_hdr_error.clear();
            } else {
                g_hdr_error = std::string("the tonemap pass 0x68e8b0 could not be hooked: ") + sw::to_string(st) + " " + herr;
            }
        }
    }
    QueryPerformanceCounter(&g_start);
    g_on.store(true);
    return true;
}

void uninstall() { g_on.store(false); }

bool add(const std::string& name, const std::string& glsl, int order, Stage stage) {
    if (!valid_name(name) || glsl.empty()) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    put(name, glsl, order, stage, L"", L"", 0);
    return true;
}

bool add_file(const std::string& name, const std::wstring& path, int order, std::string* error) {
    if (!valid_name(name)) {
        if (error) *error = "not a valid effect name";
        return false;
    }
    std::string text;
    if (!read_file(path, text) || text.empty()) {
        if (error) *error = "cannot read " + narrow(path);
        return false;
    }
    const unsigned long long stamp = file_stamp(path);
    std::lock_guard<std::mutex> lock(g_mutex);
    put(name, text, order, Stage::screen, path, L"", stamp);
    return true;
}

namespace {
// under g_mutex: the *.glsl files of a folder; returns the number of effects that changed
int scan_folder(const std::wstring& folder) {
    int changed = 0;
    std::vector<std::wstring> seen;
    WIN32_FIND_DATAW d{};
    HANDLE h = FindFirstFileW((folder + L"\\*.glsl").c_str(), &d);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const std::wstring file_name = d.cFileName;
            const std::wstring path = folder + L"\\" + file_name;
            const std::string name = narrow(file_name.substr(0, file_name.size() - 5));
            seen.push_back(path);
            const unsigned long long stamp = file_stamp(path);
            Effect* e = find(name);
            if (e && e->file == path && e->stamp == stamp) continue;
            std::string text;
            if (!read_file(path, text) || text.empty()) continue;   // half written: next time
            put(name, text, order_of(file_name), Stage::screen, path, folder, stamp);
            ++changed;
        } while (FindNextFileW(h, &d));
        FindClose(h);
    }
    const auto before = g_effects.size();
    g_effects.erase(std::remove_if(g_effects.begin(), g_effects.end(),
                                   [&](const Effect& e) {
                                       return e.folder == folder && std::find(seen.begin(), seen.end(), e.file) == seen.end();
                                   }),
                    g_effects.end());
    return changed + static_cast<int>(before - g_effects.size());
}
}  // namespace

int add_folder(const std::wstring& folder) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (std::find(g_folders.begin(), g_folders.end(), folder) == g_folders.end()) g_folders.push_back(folder);
    scan_folder(folder);
    int n = 0;
    for (const Effect& e : g_effects) n += e.folder == folder ? 1 : 0;
    return n;
}

int sync() {
    std::lock_guard<std::mutex> lock(g_mutex);
    int changed = 0;
    for (const std::wstring& f : g_folders) changed += scan_folder(f);
    for (Effect& e : g_effects) {
        if (e.file.empty() || !e.folder.empty()) continue;
        const unsigned long long stamp = file_stamp(e.file);
        if (stamp == 0 || stamp == e.stamp) continue;
        std::string text;
        if (!read_file(e.file, text) || text.empty()) continue;
        e.stamp = stamp;
        if (text != e.source) {
            e.source = text;
            e.stage = stage_of(text, e.stage);
            e.version = g_next_version++;
            e.compiled = false;
            e.error.clear();
            ++changed;
        }
    }
    return changed;
}

bool remove(const std::string& name) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto before = g_effects.size();
    g_effects.erase(std::remove_if(g_effects.begin(), g_effects.end(), [&](const Effect& e) { return e.name == name; }), g_effects.end());
    return g_effects.size() != before;
}

void clear() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_effects.clear();
    g_folders.clear();
}

bool enable(const std::string& name, bool on) {
    std::lock_guard<std::mutex> lock(g_mutex);
    Effect* e = find(name);
    if (!e) return false;
    e->enabled = on;
    return true;
}

bool set_order(const std::string& name, int order) {
    std::lock_guard<std::mutex> lock(g_mutex);
    Effect* e = find(name);
    if (!e) return false;
    e->order = order;
    return true;
}

void set_include_folder(const std::wstring& folder) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_include_folder = folder;
    for (Effect& e : g_effects)
        if (e.source.find("#include") != std::string::npos) {
            e.version = g_next_version++;   // build again with the new folder
            e.compiled = false;
        }
}

namespace {
bool set_uniform(const std::string& effect, const std::string& uniform, const float* values, int count, bool is_int, int iv) {
    if (uniform.empty()) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    Effect* e = find(effect);
    if (!e) return false;
    Uniform* u = nullptr;
    for (Uniform& x : e->uniforms)
        if (x.name == uniform) u = &x;
    if (!u) {
        e->uniforms.push_back(Uniform{});
        u = &e->uniforms.back();
        u->name = uniform;
    }
    u->is_int = is_int;
    u->int_value = iv;
    u->count = count;
    for (int i = 0; i < count; ++i) u->values[i] = values[i];
    return true;
}
}  // namespace

bool set(const std::string& effect, const std::string& uniform, const float* values, int count) {
    if (count < 1 || count > 4 || !values) return false;
    return set_uniform(effect, uniform, values, count, false, 0);
}

bool set_int(const std::string& effect, const std::string& uniform, int value) { return set_uniform(effect, uniform, nullptr, 0, true, value); }

const std::vector<Preset>& presets() { return kPresets; }

bool add_preset(const std::string& preset, int order, const std::string& as_name) {
    for (const Preset& p : kPresets)
        if (preset == p.name) return add(as_name.empty() ? preset : as_name, p.source, order);
    return false;
}

std::vector<Info> effects() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<Info> out;
    for (const Effect& e : g_effects) out.push_back({e.name, e.order, e.stage, e.enabled, e.compiled, e.error, e.file});
    std::stable_sort(out.begin(), out.end(), [](const Info& a, const Info& b) {
        if (a.stage != b.stage) return a.stage == Stage::hdr;
        return a.order != b.order ? a.order < b.order : a.name < b.name;
    });
    return out;
}

Status status() {
    Status s;
    s.installed = g_on.load();
    s.hdr_ready = g_hdr_hooked;
    s.hdr_error = g_hdr_error;
    s.frames_drawn = g_frames_drawn.load();
    s.hdr_frames = g_hdr_frames.load();
    s.width = g_last_w.load();
    s.height = g_last_h.load();
    s.light_program = g_light_seen.load();
    std::lock_guard<std::mutex> lock(g_mutex);
    s.gl_error = g_gl_error;
    return s;
}

namespace testing {
void start() {   // a test has no game to hook; install() would fail without one
    QueryPerformanceCounter(&g_start);
    g_on.store(true);
}
bool run_frame(void* (*resolver)(const char* name), unsigned framebuffer, int width, int height) {
    return draw_screen(resolver ? resolver : &default_resolver, framebuffer, width, height);
}
bool run_hdr(void* (*resolver)(const char* name), const HdrInputs& in) {
    HdrTarget t;
    t.scene = in.scene_texture;
    t.scene_fbo = in.scene_fbo;
    t.width = in.width;
    t.height = in.height;
    t.units[0] = in.scene_texture;
    t.units[1] = in.scene_depth ? in.scene_depth : in.depth;
    t.units[2] = in.depth;
    t.units[3] = in.normal;
    t.units[4] = in.albedo;
    t.units[5] = in.velocity;
    return draw_hdr(resolver ? resolver : &default_resolver, t, in.light_program);
}
}  // namespace testing

}  // namespace sw::postfx
