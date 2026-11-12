/**
 * @file hwcan_pio.h
 * @brief PIO-based CAN 2.0B driver using can2040 for Bus Pirate
 */

#ifndef PIRATE_HWCAN_PIO_H
#define PIRATE_HWCAN_PIO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HWCAN_FIFO_DEPTH 64
#define HWCAN_DEFAULT_BITRATE 500000

typedef struct {
    uint32_t id;
    uint8_t dlc;
    uint8_t data[8];
    bool is_extended;
    bool is_rtr;
    uint32_t timestamp_us;
} hwcan_frame_t;

/**
 * @brief Initialize PIO CAN hardware driver
 * @param bitrate Bus speed in bits per second (e.g. 1000000, 500000, 250000, 125000)
 * @param bio_rx  Buffered I/O pin index for RX (0..7)
 * @param bio_tx  Buffered I/O pin index for TX (0..7)
 * @return true on success, false on resource or hardware failure
 */
bool hwcan_init(uint32_t bitrate, uint8_t bio_rx, uint8_t bio_tx);

/**
 * @brief Stop CAN driver and release PIO and interrupt resources
 */
void hwcan_cleanup(void);

/**
 * @brief Transmit a CAN frame
 * @param id          Standard (11-bit) or Extended (29-bit) CAN ID
 * @param dlc         Data length code (0..8)
 * @param data        Data buffer (up to 8 bytes)
 * @param is_extended True for 29-bit extended ID, false for 11-bit standard ID
 * @param is_rtr      True for Remote Transmission Request frame
 * @return true on success, false if TX queue is full or hardware busy
 */
bool hwcan_tx_frame(uint32_t id, uint8_t dlc, const uint8_t *data, bool is_extended, bool is_rtr);

/**
 * @brief Pop the oldest received CAN frame from the RX FIFO
 * @param out_frame Pointer to store popped frame
 * @return true if a frame was popped, false if FIFO is empty
 */
bool hwcan_pop_frame(hwcan_frame_t *out_frame);

/**
 * @brief Get number of queued frames waiting in RX FIFO
 */
uint32_t hwcan_rx_available(void);

/**
 * @brief Configure acceptance filter
 * @param id      Identifier value
 * @param mask    Bitmask (1 = must match, 0 = don't care)
 * @param enabled True to enable filtering, false to accept all frames
 */
void hwcan_set_filter(uint32_t id, uint32_t mask, bool enabled);

/**
 * @brief Query current filter configuration
 */
void hwcan_get_filter(uint32_t *id, uint32_t *mask, bool *enabled);

/**
 * @brief Get CAN hardware packet and error statistics
 */
void hwcan_get_stats(uint32_t *rx_count, uint32_t *tx_count, uint32_t *error_count);

/**
 * @brief Reset statistics counters
 */
void hwcan_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif // PIRATE_HWCAN_PIO_H
