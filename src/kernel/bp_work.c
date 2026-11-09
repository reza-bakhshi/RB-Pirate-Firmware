#include "kernel/bp_work.h"
#include <stddef.h>

bp_status_t bp_work_queue_init(bp_work_queue_t* wq) {
    if (!wq) return BP_STATUS_INVALID_PARAM;
    for (uint32_t i = 0; i < BP_MAX_WORK_ITEMS; i++) {
        wq->items[i].handler = NULL;
        wq->items[i].arg = NULL;
        wq->items[i].pending = false;
        wq->items[i].in_use = false;
    }
    wq->executed_count = 0;
    return BP_STATUS_OK;
}

bp_work_item_t* bp_work_item_create(bp_work_queue_t* wq, bp_work_handler_t handler, void* arg) {
    if (!wq || !handler) return NULL;

    for (uint32_t i = 0; i < BP_MAX_WORK_ITEMS; i++) {
        if (!wq->items[i].in_use) {
            wq->items[i].handler = handler;
            wq->items[i].arg = arg;
            wq->items[i].pending = false;
            wq->items[i].in_use = true;
            return &wq->items[i];
        }
    }
    return NULL;
}

bp_status_t bp_work_schedule(bp_work_item_t* item) {
    if (!item || !item->in_use) {
        return BP_STATUS_INVALID_PARAM;
    }
    item->pending = true;
    return BP_STATUS_OK;
}

bp_status_t bp_work_cancel(bp_work_item_t* item) {
    if (!item || !item->in_use) {
        return BP_STATUS_INVALID_PARAM;
    }
    item->pending = false;
    return BP_STATUS_OK;
}

bp_status_t bp_work_item_destroy(bp_work_item_t* item) {
    if (!item || !item->in_use) {
        return BP_STATUS_INVALID_PARAM;
    }
    item->pending = false;
    item->handler = NULL;
    item->arg = NULL;
    item->in_use = false;
    return BP_STATUS_OK;
}

uint32_t bp_work_dispatch(bp_work_queue_t* wq) {
    if (!wq) return 0;
    uint32_t executed = 0;

    for (uint32_t i = 0; i < BP_MAX_WORK_ITEMS; i++) {
        bp_work_item_t* item = &wq->items[i];
        if (item->in_use && item->pending) {
            item->pending = false;
            if (item->handler) {
                item->handler(item->arg);
                executed++;
                wq->executed_count++;
            }
        }
    }
    return executed;
}

uint32_t bp_work_pending_count(const bp_work_queue_t* wq) {
    if (!wq) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < BP_MAX_WORK_ITEMS; i++) {
        if (wq->items[i].in_use && wq->items[i].pending) {
            count++;
        }
    }
    return count;
}
