#pragma once

#include "sw/generated_gl.hpp"
#include "sw/generated_render.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sw::render {

// Called once per frame, on the render thread, with the GL context current, just before the
// frame is presented. `window` is the GLFW window of the game.
using FrameCallback = void (*)(void* window, void* user);

// Hooks the swap (0x90740). False if the game does not match, the hook fails, or this is the
// 32 bit build.
bool watch();

// The swap was called in the last moments.
bool ready();

// Adds a callback for the end of a frame. False before watch(), or when the same
// callback/user pair is already there.
bool on_frame_end(FrameCallback callback, void* user);

// Removes all callbacks of this module.
void remove_callbacks();

// The number of frames presented since watch(), and the time between the last two
// presents in milliseconds.
std::uint64_t frames();
double frame_ms();

// The window of the last frame and its device context (window + 0x270).
void* window();
void* window_dc();

// The entry of the table for this name, or nullptr. Names are the OpenGL names
// ("glBindFramebuffer"), as in the mapping gl_functions.tsv.
const GlEntry* gl_entry(const char* name);

// The function pointer the game holds for this name (what wglGetProcAddress returned, or the
// entry of OPENGL32.dll), or nullptr when the name is unknown or the game has not loaded it.
void* gl_function(const char* name);

// How many places of the engine call this function (0 = loaded but never used).
int gl_uses(const char* name);

// The viewport of the current framebuffer (glGetIntegerv(GL_VIEWPORT)). Only valid inside a
// frame callback (GL context current on this thread).
bool viewport(int& x, int& y, int& width, int& height);

enum class TargetKind { target, multiple, multisample, mipmapped };

enum class TargetFormat : int {
    none = 0,
    depth24 = 1,           // GL_DEPTH_COMPONENT24
    depth24_shadow = 2,    // GL_DEPTH_COMPONENT24, linear filter, compare mode
    depth24_stencil8 = 3,  // GL_DEPTH24_STENCIL8
    rgba8 = 4,
    rgba16f = 5,
    rgba32f = 6,
    r8 = 7,
    r16f = 8,
    rg8 = 9,
    rg16f = 10,
};

// The OpenGL internal format (GL_RGBA16F = 0x881A, ...) of a format, or 0.
unsigned gl_internal_format(TargetFormat format);
// Bytes per pixel of a colour format, 0 for depth formats and none.
int bytes_per_pixel(TargetFormat format);

struct Attachment {
    unsigned texture = 0;
    int index = 0;
    TargetFormat format = TargetFormat::none;
};

struct TargetInfo {
    std::uintptr_t address = 0;
    TargetKind kind = TargetKind::target;
    unsigned fbo = 0;                 // the OpenGL framebuffer object
    int width = 0, height = 0;
    unsigned color_texture = 0;       // first colour attachment (0 = none)
    TargetFormat color_format = TargetFormat::none;
    unsigned depth_texture = 0;
    TargetFormat depth_format = TargetFormat::none;
    int attachments = 0;              // colour attachments of a "multiple" target, else 1 or 0
    std::vector<Attachment> colors;   // all colour attachments (one for a single target)
};

// Hooks the create and delete functions of the target classes (8 functions). False if the
// game does not match or a hook fails.
bool watch_targets();

// Every live target seen since watch_targets() (targets created before it are not known).
std::vector<TargetInfo> targets();

// Reads one target object; false if the address is not a target of the family.
bool read_target(std::uintptr_t address, TargetInfo& out);

// Hooks the update of the view size (0x1be550) so that the graphics state object is known.
bool watch_view();

// The size of the frame buffer / of the window client area as the renderer has it, and the
// aspect ratio. False before the first update. The two sizes are equal in this build.
bool framebuffer_size(int& width, int& height);
bool window_size(int& width, int& height);
float aspect();

// The members of the pipeline object, by index (offset = 8 * index): each one points at a target.
enum class PipelineSlot : int {
    gbuffer = 0,        // mm_resource_target_multiple: RGBA16F, RGBA8, RG16F (velocity), depth/stencil
    scene = 1,          // RGBA16F + depth/stencil: the lit scene colour; input of the AA
    history = 2,        // RGBA16F + depth: the output of the last frame (texture_color_last_frame)
    aa_output = 3,      // RGBA16F + depth/stencil: output of TAA / input of FXAA; the tonemap reads it in TAA mode
    ssao = 4,           // R8
    ssao_blur = 5,      // R8
    bloom_a = 6,        // RGBA16F at the size / bloom divisor
    bloom_b = 7,
    depth_copy = 8,     // R16F + depth/stencil, only when the pipeline flag +0xc8 is set
    mipmapped = 0xf,
    shadow_small = 0x10,   // 128 x 128, depth only
    linear_depth = 0x11,   // R16F + depth/stencil, full size: handed to the scene passes
    shadow_large = 0x12,   // 1024 x 1024 R16F
    reflection_a = 0x16,   // 512 x 512 RGBA16F
    reflection_b = 0x17,
};

enum class AaMode : int { none = 0, fxaa = 1, temporal = 2 };

struct Pipeline {
    std::uintptr_t address = 0;
    AaMode aa_mode = AaMode::none;
    bool motion_blur = false;
    std::vector<TargetInfo> slots;   // indexed by PipelineSlot (missing = address 0)
    const TargetInfo& slot(PipelineSlot s) const;
};

// What a temporal upscaler reads and writes (OpenGL texture ids and sizes), taken from the pipeline.
struct UpscalerInputs {
    unsigned color_texture = 0;       // the lit scene colour (RGBA16F), pipeline [1]
    unsigned velocity_texture = 0;    // G-buffer attachment 2 (RG16F)
    unsigned depth_texture = 0;       // G-buffer depth/stencil
    unsigned history_texture = 0;     // pipeline [2]
    unsigned output_texture = 0;      // pipeline [3]: where the tonemap reads in TAA mode
    unsigned output_fbo = 0;
    int input_width = 0, input_height = 0;       // size of the scene colour
    int output_width = 0, output_height = 0;     // size of the output target (equal in this build)
    AaMode aa_mode = AaMode::none;
    bool velocity_is_rg16f = false;   // the format check that tells the velocity attachment is what it should be
};

// Hooks the frame function (0x6d4de0) so that the renderer object is known.
bool watch_renderer();

// The renderer object (nullptr before the first frame after watch_renderer()).
void* renderer();

// The pipeline of a view: 0 = the main view (renderer + 0x2628), 1 = the second view (+0x2718).
bool pipeline(int view, Pipeline& out);

// The inputs of an upscaler for the main view; false if the pipeline is not complete.
bool upscaler_inputs(UpscalerInputs& out);

// The shader object of the renderer for a shader name ("temporal_aa"): its offset inside
// the renderer object, or 0 when there is none.
std::uint32_t renderer_shader_offset(const char* shader);

inline constexpr std::uint32_t kRvaGlfwContextInit = 0x90f90;     // fills the function table of the context
inline constexpr std::uint32_t kRvaGlfwMakeCurrent = 0x906c0;     // table + 0x240
inline constexpr std::uint32_t kRvaGlfwSwapBuffers = 0x90740;     // table + 0x248; ends in SwapBuffers(dc)
inline constexpr std::size_t kOffContextMakeCurrent = 0x240;
inline constexpr std::size_t kOffContextSwapBuffers = 0x248;
inline constexpr std::size_t kOffWindowDc = 0x270;                // window + 0x270 = the device context

// Render target classes (vtable RVAs), the factories on the resource manager, and the fields.
inline constexpr std::uint32_t kVtableTargetBase = 0xb11770;
inline constexpr std::uint32_t kVtableTarget = 0xb0a778;
inline constexpr std::uint32_t kVtableTargetMultiple = 0xb5c4d8;
inline constexpr std::uint32_t kVtableTargetMultisample = 0xb0cd40;
inline constexpr std::uint32_t kVtableTargetMipmapped = 0xb06980;
inline constexpr std::uint32_t kRvaCreateTarget = 0x1b2630;            // (manager, w, h, colour, depth, name)
inline constexpr std::uint32_t kRvaCreateTargetMultiple = 0x1b2940;
inline constexpr std::uint32_t kRvaCreateTargetMultisample = 0x1b2ad0;
inline constexpr std::uint32_t kRvaCreateTargetMipmapped = 0x1b27c0;
inline constexpr std::uint32_t kRvaTargetCreateGl[4] = {0x1ae530, 0x1aec00, 0x1af2a0, 0x1af6a0};   // slot 1
inline constexpr std::uint32_t kRvaTargetDeleteGl[4] = {0x1ae680, 0x1aedd0, 0x1af340, 0x1af7d0};   // slot 2
inline constexpr std::uint32_t kRvaTargetBlit = 0x1ae700;              // slot 4 of mm_resource_target
inline constexpr std::uint32_t kRvaRenderTargetsCreate = 0x66e3a0;     // creates the targets of one view
inline constexpr std::uint32_t kRvaRenderTargetsCreateB = 0x66ec40;
inline constexpr std::uint32_t kRvaUpdateViewportSize = 0x1be550;      // GetClientRect -> graphics state
inline constexpr std::size_t kOffTargetMemory = 0xc;
inline constexpr std::size_t kOffTargetFbo = 0x28;
inline constexpr std::size_t kOffTargetWidth = 0x2c;
inline constexpr std::size_t kOffTargetHeight = 0x30;
inline constexpr std::size_t kOffTargetColorTexture = 0x48;
inline constexpr std::size_t kOffTargetColorFormat = 0x4c;
inline constexpr std::size_t kOffTargetDepthTexture = 0x50;
inline constexpr std::size_t kOffTargetDepthFormat = 0x54;
inline constexpr std::size_t kOffTargetMultipleColors = 0x48;          // data pointer of the ring (0xc bytes each)
inline constexpr std::size_t kOffTargetMultipleCapacity = 0x50;
inline constexpr std::size_t kOffTargetMultipleHead = 0x54;
inline constexpr std::size_t kOffTargetMultipleCount = 0x58;
inline constexpr std::size_t kOffTargetMultipleDepthTexture = 0x60;
inline constexpr std::size_t kOffTargetMultipleDepthFormat = 0x68;
inline constexpr std::size_t kOffGraphicsFramebufferWidth = 0x8f0;
inline constexpr std::size_t kOffGraphicsFramebufferHeight = 0x8f4;
inline constexpr std::size_t kOffGraphicsWindowWidth = 0x8f8;
inline constexpr std::size_t kOffGraphicsWindowHeight = 0x8fc;
inline constexpr std::size_t kOffGraphicsAspect = 0x900;

// The pipeline and the anti-aliasing passes.
inline constexpr std::size_t kOffRendererPipelineMain = 0x2628;        // the pipeline object inside the renderer
inline constexpr std::size_t kOffRendererPipelineSecond = 0x2718;
inline constexpr std::size_t kOffPipelineMotionBlur = 0xc9;            // byte: motion blur on
inline constexpr std::size_t kOffPipelineAaMode = 0xcc;                // int: 1 = FXAA, 2 = temporal AA
inline constexpr std::size_t kOffPipelineBloomDivisor = 0xe4;
inline constexpr std::uint32_t kRvaTaaSetup = 0x68fcb0;                // (shader, camera): program, sampler units, matrix, rand_offset
inline constexpr std::uint32_t kRvaTaaDraw = 0x68fde0;                 // (shader, quad, colour target, G-buffer, history target)
inline constexpr std::uint32_t kRvaTonemapSetup = 0x68e670;
inline constexpr std::uint32_t kRvaTonemapDraw = 0x68e8b0;
inline constexpr std::uint32_t kRvaFxaaDraw = 0x68f8c0;
inline constexpr std::uint32_t kRvaMotionBlurDraw = 0x6902d0;
inline constexpr std::uint32_t kRvaBindScreen = 0x6ddba0;              // binds framebuffer 0 and sets the viewport to the window size
inline constexpr std::uint32_t kRvaTargetAddColor = 0x1aee70;          // mm_resource_target_multiple::add_color_attachment(format)
inline constexpr std::size_t kOffRendererLight = 0x17b8;               // the `light` shader object (deferred composition)

inline constexpr std::uint32_t kRvaRendererCtor = 0x6d16c0;       // builds the shader objects (4297 bytes)
inline constexpr std::uint32_t kRvaFrameRender = 0x6d4de0;         // the frame: passes in code order (36279 bytes)
inline constexpr std::uint32_t kRvaFramebufferGroup = 0x1adee0;    // the render target helpers 0x1adee0..0x1af800
inline constexpr std::uint32_t kRvaFramebufferBlit1 = 0x1ae700;    // glBlitFramebuffer
inline constexpr std::uint32_t kRvaFramebufferBlit2 = 0x1aeef0;
inline constexpr std::uint32_t kRvaFramebufferBlit3 = 0x1af800;

// Offsets of the post-processing shader objects inside the renderer object.
inline constexpr std::size_t kOffRendererTonemap = 0x1488;
inline constexpr std::size_t kOffRendererSsao = 0x1598;
inline constexpr std::size_t kOffRendererSsaoBlur = 0x1658;
inline constexpr std::size_t kOffRendererFxaa = 0x16b8;
inline constexpr std::size_t kOffRendererTemporalAa = 0x16f0;
inline constexpr std::size_t kOffRendererMotionBlur = 0x1760;
inline constexpr std::size_t kOffRendererDownsampleDepth = 0x1e60;

}  // namespace sw::render
