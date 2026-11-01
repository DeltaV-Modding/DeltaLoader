#pragma once

#include "sw/generated_audio.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sw::audio {

// The entry of the table for this name ("alSourcePlay", "alcOpenDevice"), or nullptr.
const AlEntry* al_entry(const char* name);

// The function pointer the game holds for this name (the import table entry of OpenAL64.dll), or
// nullptr when the name is unknown or the runtime is not attached.
void* al_function(const char* name);

// How many places of the engine call this function (0 = imported but never used).
int al_uses(const char* name);

// Hooks al_engine_update (0x1ac120, remembers the engine object) and al_engine_play_sound (0x1ac300).
// Checks the opcodes of both first. False if the game does not match or a hook fails.
bool watch();

// Removes both hooks and all callbacks.
void unwatch();

// An update was seen in the last second.
bool ready();

// The audio engine object, or nullptr before the first update.
void* engine();

// A sound is played on a pool source (the call of 0x1ac300, before it runs).
struct PlayEvent {
    void* engine;
    std::uint32_t buffer;   // the AL buffer id of the sound (0 when the reference could not be read)
    bool loop;              // the fourth argument (the gain is a float in xmm2 and is NOT readable by a hook)
};
using PlayCallback = void (*)(const PlayEvent& event, void* user);

// Adds a callback. False before watch(), or when the same callback/user pair is already there.
// The callback runs on the thread of the engine: be fast, do not throw.
bool on_play(PlayCallback callback, void* user);
void remove_callbacks();

// The number of calls of the play function seen since watch(), and of updates.
std::uint64_t plays();
std::uint64_t updates();

inline constexpr int kPoolSources = 24;

struct SourceInfo {
    int index;                 // 0..23
    std::uint32_t source;      // the ALuint of the source
    std::uint32_t buffer;      // the ALuint of the buffer it plays, 0 if none
    float gain;                // the base gain (before the master gain)
    bool in_use;
};

// The 24 pool sources, read from the engine. Empty before the first update or when a read fails.
std::vector<SourceInfo> sources();

// The number of pool sources in use.
int sources_in_use();

struct ListenerPosition {
    float x, y, z;
};
// The position the engine uses for its emitters (engine + 0x538), written by the frame function
// right before alListener3f. False before the first update. The ORIENTATION is not stored: it is
// only passed to alListenerfv.
bool listener(ListenerPosition& out);

// The four group gains of the engine (all 1.0f after construction). False before the first update.
struct GroupGains {
    float pool, emitters, streams, group4;
};
bool group_gains(GroupGains& out);

enum class Mood : std::int32_t { None = 0, MainMenu = 1, Normal = 2, MissionMid = 3, MissionHigh = 4 };
// The mood value (state + 0xa0) and the track kind that plays (state + 0x4c; 5 = nothing). False when
// the state is not initialised (its first dword must be 60) or the value is out of range.
bool music_mood(Mood& mood, int* playing_kind = nullptr);
// "none", "main_menu", "mood_normal", "mood_mission_mid", "mood_mission_high", or nullptr.
const char* mood_name(int value);

// The sound paths in the program ("audio/fx/wind_outdoor_medium.ogg"), 528 of them, and a search.
std::size_t sound_count();
const SoundPath* sound_at(std::size_t index);
// The first path that contains `part` (case sensitive), or nullptr.
const SoundPath* find_sound(const char* part);

// The cache of a sound library (library + 0x2f70): every sound loaded by name so far.
struct CachedSound {
    std::string name;          // "audio/fx/...ogg"
    std::uint32_t buffer;      // the AL buffer id, 0 if it could not be read
};
// `library` is a pointer to a sound library object: the first argument of 0x27e4b0 (hook it, or read
// scene + 0x17e8 of the game scene). All reads are checked; empty on any mismatch.
std::vector<CachedSound> library_cache(const void* library);
// The AL buffer id loaded for this exact path, or 0.
std::uint32_t library_buffer(const void* library, const char* path);

inline constexpr std::uint32_t kRvaEngineInit = 0x1abbd0;        // (engine)
inline constexpr std::uint32_t kRvaEngineShutdown = 0x1abf80;
inline constexpr std::uint32_t kRvaEngineUpdate = 0x1ac120;      // (engine, ...) once per frame
inline constexpr std::uint32_t kRvaEnginePlaySound = 0x1ac300;   // (engine, buffer_ref*, gain in xmm2, loop, out handle)
inline constexpr std::uint32_t kRvaEngineMusicStart = 0x1ac4f0;
inline constexpr std::uint32_t kRvaEngineEmitterCreate = 0x1ac6a0;
inline constexpr std::uint32_t kRvaSourceStart = 0x1a9aa0;
inline constexpr std::uint32_t kRvaBufferLoadFile = 0x1a9550;    // .ogg / .wav
inline constexpr std::uint32_t kRvaBufferFromPcm16 = 0x1a9890;
inline constexpr std::uint32_t kRvaBufferRefRelease = 0x1a9a10;
inline constexpr std::uint32_t kRvaVoiceQueuePcm = 0x1aa090;     // called by the voice_message handler
inline constexpr std::uint32_t kRvaVoiceSetGain = 0x1aa1b0;
inline constexpr std::uint32_t kRvaEffectSlotCreate = 0x1aba70;
inline constexpr std::uint32_t kRvaListenerUpdate = 0x7425a0;    // the only caller of alListener3f / alListenerfv
inline constexpr std::uint32_t kRvaSoundLibraryRegisterAll = 0x27e980;
inline constexpr std::uint32_t kRvaSoundLibraryLoad = 0x27e4b0;
inline constexpr std::uint32_t kRvaSetAudioMoodCase = 0x8602cc;  // case of s2c 174 in the client switch 0x85f330
inline constexpr std::size_t kOffOwnerEngine = 0x2c8;            // engine = owner + 0x2c8
inline constexpr std::uint32_t kRvaMusicState = 0xd056e0;      // the global music state (made by 0x289090)
inline constexpr std::uint32_t kRvaMusicMoodUpdate = 0x289280;   // the only reader of the mood
inline constexpr std::uint32_t kRvaMusicSwitch = 0x2895e0;       // (music, platform, track kind, fade, flag) -> 0x1ac4f0
inline constexpr std::size_t kOffEngineEmitterGain = 0x3d8;
inline constexpr std::size_t kOffEngineStreamGain = 0x420;
inline constexpr std::size_t kOffEngineGroup4Gain = 0x478;
inline constexpr std::size_t kOffEngineGroup4Sources = 0x480;
inline constexpr std::size_t kOffLibraryCache = 0x2f70;          // ptr; +8 capacity, +0xc start, +0x10 count; entries 0x18
inline constexpr std::size_t kOffMusicPlayingKind = 0x4c;
inline constexpr std::size_t kOffMoodOwner = 0x138a0;            // [client + 0x138a0] + 0xa0 = the mood value
inline constexpr std::size_t kOffMoodValue = 0xa0;

inline constexpr std::size_t kOffEngineDevice = 0x0;
inline constexpr std::size_t kOffEngineContext = 0x8;
inline constexpr std::size_t kOffEngineMasterGain = 0x10;        // float, read through slot + 0x10
inline constexpr std::size_t kOffEnginePool = 0x18;
inline constexpr std::size_t kPoolSlotSize = 0x28;
inline constexpr std::size_t kOffEngineEmitterManager = 0x3d8;
inline constexpr std::size_t kOffEngineStreamManager = 0x420;
inline constexpr std::size_t kOffEngineListenerPosition = 0x538;
inline constexpr std::size_t kOffEngineLastUpdate = 0x548;
inline constexpr std::size_t kOffEngineLock = 0x580;
inline constexpr std::size_t kOffSlotSource = 0x0;
inline constexpr std::size_t kOffSlotGain = 0x4;
inline constexpr std::size_t kOffSlotBufferRef = 0x8;
inline constexpr std::size_t kOffSlotMasterGain = 0x10;
inline constexpr std::size_t kOffSlotHandle = 0x18;
inline constexpr std::size_t kOffSlotInUse = 0x20;
inline constexpr std::size_t kOffBufferId = 0x0;
inline constexpr std::size_t kOffBufferSamples = 0x4;
inline constexpr std::size_t kOffBufferFrequency = 0x8;
inline constexpr std::size_t kOffBufferBytes = 0xc;
inline constexpr std::size_t kOffBufferCreated = 0x10;
inline constexpr std::size_t kBufferObjectSize = 0x14;

}  // namespace sw::audio
