#include "kernel/bp_job.h"
#include "kernel/bp_resource.h"
#include <string.h>

#if __has_include("pico/stdlib.h") && !defined(BP_HOST_TEST)
#define HAVE_PICO_SDK 1
#include "pico/stdlib.h"
#endif

static bp_job_t s_active_job;
static uint32_t s_next_job_id = 1;
static void (*s_input_reset)(void);
void bp_job_set_input_reset_hook(void (*reset)(void)) { s_input_reset = reset; }
static bp_job_state_t s_finish_state;
static bp_status_t s_finish_status;
static const char* s_finish_text;

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

bp_status_t bp_job_submit_with_cleanup(const char* name, uint8_t proto, uint32_t timeout_ms,
                          bp_job_slice_fn_t slice_fn, bp_job_cleanup_fn_t cleanup_fn, void* ctx,
                          uint32_t now_ms, uint32_t* out_job_id) {
    if (!slice_fn) return BP_STATUS_INVALID_PARAM;

    uint8_t state = __atomic_load_n(&s_active_job.state, __ATOMIC_ACQUIRE);
    if (state == BP_JOB_STATE_RUNNING || state == BP_JOB_STATE_QUEUED || state == BP_JOB_STATE_FINISHING) {
        return BP_STATUS_BUSY;
    }

    if (s_input_reset) s_input_reset();
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
    __atomic_store_n(&s_active_job.status, BP_STATUS_OK, __ATOMIC_RELAXED);
    __atomic_store_n(&s_active_job.percent, 0, __ATOMIC_RELAXED);
    s_active_job.start_time_ms = now_ms;
    s_active_job.timeout_ms = timeout_ms;
    s_active_job.slice_fn = slice_fn;
    s_active_job.ctx = ctx;
    s_active_job.cleanup_fn = cleanup_fn;
    __atomic_store_n(&s_active_job.cancel_requested, false, __ATOMIC_RELAXED);

    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&s_active_job.state, BP_JOB_STATE_QUEUED, __ATOMIC_RELEASE);

    if (out_job_id) {
        *out_job_id = jid;
    }

    return BP_STATUS_OK;
}

bp_status_t bp_job_submit(const char* name, uint8_t proto, uint32_t timeout_ms,
    bp_job_slice_fn_t slice_fn, void* ctx, uint32_t now_ms, uint32_t* out_job_id) {
    return bp_job_submit_with_cleanup(name, proto, timeout_ms, slice_fn, NULL, ctx, now_ms, out_job_id);
}

bp_status_t bp_job_dispatch_with_cleanup(const char* name, uint8_t proto, uint32_t timeout_ms,
    bp_job_slice_fn_t slice_fn, bp_job_cleanup_fn_t cleanup_fn, void* ctx, uint32_t* out_job_id) {
    uint32_t now_ms = 0;
#ifdef HAVE_PICO_SDK
    now_ms = to_ms_since_boot(get_absolute_time());
#endif
    return bp_job_submit_with_cleanup(name, proto, timeout_ms, slice_fn, cleanup_fn, ctx, now_ms, out_job_id);
}

bp_status_t bp_job_dispatch(const char* name, uint8_t proto, uint32_t timeout_ms,
    bp_job_slice_fn_t slice_fn, void* ctx, uint32_t* out_job_id) {
    return bp_job_dispatch_with_cleanup(name, proto, timeout_ms, slice_fn, NULL, ctx, out_job_id);
}

bp_status_t bp_job_cancel(uint32_t job_id) {
    uint8_t state = __atomic_load_n(&s_active_job.state, __ATOMIC_ACQUIRE);
    if (s_active_job.id == job_id &&
        (state == BP_JOB_STATE_QUEUED || state == BP_JOB_STATE_RUNNING)) {
        __atomic_thread_fence(__ATOMIC_RELEASE);
        __atomic_store_n(&s_active_job.cancel_requested, true, __ATOMIC_RELEASE);
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
    if (out_state)   *out_state = (bp_job_state_t)__atomic_load_n(&s_active_job.state, __ATOMIC_ACQUIRE);
    if (out_status)  *out_status = (bp_status_t)__atomic_load_n(&s_active_job.status, __ATOMIC_RELAXED);
    if (out_percent) *out_percent = __atomic_load_n(&s_active_job.percent, __ATOMIC_RELAXED);

    return BP_STATUS_OK;
}

bp_status_t bp_job_emit_progress(uint8_t percent, const char* text) {
    uint32_t head = __atomic_load_n(&s_output_head, __ATOMIC_ACQUIRE);
    uint32_t tail = __atomic_load_n(&s_output_tail, __ATOMIC_ACQUIRE);

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
    __atomic_store_n(&s_output_head, head + 1, __ATOMIC_RELEASE);
    return BP_STATUS_OK;
}

bp_status_t bp_job_poll_output(bp_job_output_t* out_rec) {
    if (!out_rec) return BP_STATUS_INVALID_PARAM;

    uint32_t tail = __atomic_load_n(&s_output_tail, __ATOMIC_ACQUIRE);
    uint32_t head = __atomic_load_n(&s_output_head, __ATOMIC_ACQUIRE);

    if (tail == head) {
        return BP_STATUS_QUEUE_EMPTY;
    }

    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    *out_rec = s_output_ring[tail & (BP_JOB_OUTPUT_RING_SIZE - 1)];
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&s_output_tail, tail + 1, __ATOMIC_RELEASE);

    return BP_STATUS_OK;
}

bool bp_job_check_cancelled(void) {
    uint8_t state = __atomic_load_n(&s_active_job.state, __ATOMIC_ACQUIRE);
    if (state != BP_JOB_STATE_QUEUED && state != BP_JOB_STATE_RUNNING && state != BP_JOB_STATE_FINISHING) return false;
    return __atomic_load_n(&s_active_job.cancel_requested, __ATOMIC_ACQUIRE);
}

uint32_t bp_job_get_active_id(void) {
    uint8_t state = __atomic_load_n(&s_active_job.state, __ATOMIC_ACQUIRE);
    if (state == BP_JOB_STATE_RUNNING || state == BP_JOB_STATE_QUEUED || state == BP_JOB_STATE_FINISHING) {
        return s_active_job.id;
    }
    return 0;
}

static void bp_job_publish_finish(bp_job_state_t state, bp_status_t status, const char* text) {
    bp_res_release_all(BP_RES_OWNER_JOB);
    __atomic_store_n(&s_active_job.status, status, __ATOMIC_RELAXED);
    if (state == BP_JOB_STATE_DONE) __atomic_store_n(&s_active_job.percent, 100, __ATOMIC_RELAXED);
    bp_job_emit_progress(__atomic_load_n(&s_active_job.percent, __ATOMIC_RELAXED), text);
    __atomic_store_n(&s_active_job.state, state, __ATOMIC_RELEASE);
}

static void bp_job_finish(bp_job_state_t state, bp_status_t status, const char* text) {
    if (s_active_job.cleanup_fn) {
        s_finish_state = state;
        s_finish_status = status;
        s_finish_text = text;
        /* Core 0 installed the peripheral IRQs. Its NVIC must be disabled
         * there before vectors/programs/buffers are reclaimed. */
        __atomic_store_n(&s_active_job.state, BP_JOB_STATE_FINISHING, __ATOMIC_RELEASE);
    } else {
        bp_job_publish_finish(state, status, text);
    }
}

void bp_job_finalize(void) {
    if (__atomic_load_n(&s_active_job.state, __ATOMIC_ACQUIRE) != BP_JOB_STATE_FINISHING) return;
    s_active_job.cleanup_fn(s_active_job.ctx, s_finish_status);
    s_active_job.cleanup_fn = NULL;
    bp_job_publish_finish(s_finish_state, s_finish_status, s_finish_text);
}

uint32_t bp_job_service(uint32_t now_ms) {
    uint8_t state = __atomic_load_n(&s_active_job.state, __ATOMIC_ACQUIRE);
    if (state != BP_JOB_STATE_QUEUED && state != BP_JOB_STATE_RUNNING) return 0;
    if (bp_job_check_cancelled()) {
        bp_job_finish(BP_JOB_STATE_CANCELLED, BP_STATUS_CANCELLED, "Cancelled");
        return 1;
    }
    if (state == BP_JOB_STATE_QUEUED) {
        __atomic_store_n(&s_active_job.state, BP_JOB_STATE_RUNNING, __ATOMIC_RELEASE);
        if (s_active_job.start_time_ms == 0) s_active_job.start_time_ms = now_ms;
    }
    if (s_active_job.timeout_ms > 0 && now_ms - s_active_job.start_time_ms >= s_active_job.timeout_ms) {
        bp_job_finish(BP_JOB_STATE_TIMEOUT, BP_STATUS_TIMEOUT, "Timeout");
        return 1;
    }
    uint8_t pct = __atomic_load_n(&s_active_job.percent, __ATOMIC_RELAXED);
    bp_status_t st = s_active_job.slice_fn(s_active_job.ctx, &pct);
    __atomic_store_n(&s_active_job.percent, pct, __ATOMIC_RELAXED);
    if (st == BP_STATUS_OK) bp_job_finish(BP_JOB_STATE_DONE, st, "Done");
    else if (st == BP_STATUS_CANCELLED) bp_job_finish(BP_JOB_STATE_CANCELLED, st, "Cancelled");
    else if (st != BP_STATUS_BUSY) bp_job_finish(BP_JOB_STATE_ERROR, st, "Error");
    return 1;
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
        case BP_JOB_STATE_FINISHING: return "FINISHING";
        default:                     return "UNKNOWN";
    }
}
