#pragma once

#include "sw/hook.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace sw::events {

// Callback on the event.
//   self = the first argument of the dispatcher (the Lua state)
//   user = the pointer that was passed to on()
using EventCallback = void (*)(void* self, void* user);

// Hooks an event. The name is the name of the callback of the game
// ("onTick", "onPlayerJoin", "onCreate", ...). Several callbacks per
// event are possible.
HookStatus on(std::string_view name, EventCallback callback, void* user,
              std::string& error);

// Removes one callback of an event again.
bool off(std::string_view name, EventCallback callback, void* user,
         std::string& error);

// The names of all known events.
std::vector<std::string> names();

// The names of the events that have a callback at the moment.
std::vector<std::string> active();

// Removes every callback of every event. The skeleton of the mod does
// that anyway when the mod is unloaded (sw/mod.hpp).
void clear();

}  // namespace sw::events
