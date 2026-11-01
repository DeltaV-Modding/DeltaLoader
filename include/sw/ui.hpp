#pragma once

#include <cstddef>

namespace sw::ui {

// Where the element sits. x and y are an offset from this point.
enum class Anchor {
    top_left = 0,
    top_center = 1,
    top_right = 2,
    middle_left = 3,
    center = 4,
    middle_right = 5,
    bottom_left = 6,
    bottom_center = 7,
    bottom_right = 8,
};

// Called when the player releases the button.
using ClickCallback = void (*)(void* user);

namespace main_menu {

// Shows a TEXT in the main menu: a real text element of the game (no button, no
// icon, nothing to click). width and height are the box of the text in menu
// units, color is ARGB (the default is the grey of the game's own texts).
// Returns false if the layer cannot work (32 bit, or the game functions do not
// match).
bool add_text(const char* text, Anchor anchor = Anchor::bottom_right, float x = 0.0f,
              float y = 0.04f, float width = 0.8f, float height = 0.14f,
              unsigned color = 0xFFE6E6E6u);

// Shows a button. The callback may be null.
bool add_button(const char* text, const char* icon, Anchor anchor, ClickCallback callback,
                void* user, float x = 0.0f, float y = 0.0f);

// Removes everything this mod added to the menu (the elements disappear
// with the next menu, the callbacks are released at once). The skeleton
// of the mod calls this on unload.
void clear();

// The number of elements that are registered (not necessarily built yet).
std::size_t count();

}  // namespace main_menu
}  // namespace sw::ui
