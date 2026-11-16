// This was generated.
#pragma once
#include <cstddef>
#include <cstdint>
namespace sw::vehicle::map {
inline constexpr std::uint32_t kRvaSamplerDelta = 0x54a120;
inline constexpr std::uint32_t kRvaSamplerAdd = 0x5499b0;
inline constexpr std::uint32_t kRvaSamplerWeather = 0x2c3d40;
inline constexpr std::uint32_t kRvaVtableScriptOperators = 0xb1d128;
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
    {"seat", "server.getVehicleSeat", "c_scene_vehicle_component_seat", 0xb0aa80, 0x318},
    {"button", "server.getVehicleButton", "c_scene_vehicle_component_button", 0xb765f0, 0x330},
    {"sign", "server.getVehicleSign", "c_scene_vehicle_component_sign", 0xb57bd8, 0x348},
    {"dial", "server.getVehicleDial", "c_scene_vehicle_component_indicator", 0xb50450, 0x360},
    {"tank", "server.getVehicleTank", "c_scene_vehicle_component_water", 0xb148b0, 0x378},
    {"battery", "server.getVehicleBattery", "c_scene_vehicle_component_electric", 0xb8cd70, 0x390},
    {"hopper", "server.getVehicleHopper", "c_scene_vehicle_component_steam", 0xb03060, 0x3a8},
    {"weapon", "server.getVehicleWeapon", "c_scene_vehicle_component_weapon", 0xafe1f0, 0x3d8},
    {"rope_hook", "server.getVehicleRopeHook", "c_scene_vehicle_component_rope_hook", 0xb6afe0, 0x3f0},
};
inline constexpr std::size_t kKindCount = sizeof(kKinds) / sizeof(kKinds[0]);
struct ClassRow { std::uint32_t vtable_rva; const char* name; };
inline constexpr ClassRow kComponentClasses[] = {
    {0xaeb728, "transmission_line"},
    {0xaf4400, "magall"},
    {0xaf8290, "wheel"},
    {0xafe1f0, "weapon"},
    {0xafeae8, "flag"},
    {0xb00f78, "flare_launcher"},
    {0xb03060, "steam"},
    {0xb05688, "door_dock"},
    {0xb0aa80, "seat"},
    {0xb0cc38, "handle"},
    {0xb0ddf0, "multibody"},
    {0xb0e658, "ladder"},
    {0xb148b0, "water"},
    {0xb17ca8, "extended_base"},
    {0xb19450, "door_manual"},
    {0xb19cb8, "door_manual_sliding"},
    {0xb50450, "indicator"},
    {0xb51b58, "rudder"},
    {0xb57bd8, "sign"},
    {0xb59d20, "data_logger"},
    {0xb5a400, "tool"},
    {0xb5bd98, "connector"},
    {0xb5e570, "camera"},
    {0xb5ea48, "fish_finder"},
    {0xb627c8, "metadata"},
    {0xb64b00, "fire"},
    {0xb65110, "lua"},
    {0xb67860, "pump"},
    {0xb69a58, "solid_rocket"},
    {0xb6afe0, "rope_hook"},
    {0xb6c110, "door"},
    {0xb6f4f0, "rx"},
    {0xb70138, "dynamic_base"},
    {0xb70c30, "inventory"},
    {0xb71930, "torque"},
    {0xb734e0, "friction_block"},
    {0xb75010, "logic_gate"},
    {0xb765f0, "button"},
    {0xb77780, "light"},
    {0xb77da0, "buoyancy"},
    {0xb78c88, "scope"},
    {0xb79ef0, "engine"},
    {0xb7c948, "force_emitter"},
    {0xb7e240, "monitor"},
    {0xb7f058, "nuclear"},
    {0xb80ae0, "lobster_pot"},
    {0xb83450, "stabilizer"},
    {0xb88db8, "heater"},
    {0xb8c360, "basic_dynamic"},
    {0xb8cd70, "electric"},
    {0xb8d6e8, "ski"},
    {0xb8de58, "wheel_advanced"},
    {0xb8ef28, "oil_rig"},
    {0xb8fd78, "sonar"},
    {0xb90a28, "microprocessor"},
    {0xb91d00, "lua_base"},
    {0xb93f68, "jet_engine"},
    {0xb96768, "train"},
    {0xb98950, "parachute"},
    {0xb99090, "composite"},
    {0xb9a3c0, "light_dynamic"},
    {0xb9d330, "gyro"},
    {0xb9f438, "radar"},
    {0xba1920, "winch"},
    {0xba9210, "modular_engine"},
    {0xbb10b8, "door_custom"},
};
inline constexpr std::size_t kComponentClassCount = sizeof(kComponentClasses) / sizeof(kComponentClasses[0]);
}  // namespace sw::vehicle::map
