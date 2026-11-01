# DeltaLoader native mod SDK

Welcome! This is the source code that allows you to build your own mods for the DeltaLoader! Most of this was written by Malionaro (Basically everything) and the example mod was written by Shayans.

## Requirements

- Windows x64
- CMake 3.20 or newer
- Visual Studio 2022 C++ workload (MSVC)


## Build the example

To build your mod, you just need to run build_mod.ps1
The mod.dll will be located under ./build-mod/Release


## Add a mod

Add a cpp file with `#include "sw/mod.hpp"`, `SW_MOD("your_mod")`, and add `sw_mod_start()` / `sw_mod_stop()`. Register it in `CMakeLists.txt`:

sw_add_mod(example_mod src/example_mod.cpp)

## Support

If you have any questions, just ask on our discord. We are happy to help!