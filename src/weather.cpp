#include "sw/weather.hpp"

#include "sw/generated_weather.hpp"

#if defined(_WIN64)

#include "sw/hook.hpp"
#include "sw/runtime.hpp"
#include "sw/settings.hpp"

#include <atomic>
#include <cmath>
#include <cstring>
#include <string>

namespace sw::weather {

namespace {

namespace m = sw::weather::map;

std::atomic<std::uintptr_t> g_env{0};
bool g_watching = false;

constexpr std::uintptr_t kLow = m::kWaveEventBelowE;
constexpr std::uintptr_t kHigh = 0xe0;

void on_sample(void*, void*) {
    g_env.store(static_cast<std::uintptr_t>(sw::Hook::arg(0)), std::memory_order_relaxed);
}

std::uintptr_t env() {
    const std::uintptr_t e = g_env.load(std::memory_order_relaxed);
    if (!e || e < kLow) return 0;
    if (!sw::Runtime::get().readable(reinterpret_cast<const void*>(e - kLow), kLow + kHigh)) return 0;
    return e;
}

template <typename T>
bool rd(std::uintptr_t e, std::uintptr_t offset, T& out) {
    if (!e) return false;
    std::memcpy(&out, reinterpret_cast<const void*>(e + offset), sizeof(T));
    return true;
}

template <typename T>
bool wr(std::uintptr_t e, std::uintptr_t offset, const T& value) {
    if (!e) return false;
    return sw::Runtime::get().write_bytes(reinterpret_cast<void*>(e + offset), &value, sizeof(T));
}

bool finite(float v) { return std::isfinite(v); }

bool ticks_per_day(std::uint32_t& out) {
    double day = 0;
    if (!sw::settings::get_number("day_length", day)) return false;
    if (!(day >= 1.0) || day > 100000.0) return false;
    out = static_cast<std::uint32_t>(day) * m::kTicksPerDayUnit;
    return out != 0;
}

}  // namespace

bool watch() {
    if (g_watching) return true;
    auto& rt = sw::Runtime::get();
    if (!rt.attached()) return false;
    std::string error;
    if (rt.hook_rva(m::kRvaSample, &on_sample, nullptr, error) != sw::HookStatus::ok) return false;
    // the second sampler is only a second chance to see E; a failure here is not fatal
    rt.hook_rva(m::kRvaHeightLike, &on_sample, nullptr, error);
    g_watching = true;
    return true;
}

bool ready() { return env() != 0; }

bool set_values(Weather& out) {
    const auto e = env();
    return rd(e, m::kOffFogSet, out.fog) && rd(e, m::kOffRainSet, out.rain) && rd(e, m::kOffWindSet, out.wind);
}

bool current_values(Weather& out) {
    const auto e = env();
    return rd(e, m::kOffFogCurrent, out.fog) && rd(e, m::kOffRainCurrent, out.rain) &&
           rd(e, m::kOffWindCurrent, out.wind);
}

bool set_weather(float fog, float rain, float wind) {
    if (!finite(fog) || !finite(rain) || !finite(wind)) return false;
    const auto e = env();
    return wr(e, m::kOffFogSet, fog) && wr(e, m::kOffRainSet, rain) && wr(e, m::kOffWindSet, wind);
}

bool set_current(float fog, float rain, float wind) {
    if (!finite(fog) || !finite(rain) || !finite(wind)) return false;
    const auto e = env();
    return wr(e, m::kOffFogCurrent, fog) && wr(e, m::kOffRainCurrent, rain) && wr(e, m::kOffWindCurrent, wind);
}

bool wind_direction(float& radians) { return rd(env(), m::kOffWindDirection, radians); }

bool set_wind_direction(float radians) {
    if (!finite(radians)) return false;
    return wr(env(), m::kOffWindDirection, radians);
}

bool sample(double x, double y, double z, Sample& out) {
    const auto e = env();
    if (!e) return false;
    const void* fn = sw::Runtime::get().at_rva(m::kRvaSample);
    if (!fn) return false;
    using Fn = void (*)(std::uintptr_t, float*, const double*, char);
    float buf[m::kSampleOutFloats] = {};
    const double pos[3] = {x, y, z};
    reinterpret_cast<Fn>(const_cast<void*>(fn))(e, buf, pos, 1);
    out.temp_raw = buf[0];
    out.rain = buf[1];
    out.fog = buf[2];
    out.wind_angle = buf[3];
    out.wind = buf[4];
    out.wind_x = buf[5];
    out.wind_y = buf[6];
    out.wind_z = buf[7];
    return true;
}

bool temperature_celsius(double x, double y, double z, float& out) {
    const auto e = env();
    if (!e) return false;
    const void* fn = sw::Runtime::get().at_rva(m::kRvaTemperature);
    if (!fn) return false;
    using Fn = float (*)(std::uintptr_t, const double*, char);
    const double pos[3] = {x, y, z};
    const float t = reinterpret_cast<Fn>(const_cast<void*>(fn))(e, pos, 0);
    if (!finite(t)) return false;
    out = t;
    return true;
}

bool snow(double x, double y, double z, float& out) {
    float t = 0.f;
    if (!temperature_celsius(x, y, z, t)) return false;
    float v = -t;
    v = v < 0.f ? 0.f : (v > 5.f ? 5.f : v);
    out = v / 5.f;
    return true;
}

bool noise_inputs(NoiseInputs& out) {
    const auto e = env();
    return rd(e, m::kOffNoisePhase, out.phase) && rd(e, m::kOffSeason, out.season) && rd(e, m::kOffDayFactor, out.day);
}

bool overrides(Overrides& out) {
    const void* fn = sw::Runtime::get().at_rva(m::kRvaGameSettings);
    if (!fn) return false;
    using Fn = const unsigned char* (*)();
    const unsigned char* st = reinterpret_cast<Fn>(const_cast<void*>(fn))();
    if (!st || !sw::Runtime::get().readable(st, 0x42)) return false;
    out.weather = st[m::kSettingOverrideWeather] != 0;
    out.time = st[m::kSettingOverrideTime] != 0;
    out.wind_direction = st[m::kSettingOverrideWindDirection] != 0;
    return true;
}

namespace {
bool ring(std::uintptr_t e, std::uintptr_t& data, std::uint32_t& cap, std::uint32_t& start, std::uint32_t& count) {
    if (!e) return false;
    std::uint64_t d = 0;
    if (!rd(e, m::kOffTornadoData, d) || !rd(e, m::kOffTornadoCap, cap) || !rd(e, m::kOffTornadoStart, start) ||
        !rd(e, m::kOffTornadoCount, count))
        return false;
    data = 0;
    if (count == 0) return true;
    if (!d || cap == 0 || cap > (1u << 24) || count > cap || start >= cap) return false;
    data = static_cast<std::uintptr_t>(d);
    return sw::Runtime::get().readable(reinterpret_cast<const void*>(data), static_cast<std::size_t>(cap) * m::kTornadoStride);
}
}  // namespace

bool tornado_count(std::uint32_t& out) {
    std::uintptr_t d = 0;
    std::uint32_t cap = 0, start = 0, count = 0;
    if (!ring(env(), d, cap, start, count)) return false;
    out = count;
    return true;
}

bool tornado(std::uint32_t i, Tornado& out) {
    std::uintptr_t d = 0;
    std::uint32_t cap = 0, start = 0, count = 0;
    if (!ring(env(), d, cap, start, count) || i >= count) return false;
    const std::uintptr_t entry = d + static_cast<std::uintptr_t>((static_cast<std::uint64_t>(start) + i) % cap) * m::kTornadoStride;
    std::uint8_t active = 0;
    for (int k = 0; k < 3; ++k)
        if (!rd(entry, m::kTornBasePos + 8 * k, out.base[k]) || !rd(entry, m::kTornTopPos + 8 * k, out.top[k])) return false;
    if (!rd(entry, m::kTornRadiusInner, out.radius_inner) || !rd(entry, m::kTornRadiusOuter, out.radius_outer) ||
        !rd(entry, m::kTornIsActive, active) || !rd(entry, m::kTornActiveFactor, out.active_factor) ||
        !rd(entry, m::kTornLifespan, out.lifespan) || !rd(entry, m::kTornIntensity, out.intensity) ||
        !rd(entry, m::kTornInfluenceRadius, out.influence_radius))
        return false;
    out.active = active != 0;
    return true;
}

bool time_of_day(TimeOfDay& out) {
    const auto e = env();
    std::uint32_t ticks = 0, per_day = 0;
    if (!rd(e, m::kOffTimeTicks, ticks) || !ticks_per_day(per_day)) return false;
    out.ticks = ticks;
    out.ticks_per_day = per_day;
    out.fraction = static_cast<float>(ticks) / static_cast<float>(per_day);
    const float hours = out.fraction * 24.0f;
    out.hour = static_cast<int>(std::floor(hours));
    out.minute = static_cast<int>(std::fmod(hours, 1.0f) * 60.0f);
    out.daylight_factor = std::sin(out.fraction * 6.2831855f - 1.5707964f) * 0.5f + 0.5f;
    return true;
}

bool set_time_ticks(std::uint32_t ticks) {
    std::uint32_t per_day = 0;
    if (!ticks_per_day(per_day) || ticks >= per_day) return false;
    return wr(env(), m::kOffTimeTicks, ticks);
}

bool set_time_fraction(float fraction) {
    std::uint32_t per_day = 0;
    if (!finite(fraction) || fraction < 0.f || fraction >= 1.f || !ticks_per_day(per_day)) return false;
    return set_time_ticks(static_cast<std::uint32_t>(fraction * static_cast<float>(per_day)));
}

bool wave_event(WaveEvent& out) {
    const auto e = env();
    if (!e) return false;
    const std::uintptr_t g = e - m::kWaveEventBelowE;
    if (!rd(g, m::kWaveEventX, out.x) || !rd(g, m::kWaveEventZ, out.z) ||
        !rd(g, m::kWaveEventProgress, out.progress) || !rd(g, m::kWaveEventMagnitude, out.magnitude) ||
        !rd(g, m::kWaveEventTimer, out.timer) || !rd(g, m::kWaveEventState, out.state))
        return false;
    out.idle = out.state == static_cast<std::int32_t>(m::kWaveEventStateIdle);
    return true;
}

}  // namespace sw::weather

#else  // x86: nothing is offered

namespace sw::weather {
bool watch() { return false; }
bool ready() { return false; }
bool set_values(Weather&) { return false; }
bool current_values(Weather&) { return false; }
bool set_weather(float, float, float) { return false; }
bool set_current(float, float, float) { return false; }
bool wind_direction(float&) { return false; }
bool set_wind_direction(float) { return false; }
bool sample(double, double, double, Sample&) { return false; }
bool time_of_day(TimeOfDay&) { return false; }
bool set_time_ticks(std::uint32_t) { return false; }
bool set_time_fraction(float) { return false; }
bool wave_event(WaveEvent&) { return false; }
bool temperature_celsius(double, double, double, float&) { return false; }
bool snow(double, double, double, float&) { return false; }
bool noise_inputs(NoiseInputs&) { return false; }
bool overrides(Overrides&) { return false; }
bool tornado_count(std::uint32_t&) { return false; }
bool tornado(std::uint32_t, Tornado&) { return false; }
}  // namespace sw::weather

#endif
