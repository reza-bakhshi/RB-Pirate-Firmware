#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "pirate/crash_record.h"

static void test_initialization(void) {
    printf("[RUN]  test_initialization\n");
    /* Simulate uninitialized garbage */
    memset(&bp_crash_record, 0xAA, sizeof(bp_crash_record));
    assert(!bp_crash_record_is_valid(&bp_crash_record));

    bp_crash_record_init();
    assert(bp_crash_record_is_valid(&bp_crash_record));
    assert(bp_crash_record.magic == BP_CRASH_MAGIC);
    assert(bp_crash_record.version == BP_CRASH_VERSION);
    assert(bp_crash_record.reason == BP_CRASH_REASON_NONE);
    assert(bp_crash_record.consecutive_crashes == 0);
    printf("[PASS] test_initialization\n");
}

static void test_capture_assert(void) {
    printf("[RUN]  test_capture_assert\n");
    bp_crash_record_clear();

    bp_crash_record_capture(BP_CRASH_REASON_ASSERT, 0, "src/mode/spi.c", 142, "Buffer null pointer", NULL);

    assert(bp_crash_record_is_valid(&bp_crash_record));
    assert(bp_crash_record.reason == BP_CRASH_REASON_ASSERT);
    assert(bp_crash_record.core_id == 0);
    assert(strcmp(bp_crash_record.file, "src/mode/spi.c") == 0);
    assert(bp_crash_record.line == 142);
    assert(strcmp(bp_crash_record.message, "Buffer null pointer") == 0);
    assert(bp_crash_record.consecutive_crashes == 1);
    printf("[PASS] test_capture_assert\n");
}

static void test_capture_hardfault(void) {
    printf("[RUN]  test_capture_hardfault\n");
    bp_crash_record_clear();

    bp_crash_registers_t regs = {
        .r0 = 0x10000000,
        .r1 = 0x20000000,
        .r2 = 0x00000000,
        .r3 = 0x12345678,
        .r12 = 0xCAFEBABE,
        .lr = 0x10004567,
        .pc = 0x100089AB,
        .xpsr = 0x01000000,
        .cfsr = 0x00000082, /* Precise data bus error */
        .hfsr = 0x40000000, /* FORCED */
        .mmfar = 0x00000000,
        .bfar = 0xE000ED00,
        .sp = 0x20007FF0,
    };

    bp_crash_record_capture(BP_CRASH_REASON_HARDFAULT, 1, NULL, 0, "HardFault Handler", &regs);

    assert(bp_crash_record_is_valid(&bp_crash_record));
    assert(bp_crash_record.reason == BP_CRASH_REASON_HARDFAULT);
    assert(bp_crash_record.core_id == 1);
    assert(bp_crash_record.regs.pc == 0x100089AB);
    assert(bp_crash_record.regs.cfsr == 0x00000082);
    assert(bp_crash_record.regs.bfar == 0xE000ED00);
    assert(bp_crash_record.consecutive_crashes == 1);
    printf("[PASS] test_capture_hardfault\n");
}

static void test_consecutive_crashes(void) {
    printf("[RUN]  test_consecutive_crashes\n");
    bp_crash_record_clear();
    assert(bp_crash_record.consecutive_crashes == 0);

    bp_crash_record_capture(BP_CRASH_REASON_WATCHDOG, 0, NULL, 0, "Watchdog timeout", NULL);
    assert(bp_crash_record.consecutive_crashes == 1);

    bp_crash_record_capture(BP_CRASH_REASON_WATCHDOG, 1, NULL, 0, "Watchdog timeout Core 1", NULL);
    assert(bp_crash_record.consecutive_crashes == 2);

    bp_crash_record_capture(BP_CRASH_REASON_PANIC, 0, "pirate.c", 99, "OOM", NULL);
    assert(bp_crash_record.consecutive_crashes == 3);
    printf("[PASS] test_consecutive_crashes\n");
}

static void test_corruption_detection(void) {
    printf("[RUN]  test_corruption_detection\n");
    bp_crash_record_init();
    bp_crash_record_capture(BP_CRASH_REASON_ASSERT, 0, "foo.c", 10, "Test", NULL);
    assert(bp_crash_record_is_valid(&bp_crash_record));

    /* Corrupt a single byte in the file path */
    bp_crash_record.file[0] ^= 0x55;
    assert(!bp_crash_record_is_valid(&bp_crash_record));

    /* Restore and verify valid again */
    bp_crash_record.file[0] ^= 0x55;
    assert(bp_crash_record_is_valid(&bp_crash_record));

    /* Corrupt the magic */
    bp_crash_record.magic = 0xDEADBEEF;
    assert(!bp_crash_record_is_valid(&bp_crash_record));
    printf("[PASS] test_corruption_detection\n");
}

static void test_clear(void) {
    printf("[RUN]  test_clear\n");
    bp_crash_record_init();
    bp_crash_record_capture(BP_CRASH_REASON_PANIC, 0, "main.c", 50, "Fatal", NULL);
    assert(bp_crash_record.reason == BP_CRASH_REASON_PANIC);

    bp_crash_record_clear();
    assert(bp_crash_record_is_valid(&bp_crash_record));
    assert(bp_crash_record.reason == BP_CRASH_REASON_NONE);
    assert(bp_crash_record.consecutive_crashes == 0);
    printf("[PASS] test_clear\n");
}

static void test_reset_consecutive(void) {
    printf("[RUN]  test_reset_consecutive\n");
    bp_crash_record_init();
    bp_crash_record_capture(BP_CRASH_REASON_ASSERT, 0, "test.c", 1, "msg", NULL);
    assert(bp_crash_record.consecutive_crashes == 1);

    bp_crash_record_reset_consecutive();
    assert(bp_crash_record_is_valid(&bp_crash_record));
    assert(bp_crash_record.consecutive_crashes == 0);
    assert(bp_crash_record.reason == BP_CRASH_REASON_ASSERT);
    printf("[PASS] test_reset_consecutive\n");
}

int main(void) {
    printf("=== Crash Record Test Suite ===\n");
    test_initialization();
    test_capture_assert();
    test_capture_hardfault();
    test_consecutive_crashes();
    test_corruption_detection();
    test_clear();
    test_reset_consecutive();
    printf("=== ALL CRASH RECORD TESTS PASSED ===\n");
    return 0;
}
