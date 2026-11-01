#pragma once

#include "sw/obj.hpp"
#include "sw/textures.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace sw::flags {

inline constexpr std::uint32_t kRvaDesignTable = 0xcc3970;   // in .data
inline constexpr int kDesignCount = 162;
inline constexpr std::size_t kDesignStride = 20;             // category, loc id, x, y, z (int each)
inline constexpr std::uint32_t kRvaDesignTableFill = 0x8ad30;  // writes the cells at the start
inline constexpr std::uint32_t kRvaMenuBuild = 0x45f1b0;     // builds the menu of the designs
inline constexpr std::uint32_t kRvaMenuCategory = 0x45f4b0;  // the category was changed
inline constexpr std::uint32_t kRvaComponentCtor = 0x45e3f0;
inline constexpr std::uint32_t kRvaLocTextGet = 0x20e550;    // (mm_string* out, int id): the text of a localisation id
inline constexpr std::uint32_t kCustomLocBase = 0x7e000000;  // ids from here are answered with the names of custom flags
inline constexpr std::size_t kMenuCapacity = 512;            // entries of the table made by show_in_menu
inline constexpr std::uint32_t kMenuBuildSize = 755;         // 0x45f1b0
inline constexpr std::uint32_t kMenuCategorySize = 553;      // 0x45f4b0
inline constexpr std::size_t kOffComponentCellX = 0x258;
inline constexpr std::size_t kOffComponentCellY = 0x25c;
inline constexpr std::size_t kOffComponentCellZ = 0x260;

// The categories of the table, in the order of the menu tabs.
inline constexpr const char* kCategoryName[5] = {"colors", "nations", "pride", "community", "shipping"};

// The cell of the atlas.
inline constexpr int kCellWidth = 64;
inline constexpr int kCellHeight = 32;
inline constexpr int kAtlasColumns = 16;
inline constexpr int kFirstUsedRow = 16;   // rows 0..15 are empty

// The default file this header writes and the game is told to load. It is a NEW
// file in rom/graphics; flags.txtr itself is never touched.
inline constexpr const char* kCustomAtlasPath = "graphics/flags_custom.txtr";

struct Design {
    int index = 0;
    int category = 0;
    std::uint32_t loc_id = 0;   // the name in the localisation table
    int cell_x = 0;
    int cell_y = 0;
    int cell_z = 0;
};

struct CustomFlag {
    std::string name;
    int cell_x = 0;
    int cell_y = 0;
};


// The designs of the game, read from the running game (empty if it is not
// attached). The names are in mapping/<version>/flags.tsv.
std::vector<Design> designs();

// Hooks the constructor of the flag component so that the flags of the world are
// known. False if the game does not match or the hook fails.
bool watch();

// The flag components that exist (checked: the object still has the vtable of
// the component).
std::vector<Obj> components();

bool get_cell(const Obj& component, int& cell_x, int& cell_y);

// Makes a component show another cell (also a free cell of the atlas).
bool set_cell(const Obj& component, int cell_x, int cell_y);

// Puts a picture of exactly 64 x 32 pixels into the next free cell of the top
// half of the atlas and writes graphics/flags_custom.txtr (the original atlas
// plus every custom flag added so far). Refuses a picture of another size and
// the case that no cell is free.
bool add_flag(const std::string& name, const gfx::Image& picture, std::string& error);

bool use_flag(const std::string& name, int cell_x, int cell_y, std::string& error);

std::vector<CustomFlag> custom_flags();

// Tells the game to load graphics/flags_custom.txtr instead of graphics/flags.txtr.
// Needs at least one registered flag. See the limits above.
bool redirect_atlas(std::string& error);

// Here is the same one but newer (I'm keeping the old one for comatibility) but here you can give it a custom path.
bool redirect_atlas(const std::string& rom_relative_path, std::string& error);

bool show_in_menu(int category, std::string& error);

// The number of entries the menu reads now (162 until show_in_menu).
int menu_entry_count();

// Removes the custom flags from the list and deletes the file this header made.
void clear();

}  // namespace sw::flags
