// SPDX-License-Identifier: MIT
#include "theme_policy.h"
#include <stdio.h>
#include <stdint.h>

int main() {
    unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL line %u: %s\n", __LINE__, #x); return 1; } } while (0)
    using namespace weaysy;
    ClockPolicy p;
    CHECK(p.valid());
    CHECK(p.select(359) == Theme::night);
    CHECK(p.select(360) == Theme::day);
    CHECK(p.select(1199) == Theme::day);
    CHECK(p.select(1200) == Theme::night);
    CHECK(p.select(1439) == Theme::night);
    CHECK(p.select(1440) == Theme::night);
    CHECK(p.select(1440 * 1000 + 360) == Theme::day);
    CHECK(p.select(UINT32_MAX) == p.select(UINT32_MAX % 1440));
    ClockPolicy shifted{1200, 360};
    CHECK(shifted.select(359) == Theme::day);
    CHECK(shifted.select(360) == Theme::night);
    CHECK(shifted.select(1199) == Theme::night);
    CHECK(shifted.select(1200) == Theme::day);
    CHECK(!(ClockPolicy{360, 360}.valid()));
    CHECK(!(ClockPolicy{1440, 1200}.valid()));
    uint32_t minutes = 9999;
    CHECK(parse_hhmm(L"00:00", minutes) && minutes == 0);
    CHECK(parse_hhmm(L"23:59", minutes) && minutes == 1439);
    CHECK(!parse_hhmm(nullptr, minutes));
    CHECK(!parse_hhmm(L"", minutes));
    CHECK(!parse_hhmm(L"6:00", minutes));
    CHECK(!parse_hhmm(L"06:0", minutes));
    CHECK(!parse_hhmm(L"24:00", minutes));
    CHECK(!parse_hhmm(L"00:60", minutes));
    CHECK(!parse_hhmm(L"20:00garbage", minutes));
    CHECK(!parse_hhmm(L"xx:xx", minutes));
    ThemeState s;
    CHECK(!s.has_time && !s.time_available && s.generation == 0);
    s.receive_time(1199, 100);
    CHECK(s.has_time && s.time_available && s.theme == Theme::day && s.generation == 1);
    s.receive_time(1200, 101);
    CHECK(s.theme == Theme::night && s.generation == 2);
    s.receive_time(1201, 102);
    CHECK(s.generation == 2);
    s.unavailable();
    CHECK(s.has_time && !s.time_available && s.theme == Theme::night && s.last_valid_tick_ms == 102);
    s.receive_time(360, 200); // load/sleep/fast travel backwards: no wall clock.
    CHECK(s.theme == Theme::day && s.absolute_minutes == 360 && s.generation == 3);
    s.paused = true;
    s.unavailable();
    CHECK(s.paused && s.theme == Theme::day && s.generation == 3);
    ClockPolicy test;
    test.test_interval_minutes = 2;
    CHECK(test.valid());
    CHECK(test.select(0) == Theme::day && test.select(1) == Theme::day);
    CHECK(test.select(2) == Theme::night && test.select(3) == Theme::night);
    CHECK(test.select(4) == Theme::day && test.select(5) == Theme::day);
    CHECK(test.select(1439) == Theme::night && test.select(1440) == Theme::day);
    CHECK(test.select(UINT32_MAX) == Theme::night);
    test.test_interval_minutes = 1;
    CHECK(test.valid() && test.select(1) == Theme::night && test.select(2) == Theme::day);
    test.test_interval_minutes = 60;
    CHECK(test.valid() && test.select(59) == Theme::day && test.select(60) == Theme::night);
    test.test_interval_minutes = 61;
    CHECK(!test.valid());
    test.test_interval_minutes = 0;
    CHECK(test.valid() && test.select(1200) == Theme::night && test.select(360) == Theme::day);
    CHECK(parse_test_interval(L"0", minutes) && minutes == 0);
    CHECK(parse_test_interval(L"2", minutes) && minutes == 2);
    CHECK(parse_test_interval(L"60", minutes) && minutes == 60);
    CHECK(!parse_test_interval(L"61", minutes));
    CHECK(!parse_test_interval(L"-2", minutes));
    CHECK(!parse_test_interval(L"2x", minutes));
    CHECK(!parse_test_interval(L"100", minutes));
    CHECK(!parse_test_interval(L"", minutes));
    CHECK(!parse_test_interval(nullptr, minutes));
    ThemeState alternating;
    alternating.policy.test_interval_minutes = 2;
    alternating.receive_time(1, 1000);
    CHECK(alternating.theme == Theme::day && alternating.generation == 1);
    alternating.receive_time(2, 1001);
    CHECK(alternating.theme == Theme::night && alternating.generation == 2);
    alternating.paused = true;
    alternating.unavailable();
    CHECK(alternating.theme == Theme::night && alternating.absolute_minutes == 2 && alternating.generation == 2);
    alternating.receive_time(1440, 2000); // Sleep/load jump directly across midnight.
    CHECK(alternating.theme == Theme::day && alternating.generation == 3);
    alternating.receive_time(1439, 2001); // Backwards game-time jump, not elapsed real time.
    CHECK(alternating.theme == Theme::night && alternating.generation == 4);
    printf("PASS %u policy checks\n", checks);
    return 0;
}
