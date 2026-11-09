/**
 * @file usb_tx.h
 * @brief USB transmit queue management.
 * @details Provides USB output queue handling for normal and binary modes.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Result of a terminal output operation. */
typedef enum {
    BP_TX_OK = 0,
    BP_TX_DROPPED,
    BP_TX_TIMEOUT,
} bp_tx_status_t;

/** Terminal output health counters. */
typedef struct {
    uint32_t dropped_bytes;
    uint32_t overflow_events;
    uint32_t high_water;
    uint32_t drain_timeouts;
} bp_terminal_health_t;

extern bp_terminal_health_t bp_terminal_health;

/**
 * @brief Initialize transmit FIFO.
 */
void tx_fifo_init(void);

/**
 * @brief Service transmit FIFO.
 */
void tx_fifo_service(void);

/**
 * @brief Put one character in the transmit FIFO without blocking.
 * @param c  Character to transmit
 * @return BP_TX_OK, or BP_TX_DROPPED if the FIFO is full
 */
bp_tx_status_t tx_fifo_put(char* c);

/**
 * @brief Non-blocking alias for tx_fifo_put().
 * @param c  Character to transmit
 * @return BP_TX_OK, or BP_TX_DROPPED if the FIFO is full
 */
bp_tx_status_t tx_fifo_try_put(char* c);

/**
 * @brief Write a pre-built buffer into the transmit FIFO without blocking.
 * @param buf  Buffer to send (must not be NULL).
 * @param len  Number of bytes to send.
 * @return BP_TX_OK, or BP_TX_DROPPED if any byte was dropped.
 * @pre Must be called from Core0.
 */
bp_tx_status_t tx_fifo_write(const char* buf, uint32_t len);

/**
 * @brief Wait until transmit FIFO is fully drained, with a timeout.
 * @param timeout_ms  Maximum time to wait in milliseconds.
 * @return true if drained, false on timeout.
 * @pre Must be called from Core0.
 */
bool tx_fifo_wait_drain_timeout(uint32_t timeout_ms);

/**
 * @brief Wait until transmit FIFO is drained, bounded by a default timeout.
 * @pre Must be called from Core0.
 */
void tx_fifo_wait_drain(void);

/**
 * @brief Current number of bytes waiting in the terminal TX FIFO.
 */
uint32_t tx_fifo_level(void);

/**
 * @brief Free space in the terminal TX FIFO.
 */
uint32_t tx_fifo_free(void);

/**
 * @brief Start toolbar buffer transmission.
 * @param len  Number of valid characters in toolbar buffer
 */
void tx_tb_start(uint32_t len);

/**
 * @brief Put character in binary transmit FIFO.
 * @param c  Character to transmit
 */
void bin_tx_fifo_put(const char c);

/**
 * @brief Try to put character in binary transmit FIFO without blocking.
 * @param c  Character to transmit
 * @return   true if the character was queued, false if the FIFO is full
 */
bool bin_tx_fifo_try_put(const char c);

/**
 * @brief Service binary transmit FIFO.
 */
void bin_tx_fifo_service(void);

/**
 * @brief Check if binary TX FIFO not empty.
 * @return  true if data pending
 */
bool bin_tx_not_empty(void);

/**
 * @brief Try to get character from binary TX FIFO.
 * @param c  Output character
 * @return   true if character available
 */
bool bin_tx_fifo_try_get(char* c);

extern char tx_tb_buf[1024];
extern bool tx_tb_buf_ready;