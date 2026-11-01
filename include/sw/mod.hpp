#pragma once


#include "sw/console.hpp"

#include "sw/instances.hpp"

#include "sw/runtime.hpp"


#include <cstdarg>


// Self explanatory
void sw_mod_start();
void sw_mod_stop();


namespace sw::mod {


// false as soon as the mod should be unloaded. Loops in sw_mod_start()
// must check this regularly.

bool running();


// Waits ms milliseconds. Returns false if unloading begins in the
// meantime - then leave sw_mod_start().
bool sleep(unsigned ms);


// The runtime of the game (already connected and checked).
Runtime& rt();


// Name from SW_MOD(...).
const char* name();


// Output into the console window and into the log file, with line end.
void log(const char* format, ...);


namespace detail {

int dll_main(void* module, unsigned long reason, const char* name);

unsigned long shutdown();

}  // namespace detail


}  // namespace sw::mod



#if defined(_M_IX86)

#define SW_EXPORT_PLAIN(name, bytes) \

    __pragma(comment(linker, "/EXPORT:" #name "=_" #name "@" #bytes))

#else

#define SW_EXPORT_PLAIN(name, bytes)

#endif


// Exports that swinject uses for detection and unloading, and the DLL

// entry point.

#define SW_MOD(NAME) \
    SW_EXPORT_PLAIN(swmod_shutdown, 4) \
    extern "C" __declspec(dllexport) const char* swmod_name() { return NAME; } \
    extern "C" __declspec(dllexport) unsigned long __stdcall swmod_shutdown(void*) { \
        return ::sw::mod::detail::shutdown(); \
    } \
    extern "C" int __stdcall DllMain(void* module, unsigned long reason, void*) { \
        return ::sw::mod::detail::dll_main(module, reason, NAME); \
    }