#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sw::vehicle {

enum class Kind { seat, button, sign, dial, tank, battery, hopper, weapon, rope_hook };

const char* kind_name(Kind kind);           // "button", ...
const char* kind_lua_api(Kind kind);        // "server.getVehicleButton", ...
bool kind_from_name(const char* name, Kind& out);

// Starts watching. False if the game functions do not match the mapping or a hook cannot be set.
bool watch();
// A world was seen and its rings pass the checks.
bool ready();
// The world object (address), 0 when not ready. For diagnostics only.
std::uintptr_t world_address();

struct Info {
    int id = 0;
    int group_id = 0;
    std::string name;
    double matrix[16] = {};      // record+0x10, the matrix the Lua API returns (position = elements 12..14)
    double x = 0, y = 0, z = 0;
    bool editable = false;
    bool invulnerable = false;
    bool has_state = false;      // the record points to a state record
    std::uint32_t state = 0;     // state record +0x10 (5 = ready)
    int bodies = 0;              // the number of bodies of the scene vehicle
    bool simulating = false;     // the expression of getVehicleData: state record, state == 5, bodies > 0, flag == 0
};

std::size_t count();                       // vehicles in the record ring (0 when not ready)
std::vector<int> ids();
bool find(int id, Info& out);
std::vector<Info> list();
bool position(int id, double& x, double& y, double& z);
std::vector<int> group_members(int group_id);
// the id of the first vehicle with this exact name, -1 if none
int find_by_name(const std::string& name);

struct Component {
    Kind kind = Kind::sign;
    std::uint32_t key = 0;       // the key of the ring entry
    std::string name;
    std::string class_name;      // RTTI class without the c_scene_vehicle_component_ prefix
    int x = 0, y = 0, z = 0;     // grid position
    std::uintptr_t address = 0;  // the component object (valid while the vehicle lives)
};

std::vector<Component> components(int vehicle_id, Kind kind);
bool find_component(int vehicle_id, Kind kind, const std::string& name, Component& out);

// Typed values (two independent sites each).
struct ButtonValues { float value = 0, value2 = 0; };
bool button_values(int vehicle_id, const std::string& name, ButtonValues& out);
bool battery_charge_raw(int vehicle_id, const std::string& name, float& out);   // the stored float; the Lua value is normalised by the capacity
bool tank_capacity_raw(int vehicle_id, const std::string& name, float& out);    // the Lua capacity is raw * 10

// Experiments: read bytes of a component (offset < 0x1700), no meaning is promised.
bool raw_u32(const Component& c, std::uint32_t offset, std::uint32_t& out);
bool raw_f32(const Component& c, std::uint32_t offset, float& out);

}  // namespace sw::vehicle
