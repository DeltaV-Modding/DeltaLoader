#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace sw::vfs {

namespace fs = std::filesystem;

struct Config {
    fs::path rom;                    // <game>\rom
    std::vector<fs::path> roots;     // the mod folders; the LAST one wins
};

// Replaces the import table entries of the main module (or of `module` when given, an HMODULE) and activates the mapping.
// Calling it again replaces the configuration. false with an error text when no entry could be patched.
bool install(const Config& config, std::string& error, void* module = nullptr);

// Puts the original entries back. The handles of listings that are still open stay valid.
void uninstall();
bool installed();

// Pure mapping, no hooks needed: the file or folder of a mod folder that stands in for `path` (a path below rom), or an empty path.
// With files_only the result is a regular file or nothing.
fs::path resolve(const fs::path& path, bool files_only = true);

struct Stats {
    std::size_t opens = 0;        // CreateFile calls
    std::size_t redirected = 0;   // ... of them answered from a mod folder
    std::size_t attributes = 0;   // GetFileAttributesExW answered from a mod folder
    std::size_t listings = 0;     // folder listings merged
};
Stats stats();

}  // namespace sw::vfs
