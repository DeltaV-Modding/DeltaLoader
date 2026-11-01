#include "sw/pe_file.hpp"

#include <cstring>
#include <fstream>
#include <iterator>

namespace sw {

namespace {

template <typename T>
T read_le(const std::uint8_t* p) {
    T v{};
    std::memcpy(&v, p, sizeof(T));
    return v;
}

}  // namespace

std::optional<PeFile> PeFile::open(const std::wstring& path, std::string& err) {
    err.clear();

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        err = "file not readable";
        return std::nullopt;
    }

    PeFile pe;
    pe.data_.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (pe.data_.size() < 0x40) {
        err = "file too small for a PE header";
        return std::nullopt;
    }

    const auto* d = pe.data_.data();
    if (read_le<std::uint16_t>(d) != 0x5a4d) {
        err = "no MZ header";
        return std::nullopt;
    }
    const std::uint32_t nt = read_le<std::uint32_t>(d + 0x3c);
    if (nt + 24 > pe.data_.size() || read_le<std::uint32_t>(d + nt) != 0x00004550u) {
        err = "no PE header";
        return std::nullopt;
    }

    const std::uint16_t machine = read_le<std::uint16_t>(d + nt + 4);
    const std::uint16_t section_count = read_le<std::uint16_t>(d + nt + 6);
    const std::uint16_t optional_size = read_le<std::uint16_t>(d + nt + 20);
    const std::uint16_t magic = read_le<std::uint16_t>(d + nt + 24);

    pe.machine_ = machine;
    if (machine != 0x8664 && machine != 0x014c) {
        err = "neither an x64 nor an x86 binary";
        return std::nullopt;
    }
    if (magic != 0x20b && magic != 0x10b) {
        err = "no PE32/PE32+ header";
        return std::nullopt;
    }
    // SizeOfImage lies at offset 56 in both optional header forms.
    pe.size_of_image_ = read_le<std::uint32_t>(d + nt + 24 + 56);

    const std::size_t section_table = static_cast<std::size_t>(nt) + 24 + optional_size;
    if (section_table + static_cast<std::size_t>(section_count) * 40 > pe.data_.size()) {
        err = "section table lies outside the file";
        return std::nullopt;
    }

    for (std::uint16_t i = 0; i < section_count; ++i) {
        const std::uint8_t* s = d + section_table + static_cast<std::size_t>(i) * 40;
        Section sec{};
        sec.virtual_size = read_le<std::uint32_t>(s + 8);
        sec.va = read_le<std::uint32_t>(s + 12);
        sec.raw_size = read_le<std::uint32_t>(s + 16);
        sec.raw = read_le<std::uint32_t>(s + 20);
        pe.sections_.push_back(sec);
    }

    return pe;
}

const std::uint8_t* PeFile::rva_ptr(std::uint32_t rva, std::size_t length) const {
    for (const auto& s : sections_) {
        const std::uint64_t begin = s.va;
        const std::uint64_t end = begin + (s.virtual_size > s.raw_size ? s.virtual_size : s.raw_size);
        if (rva < begin || static_cast<std::uint64_t>(rva) + length > end) continue;
        if (static_cast<std::uint64_t>(rva) + length > begin + s.raw_size) return nullptr;
        const std::uint64_t off = s.raw + (rva - begin);
        if (off + length > data_.size()) return nullptr;
        return data_.data() + off;
    }
    return nullptr;
}

}  // namespace sw
