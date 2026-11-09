/**
 * @file usb_tx.c
 * @brief USB and UART transmit buffer management
 * 
 * This file implements the transmit side of the user interface buffering system.
 * It handles both USB CDC and UART debug output, with support for VT100 toolbar
 * updates and DMA transfers.
 * 
 * The TX system uses lock-free SPSC ring buffers for regular output and toolbar output,
 * with a state machine to manage toolbar refresh without corrupting output.
 * 
 * Thread safety: Uses lock-free SPSC queues
 * - Producer: Core0 (printf output, command output)
 * - Consumer: Core1 (USB/UART transmission)
 * 
 * @author Bus Pirate Project
 * @date 2024-2026
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "spsc_queue.h"
#include "hardware/dma.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "usb_rx.h"
#include "usb_tx.h"
#include "tusb.h"
#include "pirate/bio.h"
#include "system_config.h"
#include "debug_uart.h"
#include "pirate/watchdog.h"
#include "services/bp_bin_transport.h"

/** @defgroup tx_buffers TX Buffer Definitions
 * @brief Transmit ring buffers and status bar management
 * @{
 */

spsc_queue_t tx_fifo;              /**< Main transmit FIFO for regular output */
spsc_queue_t bin_tx_fifo;          /**< Binary mode transmit FIFO */

/** TX FIFO size in bits (2^10 = 1024 bytes) */
#define TX_FIFO_LENGTH_IN_BITS 12
/** TX FIFO size in bytes */
#define TX_FIFO_LENGTH_IN_BYTES (0x0001 << TX_FIFO_LENGTH_IN_BITS)

uint8_t tx_buf[TX_FIFO_LENGTH_IN_BYTES] __attribute__((aligned(4)));     /**< TX buffer storage */
uint8_t bin_tx_buf[TX_FIFO_LENGTH_IN_BYTES] __attribute__((aligned(4))); /**< Binary TX buffer storage */

/** Maximum size of toolbar render buffer */
#define MAXIMUM_TOOLBAR_BUFFER_BYTES 1024
char tx_tb_buf[MAXIMUM_TOOLBAR_BUFFER_BYTES]; /**< Toolbar render buffer */
uint16_t tx_tb_buf_cnt = 0;                      /**< Number of valid characters in toolbar buffer */
uint16_t tx_tb_buf_index = 0;                    /**< Current position in toolbar buffer */
bool tx_tb_buf_ready = false;                    /**< Toolbar buffer ready to transmit */

bp_terminal_health_t bp_terminal_health;
static bool tx_overflow_pending;
static uint32_t tx_overflow_bytes;

/** @} */ // end of tx_buffers

/**
 * @brief Initialize transmit FIFOs
 * 
 * Sets up the lock-free SPSC ring buffers for regular and binary mode transmission.
 * Buffer sizes must be powers of 2 for efficient modulo operation.
 * 
 * @note Can be called from either core
 */
void tx_fifo_init(void) {
    // OK to call from either core
    spsc_queue_init(&tx_fifo, tx_buf, TX_FIFO_LENGTH_IN_BYTES);
    spsc_queue_init(&bin_tx_fifo, bin_tx_buf, TX_FIFO_LENGTH_IN_BYTES);
    bp_terminal_health = (bp_terminal_health_t){ 0 };
    tx_overflow_pending = false;
    tx_overflow_bytes = 0;
}

/**
 * @brief Mark status bar buffer as ready for transmission
 * 
 * @param len Number of valid characters in toolbar buffer
 * 
 * @pre Must be called from Core1
 * @pre len <= MAXIMUM_TOOLBAR_BUFFER_BYTES
 * 
 * @warning This function asserts if not called from Core1
 */
void tx_tb_start(uint32_t len) {

    BP_ASSERT_CORE1();
    BP_ASSERT(len <= MAXIMUM_TOOLBAR_BUFFER_BYTES);
    tx_tb_buf_cnt = len;
    tx_tb_buf_ready = true;
}

/**
 * @brief Service the transmit FIFO and status bar updates
 * 
 * This function implements a state machine that:
 * - Drains regular output from tx_fifo to USB/UART
 * - Manages toolbar buffer refresh timing
 * - Ensures toolbar output doesn't corrupt regular output
 * 
 * State machine:
 * - IDLE: Send regular output or prepare toolbar buffer
 * - TOOLBAR_DELAY: Wait for TX FIFO to drain before toolbar
 * - TOOLBAR_TX: Transmit toolbar buffer
 * 
 * @pre Must be called from Core1 only
 * @note Status bar updates are delayed until TX FIFO is empty to prevent VT100 corruption
 */
void tx_fifo_service(void) {
    BP_ASSERT_CORE1(); // tx fifo is drained from core1 only

/** @name TX State Machine States */
/**@{*/
#define IDLE 0             /**< Idle state - send regular output */
#define TOOLBAR_DELAY 1    /**< Waiting for TX FIFO to drain */
#define TOOLBAR_TX 2       /**< Transmitting toolbar buffer */
/**@}*/
    static uint8_t tx_state = IDLE;
    static uint8_t delay_count = 0;

    uint32_t bytes_available;
    char data[64];
    uint8_t i = 0;
    bool usb_connected = system_config.terminal_usb_enable && tud_cdc_n_connected(0);
    uint32_t write_avail = usb_connected ? tud_cdc_n_write_available(0) : 0;
    bool usb_writable = usb_connected && (write_avail > 0);

    if (!usb_connected && !system_config.terminal_uart_enable) {
        // No terminal at all: discard instead of leaving the ring full forever.
        spsc_queue_flush(&tx_fifo);
        tx_tb_buf_ready = false;
        tx_state = IDLE;
        return;
    }
    if (!usb_writable && !system_config.terminal_uart_enable) {
        // USB is backpressured and there is no UART fallback.
        return;
    }

    switch (tx_state) {
        case IDLE:
            bytes_available = spsc_queue_level(&tx_fifo);
            if (bytes_available) {
                uint32_t max_pull = (write_avail > 64 || !usb_connected) ? 64 : write_avail;
                if (max_pull == 0) max_pull = 64;
                i = 0;
                while (spsc_queue_try_remove(&tx_fifo, (uint8_t*)&data[i])) {
                    i++;
                    if (i >= max_pull) {
                        break;
                    }
                }
                break; // break out of switch and continue below
            }

            // toolbar buffer update is ready
            if (tx_tb_buf_ready) {
                tx_state = TOOLBAR_DELAY;
                tx_tb_buf_index = 0;
                return; // return for next state
            }

            return; // nothing, just return

            break;
        case TOOLBAR_DELAY:
            // Multi-cycle drain: require 3 consecutive empty checks before
            // committing to toolbar TX. This ensures any in-flight bytes from
            // Core0 have time to arrive in the FIFO before we start sending
            // toolbar VT100 sequences that would corrupt interleaved output.
            bytes_available = spsc_queue_level(&tx_fifo);
            if (bytes_available) {
                delay_count = 0;
                tx_state = IDLE; // bytes arrived, go drain them first
            } else {
                delay_count++;
                if (delay_count >= 3) {
                    delay_count = 0;
                    tx_state = TOOLBAR_TX;
                }
            }
            return; // return for next cycle

            break;
        case TOOLBAR_TX: {
            // Send up to 64 bytes directly from tx_tb_buf (no intermediate copy)
            uint16_t remaining = tx_tb_buf_cnt - tx_tb_buf_index;
            uint8_t chunk = (remaining > 64) ? 64 : (uint8_t)remaining;

            // write to terminal usb
            if (usb_writable) {
                tud_cdc_n_write(0, &tx_tb_buf[tx_tb_buf_index], chunk);
                tud_cdc_n_write_flush(0);
                if (system_config.terminal_uart_enable) {
                    tud_task();
                }
            }

            // write to terminal debug uart
            if (system_config.terminal_uart_enable) {
                for (uint8_t j = 0; j < chunk; j++) {
                    uart_putc(debug_uart[system_config.terminal_uart_number].uart,
                              tx_tb_buf[tx_tb_buf_index + j]);
                }
            }

            tx_tb_buf_index += chunk;
            if (tx_tb_buf_index >= tx_tb_buf_cnt) {
                tx_tb_buf_ready = false;
                tx_state = IDLE;
            }
            return; // already sent — skip the common write path below
        }
        default:
            tx_state = IDLE;
            break;
    }

    // if(i==0) return; //safety check

    // write to terminal usb
    if (usb_writable) {
        tud_cdc_n_write(0, data, i);
        tud_cdc_n_write_flush(0);
        if (system_config.terminal_uart_enable) {
            tud_task(); // makes it nicer if we service when the UART is enabled
        }
    }

    // write to terminal debug uart
    if (system_config.terminal_uart_enable) {
        for (uint8_t j = 0; j < i; j++) {
            uart_putc(debug_uart[system_config.terminal_uart_number].uart, data[j]);
        }
    }

    return;
}

static bool tx_fifo_raw_add(uint8_t value) {
    if (!spsc_queue_try_add(&tx_fifo, value)) {
        return false;
    }
    uint32_t level = spsc_queue_level(&tx_fifo);
    if (level > bp_terminal_health.high_water) {
        bp_terminal_health.high_water = level;
    }
    return true;
}

static void tx_fifo_emit_overflow_marker(void) {
    if (!tx_overflow_pending) {
        return;
    }
    char marker[64];
    int len = snprintf(marker, sizeof(marker), "\r\n[output truncated: %lu bytes]\r\n",
                       (unsigned long)tx_overflow_bytes);
    if (len <= 0 || (uint32_t)len >= sizeof(marker)) {
        return;
    }
    // Need room for the marker plus the sentinel byte that keeps the SPSC ring
    // full/empty states unambiguous.
    if (spsc_queue_free(&tx_fifo) <= (uint32_t)len) {
        return;
    }
    for (int i = 0; i < len; i++) {
        if (!spsc_queue_try_add(&tx_fifo, (uint8_t)marker[i])) {
            return;
        }
    }
    uint32_t level = spsc_queue_level(&tx_fifo);
    if (level > bp_terminal_health.high_water) {
        bp_terminal_health.high_water = level;
    }
    tx_overflow_pending = false;
    tx_overflow_bytes = 0;
}

bp_tx_status_t tx_fifo_put(char* c) {
    BP_ASSERT_CORE0(); // tx fifo should only be added to from core 0 (deadlock risk)
    tx_fifo_emit_overflow_marker();
    if (tx_fifo_raw_add((uint8_t)*c)) {
        return BP_TX_OK;
    }

    // Bounded backpressure: when a terminal is connected, give Core 1 time to
    // drain USB packets (64 bytes/ms) instead of immediately dropping characters.
    if (tud_cdc_n_connected(0) || system_config.terminal_uart_enable) {
        for (uint32_t retry = 0; retry < 50; retry++) {
            busy_wait_us(100);
            bp_watchdog_heartbeat(0);
            bp_watchdog_service();
            if (tx_fifo_raw_add((uint8_t)*c)) {
                return BP_TX_OK;
            }
        }
    }

    if (!tx_overflow_pending) {
        tx_overflow_pending = true;
        tx_overflow_bytes = 0;
        bp_terminal_health.overflow_events++;
    }
    bp_terminal_health.dropped_bytes++;
    tx_overflow_bytes++;
    return BP_TX_DROPPED;
}

bp_tx_status_t tx_fifo_try_put(char* c) {
    return tx_fifo_put(c);
}

bp_tx_status_t tx_fifo_write(const char* buf, uint32_t len) {
    BP_ASSERT_CORE0();
    bp_tx_status_t status = BP_TX_OK;
    for (uint32_t i = 0; i < len; i++) {
        if (tx_fifo_put((char*)&buf[i]) != BP_TX_OK) {
            status = BP_TX_DROPPED;
        }
    }
    return status;
}

bool tx_fifo_wait_drain_timeout(uint32_t timeout_ms) {
    BP_ASSERT_CORE0();
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!spsc_queue_is_empty(&tx_fifo)) {
        if (time_reached(deadline)) {
            bp_terminal_health.drain_timeouts++;
            return false;
        }
        tight_loop_contents();
    }
    return true;
}

void tx_fifo_wait_drain(void) {
    // Compatibility wrapper: callers that do not need a specific timeout still
    // get a bounded wait instead of an infinite spin.
    (void)tx_fifo_wait_drain_timeout(1000u);
}

uint32_t tx_fifo_level(void) {
    return spsc_queue_level(&tx_fifo);
}

uint32_t tx_fifo_free(void) {
    return spsc_queue_free(&tx_fifo);
}

void bin_tx_fifo_put(const char c) {
    (void)bp_bin_transport_putc((uint8_t)c);
}

bool bin_tx_fifo_try_put(const char c) {
    return (bp_bin_transport_putc((uint8_t)c) == BP_STATUS_OK);
}

bool bin_tx_fifo_try_get(char* c) {
    return (bp_bin_transport_getc((uint8_t*)c) == BP_STATUS_OK);
}

void bin_tx_fifo_service(void) {
    bp_bin_transport_usb_service();
}

bool bin_tx_not_empty(void) {
    return (bp_bin_transport_tx_free() < BP_BIN_TX_RING_SIZE);
}
