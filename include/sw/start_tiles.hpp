#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sw::start_tiles {


inline constexpr int kOptionCount = 3;


inline constexpr std::uint32_t kRvaMenuVtable = 0xb78020;
inline constexpr std::uint32_t kRvaMenuTick = 0x6ecac0;       // slot 3, every frame
inline constexpr std::size_t kOffMenuDescription[kOptionCount] = {0x160, 0x150, 0x140};
inline constexpr std::size_t kOffMenuSelector = 0xa8;          // the selector of the start
inline constexpr std::size_t kOffMenuMultiplayerFlag = 0x139;  // byte: use the multiplayer base

inline constexpr std::uint32_t kLocDescription[kOptionCount] = {0x1c8, 0x1c9, 0x1ca};
inline constexpr std::uint32_t kLocLocationTitle = 0x388;      // title of the location pane

inline constexpr std::uint32_t kRvaStartStruct[kOptionCount] = {0xceedf0, 0xcf4f80, 0xcf31e0};
inline constexpr std::uint32_t kRvaStartStructMultiplayer = 0xceee60;  // replaces the third
inline constexpr std::size_t kOffTile = 0x10;
inline constexpr std::size_t kOffAridTile = 0x20;

inline constexpr const char* kDefaultTile[kOptionCount] = {
    "data/tiles/island12.xml", "data/tiles/island12.xml", "data/tiles/island15.xml"};
inline constexpr const char* kDefaultAridTile[kOptionCount] = {
    "data/tiles/arid_island_5_14.xml", "data/tiles/arid_island_5_14.xml",
    "data/tiles/arid_island_26_14.xml"};
inline constexpr const char* kMultiplayerTile = "data/tiles/island_43_multiplayer_base.xml";

inline constexpr std::uint32_t kRvaBuildUi = 0x83a7b0;      // main_menu_state::build_ui
inline constexpr std::uint32_t kRvaAddBase = 0x847c40;      // (state, ctx, title*, key*, description*)
inline constexpr std::size_t kOffStateInterface = 0xbdbb0;  // state -> the menu interface
inline constexpr std::size_t kOffInterfaceNewGame = 0x78;   // interface -> new game menu
inline constexpr std::size_t kOffInterfaceLocations = 0x80; // interface -> location selector
inline constexpr std::size_t kOffMenuLocations = 0xc0;      // new game menu: ring of descriptors
// The ring: {pointer, capacity +8, start +0xc, count +0x10}, entries are
// pointers to a descriptor of 0x40 bytes:
inline constexpr std::size_t kOffDescTile = 0x00;           // mm_string "data/tiles/<key>.xml"
inline constexpr std::size_t kOffDescGeometry = 0x10;       // mm_string "data/tiles/<key>_map_geometry.bin"
inline constexpr std::size_t kOffDescTitle = 0x20;          // mm_string, the title of the card
inline constexpr std::size_t kOffDescDescription = 0x30;    // mm_string, the text under the picture
inline constexpr std::size_t kDescSize = 0x40;


struct Base {
    std::string key;          // the tile file without folder and extension
    std::string title;
    std::string description;
    // The key of a tile whose _map_geometry.bin is copied when yours is
    // missing (the picture of the card). Empty = "moon_surface_0_0".
    std::string geometry_template;

    // Where the tile is put into the world when the player starts a game with
    // this card (see "the spawn" below). By default the mod searches the
    // nearest free grid cell around the origin; set these to choose the cell.
    bool fixed_cell = false;
    int grid_x = 0;
    int grid_y = 0;
};

// What the menu has now.
struct BaseInfo {
    std::string tile;         // data/tiles/<key>.xml
    std::string geometry;     // data/tiles/<key>_map_geometry.bin
    std::string title;
    std::string description;
};


// One start of the new game menu as it is now.
struct Option {
    int index = 0;
    std::string description;  // empty while the menu is closed
    std::string tile;
    std::string arid_tile;
};

// Your own start.
struct Custom {
    std::string title;        // written in front of the description
    std::string description;
    std::string tile;         // empty = keep the tile of the game
    std::string arid_tile;    // empty = keep the arid tile of the game
};


// Hooks the menu. False if the game does not match the mapping or the hook
// fails.
bool watch();

// Adds a card to the page "Choose base". It is made when the page is built
// (the game builds it when the main menu opens; open the menu again after the
// mod started). False and an error if the tile file is missing, no picture
// can be made for it, or a text is too long. Needs watch().
bool add_base(const Base& base, std::string& error);

// Forgets the cards that are waiting (a card already in the menu stays).
void remove_bases();

// The cards that the menu has now (empty while it was not built).
std::vector<BaseInfo> bases();

// What the mod did at the last start of a game: the tile of the chosen card
// and where it was put (empty if the chosen start is not one of the mod's
// cards).
std::string last_start();

// The new game menu is open (it was seen in its last frame).
bool menu_open();

// The three starts as they are now.
std::vector<Option> options();

// Replaces the start `option` (0..2) with your own. The description is
// written into the menu while it is open (and again whenever it is opened).
bool define(int option, const Custom& custom);

// The single parts.
bool set_description(int option, std::string_view text);
bool set_tile(int option, std::string_view file);
bool set_arid_tile(int option, std::string_view file);

// Puts one start back (option 0..2) or all of them (-1).
void clear(int option = -1);

}  // namespace sw::start_tiles
