#ifndef BP_OP_H
#define BP_OP_H

#include <stdint.h>
#include <stdbool.h>
#include "kernel/bp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bp_cancel_reason {
    BP_CANCEL_REASON_NONE           = 0,
    BP_CANCEL_REASON_CTRL_C         = 1,
    BP_CANCEL_REASON_BUTTON         = 2,
    BP_CANCEL_REASON_USB_DISCONNECT = 3,
    BP_CANCEL_REASON_TIMEOUT        = 4,
    BP_CANCEL_REASON_USER           = 5,
} bp_cancel_reason_t;

typedef struct bp_op {
    const char*            name;
    uint32_t               start_time_ms;
    uint32_t               timeout_ms;
    volatile bool          cancelled;
    volatile uint8_t       cancel_reason;
} bp_op_t;

/**
 * @brief Initialize the operation manager.
 */
bp_status_t bp_op_subsystem_init(void);

/**
 * @brief Begin a cancellable long operation.
 * Sets the operation as the currently active operation for this core.
 */
bp_status_t bp_op_begin(bp_op_t* op, const char* name, uint32_t timeout_ms, uint32_t now_ms);

/**
 * @brief End an active operation.
 */
void bp_op_end(bp_op_t* op);

/**
 * @brief Request cancellation of a specific operation.
 */
bp_status_t bp_op_cancel(bp_op_t* op, bp_cancel_reason_t reason);

/**
 * @brief Request cancellation of whichever operation is currently active.
 * Safe to call from ISR or Ctrl+C input scanner.
 */
bp_status_t bp_op_request_cancel_active(bp_cancel_reason_t reason);

/**
 * @brief Check if an operation has been cancelled.
 */
static inline bool bp_op_is_cancelled(const bp_op_t* op) {
    if (!op) return false;
    return op->cancelled;
}

/**
 * @brief Get the currently active long operation, or NULL if none.
 */
bp_op_t* bp_op_get_active(void);

/**
 * @brief Get human-readable description of cancel reason.
 */
const char* bp_cancel_reason_str(bp_cancel_reason_t reason);

#ifdef __cplusplus
}
#endif

#endif // BP_OP_H
