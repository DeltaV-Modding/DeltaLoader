#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sw::settings {

// A setting of the server: a name, the offset in the state and the type.
struct Variable {
    std::string_view name;   // "infinite_fuel"
    std::uint32_t offset;    // the offset in the state of the server
    bool is_bool;            // true = a boolean, false = a number
};

// The names of all settings.
std::vector<std::string> names();

// The setting of this name, if the map knows it.
const Variable* find(std::string_view name);

// The state of the server is reachable (the game matches the mapping).
bool ready();

// The value of a boolean setting.
bool get_bool(std::string_view name, bool& out);

// The value of a number setting.
bool get_number(std::string_view name, double& out);

// Writes a boolean setting. False if the name is unknown, if the setting is
// a number or if the state is not reachable.
bool set_bool(std::string_view name, bool value);

// Writes a number setting.
bool set_number(std::string_view name, double value);

}  // namespace sw::settings
