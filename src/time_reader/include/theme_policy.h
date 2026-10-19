// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Weaysy
#pragma once
#include <stdint.h>
#include "clock_math.h"

namespace weaysy {
enum class Theme : uint32_t { day = 0, night = 1 };

struct ClockPolicy {
    uint32_t day_start = 360;    // Local prototype example: 06:00.
    uint32_t night_start = 1200; // Local prototype example: 20:00.
    uint32_t test_interval_minutes = 0; // 0 disables the local alternating test.

    constexpr bool valid() const noexcept {
        return day_start < 1440 && night_start < 1440 && day_start != night_start &&
            test_interval_minutes <= 60;
    }

    // Night begins inclusively and day begins exclusively, including midnight.
    constexpr Theme select(uint32_t absolute_minutes) const noexcept {
        if (test_interval_minutes)
            return ((absolute_minutes / test_interval_minutes) % 2)
                ? Theme::night : Theme::day;
        const uint32_t m = absolute_minutes % 1440;
        const bool night = night_start > day_start
            ? (m >= night_start || m < day_start)
            : (m >= night_start && m < day_start);
        return night ? Theme::night : Theme::day;
    }
};

// Accept exactly HH:MM; the config never silently clips an invalid threshold.
inline bool parse_hhmm(const wchar_t* text, uint32_t& out) noexcept {
    if (!text || !text[0] || !text[1] || text[2] != L':' ||
        !text[3] || !text[4] || text[5] != L'\0') return false;
    const unsigned positions[] = {0, 1, 3, 4};
    for (unsigned p : positions)
        if (text[p] < L'0' || text[p] > L'9') return false;
    const uint32_t hour = (text[0] - L'0') * 10 + text[1] - L'0';
    const uint32_t minute = (text[3] - L'0') * 10 + text[4] - L'0';
    if (hour > 23 || minute > 59) return false;
    out = hour * 60 + minute;
    return true;
}

// Decimal 0 disables the explicit interval test; only 1..60 are valid intervals.
inline bool parse_test_interval(const wchar_t* text, uint32_t& out) noexcept {
    if (!text || !text[0]) return false;
    uint32_t parsed = 0;
    for (unsigned i = 0; text[i]; ++i) {
        if (i >= 2 || text[i] < L'0' || text[i] > L'9') return false;
        parsed = parsed * 10 + static_cast<uint32_t>(text[i] - L'0');
    }
    if (parsed > 60) return false;
    out = parsed;
    return true;
}

struct ThemeState {
    ClockPolicy policy;
    uint32_t absolute_minutes = 0;
    Theme theme = Theme::day;
    bool has_time = false;
    bool time_available = false;
    bool paused = true;
    uint32_t generation = 0;
    uint64_t last_valid_tick_ms = 0;
    int32_t local_correction = 0;
    uint32_t selected_minute = 0;

    void receive_time(uint32_t minutes, uint64_t tick_ms, int32_t correction = 0) noexcept {
        // The explicit two-minute experiment remains a raw SDK-time experiment.
        local_correction = policy.test_interval_minutes ? 0 : correction;
        selected_minute = local_minute(minutes, local_correction);
        const Theme selected = policy.select(policy.test_interval_minutes ? minutes : selected_minute);
        if (!has_time || selected != theme) ++generation;
        absolute_minutes = minutes;
        theme = selected;
        has_time = time_available = true;
        last_valid_tick_ms = tick_ms;
    }
    void unavailable() noexcept { time_available = false; }
};
} // namespace weaysy
