// This was generated.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sw::map {

struct SettingVariable {
    std::string_view name;
    std::uint32_t offset;   // the offset in the game state
    std::uint8_t width;     // 1 = boolean, 4 = number
    bool is_bool;
};

inline constexpr std::array<SettingVariable, 36> kSettingVariables = {{
    {"third_person", 0x48, 1, true},
    {"third_person_vehicle", 0x49, 1, true},
    {"vehicle_damage", 0x4a, 1, true},
    {"player_damage", 0x4b, 1, true},
    {"npc_damage", 0x4c, 1, true},
    {"sharks", 0x4d, 1, true},
    {"starting_currency", 0x54, 4, false},
    {"fast_travel", 0x51, 1, true},
    {"teleport_vehicle", 0x50, 1, true},
    {"rogue_mode", 0x52, 1, true},
    {"megalodon", 0x4e, 1, true},
    {"lightning", 0x4f, 1, true},
    {"map_show_players", 0x58, 1, true},
    {"map_show_vehicles", 0x59, 1, true},
    {"show_3d_waypoints", 0x5a, 1, true},
    {"show_name_plates", 0x5b, 1, true},
    {"day_length", 0x0, 4, false},
    {"infinite_money", 0x8, 1, true},
    {"settings_menu", 0xa, 1, true},
    {"unlock_all_islands", 0x9, 1, true},
    {"infinite_batteries", 0x33, 1, true},
    {"infinite_fuel", 0x34, 1, true},
    {"engine_overheating", 0x35, 1, true},
    {"ceasefire", 0x36, 1, true},
    {"infinite_ammo", 0x37, 1, true},
    {"no_clip", 0x38, 1, true},
    {"map_teleport", 0x39, 1, true},
    {"cleanup_vehicle", 0x3d, 1, true},
    {"clear_fow", 0x3e, 1, true},
    {"vehicle_spawning", 0x3a, 1, true},
    {"photo_mode", 0x3b, 1, true},
    {"respawning", 0x3c, 1, true},
    {"settings_menu_lock", 0x32, 1, true},
    {"despawn_on_leave", 0x30, 1, true},
    {"unlock_all_components", 0x31, 1, true},
    {"override_weather", 0x3f, 1, true},
}};

static_assert(kSettingVariables.size() > 0);

}  // namespace sw::map
