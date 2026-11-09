#ifndef PIRATE_WATCHDOG_H
#define PIRATE_WATCHDOG_H

#include <stdint.h>
#include <stdbool.h>

#if defined(PICO_RP2040) && PICO_RP2040
#define BP_WATCHDOG_TIMEOUT_MS         4000u
#else
#define BP_WATCHDOG_TIMEOUT_MS         8000u
#endif
#define BP_WATCHDOG_CHECK_INTERVAL_MS  500u
#define BP_STACK_WATERMARK_WORD        0x55AA55AAu

typedef struct {
    uint32_t feeds;
    uint32_t core0_ticks;
    uint32_t core1_ticks;
    uint32_t core0_missed;
    uint32_t core1_missed;
    bool enabled;
    bool paused_on_debug;
} bp_watchdog_health_t;

#ifdef __cplusplus
extern "C" {
#endif

extern volatile uint32_t bp_heartbeat[2];
extern bp_watchdog_health_t bp_watchdog_health;

void bp_watchdog_init(void);
void bp_watchdog_heartbeat(uint32_t core_id);
void bp_watchdog_service(void);
uint32_t bp_stack_get_free(uint32_t core_id);
void bp_stack_paint(uint32_t core_id);

#ifdef __cplusplus
}
#endif

#endif // PIRATE_WATCHDOG_H
