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

}  // namespace sw::camera
