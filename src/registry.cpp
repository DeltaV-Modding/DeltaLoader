#include "sw/obj.hpp"

#include "sw/runtime.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>

namespace sw {

namespace {

constexpr std::uint32_t kNoClass = 0xffffffffu;

}  // namespace


bool Obj::valid() const noexcept {
    if (slot_ == kInvalidSlot) return false;
    return Objects::instance().valid_slot(slot_, generation_);
}

bool Obj::alive() const noexcept { return valid(); }

std::uintptr_t Obj::address() const noexcept {
    if (slot_ == kInvalidSlot) return 0;
    return Objects::instance().address_of(slot_, generation_);
}

std::string_view Obj::class_name() const noexcept {
    if (slot_ == kInvalidSlot) return {};
    return Objects::instance().class_of_slot(slot_, generation_);
}

bool Obj::is(std::string_view cls) const noexcept {
    const auto name = class_name();
    if (name.empty()) return false;
    if (name == cls) return true;
    // A derived class runs through the constructor of the base class and
    // therefore also appears in the list of the base class.
    const auto* entry = map::find_class(name);
    if (!entry) return false;
    const auto* wanted = map::find_class(cls);
    if (!wanted) return false;
    return entry->vtable_rva == wanted->vtable_rva;
}

bool Obj::read_bytes(std::size_t offset, void* out, std::size_t size) const noexcept {
    const std::uintptr_t base = address();
    if (!base || !out || size == 0 || offset > 0x100000) return false;
    const auto* from = reinterpret_cast<const void*>(base + offset);
    if (!Runtime::get().readable(from, size)) return false;
    std::memcpy(out, from, size);
    return true;
}

bool Obj::write_bytes(std::size_t offset, const void* data, std::size_t size) const noexcept {
    const std::uintptr_t base = address();
    if (!base || !data || size == 0 || offset > 0x100000) return false;
    void* to = reinterpret_cast<void*>(base + offset);
    if (!Runtime::get().readable(to, size)) return false;
    return Runtime::get().write_bytes(to, data, size);
}

bool Obj::operator==(const Obj& other) const noexcept {
    return slot_ == other.slot_ && generation_ == other.generation_;
}

bool Obj::operator!=(const Obj& other) const noexcept { return !(*this == other); }


Objects& Objects::instance() {
    static Objects registry;
    return registry;
}

std::uint32_t Objects::class_id(std::string_view cls) {
    for (std::size_t i = 0; i < class_names_.size(); ++i) {
        if (class_names_[i] == cls) return static_cast<std::uint32_t>(i);
    }
    class_names_.emplace_back(cls);
    return static_cast<std::uint32_t>(class_names_.size() - 1);
}

std::string_view Objects::class_name(std::uint32_t id) const {
    if (id == kNoClass || id >= class_names_.size()) return {};
    return class_names_[id];
}

Obj Objects::observe(std::uintptr_t address, std::string_view cls) {
    if (!address) return Obj();
    std::lock_guard<std::mutex> lock(mutex_);
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        Slot& s = slots_[i];
        if (s.address != address) continue;
        s.generation++;
        s.class_id = class_id(cls);
        s.alive = true;
        Obj obj;
        obj.slot_ = static_cast<std::uint32_t>(i);
        obj.generation_ = s.generation;
        return obj;
    }
    Slot s;
    s.address = address;
    s.generation = 1;
    s.class_id = class_id(cls);
    s.alive = true;
    slots_.push_back(s);
    Obj obj;
    obj.slot_ = static_cast<std::uint32_t>(slots_.size() - 1);
    obj.generation_ = s.generation;
    return obj;
}

void Objects::destroy(std::uintptr_t address) {
    if (!address) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& s : slots_) {
        if (s.address != address) continue;
        s.alive = false;
        s.generation++;
        return;
    }
}

Obj Objects::find(std::uintptr_t address) const {
    if (!address) return Obj();
    std::lock_guard<std::mutex> lock(mutex_);
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].address != address) continue;
        Obj obj;
        obj.slot_ = static_cast<std::uint32_t>(i);
        obj.generation_ = slots_[i].generation;
        return obj;
    }
    return Obj();
}

std::vector<Obj> Objects::of_class(std::string_view cls) const {
    std::vector<Obj> out;
    std::lock_guard<std::mutex> lock(mutex_);
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        const Slot& s = slots_[i];
        if (!s.alive) continue;
        if (class_name(s.class_id) != cls) continue;
        Obj obj;
        obj.slot_ = static_cast<std::uint32_t>(i);
        obj.generation_ = s.generation;
        out.push_back(obj);
    }
    return out;
}

std::vector<Obj> Objects::all() const {
    std::vector<Obj> out;
    std::lock_guard<std::mutex> lock(mutex_);
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (!slots_[i].alive) continue;
        Obj obj;
        obj.slot_ = static_cast<std::uint32_t>(i);
        obj.generation_ = slots_[i].generation;
        out.push_back(obj);
    }
    return out;
}

std::size_t Objects::size() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return slots_.size();
}

std::size_t Objects::alive_count() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t n = 0;
    for (const auto& s : slots_) {
        if (s.alive) ++n;
    }
    return n;
}

void Objects::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    slots_.clear();
    class_names_.clear();
}

bool Objects::valid_slot(std::uint32_t slot, std::uint32_t generation) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (slot >= slots_.size()) return false;
    const Slot& s = slots_[slot];
    return s.alive && s.generation == generation;
}

std::uintptr_t Objects::address_of(std::uint32_t slot, std::uint32_t generation) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (slot >= slots_.size()) return 0;
    const Slot& s = slots_[slot];
    if (!s.alive || s.generation != generation) return 0;
    return s.address;
}

std::string_view Objects::class_of_slot(std::uint32_t slot, std::uint32_t generation) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (slot >= slots_.size()) return {};
    const Slot& s = slots_[slot];
    if (!s.alive || s.generation != generation) return {};
    return class_name(s.class_id);
}

}  // namespace sw
