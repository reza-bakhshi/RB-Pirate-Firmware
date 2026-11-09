#include "pirate/watchdog.h"
#include "pirate/crash_record.h"
#include <string.h>

#if defined(__arm__) || defined(__thumb__)
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/timer.h"
#endif

volatile uint32_t bp_heartbeat[2] = { 0, 0 };
static uint32_t last_heartbeat[2] = { 0, 0 };
bp_watchdog_health_t bp_watchdog_health = { 0 };

#if defined(__arm__) || defined(__thumb__)
extern uint32_t __StackTop;
extern uint32_t __StackBottom;
extern uint32_t __StackOneTop;
extern uint32_t __StackOneBottom;
#endif

void bp_watchdog_init(void) {
#if defined(__arm__) || defined(__thumb__)
    if (watchdog_enable_caused_reboot()) {
        /* Check if intentional reboot was requested (e.g. mcu_reset) */
        if (watchdog_hw->scratch[7] == 0x52454254u) {
            watchdog_hw->scratch[7] = 0;
        } else {
            /* Check if a specific crash record was already captured */
            if (!bp_crash_record_is_valid(&bp_crash_record) || bp_crash_record.reason == BP_CRASH_REASON_NONE) {
                bp_crash_record_capture(BP_CRASH_REASON_WATCHDOG, 0, "watchdog", 0, "Hardware watchdog timeout", NULL);
            }
        }
    }

#if !defined(BP_WATCHDOG_DISABLE)
    /* Enable watchdog with pause_on_debug = true so SWD breakpoints don't cause resets */
    watchdog_enable(BP_WATCHDOG_TIMEOUT_MS, 1);
    bp_watchdog_health.enabled = true;
    bp_watchdog_health.paused_on_debug = true;
#else
    bp_watchdog_health.enabled = false;
    bp_watchdog_health.paused_on_debug = false;
#endif
#endif
}

void bp_watchdog_heartbeat(uint32_t core_id) {
    if (core_id < 2) {
        bp_heartbeat[core_id]++;
        if (core_id == 0) {
            bp_watchdog_health.core0_ticks++;
        } else {
            bp_watchdog_health.core1_ticks++;
        }
    }
}

void bp_watchdog_service(void) {
#if defined(__arm__) || defined(__thumb__)
    static uint32_t last_check_ms = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());

    if (now - last_check_ms < BP_WATCHDOG_CHECK_INTERVAL_MS) {
        return;
    }
    last_check_ms = now;

    bool core0_advanced = (bp_heartbeat[0] != last_heartbeat[0]);
    bool core1_advanced = (bp_heartbeat[1] != last_heartbeat[1]);

    if (core0_advanced && core1_advanced) {
        last_heartbeat[0] = bp_heartbeat[0];
        last_heartbeat[1] = bp_heartbeat[1];
        bp_watchdog_health.feeds++;

#if !defined(BP_WATCHDOG_DISABLE)
        watchdog_update();
#endif
    } else {
        if (!core0_advanced) {
            bp_watchdog_health.core0_missed++;
        }
        if (!core1_advanced) {
            bp_watchdog_health.core1_missed++;
        }
    }
#endif
}

void bp_stack_paint(uint32_t core_id) {
#if defined(__arm__) || defined(__thumb__)
    uint32_t* bottom = (core_id == 0) ? &__StackBottom : &__StackOneBottom;
    uint32_t* top = (core_id == 0) ? &__StackTop : &__StackOneTop;

    /* Leave a margin of 256 bytes for current call frame */
    uint32_t sp;
    __asm volatile("mov %0, sp" : "=r"(sp));
    uint32_t* current_sp = (uint32_t*)sp;

    for (uint32_t* p = bottom; p < current_sp - 64 && p < top; p++) {
        *p = BP_STACK_WATERMARK_WORD;
    }
#else
    (void)core_id;
#endif
}

uint32_t bp_stack_get_free(uint32_t core_id) {
#if defined(__arm__) || defined(__thumb__)
    const uint32_t* bottom = (core_id == 0) ? &__StackBottom : &__StackOneBottom;
    const uint32_t* top = (core_id == 0) ? &__StackTop : &__StackOneTop;

    uint32_t untouched_words = 0;
    for (const uint32_t* p = bottom; p < top; p++) {
        if (*p == BP_STACK_WATERMARK_WORD) {
            untouched_words++;
        } else {
            break;
        }
    }
    return untouched_words * sizeof(uint32_t);
#else
    (void)core_id;
    return 4096;
#endif
}
