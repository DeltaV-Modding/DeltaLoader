#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sw::tiles {

// A group of the tile XML. The game reads 23 groups out of every tile file;
// each group becomes a list in the definition.
struct Section {
    std::string_view group;    // the XML element that holds the list ("meshes")
    std::string_view element;  // the XML element of one entry ("mesh"), may be the same
    unsigned object_size;      // bytes the parser allocates per entry
    unsigned definition_offset;  // the ring list of this group inside the definition
    std::string_view cls;      // the class of the objects (proven by the RTTI)
    std::string_view placement;  // what the object does when the game puts it into the world (see above)
};

// The groups of the tile XML, in the order the game parses them.
const std::vector<Section>& sections();

// The attributes of the root element <definition> that the game reads.
struct Attribute {
    std::string_view xml;
    std::string_view type;     // bool, int, float
    unsigned definition_offset;
    unsigned cell_offset;      // where the cell keeps its copy (0 = none)
};
const std::vector<Attribute>& attributes();

// What the game keeps for a loaded definition.
struct Definition {
    bool found = false;
    bool is_island = false;
    bool is_volcanic = false;
    float sea_floor_height = 0.0f;
    int tile_type = 0;         // the attribute tile_type
    bool purchasable = false;
    int purchase_cost = 0;
};

// A cell of the level.
struct Cell {
    int gx = 0;
    int gy = 0;
    std::string name;          // the file the cell was made from
    bool is_island = false;
    bool is_volcanic = false;
    float sea_floor_height = 0.0f;
    int tile_type = 0;
    bool purchasable = false;
    int purchase_cost = 0;
    int state = 0;             // where the streaming has brought the cell (state_name)
    int requested = 0;         // where the streaming wants it to go
};


const char* state_name(int state);

// The moments of the tile system that a mod can react to.
enum class Event {
    definitions_loaded,   // the game has read all tile files (once per start)
    world_created,        // the game builds a world and places its tiles
    tiles_placed,         // directly after the game placed its tiles (place() runs here)
    cell_created,         // a cell was made; EventInfo has its grid position
    stream_update,        // every frame: the streaming looks at the cells
    tiles_placed_late,    // after tiles_placed AND after the tiles that mods queued in it were placed (cells() has them): decide here whether a tile is missing
};

struct EventInfo {
    Event event;
    int gx = 0;           // only for cell_created
    int gy = 0;
};

using EventCallback = void (*)(const EventInfo& info, void* user);

// Runs the callback at the moment. The callback is on the thread of the game:
// keep it short. Needs no watch().
bool on(Event event, EventCallback callback, void* user, std::string& error);
bool off(Event event, EventCallback callback, void* user);

// The cells of the level that exist (scans the grid of the level).
std::vector<Cell> cells();

// The cells inside a rectangle of grid cells (both corners included).
std::vector<Cell> cells_in(int min_gx, int min_gy, int max_gx, int max_gy);

// The cell and its eight neighbours are free: the test that place() makes.
bool is_free(int gx, int gy);

// Starts watching for the world and for the moment in which tiles can be
// placed. False if the game functions do not match or a hook fails.
bool watch();

// A world has been seen.
bool ready();

// The definition of a tile file ("island12.xml" or "data/tiles/island12.xml").
Definition definition(std::string_view file);

// The cell at a grid cell, if there is one.
bool cell_at(int gx, int gy, Cell& out);

// Queues a tile for placing when the game builds its next world. Refuses a
// grid cell outside the level, a tile file the game does not know, and a
// cell that is already taken. The refusal is reported at the moment of
// placing (last_report()). Returns false only if watch() was not called.
bool place(std::string_view file, int gx, int gy, std::string& error);

// What happened at the last moment of placing: one line per tile.
std::vector<std::string> last_report();

bool place_random(std::string_view file, int center_gx, int center_gy, int min_distance,
                  int max_distance, std::string& error);

// The seed of the world that is being built (the number the player typed at
// the new game, or the one of the save). Valid after the game has placed its
// tiles (in the event tiles_placed); 0 before that.
int world_seed();

// The number of tiles that wait for the next world.
std::size_t pending();

// Forgets the queue (the skeleton calls this on unload).
void clear();

}  // namespace sw::tiles
