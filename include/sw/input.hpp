#pragma once

#include "sw/generated_input.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sw::input {

std::vector<const ActionInfo*> actions();                         // all 134, sorted by id
const ActionInfo* find_action(std::uint16_t id);
const ActionInfo* find_action(const char* name);                  // "action_jump"

const char* key_name(int game_key);
int game_key_from_glfw(int glfw_key);
int glfw_key_from_game(int game_key);
const char* default_key_name(const ActionInfo& action);


struct Binding { int key; int mouse; int pad_button; int pad_controller; int pad_axis; };
bool binding(const ActionInfo& action, Binding& out);
bool binding(const char* action_name, Binding& out);
// Name of the current keyboard binding ("space", "" when unassigned or not readable).
const char* binding_key_name(const ActionInfo& action);
// Derived state: true in down when the key or the mouse button of the CURRENT binding is held according to GLFW's state. False (return)
// when nothing is readable. Controller bindings are not looked at. Keyboard state is the window's own, not the game's gated state.
bool is_down(const ActionInfo& action, bool& down);
bool is_down(const char* action_name, bool& down);
// The actions whose current keyboard binding is this game key (several actions can share one key). Needs the table.
std::vector<const ActionInfo*> actions_for_key(int game_key);

// Prepares the hooks. False if the runtime is not attached or this is the 32 bit build.
bool watch();
void unwatch();

// action: 0 release, 1 press, 2 repeat. game_key is -1 for a key the game drops. window is the GLFWwindow*.
using KeyCallback = void (*)(int glfw_key, int game_key, int action, void* window, void* user);
using CharCallback = void (*)(std::uint32_t codepoint, void* window, void* user);
// button: GLFW mouse button 0..7 (0 left, 1 right, 2 middle), action 0 release / 1 press.
using MouseButtonCallback = void (*)(int button, int action, void* window, void* user);

// One callback of each kind (a second call replaces the first). False before watch() or when the hook is refused.
bool on_key(KeyCallback callback, void* user);
bool on_char(CharCallback callback, void* user);
bool on_mouse_button(MouseButtonCallback callback, void* user);

// The GLFW window and the platform object (nullptr until the window exists; checked: window +0x20 and
// platform +0xd38 point at each other).
void* glfw_window();
void* platform();

// Safe reads of the state the game callbacks keep (false when not readable / no window).
bool mouse_down(int button, bool& down);                          // button 0..7
bool cursor_ndc(float& x, float& y);                              // -1..1 over the window client area
bool glfw_key_state(int glfw_key, std::uint8_t& raw);             // GLFW's own state byte of the window (0 = up)
// Number of events waiting in a ring buffer of the platform (the game drains them once per frame).
enum class Ring { chars, key_events, key_repeats, mouse_events };
bool queued(Ring ring, std::uint32_t& count);

}  // namespace sw::input
