// This is generated.
#pragma once
#include <array>
#include <cstddef>
#include <string_view>

namespace sw::map {

struct SettingKey {
    std::string_view name;
    std::string_view type;  // "bool" or "number"
};

inline constexpr std::array<SettingKey, 35> kSettings = {{
    {"ceasefire", "bool"},
    {"cleanup_vehicle", "bool"},
    {"clear_fow", "bool"},
    {"day_length", "number"},
    {"despawn_on_leave", "bool"},
    {"engine_overheating", "bool"},
    {"fast_travel", "bool"},
    {"infinite_ammo", "bool"},
    {"infinite_batteries", "bool"},
    {"infinite_fuel", "bool"},
    {"infinite_money", "bool"},
    {"lightning", "bool"},
    {"map_show_players", "bool"},
    {"map_show_vehicles", "bool"},
    {"map_teleport", "bool"},
    {"megalodon", "bool"},
    {"no_clip", "bool"},
    {"npc_damage", "bool"},
    {"override_weather", "bool"},
    {"photo_mode", "bool"},
    {"player_damage", "bool"},
    {"respawning", "bool"},
    {"rogue_mode", "bool"},
    {"settings_menu", "bool"},
    {"settings_menu_lock", "bool"},
    {"sharks", "bool"},
    {"show_3d_waypoints", "bool"},
    {"show_name_plates", "bool"},
    {"teleport_vehicle", "bool"},
    {"third_person", "bool"},
    {"third_person_vehicle", "bool"},
    {"unlock_all_components", "bool"},
    {"unlock_all_islands", "bool"},
    {"vehicle_damage", "bool"},
    {"vehicle_spawning", "bool"},
}};

static_assert(kSettings.size() > 0);

}  // namespace sw::map
