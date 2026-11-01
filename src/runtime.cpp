#include "sw/runtime.hpp"

#include "sw/sha256.hpp"
#include "sw/signature.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace sw {

namespace {

std::string to_utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                      nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), n,
                        nullptr, nullptr);
    return out;
}

std::wstring host_image_path() {
    wchar_t path[MAX_PATH * 4]{};
    const DWORD n = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (n == 0 || n >= std::size(path)) return {};
    return std::wstring(path, n);
}

}  // namespace

Runtime& Runtime::get() {
    static Runtime instance;
    return instance;
}

void Runtime::set_error(std::string message) {
    status_.error = std::move(message);
}

bool Runtime::attach(bool check_hash, bool check_signatures) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (status_.attached) return true;

    status_ = RuntimeStatus{};
    status_.signature_total = map::kApi.size();

    HMODULE exe = GetModuleHandleW(nullptr);
    if (!exe) {
        set_error("main module not found");
        return false;
    }
    status_.base = reinterpret_cast<std::uintptr_t>(exe);
    module_base_.store(status_.base, std::memory_order_relaxed);
    status_.image_path = host_image_path();
    if (status_.image_path.empty()) {
        set_error("path of the main module not readable");
        return false;
    }

    if (check_hash) {
        status_.hash_checked = true;
        std::string digest;
        std::string hash_err;
        if (!sha256_file(status_.image_path, digest, hash_err)) {
            set_error("SHA-256 not computable: " + hash_err);
            return false;
        }
        status_.hash_ok = (digest == map::kSha256);
        if (!status_.hash_ok) {
            set_error("SHA-256 does not match the mapping (running binary: " + digest + ")");
            return false;
        }
    } else {
        status_.hash_checked = false;
        status_.hash_ok = true;
    }

    if (check_signatures) {
        status_.signatures_checked = true;
        status_.signature_total = map::kApi.size();
        for (const auto& entry : map::kApi) {
            const auto* p = static_cast<const std::uint8_t*>(at_rva(entry.rva));
            auto pattern = BytePattern::parse(entry.signature);
            if (!p || !pattern || !readable(p, pattern->size()) ||
                !pattern->matches(p, pattern->size())) {
                set_error("signature does not match: " + std::string(entry.api));
                return false;
            }
            ++status_.signature_matches;
        }
        status_.signatures_ok = (status_.signature_matches == status_.signature_total);
        if (!status_.signatures_ok) {
            set_error("not all signatures match");
            return false;
        }
    }

    status_.attached = true;
    status_.error.clear();
    return true;
}

void Runtime::detach() {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = RuntimeStatus{};
    module_base_.store(0, std::memory_order_relaxed);
}

bool Runtime::attached() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_.attached;
}

RuntimeStatus Runtime::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

std::uintptr_t Runtime::base() const {
    return module_base_.load(std::memory_order_relaxed);
}

const void* Runtime::at_rva(std::uint32_t rva) const {
    const std::uintptr_t base = module_base_.load(std::memory_order_relaxed);
    if (!base) return nullptr;
    return reinterpret_cast<const void*>(base + rva);
}

std::uintptr_t Runtime::resolve(std::string_view api, bool verify) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!status_.attached) {
        set_error("not attached");
        return 0;
    }

    const auto* entry = map::find(api);
    if (!entry) {
        set_error("API not in the mapping: " + std::string(api));
        return 0;
    }

    const std::uintptr_t address = status_.base + entry->rva;
    if (verify) {
        auto pattern = BytePattern::parse(entry->signature);
        const auto* p = reinterpret_cast<const std::uint8_t*>(address);
        if (!pattern || !readable(p, pattern->size()) ||
            !pattern->matches(p, pattern->size())) {
            set_error("signature does not match: " + std::string(api));
            return 0;
        }
    }
    return address;
}

bool Runtime::readable(const void* address, std::size_t size) const {
    if (!address || size == 0) return false;
    std::uintptr_t current = reinterpret_cast<std::uintptr_t>(address);
    std::size_t remaining = size;

    while (remaining > 0) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(current), &mbi, sizeof(mbi)) == 0) {
            return false;
        }
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & PAGE_GUARD) return false;
        if (mbi.Protect == PAGE_NOACCESS) return false;

        const std::uintptr_t region_end =
            reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (region_end <= current) return false;
        const std::size_t chunk =
            std::min<std::size_t>(remaining, region_end - current);
        current += chunk;
        remaining -= chunk;
    }
    return true;
}

bool Runtime::write_bytes(void* address, const void* data, std::size_t size) {
    if (!address || !data || size == 0) {
        set_error("write target invalid");
        return false;
    }
    if (!readable(address, size)) {
        set_error("write target not readable");
        return false;
    }

    DWORD old_protect = 0;
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &old_protect)) {
        set_error("memory protection not changeable");
        return false;
    }
    std::memcpy(address, data, size);
    FlushInstructionCache(GetCurrentProcess(), address, size);

    DWORD ignored = 0;
    VirtualProtect(address, size, old_protect, &ignored);
    return true;
}

std::uintptr_t Runtime::vtable_of(std::uintptr_t object) const {
    std::uintptr_t vtable = 0;
    if (!readable(reinterpret_cast<const void*>(object), sizeof(vtable))) return 0;
    std::memcpy(&vtable, reinterpret_cast<const void*>(object), sizeof(vtable));
    return vtable;
}

std::uintptr_t Runtime::virtual_slot(std::uintptr_t object, std::uint32_t index) const {
    const std::uintptr_t vtable = vtable_of(object);
    if (!vtable) return 0;
    const std::uintptr_t slot = vtable + static_cast<std::uintptr_t>(index) * sizeof(void*);
    std::uintptr_t fn = 0;
    if (!readable(reinterpret_cast<const void*>(slot), sizeof(fn))) return 0;
    std::memcpy(&fn, reinterpret_cast<const void*>(slot), sizeof(fn));
    return fn;
}

HookStatus Runtime::hook_ctor(std::string_view cls, HookCallback callback, void* user,
                              std::string& error) {
    error.clear();
    if (!status_.attached) {
        error = "not attached";
        return HookStatus::target_invalid;
    }
    std::vector<const void*> done;
    std::size_t found = 0;
    for (const auto& c : map::kClassCtors) {
        if (c.cls != cls) continue;
        ++found;
        const void* address = at_rva(c.ctor_rva);
        if (!address) {
            error = "constructor address not readable";
        } else if (std::find(done.begin(), done.end(), address) != done.end()) {
            continue;   // same constructor twice in the mapping
        } else {
            const HookStatus st = Hook::install(const_cast<void*>(address), callback, user, error);
            if (st == HookStatus::ok) {
                done.push_back(address);
                continue;
            }
            if (st == HookStatus::already_hooked) return st;   // class already observed
        }
        for (const void* a : done) {
            std::string ignored;
            Hook::remove_callback(const_cast<void*>(a), callback, user, ignored);
        }
        return error.empty() ? HookStatus::target_invalid : HookStatus::relocation_failed;
    }
    if (!found) {
        error = "class has no constructor in the mapping: " + std::string(cls);
        return HookStatus::target_invalid;
    }
    return HookStatus::ok;
}

std::uintptr_t Runtime::class_vtable(std::string_view cls) const {
    const auto* c = map::find_class(cls);
    if (!c) return 0;
    return reinterpret_cast<std::uintptr_t>(at_rva(c->vtable_rva));
}

std::string_view Runtime::class_of(std::uintptr_t object) const {
    const std::uintptr_t vt = vtable_of(object);
    const std::uintptr_t b = base();
    if (!vt || !b || vt < b) return {};
    const std::uintptr_t rva = vt - b;
    for (const auto& c : map::kClasses) {
        if (c.vtable_rva == rva) return c.name;
    }
    return {};
}

bool Runtime::is_instance_of(std::uintptr_t object, std::string_view cls) const {
    const std::uintptr_t want = class_vtable(cls);
    return want != 0 && vtable_of(object) == want;
}

bool Runtime::is_function_start(std::uintptr_t address) const {
#if defined(_WIN64)
    const std::uintptr_t b = base();
    if (!b || address < b) return false;
    DWORD64 image = 0;
    const RUNTIME_FUNCTION* fn = RtlLookupFunctionEntry(static_cast<DWORD64>(address), &image,
                                                        nullptr);
    if (fn) return image + fn->BeginAddress == address;
    const auto* p = reinterpret_cast<const std::uint8_t*>(address);
    return readable(p - 1, 2) && p[-1] == 0xCC && p[0] != 0xCC;
#else
    (void)address;
    return false;
#endif
}

std::uintptr_t Runtime::method_address(std::string_view cls, std::uint32_t slot) const {
    const auto* c = map::find_class(cls);
    if (!c || slot >= c->slots) return 0;
    const auto* entry = static_cast<const std::uint8_t*>(at_rva(c->vtable_rva)) +
                        static_cast<std::size_t>(slot) * sizeof(void*);
    std::uintptr_t fn = 0;
    if (!readable(entry, sizeof(fn))) return 0;
    std::memcpy(&fn, entry, sizeof(fn));
    return fn;
}

HookStatus Runtime::hook_api(std::string_view api, HookCallback callback, void* user,
                             std::string& error) {
    error.clear();
    if (!status_.attached) {
        error = "not attached";
        return HookStatus::target_invalid;
    }
    const std::uintptr_t address = resolve(api, true);
    if (!address) {
        error = "API unknown or signature does not match: " + std::string(api);
        return HookStatus::target_invalid;
    }
    return Hook::install(reinterpret_cast<void*>(address), callback, user, error);
}

HookStatus Runtime::hook_rva(std::uint32_t rva, HookCallback callback, void* user,
                             std::string& error) {
    error.clear();
    if (!status_.attached) {
        error = "not attached";
        return HookStatus::target_invalid;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(at_rva(rva));
    if (!address || !is_function_start(address)) {
        error = "RVA is not a function start";
        return HookStatus::target_invalid;
    }
    return Hook::install(reinterpret_cast<void*>(address), callback, user, error);
}

HookStatus Runtime::hook_method(std::string_view cls, std::uint32_t slot, HookCallback callback,
                                void* user, std::string& error) {
    error.clear();
    if (!status_.attached) {
        error = "not attached";
        return HookStatus::target_invalid;
    }
    const auto* c = map::find_class(cls);
    if (!c) {
        error = "class unknown: " + std::string(cls);
        return HookStatus::target_invalid;
    }
    if (slot >= c->slots) {
        error = "Slot " + std::to_string(slot) + " lies behind the vtable (" +
                std::to_string(c->slots) + " slots)";
        return HookStatus::target_invalid;
    }
    const std::uintptr_t address = method_address(cls, slot);
    if (!address || !is_function_start(address)) {
        error = "Slot does not point to a function start in the game";
        return HookStatus::target_invalid;
    }
    return Hook::install(reinterpret_cast<void*>(address), callback, user, error);
}

bool Runtime::unhook(std::uintptr_t address, HookCallback callback, void* user,
                     std::string& error) {
    return Hook::remove_callback(reinterpret_cast<void*>(address), callback, user, error) ==
           HookStatus::ok;
}

bool Runtime::remove_ctor(std::string_view cls, HookCallback callback, void* user,
                          std::string& error) {
    error.clear();
    bool any = false;
    bool ok = true;
    for (const auto& c : map::kClassCtors) {
        if (c.cls != cls) continue;
        any = true;
        const void* address = at_rva(c.ctor_rva);
        if (!address) continue;
        std::string why;
        const HookStatus st =
            Hook::remove_callback(const_cast<void*>(address), callback, user, why);
        if (st != HookStatus::ok && st != HookStatus::target_invalid) {
            ok = false;
            error = why;
        }
    }
    if (!any) {
        error = "class has no constructor in the mapping: " + std::string(cls);
        return false;
    }
    return ok;
}

}  // namespace sw
