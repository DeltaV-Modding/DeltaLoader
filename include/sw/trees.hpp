#pragma once

#include <cstddef>
#include <cstdint>

namespace sw::trees {

enum class Kind : int { fir = 0, burnt = 1, snow = 2 };

inline constexpr std::uint32_t kRvaDrawFrame = 0x6d4de0;      // renderer_draw_frame
inline constexpr std::uint32_t kRvaSiteFoliage = 0x6d701c;    // the first place (shader object at renderer + 0x6c0)
inline constexpr std::uint32_t kRvaSiteSecond = 0x6d7848;     // the second place (+ 0x760)
inline constexpr std::size_t kSiteBytes = 35;                 // movsxd .. add rax, 0x1c0
inline constexpr std::uint32_t kRvaMeshLibraryLoader = 0x6b20c0;
inline constexpr std::size_t kOffElementKind = 0xf8;          // int
inline constexpr std::size_t kOffElementLod = 0xfc;           // int
inline constexpr std::size_t kOffElementChunkX = 0x08;        // int, taken to be the chunk index (500 m)
inline constexpr std::size_t kOffElementChunkZ = 0x0c;        // int
inline constexpr std::uintptr_t kLibrarySlotsOffset = 0x1c0;  // the nine mesh slots, 8 bytes each

// The slot of the game's formula, relative to the library: 0x1c0 + (kind * 3 + lod) * 8. lod 0..2.
constexpr std::uintptr_t slot_offset(Kind kind, int lod) {
    return kLibrarySlotsOffset + (static_cast<std::uintptr_t>(kind) * 3 + static_cast<std::uintptr_t>(lod)) * 8;
}

// The tile (grid cell) that holds a chunk index; floor((c + 1) / 2), also for negative numbers.
constexpr int tile_of_chunk(int chunk) {
    const int n = chunk + 1;
    return n >= 0 ? n / 2 : -((-n + 1) / 2);
}

// Sets the kind of a tile / a chunk. False for a kind that is not one of the three.
bool set_tile(int gx, int gz, Kind kind);
bool set_chunk(int cx, int cz, Kind kind);
// Removes a rule / all rules.
bool clear_tile(int gx, int gz);
bool clear_chunk(int cx, int cz);
void clear_rules();
std::size_t rule_count();

// The kind a chunk is drawn with, given the kind the game asks for. This is what the stub calls.
Kind resolve(Kind original, int cx, int cz);

// Checks the 35 bytes at both places (and the instruction after them), then patches both. False when the game
// does not match, the runtime is not attached, a stub cannot be placed within 2 GB of the image, or this is
// the 32 bit build. Calling it again after a success returns true.
bool watch();
// Puts the original bytes back. The stubs stay allocated, so a draw that is just inside one can finish.
void unwatch();
bool active();

struct ChunkSeen {
    int cx = 0, cz = 0;
    int kind = 0, lod = 0;     // as the game asked
    int drawn_kind = 0;        // as it was drawn
};
// Copies up to max of the first distinct chunks that were drawn (at most 64); returns how many.
std::size_t chunk_seen(ChunkSeen* out, std::size_t max);

// The first draw elements the stub saw (each element once, by address): the first 0x100 bytes as 32 quadwords. The ints at
// +0x08 / +0x0c turned out to be 0 for every element (a player's log), so they are NOT the chunk index; this is for finding where
// the position of the chunk is stored.
struct ElementSample {
    std::uintptr_t address = 0;
    std::int32_t kind = 0, lod = 0;
    std::uint64_t q[32] = {};
};
std::size_t element_samples(ElementSample* out, std::size_t max);   // at most 24

}  // namespace sw::trees
