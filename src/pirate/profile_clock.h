/* Copyright (c) 2026 RB-Pirate contributors. SPDX-License-Identifier: MIT */
#ifndef BP_PROFILE_CLOCK_H
#define BP_PROFILE_CLOCK_H
#include <stdint.h>
#include <stdbool.h>

/* Sample at least once per 32-bit timer period (about 71 minutes). */
typedef struct {
    uint32_t last_us;
    uint64_t elapsed_us;
} bp_profile_clock_t;

static inline uint32_t bp_profile_clock_update(bp_profile_clock_t *clock, uint32_t now_us) {
    uint32_t delta = now_us - clock->last_us;
    clock->last_us = now_us;
    clock->elapsed_us += delta;
    return delta;
}

static inline bool bp_profile_clock_expired(const bp_profile_clock_t *clock, uint32_t seconds) {
    return seconds != 0 && clock->elapsed_us >= (uint64_t)seconds * 1000000ULL;
}
#endif
