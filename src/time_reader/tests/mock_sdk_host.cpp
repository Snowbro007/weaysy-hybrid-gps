// SPDX-License-Identifier: MIT
// An isolated host for our own DLL; this is NOT an ATS/ETS2 runtime test.
#define WIN32_LEAN_AND_MEAN
#include "weaysy_theme_ipc.h"
#include "scssdk_telemetry.h"
#include "common/scssdk_telemetry_common_channels.h"
#include <stdio.h>
#include <string.h>

struct Channel {
    bool active = false;
    scs_telemetry_channel_callback_t callback = nullptr;
    scs_context_t context = nullptr;
    scs_u32_t flags = 0;
};
Channel time_channel, offset_channel;
struct Event { scs_telemetry_event_callback_t callback = nullptr; scs_context_t context = nullptr; };
Event events[8];
unsigned checks = 0;
scs_event_t fail_event = 0;
bool fail_channel = false;
bool fail_offset = false;

#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL line %u: %s\n", __LINE__, #x); return 1; } } while (0)

SCSAPI_VOID host_log(scs_log_type_t, scs_string_t text) { printf("%s\n", text); }

SCSAPI_RESULT register_channel(scs_string_t name, scs_u32_t index, scs_value_type_t type,
    scs_u32_t flags, scs_telemetry_channel_callback_t callback, scs_context_t context) {
    if (fail_channel) return SCS_RESULT_generic_error;
    Channel* channel = nullptr;
    if (!strcmp(name, SCS_TELEMETRY_CHANNEL_game_time) && type == SCS_VALUE_TYPE_u32)
        channel = &time_channel;
    else if (!strcmp(name, SCS_TELEMETRY_CHANNEL_multiplayer_time_offset) && type == SCS_VALUE_TYPE_s32) {
        if (fail_offset) return SCS_RESULT_not_found;
        channel = &offset_channel;
    }
    if (!channel || index != SCS_U32_NIL) return SCS_RESULT_invalid_parameter;
    if (channel->active) return SCS_RESULT_already_registered;
    channel->active = true;
    channel->callback = callback;
    channel->context = context;
    channel->flags = flags;
    return SCS_RESULT_ok;
}
SCSAPI_RESULT unregister_channel(scs_string_t name, scs_u32_t, scs_value_type_t) {
    Channel* channel = !strcmp(name, SCS_TELEMETRY_CHANNEL_game_time) ? &time_channel : &offset_channel;
    *channel = {};
    return SCS_RESULT_ok;
}
SCSAPI_RESULT register_event(scs_event_t event, scs_telemetry_event_callback_t callback, scs_context_t context) {
    if (event >= 8 || event == fail_event) return SCS_RESULT_generic_error;
    if (events[event].callback) return SCS_RESULT_already_registered;
    events[event].callback = callback;
    events[event].context = context;
    return SCS_RESULT_ok;
}
SCSAPI_RESULT unregister_event(scs_event_t event) {
    if (event >= 8) return SCS_RESULT_invalid_parameter;
    events[event] = {};
    return SCS_RESULT_ok;
}
void event(scs_event_t event) {
    if (events[event].callback) events[event].callback(event, nullptr, events[event].context);
}
void game_time(uint32_t minutes) {
    scs_value_t value{};
    value.type = SCS_VALUE_TYPE_u32;
    value.value_u32.value = minutes;
    time_channel.callback(SCS_TELEMETRY_CHANNEL_game_time, SCS_U32_NIL, &value, time_channel.context);
}

int main(int argc, char** argv) {
    CHECK(argc == 3 || (argc == 4 && !strcmp(argv[3], "--interval2")));
    const HMODULE dll = LoadLibraryA(argv[1]);
    CHECK(dll != nullptr);
    typedef scs_result_t (SCSAPIFUNC *InitFn)(scs_u32_t, const scs_telemetry_init_params_t*);
    typedef void (SCSAPIFUNC *ShutdownFn)(void);
    const auto init = reinterpret_cast<InitFn>(GetProcAddress(dll, "scs_telemetry_init"));
    const auto shutdown = reinterpret_cast<ShutdownFn>(GetProcAddress(dll, "scs_telemetry_shutdown"));
    CHECK(init && shutdown);
    scs_telemetry_init_params_v100_t api{};
    api.common.game_id = "ats";
    api.common.game_name = "Isolated Weaysy SDK mock";
    api.common.log = host_log;
    api.register_for_channel = register_channel;
    api.unregister_from_channel = unregister_channel;
    api.register_for_event = register_event;
    api.unregister_from_event = unregister_event;
    if (argc == 4) {
        CHECK(init(SCS_TELEMETRY_VERSION_CURRENT, &api) == SCS_RESULT_ok);
        wchar_t name[64]{};
        weaysy::mapping_name(GetCurrentProcessId(), name);
        HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
        CHECK(mapping != nullptr);
        const auto* mapped = static_cast<const weaysy::ThemeSnapshot*>(
            MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(weaysy::ThemeSnapshot)));
        CHECK(mapped != nullptr);
        weaysy::ThemeSnapshot snap{};
        CHECK(weaysy::read_theme_snapshot(mapped, snap) && snap.mode == 0 && snap.byte_size == 80);
        event(SCS_TELEMETRY_EVENT_started);
        const uint32_t minutes[] = {0, 1, 2, 3, 4, 5, 6};
        const uint32_t themes[] = {0, 0, 1, 1, 0, 0, 1};
        for (unsigned i = 0; i != 7; ++i) {
            game_time(minutes[i]);
            event(SCS_TELEMETRY_EVENT_frame_end);
            CHECK(weaysy::read_theme_snapshot(mapped, snap) &&
                snap.absolute_minutes == minutes[i] && snap.theme == themes[i]);
        }
        CHECK(snap.generation == 4);
        event(SCS_TELEMETRY_EVENT_paused);
        for (unsigned i = 0; i != 10; ++i) event(SCS_TELEMETRY_EVENT_frame_end);
        CHECK(weaysy::read_theme_snapshot(mapped, snap) && snap.theme == 1 &&
            snap.absolute_minutes == 6 && snap.generation == 4);
        CHECK((snap.flags & weaysy::game_paused) && !(snap.flags & weaysy::time_currently_available));
        event(SCS_TELEMETRY_EVENT_started);
        game_time(1440);
        event(SCS_TELEMETRY_EVENT_frame_end);
        CHECK(weaysy::read_theme_snapshot(mapped, snap) && snap.theme == 0 && snap.minute_of_day == 0);
        game_time(1439);
        event(SCS_TELEMETRY_EVENT_frame_end);
        CHECK(weaysy::read_theme_snapshot(mapped, snap) && snap.theme == 1 && snap.minute_of_day == 1439);
        game_time(1200); // In test mode20:00 is DAY, unlike normal daily policy.
        event(SCS_TELEMETRY_EVENT_frame_end);
        CHECK(weaysy::read_theme_snapshot(mapped, snap) && snap.theme == 0);
        game_time(1202);
        event(SCS_TELEMETRY_EVENT_frame_end);
        CHECK(weaysy::read_theme_snapshot(mapped, snap) && snap.theme == 1);
        time_channel.callback(SCS_TELEMETRY_CHANNEL_game_time, SCS_U32_NIL, nullptr, time_channel.context);
        event(SCS_TELEMETRY_EVENT_frame_end);
        CHECK(weaysy::read_theme_snapshot(mapped, snap) && snap.theme == 1 &&
            !(snap.flags & weaysy::time_currently_available));
        shutdown();
        CHECK(weaysy::read_theme_snapshot(mapped, snap) && !(snap.flags & weaysy::publisher_running));
        UnmapViewOfFile(mapped);
        CloseHandle(mapping);
        CHECK(FreeLibrary(dll));
        printf("PASS %u interval2 SDK/IPC checks (mock host, no game loaded)\n", checks);
        return 0;
    }
    CHECK(init(SCS_MAKE_VERSION(99, 0), &api) == SCS_RESULT_unsupported);
    CHECK(init(SCS_TELEMETRY_VERSION_CURRENT, nullptr) == SCS_RESULT_invalid_parameter);
    api.common.game_id = "unrelated_game";
    CHECK(init(SCS_TELEMETRY_VERSION_CURRENT, &api) == SCS_RESULT_unsupported);
    api.common.game_id = "ats";
    fail_channel = true;
    CHECK(init(SCS_TELEMETRY_VERSION_CURRENT, &api) == SCS_RESULT_generic_error);
    CHECK(!time_channel.active && !offset_channel.active);
    fail_channel = false;
    fail_event = SCS_TELEMETRY_EVENT_paused;
    CHECK(init(SCS_TELEMETRY_VERSION_CURRENT, &api) == SCS_RESULT_generic_error);
    CHECK(!time_channel.active && !events[SCS_TELEMETRY_EVENT_frame_end].callback);
    fail_event = 0;
    CHECK(init(SCS_TELEMETRY_VERSION_CURRENT, &api) == SCS_RESULT_ok);
    CHECK(init(SCS_TELEMETRY_VERSION_CURRENT, &api) == SCS_RESULT_invalid_parameter);
    CHECK(time_channel.active && offset_channel.active);
    CHECK(time_channel.flags == (SCS_TELEMETRY_CHANNEL_FLAG_each_frame | SCS_TELEMETRY_CHANNEL_FLAG_no_value));
    wchar_t name[64]{};
    weaysy::mapping_name(GetCurrentProcessId(), name);
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    CHECK(mapping != nullptr);
    const auto* mapped = static_cast<const weaysy::ThemeSnapshot*>(
        MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(weaysy::ThemeSnapshot)));
    CHECK(mapped != nullptr);
    weaysy::ThemeSnapshot snap{};
    CHECK(weaysy::read_theme_snapshot(mapped, snap));
    CHECK(snap.flags & weaysy::publisher_running);
    CHECK(!(snap.flags & weaysy::has_valid_time));
    CHECK(!weaysy::read_theme_snapshot(mapped, snap, GetCurrentProcessId() + 1));
    event(SCS_TELEMETRY_EVENT_started);
    game_time(1199);
    event(SCS_TELEMETRY_EVENT_frame_end);
    CHECK(weaysy::read_theme_snapshot(mapped, snap) && snap.theme == 0 && snap.minute_of_day == 1199);
    CHECK(snap.generation == 1 && !(snap.flags & weaysy::game_paused));
    game_time(1200);
    event(SCS_TELEMETRY_EVENT_frame_end);
    CHECK(weaysy::read_theme_snapshot(mapped, snap) && snap.theme == 1 && snap.generation == 2);
    const uint64_t valid_tick = snap.last_valid_tick_ms;
    time_channel.callback(SCS_TELEMETRY_CHANNEL_game_time, SCS_U32_NIL, nullptr, time_channel.context);
    event(SCS_TELEMETRY_EVENT_frame_end);
    CHECK(weaysy::read_theme_snapshot(mapped, snap) && snap.theme == 1 && snap.absolute_minutes == 1200);
    CHECK(!(snap.flags & weaysy::time_currently_available));
    CHECK(snap.last_valid_tick_ms == valid_tick);
    game_time(1440 * 2 + 360);
    scs_value_t offset{};
    offset.type = SCS_VALUE_TYPE_s32;
    offset.value_s32.value = -120;
    offset_channel.callback(SCS_TELEMETRY_CHANNEL_multiplayer_time_offset,
        SCS_U32_NIL, &offset, offset_channel.context);
    event(SCS_TELEMETRY_EVENT_frame_end);
    CHECK(weaysy::read_theme_snapshot(mapped, snap) && snap.theme == 0 && snap.minute_of_day == 360);
    CHECK(snap.absolute_minutes == 3240 && snap.multiplayer_offset == -120);
    CHECK(snap.flags & weaysy::multiplayer_offset_available);
    event(SCS_TELEMETRY_EVENT_paused);
    CHECK(weaysy::read_theme_snapshot(mapped, snap) && (snap.flags & weaysy::game_paused));
    CHECK(snap.theme == 0);
    shutdown();
    CHECK(!time_channel.active && !offset_channel.active);
    CHECK(!events[SCS_TELEMETRY_EVENT_frame_end].callback && !events[SCS_TELEMETRY_EVENT_paused].callback);
    CHECK(weaysy::read_theme_snapshot(mapped, snap) && !(snap.flags & weaysy::publisher_running));
    // Consumer deliberately keeps its old handle/view across plugin restart.
    // This catches sdk reload failing solely because the mapping still exists.
    api.common.game_id = "eut2";
    fail_offset = true;
    CHECK(init(SCS_TELEMETRY_VERSION_1_00, &api) == SCS_RESULT_ok);
    CHECK(time_channel.active && !offset_channel.active);
    CHECK(weaysy::read_theme_snapshot(mapped, snap) && (snap.flags & weaysy::publisher_running));
    CHECK(!(snap.flags & weaysy::has_valid_time));
    game_time(1200);
    event(SCS_TELEMETRY_EVENT_frame_end);
    CHECK(weaysy::read_theme_snapshot(mapped, snap) && snap.theme == 1 && snap.generation == 1);
    char command[MAX_PATH * 2]{};
    snprintf(command, sizeof(command), "\"%s\" %lu", argv[2], static_cast<unsigned long>(GetCurrentProcessId()));
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    CHECK(CreateProcessA(nullptr, command, nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                         nullptr, nullptr, &startup, &child));
    CHECK(WaitForSingleObject(child.hProcess, 10000) == WAIT_OBJECT_0);
    DWORD child_result = 1;
    CHECK(GetExitCodeProcess(child.hProcess, &child_result) && child_result == 0);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    shutdown();
    UnmapViewOfFile(mapped);
    CloseHandle(mapping);
    CHECK(FreeLibrary(dll));
    printf("PASS %u SDK/IPC checks (mock host, no game loaded)\n", checks);
    return 0;
}
