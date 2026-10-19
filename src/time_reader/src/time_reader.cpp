// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Weaysy
// SDK time plus an optional read-only, exact-build ATS local-time adapter.
// No graphics hooks, native calls, process writes or file swaps.
#define WIN32_LEAN_AND_MEAN
#include "weaysy_theme_ipc.h"
#include "theme_policy.h"
#include "native_local_clock.h"
#include "scssdk_telemetry.h"
#include "common/scssdk_telemetry_common_channels.h"
#include "amtrucks/scssdk_ats.h"
#include "eurotrucks2/scssdk_eut2.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

namespace {
weaysy::ThemeState state;
scs_telemetry_init_params_v100_t api{};
HANDLE mapping = nullptr;
HANDLE producer_claim = nullptr;
weaysy::ThemeSnapshot* shared = nullptr;
HANDLE log_file = INVALID_HANDLE_VALUE;
bool initialized = false;
bool channel_registered = false;
bool offset_registered = false;
bool offset_available = false;
scs_s32_t offset = 0;
scs_event_t registered_events[4]{};
unsigned event_count = 0;
uint64_t last_logged_minute = UINT64_MAX;
uint32_t last_logged_generation = 0;
int32_t last_logged_correction = 0;
bool use_local_timezone = true;
bool native_clock_active = false;
bool native_clock_valid = false;
bool native_unavailable_logged = false;
bool pending_available = false;
bool received_this_frame = false;
uint32_t pending_minutes = 0;
weaysy::NativeLocalClockReader native_clock;
weaysy::NativeClockSample last_zone{};
bool has_zone = false;
wchar_t directory[MAX_PATH]{};
SRWLOCK callback_lock = SRWLOCK_INIT;

void log_message(scs_log_type_t level, const char* text) noexcept {
    if (api.common.log) api.common.log(level, text);
    if (log_file != INVALID_HANDLE_VALUE) {
        DWORD count = 0;
        WriteFile(log_file, text, static_cast<DWORD>(strlen(text)), &count, nullptr);
        WriteFile(log_file, "\r\n", 2, &count, nullptr);
    }
}

void publish(bool running) noexcept {
    if (!shared) return;
    InterlockedIncrement(&shared->sequence);
    MemoryBarrier();
    shared->magic = weaysy::theme_ipc_magic;
    shared->version = weaysy::theme_ipc_version;
    shared->byte_size = sizeof(*shared);
    shared->process_id = GetCurrentProcessId();
    shared->flags = (running ? weaysy::publisher_running : 0) |
        (state.has_time ? weaysy::has_valid_time : 0) |
        (state.time_available ? weaysy::time_currently_available : 0) |
        (state.paused ? weaysy::game_paused : 0) |
        (offset_available ? weaysy::multiplayer_offset_available : 0) |
        (native_clock_valid && !state.policy.test_interval_minutes ? weaysy::local_timezone_applied : 0);
    shared->theme = static_cast<uint32_t>(state.theme);
    shared->absolute_minutes = state.absolute_minutes;
    shared->minute_of_day = state.absolute_minutes % 1440;
    shared->day_start = state.policy.day_start;
    shared->night_start = state.policy.night_start;
    shared->mode = 0;
    shared->multiplayer_offset = offset;
    shared->reserved0 = native_clock_valid && !state.policy.test_interval_minutes
        ? static_cast<uint32_t>(state.local_correction) : 0;
    shared->reserved1 = native_clock_valid && !state.policy.test_interval_minutes
        ? state.selected_minute : 0;
    shared->publish_tick_ms = GetTickCount64();
    shared->last_valid_tick_ms = state.last_valid_tick_ms;
    shared->generation = state.generation;
    MemoryBarrier();
    InterlockedIncrement(&shared->sequence);
}

void log_clock() noexcept {
    if (!state.has_time || (last_logged_minute == state.absolute_minutes &&
        last_logged_generation == state.generation && last_logged_correction == state.local_correction)) return;
    char line[384]{};
    snprintf(line, sizeof(line),
        "[WeaysyTimeReader] game.time=%lu clock=%02lu:%02lu selected_clock=%02lu:%02lu local_correction=%ld theme=%s generation=%lu paused=%u test_interval_minutes=%lu convoy_offset=%ld (not applied)",
        static_cast<unsigned long>(state.absolute_minutes),
        static_cast<unsigned long>((state.absolute_minutes % 1440) / 60),
        static_cast<unsigned long>(state.absolute_minutes % 60),
        static_cast<unsigned long>(state.selected_minute / 60),
        static_cast<unsigned long>(state.selected_minute % 60),
        static_cast<long>(state.local_correction),
        state.theme == weaysy::Theme::night ? "NIGHT" : "DAY",
        static_cast<unsigned long>(state.generation), state.paused ? 1 : 0,
        static_cast<unsigned long>(state.policy.test_interval_minutes),
        static_cast<long>(offset));
    log_message(SCS_LOG_TYPE_message, line);
    last_logged_minute = state.absolute_minutes;
    last_logged_generation = state.generation;
    last_logged_correction = state.local_correction;
}

void update_clock() noexcept {
    native_clock_valid = false;
    if (!received_this_frame || !pending_available) {
        state.unavailable();
        return;
    }
    received_this_frame = false;
    if (native_clock_active && !state.policy.test_interval_minutes) {
        weaysy::NativeClockSample sample{};
        if (state.paused || !native_clock.sample(pending_minutes, sample)) {
            state.unavailable(); // Loading/teleport race: retain last genuine theme.
            if (!state.paused && !native_unavailable_logged) {
                log_message(SCS_LOG_TYPE_warning,
                    "[WeaysyTimeReader] Local clock temporarily unavailable; retaining theme until a coherent fresh sample.");
                native_unavailable_logged = true;
            }
            return;
        }
        if (!has_zone || sample.zone_minutes != last_zone.zone_minutes ||
            sample.base_minutes != last_zone.base_minutes || sample.context != last_zone.context ||
            sample.economy != last_zone.economy || strcmp(sample.zone_name, last_zone.zone_name)) {
            char line[320]{};
            const uint32_t minute = weaysy::local_minute(pending_minutes, sample.correction);
            snprintf(line, sizeof(line),
                "[WeaysyTimeReader] Local zone refreshed: zone=%s utc_offset=%ld base_utc_offset=%ld correction=%ld local_clock=%02lu:%02lu; rechecked every frame, including travel/load/teleport.",
                sample.zone_name, static_cast<long>(sample.zone_minutes),
                static_cast<long>(sample.base_minutes), static_cast<long>(sample.correction),
                static_cast<unsigned long>(minute / 60), static_cast<unsigned long>(minute % 60));
            log_message(SCS_LOG_TYPE_message, line);
        }
        last_zone = sample;
        has_zone = native_clock_valid = true;
        native_unavailable_logged = false;
        state.receive_time(pending_minutes, GetTickCount64(), sample.correction);
    } else {
        state.receive_time(pending_minutes, GetTickCount64());
    }
}

SCSAPI_VOID on_time(const scs_string_t, const scs_u32_t,
                    const scs_value_t* const value, const scs_context_t) {
    AcquireSRWLockExclusive(&callback_lock);
    if (initialized) {
        received_this_frame = true;
        pending_available = value && value->type == SCS_VALUE_TYPE_u32;
        if (pending_available)
            pending_minutes = value->value_u32.value;
        else {
            state.unavailable(); // Retain last good theme, never tick a local clock.
            native_clock_valid = false;
        }
    }
    ReleaseSRWLockExclusive(&callback_lock);
}

SCSAPI_VOID on_offset(const scs_string_t, const scs_u32_t,
                      const scs_value_t* const value, const scs_context_t) {
    AcquireSRWLockExclusive(&callback_lock);
    if (initialized) {
        offset_available = value && value->type == SCS_VALUE_TYPE_s32;
        if (offset_available) offset = value->value_s32.value;
    }
    ReleaseSRWLockExclusive(&callback_lock);
}

SCSAPI_VOID on_event(const scs_event_t event, const void* const,
                     const scs_context_t) {
    AcquireSRWLockExclusive(&callback_lock);
    if (initialized) {
        if (event == SCS_TELEMETRY_EVENT_paused) {
            state.paused = true;
            state.unavailable(); // Next driving/profile session needs a fresh sample.
            received_this_frame = false;
            native_clock_valid = false;
        }
        else if (event == SCS_TELEMETRY_EVENT_started) state.paused = false;
        if (event == SCS_TELEMETRY_EVENT_frame_end) { update_clock(); log_clock(); }
        publish(true); // One complete snapshot after channel delivery per frame.
    }
    ReleaseSRWLockExclusive(&callback_lock);
}

bool module_directory() noexcept {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&module_directory), &module)) return false;
    DWORD length = GetModuleFileNameW(module, directory, MAX_PATH);
    if (!length || length >= MAX_PATH) return false;
    wchar_t* slash = wcsrchr(directory, L'\\');
    if (!slash) return false;
    slash[1] = L'\0';
    return true;
}

void open_log() noexcept {
    wchar_t path[MAX_PATH]{};
    if (swprintf(path, MAX_PATH, L"%lsWeaysyTimeReader.log", directory) < 0) return;
    log_file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

bool read_policy() noexcept {
    wchar_t path[MAX_PATH]{}, day[32]{}, night[32]{}, interval[32]{}, local[32]{};
    if (swprintf(path, MAX_PATH, L"%lsWeaysyTimeReader.ini", directory) < 0) return false;
    GetPrivateProfileStringW(L"Clock", L"DayStart", L"06:00", day, 32, path);
    GetPrivateProfileStringW(L"Clock", L"NightStart", L"20:00", night, 32, path);
    GetPrivateProfileStringW(L"Test", L"SwitchEveryGameMinutes", L"0", interval, 32, path);
    GetPrivateProfileStringW(L"Clock", L"UseLocalTimeZone", L"1", local, 32, path);
    uint32_t parsed_day = 0, parsed_night = 0, parsed_interval = 0;
    if (!weaysy::parse_hhmm(day, parsed_day) ||
        !weaysy::parse_hhmm(night, parsed_night) ||
        !weaysy::parse_test_interval(interval, parsed_interval) ||
        (wcscmp(local, L"0") && wcscmp(local, L"1"))) return false;
    use_local_timezone = !wcscmp(local, L"1");
    state.policy.day_start = parsed_day;
    state.policy.night_start = parsed_night;
    state.policy.test_interval_minutes = parsed_interval;
    return state.policy.valid();
}

void cleanup_registration() noexcept {
    if (channel_registered && api.unregister_from_channel &&
        api.unregister_from_channel(SCS_TELEMETRY_CHANNEL_game_time,
            SCS_U32_NIL, SCS_VALUE_TYPE_u32) != SCS_RESULT_ok)
        log_message(SCS_LOG_TYPE_warning, "[WeaysyTimeReader] SDK rejected game.time cleanup.");
    channel_registered = false;
    if (offset_registered && api.unregister_from_channel &&
        api.unregister_from_channel(SCS_TELEMETRY_CHANNEL_multiplayer_time_offset,
            SCS_U32_NIL, SCS_VALUE_TYPE_s32) != SCS_RESULT_ok)
        log_message(SCS_LOG_TYPE_warning, "[WeaysyTimeReader] SDK rejected offset cleanup.");
    offset_registered = false;
    while (event_count) {
        const scs_event_t event = registered_events[--event_count];
        if (api.unregister_from_event && api.unregister_from_event(event) != SCS_RESULT_ok)
            log_message(SCS_LOG_TYPE_warning, "[WeaysyTimeReader] SDK rejected event cleanup.");
    }
}

void close_resources() noexcept {
    if (shared) { UnmapViewOfFile(shared); shared = nullptr; }
    if (mapping) { CloseHandle(mapping); mapping = nullptr; }
    if (producer_claim) { CloseHandle(producer_claim); producer_claim = nullptr; }
    if (log_file != INVALID_HANDLE_VALUE) { CloseHandle(log_file); log_file = INVALID_HANDLE_VALUE; }
}

bool register_event(scs_event_t event) noexcept {
    if (api.register_for_event(event, on_event, nullptr) != SCS_RESULT_ok) return false;
    registered_events[event_count++] = event;
    return true;
}
} // namespace

extern "C" SCSAPI_RESULT scs_telemetry_init(
    const scs_u32_t version, const scs_telemetry_init_params_t* const params) {
    if (version != SCS_TELEMETRY_VERSION_1_00 && version != SCS_TELEMETRY_VERSION_1_01)
        return SCS_RESULT_unsupported;
    if (!params || initialized) return SCS_RESULT_invalid_parameter;
    api = *static_cast<const scs_telemetry_init_params_v100_t*>(params);
    if (!api.common.log || !api.common.game_id || !api.register_for_channel ||
        !api.unregister_from_channel || !api.register_for_event || !api.unregister_from_event)
        return SCS_RESULT_invalid_parameter;
    if (strcmp(api.common.game_id, SCS_GAME_ID_ATS) != 0 &&
        strcmp(api.common.game_id, SCS_GAME_ID_EUT2) != 0) return SCS_RESULT_unsupported;
    state = weaysy::ThemeState{};
    offset = 0;
    offset_available = false;
    last_logged_minute = UINT64_MAX;
    last_logged_generation = 0;
    last_logged_correction = 0;
    native_clock_active = native_clock_valid = native_unavailable_logged = false;
    pending_available = received_this_frame = has_zone = false;
    last_zone = {};
    if (!module_directory()) return SCS_RESULT_generic_error;
    open_log();
    if (!read_policy()) {
        log_message(SCS_LOG_TYPE_error, "[WeaysyTimeReader] Invalid clock thresholds; initialization stopped.");
        close_resources(); return SCS_RESULT_invalid_parameter;
    }
    if (use_local_timezone && !state.policy.test_interval_minutes) {
        native_clock_active = !strcmp(api.common.game_id, SCS_GAME_ID_ATS) &&
                              native_clock.initialize_current_process();
        log_message(native_clock_active ? SCS_LOG_TYPE_message : SCS_LOG_TYPE_warning,
            native_clock_active
                ? "[WeaysyTimeReader] ATS local clock enabled; exact executable SHA-256 verified. Live game zone and base zone are read only."
                : "[WeaysyTimeReader] Local clock adapter unsupported for this executable; using raw SDK time. Local-time switching is not guaranteed on this build.");
    }
    wchar_t name[64]{};
    // Consumers never open this event. Its object lifetime is the producer claim,
    // unlike a thread-owned mutex which could be recursively acquired by a
    // duplicate DLL initialized on the same game thread.
    wsprintfW(name, L"Local\\WeaysyThemeWriterV1-%lu", static_cast<unsigned long>(GetCurrentProcessId()));
    producer_claim = CreateEventW(nullptr, TRUE, FALSE, name);
    const DWORD claim_error = GetLastError();
    if (!producer_claim || claim_error == ERROR_ALREADY_EXISTS) {
        log_message(SCS_LOG_TYPE_error, "[WeaysyTimeReader] Another producer owns this process; initialization stopped.");
        close_resources(); return SCS_RESULT_generic_error;
    }
    weaysy::mapping_name(GetCurrentProcessId(), name);
    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                sizeof(weaysy::ThemeSnapshot), name);
    const DWORD map_error = GetLastError();
    if (!mapping) {
        log_message(SCS_LOG_TYPE_error, "[WeaysyTimeReader] Cannot create IPC mapping; initialization stopped.");
        close_resources(); return SCS_RESULT_generic_error;
    }
    shared = static_cast<weaysy::ThemeSnapshot*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(*shared)));
    if (!shared) { close_resources(); return SCS_RESULT_generic_error; }
    if (map_error == ERROR_ALREADY_EXISTS) {
        weaysy::ThemeSnapshot previous{};
        if (!weaysy::read_theme_snapshot(shared, previous) ||
            (previous.flags & weaysy::publisher_running)) {
            log_message(SCS_LOG_TYPE_error, "[WeaysyTimeReader] Existing mapping is active or incompatible; initialization stopped.");
            close_resources(); return SCS_RESULT_generic_error;
        }
        // Reuse a stopped mapping held by a graphics consumer across sdk reload.
        // Do not ZeroMemory: the next seqlock publication replaces it coherently.
    } else {
        ZeroMemory(shared, sizeof(*shared));
    }
    const scs_u32_t flags = SCS_TELEMETRY_CHANNEL_FLAG_each_frame | SCS_TELEMETRY_CHANNEL_FLAG_no_value;
    channel_registered = api.register_for_channel(SCS_TELEMETRY_CHANNEL_game_time,
        SCS_U32_NIL, SCS_VALUE_TYPE_u32, flags, on_time, nullptr) == SCS_RESULT_ok;
    if (!channel_registered || !register_event(SCS_TELEMETRY_EVENT_frame_end) ||
        !register_event(SCS_TELEMETRY_EVENT_paused) || !register_event(SCS_TELEMETRY_EVENT_started)) {
        log_message(SCS_LOG_TYPE_error, "[WeaysyTimeReader] Required telemetry registration failed; rolling back.");
        cleanup_registration(); close_resources(); return SCS_RESULT_generic_error;
    }
    // Convoy offset is optional diagnostic information, never silently applied.
    offset_registered = api.register_for_channel(SCS_TELEMETRY_CHANNEL_multiplayer_time_offset,
        SCS_U32_NIL, SCS_VALUE_TYPE_s32, flags, on_offset, nullptr) == SCS_RESULT_ok;
    initialized = true;
    publish(true);
    char mode_line[256]{};
    snprintf(mode_line, sizeof(mode_line),
        "[WeaysyTimeReader] Local prototype v0.2 initialized. mode=%s test_interval_minutes=%lu; waiting for game.time; no graphics changes in this DLL.",
        state.policy.test_interval_minutes ? "game_time_interval_test" :
            (native_clock_active ? "local_game_time_day_night" : "raw_game_time_day_night"),
        static_cast<unsigned long>(state.policy.test_interval_minutes));
    log_message(SCS_LOG_TYPE_message, mode_line);
    return SCS_RESULT_ok;
}

extern "C" SCSAPI_VOID scs_telemetry_shutdown(void) {
    // Unregister outside callback_lock; this permits a final callback to finish.
    cleanup_registration();
    AcquireSRWLockExclusive(&callback_lock);
    if (initialized) {
        initialized = false;
        state.time_available = false;
        publish(false);
        log_message(SCS_LOG_TYPE_message, "[WeaysyTimeReader] Shutdown; observer must stop applying themes.");
    }
    close_resources();
    ReleaseSRWLockExclusive(&callback_lock);
    api = {};
}
