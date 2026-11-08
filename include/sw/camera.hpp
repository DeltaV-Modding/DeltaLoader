// camera.hpp - the cameras of the game, the orbit mode of the vehicle editor and
// the view camera of a world (the camera the player looks through).
//
// The editor part (REPORT 3.99): the classes, the texts of the camera buttons and
// where the editor builds them; the callback that switches orbit and free mode was
// not found.
//
// The view part (REPORT 3.156) was read in the bytes of 1.15.25 (build 26275574) and
// carried to 1.15.26 by the strict fingerprints of anchors.tsv (same code, +0x30):
//   * mm_camera_perspective (RTTI vtable 0xb02940) keeps its world transform as a
//     double 4x4 at +0x08 (columns right, up, forward, position) and the vertical
//     field of view in radians as a float at +0x278. Its slot 0 (0x194680) builds
//     the view matrix from exactly these fields: forward = column 2, and the look-at
//     helper 0x1783e0 negates it into the third row (the camera looks along +col 2).
//   * The scene draw 0x7b01a0 (rcx = scene; `btIDebugDraw::ctor` in symbols.tsv, a
//     wrong vtable label) passes scene + 0x1010 as the first camera to
//     renderer_draw_frame (0x6d4de0, 6th argument; the 5th is the pipeline) and to the culling call before it;
//     the frame function copies cam + 8 and cam + 0x278 into its own camera.
// So a pose written into scene + 0x1010 at the entry of the scene draw is the pose
// the frame is culled and drawn with. Not proven: who writes the camera between two
// frames (set_view() keeps the game's own last pose and puts it back on release), and
// nothing of this was run in the game.
//
//   #include "sw/camera.hpp"
//
//   sw::camera::watch_view();
//   sw::camera::set_view(sw::camera::look_at({0, 20, 0}, {50, 5, 50}, 1.0f));
//   sw::camera::release_view();
#pragma once

#include <cstddef>
#include <cstdint>

namespace sw::camera {

// The mode of the camera of the vehicle editor.
enum class Mode { orbit, free };

// Hooks the tick of the camera shelf (0x617270) so that the shelf is known and a
// requested mode can be applied on the thread of the game. False if the game does
// not match or the hook fails.
bool watch();

// The shelf of the editor exists (it was ticked in the last moments).
bool shelf_ready();

// Selects the mode, as if the player clicked its button: the request is applied at
// the next tick of the shelf by the game's own group function (0x24d2c0), which
// switches the other toggle off. False before watch(). The shelf only exists while
// the vehicle editor is open.
bool set_mode(Mode mode);

// The mode that is on now (the state of the "Orbit Mode" toggle). False if the
// shelf is not there.
bool mode(Mode& out);

// Classes of the cameras of the scene (RTTI, stormworks64.exe 1.15.25).
inline constexpr std::uint32_t kVtableCamera = 0xb0c2f0;               // mm_camera
inline constexpr std::uint32_t kVtableCameraPerspective = 0xb02940;    // mm_camera_perspective
inline constexpr std::uint32_t kVtableCameraOrthographic = 0xb942e0;   // mm_camera_orthographic
// mm_camera_orthographic_inside, c_scene_vehicle_component_camera (the camera
// component of a vehicle) exist as classes; their vtables were not read.

// The vehicle editor: a tool shelf with the camera settings. "Orbit Mode" turns
// the camera around the vehicle, "Free Mode" moves it freely, "Reset Camera"
// returns it (REPORT 3.99).
inline constexpr std::uint32_t kVtableCameraToolshelf = 0xb492b0;      // camera_toolshelf
inline constexpr std::uint32_t kRvaCameraToolshelfBuild = 0x617470;    // slot 1, 955 bytes
inline constexpr std::uint32_t kRvaCameraToolshelfTick = 0x617270;     // slot 3, 178 bytes
inline constexpr std::uint32_t kRvaToggleGroupSelect = 0x24d2c0;       // (group, unused, member)
inline constexpr std::size_t kOffToolshelfGroup = 0x78;
inline constexpr std::size_t kOffToggleMember = 0x100;                 // member object inside a toggle button
inline constexpr std::size_t kSlotToggleIsOn = 1;                      // vtable slot of the member
inline constexpr std::uint32_t kLocResetCamera = 0x4fe;
inline constexpr std::uint32_t kLocOrbitMode = 0x4ff;
inline constexpr std::uint32_t kLocFreeMode = 0x500;
// The two toggles are in a group (0x24d2c0, REPORT 3.100): for every member except the
// one that was clicked, if its slot 1 says it is on, its slot 0 is called (it is
// switched off); then the clicked one is set to the opposite of its own state (slot
// 1 returns it, slot 0 sets it). So exactly one of "Orbit Mode" and "Free Mode" is
// on. The slide-in of the shelf is slot 3 (0x617270: it eases a position towards
// 0 or the width). Which code reads the toggle to switch the camera was not found.
// set_mode() calls the group function with the toggle's inner object (button + 0x100,
// as 0x617470 does at its end): (group = shelf + 0x78, unused, member).
// The shelf object: the toggle "Orbit Mode" at +0xa0 (index 0x14), "Free Mode" at
// +0xa8 (index 0x15), the group of the two at +0x78 (index 0xf).
inline constexpr std::size_t kOffToolshelfOrbitButton = 0xa0;
inline constexpr std::size_t kOffToolshelfFreeButton = 0xa8;

// Events between the game and the logic thread that concern a camera (RTTI names,
// the vtables were not read).
inline constexpr const char* kCameraEvents[] = {
    "c_event_game_to_logic_set_camera_controller_status",
    "c_event_logic_to_game_set_camera_orientation",
    "c_event_logic_to_game_add_camera_shake",
    "c_event_game_to_logic_toggle_cinematic_camera",
    "c_event_game_to_logic_set_map_camera_data",
    "c_event_logic_to_game_set_map_camera_transforms",
    "c_event_game_to_logic_set_photo_mode_camera_parameters",
    "c_event_logic_to_game_set_camera_to_photo_mode_ui",
    "c_server_to_client_event_set_camera_position",
};

// ---------------------------------------------------------------------------
// The view camera of a world (REPORT 3.156).

struct Vec3 {
    double x = 0, y = 0, z = 0;
};

// A pose of the view camera: where it is, where it looks, its up vector and its
// vertical field of view in radians (0 = keep the field of view of the game).
struct Pose {
    Vec3 eye;
    Vec3 forward{0, 0, 1};
    Vec3 up{0, 1, 0};
    float fov = 0.0f;
};

// The camera at eye that looks at target (world up = +y).
Pose look_at(const Vec3& eye, const Vec3& target, float fov = 0.0f);

// The double 4x4 that mm_camera_perspective keeps at +8, built from a pose:
// columns right = normalize(up x forward), up = forward x right, forward, eye: det +1, the
// handedness of the game's own view camera (REPORT 3.156e). The view hook also compares the sign
// with the game's transform and negates column 0 if they differ, so the faces keep their winding.
// False if forward is zero or parallel to up.
bool transform_from_pose(const Pose& pose, double out[16]);

// The other way round (fov is left 0).
Pose pose_from_transform(const double m[16]);

// Blend of two poses: eye and fov linear, forward and up normalised linear.
// t outside 0..1 is clamped.
Pose blend(const Pose& a, const Pose& b, double t);

// Hooks the scene draw (0x7b01a0) after checking its bytes. From then on the
// camera at scene + 0x1010 is read every frame (view_game_pose) and, while a view is
// set, overwritten before the frame is culled and drawn. False if the game does not
// match or the hook fails.
bool watch_view();

// A frame was drawn in the last half second (a world is open).
bool view_ready();

// The pose the game itself gave the camera last (fov included). False before the
// first frame.
bool view_game_pose(Pose& out);

// From the next frame on the camera shows this pose (until release_view). A pose
// with fov 0 keeps the game's field of view.
void set_view(const Pose& pose);

// A function that gives the pose for every frame, on the thread of the game, at the
// entry of the scene draw: (pose of the game, out) -> true to override this frame.
// Keep it short. Replaces set_view; nullptr removes it.
using ViewProvider = bool (*)(const Pose& game, Pose& out, void* user);
void set_view_provider(ViewProvider provider, void* user);

// Gives the camera back to the game: the next frame shows the game's own pose.
void release_view();

// A view is set (set_view or a provider that returned true last frame).
bool view_overridden();

// Whether the frame's draw lists are culled from the story camera while a view is set (on by default).
// The game's camera follows that same view, so a blend back to the player turns it off: otherwise the
// pose of the game would be the story camera's and the blend would have nowhere to go.
void set_view_culls(bool on);

// What the view hooks saw (for a log when a shot does not show). watch_view() also hooks the entry of
// renderer_draw_frame: when its 6th argument is the camera of the scene draw, the pose written at the
// scene draw is checked there and written again if something changed it in between.
struct ViewStats {
    std::uint64_t scene_draws = 0;    // calls of the scene draw with a perspective camera at +0x1010
    std::uint64_t renders = 0;        // calls of renderer_draw_frame
    std::uint64_t renders_scene = 0;  // ... whose 6th argument was that camera
    std::uint64_t written = 0;        // frames the pose was written at the scene draw
    std::uint64_t intact = 0;         // ... and still there at the renderer
    std::uint64_t rewritten = 0;      // ... changed in between and written again at the renderer
    bool mirrored = false;            // the game's camera is mirrored (det < 0); poses are written that way
    std::uint64_t others_written = 0; // writes into the renderer's 7th and 8th camera (scene + 0x1510, + 0x1290)
    std::uint64_t culling = 0;        // frames the three cameras got the pose and their matrices right after the controller
    std::uint64_t tiles = 0;          // calls of the tile visibility whose centre was moved to the story camera
    Vec3 tile_centre;                 // the centre the game gave the last of them (the player's view)
    std::uint64_t frame_views = 0;    // logic ticks whose draw lists were culled from the story camera
    Vec3 frame_eye;                   // the player's position in the logic's view at the last of them
    Vec3 frame_player_fwd;            // ... the forward of that view (before writing)
    Vec3 frame_event_fwd;             // ... the forward of the viewer's pose the camera event brought (logic + 0xa88)
    Vec3 frame_event_eye;             // ... and its position (logic + 0xa70)
    Vec3 frame_story_fwd;             // ... and the story camera's forward written there
    std::uint64_t look_held = 0;      // controller updates that ran with the player's look set to the identity
    Vec3 scene_eye;                   // where the camera was at the last scene draw (before writing)
    Vec3 render_eye;                  // where the 6th argument's camera was at the last renderer entry
    Vec3 render_eye7, render_eye8;    // ... and the 7th and 8th argument's cameras (before writing)
    Vec3 origin;                      // the render origin at the last renderer entry (3 doubles at its 3rd argument)
};
ViewStats view_stats();

inline constexpr std::uint32_t kRvaSceneDraw = 0x7b01a0;              // (scene, ...) per frame, calls renderer_draw_frame
inline constexpr std::uint32_t kRvaCameraPerspectiveCompute = 0x194680; // slot 0 of mm_camera_perspective (1333 bytes)
inline constexpr std::uint32_t kRvaRendererDrawFrame = 0x6d4de0;        // the camera is its 6th argument
inline constexpr std::size_t kOffSceneViewCamera = 0x1010;              // the view camera inside the scene (an offset, swupdate:keep)
inline constexpr std::size_t kOffCameraTransform = 0x08;                // double[16], column major
inline constexpr std::size_t kOffCameraFov = 0x278;                     // float, radians, vertical
// The bytes watch_view() checks before it hooks (and the API test checks in the file).
// The first 15 bytes of the scene draw: mov rax,rsp; push rbp/rbx/rsi/rdi/r12..r15.
inline constexpr std::uint8_t kSceneDrawPrologue[15] = {0x48, 0x8b, 0xc4, 0x55, 0x53, 0x56, 0x57, 0x41,
                                                        0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
// lea rdx,[r14+0x1010] (stored to [rsp+0x28]): the view camera as the 6th argument of renderer_draw_frame.
inline constexpr std::size_t kSceneDrawCameraArgAt = 0x7d4;            // from the start of the scene draw
inline constexpr std::uint8_t kSceneDrawCameraArg[7] = {0x49, 0x8d, 0x96, 0x10, 0x10, 0x00, 0x00};
// lea rdx,[rdi+8] (copy of the transform) and movss xmm0,[rdi+0x278] (the fov) in renderer_draw_frame.
inline constexpr std::size_t kFrameCopyTransformAt = 0x104;            // from the start of renderer_draw_frame
inline constexpr std::uint8_t kFrameCopyTransform[4] = {0x48, 0x8d, 0x57, 0x08};
inline constexpr std::size_t kFrameCopyFovAt = 0x17f;
// The camera controller (REPORT 3.156f): an object at scene + 0xce8 owns the three cameras
// (+0x328 = scene + 0x1010, +0x828 = scene + 0x1510, +0x5a8 = scene + 0x1290). Its update 0x71a170
// (called by the scene draw) writes each transform from the player and calls 0x719f50, which adds the
// head offsets, sets the fov from controller + 0x1c4 and tail-calls the camera's slot 0 (compute:
// view, projection and the matrices the culling reads) with the aspect at graphics state + 0x900.
// The scene draw calls 0x71be90 on the controller next: hooked there, the pose and its matrices are in
// place before anything is culled or drawn.
inline constexpr std::uint32_t kRvaCameraControllerUpdate = 0x71a170;
inline constexpr std::uint32_t kRvaAfterControllerUpdate = 0x71be90;
inline constexpr std::uint32_t kRvaControllerCameraFinish = 0x719f50;
inline constexpr std::size_t kOffSceneController = 0xce8;
inline constexpr std::size_t kOffControllerCamera[3] = {0x328, 0x828, 0x5a8};
inline constexpr std::size_t kOffGraphicsAspect = 0x900;
// lea rcx,[r14+0xce8]; call 0x71be90 in the scene draw
inline constexpr std::size_t kSceneDrawAfterControllerAt = 0x2a9;
inline constexpr std::uint8_t kSceneDrawAfterController[8] = {0x49, 0x8d, 0x8e, 0xe8, 0x0c, 0x00, 0x00, 0xe8};
// The player's look in the controller (REPORT 3.156k): 0x71b630 (called right before the update) adds the
// mouse to the yaw at +0x2a0 and the pitch at +0x2a4 and builds the 3x3 at +0x2dc; the update turns them
// into a rotation (yaw x pitch x roll +0x2c0, or the seat yaw +0x2c4 when +0x1a6 is set, then x the 3x3)
// and puts it behind the frame's view: camera = view x look. Something culls from that camera before
// 0x71be90, so while a shot holds the look is the identity during the update and put back at 0x71be90.
inline constexpr std::size_t kOffControllerYaw = 0x2a0;
inline constexpr std::size_t kOffControllerPitch = 0x2a4;
inline constexpr std::size_t kOffControllerRoll = 0x2c0;
inline constexpr std::size_t kOffControllerSeatYaw = 0x2c4;
inline constexpr std::size_t kOffControllerLook = 0x2dc;        // float 3x3
// movss xmm6,[rdi+X] in the update (yaw, pitch, roll, seat yaw) and lea r8,[rdi+0x2dc]
inline constexpr std::size_t kControllerLookReadAt[5] = {0x1c6, 0x1ed, 0x250, 0x103, 0x317};
inline constexpr std::uint8_t kControllerLookRead[5][8] = {{0xf3, 0x0f, 0x10, 0xb7, 0xa0, 0x02, 0x00, 0x00},
                                                           {0xf3, 0x0f, 0x10, 0xb7, 0xa4, 0x02, 0x00, 0x00},
                                                           {0xf3, 0x0f, 0x10, 0xb7, 0xc0, 0x02, 0x00, 0x00},
                                                           {0xf3, 0x0f, 0x10, 0xb7, 0xc4, 0x02, 0x00, 0x00},
                                                           {0x4c, 0x8d, 0x87, 0xdc, 0x02, 0x00, 0x00, 0x00}};
inline constexpr std::size_t kControllerLookReadLen[5] = {8, 8, 8, 8, 7};
// lea r8,[rdi+0x328 / 0x828 / 0x5a8] before each call of 0x719f50 in the controller update
inline constexpr std::size_t kControllerCameraAt[3] = {0xb37, 0xba7, 0xc17};
inline constexpr std::uint8_t kControllerCamera[3][7] = {{0x4c, 0x8d, 0x87, 0x28, 0x03, 0x00, 0x00},
                                                         {0x4c, 0x8d, 0x87, 0x28, 0x08, 0x00, 0x00},
                                                         {0x4c, 0x8d, 0x87, 0xa8, 0x05, 0x00, 0x00}};
// movss xmm1,[rdx+0x900] (the aspect) in 0x719f50
inline constexpr std::size_t kCameraFinishAspectAt = 0x1f4;
inline constexpr std::uint8_t kCameraFinishAspect[8] = {0xf3, 0x0f, 0x10, 0x8a, 0x00, 0x09, 0x00, 0x00};
// The tile visibility (REPORT 3.156g): 0x579060 (tile level, const double centre[3], float radius, bool)
// marks every cell within the radius of the centre visible (cell + 0x98) and the rest not; the logic
// update calls it for both tile levels with the centre taken from the player's view (0x366650,
// stored at logic + 0xac0). Hooked at its entry, the centre is moved to the story camera while a view is set.
inline constexpr std::uint32_t kRvaTileVisibility = 0x579060;
// mov rax,rsp; mov [rax+0x20],r9b ... movups xmm0,[rdx] (x, y) ... movsd xmm9,[rdx+0x10] (z)
inline constexpr std::uint8_t kTileVisibilityPrologue[7] = {0x48, 0x8b, 0xc4, 0x44, 0x88, 0x48, 0x20};
inline constexpr std::size_t kTileVisibilityCentreXAt = 0x11;   // swupdate:keep
inline constexpr std::uint8_t kTileVisibilityCentreX[3] = {0x0f, 0x10, 0x02};
inline constexpr std::size_t kTileVisibilityCentreZAt = 0x43;   // swupdate:keep
inline constexpr std::uint8_t kTileVisibilityCentreZ[6] = {0xf2, 0x44, 0x0f, 0x10, 0x4a, 0x10};
// The frame's draw lists (REPORT 3.156h): 0x6be1b0 is called by the logic once per tick (0x74cee2 in
// the frame build 0x74c470) with the logic's view as its 5th argument (logic + 0xac0: the double 4x4
// of the camera at +0, the one of the tick before at +0x80, the fov at +0x100) and the new frame
// state as its 11th; it copies the view into the frame state (+0x5e8) and culls what the frame
// draws - the terrain tiles among it - against it. Hooked at its entry, the view holds the story
// camera while a shot holds.
inline constexpr std::uint32_t kRvaFrameLists = 0x6be1b0;
inline constexpr std::size_t kOffViewFov = 0x100;               // float in the logic's view (logic + 0xbc0)
// The viewer's pose right in front of the view (logic + 0xa70, written by the game-to-logic camera event
// that 0x71be90 builds from the controller's camera): double position[3], then a float 3x3 (columns
// right / up / forward) at + 24. The draw lists get it as their 4th (position) and 3rd (rotation) argument.
inline constexpr std::size_t kOffViewerPose = 0x50;             // the view minus this
inline constexpr std::size_t kViewerPoseBytes = 60;
// push rbp/rbx/rsi/rdi/r12..r15; lea rbp,[rsp-0x8838]
inline constexpr std::uint8_t kFrameListsPrologue[21] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
                                                         0x41, 0x57, 0x48, 0x8d, 0xac, 0x24, 0xc8, 0x77, 0xff, 0xff};
// mov r12,[rbp+0x88a0]: the 5th argument (rbp = entry rsp - 0x8878)
inline constexpr std::size_t kFrameListsViewArgAt = 0xaf;       // swupdate:keep
inline constexpr std::uint8_t kFrameListsViewArg[7] = {0x4c, 0x8b, 0xa5, 0xa0, 0x88, 0x00, 0x00};
// lea rcx,[r13+0x5e8]; mov rdx,r12; call: the view copied into the frame state
inline constexpr std::size_t kFrameListsCopyAt = 0xf2;          // swupdate:keep
inline constexpr std::uint8_t kFrameListsCopy[11] = {0x49, 0x8d, 0x8d, 0xe8, 0x05, 0x00, 0x00, 0x49, 0x8b, 0xd4, 0xe8};
inline constexpr std::uint8_t kFrameCopyFov[8] = {0xf3, 0x0f, 0x10, 0x87, 0x78, 0x02, 0x00, 0x00};

}  // namespace sw::camera
