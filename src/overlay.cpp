#include "sw/overlay.hpp"

#include "sw/process.hpp"
#include "sw/sha256.hpp"
#include "sw/target.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace sw::overlay {

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string to_utf8(const fs::path& p) { return p.u8string(); }
fs::path from_utf8(const std::string& s) { return fs::u8path(s); }

bool is_reparse(const fs::path& p) {
#ifdef _WIN32
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    std::error_code ec;
    return fs::is_symlink(p, ec);
#endif
}

fs::path rom_dir(const fs::path& game) { return game / "rom"; }

fs::path journal_path(const fs::path& game) { return state_dir(game) / "journal.tsv"; }

// A path of a mod that may be used below rom: no empty parts, no "..", no ":" or "\" inside a part, no leading "." part.
bool safe_component(const std::string& c) {
    if (c.empty() || c == "." || c == "..") return false;
    for (char ch : c) if (ch == ':' || ch == '\\' || ch == '/' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|' || ch == '\t' || ch == '\n') return false;
    if (c.back() == '.' || c.back() == ' ') return false;
    return true;
}

bool ignored_name(const std::string& n) {
    const std::string l = lower(n);
    return (l.size() > 12 && l.substr(l.size() - 12) == ".placeholder") || l == "desktop.ini" || l == "thumbs.db" || l == ".ds_store" || l.rfind(".git", 0) == 0 || (l.size() > 8 && l.substr(l.size() - 8) == ".swov.tmp");
}

// The real spelling of a relative path below base (the folders and files that exist keep their case, the rest is taken from rel).
std::string actual_rel(const fs::path& base, const std::vector<std::string>& parts) {
    fs::path cur = base;
    std::string out;
    bool exists = true;
    for (const auto& part : parts) {
        std::string use = part;
        if (exists) {
            exists = false;
            std::error_code ec;
            for (const auto& e : fs::directory_iterator(cur, ec)) {
                const std::string n = to_utf8(e.path().filename());
                if (lower(n) == lower(part)) { use = n; exists = true; break; }
            }
        }
        cur /= from_utf8(use);
        if (!out.empty()) out += '/';
        out += use;
    }
    return out;
}

fs::path join_rel(const fs::path& base, const std::string& rel) {
    fs::path p = base;
    std::stringstream ss(rel);
    std::string part;
    while (std::getline(ss, part, '/')) p /= from_utf8(part);
    return p;
}

bool file_sha(const fs::path& p, std::string& out, std::string& err) { return sha256_file(p.wstring(), out, err); }

// the journal
struct Journal {
    std::vector<Entry> entries;
    std::vector<std::string> dirs;
    std::string started;
};

std::string now_text() {
    std::time_t t = std::time(nullptr);
    char buf[40];
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tmv);
    return buf;
}

bool write_journal(const fs::path& game, const Journal& j, std::string& err) {
    std::error_code ec;
    fs::create_directories(state_dir(game), ec);
    const fs::path tmp = journal_path(game).string() + ".new";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { err = "cannot write " + to_utf8(tmp); return false; }
        f << "swoverlay 1\n";
        f << "started\t" << j.started << "\n";
        for (const auto& d : j.dirs) f << "D\t" << d << "\n";
        for (const auto& e : j.entries)
            f << (e.kind == Kind::added ? "A" : "R") << "\t" << e.rel << "\t" << e.mod << "\t" << e.installed_sha << "\t" << e.original_sha << "\n";
        f.flush();
        if (!f) { err = "write error in " + to_utf8(tmp); return false; }
    }
#ifdef _WIN32
    if (!MoveFileExW(tmp.c_str(), journal_path(game).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { err = "cannot replace the journal"; return false; }
#else
    fs::rename(tmp, journal_path(game), ec);
    if (ec) { err = "cannot replace the journal"; return false; }
#endif
    return true;
}

bool read_journal(const fs::path& game, Journal& j, std::string& err) {
    std::ifstream f(journal_path(game), std::ios::binary);
    if (!f) { err = "no journal"; return false; }
    std::string line;
    if (!std::getline(f, line) || line.rfind("swoverlay 1", 0) != 0) { err = "the journal is not from this version"; return false; }
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> c;
        std::stringstream ss(line);
        std::string part;
        while (std::getline(ss, part, '\t')) c.push_back(part);
        if (line.empty()) continue;
        if (c[0] == "started" && c.size() > 1) j.started = c[1];
        else if (c[0] == "D" && c.size() > 1) j.dirs.push_back(c[1]);
        else if ((c[0] == "A" || c[0] == "R") && c.size() >= 4) {
            Entry e;
            e.kind = c[0] == "A" ? Kind::added : Kind::replaced;
            e.rel = c[1]; e.mod = c[2]; e.installed_sha = c[3];
            if (c.size() > 4) e.original_sha = c[4];
            j.entries.push_back(e);
        }
    }
    return true;
}

fs::path backup_path(const fs::path& game, const std::string& rel) { return join_rel(state_dir(game) / "backup", rel); }
fs::path conflict_path(const fs::path& game, const std::string& rel) { return join_rel(state_dir(game) / "conflicts", rel); }

bool move_file(const fs::path& from, const fs::path& to, std::string& err) {
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
#ifdef _WIN32
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH)) return true;
    err = "cannot move " + to_utf8(from) + " (error " + std::to_string(GetLastError()) + ")";
    return false;
#else
    fs::rename(from, to, ec);
    if (!ec) return true;
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) { err = "cannot move " + to_utf8(from); return false; }
    fs::remove(from, ec);
    return true;
#endif
}

}  // namespace

fs::path state_dir(const fs::path& game_dir) { return game_dir / ".swoverlay"; }

bool game_running() {
    std::wstring err;
    return find_process(kTargetExe, err).has_value();
}

std::vector<ModPack> discover(const std::vector<fs::path>& roots, const std::vector<std::string>& only, std::vector<std::string>* warnings) {
    std::vector<ModPack> all;
    for (const auto& root : roots) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        for (const auto& e : fs::directory_iterator(root, ec)) {
            if (!e.is_directory(ec) || is_reparse(e.path())) continue;
            ModPack m;
            m.name = to_utf8(e.path().filename());
            if (m.name.empty() || m.name[0] == '.' || m.name[0] == '_') continue;
            m.dir = e.path();
            for (const auto& d : fs::directory_iterator(m.dir, ec)) {
                const std::string n = to_utf8(d.path().filename());
                if (d.is_regular_file(ec) && lower(n).size() > 4 && lower(n).substr(lower(n).size() - 4) == ".dll") m.dlls.push_back(d.path());
                else if (d.is_directory(ec) && !is_reparse(d.path())) {
                    if (lower(n) == "dll" || lower(n) == "dlls") {
                        for (const auto& f : fs::directory_iterator(d.path(), ec))
                            if (f.is_regular_file(ec) && lower(to_utf8(f.path().extension())) == ".dll") m.dlls.push_back(f.path());
                    } else {
                        m.rom_dirs.push_back(n);
                        for (const auto& f : fs::recursive_directory_iterator(d.path(), ec))
                            if (f.is_regular_file(ec) && !ignored_name(to_utf8(f.path().filename()))) ++m.files;
                    }
                }
            }
            std::sort(m.dlls.begin(), m.dlls.end());
            std::sort(m.rom_dirs.begin(), m.rom_dirs.end());
            if (m.dlls.empty() && m.files == 0) { if (warnings) warnings->push_back("mod folder " + m.name + " has no dll and no files below a rom folder - ignored"); continue; }
            all.push_back(std::move(m));
        }
    }
    std::sort(all.begin(), all.end(), [](const ModPack& a, const ModPack& b) { return lower(a.name) < lower(b.name); });
    if (only.empty()) return all;
    std::vector<ModPack> out;
    for (const auto& want : only) {
        const auto it = std::find_if(all.begin(), all.end(), [&](const ModPack& m) { return lower(m.name) == lower(want); });
        if (it != all.end()) out.push_back(*it);
        else if (warnings) warnings->push_back("mod " + want + " not found");
    }
    return out;
}

Report status(const fs::path& game_dir) {
    Report r;
    Journal j;
    std::string err;
    r.ok = true;
    if (!read_journal(game_dir, j, err)) { r.active = false; r.notes.push_back("no overlay is installed"); return r; }
    r.active = true;
    r.entries = j.entries;
    for (const auto& e : j.entries) (e.kind == Kind::added ? r.added : r.replaced)++;
    r.notes.push_back("an overlay is installed since " + j.started + ": " + std::to_string(r.added) + " added, " + std::to_string(r.replaced) + " replaced");
    return r;
}

Report restore(const fs::path& game_dir, bool allow_running) {
    Report r;
    Journal j;
    std::string err;
    if (!fs::exists(journal_path(game_dir))) { r.ok = true; r.notes.push_back("nothing to restore"); return r; }
    if (!read_journal(game_dir, j, err)) { r.error = err; return r; }
    if (!allow_running && game_running()) { r.error = "the game is running - close it first (its files are in use)"; return r; }
    const fs::path rom = rom_dir(game_dir);
    std::error_code ec;
    bool unresolved = false;
    std::set<std::string> done;
    for (auto it = j.entries.rbegin(); it != j.entries.rend(); ++it) {
        const Entry& e = *it;
        if (!done.insert(lower(e.rel)).second) continue;
        const fs::path target = join_rel(rom, e.rel);
        const fs::path tmp = fs::path(target.string() + ".swov.tmp");
        fs::remove(tmp, ec);
        const fs::path backup = backup_path(game_dir, e.rel);
        const bool has_target = fs::exists(target, ec);
        std::string sha, serr;
        if (has_target && !file_sha(target, sha, serr)) { r.warnings.push_back("cannot read " + e.rel + ": " + serr); unresolved = true; continue; }
        if (e.kind == Kind::added) {
            if (!has_target) continue;
            if (sha == e.installed_sha) {
                if (!fs::remove(target, ec) || ec) { r.warnings.push_back("cannot delete " + e.rel); unresolved = true; continue; }
                ++r.removed;
            } else {
                r.warnings.push_back("left alone: " + e.rel + " was changed after the overlay put it there");
                ++r.kept;
            }
            continue;
        }
        const bool has_backup = fs::exists(backup, ec);
        if (!has_backup) {
            if (has_target && sha == e.original_sha) continue;
            r.warnings.push_back("ORIGINAL LOST: " + e.rel + " - the backup is missing; verify the game files in Steam");
            ++r.kept;
            continue;
        }
        if (!has_target || sha == e.installed_sha || sha == e.original_sha) {
            std::string merr;
            if (!move_file(backup, target, merr)) { r.warnings.push_back(merr); unresolved = true; continue; }
            ++r.restored;
        } else {
            std::string merr;
            move_file(backup, conflict_path(game_dir, e.rel), merr);
            r.warnings.push_back("left alone: " + e.rel + " was changed while the overlay was active (game update?); the original is in .swoverlay\\conflicts");
            ++r.kept;
        }
    }
    for (auto d = j.dirs.rbegin(); d != j.dirs.rend(); ++d) {
        const fs::path p = join_rel(rom, *d);
        if (fs::is_directory(p, ec) && fs::is_empty(p, ec)) fs::remove(p, ec);
    }
    if (unresolved) {
        r.error = "some files could not be restored (in use?); the journal is kept, run restore again";
        return r;
    }
    fs::remove(journal_path(game_dir), ec);
    fs::remove_all(state_dir(game_dir) / "backup", ec);
    if (fs::is_directory(state_dir(game_dir) / "conflicts", ec) && fs::is_empty(state_dir(game_dir) / "conflicts", ec)) fs::remove(state_dir(game_dir) / "conflicts", ec);
    if (fs::is_directory(state_dir(game_dir), ec) && fs::is_empty(state_dir(game_dir), ec)) fs::remove(state_dir(game_dir), ec);
    r.ok = true;
    r.notes.push_back("restored " + std::to_string(r.restored) + " original(s), removed " + std::to_string(r.removed) + " added file(s)" +
                      (r.kept ? ", left " + std::to_string(r.kept) + " alone" : ""));
    return r;
}

Report apply(const Options& options) {
    Report r;
    const fs::path game = options.game_dir;
    const fs::path rom = rom_dir(game);
    std::error_code ec;
    if (!fs::is_directory(rom, ec)) { r.error = "no rom folder in " + to_utf8(game); return r; }
    if (!options.allow_running && game_running()) { r.error = "the game is running - the overlay must be laid before it starts"; return r; }
    if (fs::exists(journal_path(game), ec)) {
        r.notes.push_back("an earlier overlay was still installed (the game or the PC ended without restore) - restoring it first");
        Report old = restore(game, options.allow_running);
        for (auto& w : old.warnings) r.warnings.push_back(w);
        if (!old.ok) { r.error = "the earlier overlay could not be restored: " + old.error; return r; }
    }
    std::vector<std::string> warn;
    const auto mods = discover(options.mod_roots, options.only, &warn);
    for (auto& w : warn) r.warnings.push_back(w);

    // the folders of rom that a mod may fill
    std::map<std::string, std::string> rom_top;     // lower -> real spelling
    for (const auto& e : fs::directory_iterator(rom, ec))
        if (e.is_directory(ec)) rom_top[lower(to_utf8(e.path().filename()))] = to_utf8(e.path().filename());

    struct Planned { std::string rel, mod; fs::path source; };
    std::map<std::string, Planned> plan;            // lower rel -> planned
    std::vector<std::string> order;
    for (const auto& m : mods) {
        for (const auto& top : m.rom_dirs) {
            const auto rt = rom_top.find(lower(top));
            if (rt == rom_top.end()) { r.warnings.push_back(m.name + ": folder " + top + " has no counterpart in rom - ignored"); continue; }
            const fs::path base = m.dir / from_utf8(top);
            for (auto it = fs::recursive_directory_iterator(base, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) break;
                if (is_reparse(it->path())) { if (it->is_directory(ec)) it.disable_recursion_pending(); continue; }
                if (!it->is_regular_file(ec)) continue;
                const std::string name = to_utf8(it->path().filename());
                if (ignored_name(name)) continue;
                std::vector<std::string> parts{rt->second};
                bool ok = true;
                for (const auto& comp : fs::relative(it->path(), base, ec)) {
                    const std::string c = to_utf8(comp);
                    if (!safe_component(c)) { ok = false; break; }
                    parts.push_back(c);
                }
                if (!ok) { r.warnings.push_back(m.name + ": unusable path " + to_utf8(it->path()) + " - skipped"); continue; }
                const std::string rel = actual_rel(rom, parts);
                const std::string key = lower(rel);
                if (plan.count(key)) r.warnings.push_back(rel + ": " + m.name + " overrides " + plan[key].mod);
                else order.push_back(key);
                plan[key] = Planned{rel, m.name, it->path()};
            }
        }
    }
    if (plan.empty()) { r.ok = true; r.notes.push_back("no mod brings files for rom - nothing to lay"); return r; }

    // classify, hash, and find the folders that have to be made
    Journal j;
    j.started = now_text();
    std::set<std::string> new_dirs;
    for (const auto& key : order) {
        const Planned& p = plan[key];
        Entry e;
        e.rel = p.rel; e.mod = p.mod;
        std::string err;
        if (!file_sha(p.source, e.installed_sha, err)) { r.error = "cannot read " + to_utf8(p.source) + ": " + err; return r; }
        const fs::path target = join_rel(rom, p.rel);
        if (fs::exists(target, ec)) {
            if (!fs::is_regular_file(target, ec)) { r.error = p.rel + " is not a file in rom"; return r; }
            e.kind = Kind::replaced;
            if (!file_sha(target, e.original_sha, err)) { r.error = "cannot read " + to_utf8(target) + ": " + err; return r; }
        } else {
            e.kind = Kind::added;
            fs::path d = target.parent_path();
            std::vector<std::string> chain;
            while (d != rom && !fs::exists(d, ec)) { chain.push_back(to_utf8(fs::relative(d, rom, ec)).empty() ? "" : to_utf8(fs::relative(d, rom, ec))); d = d.parent_path(); }
            for (auto c = chain.rbegin(); c != chain.rend(); ++c) {
                std::string slash = *c;
                std::replace(slash.begin(), slash.end(), '\\', '/');
                if (!slash.empty() && new_dirs.insert(lower(slash)).second) j.dirs.push_back(slash);
            }
        }
        (e.kind == Kind::added ? r.added : r.replaced)++;
        j.entries.push_back(e);
    }
    r.entries = j.entries;
    if (options.dry_run) {
        r.ok = true;
        r.notes.push_back("dry run: " + std::to_string(r.added) + " file(s) would be added, " + std::to_string(r.replaced) + " replaced");
        return r;
    }

    // write-ahead
    std::string err;
    if (!write_journal(game, j, err)) { r.error = err; return r; }
    auto fail = [&](const std::string& why) {
        Report back = restore(game, true);
        for (auto& w : back.warnings) r.warnings.push_back(w);
        r.error = why + (back.ok ? " (everything was put back)" : " (restore also failed: " + back.error + ")");
        return r;
    };
    for (const auto& d : j.dirs) fs::create_directories(join_rel(rom, d), ec);
    for (const auto& e : j.entries) {
        const Planned& p = plan[lower(e.rel)];
        const fs::path target = join_rel(rom, e.rel);
        std::string merr;
        if (e.kind == Kind::replaced && !move_file(target, backup_path(game, e.rel), merr)) return fail(merr);
        fs::create_directories(target.parent_path(), ec);
        const fs::path tmp = fs::path(target.string() + ".swov.tmp");
#ifdef _WIN32
        if (!CopyFileW(p.source.c_str(), tmp.c_str(), FALSE)) return fail("cannot copy " + to_utf8(p.source) + " (error " + std::to_string(GetLastError()) + ")");
#else
        fs::copy_file(p.source, tmp, fs::copy_options::overwrite_existing, ec);
        if (ec) return fail("cannot copy " + to_utf8(p.source));
#endif
        if (!move_file(tmp, target, merr)) return fail(merr);
    }
    r.ok = true;
    r.notes.push_back("laid " + std::to_string(r.added) + " new and " + std::to_string(r.replaced) + " replacing file(s) from " + std::to_string(mods.size()) + " mod(s); the originals are in .swoverlay\\backup");
    return r;
}

}  // namespace sw::overlay
