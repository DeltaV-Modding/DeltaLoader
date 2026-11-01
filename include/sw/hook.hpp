#pragma once


#include <cstddef>
#include <cstdint>
#include <string>


namespace sw {


using HookCallback = void (*)(void* self, void* user);

using ReplaceCallback = bool (*)(void* self, void* user, std::uint64_t* result);


enum class HookStatus {

    ok,

    unsupported_arch,      // not x64

    target_invalid,        // target does not lie in the main module

    already_hooked,        // a hook is already in place at this location

    decode_failed,         // instructions at the target not safely decodable

    relocation_failed,     //rel8 jump or no memory in reach

    alloc_failed,

    write_failed,

};


const char* to_string(HookStatus status);


class Hook {

public:


    static HookStatus install(void* target, HookCallback callback, void* user,

                              std::string& error);


    static HookStatus install_replace(void* target, ReplaceCallback replace, void* user,
                                      std::string& error);
    static HookStatus remove_replace(void* target, ReplaceCallback replace, void* user,
                                     std::string& error);

    static HookStatus remove(void* target, std::string& error);

    static HookStatus remove_callback(void* target, HookCallback callback, void* user,

                                      std::string& error);


    static HookStatus plan(const std::uint8_t* code, std::size_t available,

                           std::size_t& length, std::string& error);


    static bool is_hooked(void* target);

    static std::size_t count();


    static std::uint64_t arg(unsigned index);



    static void* arg2();


    static std::size_t remove_all(unsigned timeout_ms = 2000);



    static bool idle();

};


}  // namespace sw

