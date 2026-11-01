#pragma once

#include "sw/generated_game_state.hpp"

#include <cstdint>
#include <vector>

namespace sw::state {

enum class Id : std::uint8_t {
    none = 0,
    splash_screen = 1,
    menu = 2,
    game = 3,
    component_editor = 4,
    tile_editor = 5,
    exit = 6,
};

// The phase of the state machine (manager + 0x9a50).
enum class Phase : std::uint8_t {
    unloading = 0,   // the old state is unloaded on a worker thread
    waiting = 1,     // waiting for the transition object; then the new state is created
    loading = 2,     // the new state is built on a worker thread
    finishing = 3,   // the loading screen finishes, then the state is entered
    running = 4,     // tick / update / draw run on the current state
    unknown = 255,
};

enum class ChangeKind : std::uint8_t {
    requested,       // the factory was called: a new state is being created (the switch function)
    became_current,  // the pointer of the current state changed to another class (seen once per frame)
};

struct Change {
    ChangeKind kind;
    Id from;         // the state that was current
    Id to;           // requested: the id given to the factory; became_current: the new state
    void* manager;
};

enum class WorldEvent : std::uint8_t {
    load,            // game_state_load_world is entered; object = the c_game_state_simulation
    unload,          // the game state is unloaded (slot 3); object = the game state
};

using ChangeCallback = void (*)(const Change& change, void* user);
using WorldCallback = void (*)(WorldEvent event, void* object, void* user);

const StateInfo* find(Id id);
const StateInfo* find(const char* name);              // "menu", "game", ...
const StateInfo* find_class(const char* class_name);  // the RTTI name
const char* to_string(Id id);                         // "menu", ..., "none"
const char* to_string(Phase phase);
std::vector<const char*> names();                     // the short names, in id order
std::size_t count();

// Installs the hooks on the state machine update and on the factory. False if the game does not match,
// the 32 bit build, or a hook cannot be placed.
bool watch();
// Removes the callbacks of this module (the hooks stay until Hook::remove_all).
void unwatch();

// The state manager, seen by the update hook; nullptr before the first frame after watch().
void* manager();
// The class of an object by its vtable: Id::none if it is not a state object.
Id identify(const void* state_object);
// The state object that is current now (manager + 0x9a20), nullptr if unknown.
void* current_object();
Id current();
// The last id that was requested (manager + 0x4eb0); it equals current() once the change is complete.
Id requested();
Phase phase();
bool running();                                       // phase() == Phase::running

// Calls `callback` for every change. False before watch().
bool on_change(ChangeCallback callback, void* user);

// Calls `callback` when the world is loaded / the game state is unloaded. Installs two more hooks.
bool on_world(WorldCallback callback, void* user);

}  // namespace sw::state
