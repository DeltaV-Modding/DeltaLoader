#pragma once


#include <cstddef>

#include <cstdint>

#include <optional>

#include <string_view>

#include <vector>


namespace sw {


struct BytePattern {

    std::vector<std::uint8_t> bytes;

    std::vector<std::uint8_t> mask;  // 0 = placeholder, 1 = must match


    static std::optional<BytePattern> parse(std::string_view text);


    bool matches(const std::uint8_t* data, std::size_t size) const;

    std::size_t size() const { return bytes.size(); }

};


}  // namespace sw

