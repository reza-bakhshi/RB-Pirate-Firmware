/* Modified by RB-Pirate contributors, 2026. Synchronous call with independent I/O. */
#include "services/bp_protocol_call.h"
#include "kernel/bp_kernel.h"
#include "pico/stdlib.h"
#include <stddef.h>
bp_status_t bp_protocol_call(const char *name, bp_job_slice_fn_t transaction, void *ctx) {
    if (get_core_num() != 0 || !transaction) return BP_STATUS_INVALID_PARAM;
    uint32_t id;
    bp_status_t status = bp_job_dispatch(name, BP_JOB_PROTO_CUSTOM, 0, transaction, ctx, &id);
    if (status != BP_STATUS_OK) return status;
    for (;;) {
        bp_job_state_t state;
        if (bp_job_get_state(id, &state, &status, NULL) != BP_STATUS_OK) return BP_STATUS_ERROR;
        if (state != BP_JOB_STATE_QUEUED && state != BP_JOB_STATE_RUNNING && state != BP_JOB_STATE_FINISHING) return status;
        bp_service(0);
        tight_loop_contents();
    }
}
