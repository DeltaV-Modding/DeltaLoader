#pragma once


#include <cstdint>

#include <optional>

#include <string>

#include <vector>


namespace sw {


struct ProcessInfo {

    std::uint32_t pid = 0;

    std::wstring image_path;

};


std::optional<ProcessInfo> find_process(const std::wstring& exe_name, std::wstring& err);

std::optional<ProcessInfo> wait_for_process(const std::wstring& exe_name, int seconds,

                                            std::wstring& err);



bool same_bitness(std::uint32_t pid, bool& same, std::wstring& err);


struct AntiCheatCheck {

    bool checked = false;

    std::wstring module;

};

AntiCheatCheck find_anticheat_module(std::uint32_t pid);


struct InjectResult {

    std::uint32_t pid = 0;

    std::wstring dll_path;

};


std::optional<InjectResult> inject_dll(std::uint32_t pid, const std::wstring& dll_path,

                                       std::wstring& err);


std::wstring format_win_error(unsigned long code);


// ---- Game path ------------------------------------------------------------


struct GameLocation {

    std::wstring exe;      // full path to the target EXE (empty if not found)

    std::wstring source;   // where the path comes from (for the display)

    std::vector<std::wstring> searched;   // all checked locations

    std::wstring env_ignored;  // STORMWORKS_DIR was set but contains no EXE

};



GameLocation find_game();




struct RemoteModule {

    std::uintptr_t base = 0;

    std::wstring name;   // file name, e.g. "my_mod.1.dll"

    std::wstring path;

};


// All modules that are loaded in the target process.

std::vector<RemoteModule> list_remote_modules(std::uint32_t pid, std::wstring& err);


// Result of unload_mod().

struct UnloadResult {

    bool shutdown_called = false;  // swmod_shutdown() ran in the game

    unsigned long shutdown_code = 0;  // 0 ok, 2 thread does not end, 3 callback still running

    bool freed = false;            // FreeLibrary executed in the game

};



std::optional<UnloadResult> unload_mod(std::uint32_t pid, const RemoteModule& module,

                                       const std::wstring& local_dll, std::wstring& err);


}  // namespace sw

