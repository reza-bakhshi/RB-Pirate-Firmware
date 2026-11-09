#ifndef BP_KERNEL_HEALTH_H
#define BP_KERNEL_HEALTH_H

#include <stdint.h>
#include <stdbool.h>
#include "kernel/bp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bp_kernel_metrics {
    uint32_t core0_ticks;
    uint32_t core1_ticks;
    uint32_t core0_events_dispatched;
    uint32_t core1_events_dispatched;
    uint32_t timers_fired;
    uint32_t work_items_executed;
    uint32_t ops_cancelled;
    uint32_t event_drops;
    uint32_t log_drops;
    uint32_t event_high_water_c0;
    uint32_t event_high_water_c1;
} bp_kernel_metrics_t;

/**
 * @brief Initialize health tracking subsystem.
 */
bp_status_t bp_health_subsystem_init(void);

/**
 * @brief Increment core 0 loop tick counter.
 */
void bp_health_record_core0_tick(void);

/**
 * @brief Increment core 1 loop tick counter.
 */
void bp_health_record_core1_tick(void);

/**
 * @brief Record an event dispatch on specified core.
 */
void bp_health_record_event_dispatched(uint8_t core);

/**
 * @brief Record a timer firing.
 */
void bp_health_record_timer_fired(void);

/**
 * @brief Record a deferred work item execution.
 */
void bp_health_record_work_executed(void);

/**
 * @brief Record an operation cancellation.
 */
void bp_health_record_op_cancelled(void);

/**
 * @brief Snapshot current kernel metrics.
 */
void bp_health_get_metrics(bp_kernel_metrics_t* metrics);

#ifdef __cplusplus
}
#endif

#endif // BP_KERNEL_HEALTH_H
