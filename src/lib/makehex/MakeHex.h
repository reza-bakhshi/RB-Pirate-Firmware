/**
 * @file makehex.h
 * @brief C interface to the MakeHex IRP protocol encoder.
 * @details Generates raw mark/space sequences from MakeHex protocol
 *          definitions (irp_definitions.h). This is the same encoder used by
 *          the IRDB tools and by the ESP32-Bit-Pirate project, giving the
 *          INFRARED mode its wide protocol support for the `send` command.
 *
 *          IRP interpreter: Copyright 2005 John S. Fine
 *          (https://github.com/probonopd/MakeHex/), license in MakeHex.cpp.
 */

#ifndef _MAKEHEX_H
#define _MAKEHEX_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Generate the raw mark/space sequence for a command.
 * @param protocol  MakeHex protocol name (e.g. "nec2", "rc6-M-L")
 * @param dev       device value (D)
 * @param sub       sub-device value (S), -1 = not specified (protocol default)
 * @param func      function value (F)
 * @param pairs     output buffer of mark/space pairs (mark << 16 | space, us)
 * @param count     output number of pairs
 * @param max_pairs capacity of the pairs buffer
 * @param freq_hz   output carrier frequency in Hz (from the definition)
 * @return true on success
 */
bool makehex_generate(const char *protocol, int32_t dev, int32_t sub, int32_t func,
                      uint32_t *pairs, uint16_t *count, uint16_t max_pairs, uint32_t *freq_hz);

#ifdef __cplusplus
}
#endif

#endif
