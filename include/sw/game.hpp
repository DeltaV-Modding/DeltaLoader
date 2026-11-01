#pragma once

#include "sw/hook.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sw::game {

class Function;
Function function(std::string_view name);

class Function {
public:
    Function() = default;

    explicit operator bool() const noexcept { return address_ != 0; }
    std::string_view name() const noexcept { return name_; }
    bool is_lua() const noexcept { return lua_; }

    // Run the callback whenever the game calls this function.
    bool on_call(HookCallback callback, void* user, std::string& error) const;
    bool remove_call(HookCallback callback, void* user, std::string& error) const;

    bool replace(ReplaceCallback callback, void* user, std::string& error) const;
    bool remove_replace(ReplaceCallback callback, void* user, std::string& error) const;

    // Calls the function. The caller states the signature; it is NOT
    // checked. Returns a default value if the function is not valid.
    template <typename R, typename... Args>
    R call(Args... args) const {
        if (!address_) return R();
        return reinterpret_cast<R (*)(Args...)>(address_)(args...);
    }

private:
    friend Function function(std::string_view name);
    std::string name_;
    std::uintptr_t address_ = 0;
    bool lua_ = false;
};

// The names of all functions that begin with the prefix ("server.",
// "c_ui_"). An empty prefix lists everything.
std::vector<std::string> functions(std::string_view prefix = {});

}  // namespace sw::game
