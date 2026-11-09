#ifndef BP_JOB_H
#define BP_JOB_H

#include <stdint.h>
#include <stdbool.h>
#include "kernel/bp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BP_JOB_NAME_MAX 24
#define BP_JOB_OUTPUT_TEXT_MAX 48
#define BP_JOB_OUTPUT_RING_SIZE 16

typedef enum bp_job_state {
    BP_JOB_STATE_IDLE       = 0,
    BP_JOB_STATE_QUEUED     = 1,
    BP_JOB_STATE_RUNNING    = 2,
    BP_JOB_STATE_DONE       = 3,
    BP_JOB_STATE_CANCELLED  = 4,
    BP_JOB_STATE_TIMEOUT    = 5,
    BP_JOB_STATE_ERROR      = 6,
} bp_job_state_t;

typedef enum bp_job_proto {
    BP_JOB_PROTO_TEST       = 0,
    BP_JOB_PROTO_I2C        = 1,
    BP_JOB_PROTO_SPI        = 2,
    BP_JOB_PROTO_UART       = 3,
    BP_JOB_PROTO_1WIRE      = 4,
    BP_JOB_PROTO_SUBGHZ     = 5,
    BP_JOB_PROTO_IR         = 6,
    BP_JOB_PROTO_INFRARED   = 6,
    BP_JOB_PROTO_RFID       = 7,
    BP_JOB_PROTO_CUSTOM     = 8,
    BP_JOB_PROTO_RF24       = 9,
} bp_job_proto_t;

/**
 * @brief Slice execution callback for a protocol job.
 * Called cooperatively on Core 1 until it returns BP_STATUS_OK (done)
 * or an error code. Return BP_STATUS_BUSY to request next slice.
 */
typedef bp_status_t (*bp_job_slice_fn_t)(void* ctx, uint8_t* out_percent);

typedef struct bp_job {
    uint32_t           id;
    char               name[BP_JOB_NAME_MAX];
    uint8_t            proto;
    volatile uint8_t   state;        // bp_job_state_t
    volatile int8_t    status;       // bp_status_t
    volatile uint8_t   percent;      // 0..100
    uint32_t           start_time_ms;
    uint32_t           timeout_ms;
    bp_job_slice_fn_t  slice_fn;
    void*              ctx;
    volatile bool      cancel_requested;
} bp_job_t;

typedef struct bp_job_output {
    uint32_t job_id;
    uint8_t  percent;
    char     text[BP_JOB_OUTPUT_TEXT_MAX];
} bp_job_output_t;

/**
 * @brief Initialize protocol job subsystem.
 */
bp_status_t bp_job_subsystem_init(void);

/**
 * @brief Submit an asynchronous protocol job from Core 0 to Core 1.
 */
bp_status_t bp_job_submit(const char* name, uint8_t proto, uint32_t timeout_ms,
                          bp_job_slice_fn_t slice_fn, void* ctx,
                          uint32_t now_ms, uint32_t* out_job_id);

/**
 * @brief Dispatch an asynchronous protocol job from Core 0 to Core 1.
 * Automatically timestamps submission and queues the job for Core 1 execution.
 *
 * @param name Diagnostic name of job.
 * @param proto Protocol class (bp_job_proto_t).
 * @param timeout_ms Execution deadline in milliseconds (0 = none).
 * @param slice_fn Cooperative slice execution function.
 * @param ctx Context pointer passed to slice_fn.
 * @param out_job_id Optional pointer to receive allocated job ID.
 * @return BP_STATUS_OK on success, BP_STATUS_BUSY if job active, BP_STATUS_INVALID_PARAM.
 */
bp_status_t bp_job_dispatch(const char* name, uint8_t proto, uint32_t timeout_ms,
                            bp_job_slice_fn_t slice_fn, void* ctx,
                            uint32_t* out_job_id);

/**
 * @brief Request cancellation of a running job.
 */
bp_status_t bp_job_cancel(uint32_t job_id);

/**
 * @brief Poll current status and progress of a job.
 */
bp_status_t bp_job_get_state(uint32_t job_id, bp_job_state_t* out_state,
                             bp_status_t* out_status, uint8_t* out_percent);

/**
 * @brief Stream a progress update and optional message from Core 1.
 */
bp_status_t bp_job_emit_progress(uint8_t percent, const char* text);

/**
 * @brief Poll output/progress records on Core 0 for console display.
 */
bp_status_t bp_job_poll_output(bp_job_output_t* out_rec);

/**
 * @brief Check if the currently executing job on Core 1 was requested to cancel.
 */
bool bp_job_check_cancelled(void);

/**
 * @brief Execute one job slice on Core 1.
 * Called in bp_service(1).
 * @return 1 if job slice executed, 0 if idle.
 */
uint32_t bp_job_service(uint32_t now_ms);

/**
 * @brief Get human readable description of job state.
 */
const char* bp_job_state_str(bp_job_state_t state);

/**
 * @brief Get active job ID or 0 if none.
 */
uint32_t bp_job_get_active_id(void);

/**
 * @brief Cancel currently active job if any.
 */
static inline bp_status_t bp_job_cancel_active(void) {
    uint32_t jid = bp_job_get_active_id();
    if (jid != 0) {
        return bp_job_cancel(jid);
    }
    return BP_STATUS_OK;
}

#ifdef __cplusplus
}
#endif

#endif // BP_JOB_H
