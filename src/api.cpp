#include "sw/api.hpp"

#include "sw/generated_components.hpp"
#include "sw/generated_lua_api.hpp"
#include "sw/generated_settings.hpp"
#include "sw/protos.hpp"
#include "sw/runtime.hpp"

#include <algorithm>

namespace sw::api {

Obj observe(std::uintptr_t address) {
    if (!address) return Obj();
    const auto name = Runtime::get().class_of(address);
    return Objects::instance().observe(address, name);
}

bool on_call(std::string_view name, HookCallback callback, void* user, std::string& error) {
    auto& rt = Runtime::get();
    const std::string key(name);
    if (map::find_lua_function(name))
        return rt.hook_api(key, callback, user, error) == HookStatus::ok;
#if defined(_WIN64)
    if (const auto* sym = protos::resolved_symbol(key.c_str()))
        return rt.hook_rva(sym->rva, callback, user, error) == HookStatus::ok;
#endif
    error = "unknown function: " + key;
    return false;
}

bool replace(std::string_view name, ReplaceCallback callback, void* user, std::string& error) {
    auto fn = game::function(name);
    if (!fn) {
        error = "unknown function: " + std::string(name);
        return false;
    }
    return fn.replace(callback, user, error);
}

bool remove_replace(std::string_view name, ReplaceCallback callback, void* user,
                    std::string& error) {
    auto fn = game::function(name);
    if (!fn) {
        error = "unknown function: " + std::string(name);
        return false;
    }
    return fn.remove_replace(callback, user, error);
}

namespace lua {

bool find(std::string_view name, Function& out) {
    const auto* entry = map::find_lua_function(name);
    if (!entry) return false;
    out = Function{entry->name, static_cast<std::uint8_t>(entry->argc), entry->args,
                   entry->outs, entry->out_names, entry->confidence};
    return true;
}

std::vector<Function> all() {
    std::vector<Function> out;
    out.reserve(map::kLuaFunctions.size());
    for (const auto& f : map::kLuaFunctions) {
        out.push_back(Function{f.name, static_cast<std::uint8_t>(f.argc), f.args, f.outs,
                               f.out_names, f.confidence});
    }
    return out;
}

std::vector<Function> of_area(std::string_view prefix) {
    std::vector<Function> out;
    for (const auto& f : map::kLuaFunctions) {
        if (f.name.size() < prefix.size()) continue;
        if (f.name.compare(0, prefix.size(), prefix) != 0) continue;
        out.push_back(Function{f.name, static_cast<std::uint8_t>(f.argc), f.args, f.outs,
                               f.out_names, f.confidence});
    }
    return out;
}

}  // namespace lua

namespace catalogue {

std::vector<std::string> classes(std::string_view prefix) {
    std::vector<std::string> out;
    for (const auto& c : map::kClasses) {
        if (c.name.size() < prefix.size()) continue;
        if (c.name.compare(0, prefix.size(), prefix) != 0) continue;
        out.emplace_back(c.name);
    }
    return out;
}

bool has_class(std::string_view name) {
    for (const auto& c : map::kClasses) {
        if (c.name == name) return true;
    }
    return false;
}

std::vector<Setting> settings() {
    std::vector<Setting> out;
    out.reserve(map::kSettings.size());
    for (const auto& s : map::kSettings) out.push_back(Setting{s.name, s.type});
    return out;
}

std::vector<Component> components(std::string_view category) {
    std::vector<Component> out;
    for (const auto& c : map::kComponents) {
        if (c.category.size() < category.size()) continue;
        if (c.category.compare(0, category.size(), category) != 0) continue;
        out.push_back(Component{c.name, c.category});
    }
    return out;
}

std::vector<std::string> component_categories() {
    std::vector<std::string> out;
    for (const auto& c : map::kComponents) {
        if (std::find(out.begin(), out.end(), c.category) == out.end()) {
            out.emplace_back(c.category);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace catalogue
}  // namespace sw::api
