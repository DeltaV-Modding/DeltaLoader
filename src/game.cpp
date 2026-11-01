#include "sw/game.hpp"

#include "sw/generated_lua_api.hpp"
#include "sw/protos.hpp"
#include "sw/runtime.hpp"

namespace sw::game {

Function function(std::string_view name) {
    Function f;
    auto& rt = Runtime::get();
    const std::string key(name);
    if (map::find_lua_function(name)) {
        f.address_ = rt.resolve(key);
        f.lua_ = true;
    }
#if defined(_WIN64)
    else if (const auto* sym = protos::resolved_symbol(key.c_str())) {
        f.address_ = reinterpret_cast<std::uintptr_t>(rt.at_rva(sym->rva));
    }
#endif
    if (f.address_) f.name_ = key;
    return f;
}

bool Function::on_call(HookCallback callback, void* user, std::string& error) const {
    if (!address_) {
        error = "unknown function";
        return false;
    }
    auto& rt = Runtime::get();
    if (lua_) return rt.hook_api(name_, callback, user, error) == HookStatus::ok;
    return Hook::install(reinterpret_cast<void*>(address_), callback, user, error) ==
           HookStatus::ok;
}

bool Function::replace(ReplaceCallback callback, void* user, std::string& error) const {
    if (!address_) {
        error = "unknown function";
        return false;
    }
    if (lua_) {
        error = "a Lua function cannot be replaced - its result belongs to the Lua stack";
        return false;
    }
    return Hook::install_replace(reinterpret_cast<void*>(address_), callback, user, error) ==
           HookStatus::ok;
}

bool Function::remove_replace(ReplaceCallback callback, void* user, std::string& error) const {
    if (!address_) return false;
    return Hook::remove_replace(reinterpret_cast<void*>(address_), callback, user, error) ==
           HookStatus::ok;
}

bool Function::remove_call(HookCallback callback, void* user, std::string& error) const {
    if (!address_) return false;
    return Runtime::get().unhook(address_, callback, user, error);
}

std::vector<std::string> functions(std::string_view prefix) {
    std::vector<std::string> out;
    for (const auto& f : map::kLuaFunctions)
        if (f.name.substr(0, prefix.size()) == prefix) out.emplace_back(f.name);
#if defined(_WIN64)
    std::size_t count = 0;
    const auto* all = protos::resolved_symbols(&count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::string_view n(all[i].name);
        if (n.substr(0, prefix.size()) == prefix) out.emplace_back(n);
    }
#endif
    return out;
}

}  // namespace sw::game
