#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace sw {

class Objects;

// A reference to an object of the game. Copying it is cheap; it stays
// valid as long as the object exists.
class Obj {
public:
    Obj() = default;

    // The handle points to a known slot of the registry.
    bool valid() const noexcept;

    // The object was not destroyed (as far as the registry knows).
    bool alive() const noexcept;

    // The address of the object. Only the api layer should use it.
    std::uintptr_t address() const noexcept;

    // The name of the class, from the RTTI of the game.
    std::string_view class_name() const noexcept;

    // The object is an instance of this class (or of a derived one).
    bool is(std::string_view cls) const noexcept;

    template <typename T>
    bool get(std::size_t offset, T& out) const noexcept {
        return read_bytes(offset, &out, sizeof(T));
    }
    template <typename T>
    bool set(std::size_t offset, const T& value) const noexcept {
        return write_bytes(offset, &value, sizeof(T));
    }
    bool read_bytes(std::size_t offset, void* out, std::size_t size) const noexcept;
    bool write_bytes(std::size_t offset, const void* data, std::size_t size) const noexcept;

    bool operator==(const Obj& other) const noexcept;
    bool operator!=(const Obj& other) const noexcept;

private:
    friend class Objects;
    static constexpr std::uint32_t kInvalidSlot = 0xffffffffu;

    std::uint32_t slot_ = kInvalidSlot;
    std::uint32_t generation_ = 0;
};

// The registry of the objects that the mod has seen.
class Objects {
public:
    static Objects& instance();

    // Remembers an object. If the address is already known, the old
    // handle becomes invalid: the memory was reused.
    Obj observe(std::uintptr_t address, std::string_view cls);

    // The object was destroyed. All handles of it become invalid.
    void destroy(std::uintptr_t address);

    // The handle of an address, if the registry knows it.
    Obj find(std::uintptr_t address) const;

    // All known objects of a class (and of the classes derived from it).
    std::vector<Obj> of_class(std::string_view cls) const;

    // All known objects.
    std::vector<Obj> all() const;

    // The number of the slots (including the dead ones).
    std::size_t size() const noexcept;

    // The number of the objects that were not destroyed.
    std::size_t alive_count() const noexcept;

    // Forgets everything. Call it after Hook::remove_all().
    void clear();

    // The access of a slot. The generation has to match, otherwise the
    // handle belongs to an object that is gone.
    bool valid_slot(std::uint32_t slot, std::uint32_t generation) const noexcept;
    std::uintptr_t address_of(std::uint32_t slot, std::uint32_t generation) const noexcept;
    std::string_view class_of_slot(std::uint32_t slot, std::uint32_t generation) const noexcept;

private:
    struct Slot {
        std::uintptr_t address = 0;
        std::uint32_t generation = 1;
        std::uint32_t class_id = 0xffffffffu;
        bool alive = false;
    };

    Objects() = default;

    // The index of a class name, or 0xffffffff if it is unknown.
    std::uint32_t class_id(std::string_view cls);
    std::string_view class_name(std::uint32_t id) const;

    mutable std::mutex mutex_;
    std::vector<Slot> slots_;
    std::vector<std::string> class_names_;
};

}  // namespace sw
