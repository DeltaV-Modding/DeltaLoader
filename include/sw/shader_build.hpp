// shader_build.hpp - change how the game builds its shaders: your own #define macros, a newer GLSL version, and a rebuild of
// the shaders while the game runs (no restart, no reloading the save). REPORT 3.213.
//
//   #include "sw/mod.hpp"
//   #include "sw/shader_build.hpp"
//
//   void sw_mod_start() {
//       std::string error;
//       if (!sw::shader_build::install(error)) { sw::mod::log("shader_build: %s", error.c_str()); return; }
//       sw::shader_build::glsl_version(460);                       // every shader is built as "#version 460" (0 = the game's 330)
//       sw::shader_build::define("OPAL_SSR", "1");                  // in every shader
//       sw::shader_build::define("OPAL_FOG_STEPS", "32", "light.glslf");   // only in this file
//       sw::shader_build::rebuild();                                // all shaders, at the next frame, on the render thread
//   }
//
// HOW THE GAME BUILDS A SHADER (static reading of the 1.15.27 code, REPORT 3.213):
//   * every shader program is an `mm_resource_program` (vtable 0xb14748, RTTI) of the resource manager. Its slot 1 `create_gl`
//     0x1b0250 builds the program; slot 2 `delete_gl` 0x1b0700 deletes it (glDeleteProgram + glDeleteShader of +0x50 / +0x48 /
//     +0x4c / +0x44);
//   * `0x1b0740` makes the source of one stage: "#version 330\n\n" (copied from .rdata 0xb87c20), one "#define NAME VALUE" per
//     entry of the program's define list (graphics settings: vertex +0x580, fragment +0x598, geometry +0x568), then the file with
//     its #include lines resolved, and hands it to glShaderSource (the slot of the GL loader, gl_functions.tsv);
//   * the resource manager keeps the programs in two lists: waiting (+0x1e0) and built (+0x1f8). `0x1b1b60` runs every frame
//     on the render thread (scene draw, menus) and calls create_gl for every waiting resource, then moves it to "built".
// WHAT THIS HEADER DOES WITH IT:
//   * define / glsl_version: the glShaderSource slot gets a wrapper that edits the text the driver receives: the #version line,
//     and for every macro of yours the game's own "#define NAME ..." line is replaced (graphics settings AND #defines written in a
//     .glsl file, e.g. the config block of a shader pack), or a new line is added right after #version;
//   * rebuild: at the entry of 0x1b1b60 the chosen programs are deleted (slot 2) and put back into the waiting list - the game's
//     own code builds them again with its own arguments a moment later. Nothing of the game's code is patched;
//   * errors: glCompileShader / glLinkProgram get wrappers that read the status and keep the driver's message (failures()).
// Changes take effect when a shader is built: call rebuild() after define() / glsl_version(). The files of rom\graphics\shaders
// (and of a shader mod's Graphics\shaders, which the launcher lays over rom) are read again at a rebuild, so an edited file is
// picked up too.
//
// PROVEN: the text edits (native/tests/api_test.cpp, no game); with SW_GAME_DIR the bytes: the copy of "#version 330", the
// vtable slots, the list offsets in 0x1b1b60, Hook::plan of 0x1b1b60 and 0x1b0740. NOT TRIED in a running game.
// A #version above what the driver supports makes every shader fail: failures() names them, glsl_version(0) + rebuild() repairs it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sw::shader_build {

// ---- set up ----------------------------------------------------------------------------------------------------------------
// Installs the wrappers and the hook. Needs the game of the mapping (fails closed: checks the bytes first). Callable from any thread.
bool install(std::string& error);
// Takes everything out again (the game's own glShaderSource etc. back in their slots). Shaders built with your macros stay as
// they are until the next rebuild - call clear() + rebuild() first if they should go back to the game's.
void uninstall();
bool installed();

// ---- macros and version ----------------------------------------------------------------------------------------------------
// `file`: "" = every shader; a file name ("light.glslf", "shadow.glslv"), or a stage: ".glslv", ".glslf", ".glslg".
// `name` must be a valid macro name (letters, digits, _). `value` may be empty ("#define NAME").
bool define(const std::string& name, const std::string& value = "1", const std::string& file = "");
// Removes the game's (or a .glsl file's) "#define NAME ..." line, so the shader takes its #ifndef default / #else branch.
bool undefine(const std::string& name, const std::string& file = "");
// Forgets one macro of yours (the game's line stays as it was).
bool forget(const std::string& name, const std::string& file = "");
// Forgets all macros of yours and the version.
void clear();
// The GLSL version of every shader: 0 = the game's (330). 400, 410, 420, 430, 440, 450, 460 unlock newer features if the driver
// supports them. `profile`: "" (core, as the game), "core" or "compatibility".
bool glsl_version(int version, const std::string& profile = "");
int glsl_version();

// ---- rebuild ---------------------------------------------------------------------------------------------------------------
// Rebuilds every shader program whose name contains `filter` ("" = all, "light" = light, light_omni, ...), at the next frame.
// Returns false before install(). The result is in status() a few frames later.
bool rebuild(const std::string& filter = "");

struct Failure {
    std::string file;      // the shader file (graphics/shaders/light.glslf) or the program for a link error
    std::string message;   // what the driver said
};
struct Status {
    bool installed = false;
    unsigned long long rebuilds = 0;   // rebuild requests carried out
    int last_programs = 0;             // programs rebuilt by the last request
    int compiled = 0;                  // shaders compiled since install() (the game's own builds too)
    std::vector<Failure> failures;     // compile / link errors of the last rebuild (and of builds since)
    int version = 0;                   // the GLSL version in use (0 = the game's)
    std::size_t macros = 0;            // macros of yours
};
Status status();

// ---- the text edit itself (pure, used by the wrapper; public for tests and tools) ------------------------------------------
struct Macro {
    std::string name;
    std::string value;
    std::string file;      // "" = all; a file name; or a stage ".glslv" / ".glslf" / ".glslg"
    bool remove = false;   // undefine(): drop the line
};
// Does `macro` apply to the shader file `path` ("graphics/shaders/light.glslf")?
bool applies(const Macro& macro, const std::string& path);
// The source the driver gets: `source` with its first "#version" line set to `version` (0 = unchanged) and the macros applied.
std::string edit_source(const std::string& source, const std::string& path, int version, const std::string& profile,
                        const std::vector<Macro>& macros);
// A valid macro name: [A-Za-z_][A-Za-z0-9_]*, at most 64 characters.
bool valid_name(const std::string& name);

// ---- addresses (1.15.27, client RVAs; the server has no renderer) ---------------------------------------------------------
inline constexpr std::uint32_t kRvaProgramVtable = 0xb14748;      // mm_resource_program (RTTI)
inline constexpr std::uint32_t kRvaProgramCreateGl = 0x1b0250;    // slot 1
inline constexpr std::uint32_t kRvaProgramDeleteGl = 0x1b0700;    // slot 2
inline constexpr std::uint32_t kRvaStageSource = 0x1b0740;        // builds the source of one stage, calls glShaderSource
inline constexpr std::uint32_t kRvaVersionText = 0xb87c20;        // "#version 330\n\n"
inline constexpr std::uint32_t kRvaVersionCopy = 0x1b098e;        // movsd xmm0, [0xb87c20] inside 0x1b0740
inline constexpr std::uint32_t kRvaResourcePass = 0x1b1b60;       // builds every waiting resource (render thread, every frame)
inline constexpr std::uint32_t kProgramsWaiting = 0x1e0;          // resource manager + 0x1e0: list of programs to build
inline constexpr std::uint32_t kProgramsBuilt = 0x1f8;            // + 0x1f8: list of built programs
// a list: {head +0, tail +8, count u32 +0x10}; a resource: prev +0x10, next +0x18, its list +0x20, name mm_string +0x30 (text
// +0x30, length u32 +0x38), GL program +0x50.
inline constexpr std::uint32_t kResPrev = 0x10, kResNext = 0x18, kResList = 0x20, kResName = 0x30, kResNameLength = 0x38,
                               kResProgramId = 0x50;

}  // namespace sw::shader_build
