// clouds.hpp - decide where the game's clouds appear: areas with clouds only, areas without clouds, the height, how many, how far,
// and single clouds at a place you choose. REPORT 3.214.
//
//   #include "sw/mod.hpp"
//   #include "sw/clouds.hpp"
//
//   void sw_mod_start() {
//       std::string error;
//       if (!sw::clouds::install(error)) { sw::mod::log("clouds: %s", error.c_str()); return; }
//       sw::clouds::Rules rules;
//       rules.never_in.push_back({0, 0, 4000});            // no clouds within 4 km of the world origin
//       rules.min_height = 600;  rules.max_height = 900;   // metres
//       rules.max_clouds = 120;                            // more clouds than the game's
//       sw::clouds::set_rules(rules);
//       sw::clouds::respawn_all();                         // the clouds in the sky now follow the rules too
//       sw::clouds::spawn_at(1200, 700, -300);             // one cloud right there (it must be near the camera, see below)
//   }
//
// HOW THE GAME MAKES CLOUDS (static reading of the 1.15.27 code, REPORT 3.214):
//   * one cloud field per world (world + 0x718), set up by 0x2c8ce0: it loads meshes/cloud_02..08 and keeps a ring of clouds
//     (0x1d0 bytes each). The field follows the camera: its update 0x2c9700 sets the centre (+0x40, double x y z) every tick;
//   * the update fills the ring up to the maximum (+0x58) and calls 0x2c90d0 for each new cloud: a random mesh, x and z random in
//     centre +- radius (+0x38, 3000 m), height 300 + 0..700 m, a size, a turn and a lifetime;
//   * a cloud ends when its lifetime is over or when it is farther than the radius from the centre (it fades out first), and the
//     next update spawns a new one. So every cloud you see was made by 0x2c90d0.
// WHAT THIS HEADER DOES WITH IT: one entry hook on the update 0x2c9700. There it writes the maximum and the radius, and every cloud
// the game made since the last tick is moved where your rules allow it (or taken away): a new cloud starts at size 0 and grows over
// the first tenth of its lifetime (the update scales it by age / lifetime), so the one tick it spent at the game's place is not seen. It also ends clouds when you ask for it.
// A cloud can only live within the radius around the camera: an area far from the camera has no clouds until you come near.
//
// PROVEN: the rules (api_test, no game); with SW_GAME_DIR the bytes of the stores of 0x2c90d0 (+0x190 / +0x198 / +0x1a0), the
// reads of +0x38 / +0x58 / +0x5c, Hook::plan of 0x2c9700. NOT TRIED in a running game. Clouds are drawn by the client;
// in multiplayer every player sees their own (they are not synchronised by the game either).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sw::clouds {

// A circle on the map: centre x / z in metres (world coordinates, as getPlayerPos), radius in metres.
struct Area {
    double x = 0;
    double z = 0;
    double radius = 0;
};

struct Rules {
    std::vector<Area> only_in;    // not empty: clouds appear only inside these areas
    std::vector<Area> never_in;   // clouds never appear inside these areas
    float min_height = 0;         // metres; both 0 = the game's 300 .. 1000
    float max_height = 0;
    int max_clouds = -1;          // -1 = the game's number
    float field_radius = 0;       // 0 = the game's 3000 m (the distance from the camera at which clouds live)
    int mesh = -1;                // -1 = a random shape as the game does; 0..6 = always meshes/cloud_0<mesh+2> (the game loads
                                  // cloud_02 .. cloud_08 in this order; info().meshes says how many there are)
};

bool install(std::string& error);   // needs the game of the mapping; fails closed
void uninstall();                   // puts the game's maximum and radius back
bool installed();

void set_rules(const Rules& rules);
Rules rules();

// The next cloud that spawns appears at x / y / z (y = height). Up to 64 wait in a queue; if the field is full, its oldest cloud
// ends to make room. Must be within the field radius of the camera, else the game takes the cloud away again at once.
bool spawn_at(double x, double y, double z);
// The clouds in the sky follow the rules now: each is moved where the rules allow it, or shrinks away like a cloud at the end of
// its life (a new cloud of the game starts at size 0 and grows over a tenth of its life, so ending them all would empty the sky).
void respawn_all();

struct Info {
    bool active = false;          // a cloud field was seen (a world is loaded)
    bool drawn = false;           // the numbers are of the field the renderer draws (else of the logic world's)
    int fields = 0;               // cloud fields seen (2 in a game: the logic world's and the drawn one)
    int clouds = 0;               // clouds now
    int max_clouds = 0;           // the maximum in use
    int game_max_clouds = 0;      // the game's own maximum
    float field_radius = 0;
    double centre[3] = {0, 0, 0}; // where the field is (the camera)
    int meshes = 0;               // cloud shapes loaded
    unsigned long long spawned = 0, moved = 0, removed = 0, placed = 0;   // since install()
};
Info info();

// ---- the rule itself (pure; public for tests) ------------------------------------------------------------------------------
// Is a cloud at x / z allowed by the rules?
bool allowed(const Rules& rules, double x, double z);
// A place for a cloud: `x` / `z` is where the game put it, `cx` / `cz` / `radius` the field. Returns false when the rules leave no
// place in the field (the cloud is taken away). `random` gives numbers in [0, 1).
bool choose_place(const Rules& rules, double cx, double cz, double radius, double& x, double& z, double (*random)(void*), void* state);

// ---- addresses (1.15.27, client RVAs) --------------------------------------------------------------------------------------
inline constexpr std::uint32_t kRvaFieldInit = 0x2c8ce0;    // (field, loader, library, seed): loads the meshes, spawns the first clouds
inline constexpr std::uint32_t kRvaSpawn = 0x2c90d0;        // (field, cloud)
inline constexpr std::uint32_t kRvaUpdate = 0x2c9700;       // (field, &ticks, environment, double centre[3])
// Two fields run through the update: the logic world's (world + 0x718, called from 0x5ec428) and the one the renderer draws
// (X + 0xb49a8, called from 0x5f6a00, read by 0x6cd0b0). The drawn one gets environment = field + 0x6a8.
inline constexpr std::uint32_t kDrawnEnvironment = 0x6a8;
// the field
inline constexpr std::uint32_t kRing = 0x08;                // {data +0x08, capacity u32 +0x10, head u32 +0x14, count u32 +0x18}
inline constexpr std::uint32_t kMeshCount = 0x30;           // u16
inline constexpr std::uint32_t kRadius = 0x38;              // float, 3000
inline constexpr std::uint32_t kCentre = 0x40;              // double x, y, z
inline constexpr std::uint32_t kMaxClouds = 0x58;           // u32
inline constexpr std::uint32_t kSeed = 0x5c;                // u32, the field's own rand() state
// a cloud (0x1d0 bytes)
inline constexpr std::uint32_t kCloudSize = 0x1d0;
inline constexpr std::uint32_t kCloudMatrix = 0x00;         // double 4x4, translation at +0x60
inline constexpr std::uint32_t kCloudMatrixLast = 0x80;     // the matrix of the tick before
inline constexpr std::uint32_t kCloudPos = 0x190;           // double x, y, z
inline constexpr std::uint32_t kCloudLife = 0x1c0;          // float
inline constexpr std::uint32_t kCloudAge = 0x1c4;           // float
inline constexpr std::uint32_t kCloudMesh = 0x1c8;          // u16

}  // namespace sw::clouds
