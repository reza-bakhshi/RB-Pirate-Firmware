#ifndef _PIRATE_TRNG_H
#define _PIRATE_TRNG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the hardware true random number generator / entropy source.
 */
void bp_trng_init(void);

/**
 * @brief Get a 32-bit hardware true random number.
 * @return 32-bit unsigned random value.
 */
uint32_t bp_trng_get_u32(void);

/**
 * @brief Get a 64-bit hardware true random number.
 * @return 64-bit unsigned random value.
 */
uint64_t bp_trng_get_u64(void);

#ifdef __cplusplus
}
#endif

#endif // _PIRATE_TRNG_H
