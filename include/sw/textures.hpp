#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sw::gfx {


inline constexpr const char* kFlagAtlasPath = "graphics/flags.txtr";       // below rom/
inline constexpr const char* kFlagShaderVertex = "graphics/shaders/flags.glslv";
inline constexpr const char* kFlagShaderFragment = "graphics/shaders/flags.glslf";
inline constexpr const char* kFlagDefinitions[3] = {
    "data/definitions/flag_small.xml", "data/definitions/flag_medium.xml",
    "data/definitions/flag_large.xml"};
inline constexpr const char* kFlagPoleMeshes[3] = {
    "meshes/component_flagpole_small.mesh", "meshes/component_flagpole_medium.mesh",
    "meshes/component_flagpole_large.mesh"};
inline constexpr int kFlagComponentType = 65;   // type="65" in the three definitions

// The atlas: 1024 x 1024, the top half is empty, the flags are in blocks below.
inline constexpr int kFlagAtlasSize = 1024;
inline constexpr int kFlagAtlasEmptyRows = 512;   // y 0..511 is free
inline constexpr int kFlagCellWidth = 64;         // a flag of the main block
inline constexpr int kFlagCellHeight = 32;

inline constexpr std::uint32_t kRvaTextureLoad = 0x20fa80;
inline constexpr std::uint32_t kRvaGraphicsAssetsInit = 0x6b20c0;
inline constexpr std::size_t kOffFlagTexture = 0x50;


inline constexpr std::uint32_t kVtableFlagComponent = 0xafd938;      // c_scene_vehicle_component_flag
inline constexpr std::size_t kFlagComponentSize = 0x2c8;
inline constexpr std::uint32_t kVtableMaterialFlag = 0xb09540;       // c_material_flag
inline constexpr std::uint32_t kVtableMaterialFlagClip = 0xb4def0;   // c_material_flag_clip
inline constexpr std::uint32_t kVtablePropertyFlag = 0xb724c0;       // c_properties_element_flag
inline constexpr std::uint32_t kVtablePropertyFlag64 = 0xafb9b0;     // c_properties_element_flag_64
inline constexpr std::uint32_t kVtableUiPropertyFlag = 0xb78c88;     // c_ui_properties_element_flag
inline constexpr std::uint32_t kVtableUiPropertyFlag128 = 0xb7ad00;  // c_ui_properties_element_flag_128

// The file format.
inline constexpr std::size_t kTxtrHeaderSize = 23;
inline constexpr std::size_t kTxtrTrailerSize = 2;
inline constexpr std::uint32_t kTxtrVersion = 3;
inline constexpr std::uint32_t kTxtrFormatRgba = 2;


// A picture without a file format: RGBA, row by row from the top.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;   // width * height * 4
};

// A texture file that was read. The header and the trailer are kept as they are,
// so write() produces a file the game can read.
struct Texture {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    std::array<std::uint8_t, kTxtrHeaderSize> header{};
    std::array<std::uint8_t, kTxtrTrailerSize> trailer{};
    std::string source;   // the path it was read from
};

// Reads a TXTR file of version 3, format 2 (RGBA, square).
bool read(const std::string& path, Texture& out, std::string& error);

// Writes a NEW file. Refuses the path the texture was read from.
bool write(const std::string& path, const Texture& texture, std::string& error);

// Is the rectangle empty (every pixel has alpha 0)?
bool is_empty(const Texture& texture, int x, int y, int width, int height);

// The first empty place of that size, searched row by row in steps of the
// size. False if there is none.
bool find_free(const Texture& texture, int width, int height, int& x, int& y);

// The empty places of that size (the grid of cells width x height).
std::vector<std::pair<int, int>> free_cells(const Texture& texture, int width, int height);

// Puts a picture into the texture. Refuses (false and an error) if it does not
// fit or if one pixel of the place is not empty: nothing that is there is
// replaced. Fully transparent pixels of the picture are written with alpha 1,
// because alpha 0 means "empty".
bool put(Texture& texture, const Image& image, int x, int y, std::string& error);

// Cuts a rectangle out of a texture (for example one flag).
bool crop(const Texture& texture, int x, int y, int width, int height, Image& out);

struct TextureFile {
    std::uint32_t format = 0;
    bool flag = false;
    int width = 0;
    int height = 0;
    std::vector<std::vector<std::uint8_t>> levels;   // as stored
    std::array<std::uint8_t, 2> trailer{};
    std::string source;
};

inline constexpr std::uint32_t kTxtrFormatBc3 = 3;

// Reads any TXTR file of version 3 (all levels). Checks that the levels use the
// whole file.
bool read_file(const std::string& path, TextureFile& out, std::string& error);

// Writes a NEW file; refuses the file that was read.
bool write_file(const std::string& path, const TextureFile& file, std::string& error);

// Decodes one level (0 = the largest) to RGBA. Format 2 is copied, format 3 is
// decoded as BC3. Other formats are refused.
bool decode(const TextureFile& file, int level, Image& out, std::string& error);

// BC3 / DXT5 to RGBA (width and height in pixels).
bool decode_bc3(const std::vector<std::uint8_t>& data, int width, int height, Image& out);

bool encode_bc3(const Image& image, std::vector<std::uint8_t>& out);


bool make_texture(const Image& image, std::uint32_t format, TextureFile& out, std::string& error);

}  // namespace sw::gfx
