// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Weaysy
// Read-only local observer. Does not attach to, inject, or control the game.
#define WIN32_LEAN_AND_MEAN
#include "weaysy_theme_ipc.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        puts("Usage: WeaysyClockRead.exe <game-process-id>\nRead-only Weaysy telemetry IPC snapshot as JSON. No game writes.");
        return 0;
    }
    if (argc != 2) { fputs("Expected one decimal game process ID.\n", stderr); return 2; }
    errno = 0;
    char* end = nullptr;
    const unsigned long parsed = strtoul(argv[1], &end, 10);
    if (errno || !end || *end || end == argv[1] || !parsed || argv[1][0] == '-') {
        fputs("Invalid process ID.\n", stderr); return 2;
    }
    const DWORD pid = static_cast<DWORD>(parsed);
    wchar_t name[64]{};
    weaysy::mapping_name(pid, name);
    const HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!mapping) { fprintf(stderr, "No Weaysy telemetry mapping for PID %lu (error %lu).\n", parsed, GetLastError()); return 3; }
    const auto* mapped = static_cast<const weaysy::ThemeSnapshot*>(
        MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(weaysy::ThemeSnapshot)));
    if (!mapped) { CloseHandle(mapping); fputs("Cannot map telemetry read-only.\n", stderr); return 4; }
    weaysy::ThemeSnapshot value{};
    const bool ok = weaysy::read_theme_snapshot(mapped, value, pid);
    UnmapViewOfFile(mapped);
    CloseHandle(mapping);
    if (!ok) { fputs("Missing, inconsistent or incompatible telemetry snapshot.\n", stderr); return 5; }
    printf("{\"pid\":%lu,\"game_time\":%lu,\"minute_of_day\":%lu,\"clock\":\"%02lu:%02lu\","
           "\"theme\":\"%s\",\"publisher_running\":%s,\"has_valid_time\":%s,\"available\":%s,"
           "\"paused\":%s,\"day_start\":%lu,\"night_start\":%lu,\"generation\":%lu,"
           "\"heartbeat_age_ms\":%llu,\"convoy_offset\":%ld,\"offset_applied\":false,"
           "\"local_timezone_applied\":%s,\"local_correction\":%ld,\"selected_clock\":\"%02lu:%02lu\"}\n",
        parsed, static_cast<unsigned long>(value.absolute_minutes),
        static_cast<unsigned long>(value.minute_of_day),
        static_cast<unsigned long>(value.minute_of_day / 60),
        static_cast<unsigned long>(value.minute_of_day % 60),
        value.theme ? "NIGHT" : "DAY",
        (value.flags & weaysy::publisher_running) ? "true" : "false",
        (value.flags & weaysy::has_valid_time) ? "true" : "false",
        (value.flags & weaysy::time_currently_available) ? "true" : "false",
        (value.flags & weaysy::game_paused) ? "true" : "false",
        static_cast<unsigned long>(value.day_start), static_cast<unsigned long>(value.night_start),
        static_cast<unsigned long>(value.generation),
        static_cast<unsigned long long>(GetTickCount64() - value.publish_tick_ms),
        static_cast<long>(value.multiplayer_offset),
        (value.flags & weaysy::local_timezone_applied) ? "true" : "false",
        (value.flags & weaysy::local_timezone_applied) ? static_cast<long>(static_cast<int32_t>(value.reserved0)) : 0L,
        static_cast<unsigned long>(((value.flags & weaysy::local_timezone_applied) ? value.reserved1 : value.minute_of_day) / 60),
        static_cast<unsigned long>(((value.flags & weaysy::local_timezone_applied) ? value.reserved1 : value.minute_of_day) % 60));
    return (value.flags & weaysy::has_valid_time) ? 0 : 6;
}
