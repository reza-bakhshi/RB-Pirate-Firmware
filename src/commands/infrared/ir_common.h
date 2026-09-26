/**
 * @file ir_common.h
 * @brief Shared helpers for INFRARED mode commands.
 * @details Wraps the teardown/re-init dance needed to temporarily reconfigure
 *          the IR PIO programs while the INFRARED mode is active, and provides
 *          small interactive input helpers.
 *
 * Based on the INFRARED feature set of the ESP32-Bit-Pirate project
 * (https://github.com/geo-tp/ESP32-Bit-Pirate), adapted for the Bus Pirate 5.
 */

#ifndef _IR_COMMON_H
#define _IR_COMMON_H

#include <stdint.h>
#include <stdbool.h>

/** Maximum pairs stored per captured frame (matches irio_pio RX capacity). */
#define IR_CMD_MAX_PAIRS 128u

/** Maximum pairs stored per replayed frame. */
#define IR_CMD_MAX_REPLAY_PAIRS 48u

/**
 * @brief Temporarily initialize the raw IR TX PIO on the configured TX pin.
 * @details Tears down the current mode PIO programs on first use (nested
 *          calls are safe). Call ir_cmd_tx_end() when finished.
 * @param mod_freq_hz TX carrier frequency in Hz
 * @return true on success
 */
bool ir_cmd_tx_begin(uint32_t mod_freq_hz);

/** @brief Tear down the temporary raw TX PIO and restore the mode programs. */
void ir_cmd_tx_end(void);

/**
 * @brief Temporarily initialize the raw IR RX PIO on the given BIO pin.
 * @details Tears down the current mode PIO programs on first use (nested
 *          calls are safe). Call ir_cmd_rx_end() when finished.
 * @param rx_bio BIO pin number (0-7) of the receiver/sensor
 * @return true on success
 */
bool ir_cmd_rx_begin(uint8_t rx_bio);

/** @brief Tear down the temporary raw RX PIO and restore the mode programs. */
void ir_cmd_rx_end(void);

/** @brief true while a temporary TX or RX PIO is active. */
bool ir_cmd_hw_active(void);

/**
 * @brief Poll the raw RX engine for a complete captured frame.
 * @param pairs    output pairs buffer (mark<<16 | space, microseconds)
 * @param count    output number of pairs
 * @param mod_freq output measured carrier frequency in Hz (0 if unknown)
 * @return true when a complete frame is available
 */
bool ir_cmd_capture(uint32_t *pairs, uint16_t *count, float *mod_freq);

/** @brief true if a console key was pressed, value in *c. */
bool ir_cmd_key_available(char *c);

/** @brief Block until a console key is pressed and return it. */
char ir_cmd_wait_key(void);

#endif
