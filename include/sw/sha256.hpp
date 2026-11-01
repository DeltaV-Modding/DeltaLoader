#pragma once


#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>


namespace sw {


std::string sha256_hex(const std::uint8_t* data, std::size_t size);

std::string sha256_hex(std::string_view text);


// Reads the file in 64-KByte blocks and returns the hex digest in lower

// case. On a read error out stays empty and the function returns false.

bool sha256_file(const std::wstring& path, std::string& out_hex, std::string& err);


}  // namespace sw

