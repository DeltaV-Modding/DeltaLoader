#pragma once

#include <cstdint>

namespace sw::weather {

// Starts watching. False if the game does not match the mapping or a hook cannot be set.
bool watch();

// E has been seen and can be read.
bool ready();

struct Weather {
    float fog = 0.f;
    float rain = 0.f;
    float wind = 0.f;
};

// The values server.setWeather wrote (E+0x94 / 0x9c / 0xa4).
bool set_values(Weather& out);

// The values the sampler uses while override_weather is on (E+0x90 / 0x98 / 0xa0); they step 1 percent per tick towards the set values.
bool current_values(Weather& out);

// Writes fog, rain, wind like server.setWeather (E+0x94 / 0x9c / 0xa4). Local only.
bool set_weather(float fog, float rain, float wind);

// Writes the "current" fields (E+0x90 / 0x98 / 0xa0); what the sampler uses at once when override_weather is on.
bool set_current(float fog, float rain, float wind);

// The wind direction in radians (E+0xa8).
bool wind_direction(float& radians);
bool set_wind_direction(float radians);

// What server.getWeather returns at a position (calls the game's sampler; world coordinates in metres).
// See snow() and temperature_celsius() below for the other Lua values.
struct Sample {
    float temp_raw = 0.f;     // out[0] of the sampler (Lua key "temp"): a 0..1 warmth value, NOT Celsius
    float rain = 0.f;         // out[1]
    float fog = 0.f;          // out[2]
    float wind_angle = 0.f;   // out[3], radians
    float wind = 0.f;         // out[4]
    float wind_x = 0.f;       // out[5..7], the wind vector
    float wind_y = 0.f;
    float wind_z = 0.f;
};
bool sample(double x, double y, double z, Sample& out);

// The time of day. ticks counts from the start of the day; a day has day_length * 3600 ticks, with day_length
// from the game setting of that name.
struct TimeOfDay {
    std::uint32_t ticks = 0;
    std::uint32_t ticks_per_day = 0;
    float fraction = 0.f;          // Lua getTime "percent"
    int hour = 0;                  // Lua getTime "hour"
    int minute = 0;                // Lua getTime "minute"
    float daylight_factor = 0.f;   // Lua getTime "daylight_factor": sin(2 pi fraction - pi/2) * 0.5 + 0.5
};
bool time_of_day(TimeOfDay& out);
bool set_time_ticks(std::uint32_t ticks);
bool set_time_fraction(float fraction);   // 0..1, like the packet c2s 49

// The wave event block that spawnTsunami and the rogue wave fill. All fields are read only.
struct WaveEvent {
    float x = 0.f;
    float z = 0.f;
    float progress = 0.f;     // medium
    float magnitude = 0.f;    // clamped to 0.03 .. 1 by spawnTsunami
    std::int32_t timer = 0;   // 18000 when spawned by spawnTsunami, 0 after a cancel; medium
    std::int32_t state = 0;   // 2 = idle (after a cancel); 0 = spawned; medium
    bool idle = true;         // state == 2
};
bool wave_event(WaveEvent& out);

// ---- follow-up: temperature, snow, noise inputs, override flags, tornadoes ---------------------------------------

// Temperature in Celsius at a world position (the game's function 0x2c3be0: noise, rain and wind dependent, minus 6.5 per km
// of altitude y, never below -270). False when E has not been seen.
bool temperature_celsius(double x, double y, double z, float& out);
// What server.getWeather returns as "snow": clamp(-temperature, 0, 5) / 5.
bool snow(double x, double y, double z, float& out);

// The three inputs the tick recomputes every call (a write is lost at the next tick).
struct NoiseInputs {
    float phase = 0.f;      // E+0xac, 0..1, scrolls the noise
    float season = 0.f;     // E+0xb0, 0..1, an 11 day cycle
    float day = 0.f;        // E+0xb4, 0 at ticks 0, 1 at half a day
};
bool noise_inputs(NoiseInputs& out);

// The override flags: game settings +0x3f / +0x40 / +0x41. The packets call the inverse "stepping".
// override_weather on means the current values step 1 percent per tick towards the set values.
struct Overrides {
    bool weather = false;
    bool time = false;
    bool wind_direction = false;   // the sampler then returns the wind direction E+0xa8
};
bool overrides(Overrides& out);

// One tornado of the ring at E+0x78. Positions are world coordinates in metres.
struct Tornado {
    double base[3] = {};          // base_pos
    double top[3] = {};           // top_pos (moves with the wind)
    float radius_inner = 0.f;     // base_radius_inner, default 10
    float radius_outer = 0.f;     // base_radius_outer, default 30
    bool active = false;          // is_active
    float active_factor = 0.f;    // 0..1, fades in and out by 0.0005 per tick
    float lifespan = 0.f;         // default 180, minus 0.016 per tick once fully faded in
    float intensity = 0.f;
    float influence_radius = 0.f; // default 3000 m: rain and wind are raised inside it
};
// Number of entries (active or fading out). The game has no limit; the ring grows.
bool tornado_count(std::uint32_t& out);
// Entry i (0 = oldest). False when i is out of range or the ring does not look sane.
bool tornado(std::uint32_t i, Tornado& out);

}  // namespace sw::weather
