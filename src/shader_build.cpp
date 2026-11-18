// shader_build.cpp - macros, GLSL version and rebuild of the game's shaders (sw/shader_build.hpp, REPORT 3.213).
#include "sw/shader_build.hpp"

#include "sw/generated_gl.hpp"
#include "sw/hook.hpp"
#include "sw/runtime.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace sw::shader_build {

// ---- the text edit (pure) ------------------------------------------------------------------------------------------------

bool valid_name(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    if (!(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) return false;
    for (char c : name)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string leaf(const std::string& path) {
    const std::size_t at = path.find_last_of("/\\");
    return at == std::string::npos ? path : path.substr(at + 1);
}

// The lines of a text, each with its own line end.
std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start + 1));
        start = end + 1;
    }
    return lines;
}

std::string eol_of(const std::string& line) {
    if (line.size() >= 2 && line.compare(line.size() - 2, 2, "\r\n") == 0) return "\r\n";
    if (!line.empty() && line.back() == '\n') return "\n";
    return "\n";
}

std::size_t skip_space(const std::string& s, std::size_t i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return i;
}

// "#version ..." (spaces allowed around '#')
bool is_version_line(const std::string& line) {
    std::size_t i = skip_space(line, 0);
    if (i >= line.size() || line[i] != '#') return false;
    i = skip_space(line, i + 1);
    return line.compare(i, 7, "version") == 0;
}

// "#define NAME", "#define NAME value", "#define NAME(x) ..."
bool is_define_of(const std::string& line, const std::string& name) {
    std::size_t i = skip_space(line, 0);
    if (i >= line.size() || line[i] != '#') return false;
    i = skip_space(line, i + 1);
    if (line.compare(i, 6, "define") != 0) return false;
    i += 6;
    const std::size_t name_at = skip_space(line, i);
    if (name_at == i) return false;   // "#definex"
    if (line.compare(name_at, name.size(), name) != 0) return false;
    const std::size_t after = name_at + name.size();
    if (after >= line.size()) return true;
    const char c = line[after];
    return c == ' ' || c == '\t' || c == '(' || c == '\r' || c == '\n';
}

}  // namespace

bool applies(const Macro& macro, const std::string& path) {
    if (macro.file.empty()) return true;
    const std::string file = lower(macro.file);
    const std::string name = lower(leaf(path));
    if (file[0] == '.') return name.size() >= file.size() && name.compare(name.size() - file.size(), file.size(), file) == 0;
    return name == lower(leaf(file));
}

std::string edit_source(const std::string& source, const std::string& path, int version, const std::string& profile,
                        const std::vector<Macro>& macros) {
    std::vector<std::string> lines = split_lines(source);
    std::size_t insert_at = 0;
    std::string eol = "\n";
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (!is_version_line(lines[i])) continue;
        eol = eol_of(lines[i]);
        if (version > 0) lines[i] = "#version " + std::to_string(version) + (profile.empty() ? "" : " " + profile) + eol;
        insert_at = i + 1;
        break;
    }
    if (insert_at == 0 && version > 0) {
        lines.insert(lines.begin(), "#version " + std::to_string(version) + (profile.empty() ? "" : " " + profile) + eol);
        insert_at = 1;
    }

    // the last macro of a name wins (a file macro after an all-files macro of the same name)
    std::vector<const Macro*> chosen;
    for (const Macro& m : macros) {
        if (!valid_name(m.name) || !applies(m, path)) continue;
        auto same = std::find_if(chosen.begin(), chosen.end(), [&](const Macro* c) { return c->name == m.name; });
        if (same != chosen.end()) chosen.erase(same);
        chosen.push_back(&m);
    }

    std::vector<std::string> added;
    for (const Macro* m : chosen) {
        const std::string text = "#define " + m->name + (m->value.empty() ? "" : " " + m->value);
        bool found = false;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (!is_define_of(lines[i], m->name)) continue;
            found = true;
            // a define split over lines ("\" at the end) goes as a whole
            std::size_t last = i;
            while (last + 1 < lines.size()) {
                std::string body = lines[last];
                while (!body.empty() && (body.back() == '\n' || body.back() == '\r' || body.back() == ' ' || body.back() == '\t')) body.pop_back();
                if (body.empty() || body.back() != '\\') break;
                ++last;
            }
            const std::string line_eol = eol_of(lines[last]);
            lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i) + 1, lines.begin() + static_cast<std::ptrdiff_t>(last) + 1);
            lines[i] = m->remove ? "// (removed by a mod) " + m->name + line_eol : text + line_eol;
        }
        if (!found && !m->remove) added.push_back(text + eol);
    }
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insert_at), added.begin(), added.end());

    std::string out;
    out.reserve(source.size() + 64 * added.size());
    for (const auto& l : lines) out += l;
    return out;
}

// ---- the game side ------------------------------------------------------------------------------------------------------

namespace {

using ShaderSourceFn = void (*)(unsigned, int, const char* const*, const int*);
using CompileShaderFn = void (*)(unsigned);
using LinkProgramFn = void (*)(unsigned);
using GetIvFn = void (*)(unsigned, unsigned, int*);
using GetLogFn = void (*)(unsigned, int, int*, char*);
using ResourceFn = void (*)(void*);

std::mutex g_mutex;   // macros, version, failures
std::vector<Macro> g_macros;
int g_version = 0;
std::string g_profile;
std::vector<Failure> g_failures;
std::unordered_map<unsigned, std::string> g_shader_files;   // GL shader id -> file (for the compile message)

std::atomic<bool> g_installed{false};
std::atomic<int> g_compiled{0};
std::atomic<unsigned long long> g_rebuilds{0};
std::atomic<int> g_last_programs{0};
// A rebuild request has a number. The game has several resource managers (one of the application, one per application state, one per
// scene - 21 call sites of 0x1b1b60); each carries out a request at its own next pass (REPORT 3.213a).
unsigned long long g_request = 0;                                  // under g_mutex
std::string g_rebuild_filter;                                      // under g_mutex
std::unordered_map<std::uintptr_t, unsigned long long> g_managers; // manager -> the last request it carried out (under g_mutex)
std::atomic<unsigned long long> g_request_seen{0};

ShaderSourceFn g_orig_source = nullptr;
CompileShaderFn g_orig_compile = nullptr;
LinkProgramFn g_orig_link = nullptr;
std::uintptr_t g_slot_source = 0, g_slot_compile = 0, g_slot_link = 0;

thread_local std::string t_file;      // the stage being built (set at the entry of 0x1b0740)
thread_local std::string t_program;   // its program

std::uint32_t slot_of(const char* name) {
    for (const auto& e : sw::render::kGlEntries)
        if (std::strcmp(e.name, name) == 0) return e.slot_rva;
    return 0;
}

template <class T>
T gl(const char* name) {
    void* fn = nullptr;
    const std::uint32_t slot = slot_of(name);
    if (!slot || !sw::Runtime::get().read_rva(slot, fn)) return nullptr;
    return reinterpret_cast<T>(fn);
}

std::uintptr_t address_of(std::uint32_t rva) { return reinterpret_cast<std::uintptr_t>(sw::Runtime::get().at_rva(rva)); }

std::string read_mm_string(std::uintptr_t at) {
    auto& rt = sw::Runtime::get();
    const char* text = nullptr;
    std::uint32_t length = 0;
    if (!rt.readable(reinterpret_cast<const void*>(at), 12)) return {};
    std::memcpy(&text, reinterpret_cast<const void*>(at), sizeof(text));
    std::memcpy(&length, reinterpret_cast<const void*>(at + 8), sizeof(length));
    if (!text || length == 0 || length > 512 || !rt.readable(text, length)) return {};
    return std::string(text, length);
}

void add_failure(const std::string& file, const std::string& message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_failures.size() < 64) g_failures.push_back({file, message});
}

// ---- the three wrappers in the GL slots ----
void w_shader_source(unsigned shader, int count, const char* const* strings, const int* lengths) {
    std::string text;
    for (int i = 0; i < count && strings; ++i) {
        if (!strings[i]) continue;
        if (lengths && lengths[i] >= 0) text.append(strings[i], static_cast<std::size_t>(lengths[i]));
        else text.append(strings[i]);
    }
    std::vector<Macro> macros;
    int version = 0;
    std::string profile;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        macros = g_macros;
        version = g_version;
        profile = g_profile;
        if (g_shader_files.size() > 4096) g_shader_files.clear();
        g_shader_files[shader] = t_file;
    }
    if (macros.empty() && version == 0) {
        g_orig_source(shader, count, strings, lengths);
        return;
    }
    const std::string edited = edit_source(text, t_file, version, profile, macros);
    const char* one = edited.c_str();
    g_orig_source(shader, 1, &one, nullptr);
}

void w_compile_shader(unsigned shader) {
    g_orig_compile(shader);
    g_compiled.fetch_add(1);
    static auto get_iv = gl<GetIvFn>("glGetShaderiv");
    static auto get_log = gl<GetLogFn>("glGetShaderInfoLog");
    if (!get_iv || !get_log) return;
    int ok = 1;
    get_iv(shader, 0x8B81 /* GL_COMPILE_STATUS */, &ok);
    if (ok) return;
    char log[2048] = {};
    get_log(shader, sizeof(log) - 1, nullptr, log);
    std::string file;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_shader_files.find(shader);
        if (it != g_shader_files.end()) file = it->second;
    }
    add_failure(file.empty() ? std::string("(shader ") + std::to_string(shader) + ")" : file, log);
}

void w_link_program(unsigned program) {
    g_orig_link(program);
    static auto get_iv = gl<GetIvFn>("glGetProgramiv");
    static auto get_log = gl<GetLogFn>("glGetProgramInfoLog");
    if (!get_iv || !get_log) return;
    int ok = 1;
    get_iv(program, 0x8B82 /* GL_LINK_STATUS */, &ok);
    if (ok) return;
    char log[2048] = {};
    get_log(program, sizeof(log) - 1, nullptr, log);
    add_failure(t_program.empty() ? std::string("(program ") + std::to_string(program) + ")" : t_program + " (link)", log);
}

// ---- the hooks ----
// 0x1b0740(program, loader, mm_string* path, ...): remember which file is being built on this thread
void on_stage_source(void* program, void*) {
    t_file = read_mm_string(static_cast<std::uintptr_t>(sw::Hook::arg(2)));
    t_program = read_mm_string(reinterpret_cast<std::uintptr_t>(program) + kResName);
}

bool read_ptr(std::uintptr_t at, std::uintptr_t& out) {
    auto& rt = sw::Runtime::get();
    if (!rt.readable(reinterpret_cast<const void*>(at), 8)) return false;
    std::memcpy(&out, reinterpret_cast<const void*>(at), 8);
    return true;
}
void write_ptr(std::uintptr_t at, std::uintptr_t value) { std::memcpy(reinterpret_cast<void*>(at), &value, 8); }
void add_count(std::uintptr_t list, int delta) {
    std::uint32_t n = 0;
    std::memcpy(&n, reinterpret_cast<const void*>(list + 0x10), 4);
    n = static_cast<std::uint32_t>(static_cast<int>(n) + delta);
    std::memcpy(reinterpret_cast<void*>(list + 0x10), &n, 4);
}

// 0x1b1b60(manager, loader): before the game builds the waiting resources, put the chosen programs back into the waiting list
void on_resource_pass(void* manager, void*) {
    const std::uintptr_t rm = reinterpret_cast<std::uintptr_t>(manager);
    std::string filter;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_managers.find(rm);
        if (it == g_managers.end()) {   // a manager seen for the first time builds its programs with the settings of now
            if (g_managers.size() > 256) g_managers.clear();
            g_managers[rm] = g_request;
            return;
        }
        if (it->second == g_request) return;
        it->second = g_request;
        filter = g_rebuild_filter;
    }
    auto& rt = sw::Runtime::get();
    const std::uintptr_t built = rm + kProgramsBuilt, waiting = rm + kProgramsWaiting;
    const std::uintptr_t vtable = address_of(kRvaProgramVtable);
    const std::uintptr_t delete_gl = address_of(kRvaProgramDeleteGl);
    if (!vtable || !delete_gl || !rt.readable(reinterpret_cast<const void*>(rm), kProgramsBuilt + 0x18)) return;

    std::vector<std::uintptr_t> chosen;
    std::uintptr_t node = 0;
    if (!read_ptr(built, node)) return;
    for (int guard = 0; node && guard < 4096; ++guard) {
        std::uintptr_t next = 0, owner = 0, vt = 0, slot2 = 0;
        if (!read_ptr(node + kResNext, next) || !read_ptr(node + kResList, owner) || !read_ptr(node, vt)) break;
        if (owner != built) break;   // not the list we think it is: touch nothing
        if (vt == vtable && read_ptr(vt + 16, slot2) && slot2 == delete_gl) {
            const std::string name = read_mm_string(node + kResName);
            if (filter.empty() || lower(name).find(lower(filter)) != std::string::npos) chosen.push_back(node);
        }
        node = next;
    }
    for (std::uintptr_t res : chosen) {
        reinterpret_cast<ResourceFn>(delete_gl)(reinterpret_cast<void*>(res));
        // unlink from "built"
        std::uintptr_t prev = 0, next = 0, head = 0, tail = 0;
        read_ptr(res + kResPrev, prev);
        read_ptr(res + kResNext, next);
        if (prev) write_ptr(prev + kResNext, next);
        if (next) write_ptr(next + kResPrev, prev);
        read_ptr(built, head);
        read_ptr(built + 8, tail);
        if (head == res) write_ptr(built, next);
        if (tail == res) write_ptr(built + 8, prev);
        add_count(built, -1);
        // append to "waiting" (as 0x1ad130 does)
        std::uintptr_t wtail = 0, whead = 0;
        read_ptr(waiting, whead);
        read_ptr(waiting + 8, wtail);
        if (!whead) {
            write_ptr(waiting, res);
            write_ptr(waiting + 8, res);
            write_ptr(res + kResPrev, 0);
        } else {
            write_ptr(wtail + kResNext, res);
            write_ptr(res + kResPrev, wtail);
            write_ptr(waiting + 8, res);
        }
        write_ptr(res + kResNext, 0);
        write_ptr(res + kResList, waiting);
        add_count(waiting, 1);
    }
    g_last_programs.fetch_add(static_cast<int>(chosen.size()));
    g_rebuilds.store(g_request_seen.load());
}

bool swap_slot(std::uintptr_t slot, void* wrapper, void** original) {
    auto& rt = sw::Runtime::get();
    void* current = nullptr;
    if (!rt.readable(reinterpret_cast<const void*>(slot), 8)) return false;
    std::memcpy(&current, reinterpret_cast<const void*>(slot), 8);
    if (!current) return false;
    *original = current;
    return rt.write_bytes(reinterpret_cast<void*>(slot), &wrapper, 8);
}
void restore_slot(std::uintptr_t slot, void* wrapper, void* original) {
    if (!slot || !original) return;
    auto& rt = sw::Runtime::get();
    void* current = nullptr;
    std::memcpy(&current, reinterpret_cast<const void*>(slot), 8);
    if (current == wrapper) rt.write_bytes(reinterpret_cast<void*>(slot), &original, 8);
}

bool bytes_at(std::uint32_t rva, const std::vector<std::uint8_t>& expect) {
    auto& rt = sw::Runtime::get();
    const void* p = rt.at_rva(rva);
    return p && rt.readable(p, expect.size()) && std::memcmp(p, expect.data(), expect.size()) == 0;
}

}  // namespace

bool install(std::string& error) {
    if (g_installed.load()) return true;
    auto& rt = sw::Runtime::get();
    if (!rt.attached()) {
        error = "the game is not the build of the mapping";
        return false;
    }
    // the facts of REPORT 3.213, checked before anything is changed
    const char version_text[] = "#version 330\n\n";
    const void* text = rt.at_rva(kRvaVersionText);
    if (!text || !rt.readable(text, sizeof(version_text) - 1) || std::memcmp(text, version_text, sizeof(version_text) - 1) != 0) {
        error = "the \"#version 330\" text is not where it was (or this is not the game client)";
        return false;
    }
    const std::uint32_t rel = kRvaVersionText - (kRvaVersionCopy + 8);
    if (!bytes_at(kRvaVersionCopy, {0xf2, 0x0f, 0x10, 0x05, static_cast<std::uint8_t>(rel), static_cast<std::uint8_t>(rel >> 8),
                                    static_cast<std::uint8_t>(rel >> 16), static_cast<std::uint8_t>(rel >> 24)})) {
        error = "the shader source builder 0x1b0740 is not the one this was made for";
        return false;
    }
    if (!bytes_at(0x1b1e3c, {0x48, 0x8b, 0x8b, 0xe0, 0x01, 0x00, 0x00}) || !bytes_at(0x1b1ea1, {0x48, 0x8d, 0x8b, 0xf8, 0x01, 0x00, 0x00})) {
        error = "the program lists of the resource manager moved";
        return false;
    }
    std::uintptr_t slot1 = 0, slot2 = 0;
    const std::uintptr_t vt = address_of(kRvaProgramVtable);
    if (!vt || !read_ptr(vt + 8, slot1) || !read_ptr(vt + 16, slot2) || slot1 != address_of(kRvaProgramCreateGl) ||
        slot2 != address_of(kRvaProgramDeleteGl)) {
        error = "the vtable of mm_resource_program is not the one this was made for";
        return false;
    }
    const std::uint32_t s_source = slot_of("glShaderSource"), s_compile = slot_of("glCompileShader"), s_link = slot_of("glLinkProgram");
    if (!s_source || !s_compile || !s_link) {
        error = "the GL slots are not in the table";
        return false;
    }
    if (rt.hook_rva(kRvaStageSource, &on_stage_source, nullptr, error) != sw::HookStatus::ok) return false;
    if (rt.hook_rva(kRvaResourcePass, &on_resource_pass, nullptr, error) != sw::HookStatus::ok) {
        std::string ignore;
        rt.unhook(address_of(kRvaStageSource), &on_stage_source, nullptr, ignore);
        return false;
    }
    g_slot_source = address_of(s_source);
    g_slot_compile = address_of(s_compile);
    g_slot_link = address_of(s_link);
    void* o_source = nullptr;
    void* o_compile = nullptr;
    void* o_link = nullptr;
    if (!swap_slot(g_slot_source, reinterpret_cast<void*>(&w_shader_source), &o_source)) {
        error = "glShaderSource is not loaded yet";
        uninstall();
        return false;
    }
    g_orig_source = reinterpret_cast<ShaderSourceFn>(o_source);
    if (swap_slot(g_slot_compile, reinterpret_cast<void*>(&w_compile_shader), &o_compile)) g_orig_compile = reinterpret_cast<CompileShaderFn>(o_compile);
    if (swap_slot(g_slot_link, reinterpret_cast<void*>(&w_link_program), &o_link)) g_orig_link = reinterpret_cast<LinkProgramFn>(o_link);
    g_installed.store(true);
    return true;
}

void uninstall() {
    auto& rt = sw::Runtime::get();
    restore_slot(g_slot_link, reinterpret_cast<void*>(&w_link_program), reinterpret_cast<void*>(g_orig_link));
    restore_slot(g_slot_compile, reinterpret_cast<void*>(&w_compile_shader), reinterpret_cast<void*>(g_orig_compile));
    restore_slot(g_slot_source, reinterpret_cast<void*>(&w_shader_source), reinterpret_cast<void*>(g_orig_source));
    std::string ignore;
    if (rt.attached()) {
        rt.unhook(address_of(kRvaResourcePass), &on_resource_pass, nullptr, ignore);
        rt.unhook(address_of(kRvaStageSource), &on_stage_source, nullptr, ignore);
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_managers.clear();
    }
    g_installed.store(false);
}

bool installed() { return g_installed.load(); }

bool define(const std::string& name, const std::string& value, const std::string& file) {
    if (!valid_name(name) || value.find_first_of("\r\n") != std::string::npos) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = std::find_if(g_macros.begin(), g_macros.end(), [&](const Macro& m) { return m.name == name && lower(m.file) == lower(file); });
    if (it != g_macros.end()) g_macros.erase(it);
    g_macros.push_back({name, value, file, false});
    return true;
}

bool undefine(const std::string& name, const std::string& file) {
    if (!valid_name(name)) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = std::find_if(g_macros.begin(), g_macros.end(), [&](const Macro& m) { return m.name == name && lower(m.file) == lower(file); });
    if (it != g_macros.end()) g_macros.erase(it);
    g_macros.push_back({name, "", file, true});
    return true;
}

bool forget(const std::string& name, const std::string& file) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = std::find_if(g_macros.begin(), g_macros.end(), [&](const Macro& m) { return m.name == name && lower(m.file) == lower(file); });
    if (it == g_macros.end()) return false;
    g_macros.erase(it);
    return true;
}

void clear() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_macros.clear();
    g_version = 0;
    g_profile.clear();
}

bool glsl_version(int version, const std::string& profile) {
    if (version != 0 && (version < 110 || version > 460)) return false;
    if (!profile.empty() && profile != "core" && profile != "compatibility") return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_version = version;
    g_profile = profile;
    return true;
}

int glsl_version() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_version;
}

bool rebuild(const std::string& filter) {
    if (!g_installed.load()) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_rebuild_filter = filter;
    g_failures.clear();
    g_last_programs.store(0);
    g_request_seen.store(++g_request);
    return true;
}

Status status() {
    Status s;
    s.installed = g_installed.load();
    s.rebuilds = g_rebuilds.load();
    s.last_programs = g_last_programs.load();
    s.compiled = g_compiled.load();
    std::lock_guard<std::mutex> lock(g_mutex);
    s.failures = g_failures;
    s.version = g_version;
    s.macros = g_macros.size();
    return s;
}

}  // namespace sw::shader_build
