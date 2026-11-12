/* Copyright (c) 2026 RB-Pirate contributors. SPDX-License-Identifier: MIT */
#include <assert.h>
#include "pirate/profile_clock.h"
int main(void) {
    bp_profile_clock_t clock = { .last_us = UINT32_MAX - 10 };
    assert(bp_profile_clock_update(&clock, 9) == 20);
    assert(clock.elapsed_us == 20);
    clock = (bp_profile_clock_t){0};
    /* Twelve hours at 20 Hz: exercise ten timer wraps and integration precision. */
    uint64_t charge = 0;
    for (uint32_t sample = 1; sample <= 864000; ++sample) {
        uint32_t delta = bp_profile_clock_update(&clock, (uint32_t)((uint64_t)sample * 50000));
        charge += (uint64_t)150000 * delta;
        assert(bp_profile_clock_expired(&clock, 43200) == (sample == 864000));
    }
    assert(clock.elapsed_us == 43200000000ULL);
    assert(charge / 3600000000000ULL == 1800);
    assert(!bp_profile_clock_expired(&clock, 0));
    assert(!bp_profile_clock_expired(&clock, UINT32_MAX));
    return 0;
}
