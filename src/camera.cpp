#include "sw/camera.hpp"

#include "sw/hook.hpp"
#include "sw/runtime.hpp"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>

namespace sw::camera {

// ---------------------------------------------------------------------------
// Pose math (no game needed).

namespace {

Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double length(const Vec3& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
bool normalise(Vec3& a) {
    const double l = length(a);
    if (!(l > 1e-12)) return false;
    a = {a.x / l, a.y / l, a.z / l};
    return true;
}
Vec3 mix(const Vec3& a, const Vec3& b, double t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }

}  // namespace

Pose look_at(const Vec3& eye, const Vec3& target, float fov) {
    Pose p;
    p.eye = eye;
    p.forward = sub(target, eye);
    if (!normalise(p.forward)) p.forward = {0, 0, 1};
    p.up = {0, 1, 0};
    p.fov = fov;
    return p;
}

bool transform_from_pose(const Pose& pose, double out[16]) {
    Vec3 f = pose.forward, up = pose.up;
    if (!normalise(f)) return false;
    // det(right, up, forward) = +1, the handedness of the game's own camera (REPORT 3.156e):
    // right = up x forward, up = forward x right (forward x up gave det -1: every face inside out)
    Vec3 right = cross(up, f);
    if (!normalise(right)) {
        // looking straight up or down: take any up that is not parallel
        right = cross(std::fabs(f.y) < 0.9 ? Vec3{0, 1, 0} : Vec3{0, 0, 1}, f);
        if (!normalise(right)) return false;
    }
    const Vec3 u = cross(f, right);
    const double m[16] = {right.x, right.y, right.z, 0, u.x, u.y, u.z, 0, f.x, f.y, f.z, 0, pose.eye.x, pose.eye.y, pose.eye.z, 1};
    std::memcpy(out, m, sizeof(m));
    return true;
}

Pose pose_from_transform(const double m[16]) {
    Pose p;
    p.up = {m[4], m[5], m[6]};
    p.forward = {m[8], m[9], m[10]};
    p.eye = {m[12], m[13], m[14]};
    p.fov = 0.0f;
    return p;
}

Pose blend(const Pose& a, const Pose& b, double t) {
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    Pose p;
    p.eye = mix(a.eye, b.eye, t);
    p.forward = mix(a.forward, b.forward, t);
    if (!normalise(p.forward)) p.forward = t < 0.5 ? a.forward : b.forward;
    p.up = mix(a.up, b.up, t);
    if (!normalise(p.up)) p.up = {0, 1, 0};
    const float fa = a.fov, fb = b.fov;
    p.fov = (fa > 0 && fb > 0) ? static_cast<float>(fa + (fb - fa) * t) : (t < 0.5 ? fa : fb);
    return p;
}

#if defined(_WIN64)

// ---------------------------------------------------------------------------
// The view camera (REPORT 3.156).

namespace {

std::mutex g_view_mutex;
bool g_view_set = false;              // set_view() was called
Pose g_view_pose;
ViewProvider g_provider = nullptr;
void* g_provider_user = nullptr;
std::atomic<bool> g_view_active{false};   // the last frame was overridden
std::atomic<long long> g_last_frame{0};
bool g_view_watching = false;

// Only touched on the thread of the game (the hook callback), plus a copy for readers.
double g_game_transform[16];
float g_game_fov = 0.0f;
bool g_have_game = false;
double g_written[16];
bool g_have_written = false;
Pose g_game_pose_copy;
std::atomic<bool> g_game_pose_valid{false};
float g_written_fov = 0.0f;
std::atomic<std::uintptr_t> g_scene_cam{0};   // the camera of the last scene draw
// counters (view_stats); written on the game's thread, read anywhere
std::atomic<std::uint64_t> g_n_scene{0}, g_n_render{0}, g_n_render_scene{0}, g_n_written{0}, g_n_intact{0}, g_n_rewritten{0};
std::mutex g_eye_mutex;
Vec3 g_scene_eye, g_render_eye, g_render_eye7, g_render_eye8;
Vec3 g_origin;   // the render origin: 3 doubles at the renderer's 3rd argument (the frame state)
std::atomic<std::uint64_t> g_n_others{0};   // writes into the renderer's 7th / 8th camera
std::atomic<std::uint64_t> g_n_culling{0};  // frames the controller's cameras got the pose and their matrices
float g_aspect = 0.0f;                      // graphics state + 0x900, read at the scene draw
// the story camera's position for the tile visibility, which runs on the logic thread
std::mutex g_story_eye_mutex;
Vec3 g_story_eye;
std::atomic<std::uint64_t> g_n_tiles{0};
Vec3 g_tile_centre;
// ... and its whole transform and fov for the logic's view (under the same lock)
double g_story_view[16];
float g_story_fov = 0.0f;
bool g_have_story_view = false;
std::atomic<bool> g_culls{true};             // set_view_culls
std::atomic<std::uint64_t> g_n_frame_views{0};
Vec3 g_frame_eye;
// Logic thread only: the view the story camera was written into last, what was written, and the
// player's view that was there before (put back if the game did not write the view again).
std::uintptr_t g_lv_at = 0;
double g_lv_written[16];
double g_lv_saved[16];
float g_lv_saved_fov = 0.0f;
bool g_lv_live = false;
// ... and the viewer's pose in front of it (logic + 0xa70), the same way
unsigned char g_vp_saved[kViewerPoseBytes];
unsigned char g_vp_written[kViewerPoseBytes];
bool g_vp_live = false;
// what the last of those ticks saw: the forward of the player's view, of the viewer's pose, its position
Vec3 g_frame_player_fwd, g_frame_event_fwd, g_frame_event_eye, g_frame_story_fwd;
// Render thread: the player's look taken out of the controller for its update (REPORT 3.156k).
struct HeldLook {
    std::uintptr_t controller = 0;
    float yaw = 0, pitch = 0, roll = 0, seat_yaw = 0;
    float look[9] = {};
};
HeldLook g_held;
bool g_look_held = false;
std::atomic<std::uint64_t> g_n_look_held{0};

void put_look_back() {
    if (!g_look_held) return;
    g_look_held = false;
    const std::uintptr_t c = g_held.controller;
    if (!sw::Runtime::get().readable(reinterpret_cast<const void*>(c + kOffControllerYaw), kOffControllerLook + 36 - kOffControllerYaw)) return;
    *reinterpret_cast<float*>(c + kOffControllerYaw) = g_held.yaw;
    *reinterpret_cast<float*>(c + kOffControllerPitch) = g_held.pitch;
    *reinterpret_cast<float*>(c + kOffControllerRoll) = g_held.roll;
    *reinterpret_cast<float*>(c + kOffControllerSeatYaw) = g_held.seat_yaw;
    std::memcpy(reinterpret_cast<void*>(c + kOffControllerLook), g_held.look, sizeof(g_held.look));
}
// The handedness of the game's own camera: the sign of det(right, up, forward) - +1 in the game
// (logged "not mirrored"). The written transform always gets the game's sign: a transform of the other
// sign mirrors the view and turns every face inside out (REPORT 3.156d / e).
std::atomic<int> g_game_hand{0};   // -1 mirrored, +1 not, 0 not seen yet

int handedness(const double m[16]) {
    const double det = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) +
                       m[2] * (m[4] * m[9] - m[5] * m[8]);
    if (det < -0.5) return -1;
    if (det > 0.5) return 1;
    return 0;   // not a rotation (a camera that was never set)
}

bool code_matches(std::uint32_t rva, std::size_t at, const std::uint8_t* bytes, std::size_t n) {
    const auto* p = static_cast<const std::uint8_t*>(sw::Runtime::get().at_rva(rva));
    if (!p || !sw::Runtime::get().readable(p + at, n)) return false;
    return std::memcmp(p + at, bytes, n) == 0;
}

void on_scene_draw(void* self, void*) {
    put_look_back();   // 0x71be90 did not run since the last update (it always should)
    auto& rt = sw::Runtime::get();
    const auto scene = reinterpret_cast<std::uintptr_t>(self);
    if (!scene) return;
    const std::uintptr_t cam = scene + kOffSceneViewCamera;
    std::uintptr_t vtable = 0;
    if (!rt.readable(reinterpret_cast<const void*>(cam), sizeof(vtable))) return;
    std::memcpy(&vtable, reinterpret_cast<const void*>(cam), sizeof(vtable));
    if (vtable != reinterpret_cast<std::uintptr_t>(rt.at_rva(kVtableCameraPerspective))) return;
    auto* transform = reinterpret_cast<double*>(cam + kOffCameraTransform);
    auto* fov = reinterpret_cast<float*>(cam + kOffCameraFov);
    if (!rt.readable(transform, sizeof(double) * 16) || !rt.readable(fov, sizeof(float))) return;
    g_last_frame.store(static_cast<long long>(GetTickCount64()), std::memory_order_relaxed);
    g_n_scene.fetch_add(1, std::memory_order_relaxed);
    g_scene_cam.store(cam, std::memory_order_relaxed);
    if (const auto gfx = static_cast<std::uintptr_t>(sw::Hook::arg(1)); gfx && rt.readable(reinterpret_cast<const void*>(gfx + kOffGraphicsAspect), 4)) {
        float a = 0;
        std::memcpy(&a, reinterpret_cast<const void*>(gfx + kOffGraphicsAspect), 4);
        if (a > 0.2f && a < 6.0f) g_aspect = a;
    }
    {
        std::lock_guard<std::mutex> lock(g_eye_mutex);
        g_scene_eye = {transform[12], transform[13], transform[14]};
    }

    // The game wrote the camera since our last write (or never saw ours): that is its own pose.
    if (!g_have_written || std::memcmp(transform, g_written, sizeof(g_written)) != 0) {
        std::memcpy(g_game_transform, transform, sizeof(g_game_transform));
        g_game_fov = *fov;
        g_have_game = true;
        if (const int h = handedness(g_game_transform)) g_game_hand.store(h, std::memory_order_relaxed);
    }
    Pose game = pose_from_transform(g_game_transform);
    game.fov = g_game_fov;

    Pose want;
    bool override_now = false;
    {
        std::lock_guard<std::mutex> lock(g_view_mutex);
        g_game_pose_copy = game;
        g_game_pose_valid.store(true);
        if (g_provider) override_now = g_provider(game, want, g_provider_user);
        else if (g_view_set) { want = g_view_pose; override_now = true; }
    }

    double m[16];
    if (override_now && transform_from_pose(want, m)) {
        const int game_hand = g_game_hand.load(std::memory_order_relaxed);
        if (game_hand != 0 && handedness(m) != game_hand)
            for (int i = 0; i < 3; ++i) m[i] = -m[i];   // the handedness of the game's own camera
        std::memcpy(transform, m, sizeof(m));
        if (want.fov > 0.01f && want.fov < 3.0f) *fov = want.fov;
        else *fov = g_game_fov;
        std::memcpy(g_written, m, sizeof(m));
        g_written_fov = *fov;
        {
            std::lock_guard<std::mutex> lock(g_story_eye_mutex);
            g_story_eye = {m[12], m[13], m[14]};
            std::memcpy(g_story_view, m, sizeof(m));
            g_story_fov = *fov;
            g_have_story_view = true;
        }
        g_have_written = true;
        g_view_active.store(true);
        g_n_written.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // Released: if the game did not write the camera again, put its own pose back.
    if (g_have_written && g_have_game && std::memcmp(transform, g_written, sizeof(g_written)) == 0) {
        std::memcpy(transform, g_game_transform, sizeof(g_game_transform));
        *fov = g_game_fov;
    }
    g_have_written = false;
    g_view_active.store(false);
}

// The entry of 0x71be90, right after the camera controller's update in the scene draw: the three
// cameras hold the player's pose and matrices made from it. Take the game's pose (it is fresh here),
// then write the story pose into all three and let each camera compute its matrices again - with
// those the scene is culled and drawn from the story camera, not from where the player looks.
// The entry of the controller update 0x71a170 (render thread, after the mouse went into the angles):
// while a shot holds, the player's look is the identity for this update, so the camera it builds is the
// frame's view - the story camera - and not the story camera turned by where the player looks.
void on_controller_update(void* self, void*) {
    const auto controller = reinterpret_cast<std::uintptr_t>(self);
    const std::uintptr_t main_cam = g_scene_cam.load(std::memory_order_relaxed);
    if (!controller || !main_cam || controller + kOffControllerCamera[0] != main_cam) return;
    if (!g_view_active.load() || !g_culls.load()) return;
    if (!sw::Runtime::get().readable(reinterpret_cast<const void*>(controller + kOffControllerYaw), kOffControllerLook + 36 - kOffControllerYaw)) return;
    put_look_back();
    g_held.controller = controller;
    g_held.yaw = *reinterpret_cast<float*>(controller + kOffControllerYaw);
    g_held.pitch = *reinterpret_cast<float*>(controller + kOffControllerPitch);
    g_held.roll = *reinterpret_cast<float*>(controller + kOffControllerRoll);
    g_held.seat_yaw = *reinterpret_cast<float*>(controller + kOffControllerSeatYaw);
    std::memcpy(g_held.look, reinterpret_cast<const void*>(controller + kOffControllerLook), sizeof(g_held.look));
    g_look_held = true;
    *reinterpret_cast<float*>(controller + kOffControllerYaw) = 0.0f;
    *reinterpret_cast<float*>(controller + kOffControllerPitch) = 0.0f;
    *reinterpret_cast<float*>(controller + kOffControllerRoll) = 0.0f;
    *reinterpret_cast<float*>(controller + kOffControllerSeatYaw) = 0.0f;
    const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::memcpy(reinterpret_cast<void*>(controller + kOffControllerLook), identity, sizeof(identity));
    g_n_look_held.fetch_add(1, std::memory_order_relaxed);
}

void on_after_controller(void* self, void*) {
    put_look_back();   // before the camera event takes the angles: the logic keeps the player's
    const auto controller = reinterpret_cast<std::uintptr_t>(self);
    const std::uintptr_t main_cam = g_scene_cam.load(std::memory_order_relaxed);
    if (!controller || !main_cam || controller + kOffControllerCamera[0] != main_cam) return;
    if (!g_view_active.load() || !g_have_written || !(g_aspect > 0)) return;
    auto& rt = sw::Runtime::get();
    const auto vt = reinterpret_cast<std::uintptr_t>(rt.at_rva(kVtableCameraPerspective));
    const auto compute = reinterpret_cast<std::uintptr_t>(rt.at_rva(kRvaCameraPerspectiveCompute));
    for (int i = 0; i < 3; ++i) {
        const std::uintptr_t cam = controller + kOffControllerCamera[i];
        if (!rt.readable(reinterpret_cast<const void*>(cam), kOffCameraFov + sizeof(float))) continue;
        std::uintptr_t v = 0, slot0 = 0;
        std::memcpy(&v, reinterpret_cast<const void*>(cam), sizeof(v));
        if (v != vt || !rt.readable(reinterpret_cast<const void*>(v), sizeof(slot0))) continue;
        std::memcpy(&slot0, reinterpret_cast<const void*>(v), sizeof(slot0));
        if (slot0 != compute) continue;   // slot 0 must be the compute of 3.156
        auto* transform = reinterpret_cast<double*>(cam + kOffCameraTransform);
        auto* fov = reinterpret_cast<float*>(cam + kOffCameraFov);
        if (i == 0 && std::memcmp(transform, g_written, sizeof(g_written)) != 0) {
            // the player's pose of this frame (for the blend back when the shot is released)
            std::memcpy(g_game_transform, transform, sizeof(g_game_transform));
            g_game_fov = *fov;
            g_have_game = true;
        }
        std::memcpy(transform, g_written, sizeof(g_written));
        *fov = g_written_fov;
        reinterpret_cast<void (*)(void*, float)>(compute)(reinterpret_cast<void*>(cam), g_aspect);
    }
    g_n_culling.fetch_add(1, std::memory_order_relaxed);
}

// The entry of the tile visibility (logic thread): the centre is the player's view; while a view is
// set, the tiles around the story camera are the ones to show (the radius stays the game's).
void on_tile_visibility(void*, void*) {
    if (!g_view_active.load() || !g_culls.load()) return;
    const auto centre = static_cast<std::uintptr_t>(sw::Hook::arg(1));
    if (!centre || !sw::Runtime::get().readable(reinterpret_cast<const void*>(centre), 24)) return;
    auto* c = reinterpret_cast<double*>(centre);
    Vec3 eye;
    {
        std::lock_guard<std::mutex> lock(g_story_eye_mutex);
        eye = g_story_eye;
    }
    {
        std::lock_guard<std::mutex> lock(g_eye_mutex);
        g_tile_centre = {c[0], c[1], c[2]};
    }
    c[0] = eye.x;
    c[1] = eye.y;
    c[2] = eye.z;
    g_n_tiles.fetch_add(1, std::memory_order_relaxed);
}

bool on_this_stack(std::uintptr_t p) {
    ULONG_PTR low = 0, high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    return p >= low && p < high;
}

// The entry of the frame's draw lists (logic thread, once per tick): the 5th argument is the logic's
// view, which the frame is culled against and the game's camera follows. While a shot holds it gets the
// story camera. The game writes the view anew every tick before this call; should it not, the player's
// view saved here is put back first, so nothing of the story camera stays behind after a shot.
void on_frame_lists(void*, void*) {
    const auto view = static_cast<std::uintptr_t>(sw::Hook::arg(4));
    // the views of the other callers are locals (the sub view of the frame build, the editors)
    if (!view || on_this_stack(view) || !sw::Runtime::get().readable(reinterpret_cast<const void*>(view), kOffViewFov + 4)) return;
    auto* m = reinterpret_cast<double*>(view);
    auto* fov = reinterpret_cast<float*>(view + kOffViewFov);
    if (g_lv_live && g_lv_at == view && std::memcmp(m, g_lv_written, sizeof(g_lv_written)) == 0) {
        std::memcpy(m, g_lv_saved, sizeof(g_lv_saved));
        *fov = g_lv_saved_fov;
    }
    // the viewer's pose in front of the view, the same way (the camera event normally rewrites it first)
    auto* viewer = reinterpret_cast<unsigned char*>(view - kOffViewerPose);
    if (!sw::Runtime::get().readable(viewer, kViewerPoseBytes)) {
        g_lv_live = false;
        g_vp_live = false;
        return;
    }
    if (g_vp_live && g_lv_at == view && std::memcmp(viewer, g_vp_written, kViewerPoseBytes) == 0)
        std::memcpy(viewer, g_vp_saved, kViewerPoseBytes);
    g_lv_live = false;
    g_vp_live = false;
    if (!g_view_active.load() || !g_culls.load() || handedness(m) == 0) return;
    double story[16];
    float story_fov = 0.0f;
    {
        std::lock_guard<std::mutex> lock(g_story_eye_mutex);
        if (!g_have_story_view) return;
        std::memcpy(story, g_story_view, sizeof(story));
        story_fov = g_story_fov;
    }
    {
        std::lock_guard<std::mutex> lock(g_eye_mutex);
        g_frame_eye = {m[12], m[13], m[14]};
    }
    std::memcpy(g_lv_saved, m, sizeof(g_lv_saved));
    g_lv_saved_fov = *fov;
    std::memcpy(m, story, sizeof(story));
    if (story_fov > 0.01f && story_fov < 3.0f) *fov = story_fov;
    std::memcpy(g_lv_written, story, sizeof(story));
    g_lv_at = view;
    g_lv_live = true;
    g_n_frame_views.fetch_add(1, std::memory_order_relaxed);

    // The 3rd and 4th arguments: the viewer's pose the camera event brought (logic + 0xa70: double
    // position, then the float 3x3 at + 0xa88, columns right / up / forward). The event is built from
    // the camera the controller hook wrote, so this should be the story camera already; written here
    // as well, and logged, so a pose of the player cannot hide in it.
    const auto rot_at = static_cast<std::uintptr_t>(sw::Hook::arg(2));
    const auto pos_at = static_cast<std::uintptr_t>(sw::Hook::arg(3));
    if (pos_at + kOffViewerPose != view || rot_at != pos_at + 24 ||
        !sw::Runtime::get().readable(reinterpret_cast<const void*>(pos_at), kViewerPoseBytes)) return;
    auto* block = reinterpret_cast<unsigned char*>(pos_at);
    double pos[3];
    float rot[9];
    std::memcpy(pos, block, sizeof(pos));
    std::memcpy(rot, block + 24, sizeof(rot));
    {
        std::lock_guard<std::mutex> lock(g_eye_mutex);
        g_frame_player_fwd = {g_lv_saved[8], g_lv_saved[9], g_lv_saved[10]};
        g_frame_event_fwd = {rot[6], rot[7], rot[8]};
        g_frame_event_eye = {pos[0], pos[1], pos[2]};
        g_frame_story_fwd = {story[8], story[9], story[10]};
    }
    std::memcpy(g_vp_saved, block, kViewerPoseBytes);
    for (int i = 0; i < 3; ++i) pos[i] = story[12 + i];
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r) rot[c * 3 + r] = static_cast<float>(story[c * 4 + r]);
    std::memcpy(block, pos, sizeof(pos));
    std::memcpy(block + 24, rot, sizeof(rot));
    std::memcpy(g_vp_written, block, kViewerPoseBytes);
    g_vp_live = true;
}

// The entry of renderer_draw_frame: its 6th argument is the camera it copies (cam + 8, cam + 0x278);
// the 5th is the pipeline (renderer + 0x2628). Hook::arg(5) = [rsp + 0x30] at the entry.
// If that is the scene's camera and a pose is set, the pose must still be there; if something wrote the
// camera between the scene draw and here, write it again (the copy follows within the next instructions).
void on_render(void*, void*) {
    g_n_render.fetch_add(1, std::memory_order_relaxed);
    const auto cam = static_cast<std::uintptr_t>(sw::Hook::arg(5));
    if (!cam || cam != g_scene_cam.load(std::memory_order_relaxed)) return;
    g_n_render_scene.fetch_add(1, std::memory_order_relaxed);
    auto& rt = sw::Runtime::get();
    // the frame state the scene draw got from the frame hand-off (sim + 0x5c0); the renderer adds
    // the double vector at its start to the camera position (3.156)
    if (const auto frame = static_cast<std::uintptr_t>(sw::Hook::arg(2)); frame && rt.readable(reinterpret_cast<const void*>(frame), 24)) {
        double o[3];
        std::memcpy(o, reinterpret_cast<const void*>(frame), sizeof(o));
        std::lock_guard<std::mutex> lock(g_eye_mutex);
        g_origin = {o[0], o[1], o[2]};
    }
    auto* transform = reinterpret_cast<double*>(cam + kOffCameraTransform);
    auto* fov = reinterpret_cast<float*>(cam + kOffCameraFov);
    if (!rt.readable(transform, sizeof(double) * 16) || !rt.readable(fov, sizeof(float))) return;
    {
        std::lock_guard<std::mutex> lock(g_eye_mutex);
        g_render_eye = {transform[12], transform[13], transform[14]};
    }
    // the 7th and 8th arguments are two more perspective cameras (scene + 0x1510, + 0x1290 in the scene
    // draw); the renderer builds a local camera from each. They hold the game's own pose.
    const std::uintptr_t others[2] = {static_cast<std::uintptr_t>(sw::Hook::arg(6)), static_cast<std::uintptr_t>(sw::Hook::arg(7))};
    const auto vt = reinterpret_cast<std::uintptr_t>(rt.at_rva(kVtableCameraPerspective));
    double* other_t[2] = {nullptr, nullptr};
    float* other_f[2] = {nullptr, nullptr};
    for (int i = 0; i < 2; ++i) {
        const std::uintptr_t c = others[i];
        if (!c || c == cam || !rt.readable(reinterpret_cast<const void*>(c), kOffCameraFov + sizeof(float))) continue;
        std::uintptr_t v = 0;
        std::memcpy(&v, reinterpret_cast<const void*>(c), sizeof(v));
        if (v != vt) continue;
        other_t[i] = reinterpret_cast<double*>(c + kOffCameraTransform);
        other_f[i] = reinterpret_cast<float*>(c + kOffCameraFov);
        std::lock_guard<std::mutex> lock(g_eye_mutex);
        (i == 0 ? g_render_eye7 : g_render_eye8) = {other_t[i][12], other_t[i][13], other_t[i][14]};
    }
    if (!g_view_active.load() || !g_have_written) return;
    if (std::memcmp(transform, g_written, sizeof(g_written)) == 0) {
        g_n_intact.fetch_add(1, std::memory_order_relaxed);
    } else {
        std::memcpy(transform, g_written, sizeof(g_written));
        *fov = g_written_fov;
        g_n_rewritten.fetch_add(1, std::memory_order_relaxed);
    }
    // a deferred renderer lights with camera matrices: a pass that uses one of the other two cameras
    // would light the picture from the player's head (it looks like faces turned inside out)
    for (int i = 0; i < 2; ++i) {
        if (!other_t[i]) continue;
        std::memcpy(other_t[i], g_written, sizeof(g_written));
        *other_f[i] = g_written_fov;
        g_n_others.fetch_add(1, std::memory_order_relaxed);
    }
}

}  // namespace

ViewStats view_stats() {
    ViewStats v;
    v.scene_draws = g_n_scene.load();
    v.renders = g_n_render.load();
    v.renders_scene = g_n_render_scene.load();
    v.written = g_n_written.load();
    v.intact = g_n_intact.load();
    v.rewritten = g_n_rewritten.load();
    v.mirrored = g_game_hand.load() < 0;
    v.others_written = g_n_others.load();
    v.culling = g_n_culling.load();
    v.tiles = g_n_tiles.load();
    v.frame_views = g_n_frame_views.load();
    std::lock_guard<std::mutex> lock(g_eye_mutex);
    v.frame_eye = g_frame_eye;
    v.frame_player_fwd = g_frame_player_fwd;
    v.frame_event_fwd = g_frame_event_fwd;
    v.frame_event_eye = g_frame_event_eye;
    v.frame_story_fwd = g_frame_story_fwd;
    v.look_held = g_n_look_held.load();
    v.scene_eye = g_scene_eye;
    v.render_eye = g_render_eye;
    v.render_eye7 = g_render_eye7;
    v.origin = g_origin;
    v.tile_centre = g_tile_centre;
    v.render_eye8 = g_render_eye8;
    return v;
}

bool watch_view() {
    if (g_view_watching) return true;
    if (!code_matches(kRvaSceneDraw, 0, kSceneDrawPrologue, sizeof(kSceneDrawPrologue)) ||
        !code_matches(kRvaSceneDraw, kSceneDrawCameraArgAt, kSceneDrawCameraArg, sizeof(kSceneDrawCameraArg)) ||
        !code_matches(kRvaRendererDrawFrame, kFrameCopyTransformAt, kFrameCopyTransform, sizeof(kFrameCopyTransform)) ||
        !code_matches(kRvaRendererDrawFrame, kFrameCopyFovAt, kFrameCopyFov, sizeof(kFrameCopyFov)))
        return false;
    std::string error;
    if (sw::Runtime::get().hook_rva(kRvaSceneDraw, &on_scene_draw, nullptr, error) != sw::HookStatus::ok) return false;
    // the second place is a help, not a condition: without it the scene draw alone writes the pose
    sw::Runtime::get().hook_rva(kRvaRendererDrawFrame, &on_render, nullptr, error);
    // the third: after the camera controller, so the culling uses the story camera (REPORT 3.156f)
    bool chain = code_matches(kRvaSceneDraw, kSceneDrawAfterControllerAt, kSceneDrawAfterController, sizeof(kSceneDrawAfterController)) &&
                 code_matches(kRvaControllerCameraFinish, kCameraFinishAspectAt, kCameraFinishAspect, sizeof(kCameraFinishAspect));
    for (int i = 0; i < 3 && chain; ++i)
        chain = code_matches(kRvaCameraControllerUpdate, kControllerCameraAt[i], kControllerCamera[i], sizeof(kControllerCamera[i]));
    if (chain) {
        // the call after the lea must land on 0x71be90
        const auto* call = static_cast<const std::uint8_t*>(sw::Runtime::get().at_rva(kRvaSceneDraw)) + kSceneDrawAfterControllerAt + 7;
        std::int32_t rel = 0;
        std::memcpy(&rel, call + 1, 4);
        chain = kRvaSceneDraw + kSceneDrawAfterControllerAt + 7 + 5 + rel == kRvaAfterControllerUpdate;
    }
    if (chain) sw::Runtime::get().hook_rva(kRvaAfterControllerUpdate, &on_after_controller, nullptr, error);
    // the player's look held out of the controller's update (REPORT 3.156k); needs the hook above to put it back
    bool look = chain;
    for (int i = 0; i < 5 && look; ++i)
        look = code_matches(kRvaCameraControllerUpdate, kControllerLookReadAt[i], kControllerLookRead[i], kControllerLookReadLen[i]);
    if (look) sw::Runtime::get().hook_rva(kRvaCameraControllerUpdate, &on_controller_update, nullptr, error);
    // the fourth: the tiles around the story camera (REPORT 3.156g)
    if (code_matches(kRvaTileVisibility, 0, kTileVisibilityPrologue, sizeof(kTileVisibilityPrologue)) &&
        code_matches(kRvaTileVisibility, kTileVisibilityCentreXAt, kTileVisibilityCentreX, sizeof(kTileVisibilityCentreX)) &&
        code_matches(kRvaTileVisibility, kTileVisibilityCentreZAt, kTileVisibilityCentreZ, sizeof(kTileVisibilityCentreZ)))
        sw::Runtime::get().hook_rva(kRvaTileVisibility, &on_tile_visibility, nullptr, error);
    // the fifth: the frame's draw lists culled from the story camera (REPORT 3.156h)
    if (code_matches(kRvaFrameLists, 0, kFrameListsPrologue, sizeof(kFrameListsPrologue)) &&
        code_matches(kRvaFrameLists, kFrameListsViewArgAt, kFrameListsViewArg, sizeof(kFrameListsViewArg)) &&
        code_matches(kRvaFrameLists, kFrameListsCopyAt, kFrameListsCopy, sizeof(kFrameListsCopy)))
        sw::Runtime::get().hook_rva(kRvaFrameLists, &on_frame_lists, nullptr, error);
    g_view_watching = true;
    return true;
}

bool view_ready() {
    return g_view_watching && static_cast<long long>(GetTickCount64()) - g_last_frame.load() < 500;
}

bool view_game_pose(Pose& out) {
    if (!g_game_pose_valid.load()) return false;
    std::lock_guard<std::mutex> lock(g_view_mutex);
    out = g_game_pose_copy;
    return true;
}

void set_view(const Pose& pose) {
    std::lock_guard<std::mutex> lock(g_view_mutex);
    g_view_pose = pose;
    g_view_set = true;
    g_provider = nullptr;
    g_provider_user = nullptr;
}

void set_view_provider(ViewProvider provider, void* user) {
    std::lock_guard<std::mutex> lock(g_view_mutex);
    g_provider = provider;
    g_provider_user = user;
    g_view_set = false;
}

void release_view() {
    std::lock_guard<std::mutex> lock(g_view_mutex);
    g_view_set = false;
    g_provider = nullptr;
    g_provider_user = nullptr;
}

bool view_overridden() { return g_view_active.load(); }

void set_view_culls(bool on) { g_culls.store(on); }

#else

bool watch_view() { return false; }
bool view_ready() { return false; }
bool view_game_pose(Pose&) { return false; }
void set_view(const Pose&) {}
void set_view_provider(ViewProvider, void*) {}
void release_view() {}
bool view_overridden() { return false; }
void set_view_culls(bool) {}
ViewStats view_stats() { return {}; }

#endif

#if defined(_WIN64)

namespace {

std::atomic<std::uintptr_t> g_shelf{0};
std::atomic<long long> g_last_tick{0};
std::atomic<int> g_pending{-1};   // -1 none, else a Mode
bool g_watching = false;

template <typename T>
bool read_at(std::uintptr_t address, T& value) {
    if (!address || !sw::Runtime::get().readable(reinterpret_cast<const void*>(address), sizeof(T))) return false;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return true;
}

long long now_ms() { return static_cast<long long>(GetTickCount64()); }

bool is_shelf(std::uintptr_t shelf) {
    std::uintptr_t vtable = 0;
    return shelf && read_at(shelf, vtable) &&
           vtable == reinterpret_cast<std::uintptr_t>(sw::Runtime::get().at_rva(kVtableCameraToolshelf));
}

// The member object of a toggle (button + 0x100) of the shelf.
std::uintptr_t member(std::uintptr_t shelf, std::size_t off) {
    std::uintptr_t button = 0;
    if (!read_at(shelf + off, button) || !button) return 0;
    return button + kOffToggleMember;
}

bool is_on(std::uintptr_t member_object) {
    std::uintptr_t vtable = 0, fn = 0;
    if (!read_at(member_object, vtable) || !read_at(vtable + kSlotToggleIsOn * 8, fn) || !fn) return false;
    return reinterpret_cast<bool (*)(void*)>(fn)(reinterpret_cast<void*>(member_object));
}

// On the thread of the game, at the tick of the shelf.
void on_tick(void* self, void*) {
    const auto shelf = reinterpret_cast<std::uintptr_t>(self);
    if (!is_shelf(shelf)) return;
    g_shelf.store(shelf, std::memory_order_relaxed);
    g_last_tick.store(now_ms(), std::memory_order_relaxed);
    const int pending = g_pending.exchange(-1);
    if (pending < 0) return;
    const std::uintptr_t target = member(shelf, pending == static_cast<int>(Mode::orbit) ? kOffToolshelfOrbitButton
                                                                                          : kOffToolshelfFreeButton);
    if (!target) return;
    const auto* fn = sw::Runtime::get().at_rva(kRvaToggleGroupSelect);
    if (!fn || !sw::Runtime::get().is_function_start(reinterpret_cast<std::uintptr_t>(fn))) return;
    reinterpret_cast<void (*)(void*, void*, void*)>(const_cast<void*>(fn))(
        reinterpret_cast<void*>(shelf + kOffToolshelfGroup), nullptr, reinterpret_cast<void*>(target));
}

}  // namespace

bool watch() {
    if (g_watching) return true;
    std::string error;
    if (sw::Runtime::get().hook_rva(kRvaCameraToolshelfTick, &on_tick, nullptr, error) != sw::HookStatus::ok) return false;
    g_watching = true;
    return true;
}

bool shelf_ready() { return g_shelf.load() != 0 && now_ms() - g_last_tick.load() < 500; }

bool set_mode(Mode m) {
    if (!g_watching) return false;
    g_pending.store(static_cast<int>(m));
    return true;
}

bool mode(Mode& out) {
    if (!shelf_ready()) return false;
    const std::uintptr_t shelf = g_shelf.load();
    if (!is_shelf(shelf)) return false;
    const std::uintptr_t orbit = member(shelf, kOffToolshelfOrbitButton);
    if (!orbit) return false;
    out = is_on(orbit) ? Mode::orbit : Mode::free;
    return true;
}

#else

bool watch() { return false; }
bool shelf_ready() { return false; }
bool set_mode(Mode) { return false; }
bool mode(Mode&) { return false; }

#endif

}  // namespace sw::camera
