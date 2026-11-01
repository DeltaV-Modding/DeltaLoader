#pragma once

#include "sw/events.hpp"
#include "sw/game.hpp"
#include "sw/flags.hpp"
#include "sw/start_tiles.hpp"
#include "sw/camera.hpp"
#include "sw/physics.hpp"
#include "sw/mapicons.hpp"
#include "sw/render.hpp"
#include "sw/network.hpp"
#include "sw/audio.hpp"
#include "sw/game_state.hpp"
#include "sw/input.hpp"
#include "sw/vehicle.hpp"
#include "sw/weather.hpp"
#include "sw/mesh.hpp"
#include "sw/textures.hpp"
#include "sw/discord.hpp"
#include "sw/tiles.hpp"
#include "sw/trees.hpp"
#include "sw/views.hpp"
#include "sw/world.hpp"
#include "sw/hook.hpp"
#include "sw/obj.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sw::api {

// The living objects of a class. The name of the class comes from the
// RTTI of the game ("c_scene_object", "c_application_state_game", ...).
// Derived classes run through the constructor of the base class and
// therefore also appear in the list of the base class.
inline std::vector<Obj> objects(std::string_view cls) {
    return Objects::instance().of_class(cls);
}

// Remembers an object that the mod has seen itself (for example in a
// constructor hook). The class is taken from the RTTI of the game. If
// the game is not attached, the class stays empty.
Obj observe(std::uintptr_t address);

// The events of the game.
namespace events {
using sw::events::EventCallback;
using sw::events::off;
using sw::events::on;
}  // namespace events

// Run your own code whenever a function of the game is called. The name
// is either a Lua function ("server.getPlayers") or a function the
// mapping has named ("c_ui_main_menu_default_view_tick"). The callback
// runs BEFORE the original code and cannot change its result; "self" is
// the Lua state resp. the first argument. Returns false (and an error)
// if the name is unknown or the function cannot be hooked.
bool on_call(std::string_view name, HookCallback callback, void* user, std::string& error);

// Replaces a function of the game: the callback decides whether the
// original code runs, and the value it writes is then the result of the
// function. A Lua function cannot be replaced - its result belongs to the
// Lua stack.
bool replace(std::string_view name, ReplaceCallback callback, void* user, std::string& error);

// Removes a replacement again.
bool remove_replace(std::string_view name, ReplaceCallback callback, void* user,
                    std::string& error);

// What is known about the Lua API of the game.
namespace lua {

// The description of one Lua function of the game.
struct Function {
    std::string_view name;       // "server.getPlayers"
    std::uint8_t argc;           // the number of arguments
    std::string_view args;       // the types of the arguments
    std::string_view outs;       // the result fields with their type tag
    std::string_view out_names;  // the names of the result fields
    std::string_view confidence;  // how the names were proven
};

// The function of the game, if the mapping knows it. Returns false if
// the name is unknown.
bool find(std::string_view name, Function& out);

// All known functions of the Lua API.
std::vector<Function> all();

// The functions whose name begins with the prefix ("server.").
std::vector<Function> of_area(std::string_view prefix);

}  // namespace lua

// The catalogue of the game: what exists at all.
//
// A modder needs to know the names before it can use them: which classes
// the game has, which setting keys the server accepts and which component
// types the vehicles know. All three are measured and in the map.
namespace catalogue {

// A setting key of the server. The keys are the ones that
// server.getGameSettings and server.setGameSetting accept.
struct Setting {
    std::string_view name;  // "infinite_fuel"
    std::string_view type;  // "bool" or "number"
};

// A component type of the vehicles.
struct Component {
    std::string_view name;      // "engine"
    std::string_view category;  // "engine"
};

// The classes of the game (the names of the RTTI). The prefix filters
// them ("c_ui_element" -> the UI elements). 462 classes.
std::vector<std::string> classes(std::string_view prefix = {});

// Does the game have this class? The name has to be exact.
bool has_class(std::string_view name);

// The setting keys of the server with their type. 35 keys.
std::vector<Setting> settings();

// The component types of the vehicles. The category filters them
// ("engine", "misc", ...). 742 types in 17 categories.
std::vector<Component> components(std::string_view category = {});

// The categories of the component types.
std::vector<std::string> component_categories();

}  // namespace catalogue
}  // namespace sw::api
