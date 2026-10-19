// SPDX-License-Identifier: MIT
// Owned memory fixtures only: never attach to or change a running game.
#include "theme_policy.h"
#include "native_local_clock.h"
#include "weaysy_theme_ipc.h"
#include <stdio.h>

struct FakeMemory {
    static constexpr uintptr_t base = 0x10000000, eco_address = 0x20000000;
    static constexpr uintptr_t context_address = 0x30000000, name_address = 0x40000000;
    uintptr_t eco = eco_address, context = context_address;
    unsigned char economy_bytes[0x1a0]{}, context_bytes[0x180]{};
    char name[64] = "@@tz_cdt@@";
    uintptr_t fail_address = 0;
    bool change_context = false, tear_zone = false, jump_time = false;
    unsigned context_reads = 0, zone_reads = 0, raw_reads = 0;
    template<class T> void put(unsigned char* bytes, size_t at, T value) {
        memcpy(bytes + at, &value, sizeof(value));
    }
    FakeMemory() {
        put(economy_bytes, 0, base + weaysy::ats_economy_vtable);
        put(context_bytes, 0, base + weaysy::ats_context_vtable);
        put(economy_bytes, 0x19c, uint32_t(20 * 60));
        zone(-300, -360);
        put(context_bytes, 0x148, name_address);
        put(context_bytes, 0x150, uint32_t(strlen(name)));
        put(context_bytes, 0x154, uint32_t(sizeof(name)));
    }
    void zone(int32_t local, int32_t reference) {
        put(context_bytes, 0x138, local); put(context_bytes, 0x170, reference);
    }
    bool read(uintptr_t address, void* dest, size_t length) {
        if (address == fail_address) return false;
        if (address == base + weaysy::ats_economy_global && length == 8) {
            memcpy(dest, &eco, 8); return true;
        }
        if (address == base + weaysy::ats_context_global && length == 8) {
            ++context_reads;
            uintptr_t value = context_reads > 1 && change_context ? 0 : context;
            memcpy(dest, &value, 8); return true;
        }
        if (address == context_address + 0x138 && ++zone_reads > 1 && tear_zone)
            zone(-420, -360);
        if (address == eco_address + 0x19c && ++raw_reads > 1 && jump_time)
            put(economy_bytes, 0x19c, uint32_t(9000));
        struct Region { uintptr_t address; const void* bytes; size_t size; } regions[] = {
            {eco_address, economy_bytes, sizeof(economy_bytes)},
            {context_address, context_bytes, sizeof(context_bytes)},
            {name_address, name, sizeof(name)}};
        for (const auto& r : regions)
            if (address >= r.address && address - r.address <= r.size &&
                length <= r.size - (address - r.address)) {
                memcpy(dest, static_cast<const char*>(r.bytes) + address - r.address, length);
                return true;
            }
        return false;
    }
};

int main() {
    unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL line %u: %s\n", __LINE__, #x); return 1; } } while (0)
    using namespace weaysy;
    NativeClockSample sample{};
    auto read = [&](FakeMemory& memory, uint32_t raw = 1200) {
        memory.context_reads = memory.zone_reads = memory.raw_reads = 0;
        return read_native_clock(memory, FakeMemory::base, raw, true, sample);
    };
    FakeMemory memory;
    CHECK(read(memory));
    CHECK(sample.zone_minutes == -300 && sample.base_minutes == -360 && sample.correction == 60);
    CHECK(!strcmp(sample.zone_name, "@@tz_cdt@@"));
    CHECK(local_minute(1200, sample.correction) == 1260);
    ThemeState state;
    state.receive_time(1200, 100, sample.correction);
    CHECK(state.theme == Theme::night && state.selected_minute == 1260 && state.generation == 1);
    // Teleport while game.time has NOT changed: must immediately reselect DAY.
    memory.zone(-420, -360);
    strcpy(memory.name, "@@tz_pdt@@");
    CHECK(read(memory) && sample.correction == -60);
    state.receive_time(1200, 101, sample.correction);
    CHECK(state.theme == Theme::day && state.selected_minute == 1140 && state.generation == 2);
    CHECK(read(memory));
    state.receive_time(1200, 102, sample.correction);
    CHECK(state.generation == 2); // no oscillation during a stable zone
    // A custom map can also change the base zone. Never hard-code -360.
    memory.zone(330, 60);
    CHECK(read(memory) && sample.correction == 270);
    state.receive_time(1200, 103, sample.correction);
    CHECK(state.selected_minute == 30 && state.theme == Theme::night);
    CHECK(local_minute(10, -60) == 1390);
    CHECK(local_minute(1439, 60) == 59);
    CHECK(local_minute(UINT32_MAX, 1680) == (uint64_t(UINT32_MAX) + 1680) % 1440);
    CHECK(local_minute(0, -1680) == 1200);
    // A menu/load must retain the last theme; a fresh post-load zone chooses anew.
    state.unavailable();
    CHECK(!state.time_available && state.theme == Theme::night);
    memory.zone(-420, -360);
    CHECK(read(memory));
    state.receive_time(1200, 104, sample.correction);
    CHECK(state.time_available && state.theme == Theme::day);
    FakeMemory missing; missing.context = 0;
    CHECK(!read(missing));
    FakeMemory missing_eco; missing_eco.eco = 0;
    CHECK(!read(missing_eco));
    FakeMemory wrong; wrong.put(wrong.context_bytes, 0, uintptr_t(0));
    CHECK(!read(wrong));
    FakeMemory wrong_eco; wrong_eco.put(wrong_eco.economy_bytes, 0, uintptr_t(0));
    CHECK(!read(wrong_eco));
    FakeMemory changed; changed.change_context = true;
    CHECK(!read(changed));
    FakeMemory torn; torn.tear_zone = true;
    CHECK(!read(torn));
    FakeMemory jumped; jumped.jump_time = true;
    CHECK(!read(jumped));
    FakeMemory stale; stale.put(stale.economy_bytes, 0x19c, uint32_t(1100));
    CHECK(!read(stale));
    FakeMemory adjacent; adjacent.put(adjacent.economy_bytes, 0x19c, uint32_t(1201));
    CHECK(read(adjacent));
    FakeMemory bad_zone; bad_zone.zone(-10080, -360);
    CHECK(!read(bad_zone));
    FakeMemory bad_base; bad_base.zone(-420, 10080);
    CHECK(!read(bad_base));
    FakeMemory event_world; event_world.zone(0, -360);
    CHECK(read(event_world) && sample.correction == 360);
    CHECK(local_minute(1200, sample.correction) == 120); // old20:00 becomes02:00 UTC
    state.receive_time(840, 150, sample.correction); // corrected boundary:14:00 raw =20:00 UTC
    CHECK(state.theme == Theme::night && state.selected_minute == 1200);
    FakeMemory long_name; long_name.put(long_name.context_bytes, 0x150, uint32_t(64));
    CHECK(!read(long_name));
    FakeMemory bad_capacity; bad_capacity.put(bad_capacity.context_bytes, 0x154, uint32_t(2));
    CHECK(!read(bad_capacity));
    FakeMemory bad_label; bad_label.name[0] = '\n';
    CHECK(!read(bad_label));
    FakeMemory unreadable; unreadable.fail_address = FakeMemory::context_address + 0x138;
    sample.correction = 123;
    CHECK(!read(unreadable) && sample.correction == 123); // no partial publication
    state.policy.test_interval_minutes = 2;
    state.receive_time(1200, 200, -60);
    CHECK(state.theme == Theme::day && state.local_correction == 0);
    NativeLocalClockReader production_gate;
    CHECK(!production_gate.initialize_current_process()); // this exe is NOT ATS
    CHECK(!production_gate.probe(sample));
    // v1 extension keeps raw fields and the selected theme compatible with the
    // already-built graphics consumer, while exposing the separate local clock.
    ThemeSnapshot ipc{}, copied{};
    ipc.magic = theme_ipc_magic; ipc.version = theme_ipc_version;
    ipc.byte_size = sizeof(ipc); ipc.process_id = GetCurrentProcessId();
    ipc.day_start = 360; ipc.night_start = 1200;
    ipc.flags = publisher_running | has_valid_time | time_currently_available | local_timezone_applied;
    ipc.absolute_minutes = ipc.minute_of_day = 1200;
    ipc.theme = 0; ipc.reserved0 = static_cast<uint32_t>(-60); ipc.reserved1 = 1140;
    CHECK(read_theme_snapshot(&ipc, copied));
    CHECK(copied.absolute_minutes == 1200 && copied.minute_of_day == 1200 && copied.theme == 0);
    ipc.reserved1 = 1440;
    CHECK(!read_theme_snapshot(&ipc, copied));
    ipc.reserved1 = 1140; ipc.reserved0 = static_cast<uint32_t>(-2000);
    CHECK(!read_theme_snapshot(&ipc, copied));
    printf("PASS %u local-clock and teleport checks (owned memory fixtures)\n", checks);
    return 0;
}
