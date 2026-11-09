#ifndef BP_EVENT_H
#define BP_EVENT_H

#include <stdint.h>
#include <stdbool.h>
#include "kernel/bp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Standard kernel event IDs.
 */
typedef enum bp_event_id {
    BP_EV_NONE               = 0,
    BP_EV_TICK_1MS           = 1,
    BP_EV_BUTTON_PRESS       = 2,
    BP_EV_BUTTON_RELEASE     = 3,
    BP_EV_USB_CONNECT        = 4,
    BP_EV_USB_DISCONNECT     = 5,
    BP_EV_STORAGE_MOUNT      = 6,
    BP_EV_STORAGE_UNMOUNT    = 7,
    BP_EV_CANCEL_REQUEST     = 8,
    BP_EV_MODE_CHANGED       = 9,
    BP_EV_PSU_FAULT          = 10,
    BP_EV_TERMINAL_INPUT     = 11,
    BP_EV_CUSTOM_START       = 0x1000,
} bp_event_id_t;

/**
 * @brief Fixed 16-byte event envelope.
 */
typedef struct bp_event {
    uint16_t id;
    uint16_t source;         // Core ID (0, 1) or ISR flag
    uint32_t timestamp_ms;   // Millisecond timestamp
    union {
        uint32_t u32[2];
        int32_t  i32[2];
        void*    ptr;
    } data;
} bp_event_t;

/**
 * @brief Fixed-size SPSC circular event queue.
 */
typedef struct bp_event_queue {
    bp_event_t* buffer;
    uint32_t capacity;       // Must be power of 2
    uint32_t mask;
    volatile uint32_t head;  // Producer write index
    volatile uint32_t tail;  // Consumer read index
    uint32_t drops;          // Overrun drop counter
    uint32_t high_water;     // Max occupancy observed
} bp_event_queue_t;

/**
 * @brief Initialize an event queue using caller-provided static storage.
 * @param q Pointer to queue struct.
 * @param buffer Static array of bp_event_t.
 * @param capacity Number of elements in buffer (must be power of two).
 * @return BP_STATUS_OK on success, BP_STATUS_INVALID_PARAM on invalid size.
 */
bp_status_t bp_event_queue_init(bp_event_queue_t* q, bp_event_t* buffer, uint32_t capacity);

/**
 * @brief Post an event to the queue (non-blocking).
 * @return BP_STATUS_OK, or BP_STATUS_QUEUE_FULL if full.
 */
bp_status_t bp_event_queue_post(bp_event_queue_t* q, const bp_event_t* ev);

/**
 * @brief Poll an event from the queue (non-blocking).
 * @param ev Pointer to store retrieved event.
 * @return BP_STATUS_OK if retrieved, BP_STATUS_QUEUE_EMPTY if empty.
 */
bp_status_t bp_event_queue_poll(bp_event_queue_t* q, bp_event_t* ev);

/**
 * @brief Current count of items in the queue.
 */
uint32_t bp_event_queue_count(const bp_event_queue_t* q);

/**
 * @brief Check if the queue is empty.
 */
static inline bool bp_event_queue_is_empty(const bp_event_queue_t* q) {
    return q->head == q->tail;
}

/**
 * @brief Check if the queue is full.
 */
static inline bool bp_event_queue_is_full(const bp_event_queue_t* q) {
    return (q->head - q->tail) >= q->capacity;
}

/**
 * @brief Get total dropped events for this queue.
 */
static inline uint32_t bp_event_queue_drops(const bp_event_queue_t* q) {
    return q->drops;
}

/**
 * @brief Get high-water mark of this queue.
 */
static inline uint32_t bp_event_queue_high_water(const bp_event_queue_t* q) {
    return q->high_water;
}

#ifdef __cplusplus
}
#endif

#endif // BP_EVENT_H
