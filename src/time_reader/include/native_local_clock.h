// SPDX-License-Identifier: MIT
// Read-only ATS 1.61.3.18 clock adapter. No hooks, native calls or process writes.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#include <stdint.h>
#include <string.h>
#include "clock_math.h"

namespace weaysy {
constexpr uintptr_t ats_economy_global = 0x3051d40;
constexpr uintptr_t ats_context_global = 0x36bad90;
constexpr uintptr_t ats_economy_vtable = 0x224fb68;
constexpr uintptr_t ats_context_vtable = 0x22540a0;

struct NativeClockSample {
    uint32_t raw_minutes = 0;
    int32_t zone_minutes = 0;
    int32_t base_minutes = 0;
    int32_t correction = 0;
    uintptr_t economy = 0;
    uintptr_t context = 0;
    char zone_name[64]{};
};

// Memory is injectable only in the independent layout tests. The production
// adapter below must pass the full executable SHA-256 gate before calling this.
template<class Memory>
bool read_native_clock(Memory& memory, uintptr_t base, uint32_t sdk_minutes,
                       bool check_sdk, NativeClockSample& out) noexcept {
    NativeClockSample sample{};
    uintptr_t vt = 0, after_economy = 0, after_context = 0;
    if (!memory.read(base + ats_economy_global, &sample.economy, 8) ||
        !memory.read(base + ats_context_global, &sample.context, 8) ||
        !sample.economy || !sample.context ||
        !memory.read(sample.economy, &vt, 8) || vt != base + ats_economy_vtable ||
        !memory.read(sample.context, &vt, 8) || vt != base + ats_context_vtable ||
        !memory.read(sample.economy + 0x19c, &sample.raw_minutes, 4)) return false;
    const int64_t difference = static_cast<int64_t>(sample.raw_minutes) - sdk_minutes;
    if (check_sdk && (difference < -1 || difference > 1)) return false;
    // The game's live context is used, NOT the economy's saved last-zone cache.
    // Layout: zone+0x138, string+0x140, base-zone+0x170. Refreshed each frame.
    unsigned char zone[60]{}, again[60]{};
    if (!memory.read(sample.context + 0x138, zone, sizeof(zone))) return false;
    uintptr_t name = 0;
    uint32_t length = 0, capacity = 0;
    memcpy(&sample.zone_minutes, zone, 4);
    memcpy(&sample.base_minutes, zone + 56, 4);
    memcpy(&name, zone + 16, 8);
    memcpy(&length, zone + 24, 4);
    memcpy(&capacity, zone + 28, 4);
    if (sample.zone_minutes < -840 || sample.zone_minutes > 840 ||
        sample.base_minutes < -840 || sample.base_minutes > 840 ||
        !name || !length || length >= sizeof(sample.zone_name) || capacity <= length ||
        !memory.read(name, sample.zone_name, length)) return false;
    for (uint32_t i = 0; i < length; ++i)
        if (static_cast<unsigned char>(sample.zone_name[i]) < 32 ||
            sample.zone_name[i] == 127) return false;
    uint32_t raw_after = 0;
    if (!memory.read(sample.context + 0x138, again, sizeof(again)) ||
        memcmp(zone, again, sizeof(zone)) != 0 ||
        !memory.read(base + ats_economy_global, &after_economy, 8) ||
        !memory.read(base + ats_context_global, &after_context, 8) ||
        after_economy != sample.economy || after_context != sample.context ||
        !memory.read(sample.economy + 0x19c, &raw_after, 4) ||
        raw_after != sample.raw_minutes) return false;
    sample.correction = sample.zone_minutes - sample.base_minutes;
    out = sample;
    return true;
}

inline bool supported_ats_executable(const wchar_t* path) noexcept {
    constexpr unsigned char expected[32] = {
        0xc6,0x0e,0x8e,0xe0,0x2c,0xc9,0x7b,0x3c,0xc6,0xef,0x8c,0x62,0x98,0xbb,0x5c,0x3c,
        0x23,0xc4,0xff,0xd3,0x30,0xb8,0x18,0x5b,0x64,0x6f,0xb1,0xb6,0x08,0xa3,0xc7,0xb4};
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE |
                             FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    bool ok = CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES,
                                   CRYPT_VERIFYCONTEXT) &&
              CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash);
    unsigned char buffer[32768];
    DWORD bytes = 0;
    while (ok) {
        if (!ReadFile(file, buffer, sizeof(buffer), &bytes, nullptr)) { ok = false; break; }
        if (!bytes) break;
        ok = CryptHashData(hash, buffer, bytes, 0) != FALSE;
    }
    unsigned char digest[32]{};
    DWORD size = sizeof(digest);
    ok = ok && CryptGetHashParam(hash, HP_HASHVAL, digest, &size, 0) &&
         size == sizeof(digest) && !memcmp(digest, expected, sizeof(digest));
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    CloseHandle(file);
    return ok;
}

class NativeLocalClockReader {
    HANDLE process_ = nullptr;
    uintptr_t base_ = 0;
public:
    bool initialize(HANDLE process, uintptr_t base, const wchar_t* path) noexcept {
        process_ = nullptr; base_ = 0;
        if (!process || !base || !path || !supported_ats_executable(path)) return false;
        process_ = process; base_ = base;
        return true;
    }
    bool initialize_current_process() noexcept {
        wchar_t path[32768]{};
        const DWORD n = GetModuleFileNameW(nullptr, path, 32768);
        return n && n < 32768 && initialize(GetCurrentProcess(),
            reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), path);
    }
    bool read(uintptr_t address, void* destination, size_t size) noexcept {
        SIZE_T received = 0;
        return process_ && address >= 0x10000 && address <= 0x7fffffffffffULL &&
            size <= 4096 && ReadProcessMemory(process_, reinterpret_cast<const void*>(address),
                destination, size, &received) && received == size;
    }
    bool sample(uint32_t sdk_minutes, NativeClockSample& out) noexcept {
        return base_ && read_native_clock(*this, base_, sdk_minutes, true, out);
    }
    bool probe(NativeClockSample& out) noexcept {
        return base_ && read_native_clock(*this, base_, 0, false, out);
    }
};
} // namespace weaysy
