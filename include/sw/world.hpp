#pragma once

#include <string>
#include <vector>

namespace sw::world {

// A grid cell of the world. The world coordinates are gx * 1000 and
// gy * 1000; the cell of the level is relative to the origin of the level.
struct Island {
    int gx;
    int gy;
};

// The level that the game plays.
struct Level {
    int origin_x = 0;       // the origin of the level (level + 0x30)
    int origin_y = 0;       // the origin of the level (level + 0x34)
    int island_count = 0;   // the cells in the island list
    int island_capacity = 0;  // the size of the ring buffer of the list
};

// Starts watching for the world. Returns false if the game functions do not
// match the mapping or the hook cannot be set.
bool watch();

// A world has been seen and can be read.
bool ready();

// The level that the game plays. Empty if the world is not ready.
Level level();

// The islands of the level: the grid cells. Empty if the world is not ready.
std::vector<Island> islands();

// The number of the tiles of the level (the cells of the whole grid).
std::size_t tile_count();

// Is the grid cell an island (is it in the island list of the level)?
bool is_island(int gx, int gy);

// The cell of the level that belongs to a grid cell: the grid cell without
// the origin of the level. False if the world is not ready or if the cell
// lies outside of the level (-128..127 per axis).
bool relative(int gx, int gy, int& rx, int& ry);

// The grid cell of a world position. False if the world is not ready.
bool cell_of(double x, double z, int& gx, int& gy);

// The world position of a grid cell.
double world_x(int gx);
double world_y(int gy);

// The depth of the sea at a grid cell.
float sea_floor(int gx, int gy);

// The depth of the sea at a world position. 0 if the world is not ready.
float ocean_depth(double x, double z);

}  // namespace sw::world
