#pragma once


namespace sw {


#if defined(_WIN64)


inline constexpr const wchar_t* kTargetExe = L"stormworks64.exe";

inline constexpr const wchar_t* kTargetExeDisplay = L"stormworks64.exe (x64)";

inline constexpr const wchar_t* kDefaultExePath =

    L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Stormworks\\stormworks64.exe";

inline constexpr bool kBuildIsX64 = true;


#else


inline constexpr const wchar_t* kTargetExe = L"stormworks.exe";

inline constexpr const wchar_t* kTargetExeDisplay = L"stormworks.exe (x86)";

inline constexpr const wchar_t* kDefaultExePath =

    L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Stormworks\\stormworks.exe";

inline constexpr bool kBuildIsX64 = false;


#endif


}  // namespace sw

