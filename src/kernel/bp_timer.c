#include "kernel/bp_timer.h"
#include <stddef.h>

static bp_timer_t s_timer_pool[BP_MAX_SOFT_TIMERS];
static uint32_t s_total_expirations = 0;

bp_status_t bp_timer_subsystem_init(void) {
    for (uint32_t i = 0; i < BP_MAX_SOFT_TIMERS; i++) {
        s_timer_pool[i].callback = NULL;
        s_timer_pool[i].user_data = NULL;
        s_timer_pool[i].interval_ms = 0;
        s_timer_pool[i].remaining_ms = 0;
        s_timer_pool[i].periodic = false;
        s_timer_pool[i].active = false;
        s_timer_pool[i].expired = false;
        s_timer_pool[i].in_use = false;
    }
    s_total_expirations = 0;
    return BP_STATUS_OK;
}

bp_timer_t* bp_timer_create(bp_timer_cb_t cb, void* user_data, bool periodic) {
    if (!cb) return NULL;

    for (uint32_t i = 0; i < BP_MAX_SOFT_TIMERS; i++) {
        if (!s_timer_pool[i].in_use) {
            s_timer_pool[i].callback = cb;
            s_timer_pool[i].user_data = user_data;
            s_timer_pool[i].periodic = periodic;
            s_timer_pool[i].interval_ms = 0;
            s_timer_pool[i].remaining_ms = 0;
            s_timer_pool[i].active = false;
            s_timer_pool[i].expired = false;
            s_timer_pool[i].in_use = true;
            return &s_timer_pool[i];
        }
    }
    return NULL;
}

bp_status_t bp_timer_start(bp_timer_t* timer, uint32_t interval_ms) {
    if (!timer || !timer->in_use || interval_ms == 0) {
        return BP_STATUS_INVALID_PARAM;
    }
    timer->interval_ms = interval_ms;
    timer->remaining_ms = interval_ms;
    timer->expired = false;
    timer->active = true;
    return BP_STATUS_OK;
}

bp_status_t bp_timer_stop(bp_timer_t* timer) {
    if (!timer || !timer->in_use) {
        return BP_STATUS_INVALID_PARAM;
    }
    timer->active = false;
    timer->expired = false;
    return BP_STATUS_OK;
}

bp_status_t bp_timer_destroy(bp_timer_t* timer) {
    if (!timer || !timer->in_use) {
        return BP_STATUS_INVALID_PARAM;
    }
    timer->active = false;
    timer->expired = false;
    timer->callback = NULL;
    timer->user_data = NULL;
    timer->in_use = false;
    return BP_STATUS_OK;
}

void bp_timer_tick_1ms(void) {
    bp_timer_tick_ms(1);
}

void bp_timer_tick_ms(uint32_t delta_ms) {
    if (delta_ms == 0) return;

    for (uint32_t i = 0; i < BP_MAX_SOFT_TIMERS; i++) {
        bp_timer_t* t = &s_timer_pool[i];
        if (t->in_use && t->active && !t->expired) {
            if (t->remaining_ms <= delta_ms) {
                t->remaining_ms = 0;
                t->expired = true;
                s_total_expirations++;
            } else {
                t->remaining_ms -= delta_ms;
            }
        }
    }
}

uint32_t bp_timer_dispatch(void) {
    uint32_t dispatched = 0;

    for (uint32_t i = 0; i < BP_MAX_SOFT_TIMERS; i++) {
        bp_timer_t* t = &s_timer_pool[i];
        if (t->in_use && t->active && t->expired) {
            t->expired = false;
            if (t->periodic) {
                t->remaining_ms = t->interval_ms;
            } else {
                t->active = false;
            }

            if (t->callback) {
                t->callback(t->user_data);
                dispatched++;
            }
        }
    }

    return dispatched;
}

uint32_t bp_timer_active_count(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < BP_MAX_SOFT_TIMERS; i++) {
        if (s_timer_pool[i].in_use && s_timer_pool[i].active) {
            count++;
        }
    }
    return count;
}

uint32_t bp_timer_total_expirations(void) {
    return s_total_expirations;
}
