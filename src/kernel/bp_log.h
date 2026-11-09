#ifndef BP_LOG_H
#define BP_LOG_H

#include <stdint.h>
#include <stdbool.h>
#include "kernel/bp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bp_log_level {
    BP_LOG_DEBUG   = 0,
    BP_LOG_INFO    = 1,
    BP_LOG_WARN    = 2,
    BP_LOG_ERROR   = 3,
} bp_log_level_t;

#define BP_LOG_MSG_MAX 64
#define BP_LOG_TAG_MAX 8
#define BP_LOG_RING_CAPACITY 32

typedef struct bp_log_entry {
    uint32_t timestamp_ms;
    uint8_t  level;
    uint8_t  core;
    char     tag[BP_LOG_TAG_MAX];
    char     msg[BP_LOG_MSG_MAX];
} bp_log_entry_t;

typedef struct bp_log_ring {
    bp_log_entry_t entries[BP_LOG_RING_CAPACITY];
    volatile uint32_t head;
    volatile uint32_t tail;
    uint32_t drops;
} bp_log_ring_t;

/**
 * @brief Initialize the leveled log subsystem.
 */
bp_status_t bp_log_subsystem_init(void);

/**
 * @brief Post a log entry to the ring buffer. Safe for Core 1 and ISR producers.
 */
bp_status_t bp_log_post(bp_log_level_t level, const char* tag, const char* msg, uint8_t core, uint32_t now_ms);

/**
 * @brief Pop the oldest log entry from the ring buffer.
 */
bp_status_t bp_log_pop(bp_log_entry_t* entry);

/**
 * @brief Drain pending log entries to the terminal console (called by Core 0).
 * @return Number of entries drained and printed.
 */
uint32_t bp_log_drain_to_console(void);

/**
 * @brief Pending log entries in the ring buffer.
 */
uint32_t bp_log_count(void);

/**
 * @brief Total dropped log entries due to ring full.
 */
uint32_t bp_log_drops(void);

/**
 * @brief Get string representation of log level.
 */
const char* bp_log_level_str(bp_log_level_t level);

#ifdef __cplusplus
}
#endif

#endif // BP_LOG_H
