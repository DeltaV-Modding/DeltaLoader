// This was generated.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sw::map {

struct EventEntry {
    std::string_view name;
    std::string_view group;
    std::uint32_t rva;
    std::uint32_t size;
    std::string_view shared_with;  // the names that use the same dispatcher
    std::string_view confidence;
};

inline constexpr std::array<EventEntry, 46> kEvents = {{
    {"onCreate", "lifecycle", 0x5d36b0, 388, "", "high"},
    {"onParse", "lifecycle", 0x3377d0, 258, "", "high"},
    {"onDestroy", "lifecycle", 0x5d3840, 354, "", "high"},
    {"onAddToSimulation", "object-vehicle", 0x3384f0, 596, "", "high"},
    {"onRemoveFromSimulation", "object-vehicle", 0x338760, 467, "", "high"},
    {"onObjectLoad", "object-vehicle", 0x5d2c40, 1424, "onObjectUnload,onVehicleLoad,onVehicleUnload", "high"},
    {"onObjectUnload", "object-vehicle", 0x5d2c40, 1424, "onObjectLoad,onVehicleLoad,onVehicleUnload", "high"},
    {"onVehicleLoad", "object-vehicle", 0x5d2c40, 1424, "onObjectLoad,onObjectUnload,onVehicleUnload", "high"},
    {"onVehicleUnload", "object-vehicle", 0x5d2c40, 1424, "onObjectLoad,onObjectUnload,onVehicleLoad", "high"},
    {"onVehicleSpawn", "object-vehicle", 0x5bc320, 494, "", "high"},
    {"onVehicleDespawn", "object-vehicle", 0x5d48b0, 434, "", "high"},
    {"onVehicleDamaged", "object-vehicle", 0x5bcc60, 450, "", "high"},
    {"onVehicleTeleport", "object-vehicle", 0x5bc6f0, 440, "", "high"},
    {"onPlayerJoin", "player", 0x5bb3f0, 454, "", "high"},
    {"onPlayerLeave", "player", 0x5bbf80, 454, "", "high"},
    {"onPlayerDie", "player", 0x5bc150, 449, "", "high"},
    {"onPlayerRespawn", "player", 0x5d42c0, 390, "", "high"},
    {"onPlayerSit", "player", 0x5bb5c0, 404, "", "high"},
    {"onPlayerUnsit", "player", 0x5bb760, 404, "", "high"},
    {"onCharacterSit", "player", 0x5bbaa0, 403, "", "high"},
    {"onCharacterUnsit", "player", 0x5bb900, 403, "", "high"},
    {"onCreatureSit", "player", 0x5bbde0, 403, "", "high"},
    {"onCreatureUnsit", "player", 0x5bbc40, 403, "", "high"},
    {"onCharacterPickup", "player", 0x5d4a70, 403, "", "high"},
    {"onCreaturePickup", "player", 0x5d4f90, 436, "", "high"},
    {"onChatMessage", "player", 0x5bb220, 461, "", "high"},
    {"onDraw", "communication", 0x74e020, 4006, "", "high"},
    {"onRender", "communication", 0x33a300, 462, "", "high"},
    {"onCustomCommand", "communication", 0x5bafc0, 600, "", "high"},
    {"onFireExtinguished", "world", 0x5d5ae0, 452, "", "high"},
    {"onOilSpill", "world", 0x5bcfe0, 479, "", "high"},
    {"onMeteor", "world", 0x5d5510, 480, "", "high"},
    {"onTornado", "world", 0x5d56f0, 425, "", "high"},
    {"onTsunami", "world", 0x5d5150, 480, "", "high"},
    {"onVolcano", "world", 0x5d58a0, 425, "", "high"},
    {"onWhirlpool", "world", 0x5d5330, 480, "", "high"},
    {"onTick", "misc", 0x32f0f0, 569, "", "medium"},
    {"onGroupSpawn", "misc", 0x5bc510, 469, "", "high"},
    {"onSpawnAddonComponent", "misc", 0x5bca80, 480, "", "high"},
    {"onEquipmentPickup", "misc", 0x5d4c10, 436, "", "high"},
    {"onEquipmentDrop", "misc", 0x5d4dd0, 436, "", "high"},
    {"onToggleMap", "misc", 0x5d4450, 399, "", "high"},
    {"onButtonPress", "misc", 0x5bc8b0, 449, "", "high"},
    {"onForestFireSpawned", "misc", 0x738d60, 489, "", "high"},
    {"onForestFireExtinguished", "misc", 0x738f50, 489, "", "high"},
    {"onClearOilSpill", "misc", 0x7391e0, 345, "", "high"},
}};

constexpr const EventEntry* find_event(std::string_view name) noexcept {
    for (const auto& e : kEvents) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

static_assert(kEvents.size() > 0);

}  // namespace sw::map
