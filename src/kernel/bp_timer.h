#ifndef BP_TIMER_H
#define BP_TIMER_H

#include <stdint.h>
#include <stdbool.h>
#include "kernel/bp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BP_MAX_SOFT_TIMERS 32

typedef void (*bp_timer_cb_t)(void* user_data);

typedef struct bp_timer {
    bp_timer_cb_t callback;
    void*         user_data;
    uint32_t      interval_ms;
    volatile uint32_t remaining_ms;
    bool          periodic;
    volatile bool active;
    volatile bool expired;
    bool          in_use;
} bp_timer_t;

/**
 * @brief Initialize the soft timer subsystem.
 */
bp_status_t bp_timer_subsystem_init(void);

/**
 * @brief Allocate a soft timer from the static pool.
 * @param cb Callback to execute upon expiration (dispatched in main loop).
 * @param user_data Argument passed to callback.
 * @param periodic True for repeating timer, false for one-shot.
 * @return Pointer to bp_timer_t, or NULL if static pool exhausted.
 */
bp_timer_t* bp_timer_create(bp_timer_cb_t cb, void* user_data, bool periodic);

/**
 * @brief Start or re-arm a soft timer.
 * @param timer Pointer to timer returned by bp_timer_create.
 * @param interval_ms Duration in milliseconds.
 */
bp_status_t bp_timer_start(bp_timer_t* timer, uint32_t interval_ms);

/**
 * @brief Stop an active soft timer.
 */
bp_status_t bp_timer_stop(bp_timer_t* timer);

/**
 * @brief Release a soft timer back to the static pool.
 */
bp_status_t bp_timer_destroy(bp_timer_t* timer);

/**
 * @brief Advance timer ticks by 1 ms.
 * Called deterministically from system tick timer (ISR or tick service).
 */
void bp_timer_tick_1ms(void);

/**
 * @brief Advance timer ticks by delta_ms milliseconds.
 */
void bp_timer_tick_ms(uint32_t delta_ms);

/**
 * @brief Dispatch expired soft timer callbacks.
 * Must be called in cooperative main loop context.
 * @return Number of callbacks dispatched.
 */
uint32_t bp_timer_dispatch(void);

/**
 * @brief Number of currently active soft timers.
 */
uint32_t bp_timer_active_count(void);

/**
 * @brief Total number of timer expiration events handled.
 */
uint32_t bp_timer_total_expirations(void);

#ifdef __cplusplus
}
#endif

#endif // BP_TIMER_H
