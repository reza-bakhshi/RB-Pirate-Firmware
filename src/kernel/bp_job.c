#include "kernel/bp_job.h"
#include "kernel/bp_resource.h"
#include <string.h>

#ifdef HAVE_PICO_SDK
#include "pico/stdlib.h"
#endif

static bp_job_t s_active_job;
static uint32_t s_next_job_id = 1;

static bp_job_output_t s_output_ring[BP_JOB_OUTPUT_RING_SIZE];
static volatile uint32_t s_output_head = 0;
static volatile uint32_t s_output_tail = 0;

bp_status_t bp_job_subsystem_init(void) {
    bp_res_release_all(BP_RES_OWNER_JOB);
    memset(&s_active_job, 0, sizeof(s_active_job));
    s_active_job.state = BP_JOB_STATE_IDLE;
    s_output_head = 0;
    s_output_tail = 0;
    memset(s_output_ring, 0, sizeof(s_output_ring));
    return BP_STATUS_OK;
}

bp_status_t bp_job_submit(const char* name, uint8_t proto, uint32_t timeout_ms,
                          bp_job_slice_fn_t slice_fn, void* ctx,
                          uint32_t now_ms, uint32_t* out_job_id) {
    if (!slice_fn) return BP_STATUS_INVALID_PARAM;

    if (s_active_job.state == BP_JOB_STATE_RUNNING ||
        s_active_job.state == BP_JOB_STATE_QUEUED) {
        return BP_STATUS_BUSY;
    }

    uint32_t jid = s_next_job_id++;
    if (s_next_job_id == 0) s_next_job_id = 1;

    s_active_job.id = jid;
    if (name) {
        strncpy(s_active_job.name, name, BP_JOB_NAME_MAX - 1);
        s_active_job.name[BP_JOB_NAME_MAX - 1] = '\0';
    } else {
        strncpy(s_active_job.name, "protocol_job", BP_JOB_NAME_MAX - 1);
    }

    s_active_job.proto = proto;
    s_active_job.status = BP_STATUS_OK;
    s_active_job.percent = 0;
    s_active_job.start_time_ms = now_ms;
    s_active_job.timeout_ms = timeout_ms;
    s_active_job.slice_fn = slice_fn;
    s_active_job.ctx = ctx;
    s_active_job.cancel_requested = false;

    __atomic_thread_fence(__ATOMIC_RELEASE);
    s_active_job.state = BP_JOB_STATE_QUEUED;

    if (out_job_id) {
        *out_job_id = jid;
    }

    return BP_STATUS_OK;
}

bp_status_t bp_job_dispatch(const char* name, uint8_t proto, uint32_t timeout_ms,
                            bp_job_slice_fn_t slice_fn, void* ctx,
                            uint32_t* out_job_id) {
    uint32_t now_ms = 0;
#ifdef HAVE_PICO_SDK
    now_ms = to_ms_since_boot(get_absolute_time());
#endif
    return bp_job_submit(name, proto, timeout_ms, slice_fn, ctx, now_ms, out_job_id);
}

bp_status_t bp_job_cancel(uint32_t job_id) {
    if (s_active_job.id == job_id &&
        (s_active_job.state == BP_JOB_STATE_QUEUED || s_active_job.state == BP_JOB_STATE_RUNNING)) {
        __atomic_thread_fence(__ATOMIC_RELEASE);
        s_active_job.cancel_requested = true;
        return BP_STATUS_OK;
    }
    return BP_STATUS_INVALID_PARAM;
}

bp_status_t bp_job_get_state(uint32_t job_id, bp_job_state_t* out_state,
                             bp_status_t* out_status, uint8_t* out_percent) {
    if (s_active_job.id != job_id && job_id != 0) {
        return BP_STATUS_INVALID_PARAM;
    }

    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (out_state)   *out_state = (bp_job_state_t)s_active_job.state;
    if (out_status)  *out_status = (bp_status_t)s_active_job.status;
    if (out_percent) *out_percent = s_active_job.percent;

    return BP_STATUS_OK;
}

bp_status_t bp_job_emit_progress(uint8_t percent, const char* text) {
    uint32_t head = s_output_head;
    uint32_t tail = s_output_tail;

    if ((head - tail) >= BP_JOB_OUTPUT_RING_SIZE) {
        return BP_STATUS_QUEUE_FULL;
    }

    bp_job_output_t* out = &s_output_ring[head & (BP_JOB_OUTPUT_RING_SIZE - 1)];
    out->job_id = s_active_job.id;
    out->percent = percent;
    if (text) {
        strncpy(out->text, text, BP_JOB_OUTPUT_TEXT_MAX - 1);
        out->text[BP_JOB_OUTPUT_TEXT_MAX - 1] = '\0';
    } else {
        out->text[0] = '\0';
    }

    __atomic_thread_fence(__ATOMIC_RELEASE);
    s_output_head = head + 1;
    return BP_STATUS_OK;
}

bp_status_t bp_job_poll_output(bp_job_output_t* out_rec) {
    if (!out_rec) return BP_STATUS_INVALID_PARAM;

    uint32_t tail = s_output_tail;
    uint32_t head = s_output_head;

    if (tail == head) {
        return BP_STATUS_QUEUE_EMPTY;
    }

    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    *out_rec = s_output_ring[tail & (BP_JOB_OUTPUT_RING_SIZE - 1)];
    __atomic_thread_fence(__ATOMIC_RELEASE);
    s_output_tail = tail + 1;

    return BP_STATUS_OK;
}

bool bp_job_check_cancelled(void) {
    return s_active_job.cancel_requested;
}

uint32_t bp_job_get_active_id(void) {
    if (s_active_job.state == BP_JOB_STATE_RUNNING ||
        s_active_job.state == BP_JOB_STATE_QUEUED) {
        return s_active_job.id;
    }
    return 0;
}

uint32_t bp_job_service(uint32_t now_ms) {
    uint8_t state = s_active_job.state;

    if (state == BP_JOB_STATE_QUEUED) {
        if (s_active_job.cancel_requested) {
            s_active_job.state = BP_JOB_STATE_CANCELLED;
            s_active_job.status = BP_STATUS_CANCELLED;
            bp_job_emit_progress(s_active_job.percent, "Cancelled");
            bp_res_release_all(BP_RES_OWNER_JOB);
            return 1;
        }
        s_active_job.state = BP_JOB_STATE_RUNNING;
        if (s_active_job.start_time_ms == 0) {
            s_active_job.start_time_ms = now_ms;
        }
        state = BP_JOB_STATE_RUNNING;
    }

    if (state == BP_JOB_STATE_RUNNING) {
        // Check cancellation
        if (s_active_job.cancel_requested) {
            s_active_job.state = BP_JOB_STATE_CANCELLED;
            s_active_job.status = BP_STATUS_CANCELLED;
            bp_job_emit_progress(s_active_job.percent, "Cancelled");
            bp_res_release_all(BP_RES_OWNER_JOB);
            return 1;
        }

        // Check deadline
        if (s_active_job.timeout_ms > 0 && (now_ms - s_active_job.start_time_ms >= s_active_job.timeout_ms)) {
            s_active_job.state = BP_JOB_STATE_TIMEOUT;
            s_active_job.status = BP_STATUS_TIMEOUT;
            bp_job_emit_progress(s_active_job.percent, "Timeout");
            bp_res_release_all(BP_RES_OWNER_JOB);
            return 1;
        }

        // Execute slice
        uint8_t pct = s_active_job.percent;
        bp_status_t st = s_active_job.slice_fn(s_active_job.ctx, &pct);
        s_active_job.percent = pct;

        if (st == BP_STATUS_OK) {
            s_active_job.state = BP_JOB_STATE_DONE;
            s_active_job.status = BP_STATUS_OK;
            s_active_job.percent = 100;
            bp_job_emit_progress(100, "Done");
            bp_res_release_all(BP_RES_OWNER_JOB);
        } else if (st == BP_STATUS_CANCELLED) {
            s_active_job.state = BP_JOB_STATE_CANCELLED;
            s_active_job.status = BP_STATUS_CANCELLED;
            bp_job_emit_progress(s_active_job.percent, "Cancelled");
            bp_res_release_all(BP_RES_OWNER_JOB);
        } else if (st != BP_STATUS_BUSY) {
            s_active_job.state = BP_JOB_STATE_ERROR;
            s_active_job.status = st;
            bp_job_emit_progress(s_active_job.percent, "Error");
            bp_res_release_all(BP_RES_OWNER_JOB);
        }

        return 1;
    }

    return 0;
}

const char* bp_job_state_str(bp_job_state_t state) {
    switch (state) {
        case BP_JOB_STATE_IDLE:      return "IDLE";
        case BP_JOB_STATE_QUEUED:    return "QUEUED";
        case BP_JOB_STATE_RUNNING:   return "RUNNING";
        case BP_JOB_STATE_DONE:      return "DONE";
        case BP_JOB_STATE_CANCELLED: return "CANCELLED";
        case BP_JOB_STATE_TIMEOUT:   return "TIMEOUT";
        case BP_JOB_STATE_ERROR:     return "ERROR";
        default:                     return "UNKNOWN";
    }
}
