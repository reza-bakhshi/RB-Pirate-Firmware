/* SubGHz interrupt-driven edge capture (device only).
 *
 * A GPIO IRQ timestamps every GDO0 edge into a ring buffer, so pulse
 * widths stay accurate even for short pulses and during terminal output.
 * Consumers drain (level, duration) pulses; durations are derived from
 * consecutive timestamps and survive time_us_32 wraparound.
 *
 * Copyright (c) 2026 Bus Pirate contributors. SPDX-License-Identifier: MIT
 */
#ifndef BP_SUBGHZ_CAP_H
#define BP_SUBGHZ_CAP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start edge capture on a BIO pin (configures it as input). */
void subghz_cap_start(uint8_t bio_pin);

/* Stop capture and detach the IRQ. */
void subghz_cap_stop(void);

/* Drain one finished pulse. Returns false when the buffer is empty. */
bool subghz_cap_poll(bool *level, uint32_t *duration_us);

/* Overrun counter (edges dropped while full); reading clears it. */
uint32_t subghz_cap_overruns(void);

/* Timestamps currently buffered (for diagnostics). */
uint32_t subghz_cap_pending(void);

#ifdef __cplusplus
}
#endif

#endif
