/**
 * @file ir_protocols.h
 * @brief Table driven infrared protocol encoder/decoder engine.
 * @details Encodes and decodes common IR protocols (NEC, SONY, RC5, RC6,
 *          SAMSUNG, LG, JVC, PANASONIC, SHARP, DISH) as raw mark/space
 *          microsecond pairs. Transmission uses the irio_pio raw TX engine
 *          (irio_pio_tx_frame_write), capture uses irio_pio_rx_frame_buf.
 *
 *          Based on the INFRARED feature set of the ESP32-Bit-Pirate project
 *          (https://github.com/geo-tp/ESP32-Bit-Pirate), adapted for the
 *          Bus Pirate 5 hardware by Ian Lesnet.
 *
 * Protocol send argument conventions (send <dev> <sub> <cmd>):
 *  - NEC:       dev=address (8 bit, or 16 bit extended), sub unused, cmd=command (8 bit)
 *  - SONY:      dev=address (5/8/13 bit selects SIRC 12/15/20), sub unused, cmd=command (7 bit)
 *  - RC5:       dev=address (5 bit), sub=toggle bit, cmd=command (6 bit, bit 6 selects RC5X)
 *  - RC6:       dev=address (8 bit), sub unused, cmd=command (8 bit)
 *  - SAMSUNG:   dev=address (8 bit), sub unused, cmd=command (8 bit)
 *  - LG:        dev=address (8 bit), sub unused, cmd=command (8 bit)
 *  - JVC:       dev=address (8 bit), sub unused, cmd=command (8 bit)
 *  - PANASONIC: dev=device (8 bit), sub=sub-device (4 bit, address = (sub<<8)|dev),
 *               cmd=function (8 bit); vendor ID fixed to 0x4004 (Bit-Pirate compatible)
 *  - SHARP:     dev=address (5 bit), sub unused, cmd=command (8 bit)
 *  - DISH:      dev=device (4 bit), sub unused, cmd=function (10 bit)
 *  - DENON/MITSUBISHI/SHARPK: Kaseikyo variants like PANASONIC (vendor ID
 *               fixed per protocol: 0x2A4C / 0xC4D3 / 0x555A)
 */

#ifndef _IR_PROTOCOLS_H
#define _IR_PROTOCOLS_H

#include <stdint.h>
#include <stdbool.h>

/** Maximum mark/space pairs in one encoded frame. */
#define IR_MAX_PAIRS 96u

/** Number of hand-written protocols at the top of the table (with decoders). */
#define IR_PROTOCOL_HAND_COUNT 13u

/** Decode sentinel: NEC-style repeat frame (holding a button). */
#define IR_REPEAT_FRAME 0xFFFFFFFEul

/** Send sentinel: sub-device not specified (protocol default is used). */
#define IR_SUB_UNSPECIFIED 0xFFFFFFFFul

typedef struct _ir_protocol_def {
    const char *display;  ///< display name, e.g. "NEC"
    const char *arg_name; ///< lowercase command line name, e.g. "nec"
    uint32_t carrier_hz;  ///< TX carrier frequency in Hz (0 = from MakeHex definition)
    uint8_t bits;         ///< nominal frame length in bits (display only)
    uint8_t default_repeats; ///< default number of frames per send
    bool (*encode)(uint32_t dev, uint32_t sub, uint32_t cmd, uint32_t *pairs, uint16_t *count); ///< NULL = MakeHex entry
    bool (*decode)(const uint32_t *pairs, uint16_t count, uint32_t *dev, uint32_t *sub, uint32_t *cmd); ///< NULL = not auto-detected
    const char *makehex_name; ///< MakeHex/IRP definition name (see lib/makehex), NULL if unused
} ir_protocol_def_t;

/** @brief Number of protocols in the table. */
uint8_t ir_protocols_count(void);

/** @brief Get protocol definition by index (0 to ir_protocols_count()-1). */
const ir_protocol_def_t *ir_protocols_get(uint8_t idx);

/** @brief Find protocol index by lowercase arg name, -1 if not found. */
int8_t ir_protocols_find(const char *arg_name);

/**
 * @brief Encode and transmit a command frame.
 * @details The raw TX PIO must already be initialized
 *          (see infrared_cleanup_temp()/irio_pio_tx_init()).
 * @param idx     protocol index
 * @param dev     device/address field
 * @param sub     sub-device/toggle field (protocol specific)
 * @param cmd     command/function field
 * @param repeats number of frames to send (0 = protocol default)
 * @return true on success
 */
bool ir_protocols_send(uint8_t idx, uint32_t dev, uint32_t sub, uint32_t cmd, uint8_t repeats);

/**
 * @brief Attempt to decode a captured mark/space frame.
 * @details Tries every protocol in priority order until one matches.
 * @param pairs  captured pairs (mark<<16 | space, microseconds)
 * @param count  number of pairs
 * @param idx    output protocol index, -1 if not decoded
 * @param dev    output device/address
 * @param sub    output sub-device/toggle
 * @param cmd    output command (IR_REPEAT_FRAME sentinel for repeat frames)
 * @return true if a protocol matched
 */
bool ir_protocols_decode(const uint32_t *pairs, uint16_t count, int8_t *idx, uint32_t *dev, uint32_t *sub, uint32_t *cmd);

#endif
