#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace sw::overlay {

namespace fs = std::filesystem;

struct ModPack {
    std::string name;                    // the folder name
    fs::path dir;
    std::vector<fs::path> dlls;          // *.dll in the folder or in its dll\ sub folder
    std::vector<std::string> rom_dirs;   // top level folders of the mod (as spelled in the mod)
    std::size_t files = 0;               // files below those folders
};

struct Options {
    fs::path game_dir;                   // the folder with stormworks64.exe and the rom folder
    std::vector<fs::path> mod_roots;     // folders that contain mod folders
    std::vector<std::string> only;       // names of the mods to use, in this order; empty = all, alphabetical
    bool dry_run = false;
    bool allow_running = false;          // do not refuse while the game is running (for tests and for tools that know better)
};

enum class Kind { added, replaced };

struct Entry {
    Kind kind = Kind::added;
    std::string rel;                     // below rom, with forward slashes, the spelling of the game when the file existed
    std::string mod;
    std::string installed_sha;           // SHA-256 of the mod's file
    std::string original_sha;            // SHA-256 of the replaced file (replaced only)
};

struct Report {
    bool ok = false;
    std::string error;
    std::vector<std::string> notes;
    std::vector<std::string> warnings;
    std::vector<Entry> entries;
    std::size_t added = 0, replaced = 0;        // apply
    std::size_t restored = 0, removed = 0, kept = 0;   // restore: originals put back, mod files removed, files left alone (conflicts)
    bool active = false;                        // status: an overlay is installed (a journal exists)
};

// The mods in the given folders (a folder that has a dll or at least one file below a rom folder).
std::vector<ModPack> discover(const std::vector<fs::path>& roots, const std::vector<std::string>& only, std::vector<std::string>* warnings = nullptr);

// Lays the mods over rom. Restores an old, unfinished overlay first.
Report apply(const Options& options);

// Takes the overlay away. Nothing to do (ok, nothing changed) when there is no journal.
Report restore(const fs::path& game_dir, bool allow_running = false);

// Reads the journal.
Report status(const fs::path& game_dir);

// True while stormworks64.exe runs.
bool game_running();

// The folder where the journal and the backups live.
fs::path state_dir(const fs::path& game_dir);

}  // namespace sw::overlay
