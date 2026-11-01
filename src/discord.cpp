#include "sw/discord.hpp"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace sw::discord {

std::string json_escape(const std::string& text) {
    std::string out;
    for (const unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof(b), "\\u%04x", c);
                    out += b;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

bool valid_url(const std::string& url) {
    const bool scheme = url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0;
    if (!scheme || url.size() > 512) return false;
    for (const unsigned char c : url)
        if (c <= 0x20 || c == '"' || c == '\\') return false;
    return true;
}

std::string activity_json(const Config& config, const Presence& presence) {
    std::string a = "{\"details\":\"" + json_escape(presence.details) + "\",\"state\":\"" + json_escape(presence.state) + "\"";
    if (presence.start > 0) a += ",\"timestamps\":{\"start\":" + std::to_string(presence.start) + "}";
    if (!config.large_image.empty() || !config.small_image.empty()) {
        a += ",\"assets\":{";
        bool first = true;
        auto put = [&](const char* key, const std::string& value) {
            if (value.empty()) return;
            if (!first) a += ",";
            first = false;
            a += std::string("\"") + key + "\":\"" + json_escape(value) + "\"";
        };
        put("large_image", config.large_image);
        put("large_text", config.large_text);
        put("small_image", config.small_image);
        put("small_text", config.small_text);
        a += "}";
    }
    if (!config.buttons.empty()) {
        a += ",\"buttons\":[";
        for (std::size_t i = 0; i < config.buttons.size() && i < 2; ++i) {
            if (i) a += ",";
            a += "{\"label\":\"" + json_escape(config.buttons[i].label) + "\",\"url\":\"" + json_escape(config.buttons[i].url) + "\"}";
        }
        a += "]";
    }
    a += "}";
    return a;
}

namespace {

std::mutex g_mutex;
std::thread g_thread;
std::atomic<bool> g_stop{false};
std::atomic<bool> g_running{false};
std::atomic<bool> g_connected{false};

struct Pipe {
    HANDLE h = INVALID_HANDLE_VALUE;
    bool open() const { return h != INVALID_HANDLE_VALUE; }
    void close() {
        if (open()) CloseHandle(h);
        h = INVALID_HANDLE_VALUE;
    }
};

bool write_frame(Pipe& pipe, std::uint32_t opcode, const std::string& json) {
    if (!pipe.open()) return false;
    std::vector<char> buf(8 + json.size());
    const std::uint32_t length = static_cast<std::uint32_t>(json.size());
    std::memcpy(buf.data(), &opcode, 4);
    std::memcpy(buf.data() + 4, &length, 4);
    std::memcpy(buf.data() + 8, json.data(), json.size());
    DWORD written = 0;
    return WriteFile(pipe.h, buf.data(), static_cast<DWORD>(buf.size()), &written, nullptr) && written == buf.size();
}

// Reads and drops what Discord sent (the answers); false when the pipe is gone. Sets ready when READY was seen.
bool drain(Pipe& pipe, bool& ready) {
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe.h, nullptr, 0, nullptr, &available, nullptr)) return false;
        if (available < 8) return true;
        std::uint32_t header[2] = {};
        DWORD got = 0;
        if (!ReadFile(pipe.h, header, 8, &got, nullptr) || got != 8) return false;
        if (header[1] > (1u << 20)) return false;
        std::string body(header[1], '\0');
        DWORD done = 0;
        while (done < body.size()) {
            DWORD n = 0;
            if (!ReadFile(pipe.h, &body[done], static_cast<DWORD>(body.size()) - done, &n, nullptr) || n == 0) return false;
            done += n;
        }
        if (body.find("\"READY\"") != std::string::npos) ready = true;
        if (header[0] == 2) return false;   // CLOSE
    }
}

bool connect(const Config& config, Pipe& pipe) {
    for (int i = 0; i < config.pipes && !g_stop.load(); ++i) {
        const std::string name = config.pipe_prefix + std::to_string(i);
        HANDLE h = CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        pipe.h = h;
        if (!write_frame(pipe, 0, "{\"v\":1,\"client_id\":\"" + json_escape(config.client_id) + "\"}")) {
            pipe.close();
            continue;
        }
        for (int w = 0; w < 50 && !g_stop.load(); ++w) {   // READY, about 5s at most
            bool ready = false;
            if (!drain(pipe, ready)) break;
            if (ready) return true;
            Sleep(100);
        }
        pipe.close();
    }
    return false;
}

bool send_activity(Pipe& pipe, const std::string& activity_or_null, long long nonce) {
    const std::string payload = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(GetCurrentProcessId()) +
                                ",\"activity\":" + activity_or_null + "},\"nonce\":\"" + std::to_string(nonce) + "\"}";
    return write_frame(pipe, 1, payload);
}

void run(Config config, Provider provider, void* user) {
    Pipe pipe;
    std::string last;
    long long nonce = 0;
    unsigned since_try = config.retry_ms;   // try at once
    while (!g_stop.load()) {
        if (pipe.open()) {
            bool ignore = false;
            if (!drain(pipe, ignore)) {
                pipe.close();
                g_connected.store(false);
                last.clear();
            }
        }
        if (!pipe.open()) {
            if (since_try >= config.retry_ms) {
                since_try = 0;
                if (connect(config, pipe)) {
                    g_connected.store(true);
                    last.clear();
                }
            }
            since_try += config.period_ms;
        }
        if (pipe.open()) {
            Presence p;
            const bool show = provider ? provider(p, user) : false;
            const std::string activity = show ? activity_json(config, p) : "null";
            if (activity != last) {
                if (send_activity(pipe, activity, ++nonce)) last = activity;
                else {
                    pipe.close();
                    g_connected.store(false);
                    last.clear();
                }
            }
        }
        for (unsigned waited = 0; waited < config.period_ms && !g_stop.load(); waited += 20) Sleep(20);
    }
    if (pipe.open()) {
        send_activity(pipe, "null", ++nonce);
        Sleep(50);
        pipe.close();
    }
    g_connected.store(false);
}

}  // namespace

bool start(const Config& config, Provider provider, void* user) {
    if (config.client_id.empty()) return false;
    for (const Button& b : config.buttons)
        if (b.label.empty() || b.label.size() > 32 || !valid_url(b.url)) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_running.load()) return false;
    g_stop.store(false);
    g_running.store(true);
    g_thread = std::thread(run, config, provider, user);
    return true;
}

void stop() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_running.load()) return;
    g_stop.store(true);
    if (g_thread.joinable()) g_thread.join();
    g_running.store(false);
}

bool connected() { return g_connected.load(); }

}  // namespace sw::discord
