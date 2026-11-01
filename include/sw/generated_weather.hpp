// This was generated.
#pragma once
#include <cstdint>
namespace sw::weather::map {
inline constexpr std::uint32_t kRvaSetWeather = 0x2c3910;
inline constexpr std::uint32_t kRvaSetWindDirection = 0x2c39f0;
inline constexpr std::uint32_t kRvaSample = 0x2c3d40;
inline constexpr std::uint32_t kRvaHeightLike = 0x2c3be0;
inline constexpr std::uint32_t kRvaTimeSetTicks = 0x141260;
inline constexpr std::uint32_t kRvaTimeGetTicks = 0x141270;
inline constexpr std::uint32_t kRvaVtableScriptOperators = 0xb1c328;
inline constexpr std::uint32_t kSlot53 = 53;
inline constexpr std::uint32_t kSlot57 = 57;
inline constexpr std::uint32_t kSlot135 = 135;
inline constexpr std::uint32_t kSlot136 = 136;
inline constexpr std::uint32_t kSlot137 = 137;
inline constexpr std::uint32_t kOffFogSet = 0x94;
inline constexpr std::uint32_t kOffRainSet = 0x9c;
inline constexpr std::uint32_t kOffWindSet = 0xa4;
inline constexpr std::uint32_t kOffFogCurrent = 0x90;
inline constexpr std::uint32_t kOffRainCurrent = 0x98;
inline constexpr std::uint32_t kOffWindCurrent = 0xa0;
inline constexpr std::uint32_t kOffWindDirection = 0xa8;
inline constexpr std::uint32_t kOffTimeObject = 0xd0;
inline constexpr std::uint32_t kOffTimeTicks = 0xd8;
inline constexpr std::uint32_t kWaveEventBelowE = 0x98c;
inline constexpr std::uint32_t kWaveEventX = 0;
inline constexpr std::uint32_t kWaveEventZ = 4;
inline constexpr std::uint32_t kWaveEventProgress = 8;
inline constexpr std::uint32_t kWaveEventMagnitude = 0xc;
inline constexpr std::uint32_t kWaveEventTimer = 0x14;
inline constexpr std::uint32_t kWaveEventState = 0x18;
inline constexpr std::uint32_t kTicksPerDayUnit = 3600;
inline constexpr std::uint32_t kWaveEventStateIdle = 2;
inline constexpr std::uint32_t kSampleOutFloats = 8;
inline constexpr std::uint32_t kRvaTick = 0x2c2d20;
inline constexpr std::uint32_t kRvaTemperature = 0x2c3be0;
inline constexpr std::uint32_t kRvaNoise = 0x2c1790;
inline constexpr std::uint32_t kRvaGameSettings = 0x296940;
inline constexpr std::uint32_t kRvaTornadoDespawn = 0x7803e0;
inline constexpr std::uint32_t kRvaTornadoAdd = 0x2c2b20;
inline constexpr std::uint32_t kRvaTornadoUpdate = 0x2bfa00;
inline constexpr std::uint32_t kOffNoisePhase = 0xac;
inline constexpr std::uint32_t kOffSeason = 0xb0;
inline constexpr std::uint32_t kOffDayFactor = 0xb4;
inline constexpr std::uint32_t kOffTornadoData = 0x78;
inline constexpr std::uint32_t kOffTornadoCap = 0x80;
inline constexpr std::uint32_t kOffTornadoStart = 0x84;
inline constexpr std::uint32_t kOffTornadoCount = 0x88;
inline constexpr std::uint32_t kTornadoStride = 0xa0;
inline constexpr std::uint32_t kTornBasePos = 0x00;
inline constexpr std::uint32_t kTornTopPos = 0x18;
inline constexpr std::uint32_t kTornRadiusInner = 0x30;
inline constexpr std::uint32_t kTornRadiusOuter = 0x34;
inline constexpr std::uint32_t kTornIsActive = 0x38;
inline constexpr std::uint32_t kTornActiveFactor = 0x3c;
inline constexpr std::uint32_t kTornLifespan = 0x40;
inline constexpr std::uint32_t kTornIntensity = 0x44;
inline constexpr std::uint32_t kTornInfluenceRadius = 0x4c;
inline constexpr std::uint32_t kSettingOverrideWeather = 0x3f;
inline constexpr std::uint32_t kSettingOverrideTime = 0x40;
inline constexpr std::uint32_t kSettingOverrideWindDirection = 0x41;
inline constexpr float kStepRate = 0.01f;  // current += (set - current) * kStepRate per tick
}  // namespace sw::weather::map
