#include "sw/signature.hpp"

#include <cctype>

namespace sw {

namespace {

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

std::optional<BytePattern> BytePattern::parse(std::string_view text) {
    BytePattern p;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (i >= text.size()) break;

        const char a = text[i];
        if (a == '?') {
            // "?" or "??"
            ++i;
            if (i < text.size() && text[i] == '?') ++i;
            p.bytes.push_back(0);
            p.mask.push_back(0);
            continue;
        }
        if (i + 1 >= text.size()) return std::nullopt;
        const int hi = hex_value(text[i]);
        const int lo = hex_value(text[i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        p.bytes.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
        p.mask.push_back(1);
        i += 2;
    }

    if (p.bytes.empty() || p.bytes.size() > 64) return std::nullopt;
    return p;
}

bool BytePattern::matches(const std::uint8_t* data, std::size_t size) const {
    if (!data || size < bytes.size()) return false;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (mask[i] && data[i] != bytes[i]) return false;
    }
    return true;
}

}  // namespace sw
