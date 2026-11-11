/**
 * @file bp_protocol.c
 * @brief Protocol data plane implementation, heartbeats, and recovery for Core 1.
 */

#include "kernel/bp_protocol.h"
#include <string.h>

#include "system_config.h"
#include "kernel/bp_resource.h"
#include "kernel/bp_job.h"
#include "mode/bp_mode.h"

#if __has_include("pico/stdlib.h") && !defined(BP_HOST_TEST)
#include "pico/stdlib.h"
#include "pirate.h"
#include "pirate/bio.h"
#else
void bio_init(void);
#endif

static bp_protocol_telemetry_t bp_proto_telem;

bp_status_t bp_protocol_init(void) {
    memset(&bp_proto_telem, 0, sizeof(bp_proto_telem));
    bp_proto_telem.state = BP_PROTO_STATE_IDLE;
    bp_proto_telem.active_mode = 0;
    return BP_STATUS_OK;
}

void bp_protocol_heartbeat(void) {
    bp_proto_telem.heartbeat_ticks++;
#ifndef BP_HOST_TEST
    bp_proto_telem.last_heartbeat_ms = to_ms_since_boot(get_absolute_time());
#endif
}

void bp_protocol_service(uint32_t now_ms) {
    bp_proto_telem.last_heartbeat_ms = now_ms;
    bp_proto_telem.heartbeat_ticks++;
    bp_proto_telem.active_mode = system_config.mode;
}

bp_proto_state_t bp_protocol_get_state(void) {
    return (bp_proto_state_t)bp_proto_telem.state;
}

void bp_protocol_set_state(bp_proto_state_t state, uint32_t job_id) {
    bp_proto_telem.state = (uint8_t)state;
    bp_proto_telem.active_job_id = job_id;
    if (state == BP_PROTO_STATE_ACTIVE) {
        bp_proto_telem.transitions++;
    }
}

bool bp_protocol_is_healthy(uint32_t now_ms, uint32_t max_stall_ms) {
    if (bp_proto_telem.state != BP_PROTO_STATE_ACTIVE) {
        return true;
    }
    if (now_ms < bp_proto_telem.last_heartbeat_ms) {
        return true; // Clock wrap / safe
    }
    return (now_ms - bp_proto_telem.last_heartbeat_ms) <= max_stall_ms;
}

bp_status_t bp_protocol_recover(void) {
    uint32_t active = bp_job_get_active_id();
    if (active != 0) {
        bp_job_cancel(active);
        /* Retry after Core 1 acknowledges cancellation and finishes teardown. */
        return BP_STATUS_BUSY;
    }
    bp_proto_telem.state = BP_PROTO_STATE_FAULT;
    bp_proto_telem.faults++;

    // Cancel active job if any
    if (bp_proto_telem.active_job_id > 0) {
        bp_job_cancel(bp_proto_telem.active_job_id);
    }

    // Reclaim all resources owned by jobs and the active mode
    bp_res_release_all(BP_RES_OWNER_JOB);
    bp_res_release_all(BP_RES_OWNER_MODE_BASE + system_config.mode);
    bio_init();

    // Fall back to safe HiZ
    bp_status_t status = bp_mode_to_hiz();
    if (status != BP_STATUS_OK) return status;

    bp_proto_telem.state = BP_PROTO_STATE_IDLE;
    bp_proto_telem.active_job_id = 0;
    bp_proto_telem.recoveries++;
    return BP_STATUS_OK;
}

void bp_protocol_get_telemetry(bp_protocol_telemetry_t *out_telem) {
    if (out_telem) {
        *out_telem = bp_proto_telem;
    }
}
