// This is generated.
#pragma once

#include <cstddef>
#include <cstdint>

namespace sw::state {

struct StateInfo {
    std::uint8_t id;            // the argument of the state factory
    const char* name;           // short name
    const char* class_name;     // RTTI name
    std::uint32_t vtable;       // RVA
    std::uint32_t constructor;  // RVA
    std::uint32_t alloc_size;   // bytes allocated by the factory
};

inline constexpr StateInfo kStates[] = {
    {1, "splash_screen", "c_application_state_splash_screen", 0xb6bac0, 0x897200, 0xb20},
    {2, "menu", "c_application_state_menu", 0xb4cb50, 0x8980e0, 0xccbe0},
    {3, "game", "c_application_state_game", 0xb0f4a0, 0x8992e0, 0xce7c0},
    {4, "component_editor", "c_application_state_component_editor", 0xb9edc8, 0x8997c0, 0x9628},
    {5, "tile_editor", "c_application_state_tile_editor", 0xbaed38, 0x8976b0, 0xb8080},
};
inline constexpr std::size_t kStateCount = sizeof(kStates) / sizeof(kStates[0]);

// RVAs of the functions the API hooks
inline constexpr std::uint32_t kFactoryRva = 0x897550;          // state_factory(manager, engine, id)
inline constexpr std::uint32_t kUpdateRva = 0x8e0560;           // state_machine_update(manager, engine)
inline constexpr std::uint32_t kLoadWorldRva = 0x7a2e40;        // game_state_load_world (c_game_state_simulation slot 1)
inline constexpr std::uint32_t kSetModeRva = 0x7a1ea0;          // game_state_set_mode(mode_object, ..., mode)
inline constexpr std::uint32_t kSimulationVtableRva = 0xb1a988;
inline constexpr std::uint32_t kTransitionVtableRva = 0xb14230;
inline constexpr std::uint32_t kGameUnloadRva = 0x84c4e0;        // c_application_state_game slot 3 (unload)

// Offsets in the state manager (the object the update and the factory get as their first argument)
inline constexpr std::uint32_t kOffRequestId = 0x10;
inline constexpr std::uint32_t kOffActiveId = 0x4eb0;
inline constexpr std::uint32_t kOffDirtyFlag = 0x4eb4;
inline constexpr std::uint32_t kOffCurrentState = 0x9a20;
inline constexpr std::uint32_t kOffPendingState = 0x9a28;
inline constexpr std::uint32_t kOffTransition = 0x9a30;
inline constexpr std::uint32_t kOffPhase = 0x9a50;
// Offsets in the game state object
inline constexpr std::uint32_t kGameOffModeObject = 0xce150;
inline constexpr std::uint32_t kGameOffSimulation = 0xce190;
inline constexpr std::uint32_t kGameOffActiveWorld = 0x350;
inline constexpr std::uint32_t kModeObjectOffMode = 0x58;

}  // namespace sw::state
