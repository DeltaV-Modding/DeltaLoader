#pragma once
#include <string>
#include <string_view>


namespace sw::console {


// Opens the console window (or uses an already existing one, for example
// that of another mod). Returns false if Windows does not provide one; the
// log file works anyway.

bool open(std::string_view title = "Stormworks Mods");


// Writes text unchanged (UTF-8) - into the window, if open, and into the log file.

void write(std::string_view utf8);


// Live display: redraws the text from the top left and clears the rest of
// the window - for dashboards that update every second.
// Goes ONLY into the window, not into the log file (otherwise it would
// overflow). Without an open window nothing happens.

void draw(std::string_view utf8);


// printf-like output with a timestamp and the mod name in front.

void print(const char* format, ...);


// Prefix for print(), normally the mod name (set by sw/mod.hpp).

void set_prefix(std::string_view prefix);


// Path of the log file (%TEMP%\stormworks_mods.log).

std::string log_path();


}  // namespace sw::console

