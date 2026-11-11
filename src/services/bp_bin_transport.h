/**
 * @file bp_bin_transport.h
 * @brief Binary transport service interface for Bus Pirate.
 *
 * Implements non-blocking, framed transport queues bridging USB CDC1 with
 * binary mode engines (SUMP, BPIO, legacy DirtyProto, FALAIO, IRToy, CH32V003).
 * Eliminates direct TinyUSB calls from protocol drivers, enforces backpressure
 * drop accounting when host readers stall, and provides bounded timeouts.
 */

#ifndef BP_BIN_TRANSPORT_H
#define BP_BIN_TRANSPORT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "kernel/bp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BP_BIN_TX_RING_SIZE 4096
#define BP_BIN_RX_RING_SIZE 2048

/**
 * @brief Binary transport health and telemetry metrics.
 */
typedef struct bp_bin_transport_stats {
    uint32_t rx_level;          /**< Current bytes queued in RX ring */
    uint32_t rx_free;           /**< Free space in RX ring */
    uint32_t rx_dropped;        /**< Inbound bytes dropped due to RX queue full */
    uint32_t tx_level;          /**< Current bytes queued in TX ring */
    uint32_t tx_free;           /**< Free space in TX ring */
    uint32_t tx_dropped;        /**< Outbound bytes dropped due to TX queue full */
    uint32_t rx_packets;        /**< Total RX packets/chunks processed */
    uint32_t tx_packets;        /**< Total TX packets/chunks queued */
    bool     cdc1_connected;    /**< True if host CDC1 is currently connected */
} bp_bin_transport_stats_t;

/**
 * @brief Initialize binary transport queues and telemetry.
 *
 * @return BP_STATUS_OK on success.
 */
bp_status_t bp_bin_transport_init(void);

/**
 * @brief Service the binary transport subsystem on Core 0.
 *
 * Must be called periodically from Core 0 cooperative loop (`bp_service(0)`).
 * Handles timeouts, state synchronization, and health monitoring.
 */
void bp_bin_transport_service(void);

/**
 * @brief Service the binary USB CDC1 hardware endpoints.
 *
 * Must be called from the core that owns tud_task() (Core 0).
 * Transfers bytes between CDC1 hardware FIFOs and the SPSC transport rings.
 */
void bp_bin_transport_usb_service(void);

/**
 * @brief Check if CDC1 (binary endpoint) is connected to a host application.
 *
 * Control-core API; protocol workers return results through bp_protocol_call().
 *
 * @return true if connected, false otherwise.
 */
bool bp_bin_transport_is_connected(void);

/* ========================================================================== */
/* RX APIs (Consumed by binary protocol engines)                              */
/* ========================================================================== */

/**
 * @brief Non-blocking read of bytes from the binary RX queue.
 *
 * @param buf Destination buffer.
 * @param max_len Maximum bytes to read into buf.
 * @param out_read Output pointer storing the number of bytes read.
 * @return BP_STATUS_OK if bytes were read, BP_STATUS_QUEUE_EMPTY if empty,
 *         or BP_STATUS_INVALID_PARAM on invalid arguments.
 */
bp_status_t bp_bin_transport_read(uint8_t* buf, uint32_t max_len, uint32_t* out_read);

/**
 * @brief Non-blocking read of a single byte from the binary RX queue.
 *
 * @param c Output byte pointer.
 * @return BP_STATUS_OK if byte was read, BP_STATUS_QUEUE_EMPTY if empty,
 *         or BP_STATUS_INVALID_PARAM if c is NULL.
 */
bp_status_t bp_bin_transport_getc(uint8_t* c);

/**
 * @brief Bounded wait read of a single byte from the binary RX queue.
 *
 * Continually services the watchdog supervisor while waiting up to timeout_ms.
 *
 * @param c Output byte pointer.
 * @param timeout_ms Maximum time to wait in milliseconds.
 * @return BP_STATUS_OK on success, BP_STATUS_TIMEOUT on expiration,
 *         or BP_STATUS_INVALID_PARAM if c is NULL.
 */
bp_status_t bp_bin_transport_getc_timeout(uint8_t* c, uint32_t timeout_ms);

/**
 * @brief Get the number of bytes currently available in the binary RX queue.
 *
 * @return Number of unread bytes.
 */
uint32_t bp_bin_transport_rx_available(void);

/**
 * @brief Discard all unread bytes in the binary RX queue.
 */
void bp_bin_transport_rx_flush(void);

/* ========================================================================== */
/* TX APIs (Produced by binary protocol engines)                              */
/* ========================================================================== */

/**
 * @brief Non-blocking write of a buffer into the binary TX queue.
 *
 * Never blocks the caller. If the TX ring does not have enough space, as many
 * bytes as possible are written and the remainder are dropped and accounted for.
 *
 * @param buf Data buffer to write.
 * @param len Buffer length in bytes.
 * @param out_written Optional pointer to store actual bytes queued (can be NULL).
 * @return BP_STATUS_OK if all bytes were enqueued, BP_STATUS_QUEUE_FULL if any
 *         bytes were dropped, or BP_STATUS_INVALID_PARAM on NULL buffer.
 */
bp_status_t bp_bin_transport_write(const uint8_t* buf, uint32_t len, uint32_t* out_written);

/**
 * @brief Non-blocking write of a single byte into the binary TX queue.
 *
 * @param c Byte to enqueue.
 * @return BP_STATUS_OK on success, BP_STATUS_QUEUE_FULL if dropped.
 */
bp_status_t bp_bin_transport_putc(uint8_t c);

/**
 * @brief Bounded wait write of a single byte into the binary TX queue.
 *
 * Waits up to timeout_ms for free queue space while feeding the watchdog.
 *
 * @param c Byte to enqueue.
 * @param timeout_ms Maximum wait time in milliseconds.
 * @return BP_STATUS_OK on success, BP_STATUS_TIMEOUT on expiration.
 */
bp_status_t bp_bin_transport_putc_timeout(uint8_t c, uint32_t timeout_ms);

/**
 * @brief Request immediate flush of queued TX bytes to CDC1.
 */
void bp_bin_transport_tx_flush(void);

/**
 * @brief Get the free space remaining in the binary TX queue.
 *
 * @return Number of free bytes.
 */
uint32_t bp_bin_transport_tx_free(void);

/**
 * @brief Retrieve binary transport telemetry and drop metrics.
 *
 * @param stats Destination pointer.
 */
void bp_bin_transport_get_stats(bp_bin_transport_stats_t* stats);

#ifdef BP_HOST_TEST
/**
 * @brief Host test helper: Inject inbound bytes into binary RX ring.
 */
void bp_bin_transport_mock_inject_rx(const uint8_t* data, uint32_t len);

/**
 * @brief Host test helper: Drain outbound bytes from binary TX ring.
 */
uint32_t bp_bin_transport_mock_drain_tx(uint8_t* out_buf, uint32_t max_len);

/**
 * @brief Host test helper: Set simulated CDC1 connection state.
 */
void bp_bin_transport_mock_set_connected(bool connected);
#endif

#ifdef __cplusplus
}
#endif

#endif /* BP_BIN_TRANSPORT_H */
