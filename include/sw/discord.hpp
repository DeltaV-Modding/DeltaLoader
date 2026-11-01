#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sw::discord {

struct Button {
    std::string label;   // at most 32 characters
    std::string url;     // http or https
};

struct Config {
    std::string client_id;                 // the Application ID of the Discord application
    std::string large_image, large_text;   // names of art assets of the application
    std::string small_image, small_text;
    std::vector<Button> buttons;           // at most two are sent
    std::string pipe_prefix = "\\\\.\\pipe\\discord-ipc-";   // + 0 .. pipes-1
    int pipes = 10;
    unsigned period_ms = 1000;             // how often the provider is asked
    unsigned retry_ms = 15000;             // how often a connection is tried while there is none
};

struct Presence {
    std::string details, state;
    std::int64_t start = 0;                // unix time of the start; 0 = none
};

// Called on the presence thread once per period. Return false to show nothing (the activity is cleared).
using Provider = bool (*)(Presence& out, void* user);

// Starts the thread. False when the client id is empty, a button is unusable or it is running already.
bool start(const Config& config, Provider provider, void* user);
// Clears the activity, closes the pipe, ends the thread. Harmless when it is not running.
void stop();
// Discord answered the handshake with READY and the pipe is open.
bool connected();

// The pieces, public for the tests.
std::string json_escape(const std::string& text);
std::string activity_json(const Config& config, const Presence& presence);   // the object for "activity"
bool valid_url(const std::string& url);                                      // http:// or https://, no spaces or quotes

}  // namespace sw::discord
