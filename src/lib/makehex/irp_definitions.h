/**
 * @file irp_definitions.h
 * @brief MakeHex IRP protocol definitions (timing/Form data per protocol).
 * @details Ported verbatim from the ESP32-Bit-Pirate project
 *          (https://github.com/geo-tp/ESP32-Bit-Pirate,
 *          src/Data/InfraredProtocolDefinitions.h), which adapted them from
 *          https://github.com/probonopd/MakeHex/ and the IRDB protocol
 *          database. Each entry is a MakeHex protocol definition: a name and
 *          an IRP-style definition string (frequency, bit timings, Form).
 *
 * MakeHex IRP encoder: Copyright 2005 John S. Fine, license in MakeHex.cpp.
 */

#ifndef _IRP_DEFINITIONS_H
#define _IRP_DEFINITIONS_H

#include <stdint.h>

typedef struct _makehex_protocol {
    const char *name; // protocol name as used by MakeHex/IRDB
    const char *def;  // MakeHex definition string
} makehex_protocol_t;

#ifdef __cplusplus
extern "C" {
#endif

extern const makehex_protocol_t makehex_protocols[];
extern const uint8_t makehex_protocol_count;

#ifdef __cplusplus
}
#endif

#endif
