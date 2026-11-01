cmake_minimum_required(VERSION 3.20)
project(DeltaLoaderModSDK LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(swbridge STATIC
    src/signature.cpp src/sha256.cpp src/pe_file.cpp src/process.cpp
    src/runtime.cpp src/hook.cpp src/console.cpp src/instances.cpp
    src/registry.cpp src/settings.cpp src/events.cpp src/api.cpp src/ui.cpp
    src/game.cpp src/world.cpp src/tiles.cpp src/start_tiles.cpp
    src/textures.cpp src/mesh.cpp src/camera.cpp src/physics.cpp
    src/mapicons.cpp src/render.cpp src/network.cpp src/overlay.cpp src/vfs.cpp
    src/audio.cpp src/game_state.cpp src/input.cpp src/weather.cpp
    src/vehicle.cpp src/flags.cpp src/trees.cpp src/discord.cpp
)
target_include_directories(swbridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(swbridge PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

add_library(swmodkit STATIC src/mod.cpp)
target_link_libraries(swmodkit PUBLIC swbridge)

function(sw_add_mod name)
    add_library(${name} SHARED ${ARGN})
    target_link_libraries(${name} PRIVATE swmodkit)
    set_target_properties(${name} PROPERTIES PREFIX "" OUTPUT_NAME "${name}")
    if(MSVC)
        target_compile_options(${name} PRIVATE /W4 /permissive- /utf-8)
        set_property(TARGET ${name} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    endif()
endfunction()

if(MSVC)
    foreach(target swbridge swmodkit)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    endforeach()
endif()

# Add each mod as: sw_add_mod(my_mod path/to/mod.cpp [more_sources.cpp ...])
sw_add_mod(delta_example src/example_mod.cpp)