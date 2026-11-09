#include "kernel/bp_health.h"
#include <string.h>

static bp_kernel_metrics_t s_metrics;

bp_status_t bp_health_subsystem_init(void) {
    memset(&s_metrics, 0, sizeof(s_metrics));
    return BP_STATUS_OK;
}

void bp_health_record_core0_tick(void) {
    s_metrics.core0_ticks++;
}

void bp_health_record_core1_tick(void) {
    s_metrics.core1_ticks++;
}

void bp_health_record_event_dispatched(uint8_t core) {
    if (core == 0) {
        s_metrics.core0_events_dispatched++;
    } else {
        s_metrics.core1_events_dispatched++;
    }
}

void bp_health_record_timer_fired(void) {
    s_metrics.timers_fired++;
}

void bp_health_record_work_executed(void) {
    s_metrics.work_items_executed++;
}

void bp_health_record_op_cancelled(void) {
    s_metrics.ops_cancelled++;
}

void bp_health_get_metrics(bp_kernel_metrics_t* metrics) {
    if (metrics) {
        *metrics = s_metrics;
    }
}
