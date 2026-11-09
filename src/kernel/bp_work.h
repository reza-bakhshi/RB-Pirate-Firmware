#ifndef BP_WORK_H
#define BP_WORK_H

#include <stdint.h>
#include <stdbool.h>
#include "kernel/bp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BP_MAX_WORK_ITEMS 16

typedef void (*bp_work_handler_t)(void* arg);

typedef struct bp_work_item {
    bp_work_handler_t handler;
    void*             arg;
    volatile bool     pending;
    bool              in_use;
} bp_work_item_t;

typedef struct bp_work_queue {
    bp_work_item_t items[BP_MAX_WORK_ITEMS];
    uint32_t executed_count;
} bp_work_queue_t;

/**
 * @brief Initialize a deferred work queue.
 */
bp_status_t bp_work_queue_init(bp_work_queue_t* wq);

/**
 * @brief Allocate a work item from the static pool.
 */
bp_work_item_t* bp_work_item_create(bp_work_queue_t* wq, bp_work_handler_t handler, void* arg);

/**
 * @brief Schedule a work item for execution in the next cooperative slice.
 */
bp_status_t bp_work_schedule(bp_work_item_t* item);

/**
 * @brief Cancel a pending work item before execution.
 */
bp_status_t bp_work_cancel(bp_work_item_t* item);

/**
 * @brief Destroy a work item and release back to pool.
 */
bp_status_t bp_work_item_destroy(bp_work_item_t* item);

/**
 * @brief Dispatch all pending work items in the queue.
 * @return Number of work items executed.
 */
uint32_t bp_work_dispatch(bp_work_queue_t* wq);

/**
 * @brief Count of currently pending work items.
 */
uint32_t bp_work_pending_count(const bp_work_queue_t* wq);

#ifdef __cplusplus
}
#endif

#endif // BP_WORK_H
