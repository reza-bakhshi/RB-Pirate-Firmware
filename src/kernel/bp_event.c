#include "kernel/bp_event.h"

bp_status_t bp_event_queue_init(bp_event_queue_t* q, bp_event_t* buffer, uint32_t capacity) {
    if (!q || !buffer || capacity == 0 || (capacity & (capacity - 1)) != 0) {
        return BP_STATUS_INVALID_PARAM;
    }
    q->buffer = buffer;
    q->capacity = capacity;
    q->mask = capacity - 1;
    q->head = 0;
    q->tail = 0;
    q->drops = 0;
    q->high_water = 0;
    return BP_STATUS_OK;
}

bp_status_t bp_event_queue_post(bp_event_queue_t* q, const bp_event_t* ev) {
    if (!q || !ev) {
        return BP_STATUS_INVALID_PARAM;
    }

    uint32_t head = q->head;
    uint32_t tail = q->tail;
    uint32_t count = (head - tail);

    if (count >= q->capacity) {
        q->drops++;
        return BP_STATUS_QUEUE_FULL;
    }

    q->buffer[head & q->mask] = *ev;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    q->head = head + 1;

    count++;
    if (count > q->high_water) {
        q->high_water = count;
    }

    return BP_STATUS_OK;
}

bp_status_t bp_event_queue_poll(bp_event_queue_t* q, bp_event_t* ev) {
    if (!q || !ev) {
        return BP_STATUS_INVALID_PARAM;
    }

    uint32_t tail = q->tail;
    uint32_t head = q->head;

    if (tail == head) {
        return BP_STATUS_QUEUE_EMPTY;
    }

    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    *ev = q->buffer[tail & q->mask];
    __atomic_thread_fence(__ATOMIC_RELEASE);
    q->tail = tail + 1;

    return BP_STATUS_OK;
}

uint32_t bp_event_queue_count(const bp_event_queue_t* q) {
    if (!q) return 0;
    uint32_t head = q->head;
    uint32_t tail = q->tail;
    return (head - tail);
}
