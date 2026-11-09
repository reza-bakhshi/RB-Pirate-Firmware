#include "pirate/crash_record.h"
#include <string.h>
#include <stdio.h>

#if defined(__arm__) || defined(__thumb__)
#include "pico/stdlib.h"
#include "hardware/timer.h"
#else
/* Host test fallback */
static inline uint32_t to_ms_since_boot(uint32_t val) {
    (void)val;
    return 0;
}
static inline uint32_t get_absolute_time(void) {
    return 0;
}
#endif

#if defined(__arm__) || defined(__thumb__)
__attribute__((section(".uninitialized_data"))) bp_crash_record_t bp_crash_record;
#else
bp_crash_record_t bp_crash_record;
#endif

/* Standard IEEE 802.3 CRC32 calculation */
static uint32_t crc32_byte(uint32_t crc, uint8_t byte) {
    crc ^= byte;
    for (int i = 0; i < 8; i++) {
        crc = (crc >> 1) ^ (0xEDB88320u & (-(int32_t)(crc & 1)));
    }
    return crc;
}

uint32_t bp_crash_calc_crc32(const bp_crash_record_t* rec) {
    if (!rec) {
        return 0;
    }
    uint32_t crc = 0xFFFFFFFFu;
    const uint8_t* p = (const uint8_t*)rec;
    size_t len = offsetof(bp_crash_record_t, crc32);
    for (size_t i = 0; i < len; i++) {
        crc = crc32_byte(crc, p[i]);
    }
    return ~crc;
}

bool bp_crash_record_is_valid(const bp_crash_record_t* rec) {
    if (!rec) {
        return false;
    }
    if (rec->magic != BP_CRASH_MAGIC) {
        return false;
    }
    if (rec->version != BP_CRASH_VERSION) {
        return false;
    }
    return bp_crash_calc_crc32(rec) == rec->crc32;
}

void bp_crash_record_init(void) {
    if (!bp_crash_record_is_valid(&bp_crash_record)) {
        /* Cold boot or corrupted persistent RAM: zero out and set valid empty record */
        memset(&bp_crash_record, 0, sizeof(bp_crash_record));
        bp_crash_record.magic = BP_CRASH_MAGIC;
        bp_crash_record.version = BP_CRASH_VERSION;
        bp_crash_record.reason = BP_CRASH_REASON_NONE;
        bp_crash_record.consecutive_crashes = 0;
        bp_crash_record.crc32 = bp_crash_calc_crc32(&bp_crash_record);
    }
}

void bp_crash_record_capture(bp_crash_reason_t reason,
                             uint32_t core_id,
                             const char* file,
                             uint32_t line,
                             const char* msg,
                             const bp_crash_registers_t* regs) {
    uint32_t prev_consecutive = 0;
    if (bp_crash_record_is_valid(&bp_crash_record)) {
        prev_consecutive = bp_crash_record.consecutive_crashes;
    }

    memset(&bp_crash_record, 0, sizeof(bp_crash_record));
    bp_crash_record.magic = BP_CRASH_MAGIC;
    bp_crash_record.version = BP_CRASH_VERSION;
    bp_crash_record.reason = (uint32_t)reason;
    bp_crash_record.core_id = core_id;
    bp_crash_record.consecutive_crashes = prev_consecutive + 1;

#if defined(__arm__) || defined(__thumb__)
    bp_crash_record.uptime_ms = to_ms_since_boot(get_absolute_time());
#else
    bp_crash_record.uptime_ms = 0;
#endif

    if (regs) {
        bp_crash_record.regs = *regs;
    }

    if (file) {
        strncpy(bp_crash_record.file, file, sizeof(bp_crash_record.file) - 1);
        bp_crash_record.file[sizeof(bp_crash_record.file) - 1] = '\0';
    }
    bp_crash_record.line = line;

    if (msg) {
        strncpy(bp_crash_record.message, msg, sizeof(bp_crash_record.message) - 1);
        bp_crash_record.message[sizeof(bp_crash_record.message) - 1] = '\0';
    }

    bp_crash_record.crc32 = bp_crash_calc_crc32(&bp_crash_record);
}

void bp_crash_record_clear(void) {
    memset(&bp_crash_record, 0, sizeof(bp_crash_record));
    bp_crash_record.magic = BP_CRASH_MAGIC;
    bp_crash_record.version = BP_CRASH_VERSION;
    bp_crash_record.reason = BP_CRASH_REASON_NONE;
    bp_crash_record.consecutive_crashes = 0;
    bp_crash_record.crc32 = bp_crash_calc_crc32(&bp_crash_record);
}

void bp_crash_record_reset_consecutive(void) {
    if (bp_crash_record_is_valid(&bp_crash_record)) {
        bp_crash_record.consecutive_crashes = 0;
        bp_crash_record.crc32 = bp_crash_calc_crc32(&bp_crash_record);
    }
}

void bp_crash_report_print(void) {
    if (!bp_crash_record_is_valid(&bp_crash_record)) {
        printf("No valid crash record found.\r\n");
        return;
    }
    if (bp_crash_record.reason == BP_CRASH_REASON_NONE) {
        printf("Last boot was clean. No crash recorded.\r\n");
        return;
    }

    printf("\r\n==================== CRASH REPORT ====================\r\n");
    printf("Fault Reason:        %s (Code %u)\r\n",
           bp_crash_reason_str((bp_crash_reason_t)bp_crash_record.reason),
           (unsigned)bp_crash_record.reason);
    printf("Executing Core:      Core %u\r\n", (unsigned)bp_crash_record.core_id);
    printf("Uptime at Crash:     %lu ms\r\n", (unsigned long)bp_crash_record.uptime_ms);
    printf("Consecutive Crashes: %u\r\n", (unsigned)bp_crash_record.consecutive_crashes);

    if (bp_crash_record.reason == BP_CRASH_REASON_HARDFAULT) {
        printf("\r\n--- Cortex-M33 Registers ---\r\n");
        printf("  R0:  0x%08lX   R1:  0x%08lX   R2:  0x%08lX   R3:  0x%08lX\r\n",
               (unsigned long)bp_crash_record.regs.r0,
               (unsigned long)bp_crash_record.regs.r1,
               (unsigned long)bp_crash_record.regs.r2,
               (unsigned long)bp_crash_record.regs.r3);
        printf("  R12: 0x%08lX   SP:  0x%08lX   LR:  0x%08lX   PC:  0x%08lX\r\n",
               (unsigned long)bp_crash_record.regs.r12,
               (unsigned long)bp_crash_record.regs.sp,
               (unsigned long)bp_crash_record.regs.lr,
               (unsigned long)bp_crash_record.regs.pc);
        printf("  xPSR:0x%08lX\r\n", (unsigned long)bp_crash_record.regs.xpsr);
        printf("  CFSR:0x%08lX   HFSR:0x%08lX   MMFAR:0x%08lX   BFAR:0x%08lX\r\n",
               (unsigned long)bp_crash_record.regs.cfsr,
               (unsigned long)bp_crash_record.regs.hfsr,
               (unsigned long)bp_crash_record.regs.mmfar,
               (unsigned long)bp_crash_record.regs.bfar);
    } else if (bp_crash_record.reason == BP_CRASH_REASON_ASSERT ||
               bp_crash_record.reason == BP_CRASH_REASON_PANIC) {
        printf("\r\n--- Assertion / Panic Info ---\r\n");
        printf("  File:    %s\r\n", bp_crash_record.file[0] ? bp_crash_record.file : "(unknown)");
        printf("  Line:    %u\r\n", (unsigned)bp_crash_record.line);
        printf("  Message: %s\r\n", bp_crash_record.message[0] ? bp_crash_record.message : "(none)");
    } else if (bp_crash_record.message[0] != '\0') {
        printf("  Details: %s\r\n", bp_crash_record.message);
    }
    printf("======================================================\r\n\r\n");
}

const char* bp_crash_reason_str(bp_crash_reason_t reason) {
    switch (reason) {
        case BP_CRASH_REASON_NONE:
            return "None";
        case BP_CRASH_REASON_WATCHDOG:
            return "Hardware Watchdog Timeout";
        case BP_CRASH_REASON_HARDFAULT:
            return "HardFault Exception";
        case BP_CRASH_REASON_ASSERT:
            return "Assertion Failure";
        case BP_CRASH_REASON_PANIC:
            return "System Panic";
        case BP_CRASH_REASON_FORCED:
            return "Forced Crash (Diagnostic)";
        default:
            return "Unknown";
    }
}
