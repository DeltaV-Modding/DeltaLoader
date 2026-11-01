#pragma once

#include "sw/generated_packet_fields.hpp"
#include "sw/generated_network2.hpp"
#include "sw/generated_packets.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sw::net {

const MessageInfo* find(Family family, std::uint16_t id);
const MessageInfo* find(Family family, const char* name);          // the short name, e.g. "add_map_object"
const MessageInfo* find_class(const char* class_name);             // the RTTI name
std::vector<const MessageInfo*> messages(Family family);
const char* to_string(Family family);                             // "s2c_event", ...
std::size_t count(Family family);

const MessageInfo* identify(const void* object);

// The id stored in an object of a network family (at +8), 0 if unreadable.
std::uint32_t read_id(const void* object);

// The id of a net_message class (1..17; 0 if info is not a net_message). No game needed.
std::uint8_t net_id(const MessageInfo& info);

std::vector<const FieldInfo*> fields(const MessageInfo& info);

// Safe reads of a field of a live message (the offset is inside the message object).
bool read_u8(const void* message, std::uint16_t offset, std::uint8_t& out);
bool read_u16(const void* message, std::uint16_t offset, std::uint16_t& out);
bool read_u32(const void* message, std::uint16_t offset, std::uint32_t& out);
bool read_u64(const void* message, std::uint16_t offset, std::uint64_t& out);
bool read_f32(const void* message, std::uint16_t offset, float& out);
bool read_f64(const void* message, std::uint16_t offset, double& out);
// An mm_string field (kind string16).
bool read_string(const void* message, std::uint16_t offset, std::string& out);
// Three doubles (a block of 24 bytes that holds a position).
bool read_vec3d(const void* message, std::uint16_t offset, double out[3]);

using MessageCallback = void (*)(const MessageInfo& info, void* message, void* stream, bool writing, void* user);
// Called after the message was read, inside the function that handles it (the handler_hint).
using HandledCallback = void (*)(const MessageInfo& info, void* message, void* user);

// Prepares the hooks. False if the game does not match or this is the 32 bit build.
bool watch();

// Calls `callback` every time the message is read or written. False before watch(), for the internal
// families (their slot 0 is a destructor) and for a serialiser that cannot be hooked.
bool on_message(Family family, std::uint16_t id, MessageCallback callback, void* user);

// Calls `callback` after an s2c_event / c2s_request was read, in its handler. Only for ids that have a
// handler_hint. The message is found among the first three arguments of the handler by its vtable.
bool on_handled(Family family, std::uint16_t id, HandledCallback callback, void* user);

// How many times the serialiser of this message ran since on_message / on_handled hooked it.
std::uint64_t seen(Family family, std::uint16_t id);

// Removes the callbacks of this module.
void unwatch();

}  // namespace sw::net
