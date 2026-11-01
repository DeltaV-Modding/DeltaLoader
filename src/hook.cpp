#include "sw/hook.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace sw {

const char* to_string(HookStatus status) {
    switch (status) {
    case HookStatus::ok: return "ok";
    case HookStatus::unsupported_arch: return "architecture not supported (x64 only)";
    case HookStatus::target_invalid: return "target does not lie in the main module";
    case HookStatus::already_hooked: return "a hook is already in place at this location";
    case HookStatus::decode_failed: return "instructions at the target not safely decodable";
    case HookStatus::relocation_failed: return "jump not relocatable or no memory in reach";
    case HookStatus::alloc_failed: return "no memory in reach of the target";
    case HookStatus::write_failed: return "writing into the process failed";
    }
    return "unknown";
}

namespace {

struct Insn {
    std::size_t length = 0;
    bool ok = false;
    bool rel8 = false;
    int short_cc = -2;
    bool rel32 = false;
    bool rip = false;
    std::size_t disp_off = 0;
};

bool read_modrm(const std::uint8_t* start, const std::uint8_t*& p, const std::uint8_t* end,
                Insn& ins) {
    if (p >= end) return false;
    const std::uint8_t modrm = *p++;
    const std::uint8_t mod = (modrm >> 6) & 3;
    const std::uint8_t rm = modrm & 7;
    if (mod == 3) return true;

    bool rip_base = false;
    if (rm == 4) {
        if (p >= end) return false;
        const std::uint8_t sib = *p++;
        if (mod == 0 && (sib & 7) == 5) rip_base = true;
    } else if (mod == 0 && rm == 5) {
        rip_base = true;
    }

    if (mod == 0) {
        if (rip_base) {
            if (p + 4 > end) return false;
            ins.disp_off = static_cast<std::size_t>(p - start);
            ins.rip = true;
            p += 4;
        }
        return true;
    }
    if (mod == 1) {
        if (p + 1 > end) return false;
        p += 1;
        return true;
    }
    if (mod == 2) {
        if (p + 4 > end) return false;
        p += 4;
        return true;
    }
    return true;
}

Insn decode(const std::uint8_t* p, const std::uint8_t* end) {
    Insn ins;
    const std::uint8_t* start = p;
    bool rex_w = false;

    while (p < end) {
        const std::uint8_t b = *p;
        if (b == 0x66 || b == 0x67 || b == 0xf2 || b == 0xf3 || b == 0x2e || b == 0x36 ||
            b == 0x3e || b == 0x26 || b == 0x64 || b == 0x65 || b == 0xf0) {
            ++p;
            continue;
        }
        if (b >= 0x40 && b <= 0x4f) {
            rex_w = (b & 0x08) != 0;
            ++p;
            continue;
        }
        break;
    }
    if (p >= end) return ins;

    const std::uint8_t op = *p++;
    auto done = [&]() {
        ins.length = static_cast<std::size_t>(p - start);
        ins.ok = ins.length > 0;
        return ins;
    };

    switch (op) {
    // push/pop Register, nop, int3, ret
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
    case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:
    case 0x90: case 0x98: case 0x99: case 0xcc: case 0xc3:
        return done();
    case 0xc2:                                        // ret imm16
        if (p + 2 > end) return ins;
        p += 2;
        return done();
    case 0x68:                                        // push imm32
        if (p + 4 > end) return ins;
        p += 4;
        return done();
    case 0x6a:                                        // push imm8
        if (p + 1 > end) return ins;
        p += 1;
        return done();
    case 0xe8: case 0xe9:                             // call/jmp rel32
        if (p + 4 > end) return ins;
        ins.disp_off = static_cast<std::size_t>(p - start);
        ins.rel32 = true;
        p += 4;
        return done();
    case 0xeb:                                        // jmp rel8
        if (p + 1 > end) return ins;
        p += 1;
        ins.rel8 = true;
        ins.short_cc = -1;
        return done();
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f:
        // jcc rel8 - typical at the function start: "test rcx, rcx / je ..."
        if (p + 1 > end) return ins;
        p += 1;
        ins.rel8 = true;
        ins.short_cc = op & 0x0f;
        return done();
    case 0x0f: {
        if (p >= end) return ins;
        const std::uint8_t op2 = *p++;
        if ((op2 & 0xf0) == 0x80) {                   // jcc rel32
            if (p + 4 > end) return ins;
            ins.disp_off = static_cast<std::size_t>(p - start);
            ins.rel32 = true;
            p += 4;
            return done();
        }
        if (op2 == 0x1f) {                            // multi-byte nop
            if (!read_modrm(start, p, end, ins)) return ins;
            return done();
        }
        // 0F 10/11/28/29/6e/7e/af/b6/b7/be/bf, cmov, setcc etc.
        if (!read_modrm(start, p, end, ins)) return ins;
        if (op2 == 0x70 || op2 == 0x71 || op2 == 0x72 || op2 == 0x73 || op2 == 0xa4 ||
            op2 == 0xac || op2 == 0xba) {            // ... with imm8
            if (p + 1 > end) return ins;
            p += 1;
        }
        return done();
    }
    default:
        break;
    }

    if (op >= 0xb8 && op <= 0xbf) {                   // mov r, imm
        const std::size_t n = rex_w ? 8 : 4;
        if (p + n > end) return ins;
        p += n;
        return done();
    }

    // ModRM instructions
    const bool imm8 = (op == 0x80 || op == 0x83 || op == 0xc0 || op == 0xc1 || op == 0x6b);
    const bool imm32 = (op == 0x81 || op == 0x69);
    if (imm8 || imm32 || op == 0x88 || op == 0x89 || op == 0x8a || op == 0x8b || op == 0x8d ||
        op == 0x8f || op == 0xc6 || op == 0xc7 || op == 0x00 || op == 0x01 || op == 0x02 ||
        op == 0x03 || op == 0x08 || op == 0x09 || op == 0x0a || op == 0x0b || op == 0x10 ||
        op == 0x11 || op == 0x18 || op == 0x19 || op == 0x1a || op == 0x1b || op == 0x20 ||
        op == 0x21 || op == 0x22 || op == 0x23 || op == 0x28 || op == 0x29 || op == 0x2a ||
        op == 0x2b || op == 0x30 || op == 0x31 || op == 0x32 || op == 0x33 || op == 0x38 ||
        op == 0x39 || op == 0x3a || op == 0x3b || op == 0x62 || op == 0x63 || op == 0x84 ||
        op == 0x85 || op == 0x86 || op == 0x87 || op == 0xd0 || op == 0xd1 || op == 0xd2 ||
        op == 0xd3 || op == 0xf6 || op == 0xf7 || op == 0xfe || op == 0xff) {
        if (!read_modrm(start, p, end, ins)) return ins;
        if (op == 0xf6 || op == 0xf7) {               // test group: /0 and /1 have imm
            const std::uint8_t modrm = *(p - 1);
            const std::uint8_t reg = (modrm >> 3) & 7;
            if (reg == 0 || reg == 1) {
                const std::size_t n = (op == 0xf6) ? 1 : 4;
                if (p + n > end) return ins;
                p += n;
            }
        } else if (imm8) {
            if (p + 1 > end) return ins;
            p += 1;
        } else if (imm32) {
            if (p + 4 > end) return ins;
            p += 4;
        }
        return done();
    }

    return ins;   // unknown opcode -> not decodable
}

// Decodes the instructions at the target until at least 5 bytes (jmp rel32)
// are covered. Only whole instructions are overwritten.
HookStatus plan_insns(const std::uint8_t* code, std::size_t available, std::vector<Insn>& insns,
                      std::size_t& len, std::string& error) {
    insns.clear();
    len = 0;
    const std::size_t window = available < 64 ? available : 64;
    for (int i = 0; i < 8; ++i) {
        const Insn ins = decode(code + len, code + window);
        if (!ins.ok || ins.length == 0) {
            error = "instructions not safely decodable";
            return HookStatus::decode_failed;
        }
        insns.push_back(ins);
        len += ins.length;
        if (len >= 5) break;
    }
    if (len < 5 || len > 32) {
        error = "no suitable overwrite possible";
        return HookStatus::decode_failed;
    }
    return HookStatus::ok;
}

struct Code {
    std::vector<std::uint8_t> b;
    void u8(std::uint8_t v) { b.push_back(v); }
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
    }
    void u64(std::uint64_t v) {
        for (int i = 0; i < 8; ++i) b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
    }
};

constexpr std::size_t kMaxCallbacks = 32;
struct Slots {
    std::atomic<std::uint32_t> count;
    struct Item {
        std::atomic<HookCallback> callback;
        std::atomic<ReplaceCallback> replace;  // nullptr for a normal hook
        void* user;
    } items[kMaxCallbacks];
};

struct Control {
    std::atomic<std::uint64_t> result;
    std::atomic<std::uint32_t> skip;
};

std::atomic<int> g_inflight{0};

thread_local const std::uint64_t* t_args = nullptr;

void dispatch(void* self, void* slots_ptr, const std::uint64_t* args, Control* control) {
    g_inflight.fetch_add(1, std::memory_order_acq_rel);
    const std::uint64_t* const vorher = t_args;
    t_args = args;
    auto* slots = static_cast<Slots*>(slots_ptr);
    if (control) control->skip.store(0, std::memory_order_release);
    const std::uint32_t n = slots->count.load(std::memory_order_acquire);
    for (std::uint32_t i = 0; i < n && i < kMaxCallbacks; ++i) {
        const ReplaceCallback replace = slots->items[i].replace.load(std::memory_order_acquire);
        if (replace) {
            std::uint64_t value = 0;
            if (replace(self, slots->items[i].user, &value)) {
                if (control) {
                    control->result.store(value, std::memory_order_release);
                    control->skip.store(1, std::memory_order_release);
                }
                break;
            }
            continue;
        }
        const HookCallback cb = slots->items[i].callback.load(std::memory_order_acquire);
        if (cb) cb(self, slots->items[i].user);
    }
    t_args = vorher;
    g_inflight.fetch_sub(1, std::memory_order_acq_rel);
}

constexpr std::size_t kRetByte = 0xF0;
constexpr std::size_t kDispatchSlot = 0xF8;
constexpr std::size_t kTrampOffset = 0x100;
constexpr std::size_t kControlOffset = 0x3F0;
constexpr std::size_t kSlotsOffset = 0x400;

struct StubLayout {
    std::size_t call_offset = 0;   // offset of the call instruction in the stub
    std::size_t jmp_offset = 0;    // offset of the jmp instruction in the stub
    std::size_t cmp_offset = 0;    // offset of the comparison of the flag
    std::size_t jne_offset = 0;    // offset of the jump to the replacement
    std::size_t mov_offset = 0;    // offset of the load of the result
    std::size_t replace_offset = 0;  // offset of the replacement path
};

Code build_stub(void* slots, void* control, StubLayout& layout) {
    Code c;
    c.u8(0x51);                                          // push rcx
    c.u8(0x52);                                          // push rdx
    c.u8(0x41); c.u8(0x50);                              // push r8
    c.u8(0x41); c.u8(0x51);                              // push r9
    c.u8(0x48); c.u8(0x83); c.u8(0xEC); c.u8(0x68);      // sub rsp, 0x68
    const std::uint8_t modrm[4] = {0x44, 0x4C, 0x54, 0x5C};
    for (int x = 0; x < 4; ++x) {                        // movdqu [rsp+0x20+x*16], xmmx
        c.u8(0xF3); c.u8(0x0F); c.u8(0x7F); c.u8(modrm[x]); c.u8(0x24);
        c.u8(static_cast<std::uint8_t>(0x20 + x * 16));
    }
    c.u8(0x4C); c.u8(0x8D); c.u8(0x44); c.u8(0x24); c.u8(0x68);  // lea r8, [rsp+0x68]
    c.u8(0x48); c.u8(0xBA); c.u64(reinterpret_cast<std::uint64_t>(slots));  // mov rdx, slots
    c.u8(0x49); c.u8(0xB9); c.u64(reinterpret_cast<std::uint64_t>(control));  // mov r9, control

    layout.call_offset = c.b.size();
    c.u8(0xFF); c.u8(0x15);                              // call [rip+disp32]
    c.u32(0);                                            // placeholder

    layout.cmp_offset = c.b.size();
    c.u8(0x83); c.u8(0x3D);                              // cmp dword ptr [rip+disp32], 0
    c.u32(0);                                            // placeholder
    c.u8(0x00);

    layout.jne_offset = c.b.size();
    c.u8(0x0F); c.u8(0x85);                              // jne rel32 (the replacement)
    c.u32(0);                                            // placeholder

    for (int x = 0; x < 4; ++x) {                        // movdqu xmmx, [rsp+0x20+x*16]
        c.u8(0xF3); c.u8(0x0F); c.u8(0x6F); c.u8(modrm[x]); c.u8(0x24);
        c.u8(static_cast<std::uint8_t>(0x20 + x * 16));
    }
    c.u8(0x48); c.u8(0x83); c.u8(0xC4); c.u8(0x68);      // add rsp, 0x68
    c.u8(0x41); c.u8(0x59);                              // pop r9
    c.u8(0x41); c.u8(0x58);                              // pop r8
    c.u8(0x5A);                                          // pop rdx
    c.u8(0x59);                                          // pop rcx

    layout.jmp_offset = c.b.size();
    c.u8(0xE9);                                          // jmp rel32 (trampoline)
    c.u32(0);                                            // placeholder

    layout.replace_offset = c.b.size();
    layout.mov_offset = c.b.size();
    c.u8(0x48); c.u8(0x8B); c.u8(0x05);                  // mov rax, [rip+disp32]
    c.u32(0);                                            // placeholder

    for (int x = 0; x < 4; ++x) {                        // movdqu xmmx, [rsp+0x20+x*16]
        c.u8(0xF3); c.u8(0x0F); c.u8(0x6F); c.u8(modrm[x]); c.u8(0x24);
        c.u8(static_cast<std::uint8_t>(0x20 + x * 16));
    }
    c.u8(0x48); c.u8(0x83); c.u8(0xC4); c.u8(0x68);      // add rsp, 0x68
    c.u8(0x41); c.u8(0x59);                              // pop r9
    c.u8(0x41); c.u8(0x58);                              // pop r8
    c.u8(0x5A);                                          // pop rdx
    c.u8(0x59);                                          // pop rcx
    c.u8(0xC3);                                          // ret
    return c;
}

bool patch_rel32(std::vector<std::uint8_t>& code, std::size_t at, std::uint64_t from_next,
                 std::uint64_t to) {
    const std::int64_t delta = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from_next);
    const bool too_low = delta < -0x7FFFFFFFLL;
    const bool too_high = delta > 0x7FFFFFFFLL;
    if (too_low || too_high) return false;
    const std::int32_t rel = static_cast<std::int32_t>(delta);
    std::memcpy(code.data() + at + 1, &rel, sizeof(rel));
    return true;
}

struct Entry {
    void* target = nullptr;
    void* page = nullptr;
    std::size_t page_size = 0;
    std::size_t len = 0;
    std::uint8_t original[64] = {};
    Slots* slots = nullptr;
};

HookStatus add_callback(Slots* slots, HookCallback callback, void* user, std::string& error) {
    const std::uint32_t n = slots->count.load(std::memory_order_acquire);
    for (std::uint32_t i = 0; i < n; ++i) {
        if (slots->items[i].callback.load() == callback && slots->items[i].user == user) {
            error = "this callback is already in place at this location";
            return HookStatus::already_hooked;
        }
    }
    for (std::uint32_t i = 0; i < n; ++i) {
        if (slots->items[i].callback.load() == nullptr) {
            slots->items[i].user = user;
            slots->items[i].callback.store(callback, std::memory_order_release);
            return HookStatus::ok;
        }
    }
    if (n >= kMaxCallbacks) {
        error = "too many callbacks at this location";
        return HookStatus::alloc_failed;
    }
    slots->items[n].user = user;
    slots->items[n].callback.store(callback, std::memory_order_release);
    slots->count.store(n + 1, std::memory_order_release);
    return HookStatus::ok;
}

HookStatus add_replace(Slots* slots, ReplaceCallback replace, void* user, std::string& error) {
    const std::uint32_t n = slots->count.load(std::memory_order_acquire);
    for (std::uint32_t i = 0; i < n; ++i) {
        if (slots->items[i].replace.load() == replace && slots->items[i].user == user) {
            error = "this replacement is already in place at this location";
            return HookStatus::already_hooked;
        }
    }
    for (std::uint32_t i = 0; i < n; ++i) {
        if (slots->items[i].replace.load() == nullptr && slots->items[i].callback.load() == nullptr) {
            slots->items[i].user = user;
            slots->items[i].replace.store(replace, std::memory_order_release);
            return HookStatus::ok;
        }
    }
    if (n >= kMaxCallbacks) {
        error = "too many callbacks at this location";
        return HookStatus::alloc_failed;
    }
    slots->items[n].user = user;
    slots->items[n].replace.store(replace, std::memory_order_release);
    slots->count.store(n + 1, std::memory_order_release);
    return HookStatus::ok;
}

std::mutex g_mutex;
std::vector<Entry> g_entries;

Entry* find_entry(void* target) {
    for (auto& e : g_entries) {
        if (e.target == target) return &e;
    }
    return nullptr;
}

void write_code(void* target, const std::uint8_t* bytes, std::size_t len) {
    const std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(target);
    const std::uintptr_t block = addr & ~std::uintptr_t{7};
    if (addr - block + len <= 8) {
        auto* q = reinterpret_cast<volatile LONG64*>(block);
        LONG64 old = *q;
        for (;;) {
            LONG64 next = old;
            std::memcpy(reinterpret_cast<std::uint8_t*>(&next) + (addr - block), bytes, len);
            const LONG64 seen = InterlockedCompareExchange64(q, next, old);
            if (seen == old) return;
            old = seen;
        }
    }
    std::memcpy(target, bytes, len);
}

void* alloc_near(std::uintptr_t target, std::size_t size) {
    constexpr std::uintptr_t kMax = 0x7FFF0000ull;
    constexpr std::uintptr_t kStep = 0x100000ull;   // 1 MB
    for (std::uintptr_t delta = 0; delta <= kMax; delta += kStep) {
        const std::uintptr_t candidates[2] = {target + delta, target - delta};
        for (std::uintptr_t addr : candidates) {
            if (addr < 0x10000ull) continue;
            void* p = VirtualAlloc(reinterpret_cast<void*>(addr), size,
                                   MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (!p) continue;
            const std::intptr_t d = static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(p) -
                                                               target);
            if (d >= -0x7FFFFFFF && d <= 0x7FFFFFFF) return p;
            VirtualFree(p, 0, MEM_RELEASE);
        }
    }
    return nullptr;
}

std::size_t main_module_size() {
    HMODULE mod = GetModuleHandleW(nullptr);
    if (!mod) return 0;
    auto* base = reinterpret_cast<const std::uint8_t*>(mod);
    const auto* dos = base;
    if (dos[0] != 'M' || dos[1] != 'Z') return 0;
    const std::uint32_t nt = *reinterpret_cast<const std::uint32_t*>(dos + 0x3c);
    const std::uint32_t size = *reinterpret_cast<const std::uint32_t*>(dos + nt + 24 + 56);
    return size;
}

}  // namespace

// ---------------------------------------------------------------- API
HookStatus install_impl(void* target, HookCallback callback, ReplaceCallback replace, void* user,
                         std::string& error) {
    error.clear();
#if !defined(_WIN64)
    (void)target; (void)callback; (void)replace; (void)user;
    error = "hooks are only built for x64";
    return HookStatus::unsupported_arch;
#else
    if (!target || (!callback && !replace)) {
        error = "invalid target or invalid callback";
        return HookStatus::target_invalid;
    }

    const std::uintptr_t target_addr = reinterpret_cast<std::uintptr_t>(target);
    const std::size_t module_size = main_module_size();
    if (!module_size) {
        error = "main module not readable";
        return HookStatus::target_invalid;
    }
    const std::uintptr_t module_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (target_addr < module_base || target_addr >= module_base + module_size) {
        error = "target does not lie in the main module";
        return HookStatus::target_invalid;
    }

    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(target, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) {
        error = "target is not committed memory";
        return HookStatus::target_invalid;
    }
    const DWORD prot = mbi.Protect & 0xffu;
    if (prot != PAGE_EXECUTE && prot != PAGE_EXECUTE_READ && prot != PAGE_EXECUTE_READWRITE &&
        prot != PAGE_EXECUTE_WRITECOPY) {
        error = "target does not lie in executable memory";
        return HookStatus::target_invalid;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if (Entry* existing = find_entry(target)) {
        if (replace) return add_replace(existing->slots, replace, user, error);
        return add_callback(existing->slots, callback, user, error);
    }

    const std::uint8_t* code = reinterpret_cast<const std::uint8_t*>(target);
    std::vector<Insn> insns;
    std::size_t len = 0;
    {
        const HookStatus st = plan_insns(code, 64, insns, len, error);
        if (st != HookStatus::ok) return st;
    }
    const std::size_t page_size = 0x1000;
    void* page = alloc_near(target_addr, page_size);
    if (!page) {
        error = "no memory in reach of the target";
        return HookStatus::alloc_failed;
    }
    const std::uintptr_t page_addr = reinterpret_cast<std::uintptr_t>(page);
    const std::uintptr_t stub_addr = page_addr;
    const std::uintptr_t tramp_addr = page_addr + kTrampOffset;
    auto* slots = new (reinterpret_cast<void*>(page_addr + kSlotsOffset)) Slots{};
    slots->count.store(0);
    auto* control = new (reinterpret_cast<void*>(page_addr + kControlOffset)) Control{};
    control->skip.store(0);
    control->result.store(0);
    {
        std::string ignored;
        if (callback) {
            add_callback(slots, callback, user, ignored);
        } else {
            add_replace(slots, replace, user, ignored);
        }
    }
    const std::uint64_t dispatch_addr = reinterpret_cast<std::uint64_t>(&dispatch);
    *reinterpret_cast<std::uint8_t*>(page_addr + kRetByte) = 0xC3;   // ret
    std::memcpy(reinterpret_cast<void*>(page_addr + kDispatchSlot), &dispatch_addr,
                sizeof(dispatch_addr));

    std::vector<std::uint8_t> tramp;
    {
        const std::uintptr_t patch_end = target_addr + len;
        auto into_patch = [&](std::int64_t tgt) {
            return tgt >= static_cast<std::int64_t>(target_addr) &&
                   tgt < static_cast<std::int64_t>(patch_end);
        };
        auto rel_to = [&](std::int64_t tgt, std::size_t insn_end, std::int32_t& out) {
            const std::int64_t d = tgt - static_cast<std::int64_t>(tramp_addr + insn_end);
            if (d < INT32_MIN || d > INT32_MAX) return false;
            out = static_cast<std::int32_t>(d);
            return true;
        };
        std::size_t off = 0;
        for (const Insn& ins : insns) {
            const std::int64_t orig_next = static_cast<std::int64_t>(target_addr + off + ins.length);
            if (ins.rel8) {
                const auto disp8 = static_cast<std::int8_t>(code[off + ins.length - 1]);
                const std::int64_t tgt = orig_next + disp8;
                if (into_patch(tgt)) {
                    VirtualFree(page, 0, MEM_RELEASE);
                    error = "short jump targets the overwritten range";
                    return HookStatus::relocation_failed;
                }
                std::int32_t rel = 0;
                if (ins.short_cc == -1) {
                    tramp.push_back(0xE9);                                    // jmp rel32
                    if (!rel_to(tgt, tramp.size() + 4, rel)) { VirtualFree(page, 0, MEM_RELEASE); error = "jump target out of reach"; return HookStatus::relocation_failed; }
                } else {
                    tramp.push_back(0x0F);                                    // jcc rel32
                    tramp.push_back(static_cast<std::uint8_t>(0x80 | ins.short_cc));
                    if (!rel_to(tgt, tramp.size() + 4, rel)) { VirtualFree(page, 0, MEM_RELEASE); error = "jump target out of reach"; return HookStatus::relocation_failed; }
                }
                const auto* rb = reinterpret_cast<const std::uint8_t*>(&rel);
                tramp.insert(tramp.end(), rb, rb + 4);
            } else {
                const std::size_t at = tramp.size();
                tramp.insert(tramp.end(), code + off, code + off + ins.length);
                if (ins.rel32 || ins.rip) {
                    std::int32_t disp = 0;
                    std::memcpy(&disp, code + off + ins.disp_off, sizeof(disp));
                    const std::int64_t tgt = orig_next + disp;
                    if (ins.rel32 && into_patch(tgt)) {
                        VirtualFree(page, 0, MEM_RELEASE);
                        error = "jump targets the overwritten range";
                        return HookStatus::relocation_failed;
                    }
                    std::int32_t rel = 0;
                    if (!rel_to(tgt, at + ins.length, rel)) {
                        VirtualFree(page, 0, MEM_RELEASE);
                        error = "relative target out of reach of the trampoline";
                        return HookStatus::relocation_failed;
                    }
                    std::memcpy(tramp.data() + at + ins.disp_off, &rel, sizeof(rel));
                }
            }
            off += ins.length;
        }
    }
    Code back;
    back.u8(0xE9);
    back.u32(0);
    const std::size_t back_offset = tramp.size();
    tramp.insert(tramp.end(), back.b.begin(), back.b.end());
    if (!patch_rel32(tramp, back_offset, tramp_addr + back_offset + 5, target_addr + len)) {
        VirtualFree(page, 0, MEM_RELEASE);
        error = "return from the trampoline is more than 2 GB away";
        return HookStatus::relocation_failed;
    }

    StubLayout layout;
    Code stub = build_stub(slots, control, layout);
    if (stub.b.size() > kRetByte || tramp.size() > kControlOffset - kTrampOffset) {
        VirtualFree(page, 0, MEM_RELEASE);
        error = "stub or trampoline too large";
        return HookStatus::write_failed;
    }
    {
        const std::int32_t call_disp = static_cast<std::int32_t>(
            static_cast<std::int64_t>(page_addr + kDispatchSlot) -
            static_cast<std::int64_t>(stub_addr + layout.call_offset + 6));
        std::memcpy(stub.b.data() + layout.call_offset + 2, &call_disp, sizeof(call_disp));
        if (!patch_rel32(stub.b, layout.jmp_offset, stub_addr + layout.jmp_offset + 5,
                         tramp_addr)) {
            VirtualFree(page, 0, MEM_RELEASE);
            error = "trampoline is more than 2 GB away";
            return HookStatus::relocation_failed;
        }
        {
            const std::int32_t disp = static_cast<std::int32_t>(
                static_cast<std::int64_t>(page_addr + kControlOffset + offsetof(Control, skip)) -
                static_cast<std::int64_t>(stub_addr + layout.cmp_offset + 7));
            std::memcpy(stub.b.data() + layout.cmp_offset + 2, &disp, sizeof(disp));
        }
        {
            const std::int64_t delta = static_cast<std::int64_t>(stub_addr + layout.replace_offset) -
                                       static_cast<std::int64_t>(stub_addr + layout.jne_offset + 6);
            if (delta < INT32_MIN || delta > INT32_MAX) {
                VirtualFree(page, 0, MEM_RELEASE);
                error = "the replacement path is out of reach";
                return HookStatus::relocation_failed;
            }
            const std::int32_t rel = static_cast<std::int32_t>(delta);
            std::memcpy(stub.b.data() + layout.jne_offset + 2, &rel, sizeof(rel));
        }
        {
            const std::int32_t disp = static_cast<std::int32_t>(
                static_cast<std::int64_t>(page_addr + kControlOffset + offsetof(Control, result)) -
                static_cast<std::int64_t>(stub_addr + layout.mov_offset + 7));
            std::memcpy(stub.b.data() + layout.mov_offset + 3, &disp, sizeof(disp));
        }
    }

    std::memcpy(reinterpret_cast<void*>(stub_addr), stub.b.data(), stub.b.size());
    std::memcpy(reinterpret_cast<void*>(tramp_addr), tramp.data(), tramp.size());
    std::memcpy(reinterpret_cast<void*>(stub_addr), stub.b.data(), stub.b.size());
    std::memcpy(reinterpret_cast<void*>(tramp_addr), tramp.data(), tramp.size());
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(page_addr), page_size);

    Entry entry;
    entry.target = target;
    entry.page = page;
    entry.page_size = page_size;
    entry.len = len;
    entry.slots = slots;
    std::memcpy(entry.original, code, len);

    std::vector<std::uint8_t> patch(len, 0x90);
    patch[0] = 0xE9;
    const std::int32_t rel = static_cast<std::int32_t>(
        static_cast<std::int64_t>(stub_addr) - static_cast<std::int64_t>(target_addr + 5));
    std::memcpy(patch.data() + 1, &rel, sizeof(rel));

    DWORD old_protect = 0;
    if (!VirtualProtect(target, len, PAGE_EXECUTE_READWRITE, &old_protect)) {
        VirtualFree(page, 0, MEM_RELEASE);
        error = "memory protection at the target not changeable";
        return HookStatus::write_failed;
    }
    write_code(target, patch.data(), len);
    DWORD ignored = 0;
    VirtualProtect(target, len, old_protect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), target, len);

    g_entries.push_back(entry);
    return HookStatus::ok;
#endif
}

HookStatus Hook::install(void* target, HookCallback callback, void* user, std::string& error) {
    return install_impl(target, callback, nullptr, user, error);
}

HookStatus Hook::install_replace(void* target, ReplaceCallback replace, void* user,
                                 std::string& error) {
    return install_impl(target, nullptr, replace, user, error);
}

HookStatus Hook::remove(void* target, std::string& error) {
    error.clear();
#if !defined(_WIN64)
    (void)target;
    error = "hooks are only built for x64";
    return HookStatus::unsupported_arch;
#else
    if (!target) {
        error = "invalid target";
        return HookStatus::target_invalid;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    Entry* e = find_entry(target);
    if (!e) {
        error = "no hook at this location";
        return HookStatus::target_invalid;
    }

    DWORD old_protect = 0;
    if (!VirtualProtect(target, e->len, PAGE_EXECUTE_READWRITE, &old_protect)) {
        error = "memory protection at the target not changeable";
        return HookStatus::write_failed;
    }
    write_code(target, e->original, e->len);
    DWORD ignored = 0;
    VirtualProtect(target, e->len, old_protect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), target, e->len);

    e->slots->count.store(0, std::memory_order_release);
    {
        auto* slot = reinterpret_cast<volatile LONG64*>(
            reinterpret_cast<std::uintptr_t>(e->page) + kDispatchSlot);
        InterlockedExchange64(slot, static_cast<LONG64>(
            reinterpret_cast<std::uintptr_t>(e->page) + kRetByte));
    }
    g_entries.erase(g_entries.begin() + (e - g_entries.data()));
    return HookStatus::ok;
#endif
}

HookStatus Hook::remove_replace(void* target, ReplaceCallback replace, void* user,
                                std::string& error) {
    error.clear();
#if !defined(_WIN64)
    (void)target; (void)replace; (void)user;
    error = "hooks are only built for x64";
    return HookStatus::unsupported_arch;
#else
    std::lock_guard<std::mutex> lock(g_mutex);
    Entry* e = target ? find_entry(target) : nullptr;
    if (!e) {
        error = "no hook at this location";
        return HookStatus::target_invalid;
    }
    Slots* s = e->slots;
    const std::uint32_t n = s->count.load(std::memory_order_acquire);
    std::uint32_t at = n;
    for (std::uint32_t i = 0; i < n; ++i) {
        if (s->items[i].replace.load() == replace && s->items[i].user == user) at = i;
    }
    if (at == n) {
        error = "this replacement is not here";
        return HookStatus::target_invalid;
    }
    s->items[at].replace.store(nullptr, std::memory_order_release);
    return HookStatus::ok;
#endif
}

HookStatus Hook::remove_callback(void* target, HookCallback callback, void* user,
                                std::string& error) {
    error.clear();
#if !defined(_WIN64)
    (void)target; (void)callback; (void)user;
    error = "hooks are only built for x64";
    return HookStatus::unsupported_arch;
#else
    bool last = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Entry* e = target ? find_entry(target) : nullptr;
        if (!e) {
            error = "no hook at this location";
            return HookStatus::target_invalid;
        }
        Slots* s = e->slots;
        const std::uint32_t n = s->count.load(std::memory_order_acquire);
        std::uint32_t at = n;
        for (std::uint32_t i = 0; i < n; ++i) {
            if (s->items[i].callback.load() == callback && s->items[i].user == user) at = i;
        }
        if (at == n) {
            error = "this callback is not here";
            return HookStatus::target_invalid;
        }
        s->items[at].callback.store(nullptr, std::memory_order_release);
        last = true;
        for (std::uint32_t i = 0; i < n; ++i) {
            if (s->items[i].callback.load() != nullptr) last = false;
        }
    }
    if (last) return remove(target, error);
    return HookStatus::ok;
#endif
}

HookStatus Hook::plan(const std::uint8_t* code, std::size_t available, std::size_t& length,
                      std::string& error) {
    error.clear();
    length = 0;
    if (!code) {
        error = "no bytes";
        return HookStatus::target_invalid;
    }
    std::vector<Insn> insns;
    return plan_insns(code, available, insns, length, error);
}

bool Hook::is_hooked(void* target) {
#if !defined(_WIN64)
    (void)target;
    return false;
#else
    std::lock_guard<std::mutex> lock(g_mutex);
    return find_entry(target) != nullptr;
#endif
}

std::size_t Hook::remove_all(unsigned timeout_ms) {
#if !defined(_WIN64)
    (void)timeout_ms;
    return 0;
#else
    std::vector<void*> targets;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (const auto& e : g_entries) targets.push_back(e.target);
    }
    std::size_t removed = 0;
    for (void* t : targets) {
        std::string ignored;
        if (remove(t, ignored) == HookStatus::ok) ++removed;
    }
    Sleep(20);
    const ULONGLONG until = GetTickCount64() + timeout_ms;
    while (g_inflight.load(std::memory_order_acquire) > 0 && GetTickCount64() < until) Sleep(1);
    return removed;
#endif
}

bool Hook::idle() {
#if !defined(_WIN64)
    return true;
#else
    return g_inflight.load(std::memory_order_acquire) == 0;
#endif
}

std::uint64_t Hook::arg(unsigned index) {
    const std::uint64_t* a = t_args;
    if (!a) return 0;
    static constexpr unsigned kRegister[4] = {3, 2, 1, 0};   // rcx, rdx, r8, r9
    if (index < 4) return a[kRegister[index]];
    return a[5 + index];   // +0x28 + 8 * index
}

void* Hook::arg2() {
    return reinterpret_cast<void*>(arg(1));
}

std::size_t Hook::count() {
#if !defined(_WIN64)
    return 0;
#else
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_entries.size();
#endif
}

}  // namespace sw
