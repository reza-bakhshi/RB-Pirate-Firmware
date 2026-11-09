#ifndef BP_KERNEL_H
#define BP_KERNEL_H

#include <stdint.h>
#include <stdbool.h>
#include "kernel/bp_status.h"
#include "kernel/bp_event.h"
#include "kernel/bp_timer.h"
#include "kernel/bp_work.h"
#include "kernel/bp_op.h"
#include "kernel/bp_log.h"
#include "kernel/bp_health.h"
#include "kernel/bp_job.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BP_CORE0_EVENT_QUEUE_SIZE 64
#define BP_CORE1_EVENT_QUEUE_SIZE 32

typedef void (*bp_event_handler_t)(const bp_event_t* ev);

/**
 * @brief Initialize all kernel subsystems and queues.
 */
bp_status_t bp_kernel_init(void);

/**
 * @brief Post an event to a specific core's event queue and wake it.
 */
bp_status_t bp_kernel_post_event(uint8_t target_core, const bp_event_t* ev);

/**
 * @brief Set the event handler callback for a core.
 */
void bp_kernel_set_event_handler(uint8_t core, bp_event_handler_t handler);

/**
 * @brief Get the deferred work queue for a core.
 */
bp_work_queue_t* bp_kernel_get_work_queue(uint8_t core);

/**
 * @brief Get the event queue for a core.
 */
bp_event_queue_t* bp_kernel_get_event_queue(uint8_t core);

/**
 * @brief Execute one cooperative service slice for the specified core.
 * @param core 0 for Core 0 (CTRL), 1 for Core 1 (PROTO).
 * @return Total number of events, timers, and work items processed.
 */
uint32_t bp_service(uint8_t core);

/**
 * @brief Enter low-power idle state until next interrupt or inter-core event.
 */
void bp_kernel_idle(uint8_t core);

/**
 * @brief Run the kernel cooperative loop until should_continue returns false.
 */
void bp_kernel_run(uint8_t core, bool (*should_continue)(void));

#ifdef __cplusplus
}
#endif

#endif // BP_KERNEL_H
