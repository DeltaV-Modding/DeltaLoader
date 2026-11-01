#pragma once

#include "sw/textures.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace sw::mapicons {

inline constexpr std::uint32_t kRvaMapIconsInit = 0x6a05e0;      // fills the slots
inline constexpr std::uint32_t kRvaMapIconSlot = 0x6a2120;       // (map, slot, name, context, color, scale)
inline constexpr std::uint32_t kRvaAtlasAddSprites = 0x1df3e0;   // (target, ., mm_string* xml path)
inline constexpr const char* kAtlasXml = "graphics/ui/map/atlas_mapicons.xml";
inline constexpr const char* kAtlasTexture = "graphics/ui/map/atlas_mapicons.txtr";
inline constexpr const char* kCustomXml = "graphics/ui/map/atlas_mapicons_custom.xml";
inline constexpr const char* kCustomTexture = "graphics/ui/map/atlas_mapicons_custom.txtr";
inline constexpr int kAtlasSize = 1024;

struct Slot {
    std::uint32_t offset;   // inside the map object
    const char* name;       // a region of the atlas
    std::uint32_t color;    // ARGB
    float scale;
};

inline constexpr Slot kSlots[56] = {
    {0x10, "waypoint", 0xffe6e6e6u, 1.0000f},
    {0x20, "targetcircle", 0xffffffffu, 1.0000f},
    {0x30, "survivoricon", 0xff000064u, 0.6000f},
    {0x40, "fireicon", 0xff000064u, 0.6000f},
    {0x50, "iceicon", 0xff000064u, 0.6000f},
    {0x60, "mysteryicon", 0xff000064u, 0.6000f},
    {0x70, "crateicon", 0xff000064u, 0.6000f},
    {0x80, "playercircle", 0xffe6e6e6u, 1.0000f},
    {0x90, "playercircle", 0xff131516u, 1.0000f},
    {0xa0, "playercircle", 0xff0000dcu, 1.0000f},
    {0xb0, "playercircle", 0xffffffffu, 1.0000f},
    {0xc0, "circle", 0xff131516u, 1.0000f},
    {0xd0, "circle", 0xffe6e6e6u, 1.0000f},
    {0xe0, "circle", 0xff0000dcu, 1.0000f},
    {0xf0, "dashedcircle", 0xff0000dcu, 1.0000f},
    {0x100, "circle", 0xff000064u, 1.0000f},
    {0x110, "circle", 0xff82f43cu, 1.0000f},
    {0x120, "missionmarker", 0xffe6e6e6u, 0.8000f},
    {0x130, "questionmark", 0xffe6e6e6u, 0.8000f},
    {0x140, "questionmark", 0xffe6e6e6u, 0.8000f},
    {0x150, "hangar", 0xff000064u, 0.9000f},
    {0x160, "home", 0xffe6e6e6u, 0.9000f},
    {0x170, "home_purchase", 0xff131516u, 0.6000f},
    {0x180, "network", 0xffe6e6e6u, 0.6000f},
    {0x1a0, "iconcross", 0xffffffffu, 0.5000f},
    {0x1b0, "iconwreckage", 0xffffffffu, 0.5000f},
    {0x1c0, "iconterminal", 0xffffffffu, 0.5000f},
    {0x1d0, "iconmilitary", 0xffffffffu, 0.5000f},
    {0x1e0, "iconheritage", 0xffffffffu, 0.5000f},
    {0x1f0, "iconrig", 0xffffffffu, 0.5000f},
    {0x200, "iconindustrial", 0xffffffffu, 0.5000f},
    {0x210, "iconhospital", 0xffffffffu, 0.5000f},
    {0x220, "iconscience", 0xffffffffu, 0.5000f},
    {0x230, "iconairport", 0xffffffffu, 0.5000f},
    {0x240, "iconcoastguard", 0xffffffffu, 0.5000f},
    {0x250, "iconlighthouse", 0xffffffffu, 0.5000f},
    {0x260, "iconfuel", 0xffffffffu, 0.5000f},
    {0x270, "iconfuel", 0xff82f43cu, 0.5000f},
    {0x280, "hospital_ship", 0xffffffffu, 0.7000f},
    {0x290, "refueler_plane", 0xffffffffu, 0.7000f},
    {0x2a0, "ore", 0xffffffffu, 0.5000f},
    {0x2b0, "ingot", 0xffffffffu, 0.5000f},
    {0x2c0, "fish", 0xffffffffu, 0.5000f},
    {0x2d0, "dollar_sign", 0xffffffffu, 0.5000f},
    {0x330, "cargo_pickup", 0xff000000u, 0.6000f},
    {0x340, "flag1", 0xff000064u, 0.6000f},
    {0x350, "flag2", 0xff000064u, 0.6000f},
    {0x360, "house", 0xff000064u, 0.6000f},
    {0x370, "car", 0xff000064u, 0.6000f},
    {0x380, "plane2", 0xff000064u, 0.6000f},
    {0x390, "tank", 0xff000064u, 0.6000f},
    {0x3a0, "helicopter", 0xff000064u, 0.6000f},
    {0x3b0, "ship2", 0xff000064u, 0.6000f},
    {0x3c0, "small_boat", 0xff000064u, 0.6000f},
    {0x3d0, "attack", 0xff000064u, 0.6000f},
    {0x3e0, "defence", 0xff000064u, 0.6000f},
};

// A region of the atlas, as the XML has it.
struct Region {
    std::string name;
    float l = 0, r = 0, b = 0, t = 0;
};

// The regions of the XML of the game (rom/graphics/ui/map/atlas_mapicons.xml).
bool read_regions(std::vector<Region>& out, std::string& error);

struct CustomIcon {
    std::string name;
    int x = 0, y = 0, width = 0, height = 0;   // in pixels of the atlas
};

// Puts a picture (width and height multiples of 4, at most 256) into the first free place
// of the atlas and writes the two new files. Names must be new (not a region of the game).
bool add_icon(const std::string& name, const gfx::Image& picture, std::string& error);

std::vector<CustomIcon> custom_icons();

// Hooks graphic_atlas_add_sprites so that the game reads the custom description.
bool redirect_atlas(std::string& error);

inline constexpr std::uint32_t kRvaMarkerDraw = 0x6a3a20;        // draws one map object (924 bytes)
inline constexpr std::uint32_t kRvaMarkerSwitchCmp = 0x6a3c57;    // cmp eax, 0x13
inline constexpr std::uint32_t kRvaMarkerTableLoad = 0x6a3c69;    // mov edx, [rcx+rax*4+disp32]
inline constexpr std::uint32_t kRvaMarkerTable = 0x6a3d6c;        // 20 int32 RVAs
inline constexpr std::uint32_t kRvaMarkerCommon = 0x6a3d36;       // the common tail: r8 = slot, call 0x69f800
inline constexpr std::uint32_t kRvaMarkerDefault = 0x6a3d54;      // draws nothing
inline constexpr int kFirstCustomMarkerType = 20;
inline constexpr int kMaxCustomMarkerTypes = 16;

// Adds a marker type that shows the region `region` (of the game or of add_icon) with this
// colour (ARGB) and scale; returns its number (20, 21, ...) or -1 (error says why). The
// first call patches the switch. Call it before the map is drawn.
int add_marker_type(const std::string& region, std::uint32_t argb, float scale, std::string& error);

// The number of marker types added, and how many have a slot in the map now.
int marker_type_count();
int marker_slots_built();

// Forgets the custom icons and deletes the two files.
void clear();

}  // namespace sw::mapicons
