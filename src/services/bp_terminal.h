/**
 * @file bp_terminal.h
 * @brief Terminal service interface for Bus Pirate (Core 0 owned).
 *
 * Enforces Core 0 ownership of terminal I/O, isolates Core 1 console output
 * through a lock-free SPSC text ring, and demultiplexes terminal input between
 * shell prompt, job input, and Ctrl+C cancellation.
 */

#ifndef BP_TERMINAL_H
#define BP_TERMINAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "kernel/bp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BP_TERM_CORE1_TEXT_RING_SIZE 2048
#define BP_TERM_JOB_INPUT_RING_SIZE  256

/**
 * @brief Terminal service metrics and counters.
 */
typedef struct bp_term_stats {
    uint32_t tx_level;
    uint32_t tx_free;
    uint32_t tx_dropped;
    uint32_t tx_overflow_events;
    uint32_t core1_text_level;
    uint32_t core1_text_dropped;
    uint32_t job_input_level;
} bp_term_stats_t;

/**
 * @brief Initialize terminal service queues and state.
 */
bp_status_t bp_term_init(void);

/**
 * @brief Non-blocking write of a buffer to the terminal output queue.
 *
 * @param buf Character buffer.
 * @param len Buffer length in bytes.
 * @return BP_STATUS_OK on success, or BP_STATUS_QUEUE_FULL on dropped bytes.
 */
bp_status_t bp_term_write(const char* buf, uint32_t len);

/**
 * @brief Non-blocking write of a single character from Core 0.
 *
 * @param c Character to write.
 * @return BP_STATUS_OK on success, BP_STATUS_QUEUE_FULL if dropped.
 */
bp_status_t bp_term_putc(char c);

/**
 * @brief Non-blocking read of a single character from Core 0 input queue.
 *
 * @param c Output character pointer.
 * @return BP_STATUS_OK if character read, BP_STATUS_QUEUE_EMPTY if empty.
 */
bp_status_t bp_term_getc(char* c);

/**
 * @brief Peek at the next input character without removing it.
 */
bp_status_t bp_term_peekc(char* c);

/**
 * @brief Write a character from Core 1 into the Core 1 text ring.
 *
 * Safe and lock-free across cores and ISRs.
 *
 * @param c Character to output.
 * @return BP_STATUS_OK on success, BP_STATUS_QUEUE_FULL if ring is full.
 */
bp_status_t bp_term_core1_putc(char c);

/**
 * @brief Write a null-terminated string from Core 1 into the Core 1 text ring.
 */
static inline bp_status_t bp_term_core1_puts(const char* s) {
    if (!s) return BP_STATUS_INVALID_PARAM;
    while (*s) {
        bp_status_t st = bp_term_core1_putc(*s++);
        if (st != BP_STATUS_OK) return st;
    }
    return BP_STATUS_OK;
}

/**
 * @brief Read characters from the Core 1 job input ring (called by Core 1 jobs).
 *
 * @param buf Destination buffer.
 * @param max_len Maximum bytes to read.
 * @param out_len Pointer to store count of bytes read.
 * @return BP_STATUS_OK on success, BP_STATUS_QUEUE_EMPTY if no input is queued.
 */
bp_status_t bp_term_job_read(char* buf, uint32_t max_len, uint32_t* out_len);

/**
 * @brief Read a single character from the Core 1 job input ring.
 */
bp_status_t bp_term_job_getc(char* c);

/**
 * @brief Service the terminal subsystem on Core 0.
 *
 * Must be called periodically from Core 0 cooperative loop (`bp_service(0)`).
 * Drains the Core 1 text ring into TX, processes inbound characters, intercepts
 * Ctrl+C (0x03) for active jobs, and routes job input.
 */
void bp_term_service(void);

/**
 * @brief Wait for the terminal TX buffer to drain with a timeout.
 *
 * @param timeout_ms Maximum time to wait in milliseconds.
 * @return true if buffer drained empty, false on timeout.
 */
bool bp_term_wait_drain(uint32_t timeout_ms);

/**
 * @brief Retrieve terminal statistics and drop counters.
 */
void bp_term_get_stats(bp_term_stats_t* stats);

#ifdef __cplusplus
}
#endif

#endif /* BP_TERMINAL_H */
