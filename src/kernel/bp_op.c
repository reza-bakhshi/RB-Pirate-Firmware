#include "kernel/bp_op.h"
#include <stddef.h>

static bp_op_t* s_active_op = NULL;

bp_status_t bp_op_subsystem_init(void) {
    s_active_op = NULL;
    return BP_STATUS_OK;
}

bp_status_t bp_op_begin(bp_op_t* op, const char* name, uint32_t timeout_ms, uint32_t now_ms) {
    if (!op) return BP_STATUS_INVALID_PARAM;
    op->name = name ? name : "unnamed_op";
    op->start_time_ms = now_ms;
    op->timeout_ms = timeout_ms;
    op->cancelled = false;
    op->cancel_reason = BP_CANCEL_REASON_NONE;
    s_active_op = op;
    return BP_STATUS_OK;
}

void bp_op_end(bp_op_t* op) {
    if (s_active_op == op) {
        s_active_op = NULL;
    }
}

bp_status_t bp_op_cancel(bp_op_t* op, bp_cancel_reason_t reason) {
    if (!op) return BP_STATUS_INVALID_PARAM;
    op->cancel_reason = (uint8_t)reason;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    op->cancelled = true;
    return BP_STATUS_OK;
}

bp_status_t bp_op_request_cancel_active(bp_cancel_reason_t reason) {
    if (!s_active_op) {
        return BP_STATUS_OK;
    }
    return bp_op_cancel(s_active_op, reason);
}

bp_op_t* bp_op_get_active(void) {
    return s_active_op;
}

const char* bp_cancel_reason_str(bp_cancel_reason_t reason) {
    switch (reason) {
        case BP_CANCEL_REASON_NONE:           return "none";
        case BP_CANCEL_REASON_CTRL_C:         return "Ctrl+C";
        case BP_CANCEL_REASON_BUTTON:         return "button";
        case BP_CANCEL_REASON_USB_DISCONNECT: return "USB disconnect";
        case BP_CANCEL_REASON_TIMEOUT:        return "timeout";
        case BP_CANCEL_REASON_USER:           return "user abort";
        default:                              return "unknown";
    }
}
