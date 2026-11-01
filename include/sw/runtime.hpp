#pragma once


#include "sw/active_map.hpp"
#include "sw/hook.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>


namespace sw {


struct RuntimeStatus {

    bool attached = false;

    bool hash_checked = false;

    bool hash_ok = false;

    bool signatures_checked = false;

    bool signatures_ok = false;

    std::uintptr_t base = 0;

    std::wstring image_path;

    std::size_t signature_matches = 0;

    std::size_t signature_total = 0;

    std::string error;

};


class Runtime {

public:

    static Runtime& get();


    Runtime(const Runtime&) = delete;

    Runtime& operator=(const Runtime&) = delete;



    bool attach(bool check_hash = true, bool check_signatures = true);

    void detach();


    bool attached() const;

    RuntimeStatus status() const;


    std::uintptr_t base() const;

    const void* at_rva(std::uint32_t rva) const;


    std::uintptr_t resolve(std::string_view api, bool verify = false);


    bool readable(const void* address, std::size_t size) const;


    template <typename T>

    bool read_rva(std::uint32_t rva, T& out) const {

        const void* p = at_rva(rva);

        if (!p || !readable(p, sizeof(T))) return false;

        std::memcpy(&out, p, sizeof(T));

        return true;

    }


    bool write_bytes(void* address, const void* data, std::size_t size);


    template <typename T>

    bool write_rva(std::uint32_t rva, const T& value) {

        return write_bytes(const_cast<void*>(at_rva(rva)), &value, sizeof(T));

    }


    std::uintptr_t vtable_of(std::uintptr_t object) const;

    std::uintptr_t virtual_slot(std::uintptr_t object, std::uint32_t index) const;


    HookStatus hook_ctor(std::string_view cls, HookCallback callback, void* user,

                          std::string& error);


    bool remove_ctor(std::string_view cls, HookCallback callback, void* user,

                     std::string& error);



    HookStatus hook_api(std::string_view api, HookCallback callback, void* user,

                        std::string& error);



    HookStatus hook_rva(std::uint32_t rva, HookCallback callback, void* user,

                        std::string& error);


    HookStatus hook_method(std::string_view cls, std::uint32_t slot, HookCallback callback,

                           void* user, std::string& error);


    bool unhook(std::uintptr_t address, HookCallback callback, void* user, std::string& error);


    std::uintptr_t method_address(std::string_view cls, std::uint32_t slot) const;


    // Is there a function start of the main module at this address?

    bool is_function_start(std::uintptr_t address) const;



    std::string_view class_of(std::uintptr_t object) const;


    // true if the vptr of the object points to the vtable of this class.

    bool is_instance_of(std::uintptr_t object, std::string_view cls) const;


    // Address of the vtable of a class in the running game (0 if unknown).

    std::uintptr_t class_vtable(std::string_view cls) const;


private:

    Runtime() = default;

    void set_error(std::string message);


    mutable std::mutex mutex_;

    RuntimeStatus status_;

    std::atomic<std::uintptr_t> module_base_{0};

};


}  // namespace sw

