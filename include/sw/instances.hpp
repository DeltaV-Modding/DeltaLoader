#pragma once

#include "sw/hook.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>


namespace sw::Instances {



HookStatus track(std::string_view cls, std::string& error);



void untrack(std::string_view cls);
std::vector<std::uintptr_t> list(std::string_view cls, bool only_alive = true);
std::size_t count(std::string_view cls);

std::vector<std::string> tracked();

void clear();


}  // namespace sw::Instances

