#include "sw/console.hpp"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace sw::console {

namespace {

std::mutex g_mutex;
HANDLE g_out = nullptr;
std::string g_prefix;

std::wstring widen(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                      nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) {
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(),
                            n);
    }
    return out;
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(),
                            n, nullptr, nullptr);
    }
    return out;
}

// Ctrl+C and Ctrl+Break in the console window would otherwise end the game.
BOOL WINAPI ignore_ctrl(DWORD type) {
    return (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) ? TRUE : FALSE;
}

void append_log(std::string_view utf8) {
    const std::wstring path = widen(log_path());
    HANDLE f = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(f);
}

}  // namespace

std::string log_path() {
    wchar_t dir[MAX_PATH * 2]{};
    const DWORD n = GetTempPathW(static_cast<DWORD>(std::size(dir)), dir);
    std::wstring base = (n > 0 && n < std::size(dir)) ? std::wstring(dir, n) : L".\\";
    return narrow(base + L"stormworks_mods.log");
}

bool open(std::string_view title) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_out) return true;
    if (!AllocConsole()) {
        // If a console already exists (another mod), it is used as well.
        if (GetLastError() != ERROR_ACCESS_DENIED) return false;
    }
    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!g_out || g_out == INVALID_HANDLE_VALUE) {
        g_out = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, 0, nullptr);
    }
    if (!g_out || g_out == INVALID_HANDLE_VALUE) {
        g_out = nullptr;
        return false;
    }
    SetConsoleTitleW(widen(title).c_str());
    SetConsoleCtrlHandler(&ignore_ctrl, TRUE);
    // Remove the close button: closing the console ends the game.
    if (HWND wnd = GetConsoleWindow()) {
        if (HMENU menu = GetSystemMenu(wnd, FALSE)) DeleteMenu(menu, SC_CLOSE, MF_BYCOMMAND);
    }
    return true;
}

void write(std::string_view utf8) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_out) {
        const std::wstring w = widen(utf8);
        DWORD written = 0;
        WriteConsoleW(g_out, w.c_str(), static_cast<DWORD>(w.size()), &written, nullptr);
    }
    append_log(utf8);
}

void draw(std::string_view utf8) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_out) return;
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (!GetConsoleScreenBufferInfo(g_out, &info)) return;
    const SHORT width = static_cast<SHORT>(info.srWindow.Right - info.srWindow.Left + 1);
    const SHORT top = info.srWindow.Top;

    // Overwrite line by line in place and pad every line to the window
    // width - that way nothing flickers and old, longer lines disappear.
    const std::wstring w = widen(utf8);
    std::wstring frame;
    std::size_t start = 0;
    SHORT rows = 0;
    while (start <= w.size()) {
        std::size_t end = w.find(L'\n', start);
        if (end == std::wstring::npos) end = w.size();
        std::wstring row = w.substr(start, end - start);
        if (row.size() > static_cast<std::size_t>(width)) row.resize(static_cast<std::size_t>(width));
        row.resize(static_cast<std::size_t>(width), L' ');
        frame += row;
        ++rows;
        start = end + 1;
        if (end == w.size()) break;
    }
    SetConsoleCursorPosition(g_out, COORD{0, top});
    DWORD written = 0;
    WriteConsoleW(g_out, frame.c_str(), static_cast<DWORD>(frame.size()), &written, nullptr);

    // Clear the rest of the visible window.
    const SHORT window_rows = static_cast<SHORT>(info.srWindow.Bottom - info.srWindow.Top + 1);
    if (rows < window_rows) {
        const DWORD rest = static_cast<DWORD>(window_rows - rows) * static_cast<DWORD>(width);
        FillConsoleOutputCharacterW(g_out, L' ', rest, COORD{0, static_cast<SHORT>(top + rows)},
                                    &written);
    }
    SetConsoleCursorPosition(g_out, COORD{0, static_cast<SHORT>(top + rows)});
}

void set_prefix(std::string_view prefix) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_prefix.assign(prefix.begin(), prefix.end());
}

void print(const char* format, ...) {
    if (!format) return;
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    const int n = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    std::string text(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) std::vsnprintf(text.data(), text.size() + 1, format, args);
    va_end(args);

    SYSTEMTIME t{};
    GetLocalTime(&t);
    char stamp[32];
    std::snprintf(stamp, sizeof(stamp), "%02u:%02u:%02u ", t.wHour, t.wMinute, t.wSecond);

    std::string prefix;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        prefix = g_prefix;
    }
    std::string line = stamp;
    if (!prefix.empty()) line += "[" + prefix + "] ";
    line += text;
    write(line);
}

}  // namespace sw::console
