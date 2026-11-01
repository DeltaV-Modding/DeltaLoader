#include "sw/mod.hpp"
#include "sw/flags.hpp"
#include "sw/start_tiles.hpp"
#include "sw/tiles.hpp"
#include "sw/ui.hpp"

#include <atomic>
#include <cstdio>
#include <exception>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace sw::mod {

namespace {

std::atomic<bool> g_running{false};
std::atomic<bool> g_shut_down{false};
HANDLE g_stop_event = nullptr;
HANDLE g_thread = nullptr;
const char* g_name = "mod";

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

DWORD WINAPI worker(void*) {
    console::open("Stormworks Mods");
    console::set_prefix(g_name);

    auto& runtime = Runtime::get();
    if (!runtime.attach(true, true)) {
        const auto st = runtime.status();
        log("NOT STARTED: the game does not match the mapping.");
        log("  File: %s", narrow(st.image_path).c_str());
        log("  SHA-256: %s, Signatures %zu/%zu", st.hash_ok ? "OK" : "MISMATCH",
            st.signature_matches, st.signature_total);
        log("  Reason: %s", st.error.c_str());
        return 0;
    }
    log("loaded (game at 0x%llx, mapping %.*s build %.*s)",
        static_cast<unsigned long long>(runtime.base()),
        static_cast<int>(map::kVersion.size()), map::kVersion.data(),
        static_cast<int>(map::kBuildId.size()), map::kBuildId.data());

    g_running.store(true);
    try {
        sw_mod_start();
    } catch (const std::exception& e) {
        log("ERROR in sw_mod_start(): %s", e.what());
    } catch (...) {
        log("ERROR in sw_mod_start(): unknown exception");
    }
    return 0;
}

}  // namespace

bool running() { return g_running.load() && !g_shut_down.load(); }

bool sleep(unsigned ms) {
    if (!running()) return false;
    if (!g_stop_event) {
        Sleep(ms);
        return running();
    }
    return WaitForSingleObject(g_stop_event, ms) == WAIT_TIMEOUT && running();
}

Runtime& rt() { return Runtime::get(); }

const char* name() { return g_name; }

void log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    const int n = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    std::string text(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) std::vsnprintf(text.data(), text.size() + 1, format, args);
    va_end(args);
    console::print("%s\n", text.c_str());
}

namespace detail {

int dll_main(void* module, unsigned long reason, const char* name) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_name = name;
        DisableThreadLibraryCalls(static_cast<HMODULE>(module));
        g_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        g_thread = CreateThread(nullptr, 0, &worker, nullptr, 0, nullptr);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (!g_shut_down.load()) {
            g_shut_down.store(true);
            Hook::remove_all(200);
        }
    }
    return TRUE;
}

unsigned long shutdown() {
    if (g_shut_down.exchange(true)) return 0;
    if (g_stop_event) SetEvent(g_stop_event);
    unsigned long result = 0;
    if (g_thread) {
        if (WaitForSingleObject(g_thread, 5000) != WAIT_OBJECT_0) {
            log("WARNING: sw_mod_start() does not end (5 s) - does the loop check sw::mod::sleep()?");
            result = 2;
        }
    }
    if (g_running.load()) {
        try {
            sw_mod_stop();
        } catch (...) {
            log("ERROR in sw_mod_stop()");
        }
    }
    flags::clear();
    start_tiles::clear();
    tiles::clear();
    ui::main_menu::clear();
    const std::size_t hooks = Hook::remove_all(2000);
    if (!Hook::idle()) {
        log("WARNING: a callback is still running - unloading would be unsafe.");
        result = 3;
    }
    Instances::clear();
    g_running.store(false);
    log("unloaded (%zu hooks removed)", hooks);
    return result;
}

}  // namespace detail

}  // namespace sw::mod
