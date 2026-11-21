// In-process side of the native bridge.
//
// Used from a DLL that runs in the Stormworks process. It resolves the RVAs
// named in the mapping against the actual module base and checks the byte
// signature before use. With a deviating version nothing is delivered -
// fail closed.
//
// The dedicated server (server64.exe of the same version) is a target too: every RVA of the
// headers is a client RVA, and at_rva() translates it with the table of generated_server_map.hpp
// (work/gen_server_map.js). An RVA the table does not know gives nullptr, so a header that checks
// its bytes refuses on the server instead of touching the wrong place.
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
    bool server = false;              // the process is server64.exe (addresses are translated)
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

    // Checks SHA-256 and all 194 signatures by default. Returns false if
    // the running binary does not match the mapping exactly.
    bool attach(bool check_hash = true, bool check_signatures = true);
    void detach();

    bool attached() const;
    RuntimeStatus status() const;

    std::uintptr_t base() const;
    // The address of a client RVA in the running process (on the server: translated; nullptr if unknown).
    // at_rva(0) is the module base.
    const void* at_rva(std::uint32_t rva) const;
    // True in server64.exe. Code that adds a client RVA to base() itself must use address() instead.
    bool server() const { return server_.load(std::memory_order_relaxed); }
    // The RVA of the running module for a client RVA (identity on the client, 0 if the server does not know it).
    std::uint32_t module_rva(std::uint32_t client_rva) const;
    // at_rva() as an integer: the address of a client RVA, 0 if the running module does not know it. Compare a vtable or a
    // return address with this, never with base() + rva.
    std::uintptr_t address(std::uint32_t client_rva) const { return reinterpret_cast<std::uintptr_t>(at_rva(client_rva)); }

    // Returns the address of the API entry or 0. With verify=true the byte
    // signature at the address is checked as well.
    std::uintptr_t resolve(std::string_view api, bool verify = false);

    bool readable(const void* address, std::size_t size) const;

    template <typename T>
    bool read_rva(std::uint32_t rva, T& out) const {
        const void* p = at_rva(rva);
        if (!p || !readable(p, sizeof(T))) return false;
        std::memcpy(&out, p, sizeof(T));
        return true;
    }

    // Writes into the process. Meant only for mods that deliberately
    // change the game state; the example mod writes nothing.
    bool write_bytes(void* address, const void* data, std::size_t size);

    template <typename T>
    bool write_rva(std::uint32_t rva, const T& value) {
        return write_bytes(const_cast<void*>(at_rva(rva)), &value, sizeof(T));
    }

    std::uintptr_t vtable_of(std::uintptr_t object) const;
    std::uintptr_t virtual_slot(std::uintptr_t object, std::uint32_t index) const;

    // Hooks ALL constructors of a mapped class (see kClassCtors).
    // The callback receives the new object - at that point it is not
    // initialized yet, so only remember the pointer, do not use it.
    // If several classes share a constructor, each gets its own callback.
    // If one constructor fails, nothing is set.
    HookStatus hook_ctor(std::string_view cls, HookCallback callback, void* user,
                          std::string& error);
    // Removes exactly this callback again; other classes that observe the
    // same constructor stay untouched.
    bool remove_ctor(std::string_view cls, HookCallback callback, void* user,
                     std::string& error);

    // ---- Method hooks --------------------------------------------------
    // All three set the same detour as hook_ctor: the callback runs on
    // entry into the function, receives the first argument as "self", and
    // all arguments stay unchanged for the original code.

    // Lua API handler by name (e.g. "server.getPlayers"). self is the
    // lua_State* of the call. The byte signature is checked beforehand.
    HookStatus hook_api(std::string_view api, HookCallback callback, void* user,
                        std::string& error);

    // Arbitrary function by RVA. The RVA must be a function start
    // (.pdata, for small leaf functions the int3 padding in front).
    HookStatus hook_rva(std::uint32_t rva, HookCallback callback, void* user,
                        std::string& error);
    // The same for an address of the running module (read from a vtable, for example): no translation.
    HookStatus hook_at(std::uintptr_t address, HookCallback callback, void* user, std::string& error);

    // Virtual method of a class: slot "slot" of its vtable. self is the
    // object (this). Caution: if classes share an implementation
    // (inherited method), the callback fires for all of them - filter with
    // is_instance_of().
    HookStatus hook_method(std::string_view cls, std::uint32_t slot, HookCallback callback,
                           void* user, std::string& error);

    // Removes a callback from an address again (for all three).
    bool unhook(std::uintptr_t address, HookCallback callback, void* user, std::string& error);

    // Address of a virtual method according to the mapping (0 if unknown).
    std::uintptr_t method_address(std::string_view cls, std::uint32_t slot) const;

    // Is there a function start of the main module at this address?
    bool is_function_start(std::uintptr_t address) const;

    // ---- Objects ---------------------------------------------------------
    // Class name of an object via its vptr ("" if unknown).
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
    std::atomic<bool> server_{false};
};

}  // namespace sw
