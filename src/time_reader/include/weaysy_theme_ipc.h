// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Weaysy
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdint.h>
#include <stddef.h>

namespace weaysy {
static_assert(sizeof(void*) == 8, "Theme IPC is supported only by Windows x64 builds");
constexpr uint32_t theme_ipc_magic = 0x31594157; // bytes W A Y 1
constexpr uint32_t theme_ipc_version = 1;
constexpr wchar_t theme_mapping_prefix[] = L"Local\\WeaysyThemeV1-";
enum ThemeFlags : uint32_t {
    publisher_running = 1u << 0,
    has_valid_time = 1u << 1,
    time_currently_available = 1u << 2,
    game_paused = 1u << 3,
    multiplayer_offset_available = 1u << 4,
    local_timezone_applied = 1u << 5, // optional backwards-compatible v1 extension
};

// Windows x64 ABI, same-bitness producer and consumer. Single producer only.
// Ordinary fields are published under the aligned interlocked sequence below.
struct alignas(8) ThemeSnapshot {
    uint32_t magic;                  // 0
    uint32_t version;                // 4
    uint32_t byte_size;              // 8
    LONG sequence;                  // 12: odd while writing, even when stable
    uint32_t process_id;             // 16
    uint32_t flags;                  // 20
    uint32_t theme;                  // 24: 0 day, 1 night; check has_valid_time
    uint32_t absolute_minutes;       // 28: game.time, NO multiplayer adjustment
    uint32_t minute_of_day;          // 32: absolute_minutes modulo 1440
    uint32_t day_start;              // 36
    uint32_t night_start;            // 40
    uint32_t mode;                   // 44: 0 is game.time; no wall-clock fallback
    int32_t multiplayer_offset;      // 48: recorded only; not applied
    uint32_t reserved0;              // 52: signed local correction if flag5, else zero
    uint64_t publish_tick_ms;        // 56: GetTickCount64 heartbeat
    uint64_t last_valid_tick_ms;     // 64
    uint32_t generation;            // 72: first value or theme change
    uint32_t reserved1;              // 76: selected local minute if flag5, else zero
};
static_assert(sizeof(ThemeSnapshot) == 80, "IPC size changed; bump protocol version");
static_assert(offsetof(ThemeSnapshot, sequence) == 12, "IPC sequence alignment");
static_assert(offsetof(ThemeSnapshot, publish_tick_ms) == 56, "IPC tick alignment");

inline void mapping_name(DWORD pid, wchar_t (&out)[64]) noexcept {
    wsprintfW(out, L"%s%lu", theme_mapping_prefix, static_cast<unsigned long>(pid));
}

// Consumer opens mapping with FILE_MAP_READ. No writes, interlocked RMW, or casts
// to a writable view. Volatile loads plus Windows MemoryBarrier implement the
// x64 shared-memory seqlock; this is not a portable ISO C++ concurrency utility.
inline bool read_theme_snapshot(const ThemeSnapshot* mapped, ThemeSnapshot& out,
                               DWORD expected_pid = GetCurrentProcessId()) noexcept {
    if (!mapped) return false;
    const auto* source = reinterpret_cast<const volatile unsigned char*>(mapped);
    const auto* seq = reinterpret_cast<const volatile LONG*>(&mapped->sequence);
    for (unsigned attempt = 0; attempt != 8; ++attempt) {
        const LONG before = *seq;
        MemoryBarrier();
        if (before & 1) continue;
        auto* dest = reinterpret_cast<unsigned char*>(&out);
        for (size_t i = 0; i != sizeof(out); ++i) dest[i] = source[i];
        MemoryBarrier();
        const LONG after = *seq;
        if (before != after || (after & 1)) continue;
        if (out.magic != theme_ipc_magic || out.version != theme_ipc_version ||
            out.byte_size != sizeof(ThemeSnapshot) || out.process_id != expected_pid ||
            out.mode != 0 || out.theme > 1 || out.minute_of_day >= 1440 ||
            out.day_start >= 1440 || out.night_start >= 1440 ||
            out.day_start == out.night_start) return false;
        if ((out.flags & local_timezone_applied) &&
            (out.reserved1 >= 1440 || static_cast<int32_t>(out.reserved0) < -1680 ||
             static_cast<int32_t>(out.reserved0) > 1680)) return false;
        return true;
    }
    return false;
}
} // namespace weaysy
