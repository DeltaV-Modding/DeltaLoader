#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>


namespace sw {


class PeFile {

public:

    static std::optional<PeFile> open(const std::wstring& path, std::string& err);


    bool is_x64() const { return machine_ == 0x8664; }

    bool is_x86() const { return machine_ == 0x014c; }

    std::uint16_t machine() const { return machine_; }

    std::uint32_t size_of_image() const { return size_of_image_; }


    // Returns a pointer to the file bytes at this RVA or nullptr, if the
    // RVA does not lie (completely) in a section with raw data.

    const std::uint8_t* rva_ptr(std::uint32_t rva, std::size_t length) const;


private:

    struct Section {

        std::uint32_t va;

        std::uint32_t virtual_size;

        std::uint32_t raw;

        std::uint32_t raw_size;

    };


    std::vector<std::uint8_t> data_;

    std::vector<Section> sections_;

    std::uint16_t machine_ = 0;

    std::uint32_t size_of_image_ = 0;

};


}  // namespace sw

