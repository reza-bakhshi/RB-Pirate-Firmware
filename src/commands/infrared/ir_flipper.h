/**
 * @file ir_flipper.h
 * @brief Flipper Zero .ir signal file read/write support for INFRARED mode.
 * @details Supports the "IR signals file" format used by the Flipper Zero and
 *          the Flipper-IRDB database: parsed entries (protocol/address/
 *          command) and raw entries (frequency/data).
 *
 * Based on the INFRARED feature set of the ESP32-Bit-Pirate project
 * (https://github.com/geo-tp/ESP32-Bit-Pirate), adapted for the Bus Pirate 5.
 */

#ifndef _IR_FLIPPER_H
#define _IR_FLIPPER_H

#include <stdint.h>
#include <stdbool.h>
#include "fatfs/ff.h"
#include "commands/infrared/ir_common.h"

/** One IR signal (parsed or raw). */
typedef struct _ir_signal {
    char name[33];
    bool parsed;          ///< true = protocol/address/command, false = raw data
    int8_t proto;         ///< engine protocol index (parsed), -1 = unknown
    uint32_t dev;         ///< device/address
    uint32_t sub;         ///< sub-device/toggle
    uint32_t cmd;         ///< command/function
    uint32_t freq_khz;    ///< carrier frequency in kHz
    uint16_t pairs;       ///< number of mark/space pairs (raw)
    uint32_t data[IR_CMD_MAX_PAIRS]; ///< raw pairs (mark<<16 | space)
} ir_signal_t;

/**
 * @brief Write the file header if the file is empty.
 * @return true on success
 */
bool ir_flipper_write_header_if_needed(FIL *fil);

/**
 * @brief Append one signal to a .ir file.
 * @return true on success
 */
bool ir_flipper_write_signal(FIL *fil, const ir_signal_t *signal);

/**
 * @brief Scan a .ir file for signal names.
 * @param fil      open file (position reset to 0)
 * @param offsets  output file offsets of each signal block
 * @param names    output signal names
 * @param max      maximum number of entries
 * @return number of signals found (0 on parse error)
 */
uint16_t ir_flipper_scan(FIL *fil, uint32_t *offsets, char names[][33], uint16_t max);

/**
 * @brief Read one signal block at the given file offset.
 * @return true on success (proto = -1 if the protocol is not supported)
 */
bool ir_flipper_read_signal(FIL *fil, uint32_t offset, ir_signal_t *signal);

#endif
