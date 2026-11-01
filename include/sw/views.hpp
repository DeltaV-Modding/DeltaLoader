#pragma once

#include "sw/obj.hpp"

#include <cstdint>
#include <string_view>

namespace sw::views {

// The base of all UI elements ("c_ui_element_*"): position, size, anchor, visibility, parent.
class UiElement {
public:
    explicit UiElement(Obj obj) : obj_(obj) {}

    // The handle is alive and the class is a UI element of the game.
    explicit operator bool() const noexcept {
        if (!obj_.alive()) return false;
        constexpr std::string_view kPrefix = "c_ui_element";
        return obj_.class_name().substr(0, kPrefix.size()) == kPrefix;
    }

    float x() const noexcept { return get<float>(kX); }
    float y() const noexcept { return get<float>(kY); }
    float width() const noexcept { return get<float>(kWidth); }
    float height() const noexcept { return get<float>(kHeight); }
    // 0..8: x left/center/right, y top/middle/bottom (see sw::ui::Anchor)
    std::uint32_t anchor() const noexcept { return get<std::uint32_t>(kAnchor); }
    bool visible() const noexcept { return get<std::uint8_t>(kVisible) != 0; }

    bool set_x(float v) const noexcept { return obj_.set(kX, v); }
    bool set_y(float v) const noexcept { return obj_.set(kY, v); }
    bool set_visible(bool on) const noexcept {
        return obj_.set(kVisible, static_cast<std::uint8_t>(on ? 1 : 0));
    }

    // The parent as a handle; an invalid handle if there is none or the
    // game has not been seen creating it.
    Obj parent() const noexcept {
        std::uintptr_t p = 0;
        if (!obj_.get(kParent, p) || !p) return Obj();
        return Objects::instance().find(p);
    }

    Obj object() const noexcept { return obj_; }

private:
    static constexpr std::size_t kX = 0x08;
    static constexpr std::size_t kY = 0x0c;
    static constexpr std::size_t kWidth = 0x10;
    static constexpr std::size_t kHeight = 0x14;
    static constexpr std::size_t kAnchor = 0x18;
    static constexpr std::size_t kVisible = 0x1c;
    static constexpr std::size_t kParent = 0x20;

    template <typename T>
    T get(std::size_t offset) const noexcept {
        T value{};
        obj_.get(offset, value);
        return value;
    }

    Obj obj_;
};

// A button of the game ("c_ui_element_point_button*", "..._rectangle_button*").
// Offsets from sw/protos.hpp: action ID at +0x90 (0 = none), callback at
// +0xd8. The callback pointer itself is not exposed.
class UiButton {
public:
    explicit UiButton(Obj obj) : ui_(obj) {}

    explicit operator bool() const noexcept {
        if (!ui_) return false;
        return ui_.object().class_name().find("button") != std::string_view::npos;
    }

    UiElement& element() noexcept { return ui_; }
    const UiElement& element() const noexcept { return ui_; }

    std::uint32_t action() const noexcept {
        std::uint32_t v = 0;
        ui_.object().get(0x90, v);
        return v;
    }
    bool has_callback() const noexcept {
        std::uintptr_t v = 0;
        return ui_.object().get(0xd8, v) && v != 0;
    }

private:
    UiElement ui_;
};

}  // namespace sw::views
