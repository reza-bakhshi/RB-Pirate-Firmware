#include "kernel/bp_kernel.h"
#include "kernel/bp_resource.h"
#include "services/bp_terminal.h"
#include "services/bp_storage.h"
#include "services/bp_bin_transport.h"
#include "kernel/bp_protocol.h"
#include "kernel/bp_config.h"
#include "mode/bp_mode.h"
#include <stddef.h>

#if __has_include("pico/stdlib.h") && !defined(BP_HOST_TEST)
#include "pico/stdlib.h"
#define HAVE_PICO_SDK 1
#endif

static bp_event_t s_core0_event_buf[BP_CORE0_EVENT_QUEUE_SIZE];
static bp_event_t s_core1_event_buf[BP_CORE1_EVENT_QUEUE_SIZE];

static bp_event_queue_t s_core0_event_queue;
static bp_event_queue_t s_core1_event_queue;

static bp_work_queue_t  s_core0_work_queue;
static bp_work_queue_t  s_core1_work_queue;

static bp_event_handler_t s_core0_event_handler = NULL;
static bp_event_handler_t s_core1_event_handler = NULL;

static inline void bp_kernel_arch_sev(void) {
#if defined(__arm__) || defined(__thumb__)
    __asm volatile("sev" : : : "memory");
#endif
}

static inline void bp_kernel_arch_wfe(void) {
#if defined(__arm__) || defined(__thumb__)
    __asm volatile("wfe" : : : "memory");
#endif
}

bp_status_t bp_kernel_init(void) {
    bp_event_queue_init(&s_core0_event_queue, s_core0_event_buf, BP_CORE0_EVENT_QUEUE_SIZE);
    bp_event_queue_init(&s_core1_event_queue, s_core1_event_buf, BP_CORE1_EVENT_QUEUE_SIZE);

    bp_work_queue_init(&s_core0_work_queue);
    bp_work_queue_init(&s_core1_work_queue);

    bp_timer_subsystem_init();
    bp_op_subsystem_init();
    bp_log_subsystem_init();
    bp_health_subsystem_init();
    bp_job_subsystem_init();
    bp_res_init();
    bp_term_init();
    bp_storage_init();
    bp_config_subsystem_init();
    bp_mode_init();
    bp_protocol_init();

    s_core0_event_handler = NULL;
    s_core1_event_handler = NULL;

    return BP_STATUS_OK;
}

bp_status_t bp_kernel_post_event(uint8_t target_core, const bp_event_t* ev) {
    if (!ev) return BP_STATUS_INVALID_PARAM;

    bp_status_t st;
    if (target_core == 0) {
        st = bp_event_queue_post(&s_core0_event_queue, ev);
    } else {
        st = bp_event_queue_post(&s_core1_event_queue, ev);
    }

    if (st == BP_STATUS_OK) {
        bp_kernel_arch_sev();
    }
    return st;
}

void bp_kernel_set_event_handler(uint8_t core, bp_event_handler_t handler) {
    if (core == 0) {
        s_core0_event_handler = handler;
    } else {
        s_core1_event_handler = handler;
    }
}

bp_work_queue_t* bp_kernel_get_work_queue(uint8_t core) {
    return (core == 0) ? &s_core0_work_queue : &s_core1_work_queue;
}

bp_event_queue_t* bp_kernel_get_event_queue(uint8_t core) {
    return (core == 0) ? &s_core0_event_queue : &s_core1_event_queue;
}

uint32_t bp_service(uint8_t core) {
    uint32_t work_done = 0;

    if (core == 0) {
        bp_health_record_core0_tick();
        bp_job_finalize();

#ifdef HAVE_PICO_SDK
        static uint32_t s_last_timer_ms = 0;
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (s_last_timer_ms == 0) {
            s_last_timer_ms = now_ms;
        } else if (now_ms > s_last_timer_ms) {
            bp_timer_tick_ms(now_ms - s_last_timer_ms);
            s_last_timer_ms = now_ms;
        }
#else
        uint32_t now_ms = 0;
#endif

        // 1. Dispatch soft timers on Core 0
        uint32_t timers = bp_timer_dispatch();
        if (timers > 0) {
            work_done += timers;
            for (uint32_t i = 0; i < timers; i++) {
                bp_health_record_timer_fired();
            }
        }

        // 2. Dispatch deferred work for Core 0
        uint32_t work_items = bp_work_dispatch(&s_core0_work_queue);
        if (work_items > 0) {
            work_done += work_items;
            for (uint32_t i = 0; i < work_items; i++) {
                bp_health_record_work_executed();
            }
        }

        // 3. Process events for Core 0 (bounded to 8 per slice)
        bp_event_t ev;
        uint32_t ev_count = 0;
        while (ev_count < 8 && bp_event_queue_poll(&s_core0_event_queue, &ev) == BP_STATUS_OK) {
            if (s_core0_event_handler) {
                s_core0_event_handler(&ev);
            }
            bp_health_record_event_dispatched(0);
            ev_count++;
            work_done++;
        }

        // 4. Drain logs to console
        work_done += bp_log_drain_to_console();

        // 5. Service terminal I/O (drain Core 1 text ring, route input / Ctrl+C)
        bp_term_service();

        // 6. Service storage subsystem (mount state, debouncing, RPC, streaming)
        bp_storage_service();

        // 7. Service binary transport (CDC1 drainage, packets, drop accounting)
        bp_bin_transport_service();

        // 8. Service active mode periodic task
        bp_mode_periodic_service(now_ms);

        // 9. Publish UI snapshot
        bp_ui_snapshot_publish(now_ms, 0, 0, 0, 0);
    } else {
        bp_health_record_core1_tick();

        // 1. Dispatch deferred work for Core 1
        uint32_t work_items = bp_work_dispatch(&s_core1_work_queue);
        if (work_items > 0) {
            work_done += work_items;
            for (uint32_t i = 0; i < work_items; i++) {
                bp_health_record_work_executed();
            }
        }

        // 2. Process events for Core 1 (bounded to 8 per slice)
        bp_event_t ev;
        uint32_t ev_count = 0;
        while (ev_count < 8 && bp_event_queue_poll(&s_core1_event_queue, &ev) == BP_STATUS_OK) {
            if (s_core1_event_handler) {
                s_core1_event_handler(&ev);
            }
            bp_health_record_event_dispatched(1);
            ev_count++;
            work_done++;
        }

        // 3. Service active protocol job on Core 1
#ifdef HAVE_PICO_SDK
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
#else
        uint32_t now_ms = 0;
#endif
        work_done += bp_job_service(now_ms);

        // 4. Service protocol data plane heartbeat and telemetry
        bp_protocol_service(now_ms);
    }

    return work_done;
}

void bp_kernel_idle(uint8_t core) {
    (void)core;
    bp_kernel_arch_wfe();
}

void bp_kernel_run(uint8_t core, bool (*should_continue)(void)) {
    while (!should_continue || should_continue()) {
        uint32_t work = bp_service(core);
        if (work == 0) {
            bp_kernel_idle(core);
        }
    }
}
