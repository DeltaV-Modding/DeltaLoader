// postfx.cpp - post-processing effects over the finished picture (sw/postfx.hpp, REPORT 3.225).
//
// Two sides: the effect list (any thread, under g_mutex) and the GL side (render thread only, at the frame end). The frame
// end copies framebuffer 0 into one RGBA8 texture and draws each effect with a full-screen triangle back into framebuffer 0;
// the next effect copies again, so every effect reads the result of the one before. Every GL state that is changed is saved
// first and put back (the same list as sw/panel.cpp, plus the read framebuffer, the read buffer and the unpack buffer).

#include "sw/postfx.hpp"

#include "sw/render.hpp"

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
std::uint64_t g_next_version = 1;
std::atomic<bool> g_on{false};
bool g_hooked = false;
std::atomic<std::uint64_t> g_frames_drawn{0};
std::atomic<int> g_last_w{0}, g_last_h{0};
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

// under g_mutex
void put(const std::string& name, const std::string& source, int order, const std::wstring& file, const std::wstring& folder,
         unsigned long long stamp) {
    Effect* e = find(name);
    if (!e) {
        g_effects.push_back(Effect{});
        e = &g_effects.back();
        e->name = name;
    }
    if (e->source != source || e->version == 0) {
        e->source = source;
        e->version = g_next_version++;
        e->compiled = false;
        e->error.clear();
    }
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

const char kMain[] = R"(
void main() {
    vec4 c = effect(texture(u_scene, v_uv), v_uv);
    o_color = vec4(c.rgb, 1.0);
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

}  // namespace

std::string fragment_source(const std::string& user) {
    std::string version = "#version 330";
    std::string body = user;
    // a #version line of the user's replaces 330 (it must be the first line of the shader)
    std::istringstream lines(user);
    std::string line;
    std::size_t pos = 0;
    while (std::getline(lines, line)) {
        std::size_t i = 0;
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (line.compare(i, 8, "#version") == 0) {
            std::string v = line.substr(i);
            while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
            version = v;
            body = user.substr(0, pos) + "\n" + user.substr(std::min(user.size(), pos + line.size() + 1));
            break;
        }
        pos += line.size() + 1;
    }
    if (has_main(body)) return version + "\n#line 1\n" + body + "\n";
    return version + "\n" + kPrelude + "#line 1\n" + body + "\n" + kMain;
}

namespace {

// ---------------------------------------------------------------------------------------------------------------- GL side

using GLchar = char;
constexpr GLenum kFragmentShader = 0x8B30, kVertexShader = 0x8B31, kCompileStatus = 0x8B81, kLinkStatus = 0x8B82;
constexpr GLenum kInfoLogLength = 0x8B84, kTexture0 = 0x84C0, kReadFramebuffer = 0x8CA8, kDrawFramebuffer = 0x8CA9;
constexpr GLenum kCurrentProgram = 0x8B8D, kVertexArrayBinding = 0x85B5, kActiveTexture = 0x84E0;
constexpr GLenum kDrawFramebufferBinding = 0x8CA6, kReadFramebufferBinding = 0x8CAA, kSamplerBinding = 0x8919;
constexpr GLenum kFramebufferSrgb = 0x8DB9, kClampToEdge = 0x812F, kRgba8 = 0x8058;
constexpr GLenum kPixelUnpackBuffer = 0x88EC, kPixelUnpackBufferBinding = 0x88EF;

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
    void(APIENTRY* UseProgram)(GLuint);
    GLint(APIENTRY* GetUniformLocation)(GLuint, const GLchar*);
    void(APIENTRY* Uniform1i)(GLint, GLint);
    void(APIENTRY* Uniform1f)(GLint, GLfloat);
    void(APIENTRY* Uniform2f)(GLint, GLfloat, GLfloat);
    void(APIENTRY* Uniform3f)(GLint, GLfloat, GLfloat, GLfloat);
    void(APIENTRY* Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    void(APIENTRY* GenVertexArrays)(GLsizei, GLuint*);
    void(APIENTRY* BindVertexArray)(GLuint);
    void(APIENTRY* DeleteVertexArrays)(GLsizei, const GLuint*);
    void(APIENTRY* ActiveTexture)(GLenum);
    void(APIENTRY* BindFramebuffer)(GLenum, GLuint);
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
           load(r, g.UseProgram, "glUseProgram") && load(r, g.GetUniformLocation, "glGetUniformLocation") &&
           load(r, g.Uniform1i, "glUniform1i") && load(r, g.Uniform1f, "glUniform1f") && load(r, g.Uniform2f, "glUniform2f") &&
           load(r, g.Uniform3f, "glUniform3f") && load(r, g.Uniform4f, "glUniform4f") &&
           load(r, g.GenVertexArrays, "glGenVertexArrays") && load(r, g.BindVertexArray, "glBindVertexArray") &&
           load(r, g.DeleteVertexArrays, "glDeleteVertexArrays") && load(r, g.ActiveTexture, "glActiveTexture") &&
           load(r, g.BindFramebuffer, "glBindFramebuffer") && load(r, g.BindSampler, "glBindSampler") &&
           load(r, g.BindBuffer, "glBindBuffer");
}

struct Program {
    std::string name;
    std::uint64_t version = 0;
    GLuint program = 0;
    GLint u_scene = -1, u_resolution = -1, u_time = -1, u_frame = -1;
    bool ok = false;
};

struct GlSide {
    bool loaded = false, failed = false;
    Gl gl{};
    GLuint vertex = 0, vao = 0, tex = 0;
    int tex_w = 0, tex_h = 0;
    std::vector<Program> programs;
    HGLRC context = nullptr;          // the context the objects belong to
};
GlSide g_side;
LARGE_INTEGER g_start{};
std::uint64_t g_frame_index = 0;

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
        if (s.tex) glDeleteTextures(1, &s.tex);
    }
    s.programs.clear();
    s.vertex = s.vao = s.tex = 0;
    s.tex_w = s.tex_h = 0;
    s.context = nullptr;
}

struct Saved {
    GLint program, vao, active_texture, texture, sampler, read_fb, draw_fb, read_buffer, viewport[4], unpack_buffer;
    GLboolean blend, depth, cull, scissor, stencil, srgb, color_mask[4];
};

void save(Gl& g, Saved& s) {
    glGetIntegerv(kCurrentProgram, &s.program);
    glGetIntegerv(kVertexArrayBinding, &s.vao);
    glGetIntegerv(kActiveTexture, &s.active_texture);
    g.ActiveTexture(kTexture0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &s.texture);
    glGetIntegerv(kSamplerBinding, &s.sampler);
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

void restore(Gl& g, const Saved& s) {
    g.UseProgram(static_cast<GLuint>(s.program));
    g.BindVertexArray(static_cast<GLuint>(s.vao));
    g.ActiveTexture(kTexture0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(s.texture));
    g.BindSampler(0, static_cast<GLuint>(s.sampler));
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

struct Snapshot {
    std::string name, source;
    std::uint64_t version;
    std::vector<Uniform> uniforms;
};

// The work of one frame end. False when nothing was drawn.
bool draw_frame(Resolver resolver, GLuint framebuffer, int width, int height) {
    HGLRC context = wglGetCurrentContext();
    if (!g_on.load()) {
        if (g_side.context) release_side();
        return false;
    }
    if (!context || width <= 0 || height <= 0) return false;

    std::vector<Snapshot> list;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::vector<const Effect*> on;
        for (const Effect& e : g_effects)
            if (e.enabled && !e.source.empty()) on.push_back(&e);
        std::stable_sort(on.begin(), on.end(), [](const Effect* a, const Effect* b) {
            return a->order != b->order ? a->order < b->order : a->name < b->name;
        });
        for (const Effect* e : on) list.push_back({e->name, e->source, e->version, e->uniforms});
    }
    if (list.empty()) return false;

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
    Gl& g = side.gl;
    if (!side.context) {
        side.context = context;
        std::string error;
        side.vertex = compile(g, kVertexShader, kVertex, error);
        g.GenVertexArrays(1, &side.vao);
        glGenTextures(1, &side.tex);
        if (!side.vertex) {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_gl_error = "the vertex shader did not compile: " + error;
            return false;
        }
    }
    if (!side.vertex) return false;

    // (re)build the programs whose source changed
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
        std::string error;
        const GLuint fs = compile(g, kFragmentShader, fragment_source(s.source), error);
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

    Saved saved{};
    save(g, saved);
    g.BindBuffer(kPixelUnpackBuffer, 0);
    g.ActiveTexture(kTexture0);
    g.BindSampler(0, 0);
    glBindTexture(GL_TEXTURE_2D, side.tex);
    if (side.tex_w != width || side.tex_h != height) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, kClampToEdge);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, kClampToEdge);
        glTexImage2D(GL_TEXTURE_2D, 0, kRgba8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        side.tex_w = width;
        side.tex_h = height;
    }
    g.BindFramebuffer(kReadFramebuffer, framebuffer);
    g.BindFramebuffer(kDrawFramebuffer, framebuffer);
    if (framebuffer == 0) glReadBuffer(GL_BACK);
    glViewport(0, 0, width, height);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(kFramebufferSrgb);   // the picture is in display colours already: copy and draw without conversion
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    g.BindVertexArray(side.vao);

    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    const float seconds = static_cast<float>(static_cast<double>(now.QuadPart - g_start.QuadPart) / static_cast<double>(freq.QuadPart));
    int drawn = 0;
    for (const Snapshot& s : list) {
        const Program* p = nullptr;
        for (const Program& x : side.programs)
            if (x.name == s.name) p = &x;
        if (!p || !p->ok) continue;
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);   // the picture so far
        g.UseProgram(p->program);
        if (p->u_scene >= 0) g.Uniform1i(p->u_scene, 0);
        if (p->u_resolution >= 0) g.Uniform2f(p->u_resolution, static_cast<float>(width), static_cast<float>(height));
        if (p->u_time >= 0) g.Uniform1f(p->u_time, seconds);
        if (p->u_frame >= 0) g.Uniform1i(p->u_frame, static_cast<GLint>(g_frame_index & 0x7fffffff));
        for (const Uniform& u : s.uniforms) {
            const GLint at = g.GetUniformLocation(p->program, u.name.c_str());
            if (at < 0) continue;
            if (u.is_int) g.Uniform1i(at, u.int_value);
            else if (u.count == 1) g.Uniform1f(at, u.values[0]);
            else if (u.count == 2) g.Uniform2f(at, u.values[0], u.values[1]);
            else if (u.count == 3) g.Uniform3f(at, u.values[0], u.values[1], u.values[2]);
            else if (u.count == 4) g.Uniform4f(at, u.values[0], u.values[1], u.values[2], u.values[3]);
        }
        glDrawArrays(GL_TRIANGLES, 0, 3);
        ++drawn;
    }
    restore(g, saved);

    // programs of effects that are gone
    side.programs.erase(std::remove_if(side.programs.begin(), side.programs.end(),
                                       [&](Program& p) {
                                           for (const Snapshot& s : list)
                                               if (s.name == p.name) return false;
                                           std::lock_guard<std::mutex> lock(g_mutex);
                                           if (find(p.name)) return false;   // only switched off: keep it
                                           if (p.program) g.DeleteProgram(p.program);
                                           return true;
                                       }),
                        side.programs.end());

    ++g_frame_index;
    g_last_w.store(width);
    g_last_h.store(height);
    if (drawn > 0) g_frames_drawn.fetch_add(1);
    return drawn > 0;
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
    draw_frame(&default_resolver, 0, w, h);
}

const std::vector<Preset> kPresets = {
    {"vignette", "darker corners: u_amount 0.35, u_softness 0.45",
     R"(uniform float u_amount = 0.35;
uniform float u_softness = 0.45;
vec4 effect(vec4 color, vec2 uv) {
    vec2 d = uv - 0.5;
    d.x *= u_resolution.x / u_resolution.y;
    float v = smoothstep(0.75, 0.75 - u_softness, length(d));
    return vec4(color.rgb * mix(1.0 - u_amount, 1.0, v), 1.0);
}
)"},
    {"sharpen", "sharper edges: u_amount 0.4 (0 = off, 1 = strong)",
     R"(uniform float u_amount = 0.4;
vec4 effect(vec4 color, vec2 uv) {
    vec2 t = texel();
    vec3 blur = (scene(uv + vec2(t.x, 0.0)).rgb + scene(uv - vec2(t.x, 0.0)).rgb +
                 scene(uv + vec2(0.0, t.y)).rgb + scene(uv - vec2(0.0, t.y)).rgb) * 0.25;
    return vec4(clamp(color.rgb + (color.rgb - blur) * u_amount * 2.0, 0.0, 1.0), 1.0);
}
)"},
    {"saturation", "colour strength: u_amount 1.2 (0 = grey, 1 = unchanged)",
     R"(uniform float u_amount = 1.2;
vec4 effect(vec4 color, vec2 uv) {
    float grey = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
    return vec4(clamp(mix(vec3(grey), color.rgb, u_amount), 0.0, 1.0), 1.0);
}
)"},
    {"contrast", "contrast and brightness: u_contrast 1.1, u_brightness 0.0",
     R"(uniform float u_contrast = 1.1;
uniform float u_brightness = 0.0;
vec4 effect(vec4 color, vec2 uv) {
    return vec4(clamp((color.rgb - 0.5) * u_contrast + 0.5 + u_brightness, 0.0, 1.0), 1.0);
}
)"},
    {"tint", "colour grade: u_color (vec3) 1.0 0.97 0.92 multiplies the picture, u_amount 1.0",
     R"(uniform vec3 u_color = vec3(1.0, 0.97, 0.92);
uniform float u_amount = 1.0;
vec4 effect(vec4 color, vec2 uv) {
    return vec4(mix(color.rgb, color.rgb * u_color, u_amount), 1.0);
}
)"},
    {"grain", "film grain: u_amount 0.04",
     R"(uniform float u_amount = 0.04;
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
vec4 effect(vec4 color, vec2 uv) {
    float n = hash(uv * u_resolution + vec2(float(u_frame % 997), u_time)) - 0.5;
    return vec4(clamp(color.rgb + n * u_amount, 0.0, 1.0), 1.0);
}
)"},
    {"chromatic_aberration", "colour fringes towards the edges: u_amount 1.5 (pixels at the corner)",
     R"(uniform float u_amount = 1.5;
vec4 effect(vec4 color, vec2 uv) {
    vec2 d = (uv - 0.5) * 2.0 * texel() * u_amount;
    return vec4(scene(uv + d).r, color.g, scene(uv - d).b, 1.0);
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
    QueryPerformanceCounter(&g_start);
    g_on.store(true);
    return true;
}

void uninstall() { g_on.store(false); }

bool add(const std::string& name, const std::string& glsl, int order) {
    if (!valid_name(name) || glsl.empty()) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    put(name, glsl, order, L"", L"", 0);
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
    put(name, text, order, path, L"", stamp);
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
            put(name, text, order_of(file_name), path, folder, stamp);
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
    for (const Effect& e : g_effects) out.push_back({e.name, e.order, e.enabled, e.compiled, e.error, e.file});
    std::stable_sort(out.begin(), out.end(), [](const Info& a, const Info& b) { return a.order != b.order ? a.order < b.order : a.name < b.name; });
    return out;
}

Status status() {
    Status s;
    s.installed = g_on.load();
    s.frames_drawn = g_frames_drawn.load();
    s.width = g_last_w.load();
    s.height = g_last_h.load();
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
    return draw_frame(resolver ? resolver : &default_resolver, framebuffer, width, height);
}
}  // namespace testing

}  // namespace sw::postfx
