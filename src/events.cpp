#include "sw/events.hpp"

#include "sw/generated_events.hpp"
#include "sw/runtime.hpp"

#include <algorithm>
#include <mutex>

namespace sw::events {

namespace {

struct Registration {
    std::string name;
    EventCallback callback = nullptr;
    void* user = nullptr;
};

std::mutex g_mutex;
std::vector<Registration> g_registrations;

}  // namespace

HookStatus on(std::string_view name, EventCallback callback, void* user,
              std::string& error) {
    error.clear();
    if (!callback) {
        error = "no callback";
        return HookStatus::target_invalid;
    }
    const auto* event = map::find_event(name);
    if (!event) {
        error = "unknown event: " + std::string(name);
        return HookStatus::target_invalid;
    }
    auto& rt = Runtime::get();
    const HookStatus status = rt.hook_rva(event->rva, callback, user, error);
    if (status != HookStatus::ok) return status;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_registrations.push_back({std::string(name), callback, user});
    return HookStatus::ok;
}

bool off(std::string_view name, EventCallback callback, void* user,
         std::string& error) {
    error.clear();
    const auto* event = map::find_event(name);
    if (!event) {
        error = "unknown event: " + std::string(name);
        return false;
    }
    auto& rt = Runtime::get();
    const auto address = reinterpret_cast<std::uintptr_t>(rt.at_rva(event->rva));
    if (!rt.unhook(address, callback, user, error)) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_registrations.erase(
        std::remove_if(g_registrations.begin(), g_registrations.end(),
                       [&](const Registration& r) {
                           return r.name == name && r.callback == callback && r.user == user;
                       }),
        g_registrations.end());
    return true;
}

std::vector<std::string> names() {
    std::vector<std::string> out;
    out.reserve(map::kEvents.size());
    for (const auto& e : map::kEvents) out.emplace_back(e.name);
    return out;
}

std::vector<std::string> active() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<std::string> out;
    for (const auto& r : g_registrations) {
        if (std::find(out.begin(), out.end(), r.name) == out.end()) out.push_back(r.name);
    }
    return out;
}

void clear() {
    std::vector<Registration> registrations;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        registrations.swap(g_registrations);
    }
    auto& rt = Runtime::get();
    for (const auto& r : registrations) {
        const auto* event = map::find_event(r.name);
        if (!event) continue;
        const auto address = reinterpret_cast<std::uintptr_t>(rt.at_rva(event->rva));
        std::string ignored;
        rt.unhook(address, r.callback, r.user, ignored);
    }
}

}  // namespace sw::events
