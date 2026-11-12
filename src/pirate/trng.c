#include "pirate/trng.h"

#if defined(BP_HOST_TEST)
static uint32_t s_prng_state = 0x853c49e6;

void bp_trng_init(void) {
    s_prng_state = 0x853c49e6;
}

uint32_t bp_trng_get_u32(void) {
    s_prng_state ^= s_prng_state << 13;
    s_prng_state ^= s_prng_state >> 17;
    s_prng_state ^= s_prng_state << 5;
    return s_prng_state;
}

uint64_t bp_trng_get_u64(void) {
    return ((uint64_t)bp_trng_get_u32() << 32) | bp_trng_get_u32();
}
#else
#include "pico/rand.h"

void bp_trng_init(void) {
    (void)get_rand_32();
}

uint32_t bp_trng_get_u32(void) {
    return get_rand_32();
}

uint64_t bp_trng_get_u64(void) {
    return get_rand_64();
}
#endif
