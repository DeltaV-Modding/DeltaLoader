// This was generated.
#pragma once
#include <cstddef>
#include <cstdint>
namespace sw::vehicle::map {
inline constexpr std::uint32_t kRvaSamplerDelta = 0x54a120;
inline constexpr std::uint32_t kRvaSamplerAdd = 0x5499b0;
inline constexpr std::uint32_t kRvaSamplerWeather = 0x2c3d40;
inline constexpr std::uint32_t kRvaVtableScriptOperators = 0xb1c328;
inline constexpr std::uint32_t kOffWeatherInWorld = 0xbade8;
inline constexpr std::uint32_t kOffTableInWorld = 0x4990;
inline constexpr std::uint32_t kOffRingGroups = 0x18;
inline constexpr std::uint32_t kOffRingRecords = 0x30;
inline constexpr std::uint32_t kOffRingStates = 0x4cb0;
inline constexpr std::uint32_t kWorldMinBytes = 0x4cc0;
inline constexpr std::uint32_t kRecId = 0x4;
inline constexpr std::uint32_t kRecGroupId = 0x8;
inline constexpr std::uint32_t kRecMatrix = 0x10;
inline constexpr std::uint32_t kRecPosX = 0x70;
inline constexpr std::uint32_t kRecNamePtr = 0xc0;
inline constexpr std::uint32_t kRecNameLen = 0xc8;
inline constexpr std::uint32_t kRecEditable = 0xd6;
inline constexpr std::uint32_t kRecInvulnerable = 0xd7;
inline constexpr std::uint32_t kRecState = 0xe0;
inline constexpr std::uint32_t kRecMinBytes = 0xe8;
inline constexpr std::uint32_t kStFlag0 = 0x0;
inline constexpr std::uint32_t kStId = 0x4;
inline constexpr std::uint32_t kStScene = 0x8;
inline constexpr std::uint32_t kStState = 0x10;
inline constexpr std::uint32_t kStReady = 0x5;
inline constexpr std::uint32_t kStMinBytes = 0x18;
inline constexpr std::uint32_t kSceneRingBodies = 0x0;
inline constexpr std::uint32_t kSceneId = 0x178;
inline constexpr std::uint32_t kSceneMinBytes = 0x408;
inline constexpr std::uint32_t kGroupId = 0xc4;
inline constexpr std::uint32_t kGroupIds = 0x0;
inline constexpr std::uint32_t kGroupMinBytes = 0xc8;
inline constexpr std::uint32_t kCompVtable = 0x0;
inline constexpr std::uint32_t kCompPosX = 0x18;
inline constexpr std::uint32_t kCompPosY = 0x1c;
inline constexpr std::uint32_t kCompPosZ = 0x20;
inline constexpr std::uint32_t kCompNamePtr = 0xa8;
inline constexpr std::uint32_t kCompNameLen = 0xb0;
inline constexpr std::uint32_t kCompButtonValue = 0x4c8;
inline constexpr std::uint32_t kCompButtonValue2 = 0x4cc;
inline constexpr std::uint32_t kCompBatteryChargeRaw = 0x250;
inline constexpr std::uint32_t kCompTankCapacityRaw = 0x5a0;
inline constexpr std::uint32_t kCompMinBytes = 0xb8;
inline constexpr std::uint32_t kCompMaxRaw = 0x1700;
struct KindRow { const char* name; const char* lua_api; const char* class_name; std::uint32_t vtable_rva; std::uint32_t ring_offset; };
inline constexpr KindRow kKinds[] = {
    {"seat", "server.getVehicleSeat", "c_scene_vehicle_component_seat", 0xb09b18, 0x318},
    {"button", "server.getVehicleButton", "c_scene_vehicle_component_button", 0xb75658, 0x330},
    {"sign", "server.getVehicleSign", "c_scene_vehicle_component_sign", 0xb56ca0, 0x348},
    {"dial", "server.getVehicleDial", "c_scene_vehicle_component_indicator", 0xb4f3e0, 0x360},
    {"tank", "server.getVehicleTank", "c_scene_vehicle_component_water", 0xb13818, 0x378},
    {"battery", "server.getVehicleBattery", "c_scene_vehicle_component_electric", 0xb8bce8, 0x390},
    {"hopper", "server.getVehicleHopper", "c_scene_vehicle_component_steam", 0xb01f38, 0x3a8},
    {"weapon", "server.getVehicleWeapon", "c_scene_vehicle_component_weapon", 0xafd290, 0x3d8},
    {"rope_hook", "server.getVehicleRopeHook", "c_scene_vehicle_component_rope_hook", 0xb69ff8, 0x3f0},
};
inline constexpr std::size_t kKindCount = sizeof(kKinds) / sizeof(kKinds[0]);
struct ClassRow { std::uint32_t vtable_rva; const char* name; };
inline constexpr ClassRow kComponentClasses[] = {
    {0xaea478, "transmission_line"},
    {0xaf3178, "magall"},
    {0xaf7320, "wheel"},
    {0xafd290, "weapon"},
    {0xafd938, "flag"},
    {0xb00190, "flare_launcher"},
    {0xb01f38, "steam"},
    {0xb04758, "door_dock"},
    {0xb09b18, "seat"},
    {0xb0bcc0, "handle"},
    {0xb0ce88, "multibody"},
    {0xb0d6e8, "ladder"},
    {0xb13818, "water"},
    {0xb16d50, "extended_base"},
    {0xb186b0, "door_manual"},
    {0xb18da8, "door_manual_sliding"},
    {0xb4f3e0, "indicator"},
    {0xb50c08, "rudder"},
    {0xb56ca0, "sign"},
    {0xb58ca8, "data_logger"},
    {0xb59358, "tool"},
    {0xb5ad50, "connector"},
    {0xb5d328, "camera"},
    {0xb5dae8, "fish_finder"},
    {0xb61828, "metadata"},
    {0xb63ba0, "fire"},
    {0xb640f0, "lua"},
    {0xb66a88, "pump"},
    {0xb68888, "solid_rocket"},
    {0xb69ff8, "rope_hook"},
    {0xb6b150, "door"},
    {0xb6e4f0, "rx"},
    {0xb6f040, "dynamic_base"},
    {0xb6faf0, "inventory"},
    {0xb70938, "torque"},
    {0xb72588, "friction_block"},
    {0xb74038, "logic_gate"},
    {0xb75658, "button"},
    {0xb76810, "light"},
    {0xb76de0, "buoyancy"},
    {0xb77b40, "scope"},
    {0xb78f80, "engine"},
    {0xb7b948, "force_emitter"},
    {0xb7d2d0, "monitor"},
    {0xb7df18, "nuclear"},
    {0xb7fab0, "lobster_pot"},
    {0xb82528, "stabilizer"},
    {0xb87ca0, "heater"},
    {0xb8b158, "basic_dynamic"},
    {0xb8bce8, "electric"},
    {0xb8c540, "ski"},
    {0xb8cf30, "wheel_advanced"},
    {0xb8e068, "oil_rig"},
    {0xb8ee48, "sonar"},
    {0xb8f9c0, "microprocessor"},
    {0xb90db0, "lua_base"},
    {0xb93028, "jet_engine"},
    {0xb95828, "train"},
    {0xb979f8, "parachute"},
    {0xb98150, "composite"},
    {0xb99120, "light_dynamic"},
    {0xb9c6a0, "gyro"},
    {0xb9e4f8, "radar"},
    {0xba0878, "winch"},
    {0xba8278, "modular_engine"},
    {0xbb0300, "door_custom"},
};
inline constexpr std::size_t kComponentClassCount = sizeof(kComponentClasses) / sizeof(kComponentClasses[0]);
}  // namespace sw::vehicle::map
